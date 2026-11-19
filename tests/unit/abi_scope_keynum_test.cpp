// Regressions for the 2026-10-23 scoped-navigation report
// (a downstream application):
//   A. Under a scope, AdsGetKeyNum must return the position INSIDE the scope
//      (1..AdsGetKeyCount), as SAP ADS and DBFCDX report it, not the
//      absolute index position. LOCAL always; REMOTE after any GoTo or
//      bookmark, because the server's GetKeyNum calls the local function.
//   B. REMOTE AdsSetRelKeyPos must scale by the scoped key count, as
//      AdsGetRelKeyPos already does, not by the physical record count.
//   C. ace.h filter-option constants must match SAP ACE / Harbour ads.ch:
//      ADS_RESPECTFILTERS=1, ADS_IGNOREFILTERS=2, ADS_RESPECTSCOPES=3.
// No fixes here. Each test states what the reported behavior.
#include "doctest.h"
#include "openads/ace.h"
#include "network/server.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

// Sets an env var for the test's lifetime and clears it on exit (same
// idiom as abi_adi_estaelec_compound_test.cpp; MSVC has no setenv).
struct EnvGuard {
    const char* name_;
    EnvGuard(const char* n, const char* v) : name_(n) {
#ifdef _WIN32
        _putenv_s(n, v);
#else
        setenv(n, v, 1);
#endif
    }
    ~EnvGuard() {
#ifdef _WIN32
        _putenv_s(name_, "");
#else
        unsetenv(name_);
#endif
    }
};

std::vector<UNSIGNED8> cbuf(const std::string& s) {
    std::vector<UNSIGNED8> v(s.begin(), s.end());
    v.push_back(0);
    return v;
}

fs::path fresh_dir(const char* leaf) {
    auto d = fs::temp_directory_path() / leaf;
    std::error_code ec;
    fs::remove_all(d, ec);
    fs::create_directories(d);
    return d;
}

ADSHANDLE connect_remote(const fs::path& dir, std::uint16_t port) {
    auto u = cbuf("tcp://127.0.0.1:" + std::to_string(port) + "/" + dir.generic_string());
    ADSHANDLE h = 0;
    REQUIRE(AdsConnect60(u.data(), ADS_REMOTE_SERVER, nullptr, nullptr, 0, &h) == 0);
    REQUIRE(h != 0);
    return h;
}

ADSHANDLE connect_local(const fs::path& dir) {
    auto u = cbuf(dir.string());
    ADSHANDLE h = 0;
    REQUIRE(AdsConnect60(u.data(), ADS_LOCAL_SERVER, nullptr, nullptr, 0, &h) == 0);
    REQUIRE(h != 0);
    return h;
}

// Build "rows" rows named K001..K<rows> plus an index tag BYNAME on NAME.
// Returns the table and index handles (order active).
void make_keyed_table(ADSHANDLE hConn, const fs::path& dir,
                      const std::string& stem, UNSIGNED16 type, int rows,
                      ADSHANDLE* hT, ADSHANDLE* hN) {
    auto name   = cbuf(stem);
    auto fields = cbuf("NAME,C,20,0;AGE,N,4,0");
    REQUIRE(AdsCreateTable(hConn, name.data(), nullptr, type, ADS_ANSI,
                           0, 0, 0, fields.data(), hT) == 0);
    const char* ext = (type == ADS_ADT) ? ".adi" : ".cdx";
    auto bag = cbuf((dir / (stem + ext)).string());
    auto tag = cbuf("BYNAME");
    auto expr = cbuf("NAME");
    REQUIRE(AdsCreateIndex61(*hT, bag.data(), tag.data(), expr.data(),
                             nullptr, nullptr, 0, 512, hN) == 0);
    for (int i = 1; i <= rows; ++i) {
        char nm[8];
        std::snprintf(nm, sizeof nm, "K%03d", i);
        REQUIRE(AdsAppendRecord(*hT) == 0);
        REQUIRE(AdsSetString(*hT, (UNSIGNED8*)"NAME", (UNSIGNED8*)nm,
                             (UNSIGNED32)std::strlen(nm)) == 0);
        REQUIRE(AdsSetLong(*hT, (UNSIGNED8*)"AGE", i) == 0);
        REQUIRE(AdsWriteRecord(*hT) == 0);
    }
    REQUIRE(AdsSetIndexOrderByHandle(*hT, *hN) == 0);
}

