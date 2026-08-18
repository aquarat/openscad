// Experimental 3D enclosure primitive tests: offset3d() and shell().
// Requires --enable=shell (and the Manifold backend for actual geometry).

// Outward rounded surface offset of a cube (delta > 0), named arg.
translate([0, 0, 0])
  offset3d(delta = 3, $fn = 24) cube(20, center = true);

// Inward surface offset of a cube (delta < 0), positional arg.
translate([40, 0, 0])
  offset3d(-3, $fn = 24) cube(20, center = true);

// Outward offset of a cylinder.
translate([80, 0, 0])
  offset3d(delta = 2, $fn = 24) cylinder(h = 20, r = 8, center = true, $fn = 32);

// Hollow cube with a constant 2.4 mm wall.
translate([120, 0, 0])
  shell(thickness = 2.4, $fn = 24) cube(20, center = true);

// Hollow cylinder shell.
translate([160, 0, 0])
  shell(thickness = 2, $fn = 24) cylinder(h = 20, r = 8, center = true, $fn = 32);

// Offset applied to a union of children.
translate([200, 0, 0])
  offset3d(delta = 2, $fn = 24) {
    cube(16, center = true);
    translate([0, 8, 0]) sphere(r = 8, $fn = 24);
  }
