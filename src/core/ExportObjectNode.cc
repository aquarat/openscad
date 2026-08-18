/*
 *  OpenSCAD (www.openscad.org)
 *  Copyright (C) 2009-2011 Clifford Wolf <clifford@clifford.at> and
 *                          Marius Kintel <marius@kintel.net>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  As a special exception, you have permission to link this program
 *  with the CGAL library and distribute executables, as long as you
 *  follow the requirements of the GNU GPL in regard to all of the
 *  software in the executable aside from CGAL.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include "core/ExportObjectNode.h"

#include <cstddef>
#include <memory>
#include <sstream>
#include <string>
#include <utility>

#include "Feature.h"
#include "core/Builtins.h"
#include "core/Children.h"
#include "core/ColorUtil.h"
#include "core/ModuleInstantiation.h"
#include "core/Parameters.h"
#include "core/module.h"
#include "geometry/linalg.h"
#include "utils/printutils.h"

static std::shared_ptr<AbstractNode> builtin_export_object(const ModuleInstantiation *inst,
                                                           Arguments arguments, const Children& children)
{
  Parameters parameters =
    Parameters::parse(std::move(arguments), inst->location(), {"name"}, {"material", "color"});

  // Only available as an experimental feature.
  ExperimentalFeatureException::check(Feature::ExperimentalExportObject);

  auto node = std::make_shared<ExportObjectNode>(inst);

  if (parameters["name"].type() == Value::Type::STRING) {
    node->objectName = parameters["name"].toString();
  } else {
    LOG(message_group::Warning, inst->location(), parameters.documentRoot(),
        "export_object() requires a name string as its first argument.");
  }

  if (parameters["material"].type() == Value::Type::STRING) {
    node->material = parameters["material"].toString();
  }

  // color accepts a [r, g, b(, a)] vector or a color name string, mirroring color().
  if (parameters["color"].type() == Value::Type::VECTOR) {
    const auto& vec = parameters["color"].toVector();
    Vector4f color;
    for (size_t i = 0; i < 4; ++i) {
      color[i] = i < vec.size() ? (float)vec[i].toDouble() : 1.0f;
      if (color[i] > 1 || color[i] < 0) {
        LOG(message_group::Warning, inst->location(), parameters.documentRoot(),
            "export_object() expects color numbers between 0.0 and 1.0. Value of %1$.1f is out of range",
            color[i]);
      }
    }
    node->color = Color4f(color);
  } else if (parameters["color"].type() == Value::Type::STRING) {
    auto colorname = parameters["color"].toString();
    const auto parsed_color = OpenSCAD::parse_color(colorname);
    if (parsed_color) {
      node->color = *parsed_color;
    } else {
      LOG(message_group::Warning, inst->location(), parameters.documentRoot(),
          "Unable to parse color \"%1$s\"", colorname);
    }
  }

  return children.instantiate(node);
}

std::string ExportObjectNode::toString() const
{
  std::ostringstream stream;

  stream << this->name() << "(name = \"" << this->objectName << "\", material = \"" << this->material
         << "\")";

  return stream.str();
}

void register_builtin_export_object()
{
  Builtins::init("export_object", new BuiltinModule(builtin_export_object, &Feature::ExperimentalExportObject),
                 {
                   "export_object(name)",
                   "export_object(name, material = \"...\")",
                   "export_object(name, material = \"...\", color = [r, g, b])",
                 });
}
