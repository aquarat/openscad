#pragma once

#include <string>
#include <utility>

#include "core/CurveDiscretizer.h"
#include "core/ModuleInstantiation.h"
#include "core/Value.h"
#include "core/node.h"
#include "geometry/linalg.h"

class LinearExtrudeNode : public AbstractPolyNode
{
public:
  VISITABLE();
  LinearExtrudeNode(const ModuleInstantiation *mi, CurveDiscretizer discretizer)
    : AbstractPolyNode(mi), discretizer(std::move(discretizer))
  {
  }
  std::string toString() const override;
  std::string name() const override { return "linear_extrude"; }

  Vector3d height = Vector3d(0, 0, 1);
  CurveDiscretizer discretizer;
  double scale_x = 1.0, scale_y = 1.0;
  double twist = 0.0;
  unsigned int convexity = 1u;
  unsigned int slices = 1u, segments = 0u;
  bool has_twist = false, has_slices = false, has_segments = false;
  bool center = false;
  // DFM chamfer/draft (experimental, see Feature::ExperimentalLinearExtrudeChamfer).
  // chamfer: horizontal inset (mm) of the top and/or bottom rim, forming a 45deg
  // self-supporting bevel over an equal vertical distance.
  double chamfer = 0.0;
  bool chamfer_top = true;
  bool chamfer_bottom = true;
  // draft: sidewall taper angle in degrees; positive tapers the profile inward
  // toward the top over the full height.
  double draft = 0.0;
};
