# OpenADS v1.09.24

## WAN chattiness: transparent batching for RDD-only apps (Vouch) + orphan-twin fix

RDD navigational apps (Harbour `rddads`, no SQL) pay one WAN round-trip
per `Ads*` call. At ~47 ms RTT a 20-field voucher cost ~24 RTTs (~1.1 s).
This release collapses that to ~4 RTTs (~0.2 s) with **zero application
changes** — all inside `ace32.dll` / `openads_serverd`, fully compatible
both ways with older peers.

### Changes

- **Write coalescing (`SetFields` 0x5E + client buffering).** Consecutive
  `AdsSet*` calls buffer client-side and flush as one batch frame on the
  next visibility event (read/nav/lock/commit/close). The first set of a
  run still goes out immediately (probe), so write-guard errors (record
  locked by another station, X# GoHot lock-required) surface on the
  writing call exactly as before; runs of 2+ sets batch. Same-handle
  read-after-write is served from an overlay, so validation reads cost
  no extra round-trip. Flush re-applies nothing twice (the probe value
  is skipped — single-field edits cost what they always did).
- **Zero-RTT production `OpenIndex` dedup.** `rddads` re-opens the
  already-auto-opened production bag on every `USE`; the client now
  serves it from cache (`.cdx`/`.z01` matched by stem). Temp bags
  always go to the wire.
- **Orphan-twin fix (read-after-write split).** A *failed* `OpenIndex`
  left its freshly created server-side ABI twin behind; every later
  write went to the twin while `fetch` packed the engine table, so
  same-handle read-after-write served stale blanks until a nav
  reloaded from disk. Failed opens now close + erase a twin they
  created (pre-existing twins untouched).
- **`.z01` production bags (Vouch).** Server `OpenTableAck` STATs
  `.cdx` then `.z01`; client fallback and local auto-open bind
  whichever exists. Ends the `5018 OpenIndex` flood + `ads_err` write
  per open for Vouch-style bags.
- **Fixed-width `ads_err.log` text mirror.** Every entry also appends
  to `ads_err.log`: fixed widths, 3-space separators, header line, plus
  `PID`/`TID`/`SESSION`/`CLIENT`/`OP`/`TABLE` columns the frozen DBF
  schema cannot carry. Same size-cap rotation as the DBF.
- **Release body automation.** `publish` uses `release-notes-<ver>.md`
  as the GitHub Release body (fails fast if missing).
- **Tests.** New `network_setfields_test` (batch round-trip, durability
  across reconnect, skip-away flush targeting); date-format expectations
  updated to display format (`5af6fafa` behavior); `ConnectAck` caps
  echo asserted in `network_server_test`.

### Compatibility

- New client + old server: per-field loop, dedup active, no batching.
- Old client + new server: single ops, full speed as before.
- No wire changes for existing opcodes; `ConnectAck` gains a trailing
  `[u32 LE]` caps word old clients ignore.

### Test Results

- Windows MSVC x64/x86: full suite green.
- Linux/macOS: green except pre-existing load-sensitive race tests
  (`mt_contention`, `openindex_create_race`, `remote_create_stress`),
  red on this platform since before these changes (also failing on the
  `v1.09.22`/`v1.09.23` tags' runs).

### Packages

- `openads-1.09.24-windows-x64.zip`
- `openads-1.09.24-windows-x86.zip`
- `openads-1.09.24-linux-x64.tar.gz`
- `openads-1.09.24-macos-universal.tar.gz`
