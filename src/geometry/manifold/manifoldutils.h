#pragma once

#include <CGAL/Surface_mesh/Surface_mesh.h>

#include <memory>

#include "core/enums.h"
#include "geometry/Geometry.h"
#include "geometry/manifold/ManifoldGeometry.h"
#include "manifold/manifold.h"

namespace ManifoldUtils {

const char *statusToString(manifold::Manifold::Error status);

std::shared_ptr<ManifoldGeometry> createManifoldFromPolySet(const PolySet& ps);
std::shared_ptr<const ManifoldGeometry> createManifoldFromGeometry(
  const std::shared_ptr<const Geometry>& geom);

template <class SurfaceMesh>
std::shared_ptr<ManifoldGeometry> createManifoldFromSurfaceMesh(const SurfaceMesh& mesh);
template <typename SurfaceMesh>
std::shared_ptr<SurfaceMesh> createSurfaceMeshFromManifold(const manifold::Manifold& mani);

std::shared_ptr<ManifoldGeometry> applyOperator3DManifold(const Geometry::Geometries& children,
                                                          OpenSCADOperator op);

/*!
 * Clip a geometry by a plane, keeping the half-space on the side the plane
 * normal points toward. The plane is given as (a, b, c, d) with normal (a, b, c)
 * and offset d; the kept half-space is { p : a*p.x + b*p.y + c*p.z >= d }.
 * Returns an (empty) ManifoldGeometry, never nullptr.
 */
std::shared_ptr<const Geometry> trimByPlane(const std::shared_ptr<const Geometry>& geom,
                                            const Vector4d& plane);

/*!
 * 3D constant-radius surface offset of a solid. delta > 0 grows the solid
 * outward (rounded convex offset), delta < 0 shrinks it inward (rounded
 * concave offset). Implemented as a Minkowski sum (delta > 0) or Minkowski
 * difference (delta < 0) with a sphere of radius |delta|; `segments` is the
 * sphere's circular segment count. Returns an (empty) ManifoldGeometry, never
 * nullptr.
 */
std::shared_ptr<const Geometry> offset3D(const std::shared_ptr<const Geometry>& geom, double delta,
                                         int segments);

/*!
 * Hollow out a solid, leaving a constant-thickness inward wall of the given
 * thickness (> 0). The interior (the solid eroded inward by `thickness`) is
 * removed, so the result is the shell wall only. `segments` is the erosion
 * sphere's circular segment count. Returns an (empty) ManifoldGeometry, never
 * nullptr.
 */
std::shared_ptr<const Geometry> shell3D(const std::shared_ptr<const Geometry>& geom, double thickness,
                                        int segments);

Polygon2d polygonsToPolygon2d(const manifold::Polygons& polygons);

#ifdef ENABLE_CGAL
// FIXME: This shouldn't return const, but it does due to internal implementation details.
std::shared_ptr<const Geometry> applyMinkowski(const Geometry::Geometries& children);
#endif

std::unique_ptr<PolySet> createTriangulatedPolySetFromPolygon2d(const Polygon2d& polygon2d);
};  // namespace ManifoldUtils
