# DOING.md — Trabajo en curso

> Archivo vivo. Se actualiza diariamente con lo que se está haciendo,
> probando y verificando. No es un changelog — es un "qué está pasando
> ahora".

---

## 2026-09-09 — USE-teardown batching (segundo cuello: 406 SetOrders, 183 flushes, 169 exists)

### Evidencia (`ace_calls.log`, 9925 llamadas)

El startup no son aperturas: por USE hay workloads — `AdsGetRecordCount`
994, `IsFound/AtEOF/AtBOF` 801 (poll por fila), `SetOrder` 406,
`GotoRecord` 266, `OpenIndex` 204, `FlushFileBuffers` 183, `Seek` 174,
`CheckExistence` 169, `CloseAllIndexes` 166. Patrón por tag:
`SetOrder → Flush → CloseAll → OpenIndex` (~5 frames ≈ 0.25 s/tag;
una tabla de 157 tags ≈ 40 s). Medición: ciclo USE realista = 10
frames (test `network_use_budget_test`); apertura local plana vs tags
(2.6 ms) — el parse CDX no es el cuello. Servidor exonerado (0.53%
CPU, burst 100%).

### Implementado (sin release)

- **Flush+CloseAll difieren al Close** (server close flushea vía
  shadow + purga bindings; si otra op interviene, se emiten antes en
  orden). Tablas con flags pendientes no se parkean; `OpenIndex`/
  `SetOrder`/`KeyCount` enganchan el hook. Pool-adopt resetea.
- **CheckExistence cachea positivos** por conexión; solo
  drop/create/erase/rename (vía `remote_flush_pools`) y
  erase/rename directos invalidan. Negativos siempre a wire.
- **SetOrder salta si el binding ack-confirmado ya es el target**
  (SetOrder no mueve cursor). ByHandle ambas ramas + ByName mapeado.
- **KeyCount por handle ridea el cache de la orden activa**; fix:
  `WriteRecord`/`SetRecord`/probe invalidan (FOR membership cambia
  con writes — lo cazó `abi_index_intensive2_test`).
- **Tests:** `network_teardown_batch_test` (5 casos con contadores).
  Suite 1546/1547 (solo CDX pre-existente).

## 2026-09-08 — Nav-probe batching (Vouch 197s root cause)

### Evidencia de Pritpal Bedi

`C:/tmp/cli_trace.log` (199 líneas): **133 wire RTTs en ~4 aperturas
(~33 por USE)** — `GotoTop×2 + AtBOF/AtEOF×N + GotoBottom + ...` por
tabla, antes de contar open/order/keycount. Desglose: BOF/EOF sin fila
válida 96 RTTs (72%), tops/bottoms/skips 37. A ~100 ms RTT → ~2 s/USE
× ~90 = 197 s. Cuadra al segundo.

### Implementado (sin release)

- **A. Dedup de GotoTop/GotoBottom consecutivos.** Sello
  `(op, orden, wire_seq)` por tabla; `wire_seq` atómico del
  `RemoteConnection` sube con cada frame. Duplicado con seq intacta =
  frame omitido (sync keyno + relations se re-ejecutan en local).
  El sello lleva contexto de orden (id del `RemoteIndex`, o
  `server_order_id` ack-confirmado): un top en orden A no dice nada
  del orden B (esto rompió 9 tests en el primer intento —
  `GotoTop(hOrd)` tras resolver handle dedupaba contra el sello
  natural y devolvía rec 1 en vez de rec 3).
- **B. Sticky de cursor vacío.** Top/bottom sin fila = cursor vacío =
  BOF y EOF a la vez (xBase) hasta que algo toque el wire. Resets
  manuales solo para mutaciones puramente locales (`SetFilter`/
  `ClearFilter`); todo lo demás auto-expira vía seq. Los drains de
  prefetch (movimiento local sin wire) resetean en
  `remote_drain_prefetch`.
- **C. Twin flag en acks.** `AtEOFAck [u8 eof][u8 bof]`,
  `AtBOFAck [u8 bof][u8 eof]` (6 sitios en `session.cpp`, incl. fix de
  `v2` stale tras Limbo-rescue). Cliente cachea el gemelo;
  length-gated: mezcla viejo/nuevo sin fallback. Spec en
  `docs/wire-protocol.md` §5.10.
- **Tests:** `tests/unit/network_nav_batch_test.cpp` — 4 casos con
  contadores de opcodes (dup 0 frames, vacía 0 frames, gemelo 1 frame
  por par, contexto de orden). Suite 1539/1540 (solo el CDX
  pre-existente MinGW-only).

### Efecto estimado (fragmento de 133 RTTs → ~28, −80% en probes)

### Fix post-campo (mismo día)

El trace de producción (1896 líneas, 80 KB) mostró solo **2 dedupes**:
el `wire_seq` subía con CADA frame incluidos reads — rddads intercala
`FieldGet` entre probes y el sello moría al instante (638 BOF + 281
EOF a wire). Rediseño: `nav_seq_` solo sube en frames que mueven el
cursor o cambian visibilidad (nav/seek/order/scope/AOF/show-deleted/
writes/pack/zap/reindex, 32 métodos); los reads ya no invalidan. Nuevo
test "stamps survive read traffic". Suite 1540/1541 (solo CDX
pre-existente + 1 flake transitorio de carga).

### Medición decisiva (v1.09.30 en campo: 140 s vs 145 s)

Un ciclo USE realista (open+setorder+gotop×2+bof/eof×2+bottom×2+
keycount+close) cuesta **10 frames totales** (Hello+Connect incluidos;
0 frames de probes, 1 top, 1 bottom) — test
`network_use_budget_test.cpp` lo fija como regresión (≤14). Coste de
apertura local plano vs tags (2.6 ms con 5/50/157 tags): el parse CDX
NO es el cuello. A ~50 ms RTT el wire son ~0.5 s/USE; el resto (~1 s)
es proceso servidor (instancia mínima) + RDD cliente. Siguiente:
latencia de open en servidor y pool-hit rate en campo.

