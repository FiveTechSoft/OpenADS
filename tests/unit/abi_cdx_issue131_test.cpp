// Regression tests for GitHub issue #131 (rddads/ADS_LOCAL):
//   A — compound char-concat key Upper(SYM)+Upper(SUB)+DToS(DAT):
//       every operand must keep its fixed width (a blank middle operand
//       contributes its spaces); the engine stored a shrunk key and the
//       full-width Harbour seek missed.
//   B — DbSeek on a logical-type key: Harbour rddads sends the seek
//       value as the 1-byte strings "1"/"0" (ads1.c HB_IS_LOGICAL
//       branch) while the DBF index stores 'T'/'F'; SAP ACE maps the
//       seek value onto the stored form.
//   C — nested function composition Upper(PadR(LTrim(NAME),10)): the
//       expression evaluator did not know PADR/PADL/PADC and degraded
//       to an EMPTY key for every record.
//   + secondary observation: OrdKeyVal (AdsExtractKey) on a numeric
//     (FoxNumeric) tag returned ASCII text instead of the 8-byte
//     order-preserving binary key, so OrdKeyVal printed garbage.
//
// The table, data and expressions mirror the issue's self-contained
// repro: record #2 is the seek target (SYM fully filled 15/15, SUB
// blank, FLAG = .T.).

#include "doctest.h"
#include "openads/ace.h"
#include "engine/index_expr.h"

#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {

// Gregorian -> Julian Day Number (Fliegel-Van Flandern, same as
// Harbour's hb_dateEncode).
long jdn(int y, int m, int d) {
    long y32 = y, m32 = m, d32 = d;
    return (1461 * (y32 + 4800 + (m32 - 14) / 12)) / 4
         + (367  * (m32 - 2 - 12 * ((m32 - 14) / 12))) / 12
         - (3    * ((y32 + 4900 + (m32 - 14) / 12) / 100)) / 4
         + d32 - 32075;
}

struct Repro {
    fs::path  dir;
    ADSHANDLE hConn = 0;
    ADSHANDLE hT    = 0;

    Repro() {
        dir = fs::temp_directory_path() / "openads_issue131";
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir);

        UNSIGNED8 srv[256];
        std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
        REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                             nullptr, nullptr, 0, &hConn) == 0);

        UNSIGNED8 def[] =
            "SYM,C,15,0;SUB,C,6,0;DAT,D,8,0;FLAG,L,1,0;NAME,C,30,0";
        UNSIGNED8 tname[] = "repro";
        REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX,
                               0, 0, 0, 0, def, &hT) == 0);

        UNSIGNED8 fSYM[]  = "SYM";
        UNSIGNED8 fSUB[]  = "SUB";
        UNSIGNED8 fDAT[]  = "DAT";
        UNSIGNED8 fFLAG[] = "FLAG";
        UNSIGNED8 fNAME[] = "NAME";

        auto append = [&](const char* sym, const char* sub, int y, int m,
                          int d, bool flag, const char* name) {
            REQUIRE(AdsAppendRecord(hT) == 0);
            REQUIRE(AdsSetString(hT, fSYM,
                reinterpret_cast<UNSIGNED8*>(const_cast<char*>(sym)),
                static_cast<UNSIGNED16>(std::strlen(sym))) == 0);
            REQUIRE(AdsSetString(hT, fSUB,
                reinterpret_cast<UNSIGNED8*>(const_cast<char*>(sub)),
                static_cast<UNSIGNED16>(std::strlen(sub))) == 0);
            REQUIRE(AdsSetJulian(hT, fDAT,
                static_cast<SIGNED32>(jdn(y, m, d))) == 0);
            REQUIRE(AdsSetLogical(hT, fFLAG, flag ? 1 : 0) == 0);
            REQUIRE(AdsSetString(hT, fNAME,
                reinterpret_cast<UNSIGNED8*>(const_cast<char*>(name)),
                static_cast<UNSIGNED16>(std::strlen(name))) == 0);
            REQUIRE(AdsWriteRecord(hT) == 0);
        };

        append("AAA CORP",        "  ",    2025, 1, 1, false, "  alpha widget");
        append("KATE-LUX LEWIAT", "",      2026, 7, 1, true,  "  skawa item");
        append("ZZZ LTD",         "  ",    2024, 3, 1, false, "  omega thing");
    }

    ADSHANDLE make_tag(const char* tag, const char* expr) {
        UNSIGNED8 bag[16]  = "repro.cdx";
        UNSIGNED8 t_[16], e_[64];
        std::memcpy(t_, tag,  std::strlen(tag)  + 1);
        std::memcpy(e_, expr, std::strlen(expr) + 1);
        ADSHANDLE hI = 0;
        REQUIRE(AdsCreateIndex61(hT, bag, t_, e_,
                                 nullptr, nullptr, 0, 0, &hI) == 0);
        return hI;
    }

    // Hard seek expecting recno 2 (the issue's target record).
    void seek2(ADSHANDLE hI, const void* key, UNSIGNED16 len) {
        UNSIGNED16 found = 99;
        REQUIRE(AdsSeek(hI,
                        reinterpret_cast<UNSIGNED8*>(const_cast<void*>(key)),
                        len, ADS_STRINGKEY, ADS_HARDSEEK, &found) == 0);
        CHECK(found == 1);
        if (found == 1) {
            UNSIGNED32 recno = 0;
            REQUIRE(AdsGetRecordNum(hT, 0, &recno) == 0);
            CHECK(recno == 2u);
        }
    }

    ~Repro() {
        if (hT)    AdsCloseTable(hT);
        if (hConn) AdsDisconnect(hConn);
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
};

} // namespace

