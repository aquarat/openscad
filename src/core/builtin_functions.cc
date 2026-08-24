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

#include <algorithm>
#include <array>
#include <boost/format.hpp>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <limits>
#include <map>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "core/AST.h"
#include "core/Arguments.h"
#include "core/Builtins.h"
#include "core/Context.h"
#include "core/EvaluationSession.h"
#include "core/Expression.h"
#include "core/FreetypeRenderer.h"
#include "core/Parameters.h"
#include "core/UserModule.h"
#include "core/function.h"
#include "io/fileutils.h"
#include "io/import.h"
#include "utils/boost-utils.h"
#include "utils/degree_trig.h"
#include "utils/exceptions.h"
#include "utils/printutils.h"
#include "version.h"
// hash double
#include "geometry/linalg.h"
// Geometry introspection (bbox/centroid/volume/overhang)
#include "core/Children.h"
#include "core/Tree.h"
#include "core/node.h"
#include "geometry/Geometry.h"
#include "geometry/GeometryEvaluator.h"
#include "geometry/PolySet.h"
#include "geometry/PolySetUtils.h"

#if defined __WIN32__ || defined _MSC_VER
#include <process.h>
int process_id = _getpid();
#else
#include <sys/types.h>
#include <unistd.h>
int process_id = getpid();
#endif

std::mt19937 deterministic_rng(std::time(nullptr) + process_id);
void initialize_rng()
{
  static uint64_t seed_val = 0;
  seed_val ^= uint64_t(std::time(nullptr) + process_id);
  deterministic_rng.seed(seed_val);
  std::uniform_int_distribution<uint64_t> distributor(0);
  seed_val ^= distributor(deterministic_rng);
}

static inline bool check_arguments(const char *function_name, const Arguments& arguments,
                                   const Location& loc, unsigned int expected_count, bool warn = true)
{
  if (arguments.size() != expected_count) {
    if (warn) {
      print_argCnt_warning(function_name, arguments.size(), STR(expected_count), loc,
                           arguments.documentRoot());
    }
    return false;
  }
  return true;
}
/* // Commented due to compiler warning of unused function.
   static inline bool try_check_arguments(const Arguments& arguments, int expected_count)
   {
   return check_arguments(nullptr, arguments, Location::NONE, expected_count, false);
   }
 */
template <size_t N>
static inline bool check_arguments(const char *function_name, const Arguments& arguments,
                                   const Location& loc, const Value::Type (&expected_types)[N],
                                   bool warn = true)
{
  if (!check_arguments(function_name, arguments, loc, N, warn)) {
    return false;
  }
  for (size_t i = 0; i < N; i++) {
    if (arguments[i]->type() != expected_types[i]) {
      if (warn) {
        print_argConvert_positioned_warning(function_name, "argument " + STR(i), arguments[i]->clone(),
                                            {expected_types[i]}, loc, arguments.documentRoot());
      }
      return false;
    }
  }
  return true;
}

template <size_t N>
static inline bool try_check_arguments(const Arguments& arguments,
                                       const Value::Type (&expected_types)[N])
{
  return check_arguments(nullptr, arguments, Location::NONE, expected_types, false);
}

Value builtin_abs(Arguments arguments, const Location& loc)
{
  if (!check_arguments("abs", arguments, loc, {Value::Type::NUMBER})) {
    return Value::undefined.clone();
  }
  return {std::fabs(arguments[0]->toDouble())};
}

Value builtin_sign(Arguments arguments, const Location& loc)
{
  if (!check_arguments("sign", arguments, loc, {Value::Type::NUMBER})) {
    return Value::undefined.clone();
  }
  double x = arguments[0]->toDouble();
  return {(x < 0) ? -1.0 : ((x > 0) ? 1.0 : 0.0)};
}

Value builtin_rands(Arguments arguments, const Location& loc)
{
  if (arguments.size() < 3 || arguments.size() > 4) {
    print_argCnt_warning("rands", arguments.size(), "3 or 4", loc, arguments.documentRoot());
    return Value::undefined.clone();
  } else if (arguments.size() == 3) {
    if (!check_arguments("rands", arguments, loc,
                         {Value::Type::NUMBER, Value::Type::NUMBER, Value::Type::NUMBER})) {
      return Value::undefined.clone();
    }
  } else {
    if (!check_arguments(
          "rands", arguments, loc,
          {Value::Type::NUMBER, Value::Type::NUMBER, Value::Type::NUMBER, Value::Type::NUMBER})) {
      return Value::undefined.clone();
    }
  }

  double min = arguments[0]->toDouble();
  if (std::isinf(min) || std::isnan(min)) {
    LOG(message_group::Warning, loc, arguments.documentRoot(), "rands() range min cannot be infinite");
    min = -std::numeric_limits<double>::max() / 2;
    LOG(message_group::Warning, "resetting to %1f", min);
  }

  double max = arguments[1]->toDouble();
  if (std::isinf(max) || std::isnan(max)) {
    LOG(message_group::Warning, loc, arguments.documentRoot(), "rands() range max cannot be infinite");
    max = std::numeric_limits<double>::max() / 2;
    LOG(message_group::Warning, "resetting to %1f", max);
  }
  if (max < min) {
    double tmp = min;
    min = max;
    max = tmp;
  }

  double numresultsd = std::abs(arguments[2]->toDouble());
  if (std::isinf(numresultsd) || std::isnan(numresultsd)) {
    LOG(message_group::Warning, loc, arguments.documentRoot(),
        "rands() cannot create an infinite number of results");
    LOG(message_group::Warning, "resetting number of results to 1");
    numresultsd = 1;
  }
  auto numresults = boost_numeric_cast<size_t, double>(numresultsd);

  if (arguments.size() > 3) {
    auto seed = static_cast<uint32_t>(hash_floating_point(arguments[3]->toDouble()));
    deterministic_rng.seed(seed);
  }

  VectorType vec(arguments.session());
  vec.reserve(numresults);
  if (min >= max) {  // uniform_real_distribution doesn't allow min == max
    for (size_t i = 0; i < numresults; ++i) vec.emplace_back(min);
  } else {
    std::uniform_real_distribution<> distributor(min, max);
    for (size_t i = 0; i < numresults; ++i) {
      vec.emplace_back(distributor(deterministic_rng));
    }
  }
  return std::move(vec);
}

static std::vector<double> min_max_arguments(const Arguments& arguments, const Location& loc,
                                             const char *function_name)
{
  std::vector<double> output;
  // preserve special handling of the first argument
  // as a template for vector processing
  if (arguments.size() == 0) {
    print_argCnt_warning(function_name, arguments.size(), "at least 1", loc, arguments.documentRoot());
    return {};
  } else if (arguments.size() == 1 && arguments[0]->type() == Value::Type::VECTOR) {
    const auto& elements = arguments[0]->toVector();
    if (elements.size() == 0) {
      print_argCnt_warning(function_name, elements.size(), "at least 1 vector element", loc,
                           arguments.documentRoot());
      return {};
    }
    for (size_t i = 0; i < elements.size(); i++) {
      const auto& element = elements[i];
      // 4/20/14 semantic change per discussion:
      // break on any non-number
      if (element.type() != Value::Type::NUMBER) {
        print_argConvert_positioned_warning(function_name, "vector element " + STR(i), element,
                                            {Value::Type::NUMBER}, loc, arguments.documentRoot());
        return {};
      }
      output.push_back(element.toDouble());
    }
  } else {
    for (size_t i = 0; i < arguments.size(); i++) {
      const auto& argument = arguments[i];
      // 4/20/14 semantic change per discussion:
      // break on any non-number
      if (argument->type() != Value::Type::NUMBER) {
        print_argConvert_positioned_warning(function_name, "argument " + STR(i), argument->clone(),
                                            {Value::Type::NUMBER}, loc, arguments.documentRoot());
        return {};
      }
      output.push_back(argument->toDouble());
    }
  }
  return output;
}

Value builtin_min(Arguments arguments, const Location& loc)
{
  std::vector<double> values = min_max_arguments(arguments, loc, "min");
  if (values.empty()) {
    return Value::undefined.clone();
  }
  return {*std::min_element(values.begin(), values.end())};
}

Value builtin_max(Arguments arguments, const Location& loc)
{
  std::vector<double> values = min_max_arguments(arguments, loc, "max");
  if (values.empty()) {
    return Value::undefined.clone();
  }
  return {*std::max_element(values.begin(), values.end())};
}

Value builtin_sin(Arguments arguments, const Location& loc)
{
  if (!check_arguments("sin", arguments, loc, {Value::Type::NUMBER})) {
    return Value::undefined.clone();
  }
  return {sin_degrees(arguments[0]->toDouble())};
}

Value builtin_cos(Arguments arguments, const Location& loc)
{
  if (!check_arguments("cos", arguments, loc, {Value::Type::NUMBER})) {
    return Value::undefined.clone();
  }
  return {cos_degrees(arguments[0]->toDouble())};
}

Value builtin_asin(Arguments arguments, const Location& loc)
{
  if (!check_arguments("asin", arguments, loc, {Value::Type::NUMBER})) {
    return Value::undefined.clone();
  }
  return {asin_degrees(arguments[0]->toDouble())};
}

Value builtin_acos(Arguments arguments, const Location& loc)
{
  if (!check_arguments("acos", arguments, loc, {Value::Type::NUMBER})) {
    return Value::undefined.clone();
  }
  return {acos_degrees(arguments[0]->toDouble())};
}

