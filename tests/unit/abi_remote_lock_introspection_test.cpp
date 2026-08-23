// abi_remote_lock_introspection_test.cpp -- M12.36: AdsIsRecordLocked and
// AdsGetAllLocks over a remote (tcp://) connection.
//
// Root cause pinned here: both functions silently reported "no locks" for
// remote handles (ace_exports.cpp returned early for get_remote_table()),
// so Harbour dbRecordInfo(DBRI_LOCKED) and dbRLockList() under ADSCDX
// always answered .F./{} — Vouch's IsLogged() thread bailed out.
// The IsRecordLocked (0x13) / GetAllLocks (0x15) wire opcodes forward the
// query to the server-side per-session lock state.
#include "doctest.h"
#include "openads/ace.h"
#include "openads/error.h"
#include "network/server.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;
using openads::AE_SUCCESS;

namespace {

fs::path lock_tmp_dir() {
    return fs::temp_directory_path() / "openads_remote_lock_intro";
}

// Recreates <tmp>/li.dbf with 5 rows, NAME C(8).
void seed_lock_fixture(const fs::path& dir) {
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[512]{};
    const auto d = dir.string();
    std::memcpy(srv, d.c_str(), d.size());
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn)
            == AE_SUCCESS);

    UNSIGNED8 tname[] = "li.dbf";
    UNSIGNED8 def[]   = "NAME,C,8,0";
    ADSHANDLE hTable  = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX, ADS_ANSI,
                           0, 0, 0, def, &hTable) == AE_SUCCESS);
    UNSIGNED8 fld[] = "NAME";
    for (int i = 0; i < 5; ++i) {
        char v[9];
        std::snprintf(v, sizeof(v), "R%07d", i);
        REQUIRE(AdsAppendRecord(hTable) == AE_SUCCESS);
        REQUIRE(AdsSetString(hTable, fld, reinterpret_cast<UNSIGNED8*>(v),
                             8) == AE_SUCCESS);
        REQUIRE(AdsWriteRecord(hTable) == AE_SUCCESS);
    }
    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
}

} // namespace

TEST_CASE("M12.36 remote AdsIsRecordLocked reflects this connection's locks") {
    auto dir = lock_tmp_dir();
    seed_lock_fixture(dir);

    openads::network::Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());

    char uri[512];
    std::snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u/%s",
                  static_cast<unsigned>(srv.port()), dir.string().c_str());
    UNSIGNED8 srvbuf[512]{};
    std::memcpy(srvbuf, uri, std::strlen(uri) + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srvbuf, ADS_REMOTE_SERVER, nullptr, nullptr, 0,
                         &hConn) == AE_SUCCESS);

    UNSIGNED8 tname[] = "li.dbf";
    ADSHANDLE hTable  = 0;
    REQUIRE(AdsOpenTable(hConn, tname, nullptr, ADS_CDX, ADS_ANSI, ADS_SHARED,
                         ADS_COMPATIBLE_LOCKING, ADS_DEFAULT, &hTable)
            == AE_SUCCESS);

    // Nothing locked yet.
    UNSIGNED16 locked = 1;
    REQUIRE(AdsIsRecordLocked(hTable, 3, &locked) == AE_SUCCESS);
    CHECK(locked == 0);

    // Lock record 3 remotely; the query must now see it.
    REQUIRE(AdsLockRecord(hTable, 3) == AE_SUCCESS);
    REQUIRE(AdsIsRecordLocked(hTable, 3, &locked) == AE_SUCCESS);
    CHECK(locked == 1);
    REQUIRE(AdsIsRecordLocked(hTable, 4, &locked) == AE_SUCCESS);
    CHECK(locked == 0);

    // recno 0 = current record (ACE convention): position on 3 and ask.
    REQUIRE(AdsGotoRecord(hTable, 3) == AE_SUCCESS);
    REQUIRE(AdsIsRecordLocked(hTable, 0, &locked) == AE_SUCCESS);
    CHECK(locked == 1);

    REQUIRE(AdsUnlockRecord(hTable, 3) == AE_SUCCESS);
    REQUIRE(AdsIsRecordLocked(hTable, 3, &locked) == AE_SUCCESS);
    CHECK(locked == 0);

    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
}

TEST_CASE("M12.36 remote AdsGetAllLocks enumerates held record locks") {
    auto dir = lock_tmp_dir();
    seed_lock_fixture(dir);

    openads::network::Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());

    char uri[512];
    std::snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u/%s",
                  static_cast<unsigned>(srv.port()), dir.string().c_str());
    UNSIGNED8 srvbuf[512]{};
    std::memcpy(srvbuf, uri, std::strlen(uri) + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srvbuf, ADS_REMOTE_SERVER, nullptr, nullptr, 0,
                         &hConn) == AE_SUCCESS);

    UNSIGNED8 tname[] = "li.dbf";
    ADSHANDLE hTable  = 0;
    REQUIRE(AdsOpenTable(hConn, tname, nullptr, ADS_CDX, ADS_ANSI, ADS_SHARED,
                         ADS_COMPATIBLE_LOCKING, ADS_DEFAULT, &hTable)
            == AE_SUCCESS);

    REQUIRE(AdsLockRecord(hTable, 2) == AE_SUCCESS);
    REQUIRE(AdsLockRecord(hTable, 4) == AE_SUCCESS);

    // Size query first (null buffer), then the full list.
    UNSIGNED16 count = 0;
    REQUIRE(AdsGetAllLocks(hTable, nullptr, &count) == AE_SUCCESS);
    CHECK(count == 2);

    UNSIGNED32 recs[8];
    count = 8;
    REQUIRE(AdsGetAllLocks(hTable, recs, &count) == AE_SUCCESS);
    CHECK(count == 2);
    CHECK(std::find(recs, recs + count, 2u) != recs + count);
    CHECK(std::find(recs, recs + count, 4u) != recs + count);

    // Unlocking one shrinks the remote list.
    REQUIRE(AdsUnlockRecord(hTable, 2) == AE_SUCCESS);
    count = 8;
    REQUIRE(AdsGetAllLocks(hTable, recs, &count) == AE_SUCCESS);
    CHECK(count == 1);
    CHECK(recs[0] == 4u);

    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
}