TEST_CASE("#131A: compound concat key keeps fixed operand widths") {
    Repro r;
    ADSHANDLE hI = r.make_tag("TA", "Upper(SYM)+Upper(SUB)+DToS(DAT)");

    // Key width must be 15 + 6 + 8 = 29 (field widths), not the shrunk
    // content length the engine used to store.
    UNSIGNED16 klen = 0;
    REQUIRE(AdsGetKeyLength(hI, &klen) == 0);
    CHECK(klen == 29);

    UNSIGNED32 cnt = 0;
    REQUIRE(AdsGetKeyCount(hI, 0, &cnt) == 0);
    CHECK(cnt == 3u);

    // The full-width key Harbour computes for record #2:
    // "KATE-LUX LEWIAT" (15) + "      " (blank SUB, 6) + "20260701" (8).
    const char key2[] = "KATE-LUX LEWIAT      20260701";
    r.seek2(hI, key2, sizeof(key2) - 1);

    // Record #1: "AAA CORP       " (15, padded) + "      " (6) + "20250101".
    {
        const char key1[] = "AAA CORP             20250101";
        UNSIGNED16 found = 99;
        REQUIRE(AdsSeek(hI,
            reinterpret_cast<UNSIGNED8*>(const_cast<char*>(key1)),
            sizeof(key1) - 1, ADS_STRINGKEY, ADS_HARDSEEK, &found) == 0);
        CHECK(found == 1);
        UNSIGNED32 recno = 0;
        REQUIRE(AdsGetRecordNum(r.hT, 0, &recno) == 0);
        CHECK(recno == 1u);
    }
    AdsCloseIndex(hI);
}