Value builtin_tan(Arguments arguments, const Location& loc)
{
  if (!check_arguments("tan", arguments, loc, {Value::Type::NUMBER})) {
    return Value::undefined.clone();
  }
  return {tan_degrees(arguments[0]->toDouble())};
}

Value builtin_atan(Arguments arguments, const Location& loc)
{
  if (!check_arguments("atan", arguments, loc, {Value::Type::NUMBER})) {
    return Value::undefined.clone();
  }
  return {atan_degrees(arguments[0]->toDouble())};
}

Value builtin_atan2(Arguments arguments, const Location& loc)
{
  if (!check_arguments("atan2", arguments, loc, {Value::Type::NUMBER, Value::Type::NUMBER})) {
    return Value::undefined.clone();
  }
  return {atan2_degrees(arguments[0]->toDouble(), arguments[1]->toDouble())};
}

Value builtin_pow(Arguments arguments, const Location& loc)
{
  if (!check_arguments("pow", arguments, loc, {Value::Type::NUMBER, Value::Type::NUMBER})) {
    return Value::undefined.clone();
  }
  return {pow(arguments[0]->toDouble(), arguments[1]->toDouble())};
}

Value builtin_round(Arguments arguments, const Location& loc)
{
  if (!check_arguments("round", arguments, loc, {Value::Type::NUMBER})) {
    return Value::undefined.clone();
  }
  return {round(arguments[0]->toDouble())};
}

Value builtin_ceil(Arguments arguments, const Location& loc)
{
  if (!check_arguments("ceil", arguments, loc, {Value::Type::NUMBER})) {
    return Value::undefined.clone();
  }
  return {ceil(arguments[0]->toDouble())};
}

Value builtin_floor(Arguments arguments, const Location& loc)
{
  if (!check_arguments("floor", arguments, loc, {Value::Type::NUMBER})) {
    return Value::undefined.clone();
  }
  return {floor(arguments[0]->toDouble())};
}

Value builtin_sqrt(Arguments arguments, const Location& loc)
{
  if (!check_arguments("sqrt", arguments, loc, {Value::Type::NUMBER})) {
    return Value::undefined.clone();
  }
  return {sqrt(arguments[0]->toDouble())};
}

Value builtin_exp(Arguments arguments, const Location& loc)
{
  if (!check_arguments("exp", arguments, loc, {Value::Type::NUMBER})) {
    return Value::undefined.clone();
  }
  return {exp(arguments[0]->toDouble())};
}

Value builtin_length(Arguments arguments, const Location& loc)
{
  if (try_check_arguments(arguments, {Value::Type::VECTOR})) {
    return {double(arguments[0]->toVector().size())};
  }
  if (try_check_arguments(arguments, {Value::Type::OBJECT})) {
    return double(arguments[0]->toObject().values().size());
  }
  if (!check_arguments("len", arguments, loc, {Value::Type::STRING})) {
    return Value::undefined.clone();
  }
  // Unicode glyph count for the length -- rather than the string (num. of bytes) length.
  return {double(arguments[0]->toStrUtf8Wrapper().get_utf8_strlen())};
}

Value builtin_log(Arguments arguments, const Location& loc)
{
  double x, y;
  if (arguments.size() == 1) {
    if (!check_arguments("log", arguments, loc, {Value::Type::NUMBER})) {
      return Value::undefined.clone();
    }
    x = 10.0;
    y = arguments[0]->toDouble();
  } else {
    if (!check_arguments("log", arguments, loc, {Value::Type::NUMBER, Value::Type::NUMBER})) {
      return Value::undefined.clone();
    }
    x = arguments[0]->toDouble();
    y = arguments[1]->toDouble();
  }
  return {log(y) / log(x)};
}

Value builtin_ln(Arguments arguments, const Location& loc)
{
  if (!check_arguments("ln", arguments, loc, {Value::Type::NUMBER})) {
    return Value::undefined.clone();
  }
  return {log(arguments[0]->toDouble())};
}

Value builtin_str(Arguments arguments, const Location& /*loc*/)
{
  std::ostringstream stream;
  for (const auto& argument : arguments) {
    stream << argument->toString();
  }
  return {stream.str()};
}

Value builtin_chr(Arguments arguments, const Location& /*loc*/)
{
  std::ostringstream stream;
  for (const auto& argument : arguments) {
    stream << argument->chrString();
  }
  return {stream.str()};
}

Value builtin_ord(Arguments arguments, const Location& loc)
{
  if (!check_arguments("ord", arguments, loc, {Value::Type::STRING})) {
    return Value::undefined.clone();
  }
  const str_utf8_wrapper& arg_str = arguments[0]->toStrUtf8Wrapper();
  if (!arg_str.utf8_validate()) {
    LOG(message_group::Warning, loc, arguments.documentRoot(),
        "ord() argument '%1$s' is not a valid utf8 string", arg_str.toString());
    return Value::undefined.clone();
  }

  if (arg_str.get_utf8_strlen() == 0) {
    return Value::undefined.clone();
  }

  return {(double)arg_str.get_utf8_char()};
}

Value builtin_concat(Arguments arguments, const Location& /*loc*/)
{
  VectorType result(arguments.session());
  result.reserve(arguments.size());
  for (auto& argument : arguments) {
    if (argument->type() == Value::Type::VECTOR) {
      result.emplace_back(EmbeddedVectorType(std::move(argument->toVectorNonConst())));
    } else {
      result.emplace_back(std::move(argument.value));
    }
  }
  return std::move(result);
}

#define OBJECT_HELP                                                                             \
  "In an unnamed list, entries must be [key,value] to set or [key] to delete. The key must be " \
  "<string>."

static std::string builtin_object_unnamed(ObjectType& result, const Value& value, int arg_index)
{
  std::string prior_args = "Argument " + std::to_string(arg_index) + " ";

  switch (value.type()) {
  case Value::Type::OBJECT: {
    const auto& obj = value.toObject();
    for (const auto& key : obj.keys()) {
      result.set(key, obj.get(key).clone());
    }
    return "";
  }
  case Value::Type::VECTOR: {
    const VectorType& vector = value.toVector();
    int element = 0;
    for (const auto& member : vector) {
      std::string prior_entries = "Element " + std::to_string(element++) + " ";

      if (member.type() != Value::Type::VECTOR) {
        return str(
          boost::format("object( %s[%s<%s>] ) Entry type is not a list, it is <%s>. " OBJECT_HELP) %
          prior_args.c_str() % prior_entries.c_str() % Value::typeName(member.type()) %
          Value::typeName(member.type()));
      }

      const auto& entry = member.toVector();
      switch (entry.size()) {
      case 2:
      case 1: {
        if (entry[0].type() != Value::Type::STRING) {
          const char *es = entry.size() == 1 ? "" : ",value";
          return str(
            boost::format(
              "object(%s[%s[<%s>%s]]) The key of the entry is not <string> but <%s>. " OBJECT_HELP) %
            prior_args % prior_entries % Value::typeName(entry[0].type()) % es %
            Value::typeName(entry[0].type()));
        }
        const auto& key = entry[0].toString();
        if (entry.size() == 1) {
          result.del(key);
        } else {
          result.set(key, entry[1].clone());
        }
        break;
      };

      case 0:
        return str(boost::format("object(%s[%s[]]) Entry is empty. " OBJECT_HELP) % prior_args.c_str() %
                   prior_entries.c_str());

      default:
        return str(
          boost::format(
            "object(%s[%s[...]]) Entry length is %d, must be 1 [key] or 2 [key,value]. " OBJECT_HELP) %
          prior_args % prior_entries % entry.size());
      }
    }
    return "";
  }
  default:
    return str(boost::format(
                 "object(%s<%s>) An unnamed argument must be either <object> or <list>, it is <%s>. ") %
               prior_args % Value::typeName(value.type()) % Value::typeName(value.type()));
  }
}
/**
    The builtin_object function takes either a named or unnamed argument.
    A named argument is assigned with name=value to the result object. An unnamed
    argument is either an object or a vector. An object will have its
    entries copied, potentially overwriting earlier entries. A vector
    must be of vectors. It can contain as member vectors:

        ["k"]       deletes key k
        ["k", v]    sets key k=v

    Any other values are incorrect and will return undef and be logged as warning.

    If a value is a function and takes a "this" argument, then the function
    is converted to a method by creating a new context for it with "this"
    set to the object being created. When we parse the parameters in Parameters
    we will skip the this when the context already has it.
*/
Value builtin_object(const std::shared_ptr<const Context>& context, const FunctionCall *call)
{
  ObjectType result(context->session());
  int n = 0;
  for (const auto& argument : call->arguments) {
    Value value = argument->getExpr()->evaluate(context);
    if (argument->getName().empty()) {
      const auto error = builtin_object_unnamed(result, value, n);
      if (!error.empty()) {
        LOG(message_group::Warning, call->location(), context->documentRoot(), error.c_str());
        return Value::undef(error);
      }
    } else {
      result.set(argument->getName(), std::move(value));
    }
    n++;
  }
  std::vector<Context *> contexts;
  for (Value& value : result.ptr->values) {
    if (value.type() == Value::Type::FUNCTION) {
      auto& function = value.toFunction();
      auto it = function.findAssignmentByName(Parameters::THIS_PARAMETER);
      if (!it) continue;

      auto parent = function.getContext();
      if (parent->lookup_local_variable(Parameters::THIS_CONTEXT)) {
        // function is already a method, so take the parent context
        parent = parent->getParent();
      }
      ContextHandle<Context> ctx = Context::create<Context>(parent);
      contexts.push_back(ctx.operator->());
      Value method(FunctionType(*ctx, function.getExpr(), function.getParameters()));
      value = std::move(method);
    }
  }
  Value object(result);
  for (Context *c : contexts) {
    c->set_variable(Parameters::THIS_CONTEXT, object.clone());
  }
  return object;
}

