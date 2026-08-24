// Test for the experimental geometry-metrics feature: bbox(), centroid(),
// volume(), overhang() and sharp_edges().
// Enable with: openscad --enable=geometry-metrics ...
//
// Two forms are exercised:
//   1. Explicit point lists (pure, geometry-free).
//   2. In-module introspection: called inside a user module, these functions
//      measure that module's children's real geometry.

// --- Point-list form ---

// bbox() of a 3D point list -> [[minx,miny,minz],[maxx,maxy,maxz]]
echo(bbox([[0, 0, 0], [10, 5, 2], [-3, 8, 1]]));       // [[-3,0,0],[10,8,2]]

// bbox() of a 2D point list -> [[minx,miny],[maxx,maxy]]
echo(bbox([[1, 1], [4, -2], [0, 7]]));                 // [[0,-2],[4,7]]

// centroid() of a 3D point list -> [cx,cy,cz]
echo(centroid([[0, 0, 0], [3, 3, 3], [6, 6, 6]]));     // [3,3,3]

// centroid() of a 2D point list -> [cx,cy]
echo(centroid([[0, 0], [2, 4]]));                      // [1,2]

// Single-point list is valid (degenerate box).
echo(bbox([[5, 5, 5]]));                               // [[5,5,5],[5,5,5]]

// --- In-module geometry introspection ---

// Measures the module's children's actual geometry. For cube([10,20,30]):
//   bbox     = [[0,0,0],[10,20,30]]
//   centroid = [5,10,15]
//   volume   = 6000
//   overhang = 0 (vertical walls; top faces up; bottom rests on the bed)
module probe() {
  echo("bbox", bbox());
  echo("centroid", centroid());
  echo("volume", volume());
  echo("overhang", overhang());
  children();
}
probe() cube([10, 20, 30]);


// --- sharp_edges() ---

// Every edge of a box is a 90-degree convex edge, and they all connect, so the
// whole wireframe comes back as ONE feature: 4*(10+20+30) = 240 mm of it.
// Record layout is [length, angle, angle_max, convex, [min], [max], [centre]].
module edges() {
  e = sharp_edges(angle = 50);
  echo("count", len(e));
  echo("length", e[0][0], "angle", e[0][1], "convex", e[0][3]);
  echo("bbox", e[0][4], e[0][5]);
  children();
}
edges() cube([10, 20, 30]);

// Convexity is reported, not assumed. An L-shape has one CONCAVE edge running
// up its inside corner; asking for kind="concave" isolates it.
module inside_corner() {
  e = sharp_edges(angle = 50, kind = "concave");
  echo("concave count", len(e));
  echo("concave length", e[0][0], "convex", e[0][3]);
  children();
}
inside_corner() difference() {
  cube([10, 10, 10]);
  translate([5, 5, -1]) cube([10, 10, 12]);
}

// A chamfer is not a sharp edge -- which is the whole point of the threshold.
// A cylinder is used rather than a box because a box has four 90-degree
// VERTICAL corner edges of its own, and those connect everything into a single
// feature and drown out what is being demonstrated.
//
// This cylinder has a 45-degree chamfer round its top rim. At 50 degrees only
// the unchamfered bottom rim is reported: one feature, 2*PI*10 = 62.83 mm. Drop
// the threshold below 45 and the two rings the chamfer itself introduced show
// up as two more features -- separate features, because the barrel between them
// is smooth.
module chamfered() {
  echo("chamfer at 50", len(sharp_edges(angle = 50)));
  echo("chamfer at 50, length", sharp_edges(angle = 50)[0][0]);
  echo("chamfer at 40", len(sharp_edges(angle = 40)));
  children();
}
chamfered() hull() {
  linear_extrude(9) circle(r = 10, $fn = 64);
  translate([0, 0, 10]) linear_extrude(0.001) circle(r = 9, $fn = 64);
}

// A shape with no sharp edges at all returns an empty list, not undef, so a
// `for` over the result is always safe.
module smooth() {
  echo("sphere", sharp_edges(angle = 50));
  children();
}
smooth() sphere(r = 10, $fn = 64);
