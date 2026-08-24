// edges.scad — chamfers, round-overs and fillets that you EXTRUDE, rather than
// try to apply to a finished solid.
//
//   Usage:
//     use <edges.scad>
//     boss(h = 5, fillet = 1.0, chamfer = 0.6) square([8, 8], center = true);
//
//   No feature flag needed: this is plain OpenSCAD. Its companion is the
//   sharp_edges() DFM check (libraries/dfm.scad), which needs
//   --enable=geometry-metrics. Detect with that, fix with this.
//
// WHY THIS EXISTS
//
// OpenSCAD has no edge-fillet operator. The usual workarounds are all bad in a
// different way:
//
//   minkowski() with a sphere    rounds convex AND concave alike, cannot do a
//                                45-degree chamfer, cannot select edges, and is
//                                slow enough to dominate a whole render.
//   hull() of two profiles       gives a straight taper only, so a "round-over"
//                                comes out as a cone.
//   offset(delta, chamfer=true)  is 2D. It chamfers the VERTICAL edges of an
//                                extrusion; it can do nothing about the top and
//                                bottom rims, which is where the sharp edge you
//                                actually feel usually is.
//
// The observation this library is built on: nearly every enclosure feature is
// already an extrusion of a 2D profile, and 2D offset() is exact. So a treated
// edge is just the same profile extruded through a STACK of offsets — inset
// along an arc for a round-over, along a straight line for a chamfer, and
// outset along an arc for a concave base fillet. The result is exact for the
// profile's straight runs and faceted only where the profile is already
// faceted, it costs a handful of hulls instead of a minkowski, and it composes
// with everything else because it is still just a solid.
//
// A note on FDM: the three treatments are not interchangeable.
//
//   base fillet   flares OUTWARD going down, so it can never be an overhang.
//                 This is the one to use where a boss meets its floor: that
//                 concave corner is a stress riser and it is where a boss
//                 snaps off.
//   top chamfer   at 45 degrees or shallower, prints support-free in either
//                 orientation. Default treatment for the top of a boss.
//   top round     is fine printed upward (an upward-facing curve just
//                 stair-steps), but printed DOWNWARD its lower half exceeds 45
//                 degrees. Use it for edges a hand touches; keep the chamfer
//                 for edges that only have to not be sharp.
//
// THREE TRAPS, all of which produce geometry that renders, exports, and reports
// itself manifold while being wrong. Each is documented in full where it bites:
//
//   1. hull() convexifies, so a RING lofted through edge_extrude() comes back
//      with its hole filled in. Use edge_ring(). (See edge_ring.)
//   2. offset() re-tessellates arcs, so two solids built different ways can
//      share a surface only to within the arc sagitta, and unioning them makes
//      slivers. Build one ladder, not two solids. (See edge_loft.)
//   3. A treatment larger than half the smallest dimension it is applied to
//      makes offset() return an empty profile, and hull() then quietly falls
//      back to the untreated slice — the feature just comes out shorter. Cap
//      every treatment against the feature it treats. (See edge_extrude.)

// ---- Resolution -------------------------------------------------------------

// Fragments OpenSCAD would use for a full circle of radius r, by the same rule
// the core primitives use ($fn, else the finer of $fa and $fs, min 5).
function edge_fragments(r) =
  ($fn > 0) ? max(3, $fn)
            : max(5, ceil(min(360 / $fa, r * 2 * PI / $fs)));

// Steps across a quarter-turn treatment of size r, chosen by CHORD ERROR
// rather than by $fn.
//
// This matters more than it looks. $fn is set for the part -- 96, say, so the
// case's Ø65 coil tower is round -- and taking a quarter of it would put 24
// facets on a 1 mm fillet. Every one of those is a hull() of the whole profile,
// so the first version of this file turned back.stl from 3.2 MB into 14.8 MB
// for curvature nobody can see. A fillet's facet count should follow the
// FILLET's radius, not the part's.
//
// $edge_tol is the sagitta (max deviation from the true arc) in mm; the step
// angle that achieves it is 2*sqrt(2*tol/r) radians. $fn still applies as an
// upper bound, so a deliberately coarse $fn stays coarse.
//
// It is a $special so a caller can raise it for a draft render or drop it for a
// final export, and it is defaulted INSIDE the function rather than assigned at
// file scope because `use <>` imports modules and functions but not variables --
// a file-scope $edge_tol would read as undef in every caller.
function edge_tol() = is_undef($edge_tol) ? 0.02 : $edge_tol;
function edge_steps(r, steps = undef) =
  is_undef(steps)
    ? max(2, min(ceil(edge_fragments(abs(r)) / 4),
                 ceil(90 / max(1, 2 * sqrt(2 * edge_tol() / max(abs(r), 1e-6))
                                  * 180 / PI))))
    : max(1, steps);

