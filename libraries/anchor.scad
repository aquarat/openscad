// anchor.scad — first-class attachment / anchoring, built on the core
// geometry-introspection primitive (bbox()).
//
//   Requires: run OpenSCAD with  --enable=geometry-metrics  (the bbox()
//   function this library measures with is experimental).
//
//   Usage:
//     use <anchor.scad>
//     attach("top") {                        // string names work with `use`
//       cube([40, 30, 10], center = true);   // child 0 = the parent object
//       cylinder(d = 8, h = 12, $fn = 32);   // child 1.. = things attached
//     }
//
//   Anchors may be given as a string ("top","bottom","left","right","front",
//   "back","center") or as a vector (the TOP/RIGHT/... constants below, or any
//   [x,y,z] with components in -1..1). NOTE: OpenSCAD's `use <>` imports modules
//   and functions but NOT top-level variables, so the TOP/RIGHT/... constants
//   are only visible if you `include <anchor.scad>`. The string names always
//   work, so they are the recommended form with `use`.
//
// The first child is the reference object: it is rendered as-is and measured.
// Every subsequent child is placed with its origin on the chosen face of the
// reference object's bounding box (optionally pulled in by `overlap`, and
// optionally re-oriented so its +Z points along the anchor normal).

// ---- Anchor direction constants (unit vectors, object-relative) -------------
TOP    = [ 0,  0,  1];
BOTTOM = [ 0,  0, -1];
LEFT   = [-1,  0,  0];
RIGHT  = [ 1,  0,  0];
FRONT  = [ 0, -1,  0];
BACK   = [ 0,  1,  0];
CENTER = [ 0,  0,  0];

// ---- Geometry helpers -------------------------------------------------------

// Resolve an anchor given as a string name or a vector into a unit vector.
function _anchor_vec(a) =
  is_string(a) ?
      (a == "top"    ? [ 0,  0,  1] :
       a == "bottom" ? [ 0,  0, -1] :
       a == "left"   ? [-1,  0,  0] :
       a == "right"  ? [ 1,  0,  0] :
       a == "front"  ? [ 0, -1,  0] :
       a == "back"   ? [ 0,  1,  0] :
       a == "center" ? [ 0,  0,  0] :
       assert(false, str("attach/position: unknown anchor name \"", a, "\"")))
    : a;

// The point on a bounding box `bb` (=[[minx,miny,minz],[maxx,maxy,maxz]]) in
// the direction of unit anchor `a`. a=[0,0,0] gives the box centre.
function anchor_point(bb, a) =
  let (ctr = (bb[0] + bb[1]) / 2, half = (bb[1] - bb[0]) / 2)
  [ ctr[0] + a[0] * half[0],
    ctr[1] + a[1] * half[1],
    ctr[2] + a[2] * half[2] ];

// Rotation (as an [angle, axis] pair) that maps +Z onto unit vector `v`.
// Handles the parallel / antiparallel degenerate cases.
function _zrot_to(v) =
  let (d = v[2] / norm(v))                       // v assumed unit, but be safe
    d >  0.999999 ? [0,   [0, 0, 1]] :           // already +Z
    d < -0.999999 ? [180, [1, 0, 0]] :           // -Z: flip about X
    [acos(d), [-v[1], v[0], 0]];                 // axis = cross([0,0,1], v)

// ---- Anchoring modules ------------------------------------------------------

// attach(anchor, overlap, orient)
//   anchor  : one of the constants above (or any [x,y,z], components in -1..1).
//   overlap : distance the attached children are pulled back INTO the parent
//             along the anchor normal (gives a weld/overlap for a clean union).
//   orient  : if true, rotate attached children so their +Z axis points along
//             the anchor normal (e.g. a cylinder sticks straight out of a side
//             wall). If false (default) children are only translated.
module attach(anchor = "top", overlap = 0, orient = false) {
  // Child 0 is the reference object — draw it.
  children(0);

  av = _anchor_vec(anchor);
  bb = bbox(0);
  assert(!is_undef(bb), "attach(): reference child has no measurable geometry");
  pt = anchor_point(bb, av);
  rot = _zrot_to(av);

  // Attach every remaining child.
  translate(pt - av * overlap) {
    if (orient) {
      rotate(a = rot[0], v = rot[1])
        for (i = [1 : 1 : $children - 1]) children(i);
    } else {
      for (i = [1 : 1 : $children - 1]) children(i);
    }
  }
}

// position(anchor) — like attach() but does NOT draw the reference child.
// Useful to place something relative to a solid you draw elsewhere: pass the
// reference as child 0 (it is measured but not rendered) and the payload after.
module position(anchor = "top", overlap = 0, orient = false) {
  av = _anchor_vec(anchor);
  bb = bbox(0);
  assert(!is_undef(bb), "position(): reference child has no measurable geometry");
  pt = anchor_point(bb, av);
  rot = _zrot_to(av);
  translate(pt - av * overlap) {
    if (orient) {
      rotate(a = rot[0], v = rot[1])
        for (i = [1 : 1 : $children - 1]) children(i);
    } else {
      for (i = [1 : 1 : $children - 1]) children(i);
    }
  }
}
