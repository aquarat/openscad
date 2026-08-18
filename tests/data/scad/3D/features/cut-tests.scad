// Experimental section-cut cut() operator tests.
// Requires --enable=section-cut (and the Manifold backend for actual geometry).

// Cut a cube with the plane x >= 0 (keep the +X half), plane form.
translate([0, 0, 0])
  cut(plane = [1, 0, 0, 0]) cube(20, center = true);

// Same cut expressed with normal + offset.
translate([40, 0, 0])
  cut(normal = [1, 0, 0], offset = 0) cube(20, center = true);

// Offset plane: keep z >= 2.
translate([80, 0, 0])
  cut(normal = [0, 0, 1], offset = 2) sphere(r = 12, $fn = 32);

// Non-axis-aligned, non-unit normal (normal is normalized internally).
translate([120, 0, 0])
  cut(plane = [1, 1, 0, 3]) cube(20, center = true);

// Cut applied to a union of children.
translate([160, 0, 0])
  cut(normal = [0, 1, 0], offset = 0) {
    cube(16, center = true);
    translate([0, 8, 0]) sphere(r = 8, $fn = 24);
  }
