#include "geometry/linear_extrude.h"

#include <algorithm>
#include <boost/logic/tribool.hpp>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <memory>
#include <utility>
#include <vector>

#include "core/LinearExtrudeNode.h"
#include "geometry/Geometry.h"
#include "geometry/GeometryUtils.h"
#include "geometry/PolySet.h"
#include "geometry/PolySetBuilder.h"
#include "geometry/PolySetUtils.h"
#include "geometry/linalg.h"
#include "glview/RenderSettings.h"
#include "utils/degree_trig.h"

namespace LinearExtrudeInternals {

/*
  Compare Euclidean length of vectors
  Return:
   -1 : if v1  < v2
    0 : if v1 ~= v2 (approximation to compensate for floating point precision)
    1 : if v1  > v2
*/
int sgn_vdiff(const Vector2d& v1, const Vector2d& v2)
{
  constexpr double ratio_threshold = 1e5;  // 5 orders-of-magnitude difference
  double l1 = v1.norm();
  double l2 = v2.norm();
  // Compare the average and difference, to be independent of geometry scale.
  // If the difference is within ratio_threshold of the avg, treat as equal.
  double scale = (l1 + l2);
  double diff = 2 * std::fabs(l1 - l2) * ratio_threshold;
  return diff > scale ? (l1 < l2 ? -1 : 1) : 0;
}

/**
 *
 * @param vertices The first polyref.length() vertices must be in the same order as polyref and represent
 * the bottom face. Similarly, the last polyref.length() vertices must be in the same order as polyref
 * and represent the top face.
 * @param indices These indexed face sets must not include the top nor bottom faces.
 */
std::unique_ptr<PolySet> assemblePolySetForManifold(const Polygon2d& polyref,
                                                    std::vector<Vector3d>&& vertices,
                                                    PolygonIndices&& indices, int convexity,
                                                    boost::tribool isConvex, int index_offset)
{
  auto final_polyset = std::make_unique<PolySet>(3, isConvex);
  final_polyset->setTriangular(true);
  final_polyset->setConvexity(convexity);
  final_polyset->vertices = std::move(vertices);
  final_polyset->indices = std::move(indices);

  // Get valid top and bottom edges (as Indexed Face Sets aka Indices).
  // If top is scaled it doesn't matter: we're only using the edges and not vertices.
  auto ps_topbottom = polyref.tessellate();
  // Manifold tessellating doesn't add vertices (at least in this case? ever? not sure), so the indices
  // remain valid.

  // Copy indices for the top face, with appropriate offset.
  for (const auto& p_original : ps_topbottom->indices) {
    final_polyset->indices.emplace_back();
    auto& p_offset = final_polyset->indices.back();

    for (int index : p_original) {
      p_offset.push_back(index + index_offset);
    }
  }

  // Flip vertex ordering for bottom polygon and move it.
  for (auto& p : ps_topbottom->indices) {
    std::reverse(p.begin(), p.end());
  }
  std::copy(std::make_move_iterator(ps_topbottom->indices.begin()),
            std::make_move_iterator(ps_topbottom->indices.end()),
            std::back_inserter(final_polyset->indices));

  // LOG(PolySetUtils::polySetToPolyhedronSource(*final_polyset));

  return final_polyset;
}

std::unique_ptr<PolySet> assemblePolySetForCGAL(const Polygon2d& polyref,
                                                std::vector<Vector3d>& vertices, PolygonIndices& indices,
                                                int convexity, boost::tribool isConvex, double scale_x,
                                                double scale_y, const Vector3d& h1, const Vector3d& h2,
                                                double twist)
{
  PolySetBuilder builder(0, 0, 3, isConvex);
  builder.setConvexity(convexity);

  for (const auto& poly : indices) {
    builder.beginPolygon(poly.size());
    for (const auto idx : poly) {
      builder.addVertex(vertices[idx]);
    }
  }

  auto translatePolySet = [](PolySet& ps, const Vector3d& translation) {
    for (auto& v : ps.vertices) {
      v += translation;
    }
  };

  // Create bottom face.
  auto ps_bottom = polyref.tessellate();  // bottom
  // Flip vertex ordering for bottom polygon
  for (auto& p : ps_bottom->indices) {
    std::reverse(p.begin(), p.end());
  }
  translatePolySet(*ps_bottom, h1);
  builder.appendPolySet(*ps_bottom);

  // Create top face.
  // If either scale components are 0, then top will be zero-area, so skip it.
  if (scale_x != 0 && scale_y != 0) {
    Polygon2d top_poly(polyref);
    Eigen::Affine2d trans(Eigen::Scaling(scale_x, scale_y) * Eigen::Affine2d(rotate_degrees(-twist)));
    top_poly.transform(trans);
    auto ps_top = top_poly.tessellate();
    translatePolySet(*ps_top, h2);
    builder.appendPolySet(*ps_top);
  }

  return builder.build();
}

/**
 * Attempt to triangulate quads in an ideal way.
 * Each quad is composed of two adjacent outline vertices: (prev_vtx_bot, vtx_bot)
 * and their corresponding transformed points one step up: (prev_vtx_top, vtx_top).
 * Quads are triangulated across the shorter of the two diagonals, which works well in most cases.
 * However, when diagonals are equal length, decision may flip depending on other factors.
 * @param indices All faces for this slice are added to this.
 * @param slice_idx Which slice is currently having faces added **1-indexed** not 0-indexed.
 * @param slice_stride Total count of vertices in all polygons being extruded.
 * @param poly The polygon being extruded.
 * @param rotation_slice_bottom The degrees(?) of rotation for the polygon at the bottom of this slice.
 * @param scale_slice_top The vector of scaling applied to the polygon at the top of this slice.
 */
void add_slice_indices(PolygonIndices& indices, int slice_idx, int slice_stride, const Polygon2d& poly,
                       double rotation_slice_bottom, double rotation_slice_top,
                       const Vector2d& scale_slice_bottom, const Vector2d& scale_slice_top)
{
  int bottom_offset = (slice_idx - 1) * slice_stride;
  int top_offset = slice_idx * slice_stride;

  Eigen::Affine2d trans_bot(Eigen::Scaling(scale_slice_bottom) *
                            Eigen::Affine2d(rotate_degrees(-rotation_slice_bottom)));
  Eigen::Affine2d trans_top(Eigen::Scaling(scale_slice_top) *
                            Eigen::Affine2d(rotate_degrees(-rotation_slice_top)));

  bool any_zero = scale_slice_top[0] == 0 || scale_slice_top[1] == 0;
  // setting back_twist true helps keep diagonals same as previous builds.
  bool back_twist = rotation_slice_top <= rotation_slice_bottom;

  int curr_outline = 0;
  for (const auto& o : poly.outlines()) {
    // prev_vtx_bot: previous vertex, on the bottom of this slice
    // prev_vtx_top: previous vertex, on the top of this slice
    Vector2d prev_vtx_bot = trans_bot * o.vertices[0];
    Vector2d prev_vtx_top = trans_top * o.vertices[0];

    // For equal length diagonals, flip selected choice depending on direction of twist and
    // whether the outline is negative (eg circle hole inside a larger circle).
    // This was tested against circles with a single point touching the origin,
    // and extruded with twist.  Diagonal choice determined by whichever option
    // matched the direction of diagonal for neighboring edges (which did not exhibit "equal" diagonals).
    bool flip = ((!o.positive) xor (back_twist));

    for (size_t i = 1; i <= o.vertices.size(); ++i) {
      auto o_vertices_idx = i % o.vertices.size();
      // vtx_bot: current vertex, on the bottom of this slice
      // vtx_top: current vertex, on the top of this slice
      Vector2d vtx_bot = trans_bot * o.vertices[o_vertices_idx];
      Vector2d vtx_top = trans_top * o.vertices[o_vertices_idx];
      int idx = curr_outline + o_vertices_idx;
      int prev_idx = curr_outline + i - 1;

      // -1 if diagonal from current-slice-current-vertex to previous-slice-previous-vertex is smaller
      int diff_sign = sgn_vdiff(prev_vtx_bot - vtx_top, vtx_bot - prev_vtx_top);
      bool splitfirst = diff_sign == -1 || (diff_sign == 0 && !flip);

      // Split along shortest diagonal,
      // unless at top for a 0-scaled axis (which can create 0 thickness "ears")
      if (splitfirst xor any_zero) {
        indices.push_back({
          bottom_offset + idx,
          top_offset + idx,
          bottom_offset + prev_idx,
        });
        indices.push_back({
          top_offset + prev_idx,
          bottom_offset + prev_idx,
          top_offset + idx,
        });
      } else {
        indices.push_back({
          bottom_offset + idx,
          top_offset + prev_idx,
          bottom_offset + prev_idx,
        });
        indices.push_back({
          bottom_offset + idx,
          top_offset + idx,
          top_offset + prev_idx,
        });
      }
      prev_vtx_bot = vtx_bot;
      prev_vtx_top = vtx_top;
    }
    curr_outline += o.vertices.size();
  }
}

/**
 *  max(2Ddistance(vertex->scaled_vertex)^2 of all vertices).
 */
inline double calc_max_delta_sqr(const std::vector<Outline2d>& outlines, const Vector2d& scale)
{
  double max_delta_sqr = 0;
  for (const auto& o : outlines) {
    for (const auto& v : o.vertices) {
      // cwiseProduct means multiplying each element by the same element in the other matrix
      // "coefficient wise product"
      max_delta_sqr = fmax(max_delta_sqr, (v - v.cwiseProduct(scale)).squaredNorm());
    }
  }
  return max_delta_sqr;
}

size_t calc_num_slices(const LinearExtrudeNode& node, const Polygon2d& poly)
{
  size_t num_slices;
  if (node.has_slices) {
    return node.slices;
  }

  if (node.has_twist) {
    double max_r1_sqr = 0;  // r1 is before scaling
    for (const auto& o : poly.outlines())
      for (const auto& v : o.vertices) max_r1_sqr = fmax(max_r1_sqr, v.squaredNorm());
    if (node.scale_x == 1.0 && node.scale_y == 1.0) {
      // Calculate Helical curve length for Twist with no Scaling
      num_slices = static_cast<unsigned int>(
        node.discretizer.getHelixSlices(max_r1_sqr, node.height[2], node.twist)
          .value_or(std::max(static_cast<int>(std::ceil(node.twist / 120.0)), 1)));
    } else if (node.scale_x != node.scale_y) {
      // Non-uniform scaling with twist.

      Vector2d scale(node.scale_x, node.scale_y);
      double max_delta_sqr = calc_max_delta_sqr(poly.outlines(), scale);

      // Why would we not find the furthest *scaled* max_r1_sqr and use getHelixSlices on that?
      // Because it scales non-uniformly and so you need a formula for the
      // length of a non-uniformly scaled helix.
      // And you would need to check every vertex, because the vertex that's furthest away
      // before you start twisting may not be the furthest throughout the twist.
      // Consider vertices at (3.99,2) and (4,-2), and a scale=(1,2).
      // If you rotate 90 degrees CCW, the first vertex will be further away,
      // but the second vertex starts and ends out further.

      size_t slicesNonUniScale = static_cast<unsigned int>(
        node.discretizer.getDiagonalSlices(max_delta_sqr, node.height[2]).value_or(1));
      size_t slicesTwist = static_cast<unsigned int>(
        node.discretizer.getHelixSlices(max_r1_sqr, node.height[2], node.twist)
          .value_or(std::max(static_cast<int>(std::ceil(node.twist / 120.0)), 1)));
      num_slices = std::max(slicesNonUniScale, slicesTwist);
    } else {
      // uniform scaling with twist, use conical helix calculation
      num_slices = static_cast<unsigned int>(
        node.discretizer.getConicalHelixSlices(max_r1_sqr, node.height[2], node.twist, node.scale_x)
          .value_or(std::max(static_cast<int>(std::ceil(node.twist / 120.0)), 1)));
    }
  } else if (node.scale_x != node.scale_y) {
    // Non uniform scaling, w/o twist

    // Naively it doesn't seem like this case needs to be treated differently than uniform scaling,
    // because the line between the same 2d vertex is exactly straight.
    // But the faces will have error.
    // To see, animate this with fps=4 and steps=10:
    // module s(s) linear_extrude(h=30, scale=[1/20,20], slices=s) scale([1,0.1]) rotate(45) square(10,
    // center=true); steps=10; linear_extrude(h=1) projection(cut=true)
    // {
    //   low_slices=($t*steps)+1;
    //   translate([0,0,low_slices%2==1? -15 : -15+30/low_slices/2]) union() {
    //     difference() { s(low_slices); s(40);}
    //     difference() { s(40); s(low_slices);}
    //   }
    // }

    double max_delta_sqr = calc_max_delta_sqr(poly.outlines(), Vector2d(node.scale_x, node.scale_y));
    num_slices = node.discretizer.getDiagonalSlices(max_delta_sqr, node.height[2]).value_or(1);
  } else {
    // uniform scaling w/o twist needs only one slice
    num_slices = 1;
  }
  return num_slices;
}

/**
 * Break out extrusion logic to be used in unit tests.
 * @param slice_stride return value
 * @param vertices return value
 * @param indices return value
 */
void prepareVerticesAndIndices(const Polygon2d& polyref, Vector3d h1, Vector3d h2, int num_slices,
                               double scale_x, double scale_y, double twist,
                               std::vector<Vector3d>& vertices, PolygonIndices& indices,
                               int& slice_stride)
{
  for (const auto& o : polyref.outlines()) {
    slice_stride += o.vertices.size();
  }
  vertices.reserve(slice_stride * (num_slices + 1));
  indices.reserve(slice_stride * (num_slices + 1) * 2);  // sides + endcaps

  // Calculate all vertices
  Vector2d full_scale(1 - scale_x, 1 - scale_y);
  double full_rot = -twist;
  auto full_height = (h2 - h1);
  for (unsigned int slice_idx = 0; slice_idx <= num_slices; slice_idx++) {
    Eigen::Affine2d trans(Eigen::Scaling(Vector2d(1, 1) - full_scale * slice_idx / num_slices) *
                          Eigen::Affine2d(rotate_degrees(full_rot * slice_idx / num_slices)));

    for (const auto& o : polyref.outlines()) {
      for (const auto& v : o.vertices) {
        auto tmp = trans * v;
        vertices.emplace_back(Vector3d(tmp[0], tmp[1], 0.0) + h1 + full_height * slice_idx / num_slices);
      }
    }
  }

  // Create indices for sides
  for (unsigned int slice_idx = 1; slice_idx <= num_slices; slice_idx++) {
    double rot_bot = twist * (slice_idx - 1) / num_slices;
    double rot_top = twist * slice_idx / num_slices;
    Vector2d scale_bot(1 - (1 - scale_x) * (slice_idx - 1) / num_slices,
                       1 - (1 - scale_y) * (slice_idx - 1) / num_slices);
    Vector2d scale_top(1 - (1 - scale_x) * slice_idx / num_slices,
                       1 - (1 - scale_y) * slice_idx / num_slices);
    add_slice_indices(indices, slice_idx, slice_stride, polyref, rot_bot, rot_top, scale_bot, scale_top);
  }
}

/**
 * Inset a single outline by perpendicular distance @p d (mm) using miter joins,
 * preserving vertex count and order so that consecutive rings can be lofted
 * directly (edge-to-edge). Positive @p d shrinks the enclosed solid: outer
 * (CCW) contours move inward, hole (CW) contours move outward, so the solid
 * always gets smaller.
 *
 * This is a lightweight per-vertex inset intended for the small chamfer/draft
 * insets of the experimental DFM feature. It is NOT robust against large insets
 * on concave or thin profiles (no self-intersection cleanup); those cases would
 * want a Clipper offset + general loft instead. See extrudePolygonChamfered().
 */
Outline2d insetOutline(const Outline2d& o, double d)
{
  Outline2d result;
  result.positive = o.positive;
  const size_t n = o.vertices.size();
  if (d == 0.0 || n < 3) {
    result.vertices = o.vertices;
    return result;
  }

  // Signed area gives orientation so the inset always moves toward the solid,
  // independent of whether this is an outer contour or a hole.
  double area2 = 0.0;
  for (size_t i = 0; i < n; ++i) {
    const Vector2d& a = o.vertices[i];
    const Vector2d& b = o.vertices[(i + 1) % n];
    area2 += a[0] * b[1] - b[0] * a[1];
  }
  const double sign = area2 >= 0 ? 1.0 : -1.0;

  result.vertices.reserve(n);
  for (size_t i = 0; i < n; ++i) {
    const Vector2d& prev = o.vertices[(i + n - 1) % n];
    const Vector2d& cur = o.vertices[i];
    const Vector2d& next = o.vertices[(i + 1) % n];

    Vector2d din = cur - prev;
    Vector2d dout = next - cur;
    const double lin = din.norm();
    const double lout = dout.norm();
    if (lin < 1e-9 || lout < 1e-9) {
      result.vertices.push_back(cur);
      continue;
    }
    din /= lin;
    dout /= lout;
    // Left-hand normals (point to the interior of a CCW contour).
    const Vector2d nin(-din[1], din[0]);
    const Vector2d nout(-dout[1], dout[0]);
    Vector2d miter = nin + nout;
    const double mlen = miter.norm();
    if (mlen < 1e-6) {
      // ~180deg spike: fall back to a plain edge-normal offset.
      result.vertices.push_back(cur + sign * d * nin);
      continue;
    }
    miter /= mlen;
    double cosHalf = miter.dot(nin);
    // Clamp the miter length so very sharp corners don't shoot off to infinity.
    if (cosHalf < 0.1) cosHalf = 0.1;
    result.vertices.push_back(cur + sign * (d / cosHalf) * miter);
  }
  return result;
}

/**
 * Build a linear extrusion whose top and/or bottom rim is chamfered (a 45deg
 * self-supporting bevel) and/or whose sidewalls are drafted (tapered inward
 * toward the top). Composes with the existing scale/twist transforms.
 *
 * The solid is generated as a stack of rings, each with the same vertex count
 * and ordering as @p polyref: every ring applies the interpolated scale/twist
 * affine transform and then an inward inset that depends on height. Chamfer
 * rings sit at the rim (max inset) and one chamfer-depth in (zero inset); draft
 * adds an inset that grows linearly with height across the whole extrusion.
 * Because all rings share topology, side walls are simple edge-to-edge quads and
 * the end caps reuse the profile tessellation.
 */
std::unique_ptr<PolySet> extrudePolygonChamfered(const LinearExtrudeNode& node,
                                                 const Polygon2d& polyref, const Vector3d& h1,
                                                 const Vector3d& h2, size_t num_slices,
                                                 boost::tribool isConvex)
{
  const Vector3d axis = h2 - h1;
  const double length = axis.norm();

  const bool cham_bot = node.chamfer > 0 && node.chamfer_bottom;
  const bool cham_top = node.chamfer > 0 && node.chamfer_top;
  const double draft_tan = node.draft != 0.0 ? tan_degrees(node.draft) : 0.0;

  // Fraction of the total length consumed by each chamfer bevel.
  const double cb = cham_bot ? node.chamfer / length : 0.0;
  const double ct = cham_top ? node.chamfer / length : 0.0;
  assert(cb + ct < 1.0 && "chamfer(s) must fit within extrusion height");

  int slice_stride = 0;
  for (const auto& o : polyref.outlines()) slice_stride += static_cast<int>(o.vertices.size());

  // Ring height-fractions in [0,1]: rim rings plus evenly-spaced core rings.
  std::vector<double> fracs;
  if (cb > 0) fracs.push_back(0.0);
  for (size_t k = 0; k <= num_slices; ++k) {
    fracs.push_back(cb + (1.0 - cb - ct) * static_cast<double>(k) / static_cast<double>(num_slices));
  }
  if (ct > 0) fracs.push_back(1.0);
  const int num_rings = static_cast<int>(fracs.size());

  // Total inward inset at absolute distance `dist` from the bottom face.
  auto insetAt = [&](double dist) {
    double d = draft_tan > 0 ? draft_tan * dist : 0.0;
    if (cham_bot) d = std::max(d, node.chamfer - dist);
    if (cham_top) d = std::max(d, node.chamfer - (length - dist));
    return std::max(d, 0.0);
  };

  auto final_polyset = std::make_unique<PolySet>(3, isConvex);
  final_polyset->setTriangular(true);
  final_polyset->setConvexity(node.convexity);
  auto& vertices = final_polyset->vertices;
  auto& indices = final_polyset->indices;
  vertices.reserve(static_cast<size_t>(slice_stride) * num_rings);

  const Vector2d full_scale(1 - node.scale_x, 1 - node.scale_y);
  const double full_rot = -node.twist;

  // Ring vertices.
  for (int r = 0; r < num_rings; ++r) {
    const double f = fracs[r];
    const double d = insetAt(f * length);
    Eigen::Affine2d trans(Eigen::Scaling(Vector2d(1, 1) - full_scale * f) *
                          Eigen::Affine2d(rotate_degrees(full_rot * f)));
    const Vector3d ringpos = h1 + axis * f;
    for (const auto& o : polyref.outlines()) {
      // Apply the scale/twist transform, then inset the transformed ring.
      Outline2d transformed;
      transformed.positive = o.positive;
      transformed.vertices.reserve(o.vertices.size());
      for (const auto& v : o.vertices) transformed.vertices.push_back(trans * v);
      const Outline2d inset = insetOutline(transformed, d);
      for (const auto& v : inset.vertices) {
        vertices.emplace_back(Vector3d(v[0], v[1], 0.0) + ringpos);
      }
    }
  }

  // Side walls: connect consecutive rings edge-to-edge (winding matches the rest
  // of the extrude code / add_slice_indices, so normals face outward).
  for (int r = 1; r < num_rings; ++r) {
    const int bot = (r - 1) * slice_stride;
    const int top = r * slice_stride;
    int curr_outline = 0;
    for (const auto& o : polyref.outlines()) {
      const int cnt = static_cast<int>(o.vertices.size());
      for (int i = 0; i < cnt; ++i) {
        const int a = curr_outline + i;
        const int b = curr_outline + (i + 1) % cnt;
        indices.push_back({bot + b, top + b, bot + a});
        indices.push_back({top + a, bot + a, top + b});
      }
      curr_outline += cnt;
    }
  }

  // End caps: reuse the profile tessellation topology (index-preserving inset
  // keeps the same connectivity) for the bottom (reversed) and top rings.
  auto cap = polyref.tessellate();
  const int top_offset = (num_rings - 1) * slice_stride;
  for (const auto& p : cap->indices) {
    indices.emplace_back(p.rbegin(), p.rend());  // bottom face, reversed winding
    IndexedFace tp;
    tp.reserve(p.size());
    for (int idx : p) tp.push_back(idx + top_offset);
    indices.push_back(std::move(tp));  // top face
  }

  return final_polyset;
}

}  // namespace LinearExtrudeInternals

