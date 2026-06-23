# Native MS SQL Server (TDS) backend — sub-project 2: Query + read — design

Date: 2026-06-23
Status: approved (pre-implementation)
Scope: **sub-project 2 of 3** of the from-scratch TDS client behind the ACE ABI.
This sub-project delivers SQL execution, result-set token parsing, common-type
decoding, and read/navigation of a table through the ABI. Builds directly on the
authenticated `TdsTlsChannel` from sub-project 1.

## Why

Sub-project 1 produced a live, authenticated TDS 7.4 session (`AdsConnect60`
with `mssql://…` over tunnelled TLS). On its own it reads nothing: the channel
logs in and idles. Sub-project 2 makes the backend actually useful for reads —
run a `SELECT`, parse the TABULAR_RESULT token stream, decode the common SQL
Server column types, and expose the rows through the same navigational ABI the
ODBC and Firebird backends use. Write/seek and the driver-matrix entry are
deferred to sub-project 3.

## Decomposition (context — only sub-project 2 is in this spec)

1. **Connect** (done, sub-project 1): PRELOGIN, tunnelled TLS, LOGIN7, LOGINACK.
2. **Query + read** (THIS spec): SQL_BATCH, token-stream parse
   (COLMETADATA/ROW/DONE), common-type decode → read/navigation via a buffered
   result set behind the ABI.
3. **Write + seek + matrix**: append/update/delete, seek/PK snapshot, driver
   matrix entry.

## Scope of sub-project 2

