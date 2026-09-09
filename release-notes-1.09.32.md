# OpenADS v1.09.32

## Merged flush-into-closeall + regenerated MinGW import libs

Two field-driven items: the teardown merge (one frame instead of
two when flush and close-all are both owed), and import libraries
that actually contain the new exports.

### Changes

- **FlushFileBuffers merges into CloseAllIndexes.** The server now
  flushes table data inside the CloseAllIndexes handler before
  dropping bindings, so a deferred flush + close-all pair costs one
  round-trip instead of two. Caps-gated (`kCapFlushInCloseAll`) —
  old servers keep both frames, mixed peers behave as before.
- **Regenerated MinGW import libs** (`dist/import-libs/*/mingw`):
  every stdcall decorated name re-derived from source and verified
  byte-exact against the MSVC-built predecessors; new libs are
  strict supersets including `AdsGetServerVersion` (plus newer
  `Bm*`/`DDAddView`/`SetAdiV2` wrappers the old libs predated).
  `oads_hb.c` relinks clean — `OADS_SERVERVERSION()` needs no more
  commenting out.
- **Contributor credit footers** in the WAN-series notes.

### Test Results

- MinGW-x64 (GCC 16.2): batching suites green; full unit suite
  green except the pre-existing MinGW-only CDX alloc-tail case.
- Full matrix (Windows MSVC x64/x86, Linux, macOS) runs in CI on the
  tag push.

### Packages

- `openads-1.09.32-windows-x64.zip`
- `openads-1.09.32-windows-x86.zip`
- `openads-1.09.32-linux-x64.tar.gz`
- `openads-1.09.32-macos-universal.tar.gz`

*Field triage, release engineering and validation: Pritpal Bedi.*
