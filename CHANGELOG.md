# Changelog

All notable changes to OpenADS are recorded here. The project follows
[Semantic Versioning](https://semver.org/) once 1.0 ships; until then
0.x.y releases may break the C ABI between minor versions to track
the real ACE SDK.

## 1.2.0 — 2026-06-24

- **Deferred-flush bulk-insert mode (528× speedup).** A new
  `AdsSetDeferredFlush(hTable, 1)` API puts the table into
  deferred-flush mode: `AdsWriteRecord` writes the record to OS
  cache but skips the per-record `FlushFileBuffers` call. Data is
  flushed to physical media only when `AdsFlushFileBuffers` is
  called explicitly (or on table close). 500K records + CDX index
  build completes in ~26 seconds (19s bulk insert at 26,381 rec/s +
  7.2s CDX build + 36ms final flush) vs. ~2.7 hours before
  (50 rec/s). 649/649 unit tests pass; backward-compatible —
  default behaviour is unchanged (flush on every write).

- **MSSQL native TDS 7.4 backend (PR #53 integration).** Native
  SQL Server connectivity via the TDS 7.4 wire protocol with
  optional mbedTLS encryption. Supports connect, authentication
  (SQL/Windows), table open, field read, and navigation. URI
  scheme: `mssql://user:pass@host:port/database`. Enabled via
  `OPENADS_WITH_MSSQL=ON` CMake option (requires
  `OPENADS_WITH_TLS=ON`). 649/649 unit tests pass.

## 1.1.0 — 2026-06-23

- **SQL backends: PostgreSQL / MariaDB / ODBC behind a pluggable
  backend-ops registry (PR #31).** OpenADS can now open tables on
  PostgreSQL, MariaDB / MySQL and any ODBC-reachable engine behind
  the ACE ABI, selected by the connection URI (`postgresql://` /
  `mariadb://` / `odbc://`) exactly like the SQLite backend.
  Navigation, field read and column SEEK work; write is
  per-backend. The four SQL backends register one `BackendTableOps`
  struct each (17 function pointers), so the ~17 ABI navigation /
  field functions stay backend-agnostic instead of multiplying a
  per-backend `if` block — adding a further backend is one ops
  struct plus one registration line. Identifiers are validated to
  safe ASCII and SEEK values use prepared-statement parameters. The
  native local DBF / ADT and `tcp://` remote paths are unchanged
  fall-throughs. See `docs/OPENADS_PLUS.md`. Verified: full unit
  suite 572/572; PostgreSQL/MariaDB/ODBC e2e (41/45/59 assertions)
  against live servers on the contributor's side.

## 1.0.4 — 2026-06-23

- **CDX stale record-count refresh on the fetch path (PR #50).** A
  `CdxDriver` caches the DBF record count at `open()`. In a
  multiuser deployment a peer connection can append rows afterward,
  leaving that cache lagging; an index walk that reached a
  just-appended recno (e.g. mid-`REPLACE … FOR` / DBEVAL) then
  failed hard with a spurious ADSCDX error 5000. `read_record_raw` /
  `write_record_raw` now re-read the on-disk count under a shared
  header lock before declaring a recno out of range, with an
  unlocked-refresh fallback. Slow path only — a normal forward scan
  never reads past the count, so the single-writer case pays
  nothing.

## 1.0.3 — 2026-06-23

- **Round-trip-thrifty remote scan (PR #47).** A forward scan over
  the `tcp://` wire no longer costs ~one TCP round-trip per record.
  A sequential-prefetch path — negotiated via a Connect capability
  flag — piggybacks a lookahead block onto forward-`Skip` acks; the
  client serves them locally and folds the consumed count back into
  the next wire step so the server cursor never desyncs. `AdsAtEOF` /
  `AdsAtBOF` are answered from the cached current row and `AdsIsFound`
  from a cached `Found()` flag. A 50k-record loopback scan is ~3.9×
  faster (NAV-only) / ~3.3× (3-field read), `IsFound` round-trips
  drop to zero. Additive and backward-compatible: clients that don't
  advertise the capability keep the previous wire behaviour.
- **Cookbook expansion (PR #46).** New `console/` examples (SQL via
  `AdsExecuteSQLDirect`, native ADT with `ADSADT` + `.adi`, a
  `tcp://` remote client), a FiveWin `xbrowse` CRUD sample, and an
  all-back-ends ORM benchmark (`orm/complete/`) with a cross-back-end
  content checksum and a seek-vs-scan headline.

## 1.0.2 — 2026-06-23

- **Responsive Studio web console.** The Studio SPA
  (`tools/serverd/spa_index.h`) now adapts to phones and tablets:
  the table-list sidebar collapses into a slide-in drawer (☰ in the
  header, dimmed backdrop, auto-close on select) below ~768 px;
  tabs scroll horizontally; on phones forms stack to one column,
  modals fit the viewport width, and touch targets are enlarged.
  Also fixes a pre-existing dark-theme bug where `--panel` /
  `--panel-2` / `--border` were self-referential CSS variables, so
  panels and borders rendered transparent.

## 1.0.1 — 2026-06-23

- **`SKIP` honours `SET DELETED ON` in natural order.** `Table::skip`
  on an unindexed table stepped straight onto deleted rows; it now
  skips deleted records (matching the index-order path and
  `GOTO TOP` / `GOTO BOTTOM`) when `SET DELETED` is ON. Fixes
  `abi_deleted_records_test` ("middle records deleted: Skip sees only
  live rows"), which had been failing the test step on every CI
  platform.
- **Native ADT / ADI create, read, write, and index seek (PR #41).**
  OpenADS now operates end-to-end on native `.adt` / `.adi` / `.adm`
  files: `AdsCreateTable(ADS_ADT)` writes a valid header + field
  descriptors (+ optional `.adm` memo store), `AdsAppendRecord` /
  `AdsWriteRecord` persist rows and memo payloads, re-open + field get
  + memo round-trip on read, `AdsCreateIndex61` builds `.adi` bags, and
  `AdsSeek` works on character and numeric ADI keys. AUTOINC counter is
  seeded from existing rows at open.
- **POSIX platform hardening.** `file_posix` stores handles as `(fd+1)`
  so a real fd 0 (stdin closed) is not mistaken for the not-open
  sentinel; `pread` / `pwrite` retry on `EINTR`; `map_readonly` rejects
  zero-length maps; `LockMgr` refcounts repeated locks and releases the
  OS lock only on the final unlock; `TxLog::read_all` bounds-checks
  every UPDATE / APPEND field length against truncated / corrupt WAL.
- **macOS / clang build fixes.** Resolved `-Werror` breaks introduced by
  the ADT/ADI work: sign-conversion in `adi_index.cpp` and the
  `environ` / dangling-pointer issues in the ADT scope-validation test.
- **Documentation.** New SQLite backend guide (`sqlite://` connection
  URI, `?key=` encryption, field-type mapping, limitations) and stored
  procedures guide (custom AEP `CREATE`/`EXECUTE PROCEDURE` + the
  built-in `sp_*` Data Dictionary procedures), all in EN / ES / PT.
- **Cookbook (PR #44).** New `cookbook/` folder with runnable,
  heavily-commented Harbour examples — a `console/` track (pure
  `ADSCDX` xBase) and an `orm/` track (CRUD across SQLite / DBF /
  PostgreSQL / MariaDB / ODBC back-ends), plus connection-string,
  field-type and troubleshooting guides.

## 1.0.0-rc29 — 2026-05-26

- **Turnkey `hbmk2` (`.hbp`) example for Harbour apps —
  `examples/harbour-hbmk2/`.** Reported on the FiveTech forum:
  *"alguna alma caritativa que proporcione un archivo de
  compilación `.hbp` para crear un programa con OpenADS — todos
  mis intentos han fracasado"*. The repo now ships a complete
  `hbmk2` project: `openads_demo.hbp` (x64), `openads_demo_x86.hbp`
  (32-bit), `openads_demo.prg` (console app exercising
  `AdsConnect` → `DbCreate` → `INDEX ON UPPER(NAME)` → `dbSeek`),
  Windows `build.cmd` and POSIX `build.sh` wrappers. Drop in your
  `.prg`, point `OPENADS_LIB` at OpenADS' build output, run
  `hbmk2`. The `.hbp` is intentionally minimal — only the two
  link entries that change for OpenADS (`-lrddads` plus
  `-L${OPENADS_LIB} -lace64`).
- **Docs walkthrough — en / es / pt.** New "Build your own
  Harbour app against OpenADS (`hbmk2` / `.hbp`)" section in
  `docs/{en,es,pt}/getting-started.md` and the matching README,
  including a troubleshooting table for the typical *"unresolved
  external symbol `AdsConnect60`"* / *"`rddads.lib` not found"* /
  *"loaded the wrong `ace64.dll`"* pitfalls so a first-time user
  can self-diagnose without filing an issue.

## 1.0.0-rc28 — 2026-05-22

- **ADT / ADM support (M4 ADT).** OpenADS now opens `.adt` tables
  produced by SAP Advantage and writes records back; `.adm` memo
  stores auto-attach when the table carries Memo / Binary fields.
  Full 13-type field vocabulary (CHAR, CICHAR, LOGICAL, DATE,
  DOUBLE, INTEGER, SHORTINT, MEMO, BINARY, TIME, TIMESTAMP,
  AUTOINC, MONEY). ADM uses 256-byte fixed blocks; the 9-byte
  in-record reference is resolved transparently by the engine.
  `AdsCreateTable(ADS_ADT)` still produces a DBF (ADT creation
  deferred); ADI index files not yet implemented; SAP proprietary
  ADT encryption not yet supported. Verified against
  `f:\pmsys\data\landlords.adt` via `tests/unit/abi_adt_smoke_test.cpp`
  (skipped on machines without the fixture).
- **CI** — macOS leg switched to a single universal (arm64 +
  x86_64) binary instead of separate Intel / Apple-Silicon legs.
- **Harbour patch** — restored the blank context line in the
  `rddads.h` hunk of `tools/harbour_patch/rddads-compat.patch`
  so `git apply` succeeds on a pristine Harbour tree.

## 1.0.0-rc27 — 2026-05-17

- **`AdsGetField` pads CHARACTER fields to the declared width.**
  Reported by Pritpal Bedi: a Harbour `mini_xbrowse /ads` (ADSCDX
  → OpenADS) showed every text column truncated — `Charlie` as
  `Charl`, `Barcelona` as `Barcel` — while the native DBFCDX run
  rendered them full. Root cause: `AdsGetField` returned CHARACTER
  values with trailing spaces stripped (`make_string` in
  `dbf_common.cpp` rtrims, and `decode_field` used it for the
  Character branch). DBF/xbase CHAR fields are fixed-width
  space-padded; `FieldGet` of a `C(20)` field must return 20
  characters. With the trimmed value, xbrowse auto-sized each
  column to the current row's value length and clipped every
  other row. `AdsGetField` now re-pads CHARACTER values to the
  field's declared width on the way out — both the local and the
  remote (wire) read paths. The engine's internal decode is left
  trimming, so SQL comparisons, index keys and AOF filters are
  untouched; verified by `tests/smoke/harbour/fieldlenprobe.prg`
  (ADSCDX now matches the DBFCDX baseline) and `idxprobe.prg`
  (index walk still `SORTED=YES`, full suite 397/397).
- **`tools/harbour_patch/rddads-compat.patch` applies again.**
  Reported by Pritpal Bedi: `git apply` rejected the patch with
  `patch failed: contrib/rddads/rddads.h:67`. A prior edit had
  dropped the blank context line after `#include "ace.h"`, so the
  hunk carried five context lines while its header still declared
  six. The missing line is restored; verified `git apply` applies
  cleanly to a pristine Harbour `contrib/rddads` tree.
- **PHP binding — result-fetch and index seek.** `bindings/php`
  gains `Cursor::fetchAssoc()` / `fetchNum()` (single-row fetch,
  `null` past the last row) and `Table::seek()` (index key seek
  via `AdsGetIndexHandle` + `AdsSeek`). 37 PHPUnit tests.
- **CI** — the release workflow gains a macOS Intel (x64) build
  leg, so releases ship a `macos-x64` archive alongside
  `macos-arm64`.

## 1.0.0-rc26 — 2026-05-16

- **PHP binding — `bindings/php`.** Reinaldo Crespo asked whether a
  modern PHP extension for ACE existed: the proprietary Advantage
  PHP extension stopped working around PHP 5.2 and was never
  modernised. OpenADS now ships its own.

  - **`openads/openads-php`** — a pure-PHP Composer package, no
    compiled C. It loads `ace64.dll` / `ace32.dll` / `libace*.so`
    through PHP's `ext-ffi` and wraps it in a modern namespaced
    OOP API: `Connection`, `Statement`, `Cursor` (a `\Iterator`
    over result sets), `Table`, `Record`. Requires PHP 8.1+.
  - **Local and remote in one path.** A `Connection` takes a
    local data-directory path or a `tcp://` / `tls://` URI;
    `AdsConnect60` dispatches on the URI, so the binding has no
    mode branching.
  - **Parameterised SQL.** `Statement::query()` accepts `?`
    positional or `:name` named parameters. OpenADS ACE has no
    host-variable binding, so `ParameterBinder` substitutes
    values client-side with per-type quoting (single-pass, so a
    value containing a `:token` substring cannot corrupt the
    statement) — the anti-injection boundary, with its own unit
    tests.
  - Pinned by 31 PHPUnit tests (21 unit + 10 integration against
    a live engine) plus a CI leg that builds the ACE library and
    runs the suite. Design / plan under
    `docs/superpowers/{specs,plans}/2026-05-16-php-bindings*`.
- **SQL `''` string-escape fix.** `read_string_literal` in the SQL
  parser scanned to the next `'` with no escape handling, so the
  ANSI-standard doubled-quote escape (`'O''Brien'`) parsed as the
  string `O` followed by a stray token — error 7200. Any SQL
  client inserting a string containing an apostrophe failed. The
  parser now decodes `''` to a single `'`; the unterminated-literal
  error path is unchanged. Pinned by a new `sql_parser_test` case.

## 1.0.0-rc25 — 2026-05-16

- **Index correctness sweep.** Three bugs that broke CDX/NTX
  ordered access from Harbour `rddads` and X#'s `ADSRDD`:

  - **`AdsCreateIndex61` decoded the wrong option bit.** `ace.h`
    sets `ADS_UNIQUE 0x01`, `ADS_DESCENDING 0x02`, `ADS_CUSTOM
    0x04`, `ADS_COMPOUND 0x08`. The descending flag was read as
    `ulOptions & 0x08` — that bit is `ADS_COMPOUND`, which both
    `rddads` and `ADSRDD` set for every CDX/NTX tag. Every order
    built descending: `AdsGotoTop` landed on the last key and
    `SKIP` walked backward. A stale comment and the
    `abi_create_index61` test had the bit values swapped the same
    way, so the bug was self-consistent and hidden. Now decoded
    with the named `ace.h` constants; `AdsCreateIndex90` delegates
    to 61 and is covered too.
  - **`ALIAS->FIELD` qualifiers in index expressions.** Harbour
    `INDEX ON CUST->NAME` passes the literal text `"CUST->NAME"`
    to the RDD. `evaluate_index_expr` could not parse it — the
    tokenizer dropped `-` and `>`, so the alias parsed as an
    unknown identifier and every key evaluated blank, degenerating
    the index to record order. `strip_alias_qualifiers()` now
    removes any `<ident>->` qualifier (bare and nested, e.g.
    `UPPER(CUST->NAME)`) before evaluation.
  - **`AE_NO_CURRENT_RECORD` (5026) for not-positioned reads.**
    Reported by Pritpal Bedi: a Harbour `TBrowse` over an `ADSCDX`
    table failed mid-paint with `ADSCDX/5000 table not positioned
    on a record`. `Table::read_field` returned the generic 5000
    (`AE_INTERNAL_ERROR`) for not-positioned reads; `rddads`
    special-cases 5026 as the graceful read-past-EOF path and
    raises every other code as a hard error. `table.cpp` now
    returns the SAP-canonical 5026.

  Verified: `idxprobe.prg` index walk matches the DBFCDX baseline,
  `posprobe.prg` goes from 6 `ADSCDX/5000` raises to 0, full suite
  395/395.

## 1.0.0-rc24 — 2026-05-16

- **`AdsMg*` server-telemetry subsystem.** Reported by Pritpal Bedi
  after running Harbour's `contrib/rddads/tests/manage.prg` against
  OpenADS: every management figure printed `0` — uptime, connections,
  work areas, comm packets, worker threads, memory. The ~17 `AdsMg*`
  functions were placeholder stubs (zero-fill the caller's struct,
  return `AE_SUCCESS`). They now report real telemetry.

  - **`MgCollector`** (`src/mgmt/`) — single source of truth. Formats
    the SAP-canonical `ADS_MGMT_*` structs from a `MgSnapshot`. Runs
    identically for local-mode calls and for the server answering a
    remote request, so the two paths cannot diverge.
  - **`MgSnapshot`** carries everything across the wire: live counts
    (connections / work areas / tables / users / worker threads),
    per-entity lists, process RSS, listener port, and the cumulative
    `MgStats` values (uptime, comm packet totals, server-initiated
    disconnects, high-water marks).
  - **Transport.** New `MgConnect` / `MgRequest` wire opcodes
    (`0xA0..0xA3`). `AdsMgConnect` to a `host:port` validates
    reachability with an eager `MgConnect` handshake; a drive path
    such as rddads' `"C:"` resolves to a local-mode backend. Local
    mode enumerates the in-process ABI handle registry.
  - **Honesty.** Fields with no OpenADS analogue — checksum failures,
    NetWare-era ECB counts, per-category memory, serial number —
    report a real `0`, documented in
    `docs/superpowers/specs/2026-05-16-adsmg-telemetry-design.md`.
  - Verified end-to-end against a live remote `openads_serverd`:
    `manage.prg` now prints real uptime, packet counts, worker
    threads, ports and server RSS. `tests/smoke/harbour/manage_probe.prg`
    (a non-interactive `manage.prg` variant) and the new
    `tools/mgprobe` CLI (`openads_mgprobe host:port`) reproduce it.

## 1.0.0-rc23 — 2026-05-15

- **Harbour `contrib/rddads` clean-compile sweep.** Reported by
  Pritpal Bedi after he hit `error: too many arguments to function
  'AdsSetScope'` building Harbour's unmodified `contrib/rddads/`
  against OpenADS's `ace.h`. Compiling the whole contrib (not just
  the rddtst happy path) exposed a family of signatures that the
  442/442 rddtst harness never reached. End-to-end repro:
  `HB_WITH_ADS=…/openads/include/openads hbmk2
  contrib/rddads/rddads.hbp -comp=mingw64` now exits 0.

  Functions brought to SAP-canonical shape (header + ABI export +
  affected unit tests, plus wire opcode for the one function that
  round-trips over the network):

  - **`AdsSetScope`** — 3-arg `(hIndex, usScope, pucKey)` →
    5-arg `(hIndex, usScope, pucScope, usLen, usDataType)`.
    `SetScope` wire opcode now carries `usDataType`; key length
    derives from trailing payload size. Export mirrors
    `AdsSeek`'s `ADS_DOUBLEKEY → ASCII-padded numeric`
    conversion so a scope set with a `double` compares
    apples-to-apples against the index's stored key bytes.
  - **`AdsGetVersion`** — 4-arg with mistyped letter/desc slots
    (`UNSIGNED32*` x4) → 5-arg
    `(UNSIGNED32* major, UNSIGNED32* minor, UNSIGNED8* letter,
    UNSIGNED8* desc, UNSIGNED16* descLen)`. Previous shape would
    have stomped 3 extra bytes past `&ucLetter` on the caller's
    stack.
  - **`AdsCopyTableContents`** — 2-arg → 3-arg with
    `usFilterOption`. Filter mode currently accepted and
    documented; `IGNOREFILTERS` is the implemented path.
  - **`AdsCreateSavepoint` / `AdsRollbackTransaction80`** — 2-arg
    → 3-arg with reserved `ulOptions` parameter (matches ACE 8.x).
  - **`AdsGetAOF`** — `(ADSHANDLE, UNSIGNED32*, UNSIGNED32*)`
    (returned-record-count style) → `(ADSHANDLE,
    UNSIGNED8* pucFilter, UNSIGNED16* pusLen)`. Returns empty
    filter for now; full AOF-source-string round-trip lands with
    M-AOF.4.
  - **`AdsEvalAOF`** — 3rd arg was `UNSIGNED32* pulRecords`; SAP
    expects `UNSIGNED16* pusOptLevel` (returns `ADS_OPTIMIZED_*`).
  - **`AdsSetStringW` / `AdsGetStringW` / `AdsGetFieldW`** — field
    name was declared `UNSIGNED16*` (wide). SAP keeps field names
    ASCII (`UNSIGNED8*`) even on the W variants; only the data
    buffer is wide-char. Helper `resolve_field_index_w` retyped
    to match; UTF-16 → UTF-8 transcode dropped from the name path
    (it was only there to compensate for the wrong type).
  - **`AdsGetString` / `AdsGetLong`** — exports were already
    implemented but missing from the public header, so Harbour's
    ANSI-path code in `ads1.c` linked only via
    `-Wimplicit-function-declaration` warnings. Declarations
    added.
  - **`ADS_MAX_PARAMDEF_LEN`** — `#define` is now `#ifndef`-guarded
    so a Harbour-style pre-define (`#define ADS_MAX_PARAMDEF_LEN
    2048` before the include) is honoured silently.
  - **`AdsGotoBookmark60`** — was 2-arg `(hObj, *pucBookmark)`;
    SAP / real ACE is 3-arg `(hObj, *pucBookmark, ulLength)`.
    Real ACE supports variable-length bookmarks (size depends on
    the index/order), so the caller hands the length back from
    `AdsGetBookmark60`'s `*pulLength` out-param. The 2-arg form
    was internally inconsistent with the Get half of the same
    pair. Both unit tests that exercised the round-trip were
    updated.
  - **`AdsGetAllTables`** — was 2-arg `(*ahTable, *pusArrayLen)`;
    needs `ADSHANDLE hConnect` as the first arg. With no
    connection handle the function can't know whose tables to
    enumerate in a multi-connection process. (Body remains
    `AE_FUNCTION_NOT_AVAILABLE` until M-13 implements enumeration.)

  Why rddtst missed all of this: rddtst exercises the RDD via
  `dbUseArea("ads")` so the cursor walks Harbour's vtable, not
  the `HB_FUN_ADSVERSION` / `HB_FUN_ADSCREATESAVEPOINT` / etc.
  PRG-level wrappers in `adsfunc.c`. Compiling the whole contrib
  is the real header check; we now have it (`C:\harbour-git\
  contrib\rddads` mingw64 build) and will keep it green.

## 1.0.0-rc22 — 2026-05-13

- **M12.25 — `AdsCreateTable` stamps the DBF header last-update date.**
  Follow-up to M12.24 (Robert van der Hulst): a freshly created+opened
  table reported `AdsGetLastTableUpdate()` = `1900-00-00` until the
  first `DbAppend` rewrote the DBF header. ACE writes the create date
  into header bytes 1..3 up front, so OpenADS now does too — in
  `AdsCreateTable` and every other path that lays down a fresh DBF
  (`AdsRestructureTable` / convert, SQL `INTO` / `SELECT` result
  cursors, GROUP BY / aggregate scratch tables). New
  `stamp_dbf_header_today` helper uses the same UTC clock as
  `CdxDriver::rewrite_header_`, so the create stamp and the
  first-append stamp agree.

## 1.0.0-rc21 — 2026-05-12

- **M12.24 — `AdsGetLastTableUpdate` real signature + AOF
  non-optimisable handling.** Was a 3-zero stub with the wrong
  signature (`SIGNED32* date, SIGNED32* time`); now matches ACE
  (`UNSIGNED8* pucDate, UNSIGNED16* pusLen`), reads the DBF header's
  last-updated stamp (header bytes 1..3, year offset from 1900) — over
  the wire too via a new `GetLastTableUpdate` opcode — and renders it
  through the date display format.
- **`AdsSetDateFormat`** stores a process-wide format string that
  `AdsGetDateFormat` / `AdsGetLastTableUpdate` honour
  (`CCYY` / `YYYY` / `YY` / `MM` / `DD` tokens; default
  `yyyy-mm-dd`).
- **`AdsSetAOF` no longer fails error 7200** on a non-optimisable
  expression (e.g. `Empty(NAME)`, `UPPER(NAME) = 'A'`) — ACE treats
  those as "not optimised", so OpenADS drops any prior AOF, reports
  `ADS_OPTIMIZED_NONE`, and returns success, letting the client RDD
  apply the filter itself (same on the server-side `SetAOF` handler).

## 1.0.0-rc20 — 2026-05-12

- **`OPENADS_WITH_HTTP` defaults to ON.** Studio no longer needs the
  explicit CMake flag — pass `-DOPENADS_WITH_HTTP=OFF` to opt out.
- **`AdsGetKeyNum` returns the correct relative key position.**
- **FiveWin + xBrowse over ADS** sample under `examples/fivewin/`.

## 1.0.0-rc19 — 2026-05-12 — X# Advantage RDD compatibility (local + remote)

- **M12.22 — versioned ACE overloads for the X# RDD.** Exports the
  `Ads*NN` entry-point names X# binds by name (`AdsConnect26`,
  `AdsCreateTable71` / `90`, `AdsOpenTable90`, `AdsCreateIndex90`,
  `AdsDDAddTable90`, `AdsDDCreateRefIntegrity62`,
  `AdsFindFirstTable62` / `AdsFindNextTable62`, `AdsGetDateFormat60`,
  `AdsGetExact22`, `AdsReindex61`, `AdsRestructureTable90`). Most
  forward to the base signature (dropping the charset / collation /
  page-size params newer ACE builds added); `AdsGetBookmark60` /
  `AdsGotoBookmark60` round-trip the recno as a 4-byte blob;
  `AdsCancelUpdate90` / `AdsSetProperty90` are accepted no-ops;
  `AdsFindConnection25` / `AdsGetTableHandle25` report not-found
  (OpenADS keys by handle, not path / name).
- **M12.23 — close the export gap the X# Advantage RDD relies on.**
  Live run of X#'s `AXDBFCDX` RDD against OpenADS' `ace64.dll`
  surfaced ~45 more entry points `ADSRDD.prg` binds by name
  (`AdsGetMemoBlockSize`, `AdsGetTableOpenOptions`, `AdsGetBookmark`,
  `AdsCancelUpdate`, `AdsSetField` / `AdsSetEmpty` / `AdsSetNull` /
  `AdsSetShort` / `AdsSetMoney` / `AdsSetTime` / `AdsSetTimeStamp`,
  `AdsGetDate`, `AdsContinue`, `AdsEval*Expr`, RI / unique / autoinc
  enforcement toggles, `AdsStmt*` helpers, …). Forwards where one
  fits, accept-and-ignore for session / statement toggles,
  `AE_FUNCTION_NOT_AVAILABLE` for the genuinely-unimplemented (so the
  X# runtime falls back to its own client path). The field-setter
  family handles the ACE "field name *or* 1-based ordinal cast to a
  pointer" idiom.
- **`AdsAppendRecord` auto-locks the new record** (ACE semantics for
  non-exclusive tables — X#'s `GoHot` refuses to write a record it
  sees as unlocked).
- **`AdsIsRecordLocked` / `AdsLockRecord` / `AdsUnlockRecord` honour
  `recno == 0` = current record** and report the real lock state
  instead of stubbing 0.
- **`AdsCreateIndex61` / `AdsCreateIndex90` option-bit fix:** the
  "descending" flag is `ADS_DESCENDING` (`0x08`), not `0x02` — `0x02`
  is `ADS_COMPOUND`, which X#'s ADSRDD always sets for CDX orders, so
  the old mask built every X# order descending and `DbGoTop` landed
  on the last key.
- **`AdsCreateTable` / `AdsCreateTable90` stage an empty `.fpt` next
  to the `.dbf`** when the field list has an `M` field (using
  `usMemoBlockSize`, default 64) — without it `Connection::open_table`
  can't attach a memo store and any memo write fails "memo store not
  attached".
- **X# RDD against a remote OpenADS server.** Three remote-path
  fixes so X#'s ADSRDD drives `openads_serverd` over the wire
  (`AdsConnect60("tcp://host:port/<datadir>", ADS_REMOTE_SERVER) →
  AX_SetConnectionHandle → DbUseArea`): `remote_field_index` now
  honours the "field name OR 1-based ordinal cast to a pointer" idiom
  (X#'s `_FieldSub` calls `AdsGetFieldType` / `Length` / `Decimals`
  by ordinal — a tiny pointer value was being dereferenced as a
  string); the remote `AdsOpenTable` branch defaults a missing
  extension to `.dbf`; and `AdsGetTableFilename` gained a remote
  path (returning the opened name) instead of failing
  `AE_INTERNAL_ERROR`.
- Full X# `AXDBFCDX` smoke (`tests/smoke/xsharp/AdsSmoke.prg` +
  `AdsSmoke_remote.prg`) passes end-to-end against OpenADS'
  `ace64.dll`.
- **Test layout.** Third-party RDD smoke harnesses under
  `tests/smoke/` (Harbour + X#). GUI showcases (FiveWin, X#
  WinForms) under `examples/`. All opt-in — none run in default
  `ctest`. Doctest coverage `abi_versioned_overloads_test.cpp` +
  `abi_remote_overloads_test.cpp` (gated on `OPENADS_TEST_REMOTE`).
- **Clipper-convention empty / past-end / Limbo states.**
  `goto_record(0)` is no-op + Eof (not error 5000); empty table
  reports BOF / EOF + `RecNo() = LastRec() + 1`; GO past-end enters
  Limbo; CDX dup-tag silent reopen; CDX dup-key insertion uses recno
  tie-break.
- **Index cursor consistency.** `goto_record` re-syncs the index
  cursor to the row's key (so the next SKIP walks the right
  neighbour); CDX cursor state tracking; hard-seek miss parks the
  cursor on the `>` neighbour for SKIP; hard-seek past every key
  parks at AfterEnd; `GO 0` keeps Limbo.
- **`SET DELETED ON` everywhere.** Index walks skip deleted rows;
  natural-order GOTOP / GOBOTTOM skip deleted; all-deleted under
  `SET DELETED` reports Limbo (not Eof); `GOTOP` / `GOBOTTOM` on an
  empty index report Limbo.
- **DESCEND wired through.** `bFindLast` retry on DESCEND retired;
  `DBSEEK( '' )` with `bSoftSeek` lands at the first record +
  `FOUND = .T.`; empty-key shortcut applies only to ASC orders.
- **CDX FOR-clause filter.** `CREATE INDEX … FOR <expr>` honoured at
  build time and on every subsequent insert; `CREATE INDEX` inserts
  deleted rows too (DBFCDX semantics); each new `CREATE INDEX`
  replaces the active order (Clipper convention); re-`CREATE INDEX`
  with an existing tag clears the old B+tree first; `ADS_DOUBLEKEY`
  ASCII conversion + creation-order tag ordinals.
- **rddads compat patches** (`tools/harbour_patch/`). `adsSeek`
  carve-out: `Seek( '' )` with the soft-seek flag now leaves `fBof`
  alone under Limbo so Harbour's own EOF logic doesn't snap to recno
  0; `ORDSETFOCUS(N)` uses CDX-file insertion order, not handle
  ids; `rddads-compat` default connection + `CREATE INDEX` goto-top.
- **SKIP overshoot.** Cursor stays on the last live record, not
  recno 0.

## 1.0.0-rc18 — 2026-05-09 — wire-protocol perf overhaul (~30× xbrowse repaint)

- **M12.17 — `RemoteTable` row cache.** New `FetchCurrentRow` opcode
  returns the entire current record's column values in one frame.
  `AdsGetField` / `AdsGetLong` / `AdsGetDouble` / `AdsGetJulian`
  serve every cell from the cache; W cells per row collapse to 1
  RTT.
- **M12.18 — piggyback row trailer on nav acks.** `GotoTopAck` /
  `GotoBottomAck` / `SkipAck` / `GotoRecordAck` /
  `FetchCurrentRowAck` carry the full row buffer + recno + deleted
  flag. `AdsGetField` / `AdsGetRecordNum` / `AdsIsRecordDeleted`
  all hit the cache populated by the prior nav ack — zero extra RTT
  after every nav.
- **M12.19 — record_count cache.** `AdsGetRecordCount` and
  `AdsGetRelKeyPos` now serve from a per-table cache, invalidated
  only on writes that change row count (`AdsAppendRecord`,
  `AdsDeleteRecord`, `AdsRecallRecord`, `AdsPackTable`,
  `AdsZapTable`).
- **M12.20 — `TCP_NODELAY`** disabled Nagle on every per-connection
  socket. The OpenADS wire is strict ping-pong, so Nagle's
  accumulation delay (up to 200 ms) was pure latency tax. Removes
  ~40–200 ms per RTT on slow links.
- **Net xbrowse PgDn**: pre-M12.17 ~300 RTT → ~20 RTT × ~5 ms (Nagle
  off) ≈ ~100 ms. ~30× end-to-end speedup vs pre-M12.17.

## 1.0.0-rc17 — 2026-05-09 — full wire bridges (rddads parity) + AOF/Rushmore + Studio Demo

The wire layer now carries every common ABI op rddads emits when an
app connects via `AdsConnect60("tcp://host:port/dir")`. Until rc16
most ABI calls past `AdsOpenTable` collapsed with "unknown table"
because they only resolved local `Table*`. Fixed across 40+ entry
points:

- **M12.14 — field metadata + cursor state.** `AdsGetNumFields`,
  `AdsGetFieldName`, `AdsGetFieldType`, `AdsGetFieldLength`,
  `AdsGetFieldDecimals`, `AdsAtBOF`, `AdsGetRecordNum`,
  `AdsIsRecordDeleted`, `AdsGotoBottom`. New `DescribeTable` opcode
  returns schema in one round-trip (cached on `RemoteTable` so
  `adsOpen` field-iter stays cheap).
- **M12.14b — typed reads.** `AdsGetLong`, `AdsGetDouble`,
  `AdsGetJulian` reuse `GetField`, parse client-side. No new opcode.
- **M12.15 — info / lock / maintenance / AOF.** `AdsIsFound`,
  `AdsRefreshRecord`, `AdsGetTableType`, `AdsGetRecordLength`,
  `AdsGetNumIndexes`, `AdsGetLastAutoinc`, `AdsLockRecord` /
  `Unlock`, `AdsLockTable` / `Unlock`, `AdsPackTable`,
  `AdsZapTable`, `AdsFlushFileBuffers`, `AdsCloseAllIndexes`,
  `AdsSetAOF`, `AdsClearAOF`, `AdsGetAOFOptLevel`.
- **M12.15b — memo.** `AdsGetMemoLength`, `AdsBinaryToFile`,
  `AdsFileToBinary` reuse `GetField` / `SetField`.
- **M12.16 — index handle subsystem.** New
  `HandleKind::RemoteIndex` + `RemoteIndex` wrapper.
  `AdsOpenIndex` / `AdsCloseIndex` / `AdsSeek` / `AdsSeekLast` over
  the wire. Server lazy-promotes ABI handles in `tbls_h` and syncs
  the engine cursor after Seek so the two cursors never drift.
- **M12.16b — remaining index ops.** `AdsCreateIndex` /
  `AdsCreateIndex61` (CDX-on-the-wire), `AdsSkipUnique`,
  `AdsSetScope`, `AdsClearScope`.
- **M12.16c — order switching.** New ABI exports
  `AdsSetIndexOrder` (by tag name) and `AdsSetIndexOrderByHandle`
  (by hIndex). Wire bridges via `SetOrder` / `SetOrderByName`
  opcodes.

## 1.0.0-rc16 — 2026-05-09

- **`AdsGetRelKeyPos` / `AdsSetRelKeyPos` honour the active index
  walk.** When `t->order()->index()` is bound, both walk the index
  once (seek_first → next() loop) to compute / position by *key*
  fraction, not raw recno. Cursor recno is restored after the `Get`
  probe so it doesn't visibly move the user-facing position. No-
  active-order behaviour unchanged.

## 1.0.0-rc15 — 2026-05-09

- **`+` (VFP autoincrement) field type.** dBASE Level 7 / VFP
  autoincrement column carries the field-descriptor type byte `+`;
  classifier in `src/drivers/dbf_common.cpp` now maps it to
  `DbfFieldType::Integer`. Prior fall-through left the schema
  parser at `Unknown`.

## 1.0.0-rc14 — 2026-05-08 — Windows Service + systemd / launchd units

- **Windows Service support.** `openads_serverd --install-service`
  registers a `SERVICE_AUTO_START` Win32 service;
  `--uninstall-service` drops the registration; the same binary
  doubles as both interactive CLI and SCM-driven service through a
  real `RegisterServiceCtrlHandler` with `SERVICE_CONTROL_STOP` /
  `SERVICE_CONTROL_SHUTDOWN` driving the same graceful-shutdown
  path as interactive Ctrl+C.
- **Linux systemd unit** (`scripts/openads-serverd.service`)
  hardened — `User=openads`, `ProtectSystem=strict`,
  `NoNewPrivileges`, `RestrictAddressFamilies=AF_INET AF_INET6`,
  `PrivateTmp`, `Restart=on-failure`.
- **macOS launchd plist**
  (`scripts/com.openads.serverd.plist`) — KeepAlive on crash;
  stdout / stderr to `/var/log/openads-serverd.{out,err}.log`.

## 1.0.0-rc13 — 2026-05-08 — production-CDX auto-open + Studio Demo + version fix

- **M-AOF.6 — production-CDX auto-open in `AdsOpenTable`.** Opening
  `<base>.dbf` auto-binds the sibling `<base>.cdx` so every tag in
  it becomes navigable on the Table without an explicit
  `AdsOpenIndex60` call. rc12 didn't do that — Studio's per-request
  short-lived `AbiSession` re-opened the DBF on every `/rows` fetch
  but never picked up a CDX created in a prior session, so
  `AdsGetAOFOptLevel` reported `NONE` forever even after `CREATE
  INDEX`.
- **Studio — guided AOF demo.** Browse-tab `▶ Demo` button walks
  the full Rushmore-style AOF story end-to-end against whatever
  table is active.
- **Studio — AOF hint chips functional.** When the AOF doesn't
  reach `FULL`, Studio surfaces a chip per character / memo field
  referenced by the cond that doesn't have a matching index yet.
  Click → runs `CREATE INDEX <field>_IDX ON <table> (<field>)` +
  re-applies the AOF.
- **`openads_serverd --version`** now reports the actual tag via
  `git describe --tags --always --dirty` at CMake configure time.
  Previously hard-coded `1.0.0-rc1` since rc1.

## 1.0.0-rc12 — 2026-05-08 — AOF (Rushmore-style) full slice

First working slice of **Advantage Optimised Filters (AOF) —
Rushmore-style query optimisation**. `AdsSetAOF` parses + evaluates
the cond, installs a per-record bitmap as a filter predicate that
`Skip` / `GoTop` honour, and routes individual leaves through CDX /
NTX index range scans whenever an open index has the leaf's field
as its key expression. `AdsGetAOFOptLevel` reports
`ADS_OPTIMIZED_FULL` / `PART` / `NONE` based on per-leaf coverage.
Sparse-bitmap navigation lifts the visible-set walk from O(N) to
O(M).

V1 grammar (identifiers + keywords case-insensitive, both
Clipper-style `.T.` / `.AND.` and SQL-style accepted):

```
<field> OP <literal>      OP in { = == != <> # < <= > >= }
<field> BETWEEN a AND b
<field> IN ( v1, v2, ... )
expr AND expr      OR     NOT expr      ( expr )
```

Index-accelerated leaves in V1: character / memo fields with a
bare-field-name index expression; operators Eq, Ne, Lt, Le, Gt, Ge,
Between, In. Numeric / date / logical fields, `UPPER(field)` /
compound expressions still produce a correct bitmap via per-record
fallback (don't count as "served by index").

## 1.0.0-rc10 — 2026-05-08

- **Studio mode badge.** SPA header now shows 🏠 `LocalServer`
  (green) when the console runs in-process inside `ace64.dll` /
  `ace32.dll`, or 🌐 `Remote Server` (blue) when hosted by
  `openads_serverd`. Hover reveals the active data directory.
  Signal from a new `mode` field on `/api/health`
  (`"localserver"` when `HttpConsole` was started without a backing
  wire-server pointer, `"remote-server"` otherwise). Reverse-proxy
  deployments that strip unknown fields keep working unchanged —
  the badge stays hidden when `/api/health` is unreachable.

## 1.0.0-rc9 — 2026-05-08 — embedded Studio (LocalServer)

Studio web console is now embedded inside `ace64.dll` / `ace32.dll`
itself. A LocalServer application — Harbour / X# / Clipper loading
the OpenADS DLL directly without launching `openads_serverd.exe` —
spins up the console in its own process.

- Three OpenADS-only entry points:
  `AdsStudioStart(port, data_dir)`, `AdsStudioStop()`,
  `AdsStudioPort(&port)` (ordinals 238–240).
- Env-driven auto-start: `OPENADS_STUDIO_PORT=<port>` before
  launching the host app; `OPENADS_STUDIO_DATA` / `OPENADS_STUDIO_HOST`
  default to `"."` / `127.0.0.1`. Without the port env var the auto
  hook is a no-op — no surprise localhost listener.
- Compiled into the DLL only when `-DOPENADS_WITH_HTTP=ON` (default
  since rc20). Without that flag the three exports return
  `AE_FUNCTION_NOT_AVAILABLE` so callers can detect the build
  flavour at runtime.

## 1.0.0-rc8 — 2026-05-08 — dual x64+x86, static mbedtls

Addresses XSharp-Project feedback (Robert van der Hulst):

- **Dual x64+x86 ZIP.** Bundle ships both `ace64.dll` and
  `ace32.dll` plus matching `openads_serverd_{x64,x86}.exe` /
  `openads_bench_{x64,x86}.exe`. X#, Harbour-x86, and legacy
  Clipper apps all pick the right bitness without a separate
  download.
- **Static-linked mbedtls 3.6.2.** Zero runtime `libssl` /
  `libcrypto` / `mbedtls` DLL dependency. Verified via
  `dumpbin /dependents`: only KERNEL32, WS2_32, bcrypt, MSVCP140,
  VCRUNTIME, and Windows ucrt `api-ms-win-crt-*` show up.
- **`openads_serverd --port <N>`** plus an explicit "port 6262 is
  the SAP Advantage Database Server default" hint when the bind
  clashes with a running ADS service.
- **`tools/bench/README.md`** documents that `openads_bench`
  synthesises its own three-column DBF (`ID N(8,0)`, `TAG C(4)`,
  `AMT N(8,2)`) on each run from a fixed seed; nothing is shipped,
  fully reproducible.

## 1.0.0-rc6 — 2026-05-07

Studio schema view shows the ADS field-type *letter* (C / N / M / D
/ L / I / Y / B / V / Q / G) instead of the numeric code, with the
ADS type-letter mapping corrected; the numeric tooltip is dropped.

## 1.0.0-rc5 — 2026-05-07

Studio binary-memo serializer + Win x86 build fix. Binary memo
cells are now base64-encoded in `/api/tables/<t>/rows` so JSON
round-trips cleanly through the SPA.

## 1.0.0-rc4 — 2026-05-07

CDX leaf+branch full multi-level split + memo > 64 K fix +
cross-platform `-Werror` clean.

- **`M(cdx-split)`** — full multi-level B+tree leaf+branch split.
  Stress harness now exercises 2 bags × 2 tags each at 200 K rows;
  `CREATE INDEX` defaults back to CDX (the previous NTX fallback is
  retired).
- **DBF compat.** Accept 0xF5 / 0xFB headers + single-letter type
  codes; `AdsGetField` no longer truncates memos > 64 K to 65534
  bytes.
- **Cross-platform.** `ace32.lib` / `ace64.lib` import libs +
  ordinal-compat tooling shipped. GCC 13 (Ubuntu 24.04) + clang
  Release pass `-Werror` cleanly: `-Wshadow`,
  `-Wstringop-truncation`, `-Wformat-truncation`, `-Wsign-conversion`
  in `julian_to_ymd`, `COL%zu` field-name copy, stress harnesses,
  `probe_cdx2`, drivers/cdx iterators.
- **Studio.** `studio.web.0.12` ZIP backup of data dir,
  `studio.web.0.13` table-type override + memo hex viewer,
  `studio.web.0.14` host OS / arch / compiler banner.

## 1.0.0-rc3 — 2026-05-07

Studio sessions kill + JSON export + TLS deployment docs.

- **Studio.** `studio.web.0.10` bulk select + saved queries + SQL
  highlight; `studio.web.0.11` kill-session button + JSON export.
- **TLS deployment guide** (EN / ES / PT) — proxy, stunnel, SSH
  tunnel recipes for fronting the cleartext server with TLS until
  M12.12 ships.
- CI release-workflow build timeout bumped 45 → 60 minutes.

## 1.0.0-rc2 — 2026-05-07

Studio web 0.4 – 0.10: Sessions, Data Dictionary tab + REST,
Reindex / Pack / Zap + `CREATE INDEX` wizard + memo viewer, sidecar
list / server stats / DBF upload, HTTP Basic auth + table download
+ theme toggle, Browse sort + filter + i18n (EN / ES / PT). Data
Dictionary documentation pages (EN / ES / PT). `b64decode`
sign-conversion fix.

## 1.0.0-rc1 — 2026-05-07

Studio web console + bilingual+ documentation site + release CI.

- **OpenADS Studio** (`studio.web.0.1` … `0.3`) — clean-room
  ARS-equivalent web console embedded in `serverd`. CRUD + paginated
  browse + server info; `CREATE` / `DROP` / encrypt + SQL history.
  All third-party-product-name references scrubbed from Studio
  copy. `AdsGetLastError` captured before close + 'did you mean'
  hint on connection failures. Docs link in header.
- **Documentation site** — bilingual+ EN / ES / PT under `docs/`,
  published through GitHub Pages workflow. Benchmarks pages +
  Studio screenshots.
- **Release CI** — `release` workflow builds + packages + publishes
  on tag push. `docs/superpowers` and `build/` are export-ignored
  from source archives.

## 0.4.1 — 2026-05-06

`openads_bench` v2 + CI matrix gains a TLS=ON entry.

## 0.4.0 — 2026-05-06

Real TLS transport, transport abstraction, wire-protocol spec.

- **M12.12** — real TLS via vendored mbedtls 3.6 LTS (Apache-2.0).
  The `tls://` URI scheme reserved in 0.3.6 now wires through to a
  real handshake on both client and server.
- **M12.13** — transport abstraction. The wire protocol no longer
  bakes in the socket type; cleartext, TLS, and any future
  transport plug in through a single virtual interface.
  `docs/wire-protocol.md` documents the wire format.
- CI runners + actions bumped to current versions; missing
  `<tuple>` include added.

## 0.3.6 — 2026-05-06

`tls://` URI scheme reserved (M12.12 stub) ahead of the real
handshake landing in 0.4.0.

## 0.3.5 — 2026-05-06

Wire-protocol semantics complete: ACE error-code propagation
(M12.10) and batched row fetch (M12.11 — `Fetch` / `FetchAck`).

## 0.3.4 — 2026-05-06

Phase 2 server feature-complete (read + write + SQL + index +
auth).

- **M12.6** — remote write surface (`append` / `set` / `delete` /
  `recall` / `goto` / `flush`).
- **M12.7** — remote SQL exec (`ExecuteSQL` wire op).
- **M12.8** — remote `AdsReindex`.
- **M12.9** — server-side authentication.
- `RemoteConnection` dtor now calls `disconnect`.

## 0.3.3 — 2026-05-06

Phase 2 server alive end-to-end + cross-platform SQL bench.

- **M12.3** — server accept loop + `Hello` / `Connect` dispatch.
- **M12.4** — remote read-only table ops.
- **M12.5** — dual-mode DLL: `tcp://` URIs route the ABI to the
  server; local-mode paths still hit the in-process engine.
- `openads_serverd` standalone TCP server CLI.
- `openads_bench` cross-platform SQL workload timer.
- macOS bring-up: `setup_macos.sh` one-shot script,
  `HOST_NAME_MAX` fallback, `accept()` self-connect wake-up,
  same-process lock contention test skip.
- Linux bring-up: OFD locks for fd-scope contention,
  `shutdown(SHUT_RDWR)` before `close()` so blocked `accept()`
  wakes, third + fourth waves of clang `-Werror` sign /
  include / unused fixes,
  `-Wreturn-type-c-linkage` cleanup on internal helpers.

## 0.3.2 — 2026-05-05

SQL window-function + scalar-fn deepening + sockets layer.

- **M10.49** PARTITION BY.
- **M10.50** RANK / DENSE_RANK.
- **M10.51** qualified column refs.
- **M10.52** multi-row `VALUES`.
- **M10.53** `NULLIF` / `COALESCE` / `IFNULL`.
- **M10.54** `FILTER (WHERE …)` aggregate clause.
- **M12.2** sockets layer (cross-platform).

## 0.3.1 — 2026-05-05

SQL date / CTE / NULL surface + collation + wire skeleton.

- **M10.43** multi-arg scalar fns.
- **M10.44** `IS NULL` / `IS NOT NULL`.
- **M10.45** date scalar fns.
- **M10.46** derived tables.
- **M10.47** `ROW_NUMBER()`.
- **M10.48** CTE (`WITH …`).
- **M11.6** NULL bitmap.
- **M11.7** collation.
- **M11.8** OEM / UTF-8 conversion.
- **M12.1** wire skeleton.

## 0.3.0 — 2026-05-05

SQL maturity wave: every JOIN flavour, the full subquery /
aggregate / DISTINCT / UNION / CASE / LIMIT surface, plus the AEP
host, AES-256-CTR encrypted DBFs, VFP V / Q field types, and
nested transactions. Brings the SQL layer to "real apps run
through it" status.

### Highlights

- **JOIN matrix.** `INNER` (M10.13 parse + M10.14 executor), `LEFT
  OUTER` (M10.16), `RIGHT OUTER` (M10.21), `FULL OUTER` (M10.22 —
  union of LEFT + RIGHT), `JOIN` + `WHERE` / `ORDER BY` combos
  (M10.20), `JOIN` + aggregate combo (M10.23), `GROUP BY` across
  `JOIN` (M10.34).
- **Subqueries.** `IN` literal lists + subqueries (M10.15),
  `EXISTS` uncorrelated (M10.17), correlated `EXISTS` (M10.24),
  scalar subquery `<col> op (SELECT col FROM t)` (M10.18),
  aggregate scalar subquery (M10.19), correlated scalar subquery
  (M10.29), correlated `IN` subquery (M10.35).
- **Aggregates + grouping.** `COUNT(*)` / `COUNT` / `SUM` / `AVG`
  / `MIN` / `MAX` (M10.10), `GROUP BY` + `HAVING` (M10.25), HAVING
  expression tree (M10.30), aggregate / `GROUP BY` inside `UNION`
  members (M10.36).
- **Set + shape ops.** `UNION` / `UNION ALL` (M10.26),
  `UNION` + projection (M10.27), `UNION` + `ORDER BY` (M10.28),
  `DISTINCT` (M10.31), `LIMIT` / `OFFSET` (M10.32),
  `BETWEEN` / `LIKE` (M10.33), multi-column `ORDER BY` (M10.37),
  `CASE WHEN` in projection (M10.38).
- **DML / DDL.** `INSERT` (M10.5), `ORDER BY` execution (M10.6),
  `UPDATE` / `DELETE` bulk through `AdsExecuteSQLDirect` (M10.7),
  projection lists (M10.8), DDL `CREATE TABLE` / `CREATE INDEX`
  (M10.9), VFP autoinc fields with persistent counter (M10.11),
  `AdsRestructureTable` CHANGE (same-type length / decimals)
  (M10.12), `INSERT INTO … SELECT` (M10.41), `CREATE TABLE AS
  SELECT` (M10.42).
- **Other SQL.** `WHERE` `OR` / `NOT` / parens + numeric literals
  (M10.3), scalar string fns (M10.39), arithmetic in projection
  (M10.40).
- **Engine + drivers.** Real `.add` Data Dictionary persistence —
  round-trips through `.add` reopen (M10.1); VFP I / Y / B field
  decode + encode (M10.2); branch-level MISS closure
  (`AdsRestructureTable` DELETE-fields + `AdsSetIndexDirection`)
  (M10.4).
- **AEP host (M11.4).** `CREATE PROCEDURE` + `EXECUTE PROCEDURE`
  through the OpenADS clean-room AEP runtime.
- **Encrypted DBFs (M11.2).** OpenADS-encrypted DBF — AES-256-CTR
  per-page, transparent through the read / write path.
- **VFP V / Q (M11.1).** Varchar + Varbinary field types.
- **Nested transactions (M11.3).** `AdsReleaseSavepoint` + nested
  `BEGIN` / `COMMIT` (proper save-point stack).

## 0.2.0 — 2026-05-04

The 0.2.0 release closes the entire 226-symbol Harbour-reachable
`Ads*` ABI surface — every export resolves to either a real
implementation or a documented local-mode silent-success. No exports
hard-fail with `AE_FUNCTION_NOT_AVAILABLE` at the function level any
more. The release also relicensed the project from MIT to Apache
License 2.0 and added a clean-room provenance / non-commercial /
no-warranty disclaimer block + NOTICE file.

### Highlights

- **Compound CDX expression evaluator.** `UPPER`, `LOWER`,
  `LTRIM` / `RTRIM` / `ALLTRIM`, `STR(n[,len[,dec]])`, `DTOS(date)`,
  `SUBSTR(s,start[,len])`, and string concatenation with `+`. UPPER /
  LOWER / SUBSTR walk UTF-8 codepoints (ASCII + Latin-1 supplement
  case map, `ÿ↔Ÿ` pair); the Latin-1 case mapping table closes the
  M9.17 `*W` Unicode surface.
- **Real CRUD for tables and indexes.** `AdsCreateTable` parses the
  rddads `NAME,Type,Len,Dec;…` field-def syntax; `AdsCreateIndex61` /
  `AdsCreateIndex` build CDX or NTX bags compatible with FoxPro and
  Clipper layouts. `AdsZapTable` / `AdsPackTable` / `AdsReindex`
  match the Clipper bound-index lifecycle.
- **Multi-file index binding.** Multiple `.ntx` files (or multiple
  pre-built `.cdx` bags) coexist on a single Table; same-path reopen
  refreshes; `AdsCloseIndex` drops the closed view without disturbing
  the active order.
- **Transactions + savepoints + WAL recovery.** `AdsBeginTransaction`
  / `AdsCommitTransaction` / `AdsRollbackTransaction` /
  `AdsCreateSavepoint` / `AdsRollbackTransaction80(savepoint)`. Mid-
  tx crash + reopen replays the WAL and writes back before-images
  for orphan transactions.
- **Memo (DBT / FPT) read + write + binary type.** Text memos
  round-trip through DBT and FPT; `AdsGetBinary` /
  `AdsGetBinaryLength` / `AdsSetBinary` carry binary blobs through
  FPT block-type tags (Text / Picture / Object); chunked
  `AdsSetBinary` writes reassemble through a per-(table, field)
  accumulator.
- **Locking with retry policy.** `AdsLockTable` / `AdsLockRecord`
  use non-blocking byte-range acquires (`LockFileEx
  LOCKFILE_FAIL_IMMEDIATELY` on Windows, `fcntl F_SETLK` on POSIX);
  `AdsSetLockCycle` / `AdsSetLockRetryCount` configure the retry
  budget.
- **Full-text search.** `AdsCreateFTSIndex` writes a clean-room
  `# OpenADS FTS v0` text file per table; `AdsFTSSearch` and the
  SQL `CONTAINS(<col>, '<query>')` predicate intersect per-token
  recno lists with AND semantics.
- **Server / dictionary surface.** `AdsMg*` (15 calls) report local-
  mode "everything quiescent" responses; `AdsDD*` (14 advanced-DD
  calls) accept silently and zero-fill property getters. Real
  persistence in the OpenADS DD format lands with 0.3.x.
- **Schema evolution.** `AdsRestructureTable` ADD-fields path
  rewrites the DBF with extended schema and preserves every
  record's original-field bytes; DELETE / CHANGE arguments still
  surface AE_FUNCTION_NOT_AVAILABLE pending VFP / ADT structural
  extensions.
- **Misc.** Real `AdsGetServerName` / `AdsGetServerTime`,
  `AdsGetLongLong`, `AdsSetFieldRaw`, `AdsVerifySQL`,
  `AdsFailedTransactionRecovery`, `AdsGetAllLocks`, `AdsSkipUnique`,
  `AdsFindFirstTable` / `AdsFindNextTable` / `AdsFindClose`,
  `AdsCopyTable` / `AdsCopyTableContents` / `AdsConvertTable`,
  `AdsAddCustomKey` / `AdsDeleteCustomKey`.

### Project posture

- License: relicensed **MIT → Apache License 2.0** (`LICENSE` +
  `NOTICE`).
- Independence + non-commercial purpose + clean-room provenance +
  no-warranty + downstream responsibility — block added to the
  README and mirrored to the NOTICE file (Apache 4(d) preservation).
- Tests: **214** doctest cases, **3865** assertions, all green on
  Windows / MSVC Release. CI matrix builds Windows + Linux + macOS
  cleanly through `.github/workflows/ci.yml`.

### Milestones

| Tag | Milestone |
|-----|-----------|
| `m9.1`   | Compound CDX expression evaluator |
| `m9.2`   | Stub batch reorganised into real / no-op / missing |
| `m9.3`   | Compound expressions validated through Harbour |
| `m9.4`   | `AdsGotoRecord` + table / file metadata |
| `m9.5`   | `AdsCreateTable` |
| `m9.6`   | `AdsRefreshRecord` + `AdsExtractKey` |
| `m9.7`   | `AdsCreateIndex61` with compound expression |
| `m9.8`   | `AdsZapTable` + `AdsPackTable` |
| `m9.9`   | `AdsReindex` |
| `m9.10`  | NTX multi-level B+tree split |
| `m9.11`  | `AdsCopyTable` / `AdsCopyTableContents` / `AdsConvertTable` |
| `m9.12`  | `AdsFindFirstTable` / `AdsFindNextTable` / `AdsFindClose` |
| `m9.13`  | Binary memo (`AdsGetBinary` / `AdsSetBinary` / `AdsGetBinaryLength`) |
| `m9.14`  | NTX multi-tag binding |
| `m9.15`  | Real `AdsGetServerName` / `AdsGetServerTime` + binding-leak fix |
| `m9.16`  | Chunked `AdsSetBinary` |
| `m9.17`  | Unicode `*W` variants |
| `m9.18`  | Lock retry / cycle policy |
| `m9.19`  | `AdsCreateFTSIndex` |
| `m9.20`  | `AdsAddCustomKey` / `AdsDeleteCustomKey` |
| `m9.21`  | FTS search side (`AdsFTSSearch` + SQL `CONTAINS`) |
| `m9.22`  | UTF-8 codepoint-aware index-expression evaluator |
| `m9.23`  | Misc MISS fillers (LongLong / FieldRaw / VerifySQL / FailedTxRecovery / GetAllLocks / SkipUnique) |
| `m9.24`  | Local-mode `AdsMg*` surface (15 calls) |
| `m9.25`  | Local-mode `AdsDD*` CRUD surface (14 calls) |
| `m9.26`  | `AdsRestructureTable` (ADD-fields path) |
| `m9.27`  | CI matrix portability |

## 0.1.0 — 2026-05-04

Final 0.1.0. The post-rc1 work below extends the Harbour smoke
beyond the read path covered in 0.1.0-rc1: a real Harbour app now
also drives multi-tag focus swaps, ARIES-style transactions, and
memo M-field round-trips end-to-end through OpenADS' `ace64.dll`.

### M8.9 — Multi-tag CDX + OrdSetFocus

- `AdsOpenIndex` widened to its real 4-arg signature
  `(hTable, pucName, ahIndex[], &pu16ArrayLen)`. Every tag inside a
  compound CDX is opened by name through `CdxIndex::open_named`;
  the first tag's IIndex moves into `Table::set_order` and the rest
  park in their bindings.
- `Table::take_order()` / `Order::release()` surrender the active
  index's `unique_ptr<IIndex>` so a focus swap can park it in the
  previous binding's slot.
- `get_table` and `table_for_index` now call `activate_binding(h)`
  whenever a navigation call arrives with an index handle, so
  rddads' `pArea->hOrdCurrent` swaps drive the Table's active order
  in lockstep.
- `AdsGetIndexHandle` strips trailing whitespace from the caller's
  tag name; `AdsGetIndexName` / `AdsGetIndexExpr` read each
  binding's metadata directly so parked tags report their real name
  even before they become live.
- `AdsGetNumIndexes` returns the per-table binding count.

### M8.10 — Transactions through Harbour

- A real Harbour app drives `AdsBeginTransaction` /
  `AdsRollback` / `AdsCommitTransaction` directly. BEGIN + APPEND +
  ROLLBACK leaves the appended row in the DBF flagged deleted (CDX
  index entries persist by design — `Found()` still reports `T` but
  `Deleted()` is `T`); BEGIN + APPEND + COMMIT persists durably to
  both the DBF and every CDX tag.
- `Table::register_extra_index_view` /
  `Table::unregister_extra_index_view` /
  `Table::clear_extra_index_views` track the parked CDX sub-tags as
  non-owning views; the binding still owns the IIndex lifetime.
- `Table::snapshot_index_keys_()` captures the pre-write key per
  index — active order plus extras — and `sync_all_indexes_(snap)`
  erases each prior `(recno, prev_key)` and inserts the new one in
  lockstep, so a `set_field` on a multi-tag CDX keeps every tag
  consistent (M8.8 only synced the active order).
- `Table::flush()` flushes the active order **and** every extra
  view so a multi-tag commit reaches disk for every tag.

### M8.11 — Memo M-fields (FPT)

- A real Harbour app appends rows whose `FIELD->NOTES` carries a
  short memo (43 bytes) and a longer memo (280 bytes), closes the
  area, reopens, and reads the memos back via the standard Clipper
  RDD surface.
- `make_cdx.exe` now also writes an empty `data.fpt` next to
  `data.cdx` via `FptMemo::create`, so `Connection::open_table` finds
  a memo store to auto-attach when the DBF declares an M field.
- `AdsGetMemoLength` / `AdsGetMemoDataType` / `AdsGetString` are now
  real implementations using `resolve_field_index` (M4 had earlier
  versions that only accepted string field names; rddads passes the
  `ADSFIELD(n)` integer form).
- `AdsCloseTable` flushes the table before releasing the handle so
  non-transactional appends reach disk on `USE` close.
- `ADS_MEMO_TEXT` / `ADS_MEMO_PICTURE` aliases resolve to the
  M8.4-verified `ADS_STRING` (4) and `ADS_IMAGE` (7) values.

## 0.1.0-rc1 — 2026-05-03

First end-to-end validation against Harbour `contrib/rddads`. A real
`.prg` compiled with `hbmk2 -comp=msvc64 -lrddads -lace64` opens a
DBF, walks records, runs `dbSeek`, appends rows, and reopens — every
call lands on OpenADS' `ace64.dll` with no Harbour rebuild.

### M0–M3.10 (engine + drivers)

- 5-layer architecture: ABI shim → Session/Connection → SQL → Engine
  (Table / Index / MemoStore / LockMgr / TxLog) → OS abstraction.
- DBF read/write for C / N / L / D columns; deletion flag; flush.
- CDX driver with compound layout (file header + structure-tag root +
  per-tag CDXTAGHEADER + sub-tag B+tree). Multi-tag-per-file API
  (`add_tag` / `open_named` / `list_tags`). FoxPro-equivalent leaf
  bit-pack (mirrors Harbour `hb_cdxPageLeafInitSpace`). Compound CDX
  closes the last reviewer-flagged compat-breaking item.
- NTX driver with cache-based in-order traversal for multi-level
  trees; leaf-split fix promotes the separator without duplicating it.
- AES-128 / AES-256 ECB primitives (vendored tiny-AES-c, Unlicense),
  validated against FIPS-197 + NIST SP 800-38A.
- DBT + FPT memo round-trip.
- Data Dictionary (`.add`): `TABLE alias=path` text format with alias
  resolution.
- Minimal SQL: `SELECT * FROM <table> [WHERE col op 'lit' [AND ...]]`
  with six comparison operators.

### M4 — Locking

- OS byte-range locking with ranges compatible with original ACE so
  installs can coexist during migration.

### M5 — Transactions

- WAL with CRC-32C records (BEGIN / UPDATE / COMMIT / ABORT).
- In-memory ordered op log with named savepoints (M5.3).
- Group commit (M5.4): each record carries a monotonic LSN;
  `sync_to(lsn)` is the group-commit primitive — first thread to
  observe `last_synced_lsn_ < lsn` issues a single fsync covering
  the high-water mark.
- Idempotent recovery (M5.5) via `openads.lsnmap` sidecar.
  Concurrent recovery passes can never regress the per-record
  watermark.

### M6 — Data Dictionary

- `.add` parser, `Connection::open(<path>.add)` resolves member
  tables through the dictionary on every `AdsOpenTable`.

### M7 — SQL

- Parser + executor for `SELECT *` + multi-clause `WHERE` joined by
  implicit `AND`. Compiles to a `Table::RowPredicate` closure used by
  `AdsExecuteSQLDirect` to filter the cursor's record stream.

### M8 — Harbour conformance (this release)

- **M8.0** `ace64.dll` / `ace32.dll` SHARED CMake target with a `.def`
  exporting 80 real `Ads*` entry points.
- **M8.1** 226 `Ads*` exports — superset of every symbol Harbour
  `rddads.lib` references; the 146 newly-stubbed entries return
  `AE_FUNCTION_NOT_AVAILABLE` (5004) so the link resolves cleanly.
- **M8.2** Six legacy MSVC2013-era CRT shims (`_dclass`, `_dsign`,
  `_wfsopen`, `_getch`, `_kbhit`, `_eof`) re-exported under aliases
  so Harbour's prebuilt msvc64 libs link against modern UCRT.
- **M8.2** `smoke.exe` runs end-to-end: `AdsVersion()` resolves
  through the rddads wrapper to OpenADS' `AdsGetVersion`.
- **M8.3** `USE data VIA "ADSCDX"` + walk records. `Connection::open_table`
  auto-appends `.dbf`. `AdsGetField` / `AdsGetFieldType` /
  `AdsGetFieldLength` accept either string field names or the
  `ADSFIELD(n)` integer-cast-as-pointer form. `AdsConnect`
  is now a real wrapper around `AdsConnect60`.
- **M8.4** ACE field-type constants verified by sweeping
  `AdsGetFieldType`'s return through 0..40 against rddads. Result:
  `ADS_LOGICAL = 1`, `ADS_NUMERIC = 2`, `ADS_DATE = 3`,
  `ADS_STRING = 4`, ... — the inverse of the public ACE SDK ordering
  in some places. Mapping captured in `include/openads/ace.h`.
- **M8.5** Multi-field DBF (C/N/L/D) end-to-end. `AdsGetFieldDecimals`,
  `AdsGetLong`, `AdsGetDouble`, `AdsGetJulian` real impls (was 5004
  stubs); `AdsGetJulian` parses `YYYYMMDD` and computes Clipper Julian
  Day Numbers using the same Gregorian formula as `hb_dateEncode`.
- **M8.6** `dbSeek` end-to-end through OpenADS' CDX. `Table::path()`,
  index path resolution + auto-extension, polymorphic `get_table`
  (accepts table or index handles — rddads' `adsGoTop` calls
  `AdsGotoTop(hOrdCurrent)` when an order is active), 6-arg `AdsSeek`
  signature matching rddads, real `AdsIsFound` reading
  `Table::last_seek_found_`.
- **M8.7** Write path: `dbAppend` + `FIELD-> := value` + `dbCommit` +
  reopen. `AdsSetString` / `AdsSetLogical` / `AdsSetDouble` /
  `AdsSetLongLong` / `AdsSetJulian` real impls; field index resolution
  via `resolve_field_index`.
- **M8.8** Active index auto-syncs on every record mutation.
  `Table::compute_index_key_` evaluates bare-field-name expressions
  against the current `record_buf_`; `Table::sync_active_index_`
  erases the prior `(recno, prev_key)` and inserts the new one.
  `Table::flush()` flushes both the driver and the index.

### Tests

- 135 doctest cases / 1820 assertions passing on Windows / MSVC
  Release.
- One `tests/harbour_smoke/` integration harness producing a
  runnable `smoke.exe` that exercises the full Harbour →
  rddads.lib → OpenADS path.
