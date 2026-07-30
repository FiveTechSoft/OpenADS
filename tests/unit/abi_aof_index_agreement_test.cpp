// abi_aof_index_agreement_test.cpp -- the index-accelerated AOF path must
// return the SAME record set as the full-scan path.
//
// M-AOF.4 (aof_eval.cpp, serve_leaf_via_index) answers a `field OP literal`
// leaf with a range scan over an index whose key expression is that bare
// field. The bitmap it produces is authoritative -- nothing re-verifies it
// per record -- so any disagreement with eval_leaf()'s full scan is a silent
// wrong result set, not a slowdown.
//
// Two assumptions in that path were never checked. Both belong to the same
// family as the partial-key seek/scope defects: the code assumed something
// about the index it picked.
//
//  1. CONDITIONAL TAG. find_index_for_field() matched on the key expression
//     alone, ignoring IIndex::condition(). A tag built `FOR EST = "A"` holds
//     only the rows that satisfy the FOR clause, so serving `CLI = "0001"`
//     from it silently dropped every row where EST <> "A" -- records the
//     filter should have kept. Opening a .dbf auto-binds every tag in the
//     production .cdx (M-AOF.6), so a conditional tag lands in
//     Table::all_indexes() without anyone asking for it.
//
//  2. LITERAL WIDER THAN THE KEY / TRAILING BLANK. encode_char_key() forced
//     the literal to the key width with pad_key(), which truncates when it
//     is longer. The scan path compares rtrim(field) against the literal
//     verbatim, so `CLI = "0001XX"` on a C(6) field is false for every row --
//     but truncated to the key width it matched "0001". A literal ending in
//     a blank diverges the other way: rtrim(field) never ends in a blank, so
//     the scan path says no while the padded compare said yes.
//
// Each case asserts against the full-scan answer, obtained by running the
// same filter before the index exists.

#include "doctest.h"
#include "openads/ace.h"

#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

UNSIGNED8 fCli[] = "CLI";
UNSIGNED8 fEst[] = "EST";

// Every recno the AOF leaves visible, walking with the filter on.
std::string visible(ADSHANDLE hT, const std::string& cond) {
    std::string c = cond;
    REQUIRE(AdsSetAOF(hT, reinterpret_cast<UNSIGNED8*>(c.data()), 0) == 0);
    std::string out;
    REQUIRE(AdsGotoTop(hT) == 0);
    for (;;) {
        UNSIGNED16 eof = 0;
        REQUIRE(AdsAtEOF(hT, &eof) == 0);
        if (eof) break;
        UNSIGNED32 r = 0;
        REQUIRE(AdsGetRecordNum(hT, ADS_IGNOREFILTERS, &r) == 0);
        if (!out.empty()) out += ',';
        out += std::to_string(r);
        REQUIRE(AdsSkip(hT, 1) == 0);
    }
    REQUIRE(AdsClearAOF(hT) == 0);
    return out;
}

// dir/table with CLI C(6) + EST C(1) and three rows. Returns the connection
// and table handles; caller closes.
void make_table(const fs::path& dir, ADSHANDLE& hConn, ADSHANDLE& hT) {
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);

    UNSIGNED8 def[]   = "CLI,C,6,0;EST,C,1,0";
    UNSIGNED8 tname[] = "cta.dbf";
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX,
                           0, 0, 0, 0, def, &hT) == 0);

    struct Row { const char* cli; const char* est; };
    const Row rows[] = {
        {"0001  ", "A"},
        {"0001  ", "B"},   // same CLI, excluded by a FOR EST="A" tag
        {"0002  ", "A"},
    };
    for (const auto& r : rows) {
        REQUIRE(AdsAppendRecord(hT) == 0);
        AdsSetString(hT, fCli, (UNSIGNED8*)r.cli, 6);
        AdsSetString(hT, fEst, (UNSIGNED8*)r.est, 1);
    }
    REQUIRE(AdsWriteRecord(hT) == 0);
}

} // namespace

TEST_CASE("AOF: a conditional tag must not serve a leaf") {
    auto dir = fs::temp_directory_path() / "openads_aof_cond_tag";
    ADSHANDLE hConn = 0, hT = 0;
    make_table(dir, hConn, hT);

    // Baseline: no index open at all -> pure full scan.
    const std::string expected = visible(hT, "CLI = '0001'");
    CHECK(expected == "1,2");

    // Same filter with a FOR-conditional tag on CLI open. The tag holds
    // only recs 1 and 3 (EST = "A"), so serving the leaf from it would
    // lose rec 2.
    UNSIGNED8 ixBag[260]{};  std::strncpy((char*)ixBag, "cta.cdx", 259);
    UNSIGNED8 ixName[64]{};  std::strncpy((char*)ixName, "TCLIA", 63);
    UNSIGNED8 ixExpr[128]{}; std::strncpy((char*)ixExpr, "CLI", 127);
    UNSIGNED8 ixFor[128]{};  std::strncpy((char*)ixFor, "EST = 'A'", 127);
    ADSHANDLE hIx = 0;
    REQUIRE(AdsCreateIndex61(hT, ixBag, ixName, ixExpr,
                             ixFor, nullptr, 0, 0, &hIx) == AE_SUCCESS);

    CHECK(visible(hT, "CLI = '0001'") == expected);

    AdsCloseTable(hT);
    AdsDisconnect(hConn);
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST_CASE("AOF: index path agrees with the scan path on odd literals") {
    auto dir = fs::temp_directory_path() / "openads_aof_literal_width";
    ADSHANDLE hConn = 0, hT = 0;
    make_table(dir, hConn, hT);

    // Full-scan baselines, taken before any index exists.
    const std::string exp_exact    = visible(hT, "CLI = '0001'");
    const std::string exp_overlong = visible(hT, "CLI = '0001XX'");
    const std::string exp_trailing = visible(hT, "CLI = '0001 '");
    const std::string exp_lt       = visible(hT, "CLI < '0001XX'");
    CHECK(exp_exact == "1,2");
    CHECK(exp_overlong == "");     // no row's CLI is "0001XX"
    CHECK(exp_trailing == "");     // rtrim(CLI) never ends in a blank
    CHECK(exp_lt == "1,2");   // "0001" < "0001XX"

    // Now with a plain tag on CLI -- key width 6, so the literals above are
    // respectively equal-width, wider, and blank-tailed.
    UNSIGNED8 ixBag[260]{};  std::strncpy((char*)ixBag, "cta.cdx", 259);
    UNSIGNED8 ixName[64]{};  std::strncpy((char*)ixName, "TCLI", 63);
    UNSIGNED8 ixExpr[128]{}; std::strncpy((char*)ixExpr, "CLI", 127);
    ADSHANDLE hIx = 0;
    REQUIRE(AdsCreateIndex61(hT, ixBag, ixName, ixExpr,
                             nullptr, nullptr, 0, 0, &hIx) == AE_SUCCESS);

    CHECK(visible(hT, "CLI = '0001'")   == exp_exact);
    CHECK(visible(hT, "CLI = '0001XX'") == exp_overlong);
    CHECK(visible(hT, "CLI = '0001 '")  == exp_trailing);
    CHECK(visible(hT, "CLI < '0001XX'") == exp_lt);

    AdsCloseTable(hT);
    AdsDisconnect(hConn);
    std::error_code ec;
    fs::remove_all(dir, ec);
}