Value builtin_has_key(Arguments arguments, const Location& loc)
{
  if (!check_arguments("has_key", arguments, loc, {Value::Type::OBJECT, Value::Type::STRING})) {
    return Value::undefined.clone();
  }
  const auto& obj = arguments[0]->toObject();
  const auto& key = arguments[1]->toString();
  return obj.contains(key);
}

Value builtin_lookup(Arguments arguments, const Location& loc)
{
  if (!check_arguments("lookup", arguments, loc, {Value::Type::NUMBER, Value::Type::VECTOR})) {
    return Value::undefined.clone();
  }
  double p = arguments[0]->toDouble();
  if (!std::isfinite(p)) {
    LOG(message_group::Warning, loc, arguments.documentRoot(),
        "lookup(%1$s, ...) first argument is not a number", arguments[0]->toEchoStringNoThrow());
    return Value::undefined.clone();
  }

  double low_p, low_v, high_p, high_v;
  const auto& vec = arguments[1]->toVector();

  // Second must be a vector of vec2, with valid numbers inside
  auto it = vec.begin();
  if (vec.empty() || it->toVector().size() < 2 || !it->getVec2(low_p, low_v)) {
    return Value::undefined.clone();
  }
  high_p = low_p;
  high_v = low_v;

  for (++it; it != vec.end(); ++it) {
    double this_p, this_v;
    if (it->getVec2(this_p, this_v)) {
      if (this_p <= p && (this_p > low_p || low_p > p)) {
        low_p = this_p;
        low_v = this_v;
      }
      if (this_p >= p && (this_p < high_p || high_p < p)) {
        high_p = this_p;
        high_v = this_v;
      }
    }
  }
  if (p <= low_p) return {high_v};
  if (p >= high_p) return {low_v};
  double f = (p - low_p) / (high_p - low_p);
  return {high_v * f + low_v * (1 - f)};
}

/*
   Pattern:

   "search" "(" ( match_value | list_of_match_values ) "," vector_of_vectors
        ("," num_returns_per_match
          ("," index_col_num )? )?
        ")";
   match_value : ( Value::Type::NUMBER | Value::Type::STRING );
   list_of_values : "[" match_value ("," match_value)* "]";
   vector_of_vectors : "[" ("[" Value ("," Value)* "]")+ "]";
   num_returns_per_match : int;
   index_col_num : int;

   The search string and searched strings can be unicode strings.
   Examples:
   Index values return as list:
    search("a","abcdabcd");
        - returns [0]
    search("Л","Л");  //A unicode string
        - returns [0]
    search("🂡aЛ","a🂡Л🂡a🂡Л🂡a",0);
        - returns [[1,3,5,7],[0,4,8],[2,6]]
    search("a","abcdabcd",0); //Search up to all matches
        - returns [[0,4]]
    search("a","abcdabcd",1);
        - returns [0]
    search("e","abcdabcd",1);
        - returns []
    search("a",[ ["a",1],["b",2],["c",3],["d",4],["a",5],["b",6],["c",7],["d",8],["e",9] ]);
        - returns [0,4]

   Search on different column; return Index values:
    search(3,[ ["a",1],["b",2],["c",3],["d",4],["a",5],["b",6],["c",7],["d",8],["e",3] ], 0, 1);
        - returns [0,8]

   Search on list of values:
    Return all matches per search vector element:
      search("abc",[ ["a",1],["b",2],["c",3],["d",4],["a",5],["b",6],["c",7],["d",8],["e",9] ], 0);
        - returns [[0,4],[1,5],[2,6]]

    Return first match per search vector element; special case return vector:
      search("abc",[ ["a",1],["b",2],["c",3],["d",4],["a",5],["b",6],["c",7],["d",8],["e",9] ], 1);
        - returns [0,1,2]

    Return first two matches per search vector element; vector of vectors:
      search("abce",[ ["a",1],["b",2],["c",3],["d",4],["a",5],["b",6],["c",7],["d",8],["e",9] ], 2);
        - returns [[0,4],[1,5],[2,6],[8]]

 */

static VectorType search(const str_utf8_wrapper& find, const str_utf8_wrapper& table,
                         unsigned int num_returns_per_match, EvaluationSession *session)
{
  VectorType returnvec(session);
  // Unicode glyph count for the length
  size_t findThisSize = find.get_utf8_strlen();
  size_t searchTableSize = table.get_utf8_strlen();
  for (size_t i = 0; i < findThisSize; ++i) {
    unsigned int matchCount = 0;
    VectorType resultvec(session);
    const auto ft = find[i];
    for (size_t j = 0; j < searchTableSize; ++j) {
      const auto st = table[j];
      if (!ft.empty() && !st.empty() && ft.get_utf8_char() == st.get_utf8_char()) {
        matchCount++;
        if (num_returns_per_match == 1) {
          returnvec.emplace_back(double(j));
          break;
        } else {
          resultvec.emplace_back(double(j));
        }
        if (num_returns_per_match > 1 && matchCount >= num_returns_per_match) {
          break;
        }
      }
    }
    if (num_returns_per_match == 0 || num_returns_per_match > 1) {
      returnvec.emplace_back(std::move(resultvec));
    }
  }
  return returnvec;
}

static VectorType search(const str_utf8_wrapper& find, const VectorType& table,
                         unsigned int num_returns_per_match, unsigned int index_col_num,
                         const Location& loc, EvaluationSession *session)
{
  VectorType returnvec(session);
  // Unicode glyph count for the length
  unsigned int findThisSize = find.get_utf8_strlen();
  unsigned int searchTableSize = table.size();
  for (size_t i = 0; i < findThisSize; ++i) {
    unsigned int matchCount = 0;
    VectorType resultvec(session);
    const auto ft = find[i];
    for (size_t j = 0; j < searchTableSize; ++j) {
      const auto& entryVec = table[j].toVector();
      if (entryVec.size() <= index_col_num) {
        LOG(message_group::Warning, loc, session->documentRoot(),
            "Invalid entry in search vector at index %1$d, required number of values in the entry: "
            "%2$d. Invalid entry: %3$s",
            j, (index_col_num + 1), table[j].toEchoStringNoThrow());
        return {session};
      }
      if (!ft.empty() &&
          ft.get_utf8_char() == entryVec[index_col_num].toStrUtf8Wrapper().get_utf8_char()) {
        matchCount++;
        if (num_returns_per_match == 1) {
          returnvec.emplace_back(double(j));
          break;
        } else {
          resultvec.emplace_back(double(j));
        }
        if (num_returns_per_match > 1 && matchCount >= num_returns_per_match) {
          break;
        }
      }
    }
    if (num_returns_per_match == 0 || num_returns_per_match > 1) {
      returnvec.emplace_back(std::move(resultvec));
    }
  }
  return returnvec;
}

Value builtin_search(Arguments arguments, const Location& loc)
{
  if (arguments.size() < 2 || arguments.size() > 4) {
    print_argCnt_warning("search", arguments.size(), "between 2 and 4", loc, arguments.documentRoot());
    return Value::undefined.clone();
  }

  const Value& findThis = arguments[0].value;
  const Value& searchTable = arguments[1].value;
  unsigned int num_returns_per_match =
    (arguments.size() > 2) ? (unsigned int)arguments[2]->toDouble() : 1;
  unsigned int index_col_num = (arguments.size() > 3) ? (unsigned int)arguments[3]->toDouble() : 0;

  VectorType returnvec(arguments.session());

  if (findThis.type() == Value::Type::NUMBER) {
    unsigned int matchCount = 0;
    size_t j = 0;
    for (const auto& search_element : searchTable.toVector()) {
      if ((index_col_num == 0 && (findThis == search_element).toBool()) ||
          (index_col_num < search_element.toVector().size() &&
           (findThis == search_element.toVector()[index_col_num]).toBool())) {
        returnvec.emplace_back(double(j));
        matchCount++;
        if (num_returns_per_match != 0 && matchCount >= num_returns_per_match) break;
      }
      ++j;
    }
  } else if (findThis.type() == Value::Type::STRING) {
    if (searchTable.type() == Value::Type::STRING) {
      returnvec = search(findThis.toStrUtf8Wrapper(), searchTable.toStrUtf8Wrapper(),
                         num_returns_per_match, arguments.session());
    } else {
      returnvec = search(findThis.toStrUtf8Wrapper(), searchTable.toVector(), num_returns_per_match,
                         index_col_num, loc, arguments.session());
    }
  } else if (findThis.type() == Value::Type::VECTOR) {
    const auto& findVec = findThis.toVector();
    for (const auto& find_value : findVec) {
      unsigned int matchCount = 0;
      VectorType resultvec(arguments.session());

      size_t j = 0;
      for (const auto& search_element : searchTable.toVector()) {
        if ((index_col_num == 0 && (find_value == search_element).toBool()) ||
            (index_col_num < search_element.toVector().size() &&
             (find_value == search_element.toVector()[index_col_num]).toBool())) {
          matchCount++;
          if (num_returns_per_match == 1) {
            returnvec.emplace_back(double(j));
            break;
          } else {
            resultvec.emplace_back(double(j));
          }
          if (num_returns_per_match > 1 && matchCount >= num_returns_per_match) break;
        }
        ++j;
      }
      if ((num_returns_per_match == 1 && matchCount == 0) || num_returns_per_match == 0 ||
          num_returns_per_match > 1) {
        returnvec.emplace_back(std::move(resultvec));
      }
    }
  } else {
    return Value::undefined.clone();
  }
  return std::move(returnvec);
}