### Diagnóstico pedidido por Pritpal: trace con tiempo + alias

`cli_trace` ahora estampa ms desde el inicio en cada línea y el alias
de la tabla en los paths calientes (goto/skip/bof/eof), para atribuir
bloques de probes a su tabla y separar locales (~0 ms) de wire (~RTT).
Su chunk mostró el hueco restante: probes AtBOF post-EOF a wire.

---

## 2026-09-12 - Counts ride every nav (v1.09.44 field: 70 s)

### Field result v1.09.44 (both sides 1.09.44)

`v.1.09.44 1:10.122 == 70 secs` (-5 s: RecCount 85-28 via the
piggyback; RecNo/KeyCount unmoved - the app lives in orders,
both derivations assumed natural).

### Shipped (v1.09.45)

- **Ordered-phantom RecNo derivation.** The twin reports
  physical n+1 past the last key (same engine rule), so the
  EOF derivation drops its natural-order restriction (filter /
  AOF / scope exclusions stay).
- **RefreshRecord carries its row.** Trailer + bound tail on
  the ack; table-aware client overload repopulates row, bounds,
  recno, count. Same-record goto (RecNo+Goto+Refresh) and the
  skip-settle triple (Skip(0)+Refresh+flags) lose their
  follow-up frames. No seq bump (refresh never moves).
- **Key-count piggyback on fused navs only.** The server
  certifies the just-installed order's count past the bound
  tail (14-byte tails, length-gated); client stores into the
  per-order map. Plain navs pay no extra server work.
- **Tests.** Ordered-phantom values, fused-count zero-frame,
  refresh row+truth. Suite 1567/1568.
- Estimate: ~200 frames = **9 s** -> ~61 s next field run.

## 2026-09-11 — Counts without frames (v1.09.42 field: 75 s)

### Field result v1.09.42

`v.1.09.42 1.15.442 == 75 secs` (−3 s: RecNo 219→147, FileExists
76→39). Remaining: repositions/navs (709, necessary single
frames), counts (172), RecNo residue (147), RefreshRecord (64).

### Shipped (v1.09.43)

- **Record-count piggyback.** Nav acks append `[u32 reccount]`
  (in-memory, no disk refresh — same trust as the count cache;
  the wire count keeps its refresh). Client serves while
  seq-fresh and promotes into the sticky cache.
- **Phantom RecNo derivation.** EOF phantom in natural order
  without filters: `cached_count + 1`, no frame, no currency
  (immune to cross-table eviction by construction). Scoped and
  ordered phantoms keep the wire path.
- **Per-order key-count map.** Order switches don't change any
  order's count: rotation revisits serve from the map. True
  invalidations (writes, scope/filter — the scope gap the
  suite caught on the way, adopt, refresh, pack/zap) clear it.
- **Tests:** count certification, cross-table phantom
  derivation, per-order revisit. Suite 1566/1567.
- Estimate: ~250 frames ≈ **12 s** → ~63 s next field run.

## 2026-09-11 — Bound truth on every nav (v1.09.41 field: 78 s)

### Field result v1.09.41

`v.1.09.41 1:17.618 == 78 secs` (−16 s: CloseAll 166→0, OpenIndex
125→41, SetOrder 121→42; 84 parks + 84 unparks). Remaining 1590
frames: repositions (GotoRecord 266 + Seek 174, necessary) and
their poll residue (RecNo 219, rotation navs 253, counts 172).

### Shipped (v1.09.42)

- **Bound+recno piggyback on GotoTop/Bottom/Skip acks**
  (server appends after the row trailer; client parses
  length-gated — same convention, no caps). Every nav now
  certifies bounds+recno, not just repositions; covers table-
  and index-handle navs (both ride the rt overloads) and the
  fused order-switch variants.
- **FileExists short-circuit for open bags.** Per-USE
  existence probes on a bound bag answer locally (stem match;
  false positives safe, false negatives impossible).
- **Tests:** top/bottom/skip certification, open-bag
  existence (+ negatives still wire). One outdated bound
  expectation updated (skip-overshoot EOF now certified too).
- Estimate: ~250 frames ≈ **12 s** → ~66 s next field run.

## 2026-09-11 — RDD rules from source (CacheRDD + rddads)

User provided CacheRDD client source (`C:/tmp/cacherdd`, outside
the repo) plus rddads at `C:/harbour-git/contrib/rddads`. Decisive
reads, replacing three releases of trace-guessing:

- **Boundary state is RDD-owned.** CacheRDD derives BOF/EOF/TOP/
  BOTTOM/FOUND purely from cached `WA_RECNO` after each op — zero
  server calls. rddads mirrors it: `adsBof/adsEof` return
  `fBof/fEof` locally; every nav ends in `hb_adsUpdateAreaFlags`
  (`AdsAtBOF + AdsAtEOF + AdsIsFound` on the table handle) — that
  refresh, not Harbour paint code, is our 801+801 poll source.
- **Order ops are set-oriented.** CacheRDD: Clear = local list
  reset (no wire!); Add = one fetch-all-tags call, no-op when
  present; Focus = skipped when same, else one server call.
- **Rotation shape (rddads).** `adsOrderListAdd` = `AdsOpenIndex`
  + (if no current order) set + **`SELF_GOTOP` inside the Add**;
  `adsOrderListClear` = unconditional `AdsFlushFileBuffers` +
  `AdsCloseAllIndexes` + `hOrdCurrent = 0`. `adsGoTo` calls
  `AdsGetRecordNum` BEFORE `AdsGotoRecord` (2001 workaround).
