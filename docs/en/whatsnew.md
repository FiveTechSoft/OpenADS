---
title: What's New
layout: default
parent: Home (EN)
nav_order: 0
permalink: /en/whatsnew/
---

# What's New (v1.0.0-rc29 → v1.8.10)

This page summarises the most notable changes since the
v1.0.0-rc29 release. For the full commit-by-commit history see
the [CHANGELOG](https://github.com/FiveTechSoft/OpenADS/blob/main/CHANGELOG.md).

---

## v1.8.10 Highlights

### REMOTE — `SET DELETED ON` on scoped walks

`AdsShowDeleted(0)` (SET DELETED ON) now propagates to the server over
the wire (`ShowDeleted` 0xDA/0xDB). Scoped index navigation
(`OrdScope` / `AdsSetScope` + `GotoTop`/`Skip`) no longer returns rows
flagged deleted on remote aliases while LOCAL mode already hid them.

Requires updated `openace64.dll` **and** `openads_serverd`. Regression
test: `remote AdsSetScope with SET DELETED ON skips deleted rows` in
`abi_remote_index_nav_test.cpp`.

---

## v1.8.1 Highlights

### ABI — `OrdScope` string key padding

Harbour passes scope strings at trimmed length, but CDX keys are
space-padded. `setScopeTop` / `setScopeBottom` on a character work-order
field (top == bottom) could land `GotoTop` at EOF on **local and remote**.
`AdsSetScope` now pads to index `key_length`. Test: `QA-D` in
`abi_qa_repro_test.cpp`.

---

## v1.8.0 Highlights

### CDX — NTXPL852 / PL852 OEM collation

`AdsSetCollation` accepts `NTXPL852` and `PL852` for Polish CP-852 index
sorting. Ł (0x9D) sorts between L and M on CDX build, seek, insert, and
reindex — matching Harbour / Clipper local-server behaviour.

### CDX — bulk `REINDEX`

`Table::reindex()` for CDX tags uses the same bottom-up `build_bulk()`
path as `CREATE INDEX`, replacing the slow erase-then-insert walk.

### Tests

19 new unit tests cover engine collation weights, CDX driver bulk sort,
and ABI soft/hard seek + index walk.

---

## v1.7.0 Highlights

### REMOTE — `AdsSetScope` / `OrdScope` navigation

`OrdScope` top/bottom bounds were sent to the server but `GotoTop`/`Skip`
ignored them — scoped walks returned every record in the table. Fixed by
marking the parent table in `ordered_tables_` after `SetScope`, and by
always syncing `SetOrder` before remote index navigation.

Requires updated `openace64.dll` **and** `openads_serverd`. Regression
tests in `abi_remote_index_nav_test`; harness `openads_remote_scope_probe`.

### Docs — CDX rollback warning

[Migrating from ADS](migrating-from-ads/) now warns that CDX files written
by OpenADS use Harbour's `RCHB` header and are unreadable by SAP ACE
(error 7017). Back up SAP-built `.cdx` before migration if rollback is
possible.

---

## v1.6.5 Highlights

### REMOTE — `OrdKeyCount()` + `AdsGetDate()` fixes

- **`OrdKeyCount()` returned 0** on remote aliases — xBrowse grids showed
  no rows. New `GetKeyCount` wire opcode (0xB0/0xB1) computes the true
  filtered key count from the active index order.
- **`AdsGetDate()` crashed** on remote `ADS_DATE` columns when rddads
  passed a `RemoteIndex` handle. Resolves to parent `RemoteTable` before
  field I/O.

---

## v1.6.4 Highlights

### REMOTE — FiveWin TDataBase / ADSRDD fixes (Dates + unlocked writes)

- Date columns (`ADS_DATE`) no longer crash `FieldGet` / `AdsGetJulian` in
  remote mode (safe ordinal resolution + row cache + server `YYYYMMDD`
  normalisation).
- `FieldPut` / `AdsSet*` on an unlocked record now returns
  `AE_RECORD_NOT_LOCKED` (5035) instead of AV — the "probe lock by write"
  idiom works and produces the expected `EG_UNLOCKED`.
- All remote Get/Set paths hardened against field ordinals passed as small
  pointers; repeated `FieldGet` and post-open/EOF cases are stable.
- `AdsGetAllLocks` no longer 5000-crashes on remote handles.
- Server lock forwarding and post-`Append` write handling improved.

See CHANGELOG for full details. Requires updated `openace64.dll` +
`openads_serverd`.

## v1.6.3 Highlights

### REMOTE / FWH — xBrowse over `tcp://`

Fixes Harbour `ADSCDX` + FWH `xBrowse` / `TDataBase` with production CDX
tags (`OrdSetFocus`, e.g. `CUSTNAME`):

- **`AdsKeyNo` / scrollbar** — logical key position (`1..n`) instead of
  physical `RecNo`; `AdsGetRelKeyPos` / `AdsSetRelKeyPos` for remote
  indexed tables.
- **Bookmark `DbGoto` after paint** — server syncs the ABI index cursor
  on `GotoRecord` so rows no longer shift on every `Refresh`.
- **GoUp at top** — `AdsAtBOF` reports BOF after `Skip(-1)` at key #1;
  stops the first-row rubber-band repeat.

Requires **both** `openace64.dll` (client) and an updated
`openads_serverd` (GotoRecord ABI sync). Regression tests in
`abi_remote_index_nav_test`.

---

## v1.6.2 Highlights

### Production CDX tag names & append-row `FieldGet`

SAP/BCC compound CDX tag names (`CUSTNAME`, not `AME`); blank
`AdsGetField` at BOF/EOF for FWH `td_blankrow()`.

---

## v1.5.1 Highlights

### Security & remote hardening

Path jail on remote `Connect`, coherent nested `LockMgr` unlock, TLS
verification by default, and remote routing for memo/Unicode/date/raw
field writes.

### Harbour smoke CI

Windows GitHub Actions job plus `run_harbour_smoke.ps1` /
`bootstrap_harbour_ci.ps1` for end-to-end Harbour + ACE ABI validation.

### Remote ABI (Fase 2)

`AdsSetRelation`, `AdsSetRecord` / `AdsGetRecord`, and `AdsCustomizeAOF`
over `tcp://`. Local `AdsAggregate` / `AdsFetchWhere` on in-process DBF
tables.

### Plus — SQLite & MSSQL navigational write

SQLite and native MS SQL Server (TDS) backends now support
`AdsAppendRecord`, `AdsSetString`, `AdsWriteRecord`, and
`AdsDeleteRecord` — parity with MariaDB, PostgreSQL, Firebird, and ODBC.
MSSQL `ADS_STRING` fields are padded to declared width on read.

### Engine

Committed ADT/ADI fixtures for CI; VFP `0x32` header (autoinc + nullable
columns together).

---

## v1.5.0 Highlights

### SQL Backend — Transaction Management (SQLRDD Pattern)

New `BackendTxManager` provides nested transaction support for SQL
backends: BEGIN/COMMIT with SAVEPOINT emulation at nesting > 1,
auto-commit after N DML statements (configurable), and dirty-flag
tracking. Available on SQLite and PostgreSQL via `BackendTableOps`
vtable (`begin_tx`/`commit_tx`/`rollback_tx`/`set_auto_commit`).

### SQL Backend — Lazy Column Loading with Learning

`BackendFieldOptimizer` tracks which columns are actually read per
table. After 5 unique single-column fetches, it automatically
switches to `SELECT *` — avoiding repeated demand-fetches for
tables where many columns are eventually needed.

### SQL Backend — Restrictor Composition

`BackendWhereBuilder` combines For clause, user filter, scope
bounds, index restrictions, AOF predicates, and recno filters into
a single AND-ed WHERE clause. Handles exact seek (collapses to `=`)
and range seek.

### SQL Push-Down — 50+ New Translatable Functions

The `try_emit_sql_where()` emitter now translates STR, VAL, DTOS,
DTOC, CTOD, ROUND, CEILING, MOD, EXP, LOG, SQRT, SIGN, PADR/PADL/PADC,
STRTRAN, LEFT, RIGHT, AT/ATNUM, IIF, IF, NIL, ISNULL, ISBLANK, EMPTY,
LEN, YEAR, MONTH, DAY, HOUR, MINUTE, SECOND, DOW, CDOW, CMONTH, NOW,
and more into portable SQL. The `$` contains operator now supports
field-to-field (`field1 $ field2 → field2 LIKE '%' || field1 || '%'`).

### UNION / UNION ALL

The SQL parser handles `SELECT ... UNION [ALL] SELECT ...` with any
nesting depth. Each UNION member carries its own WHERE, ORDER BY,
LIMIT, and aliases.

### DDL Parsing

New `ALTER TABLE`, `DROP TABLE`, `DROP INDEX` statement parsers with
support for IF EXISTS, quoted identifiers, and column definitions.
Ready for backend execution hooks.

### AOF Expression — LIKE and IS NULL

The AOF expression layer now supports `LIKE '%pattern%'` with `%`/`_`
wildcards, and `IS NULL` / `IS NOT NULL` unary operators.

---

## New Features

### SQLite-backed Table Driver

An optional SQLite-backed table driver is now available behind
the `OPENADS_WITH_SQLITE` CMake flag. When enabled, the engine
can open and manipulate tables through a SQLite backend, providing
an alternative storage layer. New source files:

- `src/sql_backend/sqlite_backend.cpp`
- `src/sql_backend/sqlite_connection.cpp`
- `src/sql_backend/sqlite_table.h`
- `src/sql_backend/sqlite_index.h`

### SQL Backends — PostgreSQL / MariaDB / ODBC (OpenADS Plus)

OpenADS can now open tables on **PostgreSQL**, **MariaDB / MySQL**
and any **ODBC**-reachable engine behind the ACE ABI, selected by
the connection URI (`postgresql://` / `mariadb://` / `odbc://`),
exactly like the SQLite backend. From the application's point of
view the table behaves like any other work area — navigation,
field read, and column SEEK all work.

These four SQL backends sit behind a single **pluggable
backend-ops registry**: each registers one `BackendTableOps`
struct (17 function pointers mirroring the table ops) so the ~17
ABI navigation / field functions stay backend-agnostic instead of
multiplying a per-backend `if` block across each. Adding another
backend (e.g. Firebird, or MSSQL / Oracle via ODBC) is one ops
struct plus one registration line. The native local DBF / ADT and
the `tcp://` remote paths are unchanged fall-throughs. Identifiers
are validated to safe ASCII and SEEK values use prepared-statement
parameters (no string concatenation). See `docs/OPENADS_PLUS.md`.

### ADT Validation Patches (F1–F7, R1–R3)

ADT table validation now includes a full set of structural
patches (F1–F7) and record-level checks (R1–R3), strengthening
the integrity guarantees for `.adt` files produced by SAP
Advantage.

### Native ADT/ADI Create, Read, Write, and Index Seek

OpenADS can now operate end-to-end on native `.adt` / `.adi` /
`.adm` files:

- **Create** — `AdsCreateTable(ADS_ADT)` writes a valid table
  header, field descriptors, and an optional `.adm` memo store.
- **Write** — `AdsAppendRecord` / `AdsWriteRecord` persist rows
  and memo payloads.
- **Read** — Re-open, field get, record count, memo round-trip.
- **Index** — `AdsCreateIndex61` builds `.adi` bags (first tag
  via `AdiIndex::create`, additional tags via `add_tag`).
- **Seek** — `AdsSeek` on character and numeric ADI keys.
- **AUTOINC** — counter seeded from existing rows at open time;
  descriptor tail bytes 139–143 stay zero on disk.
- **ADM memo layout** — 8-byte blocks with a 1024-byte metadata
  prefix.

### ADI Write Path

The ADI index driver now supports write operations — `insert`,
`erase`, and `flush` — including dense-leaf recno decoding and
character-key seek. This completes the read-write cycle for ADI
indexes.

### Trigger Dispatch (BEFORE / INSTEAD_OF / AFTER)

Triggers now fire with proper timing dispatch:

- **BEFORE** — runs before the DML statement.
- **INSTEAD_OF** — replaces the DML on views.
- **AFTER** — runs after successful execution.

Priority sort, `__error` table for failures,
`sp_DisableTriggers` / `sp_EnableTriggers` stored procedures,
and composite trigger keys in `system.triggers` are all
supported.

### DA-Web Management UI

The browser-based Data Architect replacement (**DA-Web**) has
received extensive work:

- **Inline cell editing** with dirty tracking and visual
  feedback.
- **Index management** — save and delete indexes via API.
- **Trigger CRUD** — add, delete, and edit triggers with
  splitter and inline validation.
- **AOF (Rushmore) filter toolbar** in the table browser.
- **ADS SQL syntax highlighting** with HeidiSQL-like colours.
- **Stored procedure / function source viewer** with parameter
  grid and Save-to-DD.
- **Index tag browser**, field type labels, and SQL scripts.
- **Connection menu** — New DD, Open DD, Free Tables.
- **Effective Permissions** and **Members** tabs on user/group
  panels.
- **RI tag dropdowns** populated from binary `.add` files.

### openmonitor — TUI and Web Dashboard

A new `openmonitor` tool provides both a terminal UI (TUI) and a
web dashboard for monitoring and administering `openads_serverd`.

### OpenADS Studio — Responsive (phone / tablet)

The Studio web console now adapts to small screens — usable from a
phone or tablet, not just a desktop browser. Below ~768 px the
table-list sidebar becomes a slide-in **drawer** (☰ in the header,
dimmed backdrop, auto-close on select); the tab bar scrolls
horizontally and forms / modals reflow to a single column with
larger touch targets. A long-standing dark-theme bug — the
`--panel` / `--panel-2` / `--border` CSS variables were
self-referential, so panels and borders rendered transparent — is
fixed as part of this work.

### Remote Scan Performance (sequential prefetch)

A forward scan over the `tcp://` wire used to cost roughly one TCP
round-trip per record (`Skip` + `AtEOF` + `IsFound`). A
sequential-prefetch path (negotiated via a Connect capability flag)
now piggybacks a lookahead block of rows onto forward-`Skip` acks;
the client serves them locally and folds the consumed count back
into the next wire step so the server cursor never desyncs.
`AdsAtEOF` / `AdsAtBOF` are answered from the cached current row and
`AdsIsFound` from a cached `Found()` flag. Result: a 50k-record
loopback scan is **~3.9× faster** (NAV-only) / **~3.3×** (3-field
read), with `IsFound` round-trips dropping to zero. The change is
additive and backward-compatible — old clients (no capability
advertised) keep the previous wire behaviour byte-for-byte.

### PHP Native Extension (`php_openads`)

A native Zend PHP extension (`php_openads.dll`) is now available
for PHP 8.x, providing full DD CRUD (35 new `AdsDictionary`
methods), date/timestamp field decoding, and per-statement field
name caching.

### SAP Data Dictionary Import Improvements

- `import_dd` now copies `.am` memo files and decodes encrypted
  function bodies.
- Group membership import (DB:Admin, DB:Backup, DB:Debug) from
  binary `.add` files.
- Trigger timing captured from `system.triggers`.
- `grant_permission` and `AE_SAP_PERMS_NEED_IMPORT` error code
  for permission migration.

### WAL Crash Recovery

WAL (Write-Ahead Log) crash recovery now handles `APPEND`
records, completing the ARIES-lite recovery model.

### DD SQL Expansion

- `CREATE DATABASE`, `GRANT` / `REVOKE`.
- `sp_*` stored procedures.
- `system.*` virtual tables (`system.iota`, `system.columns`).
- `AdsDDGet/SetFieldProperty`, triggers, stored procedures,
  views, and index properties.
- Per-table access control with user/group permission levels.

### Server-Side Aggregation (Tier-3)

`AdsAggregate` now supports `COUNT`, `SUM`, `AVG`, `MIN`, and
`MAX` push-down to SQL backends (SQLite, PostgreSQL, MariaDB,
ODBC). The aggregate spec is validated before execution, and
results are served through a handle-based result set
(`AdsAggregateCount` / `AdsAggregateValue` /
`AdsAggregateClose`).

### FetchWhere V2

`AdsFetchWhere` now serves forward scans from a cached result
set — no per-match `goto_record` round-trip. The client
receives rows in bulk and walks them locally, with optional
per-row recno (`WANT_RECNO` flag). Non-AOF `SET FILTER` bulk
scans are routed through `AdsFetchWhere` for significant
throughput gains.

### ODBC Driver (slice 1–3)

A full ODBC driver (`openads_odbc.dll`) is now available:

- **SELECT round-trip** with scrollable cursors
  (`SQLFetchScroll`).
- **Typed column access** — `SQLDescribeCol` /
  `SQLColAttribute` / `SQLGetData` dispatch through the
  backend ops vtable.
- **Positional parameter binding** via `SQLBindParameter`.
- **Catalog functions** — `SQLPrimaryKeys` /
  `system.primarykeys`.
- **App-lock emulation** — `rLock()`/`fLock()` via SQL Server
  `sp_getapplock`, PostgreSQL advisory locks, MariaDB named
  locks, and Firebird `OPENADS$LOCKS` table.

### Native Write Path (PostgreSQL / MariaDB / Firebird)

`AdsAppendRecord` / `AdsSetField` / `AdsWriteRecord` /
`AdsDeleteRecord` now work end-to-end on PostgreSQL, MariaDB,
and Firebird backends — no ODBC passthrough required.

### SQL Push-Down Expansion

`SET FILTER` and AOF expressions are now pushed down to SQLite
and PostgreSQL as `WHERE` clauses when the expression tree is
within the optimisable subset (`try_emit_sql_where`). Coverage
includes `$` (contains), `LEFT()`, `RIGHT()`, `SUBSTR()`, and
`UPPER()`.

### Complete API Documentation (Portuguese)

All **364 ACE functions** are now documented in Portuguese
(pt-BR) under `docs/pt/funcoes/`, covering syntax, parameters,
return values, and examples.

### x86 (32-bit) Calling Convention Fix

`ENTRYPOINT` is now `__stdcall` (WINAPI) on Win32, matching
Harbour's `rddads` calling convention. This fixes stack
corruption when 32-bit Harbour apps call ACE functions through
the DLL. The x86 `.def` file and import library are updated
to match. (Reported by Jonsson / RusSoft Ltda.)

### CI — msvc-x86 Build Leg

A new `msvc-x86` matrix entry in `.github/workflows/ci.yml`
ensures 32-bit builds are tested on every PR, catching
x86-only breakage (bitness-dependent narrowing, `SQLLEN*`
signature mismatches, `/WX` warnings) before they reach main.

---

## Bug Fixes

### Engine

- **CDX tag order** — `list_tags()` now sorts by tag-header
  offset (creation order) instead of alphabetical leaf order.
  Fixes `DBSETORDER(n)` selecting the wrong tag on CDX bags
  written by SAP ADS or BCC Harbour. (Reported by Jonsson /
  RusSoft Ltda.)
- **CDX expression-index key size** — composite expression
  keys (e.g. `UPPER(cName)`) are now sized from the expression's
  natural fixed-width length, not the first record's content
  length. The old rtrim truncated keys, causing rows out of
  order after reindex on large tables. (Reported by Jonsson /
  RusSoft Ltda.)
- **CDX empty-leaf walk** — forward and backward index walks now
  skip empty leaves left behind by `erase()`. Fixes REINDEX /
  bulk-delete `ADSCDX/5000`. (PR #63)
- **CDX leaf recno bits** — `compute_layout` sizes the
  record-number field from `max_rec`, not just key length, so
  wide-key tags no longer truncate recnos ≥ 4096. (PR #62)
- **CDX prefix seek** — `seek_key` compares only the search-key
  length, so partial seeks like `SEEK "ART-00024800"` match stored
  `"ART-00024800 desc ..."` keys. (PR #62)
- **Conditional FOR on logical fields** — the index expression
  evaluator now treats logical fields as numeric (0/1) instead of
  truthy strings, so `FOR ACTIVE` correctly filters `.F.` records.
  (PR #121)
- **INDEX ON corruption** — prevent `INDEX ON` from corrupting
  source table indexes. (PR #118)
- **MSSQL backward SKIP** — off-by-one: `abs_n == pos` now reaches
  row 0 instead of reporting BOF. (PR #65)
- **ABI typed getters for SQL backends** — `AdsGetDouble`/`Long`/`
  LongLong`/`String` dispatch through the backend ops vtable, so
  PostgreSQL returns real values. (PR #66)
- **`AdsGetIndexHandle` for SQL backends** — resolves by-name for
  PG tables so indexed seek works end-to-end. (PR #66)
- **NTX numeric key format** — numeric fields indexed into an NTX
  bag now store keys in the native DBFNTX form (zero-padded
  magnitude + complemented negatives) instead of space-padded
  `STR()` text. A native xBase reader's `dbSeek(<number>)` now
  matches the on-disk key. Reopened index bags retain the numeric
  encoding. (PR #67)
- **Numeric `dbSeek`** — rddads sends `ADS_STRING` key type for
  numeric seeks; the engine now handles this correctly.
- **`ALIAS->FIELD` in numeric seek** — strip `ALIAS->` prefix so
  aliased CDX tags find keys.
- **Transaction rollback** — physically removes appended records
  on rollback instead of leaving ghost rows.
- **LockMgr held-state leak** — `held_` is now cleared on unlock
  so the next lock takes a real OS lock.
- **`AdsGetRecordCount`** — now respects `bFilterOption`.
- **`AdsSetRelation`** — fails honestly when appropriate.
- **`seek_key`** — `walk_to_last` now honours `SET DELETED ON`.
- **Empty table navigation** — correctly handles tables with zero
  records.
- **Deleted-record navigation** — proper state after skip past
  deleted rows.
- **LockMgr lock refcount** — repeated locks on the same key are
  now refcounted; the OS lock is released only when the final
  holder unlocks.
- **WAL record bounds checks** — `TxLog::read_all` validates each
  UPDATE/APPEND field length before reading, so truncated or
  corrupt WAL files no longer over-read.

### ABI

- **Out-of-bounds read** in numeric key formatting — prevented.
- **Swapped option bits** — `ADS_DESCENDING` (0x02) and
  `ADS_COMPOUND` (0x08) were decoded incorrectly in
  `AdsCreateIndex61`.
- **Case-insensitive field-name resolution** — `field_index` is
  now cached for performance.
- **`AE_NO_CURRENT_RECORD`** — returns 5068 instead of 5026.
- **`OrdListAdd`** — falls back to basename when a relative path
  double-prefixes the table directory.
- **Trig helper linkage** — C++ linkage for `trig_*` helpers to
  silence MSVC C4190.
- **`AdsGetField` crash on SQL backends** — reading by field ordinal
  no longer crashes.
- **AdsGetRecordCount on conditional ORDER** — counts FOR matches
  correctly. (PR #100)
- **`AdsSetAOF` for non-optimisable filters** — now returns
  `AE_INVALID_EXPRESSION` instead of success, so stock rddads
  falls back to client-side filtering. The old behaviour silently
  disabled `SET FILTER` entirely. (Reported by Jonsson / RusSoft
  Ltda.)

### ODBC Driver

- **x86 build** — `C4100` (unused parameter) and `C2733`
  (`SQLLEN*` vs `SQLPOINTER` signature mismatch) fixed for 32-bit
  MSVC `/WX`. (PR #119)
- **DM/ADO conformance** — descriptor handles, `SQLBindCol`, and
  `BIT` type mapping fixed.
- **`SQL_DRIVER_ODBC_VER` / `SQL_ODBC_VER`** — now reported in
  `SQLGetInfo`.

### DA-Web Security

- AOF filter expression sanitisation.
- Drive-root path containment fix.
- API security sweep across all PHP endpoints.
- RI meta index paths contained under DD directory.
- Wire frame size caps to prevent abuse.

### Platform / Build

- **macOS** — sign-conversion and unused-function warnings fixed.
- **GCC** — `-Werror` warnings resolved (shadow, implicit
  conversion, format-truncation, stringop-truncation).
- **MSVC** — x86 `__stdcall` decoration mismatch and x64
  `_wfsopen` crash fixed.
- **Clang** — `-Wc2y-extensions` guard for older Apple Clang.
- **POSIX fd-0 collision** — file handles are now stored as
  `(fd + 1)` so a real fd 0 (returned by `open()` when stdin is
  closed) is no longer mistaken for the "not open" sentinel.
- **POSIX `EINTR` retry** — `pread` / `pwrite` retry on signal
  interruption instead of failing the I/O.
- **POSIX zero-length mmap** — `map_readonly` rejects zero-length
  maps instead of calling `mmap` with length 0.

### CDX

- Shared header lock on open so concurrent appends don't fail.
- Structural CDX named per-table, not per-directory session.
- **Stale record-count refresh on the fetch path** — a driver
  caches the DBF record count at open; in a multiuser deployment a
  peer append could leave the cache lagging, so an index walk that
  reached a just-appended row (e.g. mid-`REPLACE … FOR`) failed
  hard with spurious error 5000. `read_record_raw` /
  `write_record_raw` now re-read the on-disk count under a shared
  header lock before declaring a recno out of range (slow path
  only — a normal forward scan pays nothing).

### Remote (Wire)

- Use-after-free crash on virtual table queries via TCP
  eliminated.

---

## Documentation

- **CONTRIBUTING.md** — new contribution guide with PR workflow,
  protocol policy, and clean-room rules.
- **Wire Protocol DD API** — comprehensive Data Dictionary API
  reference (§9) added.
- **DA-Web GUIDE.md** — full user guide for the DA-Web browser
  interface.
- **README** — comprehensive post-rc29 status update covering
  ADI write path, ADT creation, AES mode, and DA-Web scope.
- **Cookbook** — new `cookbook/` folder with runnable,
  heavily-commented Harbour examples (simple → advanced). The
  `console/` track is pure xBase: create / index seek /
  transactions / DBF maintenance (`ADSCDX`), SQL via
  `AdsCreateSQLStatement` + `AdsExecuteSQLDirect`, native ADT
  (`ADSADT` + `.adi`), and a `tcp://` remote client. The `orm/`
  track runs the same CRUD through a companion Harbour ORM across
  SQLite / DBF / PostgreSQL / MariaDB / ODBC back-ends, capped by an
  all-back-ends benchmark (`orm/complete/`) with a cross-back-end
  content checksum and a seek-vs-scan headline. A FiveWin
  `xbrowse` CRUD sample and connection-string / field-type /
  troubleshooting guides round it out.
- **API Reference (PT)** — all 364 ACE functions documented in
  Portuguese with syntax, parameters, return values, and examples.

---

## Packaging

- **Windows Inno Setup installer** (`openads-setup.iss`).
- **CPack packages** with guaranteed `openace32.lib` /
  `openace64.lib` in Windows archives.
- **Release CI** — `openace{32,64}.lib` shipped in release
  archives automatically.

---

## Testing

- **874 unit tests** passing on x64 and x86 (361 300+
  assertions).
- New test files: `abi_cdx_tag_order_test.cpp` (CDX tag creation
  order), `abi_cdx_expr_index_scale_test.cpp` (expression-index
  key size at scale), `abi_multitag_order_nav_test.cpp`
  (multi-tag navigation), `abi_ntx_numeric_edge_test.cpp` (NTX
  numeric edge cases), `cdx_empty_tree_test.cpp`, and more.
- **x86 CI** — `msvc-x86` build leg catches 32-bit breakage on
  every PR.
- Harbour demo in `examples/adt-native/` (by glokcode).

---

## Contributors

- **Jonsson / RusSoft Ltda.** — CDX tag order, expression-index
  key size, `AdsSetAOF` non-optimisable filter, and x86 calling
  convention fixes.
- **Admnwk** — ODBC driver, SQL push-down, aggregation, FetchWhere
  V2, native write path, and CI improvements.
