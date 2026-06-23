# Native MS SQL Server (TDS) — Sub-project 1 (Connect) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A from-scratch TDS client that authenticates to MS SQL Server over TCP/1433 (no vendor driver), exposed as `AdsConnect60(mssql://…)` / `AdsDisconnect`.

**Architecture:** A pure byte-layer (`tds_protocol`) builds/parses TDS packets, PRELOGIN, LOGIN7 (with password obfuscation), and the login-response token stream — fully unit-testable without a server. A `tds_tls_channel` performs the TLS handshake tunnelled inside TDS `0x12` packets (mbedtls custom BIO), then carries plain-TLS TDS packets. `mssql_connection` orchestrates connect→PRELOGIN→TLS→LOGIN7→LOGINACK; the ACE ABI dispatches `mssql://` to it. Mirrors the existing `sql_backend/` mold; reuses `sql_common.h` and the linked mbedtls.

**Tech Stack:** C++17, MS-TDS 7.4 wire protocol, mbedtls (already vendored via `OPENADS_WITH_TLS`), `network/socket.h`, doctest, CMake (MSVC x64 + winlibs), SQL Server Express (test target).

## Global Constraints

- Source-clean PUBLIC repo: NO internal drive-letter paths, NO third-party product names beyond unavoidable technical facts (the protocol name "TDS" and "SQL Server" are facts), NO business/strategy text, NO credentials in any committed file. Test credentials live in env / a local untracked file only.
- Commit trailer: end every commit with exactly `Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>` and nothing else.
- Branch: `pr/openads-plus-mssql` (worktree `H:/DEVAI/_Prj/OpenADS-mssql`). Push each commit.
- Scope is CONNECT + SQL-Server-auth ONLY. No table/row/query ops, no Windows/NTLM auth, no pooling. Those are sub-projects 2/3.
- Secrets never cross the ABI in an error or reach a log: a TDS ERROR token's message may be surfaced, but the connection string, username, and password must never appear in a returned `util::Error` or any log line.
- Normative reference for all byte layouts: the MS-TDS protocol specification ([MS-TDS], TDS 7.4). Cite section numbers in comments; do not transcribe field tables from memory — follow the spec.
- New files live in `src/sql_backend/` (the SQL-backend mold) and reuse `sql_common.h`.
- Build (x64), from the worktree root `H:/DEVAI/_Prj/OpenADS-mssql`:
  ```
  cmd /c 'call H:\DEVAI\_UtlAI\msvc\msvc_x64_full.bat && cd /d H:\DEVAI\_Prj\OpenADS-mssql && H:\DEVAI\_UtlAI\Mingw\bin\cmake.exe -S . -B build\mssql-verify -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl -DOPENADS_WITH_MSSQL=ON -DOPENADS_WITH_TLS=ON -DOPENADS_WITH_HTTP=OFF -DOPENADS_WARNINGS_AS_ERRORS=OFF -DCMAKE_TLS_VERIFY=0 && H:\DEVAI\_UtlAI\Mingw\bin\cmake.exe --build build\mssql-verify'
  ```
  (First build is slow — `OPENADS_WITH_TLS=ON` fetches and compiles mbedtls via FetchContent; needs network + `CMAKE_TLS_VERIFY=0`.)
- Run pure unit tests (no server): `build\mssql-verify\tests\openads_unit_tests.exe --test-case=*tds*` and `--test-case=*mssql*`.
- Run the live auth test: set env `OPENADS_TEST_MSSQL_CONNSTR='mssql://<user>:<pass>@127.0.0.1:1433/<db>'` then run `--test-case=*mssql*connect*`.

---

## File Structure

- `src/sql_backend/tds_protocol.h` / `.cpp` — pure byte layer (header, PRELOGIN, LOGIN7, obfuscation, token parse). No sockets, no TLS.
- `src/sql_backend/tds_tls_channel.h` / `.cpp` — mbedtls channel with TDS-framed handshake; TDS packet send/recv.
- `src/sql_backend/mssql_uri.h` / `.cpp` — `MssqlUri` + `parse_mssql_uri`.
- `src/sql_backend/mssql_connection.h` / `.cpp` — `MssqlConnection::open/disconnect`.
- `src/abi/ace_exports.cpp` — `mssql://` dispatch in `AdsConnect60`; arm in `AdsDisconnect`; `HandleKind::MssqlConnection`.
- `CMakeLists.txt` / `src/CMakeLists.txt` — `OPENADS_WITH_MSSQL` option, sources, defines, mbedtls link.
- `tests/unit/tds_protocol_test.cpp` — pure byte-layer tests.
- `tests/unit/mssql_uri_test.cpp` — URI parse tests.
- `tests/unit/abi_plus_mssql_connect_test.cpp` — gated live auth test + the disabled-at-compile-time test.
- `tools/scripts/run_mssql_connect_test.ps1` — exports the connstr env and runs the gated test.

