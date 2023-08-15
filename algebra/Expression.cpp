#include "algebra/Expression.hpp"
#include "algebra/Operator.hpp"
#include "sql/SQLWriter.hpp"
#include <algorithm>
#include <utility>
//---------------------------------------------------------------------------
// (c) 2023 Thomas Neumann
//---------------------------------------------------------------------------
using namespace std;
//---------------------------------------------------------------------------
namespace saneql::algebra {
//---------------------------------------------------------------------------
Expression::~Expression()
// Destructor
{
}
//---------------------------------------------------------------------------
void Expression::generateOperand(SQLWriter& out)
// Generate SQL in a form that is suitable as operand
{
   out.write("(");
   generate(out);
   out.write(")");
}
//---------------------------------------------------------------------------
IURef::IURef(const IU* iu)
   : Expression(iu->getType()), iu(iu)
// Constructor
{
}
//---------------------------------------------------------------------------
void IURef::generate(SQLWriter& out)
// Generate SQL
{
   out.writeIU(iu);
}
//---------------------------------------------------------------------------
void ConstExpression::generate(SQLWriter& out)
// Generate SQL
{
   if (null) {
      out.write("NULL");
   } else {
      auto type = getType();
      if ((type.getType() != Type::Char) && (type.getType() != Type::Varchar) && (type.getType() != Type::Text)) {
         out.write("cast(");
         out.writeString(value);
         out.write(" as ");
         out.writeType(type);
         out.write(")");
      } else {
         out.writeString(value);
      }
   }
}
//---------------------------------------------------------------------------
void CastExpression::generate(SQLWriter& out)
// Generate SQL
{
   out.write("cast(");
   input->generate(out);
   out.write(" as ");
   out.writeType(getType());
   out.write(")");
}
//---------------------------------------------------------------------------
ComparisonExpression::ComparisonExpression(unique_ptr<Expression> left, unique_ptr<Expression> right, Mode mode, Collate collate)
   : Expression(Type::getBool().withNullable((mode != Mode::Is) && (mode != Mode::IsNot) && (left->getType().isNullable() || right->getType().isNullable()))), left(move(left)), right(move(right)), mode(mode), collate(collate)
// Constructor
{
}
//---------------------------------------------------------------------------
void ComparisonExpression::generate(SQLWriter& out)
// Generate SQL
{
   left->generateOperand(out);
   switch (mode) {
      case Mode::Equal: out.write(" = "); break;
      case Mode::NotEqual: out.write(" <> "); break;
      case Mode::Is: out.write(" is not distinct from "); break;
      case Mode::IsNot: out.write(" is distinct from "); break;
      case Mode::Less: out.write(" < "); break;
      case Mode::LessOrEqual: out.write(" <= "); break;
      case Mode::Greater: out.write(" > "); break;
      case Mode::GreaterOrEqual: out.write(" >= "); break;
      case Mode::Like: out.write(" like "); break;
   }
   right->generateOperand(out);
}
//---------------------------------------------------------------------------
BetweenExpression::BetweenExpression(unique_ptr<Expression> base, unique_ptr<Expression> lower, unique_ptr<Expression> upper, Collate collate)
   : Expression(Type::getBool().withNullable(base->getType().isNullable() || lower->getType().isNullable() || upper->getType().isNullable())), base(move(base)), lower(move(lower)), upper(move(upper)), collate(collate)
// Constructor
{
}
//---------------------------------------------------------------------------
void BetweenExpression::generate(SQLWriter& out)
// Generate SQL
{
   base->generateOperand(out);
   out.write(" between ");
   lower->generateOperand(out);
   out.write(" and ");
   upper->generateOperand(out);
}
//---------------------------------------------------------------------------
InExpression::InExpression(unique_ptr<Expression> probe, vector<unique_ptr<Expression>> values, Collate collate)
   : Expression(Type::getBool().withNullable(probe->getType().isNullable() || any_of(values.begin(), values.end(), [](auto& e) { return e->getType().isNullable(); }))), probe(move(probe)), values(move(values)), collate(collate)
// Constructor
{
}
//---------------------------------------------------------------------------
void InExpression::generate(SQLWriter& out)
// Generate SQL
{
   probe->generateOperand(out);
   out.write(" in (");
   bool first = true;
   for (auto& v : values) {
      if (first)
         first = false;
      else
         out.write(", ");
      v->generate(out);
   }
   out.write(")");
}
//---------------------------------------------------------------------------
BinaryExpression::BinaryExpression(unique_ptr<Expression> left, unique_ptr<Expression> right, Type resultType, Operation op)
   : Expression(resultType), left(move(left)), right(move(right)), op(op)
// Constructor
{
}
//---------------------------------------------------------------------------
void BinaryExpression::generate(SQLWriter& out)
// Generate SQL
{
   left->generateOperand(out);
   switch (op) {
      case Operation::Plus: out.write(" + "); break;
      case Operation::Minus: out.write(" - "); break;
      case Operation::Mul: out.write(" * "); break;
      case Operation::Div: out.write(" / "); break;
      case Operation::Mod: out.write(" % "); break;
      case Operation::Power: out.write(" ^ "); break;
      case Operation::Concat: out.write(" || "); break;
      case Operation::And: out.write(" and "); break;
      case Operation::Or: out.write(" or "); break;
   }
   right->generateOperand(out);
}
//---------------------------------------------------------------------------
UnaryExpression::UnaryExpression(unique_ptr<Expression> input, Type resultType, Operation op)
   : Expression(resultType), input(move(input)), op(op)
// Constructor
{
}
//---------------------------------------------------------------------------
void UnaryExpression::generate(SQLWriter& out)
// Generate SQL
{
   switch (op) {
      case Operation::Plus: out.write("+"); break;
      case Operation::Minus: out.write("-"); break;
      case Operation::Not: out.write(" not "); break;
   }
   input->generateOperand(out);
}
//---------------------------------------------------------------------------
ExtractExpression::ExtractExpression(unique_ptr<Expression> input, Part part)
   : Expression(Type::getInteger().withNullable(input->getType().isNullable())), input(move(input)), part(part)
// Constructor
{
}
//---------------------------------------------------------------------------
void ExtractExpression::generate(SQLWriter& out)
// Generate SQL
{
   out.write("extract(");
   switch (part) {
      case Part::Year: out.write("year"); break;
      case Part::Month: out.write("month"); break;
      case Part::Day: out.write("day"); break;
   }
   out.write(" from ");
   input->generateOperand(out);
   out.write(")");
}
//---------------------------------------------------------------------------
SubstrExpression::SubstrExpression(unique_ptr<Expression> value, unique_ptr<Expression> from, unique_ptr<Expression> len)
   : Expression(value->getType().withNullable(value->getType().isNullable() || (from ? from->getType().isNullable() : false) || (len ? len->getType().isNullable() : false))), value(move(value)), from(move(from)), len(move(len))
// Constructor
{
}
//---------------------------------------------------------------------------
void SubstrExpression::generate(SQLWriter& out)
// Generate SQL
{
   out.write("substring(");
   value->generate(out);
   if (from) {
      out.write(" from ");
      from->generate(out);
   }
   if (len) {
      out.write(" for ");
      len->generate(out);
   }
   out.write(")");
}
//---------------------------------------------------------------------------
SimpleCaseExpression::SimpleCaseExpression(unique_ptr<Expression> value, Cases cases, unique_ptr<Expression> defaultValue)
   : Expression(defaultValue->getType()), value(move(value)), cases(move(cases)), defaultValue(move(defaultValue))
// Constructor
{
}
//---------------------------------------------------------------------------
void SimpleCaseExpression::generate(SQLWriter& out)
// Generate SQL
{
   out.write("case ");
   value->generateOperand(out);
   for (auto& c : cases) {
      out.write(" when ");
      c.first->generate(out);
      out.write(" then ");
      c.second->generate(out);
   }
   out.write(" else ");
   defaultValue->generate(out);
   out.write(" end");
}
//---------------------------------------------------------------------------
SearchedCaseExpression::SearchedCaseExpression(Cases cases, unique_ptr<Expression> defaultValue)
   : Expression(defaultValue->getType()), cases(move(cases)), defaultValue(move(defaultValue))
// Constructor
{
}
//---------------------------------------------------------------------------
void SearchedCaseExpression::generate(SQLWriter& out)
// Generate SQL
{
   out.write("case");
   for (auto& c : cases) {
      out.write(" when ");
      c.first->generate(out);
      out.write(" then ");
      c.second->generate(out);
   }
   out.write(" else ");
   defaultValue->generate(out);
   out.write(" end");
}
//---------------------------------------------------------------------------
Aggregate::Aggregate(unique_ptr<Operator> input, vector<Aggregation> aggregates, unique_ptr<Expression> computation)
   : Expression(computation->getType()), input(move(input)), aggregates(move(aggregates)), computation(move(computation))
// Constructor
{
}
//---------------------------------------------------------------------------
void Aggregate::generate(SQLWriter& out)
// Generate SQL
{
   out.write("(select ");
   computation->generate(out);
   if (!aggregates.empty()) {
      out.write(" from (select ");
      bool first = true;
      for (auto& a : aggregates) {
         if (first)
            first = false;
         else
            out.write(", ");
         switch (a.op) {
            case Op::CountStar: out.write("count(*)"); break;
            case Op::Count: out.write("count("); break;
            case Op::CountDistinct: out.write("count(distinct "); break;
            case Op::Sum: out.write("sum("); break;
            case Op::SumDistinct: out.write("sum(distinct "); break;
            case Op::Avg: out.write("avg("); break;
            case Op::AvgDistinct: out.write("avg(distinct "); break;
            case Op::Min: out.write("min("); break;
            case Op::Max: out.write("max("); break;
         }
         if (a.op != Op::CountStar) {
            a.value->generate(out);
            out.write(")");
         }
         out.write(" as ");
         out.writeIU(a.iu.get());
      }
      out.write(" from ");
      input->generate(out);
      out.write(" s");
      out.write(") s");
   }
   out.write(")");
}
//---------------------------------------------------------------------------
ForeignCall::ForeignCall(string name, Type returnType, vector<unique_ptr<Expression>> arguments, CallType callType)
   : Expression(returnType), name(std::move(name)), arguments(std::move(arguments)), callType(callType)
// Constructor
{
}
//---------------------------------------------------------------------------
void ForeignCall::generate(SQLWriter& out) {
   switch (callType) {
      case CallType::Function: {
         out.write(name);
         out.write("(");
         bool first = true;
         for (auto& a : arguments) {
            if(!std::exchange(first, false)) out.write(", ");
            a->generate(out);
         }
         out.write(")");
         break;
      }
      case CallType::LeftAssocOperator: { // ((a op b) op c) op d
         for (auto i = 0u; i != arguments.size() - 2; ++i) {
            out.write("(");
         }
         arguments[0]->generateOperand(out);
         for (auto i = 1u; i != arguments.size(); ++i) {
            out.write(" ");
            out.write(name);
            out.write(" ");
            arguments[i]->generateOperand(out);
            if (i != arguments.size() - 1) {
               out.write(")");
            }
         }
         break;
      }
      case CallType::RightAssocOperator: { // a op (b op (c op d))
         for (auto i = 0u; i != arguments.size(); ++i) {
            arguments[i]->generateOperand(out);
            if (i != arguments.size() - 1) {
               out.write(" ");
               out.write(name);
               out.write(" ");
               out.write("(");
            }
         }
         for (auto i = 0u; i != arguments.size() - 2; ++i) {
            out.write(")");
         }
         break;
      }
   }
}
//---------------------------------------------------------------------------
}
//---------------------------------------------------------------------------
