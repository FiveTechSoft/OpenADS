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

## Server error log — `ads_err.dbf`

A plain DBF table (SAP-style `ads_err`) that any DBF tool — or OpenADS
itself — can open. Rows record date, time, error code, subsystem
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

The path is a **directory**. The file name is always `ads_err.dbf`.

**Defaults** when nothing is set:

| Platform | Prefer | Fallback if not writable |
|----------|--------|---------------------------|
| Windows | `C:\` → `C:\ads_err.dbf` | `%ProgramData%\OpenADS`, then temp |
| Linux | `/var/log/advantage` | `~/.openads`, then temp |
| macOS | same idea as Linux | `~/.openads`, then temp |

**Size cap:** default **1000 KB**. When an entry would exceed the cap,
the oldest third of rows is dropped and the table is packed (same
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

Open `ads_err.dbf` as a table, or query it over any connection.  
`sp_mgGetConfigInfo` reports the active path and max size.

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
