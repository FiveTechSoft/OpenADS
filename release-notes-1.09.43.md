# OpenADS v1.09.43

## Counts without frames

v1.09.42 (75 s) left three count-shaped costs: per-USE record
counts, phantom `RecNo` after cross-table eviction, and key
counts refetched on every rotation revisit. All three now
serve locally.

### Changes

- **Record-count piggyback.** Nav acks append `[u32 reccount]`
  (in-memory server count — same trust as the count cache;
  the wire `GetRecordCount` keeps its disk refresh for
  callers that need multiuser-fresh). Served while
  sequence-fresh, then promoted into the sticky cache.
- **Phantom `RecNo` derivation.** An EOF phantom in natural
  order without filters is `cached_count + 1` — a pure
  function of certified flags plus the cached count, so no
  frame and no currency to evict. Ordered/scoped phantoms
  keep the wire path.
- **Per-order key-count map.** Order switches don't change
  any order's count, so rotation revisits serve from the map
  instead of refetching. True invalidations (writes, scope /
  filter changes — a gap the suite caught on the way — adopt,
  refresh, pack/zap) clear it.
- **Tests.** Count certification, cross-table phantom
  derivation, per-order revisit.

### Test Results

- MinGW-x64 (GCC 16.2) + strict clang: batching suites green;
  full unit suite 1566/1567 (sole failure pre-existing,
  MinGW-only CDX alloc-tail).
- Full matrix (Windows MSVC x64/x86, Linux, macOS) runs in CI on the
  tag push.

### Packages

- `openads-1.09.43-windows-x64.zip`
- `openads-1.09.43-windows-x86.zip`
- `openads-1.09.43-linux-x64.tar.gz`
- `openads-1.09.43-macos-universal.tar.gz`

*Field triage, release engineering and validation: Pritpal Bedi.*
