---
title: Release Notes
layout: default
parent: Home (EN)
nav_order: 1
permalink: /en/release-notes/
---

# OpenADS Release Notes

Complete history of releases with categorized improvements.

---

## v1.8.95 — 2026-08-20

### New Features

- **Client settings in `openads.ini`** (Pritpal Bedi) — every
  `OPENADS_*` client setting now also reads from `openads.ini` (env
  var wins): `remote_only_access`, `resolve_verbose`, `log`,
  `log_file`, `trace`, `wire_trace`, `arc_trace`, `tls_insecure`,
  `adi_v2`, `adt_cdx_index`. The client DLL looks for the file next
  to itself, then in the app working directory; `OPENADS_INI` points
  at an explicit path. The file is re-read when it changes. See
  `openads.ini.sample`.

---

## v1.8.94 — 2026-08-20

### New Features

- **`OPENADS_REMOTE_ONLY_ACCESS=1` client guard** (Pritpal Bedi) —
  remote-only deployments can now forbid legacy local-path access
  through ACE. With the env var set on the client process, a local
  `AdsOpenTable` / `AdsCreateTable` fails with `AE_ACCESS_DENIED`
  (rddads raises a Harbour runtime error) instead of silently
  reading/writing a `.dbf` next to the app. Remote and SQL-backend
  connections are unaffected, as is local I/O through another RDD.
  See [Migrating from ADS](migrating-from-ads/).

---

## v1.8.93 — 2026-08-19

### New Features

- **`OAds_Mutex*` Harbour wrappers** — the server-side distributed
  mutex service (v1.8.92, remote connections only) is now callable
  from Harbour PRG code without RDDADS via `contrib/oads_hb/oads_hb.c`:
  `OAds_MutexCreate( [hConn], cName )`,
  `OAds_MutexLock( [hConn], cName, nTimeoutMs )`,
  `OAds_MutexTryLock( [hConn], cName )`,
  `OAds_MutexUnlock( [hConn], cName )`,
  `OAds_MutexDestroy( [hConn], cName )`. Omitting `hConn` uses the
  default connection set with `OAds_SetConnection()` (Pritpal Bedi).

### Documentation

- **API reference** — new section 26 "Distributed Mutex (server-side,
  remote only)" with the Harbour signatures; `last_updated` timestamp
  added to the front matter.

---

## v1.8.89 — 2026-08-17

### New Features

- **Process-wide audit sequence** — each `RESOLVED` line is now
  `CONN ENTRY SEQ TIMESTAMP …`. `ENTRY` still restarts per connection;
  `SEQ` is a single incrementing number for the process so two
  connections no longer look like a repeated counter (Pritpal Bedi).

---

## v1.8.88 — 2026-08-17

### Bug Fixes

- **`oads_hb.c` includes `"ace.h"`** — Harbour drop-in glue now uses
  the same header as `contrib/rddads`, so it compiles in a consumer
  project without editing the file each time (Pritpal Bedi).

### Packaging

- **Windows ZIP names** — only `openads-<ver>-windows-x64.zip` /
  `windows-x86.zip`. The parallel `*-win-*.zip` slim kit is no longer
  published (it was a different `ace64.dll` and confused downloads).

---

## v1.8.87 — 2026-08-17

### New Features

- **Resolve audit trail** — every table-path resolve is a fixed-width
  line (`CONN6 ENTRY8 TIMESTAMP RESOLVED=... asked=... via=local|remote`).
  Console default is only `RESOLVED`. Set `OPENADS_RESOLVE_VERBOSE=1` for
  detail lines; `OPENADS_LOG_FILE` stores only `RESOLVED` (Pritpal Bedi).

### Bug Fixes

- **Remote is safe storage** — a remote-server connection no longer
  opens a leftover host-absolute path. `--legacy-paths` remounts under
  `--data`; otherwise the open fails with the normal RDD error
  (Pritpal Bedi).

---

## v1.8.79 — 2026-08-15

### New Features

- **Connection switching + optional hConn for OAds_F*** — `OAds_SetConnection( hConn )` sets the thread-local default connection; `OAds_GetConnection()` retrieves it. All `OAds_F*` Harbour functions (`OAds_FOpen`, `OAds_FCreate`, `OAds_CheckExistence`, `OAds_DeleteFile`, `OAds_RenameFile`, `OAds_GetFileSize`, `OAds_GetFileDate`, `OAds_GetFileTime`, `OAds_DirMake`, `OAds_DirRemove`, `OAds_DirExist`, `OAds_Directory`, `OAds_FExist`) now accept hConn as optional. When omitted, the thread-local default is used. Backward compatible — existing code passing hConn continues to work.

  Use case: single-connection apps set the default once; multi-connection apps switch via `OAds_SetConnection(hConn)` before each call — handy for transparent backup across two endpoints.

