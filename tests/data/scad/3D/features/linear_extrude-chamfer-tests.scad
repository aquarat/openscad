// Experimental linear_extrude chamfer/draft tests.
// Requires --enable=linear-extrude-chamfer

// Chamfer both rims of a square.
translate([0, 0, 0])
  linear_extrude(height = 10, chamfer = 2) square(20, center = true);

// Chamfer only the top rim.
translate([40, 0, 0])
  linear_extrude(height = 10, chamfer = 2, chamfer_bottom = false) square(20, center = true);

// Chamfer only the bottom rim.
translate([80, 0, 0])
  linear_extrude(height = 10, chamfer = 2, chamfer_top = false) square(20, center = true);

// Chamfer with a hole (ring), centered.
translate([120, 0, 0])
  linear_extrude(height = 10, chamfer = 1.5, center = true)
    difference() { circle(r = 12, $fn = 48); circle(r = 6, $fn = 48); }

// Draft angle only (tapered sidewall, no chamfer).
translate([160, 0, 0])
  linear_extrude(height = 10, draft = 5) square(20, center = true);

// Chamfer combined with scale + twist.
translate([200, 0, 0])
  linear_extrude(height = 15, chamfer = 2, scale = 0.6, twist = 45, slices = 20)
    square(20, center = true);
