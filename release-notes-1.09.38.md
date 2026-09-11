# OpenADS v1.09.38

## Index-binding park (zero-frame tag rotation)

Field census on v1.09.37 (**94 s**, 1919 wire frames) left the
per-tag rotation as the biggest structural chunk: `CloseAll` (166)
+ `OpenIndex` (125) + `SetOrder` (121) per tag touch, plus the
rotation's fused navs. The close and reopen cancel each other out —
the server drops bindings the next frame re-creates — so this
release stops sending them.

### Changes

- **Parked index bindings.** `CloseAll` snapshots the live
  tag→id maps with the server bindings still open (no frame).
  A same-bag `OpenIndex` restores them with zero frames; any op
  that only needs live bindings (order switches, seeks, counts,
  reads) adopts the park silently. A real `CloseAll` goes out
  only for different-bag opens and structural changes.
  Session-scoped validity: our session's bindings die only via
  our own frames, all funnelled through the adopt-or-emit
  decision, so a parked snapshot can never reference dead ids.
  No protocol change — old servers behave identically.
- **Order-divergence guard.** A parked server order vs the
  cleared client belief: table-handle navs landing inside a
  `CloseAll`→`OpenIndex` window force a real close first (the
  rotation never navs mid-window, so flow pays nothing); pool
  adoption force-closes too.
- **Merge preserved.** With a flush owed, the merged
  flush-into-`CloseAll` wins over the park; every real close
  clears live maps (no dead ids afterwards).
- **Invalidation.** `CreateIndex` drops the snapshot; file
  erase/rename drops parks connection-wide. Reindex/Pack/Zap
  keep wire ids, so the park survives them.
- **Key-count bonus.** Stable server index ids across rotations
  keep the order key-count cache alive (it was re-minted, hence
  re-fetched, on every reopen).
- **Tests.** 3 park cases: zero-frame rotation with live ids
  (order/count/seek all work, second round identical),
  nav-inside-window forces a real close with natural-order
  values, create-invalidation reopens for real.

### Test Results

- MinGW-x64 (GCC 16.2): batching suites green; full unit suite
  1557/1558 (sole failure pre-existing, MinGW-only CDX alloc-tail).
- Full matrix (Windows MSVC x64/x86, Linux, macOS) runs in CI on the
  tag push.

### Packages

- `openads-1.09.38-windows-x64.zip`
- `openads-1.09.38-windows-x86.zip`
- `openads-1.09.38-linux-x64.tar.gz`
- `openads-1.09.38-macos-universal.tar.gz`

*Field triage, release engineering and validation: Pritpal Bedi.*
