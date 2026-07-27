#include "doctest.h"
#include "abi/charset.h"
#include "engine/data_dict.h"
#include "openads/ace.h"
#include "openads/error.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

fs::path make_dbf(const fs::path& dir, const char* leaf) {
    fs::create_directories(dir);
    auto p = dir / leaf;
    fs::remove(p);
    std::vector<std::uint8_t> file;
    std::array<std::uint8_t, 32> hdr{};
    hdr[0]  = 0x03;
    hdr[4]  = 2;
    hdr[8]  = 32 + 32 + 1; hdr[9]  = 0;
    hdr[10] = 1 + 4;       hdr[11] = 0;
    file.insert(file.end(), hdr.begin(), hdr.end());
    std::array<std::uint8_t, 32> fd{};
    std::strncpy(reinterpret_cast<char*>(fd.data()), "TAG", 11);
    fd[11] = 'C'; fd[16] = 4;
    file.insert(file.end(), fd.begin(), fd.end());
    file.push_back(0x0D);
    auto push = [&](const char* k){
        file.push_back(' ');
        for (int i = 0; i < 4; ++i)
            file.push_back(i < (int)std::strlen(k)
                           ? static_cast<std::uint8_t>(k[i]) : ' ');
    };
    push("FOO");
    push("BAR");
    file.push_back(0x1A);
    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
    return p;
}

} // namespace

TEST_CASE("ABI SQL smoke: AdsExecuteSQLDirect on SELECT * FROM <table>") {
    const auto dir = fs::temp_directory_path() / "openads_m71_sql";
    std::error_code ec;
    fs::remove_all(dir, ec);
    make_dbf(dir, "data.dbf");

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);

    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);

    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);

    ADSHANDLE hCursor = 0;
    UNSIGNED8 sql[128] = "SELECT * FROM data.dbf";
    REQUIRE(AdsExecuteSQLDirect(hStmt, sql, &hCursor) == 0);

    UNSIGNED32 count = 0;
    REQUIRE(AdsGetRecordCount(hCursor, 0, &count) == 0);
    CHECK(count == 2);

    REQUIRE(AdsGotoTop(hCursor) == 0);
    UNSIGNED8 fld[16] = "TAG";
    UNSIGNED8 buf[16] = {0};
    UNSIGNED32 cap = sizeof(buf);
    REQUIRE(AdsGetField(hCursor, fld, buf, &cap, 0) == 0);
    // TAG is C(4); AdsGetField must return the full padded width.
    CHECK(std::string(reinterpret_cast<const char*>(buf), cap) == "FOO ");

    REQUIRE(AdsSkip(hCursor, 1) == 0);
    cap = sizeof(buf); std::memset(buf, 0, sizeof(buf));
    REQUIRE(AdsGetField(hCursor, fld, buf, &cap, 0) == 0);
    CHECK(std::string(reinterpret_cast<const char*>(buf), cap) == "BAR ");

    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("ABI SQL: AdsPrepareSQL + AdsExecuteSQL") {
    const auto dir = fs::temp_directory_path() / "openads_m71_sql_prep";
    std::error_code ec;
    fs::remove_all(dir, ec);
    make_dbf(dir, "data.dbf");

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);
    UNSIGNED8 sql[64] = "SELECT * FROM data.dbf";
    REQUIRE(AdsPrepareSQL(hStmt, sql) == 0);
    ADSHANDLE hCursor = 0;
    REQUIRE(AdsExecuteSQL(hStmt, &hCursor) == 0);
    UNSIGNED32 count = 0;
    REQUIRE(AdsGetRecordCount(hCursor, 0, &count) == 0);
    CHECK(count == 2);
    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("ABI SQL: wide SQL wrappers execute and prepare SELECT") {
    const auto dir = fs::temp_directory_path() / "openads_sql_wide_wrappers";
    std::error_code ec;
    fs::remove_all(dir, ec);
    make_dbf(dir, "data.dbf");

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);

    auto sql_w = openads::abi::utf8_to_utf16le("SELECT * FROM data.dbf");
    sql_w.push_back(0);

    ADSHANDLE directStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &directStmt) == 0);
    ADSHANDLE directCursor = 0;
    REQUIRE(AdsExecuteSQLDirectW(directStmt, sql_w.data(), &directCursor) == 0);
    UNSIGNED32 count = 0;
    REQUIRE(AdsGetRecordCount(directCursor, 0, &count) == 0);
    CHECK(count == 2);
    REQUIRE(AdsCloseSQLStatement(directStmt) == 0);

    ADSHANDLE prepStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &prepStmt) == 0);
    REQUIRE(AdsPrepareSQLW(prepStmt, sql_w.data()) == 0);
    ADSHANDLE prepCursor = 0;
    REQUIRE(AdsExecuteSQL(prepStmt, &prepCursor) == 0);
    count = 0;
    REQUIRE(AdsGetRecordCount(prepCursor, 0, &count) == 0);
    CHECK(count == 2);
    REQUIRE(AdsCloseSQLStatement(prepStmt) == 0);

    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("ABI SQL: WHERE filter selects matching record") {
    const auto dir = fs::temp_directory_path() / "openads_m73_sql_where";
    std::error_code ec;
    fs::remove_all(dir, ec);
    make_dbf(dir, "data.dbf");

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);

    ADSHANDLE hCursor = 0;
    UNSIGNED8 sql[128] = "SELECT * FROM data.dbf WHERE TAG = 'BAR'";
    REQUIRE(AdsExecuteSQLDirect(hStmt, sql, &hCursor) == 0);

    REQUIRE(AdsGotoTop(hCursor) == 0);
    UNSIGNED8 fld[16] = "TAG";
    UNSIGNED8 buf[16] = {0};
    UNSIGNED32 cap = sizeof(buf);
    REQUIRE(AdsGetField(hCursor, fld, buf, &cap, 0) == 0);
    // TAG is C(4); AdsGetField must return the full padded width.
    CHECK(std::string(reinterpret_cast<const char*>(buf), cap) == "BAR ");

    // Skipping past the matching row should hit EOF (only one BAR row).
    REQUIRE(AdsSkip(hCursor, 1) == 0);
    UNSIGNED16 at_eof = 0;
    REQUIRE(AdsAtEOF(hCursor, &at_eof) == 0);
    CHECK(at_eof == 1);

    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("ABI SQL: parse error on unsupported syntax") {
    // Projection lists are now supported (M10.8); use truly malformed
    // SQL to exercise the parse-error path.
    const auto dir = fs::temp_directory_path() / "openads_m71_sql_err";
    std::error_code ec;
    fs::remove_all(dir, ec);
    make_dbf(dir, "data.dbf");

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);
    ADSHANDLE hCursor = 0;
    UNSIGNED8 sql[64] = "GRANT SELECT ON x";   // unrecognised verb
    auto r = AdsExecuteSQLDirect(hStmt, sql, &hCursor);
    CHECK(r == openads::AE_PARSE_ERROR);
    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

