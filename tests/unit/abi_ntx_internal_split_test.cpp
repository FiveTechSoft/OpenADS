#include "doctest.h"
#include "openads/ace.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

// Regression coverage for the NTX internal-node split off-by-one.
//
// When an NTX index grows deep enough that an *internal* (branch) node fills
// up and has to split (a third tree level appears, ~1500+ keys for a 15-byte
// key), insert() builds the parent's entry list, inserts the promoted
// separator at `pos`, then wired the new right sibling in via
// `ents[pos + 1].lchild = prop_right`. For an ascending key stream the
// descent always lands at the rightmost slot (pos == key_count), so the
// separator is appended at the end and `ents[pos + 1]` reads one past the
// vector. Worse, prop_right was never recorded as the node's rightmost
// child: the stale original sentinel (which, for a rightmost leaf split,
// aliases prop_left) was reused, so prop_left ended up referenced twice and
// prop_right was orphaned.
//
// Symptom: every internal-node split injected one phantom / out-of-order
// entry. A full Skip walk returned slightly MORE rows than exist
// (e.g. 2003 vs 2000), keys came back out of order, and a few Seeks missed.
//
// Fix: when the separator is appended at the end, prop_right becomes the
// node's new rightmost child (mirroring the non-splitting "room in parent"
// path); otherwise the original sentinel is preserved.
//
// This builds a large unique-key index (no duplicates, no PACK) so it pins
// the internal-split fix in isolation.

namespace fs = std::filesystem;

namespace {

void walk_index_count(ADSHANDLE hTable, const char* field,
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

TEST_CASE("NTX build of a deep unique-key index splits internal nodes correctly") {
    auto dir = fs::temp_directory_path() / "openads_ntx_internal_split";
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

    // 2000 strictly increasing unique keys -> the index grows three levels,
    // so the internal (branch) split path is exercised several times. Every
    // insert lands at the rightmost slot (pos == key_count), the exact
    // trigger for the off-by-one.
    const int N = 2000;
    UNSIGNED8 fld[] = "CODE";
    for (int i = 1; i <= N; ++i) {
        REQUIRE(AdsAppendRecord(hTable) == 0);
        char key[16];
        std::snprintf(key, sizeof(key), "K%010d", i);   // "K0000000001" ...
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

    // (a) A full top-to-bottom Skip walk must visit exactly N records, in
    //     order -- not N + (number of internal splits), and not out of order.
    UNSIGNED32 walked = 0;
    bool ordered = true;
    walk_index_count(hTable, "CODE", walked, ordered);
    CHECK(walked == static_cast<UNSIGNED32>(N));   // was N+3 before the fix
    CHECK(ordered);                                // was false before the fix

    // (b) Every key must be reachable by Seek.
    int tried = 0, found = 0;
    for (int i = 1; i <= N; ++i) {
        char key[16];
        std::snprintf(key, sizeof(key), "K%010d", i);
        std::string padded(key);
        padded.resize(15, ' ');
        UNSIGNED16 hit = 0;
        ++tried;
        if (AdsSeek(hIndex, reinterpret_cast<UNSIGNED8*>(padded.data()),
                    15, 0, 0, &hit) == 0 && hit == 1) {
            ++found;
        }
    }
    CHECK(found == tried);                          // a few missed before the fix

    REQUIRE(AdsCloseIndex(hIndex) == 0);
    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}
