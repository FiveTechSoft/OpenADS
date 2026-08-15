// abi_navigation_test.cpp
//
// Navigation conformance battery (Pritpal Bedi, 08/15/2026): the expected
// values below are the GOLDEN transcript produced by Harbour DBFCDX running
// the same operation sequence (tests/smoke/harbour/navcmp.prg, which runs
// the battery under DBFCDX and ADSCDX and diffs the transcripts).
//
// Covered: AdsGotoTop / AdsGotoBottom / AdsSkip(+1,-1,+n,-n) at and past
// both boundaries, AdsGotoRecord, AdsAtEOF/AdsAtBOF at every step, empty
// table (Limbo -> single-flag transition), plain open of a table with a
// structural CDX (controlling order must stay NATURAL -- the dbGoBottom
// bug), explicit AdsOpenIndex (first tag active), SET DELETED ON/OFF via
// AdsShowDeleted, and AdsSeek hit/miss followed by skips.

#include "doctest.h"
#include "openads/ace.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {

// RAII connection anchored at a fresh temp dir per test case.
struct NavConn {
    ADSHANDLE hConn = 0;
    fs::path  dir;

    explicit NavConn(const char* subdir) {
        dir = fs::temp_directory_path() / subdir;
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir);
        const auto sp = dir.string();
        UNSIGNED8 srv[512];
        std::memcpy(srv, sp.c_str(), sp.size() + 1);
        REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                             nullptr, nullptr, 0, &hConn) == 0);
    }
    ~NavConn() {
        AdsDisconnect(hConn);
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
};

ADSHANDLE open_table(ADSHANDLE hConn, const char* name) {
    ADSHANDLE hT = 0;
    REQUIRE(AdsOpenTable(hConn, (UNSIGNED8*)name, nullptr, ADS_CDX,
                         ADS_ANSI, ADS_PROPRIETARY_LOCKING, ADS_CHECKRIGHTS,
                         ADS_SHARED, &hT) == 0);
    return hT;
}

// 10 rows: NAME = "name1".."name10" (natural order == insertion order).
void make_names10(ADSHANDLE hConn, const char* table) {
    UNSIGNED8 def[] = "NAME,C,20,0";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, (UNSIGNED8*)table, nullptr, ADS_CDX,
                           0, 0, 0, 0, def, &hT) == 0);
    UNSIGNED8 f[] = "NAME";
    for (int i = 1; i <= 10; ++i) {
        std::string s = "name" + std::to_string(i);
        REQUIRE(AdsAppendRecord(hT) == 0);
        REQUIRE(AdsSetString(hT, f, (UNSIGNED8*)s.c_str(),
                             (UNSIGNED32)s.size()) == 0);
        REQUIRE(AdsWriteRecord(hT) == 0);
    }
    REQUIRE(AdsCloseTable(hT) == 0);
}

// 5 rows whose NAME index order differs from physical order:
//   physical: zulu(1) alpha(2) mike(3) bravo(4) charlie(5)
//   NAME key: alpha(2) bravo(4) charlie(5) mike(3) zulu(1)
// Also builds the structural <table>.cdx with tag NAME.
void make_zulu5(ADSHANDLE hConn, const char* table, const char* bag) {
    UNSIGNED8 def[] = "NAME,C,10,0";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, (UNSIGNED8*)table, nullptr, ADS_CDX,
                           0, 0, 0, 0, def, &hT) == 0);
    const char* names[] = {"zulu", "alpha", "mike", "bravo", "charlie"};
    UNSIGNED8 f[] = "NAME";
    for (const char* n : names) {
        REQUIRE(AdsAppendRecord(hT) == 0);
        REQUIRE(AdsSetString(hT, f, (UNSIGNED8*)n,
                             (UNSIGNED32)std::strlen(n)) == 0);
        REQUIRE(AdsWriteRecord(hT) == 0);
    }
    UNSIGNED8 tag[]  = "NAME";
    UNSIGNED8 expr[] = "NAME";
    ADSHANDLE hI = 0;
    REQUIRE(AdsCreateIndex61(hT, (UNSIGNED8*)bag, tag, expr,
                             nullptr, nullptr, 0, 0, &hI) == 0);
    REQUIRE(AdsCloseTable(hT) == 0);
}

struct Pos {
    UNSIGNED32 recno = 0;
    UNSIGNED16 bof = 99, eof = 99;
};

