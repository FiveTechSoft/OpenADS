# OpenADS 1.8.17

## Changes

### Fixed — remote `DbCreate` / `AdsCreateTable` wrote next to the client app

When a Harbour/X# client connected with `AdsConnect60(tcp://…)` and called `dbCreate` / `AdsCreateTable`, create still ran against a *local* default connection (cwd of the client process) because there was no remote `CreateTable` wire opcode. The table landed next to the application, then the post-create `AdsOpenTable` (which *does* route remote) failed with **ADSCDX/5103 OpenTable: open failed**.

Reported by **Pritpal Bedi** against v1.8.16: path-to-open of `DbCreate` had been fixed, path-to-create had not.

**What changed**

- New wire opcodes: `CreateTable` / `CreateTableAck`, `DropTable` / `DropTableAck`
- `AdsCreateTable` and `AdsDropTable` detect a remote connection and forward create/drop over the wire
- The server writes under its data directory (the 1.8.15 absolute-path folding applies remotely too)
- The client re-opens through the normal remote `OpenTable` path (production bag auto-open, GotoTop)
- Remote `AdsConnect60` records the handle as the rddads default so create with `hConnect=0` works

**Update both** `openads_serverd` **and** `ace64.dll` / `openace64.dll` — create requires the new opcode on the server.

Regression tests: `tests/unit/abi_remote_create_table_test.cpp`. Live smoke verified Windows client → macOS iMac server.

Full details in [CHANGELOG.md](CHANGELOG.md).