Value builtin_version(Arguments arguments, const Location& /*loc*/)
{
  VectorType vec(arguments.session());
  vec.emplace_back(double(openscad_version_year));
  vec.emplace_back(double(openscad_version_month));
  if (openscad_has_day) {
    vec.emplace_back(double(openscad_version_day));
  }
  return std::move(vec);
}

Value builtin_version_num(Arguments arguments, const Location& loc)
{
  Value val =
    (arguments.size() == 0) ? builtin_version(std::move(arguments), loc) : std::move(arguments[0].value);
  double y, m, d;
  if (!val.getVec3(y, m, d, 0)) {
    return Value::undefined.clone();
  }
  return {y * 10000 + m * 100 + d};
}

Value builtin_parent_module(Arguments arguments, const Location& loc)
{
  double d;
  if (arguments.size() == 0) {
    d = 1;
  } else if (!check_arguments("parent_module", arguments, loc, {Value::Type::NUMBER})) {
    return Value::undefined.clone();
  } else {
    d = arguments[0]->toDouble();
  }

  int n = trunc(d);
  int s = UserModule::stack_size();
  if (n < 0) {
    LOG(message_group::Warning, loc, arguments.documentRoot(),
        "Negative parent module index (%1$d) not allowed", n);
    return Value::undefined.clone();
  }
  if (n >= s) {
    LOG(message_group::Warning, loc, arguments.documentRoot(),
        "Parent module index (%1$d) greater than the number of modules on the stack", n);
    return Value::undefined.clone();
  }
  return {UserModule::stack_element(s - 1 - n)};
}

Value builtin_norm(Arguments arguments, const Location& loc)
{
  if (!check_arguments("norm", arguments, loc, {Value::Type::VECTOR})) {
    return Value::undefined.clone();
  }
  double sum = 0;
  for (const auto& v : arguments[0]->toVector()) {
    if (v.type() == Value::Type::NUMBER) {
      double x = v.toDouble();
      sum += x * x;
    } else {
      LOG(message_group::Warning, loc, arguments.documentRoot(), "Incorrect arguments to norm()");
      return Value::undefined.clone();
    }
  }
  return {sqrt(sum)};
}

Value builtin_cross(Arguments arguments, const Location& loc)
{
  if (!check_arguments("cross", arguments, loc, {Value::Type::VECTOR, Value::Type::VECTOR})) {
    return Value::undefined.clone();
  }

  const auto& v0 = arguments[0]->toVector();
  const auto& v1 = arguments[1]->toVector();
  if ((v0.size() == 2) && (v1.size() == 2)) {
    return {v0[0].toDouble() * v1[1].toDouble() - v0[1].toDouble() * v1[0].toDouble()};
  }

  if ((v0.size() != 3) || (v1.size() != 3)) {
    LOG(message_group::Warning, loc, arguments.documentRoot(),
        "Invalid vector size of parameter for cross()");
    return Value::undefined.clone();
  }
  for (unsigned int a = 0; a < 3; ++a) {
    if ((v0[a].type() != Value::Type::NUMBER) || (v1[a].type() != Value::Type::NUMBER)) {
      LOG(message_group::Warning, loc, arguments.documentRoot(),
          "Invalid value in parameter vector for cross()");
      return Value::undefined.clone();
    }
    double d0 = v0[a].toDouble();
    double d1 = v1[a].toDouble();
    if (std::isnan(d0) || std::isnan(d1)) {
      LOG(message_group::Warning, loc, arguments.documentRoot(),
          "Invalid value (NaN) in parameter vector for cross()");
      return Value::undefined.clone();
    }
    if (std::isinf(d0) || std::isinf(d1)) {
      LOG(message_group::Warning, loc, arguments.documentRoot(),
          "Invalid value (INF) in parameter vector for cross()");
      return Value::undefined.clone();
    }
  }

  double x = v0[1].toDouble() * v1[2].toDouble() - v0[2].toDouble() * v1[1].toDouble();
  double y = v0[2].toDouble() * v1[0].toDouble() - v0[0].toDouble() * v1[2].toDouble();
  double z = v0[0].toDouble() * v1[1].toDouble() - v0[1].toDouble() * v1[0].toDouble();

  return VectorType(arguments.session(), x, y, z);
}

Value builtin_textmetrics(Arguments arguments, const Location& loc)
{
  auto *session = arguments.session();
  Parameters parameters =
    Parameters::parse(std::move(arguments), loc, {"text", "size", "font"},
                      {"direction", "language", "script", "halign", "valign", "spacing", "em"});
  parameters.set_caller("textmetrics");

  FreetypeRenderer::Params ftparams(parameters);
  ftparams.set_loc(loc);
  ftparams.set_documentPath(session->documentRoot());
  ftparams.detect_properties();

  FreetypeRenderer::TextMetrics metrics(ftparams);
  if (!metrics.ok) {
    return Value::undefined.clone();
  }

  // The bounding box, ascent/descent, and offset values will be zero
  // if the text consists of nothing but whitespace.
  VectorType bbox_pos(session);
  bbox_pos.reserve(2);
  bbox_pos.emplace_back(metrics.bbox_x);
  bbox_pos.emplace_back(metrics.bbox_y);

  VectorType bbox_dims(session);
  bbox_dims.reserve(2);
  bbox_dims.emplace_back(metrics.bbox_w);
  bbox_dims.emplace_back(metrics.bbox_h);

  VectorType offset(session);
  offset.reserve(2);
  offset.emplace_back(metrics.x_offset);
  offset.emplace_back(metrics.y_offset);

  // The advance values are valid whether or not the text
  // is whitespace.
  VectorType advance(session);
  advance.reserve(2);
  advance.emplace_back(metrics.advance_x);
  advance.emplace_back(metrics.advance_y);

  ObjectType text_metrics(session);
  text_metrics.set("position", std::move(bbox_pos));
  text_metrics.set("size", std::move(bbox_dims));
  text_metrics.set("ascent", metrics.ascent);
  text_metrics.set("descent", metrics.descent);
  text_metrics.set("offset", std::move(offset));
  text_metrics.set("advance", std::move(advance));
  return std::move(text_metrics);
}

Value builtin_fontmetrics(Arguments arguments, const Location& loc)
{
  auto *session = arguments.session();
  Parameters parameters = Parameters::parse(std::move(arguments), loc, {"size", "font", "em"});
  parameters.set_caller("fontmetrics");

  FreetypeRenderer::Params ftparams(parameters);
  ftparams.set_loc(loc);
  ftparams.set_documentPath(session->documentRoot());
  ftparams.detect_properties();

  FreetypeRenderer::FontMetrics metrics(ftparams);
  if (!metrics.ok) {
    return Value::undefined.clone();
  }

  ObjectType nominal(session);
  nominal.set("ascent", metrics.nominal_ascent);
  nominal.set("descent", metrics.nominal_descent);

  ObjectType max(session);
  max.set("ascent", metrics.max_ascent);
  max.set("descent", metrics.max_descent);

  ObjectType font(session);
  font.set("family", metrics.family_name);
  font.set("style", metrics.style_name);

  ObjectType font_metrics(session);
  font_metrics.set("nominal", nominal);
  font_metrics.set("max", max);
  font_metrics.set("interline", metrics.interline);
  font_metrics.set("font", font);

  return std::move(font_metrics);
}

/*
 * Geometry introspection: bbox(), centroid(), volume() and overhang().
 *
 * These builtin FUNCTIONS are meant to be called inside a user-module body so
 * that they can measure that module's children. A builtin function reaches the
 * enclosing module's children via context->user_module_children() -- the exact
 * mechanism the children() builtin uses (see src/core/control.cc). The child
 * ModuleInstantiations are materialized into a real node subtree (Children::
 * instantiate) and then eagerly geometry-evaluated with a scoped, throwaway
 * GeometryEvaluator. Running this sub-evaluation during the module-instantiation
 * (pass-1) build is safe: the geometry cache is content-addressed (keyed on the
 * structural Tree::getIdString dump, not on node index), so a scoped
 * sub-evaluation cannot corrupt the later main GeometryEvaluator pass.
 *
 * Each of bbox()/centroid() also accepts an explicit list of points (the pure,
 * geometry-free path -- kept working exactly as before), and bbox()/centroid()/
 * volume() additionally accept a single child-index argument to measure just
 * one child. All four are gated behind Feature::ExperimentalGeometryMetrics.
 */

// Reads a Value expected to be a non-empty list of 2D or 3D points into `points`.
// Returns true on success and sets `dims` to 2 or 3. Returns false (no throw) if
// the value is not a usable point list, so callers can emit a tailored message.
static bool read_point_list(const Value& value, std::vector<Vector3d>& points, int& dims)
{
  if (value.type() != Value::Type::VECTOR) return false;
  const VectorType& vec = value.toVector();
  if (vec.size() == 0) return false;
  dims = 0;
  for (const auto& element : vec) {
    double x, y, z = 0.0;
    if (element.getVec3(x, y, z)) {
      if (dims == 0) dims = 3;
      else if (dims != 3) return false;
    } else if (element.getVec2(x, y)) {
      if (dims == 0) dims = 2;
      else if (dims != 2) return false;
      z = 0.0;
    } else {
      return false;
    }
    points.emplace_back(x, y, z);
  }
  return !points.empty();
}