void set_scope_k(ADSHANDLE hN, const char* top, const char* bottom) {
    REQUIRE(AdsSetScope(hN, ADS_TOP, (UNSIGNED8*)top,
                        (UNSIGNED16)std::strlen(top), ADS_STRINGKEY) == 0);
    REQUIRE(AdsSetScope(hN, ADS_BOTTOM, (UNSIGNED8*)bottom,
                        (UNSIGNED16)std::strlen(bottom), ADS_STRINGKEY) == 0);
}

UNSIGNED32 key_num(ADSHANDLE hN) {
    UNSIGNED32 n = 0xFFFFFFFFu;
    REQUIRE(AdsGetKeyNum(hN, ADS_RESPECTSCOPES, &n) == 0);
    return n;
}

UNSIGNED32 key_count(ADSHANDLE hN) {
    UNSIGNED32 n = 0xFFFFFFFFu;
    REQUIRE(AdsGetKeyCount(hN, ADS_RESPECTSCOPES, &n) == 0);
    return n;
}

void seek_name(ADSHANDLE hN, const char* k) {
    UNSIGNED16 found = 0;
    REQUIRE(AdsSeek(hN, (UNSIGNED8*)k, (UNSIGNED16)std::strlen(k),
                    ADS_STRINGKEY, ADS_HARDSEEK, &found) == 0);
    REQUIRE(found == 1);
}

// A: core expectation, run on any connection kind and table type.
void scoped_keynum_checks(ADSHANDLE hConn, const fs::path& dir,
                          const std::string& stem, UNSIGNED16 type) {
    ADSHANDLE hT = 0, hN = 0;
    make_keyed_table(hConn, dir, stem, type, 30, &hT, &hN);
    set_scope_k(hN, "K011", "K015");        // 5 rows of 30
    CHECK(key_count(hN) == 5u);
    seek_name(hN, "K013");                  // row 3 of the scope
    // DBFCDX and SAP ADS report 3 here; OpenADS reported the absolute 13.
    CHECK_MESSAGE(key_num(hN) == 3u,
                  "OrdKeyNo must be the position inside the scope (3), "
                  "not the absolute index position (13)");
    // No scope: the absolute position stays (guard for everyone else).
    REQUIRE(AdsClearScope(hN, ADS_TOP) == 0);
    REQUIRE(AdsClearScope(hN, ADS_BOTTOM) == 0);
    seek_name(hN, "K013");
    CHECK(key_num(hN) == 13u);
    REQUIRE(AdsCloseTable(hT) == 0);
}

} // namespace

TEST_CASE("scope: LOCAL CDX OrdKeyNo is the position inside the scope") {
    auto dir = fresh_dir("openads_scope_local_cdx");
    ADSHANDLE h = connect_local(dir);
    scoped_keynum_checks(h, dir, "scdl", ADS_CDX);
    AdsDisconnect(h);
}

TEST_CASE("scope: LOCAL ADT (CDX-format adi) OrdKeyNo is the position inside the scope") {
    // The ADT-with-CDX-format-.adi configuration (adt_cdx_index=1) is the one
    // this fix covers. The native AdiIndex reader is not in scope.
    EnvGuard env("OPENADS_ADT_CDX_INDEX", "1");
    auto dir = fresh_dir("openads_scope_local_adt");
    ADSHANDLE h = connect_local(dir);
    scoped_keynum_checks(h, dir, "scda", ADS_ADT);
    AdsDisconnect(h);
}

TEST_CASE("scope: REMOTE OrdKeyNo after a record jump is the position inside the scope") {
    auto dir = fresh_dir("openads_scope_remote");
    openads::network::Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    ADSHANDLE h = connect_remote(dir, srv.port());
    ADSHANDLE hT = 0, hN = 0;
    make_keyed_table(h, dir, "scdr", ADS_CDX, 30, &hT, &hN);
    set_scope_k(hN, "K011", "K015");
    CHECK(key_count(hN) == 5u);
    // Sequential walk first: client-side tracking is right even before the fix.
    seek_name(hN, "K011");
    REQUIRE(AdsSkip(hT, 1) == 0);           // K012, key number 2
    CHECK(key_num(hN) == 2u);
    // A bookmark-style jump (xBrowse does these on every click): K014 is
    // recno 14, row 4 of the scope. The server's GetKeyNum must answer 4.
    REQUIRE(AdsGotoRecord(hT, 14) == 0);
    CHECK_MESSAGE(key_num(hN) == 4u,
                  "after a jump, REMOTE OrdKeyNo must be scoped (4), "
                  "not the absolute index position (14)");
    REQUIRE(AdsCloseTable(hT) == 0);
    AdsDisconnect(h);
}

