## 1.8.17 — 2026-07-20

### Fixed — remote DbCreate / AdsCreateTable wrote next to the client app

When a Harbour/X# client connected with `AdsConnect60(tcp://…)` and called
`dbCreate` / `AdsCreateTable`, create still ran against a *local* default
connection (cwd of the client process) because there was no remote
`CreateTable` wire opcode. The table landed next to the application, then
the post-create `AdsOpenTable` (which *does* route remote) failed with
`ADSCDX/5103 OpenTable: open failed`. Reported by Pritpal Bedi against
v1.8.16: path-to-open of DbCreate had been fixed, path-to-create had not.

`AdsCreateTable` and `AdsDropTable` now detect a `RemoteConnection` handle
and forward create/drop over the wire (`CreateTable` / `DropTable`
opcodes). The server writes under its data directory via the session ABI
connection (so the 1.8.15 absolute-path folding also applies remotely);
the client then re-opens through the normal remote `OpenTable` path.
Remote `AdsConnect60` also records the handle as the rddads default so
create-with-hConnect=0 works. Regression tests cover bare-name create +
open + drop and drive-rooted remote create. Verified against a live
`openads_serverd` on macOS (iMac) over the LAN.

## 1.8.16 — 2026-07-19

### Fixed — DbSeek missed on compound, nested-function and probe-sized CDX keys (#131)

Three related defects made `DbSeek` miss on local (rddads) CDX tags whose
key expression was anything more than a bare field, because the key the
engine *stored* differed from the full-width key Harbour computes for the
seek:

- **Compound keys lost operand width.** In a key like
  `Upper(SYM)+Upper(SUB)+DToS(DAT)`, a CHARACTER/DATE field reference
  decoded to its right-trimmed value, so a blank middle operand contributed
  zero bytes and the concatenated key came out shorter than Harbour's. Field
  references now restore trailing blanks to the declared width (Character,
  CiCharacter, Date); Varchar and Logical are left as-is.
