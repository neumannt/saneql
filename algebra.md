Operations on tables
====================

as(binding symbol)
   e.g. rel.as(r1).join(rel.as(r2), r1.x=r2.y)

filter(condition expression)
   e.g. rel.filter(x=1)

join(other table, condition expression, type symbol := inner)
   e.g. r1.join(r2, x=y)

map(expressions list expression)
   e.g. r1.map({y:=2*x})

project(expressions list expression)
   e.g. r1.project({x})

projectout(remove list symbol)
   e.g. r1.projectout({x})

groupby(groups list expression, aggregates list expression, type symbol := group, sets list list symbol := {})
   e.g. r1.groupby({a,b},{total:=sum(c)});

orderby(expressions list expression)
   e.g. r1.orderby({x,y.desc()})

union(other table)
unionall(other table)
intersect(other table)
intersectall(other table)
except(other table)
exceptall(other table)
   e.g. r1.union(r2)

window(expressions list expression, partitionby list expression := {}, orderby list expression := {}, framebegin expression := unbounded(), frameend expression := currentrow(), frametype symbol := values)
   r1.window({cs:=sum(x)}, orderby:={y})

Types
=====

- scalar types
- table
- tuple?
- symbol
- lambda
- list of type


