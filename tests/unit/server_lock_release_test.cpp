// Console "Release" (POST /api/server/locks/release -> Server::release_lock):
// drop one lock held by a remote session without disconnecting it. The
// release runs on the holder's own session thread; afterwards another
// session can take the lock, the holder stays connected, and the
// holder's write to that record no longer succeeds.
#include "doctest.h"
#include "mgmt/mg_collector.h"
#include "network/server.h"
#include "openads/ace.h"

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using openads::network::Server;

namespace {

ADSHANDLE connect_to(const std::string& where, UNSIGNED16 type) {
    std::vector<UNSIGNED8> buf(where.begin(), where.end());
    buf.push_back(0);
    ADSHANDLE h = 0;
    REQUIRE(AdsConnect60(buf.data(), type, nullptr, nullptr, 0, &h) == 0);
    return h;
}

ADSHANDLE open_table(ADSHANDLE conn) {
    UNSIGNED8 name[] = "rel.dbf";
    ADSHANDLE t = 0;
    REQUIRE(AdsOpenTable(conn, name, nullptr, ADS_CDX, ADS_ANSI,
                         ADS_COMPATIBLE_LOCKING, ADS_IGNORERIGHTS,
                         ADS_SHARED, &t) == 0);
    return t;
}

void make_table(const fs::path& dir) {
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    ADSHANDLE conn = connect_to(dir.string(), ADS_LOCAL_SERVER);
    UNSIGNED8 fields[] = "NAME,Character,10";
    UNSIGNED8 tname[]  = "rel.dbf";
    ADSHANDLE t = 0;
    REQUIRE(AdsCreateTable(conn, tname, nullptr, ADS_CDX, 0, 0, 0, 64,
                           fields, &t) == 0);
    for (int i = 0; i < 10; ++i) {
        REQUIRE(AdsAppendRecord(t) == 0);
        UNSIGNED8 fN[] = "NAME";
        const std::string v = "R" + std::to_string(i + 1);
        AdsSetString(t, fN, (UNSIGNED8*)v.c_str(), static_cast<UNSIGNED32>(v.size()));
        REQUIRE(AdsWriteRecord(t) == 0);
    }
    REQUIRE(AdsCloseTable(t) == 0);
    REQUIRE(AdsDisconnect(conn) == 0);
}

// The registry entry the console shows for (recno), as the HTTP layer sees it.
bool find_lock(std::uint32_t recno, openads::mgmt::MgLock& out) {
    for (const auto& l : openads::mgmt::LockRegistry::instance().snapshot()) {
        if (l.recno == recno && l.table.find("rel.dbf") != std::string::npos) {
            out = l;
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("console releases one lock without disconnecting the holder") {
    const auto dir = fs::temp_directory_path() / "openads_lock_release";
    make_table(dir);

    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    const std::string uri = "tcp://127.0.0.1:" + std::to_string(srv.port()) +
                            "/" + dir.generic_string();
    ADSHANDLE ca = connect_to(uri, ADS_REMOTE_SERVER);
    ADSHANDLE cb = connect_to(uri, ADS_REMOTE_SERVER);
    ADSHANDLE ta = open_table(ca), tb = open_table(cb);

    SUBCASE("record lock") {
        REQUIRE(AdsLockRecord(ta, 5) == 0);
        REQUIRE(AdsLockRecord(ta, 7) == 0);
        CHECK(AdsLockRecord(tb, 5) != 0);  // held by A

        openads::mgmt::MgLock l;
        REQUIRE(find_lock(5, l));
        REQUIRE(l.session_id != 0);
        CHECK(srv.release_lock(l.session_id, l.table, 5) ==
              Server::ReleaseResult::Released);
        CHECK_FALSE(find_lock(5, l));
        // Only record 5 went; A's other lock is untouched.
        openads::mgmt::MgLock l7;
        CHECK(find_lock(7, l7));

        // B can lock it now; A is still connected and working.
        CHECK(AdsLockRecord(tb, 5) == 0);
        UNSIGNED32 count = 0;
        CHECK(AdsGetRecordCount(ta, ADS_IGNOREFILTERS, &count) == 0);
        CHECK(count == 10);

        // A's save to record 5 no longer goes through.
        REQUIRE(AdsGotoRecord(ta, 5) == 0);
        UNSIGNED8 fN[] = "NAME";
        UNSIGNED8 v[] = "STALE";
        const UNSIGNED32 set_rc = AdsSetString(ta, fN, v, 5);
        const UNSIGNED32 wr_rc  = set_rc != 0 ? set_rc : AdsWriteRecord(ta);
        CHECK(wr_rc != 0);

        // Releasing it again: A no longer holds it.
        CHECK(srv.release_lock(l.session_id, l.table, 5) ==
              Server::ReleaseResult::NotHeld);
        REQUIRE(AdsUnlockRecord(tb, 5) == 0);
        AdsUnlockTable(ta);
    }

    SUBCASE("file lock") {
        REQUIRE(AdsLockTable(ta) == 0);
        CHECK(AdsLockTable(tb) != 0);
        openads::mgmt::MgLock l;
        REQUIRE(find_lock(0, l));
        CHECK(srv.release_lock(l.session_id, l.table, 0) ==
              Server::ReleaseResult::Released);
        CHECK_FALSE(find_lock(0, l));
        CHECK(AdsLockTable(tb) == 0);
        REQUIRE(AdsUnlockTable(tb) == 0);
    }

    SUBCASE("unknown session") {
        CHECK(srv.release_lock(0xFFFFFFF0u, "rel.dbf", 1) ==
              Server::ReleaseResult::NoSession);
    }

    AdsCloseTable(ta);
    AdsCloseTable(tb);
    AdsDisconnect(ca);
    AdsDisconnect(cb);
    srv.stop();
    std::error_code ec;
    fs::remove_all(dir, ec);
}
