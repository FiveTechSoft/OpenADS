# Remote predicate-subquery work preflight

Native single-source remote SELECT checks predicate subqueries before compiling
or exposing a cursor. WHERE, CASE conditions and aggregate FILTER conditions
share an allowance of one million estimated source-row visits. Each subquery
uses the outer physical count times its own physical count, with checked
multiplication and additive accounting. Its source also passes the existing
100k/64MiB source bound. Nested predicate trees share the estimate. Newly opened
preflight handles are closed, including on rejection. Local SQL is unchanged.

This conservative estimate can reject uncorrelated IN/scalar queries or EXISTS
that would stop early. Unsupported inline derived/join subquery forms now fail
remotely instead of being silently mishandled. It is not a deadline or a total
memory allocator budget; concurrent source growth, join predicate paths,
expressions, external backends and native calls still need separate controls.
Partition large trusted work or run it locally. Protocol and files are unchanged.
