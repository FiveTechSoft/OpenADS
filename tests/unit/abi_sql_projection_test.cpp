#include "doctest.h"
#include "openads/ace.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

fs::path stage_dbf(const fs::path& dir) {
    fs::create_directories(dir);
    auto p = dir / "data.dbf";
    fs::remove(p);
    std::vector<std::uint8_t> file;
    auto push = [&](const void* d, std::size_t n) {
        const auto* b = static_cast<const std::uint8_t*>(d);
        file.insert(file.end(), b, b + n);
    };
    std::array<std::uint8_t, 32> hdr{};
    hdr[0]  = 0x03;
    hdr[4]  = 1;
    hdr[8]  = 32 + 32 + 32 + 32 + 1;
    hdr[10] = 1 + 6 + 4 + 4;
    push(hdr.data(), hdr.size());
    auto add_field = [&](const char* name, char type, std::uint8_t len) {
        std::array<std::uint8_t, 32> fd{};
        std::strncpy(reinterpret_cast<char*>(fd.data()), name, 11);
        fd[11] = static_cast<std::uint8_t>(type); fd[16] = len;
        push(fd.data(), fd.size());
    };
    add_field("NAME", 'C', 6);
    add_field("AGE",  'N', 4);
    add_field("CITY", 'C', 4);
    file.push_back(0x0D);
    file.push_back(' ');
    auto put = [&](const char* s, int n) {
        for (int i = 0; i < n; ++i)
            file.push_back(i < (int)std::strlen(s)
                           ? static_cast<std::uint8_t>(s[i]) : ' ');
    };
    put("Alice",  6);
    put("  30",   4);
    put("BCN",    4);
    file.push_back(0x1A);
    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
    return p;
}

}  // namespace