// ---- Treatment profiles -----------------------------------------------------
// Each returns a list of [height_above_the_start_of_the_treatment, inset].
// Inset is how far the 2D profile is pulled IN at that height.

// Treatment SIZES ARE SIGNED. Positive pulls the profile IN at that rim, which
// is what softens a solid. Negative pushes it OUT, which is what you want when
// the thing being extruded is a CUT: growing a cut at its mouth is how a hole
// gets a lead-in chamfer. The z extent is always abs(size).

// Straight 45-degree chamfer.
function edge_chamfer_profile(size) = [[0, 0], [abs(size), size]];

// Quarter-circle round-over: tangent to the wall at the bottom of the
// treatment and tangent to the top face at the top.
//   at angle t: height = r * sin(t), inset = r * (1 - cos(t))
function edge_round_profile(r, steps = undef) =
  let (n = edge_steps(abs(r), steps))
  [for (i = [0 : n]) let (t = 90 * i / n) [abs(r) * sin(t), r * (1 - cos(t))]];

// Concave cove blending a wall into the plane it stands on. Measured UP from
// that plane; the outset shrinks to zero at height r.
//   at height z: outset = r - sqrt(r^2 - (r - z)^2)
// which is the arc of radius r tangent to both the floor and the wall.
function edge_cove_profile(r, steps = undef) =
  let (n = edge_steps(r, steps))
  [for (i = [0 : n])
    let (z = r * i / n, k = max(0, r*r - (r - z)*(r - z)))
    [z, r - sqrt(k)]];

// Pick a profile by name. "chamfer" | "round" | "none".
function edge_profile(style, size, steps = undef) =
  (size == 0 || style == "none") ? [[0, 0]] :
  (style == "round")   ? edge_round_profile(size, steps) :
  (style == "chamfer") ? edge_chamfer_profile(size) :
  assert(false, str("edges.scad: unknown style \"", style,
                    "\" (want \"chamfer\", \"round\" or \"none\")"));

// ---- Core: extrude a 2D profile with treated rims ---------------------------

// offset(delta = 0) is NOT a no-op: it re-tessellates the profile's arcs, so a
// shape offset by zero and the same shape drawn directly agree only to the arc
// sagitta. Union those two and the mesher has to reconcile two near-identical
// curves, which it does by emitting slivers. Always route offsets through here.
module _edge_off(d) {
  if (d == 0) children(); else offset(delta = d) children();
}

// One thin slice of the child profile, offset by `d` (negative pulls in).
module _edge_slice(z, d, eps) {
  translate([0, 0, z]) linear_extrude(height = eps) _edge_off(d) children();
}

// Drop rungs that repeat their predecessor. A repeated rung hulls a slice with
// ITSELF, which produces an eps-thick slab lying exactly on top of its
// neighbours' faces. OpenSCAD still calls the result manifold -- and it is, as
// a CSG tree -- but the coincident faces come out of the STL tessellator as
// edges shared by more than two triangles, which is what a mesh checker sees.
// Cheaper to never emit them than to clean up afterwards.
function _edge_dedupe(ladder, eps = 1e-7) =
  [for (i = [0 : len(ladder) - 1])
     if (i == 0 || abs(ladder[i][0] - ladder[i-1][0]) > eps
                || abs(ladder[i][1] - ladder[i-1][1]) > eps) ladder[i]];

