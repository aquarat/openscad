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

#include "core/CutNode.h"

#include <ios>
#include <memory>
#include <sstream>
#include <utility>

#include "Feature.h"
#include "core/Builtins.h"
#include "core/Children.h"
#include "core/ModuleInstantiation.h"
#include "core/Parameters.h"
#include "core/module.h"
#include "geometry/linalg.h"
#include "utils/printutils.h"

static std::shared_ptr<AbstractNode> builtin_cut(const ModuleInstantiation *inst, Arguments arguments,
                                                 const Children& children)
{
  Parameters parameters =
    Parameters::parse(std::move(arguments), inst->location(), {}, {"plane", "normal", "offset"});

  // Only available as an experimental feature.
  ExperimentalFeatureException::check(Feature::ExperimentalSectionCut);

  auto node = std::make_shared<CutNode>(inst);

  // Syntax 1: cut(plane = [a, b, c, d]).
  // Syntax 2: cut(normal = [a, b, c], offset = d).
  if (parameters["plane"].type() == Value::Type::VECTOR) {
    const auto& v = parameters["plane"].toVector();
    if (v.size() != 4) {
      LOG(message_group::Warning, inst->location(), parameters.documentRoot(),
          "cut(plane=...) expects a vector of 4 numbers [a, b, c, d]; ignoring cut.");
      node->plane << 1, 0, 0, 0;
    } else {
      node->plane << v[0].toDouble(), v[1].toDouble(), v[2].toDouble(), v[3].toDouble();
    }
  } else if (parameters["normal"].type() == Value::Type::VECTOR) {
    const auto& n = parameters["normal"].toVector();
    Vector3d normal(1, 0, 0);
    if (n.size() >= 1) normal[0] = n[0].toDouble();
    if (n.size() >= 2) normal[1] = n[1].toDouble();
    if (n.size() >= 3) normal[2] = n[2].toDouble();
    double offset = 0.0;
    parameters["offset"].getFiniteDouble(offset);
    node->plane << normal[0], normal[1], normal[2], offset;
  } else {
    LOG(message_group::Warning, inst->location(), parameters.documentRoot(),
        "cut() requires either plane=[a,b,c,d] or normal=[a,b,c] (with optional offset=d).");
    node->plane << 1, 0, 0, 0;
  }

  return children.instantiate(node);
}

std::string CutNode::toString() const
{
  std::ostringstream stream;

  stream << this->name() << "(plane = [" << std::dec << this->plane[0] << ", " << this->plane[1] << ", "
         << this->plane[2] << ", " << this->plane[3] << "])";

  return stream.str();
}

void register_builtin_cut()
{
  Builtins::init("cut", new BuiltinModule(builtin_cut, &Feature::ExperimentalSectionCut),
                 {
                   "cut(plane = [a, b, c, d])",
                   "cut(normal = [a, b, c], offset = d)",
                 });
}
