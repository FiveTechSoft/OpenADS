// abi_scope_partial_key_test.cpp — an ordScope bound SHORTER than the index key
// must bound by PREFIX, the way Clipper / DBFCDX do.
//
// Table::key_in_bottom_scope_ compared the full-width index key against the
// short bound:
//
//     return key <= *order_->scope().bottom;
//
// Every extension of a prefix is lexicographically GREATER than the prefix, so
// with a tag of ID+DOC+SEQ (16 bytes) and a bound of ID+DOC (10 bytes) every
// single key fell past the bottom and the scope came out EMPTY — while DBFCDX
// over the same table returns the rows of that document. Reported by RusSoft
// (Zerus ERP), whose helper sets top and bottom to the same partial key to walk
// one document's lines.
//
// The top bound was already right by accident (an extension is >= the prefix);
// it is covered here so the pair stays symmetric.

#include "doctest.h"
#include "openads/ace.h"

#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {

// Count the rows visible between the current scope bounds.
int walk_count(ADSHANDLE hT) {
    int n = 0;
    AdsGotoTop(hT);
    UNSIGNED16 eof = 0;
    AdsAtEOF(hT, &eof);
    while (!eof) {
        ++n;
        AdsSkip(hT, 1);
        AdsAtEOF(hT, &eof);
    }
    return n;
}

void set_scope(ADSHANDLE hIx, UNSIGNED16 which, const std::string& key) {
    UNSIGNED8 buf[64];
    std::memcpy(buf, key.c_str(), key.size() + 1);
    REQUIRE(AdsSetScope(hIx, which, buf,
                        static_cast<UNSIGNED16>(key.size()),
                        ADS_STRINGKEY) == AE_SUCCESS);
}

} // namespace

TEST_CASE("ordScope with a partial key bounds by prefix") {
    auto dir = fs::temp_directory_path() / "openads_scope_partial";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == AE_SUCCESS);

    UNSIGNED8 def[]   = "ID,C,2,0;DOC,C,8,0;SEQ,C,6,0";
    UNSIGNED8 tname[] = "mov.dbf";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX,
                           0, 0, 0, 0, def, &hT) == AE_SUCCESS);

    UNSIGNED8 fId[]  = "ID";
    UNSIGNED8 fDoc[] = "DOC";
    UNSIGNED8 fSeq[] = "SEQ";

    // Three documents; the middle one has three lines.
    struct Row { const char* id; const char* doc; const char* seq; };
    const Row rows[] = {
        {"2A", "   11111", "     1"},
        {"2B", "   22563", "     1"},
        {"2B", "   22563", "     2"},
        {"2B", "   22563", "     3"},
        {"2C", "   33333", "     1"},
    };
    for (const auto& r : rows) {
        REQUIRE(AdsAppendRecord(hT) == AE_SUCCESS);
        AdsSetString(hT, fId,  (UNSIGNED8*)r.id,  2);
        AdsSetString(hT, fDoc, (UNSIGNED8*)r.doc, 8);
        AdsSetString(hT, fSeq, (UNSIGNED8*)r.seq, 6);
    }
    REQUIRE(AdsWriteRecord(hT) == AE_SUCCESS);

    UNSIGNED8 ixBag[260]{};  std::strncpy((char*)ixBag, "mov.cdx", 259);
    UNSIGNED8 ixName[64]{};  std::strncpy((char*)ixName, "TAG1", 63);
    UNSIGNED8 ixExpr[128]{}; std::strncpy((char*)ixExpr, "ID+DOC+SEQ", 127);
    ADSHANDLE hIx = 0;                                   // 16-byte key
    REQUIRE(AdsCreateIndex61(hT, ixBag, ixName, ixExpr,
                             nullptr, nullptr, 0, 0, &hIx) == AE_SUCCESS);

    const std::string doc = "2B" "   22563";             // 10-byte prefix

    SUBCASE("top and bottom set to the same partial key: the whole document") {
        set_scope(hIx, ADS_TOP,    doc);
        set_scope(hIx, ADS_BOTTOM, doc);
        CHECK(walk_count(hT) == 3);
        AdsClearScope(hIx, ADS_TOP);
        AdsClearScope(hIx, ADS_BOTTOM);
    }

    SUBCASE("bottom only: everything up to and including the document") {
        set_scope(hIx, ADS_BOTTOM, doc);
        CHECK(walk_count(hT) == 4);          // 2A + the three 2B lines
        AdsClearScope(hIx, ADS_BOTTOM);
    }

    SUBCASE("top only: the document and everything after it") {
        set_scope(hIx, ADS_TOP, doc);
        CHECK(walk_count(hT) == 4);          // the three 2B lines + 2C
        AdsClearScope(hIx, ADS_TOP);
    }

    SUBCASE("full-width bounds still select exactly one line") {
        set_scope(hIx, ADS_TOP,    doc + "     2");
        set_scope(hIx, ADS_BOTTOM, doc + "     2");
        CHECK(walk_count(hT) == 1);
        AdsClearScope(hIx, ADS_TOP);
        AdsClearScope(hIx, ADS_BOTTOM);
    }

    SUBCASE("a partial bound that matches nothing yields an empty scope") {
        set_scope(hIx, ADS_TOP,    std::string("ZZ" "   99999"));
        set_scope(hIx, ADS_BOTTOM, std::string("ZZ" "   99999"));
        CHECK(walk_count(hT) == 0);
        AdsClearScope(hIx, ADS_TOP);
        AdsClearScope(hIx, ADS_BOTTOM);
    }

    SUBCASE("no scope: the whole table") {
        CHECK(walk_count(hT) == 5);
    }

    AdsCloseTable(hT);
    AdsDisconnect(hConn);
    fs::remove_all(dir, ec);
}

