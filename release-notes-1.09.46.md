# OpenADS v1.09.46

## Switch certifies its count

v1.09.45 (63 s) showed key counts following plain order
switches, not fused navs — the rotation goes switch → count →
nav, and the count itself flushed the switch plainly. This
release certifies the count in the switch ack, so the pair
costs one frame instead of two.

### Changes

- **Key-count piggyback on plain `SetOrder`/`SetOrderByName`
  acks** (trailing `[u32]`, length-gated). The client stores
  it into the per-order map (+ slot). Switch-adjacent counts
  — the rotation shape — go local with zero extra server
  work beyond what the switch already paid.
- **Tests.** Switch-certified counts; wire-count expectations
  updated to zero across the batching suites.

### Test Results

- MinGW-x64 (GCC 16.2) + strict clang: batching suites green;
  full unit suite 1567/1568 (sole failure pre-existing,
  MinGW-only CDX alloc-tail).
- Full matrix (Windows MSVC x64/x86, Linux, macOS) runs in CI on the
  tag push.

### Packages

- `openads-1.09.46-windows-x64.zip`
- `openads-1.09.46-windows-x86.zip`
- `openads-1.09.46-linux-x64.tar.gz`
- `openads-1.09.46-macos-universal.tar.gz`

*Field triage, release engineering and validation: Pritpal Bedi.*