TEST_CASE("M10.8 projection list reduces visible field count") {
    auto dir = fs::temp_directory_path() / "openads_m10_8_proj";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_dbf(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);

    UNSIGNED8 sql[160] = "SELECT NAME, AGE FROM data.dbf";
    ADSHANDLE hCur = 0;
    REQUIRE(AdsExecuteSQLDirect(hStmt, sql, &hCur) == 0);

    UNSIGNED16 nf = 0;
    REQUIRE(AdsGetNumFields(hCur, &nf) == 0);
    CHECK(nf == 2);

    UNSIGNED8 fname[16] = {0};
    UNSIGNED16 fnlen = sizeof(fname);
    REQUIRE(AdsGetFieldName(hCur, 1, fname, &fnlen) == 0);
    CHECK(std::string(reinterpret_cast<const char*>(fname), fnlen) == "NAME");
    fnlen = sizeof(fname); std::memset(fname, 0, sizeof(fname));
    REQUIRE(AdsGetFieldName(hCur, 2, fname, &fnlen) == 0);
    CHECK(std::string(reinterpret_cast<const char*>(fname), fnlen) == "AGE");

    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("M10.8 projection re-orders columns") {
    auto dir = fs::temp_directory_path() / "openads_m10_8_proj_reorder";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_dbf(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);

    // CITY, NAME — original order is NAME, AGE, CITY. Projection
    // re-orders them, with AGE excluded.
    UNSIGNED8 sql[160] = "SELECT CITY, NAME FROM data.dbf";
    ADSHANDLE hCur = 0;
    REQUIRE(AdsExecuteSQLDirect(hStmt, sql, &hCur) == 0);

    UNSIGNED16 nf = 0;
    REQUIRE(AdsGetNumFields(hCur, &nf) == 0);
    CHECK(nf == 2);

    // ADSFIELD(1) under projection = CITY; ADSFIELD(2) = NAME.
    REQUIRE(AdsGotoTop(hCur) == 0);
    UNSIGNED8* fld1 = reinterpret_cast<UNSIGNED8*>(static_cast<std::uintptr_t>(1));
    UNSIGNED8 buf[16] = {0};
    UNSIGNED32 cap = sizeof(buf);
    REQUIRE(AdsGetField(hCur, fld1, buf, &cap, 0) == 0);
    auto v = std::string(reinterpret_cast<const char*>(buf), cap);
    while (!v.empty() && v.back() == ' ') v.pop_back();
    CHECK(v == "BCN");

    UNSIGNED8* fld2 = reinterpret_cast<UNSIGNED8*>(static_cast<std::uintptr_t>(2));
    cap = sizeof(buf); std::memset(buf, 0, sizeof(buf));
    REQUIRE(AdsGetField(hCur, fld2, buf, &cap, 0) == 0);
    auto v2 = std::string(reinterpret_cast<const char*>(buf), cap);
    while (!v2.empty() && v2.back() == ' ') v2.pop_back();
    CHECK(v2 == "Alice");

    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("M10.8 SELECT * reports the full schema (no projection)") {
    auto dir = fs::temp_directory_path() / "openads_m10_8_star";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_dbf(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);

    UNSIGNED8 sql[80] = "SELECT * FROM data.dbf";
    ADSHANDLE hCur = 0;
    REQUIRE(AdsExecuteSQLDirect(hStmt, sql, &hCur) == 0);
    UNSIGNED16 nf = 0;
    REQUIRE(AdsGetNumFields(hCur, &nf) == 0);
    CHECK(nf == 3);   // NAME + AGE + CITY all visible

    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

// S4 — SAP names a result column after the SELECT LIST, not after the
// table's declared spelling. Probed against ace64.dll on the mp corpus:
//
//   SELECT CLAIMKEY FROM service            -> CLAIMKEY  (declared claimkey)
//   SELECT s.CLAIMKEY AS ck ...             -> ck
//   SELECT s.ADM_NUM, l.ADM_NUM, s.adm_num  -> ADM_NUM, ADM_NUM_1, adm_num_2
//
// Note the third: collisions are detected case-insensitively but each item
// keeps ITS OWN case, so the suffixed name is not a copy of the first.
// SELECT * keeps the declared spelling.
namespace {

std::string field_name_of(ADSHANDLE hCur, UNSIGNED16 n) {
    UNSIGNED8 buf[128] = {0};
    UNSIGNED16 len = sizeof(buf);
    if (AdsGetFieldName(hCur, n, buf, &len) != 0) return "<err>";
    return std::string(reinterpret_cast<const char*>(buf));
}

}  // namespace

TEST_CASE("S4 result columns are named from the SELECT list") {
    auto dir = fs::temp_directory_path() / "openads_s4_colnames";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_dbf(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);

    auto run = [&](const char* text) {
        UNSIGNED8 sql[160] = {0};
        std::memcpy(sql, text, std::strlen(text));
        ADSHANDLE hCur = 0;
        REQUIRE(AdsExecuteSQLDirect(hStmt, sql, &hCur) == 0);
        return hCur;
    };

    SUBCASE("written spelling wins over the declared one") {
        // Declared NAME / AGE; asked for in the opposite case.
        ADSHANDLE hCur = run("SELECT name, Age FROM data.dbf");
        CHECK(field_name_of(hCur, 1) == "name");
        CHECK(field_name_of(hCur, 2) == "Age");
    }

    SUBCASE("an explicit AS alias wins outright, and is addressable") {
        ADSHANDLE hCur = run("SELECT NAME AS who FROM data.dbf");
        CHECK(field_name_of(hCur, 1) == "who");
        // The alias is not a name the underlying table carries, so a
        // by-name read has to go through the cursor's own names.
        REQUIRE(AdsGotoTop(hCur) == 0);
        UNSIGNED8 want[8] = "who";
        UNSIGNED8 val[32] = {0};
        UNSIGNED32 vlen = sizeof(val);
        REQUIRE(AdsGetField(hCur, want, val, &vlen, ADS_NONE) == 0);
        CHECK(std::string(reinterpret_cast<const char*>(val)) == "Alice ");
    }

    SUBCASE("repeats are suffixed _1, _2 keeping each item's own case") {
        ADSHANDLE hCur = run("SELECT NAME, name, NaMe FROM data.dbf");
        CHECK(field_name_of(hCur, 1) == "NAME");
        CHECK(field_name_of(hCur, 2) == "name_1");
        CHECK(field_name_of(hCur, 3) == "NaMe_2");
    }

    SUBCASE("SELECT * keeps the declared spelling") {
        ADSHANDLE hCur = run("SELECT * FROM data.dbf");
        CHECK(field_name_of(hCur, 1) == "NAME");
        CHECK(field_name_of(hCur, 2) == "AGE");
        CHECK(field_name_of(hCur, 3) == "CITY");
    }

    SUBCASE("naming survives a materialised cursor (TOP / ORDER BY)") {
        // A materialised temp carries its own descriptors, so the live
        // cursor's alias map cannot reach it - the names must be baked
        // into the temp's DDL. This subcase is the one that regressed.
        ADSHANDLE hCur = run("SELECT TOP 1 name AS who, Age FROM data.dbf");
        CHECK(field_name_of(hCur, 1) == "who");
        CHECK(field_name_of(hCur, 2) == "Age");

        ADSHANDLE hOrd = run("SELECT name FROM data.dbf ORDER BY name");
        CHECK(field_name_of(hOrd, 1) == "name");
    }

    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}
