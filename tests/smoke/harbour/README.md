# OpenADS Harbour smoke tests

## Invoice fixture (`openads_cdx_invoice_fixture.prg`)

Creates four related free DBF/CDX tables under `F:\OpenADS\testdata\invoices\`
and populates them with random data. Verifies that the ADSCDX RDD produces
correct per-table compound index files and that indexed navigation works.

### Tables

| File | Records | CDX tags |
|------|---------|----------|
| `customer.dbf` | 100 | CUSTNO, CUSTNAME |
| `items.dbf` | 20 | ITEMNO |
| `invoices.dbf` | 1 000 | INVNO, CUSTNO, INVDATE |
| `invoicedetail.dbf` | 3 000 | INVNO, ITEMIDX |

Each table gets its own structural CDX (`customer.cdx`, etc.).
All tables are free tables — no data dictionary (`.add`) required.

### Prerequisites

| Component | Location |
|-----------|----------|
| Harbour 64-bit toolchain | `f:\harbour64` |
| OpenADS engine (`openace64.dll`) | `F:\OpenADS\build\msvc-x64\src\Release` |
| Visual Studio 2022 Community | default install path |
| MSYS `make.exe` | `F:\MinGW63\msys\1.0\bin` |

### Build

**Step 1 — build the Harbour toolchain (once):**

```cmd
build_harbour64.cmd
```

Compiles Harbour from source at `f:\harbour64` with MSVC 64-bit.
Output lands at `f:\harbour64\bin\win\msvc64\`.

**Step 2 — build the fixture:**

```cmd
build_openads_invoice_fixture.cmd
```

Produces `openads_cdx_invoice_fixture.exe` in this directory.
Links against `openace64.lib` (the current OpenADS engine).

### Run

**Unattended / CI** — no arguments:

```cmd
openads_cdx_invoice_fixture.exe
```

Rebuilds all tables from scratch and exits with code 0 on success.
The data directory is created automatically if it does not exist.
Existing tables are deleted and recreated for a known-good state.

**Interactive — browse each table after creation:**

```cmd
openads_cdx_invoice_fixture.exe BROWSE
```

Any command-line argument enables browse mode. After each table is
populated the program pauses with a full-screen record browser
(25 rows × 120 columns). The header bar shows the active index tag and
the current record position (`Rec X/Y`).

| Key | Action |
|-----|--------|
| ↑ ↓ | move one row |
| PgUp / PgDn | scroll one page |
| Ctrl+PgDn / Ctrl+PgUp | go to last / first record |
| ← → / Home / End | pan columns |
| **Space** | cycle controlling index (invoices only: INVNO → CUSTNO → INVDATE → …) |
| Esc | close browser, continue to next table |

### Source files

| File | Purpose |
|------|---------|
| `openads_cdx_invoice_fixture.prg` | Fixture source |
| `build_openads_invoice_fixture.cmd` | Build script |
| `build_harbour64.cmd` | Harbour toolchain build script |

### Design notes

- `RDDSETDEFAULT("ADSCDX")` is set once at startup; no `VIA` clause
  needed on individual `USE` statements.
- `SET FILETYPE TO CDX` selects compound (multi-tag) index files.
- `SetMode(25, 120)` resizes the console before any output so the
  browse fits within the window on any system.
- Tables are opened `SHARED NEW` so the fixture can run while the
  OpenADS server is active.
- The fixture links against `openace64.lib`, not the legacy `ace64.lib`.
- The invoices table carries three CDX tags (`INVNO`, `CUSTNO`,
  `INVDATE`). `OrdSetFocus` is called before the browse to start in
  invoice-number order; **Space** cycles through all three tags at
  runtime using `OrdSetFocus` + `oBrw:RefreshAll()` + `oBrw:GoTop()`.

---

# Harbour rddads smoke test (M8.1)

This directory contains a minimal Harbour `.prg` that links against
Harbour's `contrib/rddads` static library and the OpenADS-shipped
`ace64.dll` / `ace64.lib`. The point is to validate **end-to-end** that
every ACE entry point Harbour expects is resolvable from OpenADS.

## What we proved

`c:\harbour\lib\win\msvc64\rddads.lib` references **225 distinct `Ads*`
entry points**. After M8.1, OpenADS's `ace64.dll` exports all of them:
80 real implementations (M0-M7) + 146 stubs that return
`AE_FUNCTION_NOT_AVAILABLE` (5004) so the link succeeds.

Linking `smoke.prg` against `rddads.lib` + `ace64.lib` produces a clean
resolution of every `HB_FUN_ADSVERSION`/`AdsGetVersion`/etc. symbol
chain.

## M8.11 — Memo M-fields (FPT) round-trip

The fixture now declares a fifth column `NOTES M(10)` and the smoke
appends two rows with memo payloads, closes, and reopens to verify
the memo content survives:

```
Append row DELTA   NOTES = "Hello memo from OpenADS smoke test (M8.11)."
Append row OMEGA   NOTES = Replicate("Lorem ipsum dolor sit amet. ", 10)
=== Reopen + verify memo durability ===
Reopened count: 5
  Seek 'DELTA': Found=T len=43
    NOTES = [Hello memo from OpenADS smoke test (M8.11).]
  Seek 'OMEGA': Found=T len=280
    NOTES first 60 = [Lorem ipsum dolor sit amet. Lorem ipsum dolor ...]
