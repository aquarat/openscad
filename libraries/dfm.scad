// dfm.scad — geometry-aware Design-For-Manufacturing checks for FDM printing,
// built on the core geometry-introspection primitives (bbox / volume / overhang).
//
//   Requires: run OpenSCAD with  --enable=geometry-metrics
//
//   Usage:
//     use <dfm.scad>
//     dfm_check(max_overhang = 45, build_volume = [220, 220, 250]) my_part();
//
// Each check is a transparent wrapper: it measures its children, asserts the
// rule (with a clear message), optionally echoes a report, and then renders the
// children unchanged — so dropping a check into a design never alters geometry.
//
// A note on scope: these checks are as strong as the introspection primitives
// allow today. Overhang, sharp edges and bounding-box/build-volume fit are
// exact-ish and cheap. A true "minimum wall thickness anywhere" test needs a
// signed-distance field over the mesh, which the engine does not yet expose —
// dfm_min_wall() below is a deliberately conservative bounding-box proxy and is
// documented as such, not a substitute for a real thickness analysis.

// ---- Individual checks ------------------------------------------------------

// Assert that no downward-facing surface overhangs past `max_angle` degrees
// from vertical (bed-contact faces are excluded by the overhang() primitive).
module dfm_assert_overhang(max_angle = 45, report = false) {
  a = overhang();
  if (report) echo(dfm_overhang_deg = a, dfm_overhang_limit = max_angle);
  assert(a <= max_angle + 1e-6,
         str("DFM: max overhang ", a, "° exceeds limit ", max_angle,
             "° — add a chamfer or reorient for support-free printing"));
  children();
}

// Assert the part fits inside a printer build volume [x, y, z] (mm).
module dfm_assert_within(build_volume, report = false) {
  bb = bbox();
  sz = bb[1] - bb[0];
  if (report) echo(dfm_size = sz, dfm_build_volume = build_volume);
  assert(sz[0] <= build_volume[0] + 1e-6 &&
         sz[1] <= build_volume[1] + 1e-6 &&
         sz[2] <= build_volume[2] + 1e-6,
         str("DFM: part ", sz, " does not fit build volume ", build_volume));
  children();
}

// Assert a minimum keep-out: no part of the geometry lies within `radius` mm
// (in the XY plane) of `center`. Handy for "no boss within R of the coil".
// Uses the bounding box as a conservative envelope test.
module dfm_assert_clearance(center, radius, report = false) {
  bb = bbox();
  // nearest XY point of the bbox to center
  nx = max(bb[0][0], min(center[0], bb[1][0]));
  ny = max(bb[0][1], min(center[1], bb[1][1]));
  d  = norm([nx - center[0], ny - center[1]]);
  if (report) echo(dfm_nearest_xy_dist = d, dfm_clearance = radius);
  assert(d >= radius - 1e-6,
         str("DFM: geometry comes within ", d, " mm of ", center,
             " (needs ", radius, " mm clearance)"));
  children();
}

// Conservative wall proxy: warns if the SMALLEST bounding-box dimension is below
// `min_wall`. This only catches globally-thin parts; it is NOT a true anywhere-
// thickness test (see file header). Non-fatal by design (warning, not assert).
module dfm_min_wall(min_wall = 1.0, report = false) {
  bb = bbox();
  sz = bb[1] - bb[0];
  thin = min(sz[0], sz[1], sz[2]);
  if (report) echo(dfm_min_bbox_dim = thin, dfm_min_wall = min_wall);
  if (thin < min_wall)
    echo(str("DFM WARNING: smallest overall dimension ", thin,
             " mm < min wall ", min_wall, " mm (bounding-box proxy — verify locally)"));
  children();
}

// ---- Sharp edges ------------------------------------------------------------
// Two different defects, and they want opposite fixes, so they get separate
// checks rather than one budget over both:
//
//   convex   an outside corner. At 90 degrees it is a hard line under the
//            fingers and the first thing to chip. Fix with a chamfer or a
//            round-over (edges.scad: edge_extrude, seam_band).
//   concave  an inside corner. Its stress concentration factor rises without
//            bound as the radius goes to zero, and on an FDM part it is where a
//            boss shears off the floor it stands on. Fix with a cove
//            (edges.scad: edge_cove, boss).
//
// Both are budgets in MILLIMETRES OF EDGE, not counts: one long ring round a
// rim matters more than a dozen short runs, and a count cannot tell them apart.

