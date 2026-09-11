# OpenADS v1.09.37

## Rebuild of the reposition-bound payload with a green matrix

v1.09.36 shipped the right payload (bound piggyback on
`GotoRecordAck`/`SeekAck` + phantom `RecNo` cache, ~40–45 s of WAN
startup) but an unused variable in its new test file tripped
`-Werror` on Clang/MSVC, so every CI leg failed at Build and only
three of the four packages published (Linux missing). This release
is the same engine with that one line fixed and the full set below.

### Changes (vs v1.09.35)

- **Bound piggyback on `GotoRecordAck`/`SeekAck`.** The server
  appends `[u8 bof][u8 eof][u32 recno]` after the row trailer — it
  just positioned explicitly, so it knows all three exactly. The
  client certifies its boundary flags, bound cache and phantom
  recno from them: one frame per reposition instead of 3–4.
  Trailing section, length-gated, no capability bit (same
  convention as the M12.24 row trailer and the twin flag).
- **Phantom `RecNo` cache**, same currency as the bound cache.
- **Trace** gains `AdsGotoRecord`/`AdsSeek`/`AdsSeekLast` lines.
- **Tests** gain 3 opcode-counter cases (mid/past-end goto,
  first-key hit, hard-miss seek).

### Test Results

- MinGW-x64 (GCC 16.2): batching suites green; full unit suite
  1554/1555 (sole failure pre-existing, MinGW-only CDX alloc-tail).
- Full matrix (Windows MSVC x64/x86, Linux, macOS) runs in CI on the
  tag push.

### Packages

- `openads-1.09.37-windows-x64.zip`
- `openads-1.09.37-windows-x86.zip`
- `openads-1.09.37-linux-x64.tar.gz`
- `openads-1.09.37-macos-universal.tar.gz`

*Field triage, release engineering and validation: Pritpal Bedi.*