// The case the removed padding was protecting: rddads sends OrdScope() bounds
// at hb_itemGetCLen(), i.e. TRIMMED, so a bound on a single-field C(8) tag
// arrives shorter than the stored (space-padded) key. That must still select
// the row — now by prefix comparison instead of by padding the bound.
TEST_CASE("ordScope with a trimmed bound on a single-field tag") {
    auto dir = fs::temp_directory_path() / "openads_scope_trimmed";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == AE_SUCCESS);

    UNSIGNED8 def[]   = "ORD,C,8,0";
    UNSIGNED8 tname[] = "wo.dbf";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX,
                           0, 0, 0, 0, def, &hT) == AE_SUCCESS);

    UNSIGNED8 fOrd[] = "ORD";
    for (const char* v : {"WO1     ", "WO2     ", "WO2B    ", "WO3     "}) {
        REQUIRE(AdsAppendRecord(hT) == AE_SUCCESS);
        AdsSetString(hT, fOrd, (UNSIGNED8*)v, 8);
    }
    REQUIRE(AdsWriteRecord(hT) == AE_SUCCESS);

    UNSIGNED8 ixBag[260]{};  std::strncpy((char*)ixBag, "wo.cdx", 259);
    UNSIGNED8 ixName[64]{};  std::strncpy((char*)ixName, "TAGO", 63);
    UNSIGNED8 ixExpr[128]{}; std::strncpy((char*)ixExpr, "ORD", 127);
    ADSHANDLE hIx = 0;
    REQUIRE(AdsCreateIndex61(hT, ixBag, ixName, ixExpr,
                             nullptr, nullptr, 0, 0, &hIx) == AE_SUCCESS);

    // "WO2" trimmed, against stored "WO2     " — the row must be in scope.
    // Prefix semantics also pull in "WO2B", which is what Clipper does.
    set_scope(hIx, ADS_TOP,    std::string("WO2"));
    set_scope(hIx, ADS_BOTTOM, std::string("WO2"));
    CHECK(walk_count(hT) == 2);

    AdsClearScope(hIx, ADS_TOP);
    AdsClearScope(hIx, ADS_BOTTOM);
    CHECK(walk_count(hT) == 4);

    AdsCloseTable(hT);
    AdsDisconnect(hConn);
    fs::remove_all(dir, ec);
}
