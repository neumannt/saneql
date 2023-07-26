#include "semana/SemanticAnalysis.hpp"
#include "algebra/Expression.hpp"
#include "algebra/Operator.hpp"
#include "parser/AST.hpp"
#include "semana/Functions.hpp"
#include <algorithm>
#include <charconv>
#include <optional>
#include <stdexcept>
#include <unordered_set>
//---------------------------------------------------------------------------
// (c) 2023 Thomas Neumann
//---------------------------------------------------------------------------
using namespace std;
//---------------------------------------------------------------------------
namespace saneql {
//---------------------------------------------------------------------------
namespace {
using AST = ast::AST;
//---------------------------------------------------------------------------
/// Convenience class to iterate over lists that contain mixed AST nodes
class List {
   public:
   /// An iterator over the list
   class iterator {
      private:
      /// The current position within the list
      const ast::List* listPos;

      public:
      /// Constructor
      explicit iterator(const ast::List* listPos) : listPos(listPos) {}

      /// Access the current element
      const ast::AST* get() const;
      /// Access the current element
      const ast::AST& operator*() const { return *get(); }
      /// Advance
      iterator& operator++();

      /// Compare
      bool operator==(const iterator& o) const { return listPos == o.listPos; }
      /// Compare
      bool operator!=(const iterator& o) const { return listPos != o.listPos; }
   };

   /// Check a that the entry is really a list
   static const ast::List* checkList(const ast::AST* list);

   private:
   /// The list
   const ast::List* list;

   public:
   /// Constructor
   explicit List(const ast::AST* list) : list(checkList(list)) {}

   /// Get an iterator
   iterator begin() const { return iterator(list); }
   /// Get an iterator
   iterator end() const { return iterator(nullptr); }
};
//---------------------------------------------------------------------------
const ast::AST* List::iterator::get() const
// Access the current element
{
   assert(listPos);
   return listPos->head;
}
//---------------------------------------------------------------------------
List::iterator& List::iterator::operator++()
// Advance
{
   assert(listPos);
   listPos = ast::List::cast(listPos->tail);
   return *this;
}
//---------------------------------------------------------------------------
const ast::List* List::checkList(const ast::AST* list)
// Check that the entry is really a list
{
   auto* l = ast::List::cast(list);
   return !l || (!l->head && !l->tail) ? nullptr : l;
}
//---------------------------------------------------------------------------
/// Convenience class to iterate over typed lists
template <class T>
class TypedList {
   public:
   /// An iterator over the list
   class iterator {
      private:
      /// The current position within the list
      List::iterator iter;

      public:
      /// Constructor
      explicit iterator(const ast::List* listPos) : iter(listPos) {}

      /// Access the current element
      const T* get() const { return T::cast(iter.get()); }
      /// Access the current element
      const T& operator*() const { return T::ref(iter.get()); }
      /// Advance
      iterator& operator++() {
         ++iter;
         return *this;
      }

      /// Compare
      bool operator==(const iterator& o) const { return iter == o.iter; }
      /// Compare
      bool operator!=(const iterator& o) const { return iter != o.iter; }
   };

   private:
   /// The list
   const ast::List* list;

   public:
   /// Constructor
   explicit TypedList(const ast::AST* list) : list(List::checkList(list)) {}

   /// Get an iterator
   iterator begin() const { return iterator(list); }
   /// Get an iterator
   iterator end() const { return iterator(nullptr); }
};
//---------------------------------------------------------------------------
// Construct the internal name
static string getInternalName(string_view name) {
   // Use bit stuffing to get a private prefix for macros
   if ((!name.empty()) && (name.front() == ' ')) return string(" ") + string(name);
   return string(name);
}
}
//---------------------------------------------------------------------------
Collate OrderingInfo::lookupCollate(const std::string& name)
// Lookup a collate. Throws if not found
{
   throw runtime_error("unknown collate '" + name + "'");
}
//---------------------------------------------------------------------------
/// Helper class to install aggregation logic
class SemanticAnalysis::BindingInfo::GroupByScope {
   public:
   /// The scope that is available after aggregations
   BindingInfo& postAggregation;
   /// The scope that is available within aggregations
   const BindingInfo& preAggregation;
   /// The aggregations that are computed
   vector<algebra::GroupBy::Aggregation>& aggregations;
   /// The old group by scope
   GroupByScope* oldScope;

   /// Constructor
   GroupByScope(BindingInfo& postAggregation, const BindingInfo& preAggregation, vector<algebra::GroupBy::Aggregation>& aggregations) : postAggregation(postAggregation), preAggregation(preAggregation), aggregations(aggregations), oldScope(postAggregation.gbs) { postAggregation.gbs = this; }
   /// Destructor
   ~GroupByScope() { postAggregation.gbs = oldScope; }

