// abi_index_collation_persist_test.cpp
//
// Tests for collation isolation, persistence, and the interaction between
// connection-level collation settings and index operations.

#include "doctest.h"
#include "openads/ace.h"
#include "engine/oem_collation.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {

void set_str(ADSHANDLE h, const char* field, const char* val) {
    UNSIGNED8 f[16];
    std::memcpy(f, field, std::strlen(field) + 1);
    UNSIGNED8 v[64];
    std::memcpy(v, val, std::strlen(val) + 1);
    AdsSetString(h, f, v, static_cast<UNSIGNED32>(std::strlen(val)));
}

void make_tag(ADSHANDLE hTable, const char* tag, const char* expr) {
    UNSIGNED8 fn[16];
    std::memcpy(fn, "data", 5);
    UNSIGNED8 t[64];
    std::memcpy(t, tag, std::strlen(tag) + 1);
    UNSIGNED8 e[64];
    std::memcpy(e, expr, std::strlen(expr) + 1);
    ADSHANDLE hIdx = 0;
    REQUIRE(AdsCreateIndex61(hTable, fn, t, e, nullptr, nullptr,
                             0, 512, &hIdx) == 0);
}

std::string get_field(ADSHANDLE hT, const char* field) {
    UNSIGNED8 buf[64];
    UNSIGNED32 len = sizeof(buf);
    UNSIGNED8 f[16];
    std::memcpy(f, field, std::strlen(field) + 1);
    AdsGetString(hT, f, buf, &len, 0);
    std::string s(reinterpret_cast<char*>(buf), len);
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

bool seek_hit(ADSHANDLE hI, ADSHANDLE hT, const char* key,
              const char* field, const char* expect_prefix) {
    UNSIGNED16 found = 0;
    AdsSeek(hI, reinterpret_cast<UNSIGNED8*>(const_cast<char*>(key)),
            static_cast<UNSIGNED16>(std::strlen(key)),
            ADS_STRINGKEY, /*hard*/ 0, &found);
    if (found != 1) return false;
    if (expect_prefix) {
        std::string got = get_field(hT, field);
        return got.substr(0, std::strlen(expect_prefix)) == expect_prefix;
    }
    return true;
}

} // namespace

// ---- Test 1: Two connections open same CDX, different collation ----

