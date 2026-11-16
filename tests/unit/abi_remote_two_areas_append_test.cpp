// One program, the same table open in two work areas over TCP. Field
// report: "appending records in one area is not visible in the 2nd area".
// Two defects:
//  1. Client: each RemoteTable caches its record / key counts and dropped
//     them only on its OWN writes, so area 2's LastRec()/OrdKeyCount()
//     kept the old value after area 1 appended.
//  2. Server: the client's AdsWriteRecord (FlushTable) only flushed the
//     ABI twin, never committed it, so a BARE append (no field writes)
//     never got index keys -- no index walk in any area ever saw it.
#include "doctest.h"
#include "network/server.h"
#include "openads/ace.h"

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

ADSHANDLE connect_to(const std::string& where, UNSIGNED16 type) {
    std::vector<UNSIGNED8> buf(where.begin(), where.end());
    buf.push_back(0);
    ADSHANDLE h = 0;
    REQUIRE(AdsConnect60(buf.data(), type, nullptr, nullptr, 0, &h) == 0);
    return h;
}

ADSHANDLE open_table(ADSHANDLE conn) {
    UNSIGNED8 name[] = "areas.dbf";
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
    UNSIGNED8 tname[]  = "areas.dbf";
    ADSHANDLE t = 0;
    REQUIRE(AdsCreateTable(conn, tname, nullptr, ADS_CDX, 0, 0, 0, 64,
                           fields, &t) == 0);
    for (int i = 0; i < 50; ++i) {
        REQUIRE(AdsAppendRecord(t) == 0);
        UNSIGNED8 fN[] = "NAME";
        const std::string v = "N" + std::to_string(100 + i);
        AdsSetString(t, fN, (UNSIGNED8*)v.c_str(), static_cast<UNSIGNED32>(v.size()));
        REQUIRE(AdsWriteRecord(t) == 0);
    }
    ADSHANDLE hi = 0;
    UNSIGNED8 bag[] = "areas.cdx", tag[] = "TNAME", expr[] = "NAME";
    REQUIRE(AdsCreateIndex61(t, bag, tag, expr, nullptr, nullptr,
                             ADS_COMPOUND, 512, &hi) == 0);
    REQUIRE(AdsCloseTable(t) == 0);
    REQUIRE(AdsDisconnect(conn) == 0);
}

UNSIGNED32 recno_of(ADSHANDLE t) {
    UNSIGNED32 r = 0;
    AdsGetRecordNum(t, ADS_IGNOREFILTERS, &r);
    return r;
}

bool index_walk_sees(ADSHANDLE t, ADSHANDLE ix, UNSIGNED32 want) {
    REQUIRE(AdsGotoTop(ix) == 0);
    for (int guard = 0; guard < 1000; ++guard) {
        UNSIGNED16 eof = 0;
        AdsAtEOF(t, &eof);
        if (eof) return false;
        if (recno_of(t) == want) return true;
        REQUIRE(AdsSkip(ix, 1) == 0);
    }
    return false;
}

} // namespace

