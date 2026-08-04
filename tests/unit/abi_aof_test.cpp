// M-AOF.3 — AdsSetAOF / AdsClearAOF / AdsGetAOFOptLevel.
// Builds a 4-row 2-column DBF, opens it through the public ABI,
// installs an AOF expression, and asserts that Skip / GoTop walk
// only the visible records. Then clears the AOF and walks the
// full set again to make sure the filter is truly released.

#include "doctest.h"
#include "openads/ace.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

namespace {

fs::path make_fixture(const char* tag) {
    auto p = fs::temp_directory_path() / (std::string("openads_aof_abi_") + tag + ".dbf");
    fs::remove(p);

    constexpr std::uint16_t header_size = 32 + 32 + 32 + 1;
    constexpr std::uint16_t record_size = 1 + 5 + 3;

    std::vector<std::uint8_t> file;
    std::array<std::uint8_t, 32> hdr{};
    hdr[0] = 0x03;
    hdr[1] = 124; hdr[2] = 1; hdr[3] = 31;
    hdr[4] = 4; hdr[5] = 0; hdr[6] = 0; hdr[7] = 0;
    hdr[8] = static_cast<std::uint8_t>(header_size & 0xFF);
    hdr[9] = static_cast<std::uint8_t>((header_size >> 8) & 0xFF);
    hdr[10] = static_cast<std::uint8_t>(record_size & 0xFF);
    hdr[11] = static_cast<std::uint8_t>((record_size >> 8) & 0xFF);
    file.insert(file.end(), hdr.begin(), hdr.end());

    auto push_field = [&](const char* name, char type,
                          std::uint8_t length) {
        std::array<std::uint8_t, 32> fd{};
        std::strncpy(reinterpret_cast<char*>(fd.data()), name, 11);
        fd[11] = static_cast<std::uint8_t>(type);
        fd[16] = length;
        file.insert(file.end(), fd.begin(), fd.end());
    };
    push_field("NAME", 'C', 5);
    push_field("AGE",  'N', 3);
    file.push_back(0x0D);

    auto push_rec = [&](const char* name, const char* age) {
        file.push_back(' ');
        for (int i = 0; i < 5; ++i) {
            file.push_back(static_cast<std::uint8_t>(
                i < static_cast<int>(std::strlen(name)) ? name[i] : ' '));
        }
        std::string a(age);
        while (a.size() < 3) a.insert(a.begin(), ' ');
        for (char c : a) file.push_back(static_cast<std::uint8_t>(c));
    };
    push_rec("AAA", "25");
    push_rec("BBB", "42");
    push_rec("CCC", "30");
    push_rec("DDD", "18");
    file.push_back(0x1A);

    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
    return p;
}

} // namespace

TEST_CASE("AdsSetAOF: Skip walks only matching records") {
    auto p = make_fixture("walk");
    auto dir = p.parent_path().string();
    auto base = p.filename().string();
    {
        ADSHANDLE hConn = 0;
        REQUIRE(AdsConnect60(reinterpret_cast<UNSIGNED8*>(dir.data()),
                             ADS_LOCAL_SERVER, nullptr, nullptr,
                             ADS_DEFAULT, &hConn) == 0);
        ADSHANDLE hT = 0;
        REQUIRE(AdsOpenTable(hConn,
                             reinterpret_cast<UNSIGNED8*>(base.data()),
                             nullptr, ADS_CDX, ADS_ANSI, 0, 0, 0,
                             &hT) == 0);

        // AOF: AGE >= 25 — passes for AAA(25), BBB(42), CCC(30); fails DDD(18)
        std::string cond = "AGE >= 25";
        REQUIRE(AdsSetAOF(hT,
                          reinterpret_cast<UNSIGNED8*>(cond.data()),
                          0) == 0);

        // OptLevel reports NONE today (V1 full-scan path).
        UNSIGNED16 lvl = 99;
        UNSIGNED16 buflen = 0;
        REQUIRE(AdsGetAOFOptLevel(hT, &lvl, nullptr, &buflen) == 0);
        CHECK(lvl == ADS_OPTIMIZED_NONE);

        // Walk: GoTop -> rec 1 (AAA, 25) — passes filter.
        REQUIRE(AdsGotoTop(hT) == 0);
        UNSIGNED32 r = 0;
        REQUIRE(AdsGetRecordNum(hT, ADS_IGNOREFILTERS, &r) == 0);
        CHECK(r == 1);

        REQUIRE(AdsSkip(hT, 1) == 0);
        REQUIRE(AdsGetRecordNum(hT, ADS_IGNOREFILTERS, &r) == 0);
        CHECK(r == 2);                         // BBB, 42

        REQUIRE(AdsSkip(hT, 1) == 0);
        REQUIRE(AdsGetRecordNum(hT, ADS_IGNOREFILTERS, &r) == 0);
        CHECK(r == 3);                         // CCC, 30

        // Next skip would land on rec 4 (DDD, 18) which fails AOF
        // — Skip must drive past it to EoF instead.
        REQUIRE(AdsSkip(hT, 1) == 0);
        UNSIGNED16 eof = 0;
        REQUIRE(AdsAtEOF(hT, &eof) == 0);
        CHECK(eof != 0);

        // Clear AOF — full table visible again.
        REQUIRE(AdsClearAOF(hT) == 0);
        REQUIRE(AdsGotoTop(hT) == 0);
        REQUIRE(AdsGetRecordNum(hT, ADS_IGNOREFILTERS, &r) == 0);
        CHECK(r == 1);
        REQUIRE(AdsSkip(hT, 3) == 0);
        REQUIRE(AdsGetRecordNum(hT, ADS_IGNOREFILTERS, &r) == 0);
        CHECK(r == 4);                         // DDD, 18 — back in the walk

        AdsCloseTable(hT);
        AdsDisconnect(hConn);
    }
    fs::remove(p);
}