```

ABI changes:

- `make_cdx.exe` now also stages an **empty data.fpt** alongside
  data.cdx via `FptMemo::create(...)`, so `Connection::open_table`
  finds a memo store to auto-attach when the DBF declares an M field.
- `make_data.ps1` flips the DBF signature from 0x03 to 0x30
  (FoxPro CDX with FPT memo file), declares a fifth `NOTES M(10)`
  field, and updates header/record lengths (193 / 33).
- `AdsGetMemoLength` / `AdsGetMemoDataType` / `AdsGetString` are now
  real implementations sitting in `ace_exports.cpp` (M4 had earlier
  versions; M8.11 normalised them to use `resolve_field_index` so
  the rddads `ADSFIELD(n)` call form lands in the right column).
- `AdsCloseTable` flushes the table before releasing the handle so
  non-transactional appends reach disk on `USE` close. With an
  explicit transaction, `commit_tx` already flushed and this call
  is a no-op.
- `ADS_MEMO_TEXT` / `ADS_MEMO_PICTURE` aliases now resolve to the
  M8.4-verified `ADS_STRING` (4) and `ADS_IMAGE` (7) values.

## M8.10 — Transactions (BeginTransaction / Rollback / Commit)

Real ARIES-style transactions through Harbour: BEGIN + APPEND +
ROLLBACK leaves the appended row in the DBF flagged deleted; BEGIN +
APPEND + COMMIT persists durably to both DBF and CDX.

```
Initial count:          3
=== Tx 1: append EPSILON, then rollback ===
  inside tx, count =    4
  after rollback, count = 4
=== Tx 2: append ZETA, then commit ===
  inside tx, count =    5
  after commit, count = 5
=== Reopen + verify ===
Reopened count:         5
  Seek 'ZETA'   : Found=T RecNo=5 AGE=60 Deleted=F   (committed)
  Seek 'EPSILON': Found=T RecNo=4 AGE=50 Deleted=T   (rolled back)
