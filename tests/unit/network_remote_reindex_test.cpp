// tests/unit/network_remote_reindex_test.cpp
// Remote AdsReindex must rebuild the table's bags. On the server the
// bags are bound on the ABI twin handle, not on the engine table, so a
// reindex of the engine table alone rebuilt nothing and still reported
// success (rddads ordListRebuild ignores the return code anyway).
#include "doctest.h"
#include "network/server.h"
#include "openads/ace.h"

#include <cstdio>
#include <cstring>
#include <chrono>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {

UNSIGNED32 rr_local_key_count(const fs::path& dir) {
    UNSIGNED8 srv[512]{};
    std::memcpy(srv, dir.string().c_str(), dir.string().size());
    ADSHANDLE hC = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hC)
            == AE_SUCCESS);
    UNSIGNED8 tn[] = "RR.DBF";
    ADSHANDLE hT = 0;
    REQUIRE(AdsOpenTable(hC, tn, nullptr, ADS_CDX, 0, 0, 0, 0, &hT)
            == AE_SUCCESS);
    UNSIGNED8 tag[] = "BYID";
    ADSHANDLE hI = 0;
    REQUIRE(AdsGetIndexHandle(hT, tag, &hI) == AE_SUCCESS);
    UNSIGNED32 n = 0;
    REQUIRE(AdsGetKeyCount(hI, ADS_IGNOREFILTERS, &n) == AE_SUCCESS);
    REQUIRE(AdsCloseTable(hT) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hC) == AE_SUCCESS);
    return n;
}

void rr_append(ADSHANDLE hT, double id) {
    UNSIGNED8 fld[] = "ID";
    REQUIRE(AdsAppendRecord(hT) == AE_SUCCESS);
    REQUIRE(AdsSetDouble(hT, fld, id) == AE_SUCCESS);
    REQUIRE(AdsWriteRecord(hT) == AE_SUCCESS);
}

} // namespace

TEST_CASE("Remote AdsReindex rebuilds the bag bound on the server twin") {
    auto dir = fs::temp_directory_path() / "openads_remote_reindex";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    // Stage a stale production bag locally: build it over 2 rows, save a
    // copy, add 2 more rows, then put the 2-key copy back.
    {
        UNSIGNED8 srv[512]{};
        std::memcpy(srv, dir.string().c_str(), dir.string().size());
        ADSHANDLE hC = 0;
        REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0,
                             &hC) == AE_SUCCESS);
        UNSIGNED8 def[] = "ID,N,8,0";
        UNSIGNED8 tn[] = "RR.DBF";
        ADSHANDLE hT = 0;
        REQUIRE(AdsCreateTable(hC, tn, nullptr, ADS_CDX, 0, 0, 0, 0, def,
                               &hT) == AE_SUCCESS);
        UNSIGNED8 bag[] = "RR.CDX";
        UNSIGNED8 tag[] = "BYID";
        UNSIGNED8 exp[] = "STR(ID,8)";
        ADSHANDLE hI = 0;
        REQUIRE(AdsCreateIndex61(hT, bag, tag, exp, nullptr, nullptr,
                                 ADS_COMPOUND, 512, &hI) == AE_SUCCESS);
        rr_append(hT, 2);
        rr_append(hT, 1);
        REQUIRE(AdsCloseTable(hT) == AE_SUCCESS);
        REQUIRE(AdsDisconnect(hC) == AE_SUCCESS);
    }
    REQUIRE(rr_local_key_count(dir) == 2);
    fs::copy_file(dir / "RR.CDX", dir / "RR.OLD",
                  fs::copy_options::overwrite_existing);
    {
        UNSIGNED8 srv[512]{};
        std::memcpy(srv, dir.string().c_str(), dir.string().size());
        ADSHANDLE hC = 0;
        REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0,
                             &hC) == AE_SUCCESS);
        UNSIGNED8 tn[] = "RR.DBF";
        ADSHANDLE hT = 0;
        REQUIRE(AdsOpenTable(hC, tn, nullptr, ADS_CDX, 0, 0, 0, 0, &hT)
                == AE_SUCCESS);
        rr_append(hT, 4);
        rr_append(hT, 3);
        REQUIRE(AdsCloseTable(hT) == AE_SUCCESS);
        REQUIRE(AdsDisconnect(hC) == AE_SUCCESS);
    }
    fs::copy_file(dir / "RR.OLD", dir / "RR.CDX",
                  fs::copy_options::overwrite_existing);
    REQUIRE(rr_local_key_count(dir) == 2);   // stale: 4 rows, 2 keys

    // Remote REINDEX, the way rddads ordListRebuild issues it.
    {
        openads::network::Server s;
        REQUIRE(s.start("127.0.0.1", 0).has_value());
        char uri[512]{};
        std::snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u/%s",
                      static_cast<unsigned>(s.port()),
                      dir.string().c_str());
        UNSIGNED8 sb[512]{};
        std::memcpy(sb, uri, std::strlen(uri));
        ADSHANDLE hC = 0;
        REQUIRE(AdsConnect60(sb, ADS_REMOTE_SERVER, nullptr, nullptr, 0,
                             &hC) == AE_SUCCESS);
        UNSIGNED8 tn[] = "RR.DBF";
        ADSHANDLE hT = 0;
        REQUIRE(AdsOpenTable(hC, tn, nullptr, ADS_CDX, 0, 0, 0, 0, &hT)
                == AE_SUCCESS);
        UNSIGNED8 tag[] = "BYID";
        ADSHANDLE hI = 0;
        REQUIRE(AdsGetIndexHandle(hT, tag, &hI) == AE_SUCCESS);
        REQUIRE(AdsSetIndexOrderByHandle(hT, hI) == AE_SUCCESS);
        REQUIRE(AdsGotoTop(hT) == AE_SUCCESS);
        REQUIRE(AdsReindex(hT) == AE_SUCCESS);
        REQUIRE(AdsCloseTable(hT) == AE_SUCCESS);
        REQUIRE(AdsDisconnect(hC) == AE_SUCCESS);
        s.stop();
    }

    CHECK(rr_local_key_count(dir) == 4);
    fs::remove_all(dir, ec);
}

