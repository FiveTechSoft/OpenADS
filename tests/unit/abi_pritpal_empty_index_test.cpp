// Pritpal Bedi (30/07/2026) — TestIndex.zip + StageStressTest_CDX:
//
// Observed:
//   - HbDBU may report "corruption" when a structural empty/odd CDX is
//     auto-opened (DBF is fine; the companion bag is the trigger).
//   - ADSCDX: no error dialog, but Browse / ? alias->name are blank when
//     the controlling order has 0 keys.
//
// Root cause of HIS bag (TestIndex.cdx, 2560 bytes, root_page=0, keycount=0
// while DBF has 36 rows):
//   INDEX ON while empty → empty B-tree; later appends did not fill that bag
//   (no SET INDEX; and/or remote production-bag maintenance).
//
// These tests pin OpenADS LOCAL semantics so we can tell app-stage mistakes
// apart from engine defects:
//   A) Empty INDEX ON → keycount 0; GoTop succeeds; FieldGet → 5068
//      (AE_NO_CURRENT_RECORD — rddads maps that to blank, no Harbour error)
//   B) Non-structural bag, append without AdsOpenIndex → the bag on disk
//      is stale (0 keys over N rows); since Aug 2026 AdsOpenIndex detects
//      that exact shape (unconditional tag, no root page, non-empty table)
//      and reindexes once on open — the caller gets a working order
//      instead of a ghost bof=eof=1 Limbo.
//   C) Structural production bag auto-opens on USE and IS maintained on
//      append (local) — so "empty bag + N rows" is NOT the local auto-open
//      path; rebuild-after-data or order-open appends fill keys
//   D) INDEX ON after data → ordered FieldGet works
//   E) Append with order open grows keycount
#include "doctest.h"
#include "openads/ace.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

ADSHANDLE connect_local(const fs::path& dir) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    UNSIGNED8 srv[512] = {};
    const auto s = dir.string();
    REQUIRE(s.size() + 1 <= sizeof(srv));
    std::memcpy(srv, s.c_str(), s.size() + 1);
    ADSHANDLE h = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &h) == 0);
    return h;
}

ADSHANDLE create_empty_table(ADSHANDLE hConn, const char* alias) {
    UNSIGNED8 flds[] = "NAME,C,20,0;CITY,C,20,0";
    ADSHANDLE hTbl = 0;
    std::vector<UNSIGNED8> name(alias, alias + std::strlen(alias) + 1);
    REQUIRE(AdsCreateTable(hConn, name.data(), name.data(), ADS_CDX, ADS_ANSI,
                           ADS_CHECKRIGHTS, ADS_DEFAULT, 0, flds, &hTbl) == 0);
    REQUIRE(hTbl != 0);
    return hTbl;
}

void append_name(ADSHANDLE hTbl, const char* name, const char* city) {
    REQUIRE(AdsAppendRecord(hTbl) == 0);
    REQUIRE(AdsSetString(hTbl, reinterpret_cast<UNSIGNED8*>(const_cast<char*>("NAME")),
                         reinterpret_cast<UNSIGNED8*>(const_cast<char*>(name)),
                         static_cast<UNSIGNED32>(std::strlen(name))) == 0);
    REQUIRE(AdsSetString(hTbl, reinterpret_cast<UNSIGNED8*>(const_cast<char*>("CITY")),
                         reinterpret_cast<UNSIGNED8*>(const_cast<char*>(city)),
                         static_cast<UNSIGNED32>(std::strlen(city))) == 0);
    REQUIRE(AdsWriteRecord(hTbl) == 0);
}

// Returns {rc, trimmed value}. Off-record FieldGet is AE_NO_CURRENT_RECORD
// (5068); rddads special-cases that to a blank item without raising.
std::pair<UNSIGNED32, std::string> try_get_name(ADSHANDLE hTbl) {
    UNSIGNED8 buf[64] = {};
    UNSIGNED32 len = sizeof(buf);
    UNSIGNED32 rc = AdsGetString(
        hTbl, reinterpret_cast<UNSIGNED8*>(const_cast<char*>("NAME")),
        buf, &len, ADS_NONE);
    std::string s(reinterpret_cast<char*>(buf));
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return {rc, s};
}

std::string get_name(ADSHANDLE hTbl) {
    auto [rc, s] = try_get_name(hTbl);
    REQUIRE(rc == 0);
    return s;
}

bool at_bof(ADSHANDLE h) {
    UNSIGNED16 v = 0;
    REQUIRE(AdsAtBOF(h, &v) == 0);
    return v != 0;
}