### Bug Fixes

- **macOS build** — `arc2_stamp()` now uses `localtime_r` on POSIX (was Windows-only `localtime_s`); trace log uses `/tmp/ace_calls.log` on POSIX. `AE_NO_MATCHING_FILE` (5059) added to the error enum.

---

## v1.8.78 — 2026-08-15

### New Features

- **Multi-port server** — A single `openads_serverd` instance can now listen on multiple TCP ports, each with its own data directory. Configure additional ports with `[port:NNNN]` sections in `openads.ini`, each specifying a `data` path. Matches LetoDB's `Port=` / `DataPath=` capability: one process, one config file, multiple logical servers.

### Bug Fixes

- **CDX index corruption on record update (macOS)** — `sync_all_indexes_()` silently discarded the B-tree erase result when updating a record, so the old index entry stayed in the tree and the new entry was inserted on top — creating duplicate/stale keys that corrupted B-tree separators. The erase error is now propagated so the caller can abort before inserting. For fresh appends, the blank-key snapshot (fields not yet set) is tolerated because those keys were never inserted. (Commit `df9c2cc`.)

### Tests

- `abi_alternating_append_test`: 4 cases — alternating local/remote appends maintain all CDX tag keys.
- `abi_adt_scope_validation_test`: M5 stress append + index order + memo round-trip.
- Full unit suite: 524/524 on Windows, same on macOS.

---

## v1.8.77 — 2026-08-14

### Bug Fixes

- **Remote `ordListClear()` + `ordListAdd()` left the workarea without an order** — The client-side remote `AdsCloseAllIndexes` closed the indexes on the server but never invalidated the cached tag/handle map in the ACE client DLL. The next `AdsOpenIndex` / `SET ORDER TO` reused dead server wire-ids (`SetOrder` 5000 on run 2) or found no order at all (ADSCDX/301 "Workarea not indexed" on `dbSeek` on run 1). The client cache is now dropped on close-all. (Reported by Pritpal Bedi — Harbour 32-bit `TestIndexes()`.)
- **`OrdCount()` stale after `OrdCreate()` without reopen** — The remote create path never registered the new tag in the client's index handle list, so `AdsGetNumIndexes` / `AdsGetIndexHandle` lagged until close+reopen. After a successful `create_index` the client now refreshes the whole bag from the server (ADS semantics: creating a tag opens every tag in the bag), in bag order. (Reported by Pritpal Bedi.)
- **x86 `openads_serverd` crashed at startup** — `dll_probe_ace` called `AdsGetVersion` through a mismatched calling convention whenever an `ace32.dll`/`openace32.dll` sat next to the exe. The probe now identifies OpenADS builds via the OpenADS-only `oads_*` exports before calling anything, and calls `__cdecl`. (Reported by Pritpal Bedi: "Server does not launch".)
- **x86 undecorated exports restored to the `__cdecl` implementations (v1.8.74 behavior)** — The stdcall rework had aliased the plain `AdsXxx` export names to the `__stdcall` wrappers, so `__cdecl` callers (FWH apps such as `B_BIG.exe`, `GetProcAddress` probes, Xbase++) crashed with `ACCESS_VIOLATION` on the first ACE call (`ADS_GETFUNCTABLE`). The plain names now target the `__cdecl` implementations again; `_AdsXxx@N` remains on the `__stdcall` wrappers. (Reported by Pritpal Bedi.)
- **Release CI unblocked** — C4100 in `AdsFindNextTable`/`AdsFindClose` (MSVC `/WX`), UUID buffer worst-case `%llx` sizing (clang `-Wformat-truncation`), and sign-conversion casts in `session.cpp` transaction dispatch (clang `-Wsign-conversion`).

### Tests

- `abi_remote_index_reopen_test`: 2 cases — remote `ordListClear` + `ordListAdd` leaves a usable order (created bag and auto-opened bag variants).
- Fixed iterator-of-two-temporaries UB in `abi_server_fs_test` / `abi_remote_index_reopen_test` (flaky `vector too long`).
- Import libraries under `dist/import-libs` regenerated from the current DLLs.

---

## v1.8.75 — 2026-08-13

### New Features

- **ADS-compatible replication (Phase 1)** — One-way async replication between OpenADS servers via publications, articles, and subscriptions. The DataDict now supports `Publication`, `Article`, and `Subscription` object types with full CRUD and round-trip persistence. A durable CRC-32C queue (`ReplQueue`) captures INSERT/UPDATE/DELETE mutations through best-effort capture hooks, and `repl_apply_once()` drains the queue to apply changes to a local target table. 35 unit tests (464 assertions).
- **M12.33 remote FindTables** — `AdsOpenTable` and the server `OpenTable` dispatcher now strip `tcp://host:port/` URI prefixes that legacy Delphi TAdsTable components embed in table names.

### Tests

