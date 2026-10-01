// tests/unit/network_local_answers_test.cpp
// Client-only frame cuts for the Vouch WAN startup (patch 1):
//   a) AdsGetTableType answered from the opened name's extension, and
//      AdsGetRecordLength remembered per connection for re-opens;
//   b) no GetKeyCount after an ordered GotoBottom the server certified
//      empty;
//   c) AdsGotoRecord(0) on a certified-empty table served locally;
//   d) AdsRefreshRecord right after a GotoRecord (nothing sent between)
//      served from the GotoRecord ack.
// Each case also checks the answers match what the wire would say, and
// that the wire is still used when the proof does not hold.
#include "doctest.h"

#include <chrono>
#include <thread>
#include "mgmt/mg_stats.h"
#include "network/server.h"
#include "openads/ace.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {

std::uint64_t la_op(std::uint8_t op) {
    return openads::mgmt::process_mg_stats()
        .op_timing[op]
        .count.load(std::memory_order_relaxed);
}

void la_make_table(ADSHANDLE hC, const char* name, const char* bagname,
                   int rows) {
    UNSIGNED8 def[] = "ID,N,8,0;NM,C,10,0";
    UNSIGNED8 tn[64]{};
    std::memcpy(tn, name, std::strlen(name));
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hC, tn, nullptr, ADS_CDX, 0, 0, 0, 0, def, &hT)
            == AE_SUCCESS);
    UNSIGNED8 f1[] = "ID";
    for (int i = 1; i <= rows; ++i) {
        REQUIRE(AdsAppendRecord(hT) == AE_SUCCESS);
        REQUIRE(AdsSetDouble(hT, f1, i * 10) == AE_SUCCESS);
        REQUIRE(AdsWriteRecord(hT) == AE_SUCCESS);
    }
    ADSHANDLE hI = 0;
    UNSIGNED8 bag[64]{};
    std::memcpy(bag, bagname, std::strlen(bagname));
    UNSIGNED8 tag[] = "BYID";
    UNSIGNED8 exp[] = "ID";
    REQUIRE(AdsCreateIndex61(hT, bag, tag, exp, nullptr, nullptr,
                             ADS_COMPOUND, 512, &hI) == AE_SUCCESS);
    REQUIRE(AdsCloseTable(hT) == AE_SUCCESS);
}

} // namespace