   /// Get the current scope
   auto& getBinding() { return postAggregation; }
};
//---------------------------------------------------------------------------
const algebra::IU* const SemanticAnalysis::BindingInfo::ambiguousIU = reinterpret_cast<const algebra::IU*>(1);
//---------------------------------------------------------------------------
SemanticAnalysis::BindingInfo::Scope* SemanticAnalysis::BindingInfo::addScope(const std::string& name)
// Add a new scope, mark it as ambiguous if it already exists
{
   auto iter = scopes.find(name);
   if (iter != scopes.end()) {
      iter->second.columns.clear();
      iter->second.ambiguous = true;
      return nullptr;
   }
   return &(scopes[name]);
}
//---------------------------------------------------------------------------
void SemanticAnalysis::BindingInfo::addBinding(Scope* scope, const std::string& column, const algebra::IU* iu)
// Add a binding
{
   if (scope) {
      auto& e = scope->columns[column];
      if (e)
         e = ambiguousIU;
      else
         e = iu;
   }
   auto& e = columnLookup[column];
   if (e)
      e = ambiguousIU;
   else
      e = iu;
   columns.push_back({column, iu});
}
//---------------------------------------------------------------------------
const algebra::IU* SemanticAnalysis::BindingInfo::lookup(const std::string& name) const
// Lookup a column
{
   auto iter = columnLookup.find(name);
   if (iter != columnLookup.end())
      return iter->second;
   return nullptr;
}
//---------------------------------------------------------------------------
const algebra::IU* SemanticAnalysis::BindingInfo::lookup(const std::string& binding, const std::string& name) const
// Lookup a column
{
   auto iter = scopes.find(binding);
   if (iter != scopes.end()) {
      if (iter->second.ambiguous) return ambiguousIU + 1;
      auto iter2 = iter->second.columns.find(name);
      if (iter2 != iter->second.columns.end())
         return iter2->second;
   }
   return nullptr;
}
//---------------------------------------------------------------------------
void SemanticAnalysis::BindingInfo::join(const BindingInfo& other)
// Merge after a join
{
   columns.insert(columns.end(), other.columns.begin(), other.columns.end());
   for (auto& c : other.columnLookup) {
      if (!columnLookup.count(c.first)) {
         columnLookup.insert(c);
      } else {
         columnLookup[c.first] = ambiguousIU;
      }
   }
   for (auto& s : other.scopes) {
      if (!scopes.count(s.first)) {
         scopes.insert(s);
      } else {
         auto& s2 = scopes[s.first];
         s2.columns.clear();
         s2.ambiguous = true;
      }
   }
}
//---------------------------------------------------------------------------
SemanticAnalysis::ExpressionResult::ExpressionResult(unique_ptr<algebra::Expression> expression, OrderingInfo ordering)
   : content(ScalarInfo{move(expression), move(ordering)})
// Constructor
{
}
//---------------------------------------------------------------------------
SemanticAnalysis::ExpressionResult::ExpressionResult(unique_ptr<algebra::Operator> op, BindingInfo binding)
   : content(TableInfo{move(op), move(binding)})
// Constructor
{
}
//---------------------------------------------------------------------------
SemanticAnalysis::ExpressionResult::~ExpressionResult()
// Destructor
{
}
//---------------------------------------------------------------------------
void SemanticAnalysis::reportError(string message)
// Report an error
{
   throw runtime_error(move(message));
}
//---------------------------------------------------------------------------
void SemanticAnalysis::invalidAST()
// Invalid AST node
{
   reportError("invalid AST");
}
//---------------------------------------------------------------------------
string SemanticAnalysis::extractString(const ast::AST* token)
// Extract a string value
{
   return ast::Token::ref(token).asString();
}
//---------------------------------------------------------------------------
string SemanticAnalysis::extractSymbol(const ast::AST* token)
// Extract a symbol name
{
   return getInternalName(ast::Token::ref(token).asString());
}
//---------------------------------------------------------------------------
SemanticAnalysis::ExpressionResult SemanticAnalysis::analyzeQuery(const ast::AST* query)
// Analyze a query
{
   if (query->getType() == AST::Type::DefineFunction) reportError("defun not implemented yet");
   auto& qb = ast::QueryBody::ref(query);

   if (qb.lets) {
      for (auto& l : TypedList<ast::LetEntry>(qb.lets))
         analyzeLet(l);
   }

   BindingInfo emptyScope;
   return analyzeExpression(emptyScope, qb.body);
}
//---------------------------------------------------------------------------
SemanticAnalysis::ExpressionResult SemanticAnalysis::analyzeLiteral(const ast::Literal& literal)
// Analyze a literal
{
   using SubType = ast::Literal::SubType;
   unique_ptr<algebra::Expression> exp;
   switch (literal.getSubType()) {
      case SubType::Integer: exp = make_unique<algebra::ConstExpression>(extractString(literal.arg), Type::getInteger()); break;
      case SubType::Float: exp = make_unique<algebra::ConstExpression>(extractString(literal.arg), Type::getDecimal(15, 2)); break; // TODO compute the correct type
      case SubType::String: exp = make_unique<algebra::ConstExpression>(extractString(literal.arg), Type::getText()); break;
      case SubType::True: exp = make_unique<algebra::ConstExpression>("true", Type::getBool()); break;
      case SubType::False: exp = make_unique<algebra::ConstExpression>("false", Type::getBool()); break;
      case SubType::Null: exp = make_unique<algebra::ConstExpression>(nullptr, Type::getUnknown().asNullable()); break;
   }
   return ExpressionResult(move(exp), OrderingInfo::defaultOrder());
}
//---------------------------------------------------------------------------
SemanticAnalysis::ExpressionResult SemanticAnalysis::analyzeAccess(const BindingInfo& scope, const ast::Access& ast)
// Analyze access
{
   auto name = extractSymbol(ast.part);
   if (ast.base->getType() != ast::AST::Type::Token) reportError("invalid access to column '" + name + "'");
   auto base = extractSymbol(ast.base);

   auto iu = scope.lookup(base, name);
   if (iu == BindingInfo::ambiguousIU) reportError("'" + name + "' is ambiguous");
   if (iu == BindingInfo::ambiguousIU + 1) reportError("'" + base + "' is ambiguous");

   if (!iu) reportError("'" + base + "." + name + "' not found");
   return ExpressionResult(make_unique<algebra::IURef>(iu), OrderingInfo::defaultOrder());
}
//---------------------------------------------------------------------------
/// Is the type a numeric type?
static bool isNumeric(Type t) { return (t.getType() == Type::Integer) || (t.getType() == Type::Decimal); }
/// Is the type a string type?
static bool isString(Type t) { return (t.getType() == Type::Char) || (t.getType() == Type::Varchar) || (t.getType() == Type::Text); }
//---------------------------------------------------------------------------
static OrderingInfo unifyCollate(OrderingInfo a, OrderingInfo b)
// Unify collate specifications
{
   // TODO handle implicit collates etc.
   if (a.getCollate() != b.getCollate())
      throw runtime_error("collate mismatch");
   return a;
}
//---------------------------------------------------------------------------
SemanticAnalysis::ExpressionResult SemanticAnalysis::analyzeBinaryExpression(const BindingInfo& scope, const ast::BinaryExpression& ast)
// Analyze a binary expression
{
   auto left = analyzeExpression(scope, ast.left);
   auto right = analyzeExpression(scope, ast.right);

   auto doArithmetic = [&](string_view name, algebra::BinaryExpression::Operation op) {
      if ((!left.isScalar()) || (!right.isScalar())) reportError("scalar value required in operator '" + string(name) + "'");
      auto lt = left.scalar()->getType(), rt = right.scalar()->getType();
      if (isNumeric(lt) && isNumeric(rt)) {
         // TODO proper type inference
         Type resultType = ((lt.getType() < rt.getType()) ? rt : lt).withNullable(lt.isNullable() || rt.isNullable());
         return ExpressionResult(make_unique<algebra::BinaryExpression>(move(left.scalar()), move(right.scalar()), resultType, op), OrderingInfo::defaultOrder());
      } else if ((op == algebra::BinaryExpression::Operation::Plus) && isString(lt) && isString(rt)) {
         Type resultType = Type::getText().withNullable(lt.isNullable() || rt.isNullable());
         return ExpressionResult(make_unique<algebra::BinaryExpression>(move(left.scalar()), move(right.scalar()), resultType, algebra::BinaryExpression::Operation::Concat), OrderingInfo::defaultOrder());
      } else if ((lt.getType() == Type::Date) && (rt.getType() == Type::Interval) && ((op == algebra::BinaryExpression::Operation::Plus) || (op == algebra::BinaryExpression::Operation::Minus))) {
         Type resultType = Type::getDate().withNullable(lt.isNullable() || rt.isNullable());
         return ExpressionResult(make_unique<algebra::BinaryExpression>(move(left.scalar()), move(right.scalar()), resultType, op), OrderingInfo::defaultOrder());
      } else {
         reportError("'" + string(name) + "' requires numerical arguments");
      }
   };
   auto doComparison = [&](string_view name, algebra::ComparisonExpression::Mode mode) {
      if ((!left.isScalar()) || (!right.isScalar())) reportError("scalar value required in operator '" + string(name) + "'");
      enforceComparable(left, right);
      auto order = unifyCollate(left.getOrdering(), right.getOrdering());
      return ExpressionResult(make_unique<algebra::ComparisonExpression>(move(left.scalar()), move(right.scalar()), mode, order.getCollate()), OrderingInfo::defaultOrder());
   };
   auto doLogic = [&](string_view name, algebra::BinaryExpression::Operation op) {
      if ((!left.isScalar()) || (!right.isScalar())) reportError("scalar value required in operator '" + string(name) + "'");
      if (left.scalar()->getType().getType() == Type::Unknown) left.scalar() = make_unique<algebra::CastExpression>(move(left.scalar()), Type::getBool().asNullable());
      if (right.scalar()->getType().getType() == Type::Unknown) right.scalar() = make_unique<algebra::CastExpression>(move(right.scalar()), Type::getBool().asNullable());
      auto lt = left.scalar()->getType(), rt = right.scalar()->getType();
      if ((lt.getType() == Type::Bool) && (rt.getType() == Type::Bool)) {
         Type resultType = Type::getBool().withNullable(lt.isNullable() || rt.isNullable());
         return ExpressionResult(make_unique<algebra::BinaryExpression>(move(left.scalar()), move(right.scalar()), resultType, op), OrderingInfo::defaultOrder());
      } else {
         reportError("'" + string(name) + "' requires boolean arguments");
      }
   };

   using SubType = ast::BinaryExpression::SubType;
   switch (ast.getSubType()) {
      case SubType::Plus: return doArithmetic("+", algebra::BinaryExpression::Operation::Plus); break;
      case SubType::Minus: return doArithmetic("-", algebra::BinaryExpression::Operation::Minus); break;
      case SubType::Mul: return doArithmetic("*", algebra::BinaryExpression::Operation::Mul); break;
      case SubType::Div: return doArithmetic("/", algebra::BinaryExpression::Operation::Div); break;
      case SubType::Mod: return doArithmetic("%", algebra::BinaryExpression::Operation::Mod); break;
      case SubType::Pow: return doArithmetic("^", algebra::BinaryExpression::Operation::Power); break;
      case SubType::Less: return doComparison("<", algebra::ComparisonExpression::Mode::Less); break;
      case SubType::Greater: return doComparison(">", algebra::ComparisonExpression::Mode::Greater); break;
      case SubType::Equals: return doComparison("=", algebra::ComparisonExpression::Mode::Equal); break;
      case SubType::NotEquals: return doComparison("<>", algebra::ComparisonExpression::Mode::NotEqual); break;
      case SubType::LessOrEqual: return doComparison("<=", algebra::ComparisonExpression::Mode::LessOrEqual); break;
      case SubType::GreaterOrEqual: return doComparison(">=", algebra::ComparisonExpression::Mode::GreaterOrEqual); break;
      case SubType::And: return doLogic("&&", algebra::BinaryExpression::Operation::And); break;
      case SubType::Or: return doLogic("||", algebra::BinaryExpression::Operation::Or); break;
   }
   __builtin_unreachable();
}
//---------------------------------------------------------------------------
SemanticAnalysis::ExpressionResult SemanticAnalysis::analyzeUnaryExpression(const BindingInfo& scope, const ast::UnaryExpression& ast)
// Analyze a binary expression
{
   auto value = analyzeExpression(scope, ast.value);

   auto doArithmetic = [&](string_view name, algebra::UnaryExpression::Operation op) {
      if (!value.isScalar()) reportError("scalar value required in operator '" + string(name) + "'");
      auto vt = value.scalar()->getType();
      if (isNumeric(vt) || (vt.getType() == Type::Interval)) {
         return ExpressionResult(make_unique<algebra::UnaryExpression>(move(value.scalar()), vt, op), OrderingInfo::defaultOrder());
      } else {
         reportError("'" + string(name) + "' requires numerical arguments");
      }
   };
   auto doLogic = [&](string_view name, algebra::UnaryExpression::Operation op) {
      if (!value.isScalar()) reportError("scalar value required in operator '" + string(name) + "'");
      auto vt = value.scalar()->getType();
      if (vt.getType() == Type::Bool) {
         return ExpressionResult(make_unique<algebra::UnaryExpression>(move(value.scalar()), vt, op), OrderingInfo::defaultOrder());
      } else {
         reportError("'" + string(name) + "' requires boolean arguments");
      }
   };

   using SubType = ast::UnaryExpression::SubType;
   switch (ast.getSubType()) {
      case SubType::Plus: return doArithmetic("+", algebra::UnaryExpression::Operation::Plus); break;
      case SubType::Minus: return doArithmetic("-", algebra::UnaryExpression::Operation::Minus); break;
      case SubType::Not: return doLogic("!", algebra::UnaryExpression::Operation::Not); break;
   }
   __builtin_unreachable();
}
//---------------------------------------------------------------------------
SemanticAnalysis::ExpressionResult SemanticAnalysis::analyzeJoin(ExpressionResult& input, const std::vector<const ast::FuncArg*>& args)
// Analyze a groupby computation
{
   // Analyze the join type
   algebra::Join::JoinType joinType = algebra::Join::JoinType::Inner;
   bool leftOnly = false, rightOnly = false;
   if (args[2]) {
      string jt = symbolArgument("join", "type", args[2]);
      if (jt == "inner") {
         joinType = algebra::Join::JoinType::Inner;
      } else if ((jt == "left") || (jt == "leftouter")) {
         joinType = algebra::Join::JoinType::LeftOuter;
      } else if ((jt == "right") || (jt == "rightouter")) {
         joinType = algebra::Join::JoinType::RightOuter;
      } else if ((jt == "full") || (jt == "fullouter")) {
         joinType = algebra::Join::JoinType::FullOuter;
      } else if ((jt == "leftsemi") || (jt == "exists")) {
         joinType = algebra::Join::JoinType::LeftSemi;
         leftOnly = true;
      } else if (jt == "rightsemi") {
         joinType = algebra::Join::JoinType::RightSemi;
         rightOnly = true;
      } else if ((jt == "leftanti") || (jt == "notexists")) {
         joinType = algebra::Join::JoinType::LeftAnti;
         leftOnly = true;
      } else if (jt == "rightanti") {
         joinType = algebra::Join::JoinType::RightAnti;
         rightOnly = true;
      } else {
         reportError("unknown join type '" + jt + "'");
      }
   }

   // Analyze the other table
   BindingInfo empty;
   auto other = tableArgument(empty, "join", "table", args[0]);
   BindingInfo resultBinding;
   if (leftOnly || rightOnly) {
      resultBinding = input.getBinding();
   } else {
      resultBinding = move(input.getBinding());
   }
   resultBinding.join(other.getBinding());

   // Analyze the join condition
   auto cond = scalarArgument(resultBinding, "join", "condition", args[1]);
   if (cond.scalar()->getType().getType() != Type::Bool) reportError("join condition must be a boolean");

   // Fix the result scope
   if (leftOnly) {
      resultBinding = move(input.getBinding());
   } else if (rightOnly) {
      resultBinding = move(other.getBinding());
   }

   // Construct the result
   return ExpressionResult(make_unique<algebra::Join>(move(input.table()), move(other.table()), move(cond.scalar()), joinType), move(resultBinding));
}
//---------------------------------------------------------------------------
SemanticAnalysis::ExpressionResult SemanticAnalysis::analyzeGroupBy(ExpressionResult& input, const std::vector<const ast::FuncArg*>& args)
// Analyze a groupby computation
{
   // Analyze the groups
   vector<algebra::GroupBy::Entry> groupBy;
   vector<algebra::GroupBy::Aggregation> aggregates;
   vector<algebra::Map::Entry> results;
   BindingInfo resultBinding;
   auto scope = resultBinding.addScope("groupby");
   if (args[0]) {
      auto g = expressionListArgument(input.getBinding(), args[0]);
      for (auto& e : g) {
         if (!e.value.isScalar()) reportError("groupby requires scalar groups");
         auto et = e.value.scalar()->getType();
         groupBy.push_back(algebra::GroupBy::Entry{move(e.value.scalar()), make_unique<algebra::IU>(et)});
         string name = move(e.name);
         if (name.empty()) {
            name = to_string(scope->columns.size() + 1);
         }
         resultBinding.addBinding(scope, name, groupBy.back().iu.get());
      }
   }

   // Analyze the aggregates
   if (args[1]) {
      // Compute aggregates
      BindingInfo::GroupByScope gbs(resultBinding, input.getBinding(), aggregates);
      auto g = expressionListArgument(gbs.getBinding(), args[1]);
      for (auto& e : g) {
         if (!e.value.isScalar()) reportError("groupby requires scalar aggregates");
         auto et = e.value.scalar()->getType();
         results.push_back(algebra::GroupBy::Entry{move(e.value.scalar()), make_unique<algebra::IU>(et)});
      }

      // Make aggregates visible
      unsigned slot = 0;
      for (auto& e : g) {
         string name = move(e.name);
         if (name.empty()) {
            name = to_string(scope->columns.size() + 1);
         }
         if (auto iuref = dynamic_cast<algebra::IURef*>(results[slot].value.get())) {
            resultBinding.addBinding(scope, name, iuref->getIU());
            results[slot].iu.reset();
         } else {
            resultBinding.addBinding(scope, name, results[slot].iu.get());
         }
         ++slot;
      }
      for (unsigned index = 0; index < results.size();)
         if (!results[index].iu) {
            swap(results[index], results.back());
            results.pop_back();
         } else {
            ++index;
         }
   }

   // TODO
   if (args[2] || args[3]) reportError("grouping sets not implemented yet");

   unique_ptr<algebra::Operator> tree = make_unique<algebra::GroupBy>(move(input.table()), move(groupBy), move(aggregates));
   if (!results.empty()) {
      tree = make_unique<algebra::Map>(move(tree), move(results));
   }

   return ExpressionResult(move(tree), move(resultBinding));
}
//---------------------------------------------------------------------------
SemanticAnalysis::ExpressionResult SemanticAnalysis::analyzeMap(ExpressionResult& input, const std::vector<const ast::FuncArg*>& args, bool project)
// Analyze a map computation
{
   string name = project ? "project" : "map";

   // Compute the expressions
   auto g = expressionListArgument(input.getBinding(), args[0]);
   vector<algebra::Map::Entry> results;
   for (auto& e : g) {
      if (!e.value.isScalar()) reportError(name + " requires scalar values");
      auto et = e.value.scalar()->getType();
      results.push_back(algebra::Map::Entry{move(e.value.scalar()), make_unique<algebra::IU>(et)});
   }

   // Make expressions visible
   BindingInfo resultBinding;
   if (!project) resultBinding = move(input.getBinding());
   auto scope = resultBinding.addScope(name);
   unsigned slot = 0;
   for (auto& e : g) {
      string name = move(e.name);
      if (name.empty()) {
         name = to_string(scope->columns.size() + 1);
      }
      if (auto iuref = dynamic_cast<algebra::IURef*>(results[slot].value.get())) {
         resultBinding.addBinding(scope, name, iuref->getIU());
         results[slot].iu.reset();
      } else {
         resultBinding.addBinding(scope, name, results[slot].iu.get());
      }
      ++slot;
   }
   for (unsigned index = 0; index < results.size();) {
      if (!results[index].iu) {
         swap(results[index], results.back());
         results.pop_back();
      } else {
         ++index;
      }
   }

   // Add a map if needed
   unique_ptr<algebra::Operator> tree = move(input.table());
   if (!results.empty()) {
      if (auto sort = dynamic_cast<algebra::Sort*>(tree.get()); sort && project) {
         // Project promises to preserve the sort order, thus we have to add the map below the sort
         sort->input = make_unique<algebra::Map>(move(sort->input), move(results));
      } else {
         tree = make_unique<algebra::Map>(move(tree), move(results));
      }
   }

   return ExpressionResult(move(tree), move(resultBinding));
}
//---------------------------------------------------------------------------
SemanticAnalysis::ExpressionResult SemanticAnalysis::analyzeOrderBy(ExpressionResult& input, const std::vector<const ast::FuncArg*>& args)
// Analyze a groupby computation
{
   // Analyze the order
   vector<algebra::Sort::Entry> order;
   if (args[0]) {
      auto g = expressionListArgument(input.getBinding(), args[0]);
      for (auto& e : g) {
         if (!e.value.isScalar()) reportError("orderby requires scalar order values");
         auto o = e.value.getOrdering();
         order.push_back({move(e.value.scalar()), o.getCollate(), o.isDescending()});
      }
   }

   // Handle integer constants
   auto handleConstant = [&](const char* name, const ast::FuncArg* arg) {
      if (arg->value->getType() != ast::AST::Type::Literal) reportError("'" + string(name) + "' requires an integer constant");
      auto& l = ast::Literal::ref(arg->value);
      if (l.getSubType() != ast::Literal::SubType::Integer) reportError("'" + string(name) + "' requires an integer constant");
      string v = extractString(l.arg);
      uint64_t result;
      auto [ptr, ec] = std::from_chars(v.data(), v.data() + v.size(), result);
      if (ec != std::errc()) reportError("'" + string(name) + "' requires an integer constant");
      return result;
   };

   // Handle bounds
   optional<uint64_t> limit, offset;
   if (args[1]) limit = handleConstant("limit", args[1]);
   if (args[2]) offset = handleConstant("offset", args[2]);

   return ExpressionResult(make_unique<algebra::Sort>(move(input.table()), move(order), limit, offset), move(input.getBinding()));
}
//---------------------------------------------------------------------------
SemanticAnalysis::ExtendedType SemanticAnalysis::analyzeType(const ast::Type& type)
// Analyze a cast expression
{
   auto simpleType = [&](Type result) {
      if (type.getSubType() != ast::Type::SubType::Simple) reportError("invalid type syntax");
      return ExtendedType(result);
   };
   auto name = extractString(type.name);
   if (name == "integer") return simpleType(Type::getInteger());
   if (name == "boolean") return simpleType(Type::getBool());
   if (name == "date") return simpleType(Type::getDate());
   if (name == "interval") return simpleType(Type::getInterval());
   // XXX todo more types
   reportError("unknown type '" + name + "'");
}
//---------------------------------------------------------------------------
SemanticAnalysis::ExpressionResult SemanticAnalysis::analyzeCast(const BindingInfo& scope, const ast::Cast& cast)
// Analyze a cast expression
{
   auto value = analyzeExpression(scope, cast.value);
   if (!value.isScalar()) reportError("casts require scalar values");
   auto type = analyzeType(ast::Type::ref(cast.type));
   if (!type.isBasic()) reportError("invalid cast type");

   return ExpressionResult(make_unique<algebra::CastExpression>(move(value.scalar()), type.getBasicType()), value.getOrdering());
}
//---------------------------------------------------------------------------
string SemanticAnalysis::symbolArgument(const string& funcName, const string& argName, const ast::FuncArg* arg)
// Handle a symbol argument
{
   if (arg->getSubType() != ast::FuncArg::SubType::Flat) reportError("parameter '" + argName + "' requires a symbol in call to '" + funcName + "'");
   if (arg->value->getType() != ast::AST::Type::Token) reportError("parameter '" + argName + "' requires a symbol in call to '" + funcName + "'");
   return extractSymbol(arg->value);
}
//---------------------------------------------------------------------------
SemanticAnalysis::ExpressionResult SemanticAnalysis::scalarArgument(const BindingInfo& scope, const string& funcName, const string& argName, const ast::FuncArg* arg)
// Handle a symbol argument
{
   if (arg->getSubType() != ast::FuncArg::SubType::Flat) reportError("parameter '" + argName + "' requires a scalar in call to '" + funcName + "'");
   auto r = analyzeExpression(scope, arg->value);
   if (!r.isScalar()) reportError("parameter '" + argName + "' requires a scalar in call to '" + funcName + "'");
   return r;
}
//---------------------------------------------------------------------------
SemanticAnalysis::ExpressionResult SemanticAnalysis::tableArgument(const BindingInfo& scope, const string& funcName, const string& argName, const ast::FuncArg* arg)
// Handle a symbol argument
{
   if (arg->getSubType() != ast::FuncArg::SubType::Flat) reportError("parameter '" + argName + "' requires a table in call to '" + funcName + "'");
   auto r = analyzeExpression(scope, arg->value);
   if (!r.isTable()) reportError("parameter '" + argName + "' requires a table in call to '" + funcName + "'");
   return r;
}
//---------------------------------------------------------------------------
static string inferName(const ast::AST* exp)
// Try to infer a name from an expression
{
   if (exp) {
      if (exp->getType() == ast::AST::Type::Token) return getInternalName(ast::Token::ref(exp).asString());
      if (exp->getType() == ast::AST::Type::Access) return getInternalName(ast::Token::ref(ast::Access::ref(exp).part).asString());
   }
   return {};
}
//---------------------------------------------------------------------------
vector<SemanticAnalysis::ExpressionArg> SemanticAnalysis::expressionListArgument(const BindingInfo& scope, const ast::FuncArg* arg)
// Handle expression list arguments
{
   vector<SemanticAnalysis::ExpressionArg> result;
   // As convenience feature we also support single expressions
   if (arg->getSubType() == ast::FuncArg::SubType::Flat) {
      result.push_back({inferName(arg->value), analyzeExpression(scope, arg->value)});
   } else {
      for (auto& a : TypedList<ast::FuncArgNamed>(arg->value)) {
         if (a.getSubType() != ast::FuncArgNamed::SubType::Flat) reportError("nested expression list not allowed here");
         auto e = analyzeExpression(scope, a.value);
         string name;
         if (a.name)
            name = extractSymbol(a.name);
         else
            name = inferName(a.value);
         result.push_back({move(name), move(e)});
      }
   }
   return result;
}
//---------------------------------------------------------------------------
void SemanticAnalysis::enforceComparable(ExpressionResult& a, ExpressionResult& b)
// Make sure two values are comparable
{
   // Infer NULL types
   assert((a.isScalar()) && (b.isScalar()));
   auto &sa = a.scalar(), &sb = b.scalar();
   if (sa->getType().getType() == Type::Unknown) {
      // Both NULL
      if (sb->getType().getType() == Type::Unknown) return;
      sa = make_unique<algebra::CastExpression>(move(sa), sb->getType().asNullable());
   } else if (sb->getType().getType() == Type::Unknown) {
      sb = make_unique<algebra::CastExpression>(move(sb), sa->getType().asNullable());
   }

   // And check if we can compare
   auto ta = sa->getType(), tb = sb->getType();
   auto check = [this, ta, tb](bool ok) {
      if (!ok) reportError("cannot compare '" + ta.getName() + "' and '" + tb.getName() + "'");
   };
   switch (ta.getType()) {
      case Type::Unknown: return;
      case Type::Bool: return check(tb.getType() == Type::Bool);
      case Type::Integer:
      case Type::Decimal: return check((tb.getType() == Type::Integer) || (tb.getType() == Type::Decimal));
      case Type::Char:
      case Type::Varchar:
      case Type::Text: return check((tb.getType() == Type::Char) || (tb.getType() == Type::Varchar) || (tb.getType() == Type::Text));
      case Type::Date: return check(tb.getType() == Type::Date);
      case Type::Interval: return check(tb.getType() == Type::Interval);
   }
}
//---------------------------------------------------------------------------
SemanticAnalysis::ExpressionResult SemanticAnalysis::analyzeCall(const BindingInfo& scope, const ast::Call& call)
// Analyze a call expression
{
   // Recognize invoke and lookup the call signature
   optional<ExpressionResult> base;
   string name;
   const Functions::Signature* sig = nullptr;
   if (call.func->getType() == ast::AST::Type::Access) {
      auto& a = ast::Access::ref(call.func);
      base.emplace(analyzeExpression(scope, a.base));
      name = extractString(a.part);

      // Choose the correct functions list
      string typeName;
      const Functions* functions;
      if (base->isScalar()) {
         Type type = base->scalar()->getType();
         typeName = type.getName();
         functions = Functions::getFunctions(type);
      } else {
         typeName = "table";
         functions = &Functions::table;
      }

      // Check if we can find the function
      sig = functions->lookup(name);
      if (!sig) reportError("'" + name + "' not found for '" + typeName + "'");
   } else {
      if (call.func->getType() != ast::AST::Type::Token) reportError("invalid function name");
      name = extractString(call.func);
      sig = Functions::freeFunctions.lookup(name);
      if (!sig) reportError("function '" + name + "' not found");
   }

   // Assign arguments to positions
   std::vector<const ast::FuncArg*> args;
   bool hadNamed = false;
   for (auto& a : TypedList<ast::FuncArg>(call.args)) {
      // Check for names arguments
      if (a.name) {
         if (!hadNamed) {
            args.resize(sig->arguments.size(), nullptr);
            hadNamed = true;
         }
         auto argName = extractSymbol(a.name);
         auto iter = find_if(sig->arguments.begin(), sig->arguments.end(), [&](auto& c) { return c.name == argName; });
         if (iter == sig->arguments.end()) reportError("parameter '" + argName + "' not found in call to '" + name + "'");
         auto slot = iter - sig->arguments.begin();
         if (args[slot]) reportError("parameter '" + argName + "' provided more than once");
         args[slot] = &a;
      } else {
         if (hadNamed) reportError("positional parameters cannot be used after named parameters in call to '" + name + "'");
         if (args.size() >= sig->arguments.size()) reportError("too many parameters in call to '" + name + "'");
         args.push_back(&a);
      }
   }
   if (!hadNamed) args.resize(sig->arguments.size(), nullptr);
   for (unsigned index = 0, limit = sig->arguments.size(); index != limit; ++index)
      if ((!args[index]) && (!sig->arguments[index].hasDefault)) reportError("parameter '" + sig->arguments[index].name + "' missing in call to '" + name + "'");

   // Logic for handling aggregates
   auto handleAggregate = [&](algebra::GroupBy::Op op) {
      auto gbs = scope.getGroupByScope();
      if (!gbs) reportError("aggregate '" + name + "' can only be used in group by computations");
      ExpressionResult exp(unique_ptr<algebra::Expression>(), OrderingInfo::defaultOrder());
      Type resultType = Type::getInteger();
      if (op != algebra::GroupBy::Op::CountStar) {
         exp = scalarArgument(gbs->preAggregation, "aggregate", name, args[0]);
         if ((op != algebra::GroupBy::Op::Min) && (op != algebra::GroupBy::Op::Max) && (!isNumeric(exp.scalar()->getType()))) reportError("aggregate '" + name + "' requires a numerical argument");
         resultType = exp.scalar()->getType(); // TODO fix type inference
         if (op == algebra::GroupBy::Op::Count) resultType = Type::getInteger();
      }
      gbs->aggregations.push_back({move(exp.scalar()), make_unique<algebra::IU>(resultType), op});
      return ExpressionResult(make_unique<algebra::IURef>(gbs->aggregations.back().iu.get()), OrderingInfo::defaultOrder());
   };

   // Handle the functions
   using Builtin = Functions::Builtin;
   switch (sig->builtin) {
      case Builtin::Asc: base->accessOrdering().markAscending(); return move(*base);
      case Builtin::Desc: base->accessOrdering().markDescending(); return move(*base);
      case Builtin::Collate: base->accessOrdering().setCollate(OrderingInfo::lookupCollate(symbolArgument(name, sig->arguments[0].name, args[0]))); return move(*base);
      case Builtin::Is: {
         auto arg = scalarArgument(scope, name, sig->arguments[0].name, args[0]);
         enforceComparable(*base, arg);
         auto order = unifyCollate(base->getOrdering(), arg.getOrdering());
         return ExpressionResult(make_unique<algebra::ComparisonExpression>(move(base->scalar()), move(arg.scalar()), algebra::ComparisonExpression::Is, order.getCollate()), OrderingInfo::defaultOrder());
      }
      case Builtin::Like: reportError("like not implemented yet");
      case Builtin::Filter: {
         auto cond = scalarArgument(base->getBinding(), name, sig->arguments[0].name, args[0]);
         if (cond.scalar()->getType().getType() != Type::Bool) reportError("'filter' requires a boolean filter condition");
         return ExpressionResult(make_unique<algebra::Select>(move(base->table()), move(cond.scalar())), move(base->accessBinding()));
      }
      case Builtin::Join: return analyzeJoin(*base, args);
      case Builtin::GroupBy: return analyzeGroupBy(*base, args);
      case Builtin::OrderBy: return analyzeOrderBy(*base, args);
      case Builtin::Map: return analyzeMap(*base, args, false);
      case Builtin::Project: return analyzeMap(*base, args, true);
      case Builtin::AggCount: return handleAggregate(args[0] ? algebra::GroupBy::Op::Count : algebra::GroupBy::Op::CountStar);
      case Builtin::AggSum: return handleAggregate(algebra::GroupBy::Op::Sum);
      case Builtin::AggAvg: return handleAggregate(algebra::GroupBy::Op::Avg);
      case Builtin::AggMin: return handleAggregate(algebra::GroupBy::Op::Min);
      case Builtin::AggMax: return handleAggregate(algebra::GroupBy::Op::Max);
      case Builtin::Table: return analyzeTableConstruction(scope, args[0]);
   }

   reportError("call not implemented yet");
}
//---------------------------------------------------------------------------
SemanticAnalysis::ExpressionResult SemanticAnalysis::analyzeTableConstruction(const BindingInfo& scope, const ast::FuncArg* ast)
// Analyze a table construction expression
{
   // Examine the signature
   if (ast->getSubType() != ast::FuncArg::SubType::List) reportError("'table' requires a tuple list");
   vector<string> columnNames;
   for (auto& row : TypedList<ast::FuncArgNamed>(ast->value)) {
      if (row.getSubType() != ast::FuncArgNamed::SubType::List) reportError("'table' requires a tuple list");
      for (auto& arg : TypedList<ast::FuncArgNamed>(row.value)) {
         if (arg.getSubType() != ast::FuncArgNamed::SubType::Flat) reportError("'table' requires a tuple list");
         if (arg.name) {
            columnNames.push_back(extractSymbol(arg.name));
         } else {
            columnNames.push_back(to_string(columnNames.size() + 1));
         }
      }
      break;
   }

   // Collect the values
   unsigned columnCount = columnNames.size(), rowCount = 0;
   vector<unique_ptr<algebra::Expression>> values;
   vector<Type> columnTypes(columnCount, Type::getUnknown());
   for (auto& row : TypedList<ast::FuncArgNamed>(ast->value)) {
      if (row.getSubType() != ast::FuncArgNamed::SubType::List) reportError("'table' requires a tuple list");
      unsigned count = 0;
      for (auto& e : TypedList<ast::FuncArgNamed>(row.value)) {
         if (e.getSubType() != ast::FuncArgNamed::SubType::Flat) reportError("'table' requires a tuple list");
         auto v = analyzeExpression(scope, e.value);
         if (!v.isScalar()) reportError("inline tables require scalar values");
         if (rowCount == 0) {
            columnTypes[count] = v.scalar()->getType();
         } else if (columnTypes[count].getType() == Type::Unknown) {
            columnTypes[count] = v.scalar()->getType().asNullable();
         }
         values.push_back(move(v.scalar()));
         count++;
      }
      if (count < columnCount) reportError("too few column values in inline table");
      if (count > columnCount) reportError("too many column values in inline table");
      ++rowCount;
   }

   // Add casts if needed
   for (unsigned index = 0; index != rowCount; ++index) {
      for (unsigned index2 = 0; index2 != columnCount; ++index2) {
         auto& e = values[index * columnCount + index2];
         if (e->getType().getType() != columnTypes[index2].getType())
            e = make_unique<algebra::CastExpression>(move(e), columnTypes[index2].withNullable(e->getType().isNullable()));
      }
   }

   // Construct the columns
   BindingInfo binding;
   auto s = binding.addScope("table");
   vector<unique_ptr<algebra::IU>> columns;
   for (unsigned index = 0; index != columnCount; ++index) {
      columns.push_back(make_unique<algebra::IU>(columnTypes[index]));
      binding.addBinding(s, columnNames[index], columns[index].get());
   }
   return ExpressionResult(make_unique<algebra::InlineTable>(move(columns), move(values), rowCount), move(binding));
   ;
}
//---------------------------------------------------------------------------
SemanticAnalysis::ExpressionResult SemanticAnalysis::analyzeToken(const BindingInfo& scope, const ast::AST* exp)
// Analyze a token
{
   auto name = extractString(exp);

   // A column reference?
   if (auto iu = scope.lookup(name)) {
      if (iu == BindingInfo::ambiguousIU) reportError("'" + name + "' is ambiguous");
      return ExpressionResult(make_unique<algebra::IURef>(iu), OrderingInfo::defaultOrder());
   }

   // Table scan?
   auto table = schema.lookupTable(name);
   if (!table) reportError("unknown table '" + name + "'");

   auto bindingName = getInternalName(name);
   BindingInfo binding;
   auto resultScope = binding.addScope(bindingName);
   vector<algebra::TableScan::Column> columns;
   for (auto& c : table->columns) {
      columns.push_back({c.name, make_unique<algebra::IU>(c.type)});
      binding.addBinding(resultScope, getInternalName(c.name), columns.back().iu.get());
   }
   return ExpressionResult(make_unique<algebra::TableScan>(name, move(columns)), move(binding));
}
//---------------------------------------------------------------------------
SemanticAnalysis::ExpressionResult SemanticAnalysis::analyzeExpression(const BindingInfo& scope, const ast::AST* exp)
// Analyze an expression
{
   switch (exp->getType()) {
      case ast::AST::Type::Access: return analyzeAccess(scope, ast::Access::ref(exp));
      case ast::AST::Type::BinaryExpression: return analyzeBinaryExpression(scope, ast::BinaryExpression::ref(exp));
      case ast::AST::Type::Call: return analyzeCall(scope, ast::Call::ref(exp));
      case ast::AST::Type::Cast: return analyzeCast(scope, ast::Cast::ref(exp));
      case ast::AST::Type::Literal: return analyzeLiteral(ast::Literal::ref(exp));
      case ast::AST::Type::Token: return analyzeToken(scope, exp);
      case ast::AST::Type::UnaryExpression: return analyzeUnaryExpression(scope, ast::UnaryExpression::ref(exp));
      default: invalidAST();
   }
}
//---------------------------------------------------------------------------
void SemanticAnalysis::analyzeLet(const ast::LetEntry& ast)
// Analyze a let construction
{
   // Collect the arguments
   vector<Functions::Argument> args;
   vector<const ast::AST*> defaultValues;
   if (ast.args) {
      unordered_set<string> argNames;
      for (auto& a : TypedList<ast::LetArg>(ast.args)) {
         auto name = extractSymbol(a.name);
         if (argNames.contains(name)) reportError("duplicate function argument '" + name + "'");
         argNames.insert(name);
         Functions::ArgumentType argType = Functions::TypeCategory::Scalar;
         if (a.type) {
            auto& at = ast::Type::ref(a.type);
            if (at.getSubType() != ast::Type::SubType::Simple) reportError("complex argument types not implemented yet");
            auto tn = extractSymbol(at.name);
            if (tn == "table") {
               argType = Functions::TypeCategory::Table;
            } else if (tn == "expression") {
               argType = Functions::TypeCategory::Expression;
            } else {
               reportError("unsupported argument type '" + tn + "'");
            }
         }
         args.emplace_back(name, argType, !!a.value);
         defaultValues.push_back(a.value);
      }
   }

   // Register the let
   auto name = extractSymbol(ast.name);
   if (letLookup.contains(name)) reportError("duplicate let '" + name + "'");
   lets.emplace_back(move(args), move(defaultValues));
   letLookup[name] = lets.size() - 1;
}
//---------------------------------------------------------------------------
}
//---------------------------------------------------------------------------
