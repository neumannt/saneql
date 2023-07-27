#include "infra/Schema.hpp"
//---------------------------------------------------------------------------
// (c) 2023 Thomas Neumann
//---------------------------------------------------------------------------
using namespace std;
//---------------------------------------------------------------------------
namespace saneql {
//---------------------------------------------------------------------------
string Type::getName() const
// Get the name (for error reporting)
{
   switch (tag) {
      case Unknown: return "unknown";
      case Bool: return "boolean";
      case Integer: return "integer";
      case Decimal: return "decimal";
      case Char: return "char";
      case Varchar: return "varchar";
      case Text: return "text";
      case Date: return "date";
      case Interval: return "interval";
   }
   __builtin_unreachable();
}
//---------------------------------------------------------------------------
void Schema::createTable(std::string name, std::initializer_list<Column> columns)
// Create a table
{
   auto& t = tables[name];
   t.columns.assign(columns.begin(), columns.end());
}
//---------------------------------------------------------------------------
void Schema::createTPCH()
// Create the TPC-H schema for experiments
{
   createTable("part", {{"p_partkey", Type::getInteger()}, {"p_name", Type::getVarchar(55)}, {"p_mfgr", Type::getChar(25)}, {"p_brand", Type::getChar(10)}, {"p_type", Type::getVarchar(25)}, {"p_size", Type::getInteger()}, {"p_container", Type::getChar(10)}, {"p_retailprice", Type::getDecimal(12, 2)}, {"p_comment", Type::getVarchar(23)}});
   createTable("region", {{"r_regionkey", Type::getInteger()}, {"r_name", Type::getChar(25)}, {"r_comment", Type::getVarchar(152)}});
   createTable("nation", {{"n_nationkey", Type::getInteger()}, {"n_name", Type::getChar(25)}, {"n_regionkey", Type::getInteger()}, {"n_comment", Type::getVarchar(152)}});
   createTable("supplier", {{"s_suppkey", Type::getInteger()}, {"s_name", Type::getChar(25)}, {"s_address", Type::getVarchar(40)}, {"s_nationkey", Type::getInteger()}, {"s_phone", Type::getChar(15)}, {"s_acctbal", Type::getDecimal(12, 2)}, {"s_comment", Type::getVarchar(101)}});
   createTable("partsupp", {{"ps_partkey", Type::getInteger()}, {"ps_suppkey", Type::getInteger()}, {"ps_availqty", Type::getInteger()}, {"ps_supplycost", Type::getDecimal(12, 2)}, {"ps_comment", Type::getVarchar(199)}});
   createTable("customer", {{"c_custkey", Type::getInteger()}, {"c_name", Type::getVarchar(25)}, {"c_address", Type::getVarchar(40)}, {"c_nationkey", Type::getInteger()}, {"c_phone", Type::getChar(15)}, {"c_acctbal", Type::getDecimal(12, 2)}, {"c_mktsegment", Type::getChar(10)}, {"c_comment", Type::getVarchar(117)}});
   createTable("orders", {{"o_orderkey", Type::getInteger()}, {"o_custkey", Type::getInteger()}, {"o_orderstatus", Type::getChar(1)}, {"o_totalprice", Type::getDecimal(12, 2)}, {"o_orderdate", Type::getDate()}, {"o_orderpriority", Type::getChar(15)}, {"o_clerk", Type::getChar(15)}, {"o_shippriority", Type::getInteger()}, {"o_comment", Type::getVarchar(79)}});
   createTable("lineitem", {{"l_orderkey", Type::getInteger()}, {"l_partkey", Type::getInteger()}, {"l_suppkey", Type::getInteger()}, {"l_linenumber", Type::getInteger()}, {"l_quantity", Type::getDecimal(12, 2)}, {"l_extendedprice", Type::getDecimal(12, 2)}, {"l_discount", Type::getDecimal(12, 2)}, {"l_tax", Type::getDecimal(12, 2)}, {"l_returnflag", Type::getChar(1)}, {"l_linestatus", Type::getChar(1)}, {"l_shipdate", Type::getDate()}, {"l_commitdate", Type::getDate()}, {"l_receiptdate", Type::getDate()}, {"l_shipinstruct", Type::getChar(25)}, {"l_shipmode", Type::getChar(10)}, {"l_comment", Type::getVarchar(44)}});
}
//---------------------------------------------------------------------------
void Schema::populateSchema()
// Create initial schema objects
{
   // For now we hard-code TPC-H for experiments
   createTPCH();
}
//---------------------------------------------------------------------------
const Schema::Table* Schema::lookupTable(const std::string& name) const
// Check if a table exists in the schema
{
   auto iter = tables.find(name);
   if (iter != tables.end())
      return &(iter->second);
   return nullptr;
}
//---------------------------------------------------------------------------
}
//---------------------------------------------------------------------------
