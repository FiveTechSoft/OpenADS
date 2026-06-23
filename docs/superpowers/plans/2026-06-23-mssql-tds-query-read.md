# Native MS SQL Server (TDS) — Sub-project 2 (Query + read) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Run a `SELECT` over the authenticated TDS channel, parse the TABULAR_RESULT token stream (COLMETADATA/ROW/DONE), decode the common SQL Server column types, and expose the rows through the navigational ACE ABI (read-only), mirroring the ODBC/Firebird mold.

**Architecture:** Extend the pure byte layer (`tds_protocol`) with a SQL_BATCH builder and a result-set token-stream parser + per-type decoder — fully unit-testable without a server. `MssqlConnection::query()` runs one batch over the existing `TdsTlsChannel`. A new buffered `MssqlTable` materialises `SELECT * FROM <name>` once and serves navigation/field reads from memory. The ACE ABI clones the ODBC table read sites for an `MssqlTable` handle; write/seek return not-available (sub-project 3).

**Tech Stack:** C++17, MS-TDS 7.4 wire protocol, mbedtls (already linked), doctest, CMake (MSVC x64 + winlibs NMake), SQL Server 2025 Express (live test target).

## Global Constraints

- Source-clean PUBLIC repo: NO internal drive-letter paths, NO third-party product names beyond unavoidable technical facts ("TDS", "SQL Server" are facts), NO business/strategy text, NO credentials in any committed file. Test credentials live in env / a local untracked file only.
- Commit trailer: end every commit with exactly `Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>` and nothing else.
- Branch: `pr/openads-plus-mssql` (worktree `H:/DEVAI/_Prj/OpenADS-mssql`). Push after each task.
- Scope is QUERY + READ + NAV only. NO write (append/update/delete), NO seek/PK snapshot, NO driver-matrix entry, NO PLP/large/exotic types — those are sub-project 3. Write/seek ABI sites return a not-available error for an `MssqlTable`.
- Secrets never cross the ABI in an error or reach a log: a TDS ERROR token's message may be surfaced; the connection string, username, and password must never appear in a returned `util::Error` or any log line.
- Normative reference for all byte layouts: the MS-TDS protocol specification ([MS-TDS], TDS 7.4). Cite section numbers in comments; do not transcribe field tables from memory — follow the spec. Token stream: §2.2.4 / §2.2.7; COLMETADATA §2.2.7.4; ROW §2.2.7.17; NBCROW §2.2.7.13; DONE §2.2.7.5; data types §2.2.5.4–5.5; SQLBatch §2.2.6.7 with ALL_HEADERS §2.2.5.
- New code lives under the existing `OPENADS_WITH_MSSQL` guard (no new flag). Reuse `sql_common.h` (`is_safe_identifier`) and the file-local helpers already in `tds_protocol.cpp` (`push_ucs2le`, `ucs2le_to_utf8`).
- Build (x64), from worktree root `H:/DEVAI/_Prj/OpenADS-mssql`:
  ```
  cmd /c 'call H:\DEVAI\_UtlAI\msvc\msvc_x64_full.bat && cd /d H:\DEVAI\_Prj\OpenADS-mssql && H:\DEVAI\_UtlAI\Mingw\bin\cmake.exe -S . -B build\mssql-verify -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl -DOPENADS_WITH_MSSQL=ON -DOPENADS_WITH_TLS=ON -DOPENADS_WITH_HTTP=OFF -DOPENADS_WARNINGS_AS_ERRORS=OFF -DCMAKE_TLS_VERIFY=0 && H:\DEVAI\_UtlAI\Mingw\bin\cmake.exe --build build\mssql-verify'
  ```
- Run pure unit tests (no server): `build\mssql-verify\tests\openads_unit_tests.exe --test-case=*tds*`.
- Run the live read test: set env via the local untracked `tools/scripts/mssql_test_env.local.ps1` (already exists from sub-project 1; exports `OPENADS_TEST_MSSQL_CONNSTR`), then `--test-case=*mssql*read*`.

---

## File Structure

- `src/sql_backend/tds_protocol.h` / `.cpp` — EXTEND: `build_sql_batch`, `token_length_class`, `TdsColumn`/`TdsCell`/`QueryResult`, `parse_query_response`, `decode_cell`.
- `src/sql_backend/tds_tls_channel.cpp` — EDIT: `recv_tds` total reassembly cap.
- `src/sql_backend/mssql_connection.h` / `.cpp` — EXTEND: `query(sql)`.
- `src/sql_backend/mssql_table.h` / `.cpp` — NEW: buffered table + cursor + field reads.
- `src/abi/ace_exports.cpp` — `get_mssql_table` helper; `HandleKind::MssqlTable`; `AdsOpenTable`/`AdsCloseTable` arms; cloned read sites; write/seek not-available arms.
- `src/CMakeLists.txt` — add `mssql_table.cpp` to the `OPENADS_WITH_MSSQL` sources.
- `tests/unit/tds_protocol_test.cpp` — EXTEND: batch builder, token-class, COLMETADATA, decode, parse_query_response (incl. malformed-length fail-closed).
- `tests/unit/abi_plus_mssql_read_test.cpp` — NEW: gated live read test.
- `docs/superpowers/specs/2026-06-23-mssql-tds-connect-design.md` — EDIT (Task 8): reconcile the ENCRYPT_NOT_SUP note with the always-TLS implementation.

---

### Task 1: SQL_BATCH builder (pure)

**Files:**
- Modify: `src/sql_backend/tds_protocol.h` / `.cpp`
- Test: `tests/unit/tds_protocol_test.cpp`

