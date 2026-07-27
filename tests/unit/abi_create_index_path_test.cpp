// Repro for Pritpal's "can't create index" report.
// Tests AdsCreateIndex61 with various path forms: with/without extension,
// with/without directory, absolute/relative, backslash/forward slash.
#include "doctest.h"
#include "openads/ace.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

namespace {

fs::path stage_dbf(const fs::path& dir, const char* name, int nrecs) {
    std::error_code ec;
    fs::create_directories(dir);
    auto p = dir / name;
    fs::remove(p, ec);
    std::vector<std::uint8_t> file;
    std::array<std::uint8_t, 32> hdr{};
    hdr[0] = 0x03;
    hdr[4] = static_cast<std::uint8_t>(nrecs);
    hdr[8] = 32 + 32 + 1;
    hdr[10] = 1 + 5;
    file.insert(file.end(), hdr.begin(), hdr.end());
    std::array<std::uint8_t, 32> fd{};
    std::strncpy(reinterpret_cast<char*>(fd.data()), "NAME", 11);
    fd[11] = 'C'; fd[16] = 5;
    file.insert(file.end(), fd.begin(), fd.end());
    file.push_back(0x0D);
    for (int r = 0; r < nrecs; ++r) {
        file.push_back(' ');
        char val[5] = {'A', '0', '0', static_cast<char>('0' + r), '\0'};
        for (int i = 0; i < 5; ++i) file.push_back(val[i]);
    }
    file.push_back(0x1A);
    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
    return p;
}

ADSHANDLE connect_local(const fs::path& dir) {
    UNSIGNED8 srv[512];
    const auto s = dir.string();
    std::memcpy(srv, s.c_str(), s.size() + 1);
    ADSHANDLE h = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &h) == 0);
    return h;
}

}  // namespace

