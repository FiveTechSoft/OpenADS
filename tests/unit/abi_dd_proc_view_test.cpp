#include "doctest.h"
#include "openads/ace.h"
#include "openads/error.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

fs::path make_simple_dbf(const fs::path& dir, const char* leaf) {
    fs::create_directories(dir);
    auto p = dir / leaf;
    std::vector<std::uint8_t> file;
    std::array<std::uint8_t, 32> hdr{};
    hdr[0] = 0x03;
    const std::uint16_t hdr_size = 32 + 32 + 1;
    hdr[8] = hdr_size & 0xFF; hdr[9] = (hdr_size >> 8) & 0xFF;
    hdr[10] = 1 + 4; hdr[11] = 0;
    file.insert(file.end(), hdr.begin(), hdr.end());
    std::array<std::uint8_t, 32> fd{};
    std::strncpy(reinterpret_cast<char*>(fd.data()), "VAL", 11);
    fd[11] = 'N'; fd[16] = 4; fd[17] = 0;
    file.insert(file.end(), fd.begin(), fd.end());
    file.push_back(0x0D);
    file.push_back(0x1A);
    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
    return p;
}

struct PvFixture {
    fs::path  dir;
    ADSHANDLE hConn = 0;

    PvFixture() {
        dir = fs::temp_directory_path() / "openads_dd_pv";
        std::error_code ec;
        fs::remove_all(dir, ec);
        make_simple_dbf(dir, "stock.dbf");

        auto add_path = (dir / "openads.add").string();
        UNSIGNED8 add_buf[260];
        std::memcpy(add_buf, add_path.c_str(), add_path.size() + 1);
        REQUIRE(AdsDDCreate(add_buf, 0, nullptr, &hConn) == 0);
        UNSIGNED8 alias[16] = "stock";
        UNSIGNED8 path[32]  = "stock.dbf";
        REQUIRE(AdsDDAddTable(hConn, alias, path, 0, 0, nullptr, nullptr) == 0);
    }

    ~PvFixture() {
        if (hConn) AdsDisconnect(hConn);
    }
};

} // namespace

// ---- Stored procedures ---------------------------------------------------

TEST_CASE("AdsDDCreateProcedure + AdsDDGetProcProperty round-trip") {
    PvFixture f;

    UNSIGNED8 name[32]      = "sp_restock";
    UNSIGNED8 container[64] = "procs.dll";
    UNSIGNED8 proc[64]      = "do_restock";
    UNSIGNED8 input[32]     = "qty:N";
    UNSIGNED8 output[32]    = "result:C";

    REQUIRE(AdsDDCreateProcedure(f.hConn, name, container, proc,
                                  0, input, output, nullptr) == 0);

    char buf[128]; UNSIGNED16 len = sizeof(buf);

    REQUIRE(AdsDDGetProcProperty(f.hConn, name, ADS_DD_PROC_CONTAINER,
                                  buf, &len) == 0);
    CHECK(std::string(buf, len) == "procs.dll");

    len = sizeof(buf);
    REQUIRE(AdsDDGetProcProperty(f.hConn, name, ADS_DD_PROC_PROC_NAME,
                                  buf, &len) == 0);
    CHECK(std::string(buf, len) == "do_restock");

    len = sizeof(buf);
    REQUIRE(AdsDDGetProcProperty(f.hConn, name, ADS_DD_PROC_INPUT,
                                  buf, &len) == 0);
    CHECK(std::string(buf, len) == "qty:N");

    len = sizeof(buf);
    REQUIRE(AdsDDGetProcProperty(f.hConn, name, ADS_DD_PROC_OUTPUT,
                                  buf, &len) == 0);
    CHECK(std::string(buf, len) == "result:C");
}

TEST_CASE("AdsDDSetProcProperty — update comment") {
    PvFixture f;

    UNSIGNED8 name[32] = "sp_audit";
    REQUIRE(AdsDDCreateProcedure(f.hConn, name, nullptr, nullptr,
                                  0, nullptr, nullptr, nullptr) == 0);

    const char* cmt = "audit log proc";
    REQUIRE(AdsDDSetProcProperty(f.hConn, name, ADS_DD_PROC_COMMENT,
                                  const_cast<char*>(cmt),
                                  static_cast<UNSIGNED16>(std::strlen(cmt))) == 0);

    char buf[128]; UNSIGNED16 len = sizeof(buf);
    REQUIRE(AdsDDGetProcProperty(f.hConn, name, ADS_DD_PROC_COMMENT,
                                  buf, &len) == 0);
    CHECK(std::string(buf, len) == "audit log proc");
}