**Interfaces:**
- Consumes: `push_ucs2le` (file-local in `tds_protocol.cpp`).
- Produces: `std::vector<uint8_t> build_sql_batch(const std::string& utf8_sql);` — the SQLBatch message **body** (bytes AFTER the 8-byte TDS header; the channel frames the header, exactly like the `build_login7` body is fed to `send_tds`). Layout per [MS-TDS] §2.2.6.7: an ALL_HEADERS stream (§2.2.5) — `TotalLength(4 LE)` then one header `{ HeaderLength(4 LE)=18, HeaderType(2 LE)=0x0002 (Transaction Descriptor), TransactionDescriptor(8)=0, OutstandingRequestCount(4 LE)=1 }` — followed by the SQL text as UCS-2LE.

- [ ] **Step 1: Write the failing test**
```cpp
TEST_CASE("build_sql_batch: ALL_HEADERS prefix + UCS-2LE text") {
    auto m = build_sql_batch("SELECT 1");
    // ALL_HEADERS: TotalLength(4) = 4 + 18 = 22 (0x16); then one 18-byte header.
    REQUIRE(m.size() == 22 + 8 * 2);            // 22-byte ALL_HEADERS + "SELECT 1" UCS-2LE
    CHECK(m[0] == 0x16); CHECK(m[1] == 0x00); CHECK(m[2] == 0x00); CHECK(m[3] == 0x00);
    CHECK(m[4] == 0x12); CHECK(m[5] == 0x00); CHECK(m[6] == 0x00); CHECK(m[7] == 0x00); // HeaderLength=18
    CHECK(m[8] == 0x02); CHECK(m[9] == 0x00);   // HeaderType = 0x0002 (txn descriptor)
    // OutstandingRequestCount = 1 at bytes 18..21
    CHECK(m[18] == 0x01); CHECK(m[19] == 0x00); CHECK(m[20] == 0x00); CHECK(m[21] == 0x00);
    // SQL text begins at byte 22: 'S' 0x00 'E' 0x00 ...
    CHECK(m[22] == 'S'); CHECK(m[23] == 0x00);
    CHECK(m[24] == 'E'); CHECK(m[25] == 0x00);
}
```

- [ ] **Step 2: Run → FAIL** (`build_sql_batch` undefined). Build via Global Constraints, then `--test-case=*sql_batch*`.

- [ ] **Step 3: Implement** in `tds_protocol.cpp` (inside `#if OPENADS_WITH_MSSQL`):
```cpp
std::vector<uint8_t> build_sql_batch(const std::string& utf8_sql) {
    std::vector<uint8_t> out;
    auto put_u32le = [&out](uint32_t v) {
        out.push_back(uint8_t(v & 0xFF));        out.push_back(uint8_t((v >> 8) & 0xFF));
        out.push_back(uint8_t((v >> 16) & 0xFF)); out.push_back(uint8_t((v >> 24) & 0xFF));
    };
    // ALL_HEADERS (§2.2.5): TotalLength then one Transaction Descriptor header.
    put_u32le(4 + 18);          // TotalLength = its own 4 bytes + the 18-byte header
    put_u32le(18);              // HeaderLength
    out.push_back(0x02); out.push_back(0x00);    // HeaderType = 2 (txn descriptor)
    for (int i = 0; i < 8; ++i) out.push_back(0);// TransactionDescriptor = 0
    put_u32le(1);               // OutstandingRequestCount = 1
    push_ucs2le(out, utf8_sql); // SQL text, UCS-2LE
    return out;
}
```
Declare it in `tds_protocol.h`. (`push_ucs2le` is the existing file-local helper; if it is `static`, drop the `static` or add an internal forward declaration so `build_sql_batch` can call it — it is in the same TU, so no change is needed.)

- [ ] **Step 4: Run → PASS.** `--test-case=*sql_batch*`.

