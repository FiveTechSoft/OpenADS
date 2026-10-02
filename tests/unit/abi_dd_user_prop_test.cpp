#include "openads/error.h"
#include "doctest.h"
#include "openads/ace.h"
#include "openads/error.h"

#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {

fs::path setup_empty_dd(const fs::path& dir) {
    fs::create_directories(dir);
    auto p = dir / "test.add";
    UNSIGNED8 buf[260];
    std::memcpy(buf, p.string().c_str(), p.string().size() + 1);
    ADSHANDLE hConn = 0;
    AdsDDCreate(buf, 0, nullptr, &hConn);
    AdsDisconnect(hConn);
    return p;
}

} // namespace

TEST_CASE("AdsDDCreateUser keeps password write-only and description readable") {
    const auto dir = fs::temp_directory_path() / "openads_dd_uprop_create";
    std::error_code ec;
    fs::remove_all(dir, ec);
    auto add_path = setup_empty_dd(dir);

    ADSHANDLE hConn = 0;
    UNSIGNED8 add_buf[260];
    std::memcpy(add_buf, add_path.string().c_str(), add_path.string().size() + 1);
    REQUIRE(AdsConnect60(add_buf, 0, nullptr, nullptr, 0, &hConn) == 0);

    UNSIGNED8 user[32] = "alice";
    UNSIGNED8 pwd [32] = "secret";
    UNSIGNED8 desc[64] = "Test user Alice";
    REQUIRE(AdsDDCreateUser(hConn, nullptr, user, pwd, desc) == 0);

    // Password is write-only, even for local bootstrap.
    UNSIGNED8 outbuf[256] = {};
    UNSIGNED16 len = sizeof(outbuf);
    CHECK(AdsDDGetUserProperty(hConn, user, ADS_DD_USER_PASSWORD, outbuf, &len) == AE_PROPERTY_NOT_SET);
    CHECK(len == 0);

    // Comment/description readable via property 1 (ADS_DD_COMMENT).
    std::memset(outbuf, 0, sizeof(outbuf));
    len = sizeof(outbuf);
    REQUIRE(AdsDDGetUserProperty(hConn, user, ADS_DD_COMMENT, outbuf, &len) == 0);
    CHECK(std::string(reinterpret_cast<const char*>(outbuf), len) == "Test user Alice");

    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("AdsDDCreateUser with group adds membership") {
    const auto dir = fs::temp_directory_path() / "openads_dd_uprop_grp";
    std::error_code ec;
    fs::remove_all(dir, ec);
    auto add_path = setup_empty_dd(dir);

    ADSHANDLE hConn = 0;
    UNSIGNED8 add_buf[260];
    std::memcpy(add_buf, add_path.string().c_str(), add_path.string().size() + 1);
    REQUIRE(AdsConnect60(add_buf, 0, nullptr, nullptr, 0, &hConn) == 0);

    UNSIGNED8 grp [32] = "admins";
    UNSIGNED8 user[32] = "bob";
    REQUIRE(AdsDDCreateUser(hConn, grp, user, nullptr, nullptr) == 0);

    // Group membership readable via property 1102.
    UNSIGNED8 outbuf[256] = {};
    UNSIGNED16 len = sizeof(outbuf);
    REQUIRE(AdsDDGetUserProperty(hConn, user, ADS_DD_USER_GROUP_MEMBERSHIP, outbuf, &len) == 0);
    std::string groups(reinterpret_cast<const char*>(outbuf), len);
    CHECK(groups == "admins");

    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("AdsDDSetUserProperty: password remains write-only") {
    const auto dir = fs::temp_directory_path() / "openads_dd_uprop_setpwd";
    std::error_code ec;
    fs::remove_all(dir, ec);
    auto add_path = setup_empty_dd(dir);

    ADSHANDLE hConn = 0;
    UNSIGNED8 add_buf[260];
    std::memcpy(add_buf, add_path.string().c_str(), add_path.string().size() + 1);
    REQUIRE(AdsConnect60(add_buf, 0, nullptr, nullptr, 0, &hConn) == 0);

    UNSIGNED8 user[32] = "carol";
    REQUIRE(AdsDDCreateUser(hConn, nullptr, user, nullptr, nullptr) == 0);

    // Set password via SetUserProperty.
    std::string newpwd = "newpass";
    REQUIRE(AdsDDSetUserProperty(hConn, user, ADS_DD_USER_PASSWORD,
                                 const_cast<char*>(newpwd.c_str()),
                                 static_cast<UNSIGNED16>(newpwd.size())) == 0);

    // Reopen and verify.
    REQUIRE(AdsDisconnect(hConn) == 0);
    hConn = 0;
    REQUIRE(AdsConnect60(add_buf, 0, nullptr, nullptr, 0, &hConn) == 0);

    UNSIGNED8 outbuf[256] = {};
    UNSIGNED16 len = sizeof(outbuf);
    CHECK(AdsDDGetUserProperty(hConn, user, ADS_DD_USER_PASSWORD, outbuf, &len) == AE_PROPERTY_NOT_SET);
    CHECK(len == 0);

    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("AdsDDSetUserProperty — group membership (1102) adds to group") {
    const auto dir = fs::temp_directory_path() / "openads_dd_uprop_setgrp";
    std::error_code ec;
    fs::remove_all(dir, ec);
    auto add_path = setup_empty_dd(dir);

    ADSHANDLE hConn = 0;
    UNSIGNED8 add_buf[260];
    std::memcpy(add_buf, add_path.string().c_str(), add_path.string().size() + 1);
    REQUIRE(AdsConnect60(add_buf, 0, nullptr, nullptr, 0, &hConn) == 0);

    UNSIGNED8 user[32] = "dave";
    REQUIRE(AdsDDCreateUser(hConn, nullptr, user, nullptr, nullptr) == 0);

    std::string grp = "editors";
    REQUIRE(AdsDDSetUserProperty(hConn, user, ADS_DD_USER_GROUP_MEMBERSHIP,
                                 const_cast<char*>(grp.c_str()),
                                 static_cast<UNSIGNED16>(grp.size())) == 0);

    UNSIGNED8 outbuf[256] = {};
    UNSIGNED16 len = sizeof(outbuf);
    REQUIRE(AdsDDGetUserProperty(hConn, user, ADS_DD_USER_GROUP_MEMBERSHIP, outbuf, &len) == 0);
    std::string groups(reinterpret_cast<const char*>(outbuf), len);
    CHECK(groups == "editors");

    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("AdsDDGetUserProperty — bad logins (1103) returns 0") {
    const auto dir = fs::temp_directory_path() / "openads_dd_uprop_badlogins";
    std::error_code ec;
    fs::remove_all(dir, ec);
    auto add_path = setup_empty_dd(dir);

    ADSHANDLE hConn = 0;
    UNSIGNED8 add_buf[260];
    std::memcpy(add_buf, add_path.string().c_str(), add_path.string().size() + 1);
    REQUIRE(AdsConnect60(add_buf, 0, nullptr, nullptr, 0, &hConn) == 0);

    UNSIGNED8 user[32] = "eve";
    REQUIRE(AdsDDCreateUser(hConn, nullptr, user, nullptr, nullptr) == 0);

    UNSIGNED16 badlogins = 0xFFFF;
    UNSIGNED16 len = sizeof(badlogins);
    REQUIRE(AdsDDGetUserProperty(hConn, user, ADS_DD_USER_BAD_LOGINS, &badlogins, &len) == 0);
    CHECK(badlogins == 0);
    CHECK(len == 2u);

    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("AdsDDSetUserProperty — bad logins (1103) is a no-op") {
    const auto dir = fs::temp_directory_path() / "openads_dd_uprop_badlogins_set";
    std::error_code ec;
    fs::remove_all(dir, ec);
    auto add_path = setup_empty_dd(dir);

    ADSHANDLE hConn = 0;
    UNSIGNED8 add_buf[260];
    std::memcpy(add_buf, add_path.string().c_str(), add_path.string().size() + 1);
    REQUIRE(AdsConnect60(add_buf, 0, nullptr, nullptr, 0, &hConn) == 0);

    UNSIGNED8 user[32] = "frank";
    REQUIRE(AdsDDCreateUser(hConn, nullptr, user, nullptr, nullptr) == 0);

    UNSIGNED16 val = 42;
    // Set should succeed (no-op) without error.
    CHECK(AdsDDSetUserProperty(hConn, user, ADS_DD_USER_BAD_LOGINS, &val, sizeof(val)) == 0);
    // Counter still reads as 0.
    UNSIGNED16 out = 0xFFFF;
    UNSIGNED16 len = sizeof(out);
    REQUIRE(AdsDDGetUserProperty(hConn, user, ADS_DD_USER_BAD_LOGINS, &out, &len) == 0);
    CHECK(out == 0);

    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("AdsDDSetUserProperty — unknown user returns error") {
    const auto dir = fs::temp_directory_path() / "openads_dd_uprop_nouser";
    std::error_code ec;
    fs::remove_all(dir, ec);
    auto add_path = setup_empty_dd(dir);

    ADSHANDLE hConn = 0;
    UNSIGNED8 add_buf[260];
    std::memcpy(add_buf, add_path.string().c_str(), add_path.string().size() + 1);
    REQUIRE(AdsConnect60(add_buf, 0, nullptr, nullptr, 0, &hConn) == 0);

    UNSIGNED8 nobody[32] = "nobody";
    std::string val = "x";
    UNSIGNED32 r = AdsDDSetUserProperty(hConn, nobody, ADS_DD_USER_PASSWORD,
                                        const_cast<char*>(val.c_str()),
                                        static_cast<UNSIGNED16>(val.size()));
    CHECK(r != 0);

    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}
