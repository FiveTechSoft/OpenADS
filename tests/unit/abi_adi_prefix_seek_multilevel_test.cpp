// abi_adi_prefix_seek_multilevel_test.cpp -- a partial (prefix) SEEK on an
// ADT table must find its group, including once the .ADI char-key tree has
// grown branch levels.
//
// AdiIndex::compare_keys_ compares min(a, b, key_total_len_) bytes, so the
// dense-leaf scan honours a seek key shorter than the index key -- the same
// prefix rule the CDX side follows. The char-key BRANCH descent did not: it
// memcmp'd key_total_len_ bytes against the separator while the folded
// search key held only the bytes the caller supplied, reading past the end
// of that buffer whenever the separator's own prefix equalled the search
// prefix.
//
// The read is out of bounds, but no input tried here turns it into a wrong
// answer: the bytes that follow a std::string compare low, which picks the
// same child the prefix rule wants. So this file is coverage, not a
// reproducer -- the ADT/ADI side of the partial-seek rule had no test at
// all, while the CDX side has had one since the seek and scope fixes.
//
// Both cases sweep EVERY key rather than probing one: a single wrong branch
// decision shows up as one miss in 1500, and any single probe is likely to
// land mid-page and pass regardless. The tree has to be multi-level for the
// branch descent to run at all -- with one dense leaf the loop never
// executes -- hence the row counts.
//
// The key is a single wide character field rather than a compound
// expression, so the ADI v1 tag every build can create is enough.

#include "doctest.h"
#include "openads/ace.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {

// Build an ADT whose only field is a KEYLEN-wide character key, one row per
// (doc, line) pair, then index it. Sweeps every doc with a PREFIXLEN-byte
// partial seek and reports how many missed.
struct Sweep {
    int misses    = 0;
    int wrong_row = 0;
    int first_bad = 0;
};

Sweep run_sweep(const char* dirname, const char* table, const char* bag,
                int keylen, int prefixlen, int docs, int lines) {
    fs::path tmp = fs::temp_directory_path() / dirname;
    {
        std::error_code ec;
        fs::remove_all(tmp, ec);
        fs::create_directories(tmp, ec);
    }

    UNSIGNED8 srv[260]{};
    std::memcpy(srv, tmp.string().c_str(), tmp.string().size());
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn)
            == AE_SUCCESS);

    char flddef[64];
    std::snprintf(flddef, sizeof(flddef), "K,Character,%d", keylen);
    ADSHANDLE hTable = 0;
    REQUIRE(AdsCreateTable(hConn, (UNSIGNED8*)table, nullptr, ADS_ADT,
                           ADS_ANSI, 0, 0, 0, (UNSIGNED8*)flddef, &hTable)
            == AE_SUCCESS);

    // Key layout: "2P" + doc(8) + line(6), right-padded to keylen. The first
    // 10 bytes are the "document" the partial seek supplies, mirroring the
    // ERP's SEEK cCodigoCon + cDocumeTra over a con+doc+seq tag.
    for (int d = 1; d <= docs; ++d) {
        for (int s = 1; s <= lines; ++s) {
            char key[128];
            std::snprintf(key, sizeof(key), "2P%08d%6d", d, s);
            std::size_t n = std::strlen(key);
            while (n < static_cast<std::size_t>(keylen)) key[n++] = ' ';
            key[keylen] = '\0';
            REQUIRE(AdsAppendRecord(hTable) == AE_SUCCESS);
            REQUIRE(AdsSetString(hTable, (UNSIGNED8*)"K", (UNSIGNED8*)key,
                                 static_cast<UNSIGNED32>(keylen))
                    == AE_SUCCESS);
            REQUIRE(AdsWriteRecord(hTable) == AE_SUCCESS);
        }
    }

    ADSHANDLE hIdx = 0;
    REQUIRE(AdsCreateIndex61(hTable, (UNSIGNED8*)bag, (UNSIGNED8*)"TAG1",
                             (UNSIGNED8*)"K", nullptr, nullptr, 0, 0, &hIdx)
            == AE_SUCCESS);

    Sweep sw;
    for (int d = 1; d <= docs; ++d) {
        char key[128];
        std::snprintf(key, sizeof(key), "2P%08d%6d", d, 1);
        REQUIRE(AdsGotoTop(hTable) == AE_SUCCESS);
        UNSIGNED16 found = 0;
        REQUIRE(AdsSeek(hIdx, (UNSIGNED8*)key,
                        static_cast<UNSIGNED16>(prefixlen),
                        ADS_STRINGKEY, 0, &found) == AE_SUCCESS);
        if (!found) {
            if (sw.first_bad == 0) sw.first_bad = d;
            ++sw.misses;
            continue;
        }
        // A partial seek lands on the FIRST entry of the group.
        UNSIGNED8 buf[128]{};
        UNSIGNED32 len = sizeof(buf);
        REQUIRE(AdsGetString(hTable, (UNSIGNED8*)"K", buf, &len, 0)
                == AE_SUCCESS);
        char want[128];
        std::snprintf(want, sizeof(want), "2P%08d%6d", d, 1);
        if (std::string((char*)buf).compare(0, std::strlen(want), want) != 0) {
            if (sw.first_bad == 0) sw.first_bad = d;
            ++sw.wrong_row;
        }
    }

    // A prefix that matches nothing must still miss.
    char absent[128];
    std::snprintf(absent, sizeof(absent), "2P%08d%6d", docs + 77, 1);
    REQUIRE(AdsGotoTop(hTable) == AE_SUCCESS);
    UNSIGNED16 found = 1;
    REQUIRE(AdsSeek(hIdx, (UNSIGNED8*)absent,
                    static_cast<UNSIGNED16>(prefixlen),
                    ADS_STRINGKEY, 0, &found) == AE_SUCCESS);
    CHECK(found == 0);

    AdsCloseTable(hTable);
    AdsDisconnect(hConn);
    std::error_code ec;
    fs::remove_all(tmp, ec);
    return sw;
}

} // namespace

TEST_CASE("ADI: partial SEEK finds its group through a multi-level tree") {
    // 16-byte key, 10-byte prefix: the folded search key still fits
    // std::string's small-buffer optimisation.
    Sweep sw = run_sweep("openads_adi_prefix_seek", "mov.adt", "mov.adi",
                         /*keylen=*/16, /*prefixlen=*/10,
                         /*docs=*/1500, /*lines=*/4);
    CAPTURE(sw.first_bad);
    CHECK(sw.misses    == 0);
    CHECK(sw.wrong_row == 0);
}

TEST_CASE("ADI: partial SEEK on a wide key through a multi-level tree") {
    // 36-byte key, 16-byte prefix: past the small-buffer optimisation, so
    // whatever follows the search key is heap content rather than the tail
    // of an inline buffer.
    Sweep sw = run_sweep("openads_adi_prefix_seek_wide", "movw.adt",
                         "movw.adi", /*keylen=*/36, /*prefixlen=*/16,
                         /*docs=*/1500, /*lines=*/2);
    CAPTURE(sw.first_bad);
    CHECK(sw.misses    == 0);
    CHECK(sw.wrong_row == 0);
}
