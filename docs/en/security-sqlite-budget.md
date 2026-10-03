# SQLite VM and materialized-result limits

When a remote SQL execution scope is active, SQLite run_sql/exec_sql installs a
progress callback every 1,000 VM instructions and charges the shared execution
allowance. Exhaustion interrupts VM execution. A temporary 64 MiB SQLite value
length cap is restored on return, and the progress callback is removed.
Materialized results stop at 100,000 rows or 64 MiB of accounted cell bytes and
row/vector overhead, before copying the next row. Normal local calls are unchanged.

These are per-query result limits and cooperative VM interruption. They do not
bound all SQLite internal allocations, cumulative retained cursors, busy waits,
blocked filesystem calls or external database drivers. The result-memory limit
is conservative accounting, not a precise allocator cap. Existing SQLite
transaction semantics apply; a budget error must not imply a whole-script
rollback. PostgreSQL/MariaDB buffering and cancellation remain separate work.
The archive ancestor/symlink race review and broader security audit remain open.