// Vouch-shaped remote REINDEX: CDX-format production bag named <base>.Z01
// with several tags, table opened remotely (shared and exclusive) with no
// order set, then AdsReindex. Every tag must be rebuilt and the bag file
// rewritten, for both an empty table and a stale non-empty one.
namespace {

const char* const kRzTags[] = {"TA", "TB", "TC"};

void rz_counts(const fs::path& dir, UNSIGNED32 out[3]) {
    UNSIGNED8 srv[512]{};
    std::memcpy(srv, dir.string().c_str(), dir.string().size());
    ADSHANDLE hC = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hC)
            == AE_SUCCESS);
    UNSIGNED8 tn[] = "RZ.DBF";
    ADSHANDLE hT = 0;
    REQUIRE(AdsOpenTable(hC, tn, nullptr, ADS_CDX, 0, 0, 0, 0, &hT)
            == AE_SUCCESS);
    UNSIGNED8 bag[] = "RZ.Z01";
    ADSHANDLE arr[16]{};
    UNSIGNED16 alen = 16;
    (void)AdsOpenIndex(hT, bag, arr, &alen);
    for (int i = 0; i < 3; ++i) {
        UNSIGNED8 tg[8]{};
        std::memcpy(tg, kRzTags[i], std::strlen(kRzTags[i]));
        ADSHANDLE hI = 0;
        REQUIRE(AdsGetIndexHandle(hT, tg, &hI) == AE_SUCCESS);
        out[i] = 0;
        REQUIRE(AdsGetKeyCount(hI, ADS_IGNOREFILTERS, &out[i]) == AE_SUCCESS);
    }
    REQUIRE(AdsCloseTable(hT) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hC) == AE_SUCCESS);
}

void rz_append(ADSHANDLE hT, double id) {
    UNSIGNED8 fid[] = "ID";
    UNSIGNED8 fnm[] = "NM";
    char nm[16];
    std::snprintf(nm, sizeof(nm), "N%05d", static_cast<int>(id));
    REQUIRE(AdsAppendRecord(hT) == AE_SUCCESS);
    REQUIRE(AdsSetDouble(hT, fid, id) == AE_SUCCESS);
    REQUIRE(AdsSetString(hT, fnm, reinterpret_cast<UNSIGNED8*>(nm),
                         static_cast<UNSIGNED32>(std::strlen(nm)))
            == AE_SUCCESS);
    REQUIRE(AdsWriteRecord(hT) == AE_SUCCESS);
}