- [ ] **Step 5: Commit**
```
git add src/sql_backend/tds_protocol.* tests/unit/tds_protocol_test.cpp
git commit -m "feat(mssql): SQL_BATCH builder with ALL_HEADERS stream (pure)

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 2: Token length-class table + parse_login_response refactor (carry Important-A)

This replaces the sub-project-1 review's flagged "unknown token: try a 2-byte LE length" heuristic with a documented per-token-class length table. COLMETADATA/ROW walking (Task 5) needs the correct classification; the login parser uses the same table so the two parsers stay consistent.

**Files:** Modify `src/sql_backend/tds_protocol.{h,cpp}`; tests in `tests/unit/tds_protocol_test.cpp`.

**Interfaces:**
- Produces:
  - `enum class TokenLenClass { ZeroLength, FixedLength, VarLenByteCount, VarLenUShort, VarLenULong, ColMetaDataDriven, Done, Unknown };`
  - `TokenLenClass token_length_class(uint8_t token, uint8_t& fixed_len);` — returns the class and, for `FixedLength`, sets `fixed_len` (e.g. DONE family is handled as a fixed 12-byte body via its own class). Classification per [MS-TDS] §2.2.4 token definitions (the high 2 bits of a data-token's id encode its length sub-rule, but control tokens like ERROR/INFO/LOGINACK/ENVCHANGE are USHORT-length-prefixed; DONE/DONEPROC/DONEINPROC carry a fixed 12-byte body; COLMETADATA/ROW/NBCROW are parsed structurally, returned as `ColMetaDataDriven`).
- `parse_login_response` (existing) is refactored so its "unknown token" branch consults `token_length_class` instead of blindly assuming a 2-byte length.

- [ ] **Step 1: Write the failing test**
```cpp
TEST_CASE("token_length_class classifies the control tokens we skip") {
    uint8_t fx = 0;
    CHECK(token_length_class(0xAA, fx) == TokenLenClass::VarLenUShort); // ERROR
    CHECK(token_length_class(0xAB, fx) == TokenLenClass::VarLenUShort); // INFO
    CHECK(token_length_class(0xAD, fx) == TokenLenClass::VarLenUShort); // LOGINACK
    CHECK(token_length_class(0xE3, fx) == TokenLenClass::VarLenUShort); // ENVCHANGE
    CHECK(token_length_class(0xA9, fx) == TokenLenClass::VarLenUShort); // ORDER
    CHECK(token_length_class(0xFD, fx) == TokenLenClass::Done);         // DONE
    CHECK(token_length_class(0x79, fx) == TokenLenClass::FixedLength);  // RETURNSTATUS (4)
    CHECK((token_length_class(0x79, fx) == TokenLenClass::FixedLength && fx == 4));
    CHECK(token_length_class(0x81, fx) == TokenLenClass::ColMetaDataDriven); // COLMETADATA
    CHECK(token_length_class(0xD1, fx) == TokenLenClass::ColMetaDataDriven); // ROW
    CHECK(token_length_class(0xD2, fx) == TokenLenClass::ColMetaDataDriven); // NBCROW
}
TEST_CASE("parse_login_response still authenticates after refactor (regression)") {
    // Reuse the exact LOGINACK+DONE vector from the connect tests.
    std::vector<uint8_t> s = {
        0xAD, 0x06,0x00, 0x07, 0x74,0x00,0x00,0x04, 0x00,
        0xFD, 0x00,0x00, 0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
    };
    auto r = parse_login_response(s.data(), s.size());
    CHECK(r.authenticated == true);
}
```

- [ ] **Step 2: Run → FAIL** (`token_length_class` undefined; login regression test passes already — keep it green).

- [ ] **Step 3: Implement** `token_length_class` in `tds_protocol.cpp` per [MS-TDS] §2.2.4. Map the control tokens we handle (ERROR `0xAA`, INFO `0xAB`, LOGINACK `0xAD`, ENVCHANGE `0xE3`, ORDER `0xA9` → `VarLenUShort`; DONE/DONEPROC/DONEINPROC `0xFD`/`0xFE`/`0xFF` → `Done`; RETURNSTATUS `0x79` → `FixedLength`, 4; COLMETADATA `0x81`/ROW `0xD1`/NBCROW `0xD2` → `ColMetaDataDriven`; everything else → `Unknown`). Then refactor the `parse_login_response` "unknown token" branch: for `VarLenUShort` skip a 2-byte LE length + that many bytes; for `Done` stop; for `Unknown` stop fail-safe (cannot advance safely). Keep all existing login tests green.

- [ ] **Step 4: Run → PASS.** `--test-case=*token_length* *login*`.

- [ ] **Step 5: Commit**
```
git commit -am "feat(mssql): per-token length-class table; use it in login parse

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 3: COLMETADATA parse → column metadata (pure)

**Files:** Modify `src/sql_backend/tds_protocol.{h,cpp}`; tests.

**Interfaces:**
- Produces:
  - `struct TdsColumn { std::string name; uint8_t type_token=0; uint32_t length=0; uint8_t precision=0; uint8_t scale=0; uint16_t codepage=0; };`
  - `bool parse_colmetadata(const uint8_t* p, size_t n, size_t& pos, std::vector<TdsColumn>& cols);` — `pos` points at the byte AFTER the `0x81` token id; on success advances `pos` past the COLMETADATA token and fills `cols`. Per [MS-TDS] §2.2.7.4: `Count(2 LE)` columns; each column = `UserType(4 LE)`, `Flags(2 LE)`, then a TYPE_INFO (§2.2.5.4–5.5), then `ColName` (B_VARCHAR: len(1) + UCS-2LE). Returns false (fail-closed) on any short read.
- TYPE_INFO decoding the **common** set: fixed-length tokens (INT1 `0x30`/INT2 `0x34`/INT4 `0x38`/INT8 `0x7F`, BIT `0x32`, DATETIM4 `0x3A`, DATETIME `0x3D`, MONEY4 `0x7A`, MONEY `0x3C`, FLT4 `0x3B`, FLT8 `0x3E`) carry NO extra type bytes; the *N variable tokens carry a 1-byte max-length (INTN `0x26`, BITN `0x68`, MONEYN `0x6E`, FLTN `0x6D`, DATETIMN `0x6F`, DATEN `0x28`(len 0), DATETIME2N `0x2A`(1-byte scale)); DECIMALN `0x6A`/NUMERICN `0x6C` carry `max-len(1)`, `precision(1)`, `scale(1)`; BIGCHAR `0xAF`/BIGVARCHR `0xA7`/NCHAR `0xEF`/NVARCHAR `0xE7` carry `max-len(2 LE)` + 5-byte COLLATION. Any other token → return false (caller surfaces "unsupported type").

