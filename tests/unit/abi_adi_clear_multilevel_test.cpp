// Repro for ADI clear_data on a MULTI-LEVEL index (large table).
//
// Re-creating an existing tag (CREATE INDEX overwrite, or the ERP reindex
// hitting the same field twice) takes the open_named + clear_data path in
// AdsCreateIndex61. clear_data used to require the tag's root page to be a
// dense leaf and aborted with ADSCDX/5000 "root is not a dense leaf" when the
// index was big enough to have a branch root (>1 B-tree level). This is what
// killed the reindex of ESTAELEC (441k records) at INDEX ON ... TAG ORD3.
//
// Here we build a tag over enough records to force a multi-level tree (a
// 512-byte dense leaf holds ~162 entries), then re-create the SAME tag so
// clear_data runs on the branch root. It must succeed and the rebuilt index
// must still walk every record in key order.

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

void append_key(ADSHANDLE hTable, const std::string& v) {
    REQUIRE(AdsAppendRecord(hTable) == AE_SUCCESS);
    UNSIGNED8 fld[] = "K";
    UNSIGNED8 val[16]{};
    std::memcpy(val, v.data(), v.size());
    REQUIRE(AdsSetString(hTable, fld, val,
                         static_cast<UNSIGNED32>(v.size())) == AE_SUCCESS);
    REQUIRE(AdsWriteRecord(hTable) == AE_SUCCESS);
}

} // namespace

TEST_CASE("ADI clear_data: re-create tag on a multi-level (large) index") {
    fs::path tmp = fs::temp_directory_path() / "openads_adi_clear_ml";
    { std::error_code ec; fs::create_directories(tmp, ec); }
    { std::error_code ec;
      fs::remove(tmp / "big.adt", ec);
      fs::remove(tmp / "big.adi", ec); }

    UNSIGNED8 srv[260]{};
    std::memcpy(srv, tmp.string().c_str(), tmp.string().size());
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn)
            == AE_SUCCESS);

    UNSIGNED8 tbl[]    = "big.adt";
    UNSIGNED8 flddef[] = "K,Character,8";
    ADSHANDLE hTable   = 0;
    REQUIRE(AdsCreateTable(hConn, tbl, nullptr, ADS_ADT, ADS_ANSI, 0, 0, 0,
                           flddef, &hTable) == AE_SUCCESS);

    // 600 distinct keys → ~4 dense leaves → branch root (multi-level).
    const int N = 600;
    for (int i = 1; i <= N; ++i) {
        char b[16];
        std::snprintf(b, sizeof(b), "%08d", i);
        append_key(hTable, std::string(b));
    }

    UNSIGNED8 idxfile[] = "big.adi";
    UNSIGNED8 tag[]      = "ORD1";
    UNSIGNED8 expr[]     = "K";

    // First build → multi-level tree.
    ADSHANDLE hIdx1 = 0;
    REQUIRE(AdsCreateIndex61(hTable, idxfile, tag, expr,
                             nullptr, nullptr, 0, 0, &hIdx1) == AE_SUCCESS);

    // Re-create the SAME tag → exists && have_tag → open_named + clear_data on
    // a BRANCH root. Used to fail with ADSCDX/5000; must now succeed.
    ADSHANDLE hIdx2 = 0;
    REQUIRE(AdsCreateIndex61(hTable, idxfile, tag, expr,
                             nullptr, nullptr, 0, 0, &hIdx2) == AE_SUCCESS);

    // The rebuilt index must walk every record in ascending key order.
    REQUIRE(AdsGotoTop(hTable) == AE_SUCCESS);
    std::vector<std::string> seen;
    for (;;) {
        UNSIGNED16 at_eof = 0;
        REQUIRE(AdsAtEOF(hTable, &at_eof) == AE_SUCCESS);
        if (at_eof) break;
        UNSIGNED8 buf[32]{};
        UNSIGNED32 len = sizeof(buf);
        REQUIRE(AdsGetString(hTable, (UNSIGNED8*)"K", buf, &len, 0)
                == AE_SUCCESS);
        seen.push_back(rtrim(std::string(reinterpret_cast<char*>(buf), len)));
        REQUIRE(AdsSkip(hTable, 1) == AE_SUCCESS);
    }

    REQUIRE(seen.size() == static_cast<std::size_t>(N));
    CHECK(seen.front() == "00000001");
    CHECK(seen.back()  == "00000600");
    bool ordered = true;
    for (std::size_t i = 1; i < seen.size(); ++i)
        if (seen[i] < seen[i - 1]) { ordered = false; break; }
    CHECK(ordered);

    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
    { std::error_code ec; fs::remove_all(tmp, ec); }
}