// Total length of sharp edge of one kind. `kind` is "any", "convex" or
// "concave"; `angle` is how far the surface may turn across an edge before it
// counts, in degrees. 50 is the useful default — just above 45, so a 45-degree
// chamfer does not report itself as the defect it fixes.
// (OpenSCAD has no sum(); `use <>` imports every function, so the helper is
// underscore-prefixed rather than called something as grabbable as `add`.)
function _dfm_sum(v, i = 0) = i >= len(v) ? 0 : v[i] + _dfm_sum(v, i + 1);
function dfm_sharp_length(edges) =
  _dfm_sum([for (e = edges) e[0]]);

module dfm_assert_sharp(max_convex_mm = 0, max_concave_mm = 0, angle = 50,
                        report = false) {
  cx = sharp_edges(angle = angle, kind = "convex");
  cc = sharp_edges(angle = angle, kind = "concave");
  lx = dfm_sharp_length(cx);
  lc = dfm_sharp_length(cc);
  if (report)
    echo(dfm_sharp_convex_mm = lx, dfm_sharp_concave_mm = lc,
         dfm_sharp_angle = angle);
  assert(lx <= max_convex_mm + 1e-6,
         str("DFM: ", lx, " mm of convex edge sharper than ", angle,
             " deg (budget ", max_convex_mm, "). Worst feature: ", cx[0][0],
             " mm at ", cx[0][6], ". Chamfer or round it — see edges.scad."));
  assert(lc <= max_concave_mm + 1e-6,
         str("DFM: ", lc, " mm of concave edge sharper than ", angle,
             " deg (budget ", max_concave_mm, "). Worst feature: ", cc[0][0],
             " mm at ", cc[0][6], ". Fillet it — see edge_cove() in edges.scad."));
  children();
}

// Echo the sharp-edge features, worst first, without asserting anything. This
// is the one to reach for first: it names WHERE the edges are, which is what
// you need before you can decide what to do about them.
module dfm_sharp_report(angle = 50, kind = "any", limit = 10) {
  e = sharp_edges(angle = angle, kind = kind);
  echo(str("DFM sharp-edge report: ", len(e), " features, ",
           dfm_sharp_length(e), " mm total, at >= ", angle, " deg"));
  for (i = [0 : min(limit, len(e)) - 1])
    echo(str("  ", e[i][3] ? "CONVEX " : "concave", " ", e[i][0], " mm  ",
             e[i][1], " deg  centre ", e[i][6], "  bbox ", e[i][4], "..", e[i][5]));
  children();
}


// ---- Convenience report + combined check ------------------------------------

// Echo a one-shot DFM report for the children (no assertions).
module dfm_report() {
  bb = bbox();
  echo(dfm_bbox = bb,
       dfm_size = bb[1] - bb[0],
       dfm_centroid = centroid(),
       dfm_volume_mm3 = volume(),
       dfm_max_overhang_deg = overhang(),
       dfm_sharp_convex_mm = dfm_sharp_length(sharp_edges(angle = 50, kind = "convex")),
       dfm_sharp_concave_mm = dfm_sharp_length(sharp_edges(angle = 50, kind = "concave")));
  children();
}

// Run the applicable checks in one wrapper. Pass build_volume=undef to skip the
// fit check; keep_out=[center, radius] to add a clearance check.
module dfm_check(max_overhang = 45, build_volume = undef, keep_out = undef,
                 report = true) {
  if (report) dfm_report() children();

  // overhang is always checked
  dfm_assert_overhang(max_overhang)
    if (!is_undef(build_volume))
      dfm_assert_within(build_volume)
        _dfm_maybe_clearance(keep_out)
          children();
    else
      _dfm_maybe_clearance(keep_out)
        children();
}

// internal: apply clearance check only when keep_out is provided
module _dfm_maybe_clearance(keep_out) {
  if (is_undef(keep_out)) children();
  else dfm_assert_clearance(keep_out[0], keep_out[1]) children();
}
