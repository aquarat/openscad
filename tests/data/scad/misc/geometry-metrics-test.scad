// Test for the experimental geometry-metrics feature: bbox(), centroid(),
// volume() and overhang(). Enable with: openscad --enable=geometry-metrics ...
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
