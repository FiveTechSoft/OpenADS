// QA repros (engine-level, no rddads) for divergences found by the Harbour
// differential harness against OpenADS CDX. Native DBFCDX is the oracle.
//   A: INDEX ON AGE FOR ACTIVE  -> conditional FOR clause must be honored.
//   C: ordScope(28..40) on numeric index -> scoped range must return rows.
// These build a real DBF/CDX table via the ABI and exercise Ads* directly.
#include "doctest.h"
#include "openads/ace.h"
#include "openads/error.h"

#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

using openads::AE_SUCCESS;

namespace {

void put_row(ADSHANDLE hT, const char* name, double age, bool active) {
    REQUIRE(AdsAppendRecord(hT) == AE_SUCCESS);
    UNSIGNED8 fN[] = "NAME";
    UNSIGNED8 v[64]{};
    std::strncpy(reinterpret_cast<char*>(v), name, sizeof(v) - 1);
    REQUIRE(AdsSetString(hT, fN, v,
            static_cast<UNSIGNED32>(std::strlen(name))) == AE_SUCCESS);
    UNSIGNED8 fA[] = "AGE";
    REQUIRE(AdsSetDouble(hT, fA, age) == AE_SUCCESS);
    UNSIGNED8 fAct[] = "ACTIVE";
    REQUIRE(AdsSetLogical(hT, fAct, active ? 1 : 0) == AE_SUCCESS);
    REQUIRE(AdsWriteRecord(hT) == AE_SUCCESS);
}

// Same 8-row fixture as the Harbour harness; 5 rows have ACTIVE = true.
ADSHANDLE open_fixture(const fs::path& dir, const char* tbl) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    fs::remove(dir / tbl, ec);

    UNSIGNED8 srv[260]{};
    std::memcpy(srv, dir.string().c_str(), dir.string().size());
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn)
            == AE_SUCCESS);

    UNSIGNED8 name[64]{};
    std::strncpy(reinterpret_cast<char*>(name), tbl, sizeof(name) - 1);
    UNSIGNED8 flddef[] = "NAME,Character,20;AGE,Numeric,3,0;ACTIVE,Logical,1";
    ADSHANDLE hTable = 0;
    REQUIRE(AdsCreateTable(hConn, name, nullptr, ADS_CDX, ADS_ANSI, 0, 0, 0,
                           flddef, &hTable) == AE_SUCCESS);

    put_row(hTable, "Charlie", 30, true);
    put_row(hTable, "alice",   25, false);
    put_row(hTable, "Bob",     40, true);
    put_row(hTable, "dave",    22, true);
    put_row(hTable, "Eve",     35, false);
    put_row(hTable, "bob",     28, true);
    put_row(hTable, "Mary",    45, true);
    put_row(hTable, "tom",     31, false);
    return hTable;
}

UNSIGNED32 walk_count(ADSHANDLE hTable) {
    REQUIRE(AdsGotoTop(hTable) == AE_SUCCESS);
    UNSIGNED32 n = 0;
    for (;;) {
        UNSIGNED16 eof = 0;
        REQUIRE(AdsAtEOF(hTable, &eof) == AE_SUCCESS);
        if (eof) break;
        ++n;
        REQUIRE(AdsSkip(hTable, 1) == AE_SUCCESS);
        if (n > 100) break;  // safety
    }
    return n;
}

} // namespace

// A — conditional FOR index: only the 5 ACTIVE rows must be indexed.
TEST_CASE("QA-A: CDX conditional FOR index honors the condition") {
    const auto dir = fs::temp_directory_path() / "openads_qa_a";
    ADSHANDLE hTable = open_fixture(dir, "qa_a.dbf");

    UNSIGNED8 idxfile[] = "qa_a.cdx";
    UNSIGNED8 idxname[] = "TCOND";
    UNSIGNED8 expr[]    = "AGE";
    UNSIGNED8 cond[]    = "ACTIVE";   // FOR condition
    ADSHANDLE hIdx = 0;
    REQUIRE(AdsCreateIndex61(hTable, idxfile, idxname, expr,
                             cond, nullptr, 0, 0, &hIdx) == AE_SUCCESS);

    UNSIGNED32 keys = 0;
    REQUIRE(AdsGetKeyCount(hIdx, 2 /*ADS_IGNOREFILTERS*/, &keys) == AE_SUCCESS);
    INFO("AdsGetKeyCount on conditional index = ", keys, " (expected 5)");
    CHECK(keys == 5u);

    // Walking the conditional index must visit only the 5 ACTIVE rows.
    UNSIGNED32 walked = walk_count(hTable);
    INFO("walk over conditional index = ", walked, " (expected 5)");
    CHECK(walked == 5u);

    AdsCloseTable(hTable);
}

// C — numeric ordScope: range [28..40] must return the 5 in-range rows.
TEST_CASE("QA-C: numeric index ordScope returns in-range rows") {
    const auto dir = fs::temp_directory_path() / "openads_qa_c";
    ADSHANDLE hTable = open_fixture(dir, "qa_c.dbf");

    UNSIGNED8 idxfile[] = "qa_c.cdx";
    UNSIGNED8 idxname[] = "TAGE";
    UNSIGNED8 expr[]    = "AGE";
    ADSHANDLE hIdx = 0;
    REQUIRE(AdsCreateIndex61(hTable, idxfile, idxname, expr,
                             nullptr, nullptr, 0, 0, &hIdx) == AE_SUCCESS);

    double top = 28.0, bot = 40.0;
    REQUIRE(AdsSetScope(hIdx, ADS_TOP,    reinterpret_cast<UNSIGNED8*>(&top),
                        sizeof(double), ADS_DOUBLEKEY) == AE_SUCCESS);
    REQUIRE(AdsSetScope(hIdx, ADS_BOTTOM, reinterpret_cast<UNSIGNED8*>(&bot),
                        sizeof(double), ADS_DOUBLEKEY) == AE_SUCCESS);

    UNSIGNED32 inScope = walk_count(hTable);
    INFO("rows within scope [28..40] = ", inScope, " (expected 5)");
    CHECK(inScope == 5u);

    AdsClearScope(hIdx, ADS_TOP);
    AdsClearScope(hIdx, ADS_BOTTOM);
    AdsCloseTable(hTable);
}