// Eagerly instantiate the enclosing user module's children (all of them, or the
// given `indices`) into a scratch node tree and geometry-evaluate them. Returns
// the resulting geometry (which may be null or empty). Returns nullptr without
// throwing when there is no enclosing module (no user_module_children) -- the
// caller distinguishes that case up front and emits its own tailored message.
static std::shared_ptr<const Geometry> evaluate_children_geometry(
  const std::shared_ptr<const Context>& context, const std::vector<size_t> *indices)
{
  const Children *children = context->user_module_children();
  if (!children) {
    return nullptr;
  }
  // A RootNode is a self-contained GroupNode (it owns its ModuleInstantiation),
  // which makes it a convenient anonymous container to instantiate children into.
  auto root = std::make_shared<RootNode>();
  if (indices) {
    children->instantiate(root, *indices);
  } else {
    children->instantiate(root);
  }
  Tree tree(root, context->documentRoot());
  GeometryEvaluator evaluator(tree);
  return evaluator.evaluateGeometry(*root, true);
}

// Convenience wrapper: throw a clear error when a metric function that measures
// children is not actually called inside a module, otherwise return the eagerly
// evaluated children geometry (possibly null/empty).
static std::shared_ptr<const Geometry> require_children_geometry(
  const char *name, const std::shared_ptr<const Context>& context,
  const std::vector<size_t> *indices)
{
  if (!context->user_module_children()) {
    throw EvaluationException(STR(boost::format(
      "%1$s(): must be called inside a module (to measure its children), or with an explicit "
      "list of points, e.g. %1$s([[0,0,0],[1,2,3]]).") % name));
  }
  return evaluate_children_geometry(context, indices);
}

// Parse a single NUMBER argument as a child index and validate it against the
// enclosing module's child count. Returns false (with a warning) if out of range.
static bool metric_child_index(const char *name, const std::shared_ptr<const Context>& context,
                               const Value& value, const Location& loc, size_t& out)
{
  const Children *children = context->user_module_children();
  const int i = static_cast<int>(value.toDouble());
  const size_t count = children ? children->size() : 0;
  if (i < 0 || static_cast<size_t>(i) >= count) {
    LOG(message_group::Warning, loc, context->documentRoot(),
        "%1$s(): child index %2$d out of range (module has %3$lu children).", name, i, count);
    return false;
  }
  out = static_cast<size_t>(i);
  return true;
}

// Returns the triangulated PolySet for a geometry, or nullptr if it cannot be
// represented as one (e.g. empty or a pure 2D Polygon2d).
static std::shared_ptr<const PolySet> geometry_as_triangle_polyset(
  const std::shared_ptr<const Geometry>& geom)
{
  if (!geom) return nullptr;
  auto ps = PolySetUtils::getGeometryAsPolySet(geom);
  if (!ps || ps->isEmpty()) return nullptr;
  if (ps->isTriangular()) return ps;
  return PolySetUtils::tessellate_faces(*ps);
}

// Signed mesh volume via the divergence theorem over triangles (each triangle,
// together with the origin, forms a tetrahedron whose signed volume is summed).
// Also accumulates the volume-weighted centroid. `ps` must be triangulated.
// Returns the signed total volume; `centroid` is only meaningful when |volume|>0.
static double polyset_volume_centroid(const PolySet& ps, Vector3d& centroid)
{
  double vol6 = 0.0;        // 6 * signed volume
  Vector3d acc = Vector3d::Zero();  // sum of (6*tetraVol) * (v0+v1+v2)
  for (const auto& face : ps.indices) {
    if (face.size() < 3) continue;
    const Vector3d& v0 = ps.vertices[face[0]];
    const Vector3d& v1 = ps.vertices[face[1]];
    const Vector3d& v2 = ps.vertices[face[2]];
    const double sv = v0.dot(v1.cross(v2));  // = 6 * signed tetra volume
    vol6 += sv;
    acc += sv * (v0 + v1 + v2);
  }
  const double volume = vol6 / 6.0;
  if (std::abs(vol6) > 1e-9) {
    // centroid = sum(tetraVol * tetraCentroid) / sum(tetraVol)
    //          = sum( (sv/6) * (v0+v1+v2)/4 ) / (vol6/6) = acc / (4*vol6)
    centroid = acc / (4.0 * vol6);
  }
  return volume;
}

// Fallback centroid for degenerate/flat/2D meshes: plain average of vertices.
static Vector3d polyset_vertex_average(const PolySet& ps)
{
  Vector3d sum = Vector3d::Zero();
  for (const auto& v : ps.vertices) sum += v;
  if (!ps.vertices.empty()) sum /= static_cast<double>(ps.vertices.size());
  return sum;
}

static Value make_bbox_value(EvaluationSession *session, const BoundingBox& bb)
{
  VectorType minv(session);
  VectorType maxv(session);
  for (int i = 0; i < 3; ++i) minv.emplace_back(bb.min()[i]);
  for (int i = 0; i < 3; ++i) maxv.emplace_back(bb.max()[i]);
  VectorType result(session);
  result.emplace_back(std::move(minv));
  result.emplace_back(std::move(maxv));
  return std::move(result);
}

Value builtin_bbox(const std::shared_ptr<const Context>& context, const FunctionCall *call)
{
  Arguments arguments{call->arguments, context};
  const Location& loc = call->location();
  auto *session = arguments.session();

  // Point-list form: bbox([[x,y,z], ...]) -> [[min],[max]] (2D or 3D).
  if (arguments.size() == 1 && arguments[0]->type() == Value::Type::VECTOR) {
    std::vector<Vector3d> points;
    int dims = 0;
    if (read_point_list(arguments[0].value, points, dims)) {
      Vector3d lo = points.front();
      Vector3d hi = points.front();
      for (const auto& p : points) {
        lo = lo.cwiseMin(p);
        hi = hi.cwiseMax(p);
      }
      VectorType minv(session);
      VectorType maxv(session);
      for (int i = 0; i < dims; ++i) {
        minv.emplace_back(lo[i]);
        maxv.emplace_back(hi[i]);
      }
      VectorType result(session);
      result.emplace_back(std::move(minv));
      result.emplace_back(std::move(maxv));
      return std::move(result);
    }
    throw EvaluationException(STR(boost::format(
      "bbox(): vector argument must be a non-empty list of 2D or 3D points.")));
  }

  // Geometry form: bbox() -> all children, bbox(i) -> child index i.
  std::vector<size_t> indices;
  const std::vector<size_t> *idxptr = nullptr;
  if (arguments.size() == 1) {
    if (arguments[0]->type() != Value::Type::NUMBER) {
      throw EvaluationException(STR(boost::format(
        "bbox(): argument must be a child index (number) or a list of points.")));
    }
    size_t idx = 0;
    if (!metric_child_index("bbox", context, arguments[0].value, loc, idx)) {
      return Value::undefined.clone();
    }
    indices.push_back(idx);
    idxptr = &indices;
  } else if (arguments.size() != 0) {
    throw EvaluationException(STR(boost::format(
      "bbox(): expected 0 arguments, 1 child index, or 1 list of points.")));
  }

  auto geom = require_children_geometry("bbox", context, idxptr);
  if (!geom || geom->isEmpty()) {
    LOG(message_group::Warning, loc, context->documentRoot(),
        "bbox(): children produced no geometry; returning undef.");
    return Value::undefined.clone();
  }
  return make_bbox_value(session, geom->getBoundingBox());
}

Value builtin_centroid(const std::shared_ptr<const Context>& context, const FunctionCall *call)
{
  Arguments arguments{call->arguments, context};
  const Location& loc = call->location();
  auto *session = arguments.session();

  // Point-list form: centroid([[x,y,z], ...]) -> average of the points.
  if (arguments.size() == 1 && arguments[0]->type() == Value::Type::VECTOR) {
    std::vector<Vector3d> points;
    int dims = 0;
    if (read_point_list(arguments[0].value, points, dims)) {
      Vector3d sum = Vector3d::Zero();
      for (const auto& p : points) sum += p;
      sum /= static_cast<double>(points.size());
      VectorType result(session);
      for (int i = 0; i < dims; ++i) result.emplace_back(sum[i]);
      return std::move(result);
    }
    throw EvaluationException(STR(boost::format(
      "centroid(): vector argument must be a non-empty list of 2D or 3D points.")));
  }

  // Geometry form: centroid() -> all children, centroid(i) -> child index i.
  std::vector<size_t> indices;
  const std::vector<size_t> *idxptr = nullptr;
  if (arguments.size() == 1) {
    if (arguments[0]->type() != Value::Type::NUMBER) {
      throw EvaluationException(STR(boost::format(
        "centroid(): argument must be a child index (number) or a list of points.")));
    }
    size_t idx = 0;
    if (!metric_child_index("centroid", context, arguments[0].value, loc, idx)) {
      return Value::undefined.clone();
    }
    indices.push_back(idx);
    idxptr = &indices;
  } else if (arguments.size() != 0) {
    throw EvaluationException(STR(boost::format(
      "centroid(): expected 0 arguments, 1 child index, or 1 list of points.")));
  }

  auto geom = require_children_geometry("centroid", context, idxptr);
  auto ps = geometry_as_triangle_polyset(geom);
  if (!ps) {
    LOG(message_group::Warning, loc, context->documentRoot(),
        "centroid(): children produced no measurable 3D geometry; returning undef.");
    return Value::undefined.clone();
  }
  Vector3d centroid = Vector3d::Zero();
  const double volume = polyset_volume_centroid(*ps, centroid);
  if (std::abs(volume) <= 1e-9) {
    // Flat/2D/degenerate: fall back to the vertex average.
    centroid = polyset_vertex_average(*ps);
  }
  VectorType result(session);
  for (int i = 0; i < 3; ++i) result.emplace_back(centroid[i]);
  return std::move(result);
}

