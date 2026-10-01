// Regression: AdsCreateIndex61 on an ADT table with a NON-STRUCTURAL index
// bag (a bag whose file stem differs from the table's, i.e. the legacy
// `INDEX ON <field> TAG <name> TO <other path>` form). The ADI create path
// derived the companion ADT path from the .adi stem (adt_path_for), so for a
// separate bag it looked for "<bag>.adt" — which does not exist — and failed
// AFTER already writing the .adi, surfacing to rddads as EG_CREATE. The index
// was never built, so DBSETORDER fell back to natural order ("navigates in
// another order"). The fix plumbs the real table path into the create.
#include "doctest.h"
#include "drivers/adi/adi_index.h"
#include "openads/ace.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::string rtrim(std::string s) {
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

void append_name(ADSHANDLE hTable, const char* name) {
    REQUIRE(AdsAppendRecord(hTable) == AE_SUCCESS);
    UNSIGNED8 fld[] = "Name";
    UNSIGNED8 val[64]{};
    std::strncpy(reinterpret_cast<char*>(val), name, sizeof(val) - 1);
    REQUIRE(AdsSetString(hTable, fld, val,
                         static_cast<UNSIGNED32>(std::strlen(name)))
            == AE_SUCCESS);
    REQUIRE(AdsWriteRecord(hTable) == AE_SUCCESS);
}

} // namespace

TEST_CASE("ADI separate (non-structural) bag on ADT: create + ordered walk") {
    fs::path tmp = fs::temp_directory_path() / "openads_adi_sepbag";
    { std::error_code ec; fs::remove_all(tmp, ec); fs::create_directories(tmp, ec); }

    UNSIGNED8 srv[260]{};
    std::memcpy(srv, tmp.string().c_str(), tmp.string().size());
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn)
            == AE_SUCCESS);

    UNSIGNED8 tbl[]    = "articulo.adt";
    UNSIGNED8 flddef[] = "Name,Character,20";
    ADSHANDLE hTable   = 0;
    REQUIRE(AdsCreateTable(hConn, tbl, nullptr, ADS_ADT, ADS_ANSI, 0, 0, 0,
                           flddef, &hTable) == AE_SUCCESS);

    append_name(hTable, "Carol");
    append_name(hTable, "Alice");
    append_name(hTable, "Bob");

    // SEPARATE bag: stem "sepbag" differs from the table stem "articulo".
    UNSIGNED8 idxfile[] = "sepbag.adi";
    UNSIGNED8 idxname[] = "NOMTAG";
    UNSIGNED8 expr[]    = "Name";
    ADSHANDLE hIdx = 0;
    REQUIRE(AdsCreateIndex61(hTable, idxfile, idxname, expr,
                             nullptr, nullptr, 0, 0, &hIdx) == AE_SUCCESS);

    // The separate bag file must have been written.
    std::error_code ec;
    CHECK(fs::exists(tmp / "sepbag.adi", ec));

    // Navigation must follow the freshly created order (alphabetical), not the
    // physical append order (Carol, Alice, Bob).
    REQUIRE(AdsGotoTop(hTable) == AE_SUCCESS);
    std::vector<std::string> seen;
    for (;;) {
        UNSIGNED16 at_eof = 0;
        REQUIRE(AdsAtEOF(hTable, &at_eof) == AE_SUCCESS);
        if (at_eof) break;
        UNSIGNED8 buf[64]{};
        UNSIGNED32 len = sizeof(buf);
        REQUIRE(AdsGetString(hTable, (UNSIGNED8*)"Name", buf, &len, 0)
                == AE_SUCCESS);
        seen.push_back(rtrim(std::string(reinterpret_cast<char*>(buf), len)));
        REQUIRE(AdsSkip(hTable, 1) == AE_SUCCESS);
    }
    REQUIRE(seen.size() == 3u);
    CHECK(seen[0] == "Alice");
    CHECK(seen[1] == "Bob");
    CHECK(seen[2] == "Carol");

    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
}

TEST_CASE("ADI separate (non-structural) bag on ADT: reopen and navigate") {
    fs::path tmp = fs::temp_directory_path() / "openads_adi_sepbag_reopen";
    { std::error_code ec; fs::remove_all(tmp, ec); fs::create_directories(tmp, ec); }

    UNSIGNED8 srv[260]{};
    std::memcpy(srv, tmp.string().c_str(), tmp.string().size());
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn)
            == AE_SUCCESS);

    UNSIGNED8 tbl[]    = "articulo.adt";
    UNSIGNED8 flddef[] = "Name,Character,20";
    ADSHANDLE hTable   = 0;
    REQUIRE(AdsCreateTable(hConn, tbl, nullptr, ADS_ADT, ADS_ANSI, 0, 0, 0,
                           flddef, &hTable) == AE_SUCCESS);
    append_name(hTable, "Carol");
    append_name(hTable, "Alice");
    append_name(hTable, "Bob");

    UNSIGNED8 idxfile[] = "sepbag.adi";
    UNSIGNED8 idxname[] = "NOMTAG";
    UNSIGNED8 expr[]    = "Name";
    ADSHANDLE hIdx = 0;
    REQUIRE(AdsCreateIndex61(hTable, idxfile, idxname, expr,
                             nullptr, nullptr, 0, 0, &hIdx) == AE_SUCCESS);

    // Close everything — drop the in-memory index built at create time.
    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);

    // Reopen the table and the SEPARATE bag in a fresh handle. The reopen path
    // (list_tags/open_named) must locate the right companion ADT (articulo.adt),
    // not "<bag>.adt".
    ADSHANDLE hTable2 = 0;
    REQUIRE(AdsOpenTable(hConn, tbl, tbl, ADS_ADT, ADS_ANSI, ADS_DEFAULT,
                         ADS_DEFAULT, 0, &hTable2) == AE_SUCCESS);

    ADSHANDLE ah[8] = {0};
    UNSIGNED16 n = 8;
    REQUIRE(AdsOpenIndex(hTable2, idxfile, ah, &n) == AE_SUCCESS);
    REQUIRE(AdsSetIndexOrderByHandle(hTable2, ah[0]) == AE_SUCCESS);

    REQUIRE(AdsGotoTop(hTable2) == AE_SUCCESS);
    std::vector<std::string> seen;
    for (;;) {
        UNSIGNED16 at_eof = 0;
        REQUIRE(AdsAtEOF(hTable2, &at_eof) == AE_SUCCESS);
        if (at_eof) break;
        UNSIGNED8 buf[64]{};
        UNSIGNED32 len = sizeof(buf);
        REQUIRE(AdsGetString(hTable2, (UNSIGNED8*)"Name", buf, &len, 0)
                == AE_SUCCESS);
        seen.push_back(rtrim(std::string(reinterpret_cast<char*>(buf), len)));
        REQUIRE(AdsSkip(hTable2, 1) == AE_SUCCESS);
    }
    REQUIRE(seen.size() == 3u);
    CHECK(seen[0] == "Alice");
    CHECK(seen[1] == "Bob");
    CHECK(seen[2] == "Carol");

    REQUIRE(AdsCloseTable(hTable2) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
}