TEST_CASE("AdsDDDropProcedure — removes procedure") {
    PvFixture f;

    UNSIGNED8 name[32] = "sp_drop_me";
    REQUIRE(AdsDDCreateProcedure(f.hConn, name, nullptr, nullptr,
                                  0, nullptr, nullptr, nullptr) == 0);
    REQUIRE(AdsDDDropProcedure(f.hConn, name) == 0);

    char buf[64]; UNSIGNED16 len = sizeof(buf);
    CHECK(AdsDDGetProcProperty(f.hConn, name, ADS_DD_PROC_CONTAINER,
                                buf, &len) != 0);
}

TEST_CASE("AdsDDCreateProcedure — persists across DD reopen") {
    const auto dir = fs::temp_directory_path() / "openads_dd_proc_persist";
    std::error_code ec;
    fs::remove_all(dir, ec);
    make_simple_dbf(dir, "x.dbf");

    auto add_path = (dir / "openads.add").string();
    UNSIGNED8 add_buf[260];
    std::memcpy(add_buf, add_path.c_str(), add_path.size() + 1);

    {
        ADSHANDLE hConn = 0;
        REQUIRE(AdsDDCreate(add_buf, 0, nullptr, &hConn) == 0);
        UNSIGNED8 alias[4] = "x", path[8] = "x.dbf";
        REQUIRE(AdsDDAddTable(hConn, alias, path, 0, 0, nullptr, nullptr) == 0);
        UNSIGNED8 name[32] = "sp_calc";
        UNSIGNED8 container[32] = "calc.dll";
        REQUIRE(AdsDDCreateProcedure(hConn, name, container, nullptr,
                                      0, nullptr, nullptr, nullptr) == 0);
        AdsDisconnect(hConn);
    }

    {
        ADSHANDLE hConn = 0;
        REQUIRE(AdsConnect60(add_buf, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn) == 0);
        UNSIGNED8 name[32] = "sp_calc";
        char buf[64]; UNSIGNED16 len = sizeof(buf);
        REQUIRE(AdsDDGetProcProperty(hConn, name, ADS_DD_PROC_CONTAINER,
                                      buf, &len) == 0);
        CHECK(std::string(buf, len) == "calc.dll");
        AdsDisconnect(hConn);
    }
}

// ---- Views ---------------------------------------------------------------

TEST_CASE("AdsDDCreateView + AdsDDGetViewProperty round-trip") {
    PvFixture f;

    UNSIGNED8 name[32]    = "v_lowstock";
    UNSIGNED8 comment[64] = "Items below reorder point";
    UNSIGNED8 sql[256]    = "SELECT * FROM stock WHERE VAL < 10";

    REQUIRE(AdsDDCreateView(f.hConn, name, comment, sql) == 0);

    char buf[512]; UNSIGNED16 len = sizeof(buf);
    REQUIRE(AdsDDGetViewProperty(f.hConn, name, ADS_DD_VIEW_STMT,
                                  buf, &len) == 0);
    CHECK(std::string(buf, len) == "SELECT * FROM stock WHERE VAL < 10");

    len = sizeof(buf);
    REQUIRE(AdsDDGetViewProperty(f.hConn, name, ADS_DD_VIEW_COMMENT,
                                  buf, &len) == 0);
    CHECK(std::string(buf, len) == "Items below reorder point");
}

TEST_CASE("AdsDDSetViewProperty — update SQL") {
    PvFixture f;

    UNSIGNED8 name[32] = "v_all";
    UNSIGNED8 sql0[64] = "SELECT * FROM stock";
    REQUIRE(AdsDDCreateView(f.hConn, name, nullptr, sql0) == 0);

    const char* new_sql = "SELECT VAL FROM stock WHERE VAL > 0";
    REQUIRE(AdsDDSetViewProperty(f.hConn, name, ADS_DD_VIEW_STMT,
                                  const_cast<char*>(new_sql),
                                  static_cast<UNSIGNED16>(std::strlen(new_sql))) == 0);

    char buf[256]; UNSIGNED16 len = sizeof(buf);
    REQUIRE(AdsDDGetViewProperty(f.hConn, name, ADS_DD_VIEW_STMT,
                                  buf, &len) == 0);
    CHECK(std::string(buf, len) == "SELECT VAL FROM stock WHERE VAL > 0");
}