TEST_CASE("two connections: different collation isolation on same CDX") {
    auto dir = fs::temp_directory_path() / "openads_coll_twoconn";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    // Create the table and index on conn1 with PL852.
    ADSHANDLE hConn1 = 0;
    {
        UNSIGNED8 srv[256];
        std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
        REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0,
                             &hConn1) == 0);
        UNSIGNED8 coll[] = "NTXPL852";
        REQUIRE(AdsSetCollation(hConn1, coll) == 0);

        UNSIGNED8 def[]   = "NAME,C,8,0";
        UNSIGNED8 tname[] = "data";
        ADSHANDLE hT = 0;
        REQUIRE(AdsCreateTable(hConn1, tname, nullptr, ADS_CDX,
                               ADS_OEM, 0, 0, 0, def, &hT) == 0);

        const char* rows[] = {
            "LACAAAAA", "\x9D""ABBBBB", "MADCCCCC", "ZBYDDDDD" };
        UNSIGNED8 fName[] = "NAME";
        for (const char* row : rows) {
            REQUIRE(AdsAppendRecord(hT) == 0);
            REQUIRE(AdsSetString(hT, fName,
                reinterpret_cast<UNSIGNED8*>(const_cast<char*>(row)),
                static_cast<UNSIGNED32>(std::strlen(row))) == 0);
        }
        REQUIRE(AdsWriteRecord(hT) == 0);

        make_tag(hT, "BYNAME", "NAME");
        REQUIRE(AdsCloseTable(hT) == 0);
    }

    // Open with conn1 (PL852) — Ł sorts between L and M.
    {
        UNSIGNED8 onm[] = "data";
        ADSHANDLE hT = 0;
        REQUIRE(AdsOpenTable(hConn1, onm, onm, ADS_CDX, ADS_OEM, 0, 0, 0,
                             &hT) == 0);
        ADSHANDLE hI = 0;
        REQUIRE(AdsGetIndexHandle(hT, (UNSIGNED8*)"BYNAME", &hI) == 0);
        REQUIRE(AdsSetIndexOrderByHandle(hT, hI) == 0);

        REQUIRE(AdsGotoTop(hT) == 0);
        CHECK(get_field(hT, "NAME") == "LACAAAAA");
        REQUIRE(AdsSkip(hT, 1) == 0);
        CHECK(static_cast<unsigned char>(get_field(hT, "NAME")[0]) == 0x9D);
        REQUIRE(AdsSkip(hT, 1) == 0);
        CHECK(get_field(hT, "NAME") == "MADCCCCC");
        REQUIRE(AdsCloseTable(hT) == 0);
    }

    // Open with conn2 (BINARY) — Ł (0x9D) sorts after Z in binary.
    {
        UNSIGNED8 srv[256];
        std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
        ADSHANDLE hConn2 = 0;
        REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0,
                             &hConn2) == 0);
        UNSIGNED8 coll[] = "BINARY";
        REQUIRE(AdsSetCollation(hConn2, coll) == 0);

        UNSIGNED8 onm[] = "data";
        ADSHANDLE hT = 0;
        REQUIRE(AdsOpenTable(hConn2, onm, onm, ADS_CDX, 0, 0, 0, 0,
                             &hT) == 0);
        ADSHANDLE hI = 0;
        REQUIRE(AdsGetIndexHandle(hT, (UNSIGNED8*)"BYNAME", &hI) == 0);
        REQUIRE(AdsSetIndexOrderByHandle(hT, hI) == 0);

        // Walk order is PL852 physical order (built at index creation time),
        // even though the current collation is BINARY.
        REQUIRE(AdsGotoTop(hT) == 0);
        CHECK(get_field(hT, "NAME") == "LACAAAAA");
        REQUIRE(AdsSkip(hT, 1) == 0);
        CHECK(static_cast<unsigned char>(get_field(hT, "NAME")[0]) == 0x9D);
        REQUIRE(AdsSkip(hT, 1) == 0);
        CHECK(get_field(hT, "NAME") == "MADCCCCC");
        REQUIRE(AdsSkip(hT, 1) == 0);
        CHECK(get_field(hT, "NAME") == "ZBYDDDDD");

        REQUIRE(AdsCloseTable(hT) == 0);
        REQUIRE(AdsDisconnect(hConn2) == 0);
    }

    REQUIRE(AdsDisconnect(hConn1) == 0);
    fs::remove_all(dir, ec);
}

// ---- Test 2: AdsSetCollation after index open → index NOT retroactive ----

