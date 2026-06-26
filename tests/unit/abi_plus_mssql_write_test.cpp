// Live navigational-write test for the native MS SQL Server (TDS) backend.
// Requires: OPENADS_WITH_MSSQL=ON.
// Runtime gate: OPENADS_TEST_MSSQL_CONNSTR (full mssql:// URI). When unset the
// test emits a MESSAGE and returns. Mirrors the MariaDB/Postgres/Firebird
// navigational-write contract: AdsAppendRecord + AdsSetString + AdsWriteRecord
// + AdsDeleteRecord, keyed on the table's primary key.
#include "doctest.h"
#include "openads/ace.h"
#include "openads/error.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#if defined(OPENADS_WITH_MSSQL)

namespace {

const char* mssql_connstr() {
    const char* v = std::getenv("OPENADS_TEST_MSSQL_CONNSTR");
    return (v != nullptr && v[0] != '\0') ? v : nullptr;
}

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

std::string rtrim(std::string s) {
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

std::string field_str(ADSHANDLE hTable, const char* name) {
    UNSIGNED8 fld[128];
    std::memcpy(fld, name, std::strlen(name) + 1);
    UNSIGNED8 buf[512] = {0};
    UNSIGNED32 cap = sizeof(buf);
    REQUIRE(AdsGetField(hTable, fld, buf, &cap, ADS_NONE) == 0);
    return std::string(reinterpret_cast<const char*>(buf), cap);
}

void set_str(ADSHANDLE hTable, const char* field, const char* value) {
    UNSIGNED8 f[64];
    std::memcpy(f, field, std::strlen(field) + 1);
    UNSIGNED8 v[256];
    std::memcpy(v, value, std::strlen(value) + 1);
    REQUIRE(AdsSetString(hTable, f, v,
                         static_cast<UNSIGNED32>(std::strlen(value))) == 0);
}

UNSIGNED32 row_count(ADSHANDLE hTable) {
    UNSIGNED32 c = 0;
    REQUIRE(AdsGetRecordCount(hTable, ADS_RESPECTFILTERS, &c) == 0);
    return c;
}

} // namespace

TEST_CASE("ABI: mssql AdsAppendRecord + AdsSetString + AdsWriteRecord + AdsDeleteRecord") {
    const char* cs = mssql_connstr();
    if (cs == nullptr) {
        MESSAGE("OPENADS_TEST_MSSQL_CONNSTR not set; skipping mssql live write test");
        return;
    }

    ADSHANDLE hConn = connect_mssql();
    REQUIRE(hConn != 0);

    exec_sql(hConn, "IF OBJECT_ID('CLIENTES','U') IS NOT NULL DROP TABLE CLIENTES");
    REQUIRE(exec_sql(hConn,
        "CREATE TABLE CLIENTES ("
        "  id    INT PRIMARY KEY,"
        "  nome  NVARCHAR(64),"
        "  saldo FLOAT"
        ")") == 0);
    REQUIRE(exec_sql(hConn,
        "INSERT INTO CLIENTES (id, nome, saldo) VALUES"
        " (1, N'Ana', 10.5), (2, N'Bob', 0.0), (3, N'Cid', 1.0)") == 0);

    UNSIGNED8 tbl_name[32] = "CLIENTES";
    ADSHANDLE hTable = 0;
    REQUIRE(AdsOpenTable(hConn, tbl_name, tbl_name,
                         ADS_DEFAULT, 0, 0, 0, ADS_DEFAULT, &hTable) == 0);

    CHECK(row_count(hTable) == 3);

    // Append a new row through the navigational ABI.
    REQUIRE(AdsAppendRecord(hTable) == 0);
    set_str(hTable, "id", "99");
    set_str(hTable, "nome", "Dan");
    set_str(hTable, "saldo", "42.5");
    REQUIRE(AdsWriteRecord(hTable) == 0);
    CHECK(row_count(hTable) == 4);

    REQUIRE(AdsGotoBottom(hTable) == 0);
    CHECK(rtrim(field_str(hTable, "nome")) == "Dan");

    // Update the current row.
    set_str(hTable, "nome", "DanX");
    REQUIRE(AdsWriteRecord(hTable) == 0);
    REQUIRE(AdsGotoBottom(hTable) == 0);
    CHECK(rtrim(field_str(hTable, "nome")) == "DanX");

    // Delete the current row.
    REQUIRE(AdsDeleteRecord(hTable) == 0);
    CHECK(row_count(hTable) == 3);

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
}

#endif // OPENADS_WITH_MSSQL
