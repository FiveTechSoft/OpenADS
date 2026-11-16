# Configuring the remote SQL work limits

The server applies these limits to every SQL statement a remote client runs.
Local (direct-file) SQL has none of them, and neither does a real Advantage
Database Server.

| Setting (`openads.ini`)  | Environment variable              | Default    | Applies to |
|--------------------------|-----------------------------------|------------|------------|
| `remote_sql_max_rows`    | `OPENADS_REMOTE_SQL_MAX_ROWS`     | 100000     | records in each source / UPDATE / DELETE target table ([preflight](security-sql-work-shape.md)) |
| `remote_sql_max_mb`      | `OPENADS_REMOTE_SQL_MAX_MB`       | 64         | estimated fixed-width row volume, MiB |
| `remote_sql_max_steps`   | `OPENADS_REMOTE_SQL_MAX_STEPS`    | 1000000    | engine work steps per statement |
| `remote_sql_timeout_ms`  | `OPENADS_REMOTE_SQL_TIMEOUT_MS`   | 5000       | wall-clock time per statement |

`0` means unlimited. A missing or non-numeric value keeps the default. The
environment variable wins over the ini key.

The keys go in the server's `openads.ini` (the file given with `--config`, or
the one next to `openads_serverd.exe` / `openace32.dll`). Restart the server
after changing them. For example, to let a trusted LAN application run
maintenance SQL (multi-million-row `UPDATE` / `DELETE`, `SELECT ... INTO`):

```ini
[server]
remote_sql_max_rows   = 0
remote_sql_max_mb     = 0
remote_sql_max_steps  = 0
remote_sql_timeout_ms = 0
```

Raising the limits trades protection against expensive statements from
remote clients for the ability to run large statements. Keep the defaults on
servers reachable by untrusted clients.

When a limit stops a statement, the client error names it, for example
`ExecuteSQL: server-side exec failed (remote SQL source budget exceeded)`.
Other server-side SQL errors stay generic on the wire.

A statement stopped by a limit never leaves a partial result behind: a
`SELECT ... INTO <table>` / `CREATE TABLE ... AS SELECT` that fails removes
the table it created. A subquery that could not be read in full fails the
statement instead of acting on a partial list.
