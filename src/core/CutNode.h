#pragma once

#include <string>

#include "core/ModuleInstantiation.h"
#include "core/node.h"
#include "geometry/linalg.h"

// A half-space section cut. Keeps the part of the (3D) children on the side the
// plane normal points toward. The plane is stored as (a, b, c, d) with normal
// (a, b, c) and offset d; kept half-space is { p : a*p.x + b*p.y + c*p.z >= d }.
class CutNode : public AbstractNode
{
public:
  VISITABLE();
  CutNode(const ModuleInstantiation *mi) : AbstractNode(mi) {}

  std::string toString() const override;
  std::string name() const override { return "cut"; }

  Vector4d plane{1, 0, 0, 0};
};
