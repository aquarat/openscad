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

#include "core/Offset3dNode.h"

#include <ios>
#include <memory>
#include <sstream>
#include <utility>

#include "Feature.h"
#include "core/Builtins.h"
#include "core/Children.h"
#include "core/CurveDiscretizer.h"
#include "core/ModuleInstantiation.h"
#include "core/Parameters.h"
#include "core/module.h"
#include "utils/printutils.h"

static std::shared_ptr<AbstractNode> builtin_offset3d(const ModuleInstantiation *inst,
                                                      Arguments arguments, const Children& children)
{
  // Only available as an experimental feature.
  ExperimentalFeatureException::check(Feature::ExperimentalShell);

  Parameters parameters =
    Parameters::parse(std::move(arguments), inst->location(), {"delta"}, {});
  auto node = std::make_shared<Offset3dNode>(inst, Offset3dNode::OFFSET,
                                             CurveDiscretizer(parameters, inst->location()));

  node->delta = 0;
  if (parameters["delta"].isDefinedAs(Value::Type::NUMBER)) {
    node->delta = parameters["delta"].toDouble();
  } else {
    LOG(message_group::Warning, inst->location(), parameters.documentRoot(),
        "offset3d() requires a numeric delta, e.g. offset3d(delta = 2) or offset3d(-2).");
  }

  return children.instantiate(node);
}

static std::shared_ptr<AbstractNode> builtin_shell(const ModuleInstantiation *inst,
                                                   Arguments arguments, const Children& children)
{
  // Only available as an experimental feature.
  ExperimentalFeatureException::check(Feature::ExperimentalShell);

  Parameters parameters =
    Parameters::parse(std::move(arguments), inst->location(), {"thickness"}, {});
  auto node = std::make_shared<Offset3dNode>(inst, Offset3dNode::SHELL,
                                             CurveDiscretizer(parameters, inst->location()));

  node->delta = 0;
  if (parameters["thickness"].isDefinedAs(Value::Type::NUMBER)) {
    node->delta = parameters["thickness"].toDouble();
  } else {
    LOG(message_group::Warning, inst->location(), parameters.documentRoot(),
        "shell() requires a numeric thickness, e.g. shell(thickness = 2).");
  }

  return children.instantiate(node);
}

std::string Offset3dNode::toString() const
{
  std::ostringstream stream;

  const auto var = mode == SHELL ? "(thickness = " : "(delta = ";
  stream << this->name() << var << std::dec << this->delta << ", " << this->discretizer << ")";

  return stream.str();
}

void register_builtin_offset3d()
{
  Builtins::init("offset3d", new BuiltinModule(builtin_offset3d, &Feature::ExperimentalShell),
                 {
                   "offset3d(delta = number)",
                 });
  Builtins::init("shell", new BuiltinModule(builtin_shell, &Feature::ExperimentalShell),
                 {
                   "shell(thickness = number)",
                 });
}
