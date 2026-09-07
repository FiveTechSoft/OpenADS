# OpenADS v1.09.25

## WAN latency: USE ~4 RTTs → ~2, voucher save ~24 → ~4 (RDD-only apps, no app changes)

Follow-up to v1.09.24 (write coalescing). This release attacks the
other half of the WAN bill: table opens, which Vouch-style apps pay
every few seconds all session long.

### Changes

- **Warm `OpenTableAck` (USE ~4 RTTs → ~2).** Schema + first row
  (with lookahead block) ride the open reply as length-gated TLV
  sections, so a remote `USE` costs open + production auto-open only —
  no `DescribeTable`, no implicit `GotoTop`. Old clients/servers fall
  back to the legacy calls; unknown tags skip by length.
- **Orphan-twin fix (correctness).** A *failed* `OpenIndex` left its
  freshly created server-side ABI twin behind; later writes went to
  the twin while `fetch` packed the engine table (stale blanks until
  a nav reloaded from disk). Failed opens now drop a twin they
  created; pre-existing twins untouched.
- **Fixed-width `ads_err.log` text mirror.** `DATETIME CODE SOURCE
  LINE PID TID SESSION CLIENT OP TABLE DETAIL`, 3-space separators,
  header line, same size-cap rotation as the DBF. Per-frame
  session/client/op/table context; `Op: path` details backfill the
  columns for older call sites.
- **v1.09.24 batching below, shipped:** `SetFields` (0x5E) one-frame
  flush of coalesced `AdsSet*` calls (first set probes immediately so
  write-guard errors keep their timing), zero-RTT production
  `OpenIndex` dedup, `.z01` production bags, release-body automation.
- **Tests.** `network_setfields_test` (batch round-trip, durability,
  skip-away targeting), `network_open_warm_test` (server opcode
  counters prove zero `DescribeTable`/`GotoTop` frames + prefetched
  skips), `M12.4` warm-shape assertions, date-format expectations
  aligned with display behavior; MSVC `/W4` clean.

### Compatibility

- Same mix-and-match rules as v1.09.24: capability-gated batching and
  length-gated sections; old peers behave exactly as before.

### Test Results

- Windows MSVC x64/x86, Harbour smoke, PHP, SQL smokes: green.
- Linux/macOS: green except pre-existing load-sensitive race tests
  (`mt_contention`, `openindex_create_race`, `remote_create_stress`),
  also red on the `v1.09.21`–`v1.09.23` tags' runs.

### Packages

- `openads-1.09.25-windows-x64.zip`
- `openads-1.09.25-windows-x86.zip`
- `openads-1.09.25-linux-x64.tar.gz`
- `openads-1.09.25-macos-universal.tar.gz`
