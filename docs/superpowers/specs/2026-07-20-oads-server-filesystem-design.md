# Server filesystem API (`oads_*` / `Ads*`) — design

Date: 2026-07-20  
Status: approved (brainstorming), pending implementation plan

## Problem

Harbour / FiveWin / X# clients that connect with
`AdsConnect60("tcp://host:port/data")` can open and create tables on the
server, but they cannot manage arbitrary files and directories under the
server data root. LetoDB solves this with `leto_File`, `leto_FErase`,
`leto_Directory`, `leto_FOpen` / `FRead` / `FWrite`, etc., gated by
`EnableFileFunc=1` in `letodb.ini`.

OpenADS needs the same capability so client-server apps do not require a
mapped drive or SMB share to the server filesystem.

## Goals

- Leto-parity **filesystem operations on the server data directory**,
  exposed as:
  - **Harbour:** `oads_*` (not `leto_*`)
  - **ACE C API:** `Ads*` names (SAP-style), implemented in `ace32`/`ace64`
- Work over an existing remote connection (same `tcp://` / `tls://` session).
- Work **locally** too (sandbox under the connection data directory) so the
  same app code runs without a server.
- **Security default off:** server rejects ops unless `EnableFileFunc=1`.
- All paths **jailed** under configured data root(s); no escape via `..` or
  absolute paths outside the jail (reuse `platform::resolve_under_*`).
- Full first release: core file ops + directory management + low-level I/O.

## Non-goals (v1)

- Recursive delete of non-empty directory trees.
- Symlink creation or following outside the jail.
- Separate management TCP port or side protocol.
- Binary-compatible LetoDB wire protocol or `leto_*` symbol aliases.
- ACLs finer than “connection authenticated + EnableFileFunc” (optional
  future: require login when file func is on).
- Streaming beyond the existing wire frame payload cap without chunking
  (clients loop `FRead`/`FWrite`).

## Chosen approach — “Ads* + wire, oads_* thin wrappers”

Same pattern as remote `AdsCreateTable` / `AdsDropTable` (1.8.17):

1. Implement / extend **ACE entry points** in `ace_exports.cpp`.
2. If the connection handle is a `RemoteConnection`, send a new **wire
   opcode**; otherwise run against the local connection data directory.
3. On the server, `session.cpp` handles the opcode under the session’s
   resolved data root, after checking `EnableFileFunc`.
4. Harbour `oads_*.prg` (or `adsfunc`-style C wrappers) call the `Ads*` APIs
   and map return codes to xBase `.T./.F.` / sizes / arrays.

Approaches considered and rejected:

- **Studio HTTP / SQL only** — awkward for xBase and streaming I/O.
- **Separate file-ops port** — extra deploy surface; diverges from ADS/Leto
  mental model.

## Public surface

### Harbour (`oads_*`)

| Function | Returns | Notes |
|---|---|---|
| `oads_File( cFileName )` | `.T.`/`.F.` | Existence |
| `oads_FErase( cFileName )` | `.T.`/`.F.` | Delete file |
| `oads_FRename( cOld, cNew )` | `.T.`/`.F.` | Rename/move within jail |
| `oads_FSize( cFileName )` | `n` or `-1` | Size in bytes |
| `oads_FTime( cFileName )` | `cTime` or `""` | Last modified time `"hh:mm:ss"` |
| `oads_FDate( cFileName )` | `dDate` or empty date | Last modified date |
| `oads_Directory( cMask, [cAttr] )` | array | Like Harbour `Directory()` |
| `oads_DirExist( cDir )` | `.T.`/`.F.` | |
| `oads_DirMake( cDir )` | `.T.`/`.F.` | Create directory (parents optional: create intermediate if missing) |
| `oads_DirRemove( cDir )` | `.T.`/`.F.` | Empty directory only |
| `oads_FOpen( cFile, [nMode] )` | `nHandle` or `-1` | |
| `oads_FCreate( cFile, [nAttr] )` | `nHandle` or `-1` | |
| `oads_FClose( nHandle )` | `.T.`/`.F.` | |
| `oads_FRead( nHandle, @cBuf, nBytes )` | `nRead` | |
| `oads_FWrite( nHandle, cBuf, [nBytes] )` | `nWritten` | |
| `oads_FSeek( nHandle, nOffset, [nOrigin] )` | `nPos` or `-1` | Origins: 0=begin, 1=current, 2=end |

Connection: use the **current rddads default** (`AdsConnect60` last handle),
or an optional trailing `hConnect` numeric argument where useful (match
existing `Ads*` optional-connection style in wrappers).

### ACE C (`Ads*`)

Extend existing where present; add new exports otherwise:

