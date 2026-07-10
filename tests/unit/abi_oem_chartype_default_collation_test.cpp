// RCB 2026-07-10 — regression for the #130 follow-up "mechanism #2"
// (contributor report on v1.8.6/v1.8.7): a Harbour rddads app has NO way
// to call AdsSetCollation — it only passes usCharType=ADS_OEM at
// AdsOpenTable / AdsCreateTable (via AdsSetCharType()). Before v1.8.9,
// AdsOpenTable IGNORED usCharType, so:
//   - INDEX ON (AdsCreateIndex61) built keys with UTF-8 case promotion
//     (a lowercase ż stayed ż) and binary sort order, while the app's
//     DbSeek key came from Harbour's CP852 Upper() (Ż) — not-found on
//     every row with Polish letters;
//   - the tags compared with memcmp instead of the PL852 weights.
// SAP semantics (Advantage help, "Avoiding OEM Collation Mismatch
// Errors"): the OEM collation language is a machine-level setting
// (ADSLOCAL.CFG) and tables opened ADS_OEM pick it up with zero
// per-connection configuration. OpenADS mirrors that with a process
// default OEM collation (OPENADS_OEM_COLLATION env var today, an
// adslocal.cfg parser in a later phase) consumed by tables opened with
// usCharType=ADS_OEM. These tests exercise exactly that path — NO
// AdsSetCollation call anywhere. If a refactor makes them fail, the
// zero-config OEM activation is broken again; do not "fix" them by
// adding AdsSetCollation.
#include "doctest.h"
#include "fixtures/polish_oem_fixture.h"
#include "engine/oem_collation.h"
#include "openads/ace.h"

#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {

// Installs the process default OEM collation for one test and always
// clears it on exit (doctest REQUIRE throws, so cleanup must be RAII —
// a dangling default would leak into unrelated tests in this binary).
struct DefaultOemCollationGuard {
    explicit DefaultOemCollationGuard(const char* name) {
        REQUIRE(openads::engine::set_default_oem_collation(name));
    }
    ~DefaultOemCollationGuard() {
        (void)openads::engine::set_default_oem_collation("");
    }
};

void seek_and_expect(ADSHANDLE hI, ADSHANDLE hT, const char* key,
                     const char* expect_prefix, UNSIGNED16 expect_found) {
    UNSIGNED16 found = 99;
    REQUIRE(AdsSeek(hI,
        reinterpret_cast<UNSIGNED8*>(const_cast<char*>(key)),
        static_cast<UNSIGNED16>(std::strlen(key)),
        ADS_STRINGKEY, /*hard*/ 0, &found) == 0);
    INFO("seek [" << key << "] found=" << found);
    CHECK(found == expect_found);
    if (found == 1 && expect_found == 1 && expect_prefix != nullptr) {
        UNSIGNED8 got[32];
        UNSIGNED32 got_len = sizeof(got);
        UNSIGNED8 fName[] = "NAME";
        REQUIRE(AdsGetString(hT, fName, got, &got_len, 0) == 0);
        CHECK(std::memcmp(got, expect_prefix,
                          std::strlen(expect_prefix)) == 0);
    }
}

} // namespace

TEST_CASE("ADS_OEM table + default collation: INDEX ON Upper() seeks "
          "Polish keys with no AdsSetCollation") {
    DefaultOemCollationGuard coll_guard("PL852");

    auto dir = fs::temp_directory_path() / "openads_oem_chartype";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    // Deliberately NO AdsSetCollation — rddads cannot make that call.

    UNSIGNED8 def[]   = "NAME,C,20,0";
    UNSIGNED8 tname[] = "oemchar";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX,
                           ADS_OEM, 0, 0, 0, def, &hT) == 0);

    UNSIGNED16 ct = 0;
    REQUIRE(AdsGetTableCharType(hT, &ct) == 0);
    CHECK(ct == ADS_OEM);

    // Mixed-case rows; ł = 0x88 upper-cases to Ł = 0x9D under CP852
    // only — UTF-8 case promotion leaves the byte untouched.
    const char lab_lower[] = {'\x88', 'a', 'b', 'b', 'b', '\0'};
    const char* rows[] = { "Lacaaaaa", lab_lower, "Madccccc", "Zbydddd" };
    UNSIGNED8 fName[] = "NAME";
    for (const char* row : rows) {
        REQUIRE(AdsAppendRecord(hT) == 0);
        REQUIRE(AdsSetString(hT, fName,
            reinterpret_cast<UNSIGNED8*>(const_cast<char*>(row)),
            static_cast<UNSIGNED32>(std::strlen(row))) == 0);
        REQUIRE(AdsWriteRecord(hT) == 0);
    }

    // The INDEX ON rebuild the contributor's harness performs.
    UNSIGNED8 bag[]  = "oemchar.cdx";
    UNSIGNED8 tag[]  = "BYUP";
    UNSIGNED8 expr[] = "Upper(NAME)";
    ADSHANDLE hI = 0;
    REQUIRE(AdsCreateIndex61(hT, bag, tag, expr,
                             nullptr, nullptr, 0, 0, &hI) == 0);

    // Harbour-side Upper() under CP852 produces these seek keys.
    const char lab_upper[] = {'\x9D', 'A', 'B', 'B', 'B', '\0'};
    seek_and_expect(hI, hT, "MADCCCCC", "Mad", 1);
    seek_and_expect(hI, hT, lab_upper, "\x88""ab", 1);
    seek_and_expect(hI, hT, "ZBYDDDD", "Zby", 1);

    // Close and REOPEN with ADS_OEM — the production-CDX path. The
    // reopened tags must re-derive the collation from the char type.
    REQUIRE(AdsCloseTable(hT) == 0);
    hT = 0; hI = 0;
    UNSIGNED8 tfile[] = "oemchar.dbf";
    UNSIGNED8 alias[] = "oemchar";
    REQUIRE(AdsOpenTable(hConn, tfile, alias, ADS_CDX,
                         ADS_OEM, 0, 0, 0, &hT) == 0);
    REQUIRE(AdsGetIndexHandle(hT, tag, &hI) == 0);

    seek_and_expect(hI, hT, "MADCCCCC", "Mad", 1);
    seek_and_expect(hI, hT, lab_upper, "\x88""ab", 1);

    AdsCloseTable(hT);
    AdsDisconnect(hConn);
    fs::remove_all(dir, ec);
}

