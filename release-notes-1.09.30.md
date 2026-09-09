# OpenADS v1.09.30

## Nav stamps survive read traffic (field fix for v1.09.29 batching)

v1.09.29's probe batching proved itself in tests but barely engaged
in production (2 dedupes in 1896 trace lines). Root cause: the
invalidation sequence bumped on *every* wire frame, and real apps
interleave field reads between probes — every stamp died on the
first `FieldGet`, leaving 638 wire BOFs + 281 wire EOFs standing.

### Changes

- **Cursor-generation sequence.** The stamp sequence now bumps only
  on cursor/visibility-affecting frames (nav, seek, order, scope,
  AOF, show-deleted, writes, pack/zap/reindex — 32 methods); pure
  reads (fetch, counts, describe, keynum, boundary probes) never
  invalidate. Cross-station safety is unchanged: observing a change
  still requires a cursor-affecting frame.
- **Tests.** New "stamps survive read traffic" case (top → wire
  count read → boundaries still zero frames). Full suite 1540/1541
  (sole failure pre-existing, MinGW-only CDX alloc-tail).

### Compatibility

- Client-side sequencing only; no wire changes since v1.09.29.
  Mixed old/new peers behave as before.

### Test Results

- MinGW-x64 (GCC 16.2): batching suite 5/5; full unit suite
  1540/1541.
- Full matrix (Windows MSVC x64/x86, Linux, macOS) runs in CI on the
  tag push.

### Packages

- `openads-1.09.30-windows-x64.zip`
- `openads-1.09.30-windows-x86.zip`
- `openads-1.09.30-linux-x64.tar.gz`
- `openads-1.09.30-macos-universal.tar.gz`