- [ ] **Step 1: Write the failing test**
```cpp
TEST_CASE("parse_colmetadata: int + nvarchar columns") {
    // 2 columns: [INT4N maxlen 4] "id", [NVARCHAR maxlen 100, collation] "nome".
    std::vector<uint8_t> p = {
        0x02,0x00,                              // Count = 2
        // col 1: id INTN(4)
        0x00,0x00,0x00,0x00, 0x00,0x00,         // UserType, Flags
        0x26, 0x04,                             // INTN, max-len 4
        0x02, 'i',0x00,'d',0x00,                // ColName "id" (2 chars UCS-2LE)
        // col 2: nome NVARCHAR(100)
        0x00,0x00,0x00,0x00, 0x00,0x00,
        0xE7, 0xC8,0x00,                        // NVARCHAR, max-len 200 bytes (100 chars)
        0x09,0x04,0xD0,0x00,0x34,               // 5-byte collation (LCID/flags/sortid)
        0x04, 'n',0x00,'o',0x00,'m',0x00,'e',0x00,
    };
    std::vector<TdsColumn> cols; size_t pos = 0;
    REQUIRE(parse_colmetadata(p.data(), p.size(), pos, cols));
    REQUIRE(cols.size() == 2);
    CHECK(cols[0].name == "id");   CHECK(cols[0].type_token == 0x26);
    CHECK(cols[1].name == "nome"); CHECK(cols[1].type_token == 0xE7);
    CHECK(pos == p.size());
}
TEST_CASE("parse_colmetadata: unsupported type fails closed") {
    std::vector<uint8_t> p = {
        0x01,0x00, 0,0,0,0, 0,0, 0x24 /* GUIDTYPE, unsupported */, 0x10,
        0x01, 'g',0x00
    };
    std::vector<TdsColumn> cols; size_t pos = 0;
    CHECK(parse_colmetadata(p.data(), p.size(), pos, cols) == false);
}
```
(The collation/maxlen bytes above are illustrative — when implementing, follow [MS-TDS] §2.2.5.5 so the asserts encode true byte counts; the NVARCHAR max-len is in BYTES = chars×2.)

- [ ] **Step 2: Run → FAIL.**

- [ ] **Step 3: Implement** `parse_colmetadata` per the spec. Read `Count`, loop reading UserType+Flags then a `read_type_info` helper that, by `type_token`, consumes the right number of TYPE_INFO bytes and fills `length`/`precision`/`scale`/`codepage`; then read the B_VARCHAR ColName via `ucs2le_to_utf8`. Bounds-check every read; return false on short read or an unsupported type token.

- [ ] **Step 4: Run → PASS.** `--test-case=*colmetadata*`.

- [ ] **Step 5: Commit**
```
git commit -am "feat(mssql): parse COLMETADATA into typed column descriptors

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 4: decode_cell for the common type set (pure)

**Files:** Modify `src/sql_backend/tds_protocol.{h,cpp}`; tests.

**Interfaces:**
- Produces: `std::string decode_cell(const TdsColumn& col, const uint8_t* data, size_t len);` — decodes one column value's raw bytes (already extracted by the row reader; `len` is the actual value length, 0 allowed) to a printable string. Pure, table-driven by `col.type_token`:
  - INT1/INT2/INT4/INT8 and INTN(len 1/2/4/8): signed decimal text (INT1/tinyint is unsigned 0..255).
  - BIT / BITN: `"0"` or `"1"`.
  - DECIMALN/NUMERICN: sign byte + little-endian magnitude (4/8/12/16 bytes) → integer, then insert the decimal point per `col.scale`.
  - MONEY/MONEY4/MONEYN: 8-byte (hi 4 / lo 4 combined) or 4-byte integer of 1/10000 units → fixed 4-dp decimal text.
  - FLT4/FLT8/FLTN: IEEE float/double → shortest round-trip text.
  - BIGCHAR/BIGVARCHR: bytes → `std::string` passthrough (v1 treats the single-byte text as Latin1/UTF-8; documented limitation).
  - NCHAR/NVARCHAR: UCS-2LE → UTF-8 via `ucs2le_to_utf8` (`len/2` chars).
  - DATEN: 3-byte little-endian day count since 0001-01-01 → `YYYY-MM-DD`.
  - DATETIM4 (smalldatetime): `days(2 LE)` since 1900-01-01 + `minutes(2 LE)` → `YYYY-MM-DD HH:MM:00`.
  - DATETIME: `days(4 LE signed)` since 1900-01-01 + `ticks(4 LE)` of 1/300 s → `YYYY-MM-DD HH:MM:SS`.
  - DATETIME2N: `time` (scale-dependent, 3–5 bytes, 10^-scale-second units) + `date` (3-byte day count since 0001-01-01) → `YYYY-MM-DD HH:MM:SS[.fff]`.

- [ ] **Step 1: Write failing tests (reference vectors)**
```cpp
static TdsColumn col(uint8_t t, uint8_t scale=0){ TdsColumn c; c.type_token=t; c.scale=scale; return c; }
TEST_CASE("decode int / bit / nvarchar") {
    uint8_t i4[] = {0x2A,0x00,0x00,0x00};                 // 42 LE
    CHECK(decode_cell(col(0x26), i4, 4) == "42");          // INTN(4)
    uint8_t b[] = {0x01};
    CHECK(decode_cell(col(0x68), b, 1) == "1");            // BITN
    uint8_t nv[] = {'n',0x00,'o',0x00};                    // "no" UCS-2LE
    CHECK(decode_cell(col(0xE7), nv, 4) == "no");          // NVARCHAR
}
TEST_CASE("decode decimal scale and datetime epoch") {
    // NUMERIC(10,2) value 123.45 -> sign(1)=positive + magnitude 12345 LE.
    uint8_t dec[] = {0x01, 0x39,0x30,0x00,0x00};           // 1=positive, 12345 LE
    CHECK(decode_cell(col(0x6C, /*scale*/2), dec, 5) == "123.45");
    // DATETIME = 1900-01-02 00:00:00 -> days=1, ticks=0.
    uint8_t dt[] = {0x01,0x00,0x00,0x00, 0x00,0x00,0x00,0x00};
    CHECK(decode_cell(col(0x3D), dt, 8) == "1900-01-02 00:00:00");
}
```
(Reference values follow [MS-TDS] §2.2.5.5 + the documented epochs; keep the literals exact.)

- [ ] **Step 2: Run → FAIL.**

- [ ] **Step 3: Implement** `decode_cell` with a switch on `col.type_token`, each branch reading the documented byte layout. For DECIMAL/NUMERIC use a wide accumulator (e.g. unsigned 128 via two `uint64_t` or `long double` only for FLT). For the date math, implement a small `civil_from_days` (Howard Hinnant's algorithm) and apply the right epoch offset per type. An unrecognised token returns an empty string (the column was already rejected at COLMETADATA, so this is defensive).

- [ ] **Step 4: Run → PASS.** `--test-case=*decode*`.

- [ ] **Step 5: Commit**
```
git commit -am "feat(mssql): decode the common TDS column types to string

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 5: parse_query_response — ROW/NBCROW walk → QueryResult (pure), incl. malformed-length fail-closed