TEST_CASE("#131B: logical key seek maps rddads \"1\"/\"0\" onto 'T'/'F'") {
    Repro r;
    ADSHANDLE hI = r.make_tag("TB", "FLAG");

    UNSIGNED32 cnt = 0;
    REQUIRE(AdsGetKeyCount(hI, 0, &cnt) == 0);
    CHECK(cnt == 3u);

    // DbSeek(.T.) — rddads sends "1".
    {
        const char one = '1';
        r.seek2(hI, &one, 1);
    }
    // DbSeek(.F.) — rddads sends "0"; first .F. in key order is record 1.
    {
        const char zero = '0';
        UNSIGNED16 found = 99;
        REQUIRE(AdsSeek(hI,
            reinterpret_cast<UNSIGNED8*>(const_cast<char*>(&zero)),
            1, ADS_STRINGKEY, ADS_HARDSEEK, &found) == 0);
        CHECK(found == 1);
        UNSIGNED32 recno = 0;
        REQUIRE(AdsGetRecordNum(r.hT, 0, &recno) == 0);
        CHECK(recno == 1u);
    }
    // Direct 'T'/'F' seeks keep working too.
    {
        const char t = 'T';
        r.seek2(hI, &t, 1);
    }
    AdsCloseIndex(hI);
}

TEST_CASE("#131C: nested Upper(PadR(LTrim(NAME),10)) builds usable keys") {
    Repro r;
    ADSHANDLE hI = r.make_tag("TC", "Upper(PadR(LTrim(NAME),10))");

    UNSIGNED16 klen = 0;
    REQUIRE(AdsGetKeyLength(hI, &klen) == 0);
    CHECK(klen == 10);

    // Record #2: NAME "  skawa item" -> LTrim "skawa item" -> PadR 10
    // -> "skawa item" -> Upper "SKAWA ITEM".
    const char key2[] = "SKAWA ITEM";
    r.seek2(hI, key2, sizeof(key2) - 1);

    // No key in the tag may be empty (the pre-fix evaluator stored ""
    // for every record).
    UNSIGNED32 cnt = 0;
    REQUIRE(AdsGetKeyCount(hI, 0, &cnt) == 0);
    CHECK(cnt == 3u);
    AdsCloseIndex(hI);
}

TEST_CASE("#131 secondary: AdsExtractKey on a Val() tag returns FoxNumeric bytes") {
    auto dir = fs::temp_directory_path() / "openads_issue131_num";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);

    UNSIGNED8 def[]   = "NUMC,C,6,0";
    UNSIGNED8 tname[] = "numt";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX,
                           0, 0, 0, 0, def, &hT) == 0);
    UNSIGNED8 fNUMC[] = "NUMC";
    const char* vals[] = { "123", "456", "789" };
    for (const char* v : vals) {
        REQUIRE(AdsAppendRecord(hT) == 0);
        REQUIRE(AdsSetString(hT, fNUMC,
            reinterpret_cast<UNSIGNED8*>(const_cast<char*>(v)), 3) == 0);
        REQUIRE(AdsWriteRecord(hT) == 0);
    }

    UNSIGNED8 bag[]  = "numt.cdx";
    UNSIGNED8 tag[]  = "TN";
    UNSIGNED8 expr[] = "Val(NUMC)";
    ADSHANDLE hI = 0;
    REQUIRE(AdsCreateIndex61(hT, bag, tag, expr,
                             nullptr, nullptr, 0, 0, &hI) == 0);

    // Numeric DbSeek: 8-byte raw double (what rddads sends).
    double dv = 456.0;
    UNSIGNED16 found = 99;
    REQUIRE(AdsSeek(hI, reinterpret_cast<UNSIGNED8*>(&dv),
                    sizeof(double), ADS_DOUBLEKEY, ADS_HARDSEEK,
                    &found) == 0);
    CHECK(found == 1);

    // AdsExtractKey must return the 8-byte order-preserving binary key
    // (what rddads' OrdKeyVal decodes with HB_ORD2DBL), not ASCII text.
    if (found == 1) {
        UNSIGNED8 buf[32];
        UNSIGNED16 blen = sizeof(buf);
        REQUIRE(AdsExtractKey(hI, buf, &blen) == 0);
        CHECK(blen == 8);
        const std::string expect = openads::engine::fox_numeric_key(456.0);
        CHECK(std::memcmp(buf, expect.data(), 8) == 0);
    }

    AdsCloseIndex(hI);
    AdsCloseTable(hT);
    AdsDisconnect(hConn);
    fs::remove_all(dir, ec);
}
