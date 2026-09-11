# WAN Optimization Session — OpenADS + Vouch (Sep 2026)

People: Pritpal Bedi (field, Vouch, releases) + Muse Spark (engineering).
Scope: make Harbour/rddads Vouch (`VouchADS.exe`, 32-bit) fast over WAN
against `openads_serverd` on minimal Lightsail (`204.236.245.109:6262`).

## 0. Starting point

- Symptom: folders/tables/indexes create fine, APPEND/REPLACE/COMMIT work,
  but everything is extremely slow — record adds and even GETs crawl.
- Baseline startup (v1.09.27, both sides): **TimeToReadiness 3:16.963 ≈ 197 s**
  for ~90–170 table USEs (~2 s per USE at ~50 ms RTT).

## 1. v1.09.27 — pool order-active tables

- HEAD commit pooled only natural-order closes; Vouch keeps `IndexOrd()=1`
  across USEs, so the pool stayed empty. Change: order/index state survives
  park, restores on reuse.
- Blocker found pre-release: ordered close kept `ZL.CDX` open server-side
  and broke `remote: closing a table releases its files on the server` on
  Windows. Decision (Pritpal): keep HEAD, rewrite the test to the supported
  path — ordered close → park → `AdsDeleteFile` evicts → files really gone
  (plus `EnableFileFunc` gate discovery). Test green.
- Full suite 1534/1535 (only pre-existing MinGW-only CDX alloc-tail case).
- Released v1.09.27 (notes file convention, tag → CI builds 4 zips).
- Token question: no token needed — stored credentials covered push/tag.

## 2. Version reporting (both sides provable)

- Ask: `AdsVersion()` returns SAP-shaped `1.9a` (patch dropped — every
  1.09.x identical); no Harbour-callable server version.
- Built: Hello→HelloAck probe at connect (stored version, +1 RTT per
  *connect* only), new `AdsGetServerVersion(hConn, buf, len)` (`ace.h` +
  `.def` + x86 stdcall export), `OADS_ADSVERSION()` refined to full dotted
  build, new `OADS_SERVERVERSION(hConn)`.
- Correction: wrappers belong in `contrib/oads_hb/oads_hb.c` (compiled into
  the app), not the FWH bridge `.prg` — moved there, verified compiling
  under the real Harbour toolchain, documented in its README.
- 32-bit deliverables: MinGW **cannot** build a working `ace32.dll`
  (stdcall aliases are MSVC-only) — DLL comes from CI x86 zip; fresh
  MinGW `libace32.a` import lib generated locally and link-verified
  (`ld -r oads_hb.o libace32.a` clean).
- Released v1.09.28 with notes.

## 3. Finding the 197 s (field logs)

- `C:/tmp/cli_trace.log` (199 lines): **133 wire RTTs in ~4 opens (~33/USE)**.
  Per USE: `GotoTop×2`, `AtBOF/AtEOF` pairs, `GotoBottom` — before counting
  open/order costs. 72% was BOF/EOF with no valid row (always wire).
- Vouch table log: ~90 USEs × ~2 s; repeats (V_USRCFG ×14, F6SRVRAT ×8)
  never pool-hit in the field; phases `1`/`2` look like separate connections.