- Consequence for the park: uneeded `Flush` (clean tables) must
  set no flag, or every rotation poisons it.

## 2026-09-11 — Park that never fires (v1.09.39 field: 94 s)

### Field result v1.09.39

`v.1.09.39 1:34.510 == 94 secs`, census again ~1919. But the new
trace lines told the real story: **84× `parked 1 tags` + 82×
`nothing open`, and ZERO `unpark hit`** — the rotation never
reopens. It goes Clear → Clear → order-by-handle/tag → nav, with
no `AdsOpenIndex` in between. Two park holes, both fixed:

1. **Force-close fired every rotation.** Each round ends with
   `dbGoBottom()` — a table nav that hit the order-divergence
   guard and sent a real close, killing the park it just made.
   Refined: navs carrying order context (pending switch or live
   active belief — the fused path installs it explicitly) adopt
   safely; only truly natural navs force the close.
2. **Order resolution failed while parked.** `SetIndexOrder` /
   `GetIndexHandle` / `GetIndexHandleByOrder` consulted only the
   (empty) live maps, so tag switches fell back to wire frames
   and ordinal resolution errored. All three now fall back to
   the parked snapshot (server-live ids).

### Shipped (v1.09.40)

- Full rotation without reopen proven by counter: Clear, Clear,
  GetIndexHandle + ByOrder, SetOrder(tag), GotoTop, KeyCount =
  **one GotoTop frame total**, zero open/close/order frames.
- 5 park tests green; suite 1559/1560 (pre-existing CDX only).

## 2026-09-11 — Index park double-clear fix (v1.09.38 field: 94 s)

### Field result v1.09.38 (both sides 1.09.38)

`v.1.09.38 1:34.004 == 94 secs` — identical to .37 AND a
byte-identical opcode census (1919 frames). Deterministic app +
unchanged behavior ⇒ the park never fired a single time.

Root cause (code reasoning, confirmed by `mus_d_file` in the
session notes): the RDD issues **`OrdListClear` twice per
rotation**. The first Clear parked a good snapshot; the second
moved empty maps over it. Every reopen then missed (empty
snapshot → real close + wire open) — exactly today's frames.

### Shipped (v1.09.39)

- **CloseAll snapshots only live maps.** A repeat clear with
  nothing live keeps the existing snapshot (no-op); genuinely
  empty closes still pend a real close.
- **Park decision traced:** `AdsCloseAllIndexes` logs
  parked/repeat/nothing; `AdsOpenIndex` logs unpark hit/miss —
  the next field run proves the hit rate in the log itself.
- **Tests:** double-clear rotation (the field shape — would have
  failed on .38: reopen paid both frames).

## 2026-09-11 — Index-binding park (v1.09.37 field: 94 s)

### Field result v1.09.37 (both sides 1.09.37)

`v.1.09.37 1:34.355 == 94 secs` (was 124 s ≈ −30 s for the
reposition piggyback). Server census (1919 frames / 94 s):

| Frames | Op | Note |
|--------|----|------|
| 266 | GotoRecord (bookmark restores — necessary) | — |
| 219 | GetRecordNum (was 548) | −60% |
| 174 | Seek (necessary) | — |
| 166+125+121 | CloseAll + OpenIndex + SetOrder (per-tag rotation) | **next** |
| 166+87 | GotoTop + GotoBottom (rotation navs, fused) | — |
| 87+85 | KeyCount + RecordCount | — |
| 1 | AtBOF (was 352!) + 2 AtEOF | piggyback verified |

Twin verdict: AtBOF 352→1, AtEOF 3→2. Time still ≈ frames × RTT.

### Shipped (v1.09.38)

- **Index-binding park.** `CloseAll` snapshots the live tag→id
  maps (server bindings stay open, no frame) instead of dropping
  them. Same-bag `OpenIndex` restores with zero frames; any op
  that only needs live bindings (SetOrder, seeks, counts, reads)
  adopts the park; a real `CloseAll` goes out only for
  different-bag opens and structural changes. Session-scoped
  validity: our session's bindings die only via our own frames.
- **Order-divergence guard.** Parked server order vs cleared
  client belief: table-handle navs (top/bottom/skip/goto) inside
  a CloseAll→OpenIndex window force a real close first (the
  rotation never navs mid-window — Clear→Add adjacent — so it
  costs nothing in flow). Pool adoption force-closes too.
- **Flush⊂CloseAll merge preserved:** with a flush owed, the
  merged real close wins over the park (caught by the pre-existing
  teardown test); real closes clear live maps (no dead ids).
- **Invalidation:** CreateIndex drops the snapshot; erase/rename
  drops parks connection-wide; Reindex/Pack/Zap keep ids (safe).
- **KeyCount bonus:** stable server index ids across rotations
  keep the order key-count cache alive (was re-minted per open).
- **Tests:** 3 park cases (zero-frame rotation + live ids,
  nav-in-window real close, create-invalidation).
- Estimate: ~290 frames ≈ **14 s** → ~80 s next field run.

## 2026-09-11 — Reposition-bound piggyback (v1.09.35 field: 124 s)

### Field result v1.09.35 (both sides 1.09.35)

`v.1.09.35 2:04.375 == 124 secs` (was 143.2 s on .32 ≈ −19 s, as
estimated for fused order + metadata caches). Full census from
`ads_err.log` OPDUMP (2600+ frames / 124 s ≈ 21 frames/s ≈ 48 ms —
Time ≈ frames × RTT, server exonerated again with ~50–120 µs/op):

