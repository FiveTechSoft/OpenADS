# OpenADS v1.09.40

## The park engages: order resolution + context-aware adoption

v1.09.39's trace lines (not the clock) diagnosed the .38 park:
84 parks, 82 repeat-clears, **zero unpark hits** — the rotation
never reopens. It goes Clear → Clear → order → nav with no
`AdsOpenIndex` in between, and two holes broke it: every round's
closing `dbGoBottom()` tripped the order-divergence guard (real
close, park dead), and tag/ordinal order resolution failed
against the empty live maps (wire fallbacks). Both closed.

### Changes (vs v1.09.37)

- **Index-binding park, engaging.** Same-bag reopen: zero
  frames. Binding-only ops adopt silently. Real closes only
  for different bags and structural changes. No protocol change.
- **Context-aware divergence guard.** Navs carrying order
  context (pending switch, live active belief) adopt — the
  fused path installs the order explicitly. Only truly
  natural navs force a real close.
- **Parked order resolution.** `SetIndexOrder`,
  `GetIndexHandle`, `GetIndexHandleByOrder` fall back to the
  parked snapshot (server-live ids): switches defer, navs
  fuse. Full rotation without reopen: **one frame total**.
- Park tracing (`parked` / `repeat clear` / `unpark hit/miss`),
  flush-merge precedence, CreateIndex/erase invalidation,
  5 park tests.

### Test Results

- MinGW-x64 (GCC 16.2): batching suites green; full unit suite
  1559/1560 (sole failure pre-existing, MinGW-only CDX alloc-tail).
- Full matrix (Windows MSVC x64/x86, Linux, macOS) runs in CI on the
  tag push.

### Packages

- `openads-1.09.40-windows-x64.zip`
- `openads-1.09.40-windows-x86.zip`
- `openads-1.09.40-linux-x64.tar.gz`
- `openads-1.09.40-macos-universal.tar.gz`

*Field triage, release engineering and validation: Pritpal Bedi.*