TEST_CASE("AdsDDDropView — removes view") {
    PvFixture f;

    UNSIGNED8 name[32] = "v_drop_me";
    UNSIGNED8 sql[32]  = "SELECT * FROM stock";
    REQUIRE(AdsDDCreateView(f.hConn, name, nullptr, sql) == 0);
    REQUIRE(AdsDDDropView(f.hConn, name) == 0);

    char buf[64]; UNSIGNED16 len = sizeof(buf);
    CHECK(AdsDDGetViewProperty(f.hConn, name, ADS_DD_VIEW_STMT,
                                buf, &len) != 0);
}

TEST_CASE("AdsDDCreateView — persists across DD reopen") {
    const auto dir = fs::temp_directory_path() / "openads_dd_view_persist";
    std::error_code ec;
    fs::remove_all(dir, ec);
    make_simple_dbf(dir, "y.dbf");

    auto add_path = (dir / "openads.add").string();
    UNSIGNED8 add_buf[260];
    std::memcpy(add_buf, add_path.c_str(), add_path.size() + 1);

    {
        ADSHANDLE hConn = 0;
        REQUIRE(AdsDDCreate(add_buf, 0, nullptr, &hConn) == 0);
        UNSIGNED8 alias[4] = "y", path[8] = "y.dbf";
        REQUIRE(AdsDDAddTable(hConn, alias, path, 0, 0, nullptr, nullptr) == 0);
        UNSIGNED8 vname[16] = "v_y";
        UNSIGNED8 vsql[64]  = "SELECT * FROM y";
        REQUIRE(AdsDDCreateView(hConn, vname, nullptr, vsql) == 0);
        AdsDisconnect(hConn);
    }

    {
        ADSHANDLE hConn = 0;
        REQUIRE(AdsConnect60(add_buf, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn) == 0);
        UNSIGNED8 vname[16] = "v_y";
        char buf[128]; UNSIGNED16 len = sizeof(buf);
        REQUIRE(AdsDDGetViewProperty(hConn, vname, ADS_DD_VIEW_STMT,
                                      buf, &len) == 0);
        CHECK(std::string(buf, len) == "SELECT * FROM y");
        AdsDisconnect(hConn);
    }
}

TEST_CASE("AdsDDDropView — unknown view returns error") {
    PvFixture f;
    UNSIGNED8 name[32] = "no_such_view";
    CHECK(AdsDDDropView(f.hConn, name) != 0);
}

// RCB 07/16/2026: system.storedprocedures declared its PROCEDURE column as a
// fixed CHAR(255), so any SQL body longer than 255 bytes came back as a
// truncated prefix — silent data loss in the catalog even though the DD and
// the execution path hold the full text (pmsys sp_SaveIntoAuditLog is 5359
// bytes). The column is now sized to the longest actual value. Same fix
// applies to system.functions FUNC_BODY, system.triggers CONTAINER and
// system.views VIEW_SQL.
TEST_CASE("system.storedprocedures returns >255-byte bodies untruncated") {
    PvFixture f;

    std::string body = "DECLARE @pad STRING;\r\n";
    while (body.size() < 1200)
        body += "  @pad = 'padding padding padding padding';\r\n";
    body += "  INSERT INTO __output VALUES ('END_MARKER_OK');";

    UNSIGNED8 name[32] = "sp_longbody";
    REQUIRE(AdsDDCreateProcedure(f.hConn, name, nullptr,
                (UNSIGNED8*)body.c_str(), 0, nullptr, nullptr, nullptr) == 0);

    ADSHANDLE hStmt = 0, hCur = 0;
    REQUIRE(AdsCreateSQLStatement(f.hConn, &hStmt) == 0);
    UNSIGNED8 sql[64] = "SELECT * FROM system.storedprocedures";
    REQUIRE(AdsExecuteSQLDirect(hStmt, sql, &hCur) == 0);

    std::vector<UNSIGNED8> val(16384, 0);
    UNSIGNED32 vlen = static_cast<UNSIGNED32>(val.size() - 1);
    UNSIGNED8 fld[16] = "SQL_Script";   // SAP column name (was PROCEDURE)
    REQUIRE(AdsGetField(hCur, fld, val.data(), &vlen, 0) == 0);
    std::string got(reinterpret_cast<char*>(val.data()), vlen);
    while (!got.empty() && got.back() == ' ') got.pop_back();

    CHECK(got.size() > 1200);                       // not the 255-byte prefix
    CHECK(got.find("END_MARKER_OK") != std::string::npos);

    AdsCloseTable(hCur);
    AdsCloseSQLStatement(hStmt);
}

