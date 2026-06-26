---
title: Wire protocol
layout: default
parent: Home (EN)
nav_order: 4
permalink: /en/wire-protocol/
---

# OpenADS Wire Protocol — v1.4.0

This document specifies the OpenADS-native wire protocol spoken
between an OpenADS client (`ace64.dll` opened with a
`tcp://host:port/<dir>` URI) and an OpenADS server
(`tools/serverd/openads_serverd` or the `network::Server` library
embedded in another process).

The protocol is **not byte-compatible** with the proprietary
Advantage Database Server remote protocol. OpenADS implements its
own clean-room wire format because publishing or implementing the
SAP-owned protocol would require disassembly or other material
covered by the Advantage SDK / ACE EULA.

This spec is the canonical reference for downstream consumers
that want to write a non-C++ client (Python, Go, Rust, Harbour
extension hosts) without reading the C++ source. The on-the-wire
byte layout has only grown — opcode bytes are stable; new opcodes
get appended.

---

## 1. Transport

- **TCP/IP** over an arbitrary port (no IANA allocation; the
  server binds to whatever its CLI / API caller picks). The
  reference daemon (`openads_serverd`) defaults to
  `127.0.0.1:6262`.
- **Plaintext** (`tcp://...`) or **TLS** (`tls://...`). The TLS
  transport landed in v0.4.0 (M12.12 / M12.13) via vendored
  `mbedtls 3.6 LTS` (Apache-2.0, statically linked since v1.0.0-rc8 —
  no runtime `libssl` / `libcrypto` / `mbedtls` DLL dependency).
- **No multiplexing.** One connection = one session = one logical
  database connection. Statements + cursors are scoped to the
  session; multiple parallel SQL queries on the same TCP
  connection are serialised by the client mutex.
- **Nagle disabled** (`TCP_NODELAY`, M12.20 / v1.0.0-rc18) — the
  wire is strict ping-pong, so Nagle's accumulation delay was pure
  latency tax.
- **No multiplexing.** One connection = one session = one logical
  database connection. Statements + cursors are scoped to the
  session; multiple parallel SQL queries on the same TCP
  connection are serialised by the client mutex.

## 2. Frame layout

Every message is a single frame:

```
+--------+--------+--------+--------+--------+--------+ ... +--------+
|     payload length (BE u32)        | opcode |    payload bytes      |
+--------+--------+--------+--------+--------+--------+ ... +--------+
   bytes 0..3 (length)                  byte 4    bytes 5..(4+len)
```

- **`payload length`** — 32-bit unsigned, **big-endian**, counts only
  the payload bytes (excludes the 5-byte header). 0 ⇒ no payload.
- **`opcode`** — 8-bit unsigned. See §4 for the full list.
- **`payload`** — opcode-specific. Numeric integers inside the
  payload are **little-endian** unless explicitly noted (e.g. the
  4-byte BE length in the header). Strings are raw UTF-8 / OEM
  bytes with no NUL terminator unless an explicit length prefix
  precedes them.

## 3. Session lifecycle

```
client                                 server
  |                                       |
  |--Hello---------------------- --------->|
  |<------------------ ---------HelloAck---|   (banner = "openads/<ver>")
  |                                       |
  |--Connect(dir,user,pw)----------------->|
  |<-------------------- -----ConnectAck---|   ("connected:<dir>")
  |                                       |
  |  ... opcode pairs (OpenTable / SQL /  |
  |      Fetch / Skip / GetField / ...)   |
  |                                       |
  |--Disconnect--------------------------->|
  |   (server closes socket)              |
```

**Hello** is optional from a strict-protocol point of view (the
reference client skips it and goes straight to Connect), but the
server always answers with the banner if asked.

**Connect** is mandatory before any table / SQL op. After
ConnectAck the session has an `engine::Connection` open against
the requested data dir.

**Disconnect** triggers an immediate server-side close with full
cleanup (cursors, ABI statement, ABI connection). A peer-close
without Disconnect also runs cleanup.

## 4. Opcodes

The byte values are stable; new opcodes only get appended.

The table below is generated from the canonical `Opcode` enum in
`src/network/wire.h`. Opcodes are listed in hex order; note the
byte values are **not** contiguous with milestone order — later
milestones reused gaps left by earlier ones.