Value builtin_volume(const std::shared_ptr<const Context>& context, const FunctionCall *call)
{
  Arguments arguments{call->arguments, context};
  const Location& loc = call->location();

  // volume() -> all children, volume(i) -> child index i.
  std::vector<size_t> indices;
  const std::vector<size_t> *idxptr = nullptr;
  if (arguments.size() == 1) {
    if (arguments[0]->type() != Value::Type::NUMBER) {
      throw EvaluationException(STR(boost::format(
        "volume(): argument must be a child index (number).")));
    }
    size_t idx = 0;
    if (!metric_child_index("volume", context, arguments[0].value, loc, idx)) {
      return Value::undefined.clone();
    }
    indices.push_back(idx);
    idxptr = &indices;
  } else if (arguments.size() != 0) {
    throw EvaluationException(STR(boost::format(
      "volume(): expected 0 arguments or 1 child index.")));
  }

  auto geom = require_children_geometry("volume", context, idxptr);
  auto ps = geometry_as_triangle_polyset(geom);
  if (!ps) {
    // No 3D solid (e.g. empty or purely 2D): report zero volume.
    return {0.0};
  }
  Vector3d centroid = Vector3d::Zero();
  const double volume = polyset_volume_centroid(*ps, centroid);
  return {std::abs(volume)};
}

// overhang(): worst downward-facing surface angle, measured from vertical (deg).
//
// Convention: for every triangle we compute the outward unit normal from its
// vertex winding. Downward-facing faces (normal.z < 0) are overhangs; the angle
// reported is measured FROM VERTICAL -- i.e. 0 deg for a vertical wall and 90
// deg for a fully horizontal downward face (a ceiling). FDM printers typically
// print angles up to ~45 deg without support. Faces resting on the print bed
// (all vertices within a small tolerance of the global minimum Z) are excluded
// heuristically, since they are supported by the bed rather than overhanging.
Value builtin_overhang(const std::shared_ptr<const Context>& context, const FunctionCall *call)
{
  Arguments arguments{call->arguments, context};
  const Location& loc = call->location();

  std::vector<size_t> indices;
  const std::vector<size_t> *idxptr = nullptr;
  if (arguments.size() == 1) {
    if (arguments[0]->type() != Value::Type::NUMBER) {
      throw EvaluationException(STR(boost::format(
        "overhang(): argument must be a child index (number).")));
    }
    size_t idx = 0;
    if (!metric_child_index("overhang", context, arguments[0].value, loc, idx)) {
      return Value::undefined.clone();
    }
    indices.push_back(idx);
    idxptr = &indices;
  } else if (arguments.size() != 0) {
    throw EvaluationException(STR(boost::format(
      "overhang(): expected 0 arguments or 1 child index.")));
  }

  auto geom = require_children_geometry("overhang", context, idxptr);
  auto ps = geometry_as_triangle_polyset(geom);
  if (!ps) {
    return {0.0};
  }

  // Global minimum Z, used to identify (and exclude) bed-contact faces.
  double minz = std::numeric_limits<double>::infinity();
  for (const auto& v : ps->vertices) minz = std::min(minz, v.z());
  constexpr double bed_eps = 1e-4;

  double max_angle = 0.0;
  for (const auto& face : ps->indices) {
    if (face.size() < 3) continue;
    const Vector3d& v0 = ps->vertices[face[0]];
    const Vector3d& v1 = ps->vertices[face[1]];
    const Vector3d& v2 = ps->vertices[face[2]];
    const Vector3d normal = (v1 - v0).cross(v2 - v0);
    const double len = normal.norm();
    if (len < 1e-12) continue;  // degenerate triangle
    const double nz = normal.z() / len;
    if (nz >= -1e-6) continue;  // not downward-facing
    // Skip bed-contact faces (all vertices resting on the build plate).
    if (v0.z() - minz < bed_eps && v1.z() - minz < bed_eps && v2.z() - minz < bed_eps) continue;
    const double angle = asin_degrees(std::min(1.0, -nz));  // from vertical
    max_angle = std::max(max_angle, angle);
  }
  return {max_angle};
}

// sharp_edges(): find the sharp edges of the children's mesh, grouped into
// features.
//
// Motivation: overhang() answers "will this print?", but says nothing about
// "will this feel right in the hand, and will it survive?". A 90-degree convex
// edge is a UX defect (it is what you notice when you grip a case) and a
// 90-degree concave edge is a stress riser (it is where a boss snaps off its
// floor). Neither is visible to a parameter assert(), because both are produced
// by the BOOLEAN, not by any single number in the model.
//
// Method: weld the triangulated mesh by position, find every manifold edge
// (shared by exactly two triangles), and measure the angle between the two face
// normals. That angle is 0 for a flat surface and 90 for a square corner --
// i.e. it is the amount the surface TURNS across the edge, which is exactly the
// quantity a chamfer or fillet is there to reduce. An edge is convex when the
// far triangle's apex lies behind the near triangle's plane, concave otherwise.
//
// Raw edges are useless to a human -- a tessellated box rim is hundreds of them
// -- so connected runs of edges of the same convexity are unioned into a single
// FEATURE, and one record per feature is returned. That turns "4068 sharp edges"
// into "a 501 mm convex ring at z = 18.3", which names a thing you can go and
// fix.
//
// Each record is a plain vector (not an object: object member access is behind
// a separate experimental feature, and bbox() already sets the precedent that
// these functions return positional data):
//
//   [ length, angle, angle_max, convex, [min], [max], [centre] ]
//       0       1        2         3      4      5        6
//
//   length     total mm of edge in the feature
//   angle      length-weighted mean turn angle, degrees
//   angle_max  worst turn angle in the feature, degrees
//   convex     true for an outside corner, false for an inside corner
//   min/max    bounding box of the feature
//   centre     length-weighted mean of the edge midpoints
//
// Records are sorted longest-first, so record 0 is the biggest offender.
// See libraries/edges.scad for the modules that FIX what this finds.
//
// `min_face` is what keeps this honest. Chamfers and coves are usually built as
// a stack of hulled slices, and that construction leaves sub-micron slivers of
// surface behind at the ends of the stack. Those slivers meet their neighbours
// at 90 degrees, so a naive dihedral scan reports the FIX as a fresh defect.
// An edge only means anything if there is real surface either side of it, so an
// edge whose neighbouring triangle is thinner than `min_face` (measured
// perpendicular to the edge, 2*area/length) is treated as mesh construction
// noise and skipped. The default, 0.05 mm, is a quarter of a typical FDM layer:
// well below anything a printer can render or a finger can feel, and far above
// float32 STL round-off.
//
// The guard's one cost: a flat cap triangulated as a FAN has two sliver
// triangles either side of the fan's apex, and they are coplanar with the rest
// of the cap, so the surface really is wide there even though those two
// triangles are not. Their two rim edges get skipped, which nicks a few percent
// off a circular rim's reported length and can split it into two features. The
// alternative -- no guard -- makes every chamfer and fillet report itself as a
// fresh defect, which is very much worse.
namespace {

// A welded vertex key: the position quantised to SHARP_WELD_Q mm. Meshes coming
// back from the Manifold backend are already indexed, but STL-ish round trips
// and coplanar booleans can leave two indices at the same point; welding makes
// the edge match by POSITION so a feature does not silently split in two.
constexpr double SHARP_WELD_Q = 1e-5;

struct SharpEdgeRec {
  size_t a, b;        // welded vertex indices, a < b
  double length;
  double angle;       // degrees the surface turns across the edge
  bool convex;
};

// Disjoint-set over (welded vertex, convexity) slots, used to union runs of
// sharp edges into features. Convexity is part of the key so a convex rim and
// the concave groove it sits next to never merge into one meaningless blob.
class SharpUnionFind {
public:
  explicit SharpUnionFind(size_t n) : parent_(n) {
    for (size_t i = 0; i < n; ++i) parent_[i] = i;
  }
  size_t find(size_t x) {
    while (parent_[x] != x) x = parent_[x] = parent_[parent_[x]];
    return x;
  }
  void unite(size_t x, size_t y) {
    const size_t rx = find(x), ry = find(y);
    if (rx != ry) parent_[rx] = ry;
  }
private:
  std::vector<size_t> parent_;
};

struct SharpFeature {
  double length = 0.0;
  double angle_weighted = 0.0;   // sum(angle * length), divided out at the end
  double angle_max = 0.0;
  bool convex = false;
  Vector3d lo = Vector3d::Constant(std::numeric_limits<double>::infinity());
  Vector3d hi = Vector3d::Constant(-std::numeric_limits<double>::infinity());
  Vector3d centre_weighted = Vector3d::Zero();  // sum(midpoint * length)
};

}  // namespace

