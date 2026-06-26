// abi_cdx_recreate_tag_diff_expr_test.cpp
//
// Harbour / Clipper "re-filter on another column" idiom: a program reuses a
// single temporary tag name (e.g. ORDERX) and re-runs INDEX ON <other column>
// TAG ORDERX every time the user picks a different sort column, WITHOUT first
// deleting the .cdx. Native DBFCDX (src/rdd/dbfcdx/dbfcdx1.c, hb_cdxOrderCreate)
// handles this by DELETING the old tag wholesale (hb_cdxIndexDelTag) and adding
// a fresh one, so the tag's stored key expression, FOR clause and tree all
// follow the new column.
//
// OpenADS' AdsCreateIndex61 took a shortcut for the "tag already exists" case:
// it cleared the B+tree (clear_data) and rewrote the unique/descend/keysize
// options (set_options) but NEVER rewrote the stored key EXPRESSION. The freshly
// built tree carried the new column's keys, so a read straight after the
// re-create looked fine — but the on-disk expression stayed the OLD column, so
// AdsGetIndexExpr reported the wrong column and the next dbAppend/dbReplace
// (sync_all_indexes_ evaluates idx->expression()) inserted a key computed from
// the OLD column, silently disordering the index. This is the "al cambiar de
// columna la lista se desordenaba" symptom.

#include "doctest.h"
#include "openads/ace.h"
#include "drivers/cdx/cdx_index.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {
void set8(ADSHANDLE hT, const char* field, const char* val) {
    UNSIGNED8 f[ADS_MAX_TAG_NAME + 1] = {0};
    std::memcpy(f, field, std::strlen(field) + 1);
    char buf[9];
    std::snprintf(buf, sizeof(buf), "%-8.8s", val);   // left-justified, 8 wide
    REQUIRE(AdsSetString(hT, f, reinterpret_cast<UNSIGNED8*>(buf), 8) == 0);
}

// Walk the active order top-to-bottom, returning the recnos in visit order.
std::vector<UNSIGNED32> walk(ADSHANDLE hT) {
    std::vector<UNSIGNED32> out;
    REQUIRE(AdsGotoTop(hT) == 0);
    for (;;) {
        UNSIGNED16 eof = 0;
        REQUIRE(AdsAtEOF(hT, &eof) == 0);
        if (eof) break;
        UNSIGNED32 rec = 0;
        REQUIRE(AdsGetRecordNum(hT, 0, &rec) == 0);
        out.push_back(rec);
        REQUIRE(AdsSkip(hT, 1) == 0);
    }
    return out;
}

std::string index_expr(ADSHANDLE hT, const char* tag) {
    UNSIGNED8 name[ADS_MAX_TAG_NAME + 1] = {0};
    std::memcpy(name, tag, std::strlen(tag) + 1);
    ADSHANDLE hIdx = 0;
    REQUIRE(AdsGetIndexHandle(hT, name, &hIdx) == 0);
    UNSIGNED8 buf[256] = {0};
    UNSIGNED16 len = sizeof(buf);
    REQUIRE(AdsGetIndexExpr(hIdx, buf, &len) == 0);
    return std::string(reinterpret_cast<char*>(buf));
}
} // namespace

TEST_CASE("CDX re-create existing tag on a different column follows the new column") {
    auto dir = fs::temp_directory_path() / "openads_cdx_recreate_diffexpr";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    const std::string dir_str = dir.string();
    std::vector<UNSIGNED8> srv(dir_str.begin(), dir_str.end());
    srv.push_back(0);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv.data(), ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);

    // Two char columns whose ascending orders are the REVERSE of each other,
    // so a tag built on K1 vs K2 must visit records in opposite order.
    UNSIGNED8 def[]   = "K1,C,8,0;K2,C,8,0";
    UNSIGNED8 tname[] = "refilt";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX,
                           0, 0, 0, 0, def, &hT) == 0);

    // rec1: K1=A1 K2=C3 | rec2: K1=B2 K2=B2 | rec3: K1=C3 K2=A1
    //   K1 asc -> 1,2,3   ;   K2 asc -> 3,2,1
    const char* k1[] = {"A1", "B2", "C3"};
    const char* k2[] = {"C3", "B2", "A1"};
    for (int i = 0; i < 3; ++i) {
        REQUIRE(AdsAppendRecord(hT) == 0);
        set8(hT, "K1", k1[i]);
        set8(hT, "K2", k2[i]);
    }

    UNSIGNED8 bag[]   = "refilt.cdx";
    UNSIGNED8 tag[]   = "ORDERX";
    UNSIGNED8 exK1[]  = "K1", exK2[] = "K2";

    // 1) First filter: tag ORDERX on K1.
    ADSHANDLE h1 = 0;
    REQUIRE(AdsCreateIndex61(hT, bag, tag, exK1,
                             nullptr, nullptr, 0, 0, &h1) == 0);
    CHECK(walk(hT) == std::vector<UNSIGNED32>{1, 2, 3});

    // 2) Re-filter on another column: SAME tag name, SAME bag, new column K2.
    //    No FERASE / OrdBagClear first (that is the user-side workaround we are
    //    trying to make unnecessary). DBFCDX deletes + recreates the tag.
    ADSHANDLE h2 = 0;
    REQUIRE(AdsCreateIndex61(hT, bag, tag, exK2,
                             nullptr, nullptr, 0, 0, &h2) == 0);

    // The stored expression must now be the NEW column. (RED before fix:
    // returns "K1" because set_options never rewrote the expression.)
    CHECK(index_expr(hT, "ORDERX") == "K2");

    // The rebuilt tree visits records in K2 ascending order.
    CHECK(walk(hT) == std::vector<UNSIGNED32>{3, 2, 1});

    // 3) The real-world bite: append AFTER the re-create. The new record must
    //    be placed by the NEW column (K2). With a stale stored expression the
    //    sync path keys it off the OLD column (K1) and it lands out of order.
    //    rec4: K2=A0 sorts before every existing K2 -> must be FIRST.
    REQUIRE(AdsAppendRecord(hT) == 0);
    set8(hT, "K1", "Z9");   // by K1 this would sort LAST
    set8(hT, "K2", "A0");   // by K2 this must sort FIRST
    CHECK(walk(hT) == std::vector<UNSIGNED32>{4, 3, 2, 1});

    AdsCloseTable(hT);
    AdsDisconnect(hConn);
    fs::remove_all(dir, ec);
}

// set_expression must fail loud instead of silently truncating an expression
// that does not fit the 510-byte sub-header pool (a clipped key expression is
// a corrupt index). Mirrors the guard the former set_condition enforced.
TEST_CASE("CDX set_expression rejects an over-long expression instead of truncating") {
    auto p = fs::temp_directory_path() / "openads_cdx_setexpr_overflow.cdx";
    std::error_code ec;
    fs::remove(p, ec);

    auto c = openads::drivers::cdx::CdxIndex::create(
        p.string(), "ORD", "K1", 8, false, false);
    REQUIRE(c.has_value());
    openads::drivers::cdx::CdxIndex ix = std::move(c).value();

    auto ok = ix.set_expression("K2", "");          // fits -> ok
    CHECK(ok.has_value());

    std::string huge(600, 'A');                      // > 510-byte pool
    auto bad = ix.set_expression(huge, "");
    CHECK_FALSE(bad.has_value());

    fs::remove(p, ec);
}