- `repl_queue_test`: 10 cases — append, read_from, CRC corruption, empty file
- `repl_catalog_test`: 8 cases — DD round-trip, table_is_published, cascade, rejects
- `abi_repl_capture_test`: 1 case — capture hooks fire on INSERT/UPDATE/DELETE
- `repl_apply_test`: 23 cases — subscription resolution, INSERT/UPDATE/DELETE apply, mixed batches, idempotency, last_lsn persistence, error paths

---

## v1.8.74 — 2026-08-12

### Bug Fixes

- **INDEX ON bag remounted through table's connection** — `AdsCreateIndex61` / `AdsCreateIndex` / `AdsOpenIndex` now remount the index bag through the table's connection the same way as the `.dbf`. Fixes `INDEX ON` writing `.z01` next to the app while the `.dbf` remounted under `--legacy-paths`.
- **Explicit local Connection handle no longer hijacked** — An explicit local `Connection` handle is no longer hijacked by "any live `RemoteConnection`" (in-process server ABI twin + mixed local/remote apps).

### Tests

- `AdsCreateIndex61 remounts client-absolute .z01 under legacy_paths`
- `remote INDEX ON remounts client-absolute .z01 under legacy-paths jail`

---

## v1.8.73 — 2026-08-12

### Bug Fixes

- **`--legacy-paths` now remounts every client-absolute path under `--data`** — Previously, host files that already existed outside `--data` could still be opened. Every client-absolute open/create now remounts under `--data` first; the host-absolute OPEN/CREATE exceptions apply only in strict mode.

### Documentation

- Documented the `--data` / URI / `--legacy-paths` contract (forward slashes recommended) in `cookbook/docs/local-and-remote.md` and `connection-strings.md`.

### Tests

- `session_connection_test`: host-absolute file outside the jail must not win when `legacy_paths` is on.
- `abi_mt_create_vs_dbfcdx_test`: Harbour helpers are Windows-only so clang `-Werror` (Linux/macOS) does not fail on unused stubs.

---

## v1.8.72 — 2026-08-11

### Performance

- **ADSCDX multi-thread create ~30x faster than Harbour DBFCDX** — Measured on 40 tables / 4 threads, 10 rows + 3-tag CDX each:
  - Harbour DBFCDX: ~4 s (baseline)
  - OpenADS ADSCDX: ~0.13 s (~30x faster)
  - Harbour ADSCDX (rddads): ~0.15 s (~27x faster)
- **No per-op FlushFileBuffers** — Default is page-write only; set `OPENADS_FSYNC=1` to restore the old power-fail-safe path.
- **AdsCreateIndex61 no longer holds state().mu across bulk build** — Only index_bindings_mu around map mutations, so multi-thread INDEX ON scales.
- **CDX write-lock wait uses a condition variable** — Not 100 us poll; always releases the batch join on flush error paths.

### Tests

- `abi_mt_create_vs_dbfcdx_test.cpp` — every ADSCDX speed run paired with a Harbour DBFCDX baseline.
- Build harness: `tests/unit/build_mt_create_bench.bat`.

---

## v1.8.68 — 2026-08-09

### Bug Fixes

- **clang `-Werror` build breaks in new test files** — Two unused-code warnings (lambda capture, helper functions) failed macOS/Linux CI builds. No product code changes beyond the warning fixes; supersedes v1.8.67 on POSIX.

---

## v1.8.67 — 2026-08-09

### Bug Fixes

- **CDX B+tree Harbour-exact (key, recno) ordering** — Insert descent now compares full (key, recno) pairs. Separators are refreshed on every max-pair change, on insert and on erase.
- **Two multithreading defects fixed** — CDX write-lock batch now has an owner thread; remote `AdsOpenIndex` handle array overflow fixed.
- **DBF headers now byte-identical with Harbour DBFCDX** — Header length `32 + 32*n + 2` with 2-byte terminator. Production-index flag set when CDX bag shares table's basename.

### Tests

- `cdx_dup_key_split_test`: independent raw-page decoder asserting parent-separator == child-max.
- `abi_alternating_append_test`: two local connections and a remote server + local client alternating duplicate-key bursts.

---

## v1.8.66 — 2026-08-08

### Bug Fixes

- **Multi-tag CDX bags: creation and binding** — Legacy `AdsCreateIndex` no longer truncates the bag when adding a second tag. `AdsOpenIndex` now binds all tags regardless of handle array size.
- **DBF record-count race under concurrent mixed writers** — `refresh_record_count_` now takes the max of header and size-derived counts.

---

## v1.8.65 — 2026-08-07

### Bug Fixes (POSIX)

- **SIGPIPE guard** — `sock_send()` with `MSG_NOSIGNAL` on Linux; `SO_NOSIGPIPE` on macOS.
- **ADT byte-lock offset wrap** — Offsets ≥ 2^63 folded into positive range.
- **CDX write-lock registry per-file mutex** — Each `.cdx` path serialises on its own entry mutex.

