## Changes

### REMOTE — ordered/scoped browses: correctness fixes + read-ahead (M12.34)

**If you browse remote tables with an active index order (and optionally a scope) under `SET DELETED ON`, update to this release — the fixes below shipped AFTER the 1.8.12 binaries.** Update **both** `openads_serverd` and `openace64.dll`: several of these fixes live on the client side.

- **`SET DELETED ON` did not hide deleted rows mid-scan (remote).** Flipping `AdsShowDeleted` left the client's read-ahead block holding rows read under the old visibility, so deleted records kept appearing (or live ones vanished) with no wire traffic at all. The flip now invalidates every cached row and look-ahead queue on both sides.
- **Relations followed a stale parent row** — a parent/child browse showed the wrong child data from the second skip onward. The relation now reads the parent key from the client's current row, not the server's lagging cursor.
- **Stale-row prefetch bugs**: `AdsSeek`/`AdsSeekLast`/`AdsSetIndexOrder(ByHandle)` did not drop the look-ahead queue; `AdsGetRecord` read the server's lagging cursor.
- **Read-ahead now works on ordered browses** (it was disabled whenever an order was active): a 299-row ordered scan went from 598 wire round-trips to 7. Adaptive depth 8→64, 32 KB cap, `AdsCacheRecords` is now honoured (0/1 disables, N forces a depth).
- **Seek/scope operations reset the read-ahead ramp** (wasted-bandwidth fix), and `GotoTop`/`Seek` come back warm — the first read after a reposition costs no extra round-trip.

### Examples

- `examples/fivewin/xbrowse_delscope.prg` — FiveWin xBrowse over a remote table with `SET DELETED ON` + an index scope and records deleted inside the scoped range; its `/auto` mode asserts no visible deleted row and no duplicated row in either walk direction.

Full details in [CHANGELOG.md](CHANGELOG.md).
