# OpenADS v1.09.36

## Reposition-bound piggyback (one frame per paint row)

Field census on v1.09.35 (**124 s**, 2600+ wire frames at ~48 ms RTT)
showed the paint-loop shape: every xBrowse row costs a reposition
(`GotoRecord` 266, `Seek` 174) followed by `RecNo` (548 wire),
`AtBOF` (352 wire) and a twin-covered `AtEOF` — 3 frames per row.
The twin itself verified perfect (3 `AtEOF` wire frames out of 801
polls). This release certifies the whole burst in the reposition ack.

### Changes

- **Bound piggyback on `GotoRecordAck`/`SeekAck`.** The server
  appends `[u8 bof][u8 eof][u32 recno]` after the row trailer — it
  just positioned explicitly, so it knows all three exactly. The
  client certifies its boundary flags, bound cache and phantom
  recno from them: one frame per reposition instead of 3–4.
  Trailing section, length-gated, no capability bit (same
  convention as the M12.24 row trailer and the twin flag): old
  clients ignore the tail, new clients against old servers fall
  back to the wire polls exactly as before. No Limbo rescue on
  the pack path (a rescue would move the cursor during what must
  stay a pure read); both-true genuinely means an empty cursor,
  and the values are identical to the old wire polls (the server
  twin is the same ACE engine either way).
- **Phantom `RecNo` cache.** `AdsGetRecordNum` with no valid row
  serves the certified recno while no cursor-affecting frame lands
  (same currency as the bound cache, cleared with it).
- **Trace.** `cli_trace.log` gains `AdsGotoRecord`/`AdsSeek`/
  `AdsSeekLast` lines with table alias — the previously invisible
  cache invalidators.
- **Tests.** 3 opcode-counter cases (mid/past-end goto, first-key
  hit, hard-miss seek). The suite caught a real bug on the way:
  the `has_row=0` trailer path returned before consuming the 2
  lookahead bytes, misaligning any trailing section by 2.

### Test Results

- MinGW-x64 (GCC 16.2): batching suites green; full unit suite
  1554/1555 (sole failure pre-existing, MinGW-only CDX alloc-tail).
- Full matrix (Windows MSVC x64/x86, Linux, macOS) runs in CI on the
  tag push.

### Packages

- `openads-1.09.36-windows-x64.zip`
- `openads-1.09.36-windows-x86.zip`
- `openads-1.09.36-linux-x64.tar.gz`
- `openads-1.09.36-macos-universal.tar.gz`

*Field triage, release engineering and validation: Pritpal Bedi.*
