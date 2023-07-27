#ifndef H_saneql_Expression
#define H_saneql_Expression
//---------------------------------------------------------------------------
#include "infra/Schema.hpp"
#include <memory>
//---------------------------------------------------------------------------
// SaneQL
// (c) 2023 Thomas Neumann
// SPDX-License-Identifier: BSD-3-Clause
//---------------------------------------------------------------------------
namespace saneql {
//---------------------------------------------------------------------------
enum class Collate : uint8_t;
class SQLWriter;
//---------------------------------------------------------------------------
namespace algebra {
//---------------------------------------------------------------------------
class IU;
class Operator;
//---------------------------------------------------------------------------
/// Base class for expressions
class Expression {
   private:
   /// The type
   Type type;

   public:
   /// Constructor
   explicit Expression(Type type) : type(type) {}
   /// Destructor
   virtual ~Expression();

   /// Get the result type
   Type getType() const { return type; }

   /// Generate SQL
   virtual void generate(SQLWriter& out) = 0;
   /// Generate SQL in a form that is suitable as operand
   virtual void generateOperand(SQLWriter& out);
};
//---------------------------------------------------------------------------
/// An IU reference
class IURef : public Expression {
   /// The IU
   const IU* iu;

   public:
   /// Constructor
   IURef(const IU* iu);

   /// Get the IU
   const IU* getIU() const { return iu; }

   /// Generate SQL
   void generate(SQLWriter& out) override;
   /// Generate SQL in a form that is suitable as operand
   void generateOperand(SQLWriter& out) override { generate(out); }
};
//---------------------------------------------------------------------------
/// A constant value
class ConstExpression : public Expression {
   /// The raw value
   std::string value;
   /// NULL?
   bool null;

   public:
   /// Constructor for non-null values
   ConstExpression(std::string value, Type type) : Expression(type), value(std::move(value)), null(false) {}
   /// Constructor for NULL values
   ConstExpression(std::nullptr_t, Type type) : Expression(type), null(true) {}

   /// Generate SQL
   void generate(SQLWriter& out) override;
   /// Generate SQL in a form that is suitable as operand
   void generateOperand(SQLWriter& out) override { generate(out); }
};
//---------------------------------------------------------------------------
/// A cast expression
class CastExpression : public Expression {
   /// The input
   std::unique_ptr<Expression> input;

   public:
   /// Constructor
   CastExpression(std::unique_ptr<Expression> input, Type type) : Expression(type), input(move(input)) {}

   /// Generate SQL
   void generate(SQLWriter& out) override;
};
//---------------------------------------------------------------------------
/// A comparison expression
class ComparisonExpression : public Expression {
   public:
   /// Possible modes
   enum Mode {
      Equal,
      NotEqual,
      Is,
      IsNot,
      Less,
      LessOrEqual,
      Greater,
      GreaterOrEqual,
      Like
   };
   /// The input
   std::unique_ptr<Expression> left, right;
   /// The mode
   Mode mode;
   /// The collation
   Collate collate;

   public:
   /// Constructor
   ComparisonExpression(std::unique_ptr<Expression> left, std::unique_ptr<Expression> right, Mode mode, Collate collate);

   /// Generate SQL
   void generate(SQLWriter& out) override;
};
//---------------------------------------------------------------------------
/// A between expression
class BetweenExpression : public Expression {
   public:
   /// The input
   std::unique_ptr<Expression> base, lower, upper;
   /// The collation
   Collate collate;

   public:
   /// Constructor
   BetweenExpression(std::unique_ptr<Expression> base, std::unique_ptr<Expression> lower, std::unique_ptr<Expression> upper, Collate collate);

   /// Generate SQL
   void generate(SQLWriter& out) override;
};
//---------------------------------------------------------------------------
/// A binary expression
class BinaryExpression : public Expression {
   public:
   /// Possible operations
   enum Operation {
      Plus,
      Minus,
      Mul,
      Div,
      Mod,
      Power,
      Concat,
      And,
      Or
   };
   /// The input
   std::unique_ptr<Expression> left, right;
   /// The mode
   Operation op;

   public:
   /// Constructor
   BinaryExpression(std::unique_ptr<Expression> left, std::unique_ptr<Expression> right, Type resultType, Operation op);

   /// Generate SQL
   void generate(SQLWriter& out) override;
};
//---------------------------------------------------------------------------
/// An unary expression
class UnaryExpression : public Expression {
   public:
   /// Possible operations
   enum Operation {
      Plus,
      Minus,
      Not
   };
   /// The input
   std::unique_ptr<Expression> input;
   /// The mode
   Operation op;

   public:
   /// Constructor
   UnaryExpression(std::unique_ptr<Expression> input, Type resultType, Operation op);

   /// Generate SQL
   void generate(SQLWriter& out) override;
};
//---------------------------------------------------------------------------
/// An extract expression
class ExtractExpression : public Expression {
   public:
   /// Possible parts
   enum Part {
      Year,
      Month,
      Day
   };
   /// The input
   std::unique_ptr<Expression> input;
   /// The part
   Part part;

   public:
   /// Constructor
   ExtractExpression(std::unique_ptr<Expression> input, Part part);

   /// Generate SQL
   void generate(SQLWriter& out) override;
};
//---------------------------------------------------------------------------
/// Helper for aggregation steps
struct AggregationLike {
   /// A regular computation
   struct Entry {
      /// The expression
      std::unique_ptr<algebra::Expression> value;
      /// The result IU
      std::unique_ptr<algebra::IU> iu;
   };
   /// Known aggregation functions
   enum class Op {
      CountStar,
      Count,
      Sum,
      Min,
      Max,
      Avg
   };
   /// An aggregation
   struct Aggregation {
      /// The expression
      std::unique_ptr<algebra::Expression> value;
      /// The result IU
      std::unique_ptr<algebra::IU> iu;
      /// The operation
      Op op;
   };
};
//---------------------------------------------------------------------------
///n aggregate expression
class Aggregate : public Expression, public AggregationLike {
   private:
   /// The input
   std::unique_ptr<Operator> input;
   /// The aggregates
   std::vector<Aggregation> aggregates;
   /// The final result computation
   std::unique_ptr<Expression> computation;

   public:
   /// Constructor
   Aggregate(std::unique_ptr<Operator> input, std::vector<Aggregation> aggregates, std::unique_ptr<Expression> computation);

   // Generate SQL
   void generate(SQLWriter& out) override;
};
//---------------------------------------------------------------------------
}
}
//---------------------------------------------------------------------------
#endif
