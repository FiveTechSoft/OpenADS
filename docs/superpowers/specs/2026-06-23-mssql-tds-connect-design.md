# Native MS SQL Server (TDS) backend — sub-project 1: Connect — design

Date: 2026-06-23
Status: approved (pre-implementation)
Scope: a from-scratch TDS client behind the ACE ABI, **sub-project 1 of 3**. This
sub-project delivers CONNECT + AUTHENTICATE only.

## Why

MS SQL Server is reachable today only through the ODBC backend (needs a vendor
ODBC driver installed) or OLE DB (Windows-only). A native client that speaks the
TDS wire protocol over TCP/1433 needs no driver, runs cross-platform (Linux
included), and plugs into the same ACE ABI as the other SQL backends. We
implement TDS ourselves (no FreeTDS) to stay dependency-free and source-clean.

## Decomposition (context — only sub-project 1 is in this spec)

1. **Connect** (THIS spec): PRELOGIN, TLS handshake tunnelled in TDS packets,
   LOGIN7 with SQL-Server authentication, parse LOGINACK/DONE → a live
   authenticated connection through `AdsConnect60` / `AdsDisconnect`.
2. **Query + read**: SQL_BATCH, token-stream parse (COLMETADATA/ROW/DONE), type
   decode → read/navigation via PK snapshot behind the ABI.
3. **Write + seek + matrix**: append/update/delete, seek, driver-matrix entry.

## Scope of sub-project 1

- IN: TCP connect; PRELOGIN exchange; TLS handshake tunnelled in TDS `0x12`
  packets (mbedtls); LOGIN7 with SQL-Server authentication (login + password);
  parse the login response token stream (LOGINACK = success, ERROR = failure,
  plus ENVCHANGE/INFO/DONE); expose as `AdsConnect60`(`mssql://…`) returning 0
  on success and `AdsDisconnect` closing cleanly.
- OUT (later sub-projects / deferred): any table/row/query operation; Windows
  Integrated auth (NTLM/Kerberos); connection pooling; MARS; bulk copy;
  failover/redirect (ENVCHANGE routing).

## Wire facts this design relies on (TDS 7.4)

- Every message is a sequence of 8-byte-header packets: `type(1)`,
  `status(1)`, `length(2, big-endian, includes header)`, `spid(2)`,
  `packetID(1)`, `window(1)`. `status` bit `0x01` = EOM (last packet of the
  message).
- Packet types used: PRELOGIN = `0x12`, LOGIN7 = `0x10`, token stream (server
  reply) = `0x04` (TABULAR_RESULT).
- **TLS is tunnelled**: after PRELOGIN negotiates encryption, the TLS handshake
  records are carried as the payload of `0x12` packets in BOTH directions until
  the handshake completes; thereafter the connection is plain TLS over the TCP
  socket and LOGIN7 (and all later traffic) is sent as ordinary TLS-encrypted
  TDS packets.
- LOGIN7 strings (hostname, username, appname, servername, database) are
  UCS-2LE, referenced by a fixed offset/length table inside the packet.
- **Password obfuscation** (applied even under TLS): for each UCS-2LE byte of
  the password, swap the high/low nibble, then XOR with `0xA5`.

## Architecture

Mirrors the existing `sql_backend/` mold; reuses `sql_common.h` and the linked
mbedtls (`OPENADS_WITH_TLS`). New compile flag `OPENADS_WITH_MSSQL` (implies
TLS is available).

### Files

- **`src/sql_backend/tds_protocol.{h,cpp}`** — pure, no I/O. The byte layer:
  - `struct TdsPacketHeader` + encode/decode.
  - `build_prelogin()` → bytes; `parse_prelogin_response(bytes)` → negotiated
    encryption option.
  - `build_login7(const Login7Params&)` → bytes, including the offset/length
    table, UCS-2LE encoding, and password obfuscation.
  - `obfuscate_password(const std::string& utf8)` → UCS-2LE obfuscated bytes
    (own function so it can be unit-tested against the documented algorithm).
  - A token-stream reader: `parse_login_response(bytes)` → `LoginResult`
    (`authenticated` bool, server message/number on ERROR, sanitised). Handles
    LOGINACK(`0xAD`), ERROR(`0xAA`), INFO(`0xAB`), ENVCHANGE(`0xE3`),
    DONE(`0xFD`).
  - This file is the bulk of the careful work and is fully unit-testable with
    no server.

- **`src/sql_backend/tds_tls_channel.{h,cpp}`** — the framed-handshake channel.
  Owns a raw `openads::network::Socket` and an mbedtls SSL context. `handshake()`
  performs the PRELOGIN exchange, then drives `mbedtls_ssl_handshake` with custom
  BIO send/recv callbacks that wrap outgoing TLS records in `0x12` TDS packet
  headers and strip incoming ones — a `handshake_done_` flag flips the callbacks
  to pass-through afterward. Post-handshake, `send_tds(type, payload)` /
  `recv_tds() → (type, payload)` segment/reassemble TDS packets over plain TLS.
  Reuses `network/socket.h` for the TCP socket and the already-vendored mbedtls;
  does NOT reuse `connect_tls` (that does an untunnelled handshake).

- **`src/sql_backend/mssql_uri.{h,cpp}`** — `struct MssqlUri { host; port(=1433);
  user; password; database; bool encrypt(=true); bool trust_server_cert(=true
  for v1); }` and `parse_mssql_uri(uri, out)` for `mssql://user:pass@host:port/db`
  and `tds://…`. Percent-decoding of user/pass.