| ACE | Maps from oads_* |
|---|---|
| `AdsCheckExistence` | `oads_File` — **make remote-aware + sandboxed** (today ignores conn and uses raw path) |
| `AdsDeleteFile` | `oads_FErase` — **same fix** |
| `AdsRenameFile` (new) | `oads_FRename` |
| `AdsGetFileSize` (new) | `oads_FSize` |
| `AdsGetFileTime` (new) | `oads_FTime` — returns `"hh:mm:ss"` into caller buffer |
| `AdsGetFileDate` (new) | `oads_FDate` — returns `"YYYYMMDD"` or Julian-compatible buffer; Harbour wrapper converts to date |
| `AdsDirectory` (new) | `oads_Directory` — packed entry list or iterative FindFirst/Next; prefer single-shot array payload for remote |
| `AdsDirExist` / `AdsDirMake` / `AdsDirRemove` (new) | dir ops |
| `AdsFOpen` / `AdsFCreate` / `AdsFClose` / `AdsFRead` / `AdsFWrite` / `AdsFSeek` (new) | low-level I/O; handles are `ADSHANDLE` of kind `RemoteFile` / local file registry |

Error codes:

- `AE_SUCCESS` (0)
- `AE_ACCESS_DENIED` (7079) — `EnableFileFunc` off, or path outside jail
- `AE_NO_FILE_FOUND` (5018) — missing file where applicable
- `AE_INVALID_CONNECTION_HANDLE` / `AE_NO_CONNECTION`
- `AE_INSUFFICIENT_BUFFER` — string out-params too small
- `AE_INTERNAL_ERROR` / `AE_REMOTE_ERROR` — I/O and protocol failures

## Security

### Server config (`openads.ini` / CLI)

```ini
[server]
data = /var/openads/data
# Default 0 — all server filesystem opcodes denied
EnableFileFunc = 0
```

CLI mirror: `--enable-file-func` (or `enable_file_func=1` in ini). Document in
`openads.ini.sample` and service docs.

### Path jail

Every path argument is resolved with:

```text
resolve_under_any_root(server_data_roots, client_path)
```

Same rules as Connect / remote create:

- Relative paths join under data root.
- Absolute / drive-rooted client paths are **folded** to relative remainder
  under data (1.8.15 create behaviour), never used as raw OS escape.
- `..` that escapes → `AE_ACCESS_DENIED`.

Local (non-remote) `Ads*` ops use the connection’s data directory the same way
(no raw cwd open for names passed by the app).

### EnableFileFunc gate

- **Remote:** enforced on the server before any FS call. Client may still
  attempt; server returns `AE_ACCESS_DENIED`.
- **Local:** also gated when a connection was opened with a configured
  local data dir and the process is acting as “server-like”; for pure
  in-process apps without a server config, **local file ops under the
  connection data directory are allowed** (developer workstation). Document
  this: the dangerous case is **networked** `openads_serverd`.

### Low-level I/O limits

| Limit | Default | Purpose |
|---|---|---|
| Max open file handles per session | 32 | Resource cap |
| Max single `FRead`/`FWrite` chunk | 1 MiB (and ≤ `kMaxFramePayload`) | Frame safety |
| Handles closed on Disconnect | yes | No leaks across reconnect |
| FOpen share modes | exclusive write default; shared read optional | Avoid corrupting open tables |

Opening a path that is currently an open **table/index** on that session may
return `AE_LOCKED` or `AE_ACCESS_DENIED` (implementation: best-effort check
against open table paths).

## Wire protocol

New opcodes after the CreateTable range (`0xDC`–`0xDF`). Suggested block
`0xE0`–`0xF5` (final numbers assigned in implementation plan; avoid
collisions with any reserved management opcodes).

### Meta / directory

| Opcode | Request | Reply |
|---|---|---|
| `FileExists` | lp_str path | u8 exists |
| `FileErase` | lp_str path | empty ack |
| `FileRename` | lp_str old, lp_str new | empty ack |
| `FileSize` | lp_str path | u64 size LE |
| `FileMTime` | lp_str path | u16 year, u8 mon, u8 day, u8 hh, u8 mm, u8 ss (UTC or local server — **use local server wall time**, document) |
| `Directory` | lp_str mask, u16 attr_flags | u32 count + entries |
| `DirExist` | lp_str path | u8 exists |
| `DirMake` | lp_str path | empty ack |
| `DirRemove` | lp_str path | empty ack |

**Directory entry** (fixed layout per entry):

```text
u8  name_len; char name[name_len];  // or u16 name_len for long names
u64 size LE
u16 year; u8 mon; u8 day; u8 hh; u8 mm; u8 ss
u32 attr LE   // bit0=readonly, bit1=hidden, bit2=system, bit4=directory, bit5=archive
```

Mask semantics: basename wildcards `*` / `?` relative to a directory prefix
(e.g. `subdir/*.dbf`), matching Harbour `Directory()`.