Value builtin_sharp_edges(const std::shared_ptr<const Context>& context, const FunctionCall *call)
{
  Arguments arguments{call->arguments, context};
  const Location& loc = call->location();
  auto *session = arguments.session();

  double min_angle = 30.0;
  double min_length = 0.5;
  double min_face = 0.05;
  std::string kind = "any";
  bool has_child = false;
  size_t child_index = 0;
  {
    Parameters parameters = Parameters::parse(std::move(arguments), loc, {},
                                              {"angle", "kind", "min_length",
                                               "min_face", "child"});
    parameters.set_caller("sharp_edges");
    min_angle = parameters.get("angle", 30.0);
    min_length = parameters.get("min_length", 0.5);
    min_face = parameters.get("min_face", 0.05);
    kind = parameters.get("kind", std::string("any"));
    if (kind != "any" && kind != "convex" && kind != "concave") {
      throw EvaluationException(STR(boost::format(
        "sharp_edges(): kind must be \"any\", \"convex\" or \"concave\", got \"%1%\".") % kind));
    }
    const auto child = parameters.lookup("child");
    if (child && !child->isUndefined()) {
      if (child->type() != Value::Type::NUMBER) {
        throw EvaluationException(STR(boost::format(
          "sharp_edges(): child must be a child index (number).")));
      }
      if (!metric_child_index("sharp_edges", context, *child, loc, child_index)) {
        return Value::undefined.clone();
      }
      has_child = true;
    }
  }

  std::vector<size_t> indices;
  const std::vector<size_t> *idxptr = nullptr;
  if (has_child) {
    indices.push_back(child_index);
    idxptr = &indices;
  }

  auto geom = require_children_geometry("sharp_edges", context, idxptr);
  auto ps = geometry_as_triangle_polyset(geom);
  if (!ps) {
    // No 3D solid (empty, or purely 2D): no edges to measure. An empty list is
    // the honest answer and keeps `for (e = sharp_edges())` working.
    VectorType empty(session);
    return std::move(empty);
  }

  // ---- weld vertices by quantised position ---------------------------------
  std::map<std::array<int64_t, 3>, size_t> weld_map;
  std::vector<size_t> weld_of(ps->vertices.size());
  std::vector<Vector3d> wverts;
  wverts.reserve(ps->vertices.size());
  for (size_t i = 0; i < ps->vertices.size(); ++i) {
    const Vector3d& v = ps->vertices[i];
    const std::array<int64_t, 3> key{
      static_cast<int64_t>(std::llround(v.x() / SHARP_WELD_Q)),
      static_cast<int64_t>(std::llround(v.y() / SHARP_WELD_Q)),
      static_cast<int64_t>(std::llround(v.z() / SHARP_WELD_Q))};
    auto it = weld_map.find(key);
    if (it == weld_map.end()) {
      weld_map.emplace(key, wverts.size());
      weld_of[i] = wverts.size();
      wverts.push_back(v);
    } else {
      weld_of[i] = it->second;
    }
  }

  // ---- collect half-edges, each tagged with its face and opposite vertex ----
  struct HalfEdge { size_t a, b, face, opp; };
  std::vector<HalfEdge> halves;
  std::vector<Vector3d> normals(ps->indices.size(), Vector3d::Zero());
  // Twice the triangle area, kept so an edge can ask how WIDE its neighbouring
  // faces are: width perpendicular to the edge = 2*area / edge length.
  std::vector<double> area2(ps->indices.size(), 0.0);
  halves.reserve(ps->indices.size() * 3);
  for (size_t f = 0; f < ps->indices.size(); ++f) {
    const auto& face = ps->indices[f];
    if (face.size() < 3) continue;
    const size_t i0 = weld_of[face[0]], i1 = weld_of[face[1]], i2 = weld_of[face[2]];
    const Vector3d n = (wverts[i1] - wverts[i0]).cross(wverts[i2] - wverts[i0]);
    const double len = n.norm();
    if (len < 1e-12) continue;  // degenerate triangle: no meaningful normal
    normals[f] = n / len;
    area2[f] = len;
    const size_t tri[3] = {i0, i1, i2};
    for (int k = 0; k < 3; ++k) {
      size_t a = tri[k], b = tri[(k + 1) % 3];
      const size_t opp = tri[(k + 2) % 3];
      if (a == b) continue;  // collapsed edge
      if (a > b) std::swap(a, b);
      halves.push_back({a, b, f, opp});
    }
  }
  std::sort(halves.begin(), halves.end(), [](const HalfEdge& x, const HalfEdge& y) {
    return x.a != y.a ? x.a < y.a : x.b < y.b;
  });

  // ---- measure every manifold edge -----------------------------------------
  std::vector<SharpEdgeRec> sharp;
  for (size_t i = 0; i < halves.size(); ) {
    size_t j = i + 1;
    while (j < halves.size() && halves[j].a == halves[i].a && halves[j].b == halves[i].b) ++j;
    const size_t run = j - i;
    i = j;
    // run == 1 is a boundary edge and run > 2 is non-manifold; neither has a
    // single dihedral angle. Both are real defects, but they belong to the
    // manifoldness check, not here.
    if (run != 2) continue;
    const HalfEdge& h1 = halves[j - 2];
    const HalfEdge& h2 = halves[j - 1];
    const Vector3d& n1 = normals[h1.face];
    const Vector3d& n2 = normals[h2.face];
    if (n1.isZero() || n2.isZero()) continue;
    const double dot = std::max(-1.0, std::min(1.0, n1.dot(n2)));
    const double angle = acos_degrees(dot);
    if (angle < min_angle) continue;
    // Convex when the far triangle's apex sits BEHIND the near triangle's
    // plane -- material folds away from the outward normal, i.e. an outside
    // corner. Concave (an inside corner) when it sits in front.
    const bool convex = n1.dot(wverts[h2.opp] - wverts[h1.a]) < 0.0;
    if (kind == "convex" && !convex) continue;
    if (kind == "concave" && convex) continue;
    const double elen = (wverts[h1.b] - wverts[h1.a]).norm();
    if (elen <= 0.0) continue;
    // Sliver guard: an edge whose neighbouring surface is narrower than
    // min_face is construction noise, not an edge anyone can feel.
    if (min_face > 0.0 &&
        (area2[h1.face] / elen < min_face || area2[h2.face] / elen < min_face)) continue;
    sharp.push_back({h1.a, h1.b, elen, angle, convex});
  }

  // ---- union connected runs of the same convexity into features ------------
  SharpUnionFind uf(wverts.size() * 2);
  auto slot = [](size_t v, bool convex) { return v * 2 + (convex ? 1u : 0u); };
  for (const auto& e : sharp) uf.unite(slot(e.a, e.convex), slot(e.b, e.convex));

  std::map<size_t, SharpFeature> features;
  for (const auto& e : sharp) {
    SharpFeature& fe = features[uf.find(slot(e.a, e.convex))];
    fe.convex = e.convex;
    fe.length += e.length;
    fe.angle_weighted += e.angle * e.length;
    fe.angle_max = std::max(fe.angle_max, e.angle);
    const Vector3d& pa = wverts[e.a];
    const Vector3d& pb = wverts[e.b];
    fe.lo = fe.lo.cwiseMin(pa).cwiseMin(pb);
    fe.hi = fe.hi.cwiseMax(pa).cwiseMax(pb);
    fe.centre_weighted += (0.5 * (pa + pb)) * e.length;
  }

  std::vector<const SharpFeature *> ordered;
  ordered.reserve(features.size());
  for (const auto& kv : features) {
    if (kv.second.length >= min_length) ordered.push_back(&kv.second);
  }
  std::sort(ordered.begin(), ordered.end(),
            [](const SharpFeature *x, const SharpFeature *y) { return x->length > y->length; });

  // ---- emit ----------------------------------------------------------------
  VectorType result(session);
  result.reserve(ordered.size());
  for (const SharpFeature *fe : ordered) {
    const double inv = (fe->length > 0.0) ? 1.0 / fe->length : 0.0;
    const Vector3d centre = fe->centre_weighted * inv;
    VectorType lo(session), hi(session), ctr(session);
    for (int i = 0; i < 3; ++i) lo.emplace_back(fe->lo[i]);
    for (int i = 0; i < 3; ++i) hi.emplace_back(fe->hi[i]);
    for (int i = 0; i < 3; ++i) ctr.emplace_back(centre[i]);
    VectorType rec(session);
    rec.reserve(7);
    rec.emplace_back(fe->length);
    rec.emplace_back(fe->angle_weighted * inv);
    rec.emplace_back(fe->angle_max);
    rec.emplace_back(fe->convex);
    rec.emplace_back(std::move(lo));
    rec.emplace_back(std::move(hi));
    rec.emplace_back(std::move(ctr));
    result.emplace_back(std::move(rec));
  }
  return std::move(result);
}

Value builtin_is_undef(const std::shared_ptr<const Context>& context, const FunctionCall *call)
{
  if (call->arguments.size() != 1) {
    print_argCnt_warning("is_undef", call->arguments.size(), "1", call->location(),
                         context->documentRoot());
    return Value::undefined.clone();
  }
  if (auto lookup = std::dynamic_pointer_cast<Lookup>(call->arguments[0]->getExpr())) {
    auto result = context->try_lookup_variable(lookup->get_name());
    return !result || result->isUndefined();
  } else {
    return call->arguments[0]->getExpr()->evaluate(context).isUndefined();
  }
}

Value builtin_is_list(Arguments arguments, const Location& loc)
{
  if (!check_arguments("is_list", arguments, loc, 1)) {
    return Value::undefined.clone();
  }
  return {arguments[0]->isDefinedAs(Value::Type::VECTOR)};
}

Value builtin_is_num(Arguments arguments, const Location& loc)
{
  if (!check_arguments("is_num", arguments, loc, 1)) {
    return Value::undefined.clone();
  }
  return {arguments[0]->isDefinedAs(Value::Type::NUMBER) && !std::isnan(arguments[0]->toDouble())};
}

Value builtin_is_bool(Arguments arguments, const Location& loc)
{
  if (!check_arguments("is_bool", arguments, loc, 1)) {
    return Value::undefined.clone();
  }
  return {arguments[0]->isDefinedAs(Value::Type::BOOL)};
}

