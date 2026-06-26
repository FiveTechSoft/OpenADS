---
title: API Reference
layout: default
parent: Home (EN)
nav_order: 5
permalink: /en/api-reference/
has_children: true
---

# OpenADS API Reference — v1.4.0

Complete reference for the 357 exported `Ads*` functions in
`ace64.dll` / `ace32.dll` / `libace.so`. Every function available
to Harbour / X# / Clipper / C / PHP / .NET applications.

**Legend:**
- ✅ = Fully implemented
- ⚠️ = Partial / accept-and-ignore (returns `AE_SUCCESS` but does
  nothing meaningful)
- 🔴 = Stub returning `AE_FUNCTION_NOT_AVAILABLE`
- ➡️ = Forward to another implementation (e.g. versioned overload)

All functions return `UNSIGNED32` (`AE_SUCCESS` = 0 on success,
ACE error code on failure) unless noted otherwise.

---

## Table of Contents

| # | Category | Functions |
|---|----------|-----------|
| 1 | [Connection Management](#1-connection-management) | 10 |
| 2 | [Table Operations](#2-table-operations) | 15 |
| 3 | [Record Navigation](#3-record-navigation) | 14 |
| 4 | [Field Read](#4-field-read-by-type) | 21 |
| 5 | [Field Write](#5-field-write) | 17 |
| 6 | [Record Operations](#6-record-operations) | 10 |
| 7 | [Locking](#7-locking) | 12 |
| 8 | [Index Operations](#8-index-operations) | 26 |
| 9 | [Seek & Scope](#9-seek--scope) | 13 |
| 10 | [Filter & AOF](#10-filter--aof-rushmore) | 11 |
| 11 | [SQL](#11-sql) | 17 |
| 12 | [Transaction (TPS)](#12-transaction-tps) | 8 |
| 13 | [Memo / Binary](#13-memo--binary) | 8 |
| 14 | [Table Maintenance](#14-table-maintenance) | 10 |
| 15 | [Encryption](#15-encryption) | 10 |
| 16 | [Data Dictionary (DD)](#16-data-dictionary-dd) | 42 |
| 17 | [Expression Evaluation](#17-expression-evaluation) | 5 |
| 18 | [Server Telemetry (AdsMg*)](#18-server-telemetry-adsmg) | 17 |
| 19 | [Full-Text Search](#19-full-text-search) | 3 |
| 20 | [Miscellaneous](#20-miscellaneous) | 30 |
| 21 | [Callbacks & Caching](#21-callbacks--caching-stubs) | 11 |
| 22 | [RI & Enforcement Toggles](#22-ri--enforcement-toggles) | 7 |
| 23 | [Deferred Flush](#23-deferred-flush) | 2 |
| 24 | [Relation (Stubs)](#24-relation-stubs) | 3 |
| 25 | [Legacy / Lookup](#25-legacy--lookup) | 6 |
| | [Summary](#summary) | **357** |

---

## 1. Connection Management

| Function | Status | Description |
|----------|--------|-------------|
| `AdsConnect60` | ✅ | Open a connection (local path, `tcp://`, `tls://`, `sqlite://`, `postgresql://`, `mariadb://`, `mssql://`, `odbc://`) |
| `AdsConnect` | ✅ | Simplified connect (no user/pw/options) |
| `AdsDisconnect` | ✅ | Close connection and release all handles |
| `AdsGetConnectionType` | ✅ | Returns `ADS_LOCAL_SERVER` or `ADS_REMOTE_SERVER` |
| `AdsIsConnectionAlive` | ✅ | Heartbeat check (ping) |
| `AdsResetConnection` | ⚠️ | No-op, returns success |
| `AdsFindConnection` | 🔴 | `AE_FUNCTION_NOT_AVAILABLE` — lookup by server path |
| `AdsFindConnection25` | 🔴 | Versioned overload (X# compat) |
| `AdsTestLogin` | ⚠️ | Accept-and-ignore |
| `AdsConnect26` | ➡️ | Forwards to `AdsConnect60` |
| `AdsDisableLocalConnections` | ⚠️ | No-op, returns success |

---

## 2. Table Operations

| Function | Status | Description |
|----------|--------|-------------|
| `AdsOpenTable` | ✅ | Open a DBF/CDX/NTX/ADT file |
| `AdsCloseTable` | ✅ | Close table and release resources |
| `AdsCloseAllTables` | ✅ | Close every open table |
| `AdsCreateTable` | ✅ | Create a new DBF/ADT with field definitions |
| `AdsRestructureTable` | ✅ | Alter table structure (add/drop/rename fields) |
| `AdsGetTableType` | ✅ | Returns `ADS_CDX`, `ADS_NTX`, `ADS_ADT`, etc. |
| `AdsGetTableFilename` | ✅ | Returns full path to the table file |
| `AdsGetTableAlias` | ✅ | Returns the table alias |
| `AdsGetTableCharType` | ✅ | Returns `ADS_ANSI` or `ADS_OEM` |
| `AdsGetTableConType` | ✅ | Returns connection type of the table |
| `AdsGetTableConnection` | ✅ | Returns the connection handle for a table |
| `AdsGetTableOpenOptions` | ✅ | Returns the open-mode flags |
| `AdsCheckExistence` | ✅ | Test if a file exists on disk |
| `AdsDeleteFile` | ✅ | Delete a file from the data directory |
| `AdsGetNumOpenTables` | ✅ | Returns count of open tables |
| `AdsOpenTable90` | ➡️ | Forwards to `AdsOpenTable` |
| `AdsCreateTable71` | ➡️ | Forwards to `AdsCreateTable` |
| `AdsCreateTable90` | ➡️ | Forwards to `AdsCreateTable` |
| `AdsRestructureTable90` | ➡️ | Forwards to `AdsRestructureTable` |
| `AdsGetTableHandle25` | 🔴 | `AE_FUNCTION_NOT_AVAILABLE` — lookup by name |

---

## 3. Record Navigation

| Function | Status | Description |
|----------|--------|-------------|
| `AdsGotoTop` | ✅ | Move to first record |
| `AdsGotoBottom` | ✅ | Move to last record |
| `AdsGotoRecord` | ✅ | Jump to specific recno |
| `AdsSkip` | ✅ | Skip ±N records |
| `AdsAtEOF` | ✅ | Test if at end-of-file |
| `AdsAtBOF` | ✅ | Test if beginning-of-file |
| `AdsIsFound` | ✅ | Test if last `Seek` hit |
| `AdsContinue` | ✅ | Continue a `LOCATE` scan |
| `AdsGetRecordNum` | ✅ | Returns current recno |
| `AdsGetRecordCount` | ✅ | Returns total record count |
| `AdsIsRecordVisible` | ✅ | Test if record passes current filter/AOF |
| `AdsGetBookmark` | ✅ | Get current position bookmark (handle) |
| `AdsGetBookmark60` | ✅ | Get bookmark as byte array |
| `AdsGotoBookmark60` | ✅ | Restore position from byte-array bookmark |

---

## 4. Field Read (by type)

| Function | Status | Description |
|----------|--------|-------------|
| `AdsGetField` | ✅ | Read field as text (any type) |
| `AdsGetFieldW` | ✅ | Read field as UTF-16 text |
| `AdsGetFieldRaw` | ✅ | Read raw on-disk bytes |
| `AdsGetFieldName` | ✅ | Get field name by ordinal |
| `AdsGetFieldType` | ✅ | Get field type character (C/N/D/L/M/…) |
| `AdsGetFieldLength` | ✅ | Get field width in bytes |
| `AdsGetFieldDecimals` | ✅ | Get decimal places (numeric fields) |
| `AdsGetNumFields` | ✅ | Get number of fields |
| `AdsGetString` | ✅ | Read as string |
| `AdsGetStringW` | ✅ | Read as wide string |
| `AdsGetLong` | ✅ | Read as 32-bit integer |
| `AdsGetLongLong` | ✅ | Read as 64-bit integer |
| `AdsGetDouble` | ✅ | Read as double |
| `AdsGetLogical` | ✅ | Read as boolean (`.T.`/`.F.`) |
| `AdsGetJulian` | ✅ | Read as Julian Day Number |
| `AdsGetDate` | ✅ | Read as formatted date string |
| `AdsGetMemoLength` | ✅ | Get memo field data length |
| `AdsGetMemoDataType` | ✅ | Get memo field type (text/binary) |
| `AdsGetBinaryLength` | ✅ | Get binary field data length |
| `AdsGetBinary` | ✅ | Read binary field data |
| `AdsIsNull` | ✅ | Test if field is NULL |

---

## 5. Field Write

| Function | Status | Description |
|----------|--------|-------------|
| `AdsSetString` | ✅ | Write string to field |
| `AdsSetStringW` | ✅ | Write UTF-16 string to field |
| `AdsSetLogical` | ✅ | Write boolean value |
| `AdsSetDouble` | ✅ | Write double value |
| `AdsSetLongLong` | ✅ | Write 64-bit integer |
| `AdsSetJulian` | ✅ | Write Julian Day Number |
| `AdsSetFieldRaw` | ✅ | Write raw bytes to field |
| `AdsSetField` | ✅ | Generic field setter (name or ordinal) |
| `AdsSetEmpty` | ✅ | Set field to empty/blank value |
| `AdsSetNull` | ✅ | Set field to NULL |
| `AdsSetShort` | ✅ | Write short integer |
| `AdsSetMoney` | ✅ | Write MONEY (64-bit scaled) value |
| `AdsSetTime` | ✅ | Write TIME value |
| `AdsSetTimeStamp` | ✅ | Write TIMESTAMP value |
| `AdsSetBinary` | ✅ | Write binary data to field |
| `AdsFileToBinary` | ✅ | Import file contents into binary/memo field |
| `AdsBinaryToFile` | ✅ | Export binary/memo field to file |

---

## 6. Record Operations

| Function | Status | Description |
|----------|--------|-------------|
| `AdsAppendRecord` | ✅ | Append a new blank record |
| `AdsWriteRecord` | ✅ | Flush current record to disk |
| `AdsDeleteRecord` | ✅ | Mark record as deleted |
| `AdsRecallRecord` | ✅ | Undelete (recall) record |
| `AdsRecallAllRecords` | ⚠️ | No-op, returns success |
| `AdsIsRecordDeleted` | ✅ | Test if record is deleted |
| `AdsIsRecordLocked` | ✅ | Test if record is byte-range locked |
| `AdsRefreshRecord` | ✅ | Re-read current record from disk |
| `AdsGetRecordCRC` | ✅ | Compute CRC-32 of current record |
| `AdsWriteAllRecords` | ⚠️ | Returns `AE_SUCCESS` (no-op) |

---

## 7. Locking

| Function | Status | Description |
|----------|--------|-------------|
| `AdsLockRecord` | ✅ | Acquire byte-range lock on a record |
| `AdsUnlockRecord` | ✅ | Release byte-range lock |
| `AdsLockTable` | ✅ | Acquire exclusive table lock |
| `AdsUnlockTable` | ✅ | Release table lock |
| `AdsGetAllLocks` | ✅ | Get array of locked recnos |
| `AdsGetNumLocks` | ✅ | Count of held record locks |
| `AdsIsTableLocked` | ✅ | Test if table is exclusively locked |
| `AdsTestRecLocks` | ⚠️ | No-op, returns success |
| `AdsSetLockCycle` | ✅ | Set lock-escalation cycle |
| `AdsGetLockCycle` | ✅ | Get lock-escalation cycle |
| `AdsSetLockRetryCount` | ✅ | Set lock-retry count |
| `AdsGetLockRetryCount` | ✅ | Get lock-retry count |

---

## 8. Index Operations

| Function | Status | Description |
|----------|--------|-------------|
| `AdsOpenIndex` | ✅ | Open an existing CDX/NTX index file |
| `AdsCloseIndex` | ✅ | Close an index |
| `AdsCloseAllIndexes` | ✅ | Close all indexes on a table |
| `AdsCreateIndex61` | ✅ | Create a CDX/NTX index (v6.1 signature) |
| `AdsCreateIndex` | ✅ | Create an index (legacy signature) |
| `AdsDeleteIndex` | ✅ | Delete an index tag |
| `AdsReindex` | ✅ | Rebuild all bound indexes |
| `AdsGetNumIndexes` | ✅ | Count of open indexes |
| `AdsGetIndexHandle` | ✅ | Get index handle by tag name |
| `AdsGetIndexHandleByOrder` | ✅ | Get index handle by ordinal position |
| `AdsGetIndexExpr` | ✅ | Get key expression of an index |
| `AdsGetIndexName` | ✅ | Get tag name of an index |
| `AdsGetIndexCondition` | ✅ | Get FOR condition of an index |
| `AdsGetIndexFilename` | ✅ | Get filename of an index |
| `AdsGetIndexOrderByHandle` | ✅ | Get ordinal position of an index handle |
| `AdsSetIndexOrder` | ✅ | Set active order by tag name |
| `AdsSetIndexOrderByHandle` | ✅ | Set active order by handle |
| `AdsSetIndexDirection` | ✅ | Set index direction (ascending/descending) |
| `AdsIsIndexCustom` | ✅ | Test if index is custom (user-populated) |
| `AdsIsIndexDescending` | ✅ | Test if index is descending |
| `AdsIsIndexUnique` | ✅ | Test if index is unique |
| `AdsAddCustomKey` | ✅ | Add a custom key to a custom index |
| `AdsDeleteCustomKey` | ✅ | Remove a custom key |
| `AdsExtractKey` | ✅ | Extract the key for the current record |
| `AdsCreateFTSIndex` | ✅ | Create a full-text search index |
| `AdsCreateIndex90` | ➡️ | Forwards to `AdsCreateIndex61` |
| `AdsReindex61` | ➡️ | Forwards to `AdsReindex` |

---

## 9. Seek & Scope

| Function | Status | Description |
|----------|--------|-------------|
| `AdsSeek` | ✅ | Seek to a key value (exact or soft) |
| `AdsSeekLast` | ✅ | Seek to last matching key |
| `AdsSkipUnique` | ✅ | Skip to next unique key |
| `AdsSetScope` | ✅ | Set key-range scope (top/bottom) |
| `AdsClearScope` | ✅ | Clear a scope |
| `AdsGetScope` | ✅ | Read current scope |
| `AdsClearAllScopes` | ⚠️ | No-op, returns success |
| `AdsGetKeyNum` | ✅ | Get relative key position (0.0–1.0) |
| `AdsGetKeyCount` | ✅ | Count keys in current order |
| `AdsGetKeyLength` | ✅ | Get key width in bytes |
| `AdsGetKeyType` | ✅ | Get key data type |
| `AdsGetRelKeyPos` | ✅ | Get relative key position (fraction) |
| `AdsSetRelKeyPos` | ✅ | Position by relative key fraction |

---

## 10. Filter & AOF (Rushmore)

| Function | Status | Description |
|----------|--------|-------------|
| `AdsSetAOF` | ✅ | Install a Rushmore-style optimized filter |
| `AdsGetAOFOptLevel` | ✅ | Get optimization level (FULL/PART/NONE) |
| `AdsClearAOF` | ✅ | Remove installed AOF |
| `AdsRefreshAOF` | ⚠️ | No-op, returns success |
| `AdsEvalAOF` | ✅ | Evaluate AOF expression, report opt level |
| `AdsGetAOF` | ✅ | Get current AOF source string |
| `AdsCustomizeAOF` | ⚠️ | Stub |
| `AdsIsRecordInAOF` | ✅ | Test if a record passes the AOF |
| `AdsSetFilter` | ⚠️ | No-op (non-indexed filter) |
| `AdsGetFilter` | ✅ | Get current filter expression |
| `AdsClearFilter` | ⚠️ | No-op, returns success |
| `AdsFilterOption` | ✅ | Get filter optimization options |

---

## 11. SQL

| Function | Status | Description |
|----------|--------|-------------|
| `AdsCreateSQLStatement` | ✅ | Allocate a SQL statement handle |
| `AdsCloseSQLStatement` | ✅ | Free a SQL statement handle |
| `AdsPrepareSQL` | ✅ | Prepare a SQL statement |
| `AdsGetNumParams` | ✅ | Get number of parameters in prepared SQL |
| `AdsExecuteSQL` | ✅ | Execute prepared SQL, return cursor |
| `AdsExecuteSQLDirect` | ✅ | Execute raw SQL text, return cursor |
| `AdsVerifySQL` | ✅ | Validate SQL syntax without executing |
| `AdsClearSQLParams` | ⚠️ | No-op, returns success |
| `AdsClearSQLAbortFunc` | ⚠️ | No-op, returns success |
| `AdsStmtSetTableLockType` | ✅ | Set lock type for statement |
| `AdsStmtSetTablePassword` | ✅ | Set per-table password |
| `AdsStmtSetTableReadOnly` | ✅ | Set read-only mode |
| `AdsStmtSetTableType` | ✅ | Set result table type |
| `AdsStmtSetTableCharType` | ✅ | Set ANSI/OEM character type |
| `AdsStmtSetTableCollation` | ✅ | Set collation |
| `AdsStmtSetTableRights` | ✅ | Set access rights |
| `AdsStmtDisableEncryption` | ⚠️ | No-op, returns success |
| `AdsStmtClearTablePasswords` | ⚠️ | No-op, returns success |

---

## 12. Transaction (TPS)

| Function | Status | Description |
|----------|--------|-------------|
| `AdsBeginTransaction` | ✅ | Begin a transaction |
| `AdsCommitTransaction` | ✅ | Commit current transaction |
| `AdsRollbackTransaction` | ✅ | Rollback current transaction |
| `AdsInTransaction` | ✅ | Test if inside a transaction |
| `AdsCreateSavepoint` | ✅ | Create a named savepoint |
| `AdsReleaseSavepoint` | ✅ | Release a savepoint |
| `AdsRollbackTransaction80` | ✅ | Rollback to a savepoint (ACE 8.0 signature) |
| `AdsFailedTransactionRecovery` | ✅ | Recover from failed transaction |

---

## 13. Memo / Binary

| Function | Status | Description |
|----------|--------|-------------|
| `AdsGetMemoLength` | ✅ | Get memo data length |
| `AdsGetMemoDataType` | ✅ | Get memo type (text/binary) |
| `AdsGetBinaryLength` | ✅ | Get binary data length |
| `AdsGetBinary` | ✅ | Read binary data |
| `AdsSetBinary` | ✅ | Write binary data |
| `AdsBinaryToFile` | ✅ | Export memo/binary to file |
| `AdsFileToBinary` | ✅ | Import file into memo/binary field |
| `AdsGetMemoBlockSize` | ✅ | Get memo block size |

---

## 14. Table Maintenance

| Function | Status | Description |
|----------|--------|-------------|
| `AdsPackTable` | ✅ | Compact table (remove deleted records) |
| `AdsZapTable` | ✅ | Empty table completely |
| `AdsPackTable_DEFERRED` | ⚠️ | Deferred pack (stub) |
| `AdsZapTable_DEFERRED` | ⚠️ | Deferred zap (stub) |
| `AdsCopyTable` | ✅ | Copy table with optional filter |
| `AdsCopyTableContents` | ✅ | Copy filtered contents to another table |
| `AdsCopyTableContent` | ✅ | Copy all contents (alias) |
| `AdsConvertTable` | ✅ | Convert table between types (DBF↔ADT) |
| `AdsCopyTableStructure` | ✅ | Copy structure only (no data) |
| `AdsCloneTable` | ✅ | Clone table handle (shared data) |

---

## 15. Encryption

| Function | Status | Description |
|----------|--------|-------------|
| `AdsEnableEncryption` | ✅ | Enable encryption on connection |
| `AdsDisableEncryption` | ✅ | Disable encryption |
| `AdsIsEncryptionEnabled` | ✅ | Test if encryption is active |
| `AdsSetEncryptionPassword` | ✅ | Set encryption password |
| `AdsIsTableEncrypted` | ✅ | Test if table is encrypted |
| `AdsIsRecordEncrypted` | ✅ | Test if record is encrypted |
| `AdsEncryptTable` | ✅ | Encrypt entire table |
| `AdsDecryptTable` | ✅ | Decrypt entire table |
| `AdsEncryptRecord` | ✅ | Encrypt current record |
| `AdsDecryptRecord` | ✅ | Decrypt current record |

---

## 16. Data Dictionary (DD)

### Dictionary Lifecycle

| Function | Status | Description |
|----------|--------|-------------|
| `AdsDDCreate` | ✅ | Create a new `.add` dictionary |
| `AdsDDAddTable` | ✅ | Register a table alias |
| `AdsDDRemoveTable` | ✅ | Remove a table alias |
| `AdsDDAddTable90` | ➡️ | Versioned overload for X# |

### Table Properties

| Function | Status | Description |
|----------|--------|-------------|
| `AdsDDGetTableProperty` | ✅ | Read table property (200–216) |
| `AdsDDSetTableProperty` | ✅ | Write table property |
| `AdsDDGetFieldProperty` | ✅ | Read field property (301–309) |
| `AdsDDSetFieldProperty` | ✅ | Write field property |
| `AdsDDGetIndexProperty` | ✅ | Read index property (401–408) |
| `AdsDDSetIndexProperty` | ⚠️ | Stub |

### Database Properties

| Function | Status | Description |
|----------|--------|-------------|
| `AdsDDGetDatabaseProperty` | ✅ | Read database property (1–23) |
| `AdsDDSetDatabaseProperty` | ✅ | Write database property |

### Users & Groups

| Function | Status | Description |
|----------|--------|-------------|
| `AdsDDCreateUser` | ✅ | Create a user |
| `AdsDDDeleteUser` | ✅ | Delete a user |
| `AdsDDAddUserToGroup` | ✅ | Add user to group |
| `AdsDDRemoveUserFromGroup` | ✅ | Remove user from group |
| `AdsDDGetUserProperty` | ✅ | Read user property (1101–1103) |
| `AdsDDSetUserProperty` | ✅ | Write user property |

### Permissions

| Function | Status | Description |
|----------|--------|-------------|
| `AdsDDGetPermissions` | ✅ | Get effective permissions for a grantee |
| `AdsDDGrantPermission` | ✅ | Grant a permission |
| `AdsDDRevokePermission` | ✅ | Revoke a permission |
| `AdsDDSetUserTableRights` | ✅ | Set per-table rights for a user |
| `AdsDDGetUserTableRights` | ✅ | Get per-table rights for a user |

### Index File Management

| Function | Status | Description |
|----------|--------|-------------|
| `AdsDDAddIndexFile` | ✅ | Bind an index file to a table |
| `AdsDDRemoveIndexFile` | ✅ | Unbind an index file |

### Views

| Function | Status | Description |
|----------|--------|-------------|
| `AdsDDCreateView` | ✅ | Create a named SQL view |
| `AdsDDDropView` | ✅ | Delete a view |
| `AdsDDAddView` | ✅ | Alias for `AdsDDCreateView` |
| `AdsDDRemoveView` | ✅ | Alias for `AdsDDDropView` |
| `AdsDDGetViewProperty` | ✅ | Read view property (701–702) |
| `AdsDDSetViewProperty` | ✅ | Write view property |

### Stored Procedures & Functions

| Function | Status | Description |
|----------|--------|-------------|
| `AdsDDCreateProcedure` | ✅ | Create a stored procedure |
| `AdsDDDropProcedure` | ✅ | Delete a stored procedure |
| `AdsDDAddProcedure` | ✅ | Alias for `AdsDDCreateProcedure` |
| `AdsDDRemoveProcedure` | ✅ | Alias for `AdsDDDropProcedure` |
| `AdsDDGetProcProperty` | ✅ | Read procedure property (601–605) |
| `AdsDDSetProcProperty` | ✅ | Write procedure property |
| `AdsDDGetProcedureProperty` | ✅ | Alias for `AdsDDGetProcProperty` |
| `AdsDDSetProcedureProperty` | ✅ | Alias for `AdsDDSetProcProperty` |
| `AdsDDCreateFunction` | ✅ | Register a UDF |
| `AdsDDDropFunction` | ✅ | Delete a UDF |
| `AdsDDGetFunctionProperty` | ✅ | Read UDF property |
| `AdsDDSetFunctionProperty` | ✅ | Write UDF property |

### Triggers

| Function | Status | Description |
|----------|--------|-------------|
| `AdsDDCreateTrigger` | ✅ | Create a trigger (BEFORE/AFTER/INSTEAD OF) |
| `AdsDDDropTrigger` | ✅ | Delete a trigger |
| `AdsDDRemoveTrigger` | ✅ | Alias for `AdsDDDropTrigger` |
| `AdsDDGetTriggerProperty` | ✅ | Read trigger property (501–507) |
| `AdsDDSetTriggerProperty` | ✅ | Write trigger property |

### Referential Integrity

| Function | Status | Description |
|----------|--------|-------------|
| `AdsDDCreateRefIntegrity` | ✅ | Create RI rule (RESTRICT/CASCADE/SETNULL) |
| `AdsDDRemoveRefIntegrity` | ✅ | Delete RI rule |
| `AdsDDCreateRefIntegrity62` | ➡️ | Versioned overload |
| `AdsDDGetRefIntegrityProperty` | ✅ | Read RI property (401–407) |
| `AdsDDSetRefIntegrityProperty` | ✅ | Write RI property |

### Links

| Function | Status | Description |
|----------|--------|-------------|
| `AdsDDCreateLink` | ✅ | Create cross-dictionary link |
| `AdsDDDropLink` | ✅ | Delete link |
| `AdsDDModifyLink` | ✅ | Update link credentials/path |

### Object Enumeration

| Function | Status | Description |
|----------|--------|-------------|
| `AdsDDFindFirstObject` | ✅ | Start iterating DD objects by type |
| `AdsDDFindNextObject` | ✅ | Continue iteration |
| `AdsDDFindClose` | ✅ | Close iteration handle |

---

## 17. Expression Evaluation

| Function | Status | Description |
|----------|--------|-------------|
| `AdsEvalLogicalExpr` | ✅ | Evaluate expression as boolean |
| `AdsEvalNumericExpr` | ✅ | Evaluate expression as double |
| `AdsEvalStringExpr` | ✅ | Evaluate expression as string |
| `AdsEvalTestExpr` | ⚠️ | Stub |
| `AdsIsExprValid` | ✅ | Validate expression syntax |

---

## 18. Server Telemetry (AdsMg*)

| Function | Status | Description |
|----------|--------|-------------|
| `AdsMgConnect` | ✅ | Open management telemetry channel |
| `AdsMgDisconnect` | ✅ | Close management channel |
| `AdsMgGetActivityInfo` | ✅ | Get server activity snapshot |
| `AdsMgGetCommStats` | ✅ | Get communication statistics |
| `AdsMgGetConfigInfo` | ✅ | Get server configuration |
| `AdsMgGetInstallInfo` | ✅ | Get installation info |
| `AdsMgGetLockOwner` | ✅ | Get owner of a specific lock |
| `AdsMgGetLocks` | ✅ | List all locks |
| `AdsMgGetOpenIndexes` | ✅ | List open indexes |
| `AdsMgGetOpenTables` | ✅ | List open tables |
| `AdsMgGetOpenTables2` | ✅ | Extended table list |
| `AdsMgGetServerType` | ✅ | Get server type |
| `AdsMgGetUserNames` | ✅ | List connected users |
| `AdsMgGetWorkerThreadActivity` | ✅ | Get worker thread info |
| `AdsMgKillUser` | ✅ | Disconnect a user |
| `AdsMgResetCommStats` | ✅ | Reset communication counters |
| `AdsMgDumpInternalTables` | ✅ | Dump internal table metadata |

---

## 19. Full-Text Search

| Function | Status | Description |
|----------|--------|-------------|
| `AdsCreateFTSIndex` | ✅ | Create FTS index on a field |
| `AdsFTSSearch` | ✅ | Search FTS index with word query |
| `AdsGetFTSIndexes` | ⚠️ | Stub |

---

## 20. Miscellaneous

| Function | Status | Description |
|----------|--------|-------------|
| `AdsGetVersion` | ✅ | Get ACE version (major, minor, letter, desc) |
| `AdsGetLastError` | ✅ | Get last error code and message |
| `AdsGetErrorString` | ✅ | Get human-readable error string |
| `AdsGetServerName` | ✅ | Get server name |
| `AdsGetServerTime` | ✅ | Get server timestamp |
| `AdsGetDateFormat` | ✅ | Get process-wide date format |
| `AdsSetDateFormat` | ✅ | Set process-wide date format |
| `AdsGetLastTableUpdate` | ✅ | Get last-update date from DBF header |
| `AdsGetLastAutoinc` | ✅ | Get last autoincrement value |
| `AdsShowDeleted` | ✅ | Toggle `SET DELETED` visibility |
| `AdsGetDeleted` | ✅ | Query `SET DELETED` state |
| `AdsSetCollation` | ✅ | Set collation order |
| `AdsConvertOemToAnsi` | ✅ | OEM→ANSI character conversion |
| `AdsConvertAnsiToOem` | ✅ | ANSI→OEM character conversion |
| `AdsGetEpoch` | ✅ | Get 2-digit year pivot |
| `AdsSetEpoch` | ⚠️ | No-op, returns success |
| `AdsGetExact` | ✅ | Get `SET EXACT` state |
| `AdsSetExact` | ⚠️ | No-op, returns success |
| `AdsGetDefault` | ✅ | Get default drive/path |
| `AdsSetDefault` | ⚠️ | No-op, returns success |
| `AdsGetSearchPath` | ✅ | Get search path |
| `AdsSetSearchPath` | ⚠️ | No-op, returns success |
| `AdsGetNumActiveLinks` | ✅ | Count active DD links |
| `AdsGetNumOpenTables` | ✅ | Count open tables |
| `AdsApplicationExit` | ⚠️ | No-op, returns success |
| `AdsThreadExit` | ⚠️ | No-op, returns success |
| `AdsInitRawKey` | ⚠️ | No-op, returns success |
| `AdsGetRecord` | ⚠️ | Stub |
| `AdsSetRecord` | 🔴 | `AE_FUNCTION_NOT_AVAILABLE` |
| `AdsGetMilliseconds` | ⚠️ | Stub |
| `AdsSetMilliseconds` | 🔴 | `AE_FUNCTION_NOT_AVAILABLE` |
| `AdsData` | ⚠️ | No-op, returns success |

---

## 21. Callbacks & Caching (Stubs)

These functions are accepted for ABI compatibility but have no
effect in OpenADS:

| Function | Returns |
|----------|---------|
| `AdsRegisterCallbackFunction` | `AE_SUCCESS` |
| `AdsRegisterProgressCallback` | `AE_SUCCESS` |
| `AdsClearCallbackFunction` | `AE_SUCCESS` |
| `AdsClearProgressCallback` | `AE_SUCCESS` |
| `AdsCacheOpenCursors` | `AE_SUCCESS` |
| `AdsCacheOpenTables` | `AE_SUCCESS` |
| `AdsCacheRecords` | `AE_SUCCESS` |
| `AdsCloseCachedTables` | `AE_SUCCESS` |
| `AdsSetDecimals` | `AE_SUCCESS` |
| `AdsShowError` | `AE_SUCCESS` |
| `AdsSetServerType` | `AE_SUCCESS` |

---

## 22. RI & Enforcement Toggles

| Function | Status | Description |
|----------|--------|-------------|
| `AdsEnableRI` | ⚠️ | No-op, returns success |
| `AdsDisableRI` | ⚠️ | No-op, returns success |
| `AdsEnableUniqueEnforcement` | ⚠️ | No-op |
| `AdsDisableUniqueEnforcement` | ⚠️ | No-op |
| `AdsEnableAutoIncEnforcement` | ⚠️ | No-op |
| `AdsDisableAutoIncEnforcement` | ⚠️ | No-op |
| `AdsCancelUpdate` | ⚠️ | No-op |

---

## 23. Deferred Flush

| Function | Status | Description |
|----------|--------|-------------|
| `AdsSetDeferredFlush` | ✅ | Toggle deferred flush mode (528× bulk-insert speedup) |
| `AdsFlushFileBuffers` | ✅ | Force fsync on table + index files |

---

## 24. Relation (Stubs)

| Function | Status | Description |
|----------|--------|-------------|
| `AdsSetRelation` | 🔴 | `AE_FUNCTION_NOT_AVAILABLE` |
| `AdsSetScopedRelation` | ⚠️ | No-op, returns success |
| `AdsClearRelation` | ⚠️ | No-op, returns success |

---

## 25. Legacy / Lookup

| Function | Status | Description |
|----------|--------|-------------|
| `AdsFindFirstTable` | ✅ | Find first table matching a mask |
| `AdsFindNextTable` | ✅ | Find next table matching a mask |
| `AdsFindClose` | ✅ | Close find handle |
| `AdsFindFirstTable62` | ➡️ | Versioned overload |
| `AdsFindNextTable62` | ➡️ | Versioned overload |
| `AdsIsServerLoaded` | ✅ | Test if server is running locally |

---

## Summary

| Category | Total | ✅ | ⚠️ | 🔴 |
|----------|------:|----:|----:|----:|
| Connection | 11 | 7 | 2 | 2 |
| Table | 20 | 15 | 2 | 3 |
| Navigation | 14 | 14 | 0 | 0 |
| Field Read | 21 | 21 | 0 | 0 |
| Field Write | 17 | 17 | 0 | 0 |
| Record | 10 | 8 | 2 | 0 |
| Locking | 12 | 10 | 2 | 0 |
| Index | 27 | 25 | 0 | 2 |
| Seek & Scope | 13 | 12 | 1 | 0 |
| Filter & AOF | 12 | 8 | 4 | 0 |
| SQL | 18 | 12 | 6 | 0 |
| Transaction | 8 | 8 | 0 | 0 |
| Memo/Binary | 8 | 8 | 0 | 0 |
| Maintenance | 10 | 8 | 2 | 0 |
| Encryption | 10 | 10 | 0 | 0 |
| Data Dictionary | 42 | 40 | 2 | 0 |
| Expression Eval | 5 | 4 | 1 | 0 |
| Telemetry | 17 | 17 | 0 | 0 |
| FTS | 3 | 2 | 1 | 0 |
| Miscellaneous | 31 | 18 | 11 | 2 |
| Callbacks & Caching | 11 | 0 | 11 | 0 |
| RI Toggles | 7 | 0 | 7 | 0 |
| Deferred Flush | 2 | 2 | 0 | 0 |
| Relation | 3 | 0 | 2 | 1 |
| Legacy / Lookup | 6 | 4 | 0 | 2 |
| **TOTAL** | **357** | **~250** | **~56** | **~12** |

The genuinely unimplemented functions (`AE_FUNCTION_NOT_AVAILABLE`)
are: `AdsSetRelation`, `AdsFindConnection`, `AdsFindConnection25`,
`AdsGetTableHandle25`, `AdsSetRecord`, `AdsSetMilliseconds`,
`AdsCreateIndex90` (forward only), `AdsReindex61` (forward only),
`AdsDDCreateRefIntegrity62` (forward only), `AdsFindFirstTable62`
(forward only), `AdsFindNextTable62` (forward only). All other
stubs return `AE_SUCCESS` so calling applications do not fail.