- **`src/sql_backend/mssql_connection.{h,cpp}`** — `class MssqlConnection`
  (pImpl). `static Result<MssqlConnection> open(const MssqlUri&)` orchestrates:
  `connect_tcp` → `TdsTlsChannel::handshake` → send LOGIN7 → `recv_tds` →
  `parse_login_response` → authenticated (or `util::Error` from an ERROR token).
  `disconnect()` closes the channel. For sub-project 1 it holds only the
  authenticated channel; table operations arrive in sub-project 2.

### ABI dispatch (`src/abi/ace_exports.cpp`)

In `AdsConnect60`, add an `#if defined(OPENADS_WITH_MSSQL)` block (mirroring the
`odbc://` block) that detects `mssql://` / `tds://` via `parse_mssql_uri`, calls
`MssqlConnection::open`, and registers a new `HandleKind::MssqlConnection`. Add
the matching arm in `AdsDisconnect`. Without the flag, a manual prefix check
returns `AE_FUNCTION_NOT_AVAILABLE` (same pattern as ODBC). No other `Ads*`
function is touched in sub-project 1.

### CMake

Root `CMakeLists.txt`: `option(OPENADS_WITH_MSSQL "…" OFF)`. `src/CMakeLists.txt`:
under `if(OPENADS_WITH_MSSQL)` add the four new sources + `sql_common.cpp`,
define `OPENADS_WITH_MSSQL=1`, and ensure mbedtls is linked (require/imply
`OPENADS_WITH_TLS`).

## Data flow

```
connect_tcp(host, port)
  → send PRELOGIN (0x12)            [clear]
  → recv PRELOGIN response          [clear]  → read ENCRYPTION byte
  → TLS handshake tunnelled in 0x12 packets (mbedtls, custom BIO)
  → send LOGIN7 (0x10)              [TLS]    (password obfuscated)
  → recv token stream (0x04)        [TLS]
  → parse: LOGINACK + DONE → authenticated
                     ERROR → util::Error(message, number)
```

## Error handling

- Socket / TLS-handshake failures → `util::Error` with a generic context string.
- A TDS ERROR token → `util::Error{number, 0, sanitised_message, ""}`. The
  message text from the server is surfaced, but the connection string, username,
  and password are NEVER included in an error returned across the ABI (house
  rule: no secrets/PII to the UI/log). Passwords never appear in logs.

## Testing

- **Pure unit tests (no server)** — `tests/unit/tds_protocol_test.cpp`:
  - TDS packet header encode→decode round-trip (type/status/length/spid).
  - `obfuscate_password` matches the documented swap-nibbles-then-XOR-`0xA5`
    algorithm for a known input → known output vector.
  - `build_prelogin()` produces a well-formed PRELOGIN (option table + 
    terminator) — assert key bytes.
  - `build_login7()` for fixed params produces the expected fixed-field block,
    offset/length table, and UCS-2LE payload — assert against a hand-computed
    byte vector.
  - Token-stream reader classifies a synthetic LOGINACK+DONE as authenticated
    and a synthetic ERROR token as failure with the right number/message.
- **Live test (gated)** — `tests/unit/abi_plus_mssql_connect_test.cpp`,
  `#if defined(OPENADS_WITH_MSSQL)`, runtime-gated on env
  `OPENADS_TEST_MSSQL_CONNSTR` (a full `mssql://user:pass@host:port/db`). When
  set: `AdsConnect60` returns 0 and `AdsDisconnect` returns 0; a deliberately
  wrong password returns a non-zero auth error (proves the negative path). When
  unset: `MESSAGE(...skipping)` and return.

## Test prerequisite (provisioning)

The live test needs a SQL Server reachable over **TCP** — the existing LocalDB
instance listens on a named pipe only and cannot be used. The implementation
plan's first task provisions SQL Server Express with the TCP/IP protocol enabled
on port 1433, mixed-mode authentication, and a dedicated test login + database.
This is an owner-authorised, non-portable host install (started as a Windows
service). Credentials live in env / a local untracked file, never in the repo.

## Verification

- Pure unit suite green (TDS encoding tests) with `OPENADS_WITH_MSSQL=ON`.
- x64 build via the pinned recipe (MSVC x64 + winlibs cmake/ninja or NMake,
  `OPENADS_WITH_MSSQL=ON`, `OPENADS_WITH_TLS=ON`).
- Live: `AdsConnect60(mssql://…)` authenticates against the provisioned SQL
  Express over TCP, and the wrong-password path returns an auth error.

## Risks

- **TLS-in-TDS framing** is the highest-risk piece (custom BIO callbacks that
  frame only during the handshake). Mitigation: the pure packet layer is unit
  tested first; the channel is exercised against a real server early.
- **Encryption negotiation**: the implementation always negotiates TLS and
  requires the server to support encryption. If the server returns
  `ENCRYPT_NOT_SUP` in the PRELOGIN response, the connection is rejected
  with an error (rather than falling back to clear-text LOGIN7). v1 therefore
  requires a SQL Server instance that has encryption enabled (`ENCRYPT_ON` or
  `ENCRYPT_OFF` login-only). Servers explicitly refusing TLS (`ENCRYPT_NOT_SUP`)
  are not supported in v1 and must have their encryption settings updated before
  use with this backend.
- **Provisioning dependency**: the live auth test is blocked until SQL Express
  (TCP) is installed; the pure unit tests are not.
- Like the other native-backend branches, this depends on `sql_common.h` (not
  yet in upstream/main); the upstream PR is deferred until the Plus PRs land,
  same as the Firebird branch.
