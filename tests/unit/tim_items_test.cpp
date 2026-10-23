// Tests for Tim Stone's open items 1 to 3 (MLS 2026 notes), ported from
// bedipritpal/OpenADS dbd93b2e. His item 4 test (anonymous login under
// daemon_hardening) is left out: the hardening/auth stack is not upstream.
//   item 1: DbCreate("zzdectest", ...) in ADT mode, no extension, must make zzdectest.adt, not .dbf.
//   item 2: SQL CREATE TABLE [t2.adt] (...) must write an ADT table (ADT header, .adm memo), not a VFP DBF with .fpt.
//   item 3: ADT table with memo and N(9,4): append, put 0, put 12.3456, commit, unlock, get must give 12.3456.
// "tim item" in each test name lets CI run only these.
#include "doctest.h"
#include "openads/ace.h"
#include "network/server.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::vector<UNSIGNED8> cbuf(const std::string& s) {
    std::vector<UNSIGNED8> v(s.begin(), s.end());
    v.push_back(0);
    return v;
}

fs::path fresh_dir(const char* leaf) {
    auto d = fs::temp_directory_path() / leaf;
    std::error_code ec;
    fs::remove_all(d, ec);
    fs::create_directories(d);
    return d;
}

ADSHANDLE connect_remote(const fs::path& dir, std::uint16_t port) {
    auto u = cbuf("tcp://127.0.0.1:" + std::to_string(port) + "/" + dir.generic_string());
    ADSHANDLE h = 0;
    REQUIRE(AdsConnect60(u.data(), ADS_REMOTE_SERVER, nullptr, nullptr, 0, &h) == 0);
    REQUIRE(h != 0);
    return h;
}

ADSHANDLE connect_local(const fs::path& dir) {
    auto u = cbuf(dir.string());
    ADSHANDLE h = 0;
    REQUIRE(AdsConnect60(u.data(), ADS_LOCAL_SERVER, nullptr, nullptr, 0, &h) == 0);
    REQUIRE(h != 0);
    return h;
}

int first_byte(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    char c = 0;
    f.read(&c, 1);
    return f ? static_cast<unsigned char>(c) : -1;
}

void create_no_ext_adt(ADSHANDLE hConn, const fs::path& dir) {
    auto name   = cbuf("zzdectest");
    auto fields = cbuf("Valor,Numeric,9,4;Nome,Character,10");
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, name.data(), nullptr, ADS_ADT, ADS_ANSI,
                           0, 0, 0, fields.data(), &hT) == 0);
    REQUIRE(AdsCloseTable(hT) == 0);
    CHECK_MESSAGE(fs::exists(dir / "zzdectest.adt"), "expected zzdectest.adt");
    CHECK_MESSAGE(!fs::exists(dir / "zzdectest.dbf"), "zzdectest.dbf must not exist");
}

void sql_create_bracketed_adt(ADSHANDLE hConn, const fs::path& dir) {
    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);
    // Harbour rddads does not call AdsStmtSetTableType for ADT, so neither do we.
    auto sql = cbuf("CREATE TABLE [t2.adt] (a NUMERIC(9,4), b CHAR(10), m MEMO)");
    ADSHANDLE hCur = 0;
    REQUIRE(AdsExecuteSQLDirect(hStmt, sql.data(), &hCur) == 0);
    if (hCur != 0) AdsCloseTable(hCur);
    AdsCloseSQLStatement(hStmt);
    REQUIRE(fs::exists(dir / "t2.adt"));
    CHECK_MESSAGE(first_byte(dir / "t2.adt") != 0xF5, "t2.adt has a VFP DBF header (0xF5)");
    CHECK_MESSAGE(!fs::exists(dir / "t2.fpt"), "t2.fpt must not exist");
    CHECK_MESSAGE(fs::exists(dir / "t2.adm"), "expected t2.adm (ADT memo file)");
}

} // namespace

TEST_CASE("tim item 1: REMOTE create without extension in ADT mode makes .adt") {
    auto dir = fresh_dir("openads_tim_item1_remote");
    openads::network::Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    ADSHANDLE h = connect_remote(dir, srv.port());
    create_no_ext_adt(h, dir);
    AdsDisconnect(h);
}

TEST_CASE("tim item 1 control: LOCAL create without extension in ADT mode makes .adt") {
    auto dir = fresh_dir("openads_tim_item1_local");
    ADSHANDLE h = connect_local(dir);
    create_no_ext_adt(h, dir);
    AdsDisconnect(h);
}

