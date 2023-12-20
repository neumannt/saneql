#include "algebra/Operator.hpp"
#include "sql/SQLWriter.hpp"
//---------------------------------------------------------------------------
// (c) 2023 Thomas Neumann
//---------------------------------------------------------------------------
using namespace std;
//---------------------------------------------------------------------------
namespace saneql::algebra {
//---------------------------------------------------------------------------
Operator::~Operator()
// Destructor
{
}
//---------------------------------------------------------------------------
TableScan::TableScan(string name, vector<Column> columns)
   : name(move(name)), columns(move(columns))
// Constructor
{
}
//---------------------------------------------------------------------------
void TableScan::generate(SQLWriter& out)
// Generate SQL
{
   out.write("(select ");
   bool first = true;
   for (auto& c : columns) {
      if (first)
         first = false;
      else
         out.write(", ");
      out.writeIdentifier(c.name);
      out.write(" as ");
      out.writeIU(c.iu.get());
   }
   out.write(" from ");
   out.writeIdentifier(name);
   out.write(")");
}
//---------------------------------------------------------------------------
Select::Select(unique_ptr<Operator> input, unique_ptr<Expression> condition)
   : input(move(input)), condition(move(condition))
// Constructor
{
}
//---------------------------------------------------------------------------
void Select::generate(SQLWriter& out)
// Generate SQL
{
   out.write("(select * from ");
   input->generate(out);
   out.write(" s where ");
   condition->generate(out);
   out.write(")");
}
//---------------------------------------------------------------------------
Map::Map(unique_ptr<Operator> input, vector<Entry> computations)
   : input(move(input)), computations(move(computations))
// Constructor
{
}
//---------------------------------------------------------------------------
void Map::generate(SQLWriter& out)
// Generate SQL
{
   out.write("(select *");
   for (auto& c : computations) {
      out.write(", ");
      c.value->generate(out);
      out.write(" as ");
      out.writeIU(c.iu.get());
   }
   out.write(" from ");
   input->generate(out);
   out.write(" s)");
}
//---------------------------------------------------------------------------
SetOperation::SetOperation(unique_ptr<Operator> left, unique_ptr<Operator> right, vector<unique_ptr<Expression>> leftColumns, vector<unique_ptr<Expression>> rightColumns, vector<unique_ptr<IU>> resultColumns, Op op)
   : left(move(left)), right(move(right)), leftColumns(move(leftColumns)), rightColumns(move(rightColumns)), resultColumns(move(resultColumns)), op(op)
// Constructor
{
}
//---------------------------------------------------------------------------
void SetOperation::generate(SQLWriter& out)
// Generate SQL
{
   auto dumpColumns = [&out](const vector<unique_ptr<Expression>>& columns) {
      if (columns.empty()) {
         out.write("1");
      } else {
         bool first = true;
         for (auto& c : columns) {
            if (first)
               first = false;
            else
               out.write(", ");
            c->generate(out);
         }
      }
   };
   out.write("(select * from ((select ");
   dumpColumns(leftColumns);
   out.write(" from ");
   left->generate(out);
   out.write(" l) ");
   switch (op) {
      case Op::Union: out.write("union"); break;
      case Op::UnionAll: out.write("union all"); break;
      case Op::Except: out.write("except"); break;
      case Op::ExceptAll: out.write("except all"); break;
      case Op::Intersect: out.write("intersect"); break;
      case Op::IntersectAll: out.write("intersect all"); break;
   }
   out.write(" (select ");
   dumpColumns(rightColumns);
   out.write(" from ");
   right->generate(out);
   out.write(" r)) s");
   if (!resultColumns.empty()) {
      out.write("(");
      bool first = true;
      for (auto& c : resultColumns) {
         if (first)
            first = false;
         else
            out.write(", ");
         out.writeIU(c.get());
      }
      out.write(")");
   }
   out.write(")");
}
//---------------------------------------------------------------------------
Join::Join(unique_ptr<Operator> left, unique_ptr<Operator> right, unique_ptr<Expression> condition, JoinType joinType)
   : left(move(left)), right(move(right)), condition(move(condition)), joinType(joinType)
// Constructor
{
}
//---------------------------------------------------------------------------
void Join::generate(SQLWriter& out)
// Generate SQL
{
   switch (joinType) {
      case JoinType::Inner:
         out.write("(select * from ");
         left->generate(out);
         out.write(" l inner join ");
         right->generate(out);
         out.write(" r on ");
         condition->generate(out);
         out.write(")");
         break;
      case JoinType::LeftOuter:
         out.write("(select * from ");
         left->generate(out);
         out.write(" l left outer join ");
         right->generate(out);
         out.write(" r on ");
         condition->generate(out);
         out.write(")");
         break;
      case JoinType::RightOuter:
         out.write("(select * from ");
         left->generate(out);
         out.write(" l right outer join ");
         right->generate(out);
         out.write(" r on ");
         condition->generate(out);
         out.write(")");
         break;
      case JoinType::FullOuter:
         out.write("(select * from ");
         left->generate(out);
         out.write(" l full outer join ");
         right->generate(out);
         out.write(" r on ");
         condition->generate(out);
         out.write(")");
         break;
      case JoinType::LeftSemi:
         out.write("(select * from ");
         left->generate(out);
         out.write(" l where exists(select * from ");
         right->generate(out);
         out.write(" r where ");
         condition->generate(out);
         out.write("))");
         break;
      case JoinType::RightSemi:
         out.write("(select * from ");
         right->generate(out);
         out.write(" r where exists(select * from ");
         left->generate(out);
         out.write(" l where ");
         condition->generate(out);
         out.write("))");
         break;
      case JoinType::LeftAnti:
         out.write("(select * from ");
         left->generate(out);
         out.write(" l where not exists(select * from ");
         right->generate(out);
         out.write(" r where ");
         condition->generate(out);
         out.write("))");
         break;
      case JoinType::RightAnti:
         out.write("(select * from ");
         right->generate(out);
         out.write(" r where not exists(select * from ");
         left->generate(out);
         out.write(" l where ");
         condition->generate(out);
         out.write("))");
         break;
   }
}
//---------------------------------------------------------------------------
GroupBy::GroupBy(unique_ptr<Operator> input, vector<Entry> groupBy, vector<Aggregation> aggregates)
   : input(move(input)), groupBy(move(groupBy)), aggregates(move(aggregates))
// Constructor
{
}
//---------------------------------------------------------------------------
void GroupBy::generate(SQLWriter& out)
// Generate SQL
{
   out.write("(select ");
   bool first = true;
   for (auto& g : groupBy) {
      if (first)
         first = false;
      else
         out.write(", ");
      g.value->generate(out);
      out.write(" as ");
      out.writeIU(g.iu.get());
   }
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
   out.write(" s group by ");
   if (groupBy.empty()) {
      out.write("true");
   } else {
      for (unsigned index = 0, limit = groupBy.size(); index < limit; ++index) {
         if (index) out.write(", ");
         out.write(to_string(index + 1));
      }
   }
   out.write(")");
}
//---------------------------------------------------------------------------
Sort::Sort(unique_ptr<Operator> input, vector<Entry> order, optional<uint64_t> limit, optional<uint64_t> offset)
   : input(move(input)), order(move(order)), limit(limit), offset(offset)
// Constructor
{
}
//---------------------------------------------------------------------------
void Sort::generate(SQLWriter& out)
// Generate SQL
{
   out.write("(select * from ");
   input->generate(out);
   out.write(" s");
   if (!order.empty()) {
      out.write(" order by ");
      bool first = true;
      for (auto& o : order) {
         if (first)
            first = false;
         else
            out.write(", ");
         o.value->generate(out);
         if (o.collate != Collate{}) out.write(" collate TODO"); // TODO
         if (o.descending) out.write(" desc");
      }
   }
   if (limit.has_value()) {
      out.write(" limit ");
      out.write(to_string(*limit));
   }
   if (offset.has_value()) {
      out.write(" offset ");
      out.write(to_string(*offset));
   }
   out.write(")");
}
//---------------------------------------------------------------------------
Window::Window(unique_ptr<Operator> input, vector<Aggregation> aggregates, vector<unique_ptr<Expression>> partitionBy, vector<Sort::Entry> orderBy)
   : input(move(input)), aggregates(move(aggregates)), partitionBy(move(partitionBy)), orderBy(move(orderBy))
// Constructor
{
}
//---------------------------------------------------------------------------
void Window::generate(SQLWriter& out)
// Generate SQL
{
   auto aggr = [&out](const char* name, const Aggregation& a, bool distinct = false) {
      out.write(name);
      out.write("(");
      if (distinct) out.write("distinct ");
      a.value->generate(out);
      for (auto& p : a.parameters) {
         out.write(", ");
         p->generate(out);
      }
      out.write(")");
   };
   out.write("(select *");
   for (auto& a : aggregates) {
      out.write(", ");
      switch (static_cast<WindowOp>(a.op)) {
         case Op::CountStar: out.write("count(*)"); break;
         case Op::Count: aggr("count", a); break;
         case Op::CountDistinct: aggr("count", a, true); break;
         case Op::Sum: aggr("sum", a); break;
         case Op::SumDistinct: aggr("sum", a, true); break;
         case Op::Avg: aggr("avg", a); break;
         case Op::AvgDistinct: aggr("avg", a, true); break;
         case Op::Min: aggr("min", a); break;
         case Op::Max: aggr("max", a); break;
         case Op::RowNumber: out.write("row_number()"); break;
         case Op::Rank: aggr("rank", a); break;
         case Op::DenseRank: aggr("dense_rank", a); break;
         case Op::NTile: aggr("ntile", a); break;
         case Op::Lead: aggr("lead", a); break;
         case Op::Lag: aggr("lag", a); break;
         case Op::FirstValue: aggr("first_value", a); break;
         case Op::LastValue: aggr("last_value", a); break;
      }
      out.write(" over (");
      if (!partitionBy.empty()) {
         out.write("partition by ");
         bool first = true;
         for (auto& p : partitionBy) {
            if (first)
               first = false;
            else
               out.write(", ");
            p->generate(out);
         }
      }
      if (!orderBy.empty()) {
         if (!partitionBy.empty()) out.write(" ");
         out.write("order by ");
         bool first = true;
         for (auto& o : orderBy) {
            if (first)
               first = false;
            else
               out.write(", ");
            o.value->generate(out);
            if (o.collate != Collate{}) out.write(" collate TODO"); // TODO
            if (o.descending) out.write(" desc");
         }
      }
      out.write(") as ");
      out.writeIU(a.iu.get());
   }
   out.write(" from ");
   input->generate(out);
   out.write(" s)");
}
//---------------------------------------------------------------------------
InlineTable::InlineTable(vector<unique_ptr<algebra::IU>> columns, vector<unique_ptr<algebra::Expression>> values, unsigned rowCount)
   : columns(move(columns)), values(move(values)), rowCount(move(rowCount))
// Constructor
{
}
//---------------------------------------------------------------------------
void InlineTable::generate(SQLWriter& out)
// Generate SQL
{
   out.write("(select * from (values");
   if (rowCount) {
      for (unsigned index = 0; index != rowCount; ++index) {
         if (index) out.write(",");
         if (!columns.empty()) {
            out.write("(");
            for (unsigned index2 = 0, limit2 = columns.size(); index2 != limit2; ++index2) {
               if (index2) out.write(", ");
               values[index * limit2 + index2]->generate(out);
            }
            out.write(")");
         } else {
            // PostgreSQL does not support empty tuples in values, add a dummy value
            out.write("(NULL)");
         }
      }
   } else {
      if (!columns.empty()) {
         out.write("(");
         for (unsigned index2 = 0, limit2 = columns.size(); index2 != limit2; ++index2) {
            if (index2) out.write(", ");
            out.write("NULL");
         }
         out.write(")");
      } else {
         // PostgreSQL does not support empty tuples in values, add a dummy value
         out.write("(NULL)");
      }
   }
   out.write(") s(");
   bool first = true;
   for (auto& c : columns) {
      if (first)
         first = false;
      else
         out.write(", ");
      out.writeIU(c.get());
   }
   out.write(")");
   if (!rowCount) out.write(" limit 0");
   out.write(")");
}
//---------------------------------------------------------------------------
}
//---------------------------------------------------------------------------