- IN:
  - Build a SQL_BATCH (`0x01`) message: ALL_HEADERS stream header (transaction
    descriptor) + UCS-2LE SQL text.
  - Parse the server TABULAR_RESULT (`0x04`) token stream: COLMETADATA (`0x81`),
    ROW (`0xD1`), NBCROW (`0xD2`), DONE / DONEINPROC / DONEPROC
    (`0xFD`/`0xFF`/`0xFE`), ERROR (`0xAA`), INFO (`0xAB`), ENVCHANGE (`0xE3`),
    ORDER (`0xA9`), RETURNSTATUS (`0x79`).
  - A **per-token-class length table** so unknown/ignored tokens are skipped by
    their documented length class rather than the current heuristic (carries the
    sub-project-1 review's Important-A item; required to walk to ROW safely).
  - Decode the **common** column type set to string (see "Type coverage").
  - `MssqlConnection::query(sql)` → a result set.
  - A buffered table object materialised from `SELECT * FROM <name>` and the
    navigational ABI read surface (mirrors the ODBC/Firebird mold).
  - Hardening carried from the sub-project-1 final review: `recv_tds` total
    reassembly cap; a pure test that a malformed-length token stream terminates
    fail-closed (no out-of-bounds read); reconcile the ENCRYPT_NOT_SUP
    spec/comment with the implementation (always-TLS) — code or doc, pick one.
- OUT (sub-project 3 / deferred): any write (append/update/delete); seek and the
  real PK snapshot; the driver-matrix entry; PLP/large types (TEXT/NTEXT/
  VARCHAR(MAX)); `sql_variant`; `uniqueidentifier`; `varbinary`/`binary`;
  `time`/`datetimeoffset`; multiple result sets per batch; RPC / parameterised
  prepared statements (SQL_BATCH text only); MARS.

## Type coverage (the "common" set)

Decoded to a printable string (the ODBC/Firebird mold returns string cells):

| SQL Server type | TDS type token(s) | Decode |
|---|---|---|
| tinyint / smallint / int / bigint | INTNTYPE (`0x26`), INT1/2/4/8TYPE | signed decimal text (tinyint unsigned) |
| bit | BITTYPE (`0x32`), BITNTYPE (`0x68`) | `"0"` / `"1"` |
| decimal / numeric | DECIMALNTYPE (`0x6A`), NUMERICNTYPE (`0x6C`) | scaled decimal text (precision/scale from COLMETADATA) |
| money / smallmoney | MONEYNTYPE (`0x6E`), MONEY4 (`0x7A`), MONEY8 (`0x3C`) | scaled to 4 dp |
| float / real | FLTNTYPE (`0x6D`), FLT4 (`0x3B`), FLT8 (`0x3E`) | shortest round-trip text |
| char / varchar | BIGCHARTYPE (`0xAF`), BIGVARCHRTYPE (`0xA7`) | bytes → string (collation code page treated as Latin1/UTF-8 passthrough for v1) |
| nchar / nvarchar | NCHARTYPE (`0xEF`), NVARCHARTYPE (`0xE7`) | UCS-2LE → UTF-8 |
| date | DATENTYPE (`0x28`) | `YYYYMMDD` (ADS native, 8 chars, no separators) |
| smalldatetime / datetime | DATETIMNTYPE (`0x6F`), DATETIM4 (`0x3A`), DATETIME (`0x3D`) | `YYYYMMDDHHMMSS` (ADS native, 14 chars, no separators) |
| datetime2 | DATETIME2NTYPE (`0x2A`) | `YYYYMMDDHHMMSS` (ADS native, 14 chars; fractional seconds dropped to match native ADS_DATE convention) |
| (any) NULL | length sentinel / NBCROW null bitmap | `is_null = true`, empty value |

Any column whose type token is outside this set makes `AdsOpenTable` fail with a
clear `AE_*` error ("unsupported MSSQL column type 0xNN") rather than returning
garbage. Date/time conversions use the documented TDS epochs (datetime: days
since 1900-01-01 + 1/300 s ticks; date: days since 0001-01-01; datetime2:
date part + time scaled by 10^-scale seconds). Date and datetime values follow
the OpenADS native ADS_DATE convention (no separators: `YYYYMMDD` / `YYYYMMDDHHMMSS`)
for legacy-client round-trip compatibility — this matches what `CTOD()`/`STOD()`
and the DBF/ADT path emit, confirmed by `AdsGetFieldLength` returning 8 and 14
respectively for those types.

## Architecture

Continues the `sql_backend/` mold. No new compile flag; everything lives under
the existing `OPENADS_WITH_MSSQL` guard.

### Files

- **`src/sql_backend/tds_protocol.{h,cpp}`** (extend) — pure, no I/O:
  - `build_sql_batch(const std::string& utf8_sql)` → bytes. Prepends the
    ALL_HEADERS stream (one transaction-descriptor header, descriptor 0, request
    count 1) then the SQL text as UCS-2LE. No 8-byte packet header (the channel
    frames it), consistent with how `build_login7`'s body is fed to `send_tds`.
  - `struct TdsColumn { std::string name; uint8_t type_token; uint16_t
    user_type; uint32_t length; uint8_t precision; uint8_t scale; uint16_t
    codepage; };`
  - `struct TdsCell { std::string value; bool is_null; };`
  - `struct QueryResult { std::vector<TdsColumn> columns;
    std::vector<std::vector<TdsCell>> rows; bool ok=false; uint32_t error_number=0;
    std::string message; };`
  - `QueryResult parse_query_response(const uint8_t* payload, size_t n)` — walks
    the token stream. Uses a `token_length_class(token)` helper (the per-token
    length table) to skip ignored/unknown fixed-/variable-length tokens safely.
    COLMETADATA fills `columns` (driving subsequent ROW parsing); ROW/NBCROW
    append a row (NBCROW reads the null bitmap first); ERROR sets
    `error_number`/`message` and `ok=false`; DONE\* stop. Bounds-checked on every
    read; any short read sets `ok=false` and stops (fail-closed).
  - `std::string decode_cell(const TdsColumn&, const uint8_t* data, size_t len)`
    — the per-type decoder for the common set. Pure, table-driven by type_token.

- **`src/sql_backend/tds_tls_channel.cpp`** (edit) — `recv_tds` gains a
  `kMaxReassembly` total-bytes cap; exceeding it returns `util::Error`
  (DoS guard for hostile/huge replies). Cap is a named constant (e.g. 64 MiB).

- **`src/sql_backend/mssql_connection.{h,cpp}`** (extend) — add
  `util::Result<tds::QueryResult> query(const std::string& sql)`:
  `send_tds(TDS_PKT_SQLBATCH, build_sql_batch(sql))` → `recv_tds()` →
  `parse_query_response`. On a result with `ok=false` and an error number, return
  `util::Error{number, 0, sanitised_message, ""}` (never the SQL text if it could
  embed secrets — for v1 the SQL is backend-generated `SELECT *`, so the message
  is the server's). Keeps the channel for reuse.

- **`src/sql_backend/mssql_table.{h,cpp}`** (new) — `class MssqlTable` holding the
  buffered `QueryResult` for one open table plus a cursor index. Built by
  `MssqlTable::open(MssqlConnection&, const std::string& table_name)` which runs
  `SELECT * FROM <quoted name>`. Methods mirror the ODBC table read surface:
  `go_top()`, `go_bottom()`, `skip(n)`, `at_bof()`, `at_eof()`,
  `field_count()`, `field_name(i)`, `field_type(i)`, `field_length(i)`,
  `field_decimals(i)`, `record_num()` (1-based cursor ordinal), `record_count()`,
  `get_field(i)` (decoded string + is_null), `is_found()`. The cursor follows the
  ODBC/Firebird BOF/EOF semantics (BOF before first, EOF after last). Identifier
  quoting via the existing `sql_common` quoting helper (`[name]`).

### ABI dispatch (`src/abi/ace_exports.cpp`)

Mirror the ODBC table read sites under `#if defined(OPENADS_WITH_MSSQL)`:

- `AdsOpenTable` for an `MssqlConnection` handle → `MssqlTable::open` → register
  `HandleKind::MssqlTable`. `AdsCloseTable` releases it.
- Clone the ODBC read sites (the ~22 blocks already used by Firebird's slice 7):
  GoTop/GoBottom, Skip, AtBOF/AtEOF, GetNumFields, GetFieldName, GetFieldType,
  GetFieldLength, GetFieldDecimals, GetRecordNum (cursor ordinal), GetRecordCount,
  GetField, IsRecordDeleted (always false — SQL has no deleted flag), IsFound,
  and `AdsExecuteSQLDirect` passthrough (runs arbitrary SQL via
  `MssqlConnection::query`, exposes the QueryResult as a read-only cursor table).
- Write sites (`AdsAppendRecord`, `AdsUpdateRecord`, `AdsDeleteRecord`,
  `AdsSetField`, `AdsSeek`/`AdsSeekLast`, `CreateIndex61`) return
  `AE_FUNCTION_NOT_AVAILABLE` (or the closest existing not-supported code the
  ODBC/Firebird mold uses) for `MssqlTable` — explicit, not silent. New
  `HandleKind::MssqlTable`.

Clone method: the same deterministic approach as Firebird slice 7 — locate the
ODBC read blocks (`get_odbc_table(` / `get_odbc_index(`), insert an
`odbc→mssql`-transformed clone after each, then build and fix up by hand.

### CMake

No new flag. `src/CMakeLists.txt` adds `mssql_table.cpp` to the
`if(OPENADS_WITH_MSSQL)` source list. Live test target unchanged
(`OPENADS_WITH_MSSQL=ON`, `OPENADS_WITH_TLS=ON`).

## Data flow

```
AdsOpenTable(conn=mssql, "CLIENTES")
  → MssqlTable::open(conn, "CLIENTES")
      → conn.query("SELECT * FROM [CLIENTES]")
          → send_tds(SQLBATCH, build_sql_batch(sql))      [TLS]
          → recv_tds()                                     [TLS]  (reassembled, capped)
          → parse_query_response()
              COLMETADATA → columns
              ROW*        → rows (decode_cell per column)
              DONE        → stop
      → buffer rows + cursor at BOF
  → nav: GoTop/Skip/GetField/... read the buffer
```

## Error handling

- Socket/TLS errors propagate as `util::Error` from the channel.
- A TDS ERROR token → `util::Error{number, 0, sanitised_message, ""}`. The
  connection string, username, and password are NEVER surfaced across the ABI
  (house rule). The backend-generated `SELECT *` is not secret.
- An unsupported column type → `AdsOpenTable` fails with a clear `AE_*` and a
  message naming the offending type token (no row data returned).
- A malformed/short token stream → `parse_query_response` stops fail-closed
  (`ok=false`), `AdsOpenTable` returns an error, never reads out of bounds.
- Reassembly over the cap → channel error, no unbounded allocation.

## Testing

- **Pure unit tests (no server)** — `tests/unit/tds_protocol_test.cpp` (extend):
  - `build_sql_batch("SELECT 1")` → assert ALL_HEADERS prefix bytes + UCS-2LE
    text + total header length.
  - `parse_query_response` over a hand-built COLMETADATA + ROW + DONE buffer for
    each common type (one synthetic row per type) → asserts the decoded string,
    NULL via length sentinel, and NBCROW null-bitmap handling.
  - `token_length_class` skips a synthetic unknown fixed-len and var-len token
    and still reaches ROW.
  - **Malformed-length stream** (a var-len token claiming more bytes than remain)
    → `ok=false`, no crash/OOB (run under the same harness; the assertion is
    "returns, ok=false").
- **Live test (gated)** — `tests/unit/abi_plus_mssql_read_test.cpp`,
  `#if defined(OPENADS_WITH_MSSQL)`, runtime-gated on `OPENADS_TEST_MSSQL_CONNSTR`.
  When set: seed a `CLIENTES` table (id int PK, nome nvarchar, saldo decimal,
  nascimento date) with 3 rows via `MssqlConnection::query` (CREATE/INSERT through
  SQL_BATCH — exercises the write path at the SQL level even though ABI write is
  deferred), then `AdsConnect60` → `AdsOpenTable` → GoTop, walk 3 rows asserting
  each decoded field, GetRecordCount==3, AtEOF after the last Skip, then drop the
  table and `AdsDisconnect`. When unset: `MESSAGE(...skipping)` and return.

## Verification

- Pure unit suite green with `OPENADS_WITH_MSSQL=ON`; full suite no regression.
- x64 build via the pinned recipe (`OPENADS_WITH_MSSQL=ON`, `OPENADS_WITH_TLS=ON`).
- Live: `AdsOpenTable` over a real SQL Server 2025 Express reads the seeded
  `CLIENTES` rows with every common-type field decoded correctly; an unsupported
  type and a malformed reply both fail cleanly.

## Risks

- **COLMETADATA type-info parsing** is the fiddly part: each type class encodes
  its length/precision/scale differently (fixed-len tokens carry none; *NTYPE
  variable tokens carry a length byte; DECIMAL/NUMERIC carry precision+scale;
  CHAR/NCHAR carry a 5-byte collation). Mitigation: drive it from a per-token
  table and unit-test each common type with a hand-built buffer before touching
  the live server.
- **Date/time epoch math** (datetime 1/300-second ticks; datetime2 scale) is
  error-prone. Mitigation: dedicated unit tests with documented reference values.
- **Collation/code page** for `char`/`varchar`: v1 passes bytes through as
  Latin1/UTF-8 without a full code-page table; documented as a known limitation
  (non-ASCII single-byte text may not round-trip). `nvarchar` is exact (UCS-2).
- Same upstream-PR deferral as Firebird: depends on `sql_common.h`; the branch
  stays pushed on the fork until the Plus PRs land.

## Local plan (outside the public repo, leak rule): tracked in
`.superpowers/sdd/progress.md` and the existing
`_Prj\OPENADS_FIREBIRD_MSSQL_PLANO.md`.
