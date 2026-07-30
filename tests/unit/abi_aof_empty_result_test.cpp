// abi_aof_empty_result_test.cpp -- an AOF whose result set is EMPTY must
// hide every record, with or without an active index.
//
// install_aof_bitmap() translates the AOF bitmap into the recno sequence
// that goto_top / goto_bottom / skip walk. Those three gated on
// `!recno_sequence_.empty()`, which conflated "the visible set is empty"
// with "no sequence was installed": a filter matching nothing fell through
// to the active-index branch of the walk, and THAT branch never consults
// filter_. So a filter that selects no row returned the whole table --
// but only while an index was open, which is what made it look arbitrary.
//
// The same conflation covered SQL cursors: an ORDER BY / WHERE yielding no
// rows also installs an empty sequence.
//
// Table now tracks "a sequence was installed" separately from "it has
// entries", and an installed-but-empty sequence means Limbo / Eof.

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
// An AOF that selects NOTHING installs an empty recno sequence. Navigation
// used to gate on "sequence non-empty", so the empty set read as "no
// sequence installed" and goto_top/skip fell through to the active-index
// walk -- which never consults the filter closure. Net effect: a filter
// matching no record showed the WHOLE table, but only while an index was
// open. Nothing here depends on the literal being odd; "9999" is a plain
// value the index path serves happily.
TEST_CASE("AOF: a filter matching nothing hides everything, index or not") {
    auto dir = fs::temp_directory_path() / "openads_aof_empty_set";
    ADSHANDLE hConn = 0, hT = 0;
    make_table(dir, hConn, hT);

    CHECK(visible(hT, "CLI = '9999'") == "");          // no index yet

    UNSIGNED8 ixBag[260]{};  std::strncpy((char*)ixBag, "cta.cdx", 259);
    UNSIGNED8 ixName[64]{};  std::strncpy((char*)ixName, "TCLI", 63);
    UNSIGNED8 ixExpr[128]{}; std::strncpy((char*)ixExpr, "CLI", 127);
    ADSHANDLE hIx = 0;
    REQUIRE(AdsCreateIndex61(hT, ixBag, ixName, ixExpr,
                             nullptr, nullptr, 0, 0, &hIx) == AE_SUCCESS);

    CHECK(visible(hT, "CLI = '9999'") == "");          // index active
    // And the table is fully back once the AOF is gone.
    CHECK(visible(hT, "CLI < 'ZZZZZZ'") == "1,2,3");

    AdsCloseTable(hT);
    AdsDisconnect(hConn);
    std::error_code ec;
    fs::remove_all(dir, ec);
}