---

## v1.8.64 — 2026-08-07

### Bug Fixes

- **Harbour DBFCDX interop: shared-index visibility** — Peer CDX updates now detected via file-header version counter.
- **Physical write guard (GoHot)** — Shared-mode writes require RLock / FLock / exclusive.
- **Index corruption under mixed ADS/DBFCDX writers** — Lock offsets aligned with Harbour DBFCDX VFP scheme.

---

## v1.8.63 — 2026-08-07

### Bug Fixes

- **Remote `keyno` stale after APPEND/WRITE/DELETE/RECALL** — `keyno_valid` and prefetch/BOF-EOF flags now cleared.

---

## v1.8.62 — 2026-08-07

### Performance

- **Server-side `GetKeyNum` via CDX O(1) cache** — New wire opcodes `GetKeyNum` / `GetKeyNumAck` (0x03/0x04).
- **Same-record `GotoRecord` preserves keyno** — `TXBrowse:Refresh()` after remote APPEND stays O(1).

---

## v1.8.61 — 2026-08-07

### Bug Fixes

- **CDX tag-header update counter endianness** — Now big-endian to match Harbour DBFCDX.

---

## v1.8.60 — 2026-08-06

### Bug Fixes

- **CDX page allocator tail reset after recreate** — Prevents ~10x sparse-file bloat.

---

## v1.8.59 — 2026-08-05

### Performance

- **Legacy ADI `CREATE INDEX` bottom-up bulk pack** — Index ~49% smaller (73,216 → 37,376 bytes for 10,000 keys).

---

## v1.8.58 — 2026-08-05

### Documentation

- **Error log vs transaction journal paths** — Documented `ads_err.dbf` location and relocation options.

---

## v1.8.57 — 2026-08-05

### New Features

- **ADI v2 tags** — Compound/computed/FOR on ADT tables.
- **`AdsGetKeyCount` memoise** — Performance improvement.
- **ADT read-ahead** — Prefetch optimization.
- **PACK truncate** — Efficient table compaction.

---

## v1.8.56 — 2026-08-05

### Bug Fixes

- **CDX numeric leaves type-dependent trail byte** — NUL for FoxNumeric, matching DBFCDX density.

---

## v1.8.55 — 2026-08-04

### New Features

- **SAP date display format** — `AdsGetField` formatted vs `AdsGetString` raw.
- **Join/union/aggregate temps are ADT** — Temporary tables use ADT format.

---

## v1.8.54 — 2026-08-04

### Bug Fixes

- **Connect no longer fails when data root is not writable** — e.g. `--data "C:\"`.

---

## v1.8.53 — 2026-08-03

### Bug Fixes

- **`legacy_paths` remote create/reindex** — Fixed un-flagged ABI twin usage.
- **Drive-letter routing for whole-filesystem servers** — `--data "C:\;D:\;E:\"`.

---

## v1.8.52 — 2026-08-03

### New Features

- **`legacy_paths` server mode** — Zero-change ERP ports with absolute path remapping.

---

## v1.8.51 — 2026-08-02

### Bug Fixes

- **x86 `ace32.dll` exports cdecl aliases** — For MinGW rddads compatibility.

---

## v1.8.50 — 2026-08-01

### Bug Fixes

- **Empty AOF visible-set returns Limbo** — `AdsSeekLast` positions correctly.
- **AOF index selection** — Fixed.
- **ADI keycount fix** — Fixed.

---

## v1.8.49 — 2026-08-01

### Bug Fixes

- **Write coalescing broke rollback/CDX/AFTER triggers** — Fixed.
- **Remote ADT numeric round-trip encoding** — Fixed.

---

## v1.8.48 — 2026-08-01

### Bug Fixes

- **32-bit toolchain: `ace.h` `__stdcall` on MSVC x86** — All in-tree tools link against exported stdcall names.

---

## v1.8.47 — 2026-08-01

### Bug Fixes

- **Remote `AtBOF`/`AtEOF` answered from wrong cursor** — Fixed.
- **Row trailer at BOF fixed phantom duplicate row** — Fixed.

---

## v1.8.46 — 2026-08-01

### Bug Fixes

- **`DbSetOrder(0)` restores natural order** — Fixed.
- **ace32.dll exports stdcall `@N` names** — Fixed.
- **ADSHANDLE 32-bit** — Fixed.

---

## v1.8.45 — 2026-07-31

### Bug Fixes

- **Remote `KeyNo`/`RelKeyPos` clamps to scope-aware key count** — Eliminates phantom rows in xBrowse.

---

*For the full commit-by-commit history see the [CHANGELOG](https://github.com/FiveTechSoft/OpenADS/blob/main/CHANGELOG.md).*
