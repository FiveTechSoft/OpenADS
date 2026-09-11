# OpenADS v1.09.35

## Metadata caches + full-distribution diagnostics

Follow-up to the fused release: two per-USE metadata calls that each
cost a round-trip, and the instrument that ends partial-data analysis.

### Changes

- **Table-type + record-length caches.** `AdsGetTableType` and
  `AdsGetRecordLength` go out once per open handle instead of once
  per USE (~170 frames per startup). Both are immutable for an open
  table, so no invalidation paths are needed.
- **Full-distribution opdump.** The `OPENADS_OPDUMP=1` background
  sampler now logs every non-zero opcode sorted by count, not just
  the top 5 — field triage no longer works from partial data.
- **Grid trace format.** `cli_trace.log` is fixed-width columns
  (`ms | alias | op | detail`): same-millisecond runs read as local
  answers, RTT-sized jumps as wire frames, per table.

### Compatibility

- Client-side caching only; no wire changes. Mixed old/new peers
  behave as before.

### Test Results

- MinGW-x64 (GCC 16.2): batching suites green; full unit suite
  1551/1552 (sole failure pre-existing, MinGW-only CDX alloc-tail).
- Full matrix (Windows MSVC x64/x86, Linux, macOS) runs in CI on the
  tag push.

### Packages

- `openads-1.09.35-windows-x64.zip`
- `openads-1.09.35-windows-x86.zip`
- `openads-1.09.35-linux-x64.tar.gz`
- `openads-1.09.35-macos-universal.tar.gz`

*Field triage, release engineering and validation: Pritpal Bedi.*
