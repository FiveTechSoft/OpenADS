# Remote PostgreSQL direct-query limits

Remote `exec_sql` and `run_sql` use nonblocking libpq single-row retrieval,
sharing the remote SQL call's deadline and step allowance. Successful queries
restore the connection's previous blocking mode. Local direct queries keep the
existing synchronous behavior. Command lists retain only their final result,
as with `PQexec`, but budgets accumulate over all returned command rows.

The streamed path refuses more than 100,000 rows or 64 MiB of conservative
row/cell/field accounting before application string copies. This includes
results discarded by `exec_sql`. On budget, protocol or SQL error the affected
connection closes: callers must reconnect; existing transaction/cursor state
must not be reused. Partial result rows are never returned as success.

This is not a hard process-memory cap: libpq can allocate an oversized incoming
row/input buffer before the application sees it. Server work and memory are not
bounded by these client limits. Closing the socket is not confirmed server-side
cancellation, nor a rollback guarantee for already committed statements.
Connection setup and other PostgreSQL methods (`open_table`, navigation,
metadata, aggregates, writes) still have synchronous driver paths. MariaDB,
other drivers, retained cursors and blocked external operations need separate
controls. This is partial coverage, not a security-complete claim.
