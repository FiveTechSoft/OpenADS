# OpenADS v1.09.41

## Clean flushes + order-aware park (RDD rules from source)

Two things unblocked this release. First, the client's CacheRDD
source plus Harbour's rddads settled the RDD contract from code
instead of traces: boundary state is RDD-owned (derived from the
cached position, no server calls), order ops are set-oriented
(Clear = local reset, Add = one fetch-all, Focus skipped when
same), and rddads' `adsOrderListClear` flushes unconditionally
while `adsGoTo` probes `RecNo` first (2001 workaround).

### Changes (vs v1.09.37)

- **Index-binding park, engaging.** Same-bag reopen: zero
  frames; binding-only ops adopt; real closes only for
  different bags and structural changes. Full rotation
  without reopen: one frame total. No protocol change.
- **Clean flushes set no flag.** `AdsFlushFileBuffers` defers
  only when the table is dirty (buffered sets, append, delete,
  recall — tracked per handle). The RDD's blind per-rotation
  flush on read-only tables costs nothing, so the park
  survives it. Durability contract unchanged: buffered data
  always forces the flag; commits flush explicitly.
- **Context-aware divergence guard** (navs with order context
  adopt; pristine servers need no close; stale orders close
  for real), parked order resolution (tag/handle/ordinal),
  flush-merge preserved for unparked closes, create/erase
  invalidation, park tracing.
- **Tests.** Park suite (5), clean/dirty/merged teardown
  (3 rewritten), reposition truth (3). Full suite 1561/1562
  (sole failure pre-existing, MinGW-only CDX alloc-tail).

### Test Results

- MinGW-x64 (GCC 16.2) + strict clang: batching suites green.
- Full matrix (Windows MSVC x64/x86, Linux, macOS) runs in CI on the
  tag push.

### Packages

- `openads-1.09.41-windows-x64.zip`
- `openads-1.09.41-windows-x86.zip`
- `openads-1.09.41-linux-x64.tar.gz`
- `openads-1.09.41-macos-universal.tar.gz`

*Field triage, release engineering and validation: Pritpal Bedi.*
