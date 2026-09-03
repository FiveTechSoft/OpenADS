# Release notes — v1.09.21 (2026-09-03)

## Fixed — 700-client storm clean end-to-end: convoy + wire frame desync (Pritpal Bedi)

`B_BIG.exe` × 700 storm showed three different failure modes across
runs — runs 28/29 left **blank rows** (AppendBlank acked, REPLACEs
lost) and instances dying silently; run 30 fell at 54 167 / 70 000
with 187 partial instances, ~370 cascading
`SetField/AppendBlank/GotoTop: server error` and 13 `recv() failed`.

Server `ads_err` was EMPTY during the kill window — the server never
returned Error frames; **all damage was client-side**. The new
OPDUMP per-opcode telemetry (`OPENADS_OPDUMP=1`) pinned the physics:

1. **CDX write-lock convoy.** The process-wide per-path CDX write
   lock (one entry for TestIndex.cdx shared by all 700 sessions)
   queued appends for up to **52.7 s** per op (server batch deadline:
   60 s).
2. **Client `SO_RCVTIMEO` was 30 s** — BELOW the server deadline.
   When an op exceeded 30 s the client's `recv()` expired, the late
   reply stayed in the socket buffer, and the next request on the
   shared connection consumed that **stale frame**: opcode mismatch →
   cascading "server error" → instance death. Blank rows = AppendBlank
   acked but SetField lost to the desync.

### The fix is three layers

- **Wire client:** `SO_RCVTIMEO` 30 s → **180 s** — clears the 60 s
  server deadline with margin; a genuinely dead connection is still
  detected by keepalive + transport poisoning.
- **Wire client:** `RemoteConnection::request()` now **poisons the
  transport** (closes it) on any send/recv failure. Frame desync
  becomes impossible: later requests fail fast with
  `AE_NO_CONNECTION` (5036) instead of consuming a stale reply.
- **CDX convoy mitigation:** write-lock **waiter handoff** (the OS
  byte lock stays parked for the next waiter, capped at 64 handoffs
  so external peer processes still get their turn), per-append
  `AdsFlushFileBuffers` dropped — durability is now delivered at
  `DbCommit`→`FlushTable` (removes a ~9× CDX page-write
  multiplication under the storm), an in-process DBF append gate (one
  kernel waiter-IRP instead of 700), and `handle_registry` reader
  paths moved to `shared_mutex`.

Also in this release (storm fixes from the same campaign):

- `AdsOpenIndex`: single shared exists-check (the `5018 OpenIndex`
  flood is gone) and a refresh snapshot of the index-bindings map;
- OPDUMP telemetry: `OPENADS_OPDUMP=1` makes serverd sample
  per-opcode latency (count/avg/max) every 10 s into the error log;
- session/worker exception containment: a failing handler (e.g.
  `bad_alloc` once the address space is exhausted) closes the session
  instead of killing the whole server; serverd is linked
  LARGEADDRESSAWARE.

### Evidence

Two consecutive instrumented storm runs **PASS**: 70 000 / 700
instances exact, 0 missing, 0 blank, 0 RTERROR, full CDX integrity
walk clean (runs 31 + 32 on unmodified `B_BIG.exe`).

### What to test (Pritpal)

1. `B_BIG.exe` × 700 against a fresh server: expect 70 000 rows,
   every instance heartbeat `OK|0`, no `RTERROR` lines, no blank rows.
2. Normal app paths: nothing changed in the protocol — drop-in
   serverd + ace32/ace64 replacement, no config or client changes.
3. If a server seems slow under load, set `OPENADS_OPDUMP=1` and read
   the OPDUMP/OPI records in `ads_err.dbf`: top opcodes by avg latency
   point straight at the queue.

## Packages

- `openads-1.09.21-windows-x64.zip`
- `openads-1.09.21-windows-x86.zip`
- `openads-1.09.21-linux-x64.tar.gz`
- `openads-1.09.21-macos-universal.tar.gz`