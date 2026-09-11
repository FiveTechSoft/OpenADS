# OpenADS v1.09.33

## Boundary completeness + trace attribution

Field trace showed the remaining gap: post-limit boundary probes
(`AtBOF` after reaching EOF and mirror) still paid a round-trip each
while xBrowse polls them per paint row. Plus the trace itself couldn't
attribute time or tables. Both fixed.

### Changes

- **Proven-false boundary stickies** (`nav_not_bof`/`nav_not_eof`):
  skip/top/bottom establish what the cursor is *not* at — a forward
  skip to EOF proves EOF *and* not-BOF (arrived from rows), a top on
  a row proves not-BOF, and so on. Derived only from row facts, never
  from flags that could predate scope/filter edits; cleared on
  narrowing/removing ops.
- **Per-side boundary answer cache**: repeated identical BOF/EOF polls
  are served locally while no cursor-affecting frame lands. A lone
  wire answer certifies only its own side; the twin half is certified
  only by the piggybacked twin byte.
- **Trace attribution**: every `cli_trace` line carries milliseconds
  since trace start; nav/skip/BOF/EOF lines carry the table alias.
  Same-millisecond gaps read as local answers, RTT-sized gaps as
  wire frames, per table.
- **Tests.** Skip-limits round trip, filter-forced twin pair;
  full suite green except the pre-existing MinGW-only CDX case.

### Compatibility

- Client-side answer caching only; no wire changes since v1.09.29.
  Mixed old/new peers behave as before.

### Test Results

- MinGW-x64 (GCC 16.2): batching suites green; full unit suite
  1547/1548.
- Full matrix (Windows MSVC x64/x86, Linux, macOS) runs in CI on the
  tag push.

### Packages

- `openads-1.09.33-windows-x64.zip`
- `openads-1.09.33-windows-x86.zip`
- `openads-1.09.33-linux-x64.tar.gz`
- `openads-1.09.33-macos-universal.tar.gz`

*Field triage, release engineering and validation: Pritpal Bedi.*
