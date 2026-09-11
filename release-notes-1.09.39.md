# OpenADS v1.09.39

## Index park: repeat clears keep the snapshot

v1.09.38 (index-binding park) measured **identical** in the field
(94 s, byte-identical opcode census): the park never fired. The
RDD issues `OrdListClear` twice per tag rotation — the first
`CloseAll` parked a good snapshot and the second moved empty maps
over it, so every reopen missed and paid both frames. This
release fixes the park and traces its decisions.

### Changes (vs v1.09.37)

- **Index-binding park** (from .38, now actually engaging):
  `CloseAll` snapshots live tag→id maps with the server bindings
  still open; same-bag `OpenIndex` restores with zero frames;
  binding-only ops adopt silently; real closes only for
  different bags and structural changes. No protocol change.
- **Double-clear fix (.39):** `CloseAll` snapshots only when
  maps are live — a repeat clear keeps the existing snapshot.
  Genuinely empty closes still pend a real close.
- **Park tracing:** `cli_trace.log` gains `AdsCloseAllIndexes`
  (parked / repeat-clear / nothing-open) and `AdsOpenIndex`
  (unpark hit / miss) lines, so the field run proves the hit
  rate directly.
- Order-divergence guard, flush-merge precedence, real-close
  map clearing, create/erase invalidation, and the 4 park tests
  (incl. the double-clear rotation shape).

### Test Results

- MinGW-x64 (GCC 16.2): batching suites green; full unit suite
  1558/1559 (sole failure pre-existing, MinGW-only CDX alloc-tail).
- Full matrix (Windows MSVC x64/x86, Linux, macOS) runs in CI on the
  tag push.

### Packages

- `openads-1.09.39-windows-x64.zip`
- `openads-1.09.39-windows-x86.zip`
- `openads-1.09.39-linux-x64.tar.gz`
- `openads-1.09.39-macos-universal.tar.gz`

*Field triage, release engineering and validation: Pritpal Bedi.*