using namespace LinearExtrudeInternals;

/*!
   Input to extrude should be sanitized. This means non-intersecting, correct winding order
   etc., the input coming from a library like Clipper.
 */
std::unique_ptr<Geometry> extrudePolygon(const LinearExtrudeNode& node, const Polygon2d& poly)
{
  assert(poly.isSanitized());
  if (node.height[2] <= 0) return PolySet::createEmpty();

  bool non_linear = node.twist != 0 || node.scale_x != node.scale_y;
  boost::tribool isConvex{poly.is_convex()};
  // Twist makes convex polygons into unknown polyhedrons
  if (isConvex && non_linear) isConvex = unknown;

  // num_slices is the number of volumetric segments, minimum 1.
  // The number of rings of vertices will be num_slices+1.
  auto num_slices = calc_num_slices(node, poly);

  // Calculate outline segments if appropriate.
  Polygon2d seg_poly;

  // We skip segmentation if the user passed in segments=0
  if (!(node.has_segments && node.segments == 0)) {
    // A straight evenly-scaled linear extrusion doesn't lose detail, so we
    // only segment if the user requests it or we're doing something funky.
    if (node.segments > 0 || non_linear) {
      for (const auto& o : poly.outlines()) {
        seg_poly.addOutline(node.discretizer.splitOutline(o, node.twist, node.scale_x, node.scale_y,
                                                          num_slices, node.segments));
      }
    }
  }

  const Polygon2d& polyref = seg_poly.isEmpty() ? poly : seg_poly;

  Vector3d h1 = Vector3d::Zero();
  Vector3d h2 = node.height;

  if (node.center) {
    h1 -= node.height / 2.0;
    h2 -= node.height / 2.0;
  }

  // Experimental DFM chamfer/draft: build via the ring-stack chamfer builder.
  // (Chamfering/drafting can make an otherwise-convex profile non-convex.)
  const bool uses_chamfer = node.chamfer > 0 && (node.chamfer_top || node.chamfer_bottom);
  if (uses_chamfer || node.draft != 0.0) {
    return extrudePolygonChamfered(node, polyref, h1, h2, num_slices, unknown);
  }

  int slice_stride = 0;
  std::vector<Vector3d> vertices;
  PolygonIndices indices;
  prepareVerticesAndIndices(polyref, h1, h2, num_slices, node.scale_x, node.scale_y, node.twist,
                            vertices, indices, slice_stride);

  // For Manifold, we can tesselate the endcaps using existing vertices to build a manifold mesh.
  // Without Manifold, however, we don't have such a tessellator available, so we'll have to build
  // the polyset from vertices using PolySetBuilder

#ifdef ENABLE_MANIFOLD
  if (RenderSettings::inst()->backend3D == RenderBackend3D::ManifoldBackend) {
    return assemblePolySetForManifold(polyref, std::move(vertices), std::move(indices), node.convexity,
                                      isConvex, slice_stride * num_slices);
  } else
#endif
    return assemblePolySetForCGAL(polyref, vertices, indices, node.convexity, isConvex, node.scale_x,
                                  node.scale_y, h1, h2, node.twist);
}