TEST_CASE("AdsSetCollation after index open does NOT retroactively change "
          "index collation") {
    auto dir = fs::temp_directory_path() / "openads_coll_retro";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0,
                         &hConn) == 0);
    // Start with BINARY.
    UNSIGNED8 coll_bin[] = "BINARY";
    REQUIRE(AdsSetCollation(hConn, coll_bin) == 0);

    UNSIGNED8 def[]   = "NAME,C,8,0";
    UNSIGNED8 tname[] = "data";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX,
                           0, 0, 0, 0, def, &hT) == 0);

    // lowercase 'a' < uppercase 'A' in byte order? No: 'A'=0x41 < 'a'=0x61.
    const char* rows[] = { "BBB", "aaa", "AAA" };
    UNSIGNED8 fName[] = "NAME";
    for (const char* row : rows) {
        REQUIRE(AdsAppendRecord(hT) == 0);
        REQUIRE(AdsSetString(hT, fName,
            reinterpret_cast<UNSIGNED8*>(const_cast<char*>(row)),
            static_cast<UNSIGNED32>(std::strlen(row))) == 0);
    }
    REQUIRE(AdsWriteRecord(hT) == 0);

    UNSIGNED8 bag[]  = "data.cdx";
    UNSIGNED8 tag[]  = "BYNAME";
    UNSIGNED8 expr[] = "NAME";
    ADSHANDLE hI = 0;
    REQUIRE(AdsCreateIndex61(hT, bag, tag, expr, nullptr, nullptr,
                             0, 0, &hI) == 0);

    // Index was built under BINARY: order = AAA, BBB, aaa.
    REQUIRE(AdsSetIndexOrderByHandle(hT, hI) == 0);
    REQUIRE(AdsGotoTop(hT) == 0);
    CHECK(get_field(hT, "NAME") == "AAA");
    REQUIRE(AdsSkip(hT, 1) == 0);
    CHECK(get_field(hT, "NAME") == "BBB");
    REQUIRE(AdsSkip(hT, 1) == 0);
    CHECK(get_field(hT, "NAME") == "aaa");

    // Now change collation to NOCASE — should NOT affect the already-open
    // index (oem_sort_ was stamped at open/create time).
    UNSIGNED8 coll_noc[] = "NOCASE";
    REQUIRE(AdsSetCollation(hConn, coll_noc) == 0);

    // The walk order must remain unchanged — the index was built binary.
    REQUIRE(AdsGotoTop(hT) == 0);
    CHECK(get_field(hT, "NAME") == "AAA");
    REQUIRE(AdsSkip(hT, 1) == 0);
    CHECK(get_field(hT, "NAME") == "BBB");
    REQUIRE(AdsSkip(hT, 1) == 0);
    CHECK(get_field(hT, "NAME") == "aaa");

    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

// ---- Test 3: Default OEM collation changes between two opens ----

TEST_CASE("default OEM collation change between two opens") {
    auto dir = fs::temp_directory_path() / "openads_coll_default_change";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    // Create table with PL852 default.
    {
        auto _collation_guard = openads::engine::set_default_oem_collation("NTXPL852");
        (void)_collation_guard;

        UNSIGNED8 srv[256];
        std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
        ADSHANDLE hConn = 0;
        REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0,
                             &hConn) == 0);

        UNSIGNED8 def[]   = "NAME,C,8,0";
        UNSIGNED8 tname[] = "data";
        ADSHANDLE hT = 0;
        REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX,
                               ADS_OEM, 0, 0, 0, def, &hT) == 0);

        const char* rows[] = {
            "LACAAAAA", "\x9D""ABBBBB", "MADCCCCC" };
        UNSIGNED8 fName[] = "NAME";
        for (const char* row : rows) {
            REQUIRE(AdsAppendRecord(hT) == 0);
            REQUIRE(AdsSetString(hT, fName,
                reinterpret_cast<UNSIGNED8*>(const_cast<char*>(row)),
                static_cast<UNSIGNED32>(std::strlen(row))) == 0);
        }
        REQUIRE(AdsWriteRecord(hT) == 0);

        make_tag(hT, "BYNAME", "NAME");
        REQUIRE(AdsCloseTable(hT) == 0);
        REQUIRE(AdsDisconnect(hConn) == 0);
    }

    // Open with BINARY default (clear the PL852 default).
    {
        openads::engine::set_default_oem_collation("");

        UNSIGNED8 srv[256];
        std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
        ADSHANDLE hConn = 0;
        REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0,
                             &hConn) == 0);

        UNSIGNED8 onm[] = "data";
        ADSHANDLE hT = 0;
        REQUIRE(AdsOpenTable(hConn, onm, onm, ADS_CDX, ADS_OEM, 0, 0, 0,
                             &hT) == 0);
        ADSHANDLE hI = 0;
        REQUIRE(AdsGetIndexHandle(hT, (UNSIGNED8*)"BYNAME", &hI) == 0);
        REQUIRE(AdsSetIndexOrderByHandle(hT, hI) == 0);

        // Walk order is PL852 physical order (built at index creation time),
        // even though the current collation is BINARY.
        REQUIRE(AdsGotoTop(hT) == 0);
        CHECK(get_field(hT, "NAME") == "LACAAAAA");
        REQUIRE(AdsSkip(hT, 1) == 0);
        CHECK(static_cast<unsigned char>(get_field(hT, "NAME")[0]) == 0x9D);
        REQUIRE(AdsSkip(hT, 1) == 0);
        CHECK(get_field(hT, "NAME") == "MADCCCCC");

        REQUIRE(AdsCloseTable(hT) == 0);
        REQUIRE(AdsDisconnect(hConn) == 0);
    }

    fs::remove_all(dir, ec);
}

