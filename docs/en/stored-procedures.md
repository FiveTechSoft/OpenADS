---
title: Stored procedures
layout: default
parent: Home (EN)
nav_order: 9
permalink: /en/stored-procedures/
---

# Stored procedures

OpenADS supports two kinds of stored procedures, both invoked
through the SQL surface (`AdsExecuteSQLDirect`):

1. **Custom AEP procedures** — your own code in an external
   shared library (`.dll` / `.so` / `.dylib`).
2. **Built-in `sp_*` procedures** — system procedures that
   operate on the Data Dictionary.

> **Note on the API.** `AdsExecuteSQLDirect`'s first argument is a
> **statement handle** created with `AdsCreateSQLStatement`, not a
> connection handle.

## 1. Custom AEP procedures

### Register

```clipper
LOCAL hStmt, hCur
AdsCreateSQLStatement( hConn, @hStmt )
AdsExecuteSQLDirect( hStmt, "CREATE PROCEDURE my_sum AS 'mylib.dll::sum_proc'", @hCur )
```

`CREATE PROCEDURE <name> AS '<library>::<function>'` registers the
procedure in the per-connection AEP registry. The library is
loaded dynamically and the symbol resolved on execution.

### Implement (C ABI)

The exported function must have this exact signature:

```c
extern "C" int sum_proc(const char* args, char* out_buf, size_t out_cap);
```

- `args` — the call arguments joined by the `\x1F` (Unit
  Separator) byte. `EXECUTE PROCEDURE p('a', 'b')` arrives as
  `"a\x1Fb"`.
- `out_buf` / `out_cap` — write the result string here (NUL
  terminated, capped at `out_cap`).
- **Return value** — `0` for success; any non-zero return makes
  `AdsExecuteSQLDirect` fail.

### Execute

```clipper
AdsExecuteSQLDirect( hStmt, "EXECUTE PROCEDURE my_sum(5, 7)", @hCur )
```

The result is returned as a one-row cursor with a `RESULT` field:

```clipper
AdsGotoTop( hCur )
AdsGetField( hCur, "RESULT", @cBuf, @nCap, 0 )   // -> "12"
```

## 2. Built-in `sp_*` procedures

These operate on the Data Dictionary and require an open DD
connection (`AE_FUNCTION_NOT_AVAILABLE` is returned otherwise).

| Procedure | Action |
|-----------|--------|
| `sp_CreateUser` | Create a DD user (optional password, comment) |
| `sp_DropUser` | Delete a user |
| `sp_CreateGroup` | Create a group |
| `sp_DropGroup` | Delete a group |
| `sp_AddUserToGroup` | Add a user to a group |
| `sp_RemoveUserFromGroup` | Remove a user from a group |
| `sp_ModifyUserProperty` | Change user password / comment / properties |
| `sp_ModifyGroupProperty` | Change group properties |
| `sp_AddTableToDatabase` | Register a table (and its index files) in the DD |
| `sp_AddIndexFileToDatabase` | Register an index file in the DD |
| `sp_ModifyTableProperty` | Change table properties |
| `sp_ModifyFieldProperty` | Change field properties (required, default, validation…) |
| `sp_CreateReferentialIntegrity` | Create an RI rule |
| `sp_DropReferentialIntegrity` | Drop an RI rule |
| `sp_CreateLink` | Create a link to another DD |
| `sp_DropLink` | Drop a link |
| `sp_EnableTriggers` / `sp_DisableTriggers` | Enable / disable triggers (connection scope, table, single trigger, or `ALL`) |
| `sp_ModifyDatabase` | Modify DD properties (admin password, comment, default table path…) |
| `sp_BackupDatabase` | Back up a data dictionary and its tables to a destination directory |
| `sp_BackupFreeTables` | Back up a directory of free tables (not bound to a DD) |
| `sp_RestoreDatabase` | Restore a data dictionary from a backup image |
| `sp_RestoreFreeTables` | Restore free tables from a backup image |

### Backup & Restore

OpenADS provides file-level backup and restore through four stored procedures.
They share the same engine as the `adsbackup` CLI tool, so both entry points
behave identically.

#### sp_BackupDatabase

Backs up the data dictionary and every table bound to it.

```clipper
AdsCreateSQLStatement( hConn, @hStmt )
AdsExecuteSQLDirect( hStmt,;
    "EXECUTE PROCEDURE sp_BackupDatabase('c:\\backup', '')", @hCur )
```

**Parameters:**
- `DestinationPath` — directory where the backup image is written
- `Options` — semicolon-separated keywords (see table below)

**Options:**

| Keyword | Effect |
|---------|--------|
| `Include=t1,t2` | Only back up these tables |
| `Exclude=t1,t2` | Skip these tables |
| `MetaOnly` | Copy only the `.add` dictionary file, no tables |

#### sp_BackupFreeTables

Backs up a directory of free tables (`.dbf` / `.adt`).

```clipper
AdsExecuteSQLDirect( hStmt,;
    "EXECUTE PROCEDURE sp_BackupFreeTables('c:\\data', '*.adt;*.dbf', 'c:\\backup', '')", @hCur )
```

**Parameters:**
- `SourcePath` — directory containing the free tables
- `SourceMask` — file mask (e.g. `*.adt` or `*.adt;*.dbf`)
- `DestinationPath` — where to write the backup
- `Options` — same as sp_BackupDatabase

#### sp_RestoreDatabase / sp_RestoreFreeTables

Restore from a backup image. Use `-r` equivalent options.

```clipper
AdsExecuteSQLDirect( hStmt,;
    "EXECUTE PROCEDURE sp_RestoreDatabase('c:\\backup', '', 'c:\\restore\\motors.add', '')", @hCur )
```

**Result set:** An empty result set means success. Otherwise, each row
carries Severity, Error Code, Error Message, Table Name and Additional Info.

### Example

```clipper
AdsCreateSQLStatement( hConn, @hStmt )
AdsExecuteSQLDirect( hStmt, "EXECUTE PROCEDURE sp_CreateUser('admin','secret')", @hCur )
```
