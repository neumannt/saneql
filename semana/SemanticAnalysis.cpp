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
Collate OrderingInfo::lookupCollate(const string& name)
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
   /// Is a window operatopn?
   bool isWindow = false;

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
const SemanticAnalysis::BindingInfo& SemanticAnalysis::BindingInfo::rootScope() {
   static BindingInfo root;
   return root;
}
//---------------------------------------------------------------------------
SemanticAnalysis::BindingInfo::Scope* SemanticAnalysis::BindingInfo::addScope(const string& name)
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
void SemanticAnalysis::BindingInfo::addBinding(Scope* scope, const string& column, const algebra::IU* iu)
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
const algebra::IU* SemanticAnalysis::BindingInfo::lookup(const string& name) const
// Lookup a column
{
   auto iter = columnLookup.find(name);
   if (iter != columnLookup.end())
      return iter->second;
   return nullptr;
}
//---------------------------------------------------------------------------
const algebra::IU* SemanticAnalysis::BindingInfo::lookup(const string& binding, const string& name) const
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
void SemanticAnalysis::BindingInfo::registerArgument(const string& name, const ast::AST* ast, const BindingInfo* scope)
// Register an argument
{
   arguments[name] = ArgumentInformation{{pair<const ast::AST*, const BindingInfo*>{ast, scope}}};
}
//---------------------------------------------------------------------------
void SemanticAnalysis::BindingInfo::registerSymbolArgument(const string& name, const string& symbol)
// Register a symbol argument
{
   arguments[name] = ArgumentInformation{{symbol}};
}
//---------------------------------------------------------------------------
SemanticAnalysis::BindingInfo::ArgumentInformation SemanticAnalysis::BindingInfo::lookupArgument(const string& name) const
// Check for an argument
{
   if (auto iter = arguments.find(name); iter != arguments.end())
      return iter->second;
   return {};
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
   for (auto& a : other.aliases)
      if (!aliases.count(a.first)) {
         aliases.insert(a);
      } else {
         auto& a2 = aliases[a.first];
         a2.columns.clear();
         a2.ambiguous = true;
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
string SemanticAnalysis::extractRawSymbol(const ast::AST* token)
// Extract a symbol name
{
   return getInternalName(ast::Token::ref(token).asString());
}
//---------------------------------------------------------------------------
string SemanticAnalysis::extractSymbol(const BindingInfo& scope, const ast::AST* token)
// Extract a symbol name
{
   string name = extractRawSymbol(token);
   for (auto iter = &scope; iter; iter = iter->parentScope) {
      auto iter2 = iter->arguments.find(name);
      if (iter2 != iter->arguments.end() && (iter2->second.isSymbol()))
         return iter2->second.getSymbol();
   }
   return name;
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

   return analyzeExpression(BindingInfo::rootScope(), qb.body);
}
//---------------------------------------------------------------------------
string SemanticAnalysis::recognizeGensym(const ast::AST* ast)
// Recognize gensym calls. Returns an empty string otherwise
{
   if ((!ast) || (ast->getType() != ast::AST::Type::Call)) return {};
   auto& c = ast::Call::ref(ast);
   if (c.func->getType() != ast::AST::Type::Token) return {};
   if (extractString(c.func) != "gensym") return {};

   string name = "sym";
   if (c.args) {
      bool first = true;
      for (auto& a : TypedList<ast::FuncArg>(c.args)) {
         if (first)
            first = false;
         else
            return {};
         if (a.getSubType() != ast::FuncArg::SubType::Flat) return {};
         if (a.name && extractString(a.name) != "name") return {};
         if (a.value && a.value->getType() != ast::AST::Type::Token) return {};
         if (a.value) name = extractString(a.value);
      }
   }

   return " " + name + " " + to_string(nextSymbolId++);
}
//---------------------------------------------------------------------------
static Type inferDecimalType(SemanticAnalysis& semana, const string& s) {
   auto iter = s.begin(), limit = s.end();

   // Skip sign
   while ((iter != limit) && (((*iter) == '+') || ((*iter) == '-'))) ++iter;

   // Count digits before dot
   unsigned before = 0, after = 0;
   while ((iter != limit) && (*iter != '.')) {
      ++iter;
      ++before;
   }

   // Count digits after the dot
   if ((iter != limit) && (*iter == '.')) after = limit - iter - 1;

   unsigned precision = before + after, scale = after;
   if (precision < 1) precision = 1;
   if (precision > 38) semana.reportError("decimal value out of range");
   return Type::getDecimal(precision, scale);
}
//---------------------------------------------------------------------------
SemanticAnalysis::ExpressionResult SemanticAnalysis::analyzeLiteral(const ast::Literal& literal)
// Analyze a literal
{
   using SubType = ast::Literal::SubType;
   unique_ptr<algebra::Expression> exp;
   switch (literal.getSubType()) {
      case SubType::Integer: exp = make_unique<algebra::ConstExpression>(extractString(literal.arg), Type::getInteger()); break;
      case SubType::Float: exp = make_unique<algebra::ConstExpression>(extractString(literal.arg), inferDecimalType(*this, extractString(literal.arg))); break;
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
   auto name = extractSymbol(scope, ast.part);
   if (ast.base->getType() != ast::AST::Type::Token) reportError("invalid access to column '" + name + "'");
   auto base = extractSymbol(scope, ast.base);

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
SemanticAnalysis::ExpressionResult SemanticAnalysis::analyzeJoin(const BindingInfo& scope, ExpressionResult& input, const vector<const ast::FuncArg*>& args)
// Analyze a groupby computation
{
   // Analyze the join type
   algebra::Join::JoinType joinType = algebra::Join::JoinType::Inner;
   bool leftOnly = false, rightOnly = false;
   if (args[2]) {
      string jt = symbolArgument(scope, "join", "type", args[2]);
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
   auto other = tableArgument(scope, "join", "table", args[0]);
   BindingInfo resultBinding;
   if (leftOnly || rightOnly) {
      resultBinding = input.getBinding();
   } else {
      resultBinding = move(input.getBinding());
   }
   resultBinding.join(other.getBinding());
   resultBinding.parentScope = &scope;

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
SemanticAnalysis::ExpressionResult SemanticAnalysis::analyzeGroupBy(ExpressionResult& input, const vector<const ast::FuncArg*>& args)
// Analyze a groupby computation
{
   // Analyze the groups
   vector<algebra::GroupBy::Entry> groupBy;
   vector<algebra::GroupBy::Aggregation> aggregates;
   vector<algebra::Map::Entry> results;
   BindingInfo resultBinding;
   resultBinding.parentScope = input.getBinding().parentScope;
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
SemanticAnalysis::ExpressionResult SemanticAnalysis::analyzeAggregate(ExpressionResult& input, const vector<const ast::FuncArg*>& args)
// Analyze an aggregate computation
{
   // Prepare the grouping information
   vector<algebra::GroupBy::Aggregation> aggregates;
   BindingInfo resultBinding;
   resultBinding.parentScope = input.getBinding().parentScope;

   // Compute aggregate
   BindingInfo::GroupByScope gbs(resultBinding, input.getBinding(), aggregates);
   auto g = expressionListArgument(gbs.getBinding(), args[0]);
   auto& result = g.front();
   if (!result.value.isScalar()) reportError("aggregate requires scalar aggregates");

   unique_ptr<algebra::Expression> tree = make_unique<algebra::Aggregate>(move(input.table()), move(aggregates), move(result.value.scalar()));

   return ExpressionResult(move(tree), OrderingInfo::defaultOrder());
}
//---------------------------------------------------------------------------
SemanticAnalysis::ExpressionResult SemanticAnalysis::analyzeDistinct(ExpressionResult& input)
// Analyze a distinct computation
{
   // Remove duplicates with groupby
   vector<algebra::GroupBy::Entry> groupBy;
   vector<algebra::GroupBy::Aggregation> aggregates;
   BindingInfo resultBinding;
   resultBinding.parentScope = input.getBinding().parentScope;
   auto scope = resultBinding.addScope("distinct");
   for (auto& c : input.accessBinding().columns) {
      groupBy.push_back(algebra::GroupBy::Entry{make_unique<algebra::IURef>(c.iu), make_unique<algebra::IU>(c.iu->getType())});
      resultBinding.addBinding(scope, c.name, groupBy.back().iu.get());
   }

   unique_ptr<algebra::Operator> tree = make_unique<algebra::GroupBy>(move(input.table()), move(groupBy), move(aggregates));

   return ExpressionResult(move(tree), move(resultBinding));
}
//---------------------------------------------------------------------------
SemanticAnalysis::ExpressionResult SemanticAnalysis::analyzeSetOperation(const BindingInfo& scope, Functions::Builtin builtin, ExpressionResult& input, const std::vector<const ast::FuncArg*>& args)
// Analyze a set computation
{
   // Name for error reporting
   string name;
   if (builtin == Functions::Builtin::Union)
      name = "union";
   else if (builtin == Functions::Builtin::Except)
      name = "except";
   else
      name = "intersect";

   // Analyze operation type
   algebra::SetOperation::Op op;
   bool all = args[1] ? constBoolArgument(name, "all", args[1]) : false;
   if (builtin == Functions::Builtin::Union)
      op = all ? algebra::SetOperation::Op::UnionAll : algebra::SetOperation::Op::Union;
   else if (builtin == Functions::Builtin::Except)
      op = all ? algebra::SetOperation::Op::ExceptAll : algebra::SetOperation::Op::Except;
   else
      op = all ? algebra::SetOperation::Op::IntersectAll : algebra::SetOperation::Op::Intersect;

   // Analyze the other table
   auto other = tableArgument(scope, name, "table", args[0]);

   // Check that the schema matches
   BindingInfo result;
   result.parentScope = &scope;
   auto resultScope = result.addScope(name);
   if (input.getBinding().columns.size() != other.getBinding().columns.size()) reportError("'"s + name + "' requires tables with identical schema");
   vector<unique_ptr<algebra::Expression>> leftColumns, rightColumns;
   vector<unique_ptr<algebra::IU>> resultIUs;
   for (unsigned index = 0, limit = input.getBinding().columns.size(); index != limit; ++index) {
      auto iu1 = input.getBinding().columns[index].iu;
      auto iu2 = other.getBinding().columns[index].iu;
      leftColumns.push_back(make_unique<algebra::IURef>(iu1));
      rightColumns.push_back(make_unique<algebra::IURef>(iu2));
      auto t1 = iu1->getType(), t2 = iu2->getType();
      if (t1.asNullable() != t2.asNullable()) reportError("'"s + name + "' requires tables with identical schema. Mismatch in column " + to_string(index));
      resultIUs.push_back(make_unique<algebra::IU>(t1.withNullable(t1.isNullable() || t2.isNullable())));
      result.addBinding(resultScope, input.getBinding().columns[index].name, resultIUs.back().get());
   }

   // Construct the result
   return ExpressionResult(make_unique<algebra::SetOperation>(move(input.table()), move(other.table()), move(leftColumns), move(rightColumns), move(resultIUs), op), move(result));
}
//---------------------------------------------------------------------------
SemanticAnalysis::ExpressionResult SemanticAnalysis::analyzeMap(ExpressionResult& input, const vector<const ast::FuncArg*>& args, bool project)
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
   resultBinding.parentScope = input.getBinding().parentScope;
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
SemanticAnalysis::ExpressionResult SemanticAnalysis::analyzeProjectOut(ExpressionResult& input, const vector<const ast::FuncArg*>& args)
// Analyze a map computation
{
   // Compute the expressions
   auto g = expressionListArgument(input.getBinding(), args[0]);
   unordered_set<const algebra::IU*> toRemove;
   for (auto& e : g) {
      if (!e.value.isScalar()) reportError("projectout requires scalar values");
      auto ref = dynamic_cast<algebra::IURef*>(e.value.scalar().get());
      if (!ref) reportError("projectout requires column references");
      toRemove.insert(ref->getIU());
   }

   // Make expressions visible
   BindingInfo resultBinding = move(input.accessBinding());
   erase_if(resultBinding.columns, [&](auto& e) { return toRemove.contains(e.iu); });
   for (auto iter = resultBinding.columnLookup.begin(); iter != resultBinding.columnLookup.end();)
      if (toRemove.contains(iter->second))
         iter = resultBinding.columnLookup.erase(iter);
      else
         ++iter;
   for (auto iter = resultBinding.scopes.begin(); iter != resultBinding.scopes.end();) {
      auto& cols = iter->second.columns;
      if (!cols.empty()) {
         for (auto iter2 = cols.begin(); iter2 != cols.end();)
            if (toRemove.contains(iter2->second))
               iter2 = cols.erase(iter2);
            else
               ++iter2;
         if (cols.empty())
            iter = resultBinding.scopes.erase(iter);
         else
            ++iter;
      } else {
         ++iter;
      }
   }

   return ExpressionResult(move(input.table()), move(resultBinding));
}
//---------------------------------------------------------------------------
SemanticAnalysis::ExpressionResult SemanticAnalysis::analyzeWindow(ExpressionResult& input, const vector<const ast::FuncArg*>& args)
// Analyze a window computation
{
   // frame support TODO
   if (args[3] || args[4] || args[5]) reportError("frames not implemented yet");

   // Compute the expressions
   vector<algebra::Map::Entry> results;
   vector<algebra::GroupBy::Aggregation> aggregates;
   BindingInfo resultBinding = input.getBinding();
   if (args[0]) {
      // Compute aggregates
      BindingInfo::GroupByScope gbs(resultBinding, input.getBinding(), aggregates);
      gbs.isWindow = true;
      auto g = expressionListArgument(gbs.getBinding(), args[0]);
      for (auto& e : g) {
         if (!e.value.isScalar()) reportError("window requires scalar aggregates");
         auto et = e.value.scalar()->getType();
         results.push_back(algebra::GroupBy::Entry{move(e.value.scalar()), make_unique<algebra::IU>(et)});
      }

      // Make aggregates visible
      unsigned slot = 0;
      auto scope = resultBinding.addScope("window");
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

   // Analyze the partition-by clause
   vector<unique_ptr<algebra::Expression>> partitionBy;
   if (args[1]) {
      auto g = expressionListArgument(input.getBinding(), args[1]);
      for (auto& e : g) {
         if (!e.value.isScalar()) reportError("partitionby requires scalar order values");
         partitionBy.push_back(move(e.value.scalar()));
      }
   }

   // Analyze the order-by clause
   vector<algebra::Sort::Entry> order;
   if (args[2]) {
      auto g = expressionListArgument(input.getBinding(), args[2]);
      for (auto& e : g) {
         if (!e.value.isScalar()) reportError("orderby requires scalar order values");
         auto o = e.value.getOrdering();
         order.push_back({move(e.value.scalar()), o.getCollate(), o.isDescending()});
      }
   }

   unique_ptr<algebra::Operator> tree = move(input.table());
   tree = make_unique<algebra::Window>(move(tree), move(aggregates), move(partitionBy), move(order));
   tree = make_unique<algebra::Map>(move(tree), move(results));

   return ExpressionResult(move(tree), move(resultBinding));
}
//---------------------------------------------------------------------------
SemanticAnalysis::ExpressionResult SemanticAnalysis::analyzeOrderBy(ExpressionResult& input, const vector<const ast::FuncArg*>& args)
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
      auto [ptr, ec] = from_chars(v.data(), v.data() + v.size(), result);
      if (ec != errc()) reportError("'" + string(name) + "' requires an integer constant");
      return result;
   };

   // Handle bounds
   optional<uint64_t> limit, offset;
   if (args[1]) limit = handleConstant("limit", args[1]);
   if (args[2]) offset = handleConstant("offset", args[2]);

   return ExpressionResult(make_unique<algebra::Sort>(move(input.table()), move(order), limit, offset), move(input.accessBinding()));
}
//---------------------------------------------------------------------------
saneql::Type SemanticAnalysis::parseSimpleTypeName(const string& name) {
   if (name == "integer") return Type::getInteger();
   if (name == "boolean") return Type::getBool();
   if (name == "date") return Type::getDate();
   if (name == "interval") return Type::getInterval();
   if (name == "text") return Type::getText();
   // XXX todo more types
   reportError("unknown type '" + name + "'");
}
//---------------------------------------------------------------------------
SemanticAnalysis::ExtendedType SemanticAnalysis::analyzeType(const ast::Type& type)
// Analyze a type expression
{
   auto name = extractString(type.name);
   if (type.getSubType() == ast::Type::SubType::Simple) {
      return ExtendedType(parseSimpleTypeName(name));
   } else {
      reportError("invalid type syntax");
   }
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
string SemanticAnalysis::symbolArgument(const BindingInfo& scope, const string& funcName, const string& argName, const ast::FuncArg* arg)
// Handle a symbol argument
{
   if (arg->getSubType() != ast::FuncArg::SubType::Flat) reportError("parameter '" + argName + "' requires a symbol in call to '" + funcName + "'");
   if (auto s = recognizeGensym(arg->value); !s.empty()) return s;
   if (arg->value->getType() != ast::AST::Type::Token) reportError("parameter '" + argName + "' requires a symbol in call to '" + funcName + "'");
   return extractSymbol(scope, arg->value);
}
//---------------------------------------------------------------------------
bool SemanticAnalysis::constBoolArgument(const string& funcName, const string& argName, const ast::FuncArg* arg)
// Handle a const bool argument
{
   if (arg->getSubType() != ast::FuncArg::SubType::Flat) reportError("parameter '" + argName + "' requires a boolean constant in call to '" + funcName + "'");
   if (arg->value->getType() != ast::AST::Type::Literal) reportError("parameter '" + argName + "' requires a boolean constant in call to '" + funcName + "'");
   auto& l = ast::Literal::ref(arg->value);
   if ((l.getSubType() != ast::Literal::SubType::True) && (l.getSubType() != ast::Literal::SubType::False)) reportError("parameter '" + argName + "' requires a boolean constant in call to '" + funcName + "'");
   return (l.getSubType() == ast::Literal::SubType::True);
}
//---------------------------------------------------------------------------
std::string SemanticAnalysis::constStringArgument(const string& funcName, const string& argName, const ast::FuncArg* arg)
// Handle a const bool argument
{
   auto reportTypeError = [&]() { reportError("parameter '" + argName + "' requires a string constant in call to '" + funcName + "'"); };
   if (arg->getSubType() != ast::FuncArg::SubType::Flat) reportTypeError();
   if (arg->value->getType() != ast::AST::Type::Literal) reportTypeError();
   auto& l = ast::Literal::ref(arg->value);
   if (l.getSubType() != ast::Literal::SubType::String) reportTypeError();
   return extractString(l.arg);
}
//---------------------------------------------------------------------------
SemanticAnalysis::ExpressionResult SemanticAnalysis::scalarArgument(const BindingInfo& scope, const string& funcName, const string& argName, const ast::FuncArg* arg)
// Handle a scalar argument
{
   if (arg->getSubType() != ast::FuncArg::SubType::Flat) reportError("parameter '" + argName + "' requires a scalar in call to '" + funcName + "'");
   auto r = analyzeExpression(scope, arg->value);
   if (!r.isScalar()) reportError("parameter '" + argName + "' requires a scalar in call to '" + funcName + "'");
   return r;
}
//---------------------------------------------------------------------------
vector<SemanticAnalysis::ExpressionResult> SemanticAnalysis::scalarArgumentList(const BindingInfo& scope, const string& funcName, const string& argName, const ast::FuncArg* arg)
// Handle a list of scalar arguments
{
   // As convenience we also accept a single value as list
   vector<ExpressionResult> result;
   if (arg->getSubType() == ast::FuncArg::SubType::Flat) {
      result.push_back(scalarArgument(scope, funcName, argName, arg));
      return result;
   }

   if (arg->getSubType() != ast::FuncArg::SubType::List) reportError("parameter '" + argName + "' requires a list of scalars in call to '" + funcName + "'");
   for (auto& e : TypedList<ast::FuncArgNamed>(arg->value)) {
      if (e.getSubType() != ast::FuncArgNamed::SubType::Flat) reportError("parameter '" + argName + "' requires a list of scalars in call to '" + funcName + "'");
      auto r = analyzeExpression(scope, e.value);
      if (!r.isScalar()) reportError("parameter '" + argName + "' requires a list of scalars in call to '" + funcName + "'");
      result.push_back(move(r));
   }
   return result;
}
//---------------------------------------------------------------------------
SemanticAnalysis::ExpressionResult SemanticAnalysis::tableArgument(const BindingInfo& scope, const string& funcName, const string& argName, const ast::FuncArg* arg)
// Handle a table argument
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
   // Accept alias variables as convenience feature
   auto recognizeAliasVar = [&](const ast::AST* ast) -> const vector<const algebra::IU*>* {
      if ((!ast) || (ast->getType() != ast::AST::Type::Token)) return nullptr;

      auto name = extractSymbol(scope, ast);

      // A column reference?
      if (scope.columnLookup.contains(name)) return nullptr;

      // An alias reference?
      if (auto iter = scope.aliases.find(name); iter != scope.aliases.end()) {
         if (iter->second.ambiguous) reportError("'" + name + "' is ambiguous");
         return &(iter->second.columns);
      }
      return nullptr;
   };

   vector<SemanticAnalysis::ExpressionArg> result;
   // As convenience feature we also support single expressions
   if (arg->getSubType() == ast::FuncArg::SubType::Flat) {
      result.push_back({inferName(arg->value), analyzeExpression(scope, arg->value)});
   } else {
      for (auto& a : TypedList<ast::FuncArgNamed>(arg->value)) {
         if (a.getSubType() != ast::FuncArgNamed::SubType::Flat) reportError("nested expression list not allowed here");
         if (auto ac = recognizeAliasVar(a.value)) {
            unordered_map<const algebra::IU*, string> names;
            for (auto& c : scope.columns)
               names[c.iu] = c.name;
            for (auto e : *ac) {
               result.push_back({names[e], ExpressionResult(make_unique<algebra::IURef>(e), OrderingInfo::defaultOrder())});
            }
         } else {
            auto e = analyzeExpression(scope, a.value);
            string name;
            if (a.name)
               name = extractSymbol(scope, a.name);
            else
               name = inferName(a.value);
            result.push_back({move(name), move(e)});
         }
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
   enforceComparable(a.scalar(), b.scalar());
}
//---------------------------------------------------------------------------
void SemanticAnalysis::enforceComparable(unique_ptr<algebra::Expression>& sa, unique_ptr<algebra::Expression>& sb)
// Make sure two values are comparable
{
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
   optional<unsigned> letSlot;
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
      if (letLookup.contains(name)) {
         if (auto slot = letLookup.find(name)->second; slot < letScopeLimit) {
            letSlot = slot;
            sig = &lets[slot].signature;
         }
      }
      if (!sig) {
         sig = Functions::freeFunctions.lookup(name);
      }
      if (!sig) reportError("function '" + name + "' not found");
   }

   // Assign arguments to positions
   vector<const ast::FuncArg*> args;
   bool hadNamed = false;
   for (auto& a : TypedList<ast::FuncArg>(call.args)) {
      // Check for names arguments
      if (a.name) {
         if (!hadNamed) {
            args.resize(sig->arguments.size(), nullptr);
            hadNamed = true;
         }
         auto argName = extractSymbol(scope, a.name);
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

   // Handle function calls
   if (letSlot.has_value()) {
      SetLetScopeLimit setLetScopeLimit(this, *letSlot);
      auto& let = lets[*letSlot];
      BindingInfo callScope;
      for (unsigned index = 0, limit = sig->arguments.size(); index != limit; ++index) {
         auto val = args[index] ? args[index]->value : let.defaultValues[index];
         switch (sig->arguments[index].type.category) {
            case Functions::TypeCategory::Expression: callScope.registerArgument(sig->arguments[index].name, val, nullptr); break;
            case Functions::TypeCategory::Scalar: callScope.registerArgument(sig->arguments[index].name, val, &scope); break;
            case Functions::TypeCategory::Table: callScope.registerArgument(sig->arguments[index].name, val, &scope); break;
            case Functions::TypeCategory::Symbol: {
               string sn = recognizeGensym(val);
               if (sn.empty()) {
                  if (val->getType() != ast::AST::Type::Token) reportError("parameter '" + sig->arguments[index].name + "' requires a symbol in call to '" + name + "'");
                  sn = extractSymbol(scope, val);
               }
               callScope.registerSymbolArgument(sig->arguments[index].name, sn);
               break;
            }
            default: invalidAST();
         }
      }
      auto res = analyzeExpression(callScope, let.body);
      if (res.isTable()) res.accessBinding().parentScope = nullptr;
      return res;
   }

   // Logic for handling aggregates
   auto handleAggregate = [&](algebra::GroupBy::Op op, algebra::GroupBy::Op distinctOp) {
      auto gbs = scope.getGroupByScope();
      if (!gbs) reportError("aggregate '" + name + "' can only be used in group by computations");

      if ((args.size() > 1) && args[1] && constBoolArgument(name, "distinct", args[1])) {
         op = distinctOp;
      }

      ExpressionResult exp(unique_ptr<algebra::Expression>(), OrderingInfo::defaultOrder());
      Type resultType = Type::getInteger();
      if (op != algebra::GroupBy::Op::CountStar) {
         exp = scalarArgument(gbs->preAggregation, "aggregate", name, args[0]);
         if ((op != algebra::GroupBy::Op::Min) && (op != algebra::GroupBy::Op::Max) && (!isNumeric(exp.scalar()->getType()))) reportError("aggregate '" + name + "' requires a numerical argument");
         resultType = exp.scalar()->getType(); // TODO fix type inference
         if ((op == algebra::GroupBy::Op::Count) || (op == algebra::GroupBy::Op::CountDistinct)) resultType = Type::getInteger();
      }
      gbs->aggregations.push_back({move(exp.scalar()), make_unique<algebra::IU>(resultType), op});
      return ExpressionResult(make_unique<algebra::IURef>(gbs->aggregations.back().iu.get()), OrderingInfo::defaultOrder());
   };

   // Logic for handling window functions
   auto handleWindow = [&](algebra::Window::Op op) {
      using enum algebra::Window::Op;
      auto gbs = scope.getGroupByScope();
      if ((!gbs) || (!gbs->isWindow)) reportError("aggregate '" + name + "' can only be used in window computations");
      ExpressionResult exp(unique_ptr<algebra::Expression>(), OrderingInfo::defaultOrder());
      Type resultType = Type::getUnknown();
      switch (op) {
         case RowNumber: // ops without arg
         case Rank:
         case DenseRank: {
            resultType = Type::getInteger();
            break;
         }
         case NTile: // ops with arg
         case Lead:
         case Lag:
         case FirstValue:
         case LastValue: {
            exp = scalarArgument(gbs->preAggregation, "window", name, args[0]);
            resultType = exp.scalar()->getType();
            break;
         }
         default: reportError("aggregate '" + name + "' cannot be used in window computations");
      }
      if (op == NTile && exp.scalar()->getType().getType() != Type::Integer) reportError("ntile requires an integer argument");
      algebra::GroupBy::Aggregation result{move(exp.scalar()), make_unique<algebra::IU>(resultType), static_cast<algebra::GroupBy::Op>(op)};
      if (op == Lead || op == Lag) {
         if (args[1]) { // 'offset' value for lead/lag
            auto offsetArg = scalarArgument(gbs->preAggregation, "window", "offset", args[1]);
            if (offsetArg.scalar()->getType() != Type::getInteger()) reportError("offset value for lead/lag must be an integer");
            result.parameters.push_back(move(offsetArg.scalar()));
         } else {
            result.parameters.push_back(make_unique<algebra::ConstExpression>("1", Type::getInteger())); // default offset
         }
         if (args[2]) { // 'default' value for lead/lag
            auto defaultArg = scalarArgument(gbs->preAggregation, "window", "default", args[2]); // TODO allow implicit type conversion?
            if (defaultArg.scalar()->getType() != resultType) reportError("default value for lead/lag must have the same type as the lead/lag expression: " + defaultArg.scalar()->getType().getName() + " vs. " + resultType.getName()  );
            result.parameters.push_back(move(defaultArg.scalar()));
         } else {
            result.parameters.push_back(make_unique<algebra::ConstExpression>(nullptr, resultType)); // null
         }
      }
      gbs->aggregations.push_back(move(result));
      return ExpressionResult(make_unique<algebra::IURef>(gbs->aggregations.back().iu.get()), OrderingInfo::defaultOrder());
   };

   // Handle the functions
   using Builtin = Functions::Builtin;
   switch (sig->builtin) {
      case Builtin::Asc: base->accessOrdering().markAscending(); return move(*base);
      case Builtin::Desc: base->accessOrdering().markDescending(); return move(*base);
      case Builtin::Collate: base->accessOrdering().setCollate(OrderingInfo::lookupCollate(symbolArgument(scope, name, sig->arguments[0].name, args[0]))); return move(*base);
      case Builtin::Is: {
         auto arg = scalarArgument(scope, name, sig->arguments[0].name, args[0]);
         enforceComparable(*base, arg);
         auto order = unifyCollate(base->getOrdering(), arg.getOrdering());
         return ExpressionResult(make_unique<algebra::ComparisonExpression>(move(base->scalar()), move(arg.scalar()), algebra::ComparisonExpression::Is, order.getCollate()), OrderingInfo::defaultOrder());
      }
      case Builtin::Between: {
         auto lower = scalarArgument(scope, name, sig->arguments[0].name, args[0]);
         auto upper = scalarArgument(scope, name, sig->arguments[1].name, args[1]);
         enforceComparable(*base, lower);
         enforceComparable(*base, upper);
         auto order = unifyCollate(unifyCollate(base->getOrdering(), lower.getOrdering()), upper.getOrdering());
         return ExpressionResult(make_unique<algebra::BetweenExpression>(move(base->scalar()), move(lower.scalar()), move(upper.scalar()), order.getCollate()), OrderingInfo::defaultOrder());
      }
      case Builtin::In: {
         auto values = scalarArgumentList(scope, name, sig->arguments[0].name, args[0]);
         if (values.empty()) return ExpressionResult(make_unique<algebra::ConstExpression>("false", Type::getBool()), OrderingInfo::defaultOrder());
         auto order = base->getOrdering();
         vector<unique_ptr<algebra::Expression>> vals;
         for (auto& v : values) {
            enforceComparable(*base, v);
            order = unifyCollate(order, v.getOrdering());
            vals.push_back(move(v.scalar()));
         }
         return ExpressionResult(make_unique<algebra::InExpression>(move(base->scalar()), move(vals), order.getCollate()), OrderingInfo::defaultOrder());
      }
      case Builtin::Like: {
         auto arg = scalarArgument(scope, name, sig->arguments[0].name, args[0]);
         if ((!isString(base->scalar()->getType())) || (!isString(arg.scalar()->getType()))) reportError("'like' requires string arguments");
         auto order = unifyCollate(base->getOrdering(), arg.getOrdering());
         return ExpressionResult(make_unique<algebra::ComparisonExpression>(move(base->scalar()), move(arg.scalar()), algebra::ComparisonExpression::Like, order.getCollate()), OrderingInfo::defaultOrder());
      }
      case Builtin::Substr: {
         unique_ptr<algebra::Expression> from, len;
         if ((!args[0]) && (!args[1])) reportError("'substr' requires numeric arguments");
         if (args[0]) {
            from = move(scalarArgument(scope, name, sig->arguments[0].name, args[0]).scalar());
            if (!isNumeric(from->getType())) reportError("'substr' requires numeric arguments");
         }
         if (args[1]) {
            len = move(scalarArgument(scope, name, sig->arguments[1].name, args[1]).scalar());
            if (!isNumeric(len->getType())) reportError("'substr' requires numeric arguments");
         }
         return ExpressionResult(make_unique<algebra::SubstrExpression>(move(base->scalar()), move(from), move(len)), OrderingInfo::defaultOrder());
      }
      case Builtin::Extract: {
         auto partName = symbolArgument(scope, name, sig->arguments[0].name, args[0]);
         algebra::ExtractExpression::Part part;
         if (partName == "year") {
            part = algebra::ExtractExpression::Part::Year;
         } else if (partName == "month") {
            part = algebra::ExtractExpression::Part::Month;
         } else if (partName == "day") {
            part = algebra::ExtractExpression::Part::Day;
         } else {
            reportError("unknown date part '" + partName + "'");
         }
         return ExpressionResult(make_unique<algebra::ExtractExpression>(move(base->scalar()), part), OrderingInfo::defaultOrder());
      }
      case Builtin::Filter: {
         auto cond = scalarArgument(base->getBinding(), name, sig->arguments[0].name, args[0]);
         if (cond.scalar()->getType().getType() != Type::Bool) reportError("'filter' requires a boolean filter condition");
         return ExpressionResult(make_unique<algebra::Select>(move(base->table()), move(cond.scalar())), move(base->accessBinding()));
      }
      case Builtin::Join: return analyzeJoin(scope, *base, args);
      case Builtin::GroupBy: return analyzeGroupBy(*base, args);
      case Builtin::Aggregate: return analyzeAggregate(*base, args);
      case Builtin::Distinct: return analyzeDistinct(*base);
      case Builtin::Union: return analyzeSetOperation(scope, sig->builtin, *base, args);
      case Builtin::Except: return analyzeSetOperation(scope, sig->builtin, *base, args);
      case Builtin::Intersect: return analyzeSetOperation(scope, sig->builtin, *base, args);
      case Builtin::Window: return analyzeWindow(*base, args);
      case Builtin::OrderBy: return analyzeOrderBy(*base, args);
      case Builtin::Map: return analyzeMap(*base, args, false);
      case Builtin::Project: return analyzeMap(*base, args, true);
      case Builtin::ProjectOut: return analyzeProjectOut(*base, args);
      case Builtin::AggCount: return handleAggregate(args[0] ? algebra::GroupBy::Op::Count : algebra::GroupBy::Op::CountStar, args[0] ? algebra::GroupBy::Op::CountDistinct : algebra::GroupBy::Op::CountStar);
      case Builtin::AggSum: return handleAggregate(algebra::GroupBy::Op::Sum, algebra::GroupBy::Op::SumDistinct);
      case Builtin::AggAvg: return handleAggregate(algebra::GroupBy::Op::Avg, algebra::GroupBy::Op::AvgDistinct);
      case Builtin::AggMin: return handleAggregate(algebra::GroupBy::Op::Min, algebra::GroupBy::Op::Min);
      case Builtin::AggMax: return handleAggregate(algebra::GroupBy::Op::Max, algebra::GroupBy::Op::Max);
      case Builtin::WindowRowNumber: return handleWindow(algebra::Window::Op::RowNumber);
      case Builtin::WindowRank: return handleWindow(algebra::Window::Op::Rank);
      case Builtin::WindowDenseRank: return handleWindow(algebra::Window::Op::DenseRank);
      case Builtin::WindowNTile: return handleWindow(algebra::Window::Op::NTile);
      case Builtin::WindowLead: return handleWindow(algebra::Window::Op::Lead);
      case Builtin::WindowLag: return handleWindow(algebra::Window::Op::Lag);
      case Builtin::WindowFirstValue: return handleWindow(algebra::Window::Op::FirstValue);
      case Builtin::WindowLastValue: return handleWindow(algebra::Window::Op::LastValue);
      case Builtin::Table: return analyzeTableConstruction(scope, args[0]);
      case Builtin::Case: return analyzeCase(scope, args);
      case Builtin::As: {
         auto& b = base->accessBinding();
         auto newName = symbolArgument(scope, name, sig->arguments[0].name, args[0]);
         b.scopes.clear();
         b.scopes[newName].columns = b.columnLookup;
         return move(*base);
      }
      case Builtin::Alias: {
         auto& b = base->accessBinding();
         auto newName = symbolArgument(scope, name, sig->arguments[0].name, args[0]);
         auto& a = b.aliases[newName];
         a.ambiguous = false;
         a.columns.clear();
         a.columns.reserve(b.columns.size());
         for (auto& c : b.columns) a.columns.push_back(c.iu);
         return move(*base);
      }
      case Builtin::Gensym: reportError("gensym is currently only supported in binding contexts");
         // return ExpressionResult(make_unique<algebra::Select>(move(base->table()), move(cond.scalar())), move(base->accessBinding()));
      case Builtin::ForeignCall: {
         using CallType = algebra::ForeignCall::CallType;
         auto functionName = constStringArgument("foreigncall", sig->arguments[0].name, args[0]);
         auto returnType = parseSimpleTypeName(symbolArgument(scope, name, sig->arguments[1].name, args[1]));
         std::vector<std::unique_ptr<algebra::Expression>> functionArgs;
         if (args[2]) { // function arguments
            auto analyzedArgs = expressionListArgument(scope, args[2]);
            for (auto& r : analyzedArgs) {
               auto& v = r.value;
               if (!v.isScalar()) reportError("foreigncall arguments must be scalar");
               functionArgs.push_back(move(v.scalar()));
            }
         }
         CallType callType = algebra::ForeignCall::defaultType();
         if (args[3]) { // type specifier
            string readType = symbolArgument(scope, name, sig->arguments[3].name, args[3]);
            if (readType == "function") callType = CallType::Function;
            else if (readType == "operator" || readType == "leftassoc") callType = CallType::LeftAssocOperator;
            else if (readType == "rightassoc") callType = CallType::RightAssocOperator;
            else reportError("unknown foreigncall call type '" + readType + "'");
         }
         if (callType == CallType::LeftAssocOperator || callType == CallType::RightAssocOperator) {
            if (functionArgs.size() < 2) reportError("foreigncall with operator type requires at least two arguments");
         }
         return ExpressionResult(make_unique<algebra::ForeignCall>(move(functionName), move(returnType), move(functionArgs), callType), OrderingInfo::defaultOrder());
      }
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
            columnNames.push_back(extractSymbol(scope, arg.name));
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
}
//---------------------------------------------------------------------------
SemanticAnalysis::ExpressionResult SemanticAnalysis::analyzeCase(const BindingInfo& scope, const vector<const ast::FuncArg*>& args)
// Analyze a case expression
{
   // Collect the cases
   if (args[0]->getSubType() != ast::FuncArg::SubType::List)
      reportError("case requires a list of cases");
   vector<pair<unique_ptr<algebra::Expression>, unique_ptr<algebra::Expression>>> cases;
   for (auto& a : TypedList<ast::FuncArgNamed>(args[0]->value)) {
      if (a.getSubType() != ast::FuncArgNamed::SubType::Case) reportError("case requries cases of the form 'a => b'");
      auto v = analyzeExpression(scope, a.name);
      if (!v.isScalar()) reportError("case requires a scalar case value");
      auto r = analyzeExpression(scope, a.value);
      if (!r.isScalar()) reportError("case requires a scalar case result");
      cases.emplace_back(move(v.scalar()), move(r.scalar()));
   }
   if (cases.empty()) reportError("case requires a list of cases");
   unique_ptr<algebra::Expression> defaultValue(make_unique<algebra::ConstExpression>(nullptr, cases.front().second->getType().asNullable()));
   if (args[1])
      defaultValue = move(scalarArgument(scope, "case", "else", args[1]).scalar());

   // Compute the result type
   Type resultType = cases.front().second->getType().withNullable(defaultValue->getType().isNullable() | any_of(cases.begin(), cases.end(), [](auto& c) { return c.second->getType().isNullable(); }));
   // TODO type unification
   for (auto& c : cases)
      if (c.second->getType().asNullable() != resultType.asNullable())
         c.second = make_unique<algebra::CastExpression>(move(c.second), resultType);
   if (defaultValue->getType().asNullable() != resultType.asNullable())
      defaultValue = make_unique<algebra::CastExpression>(move(defaultValue), resultType);

   // Distinguish between simple and searched case
   if (args[2]) {
      auto simpleValue = scalarArgument(scope, "case", "search", args[2]);
      for (auto& c : cases)
         enforceComparable(simpleValue.scalar(), c.first);
      return ExpressionResult(make_unique<algebra::SimpleCaseExpression>(move(simpleValue.scalar()), move(cases), move(defaultValue)), OrderingInfo::defaultOrder());
   } else {
      for (auto& c : cases)
         if (c.first->getType().getType() != Type::Bool)
            reportError("case requires boolean case conditions");
      return ExpressionResult(make_unique<algebra::SearchedCaseExpression>(move(cases), move(defaultValue)), OrderingInfo::defaultOrder());
   }
}
//---------------------------------------------------------------------------
SemanticAnalysis::ExpressionResult SemanticAnalysis::analyzeToken(const BindingInfo& scope, const ast::AST* exp)
// Analyze a token
{
   auto name = extractSymbol(scope, exp);

   // A column reference?
   if (auto iu = scope.lookup(name)) {
      if (iu == BindingInfo::ambiguousIU) reportError("'" + name + "' is ambiguous");
      return ExpressionResult(make_unique<algebra::IURef>(iu), OrderingInfo::defaultOrder());
   }

   // An argument?
   for (auto iter = &scope; iter; iter = iter->parentScope) {
      if (auto arg = iter->lookupArgument(name); arg.isValue()) {
         auto res = analyzeExpression(arg.getValueScope() ? *arg.getValueScope() : scope, arg.getValueRef());
         if (res.isTable()) res.accessBinding().parentScope = &scope;
         return res;
      }
   }

   // A let?
   if (auto iter = letLookup.find(name); iter != letLookup.end()) {
      auto& l = lets[iter->second];
      if (!l.signature.arguments.empty()) reportError("'" + name + "' is a function");
      SetLetScopeLimit setLetScopeLimit(this, iter->second);
      return analyzeExpression(BindingInfo::rootScope(), l.body);
   }

   // Table scan?
   auto table = schema.lookupTable(name);
   if (!table) reportError("unknown table '" + name + "'");

   auto bindingName = getInternalName(name);
   BindingInfo binding;
   binding.parentScope = &scope;
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
         auto name = extractRawSymbol(a.name);
         if (argNames.contains(name)) reportError("duplicate function argument '" + name + "'");
         argNames.insert(name);
         Functions::ArgumentType argType = Functions::TypeCategory::Scalar;
         if (a.type) {
            auto& at = ast::Type::ref(a.type);
            if (at.getSubType() != ast::Type::SubType::Simple) reportError("complex argument types not implemented yet");
            auto tn = extractRawSymbol(at.name);
            if (tn == "table") {
               argType = Functions::TypeCategory::Table;
            } else if (tn == "expression") {
               argType = Functions::TypeCategory::Expression;
            } else if (tn == "symbol") {
               argType = Functions::TypeCategory::Symbol;
            } else {
               reportError("unsupported argument type '" + tn + "'");
            }
         }
         args.emplace_back(name, argType, !!a.value);
         defaultValues.push_back(a.value);
      }
   }

   // Register the let
   auto name = extractRawSymbol(ast.name);
   if (letLookup.contains(name)) reportError("duplicate let '" + name + "'");
   lets.emplace_back(Functions::Signature({}, move(args)), move(defaultValues), ast.body);
   letLookup[name] = lets.size() - 1;
}
//---------------------------------------------------------------------------
}
//---------------------------------------------------------------------------