// PUBLIC: loft an explicit [[z, offset], ...] ladder over the 2D child profile.
//
// Reach for this when a shape has to be ONE solid rather than several unioned
// ones. Every rung is an offset() of the same base profile, so the rungs agree
// with each other exactly; two separately-built solids do not. offset() moves a
// straight edge exactly but re-tessellates a corner arc, so a rounded rect
// offset inward by d and a rounded rect drawn at (r - d) differ by the arc
// sagitta -- microns. Union two solids that share a surface at that accuracy
// and the mesher has to intersect two nearly-identical curves, which it does by
// emitting slivers: in this project, 352 edges shared by four triangles from a
// bumper band that merely ENDED on the wall it grew out of. One ladder from the
// bed to the front face has no such seam anywhere.
module edge_loft(ladder) { _edge_ladder(ladder) children(); }

// Hull consecutive slices of a [[z, offset], ...] ladder into a solid. The
// slices are `eps` thick so consecutive hulls overlap and the union is
// watertight rather than a stack of touching-but-coplanar frusta.
// `sink_first` extrudes the BOTTOM slice downward instead of upward. Use it when
// the ladder stands on a surface it will be unioned to: the bottom slice's own
// eps-tall wall then finishes inside that surface rather than standing 1 um
// proud of it as a microscopic lip. It is a cove's natural setting.
module _edge_ladder(raw, eps = 0.001, sink_first = false) {
  ladder = _edge_dedupe(raw);
  n = len(ladder);
  // Is the segment ABOVE rung i a plain prism (no change of offset)?
  function prism_above(i) =
    i < n - 1 && abs(ladder[i][1] - ladder[i + 1][1]) < 1e-9;
  // Which way a frustum's slice is extruded off its rung. Down at the very top,
  // so the solid's height is exact; down where a PRISM starts, so the frustum
  // ends on the prism's base plane instead of duplicating its first eps; up
  // everywhere else, so consecutive frusta overlap and the union is watertight.
  function slice_z(i) =
    ladder[i][0] - (((i == n - 1) || prism_above(i) || (i == 0 && sink_first))
                    ? eps : 0);
  if (n < 2) {
    _edge_slice(ladder[0][0], ladder[0][1], eps) children();
  } else {
    for (i = [0 : n - 2])
      // A run with no change of offset is a plain prism, and it has to be
      // EXTRUDED as one, spanning its two rungs EXACTLY. Two rules, both learnt
      // the hard way against the mesh checker:
      //
      //   hull() a straight run and its lateral surface is tessellated
      //   differently from the frustum stacked on it, and the T-junctions along
      //   the join export as edges shared by four triangles.
      //
      //   let a prism and a frustum OVERLAP and their eps slabs coincide
      //   exactly — same profile, same z — which is two copies of the same
      //   solid, and the exporter duplicates the faces. Prisms are exact and
      //   frusta stop on their planes; nothing overlaps a prism.
      if (prism_above(i))
        _edge_slice(ladder[i][0], ladder[i][1],
                    ladder[i + 1][0] - ladder[i][0]) children();
      else
        hull() {
          _edge_slice(slice_z(i),     ladder[i][1],     eps) children();
          _edge_slice(slice_z(i + 1), ladder[i + 1][1], eps) children();
        }
  }
}

// Extrude the 2D child profile to height `h`, treating the bottom and/or top
// rim. `style` applies to both unless overridden per end.
//
//   edge_extrude(h = 6, top = 0.8) square([10, 10], center = true);
//   edge_extrude(h = 6, top = 1.5, style = "round") circle(d = 20);
//
// `bottom` treats the z0 rim the same way, which is what you want for a part
// that prints on that face — but NOT for a boss standing on a floor, where the
// corner wants material adding, not removing: use edge_cove() for that.
// The rungs edge_extrude() would use, as a [[z, offset], ...] ladder. Exposed
// so a caller can splice extra rungs on -- a break-through overshoot, say --
// and loft the result as ONE solid instead of unioning pieces together.
function edge_extrude_ladder(h, bottom = 0, top = 0, style = "chamfer",
                             bottom_style = undef, top_style = undef,
                             steps = undef, z0 = 0) =
  let (bs = is_undef(bottom_style) ? style : bottom_style,
       ts = is_undef(top_style)    ? style : top_style,
       bp = edge_profile(bs, bottom, steps),
       tp = edge_profile(ts, top,    steps))
  assert(abs(bottom) + abs(top) <= h + 1e-9,
         str("edges.scad: bottom (", bottom, ") + top (", top,
             ") treatments exceed the extrusion height (", h, ")."))
  concat(
    [for (i = [len(bp) - 1 : -1 : 0]) [z0 + abs(bottom) - bp[i][0], -bp[i][1]]],
    [for (p = tp)                     [z0 + h - abs(top) + p[0], -p[1]]]);

