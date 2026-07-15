#include "doctest.h"
#include "openads/ace.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <string>

TEST_CASE("M9.25 AdsMgConnect (local) produces a real mgmt handle") {
    UNSIGNED8 srv[8] = "local";
    UNSIGNED8 usr[2] = "u";
    UNSIGNED8 pwd[2] = "p";
    ADSHANDLE h = 0;
    REQUIRE(AdsMgConnect(srv, usr, pwd, &h) == 0);
    CHECK(h != 0);
    REQUIRE(AdsMgDisconnect(h) == 0);
}

TEST_CASE("M9.25 AdsMgConnect treats a drive path as local") {
    // rddads' manage.prg passes "C:" — a drive path, NOT a host. It
    // must resolve to a local-mode handle, not a remote host named C.
    UNSIGNED8 srv[8] = "C:";
    UNSIGNED8 usr[2] = "u";
    UNSIGNED8 pwd[2] = "p";
    ADSHANDLE h = 0;
    REQUIRE(AdsMgConnect(srv, usr, pwd, &h) == 0);
    CHECK(h != 0);
    UNSIGNED16 t = 99;
    REQUIRE(AdsMgGetServerType(h, &t) == 0);
    CHECK(t == 0);   // 0 = local
    REQUIRE(AdsMgDisconnect(h) == 0);
}

TEST_CASE("M9.25 AdsMgGetServerType reports 0 (local) for a local handle") {
    UNSIGNED8 srv[8] = "local";
    UNSIGNED8 usr[2] = "u";
    UNSIGNED8 pwd[2] = "p";
    ADSHANDLE h = 0;
    REQUIRE(AdsMgConnect(srv, usr, pwd, &h) == 0);
    UNSIGNED16 t = 99;
    REQUIRE(AdsMgGetServerType(h, &t) == 0);
    CHECK(t == 0);
    REQUIRE(AdsMgDisconnect(h) == 0);
}

TEST_CASE("M9.25 AdsMgGetInstallInfo reports the OpenADS version") {
    UNSIGNED8 srv[8] = "local";
    UNSIGNED8 usr[2] = "u";
    UNSIGNED8 pwd[2] = "p";
    ADSHANDLE h = 0;
    REQUIRE(AdsMgConnect(srv, usr, pwd, &h) == 0);

    ADS_MGMT_INSTALL_INFO info;
    UNSIGNED16 sz = sizeof(info);
    REQUIRE(AdsMgGetInstallInfo(h, &info, &sz) == 0);
    CHECK(sz == sizeof(ADS_MGMT_INSTALL_INFO));
    std::string ver(reinterpret_cast<const char*>(info.aucVersionStr));
    // Release builds report "OpenADS X.Y.Z"; dev builds may drop the brand
    // so the longer git-describe version fits the 16-byte field.
    CHECK_FALSE(ver.empty());
    CHECK(ver != "OpenADS 1.0");    // the pre-1.8.14 hardcoded string

    REQUIRE(AdsMgDisconnect(h) == 0);
}

TEST_CASE("M9.25 AdsMgGetActivityInfo (local) reports the process") {
    UNSIGNED8 srv[8] = "local";
    UNSIGNED8 usr[2] = "u";
    UNSIGNED8 pwd[2] = "p";
    ADSHANDLE h = 0;
    REQUIRE(AdsMgConnect(srv, usr, pwd, &h) == 0);

    ADS_MGMT_ACTIVITY_INFO act;
    UNSIGNED16 sz = sizeof(act);
    REQUIRE(AdsMgGetActivityInfo(h, &act, &sz) == 0);
    // Local mode always counts the calling process as one user;
    // the connection/table counts reflect the live ABI handle
    // registry, so they are >= 0 (whatever this process has open).
    CHECK(act.stUsers.ulInUse == 1);

    REQUIRE(AdsMgDisconnect(h) == 0);
}

TEST_CASE("M9.25 AdsMgGetUserNames (local) lists the in-process user") {
    UNSIGNED8 srv[8] = "local";
    UNSIGNED8 usr[2] = "u";
    UNSIGNED8 pwd[2] = "p";
    ADSHANDLE h = 0;
    REQUIRE(AdsMgConnect(srv, usr, pwd, &h) == 0);

    std::array<ADS_MGMT_USER_INFO, 4> buf;
    UNSIGNED16 cnt = static_cast<UNSIGNED16>(buf.size());
    UNSIGNED16 sz  = sizeof(ADS_MGMT_USER_INFO);
    REQUIRE(AdsMgGetUserNames(h, nullptr, buf.data(), &cnt, &sz) == 0);
    CHECK(cnt == 1);

    REQUIRE(AdsMgDisconnect(h) == 0);
}

TEST_CASE("M9.25 AdsMgKillUser + AdsMgResetCommStats succeed on a local handle") {
    UNSIGNED8 srv[8] = "local";
    UNSIGNED8 usr[2] = "u";
    UNSIGNED8 pwd[2] = "p";
    ADSHANDLE h = 0;
    REQUIRE(AdsMgConnect(srv, usr, pwd, &h) == 0);

    UNSIGNED8 user[8] = "alice";
    CHECK(AdsMgKillUser(h, user, /*usConnNo=*/0) == 0);
    CHECK(AdsMgResetCommStats(h) == 0);

    REQUIRE(AdsMgDisconnect(h) == 0);
}

TEST_CASE("M9.25 AdsMg* reject an unknown handle") {
    ADS_MGMT_ACTIVITY_INFO act;
    UNSIGNED16 sz = sizeof(act);
    CHECK(AdsMgGetActivityInfo(/*bogus*/ 0x1234, &act, &sz) != 0);

    ADS_MGMT_INSTALL_INFO info;
    UNSIGNED16 isz = sizeof(info);
    CHECK(AdsMgGetInstallInfo(0x1234, &info, &isz) != 0);
}

TEST_CASE("M9.25 AdsMgGetUserNames clamps to caller capacity") {
    UNSIGNED8 srv[8] = "local";
    UNSIGNED8 usr[2] = "u";
    UNSIGNED8 pwd[2] = "p";
    ADSHANDLE h = 0;
    REQUIRE(AdsMgConnect(srv, usr, pwd, &h) == 0);

    // Local mode reports exactly 1 user. Offer ZERO capacity: the
    // call must still succeed, write nothing, and report the real
    // count (1) back through pusCount.
    ADS_MGMT_USER_INFO one;   // a 1-slot buffer we promise not to fill
    UNSIGNED16 cnt = 0;       // capacity = 0 elements
    UNSIGNED16 sz  = sizeof(ADS_MGMT_USER_INFO);
    REQUIRE(AdsMgGetUserNames(h, nullptr, &one, &cnt, &sz) == 0);
    CHECK(cnt == 1);          // real count written back

    REQUIRE(AdsMgDisconnect(h) == 0);
}
