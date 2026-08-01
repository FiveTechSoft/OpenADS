// abi_setorder_zero_test.cpp
//
// Pritpal Bedi 01/08/2026 — DbSetOrder(0) must restore natural-order
// navigation: after an index order was active, resetting to order 0 makes
// the next browse walk records in physical order, not key order.
// The patched FiveTech rddads (rddads.lib) forwards DbSetOrder(0) to
// AdsSetIndexOrderByHandle(hTable, 0) — which had no local branch (fell
// into "index not bound to table") and, remotely, sent NO wire frame so
// the server kept walking the old order ("regression in v1.8.43+").
//
// The fixture's NAME index order differs from physical order, so an
// ordered walk and a natural walk are distinguishable.

#include "doctest.h"
#include "openads/ace.h"
#include "network/server.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

// Physical order: zulu(1) alpha(2) mike(3) bravo(4) charlie(5).
// NAME index order: alpha(2) bravo(4) charlie(5) mike(3) zulu(1).
const char* kNatural[] = {"1", "2", "3", "4", "5"};
const char* kIndexed[] = {"2", "4", "5", "3", "1"};

void make_nat_dbf(const fs::path& dir) {
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    const auto sp = dir.string();
    std::vector<UNSIGNED8> srv(sp.begin(), sp.end());
    srv.push_back(0);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv.data(), ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    UNSIGNED8 def[]   = "NAME,C,10,0";
    UNSIGNED8 tname[] = "natord.dbf";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX,
                           0, 0, 0, 0, def, &hT) == 0);
    const char* names[] = {"zulu", "alpha", "mike", "bravo", "charlie"};
    UNSIGNED8 f[] = "NAME";
    for (const char* n : names) {
        REQUIRE(AdsAppendRecord(hT) == 0);
        REQUIRE(AdsSetString(hT, f, (UNSIGNED8*)n,
                             static_cast<UNSIGNED32>(std::strlen(n))) == 0);
        REQUIRE(AdsWriteRecord(hT) == 0);
    }
    UNSIGNED8 bag[]  = "natord.cdx";
    UNSIGNED8 tag[]  = "NAME";
    UNSIGNED8 expr[] = "NAME";
    ADSHANDLE hI = 0;
    REQUIRE(AdsCreateIndex61(hT, bag, tag, expr,
                             nullptr, nullptr, 0, 0, &hI) == 0);
    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
}

// Walk the table, returning recnos in visit order.
std::vector<std::string> walk_recnos(ADSHANDLE hT) {
    std::vector<std::string> out;
    REQUIRE(AdsGotoTop(hT) == 0);
    for (int guard = 0; guard < 20; ++guard) {
        UNSIGNED16 eof = 0;
        REQUIRE(AdsAtEOF(hT, &eof) == 0);
        if (eof) break;
        UNSIGNED32 rn = 0;
        REQUIRE(AdsGetRecordNum(hT, 0, &rn) == 0);
        out.push_back(std::to_string(rn));
        REQUIRE(AdsSkip(hT, 1) == 0);
    }
    return out;
}

void exercise_setorder_zero(ADSHANDLE hConn) {
    ADSHANDLE hT = 0;
    UNSIGNED8 tname[] = "natord.dbf";
    REQUIRE(AdsOpenTable(hConn, tname, nullptr, ADS_CDX,
                         0, 0, 0, 0, &hT) == 0);
    ADSHANDLE hOrd = 0;
    REQUIRE(AdsGetIndexHandleByOrder(hT, 1, &hOrd) == 0);
    REQUIRE(hOrd != 0);

    // Activate the order via index-handle navigation (the rddads pattern).
    REQUIRE(AdsGotoTop(hOrd) == 0);
    CHECK(walk_recnos(hT) == std::vector<std::string>(
        std::begin(kIndexed), std::end(kIndexed)));

    // DbSetOrder(0) -> AdsSetIndexOrderByHandle(hTable, 0): natural again.
    REQUIRE(AdsSetIndexOrderByHandle(hT, 0) == 0);
    CHECK(walk_recnos(hT) == std::vector<std::string>(
        std::begin(kNatural), std::end(kNatural)));

    // And back to the index order through the explicit API.
    REQUIRE(AdsSetIndexOrderByHandle(hT, hOrd) == 0);
    CHECK(walk_recnos(hT) == std::vector<std::string>(
        std::begin(kIndexed), std::end(kIndexed)));

    // AdsSetIndexOrder(hTable, "") must restore natural order too.
    REQUIRE(AdsSetIndexOrder(hT, nullptr) == 0);
    CHECK(walk_recnos(hT) == std::vector<std::string>(
        std::begin(kNatural), std::end(kNatural)));

    REQUIRE(AdsCloseTable(hT) == 0);
}

} // namespace

TEST_CASE("DbSetOrder(0) restores natural order (local)") {
    auto dir = fs::temp_directory_path() / "openads_setorder_zero";
    make_nat_dbf(dir);
    const auto sp = dir.string();
    std::vector<UNSIGNED8> srv(sp.begin(), sp.end());
    srv.push_back(0);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv.data(), ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    exercise_setorder_zero(hConn);
    REQUIRE(AdsDisconnect(hConn) == 0);
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST_CASE("DbSetOrder(0) restores natural order (remote)") {
    using openads::network::Server;
    auto dir = fs::temp_directory_path() / "openads_setorder_zero";
    make_nat_dbf(dir);

    Server server;
    REQUIRE(server.start("127.0.0.1", 0).has_value());
    const std::uint16_t port = server.port();
    std::string uri = "tcp://127.0.0.1:" + std::to_string(port) + "/" +
                      dir.generic_string();
    std::vector<UNSIGNED8> ruri(uri.begin(), uri.end());
    ruri.push_back(0);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(ruri.data(), ADS_REMOTE_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    exercise_setorder_zero(hConn);
    REQUIRE(AdsDisconnect(hConn) == 0);
    server.stop();
    std::error_code ec;
    fs::remove_all(dir, ec);
}