| Op | Hex | Direction | Meaning | Milestone |
|----|-----|-----------|---------|-----------|
| `Hello`               | `0x01` | C→S | Banner request                  | M12.3 |
| `HelloAck`            | `0x02` | S→C | Banner reply                    | M12.3 |
| `Connect`             | `0x10` | C→S | Open session                    | M12.3 |
| `ConnectAck`          | `0x11` | S→C | Session opened                  | M12.3 |
| `Disconnect`          | `0x12` | C→S | Close session                   | M12.3 |
| `OpenTable`           | `0x20` | C→S | Open a DBF/CDX/NTX              | M12.4 |
| `OpenTableAck`        | `0x21` | S→C | Returns wire table-id           | M12.4 |
| `CloseTable`          | `0x22` | C→S | Close table                     | M12.4 |
| `CloseTableAck`       | `0x23` | S→C |                                 | M12.4 |
| `ExecuteSQL`          | `0x30` | C→S | Run SQL statement               | M12.7 |
| `ExecuteSQLAck`       | `0x31` | S→C | Returns cursor table-id (or 0)  | M12.7 |
| `Fetch`               | `0x32` | C→S | Batch row read                  | M12.11 |
| `FetchAck`            | `0x33` | S→C | Row matrix                      | M12.11 |
| `GotoTop`             | `0x40` | C→S |                                 | M12.4 |
| `GotoTopAck`          | `0x41` | S→C |                                 | M12.4 |
| `Skip`                | `0x42` | C→S | Skip ±N rows                    | M12.4 |
| `SkipAck`             | `0x43` | S→C |                                 | M12.4 |
| `GetField`            | `0x44` | C→S | Read one column at cursor       | M12.4 |
| `GetFieldAck`         | `0x45` | S→C | Column bytes                    | M12.4 |
| `GetRecordCount`      | `0x46` | C→S |                                 | M12.4 |
| `GetRecordCountAck`   | `0x47` | S→C |                                 | M12.4 |
| `AtEOF`               | `0x48` | C→S |                                 | M12.4 |
| `AtEOFAck`            | `0x49` | S→C | 0 / 1 byte                      | M12.4 |
| `DescribeTable`       | `0x4A` | C→S | Schema in one round-trip        | M12.14 |
| `DescribeTableAck`    | `0x4B` | S→C | Column list + types             | M12.14 |
| `AtBOF`               | `0x4C` | C→S |                                 | M12.14 |
| `AtBOFAck`            | `0x4D` | S→C | 0 / 1 byte                      | M12.14 |
| `GetRecordNum`        | `0x4E` | C→S | Current recno                   | M12.14 |
| `GetRecordNumAck`     | `0x4F` | S→C |                                 | M12.14 |
| `AppendBlank`         | `0x50` | C→S |                                 | M12.6 |
| `AppendBlankAck`      | `0x51` | S→C |                                 | M12.6 |
| `SetField`            | `0x52` | C→S | Write one column at cursor      | M12.6 |
| `SetFieldAck`         | `0x53` | S→C |                                 | M12.6 |
| `DeleteRecord`        | `0x54` | C→S | Mark deleted                    | M12.6 |
| `DeleteRecordAck`     | `0x55` | S→C |                                 | M12.6 |
| `RecallRecord`        | `0x56` | C→S | Undelete                        | M12.6 |
| `RecallRecordAck`     | `0x57` | S→C |                                 | M12.6 |
| `GotoRecord`          | `0x58` | C→S | Jump to recno                   | M12.6 |
| `GotoRecordAck`       | `0x59` | S→C |                                 | M12.6 |
| `FlushTable`          | `0x5A` | C→S | Force write-through             | M12.6 |
| `FlushTableAck`       | `0x5B` | S→C |                                 | M12.6 |
| `Reindex`             | `0x60` | C→S | Rebuild bound indexes           | M12.8 |
| `ReindexAck`          | `0x61` | S→C |                                 | M12.8 |
| `IsRecordDeleted`     | `0x62` | C→S |                                 | M12.14 |
| `IsRecordDeletedAck`  | `0x63` | S→C | 0 / 1 byte                      | M12.14 |
| `GotoBottom`          | `0x64` | C→S |                                 | M12.14 |
| `GotoBottomAck`       | `0x65` | S→C |                                 | M12.14 |
| `IsFound`             | `0x66` | C→S | Seek-hit flag                   | M12.15 |
| `IsFoundAck`          | `0x67` | S→C |                                 | M12.15 |
| `RefreshRecord`       | `0x68` | C→S | Re-read current record          | M12.15 |
| `RefreshRecordAck`    | `0x69` | S→C |                                 | M12.15 |
| `GetTableType`        | `0x6A` | C→S | DBF / CDX / NTX kind            | M12.15 |
| `GetTableTypeAck`     | `0x6B` | S→C |                                 | M12.15 |
| `GetRecordLength`     | `0x6C` | C→S |                                 | M12.15 |
| `GetRecordLengthAck`  | `0x6D` | S→C |                                 | M12.15 |
| `GetNumIndexes`       | `0x6E` | C→S |                                 | M12.15 |
| `GetNumIndexesAck`    | `0x6F` | S→C |                                 | M12.15 |
| `GetLastAutoinc`      | `0x70` | C→S | Last autoinc value              | M12.15 |
| `GetLastAutoincAck`   | `0x71` | S→C |                                 | M12.15 |
| `LockRecord`          | `0x72` | C→S | Single-record byte-range lock   | M12.15 |
| `LockRecordAck`       | `0x73` | S→C |                                 | M12.15 |
| `UnlockRecord`        | `0x74` | C→S |                                 | M12.15 |
| `UnlockRecordAck`     | `0x75` | S→C |                                 | M12.15 |
| `LockTable`           | `0x76` | C→S | Whole-table lock                | M12.15 |
| `LockTableAck`        | `0x77` | S→C |                                 | M12.15 |
| `UnlockTable`         | `0x78` | C→S |                                 | M12.15 |
| `UnlockTableAck`      | `0x79` | S→C |                                 | M12.15 |
| `PackTable`           | `0x7A` | C→S | Compact deleted rows            | M12.15 |
| `PackTableAck`        | `0x7B` | S→C |                                 | M12.15 |
| `ZapTable`            | `0x7C` | C→S | Empty table                     | M12.15 |
| `ZapTableAck`         | `0x7D` | S→C |                                 | M12.15 |
| `FlushFileBuffers`    | `0x7E` | C→S | fsync table + index files       | M12.15 |
| `FlushFileBuffersAck` | `0x7F` | S→C |                                 | M12.15 |
| `CloseAllIndexes`     | `0x80` | C→S |                                 | M12.15 |
| `CloseAllIndexesAck`  | `0x81` | S→C |                                 | M12.15 |
| `SetAOF`              | `0x82` | C→S | Install Rushmore filter         | M12.15 |
| `SetAOFAck`           | `0x83` | S→C | OptLevel + opt-bitmap meta      | M12.15 |
| `ClearAOFRemote`      | `0x84` | C→S | Drop the installed AOF          | M12.15 |
| `ClearAOFRemoteAck`   | `0x85` | S→C |                                 | M12.15 |
| `GetAOFOptLevel`      | `0x86` | C→S |                                 | M12.15 |
| `GetAOFOptLevelAck`   | `0x87` | S→C | FULL / PART / NONE              | M12.15 |
| `OpenIndex`           | `0x88` | C→S | Open `.cdx` / `.ntx` index      | M12.16 |
| `OpenIndexAck`        | `0x89` | S→C | Wire index-id                   | M12.16 |
| `CloseIndex`          | `0x8A` | C→S |                                 | M12.16 |
| `CloseIndexAck`       | `0x8B` | S→C |                                 | M12.16 |
| `SetOrder`            | `0x8C` | C→S | Switch active order by handle   | M12.16 |
| `SetOrderAck`         | `0x8D` | S→C |                                 | M12.16 |
| `SetOrderByName`      | `0x8E` | C→S | Switch active order by tag name | M12.16 |
| `SetOrderByNameAck`   | `0x8F` | S→C |                                 | M12.16 |
| `Seek`                | `0x90` | C→S | Index key seek (hit / miss)     | M12.16 |
| `SeekAck`             | `0x91` | S→C | Found flag + recno              | M12.16 |
| `SeekLast`            | `0x92` | C→S | Seek last matching key          | M12.16 |
| `SeekLastAck`         | `0x93` | S→C |                                 | M12.16 |
| `CreateIndex`         | `0x94` | C→S | CDX-on-the-wire `CREATE INDEX`  | M12.16 |
| `CreateIndexAck`      | `0x95` | S→C |                                 | M12.16 |
| `SkipUnique`          | `0x96` | C→S | Skip to next unique key         | M12.16 |
| `SkipUniqueAck`       | `0x97` | S→C |                                 | M12.16 |
| `SetScope`            | `0x98` | C→S | Set index key-range scope       | M12.16 |
| `SetScopeAck`         | `0x99` | S→C |                                 | M12.16 |
| `ClearScope`          | `0x9A` | C→S |                                 | M12.16 |
| `ClearScopeAck`       | `0x9B` | S→C |                                 | M12.16 |
| `FetchCurrentRow`     | `0x9C` | C→S | Read whole current row          | M12.17 |
| `FetchCurrentRowAck`  | `0x9D` | S→C | Full record buffer              | M12.17 |
| `GetLastTableUpdate`  | `0x9E` | C→S | DBF header last-update stamp    | M12.24 |
| `GetLastTableUpdateAck` | `0x9F` | S→C | Date as `YYYYMMDD` bytes       | M12.24 |
| `MgConnect`           | `0xA0` | C→S | Open management telemetry channel | M9.25 (rc24) |
| `MgConnectAck`        | `0xA1` | S→C | Channel opened / reachability ack | M9.25 (rc24) |
| `MgRequest`           | `0xA2` | C→S | Request a telemetry snapshot    | M9.25 (rc24) |
| `MgReplyAck`          | `0xA3` | S→C | `MgSnapshot` payload            | M9.25 (rc24) |
| `FetchWhere`          | `0xA4` | C→S | Server-side filtered batch scan | Tier-2 |
| `FetchWhereAck`       | `0xA5` | S→C | Matching-row matrix + EOF flag  | Tier-2 |
| `Aggregate`           | `0xA6` | C→S | Server-side COUNT/SUM/AVG/MIN/MAX | Tier-3 |
| `AggregateAck`        | `0xA7` | S→C | One scalar per requested aggregate | Tier-3 |
| `Error`               | `0xFF` | S→C | Any failure (4-byte ACE-code prefix since M12.10) | M12.3 |

