# OpenADS v1.09.50

## REVERSE() index function: UDF-based tags build correctly

Field (Vouch, remote ADSCDX): tags whose key expression calls the
Clipper-era `Reverse(cString)` UDF came out empty — the server
evaluator knew no `REVERSE`, so every key degraded to `""`
("unknown function") and all ordered navigation/seeks on the tag
missed (`COPY…WHILE` over such an order copied zero rows, while
loopback against a pre-existing good `.cdx` kept working).

### Changes

- **New builtin** (`src/engine/index_expr.cpp`): `REVERSE(cString)`
  byte-reverses its argument, reproducing the client-computed key
  exactly for single-byte (OEM/ANSI) data. Covers tag build,
  index maintenance on writes, and `FOR`-condition evaluation —
  all share the one evaluator. Client seeks (`s + Reverse(d)`,
  Vouch `seekSD`) were always correct; only the server side was
  broken.
- **Tests:** new `index_expr: REVERSE byte-reverses a string`
  regression case (full-width `C(10)`, composition with `UPPER`,
  literal with blanks).

### Test Results

- MinGW-x64 (GCC 16.2): new test green; #131 index suite 4/4;
  full unit suite 1574/1575 (sole failure pre-existing,
  MinGW-only CDX alloc-tail — verified it fails identically on
  the clean tree).
- Full matrix (Windows MSVC x64/x86, Linux, macOS) runs in CI on the
  tag push.

### Field action required

- **Rebuild the tag remotely** (drop/recreate or `REINDEX`) —
  the fix does not repair an already-degenerate `.cdx`.

### Packages

- `openads-1.09.50-windows-x64.zip`
- `openads-1.09.50-windows-x86.zip`
- `openads-1.09.50-linux-x64.tar.gz`
- `openads-1.09.50-macos-universal.tar.gz`

*Field triage, release engineering and validation: Pritpal Bedi.*
