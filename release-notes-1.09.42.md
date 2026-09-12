# OpenADS v1.09.42

## Certified truth on every nav + open-bag existence

v1.09.41 (78 s) left reposition residue: every `GotoRecord`/`Seek`
was certified, but plain tops/bottoms/skips and per-USE existence
probes still paid frames. Same treatment, no protocol versioning
(trailing sections, length-gated).

### Changes

- **Bound+recno piggyback on `GotoTop`/`GotoBottom`/`Skip`
  acks.** `[u8 bof][u8 eof][u32 recno]` rides after the row
  trailer (post-lookahead position); the client certifies flags,
  bound cache and phantom recno. Covers table- and index-handle
  navs and the fused order-switch variants.
- **Open-bag existence short-circuit.** Existence probes for a
  bag bound by any live table on the connection answer locally
  (stem match across `.cdx`/`.z01` spellings). Negatives still
  go to the wire.
- **Tests.** Top/bottom/skip certification values, open-bag
  existence (+ missing-file wire check).

### Test Results

- MinGW-x64 (GCC 16.2) + strict clang: batching suites green;
  full unit suite 1563/1564 (sole failure pre-existing,
  MinGW-only CDX alloc-tail).
- Full matrix (Windows MSVC x64/x86, Linux, macOS) runs in CI on the
  tag push.

### Packages

- `openads-1.09.42-windows-x64.zip`
- `openads-1.09.42-windows-x86.zip`
- `openads-1.09.42-linux-x64.tar.gz`
- `openads-1.09.42-macos-universal.tar.gz`

*Field triage, release engineering and validation: Pritpal Bedi.*