**Files:** Modify `src/sql_backend/tds_protocol.{h,cpp}`; tests.

**Interfaces:**
- Consumes: `parse_colmetadata` (Task 3), `decode_cell` (Task 4), `token_length_class` (Task 2).
- Produces:
  - `struct TdsCell { std::string value; bool is_null=false; };`
  - `struct QueryResult { std::vector<TdsColumn> columns; std::vector<std::vector<TdsCell>> rows; bool ok=false; uint32_t error_number=0; std::string message; std::string unsupported_type; };`
  - `QueryResult parse_query_response(const uint8_t* payload, size_t n);` — walks tokens: COLMETADATA(`0x81`) → `parse_colmetadata` (sets `columns`; if it returns false because of an unsupported type, set `unsupported_type` and `ok=false` and stop); ROW(`0xD1`) → one cell per column, reading each value's length by the column type's length rule (fixed-len types: known size; *N / char / nchar: a length prefix — 1-byte for the *N/B-style, 2-byte (USHORT, `0xFFFF`=NULL) for BIGCHAR/BIGVARCHR/NCHAR/NVARCHAR; the value bytes then go to `decode_cell`); NBCROW(`0xD2`) → read the null bitmap (`ceil(ncols/8)` bytes) first, then only the non-null columns' values; ERROR(`0xAA`) → set `error_number`/`message`, `ok=false`; INFO/ENVCHANGE/ORDER/RETURNSTATUS → skip by `token_length_class`; DONE/DONEPROC/DONEINPROC → set `ok=true` (a clean stream that reached DONE), stop. Every read is bounds-checked; any short/over-long read sets `ok=false` and stops (no OOB).

- [ ] **Step 1: Write failing tests**
```cpp
TEST_CASE("parse_query_response: one int + nvarchar row") {
    // COLMETADATA (id INTN4, nome NVARCHAR) + one ROW (42, "oi") + DONE.
    std::vector<uint8_t> s;
    auto push = [&](std::initializer_list<uint8_t> b){ for (auto x:b) s.push_back(x); };
    push({0x81, 0x02,0x00});                                  // COLMETADATA, 2 cols
    push({0,0,0,0, 0,0, 0x26,0x04, 0x02,'i',0,'d',0});        // id INTN4
    push({0,0,0,0, 0,0, 0xE7,0xC8,0x00, 0x09,0x04,0xD0,0x00,0x34, // nome NVARCHAR(100)
          0x04,'n',0,'o',0,'m',0,'e',0});
    push({0xD1});                                             // ROW
    push({0x04, 0x2A,0x00,0x00,0x00});                        // id: len 4, value 42
    push({0x04,0x00, 'o',0,'i',0});                           // nome: USHORT len 4, "oi"
    push({0xFD, 0x10,0x00, 0x00,0x00, 0,0,0,0,0,0,0,0});      // DONE (final)
    auto r = parse_query_response(s.data(), s.size());
    REQUIRE(r.ok);
    REQUIRE(r.columns.size() == 2);
    REQUIRE(r.rows.size() == 1);
    CHECK(r.rows[0][0].value == "42");
    CHECK(r.rows[0][1].value == "oi");
}
TEST_CASE("parse_query_response: ERROR token surfaces number, ok=false") {
    std::vector<uint8_t> s = {
        0xAA, 0x0E,0x00, 0x18,0x48,0x00,0x00, 0x01,0x0E,
        0x00,0x00, 0x00, 0x00, 0x01,0x00,0x00,0x00,
        0xFD, 0x02,0x00, 0,0, 0,0,0,0,0,0,0,0
    };
    auto r = parse_query_response(s.data(), s.size());
    CHECK(r.ok == false);
    CHECK(r.error_number == 18456);
}
TEST_CASE("parse_query_response: malformed length terminates fail-closed (no OOB)") {
    // ROW value claims a 4-byte int but only 1 byte remains.
    std::vector<uint8_t> s;
    auto push = [&](std::initializer_list<uint8_t> b){ for (auto x:b) s.push_back(x); };
    push({0x81, 0x01,0x00, 0,0,0,0, 0,0, 0x26,0x04, 0x02,'i',0,'d',0}); // 1 col id INTN4
    push({0xD1, 0x04, 0x2A});                                 // ROW, len 4 but 1 byte left
    auto r = parse_query_response(s.data(), s.size());
    CHECK(r.ok == false);                                     // returned, no crash
}
```

- [ ] **Step 2: Run → FAIL.**

- [ ] **Step 3: Implement** `parse_query_response` as the token walker described in Interfaces, delegating COLMETADATA to `parse_colmetadata`, each value to `decode_cell`, and unknown control tokens to `token_length_class`. A per-column "wire length rule" helper decides how many length bytes precede each value (fixed types: none; the variable types: 1- or 2-byte prefix; `0xFFFF`/`0xFF` sentinel = NULL → `TdsCell{is_null=true}`). Stop fail-closed on any bounds violation.

- [ ] **Step 4: Run → PASS.** `--test-case=*query_response*`.

