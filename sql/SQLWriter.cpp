#include "sql/SQLWriter.hpp"
#include "infra/Schema.hpp"
//---------------------------------------------------------------------------
// (c) 2023 Thomas Neumann
//---------------------------------------------------------------------------
using namespace std;
//---------------------------------------------------------------------------
namespace saneql {
//---------------------------------------------------------------------------
SQLWriter::SQLWriter()
   : target(&result)
// Constructor
{
}
//---------------------------------------------------------------------------
SQLWriter::~SQLWriter()
// Destructor
{
}
//---------------------------------------------------------------------------
void SQLWriter::write(std::string_view sql)
// Write a SQL fragment
{
   auto& writer = *target;
   writer += sql;
}
//---------------------------------------------------------------------------
void SQLWriter::writeIdentifier(std::string_view identifier)
// Write an identifier, quoting as needed
{
   auto& writer = *target;
   writer += '"';
   for (char c : identifier) {
      if (c == '"') {
         writer += "\"\"";
      } else {
         writer += c;
      }
   }
   writer += '"';
}
//---------------------------------------------------------------------------
void SQLWriter::writeIU(const algebra::IU* iu)
// Write an IU
{
   auto& writer = *target;
   if (auto iter = iuNames.find(iu); iter != iuNames.end()) {
      writer += iter->second;
   } else {
      string name = "v_"s + to_string(iuNames.size() + 1);
      writer += name;
      iuNames[iu] = move(name);
   }
}
//---------------------------------------------------------------------------
void SQLWriter::writeString(std::string_view str)
// Write a string literal
{
   auto& writer = *target;
   writer += '\'';
   for (char c : str) {
      if (c == '\'') {
         writer += "''";
      } else {
         writer += c;
      }
   }
   writer += '\'';
}
//---------------------------------------------------------------------------
void SQLWriter::writeType(Type type)
// Write a type
{
   auto& writer = *target;
   switch (type.getType()) {
      case Type::Unknown: writer += "unknown"; break; // this can only happen for NULL values
      case Type::Bool: writer += "boolean"; break;
      case Type::Integer: writer += "integer"; break;
      case Type::Decimal: writer += "decimal(" + to_string(type.getPrecision()) + "," + to_string(type.getScale()) + ")"; break;
      case Type::Char: writer += "char(" + to_string(type.getLength()) + ")"; break;
      case Type::Varchar: writer += "varchar(" + to_string(type.getLength()) + ")"; break;
      case Type::Text: writer += "text"; break;
      case Type::Date: writer += "date"; break;
      case Type::Interval: writer += "interval"; break;
   }
}
//---------------------------------------------------------------------------
}
//---------------------------------------------------------------------------
