// abi_seek_last_partial_test.cpp — AdsSeekLast must land on the LAST entry
// of the matching key group, including when the search key is a PREFIX of
// the index key.
//
// Clipper / DBFCDX / SAP-ACE semantics: `DBSEEK( key, lSoft, .T. )` maps to
// AdsSeekLast, and hb_cdxSeek with fFindLast = TRUE positions on the last
// record of the equal-key run — with a partial key, the last record whose
// key STARTS WITH the supplied bytes. rddads sends the ERP's
// `DBSEEK( cCon + cDoc, .F., .T. )` (get the last line of a document)
// straight through as AdsSeekLast.
//
// Table::seek_key already implements the walk (its `last` parameter walks
// forward across the equal-key run using prefix comparison, and steps back
// over deleted rows), but no caller ever passed it: the local branch of
// AdsSeekLast delegated to AdsSeek, which always calls seek_key(key, soft)
// with last defaulted to false. Result: AdsSeekLast returned the FIRST
// record of the group. On an ASC tag with duplicates that is a silent wrong
// row, not an error.
//
// Index key here: ID(2) + DOC(8) + SEQ(6) = 16 bytes; the partial seek
// supplies the 10-byte ID+DOC prefix.

#include "doctest.h"
#include "openads/ace.h"

#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {

UNSIGNED8 fId[]  = "ID";
UNSIGNED8 fDoc[] = "DOC";
UNSIGNED8 fSeq[] = "SEQ";

// Seek `key` with fLast semantics and report the engine's found flag.
UNSIGNED16 seek_last_found(ADSHANDLE hT, ADSHANDLE hIx, const std::string& key) {
    AdsGotoTop(hT);
    UNSIGNED16 found = 99;
    UNSIGNED8 buf[64];
    std::memcpy(buf, key.c_str(), key.size() + 1);
    AdsSeekLast(hIx, buf, static_cast<UNSIGNED16>(key.size()),
                ADS_STRINGKEY, &found);
    return found;
}

// Plain hard seek, for the contrast case (first of the group).
UNSIGNED16 seek_found(ADSHANDLE hT, ADSHANDLE hIx, const std::string& key) {
    AdsGotoTop(hT);
    UNSIGNED16 found = 99;
    UNSIGNED8 buf[64];
    std::memcpy(buf, key.c_str(), key.size() + 1);
    AdsSeek(hIx, buf, static_cast<UNSIGNED16>(key.size()),
            ADS_STRINGKEY, 0, &found);
    return found;
}

std::string cur_seq(ADSHANDLE hT) {
    UNSIGNED8 got[16];
    UNSIGNED32 len = sizeof(got);
    if (AdsGetString(hT, fSeq, got, &len, ADS_NONE) != 0) return "<err>";
    return std::string(reinterpret_cast<char*>(got), 6);
}

} // namespace

TEST_CASE("AdsSeekLast lands on the last entry of the key group") {
    auto dir = fs::temp_directory_path() / "openads_seek_last_partial";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);

    UNSIGNED8 def[]   = "ID,C,2,0;DOC,C,8,0;SEQ,C,6,0";
    UNSIGNED8 tname[] = "mov.dbf";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX,
                           0, 0, 0, 0, def, &hT) == 0);

    // Two documents; the middle one has three line items so "first" and
    // "last" of the group are distinguishable, and a neighbouring group
    // exists on each side so a runaway walk would be visible.
    struct Row { const char* id; const char* doc; const char* seq; };
    const Row rows[] = {
        {"2B", "   22563", "     1"},
        {"2P", "  972299", "     1"},
        {"2P", "  972299", "     2"},
        {"2P", "  972299", "     3"},
        {"9Z", "   99999", "     1"},
    };
    for (const auto& r : rows) {
        REQUIRE(AdsAppendRecord(hT) == 0);
        AdsSetString(hT, fId,  (UNSIGNED8*)r.id,  2);
        AdsSetString(hT, fDoc, (UNSIGNED8*)r.doc, 8);
        AdsSetString(hT, fSeq, (UNSIGNED8*)r.seq, 6);
    }
    REQUIRE(AdsWriteRecord(hT) == 0);

    UNSIGNED8 ixBag[260]{};  std::strncpy((char*)ixBag, "mov.cdx", 259);
    UNSIGNED8 ixName[64]{};  std::strncpy((char*)ixName, "TAG1", 63);
    UNSIGNED8 ixExpr[128]{}; std::strncpy((char*)ixExpr, "ID+DOC+SEQ", 127);
    ADSHANDLE hIx = 0;                          // 16-byte key
    REQUIRE(AdsCreateIndex61(hT, ixBag, ixName, ixExpr,
                             nullptr, nullptr, 0, 0, &hIx) == AE_SUCCESS);

    const std::string prefix = "2P" "  972299";              // 10 bytes
    const std::string full   = prefix + "     2";            // 16 bytes
    const std::string absent = "ZZ" "  999999";              // 10 bytes

    SUBCASE("partial key, SET DELETED OFF") {
        AdsShowDeleted(1);
        // Contrast: the plain seek stays on the FIRST of the group.
        REQUIRE(seek_found(hT, hIx, prefix) == 1);
        CHECK(cur_seq(hT) == "     1");

        CHECK(seek_last_found(hT, hIx, prefix) == 1);
        CHECK(cur_seq(hT) == "     3");
    }

    SUBCASE("partial key, SET DELETED ON") {
        AdsShowDeleted(0);
        CHECK(seek_last_found(hT, hIx, prefix) == 1);
        CHECK(cur_seq(hT) == "     3");
    }

    SUBCASE("full key still lands on its own entry") {
        AdsShowDeleted(1);
        CHECK(seek_last_found(hT, hIx, full) == 1);
        CHECK(cur_seq(hT) == "     2");
    }

    SUBCASE("no match reports not found") {
        AdsShowDeleted(1);
        CHECK(seek_last_found(hT, hIx, absent) == 0);
    }

    SUBCASE("last of the group deleted: falls back to the previous live one") {
        AdsShowDeleted(1);
        REQUIRE(seek_found(hT, hIx, prefix + "     3") == 1);
        REQUIRE(AdsDeleteRecord(hT) == 0);
        REQUIRE(AdsWriteRecord(hT) == 0);

        AdsShowDeleted(0);
        CHECK(seek_last_found(hT, hIx, prefix) == 1);
        CHECK(cur_seq(hT) == "     2");
    }

    SUBCASE("whole group deleted: must miss") {
        AdsShowDeleted(1);
        for (const char* seq : {"     1", "     2", "     3"}) {
            REQUIRE(seek_found(hT, hIx, prefix + seq) == 1);
            REQUIRE(AdsDeleteRecord(hT) == 0);
        }
        REQUIRE(AdsWriteRecord(hT) == 0);

        AdsShowDeleted(0);
        CHECK(seek_last_found(hT, hIx, prefix) == 0);
    }

    AdsShowDeleted(1);
    AdsCloseTable(hT);
    AdsDisconnect(hConn);
    fs::remove_all(dir, ec);
}
