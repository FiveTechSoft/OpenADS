---
title: Error log and journal paths
layout: default
parent: Home (EN)
nav_order: 9
permalink: /en/error-log/
---

# Error log and journal paths

OpenADS has **two** different on-disk “logs.” Only the **server error
log** is relocatable. The **transaction journal** always lives under
the connection’s data directory.

## Server error log — `ads_err.dbf` + `ads_err.log`

A plain DBF table (SAP-style `ads_err`) that any DBF tool — or OpenADS
itself — can open, plus a developer-friendly fixed-width text mirror
`ads_err.log` in the same directory. Both are written on every entry;
the DBF schema is frozen for SAP compatibility while the text line
carries extra debugging context.

Text format: every leading column occupies a fixed width, columns are
separated by exactly 3 spaces, one line per entry (multi-line messages
are flattened with ` | `). A header line is written on file creation:

```text
DATETIME              CODE   SOURCE         LINE        PID        TID        SESSION   CLIENT                  OP           TABLE                    DETAIL
2026-09-06 21:07:02     5018   NET               0      1234   1A2B3C4D          7   10.0.0.5:51234         OpenIndex    lmjshd10.cdx             OpenIndex: lmjshd10.cdx
```

| Column | Width | Notes |
|--------|-------|-------|
| `DATETIME` | 19 | `YYYY-MM-DD HH:MM:SS` |
| `CODE` | 6, right | ACE code (`5018`, `7200`, `0` = info) |
| `SOURCE` | 8 | `SQL`, `NET`, `SERVER`, ... |
| `LINE` | 8, right | source line (often 0) |
| `PID` | 8, right | server process id |
| `TID` | 8 | last 8 hex of the worker thread hash (MT storms) |
| `SESSION` | 8, right | server session id (empty when local) |
| `CLIENT` | 21 | remote `ip:port` (empty when local) |
| `OP` | 12 | wire op / API (`OpenIndex`, `ExecuteSQL`, ...) |
| `TABLE` | 24 | table/index path (basename, tail-24 if long) |
| `DETAIL` | rest | full message / SQL, never truncated |

`OP`/`TABLE` come from the request context when the server knows it;
otherwise they are derived from `Op: path` details, so the `5018
OpenIndex: xxx.cdx` flood that used to need DBF decoding is now
`grep OpenIndex ads_err.log`.

Rows record date, time, error code, subsystem
(`SQL`, `NET`, `SERVER`), optional source line, and detail text.

Written for:

- SQL statement failures (`AdsExecuteSQL` / `AdsExecuteSQLDirect`)
- Error frames returned to remote clients
- Server lifecycle (informational code-0 rows on start and stop)

### Where it lives

| Mechanism | Setting |
|-----------|---------|
| Environment | `OPENADS_ERROR_LOG_PATH=<dir>` (DLL **and** `openads_serverd`) |
| `openads.ini` (`--config`) | `error_log_path = <dir>` (alias `error_assert_logs`), `error_log_max = <KB>` |
| Command line | `--error-log-path <dir>`, `--error-log-max <KB>` |
| SQL at runtime | `EXECUTE PROCEDURE sp_mgSetConfigValue('ERROR_ASSERT_LOGS', '<dir>')` / `('ERROR_LOG_MAX', '<KB>')` |

The path is a **directory**. The file names are always `ads_err.dbf`
(DBF table) and `ads_err.log` (fixed-width text mirror).

**Defaults** when nothing is set:

| Platform | Prefer | Fallback if not writable |
|----------|--------|---------------------------|
| Windows | `C:\` → `C:\ads_err.dbf` | `%ProgramData%\OpenADS`, then temp |
| Linux | `/var/log/advantage` | `~/.openads`, then temp |
| macOS | same idea as Linux | `~/.openads`, then temp |

**Size cap:** default **1000 KB**, shared by both files. When an entry would exceed the cap,
the oldest third of rows/lines is dropped and the table is packed (same
rotation idea SAP documents for `ads_err`).

### Examples

Command line:

```text
openads_serverd --data "C:\app\data" --port 6262 ^
  --error-log-path "C:\OpenADS\logs" --error-log-max 2000
```

`openads.ini` (see also `openads.ini.sample` at the repo root):

```ini
[server]
data = C:\app\data
port = 6262
error_log_path = C:\OpenADS\logs
error_log_max  = 1000
```

```text
openads_serverd --config openads.ini
```

Windows service — bake the flag into the registered binPath:

```bat
openads_serverd --install-service ^
    --port 6262 --data C:\app\data ^
    --error-log-path C:\OpenADS\logs
```

Or set `OPENADS_ERROR_LOG_PATH` for the service account.

### Reading the log

Tail the text mirror, or open `ads_err.dbf` as a table, or query it over any connection.
`sp_mgGetConfigInfo` reports the active path and max size.

```bash
tail -f /openads-data/logs/ads_err.log
grep "  5018" /openads-data/logs/ads_err.log | grep OpenIndex
```

---

## Transaction journal — `openads.txlog` (not relocatable)

Per-connection journaling for `AdsBeginTransaction` / commit /
rollback and orphan recovery:

```text
<data-dir>/openads.txlog
<data-dir>/openads.txlog.<n>     ← rotated archives
<data-dir>/openads.lsnmap        ← recovery map
```

`<data-dir>` is the directory the connection opened (`--data` / `data=`
for the server connect path, or the local path passed to
`AdsConnect60`). There is **no** separate “tx log path” setting.

### Drive roots and non-writable data directories

If the data root cannot create files (typical non-elevated
`--data "C:\"` on Windows):

- **Connect still succeeds** (since v1.8.54)
- Tables in **writable subfolders** work normally
- **Transactions** are disabled on that connection
  (`AdsBeginTransaction` fails with a clear error)
- A notice is written to stderr (`tx journal not writable…`)

That notice is expected for whole-drive / `legacy_paths` ERP
deployments that never use ACE transactions. To enable journaling,
point `data=` at a **writable folder** (and keep using
`--legacy-paths` so absolute client paths still resolve).

```text
openads_serverd --data "C:\OpenADS\data" --legacy-paths ^
  --error-log-path "C:\OpenADS\logs"
```

See [Migrating from ADS](migrating-from-ads/) for `legacy_paths` and
whole-filesystem layouts.

---

## Quick reference

| What | File | Path control |
|------|------|----------------|
| Server / operational errors | `ads_err.dbf` | `error_log_path` / `--error-log-path` / `OPENADS_ERROR_LOG_PATH` |
| Transaction journal | `openads.txlog` | Always under connection **data** directory |