| Frames | Op | Share |
|--------|----|-------|
| 548 | GetRecordNum (RecNo per paint row, always `!row_valid`) | 21% |
| 352 | AtBOF (first of pair after every reposition) | 13.5% |
| 266 | GotoRecord (bookmark restores) | 10% |
| 174 | Seek | 7% |
| 166+125+121 | CloseAll + OpenIndex + SetOrder (per-tag rotation) | 16% |
| 3 | AtEOF of 801 polls — twin verified perfect | — |

Pattern per xBrowse row: reposition (bumps seq, clears flags) →
RecNo wire → BOF wire → EOF twin-local = 3 frames/row.

### Shipped (v1.09.36)

- **Bound piggyback on GotoRecordAck/SeekAck.** Server appends
  `[u8 bof][u8 eof][u32 recno]` after the row trailer (length-gated,
  no caps bit — M12.24 convention; old peers ignore the tail). Client
  certifies flags + bound cache + phantom recno from it: one frame
  per reposition instead of 3–4. No Limbo rescue on the pack path
  (pure read); both-true genuinely means empty (Clipper phantom —
  past-end goto and hard-miss seek both land in Limbo, value-identical
  to the old wire polls).
- **Phantom RecNo cache.** `AdsGetRecordNum` serves `recno_bound`
  while no cursor frame lands (same currency as the bound cache,
  cleared with it); stored on wire answers and piggybacks.
- **Trace:** `AdsGotoRecord`/`AdsSeek`/`AdsSeekLast` lines with
  alias (the previously invisible invalidators).
- **Tests:** 3 opcode-counter cases (mid/past-end goto, hit/first/
  miss seek — incl. the `has_row=0` 2-byte lookahead misalignment
  this caught: trailer end offset now consumed exactly).
- Estimate: ~850–950 frames ≈ **40–45 s** → ~80 s next field run.

### Deferred

- Per-table cursor generation (cross-table seq eviction) — measured
  unnecessary: the dominant pattern is same-table reposition→poll,
  which the piggyback kills without any seq surgery.
- Rotation CloseAll+OpenIndex (~290 frames) — structural, next.

## 2026-09-11 — Fused SetOrder+Goto (rotación por tag en 1 frame)

### Pedido de Pritpal Bedi

Vouch rota órdenes por tabla (`SetOrder` + `GotoTop` por tag). Cada
rotación costaba 2 frames; el trace muestra el par docenas de veces
por startup (~200 ms por par a ~60 ms RTT con el resto de frames
intercalados).

### Implementado (sin release)

- **Servidor:** `install_table_order(tid, iid)` factorizado del handler
  `SetOrder` (incl. self-heal); `GotoTop`/`GotoBottom` aceptan sección
  trailing `[0x01][order]` (length-gated; viejos la ignoran) e instalan
  antes de navegar. Bit `kCapNavOrderFuse` (echo + advertise).
- **Cliente:** `SetOrder*` difiere (belief inmediata, sin frame;
  overwrite entre switches; probe de error preservada para tags no
  mapeados). `GotoTop/Bottom` (tabla e índice) absorben el pendiente
  en frame fusionado (caps) o flushean plano (legacy). `flush_pending`
  integra el flush; pool excluye pendientes; close/disconnect absorben.
- **Tests:** fusión top/bottom (1 Goto + 0 SetOrder), absorb en close,
  flush plano en non-nav, skip-if-same tras fusión. Suite 1550/1551
  (solo CDX pre-existente).
- Estimación: ~400 pares/rotación ≈ **20–25 s** del startup.

### Completitud de límites: not_bof/not_eof + bound cache

- Skip/top/bottom establecen proven-false (`nav_not_*`): skip
  adelante→EOF prueba EOF + not-BOF; top+row prueba not-BOF, etc.
  Derivan solo de `row_valid_before`/filas — nunca de flags `at_*`
  (podrían preceder a scope/filter). Limpieza en narrow/remove
  (SetScope/SetFilter/SetAOF/Delete/Pack/Zap) + adopt/open.
- Bound cache por lado con seq: respuestas repetidas idénticas sirven
  en local mientras ningún frame cursor-moviente aterrice; el gemelo
  solo lo certifica el byte twin. Misma limpieza que `last_nav`.
- Tests: skip-limits (ida y vuelta) + twin forzado vía SetFilter.
  Suite 1547/1548 (solo CDX pre-existente).

## 2026-09-08 — Server/DLL version reporting (Vouch triage)

### Pedido de Pritpal Bedi

Deployed v1.09.27 on both sides. Needs two things: (1) `AdsVersion()`
returns `1.09.a`-style (SAP major.minor+letter) which drops the patch —
1.09.26 vs 1.09.27 indistinguishable; (2) no Harbour-callable function
for the **server's** version ("the fix didn't help" keeps being an old
serverd still running).

### Implementado (sin commit de release)

- **Hello probe at connect.** `RemoteConnection::connect_with_transport`
  sends `Hello` before `Connect` and stores the `HelloAck` payload
  (`"openads/1.09.27"`, literal `"openads/0.3.2"` on pre-1.8.14 servers).
  Best-effort: failed probe → empty (unknown), connect still decides
  success. +1 RTT per *connect* only (connects are rare; USEs untouched).
- **New `AdsGetServerVersion(hConn, buf, len)`** (OpenADS extension,
  `ace.h` + `.def` + x86 stdcall wrapper). Remote → stripped dotted
  version; local/unresolvable → DLL's own `OPENADS_VERSION_STR`. Empty +
  `AE_SUCCESS` = unknown. Valid local handles never adopt an unrelated
  remote via the thread-default fallback (same guard as `AdsCreateTable`).
- **Bridge:** `OADS_ADSVERSION()` now returns the full dotted build
  parsed from the desc (`"1.09.27"`, SAP-shape fallback); new
  `OADS_SERVERVERSION(hConn)` (`tools/fwh_patch/openads_ado_bridge.prg`).
