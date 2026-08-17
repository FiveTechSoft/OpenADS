# OpenADS

📖 **Docs**:
[English](https://fivetechsoft.github.io/OpenADS/en/)
· [Español](https://fivetechsoft.github.io/OpenADS/es/)
· [Português](https://fivetechsoft.github.io/OpenADS/pt/)

🤝 **Contributing:** [`CONTRIBUTING.md`](CONTRIBUTING.md)

A free and open-source implementation compatible with Advantage Database Server (ADS), discontinued by SAP.

The goal is to provide a *drop-in* replacement for the Advantage Client Engine (`ace32.dll` / `ace64.dll` / `libace.so`) so existing applications — particularly Harbour/Clipper apps using `contrib/rddads` — keep working without recompilation.

### One API, many backends — DBF today, any database tomorrow

OpenADS **presents as ADS** — your application talks to the same ACE
entry points it always did — but that ADS surface is just the front
door. Behind it, the very same API can be served by different storage
engines:

| Backend | Connection | Use it for |
|---------|-----------|------------|
| **DBF / ADT** (native xBase, CDX/NTX/ADI indexes) | local path or `tcp://` | the default — your existing `.dbf` data, unchanged |
| **SQLite** | `AdsConnect60("sqlite://…")` | a single-file SQL database, zero server |
| **PostgreSQL** | `postgresql://…` | a scalable, concurrent, server-class RDBMS |
| **MariaDB / MySQL** | `mariadb://…` | the MySQL ecosystem |
| **Microsoft SQL Server** | `mssql://…` | native TDS 7.4, optional TLS |
| **ODBC** | `odbc://…` | anything else with a driver |

Because the application only ever sees the ADS API, **you run on DBF
today and switch to PostgreSQL, MSSQL, or SQLite tomorrow if you
outgrow flat files — without rewriting your business logic.** The same
Harbour/Clipper/FiveWin code, the same `USE` / `dbSeek` / `tDatabase`
calls, now backed by a real database when you need concurrency, scale,
or central administration. OpenADS is a *scalable suite*, not a single
file format: keep your legacy investment, change the engine under it on
your own schedule. See [`docs/OPENADS_PLUS.md`](docs/OPENADS_PLUS.md)
and the per-backend cookbooks under [`cookbook/orm/`](cookbook/orm/).

### Independence, provenance, and trademarks

- **Independent implementation.** OpenADS is an independent
  open-source project. It is not affiliated with, sponsored by, or
  endorsed by SAP SE. "Advantage Database Server", "ADS", and any
  related marks, logos, and product names are the property of their
  respective owners and are referenced here solely to describe
  compatibility — their use does not imply any affiliation or
  endorsement.
- **No SAP-owned binaries required.** The OpenADS DLL is a
  drop-in replacement; running an application against
  `ace32.dll` / `ace64.dll` / `libace.so` produced by this project
  does **not** require any DLL, `.so`, or other binary owned by SAP.
  The only runtime dependencies are the host operating system's
  standard libraries (e.g. `KERNEL32.dll` and the Microsoft Visual
  C++ / Universal CRT runtime on Windows; `libc` / `libstdc++` on
  Linux).
- **Provenance — clean-room.** This codebase is written from
  publicly observable behavior of the original Advantage Client
  Engine and from the **public** Harbour `contrib/rddads` source
  (which is the call site OpenADS targets). It is **not** derived
  from leaked internal manuals or from disassembly / reverse
  engineering of SAP-owned binaries that would violate the
  Advantage SDK / ACE EULA. The implementation has been generated
  by an AI assistant (Anthropic Claude) under direct human
  supervision; every milestone is reviewed, tested, and committed
  by a human maintainer.
- **Purpose — non-commercial preservation.** OpenADS is a
  community-driven open-source project pursued **without economic
  benefit to its maintainers**. Its only goal is to provide a
  compatibility path for legacy applications affected by SAP's
  discontinuation of Advantage Database Server, so the existing
  Harbour / Clipper code base that depends on the ACE entry points
  can keep running. The project does not sell, license, sublicense,
  or otherwise monetise the software; redistribution is permitted
  by the Apache License 2.0 (see [`LICENSE`](LICENSE)) but the
  upstream maintainers receive no fee for the work.
- **No warranty, no support contract.** OpenADS is provided **AS
  IS**, without warranty of any kind, express or implied (see the
  Apache License 2.0 §7 and §8). There is no service-level
  agreement, no commercial support channel, and no representation
  of fitness for any particular purpose.
- **Downstream responsibility.** Users who deploy OpenADS as a
  drop-in replacement for the Advantage Client Engine remain solely
  responsible for ensuring that **their own** use of related
  third-party tooling (Harbour, Clipper, original Advantage
  installations, application code, fixtures) complies with whatever
  licenses, EULAs, or service agreements apply to those components.
  The project ships no SAP-owned binary and asserts no permission
  on behalf of any third-party rightsholder.

## Status

**Harbour rddtst.prg: 442 / 442 PASS (100%)** (2026-05-08). The
canonical Harbour RDD compatibility harness (`harbour/tests/rddtest/
rddtst.prg`, the same suite that DBFCDX is benchmarked against)
runs end-to-end through `contrib/rddads` linked to OpenADS'
`ace64.dll` with **zero failures** on Windows MSVC64. Validated
fixture (`tools/harbour_patch/adscl52_ads.prg`) is regenerated by
`rddmktst` against the ADS RDD itself, so the inlined expected
values reflect ADS-engine behaviour rather than DBFCDX's. For
context: native DBFCDX scores 369 / 442 against its own DBFCDX-
generated baseline; OpenADS clears every line of the regenerated
ADS-flavoured baseline. The session that closed the last gap is
recorded across 28 incremental commits ending at `28be1be`.

**Current release: [v1.8.4](https://github.com/FiveTechSoft/OpenADS/releases/tag/v1.8.4) (2026-07-09).**
Harbour `rddads` + FiveWin `TDataBase` / `xBrowse` over `tcp://` is
production-ready: remote scope (`OrdScope`), index navigation, key counts,
date fields, and field I/O by ordinal all work end-to-end. Full Data
Dictionary enforcement, Studio web console, SAP DD import, SQL backends
(PostgreSQL / MariaDB / MSSQL / ODBC / SQLite), and CDX bulk-load index
build are in production. `openads_serverd` serves the OpenADS wire
protocol; clients connect with
`AdsConnect60("tcp://host:port/path.add", ...)` and no application code
changes. Docs:
[English](https://fivetechsoft.github.io/OpenADS/en/) ·
[Español](https://fivetechsoft.github.io/OpenADS/es/) ·
[Português](https://fivetechsoft.github.io/OpenADS/pt/) —
including [migrating from ADS](https://fivetechsoft.github.io/OpenADS/en/migrating-from-ads/)
(CDX rollback / error 7017 caveat). `docs/wire-protocol.md` is the
formal spec for non-C++ clients (Python, Go, Rust, Harbour AEP).

Cross-platform CI is **green on all three runners**
(`ubuntu-24.04 / ninja-clang`, `macos-14 / default`,
`windows-2022 / msvc-x64`).

Release timeline:

| Tag       | Date       | Highlights |
|-----------|------------|-----------|
| **v1.8.2** | 2026-07-08 | **CI green on Linux/macOS** — NTXPL852 test fixture fixes Clang build; POSIX release ZIPs/tarballs ship again. Same engine as v1.8.1. |
| **v1.8.1** | 2026-07-08 | **`OrdScope` string key padding** — `setScopeTop`/`setScopeBottom` on character fields (work-order filters) honour scoped `GotoTop`/`Skip` on local and remote. |
| **v1.8.0** | 2026-07-08 | **NTXPL852 / PL852 OEM collation** — Polish CP-852 index sort (Ł between L and M); CDX bulk `REINDEX`; 19 new unit tests. |
| **v1.7.0** | 2026-07-08 | **REMOTE `AdsSetScope` / `OrdScope`** — `GotoTop`/`Skip` honour scoped key ranges over `tcp://` (Harbour labour-item / work-order filters). Docs: CDX rollback warning (SAP ACE error 7017). |
| **v1.6.5** | 2026-07-07 | **REMOTE xBrowse blockers** — `OrdKeyCount()` wire opcode `GetKeyCount`; `AdsGetDate()` no longer crashes when rddads passes a `RemoteIndex` handle on `ADS_DATE` columns. |
| **v1.6.4** | 2026-07-05 | **REMOTE FiveWin / TDataBase** — date `FieldGet`, ordinal-as-pointer safety, unlocked-write `EG_UNLOCKED` parity (`AE_RECORD_NOT_LOCKED`), lock forwarding on ABI handles. |
| **v1.6.3** | 2026-07-04 | **REMOTE xBrowse navigation** — `AdsKeyNo` logical position, bookmark `GotoRecord` ABI sync, `AdsAtBOF` at first key. |
| **v1.6.0** | 2026-07-01 | **REMOTE production CDX** — `AdsGetNumIndexes`, implicit GoTop after open, numeric `dbSeek` / aliased CDX tags, `abi_remote_prodcdx_test`. |
| **v1.5.2** | 2026-06-27 | Windows **x86 (32-bit) release ZIP** restored; Harbour smoke green in CI. |
| **v1.5.1** | 2026-06-27 | **Security & remote hardening** — path jail, nested unlock, TLS verify-by-default; remote `AdsSetRelation` / `AdsSetRecord` / `AdsCustomizeAOF`; SQLite + MSSQL navigational write. |
| **v1.4.0** | 2026-06-26 | **ADS dialect compat for ERP Harbour/FiveWin** — N-way comma join (3+ tables), `<alias>.*` wildcard, `UPPER()` in WHERE, table aliases, bracket free-table names, `{d}` temporal literals, constant folding. **CDX bulk-load index builder** (~10× faster `CREATE INDEX`), O(1) browse position cache, FOR-index persist+apply, flush-skip. **Wire: `FetchWhere`** server-side filtered scan. **Enterprise: sharded-reactor connection pool**, `EnterpriseConfig` singleton, session reaping + max-sessions. **SQL backends:** PostgreSQL column metadata via information_schema, stmt_map concurrency safety, SQLite WAL + busy-timeout, CREATE TABLE table type. **ADT:** correct companion stream count. xBase++ smoke test, FiveWin ORM cookbook. |
| **v1.2.2** | 2026-06-24 | CDX empty-leaf walk fix (skip holes after `erase`); CDX leaf recno bits sized from `max_rec` + prefix-seek fix; MSSQL backward SKIP off-by-one; ABI typed getters + `AdsGetIndexHandle` for SQL backends; NTX numeric-key edge-case tests; CDX empty-tree + prefix-seek edge tests. Full suite **738/738**. |
| **v1.2.1** | 2026-06-24 | NTX numeric key format fix — keys stored in native xBase zero-padded + complement form so native `dbSeek(<number>)` matches on-disk layout. adm_memo, codepage, maria_uri, postgres_uri, proc, sqlite_uri unit tests added. Remote benchmark docs (iMac WiFi 784K rec/s, SSH tunnel 676K rec/s). Suite 720/720. |
| **v1.2.0** | 2026-06-24 | **Deferred-flush bulk-insert (528× speedup)** — `AdsSetDeferredFlush` / `AdsFlushFileBuffers`; 500K records in ~26s locally, 0.69s over WiFi tcp:// (784K rec/s). **MSSQL native TDS 7.4 backend** via `mssql://` URI with optional mbedTLS encryption. Suite 649/649. |
| **v1.1.0** | 2026-06-23 | **SQL backends: PostgreSQL / MariaDB / ODBC** behind a pluggable `BackendTableOps` registry; `postgresql://` / `mariadb://` / `odbc://` URIs; navigation, field read, SEEK, write per-backend; 17-pointer ops vtable design. Suite 572/572. |
| **v1.0.4** | 2026-06-23 | CDX stale record-count refresh on the fetch path — shared-header-lock re-read before declaring a recno out of range, fixing spurious ADSCDX 5000 in multi-user append scenarios. |
| **v1.0.3** | 2026-06-23 | Round-trip-thrifty remote scan (PR #47) — sequential-prefetch path on `tcp://` wire; 50k-record loopback scan 3.9× faster (NAV-only). Cookbook expansion: SQL, native ADT, tcp:// remote, FiveWin xbrowse CRUD, ORM benchmark. |
| **v1.0.2** | 2026-06-23 | Responsive Studio web console — sidebar collapses to slide-in drawer on mobile; tabs scroll; dark-theme CSS variable fix. |
| **v1.0.1** | 2026-06-23 | `SKIP` honours `SET DELETED ON` in natural order. Native ADT/ADI create+read+write+seek (PR #41). POSIX platform hardening. macOS/clang build fixes. SQLite + stored-procedures docs. Cookbook (PR #44). |
| **post-rc29** | 2026-06-03 | **Full Data Dictionary engine enforcement + DA-Web management UI.** Triggers fire server-side (BEFORE/AFTER INSERT/UPDATE/DELETE; bodies loaded from binary `.add`/`.am`); RI rules enforced (Restrict/Cascade/SetNull); permissions enforced on `AdsOpenTable` (`GRANT`/`REVOKE` SQL persists to `.add` and takes effect immediately). Binary `.add` parser extended: User property-byte XOR group-membership tokens decoded alongside Permission-record memberships; trigger SQL bodies loaded from the `.am` continuation memo. `system.permissions` virtual table returns SELECT/UPDATE/INSERT/DELETE/EXECUTE/ALTER/DROP per (grantee, object) with correct 0/1/2 values; `system.effectivepermissions` merges direct + inherited group rights. `AdsDDGetPermissions` / `AdsDDSetUserTableRights` / `AdsDDGetUserTableRights` implemented. SAP ACE API name aliases forwarded. `system.functions` virtual table added. Auto-opens `.adi` index when `AdsOpenTable` opens an `.adt`. ADT RowVersion/ModTime field type support. **DA-Web**: full browser-based Data Architect replacement (tables, fields, indexes, triggers, stored procedures, functions, views, RI objects, users, groups, permissions, SQL editor with export); powered by the `php_openads` native Zend extension. |
| **v1.0.0-rc29** | 2026-05-26 | **Turnkey `hbmk2` (`.hbp`) example for Harbour apps — `examples/harbour-hbmk2/`.** Asked on the FiveTech forum: "alguna alma caritativa que proporcione un archivo de compilación `.hbp` para crear un programa con OpenADS — todos mis intentos han fracasado". The repo now ships a complete `hbmk2` project (x64 + x86 variants, Windows `build.cmd` + POSIX `build.sh` wrappers, `openads_demo.prg` console app that exercises `AdsConnect` → `DbCreate` → `INDEX ON UPPER(NAME)` → `dbSeek`) — drop in your `.prg`, set `OPENADS_LIB`, run `hbmk2`. The `.hbp` is short by design: only the two link entries that change for OpenADS (`-lrddads` + `-L${OPENADS_LIB} -lace64`). README and `docs/{en,es,pt}/getting-started.md` carry walkthroughs, including the typical "unresolved `AdsConnect60`" / "loaded the wrong `ace64.dll`" pitfalls. |
| **v1.0.0-rc28** | 2026-05-22 | **ADT / ADM support (M4 ADT) — read + write SAP Advantage native tables.** OpenADS can now open `.adt` files produced by the original Advantage Client Engine and write records back; `.adm` memo stores auto-attach when the table carries `Memo` or `Binary` fields. Full 13-type field vocabulary: CHAR, CICHAR (case-insensitive, maps to `ADS_CISTRING` = 25), LOGICAL, DATE (4-byte Julian Day Number), DOUBLE, INTEGER, SHORTINT, MEMO, BINARY, TIME, TIMESTAMP, AUTOINC, MONEY. ADM uses 256-byte fixed blocks with no per-block header; the 9-byte in-record reference (`block_no LE` + `data_len LE` + `0x00`) is resolved transparently by the engine. Record prefix: `0x04` = active / `0x05` = deleted, normalised to DBF convention on read and restored on write; null-bitmap bytes (1–4) are zeroed on `AppendBlank` rather than space-filled. `AdsCreateTable` with `ADS_ADT` still creates a DBF (ADT creation deferred); ADI index files are not yet implemented. Verified against `f:\pmsys\data\landlords.adt` (13 fields, 7 records, CICHAR key, ADM memo round-trip "SEC 8 preferred") via `tests/unit/abi_adt_smoke_test.cpp` (skipped on machines without the fixture). Suite 398 / 398. |
| **v1.0.0-rc27** | 2026-05-17 | **`AdsGetField` pads CHARACTER fields to the declared width.** Reported by Pritpal Bedi — a Harbour `mini_xbrowse /ads` truncated every text column (`Charlie`→`Charl`, `Barcelona`→`Barcel`) where native DBFCDX showed them full. `AdsGetField` returned CHAR values rtrim'd (`make_string` strips trailing spaces); DBF CHAR is fixed-width space-padded, so xbrowse auto-sized columns to the trimmed value and clipped the rest. `AdsGetField` now re-pads CHARACTER values to the declared field width on both the local and remote read paths; the engine's internal decode stays trimmed so SQL / index keys / AOF are untouched (verified `fieldlenprobe.prg` matches the DBFCDX baseline, `idxprobe.prg` index walk still `SORTED=YES`, suite 397/397). Also: `tools/harbour_patch/rddads-compat.patch` applies again (a dropped blank context line had made `git apply` reject the `rddads.h` hunk); PHP binding gains `Cursor::fetchAssoc/fetchNum` + `Table::seek`; the release workflow adds a macOS Intel (x64) build leg. |
| **v1.0.0-rc26** | 2026-05-16 | **PHP binding — `bindings/php`.** Asked by Reinaldo Crespo (the old proprietary Advantage PHP extension died around PHP 5.2 and was never modernised), OpenADS now ships `openads/openads-php` — a pure-PHP Composer package, no compiled C, that loads `ace64.dll` / `ace32.dll` / `libace*.so` through `ext-ffi` and wraps it in a modern OOP API (`Connection`, `Statement`, `Cursor`, `Table`, `Record`; PHP 8.1+). One code path covers local data-dir and `tcp://` / `tls://` remote — `AdsConnect60` dispatches on the URI. `Statement::query()` takes `?` / `:name` parameters; `ParameterBinder` substitutes them client-side with per-type quoting (ACE has no host-variable binding) as the anti-injection boundary. Pinned by 31 PHPUnit tests (21 unit + 10 integration vs a live engine) and a CI leg that builds ACE and runs the suite. Also fixes the SQL parser's `read_string_literal`, which decoded no escape — the ANSI doubled-quote escape (`'O''Brien'`) raised error 7200, breaking any SQL client inserting a string with an apostrophe; `''` now decodes to a single `'`. |
| **v1.0.0-rc25** | 2026-05-16 | Index correctness sweep. `AdsCreateIndex61` decoded the descending flag as `ulOptions & 0x08` — that bit is `ADS_COMPOUND`, which `rddads` and X#'s `ADSRDD` set on every CDX/NTX tag, so every order built descending (`AdsGotoTop` hit the last key, `SKIP` walked backward); now decoded with the named `ace.h` constants. `ALIAS->FIELD` index keys (`INDEX ON CUST->NAME`, passed as literal text) were unparseable — the tokenizer dropped `-`/`>` and every key evaluated blank; `strip_alias_qualifiers()` removes the `<ident>->` qualifier first. Not-positioned reads now return the SAP-canonical `AE_NO_CURRENT_RECORD` (5026) instead of generic 5000, so a Harbour `TBrowse` over `ADSCDX` no longer fails mid-paint with `ADSCDX/5000 table not positioned`. Verified: `idxprobe.prg` matches the DBFCDX baseline, full suite 395/395. |
| **v1.0.0-rc24** | 2026-05-16 | `AdsMg*` server-telemetry subsystem — the ~17 management functions, previously zero-fill stubs, now report real telemetry. A single `MgCollector` formats the SAP-canonical `ADS_MGMT_*` structs from a `MgSnapshot` that carries live counts, process RSS, listener port and cumulative `MgStats` (uptime, comm totals, high-water marks); local mode enumerates the in-process handle registry, remote mode ships a `MgRequest` frame (new `0xA0..0xA3` wire opcodes) to `openads_serverd`. Harbour `contrib/rddads/tests/manage.prg` reports real figures instead of all-zero (verified end-to-end against a live remote server; see `tests/smoke/harbour/manage_probe.prg` and the `openads_mgprobe` CLI). |
| **v1.0.0-rc15** | 2026-05-09 | Field classifier maps `'+'` (VFP autoincrement column) to `DbfFieldType::Integer`. Until this fix the dBASE Level 7 autoincrement type byte fell through to `Unknown`, so any DBF carrying a `'+'` column failed schema parsing even though the underlying storage is the 4-byte integer the engine already supports as `DbfFieldType::Integer`. One-line addition in `src/drivers/dbf_common.cpp::classify_field`. |
| **v1.0.0-rc14** | 2026-05-08 | **Run as a system service.** `openads_serverd.exe` doubles as a Windows Service — `--install-service [extra-flags…]` writes a SERVICE_AUTO_START registration with the SCM (binPath bakes the trailing flags), `sc start openads_serverd` boots it, `--uninstall-service` drops it, the SCM dispatcher path drives the same `g_running` graceful-stop flag interactive Ctrl+C uses. Bundled `tools/scripts/systemd/openads-serverd.service` (User=openads, ProtectSystem=strict, NoNewPrivileges, RestrictAddressFamilies, Restart=on-failure) and `tools/scripts/launchd/com.openads.serverd.plist` (KeepAlive on Crashed; logs to `/var/log/openads-serverd.{out,err}.log`) cover Linux + macOS. New "Running as a system service" section in the README walks through all three flavours. `main()` refactored: argv parsing → `parse_args()`, bind-and-loop → `run_server(args, console)`; SCM and interactive paths share one body. |
| **v1.0.0-rc13** | 2026-05-08 | **M-AOF.6 — production-CDX auto-open in `AdsOpenTable`.** rddads / ADS convention: opening `<base>.dbf` auto-binds the sibling `<base>.cdx` so every tag in it is navigable on the Table without an explicit `AdsOpenIndex60` call. Until rc13 OpenADS skipped this step, so Studio's per-request short-lived `AbiSession` re-opened the DBF on every `/rows` fetch but never picked up a CDX created in a prior session — `AdsGetAOFOptLevel` therefore reported `NONE` forever even after `CREATE INDEX`. Studio gains a one-click `▶ Demo` button that walks the entire AOF / Rushmore story end-to-end (peek row 1 → install AOF → trigger suggested CREATE INDEX → re-apply → land at FULL), plus per-field "💡 Create index on `<field>`" hint chips when the cond doesn't reach FULL. `openads_serverd --version` is now driven from `git describe --tags --always --dirty` so the binary actually reports its real release tag (the previous hard-coded `1.0.0-rc1` had drifted twelve releases behind reality). Bench TAG values switched to `AAAA`/`BBBB`/…/`ZZZZ` so a user typing `TAG='AAAA'` in Studio matches what the column actually stores. |
| **v1.0.0-rc12** | 2026-05-08 | M-AOF.5 — **sparse-bitmap `Skip` / `GoTop` (O(M) walk through the visible set)**. The AOF bitmap installed by `AdsSetAOF` is now flattened into a sorted `recno_sequence_` on the table; `Skip` / `GoTop` / `GoBottom` therefore O(1)-jump per step over only the matching records instead of decoding every recno asking the predicate. On the same 100k-row synthetic dataset, `aof_between_idx_walk` (3-tag visible set) drops from **321 ms** (rc11, OptLevel=FULL but O(N) walk) to **24 ms** — a **13.4× total speedup** vs the original full-scan baseline; the eq-walk case (~3.8% selectivity) is now bounded only by the per-visible-record `load_record_` decode cost (~80 µs × 3848 records = ~310 ms floor). `AdsClearAOF` drops both the predicate and the sequence in lockstep. Pinned by 367 / 367 tests, 43 424 / 43 424 assertions; nothing previously-tested broke. |
| **v1.0.0-rc11** | 2026-05-08 | M-AOF.1..4 — first working slice of **Advantage Optimized Filters (AOF) — Rushmore-style query optimisation**. `AdsSetAOF` now actually parses + evaluates the cond, installs a per-record bitmap as a filter predicate that `Skip` / `GoTop` honour, and routes individual leaves through CDX / NTX range scans whenever an open index's key expression matches the leaf's field. `AdsGetAOFOptLevel` reports `ADS_OPTIMIZED_FULL` / `PART` / `NONE` based on per-leaf coverage. V1 grammar: `<field> OP <literal>` (`= == != <> # < <= > >=`), `BETWEEN`, `IN`, `AND` / `OR` / `NOT`, parens; both Clipper-style (`.AND.`, `.T.`) and SQL-style keywords; index-accelerated leaves on character / memo fields with bare-field-name index expressions. `tools/bench/openads_bench` carries three AOF workloads — on a 100 000-row synthetic DBF, `AdsSetAOF("TAG='AAAA'")` is 1.83× faster with a TAG index installed (`OptLevel=FULL`) than without (`OptLevel=NONE`). Pinned by 367 / 367 tests, 43 424 / 43 424 assertions; nothing previously-tested broke. M-AOF.5 (sparse-bitmap Skip → O(M) walk through visible set) is the next step and is what unlocks the textbook 10-100× total speedup. |
| **v1.0.0-rc10** | 2026-05-08 | studio.web.0.17 — Studio's SPA header now carries a deployment-mode badge: 🏠 `LocalServer` (green) when the console is hosted in-process by `ace64.dll` / `ace32.dll` (the rc9 in-DLL Studio), 🌐 `Remote Server` (blue) when hosted by `openads_serverd`. Hover surfaces the active data directory. Driven by a new `mode` field on `/api/health` (`"localserver"` when the `HttpConsole` was started without a backing wire-server pointer, `"remote-server"` otherwise) so two side-by-side Studio tabs are trivial to tell apart at a glance. Failure path is silent — the badge stays hidden if `/api/health` is unreachable, so reverse-proxy deployments that strip unknown fields keep working. |
| **v1.0.0-rc9** | 2026-05-08 | Embedded Studio in `ace64.dll` / `ace32.dll`. Three new OpenADS-only entry points (`AdsStudioStart(port, data_dir)` / `AdsStudioStop()` / `AdsStudioPort(*port)`) plus an `OPENADS_STUDIO_PORT=<N>` (`OPENADS_STUDIO_DATA`, `OPENADS_STUDIO_HOST`) env-var auto-start hook driven from `DllMain` on Windows / a constructor attribute on POSIX. LocalServer apps (Harbour / X# / Clipper) loading the OpenADS DLL directly now get the same single-page Studio web console + REST surface in their own process — no `openads_serverd.exe` daemon required. The Studio HTTP target is built into the DLL only when `-DOPENADS_WITH_HTTP=ON`; without that flag the three entry points are still exported but return `AE_FUNCTION_NOT_AVAILABLE` so callers can detect the build flavour at runtime. Auto-start path is silent on bind failure so a host loading the DLL on a port-busy box doesn't crash; explicit `AdsStudioStart()` returns `AE_INTERNAL_ERROR` instead. Default bind host is `127.0.0.1` to keep the console off the LAN unless explicitly opted in. |
| **v1.0.0-rc8** | 2026-05-08 | XSharp-feedback round (Robert van der Hulst): release ZIP now bundles **both** `ace64.dll` (x64) and `ace32.dll` (x86) via the new `tools/scripts/build_release_windows.bat` two-arch driver; vendored `mbedtls 3.6.2` is forced **statically linked** (`USE_STATIC_MBEDTLS_LIBRARY=ON`, `BUILD_SHARED_LIBS=OFF`) so the shipped DLL / `openads_serverd.exe` has zero runtime libssl/libcrypto dependency; `openads_serverd --port <N>` already accepted, the bind-failure path now prints an explicit "port 6262 is the SAP Advantage Database Server default" hint when the operator hits a clash with a running ADS service; `tools/bench/README.md` documents that `openads_bench` synthesises its three-column DBF (`ID N(8,0)`, `TAG C(4)`, `AMT N(8,2)`) on every run from a fixed seed — no shipped fixture, fully reproducible. |
| **v1.0.0-rc7** | 2026-05-08 | Harbour `rddtst.prg`: 442 / 442 PASS via `contrib/rddads` against OpenADS `ace64.dll` (vs 369 / 442 for native DBFCDX). 28 commits in the session: rddads default-connection fallback, ORDSETFOCUS by CDX-insertion order, CDX FOR-clause filter + sibling-tag resync on CREATE INDEX, SET DELETED skip-deleted in goto/skip walks, AfterEndKey / Between cursor states, empty-key DBSEEK quirk, DOUBLEKEY field-width formatting, soft-seek-key-below-first → EoF, DESCEND walk-to-last-ASC, INDEX inserts deleted rows, key_size sub-tag-header rewrite on re-CREATE, plus the patched `tools/harbour_patch/rddads-compat.patch` (rddads-side fBof Limbo carve-out, bFindLast retry guarded by `u16Found`, ASC-only Brian-Hays trick) and the regenerated `tools/harbour_patch/adscl52_ads.prg` ADS-baseline fixture. |
| **v1.0.0-rc6** | 2026-05-07 | studio.web.0.16: schema view now renders single-letter dBASE field-type labels (C / N / D / L / M / B / P / I / T / @ / +) instead of the raw ADS_* numeric code; `/api/tables/<t>/schema` adds a `type_name` field alongside the existing `type` integer. |
| **v1.0.0-rc5** | 2026-05-07 | studio.web.0.15: Studio's `/api/tables/<t>/rows` endpoint no longer 500s on binary memo content — non-UTF-8 cells now return a `{"_b64": "<base64 preview>", "_size": N, "_truncated": bool}` object (1 KB cap) so multi-MB ZIP / image / encrypted blobs render cleanly; memo cells also stop being silently truncated at the previous 4 KB read buffer. Validated against the same 1.0 GB FPT used by `openads_memo_stress`. Includes Windows x86 (32-bit) `-Werror` fix in `tx_log_test.cpp` (uintmax_t → size_t cast). |
| **v1.0.0-rc4** | 2026-05-07 | M(cdx-split) full multi-level B+tree CDX leaf+branch split (closes the M3.5 single-leaf limitation); compact-leaf encoder gains entry/suffix overlap check; structure-tag leaf widens its recno field to 32 bits so multi-tag bags >64 KB no longer truncate sub-tag header offsets. **AdsGetField memo-read truncation fix** — read path no longer caps at 65 534 bytes, so memos and binary blobs (e.g. ZIP archives stored in M fields) round-trip bit-perfect at any size; pinned by `tests/unit/abi_memo_large_test.cpp` (200 KB) and exercised by `openads_memo_stress` (1.0 GB FPT, 12 000 rows × 2 fields × cycling lengths up to 256 KB, 0 mismatches). Cross-platform `-Werror` cleanup: builds clean on Windows MSVC, macOS AppleClang, and Ubuntu 24.04 GCC 13 simultaneously (346 / 346 unit tests pass on all three). New stress / verification tools: `openads_memo_stress`, `openads_memo_zip` (ZIP-in-memo round-trip with `python -m zipfile -t` validation), 200 K-row CDX multi-bag scenarios via `openads_stress`. SQL DDL `CREATE INDEX` defaults back to `.cdx`. |
| **v1.0.0-rc3** | 2026-05-06 | CI build timeout 45 → 60 minutes (mbedtls FetchContent on macOS). |
| **v0.3.6** | 2026-05-06 | M12.12 `tls://` URI reservation. |
| **v0.3.5** | 2026-05-06 | M12.10 ACE error-code propagation; M12.11 batched fetch. |
| **v0.3.4** | 2026-05-06 | M12.6 remote write; M12.7 remote SQL; M12.8 remote Reindex; M12.9 server auth; RemoteConnection dtor fix. |
| **v0.3.3** | 2026-05-06 | M12.3 server skeleton; M12.4 remote read; M12.5 dual-mode DLL; `openads_bench`; cross-platform `clang -Werror` clean-up. |
| **v0.3.0** | 2026-05-05 | 42 M10.x SQL milestones + 3 M11.x non-SQL (V/Q types, AEP host, OpenADS-encrypted DBF). Full Harbour-reachable `Ads*` surface MISS-free. |
| **v0.2.0** | 2026-05-04 | 27 M9.x milestones on top of 0.1.0; full `Ads*` ABI unlocked. |
| **v0.1.0** | earlier    | M0–M8.11 — local DBF / CDX / NTX / FPT / DBT engine. |

A real Harbour application, compiled against the standard
`contrib/rddads` static library, opens a DBF, walks records, runs
`dbSeek`, **swaps focus across multiple CDX tags**, **runs ARIES
transactions** with rollback semantics, **reads and writes memo
fields**, **creates tables / indexes dynamically**, **packs and
zaps tables**, **reindexes from a compound key expression**, and
reopens to verify durability — every call lands on OpenADS'
`ace64.dll` with no Harbour rebuild. See
`tests/smoke/harbour/README.md` for the captured output of every
M8.x / M9.x milestone.

```
$ smoke.exe
OpenADS smoke test
ACE DLL reports: 0.0a
Schema:
  1 NAME   C len=10 dec=0
  2 AGE    N len= 3 dec=0
  3 ACTIVE L len= 1 dec=0
  4 BORN   D len= 8 dec=0
  5 NOTES  M len=10 dec=0
Walk via UPPER_NAME (compound key)
Append "delta"  →  recno 4
Reopen + Seek 'DELTA' (upper) : Found=T RecNo=4 NAME=[delta]
Done.
```

### Quick start

Building OpenADS produces `ace64.dll` (or `ace32.dll` on x86; on
POSIX, `libace.so` / `libace.dylib`):

```sh
git clone https://github.com/FiveTechSoft/OpenADS
cd OpenADS
cmake --preset default
cmake --build build/default --config Release
ctest --test-dir build/default --output-on-failure -C Release
```

Drop the resulting `ace64.dll` (under
`build/default/src/Release/`) onto a Harbour app's `PATH` ahead of
any SAP-shipped copy and the standard `contrib/rddads` calls land
on OpenADS without recompiling Harbour.

### Build your own Harbour app against OpenADS (`hbmk2` / `.hbp`)

Asked on the FiveTech forum: "alguna alma caritativa que
proporcione un archivo de compilación `.hbp` para crear un
programa con OpenADS — todos mis intentos han fracasado".
[`examples/harbour-hbmk2/`](examples/harbour-hbmk2/) is a
turnkey [`hbmk2`](https://harbour.github.io/doc/hbmk2.html)
project: drop your `.prg` next to `openads_demo.hbp`, point
`OPENADS_LIB` at OpenADS' build output, run `hbmk2`. The produced
`.exe` drives DBF / CDX through Harbour's stock `contrib/rddads`
RDD — but every `Ads*` call lands on OpenADS' `ace64.dll`
instead of any SAP-shipped one.

```cmd
:: From a Visual Studio x64 Developer Command Prompt:
cd examples\harbour-hbmk2
set OPENADS_LIB=C:\OpenADS\build\default\src\Release
set PATH=C:\harbour\bin\win\msvc64;%OPENADS_LIB%;%PATH%
hbmk2 openads_demo.hbp
copy /y "%OPENADS_LIB%\ace64.dll" .
openads_demo.exe
```

The shipped `.hbp` is short by design — only the two link entries
that change for OpenADS:

```hbmk
openads_demo.prg
-comp=msvc64
-lrddads                    # Harbour's ADS RDD (contrib/rddads)
-L${OPENADS_LIB}
-lace64                     # OpenADS' import lib (instead of SAP's)
-lrddcdx
-lrddntx
-lrddfpt
```

A 32-bit variant (`openads_demo_x86.hbp` → `-lace32`) and POSIX
wrappers (`build.sh`) sit alongside the Windows `build.cmd`. The
README in that directory walks through the typical "unresolved
external symbol `AdsConnect60`" / "rddads.lib not found" /
"loaded the wrong `ace64.dll`" pitfalls.

For FiveWin (FWH) GUI apps `hbmk2` is not enough — see
[`examples/fivewin/`](examples/fivewin/) for `build_msvc64.cmd`
that mirrors FWH's stock build script with the two extra link
entries (`rddads.lib` + OpenADS' `ace64.lib`).

### Migrating from SAP ADS — DLL architecture

SAP Advantage Database Server splits its client-side runtime into
**two separate DLLs**:

| SAP DLL | Role |
|---|---|
| `ACE64.dll` | API surface — provides `AdsConnect60`, `AdsGetField`, etc. Acts as a thin dispatcher. |
| `AdsLoc64.dll` | The actual database engine (file I/O, SQL, indexes, transactions). Loaded into your process when the connection type is `ADS_LOCAL_SERVER`. |

When your application calls `AdsConnect60(..., ADS_LOCAL_SERVER, ...)`,
`ACE64.dll` loads `AdsLoc64.dll` into your process and the full
engine runs in-process. When you use `ADS_REMOTE_SERVER`, `ACE64.dll`
instead talks to the ADS server process over TCP and
`AdsLoc64.dll` is not involved at all.

**OpenADS collapses both roles into one DLL:**

| OpenADS DLL | Role |
|---|---|
| `ace64.dll` (also shipped as `openace64.dll`) | API surface **and** full database engine — handles both local and remote connections. |

- **Local path** (e.g. `C:\data\mydb.add`): the engine runs
  in-process, exactly as `AdsLoc64.dll` did under SAP.
- **Remote path** (e.g. `tcp://host:16262/data/mydb.add`): the DLL
  acts as a wire-protocol client and delegates work to the
  `openads_serverd` process, exactly as `ACE64.dll` did when talking
  to the ADS server.

There is no separate "local engine DLL" to deploy.

#### What this means for Harbour / Delphi / C++ / .NET applications

**Good news — your code does not change.** `AdsConnect60`,
`AdsGetField`, and the rest of the `Ads*` entry-point surface have
identical signatures. You replace `ACE64.dll` + `AdsLoc64.dll` with
OpenADS' single `ace64.dll` on your application's `PATH` and every
call lands on the new engine without recompiling.

| Language / framework | What to swap |
|---|---|
| **Harbour / rddads** | Put OpenADS `ace64.dll` ahead of any SAP copy on `PATH`; `contrib/rddads` links against `ace64.lib` (same name). |
| **Delphi** | Replace `ACE64.dll` in your deploy folder; your `LoadLibrary` / `GetProcAddress` calls or the Advantage Delphi Components resolve the same entry points. |
| **C / C++** | Link against OpenADS' `ace64.lib` import library (same exports as the SAP one); swap the runtime DLL. |
| **.NET (AdsDataProvider / X#)** | The managed driver calls into `ACE64.dll` via P/Invoke; dropping OpenADS' `ace64.dll` into the same directory is sufficient. |

The `ADS_LOCAL_SERVER` vs `ADS_REMOTE_SERVER` flag still matters —
it determines whether the engine runs in-process or routes to the
server daemon — but the connection string and every subsequent
`Ads*` call look exactly the same as before.

#### PHP — a special case

SAP's official PHP extension (`php_ads.dll`) stopped being compatible
with PHP after version 5.2, which reached end-of-life in 2011. It
was never updated for modern PHP and is unusable today.

A fully working, modern PHP extension was developed independently
by Reinaldo Crespo and is freely available at
[github.com/reinaldocrespo/php_advantage](https://github.com/reinaldocrespo/php_advantage).
This extension targets PHP 8.x (ZTS / NTS, x64), exposes the full
`Ads*` ABI surface including a 40-method `AdsDictionary` class, and
works against the **original SAP ACE64** runtime.

OpenADS ships its own PHP extension (`php_openads.dll`) built on
top of that same foundation. It loads `openace64.dll` instead of
SAP's `ACE64.dll` and is packaged as a native Zend extension
(`bindings/php_ext/`). One code path covers both local file paths
and `tcp://` / `tls://` remote connections. If you are on PHP 8.x
and migrating from SAP ADS, `php_openads.dll` + `openace64.dll`
is the supported replacement pair; the API surface it exposes is
compatible with what `php_advantage` exposes against SAP ACE64.

#### OEM character sets / national collations (Clipper CP-852 apps)

DOS-era Clipper / Harbour apps store national characters as OEM
code-page bytes (e.g. Polish CP-852) and open their tables with
`AdsSetCharType(ADS_OEM)`. Under SAP ADS the matching collation
language is a *machine-level* setting — the installer writes it into
`ADSLOCAL.CFG` — and any table opened OEM picks it up with zero code.
OpenADS mirrors that behaviour. Configure the default OEM collation
one of two ways:

```ini
; adslocal.cfg — same file name, format, and keyword SAP uses.
; Place it next to openace64.dll (or the exe that links OpenADS
; statically); the current directory is checked as a fallback.
[SETTINGS]
OEM_CHAR_SET=NTXPL852
```

```bat
:: or the environment variable (containers, CI, quick tests) —
:: it wins over adslocal.cfg when both are present:
set OPENADS_OEM_COLLATION=PL852
```

With either in place, every table opened with `usCharType = ADS_OEM`
(what rddads' `AdsSetCharType(ADS_OEM)` produces) builds, compares,
and seeks its CDX keys with the CP-852 sort weights, and `UPPER()`
inside index expressions cases Polish letters the same way Harbour's
`Upper()` does — so `DbSeek(Upper("...Ż..."))` finds its record.
`PL852` / `NTXPL852` name the same table; other SAP `OEM_CHAR_SET`
values (`USA`, `MAZOVIA`, …) are not implemented yet and leave the
default at raw byte order, exactly like SAP's shipping `USA` default
(see [issue #127](https://github.com/FiveTechSoft/OpenADS/issues/127)).
Tables opened ANSI (the default) are unaffected and keep UTF-8 case
promotion. `AdsSetCollation(hConn, "NTXPL852")` remains available as
an explicit per-connection override. If you indexed OEM data on
v1.8.6/v1.8.7 — where `INDEX ON` briefly built keys without CP-852
casing — `REINDEX` once after configuring the collation, the same
rebuild SAP requires after an OEM collation-language change.

### Remote TCP authentication

Protect remote connections by registering one or more server credentials:

```bat
openads_serverd --data C:\app\data --auth-user client:change-me
```

The equivalent `openads.ini` setting is repeatable: `auth_user = client:change-me`.
When configured, `AdsConnect60()` must provide matching credentials;
failures return `AE_LOGIN_FAILED` (`7077`). Use `tls://host:port/data` rather
than `tcp://` when credentials must be encrypted on the network.

### Studio web console (in-process, LocalServer mode)

OpenADS ships a single-page web admin console (Studio) that lists
the connection's tables, shows their schema, runs ad-hoc SQL, and
inspects records — including memo / binary fields. Studio comes in
two flavours:

- **Remote-server mode**: `openads_serverd.exe --http-port 8080
  --data c:\app\data` exposes Studio alongside the TCP wire server.
- **LocalServer mode**: the DLL itself spins up Studio in the
  caller's process, so a Harbour / X# / Clipper app loading
  `ace64.dll` (or `ace32.dll`) directly gets the same UI without
  launching any extra daemon.

Two ways to enable LocalServer Studio:

1. **Programmatic** — call the OpenADS-only extension entry points:

   ```c
   AdsStudioStart(8080, (UNSIGNED8*)"c:\\app\\data"); // bind 127.0.0.1:8080
   /* ... ShellExecute("http://localhost:8080") ... */
   AdsStudioStop();
   ```

   `AdsStudioPort(&port)` reports the bound port.
   `AdsStudioStart` returns `AE_SUCCESS` on success,
   `AE_INTERNAL_ERROR` if the bind / listen fails (e.g. another
   process already holds the port), or `AE_FUNCTION_NOT_AVAILABLE`
   if OpenADS was built without `-DOPENADS_WITH_HTTP=ON`.

2. **Environment-driven auto-start** — set
   `OPENADS_STUDIO_PORT=<port>` before launching the host app and
   the DLL boots Studio automatically when it loads:

   ```bat
   set OPENADS_STUDIO_PORT=8080
   set OPENADS_STUDIO_DATA=C:\app\data       :: default = "."
   set OPENADS_STUDIO_HOST=127.0.0.1         :: default = 127.0.0.1
   start MyApp.exe
   ```

   The auto-start hook runs from `DllMain DLL_PROCESS_ATTACH` on
   Windows and a constructor attribute on POSIX. Without
   `OPENADS_STUDIO_PORT` the hook is a no-op — the DLL will not
   bind any port unless the host explicitly asks for it.

Locking. Studio opens tables read-only via short-lived ABI
connections, so tables your app holds in EXCLUSIVE mode show up
as "table busy" in the browser until the app releases the lock;
shared opens coexist fine.

Default bind host is `127.0.0.1`, not `0.0.0.0` — the console is
local-only out of the box. Set `OPENADS_STUDIO_HOST=0.0.0.0` (or
pass an explicit host through a wrapper) when the deployment
genuinely needs LAN visibility, and pair it with
`openads_serverd --http-user user:pass` for HTTP Basic auth.

### Running as a system service

`openads_serverd` knows how to register itself as a Windows
Service and ships ready-to-drop unit files for the systemd /
launchd equivalents on Linux / macOS, so a typical
deployment never has to keep a console window open.

**Windows.** From an elevated cmd / PowerShell:

```bat
:: register the service (auto-start at boot). The flags after
:: --install-service are baked into the registered binPath:
openads_serverd.exe --install-service --port 6262 --http-port 6263 --data C:\app\data

:: start it via SCM
sc start openads_serverd

:: tail logs (the SCM redirects stdout to the Application event log)
:: or check the running state:
sc query openads_serverd

:: drop the registration when you no longer need it:
openads_serverd.exe --uninstall-service
```

The `--service` flag exists for the SCM dispatcher only; running
`openads_serverd.exe --service` interactively now prints an
explicit explanation rather than hanging.

**Linux (systemd).** Drop the bundled unit and enable:

```sh
sudo install -m 644 tools/scripts/systemd/openads-serverd.service \
    /etc/systemd/system/openads-serverd.service
sudo systemctl daemon-reload
sudo systemctl enable --now openads-serverd
sudo journalctl -u openads-serverd -f
```

The bundled unit drops privileges (`User=openads`,
`ProtectSystem=strict`, `NoNewPrivileges`, restricted address
families) and `Restart=on-failure`. Tweak the `--data` / `--port`
flags inside the unit to match your deployment.

`--data` accepts more than one directory, separated by `;`
(e.g. `--data "C:\app\data;D:\more-data"`), when the tables you want
to serve live under different drives or shares — a client's Connect
path only has to fall under one of the listed roots.

**macOS (launchd).** Drop the bundled plist into
`/Library/LaunchDaemons/` for a system-wide service or
`~/Library/LaunchAgents/` for a per-user one:

```sh
sudo install -m 644 tools/scripts/launchd/com.openads.serverd.plist \
    /Library/LaunchDaemons/com.openads.serverd.plist
sudo launchctl bootstrap system \
    /Library/LaunchDaemons/com.openads.serverd.plist
sudo launchctl kickstart -kp system/com.openads.serverd
tail -f /var/log/openads-serverd.{out,err}.log
```

Stop / unload via `sudo launchctl kill SIGTERM
system/com.openads.serverd` and `sudo launchctl bootout system
/Library/LaunchDaemons/com.openads.serverd.plist`.

### Error log — `ads_err.dbf`

> Full guide (error log vs transaction journal, ini/CLI/env, drive-root
> data dirs): [`docs/en/error-log.md`](docs/en/error-log.md).

OpenADS keeps a persistent error log in the same spirit as SAP ADS's
`ads_err` table: a plain **DBF table named `ads_err.dbf`** that any DBF
tool — or OpenADS itself — can open. Each row records the date, time,
error code, the subsystem that raised it (`SQL`, `NET`, `SERVER`), a
source line where applicable, and the detail text (for SQL failures,
the engine message plus the failing statement).

Entries are written for:

- **SQL statement failures** — every failing `AdsExecuteSQL(Direct)`
  call, with the error message and statement text;
- **errors returned to remote clients** — every error frame the TCP
  server sends back;
- **server lifecycle** — informational rows (code 0) at server start
  and stop.

**Where the file lives.** By default the SAP-conventional locations:
the root of `C:` on Windows, `/var/log/advantage` on Linux — falling
back automatically to a writable place (`%ProgramData%\OpenADS`,
`~/.openads`, then the temp directory) when the default isn't
writable, so unprivileged processes still get a log. Override the
location with any of:

| Mechanism | Setting |
|---|---|
| Environment variable | `OPENADS_ERROR_LOG_PATH=<dir>` (works for the DLL and the server) |
| `openads.ini` (serverd `--config`) | `error_log_path = <dir>` (alias `error_assert_logs`), `error_log_max = <KB>` |
| serverd command line | `--error-log-path <dir>`, `--error-log-max <KB>` |
| SQL, at runtime | `EXECUTE PROCEDURE sp_mgSetConfigValue('ERROR_ASSERT_LOGS', '<dir>')` / `('ERROR_LOG_MAX', '<KB>')` |

**Services announce themselves in the log.** `openads_serverd` writes
an informational row ("OpenADS server started on port N", code 0) the
moment it boots, and another on shutdown. A service installed without
any error-log override therefore creates the file at the default
location on first start — on Windows that means `C:\ads_err.dbf`
appearing right after `sc start openads_serverd` — which doubles as a
quick "is the service really up and able to write?" health check. To
move a service's log, bake `--error-log-path <dir>` (or
`--config <ini>`) into the service's binPath, or set the
`OPENADS_ERROR_LOG_PATH` environment variable for the service account.

**Size cap.** The file is limited to 1000 KB by default (configurable
as above). When an entry would push it past the cap, the oldest third
of the records is dropped and the table packed — the same rotation
behavior SAP documents for `ads_err`.

**Reading it.** Query the newest entries over any connection:

```sql
EXECUTE PROCEDURE sp_mgGetErrorLog();      -- newest 25
EXECUTE PROCEDURE sp_mgGetErrorLog(100);   -- newest 100
```

or simply open `ads_err.dbf` as a table. `sp_mgGetConfigInfo` (and the
`AdsMgGetConfigInfo` API) report the active `Error Log Path` and
`Error Log Max`. The SAP ADT variant (`ads_err.adt` with an
`Error_Number` autoinc column) is not implemented — the DBF variant is
the documented alternative and is what OpenADS writes.

### DA-Web — browser-based Data Architect replacement

`DA-Web/` is a PHP 8.x web application that manages OpenADS data
dictionaries through a browser. It is the primary management UI and
covers everything the original SAP Data Architect desktop tool did,
plus SQL query access:

- **Tables** — browse schema, create / drop, edit field structure,
  view / edit records with full type support (including Memo, Binary,
  DateTime, Money).
- **Indexes** — list tags, create / drop CDX and NTX indexes.
- **Views** — list and edit stored SQL view definitions.
- **Stored procedures + functions** — list, view body, execute.
- **Triggers** — list BEFORE / AFTER INSERT / UPDATE / DELETE triggers
  with body text from the binary `.am` continuation memo.
- **RI (referential integrity)** — list parent/child relationships with
  constraint type (Restrict / Cascade / SetNull).
- **Users + groups** — create / delete users and groups, manage
  group memberships, set / change passwords.
- **Permissions** — grant / revoke SELECT / INSERT / UPDATE / DELETE /
  EXECUTE / ALTER / DROP per table per user or group.
- **SQL editor** — ad-hoc SQL with result export (CSV).

DA-Web is powered by `php_openads.dll` — the native Zend extension
in `bindings/php_ext/`. It connects to `openads_serverd` via a
`tcp://` URI so the web server and the database server can be on
different hosts.

### openads_import_dd — SAP `.add` migration tool

**`AdsConnect60` rejects SAP binary `.add` files.** If you point an
OpenADS connection at a `.add` file created by the original SAP
Advantage Client Engine, `AdsConnect60` returns error code
`AE_SAP_PERMS_NEED_IMPORT` (5174) with the message:

```
This is a SAP Advantage Data Dictionary in proprietary binary format.
OpenADS cannot open it directly.
Run: import_dd <source.add> <dest.add>
to convert it to OpenADS format, then connect to the converted file.
```

The engine can *read* the SAP binary format (via `DataDict::load_add_binary_()`)
but not write back to it safely — the format is closed and per-user permission
fields are stored in per-database encrypted blobs. The solution is a one-time
import to the OpenADS-native format.

`tools/import_dd/openads_import_dd` reads a SAP-format binary `.add`
file and emits an OpenADS-native `.add` (plus an optional SQL script
with `GRANT` / `REVOKE` statements). Use it to migrate a SAP Data
Dictionary to OpenADS without touching the underlying `.dbf` / `.adt`
data files:

```
openads_import_dd --input pmsys.add --output pmsys_openads.add [--sql pmsys_grants.sql]
```

The importer decodes the binary User / Group / Table / RI / Trigger /
Procedure sections from the SAP format, including the group-membership
XOR tokens and permission bitmasks. SAP-encrypted permission blobs
(per-table DML differentiation within a group) are approximated as
full-access grants with a warning comment.

After import, connect to the new file:

```c
AdsConnect60((UNSIGNED8*)"pmsys_openads.add", ADS_LOCAL_SERVER,
             (UNSIGNED8*)"AdsSysAdmin", (UNSIGNED8*)"", 0, &hConn);
```

The original SAP `.add` is never modified by the import tool.

### adsbackup — backup & restore

`tools/adsbackup` builds the `adsbackup` command-line utility: back up
and restore a data dictionary (the `.add` plus every bound table and
its index/memo companions) or a directory of free tables, with the
same positional arguments and option letters as SAP's `adsbackup.exe`
so existing scheduled-backup scripts carry over:

```
adsbackup c:\data\motors\motors.add c:\data\motors\backup
adsbackup -r c:\data\motors\backup\motors.add c:\data\motors\restore\motors.add
adsbackup /srv/data "*.dbf" /srv/backup/nightly        # Linux free tables
```

The CLI and the `sp_BackupDatabase` / `sp_BackupFreeTables` /
`sp_RestoreDatabase` / `sp_RestoreFreeTables` system procedures share
one backup engine (`src/engine/backup.*`), so both entry points honor
the same Include/Exclude/MetaOnly/DontOverwrite options and behave
identically. Full usage, the option table, and the differences from
SAP's record-level online backup are documented in
[`tools/adsbackup/README.md`](tools/adsbackup/README.md).

### AOF — Rushmore-style query optimisation

OpenADS ships a working AOF (Advantage Optimized Filters) layer
modelled on the Rushmore query-optimisation technique used by
FoxPro and the original SAP Advantage Database Server. The idea
is simple: instead of walking every record evaluating the filter
expression against decoded field values, OpenADS builds a
per-record bitmap once, with each `<field> OP <literal>` leaf
served by an existing CDX / NTX index range scan when one is
available, and Skip / GoTop only visit records whose bit is set.

Minimal Harbour-flavoured C example:

```c
ADSHANDLE hT, hIdx;
UNSIGNED8 dbf[]  = "people.dbf";
UNSIGNED8 cdx[]  = "people.cdx";
UNSIGNED8 nm[]   = "LASTNAME_IDX";
UNSIGNED8 expr[] = "LASTNAME";
UNSIGNED8 cond[] = "LASTNAME = 'SMITH'";

AdsOpenTable(hConn, dbf, NULL, ADS_CDX, ADS_ANSI, 0, 0, 0, &hT);

/* Build an index on the field used by the AOF leaf. Without it, */
/* AdsSetAOF still works correctly (full-scan bitmap path) but no */
/* range-scan speedup; AdsGetAOFOptLevel will report NONE.       */
AdsCreateIndex61(hT, cdx, nm, expr, NULL, NULL, 0, 0, &hIdx);

/* Install the AOF. Skip / GoTop now walk only matching records. */
AdsSetAOF(hT, cond, 0);

UNSIGNED16 lvl = 0;
AdsGetAOFOptLevel(hT, &lvl, NULL, NULL);
/* lvl == ADS_OPTIMIZED_FULL when every leaf served by an index, */
/* PART when some leaves served + others scanned, NONE otherwise. */

AdsGotoTop(hT);
UNSIGNED16 eof = 0;
while (AdsAtEOF(hT, &eof) == 0 && eof == 0) {
    /* ... process visible record ... */
    AdsSkip(hT, 1);
}

AdsClearAOF(hT);
AdsCloseTable(hT);
```

V1 grammar (the AOF cond passed to `AdsSetAOF`):

```
<field> OP <literal>      OP in { = == != <> # < <= > >= }
<field> BETWEEN a AND b
<field> IN ( v1, v2, ... )
expr AND expr             also `.AND.` (Clipper)
expr OR  expr             also `.OR.`
NOT expr                  also `.NOT.` and `!`
( expr )
```

Identifiers and keywords are case-insensitive; both Clipper-style
(`.T.`, `.AND.`) and SQL-style keywords are accepted because
rddads forwards either depending on the call site. Anything
outside the documented grammar (function calls, arithmetic, LIKE)
returns `AE_PARSE_ERROR` so the caller can fall back to a manual
walk; OpenADS does not silently swallow an unsupported expression.

Index-accelerated leaves in V1 cover character / memo fields with
a bare-field-name index expression. Numeric / date / logical
fields, and indexes whose expression is `UPPER(field)` /
`DTOC(field)` / a compound expression, still produce a correct
bitmap via the per-record fallback — they just don't count as
"served by index" in the OptLevel report.

Measured speedup (`tools/bench/openads_bench`, 100 000 synthetic
rows, Windows MSVC x64 Release, M-AOF.5):

| AOF workload                                              | med (ms) | OptLevel | Speedup |
|-----------------------------------------------------------|---------:|----------|--------:|
| `AdsSetAOF("TAG='AAAA'")`, no TAG index                    |     586  | NONE     |   1.0×  |
| `AdsSetAOF("TAG='AAAA'")`, TAG indexed                     |     323  | FULL     |   1.83× |
| `AdsSetAOF("TAG BETWEEN 'AAAA' AND 'CCCC'")`, TAG indexed  |      24  | FULL     |  24.4×  |

Two things drive the speedup:

1. `AdsSetAOF` itself becomes a range scan over the matching index
   instead of a full record decode + AST eval per row. That alone
   is the rc11 (M-AOF.4) gain.
2. `Skip` / `GoTop` walk only the matching records (sparse-bitmap
   navigation, M-AOF.5) instead of iterating every recno asking
   the predicate. That's the rc12 gain that flips the textbook
   Rushmore window on for selective filters.

The eq-walk case at 1.83× is now bounded by the per-visible-record
`load_record_` decode cost (~80 µs × 3848 matching records ≈ 310
ms floor); applications that don't touch the matched data — for
example `COUNT(*)` over the AOF set, or `dbSeek`-style point
lookups — hit the full Rushmore speedup window on top of that.

### Performance — cross-platform SQL bench

`tools/bench/openads_bench` generates a synthetic 100 000-row DBF
(`ID N(8,0)`, `TAG C(4)`, `AMT N(8,2)`) and times a fixed set of
SQL workloads through the public ABI (`AdsExecuteSQLDirect`).
Median of 5 repeats per workload, all-Release builds:

| Workload (median ms)   | Windows MSVC | Linux clang -O3 | macOS AppleClang |
|------------------------|-------------:|----------------:|-----------------:|
| stage 100 k-row DBF    |        63.5  |          57.9   |           34.0   |
| `SELECT COUNT(*)`      |       297.7  |          42.0   |          103.9   |
| `WHERE TAG = 'AAAA'`   |       303.7  |          48.3   |          108.4   |
| `SUM/AVG/MIN/MAX(AMT)` |       374.3  |         120.5   |          136.1   |
| `GROUP BY TAG`         |       321.9  |          58.6   |          120.9   |
| `ORDER BY AMT LIMIT 10`|       668.0  |         165.4   |          260.5   |
| `DISTINCT TAG`         |       598.4  |          95.2   |          213.4   |
| `BETWEEN 100 AND 500`  |       314.1  |          63.7   |          114.4   |

Same source tree, same workload, three OS / toolchain combinations.
Linux clang -O3 wins every SQL workload — roughly 7× faster than
MSVC Release on the full-table count, 4× on the heavier `ORDER BY`.
macOS Intel sits between the two. Run it on your own hardware:

```sh
cmake --build build/default --target openads_bench
./build/default/tools/bench/openads_bench --rows 100000 --repeats 5 --csv
```

A working **TCP server (`tools/serverd/openads_serverd`) + dual-mode
client (`tcp://host:port/dir` URI in `AdsConnect60`)** is already in
the tree (M12.x, Phase 2). Cross-host smoke: 13-op session
(`Hello → Connect → OpenTable → GetRecordCount → GotoTop →
GetField → Skip → Disconnect`) round-trips end-to-end through a
Windows-side client → remote Linux server over an SSH-forwarded
TCP channel; ~9 ms server-side per op, the rest is real WAN RTT.

### What works today (v1.4.0)

#### Engine

- **DBF read/write** — C / N / L / D / M columns, positional and
  by-name field access, `APPEND BLANK`, per-field assignment,
  deletion flag, durable flush, dynamic table creation
  (`AdsCreateTable` parses rddads' `NAME,Type,Len,Dec;…` field-def
  syntax).
- **ADT read/write** — SAP Advantage native `.adt` tables and `.adm`
  memo stores produced by Advantage Client Engine are byte-compatible
  with OpenADS. All 13 ADT field types are supported: CHAR, CICHAR
  (maps to `ADS_CISTRING`), LOGICAL, DATE (Julian Day Number), DOUBLE,
  INTEGER, SHORTINT, MEMO, BINARY, TIME, TIMESTAMP, AUTOINC, MONEY.
  ADM memos (256-byte fixed blocks) auto-attach when a table carries
  Memo or Binary fields; the 9-byte in-record reference is resolved
  transparently. `AdsCreateTable(ADS_ADT)` creates a native `.adt`
  file with correct field descriptors; a companion `.adm` is created
  automatically when Memo or Binary fields are present.
  **Not yet supported:** SAP proprietary ADT encryption.
- **DBF maintenance** — `AdsZapTable` empties a DBF + clears every
  bound index in lockstep; `AdsPackTable` compacts deleted records
  out of the DBF (Clipper semantics: leaves indexes stale, caller
  follows up with `AdsReindex`); `AdsReindex` rebuilds every bound
  index from the current records using each tag's expression.
- **CDX index** — full FoxPro compound layout (file header at offset
  0 holding the structure tag, sub-tag headers, per-tag B+tree),
  multi-tag-per-file API (`add_tag` / `open_named` / `list_tags`),
  Harbour-equivalent compact-leaf bit-pack (`bBits` derived from key
  length, mirroring `hb_cdxPageLeafInitSpace`), full multi-level
  B+tree leaf+branch split with separator promotion and new-root
  creation (M(cdx-split), 0.3.7 — stress-tested at 200 000 rows ×
  2 tags in one bag), branch descent (BE child pointers), `dbSeek`
  exact + soft, `dbGoTop` / `dbSkip` walks, auto-sync on every
  mutation across **all** bound tags (active + parked extras),
  dynamic creation via `AdsCreateIndex61`. Bulk-load index builder
  (`build_bulk`) for ~10× faster `CREATE INDEX` on large tables.
  O(1) browse position cache for `AdsGetRelKeyPos` / `AdsGetKeyNum`.
  FOR-index predicates persist in the CDX sub-tag header and apply
  at insert time (only matching records are indexed).
- **NTX index** — Clipper layout, multi-level B+tree split (M9.10
  closed the M3.7 single-level limitation), cache-based in-order
  traversal for `next` / `prev` over multi-level trees, dynamic
  creation via `AdsCreateIndex61`, multi-file binding (M9.14 — apps
  can bundle several `.ntx` files on a single `USE` and swap focus
  between them without losing the parked tags' write sync).
- **ADI index** — SAP proprietary B+tree index format paired with
  `.adt` tables. `AdsOpenTable` auto-opens the `.adi` sidecar; every
  ADT mutation auto-syncs through the ADI index alongside CDX / NTX.
  Full read+write: seek, insert, erase, flush, leaf page splits with
  sibling-link maintenance, branch promotion (`promote_split_`), and
  fixed-root copy-up splits. Both char-key and numeric-key variants.
- **Compound key expressions** — `UPPER(field)`, `LOWER(field)`,
  `LTRIM` / `RTRIM` / `ALLTRIM`, `STR(n)` / `STR(n,len)` /
  `STR(n,len,dec)`, `DTOS(date)`, `SUBSTR(s,start[,len])`,
  string concatenation with `+`. `UPPER` / `LOWER` / `SUBSTR` walk
  UTF-8 codepoints (ASCII + Latin-1 supplement case map, including
  `ÿ↔Ÿ`); the evaluator runs at index sync time, so an
  `INDEX ON UPPER(name)` tag normalises every key as the app writes
  records, including non-ASCII rows produced by `AdsSetStringW`.
- **Full-text search** — `AdsCreateFTSIndex` writes a clean-room
  `# OpenADS FTS v0` text file (sorted token + recno-list per row);
  `AdsFTSSearch` and the SQL `CONTAINS(<col>, '<query>')` predicate
  intersect per-token recno lists with AND semantics. Tokeniser
  honours `ulMinWordLen` / `ulMaxWordLen`, custom delimiter +
  noise-word arrays, and an English stop-word seed.
- **WAL recovery** — append-only log with CRC-32C records,
  group-commit primitive (`sync_to(lsn)`), per-record LSN, idempotent
  recovery via the `openads.lsnmap` sidecar.
- **Transactions (TPS)** — `AdsBeginTransaction` / `AdsCommitTransaction`
  / `AdsRollbackTransaction`, in-memory ordered op log + named
  savepoints, multi-table commit, rollback marks appended records as
  deleted (Clipper convention) and writes back before-images for
  modified rows.
- **AES-128 / AES-256 ECB primitive** — vendored tiny-AES-c (Unlicense),
  validated against FIPS-197 + NIST SP 800-38A. Used as a direct block
  cipher and as the keystream generator for the AES-256-CTR per-record
  table encryption added in M11.2 (DBF only, OpenADS-specific format).
- **Memo (DBT / FPT)** — read + write round-trip;
  `Connection::open_table` auto-attaches the right memo store based on
  the DBF signature (`0x03` → no memo, `0x30` → CDX with FPT memo).
  FPT blocks carry an explicit type tag (Text / Picture / Object), so
  the same field can hold either text memos or `ADS_BINARY` /
  `ADS_IMAGE` payloads with embedded NULs.
- **Data Dictionary** — Full read/write support for the **OpenADS-native**
  `.add` format. `Connection::open(<.add>)` resolves member tables on every
  `AdsOpenTable`. The engine enforces server-side: **triggers** (BEFORE /
  AFTER INSERT / UPDATE / DELETE dispatch; bodies loaded from the `.add` / `.am`
  pair), **referential integrity** (Restrict, Cascade, SetNull on child INSERT /
  parent DELETE-UPDATE), and **permissions** (GRANT/REVOKE; effective rights OR
  group memberships).
  Virtual system tables: `system.tables`, `system.columns`, `system.indexes`,
  `system.views`, `system.storedprocedures`, `system.functions`,
  `system.triggers`, `system.relations`, `system.links`,
  `system.usergroupmembers`, `system.usergroups`, `system.permissions`,
  `system.effectivepermissions`.
  SQL DDL: `GRANT`/`REVOKE` single or multiple rights, `CREATE DATABASE`,
  `CREATE TABLE`/`INDEX`/`TRIGGER`/`PROCEDURE`/`FUNCTION`.
  Group memberships decoded from both Permission records (SAP ACE v8+) and
  User property-byte XOR tokens (pre-v8 format); both sources unioned.
  **SAP binary `.add` files are not supported as live connection targets** —
  `AdsConnect60` returns error `AE_SAP_PERMS_NEED_IMPORT` (5174) with a
  message directing the caller to run `import_dd` first (see
  `openads_import_dd` below). The binary format can be *read* by the import
  tool (using `DataDict::load_add_binary_()`) but writing back to it is
  unsafe because the format is closed and per-user permission fields are
  encrypted; OpenADS uses its own fully-documented format for all new work.
  **DA-Web** — a companion web administration tool (separate from OpenADS
  core) that manages data dictionaries through a browser: browse tables,
  views, stored procedures, functions, triggers, RI objects, users, groups;
  execute ad-hoc SQL; edit table structure; generate DDL scripts. Powered by
  the `php_openads` native PHP extension over the OpenADS TCP wire protocol.
- **Locking** — OS byte-range locks with the same ranges as the
  original ACE so installs can coexist during migration. Lock
  acquires are non-blocking (`LockFileEx LOCKFILE_FAIL_IMMEDIATELY`
  on Windows, `fcntl F_SETLK` on POSIX) and re-attempt up to a
  configurable retry budget (`AdsSetLockCycle` /
  `AdsSetLockRetryCount`, defaults 100 ms / 10 retries).

#### ABI

- **231 `Ads*` exports** — every entry point Harbour
  `c:\harbour\lib\win\msvc64\rddads.lib` references is resolvable
  through OpenADS' DLL. Real implementations for the full engine +
  DD surface; the remainder are **local-mode silent-success** (the
  `AdsMg*` server-management surface, and the `Cache*` / `Set*` /
  `Refresh*` / `Customize*` Harbour-side preferences — all return
  `AE_SUCCESS` and either zero-fill the caller's struct or report
  empty / quiescent state). `AdsDD*` DD CRUD functions have real
  implementations: `AdsDDCreate`, `AdsDDAddTable`, `AdsDDAddIndex`,
  `AdsDDAddTrigger`, `AdsDDAddRelation`, `AdsDDAddUser`,
  `AdsDDAddUserToGroup`, `AdsDDRemoveUser`, `AdsDDGetPermissions`,
  `AdsDDSetUserTableRights`, `AdsDDGetUserTableRights`, and more.
  **No exports hard-fail with `AE_FUNCTION_NOT_AVAILABLE` at the
  function level.** Specific add-only branches (e.g.
  `AdsRestructureTable`'s `pucChangeFields` type-conversion) still
  surface that error code at the argument level with a comment.
  SAP ACE API name aliases are forwarded to the OpenADS equivalents
  so legacy code calling the old names still resolves correctly.
- **6 legacy CRT shims** — `_dclass`, `_dsign`, `_wfsopen`, `_getch`,
  `_kbhit`, `_eof` re-exported from `ace64.dll` so apps built against
  Harbour's prebuilt MSVC2013-era libs link without rebuilding
  Harbour itself.

#### SQL

- **DML** — `INSERT INTO <t> (cols) VALUES (lits)`, `UPDATE <t> SET
  col=lit … [WHERE …]`, `DELETE FROM <t> [WHERE …]`, `GRANT <rights>
  ON <obj> TO <principal>`, `REVOKE <rights> ON <obj> FROM
  <principal>` (comma-separated rights list), `CREATE DATABASE`,
  `CREATE TABLE`, `CREATE INDEX`, `CREATE TRIGGER`, `CREATE PROCEDURE`,
  `CREATE FUNCTION`.
- **SELECT** — full projection lists; `WHERE` with boolean tree
  (`AND` / `OR` / `NOT` / parens) over infix comparisons, `IN (…)`,
  `BETWEEN`, `EXISTS`, scalar subqueries, `CONTAINS(<col>,'query')`;
  `ORDER BY <col> [ASC|DESC]`; `GROUP BY … [HAVING …]`; `DISTINCT`;
  `LIMIT` / `TOP`; `INNER` / `LEFT` / `RIGHT` / `FULL` JOIN; `UNION`
  / `UNION ALL`; aggregates `COUNT` / `SUM` / `AVG` / `MIN` / `MAX`;
  window functions `ROW_NUMBER()` / `RANK()` / `DENSE_RANK()` over
  `PARTITION BY … ORDER BY …`.
- **Virtual system tables** — `system.tables`, `system.columns`,
  `system.indexes`, `system.views`, `system.storedprocedures`,
  `system.functions`, `system.triggers`, `system.relations`,
  `system.links`, `system.usergroupmembers`, `system.usergroups`,
  `system.permissions`, `system.effectivepermissions`.
- **DD SQL** — `GRANT`/`REVOKE` write Permission records to the `.add`
  file and mirror into the live `permissions_` map so enforcement takes
  effect immediately without reopening the DD.

#### Tests

- **517 doctest cases** passing on Windows / MSVC Release (cross-platform
  CI green on Windows MSVC, Ubuntu 24.04 clang, macOS AppleClang).
- **Harbour smoke** harness (`smoke.exe`) drives the full read + write +
  index + multi-tag + transaction + memo + compound-expression path
  through `rddads.lib` and OpenADS' `ace64.dll`.
- **Permissions unit tests** (`tests/unit/abi_dd_perms_test.cpp`) cover
  no-ACL open, level 0 block, level 1 read-only, level 2 write, group
  inheritance, check_rights=0 bypass, bitmask bit positions, and the
  `AdsDDSetUserTableRights` / `AdsDDGetUserTableRights` round-trip.
- **Tools** — `tests/tools/sap_perm_probe.cpp` (calls `AdsDDGetPermissions`
  via SAP ACE64 to probe real bitmasks) and `tests/tools/dump_perm_records.cpp`
  (dumps raw info2 + property blobs from any binary `.add` file).

## Roadmap

OpenADS ships in three rough phases. Each row links to the milestone
tag that lands the work; partial milestones become `done` once their
follow-ups merge.

### 0.1.x — drop-in for the Harbour read/write path (DONE)

Validated against `c:\harbour\contrib\rddads.lib` end-to-end through
`tests/smoke/harbour/smoke.prg`.

| Tag | Milestone |
|-----|-----------|
| `m0-done`        | Project scaffolding, build, doctest harness |
| `m1-done`        | ABI handle registry + minimal C entry points |
| `m2-done`        | DBF reader (header / fields / records) |
| `m3-done`        | CDX + NTX index drivers (M3 baseline) |
| `m3.5-done`      | CDX leaf bit-pack matches FoxPro on disk |
| `m3.6..3.10`     | Reviewer-flagged compat fixes; CDX compound layout; CDX multi-tag API; NTX cache traversal |
| `m4-partial`     | DBF write path + memo (DBT / FPT) + AES-128/256 |
| `m5..5.5`        | WAL with savepoints, group commit, idempotent recovery via `openads.lsnmap` |
| `m6-partial`     | Data Dictionary `.add` alias resolution |
| `m7.x-partial`   | Minimal SQL (`SELECT * FROM ... [WHERE ...]`) |
| `m8.0..8.2`      | DLL build (`ace64.dll`/`ace32.dll`); rddads link validation; first end-to-end smoke (`AdsVersion`) |
| `m8.3`           | Harbour walks a real DBF |
| `m8.4`           | ACE field-type constants verified empirically |
| `m8.5`           | Multi-field DBF (C/N/L/D) end-to-end |
| `m8.6`           | `dbSeek` through CDX |
| `m8.7..8.8`      | Write path (`dbAppend` + `FIELD-> :=`); active index auto-sync |
| `m8.9`           | Multi-tag CDX with `OrdSetFocus` |
| `m8.10`          | Transactions: `Begin/Commit/Rollback` |
| `m8.11`          | Memo M-fields (FPT) round-trip |
| **`0.1.0`**      | **Final 0.1.0 release** |

### 0.2.x — broaden the ABI surface, polish the engine (DONE — 0.2.0 released)

| Tag | Milestone |
|-----|-----------|
| `m9.1-done`      | Compound CDX expressions evaluator (`UPPER`, `STR`, concat, ...) |
| `m9.2-done`      | Stub batch reorganised into real / no-op / missing |
| `m9.3-done`      | Compound expressions validated end-to-end via Harbour |
| `m9.4-done`      | `AdsGotoRecord` + table/file metadata real impls |
| `m9.5-done`      | `AdsCreateTable` (rddads field-def parser → DBF on disk) |
| `m9.6-done`      | `AdsRefreshRecord` + `AdsExtractKey` |
| `m9.7-done`      | `AdsCreateIndex61` with compound-expression support |
| `m9.8-done`      | `AdsZapTable` + `AdsPackTable` |
| `m9.9-done`      | `AdsReindex` — rebuild every bound index from current records |
| `m9.10-done`     | NTX multi-level B+tree split (closes M3.7 limit) |
| `m9.11-done`     | `AdsCopyTable` / `AdsCopyTableContents` / `AdsConvertTable` |
| `m9.12-done`     | `AdsFindFirstTable` / `AdsFindNextTable` / `AdsFindClose` (`*` / `?` glob, case-insensitive, returns `AE_NO_FILE_FOUND` when exhausted) |
| `m9.13-done`     | `AdsGetBinaryLength` / `AdsGetBinary` / `AdsSetBinary` + real `AdsGetMemoDataType` (FPT block-type tag round-trip; `ADS_BINARY` → `Object`, `ADS_IMAGE` → `Picture`, text → `Text`; offset-based chunked reads) |
| `m9.14-done`     | NTX multi-tag binding — multiple `.ntx` files coexist on one Table (`AdsOpenIndex` / `AdsCreateIndex61` / legacy `AdsCreateIndex` are all additive; same-path reopen refreshes; `AdsCloseIndex` releases extra views without disturbing the active order) |
| `m9.15-done`     | Real `AdsGetServerName` / `AdsGetServerTime` — local host name + ISO date / `HH:MM:SS` time + ms-of-day, plus the 6-arg `AdsGetServerTime` shape rddads' `ADSGETSERVERTIME` actually expects (the previous 2-arg stub left rddads' on-stack date/time bufs uninitialised). Also fixes a latent index-binding leak: `AdsCloseTable` / `AdsCloseAllTables` / `AdsDisconnect` now purge the global binding map so a future Table allocation at the same heap address can't inherit stale entries. |
| `m9.16-done`     | Chunked `AdsSetBinary` — per-`(table, field)` accumulator lets rddads deliver an oversized `ADS_BINARY` / `ADS_IMAGE` payload across several calls (`ulOffset != 0`, `ulBytes < ulTotalBytes`); the field only lands in the memo store once every byte arrives. Mid-write chunks that would run past the announced total fail; pending state is dropped at table teardown. |
| `m9.17-done`     | Unicode `*W` variants — `AdsSetStringW` / `AdsGetStringW` / `AdsGetFieldW`. UTF-16LE ↔ UTF-8 transcoding at the ABI boundary; field names accept both UTF-16 NUL-terminated strings and `ADSFIELD(n)`-style numeric indices (low-pointer encoded). Engine continues to store byte sequences without a fixed codepage assumption. |
| `m9.18-done`     | Lock retry / cycle policy — `AdsSetLockCycle` / `AdsGetLockCycle` / `AdsSetLockRetryCount` / `AdsGetLockRetryCount` (ms between attempts + retry count, defaults 100 ms / 10 retries). `AdsLockTable` / `AdsLockRecord` switched to non-blocking byte-range acquires (`LockMgr::try_lock_*` / `LockFileEx LOCKFILE_FAIL_IMMEDIATELY` / `fcntl F_SETLK`) and re-attempt up to the configured budget before reporting `AE_LOCKED`. |
| `m9.19-done`     | `AdsCreateFTSIndex` — clean-room OpenADS-native `.fts` inverted-index file (UTF-8 text: `# OpenADS FTS v0` header + sorted `<token>\t<recno1>,<recno2>,...` rows). Tokeniser respects `ulMinWordLen` / `ulMaxWordLen`, custom delimiter / noise-word arrays, plus a default ASCII delimiter set and an English stop-word seed. Search-side functions remain a follow-up milestone. |
| `m9.20-done`     | `AdsAddCustomKey` / `AdsDeleteCustomKey` — manual-mode (key, recno) injection on the current record. Each call evaluates the index's expression against the positioned row and routes through the existing `IIndex::insert` / `IIndex::erase` paths, matching rddads' `DBOI_KEYADD` / `DBOI_KEYDELETE` call sites. |
| `m9.21-done`     | FTS search side — `AdsFTSSearch(hConn, pucFile, pucQuery, paRecnos, *pulCount)` loads the `.fts` file, tokenises the query, intersects per-token recno lists (AND semantics), and writes the hit list into `paRecnos` with truncation reporting. SQL gains a `CONTAINS(<col>, '<query>')` predicate that lowers to a precomputed recno set captured in the row predicate, so the FTS lookup runs once per query instead of per row. |
| `m9.22-done`     | UTF-8 codepoint-aware index-expression evaluator — `UPPER`, `LOWER`, `SUBSTR` walk codepoints instead of bytes. ASCII + Latin-1 supplement (incl. `ÿ↔Ÿ`) case map cleanly; codepoints outside that range pass through. `INDEX ON UPPER(name)` over a UTF-8 column now produces stable keys for non-ASCII rows. Bare-field indexes still byte-identical (existing CDX / NTX files round-trip unchanged). |
| `m9.23-done`     | Misc MISS fillers — real `AdsGetLongLong`, `AdsSetFieldRaw`, `AdsVerifySQL`, `AdsFailedTransactionRecovery`, `AdsGetAllLocks`, `AdsSkipUnique`. |
| `m9.24-done`     | `AdsMg*` server-telemetry subsystem (~17 calls). Real management telemetry — a single `MgCollector` formats the SAP-canonical `ADS_MGMT_*` structs from a `MgSnapshot` carrying live counts (connections, work areas, tables, users, worker threads), process RSS, listener port and cumulative `MgStats` (uptime, comm packet totals, high-water marks). Local mode enumerates the in-process handle registry; remote mode ships a `MgRequest` wire frame (`0xA0..0xA3` opcodes) to `openads_serverd`. Harbour `contrib/rddads/tests/manage.prg` reports real figures instead of the all-zero stub output. |
| `m9.25-done`     | Local-mode `AdsDD*` CRUD surface (14 calls). All 14 accept silently / zero-fill. 0.3.x will replace these no-ops with real persistence in the OpenADS DD format. |
| `m9.26-done`     | `AdsRestructureTable` (ADD-fields path) — rewrites the DBF with the original schema + `pucAddFields` appended, copies every record's old-field bytes verbatim and blank-pads the new fields. The MISS list is now empty. |
| `m9.27-done`     | CI matrix portability fix — `legacy_crt_shims.cpp` is now a no-op on POSIX builds; the engine compiles cleanly on Linux + macOS through `.github/workflows/ci.yml` (windows-2022 / ubuntu-22.04 / macos-13). |
| **`v0.2.0`**     | **Final 0.2.0 release** (2026-05-04) |

#### Post-0.2.0 polish (rolled into 0.3.x)

- **Linux Harbour smoke install.** The engine builds + tests cleanly
  on Linux + macOS through the CI matrix; what's still missing is a
  Linux Harbour install so the smoke harness (`smoke.exe` →
  `ace64.dll`) can run end-to-end on a non-Windows runner.
- **`usPageSize` honoured beyond NTX/CDX.** `AdsCreateIndex61` /
  `AdsCreateFTSIndex` accept a `usPageSize` argument. NTX (1024)
  and FoxPro CDX (512) are fixed-size by their on-disk format —
  matching the behaviour of the original ACE — so OpenADS records
  the value but doesn't change the layout. Variable page sizes for
  ADI are a future milestone; the ADI B+tree driver itself is complete.

### 0.3.x — proprietary formats + advanced SQL (IN PROGRESS)

OpenADS will only adopt the proprietary formats once a clean-room
compatibility specification is available — written from publicly
observable behaviour and from the Harbour `contrib/rddads` source,
not from disassembly of SAP-owned binaries or from any material
whose use is restricted by the Advantage SDK / ACE EULA.

| Tag | Milestone |
|-----|-----------|
| `m10.1-done` | Real OpenADS-native DD persistence — replaces M9.25 silent-success no-ops with round-trip storage in the v1 text format (USER / MEMBER / LINK / INDEX / RI / DBPROP / USERPROP rows); `Connection::open(<.add>)` callers now see CRUD writes survive across reopens. |
| `m10.2-done` | VFP `I` (4-byte int32) / `Y` (8-byte currency, money * 10000) / `B` (8-byte IEEE-754 double) field decode + encode round-trip. |
| `m10.3-done` | SQL `WHERE` — full boolean expression tree (`OR` / `NOT` / parens) on top of the M9.21 `CONTAINS` predicate; numeric literals (`AGE >= 18`) and string literals coexist. |
| `m10.4-done` | `AdsRestructureTable` DELETE-fields path + real `AdsSetIndexDirection` (active order walks reverse on demand). The function-level Ads* surface is now MISS-free; CHANGE-fields still deferred. |
| `m10.5-done` | SQL `INSERT INTO <t> (cols) VALUES (lits)` — string + numeric literals; AdsExecuteSQLDirect dispatches by leading keyword. |
| `m10.6-done` | SQL `ORDER BY <col> [ASC|DESC]` — materialises matching recnos via the WHERE filter, sorts by the column's typed value (numeric vs string driven by field type), installs the result as the cursor's traversal sequence. |
| `m10.7-done` | SQL `UPDATE <t> SET col=lit, … [WHERE …]` + `DELETE FROM <t> [WHERE …]` — bulk row mutation through AdsExecuteSQLDirect. DELETE follows Clipper convention (rows marked, not removed; AdsPackTable evicts later). |
| `m10.8-done` | SQL projection lists — `SELECT col1, col2 FROM …`; cursor reports only the projected columns (in the listed order) via AdsGetNumFields / AdsGetFieldName / AdsGetFieldType / AdsGetFieldLength / AdsGetFieldDecimals / ADSFIELD(n). |
| `m10.9-done`  | SQL DDL — `CREATE TABLE <name> (<col> <Type> [(<len>[,<dec>])], …)` + `CREATE INDEX <tag> ON <table> (<expr>) [DESCENDING] [UNIQUE]`. Both lower into the engine's existing AdsCreateTable / AdsCreateIndex61 entry points. |
| `m10.10-done` | SQL aggregates — `COUNT(*) / COUNT(col) / SUM / AVG / MIN / MAX(col)`. The cursor is a synthetic 1-row temp DBF (one C(30) column per aggregate). |
| `m10.11-done` | VFP autoinc fields — descriptor offsets 18 (flags) / 19-22 (counter LE) / 23 (step). `AppendRecord` pre-fills the field with the current counter and persists `next = counter + step` back to disk. Counter survives close + reopen. |
| `m10.12-done` | `AdsRestructureTable` CHANGE-fields — same-type length / decimals overrides for existing columns. Type conversion still surfaces `AE_FUNCTION_NOT_AVAILABLE` (clean-room policy). |
| `m10.13-done` | SQL `INNER JOIN <b> ON <l_col> = <r_col>` — parser. |
| `m10.14-done` | SQL `INNER JOIN` executor — hash on right column, materialise merged rows into temp DBF cursor; right-side fields prefixed `R_`. Combos with WHERE / ORDER BY / aggregates over the joined cursor land in a follow-up. |
| `m10.15-done` | SQL `<col> IN (…)` — literal list `IN ('a', 'b', …)` and subquery `IN (SELECT col FROM t)`. Matching set captured at compile time so per-row check is O(1). |
| `m10.16-done` | SQL `LEFT [OUTER] JOIN` — left rows survive without a right-side match (right fields blank-pad); bare `JOIN` keyword treated as INNER per SQL convention. |
| `m10.17-done` | SQL `EXISTS (SELECT … FROM t)` — uncorrelated presence test, materialised once at compile time. `NOT EXISTS` falls out of the existing NOT-tree. |
| `m10.18-done` | SQL scalar subquery — `<col> op (SELECT <col> FROM <t>)`. Inner subquery's first projected value lands in the cmp's literal slot at compile time; outer-column type drives string vs numeric compare semantics. |
| `m10.19-done` | SQL aggregate scalar subquery — `<col> op (SELECT MAX(x)/MIN/SUM/AVG/COUNT FROM t)`. The single aggregate lands in the cmp's number slot at compile time. |
| `m10.20-done` | SQL JOIN combined with WHERE / ORDER BY in a single statement. Outer clauses compile against the merged cursor's schema (left names verbatim, right names `R_<orig>`). |
| `m10.21-done` | SQL `RIGHT [OUTER] JOIN` — every right row survives, blank left when no match. Hash built on LEFT column; merged cursor schema stays direction-agnostic. |
| `m10.22-done` | SQL `FULL [OUTER] JOIN` — union of LEFT + RIGHT semantics; left walk emits matched + LEFT-style fillers and tracks which right recnos were matched, then a follow-up scan emits unmatched right rows with a blank left filler. |
| `m10.23-done` | SQL JOIN + aggregate combo — `COUNT/SUM/AVG/MIN/MAX` over the merged JOIN cursor; aggregate walk runs after the M10.20 outer WHERE, materialises a 1-row temp DBF with C(30) per aggregate. |
| `m10.24-done` | SQL correlated EXISTS subquery — `EXISTS (SELECT … FROM b WHERE b.x = a.y)` re-evaluates per outer row; outer-column refs in the subquery WHERE bind to the live outer cursor. |
| `m10.25-done` | SQL `GROUP BY <col>[, <col>…] [HAVING <agg> op num]` — multi-row aggregate cursor (one row per group), preserves source column type byte + length, HAVING filters on a projection-aligned aggregate slot. |
| `m10.26-done` | SQL `UNION` / `UNION ALL` — N-way merge of `SELECT * FROM t [WHERE ...]` members; first member's schema drives the merged cursor; UNION dedups by raw record bytes, UNION ALL keeps duplicates. |
| `m10.27-done` | SQL UNION + member projection — UNION members may now carry a projection list (`SELECT col1, col2 FROM t`); first member's projection drives the merged DBF schema (preserving raw_type / length / decimals) and subsequent members align positionally. |
| `m10.28-done` | SQL UNION + ORDER BY — last UNION member may carry an `ORDER BY` clause; the sort applies to the merged result. Numeric vs lex compare follows the merged column type. |
| `m10.29-done` | SQL correlated scalar subquery — `<col> op (SELECT <col_or_agg> FROM b WHERE b.x = a.y)` re-evaluates per outer row, binding outer columns into the inner WHERE (M10.24 binding pattern reused). |
| `m10.30-done` | SQL HAVING tree — boolean AND / OR / NOT / parens over aggregate comparisons; e.g. `HAVING COUNT(*) > 1 AND SUM(amt) > 50`. |
| `m10.31-done` | SQL `DISTINCT` — `SELECT DISTINCT [cols\|*] FROM ...` dedups by projected columns; first occurrence wins, applied post-WHERE / post-ORDER-BY. |
| `m10.32-done` | SQL `LIMIT N [OFFSET M]` — slices the post-clause traversal sequence (skip M, take N). |
| `m10.33-done` | SQL `BETWEEN` / `LIKE` — `<col> BETWEEN <lit1> AND <lit2>` (inclusive, numeric or lexicographic) and `<col> LIKE '<pattern>'` with SQL `%` (any sequence) and `_` (single char) wildcards. |
| `m10.34-done` | SQL GROUP BY across JOIN — `SELECT <agg>(...) FROM a JOIN b ON ... GROUP BY ...` produces one row per group of joined rows, mirroring the M10.25 grouped path on the merged cursor. |
| `m10.35-done` | SQL correlated IN subquery — `<col> IN (SELECT col FROM b WHERE b.x = a.y)` re-evaluates per outer row when the inner WHERE references an outer column. |
| `m10.37-done` | SQL multi-column ORDER BY — `ORDER BY a [ASC\|DESC], b [ASC\|DESC] ...` cascades on ties; per-column direction independent; numeric vs lex driven by column type. |
| `m11.1-done`  | VFP V (Varchar) + Q (Varbinary) field types — decode trims trailing NUL pad to recover the meaningful prefix; encode NUL-pads the unused tail so reads recover the original length. |
| `m11.3-done`  | TPS — `AdsReleaseSavepoint` drops a savepoint marker without rolling back; nested `AdsBeginTransaction` / `AdsCommitTransaction` increment / decrement depth (only the outermost commit flushes); inner ROLLBACK still aborts every nesting level. |
| `m10.38-done` | SQL `CASE WHEN <cond> THEN <lit> [WHEN ...] [ELSE <lit>] END [AS alias]` in projection — first matching branch wins; result lands in a C(30) cell of the materialised result cursor. Conditions may use Cmp / AND / OR / NOT / BETWEEN / LIKE. |
| `m10.36-done` | SQL UNION + complex members — UNION members may now carry JOIN, GROUP BY, aggregates, CASE, DISTINCT, LIMIT. Dispatcher recurses into AdsExecuteSQLDirect per member (state mutex is recursive); each member's cursor schema drives merge alignment via `cursor_projections`. |
| `m11.4-done`  | AEP host — `CREATE PROCEDURE <name> AS '<dll>::<sym>'` registers an external function on a connection; `EXECUTE PROCEDURE <name>(args)` calls it through `platform/dll` (LoadLibrary / dlopen) and returns its result string in a 1-row C(255) `RESULT` cursor. Clean-room ABI: `int proc(const char* args, char* out_buf, size_t out_cap)`; not byte-compatible with the legacy ADS Extended Procedure protocol. |
| `m11.2-done`  | OpenADS-encrypted DBF (AES-256-CTR) — header byte 0xC3 marks an OpenADS-encrypted variant; record bodies XOR with an AES-256-CTR keystream keyed off the connection password (`AdsSetEncryptionPassword`). `AdsEncryptTable` upgrades a plain DBF in place. Schema stays plaintext. **Not byte-compatible** with proprietary SAP ADS encrypted .adt files — that format's byte boundary remains undocumented. |
| `m10.39-done` | SQL scalar string fns — `UPPER` / `LOWER` / `LEN` / `TRIM` / `LTRIM` / `RTRIM` over a single column in projection, optional `AS alias`. Materialised result cell is C(N). |
| `m10.40-done` | SQL arithmetic in projection — `<col> {+,-,*,/} <num_or_col>` evaluated as doubles, formatted with `%g` into a C(30) cell. |
| `m10.41-done` | SQL `INSERT INTO t (cols) SELECT ...` — inner SELECT may use any engine clause; rows copy positionally into the target via the existing append path. |
| `m10.42-done` | SQL `CREATE TABLE t AS SELECT ...` — derives the new table's schema from the inner cursor's projection, creates the DBF, bulk-copies rows. |
| `m10.43-done` | SQL multi-arg scalar fns — `SUBSTR(col, start, [len])`, `CONCAT(a, b, ...)`, `REPLACE(col, old, new)`. Each arg may be column / string / numeric literal. |
| `m10.45-done` | SQL date arithmetic on YYYYMMDD strings — `DATEDIFF(a, b)` (days), `DATEADD(col, n)` (returns YYYYMMDD). |
| `m10.46-done` | SQL derived tables — `FROM (SELECT ...) [AS alias]`. Inner SELECT runs through the recursive dispatcher; outer WHERE / ORDER BY / projection apply on top of its cursor. |
| `m10.48-done` | SQL Common Table Expression — `WITH name AS (SELECT ...) SELECT ... FROM name`. Rewritten in parse_select to a derived-table form by inline-substituting `name` with `(<inner>)`. |
| `m10.44-done` | SQL `<col> IS [NOT] NULL` — first checks the VFP NULL bitmap (M11.6); falls back to a trim-trailing-space check that treats all-blank cells as NULL for non-bitmap-bearing DBFs. |
| `m10.47-done` | SQL `ROW_NUMBER() OVER (...)` in projection — first cut emits the 1-based position in the materialised result cursor; PARTITION BY / ORDER BY inside the OVER are parsed but ignored. |
| `m11.6-done`  | VFP NULL bitmap — `parse_dbf_fields` detects flags-byte bit 1 + assigns each nullable field a `null_bit` ordinal; `Table::is_field_null` reads `_NullFlags` system column and tests the bit. |
| `m12.1-done`  | Phase 2 wire-protocol skeleton — `network/wire.{h,cpp}` defines a length-prefixed frame (4 BE bytes + opcode + payload) with an Opcode enum reserved for Hello / Connect / OpenTable / ExecuteSQL / Fetch / Disconnect / Error. No socket layer yet. |
| `m11.7-done`  | String collation — `AdsSetCollation(hConn, BINARY\|NOCASE)`. Connection-scoped flag stamps onto every WHERE Cmp; NOCASE lowercases ASCII A-Z before compare across Eq/Ne/Lt/Gt/Le/Ge/Between/Like. |
| `m11.8-done`  | OEM (CP437) ↔ UTF-8 conversion — `AdsConvertOemToAnsi` / `AdsConvertAnsiToOem` round-trip text through a new `engine/codepage` module carrying the public IBM CP437 upper-half table. |
| `m10.49-done` | SQL window OVER (PARTITION BY + ORDER BY) — pre-computes per-row values into a side map; partitions group by concatenated key, ORDER BY drives within-partition order. |
| `m10.50-done` | SQL `RANK()` / `DENSE_RANK()` — join ROW_NUMBER in the window family; RANK skips on ties, DENSE_RANK doesn't. Tie detection via the ORDER BY key. |
| `m10.51-done` | SQL qualified column refs — `<alias>.<col>` in WHERE / projection / JOIN ON; read_identifier consumes the suffix and resolves the bare column name. |
| `m10.52-done` | SQL multi-row `INSERT INTO t (cols) VALUES (...), (...), ...` — appends one record per tuple. |
| `m10.53-done` | SQL `NULLIF(a,b)` / `COALESCE(a,b,...)` / `IFNULL(expr,default)` — null-handling helpers in projection (empty string = NULL by convention). |
| `m10.54-done` | SQL aggregate `<agg>(...) FILTER (WHERE <expr>)` — per-slot row filter; CountStar with FILTER uses the filtered count, others always do. |
| `m4-adt-done` | **ADT / ADM read+write** — `AdtDriver` (`.adt`) + `AdmMemo` (`.adm`) implement the full IDriver / IMemoStore interfaces. All 13 ADT field type codes supported (CHAR, CICHAR, LOGICAL, DATE/JDN, DOUBLE, INTEGER, SHORTINT, MEMO, BINARY, TIME, TIMESTAMP, AUTOINC, MONEY). ADM auto-attaches on `AdsOpenTable` when Memo/Binary fields are present; `Connection::open_table` extends the existing `has_memo_field` scan to include Binary. `AdsCreateTable(ADS_ADT)` creates a native `.adt` file with correct field descriptors; companion `.adm` auto-created for Memo/Binary fields. |
| `m12.2-done`  | Phase 2 TCP socket layer — `network/socket.{h,_win32.cpp,_posix.cpp}`. listen / accept / connect / send / recv / close + ephemeral-port binding. Win32 links ws2_32. |
| `m12.3-done`  | Phase 2 server skeleton — `network/server` spawns an accept thread + per-connection session thread. Dispatches Hello → HelloAck (version), Connect → ConnectAck (data_dir echo), Disconnect → close, others → Error frame. recv_exact / read_frame / write_frame helpers exposed for M12.4 / M12.5. |
| `m12.4-done`  | Phase 2 remote read-only table ops — server proxies OpenTable / GotoTop / Skip / GetField / GetRecordCount / AtEOF / CloseTable. Per-session engine::Connection + 32-bit table-id map. |
| `m12.5-done`  | Phase 2 dual-mode DLL — `AdsConnect60("tcp://host:port/<dir>", ...)` opens a wire client; AdsOpenTable / AdsGotoTop / AdsSkip / AdsGetField / AdsGetRecordCount / AdsAtEOF / AdsCloseTable / AdsDisconnect each branch on remote-handle kind. ace64.dll is now drop-in for either local or TCP-server mode. |
| `m12.6-done`  | Phase 2 remote write surface — server proxies AppendBlank / SetField / DeleteRecord / RecallRecord / GotoRecord / FlushTable; AdsAppendRecord / AdsSetString / AdsDeleteRecord / AdsRecallRecord / AdsGotoRecord / AdsWriteRecord each gate a remote branch. SetField wire payload is `[u32 tid][u16 name_len][name][value]`; engine stores via `Table::set_field(idx, std::string)`. Round-trip verified by reopening the same DBF locally and confirming the new record + value hit disk. |
| `m12.7-done`  | Phase 2 remote SQL exec — `AdsCreateSQLStatement` on a remote connection allocates a wire-aware statement; `AdsExecuteSQLDirect` ships the SQL through a new `ExecuteSQL`/`ExecuteSQLAck` opcode pair. Server lazily opens a parallel ABI connection on first use, runs the query through `AdsExecuteSQLDirect`, and wraps the returned cursor handle in `cursor_tbls` so the M12.4 read ops (`GotoTop` / `Skip` / `GetField` / `GetRecordCount` / `AtEOF` / `CloseTable`) serve it through the same wire. Returns cursor table-id (0 = no cursor for INSERT / UPDATE / DELETE / DDL). |
| `m12.8-done`  | Phase 2 remote `AdsReindex` — new `Reindex`/`ReindexAck` opcode pair routes through `engine::Table::reindex()` on the per-session connection. `CREATE INDEX` is already covered by M12.7's `ExecuteSQL` DDL path. |
| `m12.9-done`  | Phase 2 server credential auth — `Connect` frame payload extended to `[u16 dlen][dir][u16 ulen][user][u16 plen][password]`; `Server::add_credential` registers a user/password pair; empty map = back-compat dev mode (any client passes). `AdsConnect60` forwards `pucUser` / `pucPwd` through the wire. RemoteConnection now self-disconnects on drop so a failed connect doesn't leave the server-side session blocked in `read_frame()`. |
| `m12.10-done` | Phase 2 ACE error-code propagation — `Error` frame payload now starts with `[u32 LE ace_code][message]`; server folds either a literal ACE code or a code pulled from a sub-result's `util::Error` into every Error frame. Client parses the prefix back into `util::Error.code` so the `Ads*` return value surfaces the real ACE code (`AE_LOGIN_FAILED` 7077, `AE_NO_FILE_FOUND` 5018, `AE_PARSE_ERROR` 7200, …) instead of the legacy generic `AE_INTERNAL_ERROR` 5000. |
| `m12.11-done` | Phase 2 batch row fetch — `Fetch`/`FetchAck` opcode pair (0x32 / 0x33, reserved in M12.1). Request: `[u32 tid][u32 max_rows][u8 ncols][per col: u8 nlen, name]`. Reply: `[u32 nrows][u8 ncols][per row, per col: u16 vlen, val]`. Walks up to `max_rows` from the cursor's current position; works for both engine handles (returned by `OpenTable`) and SQL-cursor handles (returned by `ExecuteSQL`). `RemoteConnection::fetch_batch` exposes the surface as `vector<vector<string>>`; collapses per-row `Skip`+`GetField` round-trip count by N for WAN-latency callers. |
| `m12.12-done` | Phase 2 real TLS — vendored mbedtls 3.6 LTS (Apache 2.0) via `OPENADS_WITH_TLS` CMake option (OFF by default). When enabled, `AdsConnect60("tls://host:port/<dir>", ...)` opens a real `MbedTlsTransport` instead of returning AE_FUNCTION_NOT_AVAILABLE. `connect_tls(host, port, TlsConfig)` returns a `unique_ptr<ITransport>` ready for `read_frame` / `write_frame`. Server-side TLS termination is queued for v1.0.x (mbedtls 3.6 doesn't expose a supported way to adopt an externally-accepted fd; deployments today should front the server with a TLS proxy if needed). |
| `m12.13-done` | Phase 2 transport abstraction — `network/transport.h` defines a polymorphic `ITransport` surface (`send` / `recv` / `close` / `valid`); `PlainTransport` is the only concrete impl today. `read_frame` / `write_frame` carry both `Socket&` and `ITransport&` overloads so existing call sites stay one-liner while the v0.4.0 `TlsTransport` plugs in without touching the server / client business logic. |
| docs (M12)    | `docs/wire-protocol.md` — formal spec of every opcode + payload format, error-frame layout, transport notes, and versioning policy. Reference for non-C++ clients (Python, Go, Rust, Harbour AEP) that want to talk to an OpenADS server without reading the C++ source. |
| `m-adi-rw-done` | **ADI B+tree read+write** — full `insert` / `erase` / `flush` path on `.adi` sidecar indexes; leaf page splits with sibling-link maintenance; `promote_split_` propagates splits up the branch level with recursive branch splits and fixed-root copy-up; char-key and numeric-key variants. `AdsOpenTable` continues to auto-attach the sidecar; mutations sync through the ADI B+tree alongside CDX / NTX auto-sync. |

#### Still planned for 0.3.x

- **VFP** NULL-bitmap extension — the `0x32` autoinc / null-flag
  header byte stays deferred. Autoinc (M10.11) and V / Q types
  (M11.1) are real today.
- **ADT encryption** — SAP's proprietary per-record encryption for
  `.adt` files is not implemented. OpenADS' own AES-256-CTR encryption
  (M11.2) applies to DBF files only.

### 1.0.x — TCP server (Phase 2) — DONE

`openads_serverd` is in production. The M12.x milestones above shipped
the complete Phase 2 scope:

- Wire protocol over TCP (or TLS with `tls://`); formal spec at
  `docs/wire-protocol.md`.
- `AdsConnect60("tcp://host:port/path.add", user, pwd)` drops in with
  zero application changes.
- Remote read/write, SQL exec, reindex, credential auth, ACE
  error-code propagation, batched row fetch, and TLS client side.
- `openads_serverd --install-service` for Windows SCM; systemd and
  launchd unit files bundled for Linux / macOS.
- Compatibility-test matrix against real Advantage 11.x + 12.x
  installations is a future milestone.

## Phase 1 scope

| Topic | Decision |
|------|----------|
| Operation mode | LOCAL and REMOTE. `openads_serverd` provides a TCP wire server; clients connect with a `tcp://` or `tls://` URI in `AdsConnect60`. Both modes share the same engine (L2–L5). |
| Table formats | ADT + CDX + NTX + VFP (all four ADS-supported types). |
| Memo / index | ADM / FPT / DBT (memo) · ADI / CDX / NTX (index). |
| ABI compatibility | Identical C ABI to ACE; applications do not recompile. |
| Validation frontend | `c:\harbour\contrib\rddads`, unmodified. |
| SQL | Full Advantage SQL dialect (parser + planner + executor + xBase UDFs + AEP host for external stored procedures). |
| Concurrency | OS *byte-range* locking with ranges identical to ACE — coexistence with original ACE installations during migration. |
| Data Dictionary (`.add`) | Full support: member tables, users/groups/permissions, RI, views, procedures, links, validations, defaults. |
| Encryption | AES-128 / AES-256 ECB primitive (vendored tiny-AES-c). Table encryption (M11.2) uses AES-256-CTR on top (DBF only, OpenADS-specific — not byte-compatible with SAP ADS format). SAP proprietary encryption is out of scope. |
| Transactions (TPS) | Multi-table ACID, savepoints, crash recovery via write-ahead log. |
| Platforms | Windows (x86 + x64), Linux, macOS, BSD. |
| Language / build | C++17 with `extern "C"` external ABI. CMake + GitHub Actions. |
| i18n | OEM ↔ ANSI ↔ UTF-8 ↔ UTF-16 (the API's `*W` variants). |
| License | Apache License 2.0. |

## Architecture

```
┌──────────────────────────────────────────────────────────────────┐
│  Harbour application (no recompilation)                          │
│    REQUEST ADS / dbUseArea( .T., "ADS", ... )                    │
└────────────────────────┬─────────────────────────────────────────┘
                         │  Clipper RDD API
┌────────────────────────▼─────────────────────────────────────────┐
│  rddads.lib  (contrib/rddads — untouched)                        │
│    ads1.c / adsfunc.c / adsx.c / adsmgmnt.c                      │
└────────────────────────┬─────────────────────────────────────────┘
                         │  ACE C ABI (~230 Ads* entry points)
                         │  ace.h headers
═════════════════════════╪══════════════════════════════════════════
                         │       ▼  OPENADS REPLACES HERE
┌────────────────────────▼─────────────────────────────────────────┐
│  L1 — ABI Layer  (libace32.dll / libace64.dll / libace.so)       │
│    extern "C" wrappers → ACE handle translation → C++ engine     │
│    Error code mapping (ACE codes ↔ engine errors)                │
│    OEM / ANSI / UTF-8 / UTF-16 translation                       │
└────────────────────────┬─────────────────────────────────────────┘
                         │  internal C++ API (RAII handles)
┌────────────────────────▼─────────────────────────────────────────┐
│  L2 — Session / Connection Layer                                 │
│    Connection (local path or Data Dictionary)                    │
│    Statement (prepared SQL cursor)                               │
│    HandleRegistry (ADSHANDLE → object pointer, thread-safe)      │
└────────────────────────┬─────────────────────────────────────────┘
                         │
┌────────────────────────▼─────────────────────────────────────────┐
│  L3 — SQL Engine                                                 │
│    Lexer → Parser (AST) → Resolver → Planner → Executor          │
│    DD-aware Catalog, xBase UDFs                                  │
│    AEP host (stored procedures as .dll/.so plugins)              │
└────────────────────────┬─────────────────────────────────────────┘
                         │
┌────────────────────────▼─────────────────────────────────────────┐
│  L4 — Engine Core (transport-agnostic)                           │
│    Table / Index / MemoStore / Cursor / Filter (AOF)             │
│    LockMgr (OS byte-range, ACE-compatible ranges)                │
│    TxLog (multi-table WAL ACID + savepoints + crash recovery)    │
│    Catalog (DD .add reader/writer)                               │
│    PageCache / BufferMgr                                         │
└────────────────────────┬─────────────────────────────────────────┘
                         │  Driver trait (open / read / write page)
        ┌────────────────┼────────────────┬───────────────┐
        ▼                ▼                ▼               ▼
   ┌─────────┐     ┌─────────┐     ┌─────────┐     ┌─────────┐
   │AdtDriver│     │CdxDriver│     │NtxDriver│     │VfpDriver│
   │.adt+.adm│     │.dbf+.cdx│     │.dbf+.ntx│     │.dbf+.fpt│
   │   +.adi │     │   +.fpt │     │   +.dbt │     │   +.cdx │
   └────┬────┘     └────┬────┘     └────┬────┘     └────┬────┘
        └────────────────┴────────────────┴────────────────┘
                                 │
┌────────────────────────────────▼─────────────────────────────────┐
│  L5 — OS Abstraction (Platform)                                  │
│    File I/O · mmap · byte-range locks · paths · time · threads   │
│    Win32 and POSIX implementations                               │
└──────────────────────────────────────────────────────────────────┘
```

### Key boundaries

- **L1** is the only module with C ABI. Everything else is internal C++17.
- L4's **driver trait** is the extension point: each table format is a swappable driver.
- The **remote server** (`openads_serverd`) is another L1 frontend (TCP transport layer); L2 through L5 are reused as is.
- The **SQL engine** (L3) consumes L4 only through `Cursor` and `Catalog`; it has no knowledge of file formats.
- The **LockMgr** preserves the exact byte-range semantics of ACE, allowing coexistence with original ACE installations on the same files during migration.

## Repository layout

```
OpenADS/
├── CMakeLists.txt              # top-level build, presets per platform
├── CMakePresets.json
├── LICENSE                     # Apache License 2.0
├── NOTICE                      # attribution + trademark notice
├── README.md
├── docs/
│   ├── architecture.md
│   ├── ace-coverage.md         # entry-point status table (~231 fns)
│   ├── adt-format.md           # ADT/ADM/ADI on-disk spec
│   ├── lock-ranges.md          # ACE-compat byte-range table
│   ├── tx-log.md               # WAL format + recovery protocol
│   ├── sql-grammar.md          # Advantage SQL EBNF + diffs
│   └── wire-protocol.md        # TCP opcode + payload spec for non-C++ clients
│
├── third_party/                # vendored deps
│   ├── tinyaes/                # AES-128/256 (Unlicense)
│   ├── utf8.h/                 # UTF conversion (Unlicense)
│   ├── doctest/                # unit test framework (MIT)
│   ├── mbedtls/                # TLS 1.2/1.3 (Apache 2.0, vendored 3.6 LTS)
│   └── ace-headers/            # OpenADS-authored ace.h compatibility surface
│
├── include/openads/            # public C++ headers (consumed by L1)
│   ├── engine.h
│   ├── connection.h
│   ├── table.h
│   ├── cursor.h
│   ├── catalog.h
│   └── error.h
│
├── src/
│   ├── abi/                    # L1 — ACE C ABI shim
│   │   ├── ace_exports.def     # Windows DLL export list
│   │   ├── ace_exports.cpp     # ~231 extern "C" thunks (local + remote branch)
│   │   ├── handle_registry.cpp # ADSHANDLE ↔ object map
│   │   ├── error_map.cpp       # ACE error codes
│   │   ├── charset.cpp         # OEM/ANSI/UTF conversion
│   │   └── version.cpp         # AdsGetVersion / AdsGetServerName
│   │
│   ├── session/                # L2
│   │   ├── connection.cpp
│   │   ├── statement.cpp
│   │   └── globals.cpp         # AdsSetDefault / AdsSetFileType / etc.
│   │
│   ├── sql/                    # L3
│   │   ├── lex/lexer.cpp
│   │   ├── parse/parser.cpp        # recursive-descent
│   │   ├── parse/ast.h
│   │   ├── resolve/resolver.cpp    # name binding, type check
│   │   ├── plan/planner.cpp        # logical → physical
│   │   ├── plan/optimizer.cpp      # predicate pushdown, index selection
│   │   ├── exec/executor.cpp       # iterator pipeline
│   │   ├── exec/operators/         # scan / filter / sort / agg / join / ...
│   │   ├── func/scalar.cpp         # xBase UDFs (LEFT/SUBSTR/CTOD/...)
│   │   ├── func/aggregate.cpp
│   │   └── aep/host.cpp            # AEP plugin loader (.dll / .so)
│   │
│   ├── engine/                 # L4 — core
│   │   ├── table.cpp
│   │   ├── cursor.cpp
│   │   ├── filter_aof.cpp      # Advantage Optimized Filter
│   │   ├── lock_mgr.cpp        # OS byte-range, ACE-compat ranges
│   │   ├── tx_log.cpp          # WAL writer
│   │   ├── tx_recover.cpp      # crash recovery
│   │   ├── savepoint.cpp
│   │   ├── data_dict.cpp       # .add/.am binary + OpenADS-native reader/writer
│   │   ├── page_cache.cpp
│   │   ├── buffer_mgr.cpp
│   │   └── encryption.cpp      # AES-128 / 256
│   │
│   ├── drivers/                # L4 — format drivers
│   │   ├── driver_trait.h      # abstract interface
│   │   ├── adt/
│   │   │   ├── adt_table.cpp   # .adt header + records
│   │   │   ├── adm_memo.cpp    # .adm blob store
│   │   │   └── field_types.cpp # autoinc / GUID / modtime / timestamp / ...
│   │   ├── cdx/
│   │   │   ├── dbf_table.cpp
│   │   │   ├── cdx_index.cpp   # FoxPro CDX compact index
│   │   │   └── fpt_memo.cpp
│   │   ├── ntx/
│   │   │   ├── dbf_table.cpp   # shared with cdx via dbf_common
│   │   │   ├── ntx_index.cpp   # Clipper NTX
│   │   │   └── dbt_memo.cpp
│   │   ├── vfp/
│   │   │   ├── vfp_table.cpp   # DBF v0x30 + nullable + autoinc
│   │   │   └── fpt_memo.cpp
│   │   └── dbf_common.cpp      # shared DBF header logic
│   │
│   ├── network/                # TCP wire server + client transport
│   │   ├── wire.h              # opcode enum + frame helpers
│   │   ├── server.cpp          # openads_serverd accept loop
│   │   ├── socket_win32.cpp    # platform socket (winsock2)
│   │   ├── socket_posix.cpp    # platform socket (BSD sockets)
│   │   └── transport_tls.cpp   # mbedtls transport (OPENADS_WITH_TLS)
│   │
│   ├── platform/               # L5 — OS abstraction
│   │   ├── file.h
│   │   ├── file_win32.cpp
│   │   ├── file_posix.cpp
│   │   ├── lock.h
│   │   ├── lock_win32.cpp      # LockFileEx
│   │   ├── lock_posix.cpp      # fcntl F_SETLK
│   │   ├── mmap.cpp
│   │   ├── path.cpp            # case-insensitive matching on unix
│   │   ├── time.cpp
│   │   └── thread.cpp
│   │
│   └── util/
│       ├── span.h
│       ├── result.h            # error-or-value
│       └── log.cpp
│
├── bindings/
│   ├── php_ext/                # php_openads.dll — native Zend extension (PHP 8.x)
│   │   ├── src/                # C wrapper + AdsDictionary class
│   │   └── CMakeLists.txt
│   └── python/                 # python_openads — ctypes/cffi binding (in progress)
│
├── DA-Web/                     # Browser-based Data Architect replacement
│   ├── index.php               # entry point (Apache / PHP 8.x)
│   ├── api/                    # REST endpoints consumed by the SPA
│   └── assets/                 # JS / CSS (no framework, vanilla)
│
├── tests/
│   ├── unit/                   # doctest, per-module
│   │   ├── abi_adt_smoke_test.cpp
│   │   ├── cdx_index_test.cpp
│   │   ├── lock_mgr_test.cpp
│   │   ├── tx_log_test.cpp
│   │   ├── sql_parser_test.cpp
│   │   └── ...
│   ├── smoke/                  # third-party RDD harnesses (opt-in)
│   │   ├── harbour/            # smoke.prg linked against rddads + OpenADS DLL
│   │   └── xsharp/             # AdsSmoke.prg through X#'s AXDBFCDX RDD
│   ├── fixtures/               # canonical .adt / .dbf / .cdx / .add samples
│   └── tools/                  # ad-hoc C++ test programs + SAP probe utilities
│
├── tools/
│   ├── serverd/                # openads_serverd — TCP wire server daemon
│   │   └── main.cpp
│   ├── adsbackup/              # adsbackup — backup/restore CLI (see its README.md)
│   │   └── main.cpp
│   ├── import_dd/              # openads_import_dd — SAP .add → OpenADS import
│   │   └── main.cpp
│   ├── bench/                  # openads_bench — synthetic SQL benchmark
│   ├── openmonitor/            # openmonitor — TUI + web dashboard for openads_serverd
│   ├── harbour_patch/          # rddads compatibility patches + ADS baseline fixture
│   ├── scripts/
│   │   ├── systemd/            # openads-serverd.service (Linux)
│   │   └── launchd/            # com.openads.serverd.plist (macOS)
│   ├── adt-dump/               # CLI: hex-dump ADT structure
│   ├── cdx-dump/
│   ├── tx-replay/              # WAL replay / inspect
│   └── ace-trace/              # log every ACE call (debug shim)
│
├── examples/
│   ├── harbour-hbmk2/          # turnkey hbmk2 (.hbp) project
│   └── fivewin/                # FiveWin (FWH) MSVC64 build script
│
└── .github/workflows/
    ├── ci.yml                  # matrix Win / Linux / macOS
    ├── compat.yml              # nightly run vs Harbour rddads tests
    └── release.yml             # tagged DLL / SO artifacts
```

### Module notes

- **`src/abi/ace_exports.cpp`** is the only contact point with the C ABI. A `static constexpr` table maps each ACE entry point to its internal handler. Optionally generated by a script from `ace.h`.
- **`src/drivers/driver_trait.h`** defines the minimum interface: `open / close / read_record / write_record / seek / scan / index_create / index_seek / lock_range`. Each driver is roughly 3–5 files.
- **`src/engine/lock_mgr.cpp`** centralises lock ranges — the single source of truth for ACE coexistence.
- **`src/engine/tx_log.cpp`** and **`tx_recover.cpp`** are driver-independent: the WAL log records `(driver_id, page_no, before_image, after_image)` as opaque payloads.
- **`src/sql/`** is driver-independent and operates against an abstract `Cursor`. SQL tests do not require real drivers (mock cursor).
- **`src/platform/`** is the only OS dependency. Engine tests use a platform mock to inject I/O failures.
- **`tools/`** is the debugging artillery — critical for byte-level diffs against original ACE.

## Data flow

### Example A — Opening a CDX table from Harbour

```
Harbour PRG
  USE clientes VIA "ADSCDX" SHARED
       │
       ▼
rddads.lib :: hb_adsOpen()                 [contrib/rddads/ads1.c]
  AdsConnect60( "C:\data", ..., &hConn )   ← OpenADS L1 entry
  AdsOpenTable( hConn, "clientes.dbf",     ← OpenADS L1 entry
                ADS_CDX, ADS_DEFAULT,
                ADS_NONE, ADS_DEFAULT,
                ADS_DEFAULT, &hTbl )
       │
       ▼  L1 ace_exports.cpp
extern "C" AdsConnect60(...)
  → openads::Connection::open( path, ... )
       │
       ▼  L2 session/connection.cpp
Connection ctor
  → resolves path, registers handle, returns ADSHANDLE via HandleRegistry
       │
       ▼  back to L1
extern "C" AdsOpenTable(...)
  → conn->open_table( "clientes.dbf", FormatHint::Cdx, OpenMode::Shared )
       │
       ▼  L2 → L4 engine/table.cpp
Table::open()
  1. DriverFactory::detect_or_force(path, hint) → CdxDriver
  2. CdxDriver::open()
       ├─ platform::file_open("clientes.dbf", RW)
       ├─ read DBF header (32 bytes + field descriptors)
       ├─ platform::file_open("clientes.cdx", RW)   if it exists
       ├─ CdxIndex::load_root_pages()
       └─ FptMemo::open("clientes.fpt")              if memo fields
  3. LockMgr::acquire_open_share()  (byte-range header lock, ACE range)
  4. PageCache::register(file_handles)
  5. TxLog::register_table(table_id) (no-op outside a transaction)
  6. returns Table*
       │
       ▼
HandleRegistry::register(Table*) → ADSHANDLE
       │
       ▼
return SUCCESS to rddads → returned to PRG
```

### Example B — `SELECT * FROM clientes WHERE saldo > 1000 ORDER BY nombre`

```
PRG
  AdsCreateSQLStatement( hConn, &hStmt )
  AdsExecuteSQLDirect( hStmt, "SELECT...", &hCursor )
       │
       ▼  L1
extern "C" AdsExecuteSQLDirect(hStmt, sql, &cursor)
  → stmt->execute_direct(sql)
       │
       ▼  L2 session/statement.cpp → L3
Statement::execute_direct(sql)
  ┌──────────── L3 sql/ pipeline ───────────────────────────┐
  │ Lexer    → tokens                                        │
  │ Parser   → AST (SelectStmt)                              │
  │ Resolver → bind "clientes" via Catalog → Table*          │
  │            bind columns saldo, nombre → ColumnRef        │
  │            type-check predicates                         │
  │ Planner  → LogicalPlan:                                  │
  │              Sort(nombre)                                │
  │                └─ Filter(saldo > 1000)                   │
  │                     └─ Scan(clientes)                    │
  │ Optimizer → predicate pushdown, index selection:         │
  │              if index on (saldo) → IndexRangeScan        │
  │              else                  → SeqScan + Filter    │
  │              if index on (nombre)  → drop Sort           │
  │ Executor → builds operator tree (iterator pipeline):     │
  │              SortOp                                      │
  │                └─ FilterOp                               │
  │                     └─ TableScanOp(Cursor over Table)    │
  └──────────────────────────────────────────────────────────┘
       │
       ▼  Cursor handed back
HandleRegistry::register(Cursor*) → ADSHANDLE returned as hCursor
       │
       ▼
PRG fetches rows via AdsGotoTop / AdsGetRecord / AdsSkip
  → each call drives one Executor::next() through L4 PageCache
  → AdsGetField → field decode (xBase types or ADT extended types,
                  depending on driver)
```

### Example C — Multi-table transaction

```
AdsBeginTransaction(hConn)
  → TxLog::begin(tx_id)         (write BEGIN record)
  → LockMgr::tx_attach(tx_id)

AdsLockRecord(hTblA, 42)
  → LockMgr::lock_record(tx_id, A, 42)   (escalates byte-range lock)

AdsUpdateRecord(hTblA, ...)
  → Table::update_record():
       1. Capture before-image of pages dirtied
       2. Apply change in PageCache
       3. TxLog::log_update(tx_id, A, page_no, before, after)

AdsAppendRecord(hTblB, ...)
  → similar, writes a log record for table B

AdsCommitTransaction(hConn)
  → TxLog::commit(tx_id)        (write COMMIT, fsync log)
  → PageCache::flush_tx_pages(tx_id)
  → LockMgr::tx_release(tx_id)

AdsRollbackTransaction(hConn)  [alternative]
  → TxLog::rollback_walk(tx_id):
       reverse-iterate log, restore before-images via PageCache
  → LockMgr::tx_release(tx_id)
```

#### Crash recovery (next process startup)

```
TxRecover::run()
  scan TxLog tail
  for each tx without COMMIT          → roll back (apply before-images)
  for each tx with COMMIT but pages unflushed → roll forward
  truncate log
```

## Concurrency and lock ranges

### Locking modes

`AdsLocking(ON | OFF)` selects the byte-range scheme:

| Mode | When to use | Coexistence |
|------|-------------|-------------|
| **Proprietary** (default) | ADS-only deployments | ADS-specific ranges, fastest |
| **Compatible** (`ADS_COMPATIBLE_LOCKING`) | Coexistence with Clipper / FoxPro / Harbour `DBF*` RDDs over the same files | Standard Clipper / FoxPro ranges |

OpenADS supports both modes. The `rddads` frontend selects via `AdsSetServerType(ADS_LOCAL_SERVER)` combined with `AdsLocking(ON | OFF)`.

### Granularity

Three hierarchical levels:

1. **File lock** (exclusive) — locks the whole table. `AdsLockTable` / `flock` open mode `EXCLUSIVE`.
2. **Record lock** — shared for reads, exclusive for writes. `AdsLockRecord(recno)`.
3. **Header lock** — first byte of the file header, taken only during writes that mutate the header (append, pack, zap, recno recalculation).

Rules:

- Open SHARED → acquires a shared header lock over the header range.
- Append / Pack / Zap → exclusive header lock plus an exclusive file-equivalent lock (Compatible mode uses byte `LOCKOFFSET_FILE`; Proprietary uses an internal ADS range).
- Update record → record lock required (RDD enforcement, not OS).
- Read record → no lock required in `READ COMMITTED`; `READ UNCOMMITTED` skips even versioning.

### Concrete byte-range layout

#### Compatible mode (Clipper / FoxPro coexistence)

```
DBF + NTX (Clipper scheme):
  FILE LOCK    : offset 1_000_000_000        size 1
  RECORD LOCK  : offset 1_000_000_001 + recno size 1

DBF + CDX (FoxPro scheme — descending):
  FILE LOCK    : offset 0x7FFFFFFE           size 1
  RECORD LOCK  : offset 0x7FFFFFFE - recno   size 1

DBF + VFP CDX (FoxPro VFP — same as CDX but offset 0x40000000-1):
  FILE LOCK    : offset 0x3FFFFFFE           size 1
  RECORD LOCK  : offset 0x3FFFFFFE - recno   size 1

ADT proprietary:
  FILE LOCK    : offset 0x80000000_00000000  size 0x10000  (64-bit)
  RECORD LOCK  : offset 0x80000000_00000000 + (recno << 16)  size 1
```

#### Proprietary mode

ADS-specific. The ranges are derived from captures of original ACE running over an instrumented filesystem and pinned in `docs/lock-ranges.md`. For phase 1 they are a constant table, validated by `tools/ace-trace`.

### LockMgr API (L4)

```cpp
class LockMgr {
public:
    // OS-level byte-range — inter-process coexistence
    Result<LockToken> lock_file_excl(FileHandle& fh, LockingMode mode);
    Result<LockToken> lock_record   (FileHandle& fh, LockingMode mode,
                                     uint64_t recno, LockType, Timeout);
    Result<>          unlock        (LockToken);

    // Tx-scoped — lifetime tied to TxLog::tx_id
    Result<>          lock_for_tx   (tx_id_t, FileHandle& fh,
                                     uint64_t recno, LockType);
    void              release_tx    (tx_id_t);

    // Intra-process re-entry
    bool              already_held  (FileHandle& fh, uint64_t recno) const;
};
```

Notes:

- **Intra-process re-entrancy.** A connection that already holds a record lock does not call the OS again; only a local counter is incremented.
- **Timeouts.** Per-connection (`AdsSetWaitTime`-equivalent), default 0 (fail fast).
- **Deadlock detection.** Cycle search over the `tx_id → resource → tx_id` graph. On detection, the youngest transaction is aborted (ADS does not document this precisely; OpenADS picks the youngest as victim).
- **Errors.** `AE_LOCKED` (5012) and `AE_LOCK_FAILED` (5013), mapped from LockMgr return codes.

### Coordination with TxLog

Inside an `AdsBeginTransaction` block, locks become tx-scoped: they are not released by `AdsUnlockRecord`, only by `AdsCommitTransaction` / `AdsRollbackTransaction`. This is required to preserve isolation guarantees.

Outside any transaction (autocommit), unlock releases immediately.

### Critical tests

- `lock_mgr_test.cpp` — re-entrancy, timeouts, deadlock detection.
- `tools/ace-trace` running real applications against original ACE → captures range logs → diffs against OpenADS.
- Multi-process conformance test: two OpenADS processes plus one original-ACE process operating on the same CDX table in Compatible mode — must complete without corruption.

## Transactions and write-ahead log

### Model

ARIES-lite. Page-level physiological logging. Multi-table atomicity. Nestable savepoints. Deterministic crash recovery.

### Log location

A single log shared per **data directory** (or per Data Dictionary if an `.add` file exists):

```
<data-dir>/openads.txlog          ← active log
<data-dir>/openads.txlog.<n>      ← rotated archives (truncated post-checkpoint)
<data-dir>/openads.checkpoint     ← latest CP record (LSN, dirty page table)
```

One log per DD avoids cross-DD coordination. Applications that do not use a DD but open tables in the same directory automatically share the log (detected by `Connection::open`).

### Record format

```
struct LogRecord {
    uint64_t  lsn;            // monotonic, unique
    uint64_t  prev_lsn;       // previous record in this tx (chain for undo)
    uint64_t  tx_id;
    uint16_t  type;           // BEGIN | UPDATE | INSERT | DELETE | ...
    uint16_t  driver_id;      // adt | cdx | ntx | vfp | memo | index
    uint32_t  table_id;       // assigned at first touch by tx
    uint64_t  page_no;
    uint16_t  before_len;
    uint16_t  after_len;
    uint8_t   payload[];      // before_image || after_image
    uint32_t  crc32c;         // record integrity
};
```

Record types:

| Type | Semantics |
|------|-----------|
| `BEGIN` | tx started, includes timestamp |
| `UPDATE` | physiological page update (before + after image) |
| `INSERT` | new record append (after only; undo = decrement recno) |
| `DELETE` | logical delete (after only; undo = clear deleted flag) |
| `INDEX_OP` | B+tree mutation (page split / merge / insert / delete key) |
| `MEMO_ALLOC` / `MEMO_FREE` | blob lifecycle in `.adm` / `.fpt` / `.dbt` |
| `SAVEPOINT` | named marker; `prev_lsn` chains here on partial rollback |
| `CLR` | compensation log record (written during undo, redo-only) |
| `COMMIT` | tx end ok, fsync barrier |
| `ABORT` | tx end rollback; all CLRs already written |
| `CHECKPOINT_BEGIN` / `CHECKPOINT_END` | stable point, dirty page table snapshot |

CLRs (compensation log records) make undo idempotent: a crash during rollback simply re-executes the CLRs without duplicating work (CLRs are redo-only, never undo).

### Group commit

```
AdsCommitTransaction(hConn)
  → TxLog::append(COMMIT_PENDING)              [in memory]
  → TxLog::group_commit_barrier()              [waits up to 10ms or N tx]
       └─ writev() batched COMMIT records
       └─ fsync(log_fd)
  → tx becomes visible
```

10 ms / 64 tx, whichever first (configurable). Reduces `fsync × N` to a single `fsync`.

### Savepoints

```
AdsCreateSavepoint(hConn, "sp1")
  → TxLog::append(SAVEPOINT name=sp1 lsn=L1)
  → conn->savepoint_stack.push("sp1", L1)

AdsRollbackTransaction80(hConn, "sp1")    // partial
  → walk back log from current_lsn to L1, write CLRs for each record
  → discard savepoints above sp1
  // tx still active
```

The classic `AdsRollbackTransaction()` performs a full rollback to BEGIN.

### Recovery (startup)

Three ARIES-lite phases:

```
TxRecover::run() {
  // 1. ANALYSIS
  scan log forward from the last CHECKPOINT_BEGIN
  build active_tx_table  (tx_id → last_lsn)
  build dirty_page_table (page_id → first_lsn that dirtied it)

  // 2. REDO
  scan log forward from min(dirty_page_table.first_lsn)
  for each UPDATE / INDEX_OP / INSERT / DELETE / CLR:
      if page.lsn < record.lsn:                 // not yet applied
          apply after_image to page
          page.lsn = record.lsn

  // 3. UNDO (loser txs only — no COMMIT seen)
  for each tx in active_tx_table sorted by last_lsn DESC:
      walk prev_lsn chain
      for each non-CLR record:
          apply before_image
          write CLR(undo_next_lsn = record.prev_lsn)
      write ABORT record
}
```

Determinism: re-entering recovery N times produces the same final state (idempotent via CLRs).

### Page LSN

Each page (DBF, ADT, CDX, NTX, ADI, memo) carries an 8-byte LSN at the end of its page footer. Cost: 8 bytes per page. Required to skip already-applied redo work.

**Compatible mode exception.** DBF / CDX / NTX pages in Compatible mode do not carry an inline LSN footer (it would break byte compatibility with Clipper / FoxPro applications). Workaround: in Compatible mode `TxLog` keeps a separate `lsn_table` (overlay file `.lsnmap`) instead of inlining the LSN. The cost is an extra page of I/O per commit, which is acceptable.

### Checkpointing

A background thread:

- Runs every 30 s or every 1000 transactions (configurable).
- Writes `CHECKPOINT_BEGIN` with a snapshot of `active_tx_table` and `dirty_page_table`.
- Flushes dirty pages with `lsn ≤ checkpoint_lsn`.
- Writes `CHECKPOINT_END`.
- Truncates `openads.txlog.<n>` archives older than the checkpoint.

### Recoverable vs unrecoverable errors

| Situation | Action |
|-----------|--------|
| Crash with pending tx | Recovery rollback |
| CRC failure on a log record | Stop recovery at the last valid record, log a warning |
| Missing log file at startup | Assume clean shutdown (legacy ACE behaviour) |
| Page LSN > log tail LSN | Log corruption → halt, requires manual intervention |
| Out of space during commit | Force partial fsync → mark log full → reject new tx until space is freed |

### Tests

- `tx_log_test.cpp` — unit: write / read / CRC / replay.
- `tx_recover_test.cpp` — inject a crash at every LSN boundary, verify consistency.
- `tools/tx-replay <log>` — human-readable dump and dry-run replay.
- Conformance: simulate `AdsBegin / Update / Crash` with `kill -9` mid-write, verify a consistent restart.

## SQL engine internals

### High-level pipeline

```
SQL string
  ▼  Lexer        (DFA, ~150 keywords, xBase + ANSI SQL)
tokens
  ▼  Parser       (recursive descent, Pratt for expressions)
AST
  ▼  Resolver     (name binding, type check, * expansion, qualification)
Bound AST
  ▼  Planner      (logical plan: relational algebra tree)
LogicalPlan
  ▼  Optimizer    (rules + cost model)
PhysicalPlan
  ▼  Executor     (Volcano: open / next / close iterators)
Result rows / row count
```

### Lexer

Hand-written DFA. Recognises:

- Case-insensitive keywords (~150).
- Identifiers: `[A-Za-z_][A-Za-z0-9_]*`, plus delimited `[name]` and `"name"`.
- Literals: int, float, string (ANSI plus `''` escape), date `{^YYYY-MM-DD}`, boolean `.T.` / `.F.` (xBase).
- Operators: ANSI plus xBase `=`, `==`, `!=`, `#`, `$` (substring contains), `||`.
- Parameters: `?` positional, `:name` named.

Output: stream of `Token { kind, lexeme, line, col }`. Errors carry source position.

### Parser

Recursive descent plus a Pratt parser for expressions (ANSI precedence plus xBase `$` and `||`).

EBNF grammar in `docs/sql-grammar.md`. Key productions:

```ebnf
SelectStmt   := WithClause? "SELECT" SelectList FromClause? WhereClause?
                GroupByClause? HavingClause? OrderByClause?
                LimitClause? (CompoundOp SelectStmt)?
FromClause   := "FROM" TableRef ("," TableRef)*
TableRef     := QualifiedName ("AS"? Alias)?
              | "(" SelectStmt ")" "AS"? Alias                  -- derived
              | TableRef JoinType TableRef "ON" Expr            -- join
JoinType     := "INNER" | "LEFT" "OUTER"? | "RIGHT" "OUTER"? |
                "FULL" "OUTER"? | "CROSS"
CompoundOp   := "UNION" "ALL"? | "INTERSECT" | "EXCEPT"
Expr         := PrimaryExpr (InfixOp Expr)*                     -- Pratt
PrimaryExpr  := Literal | ColumnRef | FuncCall | CaseExpr |
                "(" Expr ")" | SubQuery | Parameter |
                "CAST" "(" Expr "AS" TypeName ")"
CaseExpr     := "CASE" Expr? ("WHEN" Expr "THEN" Expr)+
                ("ELSE" Expr)? "END"
```

The AST is built from sum types (`std::variant`) per category. Visitor pattern for passes.

### Resolver

- **Name binding.** Tables are looked up via `Catalog` (DD-aware when the connection has a DD). Columns are searched in the current scope, including CTEs, derived tables, and `JOIN USING`.
- **`*` expansion.** `SELECT *` becomes a list of `ColumnRef`; `t.*` expands only columns of `t`.
- **Type check.** Arithmetic on numeric, concatenation on character, comparison on compatible types. xBase coercions are permissive (`numeric` ↔ `string` implicit conversion is configurable).
- **Aggregate detection.** Marks expressions as aggregate or scalar; validates `HAVING` vs `WHERE`.
- **Subquery scope.** Correlated references are annotated with `outer_scope_depth`.
- **Errors.** `AE_PARSE_ERROR` (7200), `AE_COLUMN_NOT_FOUND` (5063), and so on.

### Planner (logical)

Generates a relational algebra tree of Volcano nodes:

```
LogicalNode = Scan(table)
            | Filter(child, pred)
            | Project(child, exprs)
            | Sort(child, keys)
            | TopN(child, k)
            | Aggregate(child, group_keys, aggs)
            | Distinct(child)
            | Join(left, right, kind, pred)
            | Union(left, right, all?)
            | CTE(name, child)
            | Insert / Update / Delete (table, source, set, pred)
```

Canonical construction first, no optimisation.

### Optimizer

Rule and cost passes:

| Pass | Type | Description |
|------|------|-------------|
| `predicate_pushdown` | rule | Pushes Filter below Join / Project when column refs allow. |
| `column_pruning` | rule | Project drops columns not used upstream. |
| `constant_folding` | rule | Evaluates constant expressions. |
| `predicate_simplify` | rule | `x AND TRUE → x`, `NOT NOT x → x`, etc. |
| `index_selection` | cost | Matches Filter predicates against available indexes → IndexScan. |
| `join_order` | cost | Selinger-style DP up to 8 tables, greedy beyond. |
| `join_method` | cost | NLJ vs HashJoin vs MergeJoin based on cardinality and ordering. |
| `sort_avoidance` | rule | If the Sort key is a prefix of the IndexScan order, drop the Sort. |
| `aggregate_pushdown` | rule | Pre-aggregates locally when group keys are a subset of the partition. |
| `topn_pushdown` | rule | `LIMIT k` paired with index order becomes early-stop IndexScan. |

Cost model:

- Row-count estimation uses per-table statistics in `Catalog` (recno plus simple equi-width index histograms).
- I/O cost is page reads (CdxIndex 8 KB, ADT 4 KB typical).
- CPU cost is `row_count × per-op constant`.

### PhysicalPlan / Executor

Volcano iterators. Each `next()` returns `Optional<Row>`.

Physical operators:

| Operator | Description |
|----------|-------------|
| `SeqScanOp` | Iterates a `Cursor` over a `Table`, reading via L4 `Table::scan()`. |
| `IndexScanOp` | `Cursor` follows `Index::seek_range()`. |
| `IndexLookupOp` | Nested-loop join inner side via index seek. |
| `FilterOp` | Predicate evaluation, drops false rows. |
| `ProjectOp` | Per-row expression evaluation. |
| `SortOp` | External merge sort, runs in `<data-dir>/openads.tmp.<N>`. |
| `TopNOp` | Min-heap with k slots. |
| `HashAggregateOp` | Hash-table aggregation. |
| `StreamAggregateOp` | Input already ordered by group keys. |
| `DistinctOp` | Hash set. |
| `NestedLoopJoinOp` | Outer × inner (with index seek when available). |
| `HashJoinOp` | Builds a hash on the smaller side, probes. |
| `MergeJoinOp` | Both sides ordered (after `sort_avoidance`). |
| `UnionOp` / `UnionAllOp` | Concat plus optional dedupe. |
| `CTEScanOp` | Reuses a materialised CTE result. |
| `InsertOp` / `UpdateOp` / `DeleteOp` | DML, writes via `Table` API and `TxLog`. |

External sort: runs up to `mem_budget` (default 64 MB), spills to tempfiles, K-way merge.

Hash join build: if the hash table exceeds the budget, falls back to Grace Hash (partition plus spill).

### xBase scalar UDFs

Registered in `src/sql/func/scalar.cpp`. Subset (~80 functions):

```
String : LEFT, RIGHT, SUBSTR, AT, RAT, LTRIM, RTRIM, ALLTRIM, PADL, PADR,
         PADC, REPL, SPACE, UPPER, LOWER, STUFF, STRTRAN, LIKE, MATCH
Date   : CTOD, DTOS, DTOC, STOD, DAY, MONTH, YEAR, DOW, CMONTH, CDOW,
         DATE, TIME, NOW, DATEADD, DATEDIFF
Numeric: STR, VAL, INT, ROUND, MOD, ABS, MAX, MIN, EXP, LOG, SQRT,
         SIGN, FLOOR, CEILING
Logic  : IIF, IsNULL, COALESCE, NULLIF, CASE
Type   : CAST, CONVERT, EMPTY, TYPE
Misc   : RECNO, RECCOUNT, DELETED, FOUND, EOF, BOF, LASTREC,
         FIELDNAME, FIELDPOS, FCOUNT
```

Aggregates: `COUNT`, `SUM`, `AVG`, `MIN`, `MAX`, `STDDEV`, `VARIANCE`, plus xBase `TOTAL`.

The UDF registry allows AEP plugins to add custom functions.

### AEP host (stored procedures)

Advantage Extended Procedures are `.dll` / `.so` plugins exposing a C ABI. Loading and invocation:

```cpp
// AEP plugin entry (mirrored from the ADS spec):
extern "C" UNSIGNED32 GetProcInfo(...);
extern "C" UNSIGNED32 InvokeProc(IInvokeContext*);

// IInvokeContext = ABI exposed by OpenADS to the plugin:
struct IInvokeContext {
    UNSIGNED32 (*GetInputRowFieldCount)(...);
    UNSIGNED32 (*GetInputRowField)(...);
    UNSIGNED32 (*WriteOutputRow)(...);
    UNSIGNED32 (*OpenTable)(...);
    // ... ~30 functions
};
```

`AepHost` (`src/sql/aep/host.cpp`):

- Resolves `dll_name + entry` from DD properties `ADS_DD_PROC_DLL_*`.
- Lazy `dlopen` / `LoadLibrary` on first call.
- Marshals input / output arguments through a stable ABI.
- Sandboxing is optional; the plugin runs in-process (matching original ACE — no sandbox).

SQL invocation: `EXECUTE PROCEDURE my_proc(:p1)` causes the planner to emit `AepCallOp`.

Triggers (`BEFORE` / `AFTER INSERT / UPDATE / DELETE`) are AEP plugins fired by `Table::write_record` during DML.

### Prepared statements and cursors

```
AdsPrepareSQL(hStmt, sql)
  → parse + resolve + plan + optimize, caches the PhysicalPlan
  → parameter binding deferred
AdsExecuteSQL(hStmt)
  → binds parameters → executes → returns Cursor
```

Cursor types:

- **Forward-only** (default): `next()` only.
- **Scrollable** (`AdsCacheRecords ON`): materialised in a tempfile, supports `AdsGotoRecord(n)` and `AdsSkip(-N)`.

`PlanCache` (LRU, key = `SQL hash + schema_version`) avoids re-planning on repeats.

### Result rows backed by a cursor

`AdsGetField(hCursor, fieldName, ...)` reads the current row of the cursor. The cursor keeps a pointer to the row buffer (zero-copy on scans, copy when computed).

### Errors and codes

Mapped to existing ACE codes:

- `7200` AE_PARSE_ERROR
- `7201` AE_INVALID_SQL_TOKEN
- `5063` AE_COLUMN_NOT_FOUND
- `5066` AE_TABLE_NOT_FOUND
- `7041` AE_TYPE_MISMATCH
- `7042` AE_DIVISION_BY_ZERO
- and so on.

### Tests

- `sql_lexer_test.cpp`, `sql_parser_test.cpp` — grammar coverage.
- `sql_resolver_test.cpp` — name-binding edge cases.
- `sql_planner_test.cpp` — golden-file plans for canonical queries.
- `sql_optimizer_test.cpp` — verifies that rules fired (snapshot of the post-optimizer plan).
- `sql_executor_test.cpp` — end-to-end against fixtures with expected results.
- `aep_host_test.cpp` — a sample `.dll` / `.so` plugin returning fixed rows.
- `sql_conformance/` — Advantage SQL test suite derived from official ADS documentation samples.

## Error handling

### Internal model

Internal C++ code uses `Result<T>` (an `expected`-style type in `src/util/result.h`) so error paths are explicit and exceptions are reserved for true programming bugs (`assert`-equivalent contract violations).

```cpp
template<class T> using Result = std::expected<T, Error>;

struct Error {
    int32_t  code;        // ACE error code (e.g. 5012)
    int32_t  sub_code;    // OS errno / GetLastError when applicable
    std::string message;  // formatted, localised
    std::string context;  // file, table, recno, sql snippet
};
```

Errors propagate via early-return; no exception unwinding through L4.

### ACE error code surface

The L1 ABI returns the canonical `UNSIGNED32` ACE error code on every call. OpenADS reproduces the documented ranges so existing apps reading `AdsGetLastError` see identical numbers:

| Range | Family | Examples |
|-------|--------|----------|
| 4000–4999 | Transport / connection | `4001 AE_NETWORK_ERROR`, `4097 AE_INVALID_CONNECTION_HANDLE` |
| 5000–5999 | Engine / locking / records | `5012 AE_LOCKED`, `5036 AE_NO_CONNECTION`, `5063 AE_COLUMN_NOT_FOUND`, `5066 AE_TABLE_NOT_FOUND` |
| 6000–6999 | Index / order | `6105 AE_INDEX_NOT_FOUND`, `6106 AE_INDEX_CORRUPT` |
| 7000–7999 | SQL / parser / type | `7041 AE_TYPE_MISMATCH`, `7200 AE_PARSE_ERROR`, `7201 AE_INVALID_SQL_TOKEN` |

A canonical table lives in `src/abi/error_codes.h`, generated from the documented ACE constants. Anything not yet implemented returns `5004 AE_FUNCTION_NOT_AVAILABLE` rather than a fabricated code, so apps and `rddads` can degrade gracefully.

### `AdsGetLastError` semantics

ACE keeps a per-thread last-error slot. OpenADS replicates this:

```cpp
thread_local Error g_last_error;

extern "C" UNSIGNED32 AdsGetLastError(UNSIGNED32* pulErr,
                                      UNSIGNED8* pucBuf,
                                      UNSIGNED16* pusBufLen) {
    *pulErr = g_last_error.code;
    copy_text(pucBuf, pusBufLen, g_last_error.message);
    return AE_SUCCESS;
}
```

Every L1 thunk updates the slot before returning. Successful calls clear it.

### Localised messages

Default English. Optional catalogues for Spanish and Portuguese (large legacy ADS user bases). Selected via `AdsSetLocalizedMessages` or env `OPENADS_LOCALE=es`. Catalogues live in `src/abi/messages_<locale>.cpp`, lookup by `code`.

### Logging / tracing

Three levels controlled by env:

| Var | Effect |
|-----|--------|
| `OPENADS_LOG=info` | Connection open / close, table open, SQL executed (truncated) |
| `OPENADS_LOG=debug` | Plus index seeks, locks acquired, tx boundaries; resolve detail lines |
| `OPENADS_LOG=trace` | Every L1 entry / exit with arguments and return code |
| `OPENADS_LOG_FILE=<path>` | Append `RESOLVED=` audit lines only (path resolve trail) |
| `OPENADS_RESOLVE_VERBOSE=1` | Also print resolve detail (`input=`, remap, FALLBACK) on the console |

`tools/ace-trace` is a separate shim that intercepts every `Ads*` call and writes a structured trace; useful for diffing against original ACE behaviour.

### Failure boundaries

| Boundary | Strategy |
|----------|----------|
| L5 (OS) errors | Map errno / `GetLastError` into `Error.sub_code`, surface a 4xxx or 5xxx ACE code |
| L4 corruption (CRC / LSN mismatch) | Halt access to the affected file, return `5103 AE_TABLE_CORRUPTED`, log details |
| L3 SQL errors | Return 7xxx range, no abort of the connection |
| L2 invalid handle | `4097 AE_INVALID_CONNECTION_HANDLE`, no crash |
| L1 panic (assert) | Last-resort: log and `abort()` only on contract violations during debug builds; release builds convert to `5000 AE_INTERNAL_ERROR` |

## Testing strategy and roadmap

### Test pyramid

| Layer | Tools | Coverage target |
|-------|-------|-----------------|
| Unit | doctest, run on every commit | ≥ 85 % per module (engine, drivers, sql, lock, tx) |
| Integration (in-process) | doctest, real files | Full driver matrix (ADT / CDX / NTX / VFP) × open / write / index / memo / tx |
| ABI conformance | C harness invoking L1 entry points | All ~230 ACE entry points exercised at least once |
| Harbour smoke | `tests/smoke/harbour/smoke.prg` linked against `rddads` and OpenADS DLL | `tests/datad.prg`, `tests/manage.prg` from `c:\harbour\contrib\rddads\tests\` plus custom xBase scenarios |
| X# smoke | `tests/smoke/xsharp/AdsSmoke.prg` through X#'s `AXDBFCDX` RDD against OpenADS DLL | `DbCreate` / `OrdCreate` / `DbAppend` / `DbSeek` / navigation; exercises the versioned overloads the X# RDD calls |
| Byte compatibility | `tools/adt-dump`, `tools/cdx-dump` diff vs ACE-produced fixtures | All write paths produce byte-identical output |
| Multi-process | Two OpenADS plus optional original ACE on shared files | No corruption, lock semantics match |
| Fuzzing | libFuzzer over lexer / parser / log replay / driver readers | Zero crashes / UB after N hours |
| Benchmarks | google-benchmark micro-suite | No regression vs previous tag |
| Recovery | Crash-injection harness (`kill -9` between LSN boundaries) | Recovery converges deterministically |

### CI matrix

GitHub Actions:

- Compilers: MSVC 2022 (x86 / x64), GCC 11+, Clang 14+, MinGW-w64.
- OS: Windows 2022, Ubuntu 22.04, macOS 13, FreeBSD 14 (via cross / VM).
- Sanitisers: ASan, UBSan, TSan on Linux Clang job.
- Nightly: full Harbour rddads test suite plus byte-compat job against pinned ACE-produced fixtures.

### Phase 1 milestones

| Milestone | Deliverable |
|-----------|-------------|
| **M0 — skeleton** | ✅ CMake + L5 platform + util + doctest harness. Builds on Win / Linux / macOS. |
| **M1 — DBF read** | ✅ `dbf_common` + CDX driver read-only. `AdsConnect60` / `AdsOpenTable` / `AdsGotoTop` / `AdsSkip` / `AdsGetField` work over a CDX-typed DBF. |
| **M2 — DBF write + lock** | ✅ Append / update / delete + `LockMgr` Compatible mode. NTX driver. Single-process integrity tests. |
| **M3 — indexes** | ✅ CDX read / write, NTX read / write. Multi-level B+tree split, `dbSeek`, compound key expressions, AOF / Rushmore filter. |
| **M4 — ADT + memo** | ✅ ADT / ADM read+write (all 13 field types), `.fpt` / `.dbt` memo stores. `AdsCreateTable(ADS_ADT)` creates native `.adt` files. VFP driver (V/Q/autoinc/NULL bitmap). AES-128/256 ECB block-cipher primitive. |
| **ADI — B+tree index** | ✅ ADI read+write complete (post-rc29) — seek, insert, erase, flush, leaf+branch page splits, `promote_split_` branch promotion, fixed-root copy-up; char-key and numeric-key variants. |
| **M5 — TPS** | ✅ TxLog WAL + crash recovery, savepoints, multi-table atomicity, group commit, Compatible-mode `.lsnmap` overlay. |
| **M6 — DD** | ✅ Full binary `.add` / `.am` / `.ai` parser + OpenADS-native text format. Users, groups, permissions, RI, views, triggers, stored procedures, links. Server-side trigger / RI / permission enforcement. |
| **M7 — SQL** | ✅ Full Advantage SQL — 50+ milestones: SELECT / DML / DDL, all JOIN types, aggregates, window functions, subqueries, CTEs, derived tables, GROUP BY / HAVING, UNION, DISTINCT, LIMIT, xBase UDFs, AEP host. |
| **M8 — Conformance** | ✅ Harbour `rddtst.prg` 442/442 PASS. Byte-compat CI green. Tagged `v0.1.0` → `v1.0.0-rc29+`. |
| **M12 — TCP server** | ✅ `openads_serverd` wire protocol — Hello/Connect/OpenTable/ExecuteSQL/Fetch/Disconnect/Error, auth, ACE error-code propagation, batch fetch, TLS client. `php_openads` PHP extension client in production over TCP. |

## Next steps

All Phase 1 milestones (M0–M8) are complete. See the release table at the top of this document for the incremental tag history. Plans in `docs/superpowers/plans/` cover the original design; the shipped implementation is the authoritative record.

| Milestone | Status |
|-----------|--------|
| **M0 — Skeleton** | ✅ Done — `0.1.0` |
| **M1 — DBF read (CDX)** | ✅ Done — `0.1.0` |
| **M2 — DBF write + LockMgr** | ✅ Done — `0.1.0` |
| **M3 — Indexes (CDX/NTX/AOF)** | ✅ Done — M3.6 + M3.10 closed all known-issues; byte-compat CI green |
| **M4 — ADT + memo + VFP + AES** | ✅ Done — post-rc28; all 13 ADT field types; `AdsCreateTable(ADS_ADT)` creates native `.adt`; ADM; FPT/DBT; VFP V/Q/autoinc/NULL-bitmap; AES-128/256 ECB primitive (AES-256-CTR table encryption is M11.2) |
| **ADI — B+tree index read+write** | ✅ Done — post-rc29; seek / insert / erase / flush; leaf+branch page splits; `promote_split_` branch promotion; fixed-root copy-up; char-key and numeric-key variants |
| **M5 — TPS / WAL** | ✅ Done — group commit, Compatible-mode `.lsnmap`, savepoints, crash recovery |
| **M6 — Data Dictionary** | ✅ Done — binary `.add`/`.am`/`.ai` parser + OpenADS-native text format; full CRUD; server-side trigger/RI/permission enforcement |
| **M7 — SQL engine** | ✅ Done — 50+ milestones; full Advantage SQL dialect; xBase UDFs; AEP host |
| **M8 — Conformance + 0.1.0** | ✅ Done — `rddtst.prg` 442/442 PASS; byte-compat CI green; tagged `v0.1.0` → `post-rc29` |
| **M12 — TCP server** | ✅ Done — `openads_serverd` + `php_openads` PHP extension client in production |

### Snapshot (post-rc29, 2026-06-16)

- **517 doctest cases passing** on Windows MSVC Release (cross-platform CI green on Ubuntu 24.04 clang + macOS AppleClang).
- **231 `Ads*` entry points exported** — all entry points Harbour `rddads.lib` references resolve; DD CRUD surface is real, `AdsMg*` returns live telemetry, the rest are silent-success stubs.
- **Drop-in DLL:** `ace64.dll` (Win x64) + `ace32.dll` (Win x86) from the `openads_ace` SHARED target, plus 6 legacy MSVC2013-era CRT shims (`_dclass`, `_dsign`, `_wfsopen`, `_getch`, `_kbhit`, `_eof`).
- **Full Harbour `rddtst.prg` 442/442 PASS (2026-05-08).**
- **TCP server in production:** `openads_serverd` serves the OpenADS wire protocol; `php_openads` PHP extension client connects via `tcp://host:port/path.add`.
- **ADI B+tree write path complete** — `insert` / `erase` / `flush`, leaf and branch page splits, `promote_split_` branch promotion, fixed-root copy-up; char-key and numeric-key variants smoke-tested against `pmsys_imported.add`.

### Working on a milestone

1. Brainstorm the milestone briefly against the spec above to surface anything that changed since the original design was written.
2. Write its implementation plan into `docs/superpowers/plans/YYYY-MM-DD-openads-mN-<topic>.md` using the same TDD bite-sized template as M0.
3. Execute the plan task by task. Each task is `red → green → commit` and lands one focused change.
4. When the milestone is done, mark it green in the table above, push, and tag the head commit `mN-done` for traceability.

---

## Triggers

A trigger is a piece of code (similar to a stored procedure) that executes on the server automatically in response to an INSERT, UPDATE, or DELETE operation.  Triggers differ from stored procedures in that they are not called by the client but fire implicitly whenever a matching DML event occurs on the bound table.  Triggers are supported for both SQL and navigational (record-level API) update operations.

### Trigger timing

Three timing values control when the trigger fires relative to the DML event:

| Timing | Value | Description |
|--------|-------|-------------|
| `BEFORE` | 1 | Fires before the record is written. The row has not yet been committed to the table. |
| `INSTEAD OF` | 2 | Fires **instead of** the actual write. The engine skips the original DML; the trigger body is responsible for performing the write (e.g. `INSERT INTO mytable SELECT * FROM __new`). |
| `AFTER` | 4 | Fires after the record has been committed to the table. This is the most common timing. |

Firing order when multiple triggers exist for the same event and timing: triggers are sorted by **priority** (ascending — lower number fires first).

### Special in-memory tables: `__new`, `__old`, `__error`

Three virtual in-memory tables are available inside a trigger body:

**`__new`** — available for INSERT and UPDATE triggers.  Contains one row with the same field definitions as the base table.  For BEFORE and INSTEAD OF triggers the values are those *about to be written*; for AFTER triggers the values are those *just written*.

```sql
-- Read a field from the new record
SELECT empid FROM __new
```

**`__old`** — available for UPDATE and DELETE triggers.  Contains one row with the field values *before* the current operation was performed.

**`__error`** — used to return an error from inside a trigger.  The table has two fields:

```
INTEGER  errno
MEMO     message
```

If the trigger body inserts a row into `__error`, the server returns the `errno` and `message` as an error to the client.  `errno` is optional; if omitted, a default trigger failure code is returned.

```sql
-- Abort the operation with a custom error
INSERT INTO __error (errno, message) VALUES (1001, 'Value out of range');
```

> **Note:** The `__error` table can hold only one row.  DELETE operations on `__error` are not permitted.
> VarChar fields are not represented in `__old` / `__new` — use Char instead.

### INSTEAD OF trigger pattern

An INSTEAD OF trigger intercepts the original DML and replaces it with trigger-controlled logic.  A common use case is to modify a field before committing:

```sql
-- INSTEAD OF INSERT trigger that stamps the current timestamp
UPDATE __new SET date_inserted = (SELECT now() FROM system.iota);
INSERT INTO mytable SELECT * FROM __new;
```

Constraints on INSTEAD OF triggers:
- Only **one** INSTEAD OF trigger per event type (INSERT / UPDATE / DELETE) per table.
- A table that has a BEFORE trigger cannot also have an INSTEAD OF trigger for the same event, and vice versa.
- If an INSTEAD OF trigger fires, any AFTER triggers for the same event on that table **do not** fire.

### Trigger options

When creating a trigger the following options modify its behavior:

| Option | Bitmask | Description |
|--------|---------|-------------|
| `NO VALUES` (bit 0x01 clear) | — | The `__new` and `__old` tables are not built.  Use when your trigger body does not reference either table.  Improves performance. |
| `NO MEMOS/BLOBS` (bit 0x02 clear) | — | `__new` and `__old` are built but memo / blob / binary fields are excluded.  Use when you only need scalar fields from those tables.  Improves performance on tables with large memo or blob columns. |
| `PRIORITY` | — | Integer firing order.  Lower values fire first.  Affects all triggers with the same event type and timing on a table. |
| `NO TRANSACTION` | bit 0x04 | Disables the implicit transaction wrapping that surrounds trigger execution (see below).  Use only when performance matters more than atomicity. |

> **Note:** If a table has multiple triggers and at least one requires memo/BLOB data in the values tables, all triggers for that table will receive memo/BLOB data.

### Implicit transactions

All operations performed inside a trigger are wrapped in an implicit transaction:

- If the trigger (or any nested trigger) fails, all operations are rolled back and the database is returned to its pre-trigger state.
- If a transaction is already active when the first trigger fires, a **savepoint** is set instead of a new transaction.  On failure the engine rolls back to that savepoint.
- Because triggers use implicit transactions, beginning a transaction *inside* a trigger is not permitted.  Committing or rolling back from inside a trigger produces undefined behavior.

The `NO TRANSACTION` option disables this behavior for performance-critical triggers that do not require atomicity.  This flag applies per-event: if any trigger for a given event (INSERT, UPDATE, or DELETE) specifies `NO TRANSACTION`, all triggers for that event on the table will skip the implicit transaction.

### Nested and recursive triggers

Triggers may nest: an operation inside trigger A can cause trigger B to fire.  OpenADS limits nesting to **64 levels**; if this depth is exceeded an error is returned to prevent runaway triggers from monopolising server resources.

- INSTEAD OF and AFTER triggers do **not** recurse on their own base table — they may modify other tables, which can fire *those* tables' triggers, but they will not fire themselves again for the same table.
- BEFORE triggers follow the same rule.

It is the developer's responsibility to design triggers that do not create deadlock.  For example: if trigger A on tableA updates tableB, and trigger B on tableB updates tableA, a deadlock can occur when two connections concurrently update those tables.  **OpenADS has no deadlock detection or recovery.**  Avoid circular trigger chains.

### Data operations inside a trigger bypass user privileges

DML performed by trigger code runs without checking the calling user's permissions.  This lets you hide tables from end users while still providing controlled access through triggers.

### Disabling and enabling triggers

Triggers can be disabled and enabled using the system stored procedures `sp_DisableTriggers` and `sp_EnableTriggers`.

```sql
-- Disable all triggers for the current connection only (not persisted)
EXECUTE PROCEDURE sp_DisableTriggers('CURRENT USER');

-- Re-enable for the current connection
EXECUTE PROCEDURE sp_EnableTriggers('CURRENT USER');

-- Disable all triggers for all users (persisted in the DD)
EXECUTE PROCEDURE sp_DisableTriggers('ALL');

-- Disable all triggers on a single table (persisted)
EXECUTE PROCEDURE sp_DisableTriggers('employees');

-- Disable a single named trigger (persisted)
EXECUTE PROCEDURE sp_DisableTriggers('MyAuditTrigger');
```

Scope rules:

| Scope | Persistence | Effect |
|-------|-------------|--------|
| `CURRENT USER` (or no argument) | Not persisted | Only the current connection's triggers are affected.  Other connections are unaffected. |
| `ALL` | Persisted | All triggers in the database are disabled for all users. |
| *table name* | Persisted | All triggers bound to that table are disabled for all users. |
| *trigger name* | Persisted | Only that trigger is affected, for all users. |

Changes take effect immediately.

### Known limitations

- **No deadlock detection** — it is the developer's responsibility to avoid circular trigger chains (see above).
- **Cursors and flow control** — `DECLARE ... CURSOR`, `OPEN`, `FETCH`, `CLOSE`, `WHILE`, `IF/ELSE` blocks, and `EXECUTE PROCEDURE` inside trigger bodies are currently parsed but not executed.  Simple DML statements (`INSERT`, `UPDATE`, `DELETE`, `SELECT`) with `__new`/`__old` field substitution and `@variable` substitution work correctly.
- **SQL paths fire once per batch** — for SQL `UPDATE` / `DELETE` / `INSERT` that affect multiple rows, triggers fire once for the entire batch (not once per row).  Navigational (record-level) operations fire once per `AdsWriteRecord` / `AdsDeleteRecord` call.

---

## License

Apache License 2.0. See [`LICENSE`](LICENSE) for the full text and
[`NOTICE`](NOTICE) for attribution metadata.

Bundled third-party components keep their own licenses:

- `third_party/tinyaes/` — tiny-AES-c by kokke, released into the public
  domain under the Unlicense (https://unlicense.org/).
- `third_party/doctest/` — doctest by onqtam, released under the MIT
  License.
- `third_party/utf8.h/` — utf8.h by sheredom, released under the
  Unlicense.
