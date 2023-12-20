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
                                          {"is", {Builtin::Is, {{"other", TypeCategory::Scalar}}}}, // check for equality handling NULL
                                          {"between", {Builtin::Between, {{"lower", TypeCategory::Scalar}, {"upper", TypeCategory::Scalar}}}}, // range check
                                          {"in", {Builtin::In, {{"values", TypeCategory::ExpressionList}}}} // check against a list of values
                                       });
//---------------------------------------------------------------------------
/// Functions that are defined on text types
static const Functions textFunctions(&scalarFunctions,
                                     {
                                        // list of functions
                                        {"like", {Builtin::Like, {{"pattern", Type::getText()}}}}, // a like predicate
                                        {"substr", {Builtin::Substr, {{"from", Type::getInteger(), true}, {"for", Type::getInteger(), true}}}} // a like predicate
                                     });
//---------------------------------------------------------------------------
/// Functions that are defined on date values
static const Functions dateFunctions(&scalarFunctions,
                                     {
                                        // list of functions
                                        {"extract", {Builtin::Extract, {{"part", TypeCategory::Symbol}}}} // extract part of a date
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
                                    {"distinct", {Builtin::Distinct, {}}}, // remove duplicates
                                    {"orderby", {Builtin::OrderBy, {{"expressions", TypeCategory::ExpressionList}, {"limit", TypeCategory::Expression, true}, {"offset", TypeCategory::Expression, true}}}}, // order entries
                                    {"map", {Builtin::Map, {{"expressions", TypeCategory::ExpressionList}}}}, // compute new columns
                                    {"project", {Builtin::Project, {{"expressions", TypeCategory::ExpressionList}}}}, // restrict the result columns
                                    {"projectout", {Builtin::ProjectOut, {{"columns", TypeCategory::ExpressionList}}}}, // restrict the result columns
                                    {"as", {Builtin::As, {{"name", TypeCategory::Symbol}}}}, // rename the scope
                                    {"alias", {Builtin::Alias, {{"name", TypeCategory::Symbol}}}}, // provide alias name for columns
                                    {"union", {Builtin::Union, {{"table", TypeCategory::Table}, {"all", TypeCategory::Symbol, true}}}}, // set union
                                    {"except", {Builtin::Except, {{"table", TypeCategory::Table}, {"all", TypeCategory::Symbol, true}}}}, // set except
                                    {"intersect", {Builtin::Intersect, {{"table", TypeCategory::Table}, {"all", TypeCategory::Symbol, true}}}}, // set intersect
                                    {"window", {Builtin::Window, {{"expressions", TypeCategory::ExpressionList}, {"partitionby", TypeCategory::ExpressionList, true}, {"orderby", TypeCategory::ExpressionList, true}, {"framebegin", TypeCategory::Expression, true}, {"framend", TypeCategory::Expression, true}, {"frametype", TypeCategory::Symbol, true}}}}, // window computation
                                 });
//---------------------------------------------------------------------------
/// The free functions
const Functions Functions::freeFunctions(nullptr,
                                         {
                                            // list of functions
                                            {"count", {Builtin::AggCount, {{"value", TypeCategory::Expression, true}, {"distinct", TypeCategory::Symbol, true}}}}, // aggregate
                                            {"sum", {Builtin::AggSum, {{"value", TypeCategory::Expression}, {"distinct", TypeCategory::Symbol, true}}}}, // aggregate
                                            {"avg", {Builtin::AggAvg, {{"value", TypeCategory::Expression}, {"distinct", TypeCategory::Symbol, true}}}}, // aggregate
                                            {"min", {Builtin::AggMin, {{"value", TypeCategory::Expression}}}}, // aggregate
                                            {"max", {Builtin::AggMax, {{"value", TypeCategory::Expression}}}}, // aggregate
                                            {"row_number", {Builtin::WindowRowNumber, {}}}, // windw function
                                            {"rank", {Builtin::WindowRank, {{"value", TypeCategory::Expression }}}}, // windw function
                                            {"dense_rank", {Builtin::WindowDenseRank, {{"value", TypeCategory::Expression }}}}, // windw function
                                            {"ntile", {Builtin::WindowNTile, {{"n", TypeCategory::Expression }}}}, // windw function
                                            {"lead", {Builtin::WindowLead, {{"value", TypeCategory::Expression }, {"offset", TypeCategory::Expression, true}, {"default", TypeCategory::Expression, true}}}}, // windw function
                                            {"lag", {Builtin::WindowLag, {{"value", TypeCategory::Expression }, {"offset", TypeCategory::Expression, true}, {"default", TypeCategory::Expression, true}}}}, // windw function
                                            {"first_value", {Builtin::WindowFirstValue, {{"value", TypeCategory::Expression }}}}, // windw function
                                            {"last_value", {Builtin::WindowLastValue, {{"value", TypeCategory::Expression }}}}, // windw function
                                            {"table", {Builtin::Table, {{"values", TypeCategory::ExpressionList}}}}, // table construction
                                            {"case", {Builtin::Case, {{"cases", TypeCategory::ExpressionList}, {"else", TypeCategory::Expression, true}, {"search", TypeCategory::Scalar, true}}}}, // case expression
                                            {"gensym", {Builtin::Gensym, {{"name", TypeCategory::Symbol, true}}}}, // create a unique symbol
                                            {"foreigncall", {Builtin::ForeignCall, {{"name", Type::getText()}, {"returns", TypeCategory::Symbol}, {"arguments", TypeCategory::ExpressionList, true}, {"type", TypeCategory::Symbol, true}}}} // declare that a function with the given arguments exists
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
      case Type::Date: return &dateFunctions;
      case Type::Interval: return &scalarFunctions;
   }
   __builtin_unreachable();
}
//---------------------------------------------------------------------------
}
//---------------------------------------------------------------------------
