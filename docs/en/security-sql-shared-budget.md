# Shared cooperative native SQL execution budget

Remote ExecuteSQL requests establish one thread-local allowance for the entire
synchronous call tree: 1,000,000 checkpoints and a 5-second cooperative deadline.
Native ABI re-entry does not refill it. Native table navigation/field reads/writes,
script statement/expression evaluation, index-expression evaluation and native
sort comparisons charge the shared allowance. Local SQL defaults are unchanged.
Returned cursors are closed and failure is returned when exhaustion is detected;
script error handlers cannot restore a depleted shared allowance.

This supplements existing input, source-shape, UNION, predicate-subquery and
script-only limits. It is not a hard execution timeout, complete memory quota,
or transaction boundary. A blocking filesystem or external SQL-driver call
cannot be interrupted by these checkpoints. Sort checks retain comparator
ordering and detect exhaustion at the boundary; sorting itself still finishes.
Other uninstrumented work can run between checks. External backend result
memory, driver-specific cancellation and aggregate allocations need separate
controls. Earlier DML writes may remain on failure unless an applicable explicit
transaction is rolled back. A failed deadline must not be retried blindly.
The security audit remains open, including archive ancestor/symlink races.