TEST_CASE("AdsContinue: walks filter-matching records in order") {
    // Fixture: AAA/25, BBB/42, CCC/30, DDD/18
    // AOF:  AGE >= 25  →  records 1, 2, 3 pass; record 4 (DDD/18) does not.
    auto p = make_fixture("cont");
    auto dir  = p.parent_path().string();
    auto base = p.filename().string();
    {
        ADSHANDLE hConn = 0;
        REQUIRE(AdsConnect60(reinterpret_cast<UNSIGNED8*>(dir.data()),
                             ADS_LOCAL_SERVER, nullptr, nullptr,
                             ADS_DEFAULT, &hConn) == 0);
        ADSHANDLE hT = 0;
        REQUIRE(AdsOpenTable(hConn,
                             reinterpret_cast<UNSIGNED8*>(base.data()),
                             nullptr, ADS_CDX, ADS_ANSI, 0, 0, 0,
                             &hT) == 0);

        std::string cond = "AGE >= 25";
        REQUIRE(AdsSetAOF(hT,
                          reinterpret_cast<UNSIGNED8*>(cond.data()),
                          0) == 0);

        // GoTop lands on rec 1 (AAA/25 — passes filter).
        REQUIRE(AdsGotoTop(hT) == 0);
        UNSIGNED32 r = 0;
        REQUIRE(AdsGetRecordNum(hT, ADS_IGNOREFILTERS, &r) == 0);
        CHECK(r == 1);

        // Continue → rec 2 (BBB/42)
        UNSIGNED16 found = 0;
        REQUIRE(AdsContinue(hT, &found) == 0);
        CHECK(found == 1);
        REQUIRE(AdsGetRecordNum(hT, ADS_IGNOREFILTERS, &r) == 0);
        CHECK(r == 2);

        // Continue → rec 3 (CCC/30)
        found = 0;
        REQUIRE(AdsContinue(hT, &found) == 0);
        CHECK(found == 1);
        REQUIRE(AdsGetRecordNum(hT, ADS_IGNOREFILTERS, &r) == 0);
        CHECK(r == 3);

        // Continue → skips rec 4 (DDD/18 fails filter) → EOF, not found
        found = 1;
        REQUIRE(AdsContinue(hT, &found) == 0);
        CHECK(found == 0);
        UNSIGNED16 eof = 0;
        REQUIRE(AdsAtEOF(hT, &eof) == 0);
        CHECK(eof != 0);

        AdsCloseTable(hT);
        AdsDisconnect(hConn);
    }
    fs::remove(p);
}

TEST_CASE("AdsSetAOF: non-optimisable AOF succeeds with OPTIMIZED_NONE") {
    auto p = make_fixture("badparse");
    auto dir = p.parent_path().string();
    auto base = p.filename().string();
    {
        ADSHANDLE hConn = 0;
        REQUIRE(AdsConnect60(reinterpret_cast<UNSIGNED8*>(dir.data()),
                             ADS_LOCAL_SERVER, nullptr, nullptr,
                             ADS_DEFAULT, &hConn) == 0);
        ADSHANDLE hT = 0;
        REQUIRE(AdsOpenTable(hConn,
                             reinterpret_cast<UNSIGNED8*>(base.data()),
                             nullptr, ADS_CDX, ADS_ANSI, 0, 0, 0,
                             &hT) == 0);

        // Empty(NAME) is outside the optimisable AOF subset.
        // Real ADS errors when it cannot build a server-side AOF,
        // and stock rddads decides whether to run its own
        // client-side row filter purely from AdsSetAOF's return
        // value — it does NOT call AdsGetAOFOptLevel. So
        // AdsSetAOF must return a non-success code here, causing
        // rddads to fall back to client-side filtering.
        std::string cond = "Empty(NAME)";
        UNSIGNED32 rc = AdsSetAOF(hT,
                          reinterpret_cast<UNSIGNED8*>(cond.data()),
                          0);
        CHECK(rc == AE_INVALID_EXPRESSION);

        AdsCloseTable(hT);
        AdsDisconnect(hConn);
    }
    fs::remove(p);
}