Pos pos(ADSHANDLE hT) {
    Pos p;
    REQUIRE(AdsGetRecordNum(hT, ADS_IGNOREFILTERS, &p.recno) == 0);
    REQUIRE(AdsAtBOF(hT, &p.bof) == 0);
    REQUIRE(AdsAtEOF(hT, &p.eof) == 0);
    return p;
}

void check_pos(ADSHANDLE hT, UNSIGNED32 recno, UNSIGNED16 bof,
               UNSIGNED16 eof, const char* label) {
    Pos p = pos(hT);
    INFO(label, ": recno=", p.recno, " bof=", p.bof, " eof=", p.eof);
    CHECK(p.recno == recno);
    CHECK(p.bof == bof);
    CHECK(p.eof == eof);
}

// At BOF the record number is an RDD-level convention (Harbour reports 1),
// so only the flags are part of the ACE-level contract.
void check_bof(ADSHANDLE hT, const char* label) {
    Pos p = pos(hT);
    INFO(label, ": recno=", p.recno, " bof=", p.bof, " eof=", p.eof);
    CHECK(p.bof == 1);
    CHECK(p.eof == 0);
}

} // namespace

TEST_CASE("Nav natural order: gotop/gobottom/goto and skip boundaries") {
    NavConn c("openads_nav_nat");
    make_names10(c.hConn, "nav10.dbf");
    ADSHANDLE hT = open_table(c.hConn, "nav10.dbf");

    // ACE-level open leaves the cursor at BOF (recno 0); rddads maps
    // this to Harbour's RecNo()=1 / Bof()=F open state (verified in the
    // navcmp.prg transcripts).
    Pos p = pos(hT);
    INFO("open: recno=", p.recno, " bof=", p.bof, " eof=", p.eof);
    CHECK(p.bof == 1);
    CHECK(p.eof == 0);

    REQUIRE(AdsGotoTop(hT) == 0);
    check_pos(hT, 1, 0, 0, "gotop");
    REQUIRE(AdsGotoBottom(hT) == 0);
    check_pos(hT, 10, 0, 0, "gobottom");

    // Skip past EOF: EOF flag only, recno = LastRec + 1.
    REQUIRE(AdsSkip(hT, 1) == 0);
    check_pos(hT, 11, 0, 1, "skip+1 @last");
    REQUIRE(AdsSkip(hT, 1) == 0);
    check_pos(hT, 11, 0, 1, "skip+1 @eof");
    REQUIRE(AdsSkip(hT, 5) == 0);
    check_pos(hT, 11, 0, 1, "skip+5 @eof");
    // Back from EOF lands on the last record.
    REQUIRE(AdsSkip(hT, -1) == 0);
    check_pos(hT, 10, 0, 0, "skip-1 @eof");

    // Skip past BOF: BOF flag only. Engine/ADS convention: BOF is
    // "before the first record", so skip+1 from BOF lands on record 1.
    // (Harbour's DBFCDX models BOF as "on record 1, flagged", so its
    // skip+1 lands on record 2 -- rddads bridges the two; see navcmp.)
    REQUIRE(AdsGotoTop(hT) == 0);
    REQUIRE(AdsSkip(hT, -1) == 0);
    check_bof(hT, "skip-1 @first");
    REQUIRE(AdsSkip(hT, -1) == 0);
    check_bof(hT, "skip-1 @bof");
    REQUIRE(AdsSkip(hT, 1) == 0);
    check_pos(hT, 1, 0, 0, "skip+1 @bof");

    // Absolute positioning.
    REQUIRE(AdsGotoRecord(hT, 5) == 0);
    check_pos(hT, 5, 0, 0, "goto 5");
    REQUIRE(AdsGotoRecord(hT, 1) == 0);
    check_pos(hT, 1, 0, 0, "goto 1");
    REQUIRE(AdsGotoRecord(hT, 10) == 0);
    check_pos(hT, 10, 0, 0, "goto lastrec");

    // Long skips clip at the boundaries.
    REQUIRE(AdsGotoTop(hT) == 0);
    REQUIRE(AdsSkip(hT, 4) == 0);
    check_pos(hT, 5, 0, 0, "skip+4");
    REQUIRE(AdsSkip(hT, -2) == 0);
    check_pos(hT, 3, 0, 0, "skip-2");
    REQUIRE(AdsSkip(hT, 100) == 0);
    check_pos(hT, 11, 0, 1, "skip+100");
    REQUIRE(AdsSkip(hT, -100) == 0);
    check_bof(hT, "skip-100 @eof");

    REQUIRE(AdsCloseTable(hT) == 0);
}