// ===========================================================================
// Path form: bag name only (no extension) → should auto-add .cdx
// ===========================================================================
TEST_CASE("AdsCreateIndex61: bag name without extension → auto .cdx") {
    auto dir = fs::temp_directory_path() / "openads_ci_path1";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_dbf(dir, "T1.DBF", 5);

    auto hConn = connect_local(dir);
    UNSIGNED8 name[8] = "T1";
    ADSHANDLE hTbl = 0;
    REQUIRE(AdsOpenTable(hConn, name, name, ADS_CDX, 1, 1, 0, 1, &hTbl) == 0);

    UNSIGNED8 bag[16]  = "MYIDX";       // no extension
    UNSIGNED8 tag[16]  = "BYNAME";
    UNSIGNED8 expr[16] = "NAME";
    ADSHANDLE hIdx = 0;
    UNSIGNED32 rc = AdsCreateIndex61(hTbl, bag, tag, expr,
                                     nullptr, nullptr, ADS_COMPOUND, 512, &hIdx);
    CHECK(rc == 0);
    if (rc != 0) {
        UNSIGNED8 msg[256] = {};
        UNSIGNED16 msglen = sizeof(msg);
        AdsGetLastError(&rc, msg, &msglen);
        INFO("Error: ", reinterpret_cast<const char*>(msg));
    }

    // Verify the CDX file was created.
    CHECK(fs::exists(dir / "MYIDX.CDX"));

    // Verify the tag works.
    if (hIdx != 0) {
        REQUIRE(AdsGotoTop(hTbl) == 0);
        UNSIGNED32 recno = 0;
        REQUIRE(AdsGetRecordNum(hTbl, 0, &recno) == 0);
        CHECK(recno == 1);  // AAAA (record 1) should be first
    }

    if (hIdx) AdsCloseIndex(hIdx);
    REQUIRE(AdsCloseTable(hTbl) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

// ===========================================================================
// Path form: bag name with .cdx extension
// ===========================================================================
TEST_CASE("AdsCreateIndex61: bag name with .cdx extension") {
    auto dir = fs::temp_directory_path() / "openads_ci_path2";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_dbf(dir, "T2.DBF", 5);

    auto hConn = connect_local(dir);
    UNSIGNED8 name[8] = "T2";
    ADSHANDLE hTbl = 0;
    REQUIRE(AdsOpenTable(hConn, name, name, ADS_CDX, 1, 1, 0, 1, &hTbl) == 0);

    UNSIGNED8 bag[16]  = "MYIDX.CDX";
    UNSIGNED8 tag[16]  = "BYNAME";
    UNSIGNED8 expr[16] = "NAME";
    ADSHANDLE hIdx = 0;
    UNSIGNED32 rc = AdsCreateIndex61(hTbl, bag, tag, expr,
                                     nullptr, nullptr, ADS_COMPOUND, 512, &hIdx);
    CHECK(rc == 0);
    if (rc != 0) {
        UNSIGNED8 msg[256] = {};
        UNSIGNED16 msglen = sizeof(msg);
        AdsGetLastError(&rc, msg, &msglen);
        INFO("Error: ", reinterpret_cast<const char*>(msg));
    }
    CHECK(fs::exists(dir / "MYIDX.CDX"));

    if (hIdx) AdsCloseIndex(hIdx);
    REQUIRE(AdsCloseTable(hTbl) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

// ===========================================================================
// Path form: absolute path with drive letter (Windows-style)
// ===========================================================================
TEST_CASE("AdsCreateIndex61: absolute path with drive letter") {
    auto dir = fs::temp_directory_path() / "openads_ci_path3";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_dbf(dir, "T3.DBF", 5);

    auto hConn = connect_local(dir);
    UNSIGNED8 name[8] = "T3";
    ADSHANDLE hTbl = 0;
    REQUIRE(AdsOpenTable(hConn, name, name, ADS_CDX, 1, 1, 0, 1, &hTbl) == 0);

    // Build an absolute path with drive letter.
    auto abs_bag = (dir / "MYIDX.CDX").string();
    std::vector<UNSIGNED8> bag(abs_bag.begin(), abs_bag.end());
    bag.push_back(0);

    UNSIGNED8 tag[16]  = "BYNAME";
    UNSIGNED8 expr[16] = "NAME";
    ADSHANDLE hIdx = 0;
    UNSIGNED32 rc = AdsCreateIndex61(hTbl, bag.data(), tag, expr,
                                     nullptr, nullptr, ADS_COMPOUND, 512, &hIdx);
    CHECK(rc == 0);
    if (rc != 0) {
        UNSIGNED8 msg[256] = {};
        UNSIGNED16 msglen = sizeof(msg);
        AdsGetLastError(&rc, msg, &msglen);
        INFO("Error: ", reinterpret_cast<const char*>(msg));
    }
    CHECK(fs::exists(dir / "MYIDX.CDX"));

    if (hIdx) AdsCloseIndex(hIdx);
    REQUIRE(AdsCloseTable(hTbl) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

// ===========================================================================
// Path form: empty bag name → structural (table-stem .cdx)
// ===========================================================================
TEST_CASE("AdsCreateIndex61: empty bag name → structural CDX") {
    auto dir = fs::temp_directory_path() / "openads_ci_path4";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_dbf(dir, "T4.DBF", 5);

    auto hConn = connect_local(dir);
    UNSIGNED8 name[8] = "T4";
    ADSHANDLE hTbl = 0;
    REQUIRE(AdsOpenTable(hConn, name, name, ADS_CDX, 1, 1, 0, 1, &hTbl) == 0);

    UNSIGNED8 bag[4]  = "";             // empty → T4.CDX
    UNSIGNED8 tag[16] = "BYNAME";
    UNSIGNED8 expr[16] = "NAME";
    ADSHANDLE hIdx = 0;
    UNSIGNED32 rc = AdsCreateIndex61(hTbl, bag, tag, expr,
                                     nullptr, nullptr, ADS_COMPOUND, 512, &hIdx);
    CHECK(rc == 0);
    if (rc != 0) {
        UNSIGNED8 msg[256] = {};
        UNSIGNED16 msglen = sizeof(msg);
        AdsGetLastError(&rc, msg, &msglen);
        INFO("Error: ", reinterpret_cast<const char*>(msg));
    }
    CHECK(fs::exists(dir / "T4.CDX"));

    if (hIdx) AdsCloseIndex(hIdx);
    REQUIRE(AdsCloseTable(hTbl) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

// ===========================================================================
// Path form: bag name with subdirectory
// ===========================================================================
TEST_CASE("AdsCreateIndex61: bag in subdirectory") {
    auto dir = fs::temp_directory_path() / "openads_ci_path5";
    auto subdir = dir / "indexes";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_dbf(dir, "T5.DBF", 5);

    auto hConn = connect_local(dir);
    UNSIGNED8 name[8] = "T5";
    ADSHANDLE hTbl = 0;
    REQUIRE(AdsOpenTable(hConn, name, name, ADS_CDX, 1, 1, 0, 1, &hTbl) == 0);

    fs::create_directories(subdir);
    UNSIGNED8 bag[32] = "indexes/MYIDX.CDX";
    UNSIGNED8 tag[16] = "BYNAME";
    UNSIGNED8 expr[16] = "NAME";
    ADSHANDLE hIdx = 0;
    UNSIGNED32 rc = AdsCreateIndex61(hTbl, bag, tag, expr,
                                     nullptr, nullptr, ADS_COMPOUND, 512, &hIdx);
    CHECK(rc == 0);
    if (rc != 0) {
        UNSIGNED8 msg[256] = {};
        UNSIGNED16 msglen = sizeof(msg);
        AdsGetLastError(&rc, msg, &msglen);
        INFO("Error: ", reinterpret_cast<const char*>(msg));
    }
    CHECK(fs::exists(subdir / "MYIDX.CDX"));

    if (hIdx) AdsCloseIndex(hIdx);
    REQUIRE(AdsCloseTable(hTbl) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

// ===========================================================================
// Path form: backslash separators (Windows style)
// ===========================================================================
TEST_CASE("AdsCreateIndex61: backslash path") {
    auto dir = fs::temp_directory_path() / "openads_ci_path6";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_dbf(dir, "T6.DBF", 5);

    auto hConn = connect_local(dir);
    UNSIGNED8 name[8] = "T6";
    ADSHANDLE hTbl = 0;
    REQUIRE(AdsOpenTable(hConn, name, name, ADS_CDX, 1, 1, 0, 1, &hTbl) == 0);

    // Backslash in bag name
    auto bs_bag = (dir / "MYIDX.CDX").string();
    std::replace(bs_bag.begin(), bs_bag.end(), '/', '\\');
    std::vector<UNSIGNED8> bag(bs_bag.begin(), bs_bag.end());
    bag.push_back(0);

    UNSIGNED8 tag[16] = "BYNAME";
    UNSIGNED8 expr[16] = "NAME";
    ADSHANDLE hIdx = 0;
    UNSIGNED32 rc = AdsCreateIndex61(hTbl, bag.data(), tag, expr,
                                     nullptr, nullptr, ADS_COMPOUND, 512, &hIdx);
    CHECK(rc == 0);
    if (rc != 0) {
        UNSIGNED8 msg[256] = {};
        UNSIGNED16 msglen = sizeof(msg);
        AdsGetLastError(&rc, msg, &msglen);
        INFO("Error: ", reinterpret_cast<const char*>(msg));
    }

    if (hIdx) AdsCloseIndex(hIdx);
    REQUIRE(AdsCloseTable(hTbl) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

// ===========================================================================
// Path form: legacy AdsCreateIndex (not 61) with various path forms
// ===========================================================================
TEST_CASE("AdsCreateIndex (legacy): bag name without extension") {
    auto dir = fs::temp_directory_path() / "openads_ci_path7";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_dbf(dir, "T7.DBF", 5);

    auto hConn = connect_local(dir);
    UNSIGNED8 name[8] = "T7";
    ADSHANDLE hTbl = 0;
    REQUIRE(AdsOpenTable(hConn, name, name, ADS_CDX, 1, 1, 0, 1, &hTbl) == 0);

    UNSIGNED8 bag[16]  = "MYIDX";       // no extension
    UNSIGNED8 tag[16]  = "BYNAME";
    UNSIGNED8 expr[16] = "NAME";
    ADSHANDLE hIdx = 0;
    UNSIGNED32 rc = AdsCreateIndex(hTbl, bag, tag, expr,
                                    nullptr, ADS_COMPOUND, 0, &hIdx);
    CHECK(rc == 0);
    if (rc != 0) {
        UNSIGNED8 msg[256] = {};
        UNSIGNED16 msglen = sizeof(msg);
        AdsGetLastError(&rc, msg, &msglen);
        INFO("Error: ", reinterpret_cast<const char*>(msg));
    }
    CHECK(fs::exists(dir / "MYIDX.CDX"));

    if (hIdx) AdsCloseIndex(hIdx);
    REQUIRE(AdsCloseTable(hTbl) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

// ===========================================================================
// Verify AdsOpenIndex works after AdsCreateIndex61
// ===========================================================================
TEST_CASE("AdsCreateIndex61 + AdsOpenIndex round-trip") {
    auto dir = fs::temp_directory_path() / "openads_ci_roundtrip";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_dbf(dir, "RT.DBF", 5);

    auto hConn = connect_local(dir);
    UNSIGNED8 name[8] = "RT";
    ADSHANDLE hTbl = 0;
    REQUIRE(AdsOpenTable(hConn, name, name, ADS_CDX, 1, 1, 0, 1, &hTbl) == 0);

    UNSIGNED8 bag[16]  = "RT_CDX";
    UNSIGNED8 tag[16]  = "BYNAME";
    UNSIGNED8 expr[16] = "NAME";
    ADSHANDLE hIdx = 0;
    REQUIRE(AdsCreateIndex61(hTbl, bag, tag, expr,
                             nullptr, nullptr, ADS_COMPOUND, 512, &hIdx) == 0);
    if (hIdx) AdsCloseIndex(hIdx);

    // Now open it again via AdsOpenIndex.
    auto idx_path = (dir / "RT_CDX.CDX").string();
    std::vector<UNSIGNED8> idx_buf(idx_path.begin(), idx_path.end());
    idx_buf.push_back(0);
    UNSIGNED16 arr_len = 16;
    std::array<ADSHANDLE, 16> arr{};
    UNSIGNED32 rc = AdsOpenIndex(hTbl, idx_buf.data(), arr.data(), &arr_len);
    CHECK(rc == 0);
    CHECK(arr_len >= 1);

    if (rc == 0 && arr_len > 0 && arr[0]) {
        AdsGotoTop(arr[0]);
    }

    REQUIRE(AdsCloseTable(hTbl) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}
