#include "semana/Functions.hpp"
//---------------------------------------------------------------------------
// (c) 2023 Thomas Neumann
//---------------------------------------------------------------------------
using namespace std;
//---------------------------------------------------------------------------
namespace saneql {
//---------------------------------------------------------------------------
namespace {
//---------------------------------------------------------------------------
using Builtin = Functions::Builtin;
using TypeCategory = Functions::TypeCategory;
//---------------------------------------------------------------------------
/// Functions that are defined on all scalar types
static const Functions scalarFunctions(nullptr,
                                       {
                                          // list of functions
                                          {"asc", {Builtin::Asc, {}}}, // change order to ascending
                                          {"desc", {Builtin::Desc, {}}}, // change order to descending
                                          {"collate", {Builtin::Desc, {{"collate", TypeCategory::Symbol}}}}, // change collate
                                          {"is", {Builtin::Is, {{"other", TypeCategory::Scalar}}}} // check for equality handling NULL
                                       });
//---------------------------------------------------------------------------
/// Functions that are defined on text types
static const Functions textFunctions(&scalarFunctions,
                                     {
                                        // list of functions
                                        {"like", {Builtin::Like, {{"pattern", Type::getText()}}}} // a like predicate
                                     });
//---------------------------------------------------------------------------
}
//---------------------------------------------------------------------------
/// Functions that are defined on tables
const Functions Functions::table(nullptr,
                                 {
                                    // list of functions
                                    {"filter", {Builtin::Filter, {{"condition", TypeCategory::Expression}}}}, // filter tuples
                                    {"join", {Builtin::Join, {{"table", TypeCategory::Table}, {"on", TypeCategory::Expression}, {"type", TypeCategory::Symbol, true}}}}, // join tables
                                    {"groupby", {Builtin::GroupBy, {{"groups", TypeCategory::ExpressionList}, {"aggregates", TypeCategory::ExpressionList, true}, {"type", TypeCategory::Symbol, true}, {"sets", TypeCategory::ExpressionList, true}}}}, // aggregate
                                    {"aggregate", {Builtin::Aggregate, {{"aggregate", TypeCategory::Expression}}}}, // aggregate to scalar
                                    {"orderby", {Builtin::OrderBy, {{"expressions", TypeCategory::ExpressionList}, {"limit", TypeCategory::Expression, true}, {"offset", TypeCategory::Expression, true}}}}, // order entries
                                    {"map", {Builtin::Map, {{"expressions", TypeCategory::ExpressionList}}}}, // compute new columns
                                    {"project", {Builtin::Project, {{"expressions", TypeCategory::ExpressionList}}}}, // restrict the result columns
                                 });
//---------------------------------------------------------------------------
/// The free functions
const Functions Functions::freeFunctions(nullptr,
                                         {
                                            // list of functions
                                            {"count", {Builtin::AggCount, {{"value", TypeCategory::Expression, true}}}}, // aggregate
                                            {"sum", {Builtin::AggSum, {{"value", TypeCategory::Expression}}}}, // aggregate
                                            {"avg", {Builtin::AggAvg, {{"value", TypeCategory::Expression}}}}, // aggregate
                                            {"min", {Builtin::AggMin, {{"value", TypeCategory::Expression}}}}, // aggregate
                                            {"max", {Builtin::AggMax, {{"value", TypeCategory::Expression}}}}, // aggregate
                                            {"table", {Builtin::Table, {{"values", TypeCategory::ExpressionList}}}}, // table construction
                                         });
//---------------------------------------------------------------------------
Functions::Functions(const Functions* parent, std::initializer_list<std::pair<std::string, Signature>> signatures)
   : parent(parent), functions(signatures.begin(), signatures.end())
// Constructor
{
}
//---------------------------------------------------------------------------
Functions::~Functions()
// Destructor
{
}
//---------------------------------------------------------------------------
const Functions::Signature* Functions::lookup(const std::string& name) const
// Find a function
{
   for (auto iter = this; iter; iter = iter->parent) {
      auto iter2 = iter->functions.find(name);
      if (iter2 != iter->functions.end()) return &(iter2->second);
   }
   return nullptr;
}
//---------------------------------------------------------------------------
const Functions* Functions::getFunctions(Type type)
// Find the associated functions
{
   switch (type.getType()) {
      case Type::Unknown: return &scalarFunctions;
      case Type::Bool: return &scalarFunctions;
      case Type::Integer: return &scalarFunctions;
      case Type::Decimal: return &scalarFunctions;
      case Type::Char: return &textFunctions;
      case Type::Varchar: return &textFunctions;
      case Type::Text: return &textFunctions;
      case Type::Date: return &scalarFunctions;
      case Type::Interval: return &scalarFunctions;
   }
   __builtin_unreachable();
}
//---------------------------------------------------------------------------
}
//---------------------------------------------------------------------------
