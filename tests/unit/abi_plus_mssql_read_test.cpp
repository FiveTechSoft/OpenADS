// gated live read test for the native MS SQL Server backend
// Requires: OPENADS_WITH_MSSQL=ON
// Runtime gate: OPENADS_TEST_MSSQL_CONNSTR env var (full mssql:// URI).
// When unset the test emits a MESSAGE and returns; the pure build-time
// suite (tds_protocol_test + mssql_uri_test + MssqlTable unit tests) is
// not affected.

#include "doctest.h"
#include "openads/ace.h"
#include "openads/error.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#if defined(OPENADS_WITH_MSSQL)

namespace {

// Return the connection string from the environment, or nullptr when absent.
const char* mssql_connstr() {
    const char* v = std::getenv("OPENADS_TEST_MSSQL_CONNSTR");
    return (v != nullptr && v[0] != '\0') ? v : nullptr;
}

// Connect via the ACE ABI.  Returns 0 (null handle) when the env var is
// not set — the caller skips in that case.
ADSHANDLE connect_mssql() {
    const char* cs = mssql_connstr();
    if (cs == nullptr) return 0;
    std::vector<UNSIGNED8> srv(std::strlen(cs) + 1);
    std::memcpy(srv.data(), cs, std::strlen(cs) + 1);
    ADSHANDLE h = 0;
    REQUIRE(AdsConnect60(srv.data(), ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &h) == 0);
    return h;
}

// Execute a DDL/DML statement on the connection; returns the ACE rc.
UNSIGNED32 exec_sql(ADSHANDLE hConn, const char* sql) {
    ADSHANDLE hStmt = 0;
    UNSIGNED32 rc = AdsCreateSQLStatement(hConn, &hStmt);
    if (rc != 0) return rc;
    std::vector<UNSIGNED8> b(std::strlen(sql) + 1);
    std::memcpy(b.data(), sql, std::strlen(sql) + 1);
    ADSHANDLE hCursor = 0;
    rc = AdsExecuteSQLDirect(hStmt, b.data(), &hCursor);
    AdsCloseSQLStatement(hStmt);
    if (hCursor != 0) AdsCloseTable(hCursor);
    return rc;
}

// Read a field from the current row into a std::string of the exact
// declared byte length (mirrors the helper in abi_plus_odbc_read_test.cpp).
// pulLen is in/out: in = buffer capacity, out = bytes written.
std::string field_str(ADSHANDLE hTable, const char* name) {
    UNSIGNED8  fld[128];
    std::memcpy(fld, name, std::strlen(name) + 1);
    UNSIGNED8  buf[512] = {0};
    UNSIGNED32 cap = sizeof(buf);
    REQUIRE(AdsGetField(hTable, fld, buf, &cap, ADS_NONE) == 0);
    // cap is updated to the number of bytes written (without the NUL).
    return std::string(reinterpret_cast<const char*>(buf), cap);
}

} // namespace

// ---------------------------------------------------------------------------
// Live read test — seeds a 3-row CLIENTES table and navigates it through
// AdsOpenTable, exercising INT/NVARCHAR/DECIMAL/DATE decode paths.
// ---------------------------------------------------------------------------
TEST_CASE("ABI: mssql read navigates a seeded CLIENTES table") {
    const char* cs = mssql_connstr();
    if (cs == nullptr) {
        MESSAGE("OPENADS_TEST_MSSQL_CONNSTR not set; skipping mssql live read test");
        return;
    }

    ADSHANDLE hConn = connect_mssql();
    REQUIRE(hConn != 0);

    // Clean slate.
    exec_sql(hConn,
        "IF OBJECT_ID('CLIENTES','U') IS NOT NULL DROP TABLE CLIENTES");

    REQUIRE(exec_sql(hConn,
        "CREATE TABLE CLIENTES ("
        "  id          INT,"
        "  nome        NVARCHAR(50),"
        "  saldo       DECIMAL(10,2),"
        "  nascimento  DATE"
        ")") == 0);

    REQUIRE(exec_sql(hConn,
        "INSERT INTO CLIENTES VALUES"
        " (1, N'Ana',   100.50, '2020-01-15'),"
        " (2, N'Bruno',   0.00, '2019-12-31'),"
        " (3, N'Cida',   -5.25, '2021-06-30')") == 0);

    // Open the table via AdsOpenTable (9-arg ACE ABI form).
    UNSIGNED8  tbl_name[32] = "CLIENTES";
    ADSHANDLE  hTable = 0;
    REQUIRE(AdsOpenTable(hConn, tbl_name, tbl_name,
                         ADS_DEFAULT, 0, 0, 0, ADS_READONLY,
                         &hTable) == 0);

    // --- Count ---
    UNSIGNED32 cnt = 0;
    REQUIRE(AdsGetRecordCount(hTable, ADS_RESPECTFILTERS, &cnt) == 0);
    CHECK(cnt == 3);

    // --- Navigate forward ---
    REQUIRE(AdsGotoTop(hTable) == 0);

    // Row 1: Ana
    // NOME is NVARCHAR(50) → ADS_STRING → padded to 50 chars.
    CHECK(field_str(hTable, "nome") ==
          std::string(50, ' ').replace(0, 3, "Ana"));

    // Check saldo for row 1: DECIMAL(10,2) → ADS_DOUBLE → string "100.50"
    CHECK(field_str(hTable, "saldo") == "100.50");

    // Check nascimento for row 1: DATE → ADS_DATE → "YYYYMMDD" (ADS native, no separators)
    CHECK(field_str(hTable, "nascimento") == "20200115");

    REQUIRE(AdsSkip(hTable, 1) == 0);  // → row 2: Bruno

    CHECK(field_str(hTable, "nome") ==
          std::string(50, ' ').replace(0, 5, "Bruno"));
    CHECK(field_str(hTable, "saldo") == "0.00");

    REQUIRE(AdsSkip(hTable, 1) == 0);  // → row 3: Cida

    CHECK(field_str(hTable, "nome") ==
          std::string(50, ' ').replace(0, 4, "Cida"));
    CHECK(field_str(hTable, "saldo") == "-5.25");

    // EOF check: one more skip lands past the last row.
    UNSIGNED16 eof = 0;
    REQUIRE(AdsAtEOF(hTable, &eof) == 0);
    CHECK(eof == 0);  // not yet at EOF (still on row 3)

    REQUIRE(AdsSkip(hTable, 1) == 0);  // past the end
    REQUIRE(AdsAtEOF(hTable, &eof) == 0);
    CHECK(eof == 1);  // now at EOF

    // --- Cleanup ---
    REQUIRE(AdsCloseTable(hTable) == 0);
    exec_sql(hConn, "DROP TABLE CLIENTES");
    REQUIRE(AdsDisconnect(hConn) == 0);
}

#else  // !OPENADS_WITH_MSSQL

// When the backend is compiled out, the read test reduces to the same
// disabled-at-compile-time stub as the connect test.
TEST_CASE("ABI: mssql read test disabled at compile time") {
    UNSIGNED8 uri[] = "mssql://u:p@127.0.0.1:1433/db";
    ADSHANDLE hConn = 0;
    const UNSIGNED32 rc = AdsConnect60(uri, ADS_LOCAL_SERVER,
                                       nullptr, nullptr, 0, &hConn);
    CHECK(rc == openads::AE_FUNCTION_NOT_AVAILABLE);
}

#endif  // OPENADS_WITH_MSSQL