namespace {

void write_dd_smoke(const fs::path& p, const std::string& body) {
    auto res = openads::engine::DataDict::create(p.string());
    if (!res) return;
    auto dd = std::move(res).value();
    std::istringstream ss(body);
    std::string line;
    while (std::getline(ss, line)) {
        if (line.rfind("TABLE ", 0) == 0) {
            auto eq = line.find('=', 6);
            if (eq != std::string::npos) {
                std::string alias = line.substr(6, eq - 6);
                std::string tpath = line.substr(eq + 1);
                dd.add_table(alias, tpath);
            }
        } else if (line.rfind("USER ", 0) == 0) {
            dd.create_user(line.substr(5));
        }
    }
}

void make_dbf0_smoke(const fs::path& path) {
    std::vector<std::uint8_t> file;
    std::array<std::uint8_t, 32> hdr{};
    hdr[0] = 0x03;
    const std::uint16_t hl = 32 + 32 + 1, rl = 1 + 10;
    hdr[8]  = static_cast<std::uint8_t>( hl       & 0xFFu);
    hdr[9]  = static_cast<std::uint8_t>((hl >> 8) & 0xFFu);
    hdr[10] = static_cast<std::uint8_t>( rl       & 0xFFu);
    hdr[11] = static_cast<std::uint8_t>((rl >> 8) & 0xFFu);
    file.insert(file.end(), hdr.begin(), hdr.end());
    std::array<std::uint8_t, 32> fd{};
    std::memcpy(fd.data(), "NAME", 4);
    fd[11] = 'C'; fd[16] = 10;
    file.insert(file.end(), fd.begin(), fd.end());
    file.push_back(0x0D);
    file.push_back(0x1A);
    std::ofstream(path, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
}

int sql_count_smoke(ADSHANDLE hConn, const char* sql) {
    ADSHANDLE stmt = 0;
    if (AdsCreateSQLStatement(hConn, &stmt) != 0) return -1;
    std::vector<UNSIGNED8> buf(std::strlen(sql) + 1);
    std::memcpy(buf.data(), sql, buf.size());
    ADSHANDLE cur = 0;
    if (AdsExecuteSQLDirect(stmt, buf.data(), &cur) != 0) {
        AdsCloseSQLStatement(stmt);
        return -1;
    }
    UNSIGNED32 cnt = 0;
    AdsGetRecordCount(cur, 0, &cnt);
    AdsCloseSQLStatement(stmt);
    return static_cast<int>(cnt);
}

} // namespace

TEST_CASE("ABI SQL smoke: system.permissions zero-row for ungranted pair") {
    const auto dir = fs::temp_directory_path() / "openads_sql_smoke_sysperm";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    make_dbf0_smoke(dir / "a.dbf");
    make_dbf0_smoke(dir / "b.dbf");
    write_dd_smoke(dir / "test.add",
                   "TABLE A=a.dbf\n"
                   "TABLE B=b.dbf\n"
                   "USER u1\n");

    UNSIGNED8 addpath[512];
    const auto ap = (dir / "test.add").string();
    std::memcpy(addpath, ap.c_str(), ap.size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(addpath, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);

    CHECK(sql_count_smoke(hConn,
                          "SELECT * FROM system.permissions "
                          "WHERE GRANTEE = 'u1' AND Name = 'B'") == 1);

    AdsDisconnect(hConn);
    fs::remove_all(dir, ec);
}
