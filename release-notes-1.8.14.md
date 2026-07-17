# OpenADS 1.8.14

## Changes

### SQL — result cursor was left at BOF after execute (wrong data on every query)

`AdsExecuteSQLDirect` / `AdsExecuteSQL` left the result cursor positioned *before* the first row, where SAP ADS positions it *on* the first row. A client that reads the current record immediately after executing — the SAP-supported pattern — got a **phantom empty row** on every query (`SELECT COUNT(*)` came back as a blank row, then the count). The fix positions the result on the first row at the API boundary — engine tables, `system.*`/aggregate results, SQL-backend cursors, and remote clients alike. Callers that already `AdsGotoTop` first are unaffected.

### REMOTE — backward (PgUp) read-ahead: 299 → 7 round-trips on a reverse scan

Read-ahead was forward-only: a `Skip(-1)` browse (PgUp, or a report walking a table in reverse) paid one round-trip per row. The server now attaches a backward block to negative skips, the depth ramp resets on a direction reversal, and a new capability bit (`kCapPrefetchBackward`) keeps old/new client-server mixes correct in both directions. A 300-row reverse scan over loopback dropped from 299 wire requests to 7.

### The server's real version is now visible from the client

Repeated "the fix didn't help" reports keep resolving to an old `openads_serverd` still running (a live service holds its exe open, so copying an update over it can fail silently). There was no way to prove it from the client side: the wire handshake answered a hardcoded `openads/0.3.2` and `AdsMgGetInstallInfo` a hardcoded `OpenADS 1.0`.

- The handshake now carries the real build version (`openads/1.8.14`).
- `AdsMgGetInstallInfo` on a **remote** management handle reports the **server's** version — works against every server ever shipped (an old build identifies itself by answering the literal `0.3.2`). Local handles report the DLL's build. From Harbour: `AdsMgConnect("host:port")`, `AdsMgGetInstallInfo()[3]`, `AdsMgDisconnect()`.

### REMOTE — `CloseTable` kept the table files open on the server

Closing a remote table released the client's view of it, but the server-side "shadow" handle (used for ordered navigation, index scopes and locks) stayed open until the session disconnected. Erasing, renaming or reopening the just-closed `.dbf`/`.cdx`/`.fpt` in exclusive mode failed with "file in use", and an app-level retry loop turned that into seconds of delay or an apparent hang at close-all-files time. `CloseTable` now closes the shadow handle with the table and drops the session's index entries that resolved through it. Pinned by two new unit tests (lock release across close + file deletability straight after a remote close; a timed 12-table open/work/close pass over a live socket).

**Update `openads_serverd` for the CloseTable fix; update `openace64.dll` too to be able to read the server's version from the client.**

### Examples

- `examples/fivewin/xbpaint_delscope.prg` — logs the server version, then drives the exact xBrowse repaint shape (anchor / page-read / `AdsGetRelKeyPos` / `DbGoto` back / advance, arrow-up variant, scrollbar thumb drags via `AdsSetRelKeyPos`) over a 300-row remote table with `SET DELETED ON`, an index scope, and deleted rows inside the scope.

Full details in [CHANGELOG.md](CHANGELOG.md).
