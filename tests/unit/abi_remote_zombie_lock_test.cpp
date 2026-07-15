// Does closing a remote table actually release the record locks the client
// took over the wire?
//
// Wire LockRecord resolves to the session's shadow ABI handle
// (ensure_abi_handle), but the CloseTable handler closes only the ENGINE
// table and erases the tbls_h_ map entry WITHOUT AdsCloseTable-ing the
// shadow handle. If that leaks the lock, a second connection (or a reopen)
// can never lock the same record again until the first session disconnects.
#include "doctest.h"
#include "openads/ace.h"
#include "network/server.h"

#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

void make_dbf(const fs::path& dir) {
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    UNSIGNED8 srv[512];
    const auto sp = dir.string();
    std::memcpy(srv, sp.c_str(), sp.size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn) == 0);
    UNSIGNED8 def[]   = "ID,N,8,0";
    UNSIGNED8 tname[] = "ZL.DBF";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX, 0, 0, 0, 0, def, &hT) == 0);
    UNSIGNED8 fld[] = "ID";
    for (int i = 1; i <= 10; ++i) {
        REQUIRE(AdsAppendRecord(hT) == 0);
        AdsSetDouble(hT, fld, i);
        REQUIRE(AdsWriteRecord(hT) == 0);
    }
    ADSHANDLE hI = 0;
    UNSIGNED8 bag[] = "ZL.CDX";
    UNSIGNED8 tag[] = "BYID";
    UNSIGNED8 exp[] = "ID";
    REQUIRE(AdsCreateIndex61(hT, bag, tag, exp, nullptr, nullptr,
                             ADS_COMPOUND, 512, &hI) == 0);
    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
}

ADSHANDLE remote_connect(const fs::path& dir, std::uint16_t port) {
    std::string uri = "tcp://127.0.0.1:" + std::to_string(port) + "/" +
                      dir.generic_string();
    std::vector<UNSIGNED8> buf(uri.begin(), uri.end());
    buf.push_back(0);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(buf.data(), ADS_REMOTE_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    return hConn;
}

ADSHANDLE open_ordered(ADSHANDLE hConn) {
    UNSIGNED8 tname[] = "ZL.DBF";
    ADSHANDLE hT = 0;
    REQUIRE(AdsOpenTable(hConn, tname, nullptr, ADS_CDX, 0, 0, 0, 0, &hT) == 0);
    // Touch the order so the session materialises its shadow ABI handle,
    // the way any rddads browse does.
    ADSHANDLE hI = 0;
    UNSIGNED8 tag[] = "BYID";
    REQUIRE(AdsGetIndexHandle(hT, tag, &hI) == 0);
    REQUIRE(AdsGotoTop(hI) == 0);
    return hT;
}

} // namespace

TEST_CASE("remote: record lock is released when the table is closed") {
    using openads::network::Server;
    auto dir = fs::temp_directory_path() / "openads_zombie_lock";
    make_dbf(dir);

    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());

    // Session A: lock recno 5 and close WITHOUT unlocking — the pattern of
    // any xbase app that relies on close releasing its locks.
    ADSHANDLE hConnA = remote_connect(dir, srv.port());
    ADSHANDLE hTA = open_ordered(hConnA);
    REQUIRE(AdsLockRecord(hTA, 5) == 0);
    REQUIRE(AdsCloseTable(hTA) == 0);

    // Session B must be able to lock the same record now.
    ADSHANDLE hConnB = remote_connect(dir, srv.port());
    ADSHANDLE hTB = open_ordered(hConnB);
    UNSIGNED32 rc = AdsLockRecord(hTB, 5);
    CHECK(rc == 0);   // zombie shadow-handle lock => AE_LOCK_FAILED here

    // Same story for a table lock.
    ADSHANDLE hTA2 = open_ordered(hConnA);
    REQUIRE(AdsLockTable(hTA2) == 0);
    REQUIRE(AdsCloseTable(hTA2) == 0);
    if (rc == 0) { (void)AdsUnlockRecord(hTB, 5); }
    UNSIGNED32 rc2 = AdsLockTable(hTB);
    CHECK(rc2 == 0);

    (void)AdsCloseTable(hTB);
    (void)AdsDisconnect(hConnB);
    (void)AdsDisconnect(hConnA);
    std::error_code ec;
    fs::remove_all(dir, ec);
    srv.stop();
}

TEST_CASE("remote: closing a table releases its files on the server") {
    using openads::network::Server;
    auto dir = fs::temp_directory_path() / "openads_close_release";
    make_dbf(dir);

    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());

    ADSHANDLE hConn = remote_connect(dir, srv.port());
    ADSHANDLE hT = open_ordered(hConn);       // ordered nav => shadow ABI handle
    REQUIRE(AdsCloseTable(hT) == 0);

    // The app-side pattern behind "exit the invoice": close the work files,
    // then erase/rename them. If the session's shadow ABI handle leaks, the
    // server still holds ZL.DBF/.CDX open here and Windows refuses the
    // delete, which an app-level retry loop turns into a hang.
    std::error_code ec;
    bool dbf_gone = fs::remove(dir / "ZL.DBF", ec) && !ec;
    std::error_code ec2;
    bool cdx_gone = fs::remove(dir / "ZL.CDX", ec2) && !ec2;
    CHECK_MESSAGE(dbf_gone, "ZL.DBF still open server-side after CloseTable: ",
                  ec.message());
    CHECK_MESSAGE(cdx_gone, "ZL.CDX still open server-side after CloseTable: ",
                  ec2.message());

    (void)AdsDisconnect(hConn);
    fs::remove_all(dir, ec);
    srv.stop();
}