Value builtin_is_string(Arguments arguments, const Location& loc)
{
  if (!check_arguments("is_string", arguments, loc, 1)) {
    return Value::undefined.clone();
  }
  return {arguments[0]->isDefinedAs(Value::Type::STRING)};
}

Value builtin_is_function(Arguments arguments, const Location& loc)
{
  if (!check_arguments("is_function", arguments, loc, 1)) {
    return Value::undefined.clone();
  }
  return {arguments[0]->isDefinedAs(Value::Type::FUNCTION)};
}

Value builtin_is_object(Arguments arguments, const Location& loc)
{
  if (!check_arguments("is_object", arguments, loc, 1)) {
    return Value::undefined.clone();
  }
  return {arguments[0]->isDefinedAs(Value::Type::OBJECT)};
}

Value builtin_import(Arguments arguments, const Location& loc)
{
  auto session = arguments.session();
  const Parameters parameters = Parameters::parse(std::move(arguments), loc, {}, {"file"});
  std::string raw_filename = parameters.get("file", "");
  std::string file =
    lookup_file(raw_filename, loc.filePath().parent_path().string(), parameters.documentRoot());
  return import_json(file, session, loc);
}

void register_builtin_functions()
{
  Builtins::init("abs", new BuiltinFunction(&builtin_abs),
                 {
                   "abs(number) -> number",
                 });

  Builtins::init("sign", new BuiltinFunction(&builtin_sign),
                 {
                   "sign(number) -> -1, 0 or 1",
                 });

  Builtins::init("rands", new BuiltinFunction(&builtin_rands),
                 {
                   "rands(min, max, num_results) -> vector",
                   "rands(min, max, num_results, seed) -> vector",
                 });

  Builtins::init("min", new BuiltinFunction(&builtin_min),
                 {
                   "min(number, number, ...) -> number",
                   "min(vector) -> number",
                 });

  Builtins::init("max", new BuiltinFunction(&builtin_max),
                 {
                   "max(number, number, ...) -> number",
                   "max(vector) -> number",
                 });

  Builtins::init("sin", new BuiltinFunction(&builtin_sin),
                 {
                   "sin(degrees) -> number",
                 });

  Builtins::init("cos", new BuiltinFunction(&builtin_cos),
                 {
                   "cos(degrees) -> number",
                 });

  Builtins::init("asin", new BuiltinFunction(&builtin_asin),
                 {
                   "asin(number) -> degrees",
                 });

  Builtins::init("acos", new BuiltinFunction(&builtin_acos),
                 {
                   "acos(number) -> degrees",
                 });

  Builtins::init("tan", new BuiltinFunction(&builtin_tan),
                 {
                   "tan(degrees) -> number",
                 });

  Builtins::init("atan", new BuiltinFunction(&builtin_atan),
                 {
                   "atan(number) -> degrees",
                 });

  Builtins::init("atan2", new BuiltinFunction(&builtin_atan2),
                 {
                   "atan2(number, number) -> degrees",
                 });

  Builtins::init("round", new BuiltinFunction(&builtin_round),
                 {
                   "round(number) -> number",
                 });

  Builtins::init("ceil", new BuiltinFunction(&builtin_ceil),
                 {
                   "ceil(number) -> number",
                 });

  Builtins::init("floor", new BuiltinFunction(&builtin_floor),
                 {
                   "floor(number) -> number",
                 });

  Builtins::init("pow", new BuiltinFunction(&builtin_pow),
                 {
                   "pow(base, exponent) -> number",
                 });

  Builtins::init("sqrt", new BuiltinFunction(&builtin_sqrt),
                 {
                   "sqrt(number) -> number",
                 });

  Builtins::init("exp", new BuiltinFunction(&builtin_exp),
                 {
                   "exp(number) -> number",
                 });

  Builtins::init("len", new BuiltinFunction(&builtin_length),
                 {
                   "len(string) -> number",
                   "len(vector) -> number",
                 });

  Builtins::init("log", new BuiltinFunction(&builtin_log),
                 {
                   "log(number) -> number",
                 });

  Builtins::init("ln", new BuiltinFunction(&builtin_ln),
                 {
                   "ln(number) -> number",
                 });

  Builtins::init("str", new BuiltinFunction(&builtin_str),
                 {
                   "str(number or string, ...) -> string",
                 });

  Builtins::init("chr", new BuiltinFunction(&builtin_chr),
                 {
                   "chr(number) -> string",
                   "chr(vector) -> string",
                   "chr(range) -> string",
                 });

  Builtins::init("textmetrics",
                 new BuiltinFunction(&builtin_textmetrics, &Feature::ExperimentalTextMetricsFunctions),
                 {
                   "textmetrics(text, size, font, direction, language, script, halign, valign, spacing, "
                   "em) -> object",
                 });

  Builtins::init("fontmetrics",
                 new BuiltinFunction(&builtin_fontmetrics, &Feature::ExperimentalTextMetricsFunctions),
                 {
                   "fontmetrics(size, font, em) -> object",
                 });

  Builtins::init("ord", new BuiltinFunction(&builtin_ord),
                 {
                   "ord(string) -> number",
                 });

  Builtins::init("concat", new BuiltinFunction(&builtin_concat),
                 {
                   "concat(number or string or vector, ...) -> vector",
                 });

  Builtins::init("lookup", new BuiltinFunction(&builtin_lookup),
                 {
                   "lookup(key, <key,value> vector) -> value",
                 });

  Builtins::init(
    "search", new BuiltinFunction(&builtin_search),
    {
      "search(string , string or vector [, num_returns_per_match [, index_col_num ] ] ) -> vector",
    });

  Builtins::init("version", new BuiltinFunction(&builtin_version),
                 {
                   "version() -> vector",
                 });

  Builtins::init("version_num", new BuiltinFunction(&builtin_version_num),
                 {
                   "version_num() -> number",
                 });

  Builtins::init("norm", new BuiltinFunction(&builtin_norm),
                 {
                   "norm(vector) -> number",
                 });

  Builtins::init("cross", new BuiltinFunction(&builtin_cross),
                 {
                   "cross(vector, vector) -> vector",
                 });

  Builtins::init("parent_module", new BuiltinFunction(&builtin_parent_module),
                 {
                   "parent_module(number) -> string",
                 });

  Builtins::init("is_undef", new BuiltinFunction(&builtin_is_undef),
                 {
                   "is_undef(arg) -> boolean",
                 });

  Builtins::init("is_list", new BuiltinFunction(&builtin_is_list),
                 {
                   "is_list(arg) -> boolean",
                 });

  Builtins::init("is_num", new BuiltinFunction(&builtin_is_num),
                 {
                   "is_num(arg) -> boolean",
                 });

  Builtins::init("is_bool", new BuiltinFunction(&builtin_is_bool),
                 {
                   "is_bool(arg) -> boolean",
                 });

  Builtins::init("is_string", new BuiltinFunction(&builtin_is_string),
                 {
                   "is_string(arg) -> boolean",
                 });

  Builtins::init("is_function", new BuiltinFunction(&builtin_is_function),
                 {
                   "is_function(arg) -> boolean",
                 });

  Builtins::init("is_object",
                 new BuiltinFunction(&builtin_is_object, &Feature::ExperimentalTextMetricsFunctions),
                 {
                   "is_object(arg) -> boolean",
                 });

  Builtins::init("object", new BuiltinFunction(&builtin_object, &Feature::ExperimentalObjectFunction),
                 {
                   "object([ object, ] [ key-val list, ] key=value, ...) -> object",
                 });

  Builtins::init("has_key", new BuiltinFunction(&builtin_has_key, &Feature::ExperimentalObjectFunction),
                 {
                   "has_key(object, key) -> boolean",
                 });

  Builtins::init("import", new BuiltinFunction(&builtin_import, &Feature::ExperimentalImportFunction),
                 {
                   "import(file) -> object",
                 });

  Builtins::init("bbox", new BuiltinFunction(&builtin_bbox, &Feature::ExperimentalGeometryMetrics),
                 {
                   "bbox() -> [[min], [max]] of the enclosing module's children",
                   "bbox(i) -> [[min], [max]] of child index i",
                   "bbox(point list) -> [[min], [max]]",
                 });

  Builtins::init("centroid",
                 new BuiltinFunction(&builtin_centroid, &Feature::ExperimentalGeometryMetrics),
                 {
                   "centroid() -> [cx, cy, cz] of the enclosing module's children",
                   "centroid(i) -> [cx, cy, cz] of child index i",
                   "centroid(point list) -> [cx, cy, cz]",
                 });

  Builtins::init("volume", new BuiltinFunction(&builtin_volume, &Feature::ExperimentalGeometryMetrics),
                 {
                   "volume() -> solid volume of the enclosing module's children",
                   "volume(i) -> solid volume of child index i",
                 });

  Builtins::init("overhang",
                 new BuiltinFunction(&builtin_overhang, &Feature::ExperimentalGeometryMetrics),
                 {
                   "overhang() -> worst downward-facing angle from vertical (degrees)",
                   "overhang(i) -> worst downward-facing angle of child index i",
                 });

  Builtins::init("sharp_edges",
                 new BuiltinFunction(&builtin_sharp_edges, &Feature::ExperimentalGeometryMetrics),
                 {
                   "sharp_edges([angle], [kind], [min_length], [min_face], [child]) -> list of "
                   "sharp-edge "
                   "features, longest first; each is "
                   "[length, angle, angle_max, convex, [min], [max], [centre]]",
                 });
}