```

The smoke uses `AdsConnect()` + `AdsConnection()` to fetch the default
connection handle, then drives `AdsBeginTransaction(hConn)` /
`AdsRollback(hConn)` / `AdsCommitTransaction(hConn)` directly. Both
the DBF mutation and the CDX index update are bound to the WAL: ZETA
survives reopen and is reachable through `dbSeek`; EPSILON is on
disk with the deletion flag set, so `SET DELETED ON` hides it during
sequential walks while `Found()` still reports the seek result.

### Multi-index sync (engine)

The previous M8.8 implementation only synced the *active* order on
each `set_field` call; multi-tag CDX therefore left parked indexes
out of date after a `dbAppend` inside a transaction. M8.10 widens
the sync:

- `Table::register_extra_index_view(IIndex*)` /
  `Table::unregister_extra_index_view(IIndex*)` /
  `Table::clear_extra_index_views()` track the parked CDX sub-tags as
  non-owning views (the binding still owns the IIndex lifetime; the
  Table just borrows the pointer for sync).
- `Table::snapshot_index_keys_()` captures the pre-write key per
  index — active order plus extras — so `sync_all_indexes_(snap)`
  can erase the prior `(recno, prev_key)` from each index and insert
  the new one in lockstep.
- `Table::flush()` flushes the active order **and** every extra view
  so a multi-tag commit reaches disk for every tag.
- `table_for_index(hIndex)` now also activates the binding
  (previously only `get_table` did), so `AdsSeek` /
  `AdsGotoTop(hOrdCurrent)` etc. always reach the right active order
  even when rddads dispatches through the index handle directly.
- `AdsOpenIndex` registers each parked tag as an extra view at open
  time; `activate_binding` swaps the view registration in lockstep
  with the order swap so the table sees the right set after every
  `OrdSetFocus`.

## M8.9 — Multi-tag CDX + OrdSetFocus

`make_cdx.exe` now lays down a CDX with two tags, NAME (key over the
C(10) NAME column) and AGE (3-byte key over the N(3,0) AGE column).
The smoke walks each focus in turn, exercising `OrdSetFocus` and
`dbSeek`:

```
Number of orders: 2
Default order: AGE
=== Walk under NAME order ===
Active order: NAME   key: NAME
  rec 1 NAME=[ALPHA] AGE=30
  rec 2 NAME=[BETA]  AGE=125
  rec 3 NAME=[GAMMA] AGE=77
=== Walk under AGE order ===
Active order: AGE    key: AGE
  rec 1 NAME=[ALPHA] AGE=30   ( 30)
  rec 3 NAME=[GAMMA] AGE=77   ( 77)
  rec 2 NAME=[BETA]  AGE=125  (125)
=== Seek by AGE ===
  Seek ' 77': Found=T RecNo=3 NAME=[GAMMA] AGE=77
```

ABI changes:

- `AdsOpenIndex` widened to its real 4-arg signature
  `(hTable, pucName, ahIndex[], &pu16ArrayLen)`. For a CDX file we
  enumerate every tag through `CdxIndex::list_tags`, open each via
  `open_named`, and write a binding handle per tag into `ahIndex[]`.
  The first tag's IIndex moves into `Table::set_order` (becomes the
  default order); the rest stay parked in their bindings until
  `activate_binding` swaps them in.
- `Table::take_order()` (new) surrenders the active index's
  `unique_ptr<IIndex>` so the previous binding can park it on a
  focus swap. `Order::release()` (new) is the underlying primitive.
- `get_table` now calls `activate_binding(h)` whenever a navigation
  call arrives with an index handle; combined with `take_order` this
  means rddads' `pArea->hOrdCurrent` swaps drive the Table's active
  order in lock-step.
- `AdsGetIndexHandle` strips trailing whitespace / nulls from the
  caller's tag name (rddads space-pads to ADS_MAX_TAG_NAME).
- `AdsGetIndexName` and `AdsGetIndexExpr` now read from the binding's
  own metadata instead of the Table's currently-active order, so
  parked tags report their real name + expression even before they
  become live.
- `AdsGetNumIndexes` counts bindings for the table (was always 1
  when an order was set).

## M8.8 — Active index auto-sync on dbAppend / FIELD-> := value

The active CDX is now updated on every record mutation, so dbSeek
finds appended rows after reopen:

```
Walk in NAME order: ALPHA, BETA, DELTA, GAMMA   (DELTA visible!)
Reopen & dbSeek 'DELTA': Found=T RecNo=4 NAME=[DELTA] AGE=99
                                       ACTIVE=F BORN=20260101