TEST_CASE("AdsGetLastTableUpdate: returns the DBF header date string") {
    auto p = make_fixture("lastupd");
    auto dir = p.parent_path().string();
    auto base = p.filename().string();
    {
        ADSHANDLE hConn = 0;
        REQUIRE(AdsConnect60(reinterpret_cast<UNSIGNED8*>(dir.data()),
                             ADS_LOCAL_SERVER, nullptr, nullptr,
                             ADS_DEFAULT, &hConn) == 0);
        ADSHANDLE hT = 0;
        REQUIRE(AdsOpenTable(hConn,
                             reinterpret_cast<UNSIGNED8*>(base.data()),
                             nullptr, ADS_CDX, ADS_ANSI, 0, 0, 0,
                             &hT) == 0);

        // make_fixture writes header bytes 1..3 = {124, 1, 31}
        // (year 1900+124 = 2024, month 1, day 31).
        UNSIGNED8 fmt[] = "CCYY-MM-DD";
        CHECK(AdsSetDateFormat(fmt) == 0);

        UNSIGNED8  buf[32] = {0};
        UNSIGNED16 len = sizeof(buf);
        CHECK(AdsGetLastTableUpdate(hT, buf, &len) == 0);
        CHECK(std::string(reinterpret_cast<char*>(buf)) == "2024-01-31");
        CHECK(len == 10);

        // The date format is process-global - restore SAP's default so
        // later tests (abi_date_format_test) see it.
        UNSIGNED8 def_fmt[] = "MM/DD/CCYY";
        CHECK(AdsSetDateFormat(def_fmt) == 0);

        AdsCloseTable(hT);
        AdsDisconnect(hConn);
    }
    fs::remove(p);
}

TEST_CASE("AdsCreateTable: stamps the header last-update with today's date") {
    // Robert van der Hulst report: a freshly created+opened table
    // reported "1900-00-00" until the first DbAppend rewrote the
    // header. AdsCreateTable must stamp the DBF header on creation,
    // matching what a real ADS server does.
    auto p   = fs::temp_directory_path() / "openads_aof_abi_newtbl.dbf";
    fs::remove(p);
    auto dir  = p.parent_path().string();
    auto base = p.filename().string();

    // Expected stamp: today's UTC date — same clock the create path uses.
    std::time_t now = std::time(nullptr);
    std::tm tm_utc{};
#ifdef _WIN32
    gmtime_s(&tm_utc, &now);
#else
    gmtime_r(&now, &tm_utc);
#endif
    char expected[48] = {0};
    std::snprintf(expected, sizeof(expected), "%04d-%02d-%02d",
                  tm_utc.tm_year + 1900, tm_utc.tm_mon + 1, tm_utc.tm_mday);

    {
        ADSHANDLE hConn = 0;
        REQUIRE(AdsConnect60(reinterpret_cast<UNSIGNED8*>(dir.data()),
                             ADS_LOCAL_SERVER, nullptr, nullptr,
                             ADS_DEFAULT, &hConn) == 0);

        ADSHANDLE hT = 0;
        UNSIGNED8 fields[] = "NAME,Character,10;AGE,Numeric,3,0";
        REQUIRE(AdsCreateTable(hConn,
                               reinterpret_cast<UNSIGNED8*>(base.data()),
                               nullptr, ADS_CDX, ADS_ANSI, 0, 0,
                               0, fields, &hT) == 0);

        UNSIGNED8 fmt[] = "CCYY-MM-DD";
        CHECK(AdsSetDateFormat(fmt) == 0);

        UNSIGNED8  buf[32] = {0};
        UNSIGNED16 len = sizeof(buf);
        CHECK(AdsGetLastTableUpdate(hT, buf, &len) == 0);
        CHECK(std::string(reinterpret_cast<char*>(buf)) == expected);
        CHECK(len == 10);

        // Restore the process-global date format (see above).
        UNSIGNED8 def_fmt[] = "MM/DD/CCYY";
        CHECK(AdsSetDateFormat(def_fmt) == 0);

        AdsCloseTable(hT);
        AdsDisconnect(hConn);
    }
    fs::remove(p);
}