## 5. Payload formats

Notation:
- `u8`, `u16`, `u32` — unsigned little-endian unless noted.
- `len-prefixed string` — `[u16 byte_length][bytes...]` (M12.9
  Connect frame uses this form for dir/user/pw).
- `bytes` — raw, length implied by frame length.

### 5.1 Hello / HelloAck
- Hello: empty.
- HelloAck: `bytes` — server banner, e.g. `openads/1.0.0-rc25`.
  Since v1.0.0-rc13 the banner is driven from `git describe`, so
  it always reflects the actual build.

### 5.2 Connect / ConnectAck
- Connect: `[u16 dlen][dir][u16 ulen][user][u16 plen][password]`
  (M12.9 — `user` and `password` may be empty if the server
  doesn't require auth).
- ConnectAck: `bytes` — `connected:<dir>` (informational).

### 5.3 Disconnect
- C→S only. Empty payload. No ack — server closes the socket.

### 5.4 OpenTable / OpenTableAck
- OpenTable: `bytes` — table leaf path (e.g. `data.dbf`),
  resolved against the session's data dir.
- OpenTableAck: `[u32 wire_table_id]` — opaque to the client;
  every subsequent table op echoes this id.

### 5.5 CloseTable / CloseTableAck
- CloseTable: `[u32 wire_table_id]`.
- Ack: empty.

### 5.6 ExecuteSQL / ExecuteSQLAck
- ExecuteSQL: `bytes` — raw SQL text, ASCII / UTF-8.
- ExecuteSQLAck: `[u32 cursor_id]` — `0` for non-SELECT
  (INSERT / UPDATE / DELETE / DDL), otherwise a wire table-id
  the client uses with the read-side ops below.

### 5.7 Fetch / FetchAck
- Fetch: `[u32 tid][u32 max_rows][u8 ncols][per col: u8 nlen, name]`.
  Walks `max_rows` rows from the cursor's current position; works
  for both engine handles (returned by `OpenTable`) and SQL
  cursor handles (returned by `ExecuteSQL`).
- FetchAck: `[u32 nrows][u8 ncols][per row, per col: u16 vlen, val_bytes]`.
  Rows are emitted in cursor order; column order matches the
  request. `nrows` is the number actually returned; may be less
  than `max_rows` (EOF or skip failure stops the walk early).

### 5.8 GotoTop / GotoTopAck, GotoBottom / GotoBottomAck, Skip / SkipAck, GotoRecord / GotoRecordAck
- GotoTop / GotoBottom: `[u32 tid]`.
- Skip: `[u32 tid][u32 step_le]` (`step` is signed; transmit as
  little-endian raw u32 bits).
- GotoRecord: `[u32 tid][u32 recno]`.
- Acks **carry a row trailer** since M12.18 (v1.0.0-rc18):
  `[u32 recno][u8 deleted][u32 row_buf_len][row_buf bytes]`. The
  trailer is empty (length 0) only when the cursor lands at EOF /
  Limbo. Clients that pre-date M12.18 can ignore extra bytes past
  the prior 0-length frame — the wire codec passes the full payload
  through.

### 5.9 GetField / GetFieldAck
- GetField: `[u32 tid][bytes field_name]` (no length prefix —
  field name runs to end of payload).
- Ack: `bytes` — column value as the engine's textual rendering
  (DBF columns are textually formatted on disk; this is the same
  byte stream `AdsGetField` returns locally, including trailing
  blank-padding for fixed-width columns).

### 5.10 GetRecordCount / GetRecordCountAck, AtEOF / AtEOFAck
- GetRecordCount: `[u32 tid]`. Ack: `[u32 record_count]`.
- AtEOF: `[u32 tid]`. Ack: 1 byte (`0` = not EOF, `1` = EOF).

### 5.11 AppendBlank, DeleteRecord, RecallRecord, FlushTable, Reindex, Pack, Zap
- All seven: `[u32 tid]`, ack empty.
- `AppendBlank` since M12.23 / v1.0.0-rc19 auto-acquires a record
  byte-range lock on the new row (ACE semantics for non-exclusive
  tables — X#'s `GoHot` refuses to write a record it sees as
  unlocked).

### 5.12 SetField / SetFieldAck
- SetField: `[u32 tid][u16 namelen][name_bytes][value_bytes]`.
  Value runs from `5 + namelen` to end of payload. The engine
  applies the textual representation through
  `Table::set_field(idx, std::string)`, which handles all
  field types (C / N / D / L / M / V / Q / I / Y / B).
- Ack: empty.

### 5.13 Error
- S→C only. Layout (M12.10 onwards):
  `[u32 ace_code_le][message_bytes]`.
- `ace_code` is one of the constants from `include/openads/error.h`
  (e.g. `5004` AE_FUNCTION_NOT_AVAILABLE, `5018`
  AE_NO_FILE_FOUND, `5066` AE_TABLE_NOT_FOUND, `7077`
  AE_LOGIN_FAILED, `7200` AE_PARSE_ERROR).
- `message` is a human-readable diagnostic; not stable across
  versions, only for debugging / logs.

### 5.14 DescribeTable / DescribeTableAck (M12.14)
- DescribeTable: `[u32 tid]`.
- Ack: `[u8 ncols][per col: u8 nlen, name, u8 type, u16 len, u8 dec]`.
- Client caches the result on `RemoteTable` so the entire field-
  metadata API (`AdsGetNumFields`, `AdsGetFieldName/Type/Length/
  Decimals`) costs one round-trip per opened table.

### 5.15 FetchCurrentRow / FetchCurrentRowAck (M12.17)
- C→S: `[u32 tid]`.
- Ack: same row-trailer layout as the navigation acks (§5.8):
  `[u32 recno][u8 deleted][u32 row_buf_len][row_buf bytes]`.
- The client caches the row buffer and serves every cell read
  (`AdsGetField` / `AdsGetLong` / `AdsGetDouble` / `AdsGetJulian`)
  out of the cache until the next navigation, collapsing W cells
  per row to 1 RTT.

### 5.16 Lock / Unlock (M12.15)
- `LockRecord` / `UnlockRecord`: `[u32 tid][u32 recno]`. `recno == 0`
  means "current record" (M12.23 / rc19).
- `LockTable` / `UnlockTable`: `[u32 tid]`. Ack empty.

### 5.17 SetAOF / ClearAOF / GetAOFOptLevel (M12.15)
- `SetAOF`: `[u32 tid][bytes cond]` — cond is the Clipper-style
  AOF expression text (`TAG = 'AAAA'`, `AGE BETWEEN 25 AND 40`, …).
- `SetAOFAck`: `[u32 opt_level]` (`0` NONE, `1` PART, `2` FULL).
- `ClearAOF`: `[u32 tid]`, ack empty.
- Non-optimisable expressions (since M12.24 / rc21) return
  `opt_level = 0` rather than an Error frame, matching ACE.

### 5.18 OpenIndex / CloseIndex / Seek / CreateIndex (M12.16, M12.16b)
- `OpenIndex`: `[u32 tid][bytes index_path]`.
- `OpenIndexAck`: `[u32 wire_index_id]`. Server promotes an ABI
  index handle in `tbls_h` and syncs the engine cursor.
- `CloseIndex`: `[u32 wire_index_id]`, ack empty.
- `Seek`: `[u32 tid][u32 hindex][u8 soft][u8 last][u16 klen][key]`.
- `SeekAck`: `[u8 found][u32 recno]`.
- `CreateIndex`: `[u32 tid][u16 tlen][tag][u16 elen][expr][u32 flags]`
  (flags = `ADS_DESCENDING` / `ADS_UNIQUE` / `ADS_COMPOUND` /
  `ADS_DOUBLEKEY`).
- Ack: empty.

### 5.19 SetOrder / SetOrderByName (M12.16c)
- `SetOrder`: `[u32 tid][u32 hindex]` — `hindex == 0` clears order.
- `SetOrderByName`: `[u32 tid][bytes tag_name]`.
- Both acks empty.

### 5.20 GetLastTableUpdate / Ack (M12.24)
- C→S: `[u32 tid]`.
- Ack: `[bytes date_str]` — 8 bytes `YYYYMMDD` (raw, no format
  applied; client renders via the process-wide date format set by
  `AdsSetDateFormat`).

### 5.21 MgConnect / MgRequest (M9.25, rc24)

The management telemetry channel — what the `AdsMg*` ABID
functions and `tools/mgprobe` speak to a remote `openads_serverd`.

- `MgConnect`: `[bytes server]` — the `host:port` (or drive path)
  passed to `AdsMgConnect`. `MgConnectAck` is empty on success;
  the handshake doubles as an eager reachability probe.
- `MgRequest`: `[u8 kind][u16 arg]` — always 3 bytes. `kind`:
  `0x01` Snapshot (full `MgSnapshot`, covers every `Get*`),
  `0x02` KillUser (`arg` = connection number), `0x03`
  ResetCommStats, `0x04` DumpTables.
- `MgReplyAck`: a fully serialized `MgSnapshot`, little-endian —
  live counts (connections / work areas / tables / users /
  worker threads), per-entity lists, process RSS, listener port,
  and the cumulative `MgStats` (uptime, comm packet totals,
  server-initiated disconnects, high-water marks).
- An unknown `MgRequestKind` is answered with `Error` (`0xFF`).

### 5.22 FetchWhere / FetchWhereAck (Tier-2)

Server-side filtered scan. Where `Fetch` (§5.7) returns every row in
cursor order, `FetchWhere` evaluates a Clipper-style `FOR` predicate
against each row **on the server** and returns only the matching rows.
It walks the table from the cursor's current position until it has
collected `max_rows` matches or hits EOF, leaving the cursor positioned
past the last examined row so a follow-up `FetchWhere` resumes the scan.

This collapses a `SET FILTER` / `COUNT FOR` / `LOCATE FOR` scan whose
predicate falls outside the index-optimisable AOF subset (§5.17) — which
a navigational client would otherwise satisfy by reading every record
over the wire and filtering locally — down to `ceil(matches / max_rows)`
round-trips.

- `FetchWhere`:
  `[u32 tid][u32 max_rows][u16 exprlen][expr][u8 ncols][per col: u8 nlen, name]`.
  `expr` is the FOR-predicate text (e.g. `AGE > 40 .AND. CITY = 'RIO'`),
  evaluated with the same engine evaluator used for CDX `FOR` index
  conditions. It supports field / number / string-literal operands, the
  comparison operators `== != <> # >= <= > < =`, the boolean operators
  `.AND. .OR. .NOT.` (and `!`), and the key-expression functions
  (`UPPER`, `LTRIM`, `STR`, `SUBSTR`, …). An empty or unparseable
  predicate is permissive (every row passes), matching FOR-clause
  semantics — so callers that need strict filtering should validate the
  expression up front.
- `FetchWhereAck`:
  `[u32 nrows][u8 ncols][per row, per col: u16 vlen, val][u8 eof]`.
  Identical row matrix to `FetchAck`, plus a trailing `eof` byte
  (`1` = the scan reached end-of-table). Because the scan only stops
  early on reaching `max_rows` matches, a batch with `nrows < max_rows`
  always implies `eof == 1`.
- **Base tables only.** A `FetchWhere` against a SQL cursor id
  (from `ExecuteSQL`) returns `Error` — a SQL cursor already filters
  server-side through its own `WHERE` clause.

### 5.23 Aggregate / AggregateAck (Tier-3)

Server-side aggregation. Where `FetchWhere` (§5.22) streams the matching
rows back, `Aggregate` folds them **on the server** into scalar
accumulators and returns only the results. The server scans the whole
table once (independent of, and restoring, the cursor position),
evaluates the `FOR` predicate per row with the same evaluator as §5.22,
and feeds each match into the requested `COUNT` / `SUM` / `AVG` / `MIN` /
`MAX` accumulators. This collapses a `COUNT FOR` / `SUM .. FOR` /
`AVERAGE` / totalling report from one round-trip per matched row (or a
whole `FetchWhere` row matrix) down to a **single** round-trip carrying
just the scalars.

- `Aggregate`:
  `[u32 tid][u16 forlen][for_expr][u8 n_aggs][per agg: u8 fn_type, u8 nlen, field_name]`.
  `for_expr` is the FOR predicate (empty = every row). `fn_type` is
  `0=COUNT 1=SUM 2=AVG 3=MIN 4=MAX`; `field_name` is the column to fold
  (`nlen = 0` ⇒ `COUNT(*)`). SUM/AVG use the field's numeric value;
  MIN/MAX compare numerically for numeric field types and
  lexicographically (raw bytes) otherwise. A request may carry several
  aggregates so one scan answers `COUNT`+`SUM`+`MIN`+`MAX` together.
- `AggregateAck`:
  `[u8 n_aggs][per agg: u8 result_type, u16 vlen, val]`, one entry per
  requested aggregate, same order. `result_type` is `0=empty/null`
  (zero matched rows for AVG/MIN/MAX), `1=numeric` (ASCII decimal, parse
  with `VAL()`), `2=string` (raw field bytes). `COUNT` and `SUM` over zero
  rows return numeric `0`.
- **Base tables only.** An `Aggregate` against a SQL cursor id returns
  `Error` — a SQL cursor already aggregates through its own SQL.
- **Capability-gated.** A client advertises `kCapAggregate` (`0x02`) in
  the Connect capability word; it must only send `0xA6` to a server that
  understands it (older servers never receive the frame).

## 6. Versioning

- This spec covers OpenADS **v1.4.0**. Bumps append new
  opcodes and document them here without breaking existing ones.
- Clients can probe the server version via `Hello` → the banner
  string is `openads/<semver>`.

## 7. Error handling expectations

- Any frame may be replied with `Error` (`0xFF`). Clients must
  parse the 4-byte ACE-code prefix before treating the rest as
  message text.
- A **peer-closed connection** mid-frame is treated as
  `AE_INTERNAL_ERROR` 5000 with message `peer closed connection`
  — the wire layer bubbles this up via `recv_exact`.
- `AE_FUNCTION_NOT_AVAILABLE` 5004 from `Connect` means the URI
  scheme isn't supported, or the server was built without the
  matching transport (rare since TLS shipped in v0.4.0).

## 8. Reference impls

- **Server**: `src/network/server.{h,cpp}` plus the standalone
  `tools/serverd/openads_serverd` CLI.
- **Client**: `src/network/client.{h,cpp}` (`RemoteConnection`)
  + the dual-mode dispatch in `src/abi/ace_exports.cpp`'s
  `AdsConnect60` for the public `tcp://` / `tls://` URI
  integration.
- **Transport abstraction**: `src/network/transport.h` defines
  the `ITransport` polymorphic surface (M12.13). Concrete impls:
  `PlainTransport` (TCP) and `TlsTransport` (mbedtls, vendored
  statically since v1.0.0-rc8).
- **Wire codec**: `src/network/wire.{h,cpp}` (frame
  encode / decode + `Opcode` enum).

---

## 9. Data Dictionary API

The Data Dictionary (DD) layer sits **above** the wire — there are
no dedicated DD wire opcodes. DD calls in the public ABI
(`AdsDDCreate*`, `AdsDDGet/Set*Property`, etc.) operate on the
server-side `engine::DataDict` object owned by the session's
`Connection`, which loaded it from the `.add` file at `Connect`
time. Over a remote connection the call is dispatched through the
`AdsConnect60` dual-mode layer into the server process, which
then mutates the in-memory DD and persists atomically.

### 9.1 Dictionary lifecycle

| Function | Parameters | Purpose |
|----------|-----------|---------|
| `AdsDDCreate` | `pucDictionary`, `bEncrypt`, `pucAdminPassword`, `phConnect*` | Create a new `.add` file and return an open connection handle. `bEncrypt` must be `ADS_FALSE` (encryption not yet implemented). |
| `AdsDDOpen` | `pucDictionary`, `pucPassword`, `phConnect*` | Open an existing dictionary (alias for `AdsConnect60` with a `.add` path). |

### 9.2 Table registration

| Function | Parameters | Purpose |
|----------|-----------|---------|
| `AdsDDAddTable` | `hConnect`, `pucAlias`, `pucTablePath`, `usFileType`, `usCharType`, `pucIndexPath`, `pucComment` | Register a table alias in the DD. `usFileType`: `ADS_ADT=3`, `ADS_CDX=4`, `ADS_NTX=2`. `pucIndexPath` may be `NULL`. |
| `AdsDDRemoveTable` | `hConnect`, `pucAlias`, `usDeleteFiles` | Remove a table alias. `usDeleteFiles=ADS_TRUE` deletes the physical files. |

### 9.3 Table properties (`AdsDDGetTableProperty` / `AdsDDSetTableProperty`)

Both take `hConnect`, `pucTableName`, `usPropertyID`, `pvProperty`, `pusPropertyLen`.

| Constant | Value | Type | Description |
|----------|-------|------|-------------|
| `ADS_DD_TABLE_VALIDATION_EXPR` | 200 | string | Server-evaluated validation expression |
| `ADS_DD_TABLE_VALIDATION_MSG`  | 201 | string | Message returned when validation fails |
| `ADS_DD_TABLE_PRIMARY_KEY`     | 202 | string | Comma-separated PK field names |
| `ADS_DD_TABLE_AUTO_CREATE`     | 203 | u16   | `ADS_TRUE` → create physical file if absent |
| `ADS_DD_TABLE_TYPE`            | 204 | u16   | File type (`ADS_ADT`, `ADS_CDX`, `ADS_NTX`) |
| `ADS_DD_TABLE_PATH`            | 205 | string | Resolved absolute path to the DBF |
| `ADS_DD_TABLE_FIELD_COUNT`     | 206 | u16   | Number of fields (read-only) |
| `ADS_DD_TABLE_OBJ_ID`          | 208 | u32   | Internal object ID (read-only) |
| `ADS_DD_TABLE_RELATIVE_PATH`   | 211 | string | Path as stored in the `.add` (relative or absolute) |
| `ADS_DD_TABLE_CHAR_TYPE`       | 212 | u16   | OEM / ANSI character encoding |
| `ADS_DD_TABLE_DEFAULT_INDEX`   | 213 | string | Default index tag to set on open |
| `ADS_DD_TABLE_PERMISSION_LEVEL`| 216 | u16   | Minimum privilege required to open (`ADS_DD_TABLE_PERMISSION_*`) |

`ADS_DD_TABLE_PERMISSION_*` values:

| Constant | Value | Meaning |
|----------|-------|---------|
| `ADS_DD_TABLE_PERMISSION_NONE`   | 0 | No restriction |
| `ADS_DD_TABLE_PERMISSION_READ`   | 1 | Read only |
| `ADS_DD_TABLE_PERMISSION_WRITE`  | 2 | Read + update |
| `ADS_DD_TABLE_PERMISSION_DELETE` | 3 | Read + update + delete |
| `ADS_DD_TABLE_PERMISSION_FULL`   | 4 | Full DML including INSERT |

### 9.4 Field properties (`AdsDDGetFieldProperty` / `AdsDDSetFieldProperty`)

Both take `hConnect`, `pucTableName`, `pucFieldName`, `usPropertyID`, `pvProperty`, `pusPropertyLen`.

| Constant | Value | Type | Description |
|----------|-------|------|-------------|
| `ADS_DD_FIELD_NAME`            | 301 | string | Field name |
| `ADS_DD_FIELD_TYPE`            | 302 | string | Type character (`C`, `N`, `D`, `L`, `M`, `I`, `V`, `Q`, `Y`, `B`, `W`) |
| `ADS_DD_FIELD_LENGTH`          | 303 | u16   | Column width in bytes |
| `ADS_DD_FIELD_DECIMAL`         | 304 | u16   | Decimal digits (numeric fields) |
| `ADS_DD_FIELD_REQUIRED`        | 305 | u16   | `ADS_TRUE` → NULL / blank rejected by engine |
| `ADS_DD_FIELD_DEFAULT`         | 306 | string | Default value expression |
| `ADS_DD_FIELD_VALIDATION_RULE` | 307 | string | Per-field validation expression |
| `ADS_DD_FIELD_VALIDATION_MSG`  | 308 | string | Message on validation failure |
| `ADS_DD_FIELD_COMMENT`         | 309 | string | Free-text comment |

### 9.5 Index properties (`AdsDDGetIndexProperty` / `AdsDDSetIndexProperty`)

Both take `hConnect`, `pucTableName`, `pucTagName`, `usPropertyID`, `pvProperty`, `pusPropertyLen`.

| Constant | Value | Type | Description |
|----------|-------|------|-------------|
| `ADS_DD_INDEX_FILE_NAME`  | 401 | string | Bound `.cdx` / `.ntx` file name |
| `ADS_DD_INDEX_EXPR`       | 402 | string | Key expression |
| `ADS_DD_INDEX_UNIQUE`     | 403 | u16   | `ADS_TRUE` → unique key |
| `ADS_DD_INDEX_DESCENDING` | 404 | u16   | `ADS_TRUE` → descending sort |
| `ADS_DD_INDEX_CONDITION`  | 405 | string | FOR condition expression |
| `ADS_DD_INDEX_KEY_LENGTH` | 406 | u16   | Compiled key width in bytes |
| `ADS_DD_INDEX_TYPE`       | 407 | u16   | `ADS_CDX` / `ADS_NTX` constant |
| `ADS_DD_INDEX_FILE_TYPE`  | 408 | u16   | Same as `ADS_DD_INDEX_TYPE` |

Index file management:

| Function | Parameters | Purpose |
|----------|-----------|---------|
| `AdsDDAddIndexFile`    | `hConnect`, `pucTableName`, `pucIndexFile`, `pucComment` | Bind an existing `.cdx` / `.ntx` to the table alias |
| `AdsDDRemoveIndexFile` | `hConnect`, `pucTableName`, `pucIndexFile`, `usDeleteFile` | Unbind (and optionally delete) an index file |

### 9.6 Database properties (`AdsDDGetDatabaseProperty` / `AdsDDSetDatabaseProperty`)

Both take `hConnect`, `usPropertyID`, `pvProperty`, `pusPropertyLen`.

| Constant | Value | Type | Description |
|----------|-------|------|-------------|
| `ADS_DD_COMMENT`               | 1  | string | Free-text database description |
| `ADS_DD_ADMIN_PASSWORD`        | 2  | string | Write-only; sets the `adssys` password |
| `ADS_DD_DEFAULT_TABLE_PATH`    | 3  | string | Default directory for new table files |
| `ADS_DD_TEMP_TABLE_PATH`       | 4  | string | Scratch directory for temp tables |
| `ADS_DD_LOG_IN_REQUIRED`       | 5  | u16   | `ADS_TRUE` → reject anonymous connects |
| `ADS_DD_VERIFY_ACCESS_RIGHTS`  | 6  | u16   | `ADS_TRUE` → enforce table-level permissions |
| `ADS_DD_ENCRYPT_NEW_TABLE`     | 7  | u16   | Encrypt tables on creation |
| `ADS_DD_ENCRYPT_TABLE_PASSWORD`| 8  | string | Encryption passphrase |
| `ADS_DD_ENCRYPT_INDEXES`       | 9  | u16   | Encrypt index files |
| `ADS_DD_ENCRYPTED`             | 11 | u16   | Read-only; `ADS_TRUE` if the `.add` itself is encrypted |
| `ADS_DD_LOGINS_DISABLED`       | 14 | u16   | Temporarily bar new logins |
| `ADS_DD_LOGINS_DISABLED_ERRSTR`| 15 | string | Message sent to rejected clients |
| `ADS_DD_FTS_DELIMITERS`        | 17 | string | Full-text search word delimiters |
| `ADS_DD_FTS_NOISE`             | 18 | string | FTS noise-word list |
| `ADS_DD_MAX_FAILED_ATTEMPTS`   | 21 | u16   | Lock-out threshold (0 = no limit) |
| `ADS_DD_USER_DEFINED_PROP`     | 22 | string | Arbitrary application-level property |
| `ADS_DD_VERSION`               | 23 | u16   | Dictionary format version (read-only) |

### 9.7 User and group management

| Function | Parameters | Purpose |
|----------|-----------|---------|
| `AdsDDCreateUser`          | `hConnect`, `pucGroup`, `pucUser`, `pucPassword`, `pucDescription` | Create user; add to `pucGroup` if non-NULL. Alias `adssys` is the built-in admin. |
| `AdsDDDeleteUser`          | `hConnect`, `pucUser` | Remove user (and all group memberships) |
| `AdsDDAddUserToGroup`      | `hConnect`, `pucGroup`, `pucUser` | Add an existing user to a group |
| `AdsDDRemoveUserFromGroup` | `hConnect`, `pucGroup`, `pucUser` | Remove user from group |
| `AdsDDGetUserProperty`     | `hConnect`, `pucUser`, `usPropertyID`, `pvProperty`, `pusPropertyLen` | Read a user property |
| `AdsDDSetUserProperty`     | `hConnect`, `pucUser`, `usPropertyID`, `pvProperty`, `usPropertyLen` | Write a user property |

User property IDs:

| Constant | Value | Type | Description |
|----------|-------|------|-------------|
| `ADS_DD_USER_PASSWORD`        | 1101 | string | Write-only new password |
| `ADS_DD_USER_GROUP_MEMBERSHIP`| 1102 | string | Read-only; pipe-separated group names |
| `ADS_DD_USER_BAD_LOGINS`      | 1103 | u16   | Failed login counter (writable for reset) |

### 9.8 Table-level access rights

| Function | Parameters | Purpose |
|----------|-----------|---------|
| `AdsDDGetUserTableRights` | `hConnect`, `pucTableName`, `pucUser`, `pulRights*` | Read a `ADS_RIGHTS_*` bitmask |
| `AdsDDSetUserTableRights` | `hConnect`, `pucTableName`, `pucUser`, `ulRights` | Write the bitmask |

`pulRights` / `ulRights` is a bitfield of:

| Bit | Hex | Meaning |
|-----|-----|---------|
| 0 | `0x00000001` | `ADS_RIGHTS_READ` |
| 1 | `0x00000002` | `ADS_RIGHTS_WRITE` |
| 2 | `0x00000004` | `ADS_RIGHTS_INSERT` |
| 3 | `0x00000008` | `ADS_RIGHTS_DELETE` |
| 4 | `0x00000010` | `ADS_RIGHTS_EXECUTE` |
| 5 | `0x00000020` | `ADS_RIGHTS_CREATE` |
| 6 | `0x00000040` | `ADS_RIGHTS_DROP` |

### 9.9 Views

| Function | Parameters | Purpose |
|----------|-----------|---------|
| `AdsDDCreateView`     | `hConnect`, `pucName`, `pucComment`, `pucSQL` | Register a named SQL view |
| `AdsDDDropView`       | `hConnect`, `pucName` | Delete a view |
| `AdsDDGetViewProperty`| `hConnect`, `pucName`, `usPropertyID`, `pvProperty`, `pusPropertyLen` | Read view property |
| `AdsDDSetViewProperty`| `hConnect`, `pucName`, `usPropertyID`, `pvProperty`, `usPropertyLen` | Write view property |

View property IDs:

| Constant | Value | Type | Description |
|----------|-------|------|-------------|
| `ADS_DD_VIEW_STMT`    | 701 | string | SQL SELECT statement of the view |
| `ADS_DD_VIEW_COMMENT` | 702 | string | Free-text comment |

### 9.10 Stored procedures and functions

**Stored procedures** (SQL bodies stored in the `.add`):

| Function | Parameters | Purpose |
|----------|-----------|---------|
| `AdsDDCreateProcedure` | `hConnect`, `pucName`, `pucContainer`, `pucProcName`, `ulInvokeOption`, `pucInParams`, `pucOutParams`, `pucComments` | Create a stored proc. Pass the SQL body in `pucComments`; `pucContainer` / `pucProcName` hold the DLL path and entry point for external-DLL procs. |
| `AdsDDDropProcedure`   | `hConnect`, `pucName` | Delete a stored proc |
| `AdsDDGetProcProperty` | `hConnect`, `pucName`, `usPropertyID`, `pvProperty`, `pusPropertyLen` | Read a proc property |
| `AdsDDSetProcProperty` | `hConnect`, `pucName`, `usPropertyID`, `pvProperty`, `usPropertyLen` | Write a proc property |

Aliases: `AdsDDAddProcedure` = `AdsDDCreateProcedure`, `AdsDDRemoveProcedure` = `AdsDDDropProcedure`, `AdsDDGetProcedureProperty` = `AdsDDGetProcProperty`, `AdsDDSetProcedureProperty` = `AdsDDSetProcProperty`.

Procedure property IDs:

| Constant | Value | Alias | Description |
|----------|-------|-------|-------------|
| `ADS_DD_PROC_INPUT`       | 601 | — | Pipe-separated input parameter types |
| `ADS_DD_PROC_OUTPUT`      | 602 | — | Pipe-separated output parameter types |
| `ADS_DD_PROC_CONTAINER`   | 603 | `ADS_DD_PROC_DLL_NAME` | DLL path (external) or empty (SQL body) |
| `ADS_DD_PROC_PROC_NAME`   | 604 | `ADS_DD_PROC_DLL_FUNCTION_NAME` | Entry-point name (DLL) or SQL body text |
| `ADS_DD_PROC_COMMENT`     | 605 | `ADS_DD_PROC_SCRIPT` | SQL body for OpenADS SQL procs |

**User-defined functions** (UDFs):

| Function | Parameters | Purpose |
|----------|-----------|---------|
| `AdsDDCreateFunction`     | `hConnect`, `pucName`, `pucContainer`, `pucImplementation`, `pucRetType`, `pucInParams`, `pucComment` | Register a scalar UDF |
| `AdsDDDropFunction`       | `hConnect`, `pucName` | Delete a UDF |
| `AdsDDGetFunctionProperty`| `hConnect`, `pucName`, `usPropertyID`, `pvProperty`, `pusPropertyLen` | Read a UDF property |
| `AdsDDSetFunctionProperty`| `hConnect`, `pucName`, `usPropertyID`, `pvProperty`, `usPropertyLen` | Write a UDF property |

### 9.11 Triggers

**Create / drop:**

| Function | Parameters | Purpose |
|----------|-----------|---------|
| `AdsDDCreateTrigger` | `hConnect`, `pucName`, `pucTable`, `ulType`, `ulOptions`, `pucContainer`, `pucProcedure`, `ulPriority` | Create a trigger. `pucName` is `"table::name"` form or bare name. SQL bodies go in `pucContainer`. |
| `AdsDDDropTrigger`   | `hConnect`, `pucName` | Alias for `AdsDDRemoveTrigger` |
| `AdsDDRemoveTrigger` | `hConnect`, `pucName` | Delete a trigger |

**`ulType` — combined event/timing constant (`include/openads/ace.h`):**

| Constant | Value | Fires |
|----------|-------|-------|
| `ADS_BEFORE_INSERT`   | `0x0001` | Before an INSERT |
| `ADS_AFTER_INSERT`    | `0x0002` | After a successful INSERT |
| `ADS_INSTEAD_OF_INSERT` | `0x0040` | Instead of an INSERT (suppresses the actual DML) |
| `ADS_BEFORE_UPDATE`   | `0x0004` | Before an UPDATE |
| `ADS_AFTER_UPDATE`    | `0x0008` | After a successful UPDATE |
| `ADS_INSTEAD_OF_UPDATE` | `0x0080` | Instead of an UPDATE |
| `ADS_BEFORE_DELETE`   | `0x0010` | Before a DELETE |
| `ADS_AFTER_DELETE`    | `0x0020` | After a successful DELETE |
| `ADS_INSTEAD_OF_DELETE` | `0x0100` | Instead of a DELETE |

**`ulOptions` bitmask:**

| Bit | Value | Effect |
|-----|-------|--------|
| 0 | `0x01` | `WANT_VALUES` — build `__new` / `__old` virtual tables (default ON when bit is set) |
| 1 | `0x02` | `WANT_MEMOS` — include MEMO / BLOB fields in `__new` / `__old` |
| 2 | `0x04` | `NO_TRANSACTION` — skip the implicit transaction wrapper |

**Virtual tables available inside a trigger body:**

- **`__new`** — one-row table with the same fields as the base table; holds the new (post-change) values. Available in INSERT and UPDATE triggers.
- **`__old`** — one-row table; holds the pre-change values. Available in UPDATE and DELETE triggers.
- **`__error`** — two-field table (`errno INTEGER`, `message MEMO`). INSERTing a row aborts the trigger and returns the error to the client.

Only one INSTEAD OF trigger per event type (INSERT / UPDATE / DELETE) is allowed per table. If a BEFORE trigger exists for an event, no INSTEAD OF trigger may coexist for the same event. AFTER triggers do not fire when an INSTEAD OF trigger handles the same event. Nesting depth is capped at 64 levels.

**`ulPriority`** — lower integer fires first when multiple triggers share the same event and timing.

**Trigger properties (`AdsDDGetTriggerProperty` / `AdsDDSetTriggerProperty`):**

Both take `hConnect`, `pucName`, `usPropertyID`, `pvProperty`, `pusPropertyLen / usPropertyLen`.

| Constant | Value | Type | Description |
|----------|-------|------|-------------|
| `ADS_DD_TRIGGER_TABLE`     | 501 | string | Table alias this trigger is bound to |
| `ADS_DD_TRIGGER_EVENT`     | 502 | u32   | Combined event/timing constant (one of the `ADS_BEFORE_*` / `ADS_AFTER_*` / `ADS_INSTEAD_OF_*` values) |
| `ADS_DD_TRIGGER_CONTAINER` | 503 | string | SQL body (OpenADS) or DLL path (external AEP) |
| `ADS_DD_TRIGGER_PROC_NAME` | 504 | string | Entry-point name (external AEP) or empty (SQL body) |
| `ADS_DD_TRIGGER_ENABLED`   | 505 | u16   | `ADS_TRUE` → trigger fires; `ADS_FALSE` → disabled |
| `ADS_DD_TRIGGER_PRIORITY`  | 506 | u32   | Firing priority (lower = first) |
| `ADS_DD_TRIGGER_COMMENT`   | 507 | string | Free-text comment |

Synonym aliases: `ADS_DD_TRIG_TABLEID` = 501, `ADS_DD_TRIG_EVENT_TYPE` = 502, `ADS_DD_TRIG_CONTAINER` = 503, `ADS_DD_TRIG_FUNCTION_NAME` = 504, `ADS_DD_TRIG_PRIORITY` = 506, `ADS_DD_TRIG_TABLENAME` = 501.

**Disable / enable at runtime (system stored procedures):**

```sql
-- Disable all triggers for the current connection (non-persistent)
EXECUTE PROCEDURE sp_DisableTriggers('CURRENT USER', '', '');

-- Disable all triggers for all users (persistent)
EXECUTE PROCEDURE sp_DisableTriggers('ALL', '', '');

-- Disable all triggers on a single table (persistent, all users)
EXECUTE PROCEDURE sp_DisableTriggers('TABLE', 'orders', '');

-- Disable one trigger by name (persistent, all users)
EXECUTE PROCEDURE sp_DisableTriggers('TRIGGER', 'orders', 'orders::audit_insert');

-- Re-enable (same scope arguments as Disable)
EXECUTE PROCEDURE sp_EnableTriggers('ALL', '', '');
```

### 9.12 Referential Integrity

| Function | Parameters | Purpose |
|----------|-----------|---------|
| `AdsDDCreateRefIntegrity` | `hConnect`, `pucName`, `pucFailTable`, `pucParent`, `pucParentTag`, `pucChild`, `pucChildTag`, `usUpdateOption`, `usDeleteOption` | Define an RI rule between two DD-registered tables |
| `AdsDDRemoveRefIntegrity` | `hConnect`, `pucName` | Delete an RI rule |
| `AdsDDGetRefIntegrityProperty` | `hConnect`, `pucName`, `usPropertyID`, `pvProperty`, `pusPropertyLen` | Read an RI rule property |
| `AdsDDSetRefIntegrityProperty` | `hConnect`, `pucName`, `usPropertyID`, `pvProperty`, `usPropertyLen` | Write an RI rule property |

`usUpdateOption` / `usDeleteOption` constants:

| Constant | Value | Meaning |
|----------|-------|---------|
| `ADS_DD_RI_CASCADE`    | 1 | Cascade changes to child table |
| `ADS_DD_RI_RESTRICT`   | 2 | Reject parent change if child row exists |
| `ADS_DD_RI_SETNULL`    | 3 | Set child FK to NULL on parent change |
| `ADS_DD_RI_SETDEFAULT` | 4 | Set child FK to default on parent change |

RI property IDs:

| Constant | Value | Type | Description |
|----------|-------|------|-------------|
| `ADS_DD_RI_PARENT`     | 401 | string | Parent table alias |
| `ADS_DD_RI_CHILD`      | 402 | string | Child table alias |
| `ADS_DD_RI_PARENT_TAG` | 403 | string | Parent index tag used as FK source |
| `ADS_DD_RI_CHILD_TAG`  | 404 | string | Child index tag used as FK |
| `ADS_DD_RI_UPDATE_RULE`| 405 | u16   | One of `ADS_DD_RI_*` constants |
| `ADS_DD_RI_DELETE_RULE`| 406 | u16   | One of `ADS_DD_RI_*` constants |
| `ADS_DD_RI_FAIL_TABLE` | 407 | string | Table to log RI violations into |

### 9.13 Links (cross-dictionary references)

| Function | Parameters | Purpose |
|----------|-----------|---------|
| `AdsDDCreateLink` | `hConnect`, `pucAlias`, `pucPath`, `pucUser`, `pucPassword`, `usOptions` | Register a remote DD path (e.g. `tcp://other-host:16262/data`) under an alias. |
| `AdsDDDropLink`   | `hConnect`, `pucAlias`, `usOptions` | Remove a link |
| `AdsDDModifyLink` | `hConnect`, `pucAlias`, `pucPath`, `pucUser`, `pucPassword`, `usOptions` | Update link credentials / path |

### 9.14 Object enumeration

`AdsDDFindFirstObject` / `AdsDDFindNextObject` / `AdsDDFindClose` iterate over
objects of a given type in the current DD:

```c
ADSHANDLE hFind;
char name[256]; UNSIGNED16 len = sizeof(name);
AdsDDFindFirstObject(hConnect, ADS_DD_TABLE_OBJECT, NULL, name, &len, &hFind);
while (len > 0) {
    printf("table: %.*s\n", len, name);
    len = sizeof(name);
    AdsDDFindNextObject(hConnect, hFind, name, &len);
}
AdsDDFindClose(hConnect, hFind);
```

`usFindObjectType` values: `ADS_DD_TABLE_OBJECT=1`, `ADS_DD_USER_OBJECT=2`,
`ADS_DD_INDEX_FILE_OBJECT=3`, `ADS_DD_VIEW_OBJECT=4`, `ADS_DD_PROC_OBJECT=5`,
`ADS_DD_RI_OBJECT=6`, `ADS_DD_TRIGGER_OBJECT=7`, `ADS_DD_LINK_OBJECT=8`.
`pucParentName` filters by table name (e.g. for `ADS_DD_TRIGGER_OBJECT` to list
only triggers on one table); pass `NULL` to enumerate all.

### 9.15 Full example (C)

```c
#include <openads/ace.h>

/* Create a dictionary with one table, one trigger, and one RI rule */
int main(void) {
    ADSHANDLE hConn;

    /* 1. Create dictionary */
    AdsDDCreate("C:/data/myapp.add", ADS_FALSE, "secret", &hConn);

    /* 2. Register the orders table */
    AdsDDAddTable(hConn, "orders", "orders.dbf",
                  ADS_CDX, ADS_ANSI, NULL, "Order master");

    /* 3. Set primary key property */
    const char* pk = "order_id";
    AdsDDSetTableProperty(hConn, "orders",
                          ADS_DD_TABLE_PRIMARY_KEY,
                          (void*)pk, (UNSIGNED16)strlen(pk));

    /* 4. Add an AFTER INSERT trigger with an inline SQL body */
    const char* body =
        "INSERT INTO auditlog (action, ts) "
        "SELECT 'INSERT', NOW() FROM system.iota;";
    AdsDDCreateTrigger(hConn,
        "orders::after_insert",   /* name */
        "orders",                 /* table */
        ADS_AFTER_INSERT,         /* ulType */
        0x03u,                    /* WANT_VALUES | WANT_MEMOS */
        (UNSIGNED8*)body,         /* pucContainer = SQL body */
        NULL,                     /* pucProcedure = NULL for SQL */
        10);                      /* priority */

    /* 5. Add an RI rule: orders.customer_id → customers.id */
    AdsDDCreateRefIntegrity(hConn,
        "orders_customer",    /* rule name */
        "rierrors",           /* fail table */
        "customers",          /* parent */
        "CUST_PK",            /* parent tag */
        "orders",             /* child */
        "CUST_FK",            /* child tag */
        ADS_DD_RI_RESTRICT,   /* update */
        ADS_DD_RI_RESTRICT);  /* delete */

    AdsDisconnect(hConn);
    return 0;
}
```

### 9.16 PHP (via php_advantage / OpenADS PHP extension)

```php
// Connect to a remote OpenADS server with a DD
$conn = ads_connect("tcp://localhost:16262/data/myapp.add",
                    "adssys", "secret", ADS_REMOTE_SERVER);

// Read the primary key of the orders table
$len = 256; $pk = "";
ads_dd_get_table_property($conn, "orders",
    ADS_DD_TABLE_PRIMARY_KEY, $pk, $len);

// Create an AFTER UPDATE trigger
$body = "UPDATE auditlog SET updated = NOW() "
      . "WHERE tbl = 'orders';";
ads_dd_create_trigger($conn,
    "orders::after_update", "orders",
    ADS_AFTER_UPDATE, 0x01,   // WANT_VALUES
    $body, null, 10);

ads_disconnect($conn);
```