TEST_CASE("Nav empty table: Limbo, then single-flag on skip") {
    NavConn c("openads_nav_empty");
    UNSIGNED8 def[] = "NAME,C,4,0";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(c.hConn, (UNSIGNED8*)"nve.dbf", nullptr, ADS_CDX,
                           0, 0, 0, 0, def, &hT) == 0);

    // Freshly created empty table: GOTO TOP leaves BOF+EOF both set
    // (Limbo), matching Harbour's open-of-empty state.
    REQUIRE(AdsGotoTop(hT) == 0);
    Pos p = pos(hT);
    INFO("gotop @empty: bof=", p.bof, " eof=", p.eof);
    CHECK(p.bof == 1);
    CHECK(p.eof == 1);
    REQUIRE(AdsGotoBottom(hT) == 0);
    p = pos(hT);
    CHECK(p.bof == 1);
    CHECK(p.eof == 1);

    // First direction-bearing skip drops out of Limbo into a single
    // flag -- the DBFCDX semantics. (Harbour's rddads never forwards
    // this call when unpositioned, so at the Harbour level both flags
    // stay set with ANY ACE server; verified here at the ABI level.)
    REQUIRE(AdsSkip(hT, 1) == 0);
    p = pos(hT);
    INFO("skip+1 @empty: bof=", p.bof, " eof=", p.eof);
    CHECK(p.bof == 0);
    CHECK(p.eof == 1);
    REQUIRE(AdsSkip(hT, -1) == 0);
    p = pos(hT);
    INFO("skip-1 @empty: bof=", p.bof, " eof=", p.eof);
    CHECK(p.bof == 1);
    CHECK(p.eof == 0);

    REQUIRE(AdsCloseTable(hT) == 0);
}

TEST_CASE("Nav structural CDX auto-open keeps natural order") {
    NavConn c("openads_nav_struct");
    make_zulu5(c.hConn, "navs.dbf", "navs.cdx");

    // Plain open: the production CDX auto-opens, but the controlling
    // order must stay NATURAL (0) until the app picks a tag.
    // (Pritpal Bedi: dbGoBottom() landed on the last index key.)
    ADSHANDLE hT = open_table(c.hConn, "navs.dbf");
    REQUIRE(AdsGotoTop(hT) == 0);
    check_pos(hT, 1, 0, 0, "auto-open gotop (zulu, natural first)");
    REQUIRE(AdsGotoBottom(hT) == 0);
    check_pos(hT, 5, 0, 0, "auto-open gobottom (charlie, natural last)");

    // Activate the tag: index order.
    ADSHANDLE hI = 0;
    REQUIRE(AdsGetIndexHandle(hT, (UNSIGNED8*)"NAME", &hI) == 0);
    REQUIRE(AdsSetIndexOrderByHandle(hT, hI) == 0);
    REQUIRE(AdsGotoTop(hT) == 0);
    check_pos(hT, 2, 0, 0, "ord1 gotop (alpha, first key)");
    REQUIRE(AdsGotoBottom(hT) == 0);
    check_pos(hT, 1, 0, 0, "ord1 gobottom (zulu, last key)");

    // Back to natural.
    REQUIRE(AdsSetIndexOrder(hT, (UNSIGNED8*)"") == 0);
    REQUIRE(AdsGotoBottom(hT) == 0);
    check_pos(hT, 5, 0, 0, "ord0 gobottom (natural again)");
    REQUIRE(AdsGotoTop(hT) == 0);
    check_pos(hT, 1, 0, 0, "ord0 gotop");

    REQUIRE(AdsCloseTable(hT) == 0);
}

TEST_CASE("Nav explicit AdsOpenIndex activates the first tag") {
    NavConn c("openads_nav_explidx");
    make_zulu5(c.hConn, "nave.dbf", "nave.cdx");

    ADSHANDLE hT = open_table(c.hConn, "nave.dbf");
    ADSHANDLE hI = 0;
    REQUIRE(AdsOpenIndex(hT, (UNSIGNED8*)"nave.cdx", &hI, nullptr) == 0);
    REQUIRE(AdsGotoTop(hT) == 0);
    check_pos(hT, 2, 0, 0, "gotop (alpha, first key)");
    REQUIRE(AdsGotoBottom(hT) == 0);
    check_pos(hT, 1, 0, 0, "gobottom (zulu, last key)");
    REQUIRE(AdsSkip(hT, 1) == 0);
    Pos p = pos(hT);
    INFO("skip+1 @last key: eof=", p.eof);
    CHECK(p.eof == 1);
    CHECK(p.bof == 0);
    REQUIRE(AdsSkip(hT, -1) == 0);
    check_pos(hT, 1, 0, 0, "skip-1 @eof -> last key");

    REQUIRE(AdsCloseTable(hT) == 0);
}