bool at_eof(ADSHANDLE h) {
    UNSIGNED16 v = 0;
    REQUIRE(AdsAtEOF(h, &v) == 0);
    return v != 0;
}

UNSIGNED32 key_count(ADSHANDLE hIdx) {
    UNSIGNED32 n = 0xFFFFFFFFu;
    REQUIRE(AdsGetKeyCount(hIdx, ADS_IGNOREFILTERS, &n) == 0);
    return n;
}

UNSIGNED32 rec_count(ADSHANDLE hTbl) {
    UNSIGNED32 n = 0;
    REQUIRE(AdsGetRecordCount(hTbl, ADS_IGNOREFILTERS, &n) == 0);
    return n;
}

}  // namespace

// ---------------------------------------------------------------------------
// A) INDEX ON empty table → valid empty bag (not "corrupt")
// ---------------------------------------------------------------------------
TEST_CASE("Pritpal empty-index: INDEX ON empty table yields keycount 0 (not corrupt)") {
    auto dir = fs::temp_directory_path() / "openads_pritpal_empty_idx_a";
    std::error_code ec;
    fs::remove_all(dir, ec);

    auto hConn = connect_local(dir);
    auto hTbl = create_empty_table(hConn, "TestIndex");
    CHECK(rec_count(hTbl) == 0u);

    UNSIGNED8 bag[]  = "TestIndex.cdx";
    UNSIGNED8 tag[]  = "IDX01";
    UNSIGNED8 expr[] = "NAME";
    ADSHANDLE hIdx = 0;
    REQUIRE(AdsCreateIndex61(hTbl, bag, tag, expr, nullptr, nullptr,
                             ADS_COMPOUND, 512, &hIdx) == 0);
    REQUIRE(hIdx != 0);

    CHECK(key_count(hIdx) == 0u);
    // GoTop on empty order succeeds (rddads would NOT raise EG_CORRUPTION).
    REQUIRE(AdsGotoTop(hIdx) == 0);
    CHECK(at_bof(hTbl));
    CHECK(at_eof(hTbl));
    // FieldGet off-record → 5068 AE_NO_CURRENT_RECORD. Harbour rddads maps
    // that to a blank value, so ? _STG->name prints empty with no error box.
    auto [rc, name] = try_get_name(hTbl);
    CHECK(rc == AE_NO_CURRENT_RECORD);
    CHECK(name.empty());

    REQUIRE(AdsCloseTable(hTbl) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

// ---------------------------------------------------------------------------
// B) Non-structural bag: INDEX ON empty, close, append WITHOUT opening bag
//    → bag on disk goes stale (0 keys over N rows). AdsOpenIndex now heals
//    that exact shape (reindex once on open), so DbSetIndex gives a working
//    order instead of the blank bof=eof=1 Limbo Pritpal hit over the wire.
// ---------------------------------------------------------------------------
TEST_CASE("Pritpal empty-index: stale non-structural bag is healed on open") {
    auto dir = fs::temp_directory_path() / "openads_pritpal_empty_idx_b";
    std::error_code ec;
    fs::remove_all(dir, ec);

    auto hConn = connect_local(dir);
    auto hTbl = create_empty_table(hConn, "TestIndex");

    // Bag name != table stem → not production auto-open.
    UNSIGNED8 bag[]  = "MyIdx.cdx";
    UNSIGNED8 tag[]  = "IDX01";
    UNSIGNED8 expr[] = "NAME";
    ADSHANDLE hIdx = 0;
    REQUIRE(AdsCreateIndex61(hTbl, bag, tag, expr, nullptr, nullptr,
                             ADS_COMPOUND, 512, &hIdx) == 0);
    REQUIRE(AdsCloseIndex(hIdx) == 0);
    REQUIRE(AdsCloseTable(hTbl) == 0);
    hTbl = 0;
    hIdx = 0;

    UNSIGNED8 tname[] = "TestIndex";
    REQUIRE(AdsOpenTable(hConn, tname, tname, ADS_CDX, 1, 1, 0, 1, &hTbl) == 0);
    // No AdsOpenIndex — mirrors SET INDEX off during HeyAddRecords.
    append_name(hTbl, "Alice", "Madrid");
    append_name(hTbl, "Bob", "Barcelona");
    append_name(hTbl, "Charlie", "Valencia");
    CHECK(rec_count(hTbl) == 3u);

    REQUIRE(AdsGotoTop(hTbl) == 0);
    CHECK(get_name(hTbl) == "Alice");

    // Open the stale bag: the heal reindexes it once, on open.
    std::array<ADSHANDLE, 8> arr{};
    UNSIGNED16 n = static_cast<UNSIGNED16>(arr.size());
    REQUIRE(AdsOpenIndex(hTbl, bag, arr.data(), &n) == 0);
    REQUIRE(n >= 1u);
    hIdx = arr[0];
    CHECK(key_count(hIdx) == 3u);

    REQUIRE(AdsGotoTop(hIdx) == 0);
    CHECK_FALSE(at_bof(hTbl));
    CHECK_FALSE(at_eof(hTbl));
    CHECK(get_name(hTbl) == "Alice");

    REQUIRE(AdsCloseTable(hTbl) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

// ---------------------------------------------------------------------------
// C) Structural production bag: USE auto-opens it and LOCAL append maintains
//    keys. Documents that "36 rows / 0 keys" is NOT what local auto-open
//    produces after appends — his empty structural bag needs another
//    explanation (remote maintenance gap or never reopened with bag).
// ---------------------------------------------------------------------------
TEST_CASE("Pritpal empty-index: local production CDX auto-open is maintained on append") {
    auto dir = fs::temp_directory_path() / "openads_pritpal_empty_idx_c";
    std::error_code ec;
    fs::remove_all(dir, ec);

    auto hConn = connect_local(dir);
    auto hTbl = create_empty_table(hConn, "TestIndex");

    UNSIGNED8 bag[]  = "TestIndex.cdx";  // same stem → production bag
    UNSIGNED8 tag[]  = "IDX01";
    UNSIGNED8 expr[] = "NAME";
    ADSHANDLE hIdx = 0;
    REQUIRE(AdsCreateIndex61(hTbl, bag, tag, expr, nullptr, nullptr,
                             ADS_COMPOUND, 512, &hIdx) == 0);
    CHECK(key_count(hIdx) == 0u);
    REQUIRE(AdsCloseIndex(hIdx) == 0);
    REQUIRE(AdsCloseTable(hTbl) == 0);

    UNSIGNED8 tname[] = "TestIndex";
    REQUIRE(AdsOpenTable(hConn, tname, tname, ADS_CDX, 1, 1, 0, 1, &hTbl) == 0);
    // Do NOT call AdsOpenIndex explicitly — production auto-open only.
    append_name(hTbl, "Alice", "Madrid");
    append_name(hTbl, "Bob", "Barcelona");
    append_name(hTbl, "Charlie", "Valencia");

    // Re-open the bag (or use auto-opened handles via AdsGetNumIndexes).
    std::array<ADSHANDLE, 8> arr{};
    UNSIGNED16 n = static_cast<UNSIGNED16>(arr.size());
    REQUIRE(AdsOpenIndex(hTbl, bag, arr.data(), &n) == 0);
    REQUIRE(n >= 1u);
    // Local engine must have maintained the production bag during append.
    CHECK(key_count(arr[0]) == 3u);
    REQUIRE(AdsGotoTop(arr[0]) == 0);
    CHECK(get_name(hTbl) == "Alice");

    REQUIRE(AdsCloseTable(hTbl) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

// Scratch repro for the remote keyno failure: mirrors the exact wire op
// sequence the server applies on the ABI twin — active order, append,
// per-op flush — to find where the new key gets lost.
TEST_CASE("twin repro: append+flush with active order maintains key") {
    auto dir = fs::temp_directory_path() / "openads_twin_append_repro";
    std::error_code ec;
    fs::remove_all(dir, ec);

    auto hConn = connect_local(dir);
    auto hTbl = create_empty_table(hConn, "TestIndex");

    UNSIGNED8 bag[]  = "TestIndex.cdx";  // production bag
    UNSIGNED8 tag[]  = "IDX01";
    UNSIGNED8 expr[] = "NAME";
    ADSHANDLE hIdx = 0;
    REQUIRE(AdsCreateIndex61(hTbl, bag, tag, expr, nullptr, nullptr,
                             ADS_COMPOUND, 512, &hIdx) == 0);
    // Seed 3 rows (through the bound order, like the fixture's maker).
    append_name(hTbl, "Seed1", "A");
    append_name(hTbl, "Seed2", "B");
    append_name(hTbl, "Seed3", "C");
    REQUIRE(AdsCloseIndex(hIdx) == 0);
    REQUIRE(AdsCloseTable(hTbl) == 0);

    // Reopen: production auto-open + explicit open + SetOrder (twin flow).
    UNSIGNED8 tname[] = "TestIndex";
    REQUIRE(AdsOpenTable(hConn, tname, tname, ADS_CDX, 1, 1, 0, 1, &hTbl) == 0);
    std::array<ADSHANDLE, 8> arr{};
    UNSIGNED16 n = static_cast<UNSIGNED16>(arr.size());
    REQUIRE(AdsOpenIndex(hTbl, bag, arr.data(), &n) == 0);
    REQUIRE(n >= 1u);
    REQUIRE(AdsSetIndexOrderByHandle(hTbl, arr[0]) == 0);
    CHECK(key_count(arr[0]) == 3u);

    // The remote test queries keyno BEFORE the append (builds the pos
    // cache with 3 entries) — mirror that, it is part of the failure.
    REQUIRE(AdsGotoBottom(hTbl) == 0);
    UNSIGNED32 kb = 0;
    REQUIRE(AdsGetKeyNum(hTbl, 0, &kb) == 0);
    CHECK(kb == 3u);

    // Wire AppendBlank handler: append, then flush immediately.
    REQUIRE(AdsAppendRecord(hTbl) == 0);
    REQUIRE(AdsFlushFileBuffers(hTbl) == 0);
    // Wire SetField handler: set each field, flush after each. NON-key
    // field FIRST — that intermediate commit used to clear the append
    // marker, so the key-bearing commit aborted its blank-key erase as a
    // bogus corrupt-tree and the key was never inserted.
    REQUIRE(AdsSetString(hTbl, (UNSIGNED8*)"CITY", (UNSIGNED8*)"X", 1) == 0);
    REQUIRE(AdsFlushFileBuffers(hTbl) == 0);
    REQUIRE(AdsSetString(hTbl, (UNSIGNED8*)"NAME", (UNSIGNED8*)"AAA first", 9) == 0);
    REQUIRE(AdsFlushFileBuffers(hTbl) == 0);

    CHECK(key_count(arr[0]) == 4u);
    UNSIGNED32 kn = 0;
    REQUIRE(AdsGetKeyNum(hTbl, 0, &kn) == 0);
    CHECK(kn == 1u);
    REQUIRE(AdsGotoTop(arr[0]) == 0);
    CHECK(get_name(hTbl) == "AAA first");

    REQUIRE(AdsCloseTable(hTbl) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}
// ---------------------------------------------------------------------------
// D) Correct stage order: data first, then INDEX ON
// ---------------------------------------------------------------------------
TEST_CASE("Pritpal empty-index: INDEX ON after append populates keys") {
    auto dir = fs::temp_directory_path() / "openads_pritpal_empty_idx_d";
    std::error_code ec;
    fs::remove_all(dir, ec);

    auto hConn = connect_local(dir);
    auto hTbl = create_empty_table(hConn, "TestIndex");
    append_name(hTbl, "Charlie", "Valencia");
    append_name(hTbl, "Alice", "Madrid");
    append_name(hTbl, "Bob", "Barcelona");

    UNSIGNED8 bag[]  = "TestIndex.cdx";
    UNSIGNED8 tag[]  = "IDX01";
    UNSIGNED8 expr[] = "NAME";
    ADSHANDLE hIdx = 0;
    REQUIRE(AdsCreateIndex61(hTbl, bag, tag, expr, nullptr, nullptr,
                             ADS_COMPOUND, 512, &hIdx) == 0);
    CHECK(key_count(hIdx) == 3u);
    REQUIRE(AdsGotoTop(hIdx) == 0);
    CHECK(get_name(hTbl) == "Alice");

    REQUIRE(AdsCloseTable(hTbl) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

// ---------------------------------------------------------------------------
// E) Append with order open grows keycount (engine does maintain when open)
// ---------------------------------------------------------------------------
TEST_CASE("Pritpal empty-index: append with order open grows keycount") {
    auto dir = fs::temp_directory_path() / "openads_pritpal_empty_idx_e";
    std::error_code ec;
    fs::remove_all(dir, ec);

    auto hConn = connect_local(dir);
    auto hTbl = create_empty_table(hConn, "TestIndex");

    UNSIGNED8 bag[]  = "MyIdx.cdx";
    UNSIGNED8 tag[]  = "IDX01";
    UNSIGNED8 expr[] = "NAME";
    ADSHANDLE hIdx = 0;
    REQUIRE(AdsCreateIndex61(hTbl, bag, tag, expr, nullptr, nullptr,
                             ADS_COMPOUND, 512, &hIdx) == 0);
    CHECK(key_count(hIdx) == 0u);

    append_name(hTbl, "Alice", "Madrid");
    append_name(hTbl, "Bob", "Barcelona");
    CHECK(key_count(hIdx) == 2u);
    REQUIRE(AdsGotoTop(hIdx) == 0);
    CHECK(get_name(hTbl) == "Alice");

    REQUIRE(AdsCloseTable(hTbl) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}