TEST_CASE("ADS_OEM C(8) tag orders by PL852 weights from the default "
          "collation (no AdsSetCollation)") {
    DefaultOemCollationGuard coll_guard("NTXPL852");

    auto dir = fs::temp_directory_path() / "openads_oem_chartype_c8";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);

    UNSIGNED8 def[]   = "NAME,C,8,0";
    UNSIGNED8 tname[] = "oemc8";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX,
                           ADS_OEM, 0, 0, 0, def, &hT) == 0);

    // PL852 order: LAC < ŁAB (0x9D) < MAD < ZBY; byte-wise 0x9D sorts
    // after Z, so a binary tree walk misses the Ł row.
    const char* rows[] = {
        "LACAAAAA", openads::test::kPolishLabRow8, "MADCCCCC", "ZBYDDDDD" };
    UNSIGNED8 fName[] = "NAME";
    for (const char* row : rows) {
        REQUIRE(AdsAppendRecord(hT) == 0);
        REQUIRE(AdsSetString(hT, fName,
            reinterpret_cast<UNSIGNED8*>(const_cast<char*>(row)),
            static_cast<UNSIGNED32>(std::strlen(row))) == 0);
        REQUIRE(AdsWriteRecord(hT) == 0);
    }

    UNSIGNED8 bag[]  = "oemc8.cdx";
    UNSIGNED8 tag[]  = "BYNAME";
    UNSIGNED8 expr[] = "NAME";
    ADSHANDLE hI = 0;
    REQUIRE(AdsCreateIndex61(hT, bag, tag, expr,
                             nullptr, nullptr, 0, 0, &hI) == 0);

    seek_and_expect(hI, hT, openads::test::kPolishLabRow8, "\x9D""AB", 1);
    seek_and_expect(hI, hT, "MADCCCCC", "MAD", 1);
    seek_and_expect(hI, hT, "ZBYDDDDD", "ZBY", 1);

    AdsCloseTable(hT);
    AdsDisconnect(hConn);
    fs::remove_all(dir, ec);
}

TEST_CASE("ANSI table is untouched by the default OEM collation "
          "(UTF-8 UPPER preserved)") {
    DefaultOemCollationGuard coll_guard("PL852");

    auto dir = fs::temp_directory_path() / "openads_oem_chartype_ansi";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);

    // Default char type (0) → ANSI: the OEM default must NOT kick in,
    // otherwise UTF-8 case promotion breaks for every non-OEM caller —
    // the exact bug the v1.8.6 scoping fixed.
    UNSIGNED8 def[]   = "NAME,C,20,0";
    UNSIGNED8 tname[] = "ansich";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX,
                           0, 0, 0, 0, def, &hT) == 0);

    UNSIGNED16 ct = 99;
    REQUIRE(AdsGetTableCharType(hT, &ct) == 0);
    CHECK(ct == ADS_ANSI);

    // UTF-8 ñ (0xC3 0xB1) upper-cases to Ñ (0xC3 0x91) on the UTF-8
    // path; the CP852 table would mangle those bytes instead.
    const char* rows[] = { "ni\xC3\xB1o", "madccccc" };
    UNSIGNED8 fName[] = "NAME";
    for (const char* row : rows) {
        REQUIRE(AdsAppendRecord(hT) == 0);
        REQUIRE(AdsSetString(hT, fName,
            reinterpret_cast<UNSIGNED8*>(const_cast<char*>(row)),
            static_cast<UNSIGNED32>(std::strlen(row))) == 0);
        REQUIRE(AdsWriteRecord(hT) == 0);
    }

    UNSIGNED8 bag[]  = "ansich.cdx";
    UNSIGNED8 tag[]  = "BYUP";
    UNSIGNED8 expr[] = "Upper(NAME)";
    ADSHANDLE hI = 0;
    REQUIRE(AdsCreateIndex61(hT, bag, tag, expr,
                             nullptr, nullptr, 0, 0, &hI) == 0);

    seek_and_expect(hI, hT, "NI\xC3\x91O", "ni\xC3\xB1o", 1);
    seek_and_expect(hI, hT, "MADCCCCC", "mad", 1);

    AdsCloseTable(hT);
    AdsDisconnect(hConn);
    fs::remove_all(dir, ec);
}