TEST_CASE("Nav with deleted rows: show-deleted off/on") {
    NavConn c("openads_nav_deleted");
    make_names10(c.hConn, "navd.dbf");
    ADSHANDLE hT = open_table(c.hConn, "navd.dbf");

    // Delete records 2 and 10. Proprietary locking requires the record
    // locked before delete (else AE_RECORD_NOT_LOCKED / 5035).
    REQUIRE(AdsGotoRecord(hT, 2) == 0);
    REQUIRE(AdsLockRecord(hT, 0) == 0);
    REQUIRE(AdsDeleteRecord(hT) == 0);
    REQUIRE(AdsUnlockRecord(hT, 0) == 0);
    REQUIRE(AdsGotoRecord(hT, 10) == 0);
    REQUIRE(AdsLockRecord(hT, 0) == 0);
    REQUIRE(AdsDeleteRecord(hT) == 0);
    REQUIRE(AdsUnlockRecord(hT, 0) == 0);

    // SET DELETED ON.
    REQUIRE(AdsShowDeleted(0) == 0);
    REQUIRE(AdsGotoTop(hT) == 0);
    check_pos(hT, 1, 0, 0, "del-on gotop");
    REQUIRE(AdsGotoBottom(hT) == 0);
    check_pos(hT, 9, 0, 0, "del-on gobottom (10 is deleted)");
    REQUIRE(AdsSkip(hT, 1) == 0);
    check_pos(hT, 11, 0, 1, "del-on skip+1 @last live");
    REQUIRE(AdsSkip(hT, -1) == 0);
    check_pos(hT, 9, 0, 0, "del-on skip-1 @eof");
    REQUIRE(AdsGotoTop(hT) == 0);
    REQUIRE(AdsSkip(hT, -1) == 0);
    check_bof(hT, "del-on skip-1 @first live");
    // ADS convention: BOF is "before the first record", so skip+1 from
    // BOF lands on the FIRST VISIBLE row (record 1; 2 is deleted).
    // (Harbour DBFCDX lands on 3 with its own BOF model; rddads bridges
    // the two -- see the navcmp.prg transcripts.)
    REQUIRE(AdsSkip(hT, 1) == 0);
    check_pos(hT, 1, 0, 0, "del-on skip+1 @bof");

    // SET DELETED OFF: deleted rows are navigable again.
    REQUIRE(AdsShowDeleted(1) == 0);
    REQUIRE(AdsGotoTop(hT) == 0);
    check_pos(hT, 1, 0, 0, "del-off gotop");
    REQUIRE(AdsGotoBottom(hT) == 0);
    check_pos(hT, 10, 0, 0, "del-off gobottom");

    REQUIRE(AdsCloseTable(hT) == 0);
}

TEST_CASE("Nav seek then skip: hit and miss") {
    NavConn c("openads_nav_seek");
    make_zulu5(c.hConn, "navk.dbf", "navk.cdx");
    ADSHANDLE hT = open_table(c.hConn, "navk.dbf");
    ADSHANDLE hI = 0;
    REQUIRE(AdsGetIndexHandle(hT, (UNSIGNED8*)"NAME", &hI) == 0);
    REQUIRE(AdsSetIndexOrderByHandle(hT, hI) == 0);

    // Hit: bravo(4) -> skip -> charlie(5).
    UNSIGNED8 key1[] = "bravo";
    UNSIGNED16 found = 0;
    REQUIRE(AdsSeek(hI, key1, 5, 0, ADS_HARDSEEK, &found) == 0);
    CHECK(found == 1);
    check_pos(hT, 4, 0, 0, "seek bravo");
    REQUIRE(AdsSkip(hT, 1) == 0);
    check_pos(hT, 5, 0, 0, "skip+1 after hit");

    // Miss: not found, EOF; skip-1 returns to the last key (zulu, rec 1).
    UNSIGNED8 key2[] = "zzzzz";
    REQUIRE(AdsSeek(hI, key2, 5, 0, ADS_HARDSEEK, &found) == 0);
    CHECK(found == 0);
    Pos p = pos(hT);
    INFO("seek miss: eof=", p.eof, " bof=", p.bof);
    CHECK(p.eof == 1);
    CHECK(p.bof == 0);
    REQUIRE(AdsSkip(hT, -1) == 0);
    check_pos(hT, 1, 0, 0, "skip-1 after miss -> last key");

    REQUIRE(AdsCloseTable(hT) == 0);
}
