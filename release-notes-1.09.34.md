# OpenADS v1.09.34

## Fused SetOrder+GotoTop/Bottom (one frame per rotation)

Field traces showed the per-tag rotation heartbeat: `SetOrder` then
`GotoTop`/`GotoBottom`, two round-trips every time, hundreds of times
per startup. This release fuses them into one frame.

### Changes

- **Deferred order switch.** `AdsSetIndexOrder*` updates the client
  belief immediately and sends nothing; repeats overwrite the pending
  switch. Unmapped tags still go out at once (errors surface now).
- **Absorbing nav.** The following `GotoTop`/`GotoBottom` carries the
  pending order in a trailing section and the server installs it
  before navigating — one frame, row trailer included. Any other
  binding-dependent op flushes the switch plainly first; close and
  disconnect absorb it silently; pooling excludes pending tables.
- **Caps-gated** (`kCapNavOrderFuse`): length-gated trailer, old
  servers ignore it, old clients never emit it. Mixed peers behave
  as before.
- **Server refactor**: order-install factored out of the `SetOrder`
  handler (stale-handle self-heal preserved) and reused by the fused
  path.
- **Tests.** Fused top/bottom (1 Goto + 0 SetOrder, correct rows),
  close-absorb, plain flush on non-nav, skip-after-fuse.

### Test Results

- MinGW-x64 (GCC 16.2): batching suites green; full unit suite
  1550/1551 (sole failure pre-existing, MinGW-only CDX alloc-tail).
- Full matrix (Windows MSVC x64/x86, Linux, macOS) runs in CI on the
  tag push.

### Packages

- `openads-1.09.34-windows-x64.zip`
- `openads-1.09.34-windows-x86.zip`
- `openads-1.09.34-linux-x64.tar.gz`
- `openads-1.09.34-macos-universal.tar.gz`

*Field triage, release engineering and validation: Pritpal Bedi.*
