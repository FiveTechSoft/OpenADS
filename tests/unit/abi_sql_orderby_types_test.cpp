// A materialised ORDER BY cursor must keep the source column types.
//
// The memory-table cursor introduced with #136 rebuilt the result schema from
// scratch and typed every numeric column as a 4-byte integer, so
// `SELECT * FROM t ORDER BY c` silently dropped the decimals of an N(12,2) —
// and a wide table could exceed the helper's 64 KB record cap outright.
// Reported from a Harbour/FiveWin ERP whose article table has 187 fields and
// prices with two decimals (issue #146: ADSCDX/1012 on
// `Select * From [articulo.dat] as a ... order by a.cnombreart`).
//
// The cursor is materialised from the SOURCE field descriptors, so the checks
// below are just "what went in comes out".

#include "doctest.h"
#include "openads/ace.h"

#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

TEST_CASE("SQL: ORDER BY cursor preserves numeric scale") {
    fs::path tmp = fs::temp_directory_path() / "openads_sql_orderby_types";
    { std::error_code ec; fs::remove_all(tmp, ec); fs::create_directories(tmp, ec); }

    UNSIGNED8 srv[260]{};
    std::memcpy(srv, tmp.string().c_str(), tmp.string().size());
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn)
            == AE_SUCCESS);

    UNSIGNED8 tbl[]    = "prices.dbf";
    UNSIGNED8 flddef[] = "CNOMBRE,Character,20;NPRECIO,Numeric,12,2";
    ADSHANDLE hTable   = 0;
    REQUIRE(AdsCreateTable(hConn, tbl, nullptr, ADS_CDX, ADS_ANSI, 0, 0, 0,
                           flddef, &hTable) == AE_SUCCESS);

    struct Row { const char* name; const char* price; };
    const Row rows[] = {
        {"ZETA",  "12345.67"},
        {"ALFA",     "10.50"},
        {"MIKE",      "0.05"},
    };
    for (const auto& r : rows) {
        REQUIRE(AdsAppendRecord(hTable) == AE_SUCCESS);
        AdsSetString(hTable, (UNSIGNED8*)"CNOMBRE", (UNSIGNED8*)r.name,
                     (UNSIGNED32)std::strlen(r.name));
        AdsSetString(hTable, (UNSIGNED8*)"NPRECIO", (UNSIGNED8*)r.price,
                     (UNSIGNED32)std::strlen(r.price));
    }
    REQUIRE(AdsWriteRecord(hTable) == AE_SUCCESS);
    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);

    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == AE_SUCCESS);

    ADSHANDLE hCur = 0;
    UNSIGNED8 sql[] = "SELECT * FROM [prices.dbf] ORDER BY CNOMBRE";
    REQUIRE(AdsExecuteSQLDirect(hStmt, sql, &hCur) == AE_SUCCESS);
    REQUIRE(hCur != 0);

    // Rows come back in key order: ALFA, MIKE, ZETA.
    REQUIRE(AdsGotoTop(hCur) == AE_SUCCESS);

    auto read = [&](const char* field) {
        UNSIGNED8  buf[64]{};
        UNSIGNED32 len = sizeof(buf);
        REQUIRE(AdsGetString(hCur, (UNSIGNED8*)field, buf, &len, 0)
                == AE_SUCCESS);
        std::string s(reinterpret_cast<char*>(buf), len);
        while (!s.empty() && (s.back() == ' ')) s.pop_back();
        while (!s.empty() && (s.front() == ' ')) s.erase(s.begin());
        return s;
    };

    CHECK(read("CNOMBRE") == "ALFA");
    CHECK(read("NPRECIO") == "10.50");    // was "10" through the memory table

    REQUIRE(AdsSkip(hCur, 1) == AE_SUCCESS);
    CHECK(read("CNOMBRE") == "MIKE");
    CHECK(read("NPRECIO") == "0.05");     // was "0"

    REQUIRE(AdsSkip(hCur, 1) == AE_SUCCESS);
    CHECK(read("CNOMBRE") == "ZETA");
    CHECK(read("NPRECIO") == "12345.67"); // was "12345"

    AdsCloseTable(hCur);
    AdsCloseSQLStatement(hStmt);
    AdsDisconnect(hConn);
    { std::error_code ec; fs::remove_all(tmp, ec); }
}