void rz_run(bool empty, UNSIGNED16 mode) {
    CAPTURE(empty);
    CAPTURE(mode);
    auto dir = fs::temp_directory_path() / "openads_remote_reindex_z01";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    const UNSIGNED32 want = empty ? 0 : 4;
    {
        UNSIGNED8 srv[512]{};
        std::memcpy(srv, dir.string().c_str(), dir.string().size());
        ADSHANDLE hC = 0;
        REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0,
                             &hC) == AE_SUCCESS);
        UNSIGNED8 def[] = "ID,N,8,0;NM,C,10,0";
        UNSIGNED8 tn[] = "RZ.DBF";
        ADSHANDLE hT = 0;
        REQUIRE(AdsCreateTable(hC, tn, nullptr, ADS_CDX, 0, 0, 0, 0, def,
                               &hT) == AE_SUCCESS);
        const char* exprs[] = {"STR(ID,8)", "NM", "NM+STR(ID,8)"};
        for (int i = 0; i < 3; ++i) {
            UNSIGNED8 bag[] = "RZ.Z01";
            UNSIGNED8 tg[8]{};
            std::memcpy(tg, kRzTags[i], std::strlen(kRzTags[i]));
            UNSIGNED8 ex[32]{};
            std::memcpy(ex, exprs[i], std::strlen(exprs[i]));
            ADSHANDLE hI = 0;
            REQUIRE(AdsCreateIndex61(hT, bag, tg, ex, nullptr, nullptr,
                                     ADS_COMPOUND, 512, &hI) == AE_SUCCESS);
        }
        if (!empty) { rz_append(hT, 2); rz_append(hT, 1); }
        REQUIRE(AdsCloseTable(hT) == AE_SUCCESS);
        REQUIRE(AdsDisconnect(hC) == AE_SUCCESS);
    }
    if (!empty) {
        fs::copy_file(dir / "RZ.Z01", dir / "RZ.OLD",
                      fs::copy_options::overwrite_existing);
        UNSIGNED8 srv[512]{};
        std::memcpy(srv, dir.string().c_str(), dir.string().size());
        ADSHANDLE hC = 0;
        REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0,
                             &hC) == AE_SUCCESS);
        UNSIGNED8 tn[] = "RZ.DBF";
        ADSHANDLE hT = 0;
        REQUIRE(AdsOpenTable(hC, tn, nullptr, ADS_CDX, 0, 0, 0, 0, &hT)
                == AE_SUCCESS);
        UNSIGNED8 bag[] = "RZ.Z01";
        ADSHANDLE arr[16]{};
        UNSIGNED16 alen = 16;
        (void)AdsOpenIndex(hT, bag, arr, &alen);
        rz_append(hT, 4);
        rz_append(hT, 3);
        REQUIRE(AdsCloseTable(hT) == AE_SUCCESS);
        REQUIRE(AdsDisconnect(hC) == AE_SUCCESS);
        fs::copy_file(dir / "RZ.OLD", dir / "RZ.Z01",
                      fs::copy_options::overwrite_existing);
        UNSIGNED32 c[3]{};
        rz_counts(dir, c);
        CHECK(c[0] == 2);  // stale before the remote reindex
    }
    const auto old_t = fs::file_time_type::clock::now() - std::chrono::hours(24);
    fs::last_write_time(dir / "RZ.Z01", old_t);
    {
        openads::network::Server s;
        REQUIRE(s.start("127.0.0.1", 0).has_value());
        char uri[512]{};
        std::snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u/%s",
                      static_cast<unsigned>(s.port()),
                      dir.string().c_str());
        UNSIGNED8 sb[512]{};
        std::memcpy(sb, uri, std::strlen(uri));
        ADSHANDLE hC = 0;
        REQUIRE(AdsConnect60(sb, ADS_REMOTE_SERVER, nullptr, nullptr, 0,
                             &hC) == AE_SUCCESS);
        UNSIGNED8 tn[] = "RZ.DBF";
        ADSHANDLE hT = 0;
        REQUIRE(AdsOpenTable(hC, tn, nullptr, ADS_CDX, 0, 0, 0, mode, &hT)
                == AE_SUCCESS);
        CHECK(AdsReindex(hT) == AE_SUCCESS);
        REQUIRE(AdsCloseTable(hT) == AE_SUCCESS);
        REQUIRE(AdsDisconnect(hC) == AE_SUCCESS);
        s.stop();
    }
    CHECK(fs::last_write_time(dir / "RZ.Z01") > old_t + std::chrono::hours(1));
    UNSIGNED32 c[3]{};
    rz_counts(dir, c);
    CHECK(c[0] == want);
    CHECK(c[1] == want);
    CHECK(c[2] == want);
    fs::remove_all(dir, ec);
}

} // namespace

TEST_CASE("Remote AdsReindex, Vouch shape: multi-tag .Z01, no order") {
    rz_run(true,  ADS_SHARED);
    rz_run(true,  ADS_EXCLUSIVE);
    rz_run(false, ADS_SHARED);
    rz_run(false, ADS_EXCLUSIVE);
}