```

Implementation:

- `Table::compute_index_key_(expr, key_len)` evaluates the active
  index's key expression against the current `record_buf_`. Bare
  field-name expressions (`NAME`) are supported now; compound
  expressions (`UPPER(NAME)`, concatenations, ...) land later.
- `Table::sync_active_index_(prev_key)` is called after every
  `set_field` overload writes back. It computes the new key, erases
  the prior `(recno, prev_key)` entry if it differs, and inserts
  `(recno, new_key)`. For dbAppend the previous record is empty so
  the prior key is all-spaces; the helper inserts the real key on
  the first set_field call that touches the indexed column.
- `Table::flush()` now flushes both the driver and the active
  index, so AdsWriteRecord on a row whose key changed persists the
  CDX update before the call returns.

## M8.7 — Write path: dbAppend + FIELD-> := value

The smoke now exercises `dbAppend()` plus per-field assignment, then
closes and reopens to verify durability:

```
Initial record count: 3
Append a fourth row...
  appended at recno 4
Walk in NAME order: ALPHA, BETA, GAMMA, DELTA   (in DBF order; only
                                                 first 3 visible
                                                 through the index)
Reopened record count: 4
```

The DBF mutation is durable: the appended record survives reopen,
record count climbs from 3 to 4, and the field bytes are intact on
disk. ABI-side fixes:

- `AdsSetString` / `AdsSetLogical` / `AdsSetDouble` now resolve
  `pucField` with `resolve_field_index` (previously they only
  accepted string field names; rddads passes `ADSFIELD(idx)` =
  small-int field index cast to a pointer).
- `AdsSetLongLong` is now a real wrapper that forwards to
  `AdsSetDouble` (was a 5004 stub; rddads uses it for
  HB_FT_INTEGER columns).
- `AdsSetJulian` converts the Clipper Julian Day Number back to
  `YYYYMMDD` ASCII and writes it through `Table::set_field`. The
  `julian_to_ymd` helper is the inverse of M8.5's `to_julian`.

### Known gap (closed by M8.8 below)

After dbAppend at the M8.7 layer, the active CDX was not updated.
M8.8 wires `Table::set_field` / `Table::flush()` into the active
`Order` so an `insert(recno, key)` runs after every record write.

## M8.6 — Index seek through OpenADS' CDX

The smoke now stages a CDX alongside the DBF (built by `make_cdx.exe`,
which calls OpenADS' own `CdxIndex::create` + `insert`), opens both via
`USE data INDEX data VIA "ADSCDX"`, walks records in NAME order, then
exercises three `dbSeek` calls:

```
OrderName: NAME
OrderKey : NAME
Walking 3 records in NAME order:
  rec 1 NAME=[ALPHA] AGE=30  ACTIVE=T BORN=19900101
  rec 2 NAME=[BETA]  AGE=125 ACTIVE=F BORN=20000615
  rec 3 NAME=[GAMMA] AGE=77  ACTIVE=T BORN=20251231
Seek 'BETA':  Found=T RecNo=2 NAME=[BETA]
Seek 'GAMMA': Found=T RecNo=3 NAME=[GAMMA]
Seek 'NOPE':  Found=F EOF=T
```

This validates the entire M3.x CDX implementation through a real
consumer: rddads reads OpenADS' compound CDX, locates each tag, walks
the B+tree, and the `Found()` flag reflects the engine's actual
seek-hit state.

### M8.6 fixes

- New `make_cdx.exe` CMake target writes a CDX from
  `tests/smoke/harbour/make_cdx.cpp` using OpenADS' `CdxIndex::create`.
  `run_build.bat` invokes it after the DBF fixture so each smoke run
  starts from clean disk state.
- `Table::path()` accessor lets the ABI layer resolve relative index
  paths (e.g., `INDEX data` -> `<table_dir>/data.cdx`).
- `AdsOpenIndex` now resolves the `pucName` argument relative to the
  table's directory and auto-appends `.cdx` when the caller passed a
  bare alias.
- `get_table` now falls back to `lookup_table_by_index` when the
  handle isn't a registered Table — real ACE polymorphism: rddads'
  `adsGoTop` calls `AdsGotoTop(pArea->hOrdCurrent)` (the **index**
  handle) when an order is active, and we have to navigate the same
  underlying Table for it to work.
- `AdsSeek` now matches rddads' real 6-arg signature
  `(hIndex, pucKey, u16KeyLen, u16KeyType, u16SeekType, &pbFound)` —
  the previous 4-arg shape was reading garbage from the stack and
  causing a segfault. `AdsSeekLast` was widened to match.
- New `Table::last_seek_found_` flag, set inside `seek_key`, is
  surfaced through a real `AdsIsFound` implementation. rddads'
  `hb_adsUpdateAreaFlags` calls `AdsIsFound` after every seek to
  decide whether `Found()` should report `.T.`.

## M8.5 — Multi-field DBF (C / N / L / D)

The fixture now declares four fields (NAME C(10), AGE N(3,0),
ACTIVE L(1), BORN D(8)) and three records, and the smoke prints
each field per row:

```
Schema:
  1 NAME   C len=10 dec=0
  2 AGE    N len= 3 dec=0
  3 ACTIVE L len= 1 dec=0
  4 BORN   D len= 8 dec=0
