#include "doctest.h"
#include "openads/ace.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

// Regression coverage for the NTX multi-page index that is built INCOMPLETE
// when the key stream contains a clustered run of DUPLICATE keys.
//
// NTX is a B-tree that stores keys in internal nodes too (unlike CDX's
// B+tree). Two independent defects corrupted the tree once duplicate keys
// spanned more than one page:
//
//   1. seek_key_for_write_(), shared by insert() and erase(), stopped the
//      descent at an internal node whenever the key matched a separator
//      exactly. For erase that is correct (the key may live in a branch),
//      but an insert must always reach a leaf -- stopping at a branch made
//      insert() write the new entry into that internal node with a 0 left
//      child, orphaning the whole subtree. Exact matches only happen for
//      duplicate keys, so unique streams never tripped it.
//
//   2. The "room in parent" split path reused the parent's sentinel slot
//      (its rightmost child) for the promoted separator without preserving
//      the original rightmost child whenever the separator landed left of
//      the end (pos < key_count). Runs of duplicates sort into the leftmost
//      subtree, so their separators are inserted left of the end -- dropping
//      the right subtree and leaving an unreachable branch with a 0 right
//      child. Ascending unique keys always split at the rightmost position,
//      so they never tripped it either.
//
// Symptom before the fix: a top-to-bottom Skip walk reached only a fraction
// of the records and AdsSeek found almost none of the survivors, even though
// AdsCreateIndex61 reported success. This test builds a multi-page NTX over a
// duplicate-laden key stream and asserts every record is reachable by both a
// full Skip traversal (in order) and an individual Seek.
//
// N is kept below the ~1500-key third-level threshold so this pins the
// duplicate-key fix alone; the large-N internal-node split is tracked apart.

namespace fs = std::filesystem;

namespace {

// Walk the active index top to bottom; returns how many records were visited
// and whether the keys came back non-decreasing.
void walk_index(ADSHANDLE hTable, const char* field,
                UNSIGNED32& walked, bool& ordered) {
    UNSIGNED8 fld[32];
    std::memcpy(fld, field, std::strlen(field) + 1);
    walked = 0;
    ordered = true;
    std::string prev;
    REQUIRE(AdsGotoTop(hTable) == 0);
    UNSIGNED16 eof = 0;
    while (AdsAtEOF(hTable, &eof) == 0 && eof == 0) {
        UNSIGNED8 kb[64];
        UNSIGNED32 kc = sizeof(kb);
        REQUIRE(AdsGetString(hTable, fld, kb, &kc, 0) == 0);
        std::string cur(reinterpret_cast<char*>(kb), kc);
        if (!prev.empty() && cur < prev) ordered = false;
        prev = cur;
        ++walked;
        if (AdsSkip(hTable, 1) != 0) break;
        if (walked > 1000000) break;   // runaway guard
    }
}

} // namespace

TEST_CASE("NTX build over clustered duplicate keys yields a complete multi-page index") {
    auto dir = fs::temp_directory_path() / "openads_ntx_multipage_dup";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn) == 0);

    UNSIGNED8 def[]   = "CODE,C,15,0";
    UNSIGNED8 tname[] = "data";
    ADSHANDLE hTable = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_NTX,
                           0, 0, 0, 0, def, &hTable) == 0);

    // 300 records: every 5th carries an empty (all-blank) key. Empty keys are
    // identical and sort to the front, forming a clustered run of duplicates
    // that spans several leaf pages -- the exact trigger for both defects.
    const int N = 300;
    UNSIGNED8 fld[] = "CODE";
    for (int i = 1; i <= N; ++i) {
        REQUIRE(AdsAppendRecord(hTable) == 0);
        char key[16];
        if (i % 5 == 0) key[0] = '\0';                       // empty -> blanks
        else std::snprintf(key, sizeof(key), "ART%08d", i);  // unique
        REQUIRE(AdsSetString(hTable, fld,
                    reinterpret_cast<UNSIGNED8*>(key),
                    static_cast<UNSIGNED32>(std::strlen(key))) == 0);
    }
    REQUIRE(AdsWriteRecord(hTable) == 0);

    UNSIGNED8 idx_file[32] = "data.ntx";
    UNSIGNED8 tag[16]      = "BYCODE";
    UNSIGNED8 expr[16]     = "CODE";
    ADSHANDLE hIndex = 0;
    REQUIRE(AdsCreateIndex61(hTable, idx_file, tag, expr,
                             nullptr, nullptr, 0, 0, &hIndex) == 0);

    UNSIGNED8 order[] = "BYCODE";
    REQUIRE(AdsSetIndexOrder(hTable, order) == 0);

    // (a) A full top-to-bottom Skip walk must visit every record, in order.
    UNSIGNED32 walked = 0;
    bool ordered = true;
    walk_index(hTable, "CODE", walked, ordered);
    CHECK(walked == static_cast<UNSIGNED32>(N));   // was ~30/300 before the fix
    CHECK(ordered);

    // (b) Every unique (non-empty) key must be found via the rebuilt index.
    int tried = 0, found = 0;
    for (int i = 1; i <= N; ++i) {
        if (i % 5 == 0) continue;                  // empty key, skip
        char key[16];
        std::snprintf(key, sizeof(key), "ART%08d", i);
        std::string padded(key);
        padded.resize(15, ' ');
        UNSIGNED16 hit = 0;
        ++tried;
        if (AdsSeek(hIndex, reinterpret_cast<UNSIGNED8*>(padded.data()),
                    15, 0, 0, &hit) == 0 && hit == 1) {
            ++found;
        }
    }
    CHECK(found == tried);                          // was ~24/240 before the fix

    REQUIRE(AdsCloseIndex(hIndex) == 0);
    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}
