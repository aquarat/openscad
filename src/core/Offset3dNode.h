#pragma once

#include <string>
#include <utility>

#include "core/CurveDiscretizer.h"
#include "core/ModuleInstantiation.h"
#include "core/node.h"

// 3D enclosure primitives: a constant-radius surface offset (offset3d) and a
// constant-thickness hollowing operator (shell). Both take the union of their
// children as the input solid.
//   OFFSET: rounded surface offset by `delta` (delta > 0 outward, delta < 0 inward).
//   SHELL:  hollow the solid, leaving an inward wall of thickness `delta`.
// The sphere used for the Minkowski sum/difference is tessellated from the
// discretizer ($fn/$fs/$fa) evaluated at radius |delta|.
class Offset3dNode : public AbstractNode
{
public:
  VISITABLE();
  enum Mode { OFFSET, SHELL };

  Offset3dNode(const ModuleInstantiation *mi, Mode mode, CurveDiscretizer discretizer)
    : AbstractNode(mi), mode(mode), discretizer(std::move(discretizer))
  {
  }

  std::string toString() const override;
  std::string name() const override { return mode == SHELL ? "shell" : "offset3d"; }

  Mode mode{OFFSET};
  double delta{1};
  CurveDiscretizer discretizer;
};