// S5 — `SELECT ... FROM <view>` resolves the view's stored SELECT as a
// derived table and composes the outer clauses over it, matching SAP
// (oracle-probed on the mp corpus: TOP/WHERE/ORDER/COUNT/star over both
// mp views are byte-identical). Both mp views were unreadable (5004)
// before this: the SQL branch never consulted dd->views(), and the SAP
// binary View decode had sql/comment SWAPPED, so every imported view
// carried an empty statement (system.views showed the SQL under Comment).
TEST_CASE("SELECT FROM a DD view composes outer clauses like SAP") {
    PvFixture f;

    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(f.hConn, &hStmt) == 0);
    auto exec = [&](const char* text) {
        UNSIGNED8 sql[256] = {0};
        std::memcpy(sql, text, std::strlen(text));
        ADSHANDLE hc = 0;
        UNSIGNED32 rc = AdsExecuteSQLDirect(hStmt, sql, &hc);
        REQUIRE(rc == 0);
        return hc;
    };
    auto val = [&](ADSHANDLE hc, const char* col) {
        UNSIGNED8 fld[32] = {0};
        std::strncpy(reinterpret_cast<char*>(fld), col, sizeof(fld) - 1);
        UNSIGNED8 buf[64] = {0};
        UNSIGNED32 len = sizeof(buf);
        if (AdsGetString(hc, fld, buf, &len, ADS_NONE) != 0) return std::string("<err>");
        std::string s(reinterpret_cast<char*>(buf), len);
        auto b = s.find_first_not_of(' ');
        return b == std::string::npos ? std::string() : s.substr(b);
    };

    for (const char* ins : {"INSERT INTO stock VALUES (5)",
                            "INSERT INTO stock VALUES (15)",
                            "INSERT INTO stock VALUES (3)"}) {
        UNSIGNED8 sql[64] = {0};
        std::memcpy(sql, ins, std::strlen(ins));
        ADSHANDLE hc = 0;
        REQUIRE(AdsExecuteSQLDirect(hStmt, sql, &hc) == 0);
        if (hc) AdsCloseTable(hc);
    }

    UNSIGNED8 v1[16]  = "v_small";
    UNSIGNED8 v1s[64] = "SELECT VAL FROM stock WHERE VAL < 10";
    REQUIRE(AdsDDCreateView(f.hConn, v1, nullptr, v1s) == 0);
    UNSIGNED8 v2[16]  = "v_total";
    UNSIGNED8 v2s[64] = "SELECT SUM(VAL) AS Total_Value FROM stock";
    REQUIRE(AdsDDCreateView(f.hConn, v2, nullptr, v2s) == 0);

    {   // outer ORDER BY composes over the view's WHERE — and the row
        // the view hides (VAL 15) must NOT appear at the end.
        ADSHANDLE hc = exec("SELECT * FROM v_small ORDER BY VAL");
        REQUIRE(AdsGotoTop(hc) == 0);
        CHECK(val(hc, "VAL") == "3");
        REQUIRE(AdsSkip(hc, 1) == 0);
        CHECK(val(hc, "VAL") == "5");
        REQUIRE(AdsSkip(hc, 1) == 0);
        UNSIGNED16 eof = 0;
        REQUIRE(AdsAtEOF(hc, &eof) == 0);
        CHECK(eof == 1);
        AdsCloseTable(hc);
    }
    {   // outer aggregate over the view
        ADSHANDLE hc = exec("SELECT COUNT(*) AS n FROM v_small");
        REQUIRE(AdsGotoTop(hc) == 0);
        CHECK(val(hc, "n") == "2");
        AdsCloseTable(hc);
    }
    {   // aggregate VIEW: outer projection sees the view's alias
        ADSHANDLE hc = exec("SELECT Total_Value FROM v_total");
        REQUIRE(AdsGotoTop(hc) == 0);
        CHECK(val(hc, "Total_Value") == "23");
        AdsCloseTable(hc);
    }
    {   // case-insensitive view name
        ADSHANDLE hc = exec("SELECT COUNT(*) AS n FROM V_SMALL");
        REQUIRE(AdsGotoTop(hc) == 0);
        CHECK(val(hc, "n") == "2");
        AdsCloseTable(hc);
    }

    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
}
