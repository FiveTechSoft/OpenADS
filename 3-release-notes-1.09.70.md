# OpenADS v1.09.70

## xBase writes to MariaDB/MySQL: numeric and logical setters reach the staging buffer

`AdsSetDouble`, `AdsSetLong`, `AdsSetLongLong` and `AdsSetLogical` had
no MariaDB branch and fell through to the native-DBF table lookup,
failing with `5000 unknown table` on `mariadb://` tables. `REPLACE`
on numeric and logical fields was impossible; character and date
fields already worked because `AdsSetString` (and the date setters
that delegate to it) staged through the backend.

They now stage through `MariaConnection::set_field`, exactly like
`AdsSetString`: doubles are formatted with `"%.17g"` (round-trip
exact) and logicals as `"1"`/`"0"`, which MariaDB coerces into both
numeric (`TINYINT`) and `CHAR(1)` columns; `AdsGetLogical` accepts
both forms on read. This completes the xBase write path over MariaDB:
`APPEND BLANK` + `REPLACE` + commit now works for every field type.

### Changes

* `src/abi/ace_exports.cpp` — `OPENADS_WITH_MARIADB` branches in
  `AdsSetDouble`, `AdsSetLong`, `AdsSetLongLong`, `AdsSetLogical`.
* `tests/unit/abi_plus_mariadb_write_test.cpp` — live-server
  regression: numeric/logical INSERT and UPDATE round-trip through
  the setters, plus `AdsLockRecord`/`AdsUnlockRecord` contention
  between two MariaDB sessions. Skips cleanly without a server.

## Inter-process `.ntx` index lock, Harbour DBFNTX-compatible

The NTX driver now locks the `.ntx` file itself at byte
1,000,000,000, the same scheme Harbour's DBFNTX uses, so OpenADS,
Harbour and xHarbour/Clipper applications can share NTX indexes
across processes safely: reads take a shared lock, writes an
exclusive one held until `flush()`, and a version counter lets other
processes notice the change and reload the index. Before this, only
DBF record locks were aligned across processes and concurrent
writers could corrupt an `.ntx`.

### Changes

* NTX driver: inter-process lock implementation + header.
* New inter-process unit tests, including a two-process concurrent
  writer test.

### Test Results

* New suites: MariaDB setter round-trip (live-server gated) and NTX
  inter-process lock cases.
* Full matrix (Windows MSVC x64/x86, Linux, macOS) runs in CI on the
  tag push.

### Packages

* `openads-1.09.70-windows-x64.zip`
* `openads-1.09.70-windows-x86.zip`
* `openads-1.09.70-linux-x64.tar.gz`
* `openads-1.09.70-macos-universal.tar.gz`

*MariaDB setter gap reported on the FiveTech forum (walterburgos).
Release engineering and validation: Pritpal Bedi.*