- [ ] **Step 5: Commit**
```
git commit -am "feat(mssql): parse TABULAR_RESULT into a typed QueryResult (fail-closed)

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 6: recv_tds reassembly cap + MssqlConnection::query

**Files:** Modify `src/sql_backend/tds_tls_channel.cpp`; `src/sql_backend/mssql_connection.{h,cpp}`.

**Interfaces:**
- Consumes: `build_sql_batch` (Task 1), `parse_query_response` (Task 5), the existing `TdsTlsChannel::send_tds`/`recv_tds`.
- Produces: `util::Result<tds::QueryResult> MssqlConnection::query(const std::string& sql);`
- Hardening: `recv_tds` gains a named cap `static constexpr size_t kMaxReassembly = 64u * 1024u * 1024u;` — if the accumulated payload would exceed it, return `util::Error` instead of growing unbounded.

- [ ] **Step 1: Add the reassembly cap in `recv_tds`**

In `tds_tls_channel.cpp`, inside the `recv_tds` reassembly loop, after appending each packet's payload, add:
```cpp
if (payload.size() > kMaxReassembly) {
    return util::Error{openads::AE_INVALID_FIELD_NUMBER, 0,
                       "TDS reply exceeds reassembly cap", ""};
}
```
(Use whichever generic non-secret error code the channel already uses for protocol faults; the message must not contain the connstr.) Declare `kMaxReassembly` as a file-local `constexpr` near the top of the TU.

- [ ] **Step 2: Declare + implement `query`**

`mssql_connection.h` (inside the guard, add to the class):
```cpp
#include "sql_backend/tds_protocol.h"
util::Result<tds::QueryResult> query(const std::string& sql);
```
`mssql_connection.cpp`:
```cpp
util::Result<tds::QueryResult> MssqlConnection::query(const std::string& sql) {
    if (!impl_ || !impl_->channel.valid())
        return util::Error{openads::AE_NO_CONNECTION, 0, "MSSQL not connected", ""};
    if (auto r = impl_->channel.send_tds(tds::TDS_PKT_SQLBATCH,
                                         tds::build_sql_batch(sql)); !r)
        return r.error();
    auto reply = impl_->channel.recv_tds();
    if (!reply) return reply.error();
    const auto& payload = reply.value().second;
    tds::QueryResult qr = tds::parse_query_response(payload.data(), payload.size());
    if (!qr.ok) {
        if (!qr.unsupported_type.empty())
            return util::Error{openads::AE_DATA_TOO_LONG, 0,
                               "unsupported MSSQL column type: " + qr.unsupported_type, ""};
        std::int32_t code = qr.error_number ? static_cast<std::int32_t>(qr.error_number)
                                            : openads::AE_INVALID_QUERY;
        return util::Error{code, 0,
                           qr.message.empty() ? std::string("MSSQL query failed") : qr.message, ""};
    }
    return qr;
}
```
(Use the closest existing `AE_*` codes; do not invent new ones unless the header already has a fitting one. Never put `sql` in an error if it could embed a secret — here `sql` is backend-generated, so the server message is safe.)

- [ ] **Step 3: Build → compiles.** No new unit test here (the channel needs a live server); `query` is exercised by the live read test in Task 8. Run the pure suite to confirm no regression: `--test-case=*tds*`.

- [ ] **Step 4: Commit**
```
git add src/sql_backend/tds_tls_channel.cpp src/sql_backend/mssql_connection.*
git commit -m "feat(mssql): MssqlConnection::query + recv_tds reassembly cap

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 7: MssqlTable + ABI read sites (HandleKind::MssqlTable)

**Files:**
- Create: `src/sql_backend/mssql_table.{h,cpp}`
- Modify: `src/abi/ace_exports.cpp` (helper, HandleKind, AdsOpenTable/AdsCloseTable arms, cloned read sites, write/seek not-available arms), `src/CMakeLists.txt`

**Interfaces:**
- Consumes: `MssqlConnection::query` (Task 6), `is_safe_identifier` (sql_common), the ODBC read-site shape (`get_odbc_table` blocks at the line numbers found by `grep -n "get_odbc_table" src/abi/ace_exports.cpp`).
- Produces:
  - `class MssqlTable` (in `mssql_table.h`):
    ```cpp
    struct MssqlTable {
        tds::QueryResult data;            // buffered SELECT * result
        std::size_t pos = 0;              // 0-based cursor; == rows.size() => EOF
        bool bof = false, eof = false;
        bool last_found = false;
        static util::Result<std::unique_ptr<MssqlTable>>
            open(MssqlConnection& c, const std::string& table_name);  // runs SELECT *
        static std::unique_ptr<MssqlTable> from_result(tds::QueryResult qr); // passthrough
        void go_top(); void go_bottom(); void skip(long n);
        bool at_bof() const; bool at_eof() const;
        std::size_t field_count() const;
        std::string field_name(std::size_t i) const;
        uint16_t field_type(std::size_t i) const;       // ADS field-type mapping
        uint32_t field_length(std::size_t i) const;
        uint16_t field_decimals(std::size_t i) const;
        uint32_t record_num() const;                     // 1-based; 0 at BOF
        uint32_t record_count() const;
        bool get_field(std::size_t i, std::string& out, bool& is_null) const;
    };
    ```
  - `HandleKind::MssqlTable`; `get_mssql_table(ADSHANDLE)` helper + a `mssql_tables_map()` registry (mirror `get_odbc_table`/`odbc_tables_map`).

- [ ] **Step 1: Write a pure unit test for the buffer cursor**

