// Experimental export_object() operator tests.
// Requires --enable=export-object. export_object() is a transparent passthrough
// that tags a top-level child with a name and (optionally) a material and color
// that survive into 3MF export as a named object carrying that material.

// An assembly whose two parts should be printed on different plates/materials.
module bezel() cube([20, 20, 4], center = true);
module gasket() {
  difference() {
    cylinder(h = 4, r = 12, center = true, $fn = 48);
    cylinder(h = 5, r = 8, center = true, $fn = 48);
  }
}

// Positional name, named material.
translate([0, 0, 0])
  export_object("bezel", material = "PETG") bezel();

// Positional name, different material, so it lands on a separate plate.
translate([40, 0, 0])
  export_object("gasket", material = "TPU") gasket();

// Fully named form with an optional display color.
translate([80, 0, 0])
  export_object(name = "clip", material = "PETG", color = [0.2, 0.2, 0.2])
    cube([10, 6, 4], center = true);