- `mus_d_file` (Vouch's open routine, shared in full): per USE =
  open + `VouExistIndex` + `OrdListAdd` + `OrdNumber` + `OrdListClear` ×2 +
  `OrdListAdd` + `SET ORDER TO 1` + `dbGoBottom()`. Production bag is `.z01`.
  No rebuilds occur in these runs (`lCreate` always false).

## 4. Nav-probe batching (v1.09.29)

- **A. Duplicate GotoTop/GotoBottom suppression** — stamp `(op, order,
  seq)` per table; skips provably redundant frames (keyno sync + relations
  still re-run locally). Order-aware: top in order A never suppresses B
  (first attempt broke 9 tests — `GotoTop(hOrd)` deduped to rec 1 instead
  of rec 3; pinned by new order-context test).
- **B. Empty-cursor sticky** — top/bottom with no row proves BOF+EOF;
  answers locally until a cursor-affecting frame lands. Local filter
  mutations reset explicitly; prefetch drains reset (local move, no wire).
- **C. BOF/EOF twin flag** — acks carry `[u8 bof][u8 eof]` /
  `[u8 eof][u8 bof]` (length-gated, no new opcode, old peers unaffected);
  also fixed stale twin after Limbo-rescue. Spec updated (`wire-protocol.md`).
- Follow-up fix same week: stamps died on first `FieldGet` (sequence
  bumped per frame) — redesigned to `nav_seq_`, bumped only by 32
  cursor/visibility-affecting methods; reads never invalidate.
- Projected 133 → ~28 wire RTTs per fragment (~80% off probes).
- Released v1.09.29. Field: **145 s**. Expected more (~45–50 s) — didn't land.

## 5. Teardown batching (v1.09.31)

- `ace_calls.log` (9925 calls): per-tag rotation
  `SetOrder → Flush → CloseAll → OpenIndex` (~5 frames ≈ 0.25 s/tag).
- Shipped: Flush+CloseAll defer into Close (server close flushes + purges);
  CheckExistence positive cache (negatives always wire); SetOrder
  skip-if-same (SetOrder never moves cursor); order-handle keycount cache
  (+ WriteRecord/SetRecord/probe invalidation for FOR membership).
- Caught by tests along the way: pooled tables with cleared index maps
  (excluded from pooling), key-count staleness on conditional writes.
- Released v1.09.31. Field: **145 s again** — absorption never fires
  because the real pattern is CloseAll→OpenIndex (no close follows).

## 6. v1.09.32 — flush⊂CloseAll merge + import libs

- Server flushes inside CloseAllIndexes handler; client merges the pair
  into one frame, caps-gated (`kCapFlushInCloseAll`).
- Regenerated MinGW import libs from source (stdcall decorations
  re-derived and verified byte-exact vs MSVC-built predecessors):
  strict supersets including `AdsGetServerVersion`. `oads_hb.c` relinks
  clean — no more commenting out.
- Released v1.09.32. Field: **143.2 s, 1569 ms/USE** (vs 2155 ms on .27
  = −27% per USE). Loopback baseline: **14.7 s** all-in.

## 7. Decisive measurements (no more guessing)

- TCP connect RTT: ~50 ms (client→Lightsail). Ping blocked by firewall.
- Synthetic open cost flat vs tags (2.6 ms local at 5/50/157 tags).
- Realistic USE cycle = **10 wire frames** (locked as `network_use_budget_test`).
- Server OPDUMP (`OPENADS_OPDUMP=1`): ~2870 frames/startup at ~60 µs/op
  server-side. **Time ≈ frames × RTT.** Server CPU 0.53%, burst 100% —
  server exonerated twice.
- Per-RDD split: ADSCDX ~1.6–2.1 s/USE; RMDBFCDX (local Harbour RDD on
  47/157-tag tables) ~1.4–1.9 s/USE with zero wire — Harbour-native cost.
- Slowest units are workloads, not opens: USERS 7.2 s, CONFIG_U 5 s,
  lock pairs ~3.5 s.

## 8. Attribution

- Pritpal is a contributor (not owner) on FiveTechSoft/OpenADS: no PAT
  games — commits/tags already carry his authorship; release-notes
  convention now closes with *"Field triage, release engineering and
  validation: Pritpal Bedi."* (applied .24–.32).

## 9. Context (not pursued)

- CacheRDD (Pritpal's Harbour RDD for InterSystems Caché, 2007, open
  source): WAN-fast by set-oriented design — the direction all batching
  here converges toward. No further work.
- Filters: no open wound; Vouch runs smoothly (LAN decision pending).
- LAN test pending: if reasonable, WAN effort pauses; work preserved.

## 10. State + what's left

- `main` green through v1.09.33 + fused SetOrder+Goto (commit `8918294d`,
  unreleased): deferred order switch absorbed into nav (1 frame instead
  of 2 per rotation; caps-gated `kCapNavOrderFuse`; suite 1550/1551).
  Estimated ~20–25 s off startup. Trace upgraded to fixed-width grid
  (`ms | alias | op | detail`).
- Remaining ≈ 2700 frames/startup: per-tag rotation (SetOrder+OpenIndex
  per tag — structurally necessary today), 266 GotoRecord restores,
  174 seeks, workload seeks/reads/locks. Next lever: call fusion
  (open+order+position+count in fewer frames) — a protocol addition,
  scoped but unbuilt.
- Open threads: MSVC/Borland import libs (toolchains absent here);
  `dist` x86/x64 MinGW libs are fixed.
