# Remote SELECT work preflight

Remote-owned SQL SELECT sources are capped at 100,000 physical records and an
estimated 64 MiB of fixed-width row images. Joins also check the worst-case
Cartesian product of source counts and sum of widths before building hash
maps or materialized results. Empty sources count as one for outer-join safety.
Overflow is checked by division. Failure closes the newly opened source handles
and returns an error, not a partial cursor. Local SQL is unchanged.

This deliberately conservative bound can reject a selective join that would
produce fewer actual rows. WHERE, TOP and indexes do not bypass the bound.
For trusted batch work use local execution or partition the source workload.

This is not a wall-clock deadline, an exact memory allocator limit, or a bound
on memo values. External backends, native DLLs, script-level UDFs,
repeated statements and several UNION/derived stages still need separate work
budgets. The existing SQL input and recursion limits remain in force.

UPDATE/DELETE/MERGE target scans now use the same source preflight. See
[Remote DML target preflight](security-sql-dml-preflight.md).
