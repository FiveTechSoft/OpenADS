// abi_remote_timscope_test.cpp
//
// Tim Stone 31/07/2026 — REMOTE scoped browse shows phantom rows.
// REMTEST.DBF (his file): 36 rows, order numbers 100011-100016, six rows
// each; group 100011 has 1 LIVE + 5 DELETED. With SET DELETED ON and a
// top/bottom scope of "100011" the browse must show exactly ONE row.
// His remotetest.log shows KeyCount=1 (scope is computed correctly), yet
// xBrowse renders the correct row, then a blank row, then a duplicate —
// "as if OpenADS REMOTE does not know how to cut off at only 1 record".
//
// This test replays the exact ACE call sequence rddads/xBrowse makes over
// the wire (open -> order -> scope -> GoTop -> Skip*) against his real
// REMTEST.DBF/REMTEST.CDX fixture, and cross-checks the same walk LOCAL.

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

const fs::path kFixtureDir = fs::path("C:/OpenADS/_remtest");

void stage_tim_fixture(const fs::path& dir) {
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    fs::copy_file(kFixtureDir / "REMTEST.DBF", dir / "REMTEST.DBF",
                  fs::copy_options::overwrite_existing, ec);
    REQUIRE(!ec);
    fs::copy_file(kFixtureDir / "REMTEST.CDX", dir / "REMTEST.CDX",
                  fs::copy_options::overwrite_existing, ec);
    REQUIRE(!ec);
}

// Walk the active order top-to-bottom over the given nav handle, returning
// the ORDER keys of every row served (and whether EOF behaved).
std::vector<std::string> walk_keys(ADSHANDLE hT, ADSHANDLE hNav) {
    std::vector<std::string> out;
    REQUIRE(AdsGotoTop(hNav) == 0);
    for (int guard = 0; guard < 100; ++guard) {
        UNSIGNED16 eof = 0;
        REQUIRE(AdsAtEOF(hT, &eof) == 0);
        if (eof) break;
        UNSIGNED8 f[] = "ORDER";
        UNSIGNED8 buf[16] = {0};
        UNSIGNED32 len = sizeof(buf) - 1;
        REQUIRE(AdsGetString(hT, f, buf, &len, ADS_NONE) == 0);
        out.emplace_back(reinterpret_cast<char*>(buf));
        REQUIRE(AdsSkip(hNav, 1) == 0);
    }
    return out;
}

ADSHANDLE open_and_scope(ADSHANDLE hConn) {
    ADSHANDLE hT = 0;
    UNSIGNED8 tname[] = "REMTEST.DBF";
    REQUIRE(AdsOpenTable(hConn, tname, nullptr, ADS_CDX,
                         0, 0, 0, 0, &hT) == 0);
    ADSHANDLE hOrd = 0;
    REQUIRE(AdsGetIndexHandleByOrder(hT, 1, &hOrd) == 0);
    REQUIRE(hOrd != 0);
    UNSIGNED8 key[] = "100011";
    REQUIRE(AdsSetScope(hOrd, ADS_TOP, key, 6, ADS_STRINGKEY) == 0);
    REQUIRE(AdsSetScope(hOrd, ADS_BOTTOM, key, 6, ADS_STRINGKEY) == 0);
    // SET DELETED ON
    REQUIRE(AdsShowDeleted(0) == 0);
    return hT;
}

} // namespace

