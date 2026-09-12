# OpenADS v1.09.45

## Every nav carries its counts

v1.09.44 (70 s) showed the residue lives in orders: phantom
`RecNo` and key counts the derivations assumed were natural.
This release certifies them where they are instead.

### Changes

- **Ordered-phantom `RecNo` derivation.** The twin reports
  physical `LastRec+1` past the last key (same engine rule),
  so the EOF derivation (`cached_count + 1`, no frame, no
  currency) drops its natural-order restriction. Filter/AOF/
  scope exclusions stay.
- **RefreshRecord carries its row.** Trailer + bound tail on
  the ack; the client repopulates row, bounds, recno and
  count. Same-record restores and skip-settles lose their
  follow-up frames. No cursor-seq bump (refresh never moves).
- **Key-count piggyback on fused navs only.** The server
  certifies the just-installed order's count past the bound
  tail; the client stores it in the per-order map. Plain navs
  pay no extra server work.
- **Tests.** Ordered-phantom values, fused-count zero-frame,
  refresh row+truth.

### Test Results

- MinGW-x64 (GCC 16.2) + strict clang: batching suites green;
  full unit suite 1567/1568 (sole failure pre-existing,
  MinGW-only CDX alloc-tail).
- Full matrix (Windows MSVC x64/x86, Linux, macOS) runs in CI on the
  tag push.

### Packages

- `openads-1.09.45-windows-x64.zip`
- `openads-1.09.45-windows-x86.zip`
- `openads-1.09.45-linux-x64.tar.gz`
- `openads-1.09.45-macos-universal.tar.gz`

*Field triage, release engineering and validation: Pritpal Bedi.*
