# OpenADS v1.09.47

## Storm revert: switch count withdrawn

Loopback bulk append (1 instance × 10 threads × 10 records)
convoyed from 0.283 s (through v1.09.45) to 8.2 s on v1.09.46.
Bisected to the one hot-path change: the key-count piggyback
on plain switch acks forced `commit_dirty_record` plus a count
walk on the twin per `SetOrder`, under the append storm's
index lock. Fused-only traffic never showed it.

### Changes

- **Reverted.** Plain `SetOrder`/`SetOrderByName` acks are
  empty again (exactly v1.09.45 behavior). Fused navs keep
  their count piggyback (browse-shaped traffic only).
  Client outcome parsing stays length-gated (inert,
  forward-compatible).
- Everything else from v1.09.46 stands (reposition truth,
  phantom derivation, per-order map, refresh row, park).

### Test Results

- MinGW-x64 (GCC 16.2) + strict clang: batching suites green;
  full unit suite 1567/1568 (sole failure pre-existing,
  MinGW-only CDX alloc-tail).
- Full matrix (Windows MSVC x64/x86, Linux, macOS) runs in CI on the
  tag push.

### Packages

- `openads-1.09.47-windows-x64.zip`
- `openads-1.09.47-windows-x86.zip`
- `openads-1.09.47-linux-x64.tar.gz`
- `openads-1.09.47-macos-universal.tar.gz`

*Field triage, release engineering and validation: Pritpal Bedi.*