TEST_CASE("relkeypos: REMOTE AdsSetRelKeyPos scales by the scoped key count") {
    auto dir = fresh_dir("openads_relkeypos_remote");
    openads::network::Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    ADSHANDLE h = connect_remote(dir, srv.port());
    ADSHANDLE hT = 0, hN = 0;
    make_keyed_table(h, dir, "scrr", ADS_CDX, 300, &hT, &hN);
    set_scope_k(hN, "K101", "K110");        // 10 rows of 300
    CHECK(key_count(hN) == 10u);
    REQUIRE(AdsSetRelKeyPos(hN, 0.0) == 0);
    CHECK(key_num(hN) == 1u);
    REQUIRE(AdsSetRelKeyPos(hN, 1.0) == 0);
    CHECK(key_num(hN) == 10u);
    REQUIRE(AdsSetRelKeyPos(hN, 0.5) == 0);
    // uint32(0.5 * 9 + 0.5) + 1 -> 6 (the pinned SetRelKeyPos rounding,
    // unchanged). Before the fix the target was scaled by the physical
    // 300 and clamped, so every position past 0 landed on row 10.
    CHECK_MESSAGE(key_num(hN) == 6u,
                  "SetRelKeyPos(0.5) on a 10-row scope must land on row 6, "
                  "not the last scoped row (10)");
    REQUIRE(AdsCloseTable(hT) == 0);
    AdsDisconnect(h);
}

TEST_CASE("relkeypos guard: without a scope REMOTE AdsSetRelKeyPos is unchanged") {
    auto dir = fresh_dir("openads_relkeypos_guard");
    openads::network::Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    ADSHANDLE h = connect_remote(dir, srv.port());
    ADSHANDLE hT = 0, hN = 0;
    make_keyed_table(h, dir, "scrg", ADS_CDX, 300, &hT, &hN);
    REQUIRE(AdsSetRelKeyPos(hN, 0.5) == 0);
    CHECK(key_num(hN) == 151u);             // uint32(0.5 * 299 + 0.5) + 1
    REQUIRE(AdsCloseTable(hT) == 0);
    AdsDisconnect(h);
}

TEST_CASE("ace: filter option constants match SAP ACE and Harbour ads.ch") {
    // rddads is compiled against the SAP values, so the DLL must use them.
    CHECK(ADS_RESPECTFILTERS == 1);
    CHECK(ADS_IGNOREFILTERS  == 2);
    CHECK(ADS_RESPECTSCOPES  == 3);
}

TEST_CASE("ace: AdsGetRecordCount honours SAP RESPECTFILTERS with SET DELETED ON") {
    auto dir = fresh_dir("openads_ace_reccount");
    ADSHANDLE h = connect_local(dir);
    auto name   = cbuf("rc1");
    auto fields = cbuf("NAME,C,10,0");
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(h, name.data(), nullptr, ADS_CDX, ADS_ANSI,
                           0, 0, 0, fields.data(), &hT) == 0);
    for (int i = 0; i < 10; ++i) {
        REQUIRE(AdsAppendRecord(hT) == 0);
        REQUIRE(AdsSetString(hT, (UNSIGNED8*)"NAME", (UNSIGNED8*)"x", 1) == 0);
        REQUIRE(AdsWriteRecord(hT) == 0);
    }
    REQUIRE(AdsGotoRecord(hT, 3) == 0);
    REQUIRE(AdsDeleteRecord(hT) == 0);
    REQUIRE(AdsGotoRecord(hT, 7) == 0);
    REQUIRE(AdsDeleteRecord(hT) == 0);
    REQUIRE(AdsShowDeleted(0) == 0);        // SET DELETED ON
    UNSIGNED32 n = 0;
    // SAP value 1 = ADS_RESPECTFILTERS: live rows only.
    REQUIRE(AdsGetRecordCount(hT, 1, &n) == 0);
    CHECK_MESSAGE(n == 8u, "RESPECTFILTERS with SET DELETED ON must count 8 live rows");
    // SAP value 2 = ADS_IGNOREFILTERS: raw physical count.
    REQUIRE(AdsGetRecordCount(hT, 2, &n) == 0);
    CHECK_MESSAGE(n == 10u, "IGNOREFILTERS must count all 10 physical rows");
    REQUIRE(AdsShowDeleted(1) == 0);
    REQUIRE(AdsCloseTable(hT) == 0);
    AdsDisconnect(h);
}