module edge_extrude(h, bottom = 0, top = 0, style = "chamfer",
                    bottom_style = undef, top_style = undef,
                    steps = undef, z0 = 0) {
  // Both profiles are measured from the INNER end of their treatment — the
  // height at which the profile is still full size — so the bottom rung runs
  // DOWN from z0+bottom and the top rung runs UP from z0+h-top. Getting this
  // backwards is easy and silent: it flares the rim out instead of pulling it
  // in, which still renders, still exports, and is still a sharp edge.
  _edge_ladder(edge_extrude_ladder(h, bottom, top, style, bottom_style,
                                   top_style, steps, z0)) children();
}

// The concave cove alone: a skirt that blends the 2D child profile into the
// plane z = z0. Union it with whatever stands on that plane.
module edge_cove(r, z0 = 0, steps = undef) {
  if (r > 0)
    _edge_ladder([for (p = edge_cove_profile(r, steps)) [z0 + p[0], p[1]]],
                 sink_first = true) children();
}

// A boss: the 2D child profile extruded to `h` from `z0`, with a concave
// fillet where it meets the floor and a chamfer (or round) where it ends.
// This is the shape you want for every internal standoff, rib and tower.
module boss(h, z0 = 0, fillet = 0, chamfer = 0, style = "chamfer",
            steps = undef) {
  union() {
    edge_extrude(h = h, top = chamfer, style = style, steps = steps, z0 = z0)
      children();
    edge_cove(fillet, z0 = z0, steps = steps) children();
  }
}

// Round-profile convenience wrappers, so the common cases read as themselves.
module cyl_boss(d, h, z0 = 0, fillet = 0, chamfer = 0, style = "chamfer",
                steps = undef) {
  boss(h = h, z0 = z0, fillet = fillet, chamfer = chamfer, style = style,
       steps = steps) circle(d = d);
}

// ---- Rings and other non-convex profiles ------------------------------------

// hull() convexifies. A ring put through edge_extrude() therefore comes back
// with its hole HULLED SHUT, and it does so silently, because a filled ring is
// still a perfectly good manifold that renders, exports and passes a
// manifoldness check. (In this project that cost a gasket land: a 0.25 mm
// chamfer on the groove mouth quietly deleted the entire 1.2 mm inner sealing
// face, and the only thing that noticed was a cross-section probe.)
//
// So a ring is lofted as two separate profiles: child 0 the outer boundary,
// child 1 the hole. Each gets its own rim treatments, applied by the same
// signed convention as edge_extrude() -- positive shrinks THAT SOLID, negative
// grows it. Because the hole is subtracted, the sign reads inverted from the
// finished part's point of view, so both worked cases are spelled out:
//
//   a solid ring wanting a lead-in at its bore
//       edge_ring(h, outer_top = 0.6, hole_top = -0.6) { outer(); bore(); }
//       hole_top < 0 grows the bore's cut upward, opening the bore's mouth.
//
//   a ring-shaped CUT (a groove) wanting a chamfered mouth on both walls
//       edge_ring(h, outer_top = -0.25, hole_top = 0.25) { outer(); inner(); }
//       outer_top < 0 flares the groove's outer wall out; hole_top > 0 shrinks
//       the island in the middle, which moves the groove's inner wall in.
module edge_ring(h, z0 = 0, outer_bottom = 0, outer_top = 0,
                 hole_bottom = 0, hole_top = 0, cove = 0,
                 style = "chamfer", steps = undef, over = 0.01) {
  difference() {
    union() {
      edge_extrude(h = h, z0 = z0, bottom = outer_bottom, top = outer_top,
                   style = style, steps = steps) children(0);
      edge_cove(cove, z0 = z0, steps = steps) children(0);
    }
    // The hole is treated over exactly [z0, z0+h] so its chamfers land where the
    // ring's rims actually are, and the break-through overshoot is spliced on
    // as two extra RUNGS rather than unioned on as two extra solids. Stretching
    // the treatment instead would move each chamfer by `over`; unioning instead
    // would put two coincident prisms in the cut, and coincident prisms come
    // out of the exporter as duplicated faces.
    let (base = edge_extrude_ladder(h = h, z0 = z0, bottom = hole_bottom,
                                    top = hole_top, style = style, steps = steps))
      edge_loft(concat([[z0 - over,     base[0][1]]],
                       base,
                       [[z0 + h + over, base[len(base) - 1][1]]]))
        children(1);
  }
}


