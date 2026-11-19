// tests/unit/abi_remote_recno_bound_test.cpp
// mtfix67: AdsGetRecordNum's reposition-bound recno is keyed on the
// PER-TABLE nav generation. Other-table navs and this table's own writes
// (SetField/FlushTable bump the conn-wide nav_seq but move no cursor)
// must not evict it; any cursor-moving op on THIS table must.
#include "doctest.h"
#include "network/client.h"
#include "network/server.h"
#include "openads/ace.h"
#include "util/log.h"

#include <atomic>
#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {

std::atomic<unsigned> g_recnum_frames{0};
std::atomic<unsigned> g_skip_frames{0};
std::atomic<unsigned> g_other_frames{0};

void count_recnum(const void* /*conn*/, std::uint8_t op, std::uint32_t /*tid*/,
                  std::size_t /*req_bytes*/, std::uint8_t /*rep_op*/,
                  std::size_t /*rep_bytes*/, long long /*us*/) {
    if (op == 0x4E) g_recnum_frames.fetch_add(1);   // GetRecordNum
    else if (op == 0x42) g_skip_frames.fetch_add(1); // Skip
    else g_other_frames.fetch_add(1);
}

} // namespace

TEST_CASE("mtfix67: recno_bound survives other-table navs and own writes, dies on own nav") {
    const bool old_logging = openads::util::logging_enabled();
    struct Restore { bool old; ~Restore() { openads::util::set_logging_enabled(old); } } restore{old_logging};
    openads::util::set_logging_enabled(true);
    auto dir = fs::temp_directory_path() / "openads_recnobound";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    // Seed two tables with 3 rows each, locally.
    UNSIGNED8 srv[512]{};
    std::memcpy(srv, dir.string().c_str(), dir.string().size());
    ADSHANDLE hC0 = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hC0)
            == AE_SUCCESS);
    for (const char* nm : {"RA.DBF", "RB.DBF"}) {
        UNSIGNED8 def[] = "ID,N,8,0;NM,C,10,0";
        UNSIGNED8 tn[16]{};
        std::memcpy(tn, nm, std::strlen(nm) + 1);
        ADSHANDLE hT0 = 0;
        REQUIRE(AdsCreateTable(hC0, tn, nullptr, ADS_CDX, 0, 0, 0, 0, def, &hT0)
                == AE_SUCCESS);
        for (int i = 0; i < 3; ++i) {
            REQUIRE(AdsAppendRecord(hT0) == AE_SUCCESS);
            REQUIRE(AdsSetField(hT0, (UNSIGNED8*)"ID", (UNSIGNED8*)"7", 1) == AE_SUCCESS);
            REQUIRE(AdsWriteRecord(hT0) == AE_SUCCESS);
        }
        REQUIRE(AdsCloseTable(hT0) == AE_SUCCESS);
    }
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

    ADSHANDLE hA = 0, hB = 0;
    REQUIRE(AdsOpenTable(hC, (UNSIGNED8*)"RA.DBF", nullptr, ADS_CDX, 0, 0, 0, 0, &hA)
            == AE_SUCCESS);
    REQUIRE(AdsOpenTable(hC, (UNSIGNED8*)"RB.DBF", nullptr, ADS_CDX, 0, 0, 0, 0, &hB)
            == AE_SUCCESS);

    openads::network::set_frame_trace_hook(&count_recnum);
    UNSIGNED32 rn = 0;

    // Position A on record 1 (nav ack binds the recno), then drop the row
    // cache with a locked write: old code paid a GetRecordNum RTT here
    // because SetField bumps the conn-wide nav_seq.
    REQUIRE(AdsGotoRecord(hA, 1) == AE_SUCCESS);
    REQUIRE(AdsGetRecordNum(hA, 0, &rn) == AE_SUCCESS);
    CHECK(rn == 1);

    REQUIRE(AdsLockRecord(hA, 0) == AE_SUCCESS);
    REQUIRE(AdsSetField(hA, (UNSIGNED8*)"NM", (UNSIGNED8*)"x", 1) == AE_SUCCESS);
    REQUIRE(AdsWriteRecord(hA) == AE_SUCCESS);
    REQUIRE(AdsUnlockTable(hA) == AE_SUCCESS);

    // First post-write query: if the opening nav was cache-served (no ack
    // ever certified the recno), this pays the one rebind trip - the same
    // trip the old code pays. Baseline AFTER certification.
    REQUIRE(AdsGetRecordNum(hA, 0, &rn) == AE_SUCCESS);
    CHECK(rn == 1);
    const unsigned base = g_recnum_frames.load();

    // WRITE EVICTION (pair-5 idx 41/74/85 class): a second full write
    // cycle must NOT cost another GetRecordNum trip; pre-fix it did,
    // because SetField/SetFields/FlushTable bump the conn-wide nav_seq.
    REQUIRE(AdsLockRecord(hA, 0) == AE_SUCCESS);
    REQUIRE(AdsSetField(hA, (UNSIGNED8*)"NM", (UNSIGNED8*)"x", 1) == AE_SUCCESS);
    REQUIRE(AdsWriteRecord(hA) == AE_SUCCESS);
    REQUIRE(AdsUnlockTable(hA) == AE_SUCCESS);
    REQUIRE(AdsGetRecordNum(hA, 0, &rn) == AE_SUCCESS);
    CHECK(rn == 1);
    CHECK(g_recnum_frames.load() == base);

    // CROSS-TABLE EVICTION (pair-5 idx 15/17 class): nav on B must not
    // evict A's bound recno.
    REQUIRE(AdsGotoTop(hB) == AE_SUCCESS);
    REQUIRE(AdsGotoRecord(hB, 2) == AE_SUCCESS);
    REQUIRE(AdsLockRecord(hA, 0) == AE_SUCCESS);
    REQUIRE(AdsSetField(hA, (UNSIGNED8*)"NM", (UNSIGNED8*)"y", 1) == AE_SUCCESS);
    REQUIRE(AdsWriteRecord(hA) == AE_SUCCESS);
    REQUIRE(AdsUnlockTable(hA) == AE_SUCCESS);
    REQUIRE(AdsGetRecordNum(hA, 0, &rn) == AE_SUCCESS);
    CHECK(rn == 1);
    CHECK(g_recnum_frames.load() == base);

    // SAME-TABLE INVALIDATION: a cursor op on A itself must force a fresh
    // wire answer and return the NEW position.
    REQUIRE(AdsGotoRecord(hA, 2) == AE_SUCCESS);
    REQUIRE(AdsLockRecord(hA, 0) == AE_SUCCESS);
    REQUIRE(AdsSetField(hA, (UNSIGNED8*)"NM", (UNSIGNED8*)"z", 1) == AE_SUCCESS);
    REQUIRE(AdsWriteRecord(hA) == AE_SUCCESS);
    REQUIRE(AdsUnlockTable(hA) == AE_SUCCESS);
    REQUIRE(AdsGetRecordNum(hA, 0, &rn) == AE_SUCCESS);
    CHECK(rn == 2);                                // value must be the NEW position
    CHECK(g_recnum_frames.load() <= base + 1u);    // at most one rebind trip

    openads::network::set_frame_trace_hook(nullptr);
    REQUIRE(AdsCloseTable(hA) == AE_SUCCESS);
    REQUIRE(AdsCloseTable(hB) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hC) == AE_SUCCESS);
    s.stop();
    fs::remove_all(dir, ec);
}