`tests/unit/tds_protocol_test.cpp` (cursor logic is pure — build a `QueryResult` by hand):
```cpp
#include "sql_backend/mssql_table.h"
TEST_CASE("MssqlTable cursor over a 2-row buffer") {
    tds::QueryResult qr; qr.ok = true;
    qr.columns = { {"id",0x26,4,0,0,0} };
    qr.rows = { {{"1",false}}, {{"2",false}} };
    auto t = openads::sql_backend::MssqlTable::from_result(std::move(qr));
    t->go_top();
    CHECK(t->record_count() == 2);
    CHECK(t->record_num() == 1);
    std::string v; bool nul;
    REQUIRE(t->get_field(0, v, nul)); CHECK(v == "1");
    t->skip(1); CHECK(t->record_num() == 2);
    t->skip(1); CHECK(t->at_eof());
}
```

- [ ] **Step 2: Run → FAIL.**

- [ ] **Step 3: Implement `MssqlTable`** in `mssql_table.{h,cpp}`. `open` builds `SELECT * FROM [name]` (reject the name via `is_safe_identifier` → `AE_INVALID_TABLE_NAME` if unsafe; bracket-quote), calls `c.query(sql)`, stores the `QueryResult`, positions at BOF. `from_result` wraps a `QueryResult` (for `AdsExecuteSQLDirect` passthrough). Cursor semantics mirror ODBC: `go_top` → pos 0 (or eof if empty), `skip(n)` clamps to `[0,rows.size()]` and sets eof/bof, `record_num` = pos+1 (0 at bof/empty), `record_count` = rows.size(). `field_type` maps the TDS type token to the ADS field-type code the ODBC mold returns (reuse the same mapping the ODBC table uses — find it via `grep -n "field_type\|ADS_" src/sql_backend/odbc_table.cpp`; for v1 a string-ish default plus numeric/date is acceptable, matching what the ODBC mold does for the same SQL types). `get_field` returns the decoded value + null flag from the buffered row.

- [ ] **Step 4: Run → PASS** (`--test-case=*MssqlTable*`). Add `mssql_table.cpp` to the `OPENADS_WITH_MSSQL` sources in `src/CMakeLists.txt`; add `mssql_table.h` include to the test CMake if needed (header-only include from the existing test TU needs no new source entry).

- [ ] **Step 5: Add the ABI plumbing**

In `src/abi/ace_exports.cpp`, under `#if defined(OPENADS_WITH_MSSQL)`:
  - Add `HandleKind::MssqlTable` to the enum (next to `MssqlConnection`).
  - Add a `mssql_tables_map()` and `get_mssql_table(ADSHANDLE)` helper mirroring `odbc_tables_map()`/`get_odbc_table` (lines ~289–299).
  - `AdsOpenTable`: before the generic path, add an arm mirroring the ODBC one (lines ~1125–1138): if `hConnect` resolves to an `MssqlConnection`, call `MssqlTable::open(*conn, name)`, register `HandleKind::MssqlTable`, store in the map, set `*phTable`, return `ok()`.
  - `AdsCloseTable`: erase from `mssql_tables_map()`.

- [ ] **Step 6: Clone the read sites**

For each ODBC read site (`grep -n "if (auto\* st = get_odbc_table(hTable))" src/abi/ace_exports.cpp` — GoTop/GoBottom, Skip, AtBOF, AtEOF, GetNumFields, GetFieldName, GetFieldType, GetFieldLength, GetFieldDecimals, GetRecordNum, GetRecordCount, GetField, IsRecordDeleted, IsFound, and the `AdsExecuteSQLDirect` passthrough), add an analogous `#if defined(OPENADS_WITH_MSSQL) if (auto* st = get_mssql_table(hTable)) { … } #endif` block immediately after, translating the ODBC table calls to the `MssqlTable` methods. `IsRecordDeleted` → always false. For `AdsExecuteSQLDirect` on an `MssqlConnection`: call `conn->query(sql)`, wrap with `MssqlTable::from_result`, register as an `MssqlTable` handle. **Write/seek sites** (`AdsAppendRecord`, `AdsUpdateRecord`, `AdsDeleteRecord`, `AdsSetField*` write path, `AdsSeek*`, `AdsCreateIndex*`): add an `MssqlTable` arm that returns `openads::AE_FUNCTION_NOT_AVAILABLE` (explicit; sub-project 3). Use the deterministic clone method from Firebird slice 7 (locate `get_odbc_table(`/`get_odbc_index(` blocks, insert transformed clones), then build and hand-fix.

- [ ] **Step 7: Build → compiles clean; pure suite green.**
Run `--test-case=*tds* *MssqlTable*`, and the inherited ODBC suite `--test-case=*odbc*` (no regression).