- **`PADR` / `PADL` / `PADC` were unimplemented**, so a pervasive nested key
  like `Upper(PadR(LTrim(NAME),10))` degraded to an *empty* key ("unknown
  function") and every seek missed. The three pad functions now evaluate
  with xBase fixed-width semantics (truncate-or-pad to n, default fill
  space).
- **The `AdsCreateIndex61` key-length probe was truncating.** A `key_len == 0`
  request now returns the *natural* full-width key (bare field, `Upper(field)`,
  and composite-expression paths) instead of a zero-length or padded key, so
  the tag is sized correctly.

Acceptance tests added (`abi_cdx_issue131_test.cpp`): compound concat width,
logical `"1"`/`"0"` → `'T'`/`'F'` seek mapping, and the nested
`Upper(PadR(LTrim(NAME),10))` case — all seek to the expected record.

## 1.8.15 — 2026-07-19

### Fixed — AdsCreateTable wrote the file next to the client app instead of the ADS data directory

When a client (Harbour rddads, X# ADSRDD, …) passed `AdsCreateTable` an
absolute or drive-rooted table name derived from its own working directory,
the create path joined that verbatim onto the connection's data directory.
Because joining an absolute path replaces the base, the data directory was
silently dropped and the new `.dbf`/`.adt` landed next to the application —
then a later `AdsOpenTable` with the same bare name resolved *under* the data
directory and found nothing.

`Connection::resolve_table_file` now strips the root and keeps only the
relative remainder (subdirs + filename) before joining, so a table always
lands under the configured data directory and a subsequent open by the same
name resolves to the very same file. `AdsCreateTable` routes its target
through the same resolver (rather than an independent join) so create and open
agree byte-for-byte on the physical path, for CDX, VFP and ADT alike.
Regression test added: a drive-rooted create lands under the data dir, does
*not* appear at the client-named absolute path, and reopens by bare leaf.

## 1.8.14 — 2026-07-17

### Fixed — SQL result cursor was left at BOF after execute (wrong data on every query)

`AdsExecuteSQLDirect` (and `AdsExecuteSQL`, which delegates to it) left the
result cursor positioned *before* the first row, where SAP ADS positions it
*on* the first row. A client that reads the current record immediately after
executing — the SAP-supported pattern, used by countless apps — got a
**phantom empty row** on every query. Proven with an aggregate, which returns
exactly one logical row: `SELECT COUNT(*)` came back as two rows (a blank,
then the count) on OpenADS versus one on SAP, and a plain `SELECT` yielded an
all-empty leading row.

Found by a differential parity dump of a real production dictionary (PMSYS)
against SAP ACE as the oracle. The fix positions the result on the first row at
the API boundary, so it applies uniformly to engine tables, memory-backed
`system.*`/aggregate results, and SQL-backend cursors, and to remote clients
(the server executes through the same entry point). `AdsGotoTop` is absolute,
so callers that already call it are unaffected — which is exactly why the whole
existing suite passed without catching this. Regression test added that reads
a result with no prior `GotoTop`.

### Backward (PgUp) read-ahead — 299 → 7 round-trips on a reverse scan

Read-ahead was forward-only: a `Skip(-1)` browse (PgUp, or a report walking a
table in reverse) paid one round-trip per row. It now gets the same treatment
as a forward scan — a 300-row reverse scan over loopback dropped from 299 wire
requests to 7 (the depth ramp: 8+16+32+64×4).

- The server attaches a **backward block** (rows *before* the cursor, in
  backward visit order) to a `Skip` with a negative step, walked and restored
  symmetrically to the forward block.
- The prefetch lag counter is now **signed** (`cursor_lag = client_logical −
  server_cursor`): a forward local drain moves it positive, a backward one
  negative, and the resync `step + cursor_lag` handles both with one formula.
  The forward path is byte-for-byte unchanged (the lag was always ≥ 0 there),
  which the existing forward tests confirm by still passing.
- The depth ramp resets on a **direction reversal**, so a PgUp right after a
  long PgDn doesn't inherit the forward run's ceiling depth.
- **New capability bit `kCapPrefetchBackward` (0x04), and it is a correctness
  gate, not an optimization.** A read-ahead block carries no direction marker
  on the wire, so a client that only understood forward blocks would drain a
  backward one the wrong way and serve the wrong record. The server therefore
  sends a backward block only to clients that advertise the bit. Safe in both
  mix directions: an old server ignores it; a new server never sends a backward
  block to a client that didn't ask for it (that client just keeps paying one
  round-trip per backward step).

A direction reversal itself costs one round-trip (the standing queue is on the
wrong side and can't serve it); the wire skip refills the queue in the new
direction. Verified end-to-end: a reverse scan is correct to the BoF boundary,
mixed PgDn/PgUp navigation lands on the right recno, and the thrift test drops
from 299 requests to 7 (and back to 299 with the backward path disabled, which
is how the test proves the read-ahead is real).

### The server's real version is now visible from the client

Repeated "the fix didn't help" reports keep resolving to an old
`openads_serverd` still running (a live service holds its exe, so an
update copy can fail silently). There was no way to prove it from the
client side: the wire `HelloAck` carried a hardcoded `openads/0.3.2` and
`AdsMgGetInstallInfo` a hardcoded `OpenADS 1.0`.

- `HelloAck` now answers with the real build version
  (`openads/<version>`, e.g. `openads/1.8.14`).
- `AdsMgGetInstallInfo` on a **remote** mgmt handle reports the
  **server's** version (one `Hello` round-trip — works against every
  wire-protocol version ever shipped; an old server identifies itself by
  answering the literal `0.3.2`). Local handles report the DLL's build.
  From Harbour:
  `AdsMgConnect("host:port")`, `AdsMgGetInstallInfo()[3]`,
  `AdsMgDisconnect()`.
- `examples/fivewin/xbpaint_delscope.prg` (new) logs that server version
  and drives the exact xBrowse repaint shape — anchor / page-read /
  `AdsGetRelKeyPos` / `DbGoto` back / advance, plus the arrow-up variant
  and scrollbar thumb drags via `AdsSetRelKeyPos` — over a 300-row
  remote table with SET DELETED ON, an index scope, and deleted rows
  inside the scope.

### Remote `CloseTable` released the client's view of the table but kept the files open

Ordered navigation, index scopes, locks and a few other remote operations
run through a per-session "shadow" ABI handle the server opens alongside
the engine table (M12.16). The wire `CloseTable` handler closed the engine
table but only *erased the map entry* for that shadow handle — the local
open itself (and its `.dbf`/`.cdx`/`.fpt` OS file handles) survived until
the session disconnected. Consequence on the app side: close a set of work
tables, then try to erase / rename / reopen-exclusive any of them, and the
files are still "in use" by the server for as long as the connection
lives — an app-level retry loop turns that into seconds of delay or an
apparent hang at "close all the invoice's files" time.

`CloseTable` now closes the shadow ABI handle with the engine table and
drops the session's index-id entries that resolved through it. Pinned by
`tests/unit/abi_remote_zombie_lock_test.cpp` (lock release across close +
file deletability straight after a remote close) and
`tests/unit/abi_remote_close_hang_test.cpp` (a 12-table invoice-shaped
open/work/close pass over a live socket, timed).

## 1.8.13 — 2026-07-15

### Remote read-ahead on ordered browses (the Mp10 xBrowse case) — 598 → 7 round-trips

Sequential prefetch shipped in M12.21, but it was explicitly disabled
whenever a controlling order was active: the look-ahead block was walked
on the engine cursor, which only ever moves in natural record order, so
for an ordered table it would have returned the wrong rows. That left the
browse that actually matters paying full price — Harbour's rddads
navigates on `hOrdCurrent` (an index handle) whenever an order is set, and
every such skip *also* re-sent a `SetOrder` frame first, then threw the
prefetch queue away. **Two round-trips per row, cache discarded each time.**

A 299-row ordered scan over loopback measured **598 wire requests before,
7 after** (`tests/unit/abi_remote_ordered_prefetch_test.cpp`). No wire
format change.

- **Index-order look-ahead.** `pack_row_trailer` now resolves an ordered
  table to its ABI handle — where the order and any scope live — so the
  block is walked in index order and the cursor restored afterwards.
- **`SetOrder` is sent once per order change, not once per row.** The new
  `RemoteTable::server_order_id` tracks what the server *actually* has
  installed (only a `SetOrder` ack writes it), as distinct from the
  client's belief, which can be set with no round-trip at all via
  production-bag auto-open. That distinction is what makes skipping the
  frame safe.
- **Adaptive depth.** Sequential detection lives on the server, as in OS
  and DB read-ahead generally: depth ramps 8 → 16 → 32 → 64 per
  consecutive forward `Skip` on a table, and any reposition, write, or
  order change resets the run. A one-off "seek a record and read it" no
  longer drags a full 64-row block it will never look at.
- **Byte budget.** The block is capped at 32 KB as well as by row count —
  64 rows of a 4 KB-record table would otherwise be a 256 KB frame. SAP
  documents the same two-sided rule for its own client cache.

### `AdsCacheRecords` is now honoured (was a documented no-op)

The caller's requested read-ahead depth now reaches the server, riding on
each `Skip` as an optional trailing `[u16]`. rddads already exposes the
call, so a Harbour app can use it directly.

- `0` or `1` **turns read-ahead off** — SAP's documented remedy for a batch
  loop that edits most of the records it visits, where every write dumps
  the block anyway and reading ahead is pure waste. This previously had no
  way to be expressed.
- `N` reads exactly `N` rows per skip, overriding the automatic ramp (SAP's
  "aggressive" setting is 100). Capped at 512 so one request cannot become
  an unbounded server-side scan.

**No capability bit, and no break in either version-mix direction.** A new
client sending the extra 2 bytes to an old server is ignored by its length
check; an old client sending none to a new server reads as
`kPrefetchDepthAuto`. That sentinel is deliberately `0xFFFF` and not `0`,
because `0` already means "disable" — had absent-been-zero, every
pre-existing client would have silently *lost* read-ahead. That exact
regression is pinned by a raw-frame test
(`tests/unit/network_skip_depth_test.cpp`), which hand-builds a legacy
8-byte `Skip` because the ABI client can no longer produce one.

### Fixed — relations followed a STALE parent row (wrong data, shipped)

`AdsSetRelation` over a remote connection pointed the child at the **wrong
parent record** from the second skip of any scan onward. A parent/child grid
— every Mp10 relation browse — showed mismatched child data.

The cause: the relation read the parent's key with a wire `GetField`, which
reads the **server's** cursor. That cursor lags the client's logical position
by however many rows were served out of the read-ahead block, so the key was
one or more records behind. It read the parent's row into the client cache
first and then ignored it.

It hid because the only coverage skipped the parent exactly **once**, and the
first skip of a scan always went to the wire (the block was still empty),
leaving the two cursors coincidentally in sync. Every skip after it drained
the block locally and drifted. Present since sequential prefetch landed
(M12.21); found while adding the warm-GotoTop below, which makes even the
first skip local and so exposed it immediately. The relation test now walks
the whole parent instead of stopping at the first row.

Reading the cached row also removes a wire round-trip per parent row, per
relation.

### The first page after a reposition is no longer cold

- **`SeekAck` now carries the row it landed on.** It used to be just
  `[u8 found][u32 recno]` — the client knew *where* it was but not *what*
  was there, so the first `AdsGetField` after a seek paid a second
  round-trip (`FetchCurrentRow`). **Seek-then-read — the most common thing a
  business app does — cost 2 round-trips; it now costs 1.** The relation code
  had been papering over this by firing a `GotoRecord` immediately after
  every seek purely to pull the row down, so a parent browse with a child
  relation paid **2 round-trips per parent row**; that frame is now only sent
  when talking to an older server.
- **`GotoTop` now comes back warm**, carrying a read-ahead block with the
  row. A browse painting its first screen no longer pays a round-trip for the
  first `Skip`.
- Deliberately **not** `GotoBottom` (a *forward* block past the last record is
  empty — that needs backward look-ahead, which is separate work) and
  deliberately **not** `Seek`/`GotoRecord` (relation navigation drives those
  once per parent row, and a block there would drag child rows over the wire
  every time for nothing). SAP draws the same line: read-ahead triggers on
  "a skip operation after ... any other movement operation" — the skip earns
  the block, not the movement.

Both are wire-compatible in either direction with no capability bit: an old
client requires `size() >= 5` on a `SeekAck` and ignores trailing bytes, and
a new client against an old server sees the short ack, parses no trailer, and
falls back to the previous behaviour unchanged.

### Fixed — a seek/scope op left the read-ahead ramp climbing (code review)

The central run-breaker that resets the adaptive read-ahead depth on a
reposition keys its map by table id, but `Seek` / `SeekLast` / `SkipUnique` /
`SetScope` / `ClearScope` frames lead with an *index* id. Passing that
through unresolved erased an absent key, so the table's ramp kept climbing
across a seek and the next `Skip` pulled a full ceiling block instead of
restarting at the floor — defeating the "seek a record, read it, move on"
case the ramp exists to protect. Rows were always correct (the block is
walked fresh), so this was a wasted-bandwidth bug, not wrong data. The
dispatcher now resolves the index id to its table before resetting.

### Fixed — `SET DELETED ON` did not hide deleted rows mid-scan (remote)

Flipping `AdsShowDeleted` changes which rows are *visible*, but it did not drop
the client's read-ahead block — and that block holds rows already read **ahead**
of the cursor, under the old visibility. So a scan kept serving them: deleted
records continued to appear after `SET DELETED ON` (and live ones vanished after
`SET DELETED OFF`), with **no wire traffic at all** to hint anything was stale.
Measured on a 60-row table with the even records deleted: the scan returned 34
rows instead of 30, including deleted ones.

This sits exactly at the seam between remote `SET DELETED` (1.8.10/1.8.11) and
sequential prefetch (M12.21) — each is correct alone. `AdsShowDeleted` now
invalidates every remote table's cached row and look-ahead queue, and the server
ends the read-ahead run on all tables in the session (its payload carries no
table id, so it cannot go through the usual per-table run-breaker).

### Fixed — stale-row bugs in the existing prefetch (wrong data, not just slow)

- `AdsSeek` / `AdsSeekLast` invalidated the cached row but **not** the
  look-ahead queue, so the next `Skip(1)` could serve a **stale pre-seek
  row** with no wire traffic, and the following wire skip sent a step
  inflated by a lag that no longer applied.
- `AdsSetIndexOrder` / `AdsSetIndexOrderByHandle` had the same hole: rows
  read in the previous order stayed queued.
- `AdsGetRecord` did not settle the prefetch lag, so it returned the raw
  image of the record at the *server's* lagging cursor rather than the
  caller's current row.

### Fixed — telemetry

- `bytes_out` was declared alongside `packets_out` but never incremented,
  so `AdsMgGetCommStats` / `sp_mgGetCommStats` always reported **0 bytes
  sent**. Now fed per reply frame.

### Performance

- `pack_one_row_abi` re-resolved every column's name and type from the ABI
  on every row (three ABI calls per column, per row). Harmless at one row
  per ack; with a 64-row block it is thousands of calls per `Skip`. The
  schema of an open handle cannot change, so it is now resolved once per
  handle and cached.

### Docs

- `AdsCacheRecords` was documented as a no-op because "OpenADS does not
  pre-cache rows" — untrue since M12.21. Now describes the automatic
  read-ahead and states plainly that the requested *depth* is still not
  honoured.
- `docs/wire-protocol.md` §5.8 documented a row-trailer layout that has
  never existed in the code. Replaced with the real format, including the
  look-ahead block and the consumed-lag protocol.

### Examples

- `examples/fivewin/xbrowse_delscope.prg` — FiveWin xBrowse over a remote
  table with `SET DELETED ON` and an index scope, records deleted inside
  the scoped range; `/auto` mode asserts no visible deleted row, no
  duplicate, and the exact expected key walk in both directions.

## 1.8.12 — 2026-07-14

### ENGINE — multi-record `Skip` counts *visible* rows under `SET DELETED ON` / filters (M12.33)

Follow-up to 1.8.10/1.8.11: with the deleted-row filter now actually
active on the server, a remote natural-order browse showed a
**duplicate of the previous row** where a deleted record used to
appear (or silently truncated the walk a few rows early on tables
longer than the prefetch window).

Root cause: `Table::skip` on the natural-order path computed the
landing as `recno + delta` (**physical** record arithmetic) and only
slid further while sitting *on* a hidden row. Every deleted/filtered
row strictly inside the range therefore left the cursor one *visible*
row short of where a Clipper `SKIP n` lands. Two remote mechanisms
turn that into user-visible corruption:

- The client prefetch fold (M12.21 option C) resynchronises the server
  with `Skip(step + prefetch_consumed)` — a multi-record skip. When it
  crossed a deleted row the server landed on the row the client had
  just painted and re-served it: the duplicated item.
- The same short landing tripped the client's same-recno EOF heuristic,
  ending walks early on tables longer than the 64-row lookahead.

The bug was latent for years but unreachable remotely: before 1.8.10
the server always ran `show_deleted=true`, so physical and visible
arithmetic coincided. LOCAL apps could hit it with any `dbSkip(n > 1)`
over deleted rows or with `AdsSetAOF`-style filters.

Fix: when the deleted filter or an AOF is active, `Table::skip` now
walks one record at a time and counts only visible rows — same
semantics as the index-order path. The index path and the lookahead
restore (`skip(-cursor_advance)`) are unchanged and verified symmetric.

Requires updated `openads_serverd` (the engine runs server-side; the
DLL fix matters for LOCAL mode).

Files: `engine/table.cpp`.
Tests: `local natural-order Skip(N) counts visible rows under SET
DELETED ON`, `remote natural-order walk under SET DELETED ON has no
duplicate rows`, and `remote natural-order walk longer than prefetch
window under SET DELETED ON` in `abi_remote_index_nav_test.cpp` (the
long-walk case reproduced the truncation: 66 rows seen instead of 69
pre-fix).

## 1.8.11 — 2026-07-14

### REMOTE — `SET DELETED ON` issued before connect now reaches the server (M12.32)

Follow-up to 1.8.10 (M12.31): a scoped remote browse still showed
deleted rows when the application ran `SET DELETED ON` **before**
`AdsConnect60` — the startup order virtually every rddads / FiveWin
app uses (`SET DELETED ON` in `Main`, then connect). LOCAL mode was
unaffected.

Root cause, two gaps left by M12.31:

- Client: the `AdsShowDeleted` broadcast only notified remote
  connections open at call time. A connection created afterwards never
  received the flag, and `connect_with_transport` did not sync it.
- Server: the `ShowDeleted` handler applied the flag to `abi_conn_`
  only if it already existed. That connection is created lazily (first
  `SetScope` / `SetOrder`), so when the opcode arrived earlier the
  lazily-created ABI connection started with the default
  `show_deleted=true` — and the per-connection flag overrides the
  engine global on ordered/scoped walks.

Fix:
- `RemoteConnection::connect_with_transport` pushes `ShowDeleted(0)`
  right after `ConnectAck` whenever the client state is "hide deleted"
  (the server default is "show", so only that direction needs syncing).
- `Session` remembers the last `ShowDeleted` state and re-applies it
  when `ensure_abi_conn` creates the lazy ABI connection.

Requires updated `openace64.dll` **and** `openads_serverd`. A 1.8.10
server honours the flag only if it changes while connected; pre-1.8.10
servers ignore it entirely.

Files: `client.cpp`, `session.{h,cpp}`.
Tests: `remote scoped walk honours SET DELETED ON issued before
connect` and `… before first ordered op` in
`abi_remote_index_nav_test.cpp`. New wire probe
`tools/remote_deleted_probe.cpp` (`openads_remote_deleted_probe`)
reproduced the leak against a live pre-fix server and verified the fix
end-to-end over TCP.

## 1.8.10 — 2026-07-14

### REMOTE — `SET DELETED ON` honoured on scoped index walks (M12.31)

With `SET DELETED ON` (`AdsShowDeleted(0)`), a scoped browse over a
remote alias (`OrdScope` / `AdsSetScope` + `GotoTop`/`Skip`) still
returned rows flagged deleted. LOCAL mode filtered them correctly.

Root cause: `AdsShowDeleted` only updated the client process; the server
session (`sess_conn_` and the parallel ABI connection used for ordered
navigation) kept `show_deleted=true`, so index walks on the server
included deleted keys inside the active scope.

Fix:
- New wire opcodes `ShowDeleted` / `ShowDeletedAck` (`0xDA` / `0xDB`).
  `AdsShowDeleted` pushes the flag to every open `RemoteConnection`.
- Server handler applies it to the global engine flag, `sess_conn_`, and
  `abi_conn_` (the handle used for `ordered_tables_` navigation).
- `SetScope` handler also calls `AdsSetIndexOrderByHandle` on the ABI
  table so scope stays bound to the active order without a prior
  `SetOrder` wire op.
- Client releases `s.mu` before the `ShowDeleted` wire round-trip (same
  deadlock pattern as remote `AdsOpenTable` / `AdsOpenIndex`).

Requires updated `openace64.dll` **and** `openads_serverd`. Pre-M12.31
servers ignore the new opcode (best-effort); upgrade both sides together.

Files: `wire.h`, `client.{h,cpp}`, `session.cpp`, `ace_exports.cpp`.
Test: `remote AdsSetScope with SET DELETED ON skips deleted rows` in
`abi_remote_index_nav_test.cpp`.

## 1.8.9 — 2026-07-10

### Zero-config OEM collation for rddads apps (`usCharType` honoured, adslocal.cfg) — #130 follow-up

The second mechanism from the v1.8.6/v1.8.7 regression report: a
Harbour rddads app has no way to call `AdsSetCollation` — its only OEM
signal is `usCharType = ADS_OEM` on `AdsOpenTable` (what
`AdsSetCharType(ADS_OEM)` produces) — and OpenADS ignored that
parameter. `INDEX ON` therefore built keys with UTF-8 casing and binary
sort while the app seeks with Harbour's CP-852 `Upper()` keys →
not-found on every row with Polish letters.

OpenADS now mirrors SAP's model (help: "Advantage Local Server
Configuration" / "Avoiding OEM Collation Mismatch Errors"): the OEM
collation language is a machine-level default, and tables opened
`ADS_OEM` pick it up with zero per-connection code.

- `AdsOpenTable` / `AdsCreateTable` honour `usCharType`;
  `AdsGetTableCharType` reports the stored value (was hard-coded ANSI).
- Default OEM collation configured via SAP-style `adslocal.cfg`
  (`[SETTINGS]` / `OEM_CHAR_SET=NTXPL852`, file next to `openace64.dll`
  or in the current directory) or the `OPENADS_OEM_COLLATION`
  environment variable (env wins; `AdsSetCollation` remains the
  per-connection override).
- Effective collation resolved per table: CDX tags of ADS_OEM tables
  build/compare/seek with the PL852 sort weights; `UPPER()` in index
  expressions cases per table (thread-local evaluation scope), so ANSI
  tables keep UTF-8 case promotion untouched.
- Unsupported `OEM_CHAR_SET` values (USA, MAZOVIA, …) leave raw byte
  order, like SAP's shipping `USA` default (#127 tracks more tables).
- If OEM data was indexed on v1.8.6/v1.8.7, `REINDEX` once after
  configuring the collation (same rule as SAP after an OEM
  collation-language change).

## 1.8.8 — 2026-07-10

### Fix: NTXPL852 / OEM collation seek broken on reopened CDX bags (#130 regression)

v1.8.6 regression, reported with a clean 1.8.4→1.8.6 bisect: `DbSeek` on
a character tag under NTXPL852 returned not-found for existing records —
but only after the bag was closed and reopened (production usage), which
the test suite never exercised.

Cause was a two-commit interaction: v1.8.5 marked ANY reopened CDX tag
with an 8-byte key as FoxNumeric (a plain `C(8)` character tag included),
and v1.8.6 made that flag select the B+tree comparator (`memcmp` for
numeric encodings). A mis-marked character tag then descended a
collation-ordered tree in raw byte order and missed keys whose PL852
weight order differs from byte order (e.g. `Ł` = 0x9D).

The reopen path now decides the encoding from the key expression — bare
fields by schema type, computed expressions only when the key is 8 bytes
AND the expression provably evaluates numeric (`Val()` heuristic +
record-1 probe, same rules as index creation). The #130 numeric `Val()`
fix is preserved. New regression test closes and reopens the bag before
seeking.

## 1.8.7 — 2026-07-10

### Index scopes (`OrdScope` / `AdsSetScope`) fixed end-to-end

Scoped browses over rddads (grids restricted to one parent key) returned
either every FOR-matching row or no rows at all. Four root causes, all
fixed:

- **`ADS_TOP` / `ADS_BOTTOM` constants** (`ace.h`): OpenADS used 0/1; the
  ACE convention (and Harbour's `ads.ch`) is 1/2. `AdsSetScope` therefore
  never set the TOP bound for rddads callers — both bounds landed on
  BOTTOM. Note this also applies over the wire: `openads_serverd` must be
  rebuilt together with the client, or TOP/BOTTOM invert silently.
- **`AdsGetKeyType` returned the wrong constant family**: it reported the
  seek/scope buffer encodings (`ADS_STRINGKEY`=1 / `ADS_DOUBLEKEY`=2)
  instead of the ACE field-type constants (`ADS_STRING`=4, `ADS_NUMERIC`=2,
  `ADS_DATE`=3, `ADS_LOGICAL`=1). rddads switches `OrdScope()`'s key
  encoding on this value, read 1 as `ADS_LOGICAL`, and sent a 1-byte
  `"T"`/`"F"` scope instead of the real key for every character-key tag.
  Now answers from the table schema for bare-field keys and falls back to
  the key encoding for computed expressions.
- **`$` ("contains") operator** was a no-op in FOR-condition evaluation
  (`STATUS $ 'IEC'` passed every non-blank row). Real containment
  semantics implemented.
- **`AdsGetKeyCount` ignored the active scope** on CDX orders — it
  returned the whole conditional index size. Now walks only the scoped
  range.

Also new, enabled by the `AdsGetKeyType` fix: rddads sends date scopes
and `DbSeek(date)` as julian-day doubles — `AdsSetScope` and `AdsSeek`
now convert those to the `YYYYMMDD` text key form for DBF date keys
(ADT/ADI date keys keep their packed binary path).

Verified end-to-end locally and over the wire against a live
`openads_serverd` (new gated tests: `OPENADS_TEST_REMOTE` scope walk /
key count, plus a seed helper via `OPENADS_SEED_DIR`). Trilingual
`AdsGetKeyType` reference pages updated.

### Build

- Removed a dead CP-852 upper table in `oem_collation.cpp` that broke
  clang `-Werror` builds (`-Wunused-const-variable`).

## 1.8.6 — 2026-07-10

- Numeric `Val()` `DbSeek` fix (#130): `memcmp` ordering for
  FoxNumeric / NtxNumeric key comparison in CDX.

## 1.8.5 — 2026-07-09

- OEM UPPER for NTXPL852/PL852 index keys + complete `Val()` numeric
  support (#130); collation scoping fix (OEM UPPER only applies when an
  OEM collation is actually active).

## 1.8.4 — 2026-07-09

### CDX — further `INDEX ON` speedups (#128)

- Single-pass batching for sibling tag rebuilds using the new
  `Table::collect_keys_for_multiple_expressions` prototype. When multiple
  tags in a bag need a full rebuild (common after ORDLSTCLEAR + repeated
  `INDEX ON`), we now do one DBF scan instead of N.
- Fast paths + OEM upper-casing for `UPPER(barefield)` and similar common
  expressions (skips per-row Parser + UTF-8 work; uses proper CP-852
  upper table for NTXPL852/PL852 data).
- Added `oem_upper` / `lookup_oem_upper_table` support in oem_collation.
- More unit test coverage: wide tables (100+ cols), composite expressions,
  additional FOR clauses, deleted records during `AdsCreateIndex61`.
- CI now builds and runs `dbf_cdx_bench` as a multi-tag smoke on every job.
- Enhanced `dbf_cdx_bench` to time realistic 4-tag `INDEX ON` sequences.

These changes attack the remaining per-row expression cost and multi-tag
scan cost after the initial read-ahead fix.

### Numeric `Val()` keys + full OEM UPPER support for rddads (#130)

- `INDEX ON Val(charfield)` now correctly creates 8-byte FoxNumeric CDX keys
  (via `evaluate_index_expr_number` probe on first record + `"VAL("` heuristic
  + `klen=8` + `FoxNumeric` encoding).
- `mark_cdx_key_encoding` on reopen now promotes any CDX with keylen==8.
- Robust `AdsSeek` conversion for cases where rddads passes the numeric as
  string digits.
- `UPPER()` (both general and fast bare-field paths) now dispatches to the
  OEM upper table (`oem_upper` + NTXPL852/PL852) when the connection uses
  national OEM collation. This makes `Upper(field)` indexes + seeks match
  Harbour behaviour under PL852/OEM.
- Unit test coverage for the exact repro path (`Val()` seek + key_length==8
  on real ASORTYM CDX from the issue).
- Verified end-to-end on the anonymised ASORTYM repro (14k rows, 125 cols,
  16-tag bag, NTXPL852): both `Val()` and `Upper()` seeks now OK, raw keys
  continue to work.

## 1.8.3 — 2026-07-09

### CDX — `INDEX ON` / REINDEX performance for local rddads (#128)

Significant speedup for local DBFCDX index creation when using Harbour
`contrib/rddads` (`ADSCDX`) or direct `AdsCreateIndex*` calls.

**Root cause**: Key collection for the bulk B+tree builder did
`goto_record(r)` for every record. This unconditionally called
`invalidate_read_cache()` on the `CdxDriver` and performed active index
cursor repositioning, defeating the 64 KB read-ahead block cache.

**Fix**:
- New internal helper `Table::load_record_for_bulk_scan()`.
- Changed the full-table scan loops in:
  - `AdsCreateIndex61` (main path + sibling-tag resync for multi-tag bags)
  - legacy `AdsCreateIndex`
  - `Table::reindex()`
- Now use direct `driver()->read_record_raw(r)` followed by bulk buffer
  install. Sequential scans benefit from large reads + memcpy.

Also updated `known-issues.md`.

This directly addresses the ~11× slowdown vs SAP ACE 11.10 on real
production tables (e.g. `ASORTYM.DBF` 14k rows / 125 cols / 16 tags) and
full reindex workloads.

Reported-by: rkedzioralmaalpinex

## 1.8.2 — 2026-07-08

### CI / release — NTXPL852 test fixture (Clang + Linux/macOS builds)

The v1.8.0 NTXPL852 unit tests used adjacent string literals and greedy
`\x` escapes for the Polish Ł (OEM `0x9D`) rows. Clang on Linux/macOS
treated them as `-Werror,-Wstring-concatenation` / `hex escape sequence
out of range`, breaking **ci** and **release** legs for POSIX platforms
while Windows MSVC passed.

Fix: shared `tests/fixtures/polish_oem_fixture.h` with explicit
`constexpr` byte arrays; `release.yml` skips artifact upload when a build
leg produced no `ASSET`.

No functional engine changes vs v1.8.1 — use v1.8.2 release archives for
Linux/macOS binaries.

## 1.8.1 — 2026-07-08

### ABI — `OrdScope` / `AdsSetScope` string key padding

Harbour `rddads` passes scope strings at trimmed length (`hb_itemGetCLen`),
but CDX index keys are space-padded to `key_length`. An unpadded scope on
a wide character field (e.g. work-order `C(10)` with `setScopeTop` /
`setScopeBottom` on the same `cWrkord`) made `key <= bottom` fail for every
matching row — `GotoTop` landed at EOF on **both local and remote**.

Fix: pad string/RAW scope keys to the active index `key_length` in
`AdsSetScope`, matching `relation_child_key()` and native ACE behaviour.

Test: `QA-D: character ordScope honours unpadded scope on wide key` in
`abi_qa_repro_test.cpp`.

Reported-by: production work-order labour-items filter.

## 1.8.0 — 2026-07-08

### CDX — NTXPL852 / PL852 OEM collation (#127)

Polish CP-852 national collation for CDX index build, soft/hard seek,
insert, and reindex. `AdsSetCollation` now accepts `NTXPL852` and
`PL852` (case-insensitive aliases) in addition to `BINARY` / `NOCASE`.
Ł (0x9D) sorts between L and M, matching Harbour / Clipper / SAP ACE
local-server behaviour on Central-European datasets.

- New engine module `oem_collation` (256-byte PL852 sort table).
- `CdxIndex::compare_keys_()` honours the connection's OEM sort weights.
- `apply_cdx_oem_collation()` stamps collation on create/open/reindex.

### CDX — bulk `REINDEX` path (#128)

`Table::reindex()` for CDX tags now uses `clear_data()` +
`build_bulk()` (same fast bottom-up path as `CREATE INDEX`) instead of
erase-then-per-record insert.

### Tests

19 new unit tests across engine, CDX driver, and ABI layers
(`oem_collation_test`, `cdx_build_bulk_collation_test`,
`abi_ntxpl852_seek_test`, `abi_ntxpl852_collation_abi_test`).

## 1.7.0 — 2026-07-08

### REMOTE — AdsSetScope / OrdScope navigation fix

`OrdScope` / `AdsSetScope` top/bottom key-range bounds were sent to the
server (wire opcode `SetScope` 0x98) but `GotoTop`/`Skip` ignored them:
navigation walked every record in the table instead of the scoped group.

Root cause: scope is stored on the ABI table's active order, while
`GotoTop`/`Skip` only routed through that handle when `ordered_tables_`
was set — which happened on `SetOrder` but never on `SetScope`. Harbour
`OrdSetFocus` + `OrdScope` flows often set scope without a preceding
`SetOrder` wire op (client already tracks `active_index_id` from
auto-opened production CDX).

Fix:
- Server `SetScope` handler: mark the parent table in `ordered_tables_`
  after a successful `AdsSetScope`.
- Client `remote_activate_index`: always sync `SetOrder` to the server
  before index-driven navigation.

Files: `session.cpp`, `remote_index_nav.cpp`.
Tests: `remote AdsSetScope constrains GotoTop/Skip walk` in
`abi_remote_index_nav_test.cpp`; `openads_remote_scope_probe` harness.

## 1.6.5 — 2026-07-07

### REMOTE — OrdKeyCount() fix + AdsGetDate() crash fix

Two critical blockers for Harbour `rddads` + FiveWin `TDataBase` /
`xBrowse` over remote connections:

- **OrdKeyCount() returns 0 on remote aliases (#128)**: `xBrowse`
  grids showed no rows because `OrdKeyCount()` internally called
  `AdsGetKeyCount(hOrdCurrent)` which had no remote code path.
  Added new wire opcode `GetKeyCount` / `GetKeyCountAck`
  (0xB0/0xB1), `RemoteConnection::key_count()` client method,
  `remote_index_key_count()` bridge, and a server handler that routes
  through the ABI's `AdsGetKeyCount` to compute the true filtered key
  count from the active index order (e.g. 100 for a `FOR CODIGO>100`
  tag on a 200-row table). Also added a `get_remote_table()` guard
  so table handles route correctly too.

- **AdsGetDate() crashes on remote Date-type fields (#128)**:
  rddads resolves `FieldGet` on `ADS_DATE` columns to
  `AdsGetDate(hOrdCurrent, ...)`, passing the `RemoteIndex` handle.
  `AdsGetDate` delegated to `AdsGetField` which only checks
  `get_remote_table()` -- never sees the `RemoteIndex` and falls through
  to a null local pointer -> ACCESS_VIOLATION. Fixed by resolving
  `RemoteIndex` -> parent `RemoteTable` via `handle_for_remote_table`
  before calling `AdsGetField`.

Files: `wire.h`, `client.h`, `client.cpp`, `remote_index_nav.h`,
`remote_index_nav.cpp`, `session.cpp`, `ace_exports.cpp`.
Tests: 2 new unit tests in `abi_remote_index_nav_test.cpp`.

# Changelog

All notable changes to OpenADS are recorded here. The project follows
[Semantic Versioning](https://semver.org/) once 1.0 ships; until then
0.x.y releases may break the C ABI between minor versions to track
the real ACE SDK.

## 1.6.4 — 2026-07-05

### REMOTE — FiveWin TDataBase / ADSRDD integration (critical fixes)

Fixes reported while using Harbour `ADSCDX` + FiveWin `TDataBase` over
remote connections (`tcp://` against `openads_serverd`):

- **Date fields (#4)**: `FieldGet` (and `AdsGetJulian` / `AdsGetField`) on
  `ADS_DATE` columns (e.g. `WRKDAT`) no longer ACCESS_VIOLATION crashes.
  All remote value paths now resolve fields via `remote_field_index`
  first (handles the ordinal-as-small-pointer idiom used by X#/FWH/rddads
  safely) before any `to_internal` or name lookup. Server-side ABI
  connections now force `YYYYMMDD` date format so the row cache always
  carries canonical 8-digit strings (matching local engine behaviour).
  `AdsGetLong`/`AdsGetDouble` fallbacks and memo paths also hardened.

- **FieldPut on unlocked record (#6)**: No longer crashes with AV (or
  succeeds silently). `SetField` on the server now obtains the ABI handle
  (via `tbls_h_` or `ensure_abi_handle`) and returns `AE_RECORD_NOT_LOCKED`
  (5035) for writes to existing records that are not locked. The classic
  Clipper "write value back to test lock, catch EG_UNLOCKED" idiom now
  works over REMOTE. `LockRecord`/`LockTable` (and unlock) are forwarded
  to ABI handles for regular remote tables. Engine table is still used
  for the actual mutation to preserve `append_record` + immediate set
  state machine. `pending_append` is explicitly set after server
  `AppendBlank`; post-append sets are allowed without prior lock (standard
  behaviour).

- **Ordinal safety & repeated FieldGet (#5, #1, #2)**: Removed direct
  `to_internal(pucField)` / `reinterpret_cast` on `pucField` in every
  remote `AdsGet*` and `AdsSet*` (String, StringW, Double, Logical, Julian,
  Long, MemoLength, FieldRaw, FileTo/FromField, etc.). All now go through
  `remote_field_index` (which already handled the <0x10000 ordinal case).
  This prevents AVs on first Get after open, at EOF, on Date fields, and
  on the redundant second Get that some FWH helpers perform.

- **Other remote robustness (#3, #7)**: `AdsGetAllLocks` no longer returns
  5000 / crashes on remote table handles (returns count=0 as safe stub;
  full wire impl still pending). `DbInfo(DBI_FULLPATH)` paths remain stable
  (remote name returned). Row cache / blank handling at open/EOF/BOF made
  more consistent so fewer workarounds are required.

- **Server lock & append hygiene**: Lock opcodes now also act on `tbls_h_`
  handles. `AppendBlank` handler explicitly marks `pending_append`.
  `SetField` prefers the engine `Table*` for data writes (correct cursor
  after append) while using ABI only for the lock pre-check.

All changes are covered by existing remote wire unit tests (including
`AdsAppendRecord` + multi-Set including Date columns + navigation).
`openace64.dll` and `openads_serverd` must both be updated for full effect.

Reported by users integrating OpenADS REMOTE mode with FiveWin
`TDataBase`.

## 1.6.3 — 2026-07-04

### REMOTE / FWH — xBrowse index navigation over `tcp://`

Harbour `ADSCDX` + FWH `xBrowse` / `TDataBase` with `OrdSetFocus` and
production CDX tags (e.g. `customer.dbf` / `CUSTNAME` on `openads_serverd`)
had chaotic browse behaviour: scrollbar misaligned, rows shifting on
`Refresh`, and the first row repeating when scrolling up from the top.

- **Bug fix: `AdsGetKeyNum` returned physical `RecNo` instead of logical
  key position** — FWH `xBrowse:SetRDD()` uses `AdsKeyNo(, , 1)` for the
  vertical scrollbar. Remote indexed tables now track `current_keyno` across
  `AdsGotoTop` / `AdsSkip` / `AdsGotoRecord` and implement
  `AdsGetRelKeyPos` / `AdsSetRelKeyPos` against the active order.

- **Bug fix: `GotoRecord` left the server ABI index cursor stale** —
  `xBrowse:Paint()` saves `RecNo()`, walks visible rows via `hOrdCurrent`
  (`AdsSkip` on the index handle), then restores with `DbGoto(bookmark)`.
  The server engine moved to the bookmark recno but the parallel ABI
  handle (used for ordered `Skip`) did not, so the next index skip walked
  from the paint position instead of the selected row. `Session::GotoRecord`
  now calls `AdsGotoRecord(hord, recno)` when the table is in
  `ordered_tables_`.

- **Bug fix: `AdsAtBOF` always answered “not BOF” while `row_valid`** —
  After `Skip(-1)` at key #1, Harbour's `hb_adsUpdateAreaFlags` never saw
  BOF, so FWH `GoUp` rubber-banded and repainted the first row. The client
  now sets `nav_at_bof` / `nav_at_eof` when a skip does not change recno.

- **Client: `remote_index_skip(0)`** — `Skip(0)` settles prefetch lag
  without clearing `prefetch_consumed` first; index-nav preamble no longer
  invalidates `row_valid` before every skip ack.

- **Regression tests** — `remote bookmark restore keeps AdsGetKeyNum
  coherent`, `remote index skip after GotoRecord bookmark restore`,
  `remote index skip(-1) at top sets BOF for xBrowse GoUp`, plus extended
  `abi_remote_index_nav_test` / `abi_remote_prefetch_test` coverage.

Reported while validating FWH `testads.prg` / `TDataBase` + `xBrowse`
against `tcp://192.168.18.184:16262//tmp/openads_mac` (Harbour `rddads`
unchanged).

## 1.6.2 — 2026-07-04

### REMOTE / FWH — production CDX tag names & append-row FieldGet

- **Bug fix: SAP production CDX tag names corrupted** — `CdxIndex::list_tags()`
  and `open_named()` decoded struct-tag compact keys incorrectly for
  ADS-SAP / BCC compound CDX files (e.g. `customer.cdx` on the iMac test
  dataset). Tag 2 surfaced as `AME` instead of `CUSTNAME`; tag 1 as
  `    CUSTNO` instead of `CUSTNO`. OpenADS now reads the canonical tag
  name from each sub-tag CDXTAGHEADER (+24) and forces `dup=0` on the
  first struct-leaf key (Harbour `hb_cdxPageLeafDecode` parity). Fixes
  `AdsGetIndexName` / rddads `OrdName()` / FWH `TDataBase:IndexName()`
  over REMOTE mode.

- **Bug fix: `AdsGetField` at BOF/EOF returned 5000** — FWH
  `TDataBase:td_blankrow()` does `DBGOBOTTOM` + `DBSKIP(1)` then
  `FieldGet` on the append-row position. OpenADS now returns
  `AE_SUCCESS` with a type-default blank field (spaces / `F` for logical)
  instead of `AE_INTERNAL_ERROR`, for local tables and remote
  `GetField` wire ops. Unblocks Harbour `ADSCDX` / xBrowse on OpenADS
  without patching FiveWin.

- **Regression tests** — `abi_no_current_record_test`,
  `engine_navigation_empty_test`, `abi_cdx_tag_order_test` (SAP
  `customer.cdx` fixture), `abi_remote_prodcdx_test` (expects
  `CUSTNO` + `CUSTNAME` when `OPENADS_TEST_REMOTE` is set).

## 1.6.1 — 2026-07-02

### REMOTE mode — production CDX auto-open in subdirectories

- **Bug fix: `ensure_abi_handle()` used basename-only table paths** — When
  `openads_serverd` handled remote `OpenIndex` for a table opened as
  `orders/workorders.dbf`, the lazy ABI handle was reopened as
  `workorders.dbf` at the data root. Production CDX auto-bind then failed
  (`AdsGetNumIndexes` / rddads `OrdCount()` returned 0) even though the
  CDX sat beside the DBF in a subdirectory. The session now stores the
  original `OpenTable` payload and reopens with that relative path; the
  `OpenTableAck` production-bag hint is also sent relative to the data
  root.

- **Regression tests** — `remote production CDX auto-open in subdirectory`
  (embedded server) and `REMOTE: workorders subdirectory auto-binds
  production CDX` (live server, gated on `OPENADS_TEST_REMOTE`).

Reported by FWH users opening large DBF/CDX tables via Harbour `ADSRDD` /
`TDataBase` over `tcp://` against `openads_serverd`.

### Release packaging (Windows)

- Windows x64/x86 archives ship **both** engine DLL names at the ZIP
  root: `openace64.dll` / `openace32.dll` (OpenADS build product) and
  `ace64.dll` / `ace32.dll` (Harbour `rddads` / FWH drop-in), plus
  matching `.lib` import libraries. The release workflow now verifies
  all four files before publishing.

## 1.6.0 — 2026-07-01

### REMOTE mode — FWH production CDX validation & bug fixes

- **Bug fix: `AdsGetNumIndexes` in REMOTE mode** — Previously returned 0
  because it queried the server engine handle (which hadn't opened the
  production index). Now counts `RemoteTable::index_handles.size()`
  locally, matching the production CDX auto-open + explicit AdsOpenIndex
  index count. This unblocks rddads' `DbSetOrder(n)` and
  `OrdBagName()` in REMOTE mode.

- **Bug fix: implicit GoTop after `AdsOpenTable90` in REMOTE mode** —
  After table open + production CDX auto-open, the client had no record
  buffer, causing crashes on `AdsGetField` / `AdsGetRecordCount` etc.
  LOCAL mode leaves the cursor at BOF with a valid buffer; REMOTE left
  the buffer empty. Now sends an implicit `GotoTop` on table open to
  populate the record cache, matching LOCAL semantics. This eliminates
  the need for an explicit `DbGoTop()` after `USE` in FWH.

- **New test: `abi_remote_prodcdx_test.cpp`** — 8 test cases that
  validate the complete FWH rddads workflow over TCP against a
  pre-existing production database (DBF + CDX): OrdBagName,
  AdsGetIndexName, GoTop + FieldGet by ordinal and by name, AdsSetIndexOrderByHandle,
  AdsSetIndexOrder by tag name, ordered full-scan, multi-table simultaneous
  open, and FieldGet on multiple fields after Skip.

- **FieldGet by ordinal idiom documented** — ACE's `ADSFIELD(n)` casts
  a 1-based ordinal to a pointer (`(UNSIGNED8*)(uintptr_t)n`), NOT a
  string `"1"`. Both `remote_field_index()` and `resolve_field_index()`
  detect small pointer values (< 0x10000) as ordinals. Tests updated
  to use the correct idiom.

- **Gated on `OPENADS_TEST_REMOTE`** — set to a server URI (e.g.
  `tcp://192.168.18.184:16262/`) to run against a live remote server.
  Skipped in default CI.

- **DOING.md** added — live working-notes file tracking in-progress
  investigation, test results, and pending fixes.

## 1.5.2 — 2026-06-27

### Release packaging

- **Windows x86 (32-bit) archive restored** — v1.5.1 shipped only
  `openads-*-windows-x64.zip` because the x86 MSVC leg failed at build
  time (duplicate `ENTRYPOINT` declarations in `http_server.cpp` /
  `mgprobe`, fixed on main). The release workflow now verifies both
  `windows-x64` and `windows-x86` ZIPs before publishing; the x86 ZIP
  bundles prebuilt `lib/msvc/ace32.lib` (stdcall) plus
  `openads_ace_x86.def` for Harbour `rddads`.

### CI

- **Harbour smoke** — green on GitHub Actions (`contrib/rddads` bootstrap,
  fresh `openace64.lib` link).

## 1.5.1 — 2026-06-27

### Security & remote hardening

- **Path jail on remote Connect** — client paths are canonicalized and
  confined under `openads_serverd --data`; traversal attempts are rejected.
- **LockMgr nested unlock** — OS byte locks remain held until the final
  nested `unlock_*` releases them.
- **Remote field writes** — `AdsGetMemoDataType`, `AdsSetStringW`,
  `AdsSetJulian`, and `AdsSetFieldRaw` route through `tcp://`.
- **TLS** — peer certificate verification on by default;
  `OPENADS_TLS_INSECURE=1` for dev/self-signed endpoints.

### CI & Harbour smoke

- **`harbour-smoke` job** in GitHub Actions (Windows).
- **`tools/scripts/run_harbour_smoke.ps1`** and
  **`bootstrap_harbour_ci.ps1`** — portable Harbour bootstrap for CI.

### Remote ABI — Fase 2 closed

- **`AdsSetRelation` / `AdsSetScopedRelation`** — parent→child relations on
  local and `tcp://` tables; `apply_relations_for_handle()` after navigation.
- **`AdsSetRecord` / `AdsGetRecord`** — wire opcodes `0xA8`–`0xAB`.
- **`AdsCustomizeAOF`** — wire opcodes `0xAC`/`0xAD`.
- **`AdsAggregate` / `AdsFetchWhere`** — local in-process DBF tables (SQL
  backends via existing aggregate path).

### SQL backends — navigational write (Plus)

- **SQLite write** — `AdsAppendRecord`, `AdsSetString`, `AdsWriteRecord`,
  `AdsDeleteRecord` with rowid-keyed DML and parameterized binds
  (`abi_plus_sqlite_write_test.cpp`, in-process).
- **MSSQL native (TDS) write** — same ABI surface; PK discovery via
  `INFORMATION_SCHEMA`, staging buffer, `SELECT *` refetch after DML
  (`abi_plus_mssql_write_test.cpp`, gated on `OPENADS_TEST_MSSQL_CONNSTR`).
- **MSSQL read fix** — `ADS_STRING` fields padded to declared width in
  `mssql_get_field` (NVARCHAR live read test).

### Engine & fixtures

- **ADT/ADI fixtures** in `tests/fixtures/adi/` + generator
  `generate_adi_fixtures`; smoke tests no longer skip for missing files.
- **VFP header `0x32`** — autoinc and nullable columns together;
  `_NullFlags` synthetic column when the NULL bitmap is present.

### SQL Tier-1 wiring

- **`BackendTxManager` hooks** — `AdsBeginTransaction` / commit / rollback /
  `AdsSetAutoCommit` on SQLite and PostgreSQL; DML auto-commit after write.
- **Tier-1 utilities in execution** — field optimizer and where builder
  drive actual SQL generation on SQLite reads.

### Fixes

- **AOF V2** — `Like` / `IsNull` ops handled in `aof_eval` (clang `-Wswitch`).
- **Harbour CI bootstrap** — track `harbour/core` master.
- **Concurrent SQLite test** — tolerate minor `SQLITE_BUSY` under contention.

## 1.5.0 — 2026-06-27

### SQL Backend Tier-1 Improvements (SQLRDD Patterns)

- **`BackendTxManager`: nested transactions + auto-commit.**
  Shared transaction manager embedded in every SQL backend connection.
  Supports nested BEGIN/COMMIT with SAVEPOINT emulation, auto-commit
  after N DML statements (configurable via connection string), and
  dirty-flag tracking. SQLRDD reference: `SR_CONNECTION:nTransacCount`,
  `nAutoCommit`, `nIteractions`.
- **`BackendFieldOptimizer`: lazy column loading with learning.**
  Tracks which columns are actually read per table. After
  `LEARNING_THRESHOLD` (5) unique single-column fetches, switches to
  `SELECT *` to avoid repeated demand-fetches. Integrated into
  `SqliteTable` and `PostgresTable`. SQLRDD reference:
  `SR_WORKAREA:sqlGetValue`, `FIELD_LIST_*`.
- **`BackendWhereBuilder`: restrictor composition.** Combines For
  clause, user filter, scope bounds, index restrictions, AOF
  predicates, and recno filters into a single AND-ed WHERE clause.
  Handles exact seek (lower == upper collapses to `=`) and range
  seek. SQLRDD reference: `SR_WORKAREA:SolveRestrictors`.
- **`BackendTableOps` vtable: transaction ops.** New `begin_tx`,
  `commit_tx`, `rollback_tx`, `set_auto_commit` function pointers
  in the backend vtable. SQLite and PostgreSQL adapters registered.

### SQL Push-Down Expansion

- **50+ new translatable functions.** The `try_emit_sql_where()`
  emitter now handles STR, VAL, DTOS, DTOC, CTOD, ROUND, CEILING,
  CEIL, MOD, EXP, LOG, LOG10, SQRT, SIGN, PADR, PADL, PADC, STRTRAN,
  LEFT, RIGHT, AT, ATNUM, DATEADD, DATEDIFF, IIF, IF, NIL, ISNULL,
  ISBLANK, EMPTY, LEN, YEAR, MONTH, DAY, HOUR, MINUTE, SECOND, DOW,
  CDOW, CMONTH, NOW, and more. Unsupported functions (RECNO, DELETED,
  REPLICATE, SPACE, STUFF, OCCURS) decline cleanly.
- **`$` contains: field-to-field support.** `field1 $ field2` now
  emits `field2 LIKE '%' || field1 || '%'` (or CONCAT variant).
  Literals with LIKE wildcards (% _ \) still decline to avoid
  semantic mismatch.
- **`SqlDialect` expansion.** New fields: `length_fn` (LEN →
  LENGTH/CHAR_LENGTH), `now_fn` (DATE() → NOW()/CURRENT_DATE),
  `true_literal` / `false_literal` for .T./.F. rendering.

### UNION / UNION ALL Parser

- **`UNION [ALL]` SELECT support.** The SQL parser now handles
  `SELECT ... UNION [ALL] SELECT ...` with any nesting depth.
  Parsed via `SelectStmt::UnionMember` list; each member carries
  its own FROM, WHERE, ORDER BY, LIMIT, and aliases. Full
  round-trip through ADS query execution.

### ALTER TABLE / DROP TABLE / DROP INDEX

- **DDL statement parsing.** New `AlterTableStmt`, `DropTableStmt`,
  `DropIndexStmt` structs with full parser support. Identifiers,
  quoted names, and IF EXISTS clauses are all handled. Ready for
  backend execution hooks.

### AOF Expression Expansion

- **LIKE operator.** `NAME LIKE 'A%'` now parses and round-trips
  in the AOF expression layer with full `%` and `_` wildcard
  support.
- **IS NULL / IS NOT NULL.** Unary null-test operators added to
  the AOF filter expression grammar.

## 1.4.0 — 2026-06-26

### ADS Dialect Compatibility (ERP Harbour/FiveWin)

- **N-way comma join (3+ tables).** `FROM a, b, c, d, e` now
  parses and executes with an arbitrary number of tables (was limited
  to exactly 2). Left-deep execution plan with hash-join on composite
  keys. Filter pushdown pushes WHERE residuals to the deepest join
  level. Pinned by `sql_parser_test` and `abi_cdx_conditional_index_test`.
- **`<alias>.*` wildcard projection.** `SELECT line.*` expands to
  all columns of the aliased table, matching ADS behaviour.
- **`UPPER(col)` scalar function in WHERE.** Parsed and mapped to
  a case-insensitive comparison, so `WHERE UPPER(name) = 'SMITH'`
  now works end-to-end.
- **`FROM t AS a` table alias on the base table.** Previously only
  consumed for derived tables; now accepted on plain table names.
- **Brackets `[file.dat]` for free-table names in FROM.** The
  `read_identifier_or_filename()` parser now handles `[...]` syntax,
  matching ADS canonical free-table references.
- **`WHERE 1 = 1` constant folding.** Always-true predicates are
  folded at parse time, eliminating unnecessary runtime evaluation.
- **ODBC temporal literals.** `{d 'YYYY-MM-DD'}`, `{ts ...}`,
  `{t ...}` are now parsed and accepted in SQL.

### CDX Index Engine

- **Bulk-load index builder (`build_bulk`).** New bottom-up
  B+tree construction path for `CREATE INDEX` — approximately 10×
  faster than record-by-record `insert()` on large tables. The
  builder sorts keys in-memory and emits a complete B+tree in a
  single pass.
- **O(1) browse position cache.** `ordered_recnos_cached()` and
  `pos_of_recno_cached()` on `CdxIndex` cache the key↔recno
  mapping so `AdsGetRelKeyPos` / `AdsGetKeyNum` answer from an
  in-memory vector instead of walking the index.
- **CDX conditional (FOR) index predicates — persist + apply.**
  `CREATE INDEX ... FOR <condition>` now persists the condition in
  the CDX sub-tag header and applies it at insert time — only
  records satisfying the FOR clause get indexed. Full round-trip
  through reopen. Pinned by `abi_cdx_conditional_index_test`.
- **CDX flush-skip for read-only.** Opening and closing a CDX
  file no longer triggers a flush when no page is dirty.
- **CDX FOR-clause hardening.** Fails loud instead of silently
  dropping or truncating unparseable FOR clauses.
- **NTX empty-but-rooted leaf on PACK/reindex.** Fixes error 5004
  when reindexing an NTX that had empty leaves left by prior
  `erase()` calls. Pinned by `abi_ntx_pack_reindex_test`.
- **Composite CDX key width not pinned to the 254-byte probe.**
  Follow-up to the v1.2.3 character-key fix (PR #68): a composite
  key expression no longer derives its on-disk width from the 254-byte
  evaluation probe — it uses the actual key width, so composite tags
  stay the right size and interoperate with native readers.

### Wire Protocol

- **Server-side filtered scan (`FetchWhere`).** New `FetchWhere`
  opcode (`0xA4`) lets the client send a Clipper-style FOR predicate
  and receive only matching rows — reducing round-trips and bandwidth
  for non-AOF predicates. Evaluated with the same engine evaluator
  used for CDX FOR index conditions. Documented in
  `docs/wire-protocol.md` §5.22.

### Enterprise Server

- **Sharded-reactor connection pool (`WorkerPool`).** New
  `WorkerPool` class multiplexes many client connections over a
  fixed pool of worker threads (default OFF via
  `OPENADS_SERVER_POOL=ON`). Includes `FrameReader` for
  non-blocking partial-frame buffering and `Session` class extracted
  from `server.cpp`. Stress harnesses: `tools/stress/remote_random_main.cpp`
  and `tools/stress/remote_concurrency_main.cpp`.
- **`EnterpriseConfig` singleton.** Environment-driven tunables:
  `OPENADS_SERVER_POOL` (enable pool), `OPENADS_SERVER_POOL_WORKERS`
  (thread count), `OPENADS_SERVER_MAX_SESSIONS` (connection cap),
  pool toggles for ODBC/SQLite/OLEDB backends.
- **Session reaping + max-sessions cap.** Abandoned connections are
  reaped after a timeout; a hard cap prevents thread exhaustion
  under load. Deadlock-free `stop()` lifecycle.

### SQL Backend Improvements

- **PostgreSQL column metadata via information schema.**
  `AdsDDGetFieldProperty` for PostgreSQL tables now exposes
  `IS_NULLABLE` and `COLUMN_DEFAULT` via `information_schema.columns`.
- **SQL concurrency safety — stmt_map serialisation.** Concurrent
  SQL statement execution no longer corrupts the internal statement
  map; access is serialised.
- **SQLite busy-timeout + WAL mode.** Contended SQLite writes no
  longer fail with `SQLITE_BUSY`; a busy-timeout and WAL journal
  mode are enabled at connection time.
- **SQL CREATE TABLE honours statement table type.** `CREATE TABLE`
  and `CREATE TABLE ... AS` now respect the type specified in the
  statement (e.g. `ADS_ADT`).

### ADT

- **ADT companion stream count.** `AdsCreateTable(ADS_ADT)` now
  writes the correct ADT header companion-type count instead of a
  flat 1.

### ABI

- **Connection / handle introspection.** `AdsGetConnectionType`
  reports `ADS_REMOTE_SERVER` for a remote handle (local otherwise);
  `AdsGetHandleType` dispatches on the registry handle kind
  (connection / table across all backends / statement);
  `AdsGetIndexCondition` / `AdsGetIndexFilename` return real values
  instead of empty stubs.

### Build

- **Strict-warning (`-Werror`) cleanups in `data_dict.cpp`.**
  Explicit casts in `le16()` and the `\uXXXX` escape loop, and removal
  of two dead static helpers (`trim`, `split_tabs`), so the data
  dictionary compiles clean under clang/gcc `-Wconversion`/
  `-Wsign-conversion` and MSVC `/WX` (C4505).

### Tests & Tooling

- **xBase++ smoke test.** New `tests/xpp/` directory with a
  raw-ACE smoke test via `DllPrepareCall`, plus translations (ES,
  PT). Runner: `tests/xpp/run.sh`.
- **FiveWin ORM cookbook.** New `cookbook/orm/fivewin/` with a
  `grid_orm.prg` example, FiveWin build script, and README.
- **CDX empty-table key-width edge test.** Verifies correct key
  width for composite expressions on an empty table.
- **Concurrent SQL + SQLite contention tests.**
  `abi_sql_stmt_concurrency_test` and `sqlite_concurrency_test`
  validate thread-safety under contention.

## 1.3.0 — 2026-06-25

- **CDX index direction fix for Harbour rddads (FiveWin).** `AdsCreateIndex61`
  decoded `descending = ulOptions & ADS_DESCENDING (0x08)`. Instrumenting the
  two RDD clients showed they put the compound/descending option bits on
  **swapped** positions: X#'s ADSRDD sends `0x02` for an ascending tag and
  `0x0A` for descending, while Harbour's `rddads` sends `0x08` for ascending
  and `0x0A` for descending. So a plain Harbour `INDEX ON f TAG t` (`0x08`)
  was read as descending and **every** Harbour/FiveWin index was built
  reversed — `AdsGotoTop` landed on the last key and `Skip` walked backward,
  so a `TBrowse`/`tDatabase` grid showed its rows upside-down (`Seek` still
  worked, which masked it). Direction is now decoded as descending only when
  **both** `0x02` and `0x08` are set (`0x0A`); a lone `0x02` or `0x08` is that
  client's compound marker and is ascending. The SQL `CREATE INDEX` path emits
  `0x0A` for a descending tag so it round-trips through the same decode. Pinned
  by `abi_cdx_index_direction_test` and `examples/fivewin/tdata_index_test.prg`.
- **Build fix: drop a dead `trim()` in `data_dict.cpp`.** An unreferenced
  static function tripped `-WX` C4505 on a clean MSVC build.

## 1.2.3 — 2026-06-25

- **CDX character index key width fix (PR #68).** `AdsCreateIndex61`
  derived a character tag's fixed key width from the **trimmed** value
  of the first record. When the first row was short (e.g. `"ANA"`) and
  later rows shared a longer prefix (`"ANABELA CARDOSO"`,
  `"ANABELA FERREIRA"`), every later key was truncated to the first
  row's width and collapsed onto the same stored key, so distinct
  values became indistinguishable and a seek landed on the wrong
  record — both inside the index and for native FoxPro/Clipper readers
  of the bag. The key width now comes from the declared field length
  for a bare character field, falling back to the **untrimmed**
  first-record width for a composite expression, keeping the 32-char
  default only for an empty table. Numeric CDX/NTX key widths are
  unchanged. Pinned by `abi_cdx_char_keylen_test`.
- **Build fix: `<cstdint>` in `sqlite_uri_test`.** `std::uint8_t` was
  used without including `<cstdint>`; clang/libc++ does not pull it in
  transitively, so the `ninja-clang` `-Werror` CI job failed while MSVC
  and AppleClang stayed green. Added the explicit include.
- Full unit suite **739/739**, 0 regression (was 738).

## 1.2.2 — 2026-06-24

- **CDX empty-leaf walk fix (PR #63).** Forward and backward
  index walks now skip empty leaves left behind by `erase()`.
  Previously, `seek_first`/`seek_key`/`next` stopped at the first
  empty hole and `prev` followed the left-sibling pointer into it,
  reporting end-of-index while live keys remained in later (or
  earlier) leaves. A shared `skip_empty_leaves_right_` /
  `skip_empty_leaves_left_` helper advances over holes. Fixes
  REINDEX / bulk-delete `ADSCDX/5000` (record number out of range).
- **CDX leaf recno bits + prefix seek (PR #62).** Two correctness
  fixes: (1) `compute_layout` now sizes the record-number field
  from `max_rec` (not just key length), so tags with wide keys
  (≥40 bytes) no longer silently truncate recnos ≥ 4096; (2)
  `seek_key` compares only the search-key length, so a partial
  (prefix) seek like `SEEK "ART-00024800"` matches a stored
  `"ART-00024800 desc ..."` key. A guard refuses encode when
  `max_rec > rec_mask`, failing loudly at write time.
- **MSSQL backward SKIP off-by-one (PR #65).** `MssqlTable::skip`
  used `abs_n >= pos` for the backward branch, so a SKIP landing
  exactly on row 0 reported BOF. Changed to `>` so `abs_n == pos`
  reaches index 0 (a valid row).
- **ABI typed getters + AdsGetIndexHandle for SQL backends
  (PR #66).** `AdsGetDouble`/`AdsGetLong`/`AdsGetLongLong`/`
  AdsGetString` now dispatch through the per-backend ops vtable,
  so PostgreSQL (and other SQL backends) return real values instead
  of error 5000. `AdsGetIndexHandle` resolves by-name for PG
  tables so indexed seek works end-to-end.
- **NTX numeric key edge-case tests.** `ntx_numeric_key()` pure
  function now tested for -0.0 normalisation, width/dec clamping,
  negative byte-complement, and large-value truncation. Custom-key
  add/delete on numeric NTX index covered.
- **CDX empty-tree + prefix-seek edge tests.** Empty tree
  (seek_first/seek_last/seek_key return AfterEnd), all-erased tree
  (forward walk crosses empty leaves), exact-length prefix match,
  and descending prefix seek.
- Full unit suite **738/738**, 0 regression (was 726).

## 1.2.1 — 2026-06-24

- **NTX numeric key format fix (PR #67).** Numeric fields indexed
  into an NTX bag now store keys in the native DBFNTX form
  (zero-padded magnitude + complemented negatives) instead of
  space-padded `STR()` text at a probed width. A native xBase
  reader's `dbSeek(<number>)` now matches the on-disk key for
  positive, decimal, and negative values. Reopened index bags retain
  the numeric encoding. `abi_ntx_numeric_key_test` asserts the
  native byte layout; full unit suite 720/720, 0 regression.
- Added unit tests: adm_memo, codepage, maria_uri, postgres_uri,
  proc, sqlite_uri (710 new lines, 706/706 tests pass).
- Remote benchmark docs: iMac WiFi (784K rec/s) and charleskwon.com
  SSH tunnel (676K rec/s) with 500K-record results.
- Removed IMAC_CONNECTION.md from tracking (contains credentials).
- ORM examples synced to v1.1.0-alpha.

## 1.2.0 — 2026-06-24

- **Deferred-flush bulk-insert mode (528× speedup).** A new
  `AdsSetDeferredFlush(hTable, 1)` API puts the table into
  deferred-flush mode: `AdsWriteRecord` writes the record to OS
  cache but skips the per-record `FlushFileBuffers` call. Data is
  flushed to physical media only when `AdsFlushFileBuffers` is
  called explicitly (or on table close). 500K records + CDX index
  build completes in ~26 seconds (19s bulk insert at 26,381 rec/s +
  7.2s CDX build + 36ms final flush) vs. ~2.7 hours before
  (50 rec/s). Remote benchmark (Windows client → iMac server over
  WiFi tcp://): 500K records in 0.69s at 784K rec/s — 36× faster
  than local mode. 649/649 unit tests pass; backward-compatible —
  default behaviour is unchanged (flush on every write).

- **MSSQL native TDS 7.4 backend (PR #53 integration).** Native
  SQL Server connectivity via the TDS 7.4 wire protocol with
  optional mbedTLS encryption. Supports connect, authentication
  (SQL/Windows), table open, field read, and navigation. URI
  scheme: `mssql://user:pass@host:port/database`. Enabled via
  `OPENADS_WITH_MSSQL=ON` CMake option (requires
  `OPENADS_WITH_TLS=ON`). 649/649 unit tests pass.

## 1.1.0 — 2026-06-23

- **SQL backends: PostgreSQL / MariaDB / ODBC behind a pluggable
  backend-ops registry (PR #31).** OpenADS can now open tables on
  PostgreSQL, MariaDB / MySQL and any ODBC-reachable engine behind
  the ACE ABI, selected by the connection URI (`postgresql://` /
  `mariadb://` / `odbc://`) exactly like the SQLite backend.
  Navigation, field read and column SEEK work; write is
  per-backend. The four SQL backends register one `BackendTableOps`
  struct each (17 function pointers), so the ~17 ABI navigation /
  field functions stay backend-agnostic instead of multiplying a
  per-backend `if` block — adding a further backend is one ops
  struct plus one registration line. Identifiers are validated to
  safe ASCII and SEEK values use prepared-statement parameters. The
  native local DBF / ADT and `tcp://` remote paths are unchanged
  fall-throughs. See `docs/OPENADS_PLUS.md`. Verified: full unit
  suite 572/572; PostgreSQL/MariaDB/ODBC e2e (41/45/59 assertions)
  against live servers on the contributor's side.

## 1.0.4 — 2026-06-23

- **CDX stale record-count refresh on the fetch path (PR #50).** A
  `CdxDriver` caches the DBF record count at `open()`. In a
  multiuser deployment a peer connection can append rows afterward,
  leaving that cache lagging; an index walk that reached a
  just-appended recno (e.g. mid-`REPLACE … FOR` / DBEVAL) then
  failed hard with a spurious ADSCDX error 5000. `read_record_raw` /
  `write_record_raw` now re-read the on-disk count under a shared
  header lock before declaring a recno out of range, with an
  unlocked-refresh fallback. Slow path only — a normal forward scan
  never reads past the count, so the single-writer case pays
  nothing.

## 1.0.3 — 2026-06-23

- **Round-trip-thrifty remote scan (PR #47).** A forward scan over
  the `tcp://` wire no longer costs ~one TCP round-trip per record.
  A sequential-prefetch path — negotiated via a Connect capability
  flag — piggybacks a lookahead block onto forward-`Skip` acks; the
  client serves them locally and folds the consumed count back into
  the next wire step so the server cursor never desyncs. `AdsAtEOF` /
  `AdsAtBOF` are answered from the cached current row and `AdsIsFound`
  from a cached `Found()` flag. A 50k-record loopback scan is ~3.9×
  faster (NAV-only) / ~3.3× (3-field read), `IsFound` round-trips
  drop to zero. Additive and backward-compatible: clients that don't
  advertise the capability keep the previous wire behaviour.
- **Cookbook expansion (PR #46).** New `console/` examples (SQL via
  `AdsExecuteSQLDirect`, native ADT with `ADSADT` + `.adi`, a
  `tcp://` remote client), a FiveWin `xbrowse` CRUD sample, and an
  all-back-ends ORM benchmark (`orm/complete/`) with a cross-back-end
  content checksum and a seek-vs-scan headline.

## 1.0.2 — 2026-06-23

- **Responsive Studio web console.** The Studio SPA
  (`tools/serverd/spa_index.h`) now adapts to phones and tablets:
  the table-list sidebar collapses into a slide-in drawer (☰ in the
  header, dimmed backdrop, auto-close on select) below ~768 px;
  tabs scroll horizontally; on phones forms stack to one column,
  modals fit the viewport width, and touch targets are enlarged.
  Also fixes a pre-existing dark-theme bug where `--panel` /
  `--panel-2` / `--border` were self-referential CSS variables, so
  panels and borders rendered transparent.

## 1.0.1 — 2026-06-23

- **`SKIP` honours `SET DELETED ON` in natural order.** `Table::skip`
  on an unindexed table stepped straight onto deleted rows; it now
  skips deleted records (matching the index-order path and
  `GOTO TOP` / `GOTO BOTTOM`) when `SET DELETED` is ON. Fixes
  `abi_deleted_records_test` ("middle records deleted: Skip sees only
  live rows"), which had been failing the test step on every CI
  platform.
- **Native ADT / ADI create, read, write, and index seek (PR #41).**
  OpenADS now operates end-to-end on native `.adt` / `.adi` / `.adm`
  files: `AdsCreateTable(ADS_ADT)` writes a valid header + field
  descriptors (+ optional `.adm` memo store), `AdsAppendRecord` /
  `AdsWriteRecord` persist rows and memo payloads, re-open + field get
  + memo round-trip on read, `AdsCreateIndex61` builds `.adi` bags, and
  `AdsSeek` works on character and numeric ADI keys. AUTOINC counter is
  seeded from existing rows at open.
- **POSIX platform hardening.** `file_posix` stores handles as `(fd+1)`
  so a real fd 0 (stdin closed) is not mistaken for the not-open
  sentinel; `pread` / `pwrite` retry on `EINTR`; `map_readonly` rejects
  zero-length maps; `LockMgr` refcounts repeated locks and releases the
  OS lock only on the final unlock; `TxLog::read_all` bounds-checks
  every UPDATE / APPEND field length against truncated / corrupt WAL.
- **macOS / clang build fixes.** Resolved `-Werror` breaks introduced by
  the ADT/ADI work: sign-conversion in `adi_index.cpp` and the
  `environ` / dangling-pointer issues in the ADT scope-validation test.
- **Documentation.** New SQLite backend guide (`sqlite://` connection
  URI, `?key=` encryption, field-type mapping, limitations) and stored
  procedures guide (custom AEP `CREATE`/`EXECUTE PROCEDURE` + the
  built-in `sp_*` Data Dictionary procedures), all in EN / ES / PT.
- **Cookbook (PR #44).** New `cookbook/` folder with runnable,
  heavily-commented Harbour examples — a `console/` track (pure
  `ADSCDX` xBase) and an `orm/` track (CRUD across SQLite / DBF /
  PostgreSQL / MariaDB / ODBC back-ends), plus connection-string,
  field-type and troubleshooting guides.

## 1.0.0-rc29 — 2026-05-26

- **Turnkey `hbmk2` (`.hbp`) example for Harbour apps —
  `examples/harbour-hbmk2/`.** Reported on the FiveTech forum:
  *"alguna alma caritativa que proporcione un archivo de
  compilación `.hbp` para crear un programa con OpenADS — todos
  mis intentos han fracasado"*. The repo now ships a complete
  `hbmk2` project: `openads_demo.hbp` (x64), `openads_demo_x86.hbp`
  (32-bit), `openads_demo.prg` (console app exercising
  `AdsConnect` → `DbCreate` → `INDEX ON UPPER(NAME)` → `dbSeek`),
  Windows `build.cmd` and POSIX `build.sh` wrappers. Drop in your
  `.prg`, point `OPENADS_LIB` at OpenADS' build output, run
  `hbmk2`. The `.hbp` is intentionally minimal — only the two
  link entries that change for OpenADS (`-lrddads` plus
  `-L${OPENADS_LIB} -lace64`).
- **Docs walkthrough — en / es / pt.** New "Build your own
  Harbour app against OpenADS (`hbmk2` / `.hbp`)" section in
  `docs/{en,es,pt}/getting-started.md` and the matching README,
  including a troubleshooting table for the typical *"unresolved
  external symbol `AdsConnect60`"* / *"`rddads.lib` not found"* /
  *"loaded the wrong `ace64.dll`"* pitfalls so a first-time user
  can self-diagnose without filing an issue.

## 1.0.0-rc28 — 2026-05-22

- **ADT / ADM support (M4 ADT).** OpenADS now opens `.adt` tables
  produced by SAP Advantage and writes records back; `.adm` memo
  stores auto-attach when the table carries Memo / Binary fields.
  Full 13-type field vocabulary (CHAR, CICHAR, LOGICAL, DATE,
  DOUBLE, INTEGER, SHORTINT, MEMO, BINARY, TIME, TIMESTAMP,
  AUTOINC, MONEY). ADM uses 256-byte fixed blocks; the 9-byte
  in-record reference is resolved transparently by the engine.
  `AdsCreateTable(ADS_ADT)` still produces a DBF (ADT creation
  deferred); ADI index files not yet implemented; SAP proprietary
  ADT encryption not yet supported. Verified against
  `f:\pmsys\data\landlords.adt` via `tests/unit/abi_adt_smoke_test.cpp`
  (skipped on machines without the fixture).
- **CI** — macOS leg switched to a single universal (arm64 +
  x86_64) binary instead of separate Intel / Apple-Silicon legs.
- **Harbour patch** — restored the blank context line in the
  `rddads.h` hunk of `tools/harbour_patch/rddads-compat.patch`
  so `git apply` succeeds on a pristine Harbour tree.

## 1.0.0-rc27 — 2026-05-17

- **`AdsGetField` pads CHARACTER fields to the declared width.**
  Reported by Pritpal Bedi: a Harbour `mini_xbrowse /ads` (ADSCDX
  → OpenADS) showed every text column truncated — `Charlie` as
  `Charl`, `Barcelona` as `Barcel` — while the native DBFCDX run
  rendered them full. Root cause: `AdsGetField` returned CHARACTER
  values with trailing spaces stripped (`make_string` in
  `dbf_common.cpp` rtrims, and `decode_field` used it for the
  Character branch). DBF/xbase CHAR fields are fixed-width
  space-padded; `FieldGet` of a `C(20)` field must return 20
  characters. With the trimmed value, xbrowse auto-sized each
  column to the current row's value length and clipped every
  other row. `AdsGetField` now re-pads CHARACTER values to the
  field's declared width on the way out — both the local and the
  remote (wire) read paths. The engine's internal decode is left
  trimming, so SQL comparisons, index keys and AOF filters are
  untouched; verified by `tests/smoke/harbour/fieldlenprobe.prg`
  (ADSCDX now matches the DBFCDX baseline) and `idxprobe.prg`
  (index walk still `SORTED=YES`, full suite 397/397).
- **`tools/harbour_patch/rddads-compat.patch` applies again.**
  Reported by Pritpal Bedi: `git apply` rejected the patch with
  `patch failed: contrib/rddads/rddads.h:67`. A prior edit had
  dropped the blank context line after `#include "ace.h"`, so the
  hunk carried five context lines while its header still declared
  six. The missing line is restored; verified `git apply` applies
  cleanly to a pristine Harbour `contrib/rddads` tree.
- **PHP binding — result-fetch and index seek.** `bindings/php`
  gains `Cursor::fetchAssoc()` / `fetchNum()` (single-row fetch,
  `null` past the last row) and `Table::seek()` (index key seek
  via `AdsGetIndexHandle` + `AdsSeek`). 37 PHPUnit tests.
- **CI** — the release workflow gains a macOS Intel (x64) build
  leg, so releases ship a `macos-x64` archive alongside
  `macos-arm64`.

## 1.0.0-rc26 — 2026-05-16

- **PHP binding — `bindings/php`.** Reinaldo Crespo asked whether a
  modern PHP extension for ACE existed: the proprietary Advantage
  PHP extension stopped working around PHP 5.2 and was never
  modernised. OpenADS now ships its own.

  - **`openads/openads-php`** — a pure-PHP Composer package, no
    compiled C. It loads `ace64.dll` / `ace32.dll` / `libace*.so`
    through PHP's `ext-ffi` and wraps it in a modern namespaced
    OOP API: `Connection`, `Statement`, `Cursor` (a `\Iterator`
    over result sets), `Table`, `Record`. Requires PHP 8.1+.
  - **Local and remote in one path.** A `Connection` takes a
    local data-directory path or a `tcp://` / `tls://` URI;
    `AdsConnect60` dispatches on the URI, so the binding has no
    mode branching.
  - **Parameterised SQL.** `Statement::query()` accepts `?`
    positional or `:name` named parameters. OpenADS ACE has no
    host-variable binding, so `ParameterBinder` substitutes
    values client-side with per-type quoting (single-pass, so a
    value containing a `:token` substring cannot corrupt the
    statement) — the anti-injection boundary, with its own unit
    tests.
  - Pinned by 31 PHPUnit tests (21 unit + 10 integration against
    a live engine) plus a CI leg that builds the ACE library and
    runs the suite. Design / plan under
    `docs/superpowers/{specs,plans}/2026-05-16-php-bindings*`.
- **SQL `''` string-escape fix.** `read_string_literal` in the SQL
  parser scanned to the next `'` with no escape handling, so the
  ANSI-standard doubled-quote escape (`'O''Brien'`) parsed as the
  string `O` followed by a stray token — error 7200. Any SQL
  client inserting a string containing an apostrophe failed. The
  parser now decodes `''` to a single `'`; the unterminated-literal
  error path is unchanged. Pinned by a new `sql_parser_test` case.

## 1.0.0-rc25 — 2026-05-16

- **Index correctness sweep.** Three bugs that broke CDX/NTX
  ordered access from Harbour `rddads` and X#'s `ADSRDD`:

  - **`AdsCreateIndex61` decoded the wrong option bit.** `ace.h`
    sets `ADS_UNIQUE 0x01`, `ADS_DESCENDING 0x02`, `ADS_CUSTOM
    0x04`, `ADS_COMPOUND 0x08`. The descending flag was read as
    `ulOptions & 0x08` — that bit is `ADS_COMPOUND`, which both
    `rddads` and `ADSRDD` set for every CDX/NTX tag. Every order
    built descending: `AdsGotoTop` landed on the last key and
    `SKIP` walked backward. A stale comment and the
    `abi_create_index61` test had the bit values swapped the same
    way, so the bug was self-consistent and hidden. Now decoded
    with the named `ace.h` constants; `AdsCreateIndex90` delegates
    to 61 and is covered too.
  - **`ALIAS->FIELD` qualifiers in index expressions.** Harbour
    `INDEX ON CUST->NAME` passes the literal text `"CUST->NAME"`
    to the RDD. `evaluate_index_expr` could not parse it — the
    tokenizer dropped `-` and `>`, so the alias parsed as an
    unknown identifier and every key evaluated blank, degenerating
    the index to record order. `strip_alias_qualifiers()` now
    removes any `<ident>->` qualifier (bare and nested, e.g.
    `UPPER(CUST->NAME)`) before evaluation.
  - **`AE_NO_CURRENT_RECORD` (5026) for not-positioned reads.**
    Reported by Pritpal Bedi: a Harbour `TBrowse` over an `ADSCDX`
    table failed mid-paint with `ADSCDX/5000 table not positioned
    on a record`. `Table::read_field` returned the generic 5000
    (`AE_INTERNAL_ERROR`) for not-positioned reads; `rddads`
    special-cases 5026 as the graceful read-past-EOF path and
    raises every other code as a hard error. `table.cpp` now
    returns the SAP-canonical 5026.

  Verified: `idxprobe.prg` index walk matches the DBFCDX baseline,
  `posprobe.prg` goes from 6 `ADSCDX/5000` raises to 0, full suite
  395/395.

## 1.0.0-rc24 — 2026-05-16

- **`AdsMg*` server-telemetry subsystem.** Reported by Pritpal Bedi
  after running Harbour's `contrib/rddads/tests/manage.prg` against
  OpenADS: every management figure printed `0` — uptime, connections,
  work areas, comm packets, worker threads, memory. The ~17 `AdsMg*`
  functions were placeholder stubs (zero-fill the caller's struct,
  return `AE_SUCCESS`). They now report real telemetry.

  - **`MgCollector`** (`src/mgmt/`) — single source of truth. Formats
    the SAP-canonical `ADS_MGMT_*` structs from a `MgSnapshot`. Runs
    identically for local-mode calls and for the server answering a
    remote request, so the two paths cannot diverge.
  - **`MgSnapshot`** carries everything across the wire: live counts
    (connections / work areas / tables / users / worker threads),
    per-entity lists, process RSS, listener port, and the cumulative
    `MgStats` values (uptime, comm packet totals, server-initiated
    disconnects, high-water marks).
  - **Transport.** New `MgConnect` / `MgRequest` wire opcodes
    (`0xA0..0xA3`). `AdsMgConnect` to a `host:port` validates
    reachability with an eager `MgConnect` handshake; a drive path
    such as rddads' `"C:"` resolves to a local-mode backend. Local
    mode enumerates the in-process ABI handle registry.
  - **Honesty.** Fields with no OpenADS analogue — checksum failures,
    NetWare-era ECB counts, per-category memory, serial number —
    report a real `0`, documented in
    `docs/superpowers/specs/2026-05-16-adsmg-telemetry-design.md`.
  - Verified end-to-end against a live remote `openads_serverd`:
    `manage.prg` now prints real uptime, packet counts, worker
    threads, ports and server RSS. `tests/smoke/harbour/manage_probe.prg`
    (a non-interactive `manage.prg` variant) and the new
    `tools/mgprobe` CLI (`openads_mgprobe host:port`) reproduce it.

## 1.0.0-rc23 — 2026-05-15

- **Harbour `contrib/rddads` clean-compile sweep.** Reported by
  Pritpal Bedi after he hit `error: too many arguments to function
  'AdsSetScope'` building Harbour's unmodified `contrib/rddads/`
  against OpenADS's `ace.h`. Compiling the whole contrib (not just
  the rddtst happy path) exposed a family of signatures that the
  442/442 rddtst harness never reached. End-to-end repro:
  `HB_WITH_ADS=…/openads/include/openads hbmk2
  contrib/rddads/rddads.hbp -comp=mingw64` now exits 0.

  Functions brought to SAP-canonical shape (header + ABI export +
  affected unit tests, plus wire opcode for the one function that
  round-trips over the network):

  - **`AdsSetScope`** — 3-arg `(hIndex, usScope, pucKey)` →
    5-arg `(hIndex, usScope, pucScope, usLen, usDataType)`.
    `SetScope` wire opcode now carries `usDataType`; key length
    derives from trailing payload size. Export mirrors
    `AdsSeek`'s `ADS_DOUBLEKEY → ASCII-padded numeric`
    conversion so a scope set with a `double` compares
    apples-to-apples against the index's stored key bytes.
  - **`AdsGetVersion`** — 4-arg with mistyped letter/desc slots
    (`UNSIGNED32*` x4) → 5-arg
    `(UNSIGNED32* major, UNSIGNED32* minor, UNSIGNED8* letter,
    UNSIGNED8* desc, UNSIGNED16* descLen)`. Previous shape would
    have stomped 3 extra bytes past `&ucLetter` on the caller's
    stack.
  - **`AdsCopyTableContents`** — 2-arg → 3-arg with
    `usFilterOption`. Filter mode currently accepted and
    documented; `IGNOREFILTERS` is the implemented path.
  - **`AdsCreateSavepoint` / `AdsRollbackTransaction80`** — 2-arg
    → 3-arg with reserved `ulOptions` parameter (matches ACE 8.x).
  - **`AdsGetAOF`** — `(ADSHANDLE, UNSIGNED32*, UNSIGNED32*)`
    (returned-record-count style) → `(ADSHANDLE,
    UNSIGNED8* pucFilter, UNSIGNED16* pusLen)`. Returns empty
    filter for now; full AOF-source-string round-trip lands with
    M-AOF.4.
  - **`AdsEvalAOF`** — 3rd arg was `UNSIGNED32* pulRecords`; SAP
    expects `UNSIGNED16* pusOptLevel` (returns `ADS_OPTIMIZED_*`).
  - **`AdsSetStringW` / `AdsGetStringW` / `AdsGetFieldW`** — field
    name was declared `UNSIGNED16*` (wide). SAP keeps field names
    ASCII (`UNSIGNED8*`) even on the W variants; only the data
    buffer is wide-char. Helper `resolve_field_index_w` retyped
    to match; UTF-16 → UTF-8 transcode dropped from the name path
    (it was only there to compensate for the wrong type).
  - **`AdsGetString` / `AdsGetLong`** — exports were already
    implemented but missing from the public header, so Harbour's
    ANSI-path code in `ads1.c` linked only via
    `-Wimplicit-function-declaration` warnings. Declarations
    added.
  - **`ADS_MAX_PARAMDEF_LEN`** — `#define` is now `#ifndef`-guarded
    so a Harbour-style pre-define (`#define ADS_MAX_PARAMDEF_LEN
    2048` before the include) is honoured silently.
  - **`AdsGotoBookmark60`** — was 2-arg `(hObj, *pucBookmark)`;
    SAP / real ACE is 3-arg `(hObj, *pucBookmark, ulLength)`.
    Real ACE supports variable-length bookmarks (size depends on
    the index/order), so the caller hands the length back from
    `AdsGetBookmark60`'s `*pulLength` out-param. The 2-arg form
    was internally inconsistent with the Get half of the same
    pair. Both unit tests that exercised the round-trip were
    updated.
  - **`AdsGetAllTables`** — was 2-arg `(*ahTable, *pusArrayLen)`;
    needs `ADSHANDLE hConnect` as the first arg. With no
    connection handle the function can't know whose tables to
    enumerate in a multi-connection process. (Body remains
    `AE_FUNCTION_NOT_AVAILABLE` until M-13 implements enumeration.)

  Why rddtst missed all of this: rddtst exercises the RDD via
  `dbUseArea("ads")` so the cursor walks Harbour's vtable, not
  the `HB_FUN_ADSVERSION` / `HB_FUN_ADSCREATESAVEPOINT` / etc.
  PRG-level wrappers in `adsfunc.c`. Compiling the whole contrib
  is the real header check; we now have it (`C:\harbour-git\
  contrib\rddads` mingw64 build) and will keep it green.

## 1.0.0-rc22 — 2026-05-13

- **M12.25 — `AdsCreateTable` stamps the DBF header last-update date.**
  Follow-up to M12.24 (Robert van der Hulst): a freshly created+opened
  table reported `AdsGetLastTableUpdate()` = `1900-00-00` until the
  first `DbAppend` rewrote the DBF header. ACE writes the create date
  into header bytes 1..3 up front, so OpenADS now does too — in
  `AdsCreateTable` and every other path that lays down a fresh DBF
  (`AdsRestructureTable` / convert, SQL `INTO` / `SELECT` result
  cursors, GROUP BY / aggregate scratch tables). New
  `stamp_dbf_header_today` helper uses the same UTC clock as
  `CdxDriver::rewrite_header_`, so the create stamp and the
  first-append stamp agree.

## 1.0.0-rc21 — 2026-05-12

- **M12.24 — `AdsGetLastTableUpdate` real signature + AOF
  non-optimisable handling.** Was a 3-zero stub with the wrong
  signature (`SIGNED32* date, SIGNED32* time`); now matches ACE
  (`UNSIGNED8* pucDate, UNSIGNED16* pusLen`), reads the DBF header's
  last-updated stamp (header bytes 1..3, year offset from 1900) — over
  the wire too via a new `GetLastTableUpdate` opcode — and renders it
  through the date display format.
- **`AdsSetDateFormat`** stores a process-wide format string that
  `AdsGetDateFormat` / `AdsGetLastTableUpdate` honour
  (`CCYY` / `YYYY` / `YY` / `MM` / `DD` tokens; default
  `yyyy-mm-dd`).
- **`AdsSetAOF` no longer fails error 7200** on a non-optimisable
  expression (e.g. `Empty(NAME)`, `UPPER(NAME) = 'A'`) — ACE treats
  those as "not optimised", so OpenADS drops any prior AOF, reports
  `ADS_OPTIMIZED_NONE`, and returns success, letting the client RDD
  apply the filter itself (same on the server-side `SetAOF` handler).

## 1.0.0-rc20 — 2026-05-12

- **`OPENADS_WITH_HTTP` defaults to ON.** Studio no longer needs the
  explicit CMake flag — pass `-DOPENADS_WITH_HTTP=OFF` to opt out.
- **`AdsGetKeyNum` returns the correct relative key position.**
- **FiveWin + xBrowse over ADS** sample under `examples/fivewin/`.

## 1.0.0-rc19 — 2026-05-12 — X# Advantage RDD compatibility (local + remote)

- **M12.22 — versioned ACE overloads for the X# RDD.** Exports the
  `Ads*NN` entry-point names X# binds by name (`AdsConnect26`,
  `AdsCreateTable71` / `90`, `AdsOpenTable90`, `AdsCreateIndex90`,
  `AdsDDAddTable90`, `AdsDDCreateRefIntegrity62`,
  `AdsFindFirstTable62` / `AdsFindNextTable62`, `AdsGetDateFormat60`,
  `AdsGetExact22`, `AdsReindex61`, `AdsRestructureTable90`). Most
  forward to the base signature (dropping the charset / collation /
  page-size params newer ACE builds added); `AdsGetBookmark60` /
  `AdsGotoBookmark60` round-trip the recno as a 4-byte blob;
  `AdsCancelUpdate90` / `AdsSetProperty90` are accepted no-ops;
  `AdsFindConnection25` / `AdsGetTableHandle25` report not-found
  (OpenADS keys by handle, not path / name).
- **M12.23 — close the export gap the X# Advantage RDD relies on.**
  Live run of X#'s `AXDBFCDX` RDD against OpenADS' `ace64.dll`
  surfaced ~45 more entry points `ADSRDD.prg` binds by name
  (`AdsGetMemoBlockSize`, `AdsGetTableOpenOptions`, `AdsGetBookmark`,
  `AdsCancelUpdate`, `AdsSetField` / `AdsSetEmpty` / `AdsSetNull` /
  `AdsSetShort` / `AdsSetMoney` / `AdsSetTime` / `AdsSetTimeStamp`,
  `AdsGetDate`, `AdsContinue`, `AdsEval*Expr`, RI / unique / autoinc
  enforcement toggles, `AdsStmt*` helpers, …). Forwards where one
  fits, accept-and-ignore for session / statement toggles,
  `AE_FUNCTION_NOT_AVAILABLE` for the genuinely-unimplemented (so the
  X# runtime falls back to its own client path). The field-setter
  family handles the ACE "field name *or* 1-based ordinal cast to a
  pointer" idiom.
- **`AdsAppendRecord` auto-locks the new record** (ACE semantics for
  non-exclusive tables — X#'s `GoHot` refuses to write a record it
  sees as unlocked).
- **`AdsIsRecordLocked` / `AdsLockRecord` / `AdsUnlockRecord` honour
  `recno == 0` = current record** and report the real lock state
  instead of stubbing 0.
- **`AdsCreateIndex61` / `AdsCreateIndex90` option-bit fix:** the
  "descending" flag is `ADS_DESCENDING` (`0x08`), not `0x02` — `0x02`
  is `ADS_COMPOUND`, which X#'s ADSRDD always sets for CDX orders, so
  the old mask built every X# order descending and `DbGoTop` landed
  on the last key.
- **`AdsCreateTable` / `AdsCreateTable90` stage an empty `.fpt` next
  to the `.dbf`** when the field list has an `M` field (using
  `usMemoBlockSize`, default 64) — without it `Connection::open_table`
  can't attach a memo store and any memo write fails "memo store not
  attached".
- **X# RDD against a remote OpenADS server.** Three remote-path
  fixes so X#'s ADSRDD drives `openads_serverd` over the wire
  (`AdsConnect60("tcp://host:port/<datadir>", ADS_REMOTE_SERVER) →
  AX_SetConnectionHandle → DbUseArea`): `remote_field_index` now
  honours the "field name OR 1-based ordinal cast to a pointer" idiom
  (X#'s `_FieldSub` calls `AdsGetFieldType` / `Length` / `Decimals`
  by ordinal — a tiny pointer value was being dereferenced as a
  string); the remote `AdsOpenTable` branch defaults a missing
  extension to `.dbf`; and `AdsGetTableFilename` gained a remote
  path (returning the opened name) instead of failing
  `AE_INTERNAL_ERROR`.
- Full X# `AXDBFCDX` smoke (`tests/smoke/xsharp/AdsSmoke.prg` +
  `AdsSmoke_remote.prg`) passes end-to-end against OpenADS'
  `ace64.dll`.
- **Test layout.** Third-party RDD smoke harnesses under
  `tests/smoke/` (Harbour + X#). GUI showcases (FiveWin, X#
  WinForms) under `examples/`. All opt-in — none run in default
  `ctest`. Doctest coverage `abi_versioned_overloads_test.cpp` +
  `abi_remote_overloads_test.cpp` (gated on `OPENADS_TEST_REMOTE`).
- **Clipper-convention empty / past-end / Limbo states.**
  `goto_record(0)` is no-op + Eof (not error 5000); empty table
  reports BOF / EOF + `RecNo() = LastRec() + 1`; GO past-end enters
  Limbo; CDX dup-tag silent reopen; CDX dup-key insertion uses recno
  tie-break.
- **Index cursor consistency.** `goto_record` re-syncs the index
  cursor to the row's key (so the next SKIP walks the right
  neighbour); CDX cursor state tracking; hard-seek miss parks the
  cursor on the `>` neighbour for SKIP; hard-seek past every key
  parks at AfterEnd; `GO 0` keeps Limbo.
- **`SET DELETED ON` everywhere.** Index walks skip deleted rows;
  natural-order GOTOP / GOBOTTOM skip deleted; all-deleted under
  `SET DELETED` reports Limbo (not Eof); `GOTOP` / `GOBOTTOM` on an
  empty index report Limbo.
- **DESCEND wired through.** `bFindLast` retry on DESCEND retired;
  `DBSEEK( '' )` with `bSoftSeek` lands at the first record +
  `FOUND = .T.`; empty-key shortcut applies only to ASC orders.
- **CDX FOR-clause filter.** `CREATE INDEX … FOR <expr>` honoured at
  build time and on every subsequent insert; `CREATE INDEX` inserts
  deleted rows too (DBFCDX semantics); each new `CREATE INDEX`
  replaces the active order (Clipper convention); re-`CREATE INDEX`
  with an existing tag clears the old B+tree first; `ADS_DOUBLEKEY`
  ASCII conversion + creation-order tag ordinals.
- **rddads compat patches** (`tools/harbour_patch/`). `adsSeek`
  carve-out: `Seek( '' )` with the soft-seek flag now leaves `fBof`
  alone under Limbo so Harbour's own EOF logic doesn't snap to recno
  0; `ORDSETFOCUS(N)` uses CDX-file insertion order, not handle
  ids; `rddads-compat` default connection + `CREATE INDEX` goto-top.
- **SKIP overshoot.** Cursor stays on the last live record, not
  recno 0.

## 1.0.0-rc18 — 2026-05-09 — wire-protocol perf overhaul (~30× xbrowse repaint)

- **M12.17 — `RemoteTable` row cache.** New `FetchCurrentRow` opcode
  returns the entire current record's column values in one frame.
  `AdsGetField` / `AdsGetLong` / `AdsGetDouble` / `AdsGetJulian`
  serve every cell from the cache; W cells per row collapse to 1
  RTT.
- **M12.18 — piggyback row trailer on nav acks.** `GotoTopAck` /
  `GotoBottomAck` / `SkipAck` / `GotoRecordAck` /
  `FetchCurrentRowAck` carry the full row buffer + recno + deleted
  flag. `AdsGetField` / `AdsGetRecordNum` / `AdsIsRecordDeleted`
  all hit the cache populated by the prior nav ack — zero extra RTT
  after every nav.
- **M12.19 — record_count cache.** `AdsGetRecordCount` and
  `AdsGetRelKeyPos` now serve from a per-table cache, invalidated
  only on writes that change row count (`AdsAppendRecord`,
  `AdsDeleteRecord`, `AdsRecallRecord`, `AdsPackTable`,
  `AdsZapTable`).
- **M12.20 — `TCP_NODELAY`** disabled Nagle on every per-connection
  socket. The OpenADS wire is strict ping-pong, so Nagle's
  accumulation delay (up to 200 ms) was pure latency tax. Removes
  ~40–200 ms per RTT on slow links.
- **Net xbrowse PgDn**: pre-M12.17 ~300 RTT → ~20 RTT × ~5 ms (Nagle
  off) ≈ ~100 ms. ~30× end-to-end speedup vs pre-M12.17.

## 1.0.0-rc17 — 2026-05-09 — full wire bridges (rddads parity) + AOF/Rushmore + Studio Demo

The wire layer now carries every common ABI op rddads emits when an
app connects via `AdsConnect60("tcp://host:port/dir")`. Until rc16
most ABI calls past `AdsOpenTable` collapsed with "unknown table"
because they only resolved local `Table*`. Fixed across 40+ entry
points:

- **M12.14 — field metadata + cursor state.** `AdsGetNumFields`,
  `AdsGetFieldName`, `AdsGetFieldType`, `AdsGetFieldLength`,
  `AdsGetFieldDecimals`, `AdsAtBOF`, `AdsGetRecordNum`,
  `AdsIsRecordDeleted`, `AdsGotoBottom`. New `DescribeTable` opcode
  returns schema in one round-trip (cached on `RemoteTable` so
  `adsOpen` field-iter stays cheap).
- **M12.14b — typed reads.** `AdsGetLong`, `AdsGetDouble`,
  `AdsGetJulian` reuse `GetField`, parse client-side. No new opcode.
- **M12.15 — info / lock / maintenance / AOF.** `AdsIsFound`,
  `AdsRefreshRecord`, `AdsGetTableType`, `AdsGetRecordLength`,
  `AdsGetNumIndexes`, `AdsGetLastAutoinc`, `AdsLockRecord` /
  `Unlock`, `AdsLockTable` / `Unlock`, `AdsPackTable`,
  `AdsZapTable`, `AdsFlushFileBuffers`, `AdsCloseAllIndexes`,
  `AdsSetAOF`, `AdsClearAOF`, `AdsGetAOFOptLevel`.
- **M12.15b — memo.** `AdsGetMemoLength`, `AdsBinaryToFile`,
  `AdsFileToBinary` reuse `GetField` / `SetField`.
- **M12.16 — index handle subsystem.** New
  `HandleKind::RemoteIndex` + `RemoteIndex` wrapper.
  `AdsOpenIndex` / `AdsCloseIndex` / `AdsSeek` / `AdsSeekLast` over
  the wire. Server lazy-promotes ABI handles in `tbls_h` and syncs
  the engine cursor after Seek so the two cursors never drift.
- **M12.16b — remaining index ops.** `AdsCreateIndex` /
  `AdsCreateIndex61` (CDX-on-the-wire), `AdsSkipUnique`,
  `AdsSetScope`, `AdsClearScope`.
- **M12.16c — order switching.** New ABI exports
  `AdsSetIndexOrder` (by tag name) and `AdsSetIndexOrderByHandle`
  (by hIndex). Wire bridges via `SetOrder` / `SetOrderByName`
  opcodes.

## 1.0.0-rc16 — 2026-05-09

- **`AdsGetRelKeyPos` / `AdsSetRelKeyPos` honour the active index
  walk.** When `t->order()->index()` is bound, both walk the index
  once (seek_first → next() loop) to compute / position by *key*
  fraction, not raw recno. Cursor recno is restored after the `Get`
  probe so it doesn't visibly move the user-facing position. No-
  active-order behaviour unchanged.

## 1.0.0-rc15 — 2026-05-09

- **`+` (VFP autoincrement) field type.** dBASE Level 7 / VFP
  autoincrement column carries the field-descriptor type byte `+`;
  classifier in `src/drivers/dbf_common.cpp` now maps it to
  `DbfFieldType::Integer`. Prior fall-through left the schema
  parser at `Unknown`.

## 1.0.0-rc14 — 2026-05-08 — Windows Service + systemd / launchd units

- **Windows Service support.** `openads_serverd --install-service`
  registers a `SERVICE_AUTO_START` Win32 service;
  `--uninstall-service` drops the registration; the same binary
  doubles as both interactive CLI and SCM-driven service through a
  real `RegisterServiceCtrlHandler` with `SERVICE_CONTROL_STOP` /
  `SERVICE_CONTROL_SHUTDOWN` driving the same graceful-shutdown
  path as interactive Ctrl+C.
- **Linux systemd unit** (`scripts/openads-serverd.service`)
  hardened — `User=openads`, `ProtectSystem=strict`,
  `NoNewPrivileges`, `RestrictAddressFamilies=AF_INET AF_INET6`,
  `PrivateTmp`, `Restart=on-failure`.
- **macOS launchd plist**
  (`scripts/com.openads.serverd.plist`) — KeepAlive on crash;
  stdout / stderr to `/var/log/openads-serverd.{out,err}.log`.

## 1.0.0-rc13 — 2026-05-08 — production-CDX auto-open + Studio Demo + version fix

- **M-AOF.6 — production-CDX auto-open in `AdsOpenTable`.** Opening
  `<base>.dbf` auto-binds the sibling `<base>.cdx` so every tag in
  it becomes navigable on the Table without an explicit
  `AdsOpenIndex60` call. rc12 didn't do that — Studio's per-request
  short-lived `AbiSession` re-opened the DBF on every `/rows` fetch
  but never picked up a CDX created in a prior session, so
  `AdsGetAOFOptLevel` reported `NONE` forever even after `CREATE
  INDEX`.
- **Studio — guided AOF demo.** Browse-tab `▶ Demo` button walks
  the full Rushmore-style AOF story end-to-end against whatever
  table is active.
- **Studio — AOF hint chips functional.** When the AOF doesn't
  reach `FULL`, Studio surfaces a chip per character / memo field
  referenced by the cond that doesn't have a matching index yet.
  Click → runs `CREATE INDEX <field>_IDX ON <table> (<field>)` +
  re-applies the AOF.
- **`openads_serverd --version`** now reports the actual tag via
  `git describe --tags --always --dirty` at CMake configure time.
  Previously hard-coded `1.0.0-rc1` since rc1.

## 1.0.0-rc12 — 2026-05-08 — AOF (Rushmore-style) full slice

First working slice of **Advantage Optimised Filters (AOF) —
Rushmore-style query optimisation**. `AdsSetAOF` parses + evaluates
the cond, installs a per-record bitmap as a filter predicate that
`Skip` / `GoTop` honour, and routes individual leaves through CDX /
NTX index range scans whenever an open index has the leaf's field
as its key expression. `AdsGetAOFOptLevel` reports
`ADS_OPTIMIZED_FULL` / `PART` / `NONE` based on per-leaf coverage.
Sparse-bitmap navigation lifts the visible-set walk from O(N) to
O(M).

V1 grammar (identifiers + keywords case-insensitive, both
Clipper-style `.T.` / `.AND.` and SQL-style accepted):

```
<field> OP <literal>      OP in { = == != <> # < <= > >= }
<field> BETWEEN a AND b
<field> IN ( v1, v2, ... )
expr AND expr      OR     NOT expr      ( expr )
```

Index-accelerated leaves in V1: character / memo fields with a
bare-field-name index expression; operators Eq, Ne, Lt, Le, Gt, Ge,
Between, In. Numeric / date / logical fields, `UPPER(field)` /
compound expressions still produce a correct bitmap via per-record
fallback (don't count as "served by index").

## 1.0.0-rc10 — 2026-05-08

- **Studio mode badge.** SPA header now shows 🏠 `LocalServer`
  (green) when the console runs in-process inside `ace64.dll` /
  `ace32.dll`, or 🌐 `Remote Server` (blue) when hosted by
  `openads_serverd`. Hover reveals the active data directory.
  Signal from a new `mode` field on `/api/health`
  (`"localserver"` when `HttpConsole` was started without a backing
  wire-server pointer, `"remote-server"` otherwise). Reverse-proxy
  deployments that strip unknown fields keep working unchanged —
  the badge stays hidden when `/api/health` is unreachable.

## 1.0.0-rc9 — 2026-05-08 — embedded Studio (LocalServer)

Studio web console is now embedded inside `ace64.dll` / `ace32.dll`
itself. A LocalServer application — Harbour / X# / Clipper loading
the OpenADS DLL directly without launching `openads_serverd.exe` —
spins up the console in its own process.

- Three OpenADS-only entry points:
  `AdsStudioStart(port, data_dir)`, `AdsStudioStop()`,
  `AdsStudioPort(&port)` (ordinals 238–240).
- Env-driven auto-start: `OPENADS_STUDIO_PORT=<port>` before
  launching the host app; `OPENADS_STUDIO_DATA` / `OPENADS_STUDIO_HOST`
  default to `"."` / `127.0.0.1`. Without the port env var the auto
  hook is a no-op — no surprise localhost listener.
- Compiled into the DLL only when `-DOPENADS_WITH_HTTP=ON` (default
  since rc20). Without that flag the three exports return
  `AE_FUNCTION_NOT_AVAILABLE` so callers can detect the build
  flavour at runtime.

## 1.0.0-rc8 — 2026-05-08 — dual x64+x86, static mbedtls

Addresses XSharp-Project feedback (Robert van der Hulst):

- **Dual x64+x86 ZIP.** Bundle ships both `ace64.dll` and
  `ace32.dll` plus matching `openads_serverd_{x64,x86}.exe` /
  `openads_bench_{x64,x86}.exe`. X#, Harbour-x86, and legacy
  Clipper apps all pick the right bitness without a separate
  download.
- **Static-linked mbedtls 3.6.2.** Zero runtime `libssl` /
  `libcrypto` / `mbedtls` DLL dependency. Verified via
  `dumpbin /dependents`: only KERNEL32, WS2_32, bcrypt, MSVCP140,
  VCRUNTIME, and Windows ucrt `api-ms-win-crt-*` show up.
- **`openads_serverd --port <N>`** plus an explicit "port 6262 is
  the SAP Advantage Database Server default" hint when the bind
  clashes with a running ADS service.
- **`tools/bench/README.md`** documents that `openads_bench`
  synthesises its own three-column DBF (`ID N(8,0)`, `TAG C(4)`,
  `AMT N(8,2)`) on each run from a fixed seed; nothing is shipped,
  fully reproducible.

## 1.0.0-rc6 — 2026-05-07

Studio schema view shows the ADS field-type *letter* (C / N / M / D
/ L / I / Y / B / V / Q / G) instead of the numeric code, with the
ADS type-letter mapping corrected; the numeric tooltip is dropped.

## 1.0.0-rc5 — 2026-05-07

Studio binary-memo serializer + Win x86 build fix. Binary memo
cells are now base64-encoded in `/api/tables/<t>/rows` so JSON
round-trips cleanly through the SPA.

## 1.0.0-rc4 — 2026-05-07

CDX leaf+branch full multi-level split + memo > 64 K fix +
cross-platform `-Werror` clean.

- **`M(cdx-split)`** — full multi-level B+tree leaf+branch split.
  Stress harness now exercises 2 bags × 2 tags each at 200 K rows;
  `CREATE INDEX` defaults back to CDX (the previous NTX fallback is
  retired).
- **DBF compat.** Accept 0xF5 / 0xFB headers + single-letter type
  codes; `AdsGetField` no longer truncates memos > 64 K to 65534
  bytes.
- **Cross-platform.** `ace32.lib` / `ace64.lib` import libs +
  ordinal-compat tooling shipped. GCC 13 (Ubuntu 24.04) + clang
  Release pass `-Werror` cleanly: `-Wshadow`,
  `-Wstringop-truncation`, `-Wformat-truncation`, `-Wsign-conversion`
  in `julian_to_ymd`, `COL%zu` field-name copy, stress harnesses,
  `probe_cdx2`, drivers/cdx iterators.
- **Studio.** `studio.web.0.12` ZIP backup of data dir,
  `studio.web.0.13` table-type override + memo hex viewer,
  `studio.web.0.14` host OS / arch / compiler banner.

## 1.0.0-rc3 — 2026-05-07

Studio sessions kill + JSON export + TLS deployment docs.

- **Studio.** `studio.web.0.10` bulk select + saved queries + SQL
  highlight; `studio.web.0.11` kill-session button + JSON export.
- **TLS deployment guide** (EN / ES / PT) — proxy, stunnel, SSH
  tunnel recipes for fronting the cleartext server with TLS until
  M12.12 ships.
- CI release-workflow build timeout bumped 45 → 60 minutes.

## 1.0.0-rc2 — 2026-05-07

Studio web 0.4 – 0.10: Sessions, Data Dictionary tab + REST,
Reindex / Pack / Zap + `CREATE INDEX` wizard + memo viewer, sidecar
list / server stats / DBF upload, HTTP Basic auth + table download
+ theme toggle, Browse sort + filter + i18n (EN / ES / PT). Data
Dictionary documentation pages (EN / ES / PT). `b64decode`
sign-conversion fix.

## 1.0.0-rc1 — 2026-05-07

Studio web console + bilingual+ documentation site + release CI.

- **OpenADS Studio** (`studio.web.0.1` … `0.3`) — clean-room
  ARS-equivalent web console embedded in `serverd`. CRUD + paginated
  browse + server info; `CREATE` / `DROP` / encrypt + SQL history.
  All third-party-product-name references scrubbed from Studio
  copy. `AdsGetLastError` captured before close + 'did you mean'
  hint on connection failures. Docs link in header.
- **Documentation site** — bilingual+ EN / ES / PT under `docs/`,
  published through GitHub Pages workflow. Benchmarks pages +
  Studio screenshots.
- **Release CI** — `release` workflow builds + packages + publishes
  on tag push. `docs/superpowers` and `build/` are export-ignored
  from source archives.

## 0.4.1 — 2026-05-06

`openads_bench` v2 + CI matrix gains a TLS=ON entry.

## 0.4.0 — 2026-05-06

Real TLS transport, transport abstraction, wire-protocol spec.

- **M12.12** — real TLS via vendored mbedtls 3.6 LTS (Apache-2.0).
  The `tls://` URI scheme reserved in 0.3.6 now wires through to a
  real handshake on both client and server.
- **M12.13** — transport abstraction. The wire protocol no longer
  bakes in the socket type; cleartext, TLS, and any future
  transport plug in through a single virtual interface.
  `docs/wire-protocol.md` documents the wire format.
- CI runners + actions bumped to current versions; missing
  `<tuple>` include added.

## 0.3.6 — 2026-05-06

`tls://` URI scheme reserved (M12.12 stub) ahead of the real
handshake landing in 0.4.0.

## 0.3.5 — 2026-05-06

Wire-protocol semantics complete: ACE error-code propagation
(M12.10) and batched row fetch (M12.11 — `Fetch` / `FetchAck`).

## 0.3.4 — 2026-05-06

Phase 2 server feature-complete (read + write + SQL + index +
auth).

- **M12.6** — remote write surface (`append` / `set` / `delete` /
  `recall` / `goto` / `flush`).
- **M12.7** — remote SQL exec (`ExecuteSQL` wire op).
- **M12.8** — remote `AdsReindex`.
- **M12.9** — server-side authentication.
- `RemoteConnection` dtor now calls `disconnect`.

## 0.3.3 — 2026-05-06

Phase 2 server alive end-to-end + cross-platform SQL bench.

- **M12.3** — server accept loop + `Hello` / `Connect` dispatch.
- **M12.4** — remote read-only table ops.
- **M12.5** — dual-mode DLL: `tcp://` URIs route the ABI to the
  server; local-mode paths still hit the in-process engine.
- `openads_serverd` standalone TCP server CLI.
- `openads_bench` cross-platform SQL workload timer.
- macOS bring-up: `setup_macos.sh` one-shot script,
  `HOST_NAME_MAX` fallback, `accept()` self-connect wake-up,
  same-process lock contention test skip.
- Linux bring-up: OFD locks for fd-scope contention,
  `shutdown(SHUT_RDWR)` before `close()` so blocked `accept()`
  wakes, third + fourth waves of clang `-Werror` sign /
  include / unused fixes,
  `-Wreturn-type-c-linkage` cleanup on internal helpers.

## 0.3.2 — 2026-05-05

SQL window-function + scalar-fn deepening + sockets layer.

- **M10.49** PARTITION BY.
- **M10.50** RANK / DENSE_RANK.
- **M10.51** qualified column refs.
- **M10.52** multi-row `VALUES`.
- **M10.53** `NULLIF` / `COALESCE` / `IFNULL`.
- **M10.54** `FILTER (WHERE …)` aggregate clause.
- **M12.2** sockets layer (cross-platform).

## 0.3.1 — 2026-05-05

SQL date / CTE / NULL surface + collation + wire skeleton.

- **M10.43** multi-arg scalar fns.
- **M10.44** `IS NULL` / `IS NOT NULL`.
- **M10.45** date scalar fns.
- **M10.46** derived tables.
- **M10.47** `ROW_NUMBER()`.
- **M10.48** CTE (`WITH …`).
- **M11.6** NULL bitmap.
- **M11.7** collation.
- **M11.8** OEM / UTF-8 conversion.
- **M12.1** wire skeleton.

## 0.3.0 — 2026-05-05

SQL maturity wave: every JOIN flavour, the full subquery /
aggregate / DISTINCT / UNION / CASE / LIMIT surface, plus the AEP
host, AES-256-CTR encrypted DBFs, VFP V / Q field types, and
nested transactions. Brings the SQL layer to "real apps run
through it" status.

### Highlights

- **JOIN matrix.** `INNER` (M10.13 parse + M10.14 executor), `LEFT
  OUTER` (M10.16), `RIGHT OUTER` (M10.21), `FULL OUTER` (M10.22 —
  union of LEFT + RIGHT), `JOIN` + `WHERE` / `ORDER BY` combos
  (M10.20), `JOIN` + aggregate combo (M10.23), `GROUP BY` across
  `JOIN` (M10.34).
- **Subqueries.** `IN` literal lists + subqueries (M10.15),
  `EXISTS` uncorrelated (M10.17), correlated `EXISTS` (M10.24),
  scalar subquery `<col> op (SELECT col FROM t)` (M10.18),
  aggregate scalar subquery (M10.19), correlated scalar subquery
  (M10.29), correlated `IN` subquery (M10.35).
- **Aggregates + grouping.** `COUNT(*)` / `COUNT` / `SUM` / `AVG`
  / `MIN` / `MAX` (M10.10), `GROUP BY` + `HAVING` (M10.25), HAVING
  expression tree (M10.30), aggregate / `GROUP BY` inside `UNION`
  members (M10.36).
- **Set + shape ops.** `UNION` / `UNION ALL` (M10.26),
  `UNION` + projection (M10.27), `UNION` + `ORDER BY` (M10.28),
  `DISTINCT` (M10.31), `LIMIT` / `OFFSET` (M10.32),
  `BETWEEN` / `LIKE` (M10.33), multi-column `ORDER BY` (M10.37),
  `CASE WHEN` in projection (M10.38).
- **DML / DDL.** `INSERT` (M10.5), `ORDER BY` execution (M10.6),
  `UPDATE` / `DELETE` bulk through `AdsExecuteSQLDirect` (M10.7),
  projection lists (M10.8), DDL `CREATE TABLE` / `CREATE INDEX`
  (M10.9), VFP autoinc fields with persistent counter (M10.11),
  `AdsRestructureTable` CHANGE (same-type length / decimals)
  (M10.12), `INSERT INTO … SELECT` (M10.41), `CREATE TABLE AS
  SELECT` (M10.42).
- **Other SQL.** `WHERE` `OR` / `NOT` / parens + numeric literals
  (M10.3), scalar string fns (M10.39), arithmetic in projection
  (M10.40).
- **Engine + drivers.** Real `.add` Data Dictionary persistence —
  round-trips through `.add` reopen (M10.1); VFP I / Y / B field
  decode + encode (M10.2); branch-level MISS closure
  (`AdsRestructureTable` DELETE-fields + `AdsSetIndexDirection`)
  (M10.4).
- **AEP host (M11.4).** `CREATE PROCEDURE` + `EXECUTE PROCEDURE`
  through the OpenADS clean-room AEP runtime.
- **Encrypted DBFs (M11.2).** OpenADS-encrypted DBF — AES-256-CTR
  per-page, transparent through the read / write path.
- **VFP V / Q (M11.1).** Varchar + Varbinary field types.
- **Nested transactions (M11.3).** `AdsReleaseSavepoint` + nested
  `BEGIN` / `COMMIT` (proper save-point stack).

## 0.2.0 — 2026-05-04

The 0.2.0 release closes the entire 226-symbol Harbour-reachable
`Ads*` ABI surface — every export resolves to either a real
implementation or a documented local-mode silent-success. No exports
hard-fail with `AE_FUNCTION_NOT_AVAILABLE` at the function level any
more. The release also relicensed the project from MIT to Apache
License 2.0 and added a clean-room provenance / non-commercial /
no-warranty disclaimer block + NOTICE file.

### Highlights

- **Compound CDX expression evaluator.** `UPPER`, `LOWER`,
  `LTRIM` / `RTRIM` / `ALLTRIM`, `STR(n[,len[,dec]])`, `DTOS(date)`,
  `SUBSTR(s,start[,len])`, and string concatenation with `+`. UPPER /
  LOWER / SUBSTR walk UTF-8 codepoints (ASCII + Latin-1 supplement
  case map, `ÿ↔Ÿ` pair); the Latin-1 case mapping table closes the
  M9.17 `*W` Unicode surface.
- **Real CRUD for tables and indexes.** `AdsCreateTable` parses the
  rddads `NAME,Type,Len,Dec;…` field-def syntax; `AdsCreateIndex61` /
  `AdsCreateIndex` build CDX or NTX bags compatible with FoxPro and
  Clipper layouts. `AdsZapTable` / `AdsPackTable` / `AdsReindex`
  match the Clipper bound-index lifecycle.
- **Multi-file index binding.** Multiple `.ntx` files (or multiple
  pre-built `.cdx` bags) coexist on a single Table; same-path reopen
  refreshes; `AdsCloseIndex` drops the closed view without disturbing
  the active order.
- **Transactions + savepoints + WAL recovery.** `AdsBeginTransaction`
  / `AdsCommitTransaction` / `AdsRollbackTransaction` /
  `AdsCreateSavepoint` / `AdsRollbackTransaction80(savepoint)`. Mid-
  tx crash + reopen replays the WAL and writes back before-images
  for orphan transactions.
- **Memo (DBT / FPT) read + write + binary type.** Text memos
  round-trip through DBT and FPT; `AdsGetBinary` /
  `AdsGetBinaryLength` / `AdsSetBinary` carry binary blobs through
  FPT block-type tags (Text / Picture / Object); chunked
  `AdsSetBinary` writes reassemble through a per-(table, field)
  accumulator.
- **Locking with retry policy.** `AdsLockTable` / `AdsLockRecord`
  use non-blocking byte-range acquires (`LockFileEx
  LOCKFILE_FAIL_IMMEDIATELY` on Windows, `fcntl F_SETLK` on POSIX);
  `AdsSetLockCycle` / `AdsSetLockRetryCount` configure the retry
  budget.
- **Full-text search.** `AdsCreateFTSIndex` writes a clean-room
  `# OpenADS FTS v0` text file per table; `AdsFTSSearch` and the
  SQL `CONTAINS(<col>, '<query>')` predicate intersect per-token
  recno lists with AND semantics.
- **Server / dictionary surface.** `AdsMg*` (15 calls) report local-
  mode "everything quiescent" responses; `AdsDD*` (14 advanced-DD
  calls) accept silently and zero-fill property getters. Real
  persistence in the OpenADS DD format lands with 0.3.x.
- **Schema evolution.** `AdsRestructureTable` ADD-fields path
  rewrites the DBF with extended schema and preserves every
  record's original-field bytes; DELETE / CHANGE arguments still
  surface AE_FUNCTION_NOT_AVAILABLE pending VFP / ADT structural
  extensions.
- **Misc.** Real `AdsGetServerName` / `AdsGetServerTime`,
  `AdsGetLongLong`, `AdsSetFieldRaw`, `AdsVerifySQL`,
  `AdsFailedTransactionRecovery`, `AdsGetAllLocks`, `AdsSkipUnique`,
  `AdsFindFirstTable` / `AdsFindNextTable` / `AdsFindClose`,
  `AdsCopyTable` / `AdsCopyTableContents` / `AdsConvertTable`,
  `AdsAddCustomKey` / `AdsDeleteCustomKey`.

### Project posture

- License: relicensed **MIT → Apache License 2.0** (`LICENSE` +
  `NOTICE`).
- Independence + non-commercial purpose + clean-room provenance +
  no-warranty + downstream responsibility — block added to the
  README and mirrored to the NOTICE file (Apache 4(d) preservation).
- Tests: **214** doctest cases, **3865** assertions, all green on
  Windows / MSVC Release. CI matrix builds Windows + Linux + macOS
  cleanly through `.github/workflows/ci.yml`.

### Milestones

| Tag | Milestone |
|-----|-----------|
| `m9.1`   | Compound CDX expression evaluator |
| `m9.2`   | Stub batch reorganised into real / no-op / missing |
| `m9.3`   | Compound expressions validated through Harbour |
| `m9.4`   | `AdsGotoRecord` + table / file metadata |
| `m9.5`   | `AdsCreateTable` |
| `m9.6`   | `AdsRefreshRecord` + `AdsExtractKey` |
| `m9.7`   | `AdsCreateIndex61` with compound expression |
| `m9.8`   | `AdsZapTable` + `AdsPackTable` |
| `m9.9`   | `AdsReindex` |
| `m9.10`  | NTX multi-level B+tree split |
| `m9.11`  | `AdsCopyTable` / `AdsCopyTableContents` / `AdsConvertTable` |
| `m9.12`  | `AdsFindFirstTable` / `AdsFindNextTable` / `AdsFindClose` |
| `m9.13`  | Binary memo (`AdsGetBinary` / `AdsSetBinary` / `AdsGetBinaryLength`) |
| `m9.14`  | NTX multi-tag binding |
| `m9.15`  | Real `AdsGetServerName` / `AdsGetServerTime` + binding-leak fix |
| `m9.16`  | Chunked `AdsSetBinary` |
| `m9.17`  | Unicode `*W` variants |
| `m9.18`  | Lock retry / cycle policy |
| `m9.19`  | `AdsCreateFTSIndex` |
| `m9.20`  | `AdsAddCustomKey` / `AdsDeleteCustomKey` |
| `m9.21`  | FTS search side (`AdsFTSSearch` + SQL `CONTAINS`) |
| `m9.22`  | UTF-8 codepoint-aware index-expression evaluator |
| `m9.23`  | Misc MISS fillers (LongLong / FieldRaw / VerifySQL / FailedTxRecovery / GetAllLocks / SkipUnique) |
| `m9.24`  | Local-mode `AdsMg*` surface (15 calls) |
| `m9.25`  | Local-mode `AdsDD*` CRUD surface (14 calls) |
| `m9.26`  | `AdsRestructureTable` (ADD-fields path) |
| `m9.27`  | CI matrix portability |

## 0.1.0 — 2026-05-04

Final 0.1.0. The post-rc1 work below extends the Harbour smoke
beyond the read path covered in 0.1.0-rc1: a real Harbour app now
also drives multi-tag focus swaps, ARIES-style transactions, and
memo M-field round-trips end-to-end through OpenADS' `ace64.dll`.

### M8.9 — Multi-tag CDX + OrdSetFocus

- `AdsOpenIndex` widened to its real 4-arg signature
  `(hTable, pucName, ahIndex[], &pu16ArrayLen)`. Every tag inside a
  compound CDX is opened by name through `CdxIndex::open_named`;
  the first tag's IIndex moves into `Table::set_order` and the rest
  park in their bindings.
- `Table::take_order()` / `Order::release()` surrender the active
  index's `unique_ptr<IIndex>` so a focus swap can park it in the
  previous binding's slot.
- `get_table` and `table_for_index` now call `activate_binding(h)`
  whenever a navigation call arrives with an index handle, so
  rddads' `pArea->hOrdCurrent` swaps drive the Table's active order
  in lockstep.
- `AdsGetIndexHandle` strips trailing whitespace from the caller's
  tag name; `AdsGetIndexName` / `AdsGetIndexExpr` read each
  binding's metadata directly so parked tags report their real name
  even before they become live.
- `AdsGetNumIndexes` returns the per-table binding count.

### M8.10 — Transactions through Harbour

- A real Harbour app drives `AdsBeginTransaction` /
  `AdsRollback` / `AdsCommitTransaction` directly. BEGIN + APPEND +
  ROLLBACK leaves the appended row in the DBF flagged deleted (CDX
  index entries persist by design — `Found()` still reports `T` but
  `Deleted()` is `T`); BEGIN + APPEND + COMMIT persists durably to
  both the DBF and every CDX tag.
- `Table::register_extra_index_view` /
  `Table::unregister_extra_index_view` /
  `Table::clear_extra_index_views` track the parked CDX sub-tags as
  non-owning views; the binding still owns the IIndex lifetime.
- `Table::snapshot_index_keys_()` captures the pre-write key per
  index — active order plus extras — and `sync_all_indexes_(snap)`
  erases each prior `(recno, prev_key)` and inserts the new one in
  lockstep, so a `set_field` on a multi-tag CDX keeps every tag
  consistent (M8.8 only synced the active order).
- `Table::flush()` flushes the active order **and** every extra
  view so a multi-tag commit reaches disk for every tag.

### M8.11 — Memo M-fields (FPT)

- A real Harbour app appends rows whose `FIELD->NOTES` carries a
  short memo (43 bytes) and a longer memo (280 bytes), closes the
  area, reopens, and reads the memos back via the standard Clipper
  RDD surface.
- `make_cdx.exe` now also writes an empty `data.fpt` next to
  `data.cdx` via `FptMemo::create`, so `Connection::open_table` finds
  a memo store to auto-attach when the DBF declares an M field.
- `AdsGetMemoLength` / `AdsGetMemoDataType` / `AdsGetString` are now
  real implementations using `resolve_field_index` (M4 had earlier
  versions that only accepted string field names; rddads passes the
  `ADSFIELD(n)` integer form).
- `AdsCloseTable` flushes the table before releasing the handle so
  non-transactional appends reach disk on `USE` close.
- `ADS_MEMO_TEXT` / `ADS_MEMO_PICTURE` aliases resolve to the
  M8.4-verified `ADS_STRING` (4) and `ADS_IMAGE` (7) values.

## 0.1.0-rc1 — 2026-05-03

First end-to-end validation against Harbour `contrib/rddads`. A real
`.prg` compiled with `hbmk2 -comp=msvc64 -lrddads -lace64` opens a
DBF, walks records, runs `dbSeek`, appends rows, and reopens — every
call lands on OpenADS' `ace64.dll` with no Harbour rebuild.

### M0–M3.10 (engine + drivers)

- 5-layer architecture: ABI shim → Session/Connection → SQL → Engine
  (Table / Index / MemoStore / LockMgr / TxLog) → OS abstraction.
- DBF read/write for C / N / L / D columns; deletion flag; flush.
- CDX driver with compound layout (file header + structure-tag root +
  per-tag CDXTAGHEADER + sub-tag B+tree). Multi-tag-per-file API
  (`add_tag` / `open_named` / `list_tags`). FoxPro-equivalent leaf
  bit-pack (mirrors Harbour `hb_cdxPageLeafInitSpace`). Compound CDX
  closes the last reviewer-flagged compat-breaking item.
- NTX driver with cache-based in-order traversal for multi-level
  trees; leaf-split fix promotes the separator without duplicating it.
- AES-128 / AES-256 ECB primitives (vendored tiny-AES-c, Unlicense),
  validated against FIPS-197 + NIST SP 800-38A.
- DBT + FPT memo round-trip.
- Data Dictionary (`.add`): `TABLE alias=path` text format with alias
  resolution.
- Minimal SQL: `SELECT * FROM <table> [WHERE col op 'lit' [AND ...]]`
  with six comparison operators.

### M4 — Locking

- OS byte-range locking with ranges compatible with original ACE so
  installs can coexist during migration.

### M5 — Transactions

- WAL with CRC-32C records (BEGIN / UPDATE / COMMIT / ABORT).
- In-memory ordered op log with named savepoints (M5.3).
- Group commit (M5.4): each record carries a monotonic LSN;
  `sync_to(lsn)` is the group-commit primitive — first thread to
  observe `last_synced_lsn_ < lsn` issues a single fsync covering
  the high-water mark.
- Idempotent recovery (M5.5) via `openads.lsnmap` sidecar.
  Concurrent recovery passes can never regress the per-record
  watermark.

### M6 — Data Dictionary

- `.add` parser, `Connection::open(<path>.add)` resolves member
  tables through the dictionary on every `AdsOpenTable`.

### M7 — SQL

- Parser + executor for `SELECT *` + multi-clause `WHERE` joined by
  implicit `AND`. Compiles to a `Table::RowPredicate` closure used by
  `AdsExecuteSQLDirect` to filter the cursor's record stream.

### M8 — Harbour conformance (this release)

- **M8.0** `ace64.dll` / `ace32.dll` SHARED CMake target with a `.def`
  exporting 80 real `Ads*` entry points.
- **M8.1** 226 `Ads*` exports — superset of every symbol Harbour
  `rddads.lib` references; the 146 newly-stubbed entries return
  `AE_FUNCTION_NOT_AVAILABLE` (5004) so the link resolves cleanly.
- **M8.2** Six legacy MSVC2013-era CRT shims (`_dclass`, `_dsign`,
  `_wfsopen`, `_getch`, `_kbhit`, `_eof`) re-exported under aliases
  so Harbour's prebuilt msvc64 libs link against modern UCRT.
- **M8.2** `smoke.exe` runs end-to-end: `AdsVersion()` resolves
  through the rddads wrapper to OpenADS' `AdsGetVersion`.
- **M8.3** `USE data VIA "ADSCDX"` + walk records. `Connection::open_table`
  auto-appends `.dbf`. `AdsGetField` / `AdsGetFieldType` /
  `AdsGetFieldLength` accept either string field names or the
  `ADSFIELD(n)` integer-cast-as-pointer form. `AdsConnect`
  is now a real wrapper around `AdsConnect60`.
- **M8.4** ACE field-type constants verified by sweeping
  `AdsGetFieldType`'s return through 0..40 against rddads. Result:
  `ADS_LOGICAL = 1`, `ADS_NUMERIC = 2`, `ADS_DATE = 3`,
  `ADS_STRING = 4`, ... — the inverse of the public ACE SDK ordering
  in some places. Mapping captured in `include/openads/ace.h`.
- **M8.5** Multi-field DBF (C/N/L/D) end-to-end. `AdsGetFieldDecimals`,
  `AdsGetLong`, `AdsGetDouble`, `AdsGetJulian` real impls (was 5004
  stubs); `AdsGetJulian` parses `YYYYMMDD` and computes Clipper Julian
  Day Numbers using the same Gregorian formula as `hb_dateEncode`.
- **M8.6** `dbSeek` end-to-end through OpenADS' CDX. `Table::path()`,
  index path resolution + auto-extension, polymorphic `get_table`
  (accepts table or index handles — rddads' `adsGoTop` calls
  `AdsGotoTop(hOrdCurrent)` when an order is active), 6-arg `AdsSeek`
  signature matching rddads, real `AdsIsFound` reading
  `Table::last_seek_found_`.
- **M8.7** Write path: `dbAppend` + `FIELD-> := value` + `dbCommit` +
  reopen. `AdsSetString` / `AdsSetLogical` / `AdsSetDouble` /
  `AdsSetLongLong` / `AdsSetJulian` real impls; field index resolution
  via `resolve_field_index`.
- **M8.8** Active index auto-syncs on every record mutation.
  `Table::compute_index_key_` evaluates bare-field-name expressions
  against the current `record_buf_`; `Table::sync_active_index_`
  erases the prior `(recno, prev_key)` and inserts the new one.
  `Table::flush()` flushes both the driver and the index.

### Tests

- 135 doctest cases / 1820 assertions passing on Windows / MSVC
  Release.
- One `tests/harbour_smoke/` integration harness producing a
  runnable `smoke.exe` that exercises the full Harbour →
  rddads.lib → OpenADS path.
