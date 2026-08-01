// SQL URI backend smoke: sqlite:// connect, DDL, DML, filter, relations, ALTER.
// Runs in CI on every platform where OPENADS_WITH_SQLITE=ON (default).
#include "doctest.h"
#include "openads/ace.h"
#include "openads/error.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;

#if defined(OPENADS_WITH_SQLITE)

namespace {

int visible_count(ADSHANDLE h) {
    if (AdsGotoTop(h) != 0) return -1;
    int n = 0;
    for (;;) {
        UNSIGNED16 eof = 0;
        if (AdsAtEOF(h, &eof) != 0) return -1;
        if (eof) break;
        ++n;
        if (AdsSkip(h, 1) != 0) return -1;
    }
    return n;
}

UNSIGNED32 record_count(ADSHANDLE h) {
    UNSIGNED32 c = 0;
    if (AdsGetRecordCount(h, 0, &c) != 0) return 0;
    return c;
}

}  // namespace

TEST_CASE("SQL URI smoke: sqlite:// DDL + DML + filter + scoped relation + ALTER") {
    const auto dir = fs::temp_directory_path() / "openads_sql_uri_smoke";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    const auto db_path = dir / "smoke.db";

    const std::string uri = "sqlite://" + db_path.string();
    std::vector<UNSIGNED8> srv(uri.size() + 1);
    std::memcpy(srv.data(), uri.c_str(), uri.size() + 1);

    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv.data(), ADS_LOCAL_SERVER, nullptr, nullptr, 0,
                         &hConn) == 0);

    // CREATE parent + child via AdsCreateTable (SQL DDL path).
    UNSIGNED8 pdef[] = "GRP,Character,1";
    UNSIGNED8 pname[] = "grp";
    ADSHANDLE hP = 0;
    REQUIRE(AdsCreateTable(hConn, pname, nullptr, ADS_CDX, 0, 0, 0, 0, pdef,
                           &hP) == 0);
    for (const char* g : {"A", "B"}) {
        REQUIRE(AdsAppendRecord(hP) == 0);
        UNSIGNED8 f[8] = "GRP";
        REQUIRE(AdsSetString(hP, f, (UNSIGNED8*)g, 1) == 0);
        REQUIRE(AdsWriteRecord(hP) == 0);
    }

    UNSIGNED8 cdef[] = "GRP,Character,1;DATA,Character,8";
    UNSIGNED8 cname[] = "item";
    ADSHANDLE hC = 0;
    REQUIRE(AdsCreateTable(hConn, cname, nullptr, ADS_CDX, 0, 0, 0, 0, cdef,
                           &hC) == 0);
    struct Row { const char* grp; const char* data; } rows[] = {
        {"A", "a1"}, {"B", "b1"}, {"A", "a2"}, {"A", "a3"},
    };
    for (auto& r : rows) {
        REQUIRE(AdsAppendRecord(hC) == 0);
        UNSIGNED8 fg[8] = "GRP";
        UNSIGNED8 fd[8] = "DATA";
        REQUIRE(AdsSetString(hC, fg, (UNSIGNED8*)r.grp, 1) == 0);
        REQUIRE(AdsSetString(hC, fd, (UNSIGNED8*)r.data,
                             (UNSIGNED32)std::strlen(r.data)) == 0);
        REQUIRE(AdsWriteRecord(hC) == 0);
    }
    CHECK(record_count(hC) == 4u);

    // Index + controlling order on child (required for scoped relation).
    UNSIGNED8 idx_tag[16] = "grp";
    ADSHANDLE hIdx = 0;
    UNSIGNED16 nidx = 1;
    REQUIRE(AdsOpenIndex(hC, idx_tag, &hIdx, &nidx) == 0);
    REQUIRE(AdsSetIndexOrder(hC, idx_tag) == 0);

    // Plain relation: parent A -> child seeks first A row.
    UNSIGNED8 rexpr[] = "GRP";
    REQUIRE(AdsSetRelation(hP, hC, rexpr) == 0);
    REQUIRE(AdsGotoTop(hP) == 0);
    UNSIGNED16 found = 0;
    REQUIRE(AdsIsFound(hC, &found) == 0);
    CHECK(found == 1);

    // Scoped relation: only rows matching parent GRP are visible.
    REQUIRE(AdsClearRelation(hP) == 0);
    REQUIRE(AdsSetScopedRelation(hP, hC, rexpr) == 0);
    REQUIRE(AdsGotoTop(hP) == 0);
    CHECK(visible_count(hC) == 3);
    REQUIRE(AdsSkip(hP, 1) == 0);
    CHECK(visible_count(hC) == 1);
    REQUIRE(AdsClearRelation(hP) == 0);
    CHECK(visible_count(hC) == 4);

    // SET FILTER push-down on child.
    UNSIGNED8 filt[] = "GRP = 'A'";
    REQUIRE(AdsSetFilter(hC, filt) == 0);
    CHECK(record_count(hC) == 3u);
    UNSIGNED16 aof_lvl = 0;
    REQUIRE(AdsGetAOFOptLevel(hC, &aof_lvl, nullptr, nullptr) == 0);
    CHECK(aof_lvl == ADS_OPTIMIZED_FULL);
    REQUIRE(AdsClearFilter(hC) == 0);
    CHECK(record_count(hC) == 4u);

    // ALTER ADD via AdsRestructureTable.
    REQUIRE(AdsCloseIndex(hIdx) == 0);
    REQUIRE(AdsCloseTable(hC) == 0);
    UNSIGNED8 add[] = "NOTE,Character,4";
    REQUIRE(AdsRestructureTable(hConn, cname, nullptr, 0, 0, 0, 0, add,
                                nullptr, nullptr) == 0);
    REQUIRE(AdsOpenTable(hConn, cname, cname, ADS_DEFAULT, 0, 0, 0,
                         ADS_READONLY, &hC) == 0);
    CHECK(record_count(hC) == 4u);

    UNSIGNED16 nfields = 0;
    REQUIRE(AdsGetNumFields(hC, &nfields) == 0);
    CHECK(nfields == 3u);

    // ALTER DROP via AdsRestructureTable.
    REQUIRE(AdsCloseTable(hC) == 0);
    UNSIGNED8 drop[] = "NOTE";
    REQUIRE(AdsRestructureTable(hConn, cname, nullptr, 0, 0, 0, 0, nullptr,
                                drop, nullptr) == 0);
    REQUIRE(AdsOpenTable(hConn, cname, cname, ADS_DEFAULT, 0, 0, 0,
                         ADS_READONLY, &hC) == 0);
    REQUIRE(AdsGetNumFields(hC, &nfields) == 0);
    CHECK(nfields == 2u);

    // Navigational write smoke (dbAppend path).
    REQUIRE(AdsCloseTable(hC) == 0);
    REQUIRE(AdsOpenTable(hConn, cname, cname, ADS_DEFAULT, 0, 0, 0, 0,
                         &hC) == 0);
    REQUIRE(AdsAppendRecord(hC) == 0);
    UNSIGNED8 fg[8] = "GRP";
    UNSIGNED8 fd[8] = "DATA";
    REQUIRE(AdsSetString(hC, fg, (UNSIGNED8*)"A", 1) == 0);
    REQUIRE(AdsSetString(hC, fd, (UNSIGNED8*)"a4", 2) == 0);
    REQUIRE(AdsWriteRecord(hC) == 0);
    CHECK(record_count(hC) == 5u);

    // Prepared statement (AdsPrepareSQL + AdsExecuteSQL) on SQL URI.
    {
        REQUIRE(AdsCloseTable(hC) == 0);
        REQUIRE(AdsOpenTable(hConn, cname, cname, ADS_DEFAULT, 0, 0, 0, 0,
                             &hC) == 0);
        ADSHANDLE hStmt = 0;
        REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);
        UNSIGNED8 ins[] =
            "INSERT INTO item (GRP, DATA) VALUES (:g, :d)";
        REQUIRE(AdsPrepareSQL(hStmt, ins) == 0);
        UNSIGNED8 pg[] = "g";
        UNSIGNED8 pd[] = "d";
        REQUIRE(AdsSetString(hStmt, pg, (UNSIGNED8*)"Z", 1) == 0);
        REQUIRE(AdsSetString(hStmt, pd, (UNSIGNED8*)"zprep", 5) == 0);
        ADSHANDLE hCur = 0;
        REQUIRE(AdsExecuteSQL(hStmt, &hCur) == 0);
        CHECK(record_count(hC) == 6u);
        AdsCloseSQLStatement(hStmt);
    }

    // system.tables via AdsOpenTable (SR_MGMNT-style catalog workarea).
    {
        UNSIGNED8 sysname[] = "system.tables";
        ADSHANDLE hSys = 0;
        REQUIRE(AdsOpenTable(hConn, sysname, sysname, ADS_DEFAULT, 0, 0, 0,
                             ADS_READONLY, &hSys) == 0);
        UNSIGNED32 n = 0;
        REQUIRE(AdsGetRecordCount(hSys, 0, &n) == 0);
        CHECK(n >= 2u);
        REQUIRE(AdsCloseTable(hSys) == 0);
    }

    // system.tables / system.columns catalog (SQL URI).
    {
        ADSHANDLE hStmt = 0;
        REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);
        UNSIGNED8 stbl[] = "SELECT * FROM system.tables";
        ADSHANDLE hCur = 0;
        REQUIRE(AdsExecuteSQLDirect(hStmt, stbl, &hCur) == 0);
        REQUIRE(hCur != 0);
        std::set<std::string> names;
        REQUIRE(AdsGotoTop(hCur) == 0);
        for (;;) {
            UNSIGNED16 eof = 0;
            REQUIRE(AdsAtEOF(hCur, &eof) == 0);
            if (eof) break;
            UNSIGNED8 fname[8] = "Name";
            UNSIGNED8 vbuf[256] = {};
            UNSIGNED32 vlen = sizeof(vbuf) - 1;
            REQUIRE(AdsGetField(hCur, fname, vbuf, &vlen, 0) == 0);
            std::string n(reinterpret_cast<const char*>(vbuf), vlen);
            while (!n.empty() && n.back() == ' ') n.pop_back();
            names.insert(n);
            REQUIRE(AdsSkip(hCur, 1) == 0);
        }
        CHECK(names.count("grp") == 1);
        CHECK(names.count("item") == 1);
        REQUIRE(AdsCloseTable(hCur) == 0);

        UNSIGNED8 scol[] = "SELECT * FROM system.columns";
        hCur = 0;
        REQUIRE(AdsExecuteSQLDirect(hStmt, scol, &hCur) == 0);
        REQUIRE(hCur != 0);
        int item_cols = 0;
        REQUIRE(AdsGotoTop(hCur) == 0);
        for (;;) {
            UNSIGNED16 eof = 0;
            REQUIRE(AdsAtEOF(hCur, &eof) == 0);
            if (eof) break;
            UNSIGNED8 tfn[16] = "TABLE_NAME";
            UNSIGNED8 cfn[16] = "COL_NAME";
            UNSIGNED8 tbuf[64] = {};
            UNSIGNED8 cbuf[64] = {};
            UNSIGNED32 tlen = sizeof(tbuf) - 1;
            UNSIGNED32 clen = sizeof(cbuf) - 1;
            REQUIRE(AdsGetField(hCur, tfn, tbuf, &tlen, 0) == 0);
            REQUIRE(AdsGetField(hCur, cfn, cbuf, &clen, 0) == 0);
            std::string tbl(reinterpret_cast<const char*>(tbuf), tlen);
            while (!tbl.empty() && tbl.back() == ' ') tbl.pop_back();
            if (tbl == "item") ++item_cols;
            REQUIRE(AdsSkip(hCur, 1) == 0);
        }
        CHECK(item_cols == 2);
        REQUIRE(AdsCloseTable(hCur) == 0);

        UNSIGNED8 spk[] = "SELECT * FROM system.primarykeys";
        hCur = 0;
        REQUIRE(AdsExecuteSQLDirect(hStmt, spk, &hCur) == 0);
        REQUIRE(hCur != 0);
        REQUIRE(AdsCloseTable(hCur) == 0);

        UNSIGNED8 sixname[] = "system.indexes";
        ADSHANDLE hIdxSys = 0;
        REQUIRE(AdsOpenTable(hConn, sixname, sixname, ADS_DEFAULT, 0, 0, 0,
                             ADS_READONLY, &hIdxSys) == 0);
        REQUIRE(AdsCloseTable(hIdxSys) == 0);

        // SR_MGMNT synthetic ACL on SQL URI (open-access PUBLIC grants).
        UNSIGNED8 sugname[] = "system.usergroups";
        ADSHANDLE hUg = 0;
        REQUIRE(AdsOpenTable(hConn, sugname, sugname, ADS_DEFAULT, 0, 0, 0,
                             ADS_READONLY, &hUg) == 0);
        REQUIRE(AdsGotoTop(hUg) == 0);
        UNSIGNED8 gfn[16] = "Name";   // SAP's system.usergroups column
        UNSIGNED8 gbuf[64] = {};
        UNSIGNED32 glen = sizeof(gbuf) - 1;
        REQUIRE(AdsGetField(hUg, gfn, gbuf, &glen, 0) == 0);
        std::string grp(reinterpret_cast<const char*>(gbuf), glen);
        while (!grp.empty() && grp.back() == ' ') grp.pop_back();
        CHECK(grp == "PUBLIC");
        REQUIRE(AdsCloseTable(hUg) == 0);

        UNSIGNED8 spermsname[] = "system.permissions";
        ADSHANDLE hPerm = 0;
        REQUIRE(AdsOpenTable(hConn, spermsname, spermsname, ADS_DEFAULT, 0, 0,
                             0, ADS_READONLY, &hPerm) == 0);
        bool found_item = false;
        REQUIRE(AdsGotoTop(hPerm) == 0);
        for (;;) {
            UNSIGNED16 eof_perm = 0;
            REQUIRE(AdsAtEOF(hPerm, &eof_perm) == 0);
            if (eof_perm) break;
            UNSIGNED8 obuf[64] = {};
            UNSIGNED8 tbuf[8] = {};
            UNSIGNED8 geebuf[16] = {};
            UNSIGNED8 sbuf[16] = {};
            UNSIGNED32 olen = sizeof(obuf) - 1;
            UNSIGNED32 tlen = sizeof(tbuf) - 1;
            UNSIGNED32 geelen = sizeof(geebuf) - 1;
            UNSIGNED32 slen = sizeof(sbuf) - 1;
            UNSIGNED8 ofn[16] = "OBJ_NAME";
            UNSIGNED8 tfn[16] = "OBJ_TYPE";
            UNSIGNED8 gee[16] = "GRANTEE";
            UNSIGNED8 sfn[16] = "SELECT";
            REQUIRE(AdsGetField(hPerm, ofn, obuf, &olen, 0) == 0);
            REQUIRE(AdsGetField(hPerm, tfn, tbuf, &tlen, 0) == 0);
            std::string obj(reinterpret_cast<const char*>(obuf), olen);
            while (!obj.empty() && obj.back() == ' ') obj.pop_back();
            std::string typ(reinterpret_cast<const char*>(tbuf), tlen);
            while (!typ.empty() && typ.back() == ' ') typ.pop_back();
            if (obj == "item" && typ == "1") {
                REQUIRE(AdsGetField(hPerm, gee, geebuf, &geelen, 0) == 0);
                REQUIRE(AdsGetField(hPerm, sfn, sbuf, &slen, 0) == 0);
                std::string grantee(reinterpret_cast<const char*>(geebuf), geelen);
                while (!grantee.empty() && grantee.back() == ' ')
                    grantee.pop_back();
                CHECK(grantee == "PUBLIC");
                std::string sel(reinterpret_cast<const char*>(sbuf), slen);
                while (!sel.empty() && sel.back() == ' ') sel.pop_back();
                CHECK(sel == "2");
                found_item = true;
                break;
            }
            REQUIRE(AdsSkip(hPerm, 1) == 0);
        }
        CHECK(found_item);
        REQUIRE(AdsCloseTable(hPerm) == 0);

        // Remaining SR_MGMNT catalog tables (empty or native catalog).
        for (const char* sys :
             {"system.views", "system.triggers", "system.storedprocedures",
              "system.functions", "system.links"}) {
            std::vector<UNSIGNED8> sname(std::strlen(sys) + 1);
            std::memcpy(sname.data(), sys, sname.size());
            ADSHANDLE hSys = 0;
            REQUIRE(AdsOpenTable(hConn, sname.data(), sname.data(),
                                 ADS_DEFAULT, 0, 0, 0, ADS_READONLY,
                                 &hSys) == 0);
            REQUIRE(AdsCloseTable(hSys) == 0);
        }

        // system.* rewrite preserves client WHERE clause.
        UNSIGNED8 sperms_where[] =
            "SELECT OBJ_NAME FROM system.permissions "
            "WHERE OBJ_NAME = 'item' AND OBJ_TYPE = '1'";
        hCur = 0;
        REQUIRE(AdsExecuteSQLDirect(hStmt, sperms_where, &hCur) == 0);
        REQUIRE(hCur != 0);
        int perm_rows = 0;
        REQUIRE(AdsGotoTop(hCur) == 0);
        for (;;) {
            UNSIGNED16 eof_w = 0;
            REQUIRE(AdsAtEOF(hCur, &eof_w) == 0);
            if (eof_w) break;
            ++perm_rows;
            REQUIRE(AdsSkip(hCur, 1) == 0);
        }
        CHECK(perm_rows == 1);
        REQUIRE(AdsCloseTable(hCur) == 0);

        // GRANT persisted in OPENADS$ACL (SQL URI, no Advantage DD).
        UNSIGNED8 grant_sql[] = "GRANT SELECT ON item TO alice";
        hCur = 0;
        REQUIRE(AdsExecuteSQLDirect(hStmt, grant_sql, &hCur) == 0);
        CHECK(hCur == 0);
        UNSIGNED8 sperms_alice[] = "system.permissions";
        ADSHANDLE hPermAlice = 0;
        REQUIRE(AdsOpenTable(hConn, sperms_alice, sperms_alice, ADS_DEFAULT,
                             0, 0, 0, ADS_READONLY, &hPermAlice) == 0);
        bool found_alice = false;
        REQUIRE(AdsGotoTop(hPermAlice) == 0);
        for (;;) {
            UNSIGNED16 eof_a = 0;
            REQUIRE(AdsAtEOF(hPermAlice, &eof_a) == 0);
            if (eof_a) break;
            UNSIGNED8 obuf[64] = {};
            UNSIGNED8 abuf[16] = {};
            UNSIGNED8 sbuf[8] = {};
            UNSIGNED32 olen = sizeof(obuf) - 1;
            UNSIGNED32 alen = sizeof(abuf) - 1;
            UNSIGNED32 slen = sizeof(sbuf) - 1;
            UNSIGNED8 ofn[16] = "OBJ_NAME";
            UNSIGNED8 afn[16] = "GRANTEE";
            UNSIGNED8 sfn[16] = "SELECT";
            REQUIRE(AdsGetField(hPermAlice, ofn, obuf, &olen, 0) == 0);
            REQUIRE(AdsGetField(hPermAlice, afn, abuf, &alen, 0) == 0);
            std::string obj(reinterpret_cast<const char*>(obuf), olen);
            std::string gee(reinterpret_cast<const char*>(abuf), alen);
            while (!obj.empty() && obj.back() == ' ') obj.pop_back();
            while (!gee.empty() && gee.back() == ' ') gee.pop_back();
            if (obj == "item" && gee == "alice") {
                REQUIRE(AdsGetField(hPermAlice, sfn, sbuf, &slen, 0) == 0);
                std::string sel(reinterpret_cast<const char*>(sbuf), slen);
                while (!sel.empty() && sel.back() == ' ') sel.pop_back();
                CHECK(sel == "2");
                found_alice = true;
                break;
            }
            REQUIRE(AdsSkip(hPermAlice, 1) == 0);
        }
        CHECK(found_alice);
        REQUIRE(AdsCloseTable(hPermAlice) == 0);

        AdsCloseSQLStatement(hStmt);
    }

    // SQLite CHANGE via table-rebuild path (no native ALTER COLUMN length).
    {
        REQUIRE(AdsCloseTable(hC) == 0);
        UNSIGNED8 chg[] = "DATA,Character,16";
        REQUIRE(AdsRestructureTable(hConn, cname, nullptr, 0, 0, 0, 0, nullptr,
                                    nullptr, chg) == 0);
        REQUIRE(AdsOpenTable(hConn, cname, cname, ADS_DEFAULT, 0, 0, 0,
                             ADS_READONLY, &hC) == 0);
        CHECK(record_count(hC) == 6u);
        REQUIRE(AdsGotoTop(hC) == 0);
        UNSIGNED8 data_fld[8] = "DATA";
        UNSIGNED8 vbuf[32] = {};
        UNSIGNED32 vlen = sizeof(vbuf) - 1;
        REQUIRE(AdsGetField(hC, data_fld, vbuf, &vlen, 0) == 0);
        std::string val(reinterpret_cast<const char*>(vbuf), vlen);
        while (!val.empty() && val.back() == ' ') val.pop_back();
        CHECK(val == "a1");
        UNSIGNED32 fl = 0;
        REQUIRE(AdsGetFieldLength(hC, data_fld, &fl) == 0);
        CHECK(fl == 16u);
    }

    // SQLite CHANGE retype C→N (table-rebuild + CAST).
    {
        REQUIRE(AdsCloseTable(hC) == 0);
        UNSIGNED8 chg_num[] = "DATA,Numeric,10";
        REQUIRE(AdsRestructureTable(hConn, cname, nullptr, 0, 0, 0, 0, nullptr,
                                    nullptr, chg_num) == 0);
        REQUIRE(AdsOpenTable(hConn, cname, cname, ADS_DEFAULT, 0, 0, 0,
                             ADS_READONLY, &hC) == 0);
        REQUIRE(AdsGotoTop(hC) == 0);
        UNSIGNED8 data_fld[8] = "DATA";
        double dval = -1.0;
        REQUIRE(AdsGetDouble(hC, data_fld, &dval) == 0);
        CHECK(dval == 0.0);
    }

    // Runtime ACL: REVOKE PUBLIC, GRANT alice, enforce usCheckRights on open.
    {
        REQUIRE(AdsCloseTable(hC) == 0);
        ADSHANDLE hStmtAcl = 0;
        REQUIRE(AdsCreateSQLStatement(hConn, &hStmtAcl) == 0);
        UNSIGNED8 revoke_pub[] = "REVOKE SELECT ON item FROM PUBLIC";
        ADSHANDLE hCurAcl = 0;
        REQUIRE(AdsExecuteSQLDirect(hStmtAcl, revoke_pub, &hCurAcl) == 0);
        UNSIGNED8 grant_alice_acl[] = "GRANT SELECT ON item TO alice";
        REQUIRE(AdsExecuteSQLDirect(hStmtAcl, grant_alice_acl, &hCurAcl) == 0);
        AdsCloseSQLStatement(hStmtAcl);

        UNSIGNED8 user_alice[] = "alice";
        ADSHANDLE hAlice = 0;
        REQUIRE(AdsConnect60(srv.data(), ADS_LOCAL_SERVER, user_alice,
                             nullptr, 0, &hAlice) == 0);
        ADSHANDLE hAliceTbl = 0;
        REQUIRE(AdsOpenTable(hAlice, cname, cname, ADS_DEFAULT, 0, 0, 1,
                             ADS_READONLY, &hAliceTbl) == 0);
        REQUIRE(AdsCloseTable(hAliceTbl) == 0);

        UNSIGNED8 user_bob[] = "bob";
        ADSHANDLE hBob = 0;
        REQUIRE(AdsConnect60(srv.data(), ADS_LOCAL_SERVER, user_bob,
                             nullptr, 0, &hBob) == 0);
        ADSHANDLE hStmtBob = 0;
        REQUIRE(AdsCreateSQLStatement(hConn, &hStmtBob) == 0);
        ADSHANDLE hCurBob = 0;
        UNSIGNED8 sbob[] =
            "SELECT Name FROM system.users WHERE Name = 'bob'";
        REQUIRE(AdsExecuteSQLDirect(hStmtBob, sbob, &hCurBob) == 0);
        REQUIRE(hCurBob != 0);
        REQUIRE(AdsGotoTop(hCurBob) == 0);
        UNSIGNED16 eof_bob = 0;
        REQUIRE(AdsAtEOF(hCurBob, &eof_bob) == 0);
        CHECK(eof_bob == 0);
        REQUIRE(AdsCloseTable(hCurBob) == 0);
        AdsCloseSQLStatement(hStmtBob);
        ADSHANDLE hBobTbl = 0;
        CHECK(AdsOpenTable(hBob, cname, cname, ADS_DEFAULT, 0, 0, 1,
                           ADS_READONLY, &hBobTbl) != 0);
        REQUIRE(AdsDisconnect(hBob) == 0);
        REQUIRE(AdsDisconnect(hAlice) == 0);

        REQUIRE(AdsOpenTable(hConn, cname, cname, ADS_DEFAULT, 0, 0, 0,
                             ADS_READONLY, &hC) == 0);
    }

    // Group ACL + navigational INSERT enforcement with usCheckRights.
    {
        REQUIRE(AdsCloseTable(hC) == 0);
        ADSHANDLE hStmtGrp = 0;
        REQUIRE(AdsCreateSQLStatement(hConn, &hStmtGrp) == 0);
        ADSHANDLE hCurGrp = 0;
        UNSIGNED8 grant_grp[] = "GRANT GROUP SALES TO carol";
        REQUIRE(AdsExecuteSQLDirect(hStmtGrp, grant_grp, &hCurGrp) == 0);
        UNSIGNED8 revoke_ins[] = "REVOKE INSERT ON item FROM PUBLIC";
        REQUIRE(AdsExecuteSQLDirect(hStmtGrp, revoke_ins, &hCurGrp) == 0);
        UNSIGNED8 grant_sales[] = "GRANT INSERT ON item TO SALES";
        REQUIRE(AdsExecuteSQLDirect(hStmtGrp, grant_sales, &hCurGrp) == 0);
        UNSIGNED8 grant_sel_pub[] = "GRANT SELECT ON item TO PUBLIC";
        REQUIRE(AdsExecuteSQLDirect(hStmtGrp, grant_sel_pub, &hCurGrp) == 0);

        // Persisted membership surfaces in system.usergroupmembers / users.
        UNSIGNED8 smem[] =
            "SELECT USER_NAME, GROUP_NAME FROM system.usergroupmembers "
            "WHERE USER_NAME = 'carol'";
        REQUIRE(AdsExecuteSQLDirect(hStmtGrp, smem, &hCurGrp) == 0);
        REQUIRE(hCurGrp != 0);
        REQUIRE(AdsGotoTop(hCurGrp) == 0);
        UNSIGNED16 eof_mem = 0;
        REQUIRE(AdsAtEOF(hCurGrp, &eof_mem) == 0);
        CHECK(eof_mem == 0);
        UNSIGNED8 ufn[16] = "USER_NAME";
        UNSIGNED8 gfn2[16] = "GROUP_NAME";
        UNSIGNED8 ubuf[64] = {};
        UNSIGNED8 gbuf2[64] = {};
        UNSIGNED32 ulen = sizeof(ubuf) - 1;
        UNSIGNED32 glen2 = sizeof(gbuf2) - 1;
        REQUIRE(AdsGetField(hCurGrp, ufn, ubuf, &ulen, 0) == 0);
        REQUIRE(AdsGetField(hCurGrp, gfn2, gbuf2, &glen2, 0) == 0);
        std::string uname(reinterpret_cast<const char*>(ubuf), ulen);
        std::string gname(reinterpret_cast<const char*>(gbuf2), glen2);
        while (!uname.empty() && uname.back() == ' ') uname.pop_back();
        while (!gname.empty() && gname.back() == ' ') gname.pop_back();
        CHECK(uname == "carol");
        CHECK(gname == "SALES");
        REQUIRE(AdsCloseTable(hCurGrp) == 0);

        UNSIGNED8 susers[] =
            "SELECT Name FROM system.users WHERE Name = 'carol'";
        REQUIRE(AdsExecuteSQLDirect(hStmtGrp, susers, &hCurGrp) == 0);
        REQUIRE(hCurGrp != 0);
        REQUIRE(AdsGotoTop(hCurGrp) == 0);
        UNSIGNED16 eof_usr = 0;
        REQUIRE(AdsAtEOF(hCurGrp, &eof_usr) == 0);
        CHECK(eof_usr == 0);
        ulen = sizeof(ubuf) - 1;
        // system.users exposes SAP's "Name"; ufn above is User_Name, which is
        // the usergroupmembers column and does not exist on system.users.
        UNSIGNED8 nfn[16] = "Name";
        REQUIRE(AdsGetField(hCurGrp, nfn, ubuf, &ulen, 0) == 0);
        uname = std::string(reinterpret_cast<const char*>(ubuf), ulen);
        while (!uname.empty() && uname.back() == ' ') uname.pop_back();
        CHECK(uname == "carol");
        REQUIRE(AdsCloseTable(hCurGrp) == 0);

        AdsCloseSQLStatement(hStmtGrp);

        UNSIGNED8 user_carol[] = "carol";
        ADSHANDLE hCarol = 0;
        REQUIRE(AdsConnect60(srv.data(), ADS_LOCAL_SERVER, user_carol,
                             nullptr, 0, &hCarol) == 0);
        ADSHANDLE hCarolTbl = 0;
        REQUIRE(AdsOpenTable(hCarol, cname, cname, ADS_DEFAULT, 0, 0, 1,
                             ADS_SHARED, &hCarolTbl) == 0);
        REQUIRE(AdsAppendRecord(hCarolTbl) == 0);
        REQUIRE(AdsCloseTable(hCarolTbl) == 0);

        UNSIGNED8 user_dave[] = "dave";
        ADSHANDLE hDave = 0;
        REQUIRE(AdsConnect60(srv.data(), ADS_LOCAL_SERVER, user_dave,
                             nullptr, 0, &hDave) == 0);
        ADSHANDLE hDaveTbl = 0;
        REQUIRE(AdsOpenTable(hDave, cname, cname, ADS_DEFAULT, 0, 0, 1,
                             ADS_READONLY, &hDaveTbl) == 0);
        CHECK(AdsAppendRecord(hDaveTbl) != 0);
        REQUIRE(AdsCloseTable(hDaveTbl) == 0);
        REQUIRE(AdsDisconnect(hDave) == 0);
        REQUIRE(AdsDisconnect(hCarol) == 0);

        REQUIRE(AdsOpenTable(hConn, cname, cname, ADS_DEFAULT, 0, 0, 0,
                             ADS_READONLY, &hC) == 0);
    }

    REQUIRE(AdsCloseTable(hC) == 0);
    REQUIRE(AdsCloseTable(hP) == 0);

    // AdsDropTable on SQL connection.
    REQUIRE(AdsDropTable(hConn, cname, 0) == 0);
    REQUIRE(AdsDropTable(hConn, pname, 0) == 0);

    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

#else

TEST_CASE("SQL URI smoke: disabled without OPENADS_WITH_SQLITE") {
    MESSAGE("OPENADS_WITH_SQLITE=OFF at compile time");
}

#endif