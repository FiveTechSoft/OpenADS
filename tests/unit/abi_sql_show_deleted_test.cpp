// SQL walks must honour AdsShowDeleted, whose default is TRUE — SAP's SQL
// engine counts, filters, orders, UPDATEs and copies deleted DBF rows just
// like live ones (oracle-probed on a users.dbf copy: COUNT(*) 18 not 10, an
// UPDATE touched all 18 including delete-marked rows, SELECT INTO copied all
// 18 as live rows). Only AdsShowDeleted(0) hides them from SQL, exactly as
// it does from navigation. ADT tables never surface deleted rows, so this is
// inherently DBF-only behaviour.
#include "doctest.h"
#include "openads/ace.h"

#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {

// Restore the process-global flag no matter how the test exits.
struct ShowDeletedRestore {
    ~ShowDeletedRestore() { AdsShowDeleted(1); }
};

std::string read_field(ADSHANDLE hCur, const char* name) {
    UNSIGNED8 buf[64] = {0};
    UNSIGNED32 cap = sizeof(buf);
    REQUIRE(AdsGetField(hCur, (UNSIGNED8*)name, buf, &cap, 0) == 0);
    std::string s((char*)buf, cap);
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

int walk_rows(ADSHANDLE hCur) {
    int n = 0;
    if (AdsGotoTop(hCur) != 0) return 0;
    for (;;) {
        UNSIGNED16 eof = 0;
        AdsAtEOF(hCur, &eof);
        if (eof) break;
        ++n;
        AdsSkip(hCur, 1);
    }
    return n;
}

int sql_count(ADSHANDLE hStmt, const char* sql) {
    ADSHANDLE hCur = 0;
    UNSIGNED8 buf[300];
    std::memcpy(buf, sql, std::strlen(sql) + 1);
    REQUIRE(AdsExecuteSQLDirect(hStmt, buf, &hCur) == 0);
    REQUIRE(AdsGotoTop(hCur) == 0);
    int n = std::stoi(read_field(hCur, "n"));
    AdsCloseTable(hCur);
    return n;
}

ADSHANDLE sql_cursor(ADSHANDLE hStmt, const char* sql) {
    ADSHANDLE hCur = 0;
    UNSIGNED8 buf[300];
    std::memcpy(buf, sql, std::strlen(sql) + 1);
    REQUIRE(AdsExecuteSQLDirect(hStmt, buf, &hCur) == 0);
    return hCur;
}

}  // namespace