- [ ] **Step 8: Commit**
```
git add src/sql_backend/mssql_table.* src/abi/ace_exports.cpp src/CMakeLists.txt tests/unit/tds_protocol_test.cpp
git commit -m "feat(mssql): buffered MssqlTable + navigational ABI read sites

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 8: Gated live read test + ENCRYPT_NOT_SUP reconciliation + final verification

**Files:**
- Create: `tests/unit/abi_plus_mssql_read_test.cpp` (register in the test CMake list next to `abi_plus_mssql_connect_test.cpp`)
- Modify: `docs/superpowers/specs/2026-06-23-mssql-tds-connect-design.md` (reconcile the ENCRYPT_NOT_SUP note)

**Interfaces:**
- Consumes: the full ABI (`AdsConnect60`, `AdsOpenTable`, GoTop/Skip/GetField/GetRecordCount/AtEOF, `AdsCloseTable`, `AdsDisconnect`) and `OPENADS_TEST_MSSQL_CONNSTR`.

- [ ] **Step 1: Write the gated live read test**
```cpp
#include "doctest.h"
#include "openads/ace.h"
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#if defined(OPENADS_WITH_MSSQL)
namespace {
const char* cs() { const char* v = std::getenv("OPENADS_TEST_MSSQL_CONNSTR");
                   return (v && v[0]) ? v : nullptr; }
ADSHANDLE conn_or_skip() {
    const char* c = cs(); if (!c) return 0;
    std::vector<UNSIGNED8> b(std::strlen(c)+1); std::memcpy(b.data(), c, std::strlen(c)+1);
    ADSHANDLE h=0; if (AdsConnect60(b.data(), ADS_LOCAL_SERVER, nullptr,nullptr,0,&h)!=0) return 0;
    return h;
}
UNSIGNED32 exec(ADSHANDLE h, const char* sql){ ADSHANDLE st=0,cur=0;
    if (AdsCreateSQLStatement(h,&st)!=0) return 1;
    std::vector<UNSIGNED8> b(std::strlen(sql)+1); std::memcpy(b.data(),sql,std::strlen(sql)+1);
    UNSIGNED32 rc=AdsExecuteSQLDirect(st,b.data(),&cur); AdsCloseSQLStatement(st); return rc; }
}
TEST_CASE("ABI: mssql read navigates a seeded CLIENTES table") {
    ADSHANDLE h = conn_or_skip();
    if (!h) { MESSAGE("OPENADS_TEST_MSSQL_CONNSTR not set; skipping"); return; }
    exec(h, "IF OBJECT_ID('CLIENTES') IS NOT NULL DROP TABLE CLIENTES");
    REQUIRE(exec(h, "CREATE TABLE CLIENTES (id INT, nome NVARCHAR(50), saldo DECIMAL(10,2), nascimento DATE)") == 0);
    REQUIRE(exec(h, "INSERT INTO CLIENTES VALUES (1,N'Ana',100.50,'2020-01-15'),(2,N'Bruno',0.00,'2019-12-31'),(3,N'Cida',-5.25,'2021-06-30')") == 0);
    ADSHANDLE t=0; UNSIGNED8 nm[]="CLIENTES";
    REQUIRE(AdsOpenTable(h, nm, nullptr, ADS_DEFAULT, ADS_DEFAULT, ADS_DEFAULT, ADS_READONLY, &t) == 0);
    UNSIGNED32 cnt=0; AdsGetRecordCount(t, ADS_RESPECTFILTERS, &cnt); CHECK(cnt == 3);
    AdsGotoTop(t);
    UNSIGNED8 buf[128]; UNSIGNED32 blen;
    blen=sizeof(buf); AdsGetField(t,(UNSIGNED8*)"NOME",buf,&blen,ADS_NONE);
    CHECK(std::string((char*)buf) == "Ana");
    AdsSkip(t,1); blen=sizeof(buf); AdsGetField(t,(UNSIGNED8*)"NOME",buf,&blen,ADS_NONE);
    CHECK(std::string((char*)buf) == "Bruno");
    AdsSkip(t,2); UNSIGNED16 eof=0; AdsAtEOF(t,&eof); CHECK(eof==1);
    AdsCloseTable(t);
    exec(h, "DROP TABLE CLIENTES");
    CHECK(AdsDisconnect(h) == 0);
}
#endif
```
(Field-accessor names/signatures: match the actual ACE ABI used by the ODBC live test — open `tests/unit/abi_plus_odbc_read_test.cpp` and copy the exact `AdsGetField`/`AdsSkip`/`AdsAtEOF`/`AdsGetRecordCount`/`AdsGotoTop` call shapes; the snippet above is the shape, adjust arg types to the real prototypes.)

- [ ] **Step 2: Build, then run live → PASS**
```
cmd /c 'call H:\DEVAI\_UtlAI\msvc\msvc_x64_full.bat && cd /d H:\DEVAI\_Prj\OpenADS-mssql && H:\DEVAI\_UtlAI\Mingw\bin\cmake.exe --build build\mssql-verify'
pwsh -c ". tools\scripts\mssql_test_env.local.ps1; build\mssql-verify\tests\openads_unit_tests.exe --test-case=*mssql*read*"
```
Expected: the seeded rows read back with NOME decoded correctly, count 3, EOF after the last skip. If anything fails, debug against the live server (this is where real wire bytes correct any spec mis-reading — same lesson as sub-project 1's `cchPassword`).

- [ ] **Step 3: Reconcile the ENCRYPT_NOT_SUP note**

The implementation always negotiates TLS (rejects `ENCRYPT_NOT_SUP`). Edit the connect design spec's Risks section so the doc matches the code: state that v1 requires server encryption support and treats `ENCRYPT_NOT_SUP` as a connection error (rather than "skip TLS"). One-paragraph edit.

- [ ] **Step 4: Final verification (checklist below) + commit + push**
```
git add tests/unit/abi_plus_mssql_read_test.cpp <test-cmake-file> docs/superpowers/specs/2026-06-23-mssql-tds-connect-design.md
git commit -m "test(mssql): gated live read test + reconcile encryption-negotiation doc

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
git push origin pr/openads-plus-mssql
```

---

## Final verification

- [ ] Pure unit suite green with the flag: `build\mssql-verify\tests\openads_unit_tests.exe --test-case=*tds* *MssqlTable*` — all green, including the malformed-length fail-closed case.
- [ ] Full suite no regression vs the base branch; inherited ODBC suite green: `--test-case=*odbc*`.
- [ ] Live read: seeded `CLIENTES` reads back through `AdsOpenTable` + nav with every common-type field correct; count and EOF correct.
- [ ] `git push origin pr/openads-plus-mssql` done (each task pushed).
- [ ] Update memory `project_openads_native_firebird_mssql` (sub-project 2 done, what's verified) + the central ledger (one ≤3-line entry) once the live read passes.
- [ ] Confirm scope held: no write/seek implemented; those ABI arms return not-available; sub-project 3 still the remaining work.