TEST_CASE("Remote local answers: empty tables, table type, refresh") {
    auto dir = fs::temp_directory_path() / "openads_localans";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[512]{};
    std::memcpy(srv, dir.string().c_str(), dir.string().size());
    ADSHANDLE hC0 = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hC0)
            == AE_SUCCESS);
    la_make_table(hC0, "LAEMPTY.DBF", "LAEMPTY.CDX", 0);
    la_make_table(hC0, "LAFULL.DBF", "LAFULL.CDX", 5);
    REQUIRE(AdsDisconnect(hC0) == AE_SUCCESS);

    openads::network::Server s;
    REQUIRE(s.start("127.0.0.1", 0).has_value());
    char uri[512]{};
    std::snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u/%s",
                  static_cast<unsigned>(s.port()), dir.string().c_str());
    UNSIGNED8 sb[512]{};
    std::memcpy(sb, uri, std::strlen(uri) + 1);
    ADSHANDLE hC = 0;
    REQUIRE(AdsConnect60(sb, ADS_REMOTE_SERVER, nullptr, nullptr, 0, &hC)
            == AE_SUCCESS);

    UNSIGNED8 want[] = "BYID";

    SUBCASE("empty ordered table: no GetKeyCount, local GotoRecord") {
        UNSIGNED8 tn[] = "LAEMPTY.DBF";
        ADSHANDLE hT = 0;
        REQUIRE(AdsOpenTable(hC, tn, nullptr, ADS_CDX, 0, 0, 0, 0, &hT)
                == AE_SUCCESS);
        ADSHANDLE hOrd = 0;
        REQUIRE(AdsGetIndexHandle(hT, want, &hOrd) == AE_SUCCESS);
        REQUIRE(AdsSetIndexOrderByHandle(hT, hOrd) == AE_SUCCESS);

        // rddads order: top first (installs the order), then bottom.
        REQUIRE(AdsGotoTop(hOrd) == AE_SUCCESS);
        const auto kc0 = la_op(0xB0);
        const auto gr0 = la_op(0x58);
        const auto tt0 = la_op(0x6A);
        REQUIRE(AdsGotoBottom(hOrd) == AE_SUCCESS);
        CHECK(la_op(0xB0) - kc0 == 0u);   // (b) no key count frame

        UNSIGNED16 b = 0, e = 0;
        REQUIRE(AdsAtBOF(hT, &b) == AE_SUCCESS);
        REQUIRE(AdsAtEOF(hT, &e) == AE_SUCCESS);
        CHECK(b == 1);
        CHECK(e == 1);

        REQUIRE(AdsGotoRecord(hT, 0) == AE_SUCCESS);   // (c)
        CHECK(la_op(0x58) - gr0 == 0u);
        b = 0; e = 0;
        REQUIRE(AdsAtBOF(hT, &b) == AE_SUCCESS);
        REQUIRE(AdsAtEOF(hT, &e) == AE_SUCCESS);
        CHECK(b == 1);
        CHECK(e == 1);

        UNSIGNED16 tt = 0;
        REQUIRE(AdsGetTableType(hT, &tt) == AE_SUCCESS);   // (a)
        CHECK(tt == ADS_CDX);
        CHECK(la_op(0x6A) - tt0 == 0u);

        // GO n>0 is not answered by these local answers (a peer may have
        // appended). The separate still-empty window (OPENADS_EMPTY_TTL_MS,
        // at most 2s) may answer it right after an empty certification, so
        // let that window lapse first.
        std::this_thread::sleep_for(std::chrono::milliseconds(2100));
        REQUIRE(AdsGotoRecord(hT, 1) == AE_SUCCESS);
        CHECK(la_op(0x58) - gr0 == 1u);

        // The wire still agrees: the explicit key count is 0.
        UNSIGNED32 kc = 99;
        REQUIRE(AdsGetKeyCount(hOrd, 0, &kc) == AE_SUCCESS);
        CHECK(kc == 0u);
        REQUIRE(AdsCloseTable(hT) == AE_SUCCESS);
    }

    SUBCASE("non-empty table: GO 0 and keycount still use the wire") {
        UNSIGNED8 tn[] = "LAFULL.DBF";
        ADSHANDLE hT = 0;
        REQUIRE(AdsOpenTable(hC, tn, nullptr, ADS_CDX, 0, 0, 0, 0, &hT)
                == AE_SUCCESS);
        ADSHANDLE hOrd = 0;
        REQUIRE(AdsGetIndexHandle(hT, want, &hOrd) == AE_SUCCESS);
        REQUIRE(AdsSetIndexOrderByHandle(hT, hOrd) == AE_SUCCESS);
        REQUIRE(AdsGotoTop(hOrd) == AE_SUCCESS);
        REQUIRE(AdsGotoBottom(hOrd) == AE_SUCCESS);
        // Non-empty order: the key count is the real one (5), however
        // it was obtained (wire or an existing piggyback).
        UNSIGNED32 kcf = 0;
        REQUIRE(AdsGetKeyCount(hOrd, 0, &kcf) == AE_SUCCESS);
        CHECK(kcf == 5u);
        const auto gr0 = la_op(0x58);
        REQUIRE(AdsGotoRecord(hT, 0) == AE_SUCCESS);
        CHECK(la_op(0x58) - gr0 == 1u);
        UNSIGNED16 e = 0;
        REQUIRE(AdsAtEOF(hT, &e) == AE_SUCCESS);
        CHECK(e == 1);
        REQUIRE(AdsCloseTable(hT) == AE_SUCCESS);
    }

    SUBCASE("refresh after goto is local; after a lock it is not") {
        UNSIGNED8 tn[] = "LAFULL.DBF";
        ADSHANDLE hT = 0;
        REQUIRE(AdsOpenTable(hC, tn, nullptr, ADS_CDX, 0, 0, 0, 0, &hT)
                == AE_SUCCESS);
        const auto rf0 = la_op(0x68);
        REQUIRE(AdsGotoRecord(hT, 3) == AE_SUCCESS);
        REQUIRE(AdsRefreshRecord(hT) == AE_SUCCESS);   // (d)
        CHECK(la_op(0x68) - rf0 == 0u);
        UNSIGNED32 rn = 0;
        REQUIRE(AdsGetRecordNum(hT, ADS_IGNOREFILTERS, &rn) == AE_SUCCESS);
        CHECK(rn == 3u);
        double v = 0;
        UNSIGNED8 f1[] = "ID";
        REQUIRE(AdsGetDouble(hT, f1, &v) == AE_SUCCESS);
        CHECK(v == doctest::Approx(30.0));

        // A second refresh has no fresh GotoRecord behind it: wire.
        REQUIRE(AdsRefreshRecord(hT) == AE_SUCCESS);
        CHECK(la_op(0x68) - rf0 == 1u);

        // GotoRecord, then a lock frame, then refresh: must re-read.
        REQUIRE(AdsGotoRecord(hT, 4) == AE_SUCCESS);
        REQUIRE(AdsLockRecord(hT, 4) == AE_SUCCESS);
        REQUIRE(AdsRefreshRecord(hT) == AE_SUCCESS);
        CHECK(la_op(0x68) - rf0 == 2u);
        REQUIRE(AdsUnlockRecord(hT, 4) == AE_SUCCESS);
        REQUIRE(AdsCloseTable(hT) == AE_SUCCESS);
    }

    SUBCASE("record length remembered across re-opens") {
        UNSIGNED8 tn[] = "LAFULL.DBF";
        const auto rl0 = la_op(0x6C);
        for (int i = 0; i < 2; ++i) {
            ADSHANDLE hT = 0;
            // Exclusive: never parked, so the second open is a real
            // re-open on a new handle.
            REQUIRE(AdsOpenTable(hC, tn, nullptr, ADS_CDX, 0, 0, 0,
                                 ADS_EXCLUSIVE, &hT) == AE_SUCCESS);
            UNSIGNED32 len = 0;
            REQUIRE(AdsGetRecordLength(hT, &len) == AE_SUCCESS);
            CHECK(len == 19u);   // 1 (delete flag) + 8 + 10
            REQUIRE(AdsCloseTable(hT) == AE_SUCCESS);
        }
        CHECK(la_op(0x6C) - rl0 <= 1u);
    }

    REQUIRE(AdsDisconnect(hC) == AE_SUCCESS);
    s.stop();
    fs::remove_all(dir, ec);
}
