# Remote MariaDB direct-query limits

Remote `exec_sql` and `run_sql` use MariaDB Connector/C continuation query/fetch
calls with `mysql_use_result`. Socket-read/write waits check the shared remote
SQL deadline/steps. Successful queries consume through EOF before freeing the
result; discarded `exec_sql` rows are also consumed and budgeted. Local direct
queries keep their existing path.

The client refuses more than 100,000 rows or 64 MiB of conservative cell/row/
field accounting before application copies. On budget, SQL, wait or protocol
error, the affected socket is shut down, unread result drain is disabled and
that connection closes. Callers must reconnect and must not reuse its old
transaction/cursor state. Multiple result sets are refused and close the
connection rather than leaving an unread result on a successful connection.

This depends on MariaDB Connector/C's continuation API, not MySQL's different
async API. Not a process allocator quota: the driver can allocate a giant row
before its lengths are inspected. Not confirmed server-side cancellation,
server memory/work bounds or rollback of earlier committed commands. Setup,
table/navigation/aggregate/write driver calls and retained cursor totals still
need separate controls. Coverage is partial.
