# OpenADS v1.09.31

## USE-teardown batching: the per-tag order-rotation storm

Field call log (9925 `Ads*` calls over a Vouch startup) showed the
real shape: per tag index, `SetOrder → Flush → CloseAll → OpenIndex`
(~5 frames ≈ 0.25 s/tag at 50 ms RTT — a 157-tag table alone costs
~40 s). Four kills, all client-transparent:

### Changes

- **FlushFileBuffers + CloseAllIndexes defer into CloseTable.**
  The server close already flushes data (via its shadow handle) and
  purges index bindings, so both frames per USE were pure waste. If
  any other wire op intervenes, they emit first, in order. Tables
  carrying either flag are never parked (a park performs no close);
  `OpenIndex`/`SetOrder`/`KeyCount` flush hooks added.
- **CheckExistence caches positives** per connection (169 probes in
  the trace). Negatives always go to the wire; drops, creates,
  erases and renames invalidate.
- **SetOrder skips when the ack-confirmed server binding already
  matches** (406 switches in the trace; SetOrder never moves the
  cursor, so repeats — including every pooled re-USE — cost zero).
- **Order-handle key counts ride the parent order cache**, with the
  invalidation `WriteRecord`/`SetRecord`/write-probe needed for
  FOR-index membership (caught by `abi_index_intensive2_test`).
- **Tests.** New `network_teardown_batch_test` (5 opcode-counter
  cases) + `network_use_budget_test` (full USE cycle ≤ 14 frames).

### Compatibility

- Client-side batching only; no wire changes since v1.09.29. Mixed
  old/new peers behave as before.

### Known issue

- The prebuilt MinGW import lib in the x86/x64 zips
  (`dist/import-libs/*/mingw/libace*.a`, Aug-29 build) predates
  `AdsGetServerVersion` — linking `OADS_SERVERVERSION` against it
  fails. Workaround: generate a fresh one from the shipped DLL
  (`dlltool --input-def <def> --dllname ace32.dll --output-lib
  libace32.a`); a regenerated `dist` ships in the next release.

### Test Results

- MinGW-x64 (GCC 16.2): batching suites green; full unit suite
  1546/1547 (sole failure pre-existing, MinGW-only CDX alloc-tail).
- Full matrix (Windows MSVC x64/x86, Linux, macOS) runs in CI on the
  tag push.

### Packages

- `openads-1.09.31-windows-x64.zip`
- `openads-1.09.31-windows-x86.zip`
- `openads-1.09.31-linux-x64.tar.gz`
- `openads-1.09.31-macos-universal.tar.gz`

*Field triage, release engineering and validation: Pritpal Bedi.*