---

### Task 1: Provision the SQL Server Express (TCP) test target

This task sets up the live test target. It is host provisioning, not repo code — its deliverable is a TCP-reachable SQL Server and a connection string in env. (Controller may perform this directly rather than via a subagent.)

**Files:** none committed (provisioning + a local untracked `tools/scripts/mssql_test_env.local.ps1` that exports the connstr; add `*.local.ps1` to `.gitignore` if not already ignored).

- [ ] **Step 1: Install SQL Server Express 2022 with TCP enabled**

Download the Express installer (bootstrapper `SQL2022-SSEI-Expr.exe` from Microsoft's official SQL Server 2022 Express download page) to a temp dir, then run an unattended install enabling mixed-mode auth and a SA password:
```
<installer> /Q /ACTION=Install /FEATURES=SQLEngine /INSTANCENAME=SQLEXPRESS /SECURITYMODE=SQL /SAPWD='<StrongTempPwd!>' /TCPENABLED=1 /IACCEPTSQLSERVERLICENSETERMS
```
(If the bootstrapper requires a two-step `/ACTION=Download` then run the extracted `SETUP.EXE` with the args above — follow whichever the downloaded bootstrapper requires.)

- [ ] **Step 2: Ensure the TCP/IP protocol listens on 1433 and start the service**

Enable TCP/IP for the instance and set a static port 1433 (via SQL Server Configuration Manager WMI, or by setting the instance's `IPAll` TcpPort to `1433` in the registry under the instance's `…\MSSQLServer\SuperSocketNetLib\Tcp\IPAll`), then restart the `MSSQL$SQLEXPRESS` service:
```
Restart-Service 'MSSQL$SQLEXPRESS'
```
Confirm it listens: `netstat -ano -p tcp | findstr :1433` shows a LISTENING line.

- [ ] **Step 3: Create a dedicated test login and database**

Using `sqlcmd` (present at the SQL Client SDK path) against the instance, create a SQL login and a small DB:
```
sqlcmd -S 127.0.0.1,1433 -U sa -P '<StrongTempPwd!>' -Q "CREATE DATABASE openads_test; CREATE LOGIN openads_user WITH PASSWORD='Openads#Test1', CHECK_POLICY=OFF; USE openads_test; CREATE USER openads_user FOR LOGIN openads_user; ALTER ROLE db_owner ADD MEMBER openads_user;"
```

- [ ] **Step 4: Verify TCP auth works and write the local env file**

Verify a TCP SQL-auth login succeeds:
```
sqlcmd -S 127.0.0.1,1433 -U openads_user -P 'Openads#Test1' -d openads_test -Q "SELECT 1"
```
Expected: returns `1`. Then write the untracked `tools/scripts/mssql_test_env.local.ps1`:
```powershell
$env:OPENADS_TEST_MSSQL_CONNSTR = 'mssql://openads_user:Openads#Test1@127.0.0.1:1433/openads_test'
```
Do NOT commit this file. Record (in the task report, not the repo) that the target is provisioned.

- [ ] **Step 5: No commit (provisioning only)** — nothing to commit; subsequent tasks consume the env var.

---

### Task 2: CMake flag, backend skeleton, and disabled-at-compile-time ABI guard

**Files:**
- Modify: `CMakeLists.txt` (add the option), `src/CMakeLists.txt` (sources/defines/link)
- Create: `src/sql_backend/tds_protocol.h` (header-only declarations for now), `src/sql_backend/tds_protocol.cpp` (empty TU guarded by the flag)
- Modify: `src/abi/ace_exports.cpp` (the `#else` guard for `mssql://`)
- Create: `tests/unit/abi_plus_mssql_connect_test.cpp` (only the disabled-at-compile-time case for now)
- Modify: the test target's CMake source list

**Interfaces:**
- Produces: the `OPENADS_WITH_MSSQL` build flag and a compiling (empty) `tds_protocol` TU. Later tasks fill in `tds_protocol`.

- [ ] **Step 1: Add the build option**

In root `CMakeLists.txt`, after the `OPENADS_WITH_ODBC` option line, add:
```cmake
option(OPENADS_WITH_MSSQL "Enable native MS SQL Server / TDS backend" OFF)
```
In `src/CMakeLists.txt`, after the ODBC `if(OPENADS_WITH_ODBC)` sources block, add (sources for files that exist now; later tasks append theirs):
```cmake
if(OPENADS_WITH_MSSQL)
    target_sources(openads_core PRIVATE
        sql_backend/sql_common.cpp
        sql_backend/tds_protocol.cpp
    )
    target_compile_definitions(openads_core PUBLIC OPENADS_WITH_MSSQL=1)
endif()
```
(`OPENADS_WITH_MSSQL` requires `OPENADS_WITH_TLS=ON` for mbedtls; the build recipe passes both. Do not duplicate the mbedtls link — the `OPENADS_WITH_TLS` block already links it.)

- [ ] **Step 2: Create the minimal header and an empty guarded TU**

`src/sql_backend/tds_protocol.h`:
```cpp
#pragma once
// Pure TDS (MS-TDS 7.4) byte layer: no sockets, no TLS. Built/parsed buffers
// only. See [MS-TDS] for all field layouts.
#include <cstdint>
#include <string>
#include <vector>

namespace openads::sql_backend::tds {
}  // namespace openads::sql_backend::tds
```
`src/sql_backend/tds_protocol.cpp`:
```cpp
#include "sql_backend/tds_protocol.h"
#if defined(OPENADS_WITH_MSSQL)
namespace openads::sql_backend::tds {
}  // namespace
#endif
```

- [ ] **Step 3: Add the disabled-path ABI guard**

In `src/abi/ace_exports.cpp` `AdsConnect60`, mirror the ODBC `#if/#else` shape. Add, near the ODBC block:
```cpp
#if defined(OPENADS_WITH_MSSQL)
    // (filled in Task 7)
#else
    {
        const char* p = reinterpret_cast<const char*>(pucServerPath);
        if (p && (std::strncmp(p, "mssql://", 8) == 0 ||
                  std::strncmp(p, "tds://", 6) == 0)) {
            return openads::AE_FUNCTION_NOT_AVAILABLE;
        }
    }
#endif
```
(Place it consistently with the existing `odbc://` disabled-guard; reuse whatever `path`/`p` variable the surrounding code already derives if present, rather than re-deriving.)

- [ ] **Step 4: Write the disabled-at-compile-time test**

`tests/unit/abi_plus_mssql_connect_test.cpp`:
```cpp
#include "doctest.h"
#include "openads/ace.h"
#include "openads/error.h"
#include <cstring>

#if !defined(OPENADS_WITH_MSSQL)
TEST_CASE("ABI: mssql backend disabled at compile time") {
    UNSIGNED8 uri[] = "mssql://u:p@127.0.0.1:1433/db";
    ADSHANDLE hConn = 0;
    const UNSIGNED32 rc = AdsConnect60(uri, ADS_LOCAL_SERVER,
                                       nullptr, nullptr, 0, &hConn);
    CHECK(rc == openads::AE_FUNCTION_NOT_AVAILABLE);
}
#endif
```
Register it in the test target's CMake source list next to `abi_plus_odbc_read_test.cpp` (find via `grep -rn abi_plus_odbc_read_test.cpp src tests CMakeLists.txt`).

- [ ] **Step 5: Build BOTH ways and verify**

Build WITH the flag (Global Constraints recipe) → compiles clean (empty backend). Then a second config WITHOUT mssql to exercise the guard:
```
... cmake -S . -B build\nomssql -G "NMake Makefiles" -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl -DOPENADS_WITH_MSSQL=OFF -DOPENADS_WITH_TLS=OFF -DOPENADS_WITH_HTTP=OFF -DCMAKE_TLS_VERIFY=0 && ... --build build\nomssql
build\nomssql\tests\openads_unit_tests.exe --test-case=*mssql*disabled*
```
Expected: the disabled test PASSES (returns AE_FUNCTION_NOT_AVAILABLE). The WITH-flag build compiles with no errors.

- [ ] **Step 6: Commit**
```
git add CMakeLists.txt src/CMakeLists.txt src/sql_backend/tds_protocol.* src/abi/ace_exports.cpp tests/unit/abi_plus_mssql_connect_test.cpp <test-cmake-file>
git commit -m "feat(mssql): OPENADS_WITH_MSSQL flag, backend skeleton, disabled-path ABI guard

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 3: TDS packet header + password obfuscation (pure, exact)

**Files:**
- Modify: `src/sql_backend/tds_protocol.h` / `.cpp`
- Test: `tests/unit/tds_protocol_test.cpp` (create; register in test CMake list)

**Interfaces:**
- Produces:
  - `struct TdsPacketHeader { uint8_t type; uint8_t status; uint16_t length; uint16_t spid; uint8_t packet_id; uint8_t window; };`
  - `void write_header(std::vector<uint8_t>& out, uint8_t type, uint8_t status, uint16_t total_len);` (length is big-endian, includes the 8-byte header)
  - `bool read_header(const uint8_t* buf, size_t n, TdsPacketHeader& h);`
  - `std::vector<uint8_t> obfuscate_password(const std::string& utf8_password);` — UCS-2LE then per-byte (swap nibbles, XOR 0xA5).
  - Constants: `TDS_PKT_SQLBATCH=0x01`, `TDS_PKT_LOGIN7=0x10`, `TDS_PKT_PRELOGIN=0x12`, `TDS_PKT_REPLY=0x04`, `TDS_STATUS_EOM=0x01`.

- [ ] **Step 1: Write failing tests**

`tests/unit/tds_protocol_test.cpp`:
```cpp
#include "doctest.h"
#if defined(OPENADS_WITH_MSSQL)
#include "sql_backend/tds_protocol.h"
using namespace openads::sql_backend::tds;

TEST_CASE("tds header write/read round-trip, length is big-endian incl header") {
    std::vector<uint8_t> out;
    write_header(out, TDS_PKT_PRELOGIN, TDS_STATUS_EOM, 8 + 5);
    REQUIRE(out.size() == 8);
    CHECK(out[0] == 0x12);
    CHECK(out[1] == 0x01);
    CHECK(out[2] == 0x00);          // length high byte (big-endian)
    CHECK(out[3] == 0x0D);          // length low byte = 13
    TdsPacketHeader h{};
    REQUIRE(read_header(out.data(), out.size(), h));
    CHECK(h.type == 0x12);
    CHECK(h.status == 0x01);
    CHECK(h.length == 13);
}

TEST_CASE("tds password obfuscation: swap nibbles then XOR 0xA5 over UCS-2LE") {
    // "abc" -> UCS-2LE bytes 61 00 62 00 63 00 ; each byte: swap nibbles, ^0xA5.
    //   0x61 -> swap 0x16 -> ^0xA5 = 0xB3 ;  0x00 -> swap 0x00 -> ^0xA5 = 0xA5
    //   0x62 -> swap 0x26 -> ^0xA5 = 0x83 ;  0x63 -> swap 0x36 -> ^0xA5 = 0x93
    auto o = obfuscate_password("abc");
    REQUIRE(o.size() == 6);
    CHECK(o[0] == 0xB3);  CHECK(o[1] == 0xA5);
    CHECK(o[2] == 0x83);  CHECK(o[3] == 0xA5);
    CHECK(o[4] == 0x93);  CHECK(o[5] == 0xA5);
}
```
This is the verified vector (matches the LOGIN7 password needle in Task 5). The
algorithm is fixed; the implementer must keep these exact literals.

- [ ] **Step 2: Run to verify it fails**

Build, then `build\mssql-verify\tests\openads_unit_tests.exe --test-case=*tds*`. Expected: FAIL (functions undefined).

- [ ] **Step 3: Implement header + obfuscation**

In `tds_protocol.h` add the struct, constants, and declarations. In `tds_protocol.cpp` (inside `#if OPENADS_WITH_MSSQL`):
```cpp
void write_header(std::vector<uint8_t>& out, uint8_t type, uint8_t status,
                  uint16_t total_len) {
    out.push_back(type);
    out.push_back(status);
    out.push_back(static_cast<uint8_t>((total_len >> 8) & 0xFF));  // big-endian
    out.push_back(static_cast<uint8_t>(total_len & 0xFF));
    out.push_back(0); out.push_back(0);   // SPID (client→server: 0)
    out.push_back(0);                     // PacketID
    out.push_back(0);                     // Window
}

bool read_header(const uint8_t* buf, size_t n, TdsPacketHeader& h) {
    if (n < 8) return false;
    h.type = buf[0]; h.status = buf[1];
    h.length = static_cast<uint16_t>((buf[2] << 8) | buf[3]);
    h.spid = static_cast<uint16_t>((buf[4] << 8) | buf[5]);
    h.packet_id = buf[6]; h.window = buf[7];
    return true;
}

std::vector<uint8_t> obfuscate_password(const std::string& pw) {
    std::vector<uint8_t> out;
    out.reserve(pw.size() * 2);
    for (unsigned char c : pw) {
        // UCS-2LE: low byte = char, high byte = 0 (ASCII passwords for v1).
        for (uint8_t b : {static_cast<uint8_t>(c), uint8_t{0}}) {
            uint8_t swapped = static_cast<uint8_t>((b >> 4) | (b << 4));
            out.push_back(static_cast<uint8_t>(swapped ^ 0xA5));
        }
    }
    return out;
}
```
(v1 assumes ASCII passwords → high UCS-2 byte 0. Non-ASCII passwords are a documented v1 limitation.)

- [ ] **Step 4: Run to verify pass** — `--test-case=*tds*` → PASS.

- [ ] **Step 5: Commit**
```
git add src/sql_backend/tds_protocol.* tests/unit/tds_protocol_test.cpp <test-cmake-file>
git commit -m "feat(mssql): TDS packet header + password obfuscation (pure, unit-tested)

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 4: PRELOGIN build + response parse (pure)

**Files:** Modify `src/sql_backend/tds_protocol.{h,cpp}`; add tests to `tests/unit/tds_protocol_test.cpp`.

**Interfaces:**
- Produces:
  - `std::vector<uint8_t> build_prelogin();` — a complete PRELOGIN message (header + option table + data) advertising VERSION and ENCRYPTION=`ENCRYPT_ON`(`0x01`) (we want TLS). Per [MS-TDS] §2.2.6.5.
  - `enum class PreloginEncryption { Off=0, On=1, NotSup=2, Req=3 };`
  - `bool parse_prelogin_response(const uint8_t* payload, size_t n, PreloginEncryption& enc);` — reads the ENCRYPTION option from the server's PRELOGIN response payload (payload = bytes AFTER the 8-byte packet header).

- [ ] **Step 1: Write failing tests**
```cpp
TEST_CASE("prelogin request is a valid 0x12 message advertising encryption") {
    auto m = build_prelogin();
    REQUIRE(m.size() > 8);
    CHECK(m[0] == 0x12);                 // PRELOGIN packet type
    CHECK((m[1] & 0x01) == 0x01);        // EOM
    // The option table starts at byte 8; first option token is VERSION (0x00),
    // and a terminator token (0xFF) ends the table. Assert the table is
    // terminated and an ENCRYPTION option (token 0x01) is present.
    bool has_enc = false, terminated = false;
    for (size_t i = 8; i + 1 < m.size(); ++i) {
        if (m[i] == 0xFF) { terminated = true; break; }
        if (m[i] == 0x01) has_enc = true;
    }
    CHECK(has_enc);
    CHECK(terminated);
}

TEST_CASE("parse_prelogin_response reads the ENCRYPTION option") {
    // Minimal server PRELOGIN response payload: ENCRYPTION option (token 0x01)
    // at offset, value ENCRYPT_ON(0x01), terminated by 0xFF.
    // Option table entry: token(1) offset(2,BE) length(2,BE); then 0xFF; then data.
    std::vector<uint8_t> p = {
        0x01, 0x00, 0x06, 0x00, 0x01,   // ENCRYPTION @ offset 6, len 1
        0xFF,                           // terminator
        0x01                            // data: ENCRYPT_ON
    };
    PreloginEncryption enc{};
    REQUIRE(parse_prelogin_response(p.data(), p.size(), enc));
    CHECK(enc == PreloginEncryption::On);
}
```

- [ ] **Step 2: Run → FAIL.**

- [ ] **Step 3: Implement** `build_prelogin` and `parse_prelogin_response` per [MS-TDS] §2.2.6.5: an option table of `{token(1), offset(2 BE), length(2 BE)}` entries terminated by `0xFF`, followed by the option data. Include at minimum VERSION(`0x00`, 6 bytes: 4-byte version + 2-byte subbuild) and ENCRYPTION(`0x01`, 1 byte = `0x01` ENCRYPT_ON). Offsets are measured from the start of the option table data region per the spec. `parse_prelogin_response` walks the option table, finds token `0x01`, and reads its 1 data byte into `enc`. Build the header with `write_header(out, TDS_PKT_PRELOGIN, TDS_STATUS_EOM, total)`.

- [ ] **Step 4: Run → PASS.** (`--test-case=*prelogin*`)

- [ ] **Step 5: Commit**
```
git commit -am "feat(mssql): build PRELOGIN and parse its encryption option

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 5: LOGIN7 build + login-response token parse (pure)

**Files:** Modify `src/sql_backend/tds_protocol.{h,cpp}`; add tests.

**Interfaces:**
- Produces:
  - `struct Login7Params { std::string hostname; std::string username; std::string password; std::string app_name; std::string server_name; std::string database; };`
  - `std::vector<uint8_t> build_login7(const Login7Params&);` — a complete LOGIN7 message per [MS-TDS] §2.2.6.4: a fixed-length header (Length, TDSVersion=`0x74000004` for 7.4, PacketSize, ClientProgVer, ClientPID, ConnectionID, OptionFlags1-3, TypeFlags, ClientTimeZone, ClientLCID), then the variable offset/length table (OffsetLength block), then the UCS-2LE string data, with the password field obfuscated via `obfuscate_password`. Offsets are relative to the start of the LOGIN7 packet data (the byte after the packet header, i.e. position 0 of the LOGIN7 structure).
  - `struct LoginResult { bool authenticated=false; uint32_t error_number=0; std::string message; };`
  - `LoginResult parse_login_response(const uint8_t* payload, size_t n);` — walks the token stream: LOGINACK(`0xAD`) → authenticated=true; ERROR(`0xAA`) → authenticated=false, fills error_number+message; ignores INFO(`0xAB`), ENVCHANGE(`0xE3`); stops at DONE(`0xFD`). Token framing per [MS-TDS] §2.2.4 / §2.2.7.

- [ ] **Step 1: Write failing tests**
```cpp
TEST_CASE("login7 has TDS 7.4 version and obfuscated password at its offset") {
    Login7Params p;
    p.hostname="h"; p.username="sa"; p.password="abc";
    p.app_name="openads"; p.server_name="srv"; p.database="db";
    auto m = build_login7(p);
    REQUIRE(m.size() > 8 + 36);          // header + fixed LOGIN7 prefix
    CHECK(m[0] == 0x10);                  // LOGIN7 packet type
    // TDSVersion field (LOGIN7 offset 4..7, little-endian 0x74000004).
    size_t base = 8;                      // packet header is 8 bytes
    CHECK(m[base+4] == 0x04);
    CHECK(m[base+7] == 0x74);
    // The obfuscated bytes of "abc" (0xB3 0xA5 0x83 0xA5 0x93 0xA5) appear once.
    std::vector<uint8_t> needle = {0xB3,0xA5,0x83,0xA5,0x93,0xA5};
    auto it = std::search(m.begin(), m.end(), needle.begin(), needle.end());
    CHECK(it != m.end());
}

TEST_CASE("parse_login_response: LOGINACK+DONE = authenticated") {
    // LOGINACK token 0xAD with a length and minimal body, then DONE 0xFD.
    std::vector<uint8_t> s = {
        0xAD, 0x06,0x00, 0x07, 0x74,0x00,0x00,0x04, 0x00,
        0xFD, 0x00,0x00, 0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
    };
    auto r = parse_login_response(s.data(), s.size());
    CHECK(r.authenticated == true);
}

TEST_CASE("parse_login_response: ERROR token = not authenticated with number") {
    // ERROR token 0xAA: length(2), number(4), state(1), class(1),
    // msgtext(US_VARCHAR: len(2) + UCS-2LE), server/proc (B_VARCHAR), line(4).
    // Build a minimal one with number 18456 (login failed).
    std::vector<uint8_t> s = {
        0xAA, 0x0E,0x00,                  // token + length(14) (illustrative)
        0x18,0x48,0x00,0x00,              // number 18456 LE
        0x01, 0x0E,                       // state, class
        0x00,0x00,                        // msg US_VARCHAR len 0
        0x00,                             // server B_VARCHAR len 0
        0x00,                             // proc B_VARCHAR len 0
        0x01,0x00,0x00,0x00,              // line
        0xFD, 0x02,0x00, 0x00,0x00, 0,0,0,0,0,0,0,0
    };
    auto r = parse_login_response(s.data(), s.size());
    CHECK(r.authenticated == false);
    CHECK(r.error_number == 18456);
}
```
(The exact token bodies above are illustrative scaffolds — when implementing, build the test vectors to match the real token framing in [MS-TDS] §2.2.7.6 (LOGINACK), §2.2.7.10 (ERROR), §2.2.7.8 (DONE) so the asserts encode true bytes.)

- [ ] **Step 2: Run → FAIL.**

- [ ] **Step 3: Implement** `build_login7` and `parse_login_response` strictly per [MS-TDS] §2.2.6.4 and §2.2.7. Use `obfuscate_password` for the password field; all strings UCS-2LE; offsets in the OffsetLength block relative to the LOGIN7 structure start; the leading 4-byte Length covers the whole LOGIN7 structure. For `parse_login_response`, iterate tokens by their documented framing (fixed-length vs variable-length-with-length-prefix), recognise `0xAD`/`0xAA`/`0xAB`/`0xE3`/`0xFD`, and decode the ERROR `Number` (4-byte LE) and message (US_VARCHAR, UCS-2LE → UTF-8). Never place the password anywhere except the obfuscated LOGIN7 field.

- [ ] **Step 4: Run → PASS.** (`--test-case=*login*`)

- [ ] **Step 5: Commit**
```
git commit -am "feat(mssql): build LOGIN7 and parse the login-response token stream

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 6: `mssql_uri` parse (pure)

**Files:** Create `src/sql_backend/mssql_uri.{h,cpp}`; test `tests/unit/mssql_uri_test.cpp` (register in CMake); add `mssql_uri.cpp` to the `OPENADS_WITH_MSSQL` sources block.

**Interfaces:**
- Produces: `struct MssqlUri { std::string host; uint16_t port=1433; std::string user; std::string password; std::string database; bool trust_server_cert=true; };` and `bool parse_mssql_uri(const std::string& uri, MssqlUri& out);` accepting `mssql://user:pass@host[:port]/database` and `tds://…`, percent-decoding user/password.

- [ ] **Step 1: Write failing test**
```cpp
#include "doctest.h"
#if defined(OPENADS_WITH_MSSQL)
#include "sql_backend/mssql_uri.h"
using namespace openads::sql_backend;
TEST_CASE("parse mssql uri with port and percent-encoded password") {
    MssqlUri u;
    REQUIRE(parse_mssql_uri("mssql://sa:p%40ss@10.0.0.5:1433/openads_test", u));
    CHECK(u.host == "10.0.0.5");
    CHECK(u.port == 1433);
    CHECK(u.user == "sa");
    CHECK(u.password == "p@ss");          // %40 decoded
    CHECK(u.database == "openads_test");
}
TEST_CASE("parse mssql uri default port and tds scheme") {
    MssqlUri u;
    REQUIRE(parse_mssql_uri("tds://u:p@host/db", u));
    CHECK(u.port == 1433);
    CHECK(u.host == "host");
}
TEST_CASE("reject non-mssql uri") {
    MssqlUri u;
    CHECK(parse_mssql_uri("odbc://x", u) == false);
}
#endif
```

- [ ] **Step 2: Run → FAIL.**
- [ ] **Step 3: Implement** `parse_mssql_uri`: strip `mssql://` or `tds://` (else return false); split `user:pass@host:port/db`; percent-decode user+pass; default port 1433; require host and database non-empty.
- [ ] **Step 4: Run → PASS.**
- [ ] **Step 5: Commit**
```
git commit -am "feat(mssql): parse mssql:// / tds:// connection URIs

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 7: TLS-in-TDS channel, connection orchestration, ABI dispatch, live auth test

**Files:**
- Create: `src/sql_backend/tds_tls_channel.{h,cpp}`, `src/sql_backend/mssql_connection.{h,cpp}`
- Modify: `src/abi/ace_exports.cpp` (the `#if OPENADS_WITH_MSSQL` connect block + AdsDisconnect arm + `HandleKind::MssqlConnection`), `src/CMakeLists.txt` (add the two sources)
- Modify: `tests/unit/abi_plus_mssql_connect_test.cpp` (add the gated live test)
- Create: `tools/scripts/run_mssql_connect_test.ps1`

**Interfaces:**
- Consumes: `tds_protocol` (Tasks 3-5), `MssqlUri` (Task 6), `network/socket.h` (`connect_tcp`, `sock_send`, `sock_recv`, `sock_close`), mbedtls (already linked under `OPENADS_WITH_TLS`).
- Produces:
  - `class TdsTlsChannel` — `static util::Result<TdsTlsChannel> connect(const MssqlUri&);` (does TCP connect + PRELOGIN + tunnelled TLS handshake), `util::Result<void> send_tds(uint8_t type, const std::vector<uint8_t>& payload);`, `util::Result<std::pair<uint8_t,std::vector<uint8_t>>> recv_tds();`, `void close();`.
  - `class MssqlConnection` — `static util::Result<MssqlConnection> open(const MssqlUri&);`, `void disconnect();`, `bool valid() const;`.
  - `HandleKind::MssqlConnection` registered; `AdsConnect60(mssql://…)` returns 0 on auth success, an auth error code on failure; `AdsDisconnect` closes.

- [ ] **Step 1: Write the gated live test (RED against the real server)**

Add to `tests/unit/abi_plus_mssql_connect_test.cpp` under `#if defined(OPENADS_WITH_MSSQL)`:
```cpp
#if defined(OPENADS_WITH_MSSQL)
namespace {
const char* mssql_connstr() {
    const char* v = std::getenv("OPENADS_TEST_MSSQL_CONNSTR");
    return (v && v[0]) ? v : nullptr;
}
}
TEST_CASE("ABI: mssql native connect authenticates over TCP") {
    const char* cs = mssql_connstr();
    if (!cs) { MESSAGE("OPENADS_TEST_MSSQL_CONNSTR not set; skipping"); return; }
    std::vector<UNSIGNED8> srv(std::strlen(cs)+1);
    std::memcpy(srv.data(), cs, std::strlen(cs)+1);
    ADSHANDLE hConn = 0;
    CHECK(AdsConnect60(srv.data(), ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn) == 0);
    CHECK(hConn != 0);
    CHECK(AdsDisconnect(hConn) == 0);
}
TEST_CASE("ABI: mssql native connect rejects a wrong password") {
    const char* cs = mssql_connstr();
    if (!cs) { MESSAGE("skipping"); return; }
    // Corrupt the password segment: replace ':<pass>@' — simplest is to append
    // a bogus suffix to the password via a hand-built bad connstr from parts.
    std::string bad = std::string(cs);
    auto at = bad.find('@'); auto col = bad.rfind(':', at);
    REQUIRE(at != std::string::npos); REQUIRE(col != std::string::npos);
    bad.insert(at, "WRONG");           // corrupt the password
    std::vector<UNSIGNED8> srv(bad.size()+1);
    std::memcpy(srv.data(), bad.c_str(), bad.size()+1);
    ADSHANDLE hConn = 0;
    CHECK(AdsConnect60(srv.data(), ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn) != 0);
}
#endif
```

- [ ] **Step 2: Run → FAIL** (connect not implemented; the `#if OPENADS_WITH_MSSQL` block in AdsConnect60 is still empty so the URI is unrecognised). Confirm the live test is reached (env set via the run script below).

- [ ] **Step 3: Implement `TdsTlsChannel`** — TCP `connect_tcp(host, port)`; send `build_prelogin()` as a `0x12` packet; recv the PRELOGIN response and `parse_prelogin_response`; if encryption negotiated, run `mbedtls_ssl_handshake` with custom BIO callbacks: the send callback wraps mbedtls output in a `0x12` TDS packet (`write_header` + payload) and `sock_send`s it; the recv callback reads one TDS `0x12` packet, strips the 8-byte header, and feeds the payload to mbedtls — **only while `handshake_done_` is false**; once handshake completes, set the flag and route `send_tds`/`recv_tds` through `mbedtls_ssl_write`/`mbedtls_ssl_read` directly (segmenting payloads larger than the negotiated packet size and reassembling multi-packet replies by the EOM status bit). Use `TlsConfig{insecure_skip_verify = uri.trust_server_cert}`. Reuse the mbedtls setup pattern from `src/network/tls_transport.cpp` (ctr_drbg seed, ssl_config_defaults CLIENT) but with the custom BIO instead of `mbedtls_net_*`.

- [ ] **Step 4: Implement `MssqlConnection::open`** — `TdsTlsChannel::connect(uri)` → `channel.send_tds(TDS_PKT_LOGIN7, build_login7(params_from_uri))` → `auto [type, payload] = channel.recv_tds()` → `parse_login_response(payload)` → on `authenticated` keep the channel; else `util::Error{result.error_number, 0, result.message, ""}` (NO connstr/password in the error). `disconnect()` closes the channel.

- [ ] **Step 5: Wire the ABI** — in `AdsConnect60`, the `#if defined(OPENADS_WITH_MSSQL)` block: `MssqlUri u; if (parse_mssql_uri(path, u)) { auto c = MssqlConnection::open(u); if (!c) return <map error>; register HandleKind::MssqlConnection; return 0; }`. Add the `MssqlConnection` arm to `AdsDisconnect` (close + free handle), mirroring the Odbc arm. Add `HandleKind::MssqlConnection` to the enum.

- [ ] **Step 6: Add sources to CMake + write the run script**

`src/CMakeLists.txt` `OPENADS_WITH_MSSQL` sources: add `sql_backend/mssql_uri.cpp`, `sql_backend/tds_tls_channel.cpp`, `sql_backend/mssql_connection.cpp`.
`tools/scripts/run_mssql_connect_test.ps1`:
```powershell
param([string]$BuildDir = "build\mssql-verify")
. "$PSScriptRoot\mssql_test_env.local.ps1"   # local, untracked; sets the connstr
& "$PSScriptRoot\..\..\$BuildDir\tests\openads_unit_tests.exe" --test-case=*mssql*connect*
exit $LASTEXITCODE
```

- [ ] **Step 7: Build and run live → PASS**

Build (Global Constraints), then:
```
pwsh tools\scripts\run_mssql_connect_test.ps1
```
Expected: both live cases PASS — a correct connstr authenticates (AdsConnect60 == 0, AdsDisconnect == 0) and a corrupted password returns non-zero. Also run the pure suite once: `--test-case=*tds* *mssql*uri*` stays green.

- [ ] **Step 8: Commit**
```
git add src/sql_backend/tds_tls_channel.* src/sql_backend/mssql_connection.* src/abi/ace_exports.cpp src/CMakeLists.txt tests/unit/abi_plus_mssql_connect_test.cpp tools/scripts/run_mssql_connect_test.ps1
git commit -m "feat(mssql): TLS-in-TDS channel, LOGIN7 auth, and mssql:// ABI connect

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Final verification

- [ ] Pure unit suite green with the flag: `build\mssql-verify\tests\openads_unit_tests.exe --test-case=*tds* ; --test-case=*mssql*uri*`.
- [ ] Disabled-path test green in a no-mssql build.
- [ ] Live: `pwsh tools\scripts\run_mssql_connect_test.ps1` — correct creds authenticate, wrong password rejected, against SQL Express over TCP.
- [ ] `git push origin pr/openads-plus-mssql` after each task.
- [ ] No regression in the inherited ODBC suite (the base branch): `--test-case=*odbc*` still green.
- [ ] Update memory `project_openads_native_firebird_mssql` + the central ledger (MSSQL connect slice done) once the live auth passes.