Walking 3 records:
  rec 1 NAME=[ALPHA] AGE=30  ACTIVE=T BORN=19900101
  rec 2 NAME=[BETA ] AGE=125 ACTIVE=F BORN=20000615
  rec 3 NAME=[GAMMA] AGE=77  ACTIVE=T BORN=20251231
```

Implementations landed for the per-type ACE getters rddads' adsGetValue
calls into:

- `AdsGetFieldDecimals` reads `DbfField::decimals` (was a 5004 stub —
  rddads previously left `dec` uninitialised at 65535).
- `AdsGetLong` returns the numeric field rounded to a 32-bit integer.
- `AdsGetDouble` returns the numeric field as a double.
- `AdsGetJulian` parses the 8-byte `YYYYMMDD` date string and returns
  the Clipper Julian Day Number, computed with the same Gregorian
  formula Harbour core uses (`hb_dateEncode`).

## M8.4 — ACE field-type constants verified

Empirical sweep against `c:\harbour\lib\win\msvc64\rddads.lib`: probe
`AdsGetFieldType` returning each value 0..40, observe Harbour's
`FieldType()`. Mapping captured in `include/openads/ace.h`. Notably,
this Harbour build was compiled against an `ace.h` where
`ADS_LOGICAL = 1` and `ADS_STRING = 4` — the inverse of the commonly
cited public ACE SDK layout. `map_field_type` now returns 4 for
`Character`; `Field 1 type` shows the canonical Clipper `'C'` (M8.3
showed `'CICHARACTER'`, the case-insensitive alias at value 20).

## M8.3 — DBF walk via Harbour rddads

The smoke now stages a 88-byte DBF (`make_data.ps1`), opens it through
the standard Clipper RDD surface (`USE data VIA "ADSCDX"`), and walks
its records:

```
OpenADS smoke test (M8.3)
ACE DLL reports: 0.0a
AdsConnect handle: .T.
Opening data.dbf VIA ADSCDX...
Field count:          1
Field 1 name : NAME
Field 1 type : CICHARACTER len=         10
Record count:          2
Walking records:
  rec          1 name=[ ALPHA ]
  rec          2 name=[ BETA ]
