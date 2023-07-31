customer
.filter(c_mktsegment = 'BUILDING')
.join(orders.filter(o_orderdate < '1995-03-15'::date), c_custkey = o_custkey)
.join(lineitem.filter(l_shipdate > '1995-03-15'::date), l_orderkey = o_orderkey)
.groupby({l_orderkey,o_orderdate,o_shippriority},{revenue:=sum(l_extendedprice * (1 - l_discount))})
.orderby({revenue.desc(), o_orderdate}, limit:=10)
.project({l_orderkey, revenue, o_orderdate, o_shippriority})
