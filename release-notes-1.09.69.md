# OpenADS v1.09.69

## REMOTE logical writes store `T`/`F` (conditional-index fix)

Over REMOTE (`tcp://` to `openads_serverd`), setting a logical field
(`RLock()`, `FieldPut()`, `DbCommit()`) stored the raw byte `'1'`/`'0'`
instead of the DBF logical byte `'T'`/`'F'`.

The app read the value back as `.T.` (`AdsGetLogical` accepts `'1'`),
but index `FOR` evaluation and every other driver (DBFCDX, SAP ADS)
read `'1'` as `.F.` — so the record stayed in `FOR comple = .F.` tags
(open work orders kept showing closed ones) and reindexes built
conditional tags wrongly. Production byte scans found ~200 such bytes
in 9 tables, all written over REMOTE.

### Changes

* `src/abi/ace_exports.cpp` — remote `AdsSetLogical()` sends the DBF
  byte itself (`T`/`F`), not `"1"`/`"0"`. Works against old servers too.
* `src/drivers/dbf_common.cpp` — `encode_field_string()` normalizes any
  string into a logical field (`T t Y y 1` -> `T`, blank stays blank,
  anything else -> `F`). Covers the twin-handle `AdsSetString` path,
  local `AdsSetString` on logicals, and SQL string sources.
* `tests/unit/abi_remote_logical_write_test.cpp` — regression: raw-byte
  `T`/`F` on disk without index, plus `FOR`-tag maintenance
  (`LOGIN`/`LOGOUT`) through the indexed twin-handle path.

### Existing data

Files written by an unpatched REMOTE server keep their `'1'`/`'0'`
bytes until repaired: rewrite them offline (`'1'`->`'T'`, `'0'`->`'F'`,
logical fields only) and reindex.

### Test Results

* New suites: remote logical plain + indexed `FOR`-tag cases green.
* Full unit suite (MinGW-x64): 1593/1594 — sole failure the
  pre-existing MinGW-only CDX alloc-tail case
  (`CdxIndex create resets page allocator tail`), which fails
  identically on the clean tree.
* Full matrix (Windows MSVC x64/x86, Linux, macOS) runs in CI on the
  tag push.

### Packages

* `openads-1.09.69-windows-x64.zip`
* `openads-1.09.69-windows-x86.zip`
* `openads-1.09.69-linux-x64.tar.gz`
* `openads-1.09.69-macos-universal.tar.gz`

*Fix from a contributor patch on top of v1.09.68, verified against a
production `EPRORD.DBF` (45,316 records). Release engineering and
validation: Pritpal Bedi.*