Done.
```

This proves the full chain: Harbour PP → rddads.lib's ADSCDX RDD →
OpenADS' `AdsConnect` / `AdsOpenTable` / `AdsAtEOF` /
`AdsGetRecordNum` / `AdsGetField` / `AdsSkip` / `AdsCloseTable` /
`AdsDisconnect`. Records stream back from the OpenADS engine into a
running Harbour process exactly as they would from SAP's `ace64.dll`.

### Fixes landed in M8.3

- `Connection::open_table` now auto-appends `.dbf` when the caller
  passes a bare alias (rddads' convention; mirrors Clipper DBFCDX).
- `AdsGetField` / `AdsGetFieldType` / `AdsGetFieldLength` now resolve
  `pucField` either as a NUL-terminated field name *or* — courtesy of
  ACE's `ADSFIELD(n)` macro — as a small integer field index cast to
  a pointer. A new `resolve_field_index` helper in `ace_exports.cpp`
  handles both.
- `AdsConnect(server, &hConnect)` is now a real wrapper around
  `AdsConnect60` instead of a stub returning 5004; rddads'
  `HB_FUNC(ADSCONNECT)` calls it for `AdsConnect(".")` from Harbour.
- `AdsGetFieldRaw` forwards to `AdsGetField` (used by rddads when
  OEM translation is on; charset translation is a no-op for ASCII
  fixtures).
- `AdsIsFound` / `AdsGetLogical` are no longer stubs returning 5004 —
  the former reports 'not found' (we don't yet track last-seek-hit
  state), the latter reads the underlying field through `AdsGetField`
  and decodes 'T' / 'Y' as true.
- `map_field_type(Character)` now returns **20** (`ADS_CISTRING` in
  rddads' compiled-in `ace.h`) instead of 1; an empirical probe
  showed value 1 routes to `ADS_LOGICAL`. Documenting OpenADS'
  authoritative `ace.h` constant set is M8.4.

## End-to-end validation result (M8.2)

`smoke.exe` builds **and runs**. Output:

    OpenADS smoke test
    rddads version probe...
    ACE DLL reports: 0.0a

The flow exercised:

1. Harbour PP compiles `smoke.prg` and resolves `AdsVersion()` to the
   `HB_FUN_ADSVERSION` wrapper inside `rddads.lib`.
2. `rddads.lib`'s wrapper calls `AdsGetVersion(...)` — a true import
   resolved through the OpenADS-shipped `ace64.lib` import library.
3. At runtime, `ace64.dll` (loaded from `c:\harbour\bin\win\msvc64\`)
   answers the call and returns OpenADS' version string.

### Legacy CRT shims

Harbour's prebuilt `msvc64` libs were compiled against MSVC 2013-era
CRT entry points that disappeared in the VS2015 UCRT split. To keep
Harbour usable without rebuilding Harbour itself, `ace64.dll` exports
shims for the missing symbols (`abi/legacy_crt_shims.cpp`):

| Legacy symbol | Replacement                                 |
|---------------|---------------------------------------------|
| `_dclass`     | `std::fpclassify`                           |
| `_dsign`      | `std::signbit`                              |
| `_wfsopen`    | UCRT `_wfsopen`                             |
| `_getch`      | UCRT `_getch` (used by gtstd)               |
| `_kbhit`      | UCRT `_kbhit` (used by gtstd)               |
| `_eof`        | UCRT `_eof`   (used by gtstd)               |

`openads_ace.def` aliases these legacy names onto OpenADS-prefixed
implementations so the DLL exports the names hbcommon / gtstd expect.

### Drop-in install

`run_build.bat` (and any future automation) currently overwrites the
two Harbour-shipped artefacts:

    c:\harbour\lib\win\msvc64\ace64.lib   (import lib for the DLL)
    c:\harbour\bin\win\msvc64\ace64.dll   (loaded at runtime)

with the OpenADS-built versions. After that, `hbmk2 -comp=msvc64
-gtstd -lrddads -lace64 smoke.prg` produces a runnable executable.

## Running

```cmd
:: From the OpenADS root:
cmake --build build\default --config Release
cd tests\smoke\harbour
run_build.bat
```

`run_build.bat`:
1. Calls `vcvars64.bat` to bring the MSVC link toolchain into PATH.
2. Puts `c:\harbour\bin\win\msvc64` and the OpenADS Release output on
   PATH so `hbmk2` and `ace64.dll` are found.
3. Invokes `hbmk2 -comp=msvc64 -lrddads -L<openads-out> -lace64`.

## AdsMg* telemetry (M9.25)

Build `contrib/rddads/tests/manage.prg`, then run it against a live
`openads_serverd`:

    manage

Expected: with at least one other client connected, the report shows
non-zero `Connections` / `WorkAreas` / `Tables` counters and a
non-zero `Up Time`, instead of the all-zero output the earlier stub
produced. `AdsVersion( 3 )` continues to report the OpenADS engine
string. The `AdsMg*` management functions (`AdsMgGetActivityInfo`,
`AdsMgGetInstallInfo`, `AdsMgGetCommStats`, `AdsMgGetConfigInfo`,
`AdsMgGetUserNames`, ...) are now backed by the real `MgCollector`
telemetry subsystem rather than zero-fill stubs.