### Low-level I/O

| Opcode | Request | Reply |
|---|---|---|
| `FOpen` | lp_str path, u16 mode | u32 file_id |
| `FCreate` | lp_str path, u16 attr | u32 file_id |
| `FClose` | u32 file_id | empty ack |
| `FRead` | u32 file_id, u32 nbytes | u32 nread + bytes |
| `FWrite` | u32 file_id, u32 nbytes + bytes | u32 nwritten |
| `FSeek` | u32 file_id, i64 offset, u8 origin | i64 position |

`file_id` is session-local (not a raw OS fd on the wire).

**FOpen modes** (bit flags, documented to mirror Harbour `FO_*` where practical):

- bit0 read, bit1 write, bit2 create-if-missing (FCreate path preferred),
- share bits deferred if needed — v1: exclusive for write, shared for read-only.

Errors: opcode `Error` (0xFF) with message + ACE code, same as existing
session `err()` helper.

## Client routing (DLL)

```text
AdsXxx(hConn, …)
  h = resolve connection (0 → rddads_default_connection)
  if RemoteConnection(h):
      send wire opcode; map reply → ACE codes / out-params
  else:
      resolve path under Connection data dir
      std::filesystem / platform file I/O
```

`AdsCheckExistence` and `AdsDeleteFile` **must** stop using raw
`std::filesystem::exists(path)` on the client-supplied string without jail
when a connection is present (security + remote correctness).

## Harbour packaging

- New unit: `contrib/oads_fs/` or `examples/harbour/oads_fs/`:
  - `oads_fs.prg` — all `oads_*` functions
  - `oads_fs.ch` — optional constants (`OADS_FO_READ`, …)
  - `oads_fs.hbp` — links against rddads + ace
- Cookbook page: `docs/en/…` or `cookbook/` “Server filesystem (`oads_*`)”
- Smoke PRG: connect remote → DirMake → FCreate/FWrite → Directory →
  FSize/FDate → FRename → FErase → DirRemove; assert EnableFileFunc off fails

## Server implementation notes

- Config parse: add `EnableFileFunc` / `enable_file_func` next to `data=`,
  `port=`, etc. in serverd config loader.
- Session state: `std::unordered_map<uint32_t, FileHandle>` with
  `std::fstream` or platform `File` abstraction; generate ids from a
  per-session counter.
- Prefer existing `openads::platform` file helpers where present; otherwise
  `std::filesystem` + iostreams is acceptable for v1.
- Logging: optional stderr/txlog line on denied path escape (no path
  content leakage beyond local admin logs).

## Testing

| Layer | Cases |
|---|---|
| Unit path jail | `../escape`, absolute foreign drive, valid relative, multi-root |
| Unit EnableFileFunc | off → ACCESS_DENIED; on → success |
| Unit local Ads* | create/read/write/seek/close under temp data dir |
| Unit directory | mask `*.dbf`, attributes directory bit |
| Integration remote | embedded `Server` like `abi_remote_create_table_test` |
| Live smoke | Windows client → iMac `openads_serverd` with flag on |

## Rollout / versioning

- Target release: next minor after 1.8.17 (e.g. **1.8.18** or **1.9.0** —
  decide at ship time).
- Both client DLL and `openads_serverd` must be upgraded together for remote
  file ops (unknown opcode → clear error).
- `openads.ini.sample` documents `EnableFileFunc=0` default and security
  warning.

## Documentation deliverables

- This design: `docs/superpowers/specs/2026-07-20-oads-server-filesystem-design.md`
- Implementation plan (next): `docs/superpowers/plans/…-oads-server-filesystem.md`
- User-facing: short section in README or `docs/en/` + cookbook example
- CHANGELOG entry when shipped

## Open decisions (resolved)

| Topic | Decision |
|---|---|
| Consumer | ACE C API + Harbour `oads_*` wrappers |
| Naming | `Ads*` C / `oads_*` Harbour (not `leto_*`) |
| Scope v1 | Full Leto-parity set including low-level I/O |
| Architecture | Wire opcodes on existing connection (Approach A) |
| Default security | `EnableFileFunc=0` |

## Success criteria

1. With `EnableFileFunc=0`, remote `oads_File` / `AdsCheckExistence` returns
   access denied (no information leak of existence optional: still deny
   uniformly).
2. With `EnableFileFunc=1`, client can create a text file on the server via
   `oads_FCreate`/`FWrite`, list it with `oads_Directory`, read size/date,
   rename, erase; no file appears next to the client app.
3. Path `..\Windows\System32\...` (or POSIX equivalent) never touches outside
   the data root.
4. Disconnect closes all server-side file handles for that session.
5. Unit + remote integration tests green on Windows; live smoke vs iMac
   optional but recommended before release.