TEST_CASE("Tim Stone REMOTE scoped browse: 1 live + 5 deleted shows exactly 1 row") {
    if (!fs::exists(kFixtureDir / "REMTEST.DBF")) {
        MESSAGE("fixture missing — skip");
        return;
    }
    auto dir = fs::temp_directory_path() / "openads_timscope";
    stage_tim_fixture(dir);

    // -- LOCAL reference walk -------------------------------------------
    const auto sp = dir.string();
    std::vector<UNSIGNED8> srv(sp.begin(), sp.end());
    srv.push_back(0);
    ADSHANDLE hLoc = 0;
    REQUIRE(AdsConnect60(srv.data(), ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hLoc) == 0);
    ADSHANDLE hLT = open_and_scope(hLoc);
    ADSHANDLE hLOrd = 0;
    REQUIRE(AdsGetIndexHandleByOrder(hLT, 1, &hLOrd) == 0);
    auto local_keys = walk_keys(hLT, hLOrd);
    UNSIGNED32 lkcount = 0;
    REQUIRE(AdsGetKeyCount(hLOrd, 0, &lkcount) == 0);
    AdsCloseTable(hLT);
    AdsDisconnect(hLoc);

    MESSAGE("local keys: ", local_keys.size(), " keycount: ", lkcount);
    CHECK(local_keys.size() == 1);
    CHECK(lkcount == 1);

    // -- REMOTE walk -----------------------------------------------------
    using openads::network::Server;
    Server server;
    REQUIRE(server.start("127.0.0.1", 0).has_value());
    const std::uint16_t port = server.port();

    std::string uri = "tcp://127.0.0.1:" + std::to_string(port) + "/" +
                      dir.generic_string();
    std::vector<UNSIGNED8> ruri(uri.begin(), uri.end());
    ruri.push_back(0);
    ADSHANDLE hRC = 0;
    REQUIRE(AdsConnect60(ruri.data(), ADS_REMOTE_SERVER,
                         nullptr, nullptr, 0, &hRC) == 0);
    ADSHANDLE hRT = open_and_scope(hRC);
    ADSHANDLE hROrd = 0;
    REQUIRE(AdsGetIndexHandleByOrder(hRT, 1, &hROrd) == 0);

    UNSIGNED32 rkcount = 0;
    REQUIRE(AdsGetKeyCount(hROrd, 0, &rkcount) == 0);
    auto remote_keys = walk_keys(hRT, hROrd);

    MESSAGE("remote keys: ", remote_keys.size(), " keycount: ", rkcount);
    for (const auto& k : remote_keys) MESSAGE("  key=[", k, "]");
    CHECK(rkcount == 1);
    CHECK(remote_keys == local_keys);

    // xBrowse scrollbar / KeyNo machinery must also be scope-relative
    // (this was the phantom-row bug: GoBottom reported KeyNo=36 — the
    // physical record count — instead of the scoped key count 1).
    auto keyno = [&](ADSHANDLE hOrd_) -> UNSIGNED32 {
        UNSIGNED32 kn = 0xFFFFFFFF;
        REQUIRE(AdsGetKeyNum(hOrd_, ADS_RESPECTSCOPES, &kn) == 0);
        return kn;
    };
    REQUIRE(AdsGotoTop(hROrd) == 0);
    CHECK(keyno(hROrd) == 1);
    REQUIRE(AdsGotoBottom(hROrd) == 0);
    CHECK(keyno(hROrd) == 1);          // was 36 (physical rec count)
    // Skip past the single scoped row: EOF, KeyNo 0, then Skip(-1) back.
    REQUIRE(AdsSkip(hROrd, 1) == 0);
    UNSIGNED16 eof = 0;
    REQUIRE(AdsAtEOF(hRT, &eof) == 0);
    CHECK(eof == 1);
    CHECK(keyno(hROrd) == 0);
    REQUIRE(AdsSkip(hROrd, -1) == 0);
    CHECK(keyno(hROrd) == 1);          // was 36
    // RelKeyPos within a 1-row scope: 0.0 at the only row.
    double pos = -1.0;
    REQUIRE(AdsGetRelKeyPos(hROrd, &pos) == 0);
    CHECK(pos == doctest::Approx(0.0));
    // SetRelKeyPos(1.0) must clamp to the last SCOPED key, not walk to
    // physical record 36 outside the scope.
    REQUIRE(AdsSetRelKeyPos(hROrd, 1.0) == 0);
    UNSIGNED32 rn = 0;
    REQUIRE(AdsGetRecordNum(hRT, 0, &rn) == 0);
    CHECK(rn == 1);
    CHECK(keyno(hROrd) == 1);

    // Phantom-duplicate root cause (v1.8.47): a backward skip at the
    // scope TOP must report BOF on the FIRST try — even right after
    // AdsRefreshRecord, which invalidates the client's row cache. The
    // server used to pack the current row at BOF (has_row=1), and the
    // client's boundary detection then needed a pristine row_valid_before
    // that RefreshRecord had just destroyed: the first skip(-1) at the
    // top answered Bof()=.F., xBrowse counted one extra row above, and
    // the single scoped record painted twice.
    REQUIRE(AdsGotoTop(hROrd) == 0);
    REQUIRE(AdsRefreshRecord(hRT) == 0);
    REQUIRE(AdsSkip(hROrd, -1) == 0);
    UNSIGNED16 bof = 0;
    REQUIRE(AdsAtBOF(hRT, &bof) == 0);
    CHECK(bof == 1);                     // was 0 on the first backward skip
    // From the top the downward walk serves the single row exactly once.
    REQUIRE(AdsGotoTop(hROrd) == 0);
    UNSIGNED16 eof2 = 0;
    REQUIRE(AdsAtEOF(hRT, &eof2) == 0);
    CHECK(eof2 == 0);
    CHECK(keyno(hROrd) == 1);
    REQUIRE(AdsSkip(hROrd, 1) == 0);
    REQUIRE(AdsAtEOF(hRT, &eof2) == 0);
    CHECK(eof2 == 1);

    REQUIRE(AdsCloseTable(hRT) == 0);
    REQUIRE(AdsDisconnect(hRC) == 0);
    server.stop();
    std::error_code ec;
    fs::remove_all(dir, ec);
}
