# OpenADS v1.09.48

## Session pool: thread-lane sessions per logical connect

Vouch is multi-threaded and a second thread joins at the last leg —
but one `RemoteConnection` serialised every frame on its mutex, so
all threads queued behind a single TCP session. Each `AdsConnect60`
(TCP and TLS) now opens a pool of sessions for the same logical
connection and pins each calling thread to a lane, so threads
proceed in parallel while each table stays on the session that
opened it. Single-threaded callers always land on lane 0
(behaviour identical). Disconnect, table-park and file-existence
teardown fan out pool-wide; dead lanes fail over. No protocol
change — old servers and clients interoperate.

Pool size via `OPENADS_POOL_SIZE` env / `pool_size` ini key
(default 4, clamp 1..16); `=1` restores single-session behaviour
without rebuilding.

### Changes

- **Session pool in the wire client** (`src/abi/ace_exports.cpp`):
  pool grow at connect (best-effort — failures degrade lane
  count), thread-affine lane pick at `AdsOpenTable`, pool-wide
  `AdsDisconnect`, park invalidation and drop/create/erase/rename
  flushes.
- **Tests:** new `network_pool_mt_test` (4 threads x 2 tables,
  sentinel values — any cursor cross-talk fails; Connect delta
  proves 4 sessions). `network_use_budget_test` snapshots after
  connect (pool setup is once-per-connection, amortised — not
  per-USE).

### Test Results

- MinGW-x64 (GCC 16.2): batching + pool suites green; full unit
  suite 1568/1569 (sole failure pre-existing, MinGW-only CDX
  alloc-tail).
- Full matrix (Windows MSVC x64/x86, Linux, macOS) runs in CI on the
  tag push.

### Packages

- `openads-1.09.48-windows-x64.zip`
- `openads-1.09.48-windows-x86.zip`
- `openads-1.09.48-linux-x64.tar.gz`
- `openads-1.09.48-macos-universal.tar.gz`

*Field triage, release engineering and validation: Pritpal Bedi.*