TEST_CASE("tim item 2: REMOTE SQL CREATE TABLE [t2.adt] writes an ADT table") {
    auto dir = fresh_dir("openads_tim_item2_remote");
    openads::network::Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    ADSHANDLE h = connect_remote(dir, srv.port());
    sql_create_bracketed_adt(h, dir);
    AdsDisconnect(h);
}

TEST_CASE("tim item 2 control: LOCAL SQL CREATE TABLE [t2.adt] writes an ADT table") {
    auto dir = fresh_dir("openads_tim_item2_local");
    ADSHANDLE h = connect_local(dir);
    sql_create_bracketed_adt(h, dir);
    AdsDisconnect(h);
}

TEST_CASE("tim item 3: REMOTE ADT with memo, N(9,4) reads back what was written after commit and unlock") {
    auto dir = fresh_dir("openads_tim_item3_remote");
    openads::network::Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    ADSHANDLE h = connect_remote(dir, srv.port());

    auto name   = cbuf("ECLMST.adt");
    auto fields = cbuf("Id,Character,8;Valor,Numeric,9,4;Obs,Memo,9");
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(h, name.data(), nullptr, ADS_ADT, ADS_ANSI,
                           0, 0, 0, fields.data(), &hT) == 0);

    auto fValor = cbuf("Valor");
    int bad = 0;
    for (int i = 0; i < 300; ++i) {
        REQUIRE(AdsAppendRecord(hT) == 0);
        REQUIRE(AdsSetDouble(hT, fValor.data(), 0.0) == 0);
        REQUIRE(AdsSetDouble(hT, fValor.data(), 12.3456) == 0);
        REQUIRE(AdsWriteRecord(hT) == 0);
        UNSIGNED32 rn = 0;
        REQUIRE(AdsGetRecordNum(hT, ADS_IGNOREFILTERS, &rn) == 0);
        REQUIRE(AdsUnlockRecord(hT, rn) == 0);
        double v = -1.0;
        REQUIRE(AdsGetDouble(hT, fValor.data(), &v) == 0);
        if (std::fabs(v - 12.3456) > 1e-9) ++bad;
    }
    CHECK_MESSAGE(bad == 0, "stale reads out of 300: " << bad);
    AdsCloseTable(hT);
    AdsDisconnect(h);
}

// Others must not change: names without .adt and non-ADT creates keep their format.
TEST_CASE("tim item 1 and 2 guard: non-ADT creates are unchanged") {
    auto dir = fresh_dir("openads_tim_guard");
    ADSHANDLE h = connect_local(dir);
    {   // CDX create without extension still makes .dbf
        auto name = cbuf("plain1");
        auto fields = cbuf("Id,Numeric,4,0");
        ADSHANDLE ht = 0;
        REQUIRE(AdsCreateTable(h, name.data(), nullptr, ADS_CDX, ADS_ANSI, 0, 0, 0, fields.data(), &ht) == 0);
        AdsCloseTable(ht);
        CHECK(fs::exists(dir / "plain1.dbf"));
        CHECK_FALSE(fs::exists(dir / "plain1.adt"));
    }
    {   // SQL CREATE TABLE without .adt still makes .dbf
        ADSHANDLE hStmt = 0;
        REQUIRE(AdsCreateSQLStatement(h, &hStmt) == 0);
        auto sql = cbuf("CREATE TABLE plain2 (a NUMERIC(9,4), b CHAR(10))");
        ADSHANDLE hCur = 0;
        REQUIRE(AdsExecuteSQLDirect(hStmt, sql.data(), &hCur) == 0);
        if (hCur != 0) AdsCloseTable(hCur);
        AdsCloseSQLStatement(hStmt);
        CHECK(fs::exists(dir / "plain2.dbf"));
        CHECK_FALSE(fs::exists(dir / "plain2.adt"));
    }
    {   // SQL CREATE TABLE with an explicit CDX statement type and .adt name: caller's choice wins
        ADSHANDLE hStmt = 0;
        REQUIRE(AdsCreateSQLStatement(h, &hStmt) == 0);
        REQUIRE(AdsStmtSetTableType(hStmt, ADS_CDX) == 0);
        auto sql = cbuf("CREATE TABLE [explicit.dbf] (a NUMERIC(9,4))");
        ADSHANDLE hCur = 0;
        REQUIRE(AdsExecuteSQLDirect(hStmt, sql.data(), &hCur) == 0);
        if (hCur != 0) AdsCloseTable(hCur);
        AdsCloseSQLStatement(hStmt);
        CHECK(fs::exists(dir / "explicit.dbf"));
    }
    AdsDisconnect(h);
}
