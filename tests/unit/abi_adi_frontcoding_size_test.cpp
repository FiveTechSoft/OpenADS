// Front-coding size oracle: a v2 char-key .adi must store its dense leaves
// front-coded (dup/trail), so an index over keys with a long shared prefix
// occupies FAR fewer leaf pages than the uncompressed "full key per entry"
// layout would — parity with ADS-SAP (~3x smaller). The build must also stay
// fully navigable (every key visited once, ascending) so the size win is not
// bought with corruption.
#include <cstdlib>
#include "doctest.h"
#include "openads/ace.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

namespace {
// The v2 tag layout is opt-in (OPENADS_ADI_V2). These cases exercise it, so
// they turn it on for their own duration and put it back afterwards — the rest
// of the suite must keep running against the legacy layout.
struct AdiV2Scope {
    AdiV2Scope()  { _putenv_s("OPENADS_ADI_V2", "1"); }
    ~AdiV2Scope() { _putenv_s("OPENADS_ADI_V2", ""); }
};
} // namespace

namespace fs = std::filesystem;

namespace {
std::string trim_sp(std::string s) {
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}
} // namespace

TEST_CASE("ADI: front-coded leaf shrinks an index over high-prefix keys") {
    AdiV2Scope _v2;
    fs::path tmp = fs::temp_directory_path() / "openads_adi_frontcoding";
    { std::error_code ec; fs::create_directories(tmp, ec);
      fs::remove(tmp / "fc.adt", ec); fs::remove(tmp / "fc.adi", ec); }

    UNSIGNED8 srv[260]{};
    std::memcpy(srv, tmp.string().c_str(), tmp.string().size());
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn)
            == AE_SUCCESS);

    // Key width 20; the first 16 bytes are constant across every row, so the
    // shared prefix is large and front-coding is highly effective.
    const std::uint32_t KLEN = 20;
    UNSIGNED8 tbl[]    = "fc.adt";
    UNSIGNED8 flddef[] = "Code,Character,20";
    ADSHANDLE hTable   = 0;
    REQUIRE(AdsCreateTable(hConn, tbl, nullptr, ADS_ADT, ADS_ANSI, 0, 0, 0,
                           flddef, &hTable) == AE_SUCCESS);

    const int N = 4000;
    for (int i = 0; i < N; ++i) {
        char code[32];
        std::snprintf(code, sizeof(code), "CONSTANTPREFIX__%04d", i);  // 16 + 4
        REQUIRE(AdsAppendRecord(hTable) == AE_SUCCESS);
        REQUIRE(AdsSetString(hTable, (UNSIGNED8*)"Code", (UNSIGNED8*)code, KLEN)
                == AE_SUCCESS);
        REQUIRE(AdsWriteRecord(hTable) == AE_SUCCESS);
    }

    UNSIGNED8 idxfile[] = "fc.adi";
    ADSHANDLE hIdx = 0;
    REQUIRE(AdsCreateIndex61(hTable, idxfile, (UNSIGNED8*)"CODE",
                             (UNSIGNED8*)"Code", nullptr, nullptr, 0, 0, &hIdx)
            == AE_SUCCESS);

    // Uncompressed leaf layout would store recno(4)+key(KLEN) per entry, packing
    // floor(488/(4+KLEN)) entries per 512-byte leaf page. Front-coding must beat
    // that leaf-page footprint by a wide margin.
    std::error_code ec;
    const std::uintmax_t fsize = fs::file_size(tmp / "fc.adi", ec);
    REQUIRE(!ec);
    const std::uint32_t fixed_entry      = 4u + KLEN;                  // 24
    const std::uint32_t fixed_per_leaf   = (512u - 24u) / fixed_entry; // 20
    const std::uint32_t fixed_leaves     = (N + fixed_per_leaf - 1) / fixed_per_leaf;
    const std::uintmax_t fixed_leaf_bytes = std::uintmax_t(fixed_leaves) * 512u;

    // The whole front-coded file (header + branches + leaves) must fit in less
    // than the uncompressed LEAF pages alone — i.e. a clear, structural win.
    CHECK(fsize < fixed_leaf_bytes);
    // And it should be a big win, not a marginal one: < half the fixed leaves.
    CHECK(fsize * 2u < fixed_leaf_bytes);

    // Correctness: a full ordered walk visits every key exactly once, ascending.
    REQUIRE(AdsGotoTop(hTable) == AE_SUCCESS);
    int walked = 0;
    std::string prev;
    for (;;) {
        UNSIGNED16 eof = 0;
        REQUIRE(AdsAtEOF(hTable, &eof) == AE_SUCCESS);
        if (eof) break;
        UNSIGNED8 buf[32]{}; UNSIGNED32 len = sizeof(buf);
        REQUIRE(AdsGetString(hTable, (UNSIGNED8*)"Code", buf, &len, 0)
                == AE_SUCCESS);
        std::string cur = trim_sp(std::string(reinterpret_cast<char*>(buf), len));
        CHECK(cur > prev);
        prev = cur;
        ++walked;
        REQUIRE(AdsSkip(hTable, 1) == AE_SUCCESS);
    }
    CHECK(walked == N);

    // Seek a few scattered keys -> all found.
    for (int key : {0, 1, 1999, 2500, N - 1}) {
        char code[32];
        std::snprintf(code, sizeof(code), "CONSTANTPREFIX__%04d", key);
        UNSIGNED16 found = 0;
        REQUIRE(AdsSeek(hIdx, (UNSIGNED8*)code, KLEN, ADS_STRINGKEY, 0, &found)
                == AE_SUCCESS);
        CHECK(found != 0);
    }

    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
}
