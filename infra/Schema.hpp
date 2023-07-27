#ifndef H_saneql_Schema
#define H_saneql_Schema
//---------------------------------------------------------------------------
#include <string>
#include <unordered_map>
#include <vector>
//---------------------------------------------------------------------------
// SaneQL
// (c) 2023 Thomas Neumann
// SPDX-License-Identifier: BSD-3-Clause
//---------------------------------------------------------------------------
namespace saneql {
//---------------------------------------------------------------------------
/// An SQL data type
class Type {
   public:
   /// Known types
   enum Tag {
      Unknown,
      Bool,
      Integer,
      Decimal,
      Char,
      Varchar,
      Text,
      Date,
      Interval
   };

   private:
   /// The type
   Tag tag;
   /// The modifier
   unsigned modifier;

   /// Constructor
   constexpr Type(Tag tag, unsigned modifier) : tag(tag), modifier(modifier) {}

   public:
   /// Get the type tag
   constexpr Tag getType() const { return tag; }

   /// Get the name (for error reporting)
   std::string getName() const;

   /// Is the type nullable?
   constexpr bool isNullable() const { return modifier & 1; }
   /// Make the type nullable
   constexpr Type asNullable() const { return Type(tag, modifier | 1); }
   /// Change the nullability
   constexpr Type withNullable(bool nullable) const { return Type(tag, ((modifier >> 1) << 1) | nullable); }

   /// Get the precision of decimal types
   constexpr unsigned getPrecision() const { return modifier >> 9; }
   /// Get the scale of decimal types
   constexpr unsigned getScale() const { return (modifier >> 1) & 0xFF; }
   /// Get the maximum length of text types
   constexpr unsigned getLength() const { return (modifier >> 1); }

   /// Comparison
   bool operator==(const Type& o) const { return ((tag == o.tag) && (modifier == o.modifier)); }
   /// Comparison
   bool operator!=(const Type& o) const { return ((tag != o.tag) || (modifier != o.modifier)); }

   /// Create an unknown type
   static constexpr Type getUnknown() { return Type(Unknown, 0); }
   /// Create a boolean type
   static constexpr Type getBool() { return Type(Bool, 0); }
   /// Create an integer type
   static constexpr Type getInteger() { return Type(Integer, 0); }
   /// Create a decimal type
   static constexpr Type getDecimal(unsigned precision, unsigned scale) { return Type(Decimal, (precision << 9) | (scale << 1)); }
   /// Create a char type
   static constexpr Type getChar(unsigned maxLen) { return Type(Char, maxLen << 1); }
   /// Create a varchar type
   static constexpr Type getVarchar(unsigned maxLen) { return Type(Varchar, maxLen << 1); }
   /// Create a text type
   static constexpr Type getText() { return Type(Text, 0); }
   /// Create a date type
   static constexpr Type getDate() { return Type(Date, 0); }
   /// Create an interval type
   static constexpr Type getInterval() { return Type(Interval, 0); }
};
//---------------------------------------------------------------------------
/// Access to the database schema
class Schema {
   public:
   /// A column definition
   struct Column {
      /// The name
      std::string name;
      /// The type
      Type type;
   };
   /// A table definition
   struct Table {
      /// The columns
      std::vector<Column> columns;
   };

   private:
   /// The tables
   std::unordered_map<std::string, Table> tables;

   /// Create a table
   void createTable(std::string name, std::initializer_list<Column> columns);
   /// Create the TPC-H schema
   void createTPCH();

   public:
   /// Create some test schema for experiments
   void populateSchema();

   /// Check if a table exists in the schema
   const Table* lookupTable(const std::string& name) const;
};
//---------------------------------------------------------------------------
}
//---------------------------------------------------------------------------
#endif
