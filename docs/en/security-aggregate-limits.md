# Remote aggregate budgets

Remote Aggregate accepts at most 32 functions and 4096 expression bytes.
Unknown function selectors, embedded NUL and trailing payload are rejected.
Tables over 100000 physical rows are rejected before navigation. Smaller
scans have a cooperative two-second budget checked between rows; slow disk
or expression work within a row is not preempted. Exhaustion returns an error,
never a partial total. Values over 65535 bytes cannot truncate wire lengths.

These are intentional compatibility limits for the remote Aggregate opcode.
Use database SQL aggregates for larger reports. Local aggregates and SQL
execution have separate paths and are not bounded by this change.
