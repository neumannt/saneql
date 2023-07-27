#include "algebra/Expression.hpp"
#include "algebra/Operator.hpp"
#include "sql/SQLWriter.hpp"
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
            case Op::Count: out.write("count"); break;
            case Op::Sum: out.write("sum"); break;
            case Op::Avg: out.write("avg"); break;
            case Op::Min: out.write("min"); break;
            case Op::Max: out.write("max"); break;
         }
         if (a.op != Op::CountStar) {
            out.write("(");
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
}
//---------------------------------------------------------------------------