// ---- Cuts -------------------------------------------------------------------

// A cutting solid whose BOTTOM rim is treated, so that subtracting it leaves a
// fillet (or chamfer) in the floor-to-wall corner of the resulting pocket.
// The cut is grown by `over` above its nominal top so it always breaks through.
//
//   difference() { block(); pocket_cut(h = 10, fillet = 1) square([40,30], center=true); }
module pocket_cut(h, fillet = 0, mouth = 0, style = "round", steps = undef,
                  z0 = 0, over = 0.01) {
  // One ladder, bottom to top: the floor cove (the cut shrinks toward the
  // floor, which leaves a fillet of material in the corner), the straight
  // pocket wall, then an optional flare at the mouth. Built as a single loft
  // rather than a union of three solids so there are no coincident faces where
  // the pieces would otherwise meet.
  cove = (fillet > 0)
    ? [for (p = edge_cove_profile(fillet, steps)) [z0 + p[0], -p[1]]]
    : [[z0, 0]];
  lip = (mouth > 0)
    ? [for (p = edge_profile(style, -mouth, steps)) [z0 + h - mouth + p[0], -p[1]]]
    : [[z0 + h, 0]];
  edge_loft(concat(cove, lip, [[z0 + h + over, lip[len(lip) - 1][1]]]))
    children();
}

// ---- Bands ------------------------------------------------------------------

// A symmetric band that flares out to the child profile at z = seam and tapers
// back in above and below, with the apex ROUNDED rather than left as a ridge.
// This is the shape of a clamshell bumper: two 45-degree tapers meeting at the
// seam. Left sharp, that apex is the widest part of the case and therefore
// exactly the edge a hand rests on.
//
//   `reach`  how far in the taper pulls the profile at the top and bottom of
//            the band, `half` the half-height, `r` the apex radius.
//   `bury`   extend both flanks this much further along the SAME slope, so the
//            band's ends finish strictly inside whatever it is unioned to
//            rather than tangent to that surface.
//
// `bury` is not cosmetic. A band whose ends land exactly on the wall it grows
// out of is a near-tangency, and the two surfaces are only equal in exact
// arithmetic: offset(delta=-reach) of a tessellated rounded rect and a rounded
// rect drawn at the smaller radius differ by the arc sagitta, a couple of
// microns. That is far too small to see and far too large to ignore -- it
// showed up as 352 edges shared by four triangles in the exported mesh, which
// OpenSCAD still called manifold.
// The band's rungs, as a [[z, offset], ...] ladder. Exposed separately so a
// caller can splice it into a longer ladder (see edge_loft) instead of unioning
// a band onto a wall.
function seam_band_ladder(half, reach, r = 0, seam = 0, bury = 0, steps = undef) =
  let (m    = (half > 0) ? reach / half : 0,
       hb   = half + bury,
       rb   = m * hb,
       c    = r * sqrt(m * m + 1),
       phit = atan(m),
       n    = edge_steps(r, steps))
  assert(r * sin(phit) <= half + 1e-9,
         str("edges.scad: seam_band apex radius (", r, ") is too large for a ",
             half, " mm half-band — the arc would run past the flank."))
  (r <= 0)
    ? [[seam - hb, -rb], [seam, 0], [seam + hb, -rb]]
    : concat(
        [[seam - hb, -rb]],
        [for (i = [0 : 2 * n])
           let (phi = -phit + 2 * phit * i / (2 * n))
           [seam + r * sin(phi), -c + r * cos(phi)]],
        [[seam + hb, -rb]]);

module seam_band(half, reach, r = 0, seam = 0, bury = 0, steps = undef) {
  _edge_ladder(seam_band_ladder(half, reach, r, seam, bury, steps)) children();
}