// ---- Test 4: Reopen table with ADS_OEM: collation re-derived from char_type ----

TEST_CASE("reopen ADS_OEM table: collation re-derived from char_type") {
    auto dir = fs::temp_directory_path() / "openads_coll_reopen_oem";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    auto _collation_guard2 = openads::engine::set_default_oem_collation("NTXPL852");
    (void)_collation_guard2;

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0,
                         &hConn) == 0);

    UNSIGNED8 def[]   = "NAME,C,8,0";
    UNSIGNED8 tname[] = "data";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX,
                           ADS_OEM, 0, 0, 0, def, &hT) == 0);

    const char* rows[] = {
        "LACAAAAA", "\x9D""ABBBBB", "MADCCCCC", "ZBYDDDDD" };
    UNSIGNED8 fName[] = "NAME";
    for (const char* row : rows) {
        REQUIRE(AdsAppendRecord(hT) == 0);
        REQUIRE(AdsSetString(hT, fName,
            reinterpret_cast<UNSIGNED8*>(const_cast<char*>(row)),
            static_cast<UNSIGNED32>(std::strlen(row))) == 0);
    }
    REQUIRE(AdsWriteRecord(hT) == 0);

    make_tag(hT, "BYNAME", "NAME");

    // First open: PL852 default active.
    {
        UNSIGNED8 onm[] = "data";
        ADSHANDLE hT2 = 0;
        REQUIRE(AdsOpenTable(hConn, onm, onm, ADS_CDX, ADS_OEM, 0, 0, 0,
                             &hT2) == 0);
        ADSHANDLE hI = 0;
        REQUIRE(AdsGetIndexHandle(hT2, (UNSIGNED8*)"BYNAME", &hI) == 0);
        REQUIRE(AdsSetIndexOrderByHandle(hT2, hI) == 0);

        REQUIRE(AdsGotoTop(hT2) == 0);
        CHECK(get_field(hT2, "NAME") == "LACAAAAA");
        REQUIRE(AdsSkip(hT2, 1) == 0);
        CHECK(static_cast<unsigned char>(get_field(hT2, "NAME")[0]) == 0x9D);

        REQUIRE(AdsCloseTable(hT2) == 0);
    }

    // Clear the default, reopen — binary order expected.
    openads::engine::set_default_oem_collation("");
    {
        UNSIGNED8 onm[] = "data";
        ADSHANDLE hT2 = 0;
        REQUIRE(AdsOpenTable(hConn, onm, onm, ADS_CDX, ADS_OEM, 0, 0, 0,
                             &hT2) == 0);
        ADSHANDLE hI = 0;
        REQUIRE(AdsGetIndexHandle(hT2, (UNSIGNED8*)"BYNAME", &hI) == 0);
        REQUIRE(AdsSetIndexOrderByHandle(hT2, hI) == 0);

        // Walk order is PL852 physical order (built at index creation time),
        // even though the current collation is BINARY.
        REQUIRE(AdsGotoTop(hT2) == 0);
        CHECK(get_field(hT2, "NAME") == "LACAAAAA");
        REQUIRE(AdsSkip(hT2, 1) == 0);
        CHECK(static_cast<unsigned char>(get_field(hT2, "NAME")[0]) == 0x9D);
        REQUIRE(AdsSkip(hT2, 1) == 0);
        CHECK(get_field(hT2, "NAME") == "MADCCCCC");
        REQUIRE(AdsSkip(hT2, 1) == 0);
        CHECK(get_field(hT2, "NAME") == "ZBYDDDDD");

        REQUIRE(AdsCloseTable(hT2) == 0);
    }

    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}