TEST_CASE("SQL over DBF honours AdsShowDeleted (default: deleted visible)") {
    auto dir = fs::temp_directory_path() / "openads_sql_showdel";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    ShowDeletedRestore restore;
    AdsShowDeleted(1);  // explicit default: deleted rows visible

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);

    // 5-row DBF; rows 2 and 4 delete-marked.
    UNSIGNED8 def[]   = "ID,N,4,0;NM,C,6";
    UNSIGNED8 tname[] = "del.dbf";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX,
                           0, 0, 0, 0, def, &hT) == 0);
    UNSIGNED8 fid[] = "ID", fnm[] = "NM";
    for (int i = 1; i <= 5; ++i) {
        REQUIRE(AdsAppendRecord(hT) == 0);
        AdsSetDouble(hT, fid, static_cast<double>(i));
        AdsSetString(hT, fnm, (UNSIGNED8*)"live", 4);
        REQUIRE(AdsWriteRecord(hT) == 0);
    }
    for (UNSIGNED32 r : {2u, 4u}) {
        REQUIRE(AdsGotoRecord(hT, r) == 0);
        REQUIRE(AdsDeleteRecord(hT) == 0);
    }
    REQUIRE(AdsWriteRecord(hT) == 0);
    AdsCloseTable(hT);

    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);

    // Default (show-deleted TRUE): SQL sees all 5 rows, like SAP.
    CHECK(sql_count(hStmt, "SELECT COUNT(*) AS n FROM del.dbf") == 5);
    CHECK(sql_count(hStmt,
        "SELECT COUNT(*) AS n FROM del.dbf WHERE ID >= 1") == 5);

    {   // ORDER BY walks the deleted rows too.
        ADSHANDLE hCur = sql_cursor(hStmt,
            "SELECT ID FROM del.dbf ORDER BY ID DESC");
        REQUIRE(AdsGotoTop(hCur) == 0);
        CHECK(read_field(hCur, "ID") == "5");
        CHECK(walk_rows(hCur) == 5);
        AdsCloseTable(hCur);
    }

    {   // UPDATE modifies delete-marked rows as well (SAP-probed: 18/18).
        ADSHANDLE hCur = 0;
        UNSIGNED8 upd[] = "UPDATE del.dbf SET NM = 'x'";
        REQUIRE(AdsExecuteSQLDirect(hStmt, upd, &hCur) == 0);
        if (hCur) AdsCloseTable(hCur);
        ADSHANDLE hN = 0;
        REQUIRE(AdsOpenTable(hConn, tname, nullptr, ADS_CDX, 0, 0, 0,
                             ADS_EXCLUSIVE, &hN) == 0);
        REQUIRE(AdsGotoRecord(hN, 2) == 0);   // a deleted row
        UNSIGNED16 del = 0;
        AdsIsRecordDeleted(hN, &del);
        CHECK(del == 1);
        CHECK(read_field(hN, "NM") == "x");
        AdsCloseTable(hN);
    }

    // INSERT..SELECT copies deleted source rows too, as live rows (SAP's
    // SELECT INTO was probed doing the same; OA's parser lacks real-table
    // SELECT INTO entirely — separate gap, TODO.parity.md).
    {
        UNSIGNED8 cdef[]   = "ID,N,4,0";
        UNSIGNED8 cpname[] = "cp.dbf";
        ADSHANDLE hC = 0;
        REQUIRE(AdsCreateTable(hConn, cpname, nullptr, ADS_CDX,
                               0, 0, 0, 0, cdef, &hC) == 0);
        AdsCloseTable(hC);
        ADSHANDLE hCur = 0;
        UNSIGNED8 ins[] = "INSERT INTO cp.dbf SELECT ID FROM del.dbf";
        REQUIRE(AdsExecuteSQLDirect(hStmt, ins, &hCur) == 0);
        if (hCur) AdsCloseTable(hCur);
        CHECK(sql_count(hStmt, "SELECT COUNT(*) AS n FROM cp.dbf") == 5);
    }

    // AdsShowDeleted(0) hides them from SQL, same as navigation.
    AdsShowDeleted(0);
    CHECK(sql_count(hStmt, "SELECT COUNT(*) AS n FROM del.dbf") == 3);
    CHECK(sql_count(hStmt,
        "SELECT COUNT(*) AS n FROM del.dbf WHERE ID >= 1") == 3);
    {
        ADSHANDLE hCur = sql_cursor(hStmt,
            "SELECT ID FROM del.dbf ORDER BY ID DESC");
        CHECK(walk_rows(hCur) == 3);
        AdsCloseTable(hCur);
    }
    AdsShowDeleted(1);

    // ADT is different: SAP's AdsShowDeleted "has no effect upon ADT
    // tables" — deleted ADT rows can never be retrieved. Even under the
    // TRUE default, SQL must not surface them (caught live: OA briefly
    // showed delete-marked ADT audit rows and the pmsys trigger gate
    // diverged).
    {
        UNSIGNED8 adef[]  = "ID,Integer,4";
        UNSIGNED8 aname[] = "delta.adt";
        ADSHANDLE hA = 0;
        REQUIRE(AdsCreateTable(hConn, aname, nullptr, ADS_ADT,
                               0, 0, 0, 0, adef, &hA) == 0);
        for (int i = 1; i <= 4; ++i) {
            REQUIRE(AdsAppendRecord(hA) == 0);
            AdsSetDouble(hA, fid, static_cast<double>(i));
            REQUIRE(AdsWriteRecord(hA) == 0);
        }
        for (UNSIGNED32 r : {1u, 3u}) {
            REQUIRE(AdsGotoRecord(hA, r) == 0);
            REQUIRE(AdsDeleteRecord(hA) == 0);
        }
        AdsCloseTable(hA);
        CHECK(sql_count(hStmt,
            "SELECT COUNT(*) AS n FROM delta.adt") == 2);
        CHECK(sql_count(hStmt,
            "SELECT COUNT(*) AS n FROM delta.adt WHERE ID >= 1") == 2);
    }

    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}