- **Tests:** `tests/unit/network_version_test.cpp` — remote == local ==
  desc token (same binary in-process). Full suite 1535/1536 (only the
  pre-existing MinGW-only CDX alloc-tail case).

### Pendiente

- User's `TimeToReadiness == 3:16.963 == 197s` metric is not produced by
  this repo — awaiting definition (server startup on EC2 vs app startup
  over WAN) before investigating.

## 2026-07-27 — Fix: Legacy AdsCreateIndex path resolution

### Problema reportado por Pritpal Bedi

"Can't create index no matter I provide filename with or without path."

### Causa raíz

`AdsCreateIndex` (wrapper legacy usado por Harbour's `INDEX ON ... TO`) NO
resolvía paths de índice igual que `AdsCreateIndex61`:

| Caso | Antes (bug) | Después (fix) |
|------|-------------|---------------|
| Bag vacío | No creaba CDX structural | Crea `<tabla>.cdx` |
| Path relativo | No resolvía contra directorio de tabla | Resuelve correctamente |
| Sin extensión | Caía a creación NTX | Auto-agrega `.cdx` |

### Fix

Agregado bloque de resolución de path en `AdsCreateIndex` (ace_exports.cpp)
que replica la lógica de `AdsCreateIndex61`.

### Tests nuevos

| Test | Qué valida |
|------|------------|
| `AdsCreateIndex61: bag name without extension → auto .cdx` | Auto-add `.cdx` |
| `AdsCreateIndex61: bag name with .cdx extension` | Works as-is |
| `AdsCreateIndex61: absolute path with drive letter` | Windows drive paths |
| `AdsCreateIndex61: empty bag name → structural CDX` | Table-stem CDX |
| `AdsCreateIndex61: bag in subdirectory` | Relative subdir |
| `AdsCreateIndex61: backslash path` | Windows separators |
| `AdsCreateIndex (legacy): bag name without extension` | **Fixed bug** |
| `AdsCreateIndex61 + AdsOpenIndex round-trip` | Create + reopen |

---

## 2026-07-26 — Pruebas de bloqueo: cobertura comprehensiva

### Archivo nuevo: `tests/unit/abi_lock_comprehensive_test.cpp`

23 nuevos test cases que cubren gaps en la suite de locking existente.

### Hallazgos documentados

| API / Comportamiento | Detalle |
|----------------------|---------|
| `AdsGetNumLocks` | Cuenta **solo locks de registro**, NO locks de tabla |
| `AdsTestRecLocks` | No-op diagnóstico: siempre retorna 0 (AE_SUCCESS) sin importar el estado |
| `AdsGetAllLocks` | Retorna array con los recnos lockeados; table locks no aparecen |
| `AdsIsTableLocked` | Solo refleja `AdsLockTable`, NO `AdsExclusive` en local mode |
| Exclusive open (local) | `AdsIsTableLocked` retorna 0; `AdsSetString` retorna 5035 (lock required) |
| Exclusive open (remote) | El servidor gestiona el lock, write sin lock explícito funciona |
| Re-entrant lock | 2x `AdsLockRecord` mismo recno → 2x `AdsUnlockRecord` necesarios |

### Tests nuevos

| Test | Qué valida |
|------|------------|
| `AdsGetNumLocks returns 0 on freshly opened table` | Lock count starts at 0 |
| `AdsGetNumLocks increments with each record lock` | Count increments/decrements correctly |
| `AdsGetNumLocks does NOT count table lock` | Table lock is NOT counted |
| `AdsGetAllLocks returns locked record numbers` | Returns {3,7,15} correctly |
| `AdsGetAllLocks returns 0 count when table-locked` | Table lock doesn't appear |
| `AdsIsTableLocked: no lock → FALSE, lock → TRUE` | Basic lock/unlock cycle |
| `AdsIsTableLocked returns FALSE when only record-locked` | Record lock ≠ table lock |
| `AdsTestRecLocks: diagnostic hook — always returns success` | Documents no-op behavior |
| `AdsGetTableLockType: returns the open-mode lock type` | Shared vs Exclusive |
| `Multi-record lock accumulation` | Lock 5, unlock reverse, verify count each step |
| `Re-entrant record lock: two locks on same recno` | Need two unlocks |
| `Closing a table releases all its record locks` | No locks after close+reopen |
| `Lock on NTX table: lock and unlock record successfully` | NTX lock offsets work |
| `Exclusive open: table is reported locked; record lock still needed for writes in local mode` | Documents local exclusive behavior |
| `AdsIsRecordLocked: record 0 (current) vs explicit recno` | Current record vs explicit |
| `Append auto-lock: GetNumLocks increments, then decrements on unlock` | Auto-lock + unlock cycle |
| `Table lock and record locks coexist independently` | Coexistence of both lock types |
| `AdsGetAllLocks: lock many records, verify all returned` | 5 locks, sorted compare |
| `AdsIsRecordLocked on record 0 with no current record` | BOF state edge case |
| `Lock retry: tight policy → contention resolved within expected window` | Timing of retry loop |
| `Disconnect releases all locks on all tables` | Disconnect cleans up all locks |
| `AdsIsRecordLocked after write+flush preserves lock state` | Write doesn't drop lock |

### Pruebas completadas hoy

| Suite | Resultado |
|-------|-----------|
| Lock tests locales (37 tests) | ✅ 37/37 passed (23 new + 14 existing) |
| Total assertions lock | 511 passed, 0 failed |
| Full test suite (smoke subset) | ✅ Sin regresiones |

---

## 2026-07-01 — Investigación y fix del reporte FWH REMOTE

### Problema reportado

Un usuario FWH (FiveWin) reportó que al usar `openads_serverd` en modo
REMOTE con la clase `TDatabase` de FWH:

1. **FieldGet crashea después de USE** — En LOCAL mode el record buffer
   siempre está poblado después del open. En REMOTE no, y `FieldGet`
   intenta leer de un buffer NULL → crash.

2. **Production CDX no se auto-asocia** — `OrdBagName()` devuelve vacío
   después de abrir la tabla. En LOCAL funciona correctamente.

3. **RddSetDefault("ADSCDX") falla** — El usuario sospecha que está
   accediendo al archivo equivocado o que la secuencia de inicialización
   remota no es la misma que la local.

### Qué se investigó

Se revisó el flujo completo de `AdsOpenTable90` en modo REMOTE:

```
FWH: USE table VIA "ADSCDX"
  → AdsConnect60(tcp://host:port/, ADS_REMOTE_SERVER)
  → AdsOpenTable90(hConn, name, alias, ADS_CDX, ...)
      → Wire: OpenTable opcode → Server abre la tabla
      → Server: STAT del .cdx → envía prod_bag_path en OpenTableAck
      → Client: recibe OpenTableResult{id, prod_bag_path}
      → Client: auto-calls AdsOpenIndex(bag_path)
          → Wire: OpenIndex → Server abre CDX localmente
          → Server: retorna tags + bag_path
          → Client: crea RemoteIndex con bag_path
      → Client: resetea active_index_id = 0 (orden natural)
```

### Hallazgos

| Pregunta del usuario | Respuesta |
|---------------------|-----------|
| ¿FieldGet es válido inmediatamente después del open? | **NO.** Se necesita `DbGoTop()` primero. El buffer del cliente está vacío. Esto es un bug real en ace64.dll que necesita fix. |
| ¿FieldGet en memo field funciona en REMOTE? | **No funciona** si no hay buffer previo. Los offsets de crash (0x2, 0xB) indican buffer pointer NULL. |
| ¿Hay un ejemplo de REMOTE contra DBF/CDX existente? | **No había.** Se creó uno nuevo (ver abajo). |

### Códigos revisados

| Archivo | Líneas | Qué hace |
|---------|--------|----------|
| `src/network/session.cpp` | 554-616 | `Opcode::OpenTable` handler — abre tabla, STAT del CDX, envía bag_path |
| `src/network/session.cpp` | 1321-1375 | `Opcode::OpenIndex` handler — abre CDX vía ABI, retorna tags + bag_path |
| `src/abi/ace_exports.cpp` | 5200-5260 | `AdsOpenTable90` remote path — auto-opens production CDX after table open |
| `src/abi/ace_exports.cpp` | 9911-9965 | `AdsOpenIndex` remote path — crea RemoteIndex con bag_path |
| `src/abi/ace_exports.cpp` | 23551-23580 | `AdsGetIndexFilename` remote — retorna bag_path del RemoteIndex |
| `src/session/connection.cpp` | 166-251 | `Connection::open_table` — abre tabla + adjunta memo, pero NO abre CDX automáticamente |
| `src/network/client.cpp` | 1171-1243 | `RemoteConnection::open_index` — parsea OpenIndexAck con tags + bag_path |

### Test creado

**`tests/unit/abi_remote_prodcdx_test.cpp`** — 8 test cases que validan
exactamente el workflow FWH contra un servidor remoto con datos reales:

| Test | Qué valida | Issue FWH |
|------|------------|-----------|
| `OrdBagName returns production CDX bag path after open` | AdsGetNumIndexes > 0, AdsGetIndexFilename retorna nombre del CDX | #2 |
| `AdsGetIndexName returns tag names for all orders` | Cada orden tiene tag name válido | #2 |
| `GoTop + FieldGet on first record after open` | AdsGotoTop → AdsGetField no crashea | #1 |
| `DbSetOrder by number changes cursor order` | Cambiar orden cambia el registro top | #3 |
| `DbSetOrder by tag name` | Funciona por nombre, walk de registros | #3 |
| `Ordered full-scan counts match record count` | Scan completo con orden = record count total | General |
| `Multiple tables open simultaneously` | customer.dbf + invoices.dbf abiertos a la vez | General |
| `FieldGet on multiple fields after Skip` | FieldGet por nombre en todos los campos tras Skip | #1 |

### Entorno de prueba

- **Servidor:** iMac `192.168.18.184`, puerto `16262`, data_dir `/tmp/openads_mac`
- **Datos:** `customer.dbf` (100 records, CDX production), `invoices.dbf` (1000 records, CDX), `invoicedetail.dbf` (3000 records, CDX), `items.dbf` (20 records, CDX)
- **SSH:** `Anto@192.168.18.184`, password `1234`
- **Ejecución:** `$env:OPENADS_TEST_REMOTE = "tcp://192.168.18.184:16262/"; .\openads_unit_tests.exe -tc="REMOTE*"`

### Fix pendiente (no implementado aún)

El bug real es que después de `OpenTable` remoto, el cursor del engine
queda en posición indefinida. En LOCAL mode, el engine deja el cursor
poblado (aunque en BOF). En REMOTE, el cliente no tiene ningún buffer
hasta que pide una navegación.

**Opciones de fix:**

1. **Servidor: ejecutar GotoTop implícito después de OpenTable** — El
   OpenTableAck podría incluir el recno actual y los primeros bytes del
   registro. Esto arregla el crash pero cambia la semántica (ADS no hace
   GoTop implícito).

2. **Servidor: el OpenTableAck incluye el record buffer completo** — Más
   datos por round-trip pero el cliente tiene todo de inmediato.

3. **Cliente: AdsOpenTable90 hace GotoTop implícito en REMOTE** — El
   cliente envía GotoTop después de recibir el OpenTableAck. Esto es lo
   más cercano al comportamiento LOCAL.

### Pruebas completadas hoy

| Suite | Resultado |
|-------|-----------|
| Unit tests locales (960 tests) | ✅ 960/960 passed |
| REMOTE prodcdx tests (8 tests) | ✅ 8/8 passed |
| Total assertions | 394,513 passed, 0 failed |

### Bugs corregidos

1. **`AdsGetNumIndexes` returns 0 in REMOTE** — El código consultaba
   `get_num_indexes()` al servidor, que retornaba 0 porque el engine
   handle no tenía el production index abierto. **Fix:** contar
   `rt->index_handles.size()` localmente (ace_exports.cpp:13110).

2. **FieldGet crashea después de OpenTable en REMOTE** — El buffer del
   registro estaba vacío después del open. En LOCAL el cursor queda en
   BOF con buffer válido. **Fix:** GoTop implícito después del auto-open
   del production CDX en `AdsOpenTable90` (ace_exports.cpp:5260).
   Un round-trip extra por tabla abierta, trivial comparado con el crash.

3. **FieldGet por ordinal con string "1"** — Los tests usaban
   `(UNSIGNED8*)"1"` (string literal) que en 64-bit tiene dirección
   alta (>0x10000), por lo que el detector de ordinal no lo reconoce.
   **Fix del test:** usar el idiom ACE correcto
   `reinterpret_cast<UNSIGNED8*>(static_cast<std::uintptr_t>(1))`.
   El código del DLL ya era correcto.

4. **AdsGetIndexName retorna tag vacío** — El CDX de customer tiene
   un tag estructural cuyo nombre es todo padding (se trimea a vacío).
   **Fix del test:** aceptar nombre vacío como válido para tags CDX
   estructurales de dBASE.

5. **Test AdsSetIndexOrderByHandle asumía 2+ tags** — customer.cdx solo
   tiene 1 tag. **Fix del test:** trabajar con 1 tag, comparar solo
   cuando hay 2+.

### Pendiente

- [x] Implementar fix para el crash de FieldGet en REMOTE → **Hecho: GoTop
       implícito después de AdsOpenTable90 en modo REMOTE**
- [ ] Agregar test de memo fields en REMOTE (las tablas actuales no tienen .fpt)
- [ ] Agregar test con `RddSetDefault("ADSCDX")` para reproducir el caso exacto del usuario
- [ ] Verificar que el fix funciona con la app real del usuario FWH
- [ ] Investigar por qué customer.cdx tiene tag con nombre vacío
      (posiblemente creado por herramienta que no pone nombre al tag default)

---

## 2026-07-15 — rddads: HB_FUNC wrappers de setters tipados en `adsfunc.c` — DONE / no es gap de OpenADS

**Resuelto 2026-07-17 (RCB):** los wrappers `HB_FUNC( ADSSET* )` viven en el
`rddads` que cada usuario compila con SU Harbour — es un asunto de la
distribución de Harbour, no de OpenADS. RCB lo coordinará con los
mantenedores de Harbour. Si el `rddads.lib` de un usuario no trae estos
wrappers, simplemente no podrá llamarlos desde `.prg`; el RDD normal
(`REPLACE`/`FIELD->x :=`) no los usa y funciona igual.

Para referencia, los `HB_FUNC` de setters tipados de valor que un
`adsfunc.c` completo debe exponer (el árbol `f:\harbour3.2-bcc7.3` los tiene
todos; `bcc7.4` no tiene ninguno; `gcc6.3` sólo le falta MONEY):

`ADSSETFIELD, ADSSETSTRING, ADSSETLONG, ADSSETDOUBLE, ADSSETSHORT,
ADSSETDATE, ADSSETLOGICAL, ADSSETMONEY, ADSSETBINARY, ADSSETNULL,
ADSSETTIMESTAMP` (familia completa: 21 wrappers `ADSSET*`, incluyendo los de
configuración `ADSSETAOF`, `ADSSETDATEFORMAT`, etc., que sí suelen estar).

---

## 2026-07-17 — Script Engine (SQL scripting: stored procs / UDFs / triggers)

**Design doc: `docs/script-engine.md` (accepted 2026-07-17).** OpenADS had no
real script execution: a ~310-line string interpreter for UDFs (untyped, only
`+`/`-`, IF skipped), NO path at all for DD script procs (`EXECUTE PROCEDURE`
→ 5000), a third fragment for triggers that silently swallowed failures, and
no `EXECUTE IMMEDIATE`. Replacement: ONE typed engine in `src/engine/script/`
(lexer → parser → AST cached per body → typed interpreter); embedded SQL and
subqueries delegate through a `SqlBridge` into the one SQL executor.

**Language rules are oracle-verified** — 34 probes against SAP `ace64.dll`
recorded in the doc §10. Highlights: strict typing (`'5'+1` errors, CHAR ↔
number assigns error), integer division, only `=`/`<>`, `LEAVE` (not BREAK),
`TRY…CATCH ALL…END TRY` + `RAISE name(code,'msg')` + `__errcode/__errtext`,
3-valued NULL logic, `{d '…'}` literals, case-insensitive variables that
error on column-name collision.

**S1 (language core) status:** engine implemented (`value/lexer/parser/exec` +
`parse_params`), 21 unit tests / 205 assertions green
(`tests/unit/script_engine_test.cpp`, each case mirrors a probe). UDF call
site (`K::Udf`) switched to the engine via `scriptbridge::` in
`ace_exports.cpp`; the old `proc::` interpreter is DELETED. UDF errors now
PROPAGATE and fail the SELECT (SAP parity) instead of silently returning "".
Typed column/literal/nested-call arguments; UDF→UDF recursion is direct (no
SQL round-trip), depth-capped at 32.

**S2 (DD procs + triggers) status — DONE:** the two paths that did not exist
before now run through the engine (`scriptbridge::` in `ace_exports.cpp`):

- **`EXECUTE PROCEDURE` → DD SQL-script procs** (`run_dd_procedure` +
  `ProcBridge`). Inputs bind as scope variables; `(SELECT p FROM __input)` is
  rewritten (`__input`→`system.iota`, param→literal) so it resolves to a plain
  select. Output params create a per-call temp `AS FREE TABLE`; the body
  `INSERT`s into `__output` and that table is returned as the statement cursor,
  SAP-style. Dispatched from `exec_sql_direct` ahead of the native-AEP path;
  script-proc errors fail the statement.
- **Trigger bodies through the engine** (`script_run_trigger_body` +
  `TriggerBridge`, replacing the old error-swallowing fragment).
  `__new.field`/`__old.field` substitute from the collected row image; a bare
  `SELECT col FROM __new/__old/__input` takes a one-value fast path; normal
  triggers that re-reference `__new/__old` skip (no double-write), INSTEAD OF
  runs the write. `INSERT INTO __error(code,'msg')` surfaces as an error.
  Failures PROPAGATE via `fire_triggers_`' `out_err` → oracle probe Q6:
  **BEFORE** failure returns error + blocks the write, **AFTER** returns error
  + keeps it. Verified on INSERT/UPDATE/DELETE and trigger→`EXECUTE PROCEDURE`
  chains.
- **pmsys-driven builtins/syntax** picked up along the way: `USER()`,
  `CHAR()`/`CHR()`, `NOW()`/`CURTIMESTAMP`, `SET @x = …` assignment form,
  `[bracketed identifiers]`, and Char→numeric coercion so
  `@n = (SELECT COUNT(*) …)` (aggregate columns arrive as text) assigns to a
  numeric slot. `EXECUTE IMMEDIATE` also landed early (routes through the same
  bridge). Cursor `DECLARE … CURSOR` / `WHILE FETCH` now PARSE but raise a
  clean "not supported yet (S3)" runtime error instead of a parse failure.
- **Tests:** `tests/unit/abi_script_proc_test.cpp` — 12 integration cases
  (proc `__output`/`__input`, control flow, error propagation, BEFORE/AFTER
  block-vs-persist on all three DML verbs, trigger→proc chains), all green;
  full unit suite green.

**S3 (cursors + completions) status — DONE 2026-07-18.** Probe-first per the
design: 40+ new oracle probes against SAP ADS 11 recorded in
`docs/script-engine.md` §11 (battery script `tools/qa-diff/s3_probes.ps1`,
driver `dd_meta_dump --sql`); they settled §9 Q5 (no `FETCH NEXT`/`INTO`, no
`WHERE CURRENT OF`; FETCH is boolean; the 2218-2224 error matrix) and Q11/Q12
(FINALLY always runs — even uncaught; `CATCH <name>` matches case-insensitively;
uncaught RAISE = 7200/2224 `{[name] code : msg}`).

- **Cursors** end-to-end: `DECLARE c CURSOR [AS]`, `OPEN [AS]` (rebinds),
  `FETCH`, `CLOSE`, `WHILE FETCH … DO`, `IF FETCH c THEN`, `c.field` /
  `c.[br field]` / `@c.field` in expressions AND substituted into embedded
  SQL + `EXECUTE PROCEDURE` args; re-open rescans; nested per-row re-open
  (the `sp_mgGetAllLocksAllTablesAllUsers` pattern). SAP error-state parity:
  closed 2220 / double-open 2221 / unbound 2219 / undeclared 2218 /
  no-row 2223 messages.
- **TRY/CATCH/FINALLY** per F-probes (≥1 CATCH-or-FINALLY required; order
  body→catch→finally; FINALLY on uncaught before propagation).
- **CHAR(N) pad-on-assign** + rtrim `LENGTH` (N-probes) — explains SAP's
  `''+'a'` behavior. **Declare-first rule** (2217) enforced.
- **Top-level scripts through `AdsExecuteSQLDirect`** (§10 mechanism): full
  multi-statement scripts run through the engine; last SELECT's cursor is
  the statement cursor (real handle passed through; engine-internal cursors
  materialize to a temp DBF). This is what the probe battery, ARC-style
  ad-hoc scripts, and pmsys `sp_GetPhysicalPath`'s full-script
  `EXECUTE IMMEDIATE` needed. EI itself discards the inner cursor
  (oracle-checked).
- **Typed trigger row images**: `TrigField_` carries the field type char, so
  `DECLARE n CURSOR AS SELECT * FROM __new` + `n.recurring > 0` sees numbers
  (Trig_Container shape); fixed the S2 enum-vs-char memo-skip latent bug.
- **Bug found by the differential battery (C22)**: ordinal field access on a
  projected SELECT cursor returned the BASE table's column;
  `AbiSqlCursor::field` now resolves by name first.
- **Tests**: +17 engine cases (fake-bridge cursor/FINALLY/padding, each
  mirroring a probe) and +5 ABI cases (cursor copy loop, cursor over
  `EXECUTE PROCEDURE`, typed `__new` trigger cursor, declare-order); C/F/N
  battery green against `openace64.dll` except two known SQL-engine (not
  script) gaps: bracketed column aliases (C16) and column-list-less INSERT.

**Next:** S4 candidates = transactions in scripts (Q7), recursion-limit probe
(Q8), `LASTAUTOINC`/`::conn` system values (Q10), the two SQL-engine gaps
above, builtin sweep continuation. PMSYS proc-parity run
(`sp_GetPhysicalPath`, `sp_SaveIntoAuditLog`, `sp_ChargeLateFees`, …) against
the converted DD is the natural S4 gate.
