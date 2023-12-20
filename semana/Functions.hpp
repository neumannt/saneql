#ifndef H_saneql_Functions
#define H_saneql_Functions
//---------------------------------------------------------------------------
#include "infra/Schema.hpp"
#include <unordered_map>
//---------------------------------------------------------------------------
// SaneQL
// (c) 2023 Thomas Neumann
// SPDX-License-Identifier: BSD-3-Clause
//---------------------------------------------------------------------------
namespace saneql {
//---------------------------------------------------------------------------
/// A collection of functions
class Functions {
   public:
   /// Builtins
   enum class Builtin {
      Asc,
      Desc,
      Collate,
      Is,
      Like,
      Substr,
      Between,
      In,
      Extract,
      Case,
      Filter,
      Join,
      Gensym,
      GroupBy,
      Aggregate,
      Distinct,
      OrderBy,
      Map,
      Project,
      ProjectOut,
      Union,
      Except,
      Intersect,
      Window,
      As,
      Alias,
      AggCount,
      AggSum,
      AggAvg,
      AggMin,
      AggMax,
      WindowRowNumber,
      WindowRank,
      WindowDenseRank,
      WindowNTile,
      WindowLead,
      WindowLag,
      WindowFirstValue,
      WindowLastValue,
      Table,
      ForeignCall
   };
   /// Type category
   enum class TypeCategory {
      Scalar,
      Table,
      Expression,
      ExpressionList,
      Symbol,
      SymbolList
   };
   /// An argument type
   struct ArgumentType {
      /// The type category
      TypeCategory category;
      /// The underlying type (if scalar)
      Type type;

      /// Constructor for scalar types
      ArgumentType(Type type) : category(TypeCategory::Scalar), type(type) {}
      /// Constructor for other categories
      ArgumentType(TypeCategory category) : category(category), type(Type::getUnknown()) {}
   };
   /// An argument
   struct Argument {
      /// The name
      std::string name;
      /// The argument type
      ArgumentType type;
      /// Does it have a default value?
      bool hasDefault = false;
   };
   /// A signature
   struct Signature {
      /// The builtin function
      Builtin builtin;
      /// The arguments
      std::vector<Argument> arguments;
   };

   private:
   /// The parent set (if any)
   const Functions* parent;
   /// All functions contained here
   std::unordered_map<std::string, Signature> functions;

   public:
   /// Constructor
   Functions(const Functions* parent, std::initializer_list<std::pair<std::string, Signature>> signatures);
   /// Destructor
   ~Functions();

   /// Find a function
   const Signature* lookup(const std::string& name) const;

   /// Get the functions for a given type
   static const Functions* getFunctions(Type type);
   /// The functions defined on tables
   static const Functions table;
   /// The free functions
   static const Functions freeFunctions;
};
//---------------------------------------------------------------------------
}
//---------------------------------------------------------------------------
#endif