TEST_CASE("remote: an append in one work area is visible in another") {
    const auto dir = fs::temp_directory_path() / "openads_two_areas_append";
    make_table(dir);

    openads::network::Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    const std::string uri = "tcp://127.0.0.1:" + std::to_string(srv.port()) +
                            "/" + dir.generic_string();
    ADSHANDLE c = connect_to(uri, ADS_REMOTE_SERVER);
    ADSHANDLE a1 = open_table(c), a2 = open_table(c);
    ADSHANDLE ix1 = 0, ix2 = 0;
    REQUIRE(AdsGetIndexHandleByOrder(a1, 1, &ix1) == 0);
    REQUIRE(AdsGetIndexHandleByOrder(a2, 1, &ix2) == 0);

    // Area 2 browses by index first, warming every count cache it has.
    REQUIRE(AdsGotoTop(ix2) == 0);
    for (int i = 0; i < 10; ++i) REQUIRE(AdsSkip(ix2, 1) == 0);
    UNSIGNED32 cnt = 0, keys = 0;
    REQUIRE(AdsGetRecordCount(a2, ADS_IGNOREFILTERS, &cnt) == 0);
    REQUIRE(AdsGetKeyCount(ix2, ADS_IGNOREFILTERS, &keys) == 0);
    REQUIRE(cnt == 50);
    REQUIRE(keys == 50);

    SUBCASE("bare append (no field written)") {
        REQUIRE(AdsAppendRecord(a1) == 0);
        const UNSIGNED32 nr = recno_of(a1);
        REQUIRE(nr == 51);
        REQUIRE(AdsWriteRecord(a1) == 0);
        REQUIRE(AdsUnlockRecord(a1, 0) == 0);

        // dballcmp "two areas": B navigates first, then reads LastRec().
        REQUIRE(AdsGotoTop(ix2) == 0);
        CHECK(AdsGetRecordCount(a2, ADS_IGNOREFILTERS, &cnt) == 0);
        CHECK(cnt == 51);
        CHECK(AdsGetKeyCount(ix2, ADS_IGNOREFILTERS, &keys) == 0);
        CHECK(keys == 51);
        CHECK(index_walk_sees(a2, ix2, nr));
        // The blank key sorts first: index top in area 2 is the new row.
        REQUIRE(AdsGotoTop(ix2) == 0);
        CHECK(recno_of(a2) == nr);
    }

    SUBCASE("append with a value (APPEND BLANK + REPLACE)") {
        REQUIRE(AdsAppendRecord(a1) == 0);
        const UNSIGNED32 nr = recno_of(a1);
        UNSIGNED8 fN[] = "NAME", v[] = "ZZZ";
        REQUIRE(AdsSetString(a1, fN, v, 3) == 0);
        REQUIRE(AdsWriteRecord(a1) == 0);
        REQUIRE(AdsUnlockRecord(a1, 0) == 0);

        CHECK(AdsGetRecordCount(a2, ADS_IGNOREFILTERS, &cnt) == 0);
        CHECK(cnt == 51);
        CHECK(AdsGetKeyCount(ix2, ADS_IGNOREFILTERS, &keys) == 0);
        CHECK(keys == 51);
        REQUIRE(AdsGotoBottom(ix2) == 0);
        CHECK(recno_of(a2) == nr);
    }

    AdsCloseTable(a1);
    AdsCloseTable(a2);
    AdsDisconnect(c);

    // The bare-append key reached the CDX file itself: a fresh local
    // open sees 51 keys.
    {
        ADSHANDLE cl = connect_to(dir.string(), ADS_LOCAL_SERVER);
        ADSHANDLE tl = open_table(cl), il = 0;
        REQUIRE(AdsGetIndexHandleByOrder(tl, 1, &il) == 0);
        UNSIGNED32 k = 0;
        CHECK(AdsGetKeyCount(il, ADS_IGNOREFILTERS, &k) == 0);
        CHECK(k == 51);
        AdsCloseTable(tl);
        AdsDisconnect(cl);
    }
    srv.stop();
    std::error_code ec;
    fs::remove_all(dir, ec);
}

// START.exe pattern (AC / T_AC44 trace, 2026-11-15): APPEND BLANK, REPLACE
// fields, NO dbCommit -- the next Seek commits the record (GoCold) -- then
// UNLOCK. The keys went into area 1's CDX page cache only and were never
// written on unlock, so a seek in area 2 did not find the new record.
static void commit_by_navigation_case(ADSHANDLE c) {
    ADSHANDLE a1 = open_table(c), a2 = open_table(c);
    ADSHANDLE ix1 = 0, ix2 = 0;
    REQUIRE(AdsGetIndexHandleByOrder(a1, 1, &ix1) == 0);
    REQUIRE(AdsGetIndexHandleByOrder(a2, 1, &ix2) == 0);
    UNSIGNED8 key[] = "NEWCODE";
    UNSIGNED16 found = 1;
    REQUIRE(AdsSeek(ix2, key, 7, ADS_STRINGKEY, ADS_HARDSEEK, &found) == 0);
    REQUIRE(found == 0);

    REQUIRE(AdsAppendRecord(a1) == 0);
    const UNSIGNED32 nr = recno_of(a1);
    UNSIGNED8 fN[] = "NAME";
    REQUIRE(AdsSetString(a1, fN, key, 7) == 0);
    UNSIGNED8 other[] = "N100";
    REQUIRE(AdsSeek(ix1, other, 4, ADS_STRINGKEY, ADS_HARDSEEK, &found) == 0);  // commits
    REQUIRE(AdsUnlockRecord(a1, nr) == 0);

    found = 0;
    CHECK(AdsSeek(ix2, key, 7, ADS_STRINGKEY, ADS_HARDSEEK, &found) == 0);
    CHECK(found == 1);
    CHECK(recno_of(a2) == nr);
    AdsCloseTable(a1);
    AdsCloseTable(a2);
}

TEST_CASE("an append committed by navigation is visible in another area after unlock") {
    const auto dir = fs::temp_directory_path() / "openads_two_areas_nav_commit";
    SUBCASE("local") {
        make_table(dir);
        ADSHANDLE c = connect_to(dir.string(), ADS_LOCAL_SERVER);
        commit_by_navigation_case(c);
        AdsDisconnect(c);
    }
    SUBCASE("remote") {
        make_table(dir);
        openads::network::Server srv;
        REQUIRE(srv.start("127.0.0.1", 0).has_value());
        ADSHANDLE c = connect_to("tcp://127.0.0.1:" + std::to_string(srv.port()) +
                                 "/" + dir.generic_string(), ADS_REMOTE_SERVER);
        commit_by_navigation_case(c);
        AdsDisconnect(c);
        srv.stop();
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
}
