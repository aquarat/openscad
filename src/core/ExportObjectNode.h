#pragma once

#include <string>

#include <boost/optional.hpp>

#include "core/ModuleInstantiation.h"
#include "core/node.h"
#include "geometry/linalg.h"

// A transparent operator that passes its child geometry through unchanged but
// tags it with an export name and (optionally) a material and color. The
// metadata rides on the produced geometry so that the 3MF exporter can emit the
// child as a named 3MF object carrying that material.
class ExportObjectNode : public AbstractNode
{
public:
  VISITABLE();
  ExportObjectNode(const ModuleInstantiation *mi) : AbstractNode(mi) {}

  std::string toString() const override;
  std::string name() const override { return "export_object"; }

  std::string objectName;
  std::string material;
  boost::optional<Color4f> color;
};
