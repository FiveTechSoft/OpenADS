// abi_prefix_seek_deleted_test.cpp — partial (prefix) SEEK must report
// Found() = true also when SET DELETED is ON.
//
// Clipper / DBFCDX / SAP-ACE semantics: a seek key SHORTER than the index
// key matches on the prefix. cdx_prefix_seek_test.cpp already covers that
// at the CdxIndex layer, but Table::seek_key re-derived the `exact` flag
// when deleted rows are hidden, and it did so by padding the search key out
// to the full index key width with spaces:
//
//     del_key.resize(idx->key_length(), ' ');
//     ck.resize(del_key.size(), ' ');
//     exact = (ck == del_key);
//
// For a partial key that compares "CON+DOC" + 6 pad spaces against the
// stored "CON+DOC" + "     1", so it NEVER matched: the cursor landed on the
// right record but AdsIsFound reported 0. Reported by RusSoft (Zerus ERP):
// legacy code does `SEEK cCodigoCon+cDocumeTra` over a
// `cCodigoCon+cDocumeTra+STR(nSecuenMov,6,0)` tag and then `IF !FOUND()`,
// so every document read as missing on the ADS path while DBFCDX found it.
//
// Index key here: ID(2) + DOC(8) + SEQ(6) = 16 bytes; the seek uses the
// 10-byte ID+DOC prefix.

#include "doctest.h"
#include "openads/ace.h"

#include <cstring>
#include <cstdio>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {

// Seek `key` on hIndex and report the engine's found flag.
UNSIGNED16 seek_found(ADSHANDLE hT, ADSHANDLE hIx, const std::string& key) {
    AdsGotoTop(hT);
    UNSIGNED16 found = 99;
    UNSIGNED8 buf[64];
    std::memcpy(buf, key.c_str(), key.size() + 1);
    // usSeekType 0 = hard (exact) seek; 1 would be soft.
    AdsSeek(hIx, buf, static_cast<UNSIGNED16>(key.size()),
            ADS_STRINGKEY, 0, &found);
    return found;
}

} // namespace

TEST_CASE("partial prefix SEEK keeps Found() true with SET DELETED ON") {
    auto dir = fs::temp_directory_path() / "openads_prefix_seek_del";
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

    UNSIGNED8 fId[]  = "ID";
    UNSIGNED8 fDoc[] = "DOC";
    UNSIGNED8 fSeq[] = "SEQ";

    // Three documents, two line items each. The suffix (SEQ) is what the
    // partial seek does NOT supply — right-aligned like STR(n,6,0).
    struct Row { const char* id; const char* doc; const char* seq; };
    const Row rows[] = {
        {"2B", "   22563", "     1"}, {"2B", "   22563", "     2"},
        {"2P", "  972299", "     1"}, {"2P", "  972299", "     2"},
        {"9Z", "   99999", "     1"}, {"9Z", "   99999", "     2"},
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
    const std::string full   = prefix + "     1";            // 16 bytes
    const std::string absent = "ZZ" "  999999";              // 10 bytes, no match

    SUBCASE("SET DELETED OFF (show deleted)") {
        AdsShowDeleted(1);
        CHECK(seek_found(hT, hIx, prefix) == 1);
        CHECK(seek_found(hT, hIx, full)   == 1);
        CHECK(seek_found(hT, hIx, absent) == 0);
    }

    SUBCASE("SET DELETED ON (hide deleted) — the regression") {
        AdsShowDeleted(0);
        CHECK(seek_found(hT, hIx, prefix) == 1);
        CHECK(seek_found(hT, hIx, full)   == 1);
        CHECK(seek_found(hT, hIx, absent) == 0);
    }

    SUBCASE("first line of the group deleted: prefix still finds the second") {
        AdsShowDeleted(1);
        // Delete only "2P/972299/1" — the group's first entry.
        REQUIRE(seek_found(hT, hIx, full) == 1);
        REQUIRE(AdsDeleteRecord(hT) == 0);
        REQUIRE(AdsWriteRecord(hT) == 0);

        AdsShowDeleted(0);
        // Clipper/DBFCDX: the group still exists (seq 2 is live) -> found.
        CHECK(seek_found(hT, hIx, prefix) == 1);
        UNSIGNED8 got[16];
        UNSIGNED32 len = sizeof(got);
        REQUIRE(AdsGetString(hT, fSeq, got, &len, ADS_NONE) == 0);
        CHECK(std::string((char*)got, 6) == "     2");
    }

    SUBCASE("whole group deleted: prefix must miss") {
        AdsShowDeleted(1);
        for (const char* seq : {"     1", "     2"}) {
            REQUIRE(seek_found(hT, hIx, prefix + seq) == 1);
            REQUIRE(AdsDeleteRecord(hT) == 0);
        }
        REQUIRE(AdsWriteRecord(hT) == 0);

        AdsShowDeleted(0);
        CHECK(seek_found(hT, hIx, prefix) == 0);
    }

    AdsShowDeleted(1);
    AdsCloseTable(hT);
    AdsDisconnect(hConn);
    fs::remove_all(dir, ec);
}
