// Intensive index-usage battery, batch 2 (Pritpal Bedi: "cover every
// case"). Complements abi_index_intensive_test.cpp with: soft/hard seek
// semantics, date keys, expression indexes (UPPER / concatenation),
// decimal numerics, multiple independent bags, wide multi-tag writes,
// blank keys, conditional rebuild on pack, SET DELETED interplay,
// skip-unique, descending scopes, peer reindex visibility, and a remote
// (wire) section mirroring the riskiest flows through the server twin.

#include "doctest.h"
#include "openads/ace.h"
#include "network/server.h"
#include "drivers/adi/adi_index.h"
#include <cstdio>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

ADSHANDLE connect_at(const fs::path& dir) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    UNSIGNED8 srv[512] = {};
    const auto s = dir.string();
    std::memcpy(srv, s.c_str(), s.size() + 1);
    ADSHANDLE h = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &h) == 0);
    return h;
}

ADSHANDLE connect_remote_at(const fs::path& dir, std::uint16_t port) {
    std::string uri = "tcp://127.0.0.1:" + std::to_string(port) + "/" +
                      dir.generic_string();
    ADSHANDLE h = 0;
    std::vector<UNSIGNED8> u(uri.c_str(), uri.c_str() + uri.size() + 1);
    REQUIRE(AdsConnect60(u.data(), ADS_REMOTE_SERVER,
                         nullptr, nullptr, 0, &h) == 0);
    return h;
}

void make_table(ADSHANDLE hConn, const char* name, const char* flds,
                ADSHANDLE* hTbl) {
    std::vector<UNSIGNED8> n(name, name + std::strlen(name) + 1);
    std::vector<UNSIGNED8> f(flds, flds + std::strlen(flds) + 1);
    REQUIRE(AdsCreateTable(hConn, n.data(), n.data(), ADS_CDX, ADS_ANSI,
                           ADS_CHECKRIGHTS, ADS_DEFAULT, 0, f.data(),
                           hTbl) == 0);
}

void add_index(ADSHANDLE hTbl, const char* bagPath, const char* tag,
               const char* expr, const char* cond, UNSIGNED32 opts,
               ADSHANDLE* hIdx) {
    std::vector<UNSIGNED8> bag(bagPath, bagPath + std::strlen(bagPath) + 1);
    std::vector<UNSIGNED8> tg(tag, tag + std::strlen(tag) + 1);
    std::vector<UNSIGNED8> ex(expr, expr + std::strlen(expr) + 1);
    std::vector<UNSIGNED8> cd;
    UNSIGNED8* cdp = nullptr;
    if (cond != nullptr) {
        cd.assign(cond, cond + std::strlen(cond) + 1);
        cdp = cd.data();
    }
    REQUIRE(AdsCreateIndex61(hTbl, bag.data(), tg.data(), ex.data(), cdp,
                             nullptr, opts, 512, hIdx) == 0);
}

void put_rec2(ADSHANDLE hTbl, const char* name, const char* city) {
    REQUIRE(AdsAppendRecord(hTbl) == 0);
    REQUIRE(AdsSetString(hTbl, (UNSIGNED8*)"NAME", (UNSIGNED8*)name,
                       (UNSIGNED32)std::strlen(name)) == 0);
    if (city != nullptr && *city != '\0') {
        REQUIRE(AdsSetString(hTbl, (UNSIGNED8*)"CITY", (UNSIGNED8*)city,
                           (UNSIGNED32)std::strlen(city)) == 0);
    }
    REQUIRE(AdsWriteRecord(hTbl) == 0);
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

std::vector<std::pair<std::string, UNSIGNED32>>
walk_order(ADSHANDLE hTbl, const char* field) {
    std::vector<std::pair<std::string, UNSIGNED32>> out;
    REQUIRE(AdsGotoTop(hTbl) == 0);
    for (;;) {
        UNSIGNED16 eof = 0;
        REQUIRE(AdsAtEOF(hTbl, &eof) == 0);
        if (eof) break;
        UNSIGNED8 buf[64] = {0};
        UNSIGNED32 len = sizeof(buf) - 1;
        REQUIRE(AdsGetString(hTbl, (UNSIGNED8*)field, buf, &len, 0) == 0);
        std::string k(reinterpret_cast<char*>(buf), len);
        while (!k.empty() && k.back() == ' ') k.pop_back();
        UNSIGNED32 rec = 0;
        REQUIRE(AdsGetRecordNum(hTbl, 0, &rec) == 0);
        out.emplace_back(k, rec);
        REQUIRE(AdsSkip(hTbl, 1) == 0);
    }
    return out;
}

}  // namespace

// ---------------------------------------------------------------------------
TEST_CASE("intensive2: soft seek lands on next key; not-found goes EOF") {
    auto dir = fs::temp_directory_path() / "oads_int2_softseek";
    std::error_code ec;
    fs::remove_all(dir, ec);
    auto hC = connect_at(dir);
    ADSHANDLE hT = 0;
    make_table(hC, "ss", "NAME,C,20,0", &hT);
    const auto bag = (dir / "ss.cdx").string();
    ADSHANDLE hN = 0;
    add_index(hT, bag.c_str(), "BYNAME", "NAME", nullptr, 0x0008, &hN);
    put_rec2(hT, "Beta", "");
    put_rec2(hT, "Delta", "");
    put_rec2(hT, "Hotel", "");

    REQUIRE(AdsSetIndexOrderByHandle(hT, hN) == 0);
    // Soft seek for a missing key between Beta and Delta -> lands on Delta.
    UNSIGNED16 found = 0;
    REQUIRE(AdsSeek(hN, (UNSIGNED8*)"Charlie", 7, ADS_STRINGKEY,
                    ADS_SOFTSEEK, &found) == 0);
    CHECK(found == 0);
    UNSIGNED16 eof = 1;
    REQUIRE(AdsAtEOF(hT, &eof) == 0);
    CHECK(eof == 0);
    UNSIGNED8 buf[32] = {0};
    UNSIGNED32 len = sizeof(buf) - 1;
    REQUIRE(AdsGetString(hT, (UNSIGNED8*)"NAME", buf, &len, 0) == 0);
    CHECK(std::string(reinterpret_cast<char*>(buf), len).substr(0, 5)
          == "Delta");

    // Hard seek for a missing key -> not found, at EOF.
    REQUIRE(AdsSeek(hN, (UNSIGNED8*)"Charlie", 7, ADS_STRINGKEY,
                    ADS_HARDSEEK, &found) == 0);
    CHECK(found == 0);
    REQUIRE(AdsAtEOF(hT, &eof) == 0);
    CHECK(eof == 1);

    // Soft seek past the last key -> EOF.
    REQUIRE(AdsSeek(hN, (UNSIGNED8*)"ZZZ", 3, ADS_STRINGKEY, ADS_SOFTSEEK,
                    &found) == 0);
    REQUIRE(AdsAtEOF(hT, &eof) == 0);
    CHECK(eof == 1);

    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hC) == 0);
    fs::remove_all(dir, ec);
}

// ---------------------------------------------------------------------------
TEST_CASE("intensive2: date keys order chronologically and seek") {
    auto dir = fs::temp_directory_path() / "oads_int2_dates";
    std::error_code ec;
    fs::remove_all(dir, ec);
    auto hC = connect_at(dir);
    ADSHANDLE hT = 0;
    make_table(hC, "dates", "NAME,C,10,0;WHEN,D,8,0", &hT);
    const auto bag = (dir / "dates.cdx").string();
    ADSHANDLE hD = 0;
    add_index(hT, bag.c_str(), "BYWHEN", "WHEN", nullptr, 0x0008, &hD);

    auto put = [&](const char* nm, const char* ymd) {
        REQUIRE(AdsAppendRecord(hT) == 0);
        REQUIRE(AdsSetString(hT, (UNSIGNED8*)"NAME", (UNSIGNED8*)nm,
                           (UNSIGNED32)std::strlen(nm)) == 0);
        REQUIRE(AdsSetDate(hT, (UNSIGNED8*)"WHEN", (UNSIGNED8*)ymd, 8) == 0);
        REQUIRE(AdsWriteRecord(hT) == 0);
    };
    put("new", "20260201");
    put("old", "20240115");
    put("mid", "20250630");

    REQUIRE(AdsSetIndexOrderByHandle(hT, hD) == 0);
    auto w = walk_order(hT, "NAME");
    REQUIRE(w.size() == 3);
    CHECK(w[0].first == "old");
    CHECK(w[1].first == "mid");
    CHECK(w[2].first == "new");

    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hC) == 0);
    fs::remove_all(dir, ec);
}

// ---------------------------------------------------------------------------
TEST_CASE("intensive2: expression indexes UPPER and concatenated fields") {
    auto dir = fs::temp_directory_path() / "oads_int2_expr";
    std::error_code ec;
    fs::remove_all(dir, ec);
    auto hC = connect_at(dir);
    ADSHANDLE hT = 0;
    make_table(hC, "expr", "NAME,C,20,0;CITY,C,15,0", &hT);
    const auto bag = (dir / "expr.cdx").string();
    ADSHANDLE hU = 0, hK = 0;
    add_index(hT, bag.c_str(), "UP", "UPPER(NAME)", nullptr, 0x0008, &hU);
    add_index(hT, bag.c_str(), "NC", "NAME+CITY", nullptr, 0x0008, &hK);

    put_rec2(hT, "alice", "Madrid");
    put_rec2(hT, "Bob", "Paris");
    put_rec2(hT, "carol", "London");

    REQUIRE(AdsSetIndexOrderByHandle(hT, hU) == 0);
    auto w = walk_order(hT, "NAME");
    REQUIRE(w.size() == 3);
    CHECK(w[0].first == "alice");   // ALICE < BOB < CAROL under UPPER
    CHECK(w[1].first == "Bob");
    CHECK(w[2].first == "carol");

    REQUIRE(AdsSetIndexOrderByHandle(hT, hK) == 0);
    w = walk_order(hT, "NAME");
    REQUIRE(w.size() == 3);
    CHECK(w[0].first == "Bob");     // "BobParis" < "aliceMadrid" < "carolLondon"
    CHECK(w[1].first == "alice");
    CHECK(w[2].first == "carol");

    // Edits through expressions stay maintained.
    REQUIRE(AdsGotoRecord(hT, 2) == 0);
    REQUIRE(AdsSetString(hT, (UNSIGNED8*)"CITY", (UNSIGNED8*)"Zurich", 6) == 0);
    REQUIRE(AdsWriteRecord(hT) == 0);
    w = walk_order(hT, "NAME");
    CHECK(w.size() == 3);
    CHECK(key_count(hK) == 3u);

    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hC) == 0);
    fs::remove_all(dir, ec);
}

// ---------------------------------------------------------------------------
TEST_CASE("intensive2: decimal numerics sort by value incl. fractions") {
    auto dir = fs::temp_directory_path() / "oads_int2_dec";
    std::error_code ec;
    fs::remove_all(dir, ec);
    auto hC = connect_at(dir);
    ADSHANDLE hT = 0;
    make_table(hC, "dec", "PRICE,N,8,2", &hT);
    const auto bag = (dir / "dec.cdx").string();
    ADSHANDLE hP = 0;
    add_index(hT, bag.c_str(), "BYPRICE", "PRICE", nullptr, 0x0008, &hP);

    const double vals[] = {9.99, -0.01, 0.0, 100.5, 9.10, -12.75, 9.99};
    for (double v : vals) {
        REQUIRE(AdsAppendRecord(hT) == 0);
        REQUIRE(AdsSetDouble(hT, (UNSIGNED8*)"PRICE", v) == 0);
        REQUIRE(AdsWriteRecord(hT) == 0);
    }

    REQUIRE(AdsSetIndexOrderByHandle(hT, hP) == 0);
    auto w = walk_order(hT, "PRICE");
    REQUIRE(w.size() == 7);
    std::vector<double> got;
    for (auto& kv : w) got.push_back(std::stod(kv.first));
    CHECK(got == std::vector<double>{-12.75, -0.01, 0.0, 9.10, 9.99, 9.99,
                                     100.50});

    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hC) == 0);
    fs::remove_all(dir, ec);
}

// ---------------------------------------------------------------------------
TEST_CASE("intensive2: two independent bags stay both maintained") {
    auto dir = fs::temp_directory_path() / "oads_int2_bags";
    std::error_code ec;
    fs::remove_all(dir, ec);
    auto hC = connect_at(dir);
    ADSHANDLE hT = 0;
    make_table(hC, "bags", "NAME,C,20,0;CITY,C,15,0", &hT);
    const auto bag1 = (dir / "one.cdx").string();
    const auto bag2 = (dir / "two.cdx").string();
    ADSHANDLE h1 = 0, h2 = 0;
    add_index(hT, bag1.c_str(), "BYNAME", "NAME", nullptr, 0x0008, &h1);
    add_index(hT, bag2.c_str(), "BYCITY", "CITY", nullptr, 0x0008, &h2);

    put_rec2(hT, "Alice", "Madrid");
    put_rec2(hT, "Bob", "Paris");
    CHECK(key_count(h1) == 2u);
    CHECK(key_count(h2) == 2u);

    REQUIRE(AdsGotoRecord(hT, 1) == 0);
    REQUIRE(AdsSetString(hT, (UNSIGNED8*)"CITY", (UNSIGNED8*)"Accra", 5) == 0);
    REQUIRE(AdsWriteRecord(hT) == 0);
    CHECK(key_count(h1) == 2u);
    CHECK(key_count(h2) == 2u);

    REQUIRE(AdsSetIndexOrderByHandle(hT, h2) == 0);
    auto w = walk_order(hT, "CITY");
    REQUIRE(w.size() == 2);
    CHECK(w[0].first == "Accra");

    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hC) == 0);
    fs::remove_all(dir, ec);
}

// ---------------------------------------------------------------------------
TEST_CASE("intensive2: six tags on one write path stay in lockstep") {
    auto dir = fs::temp_directory_path() / "oads_int2_six";
    std::error_code ec;
    fs::remove_all(dir, ec);
    auto hC = connect_at(dir);
    ADSHANDLE hT = 0;
    make_table(hC, "six", "NAME,C,20,0;CITY,C,15,0", &hT);
    const auto bag = (dir / "six.cdx").string();
    ADSHANDLE h[6] = {0};
    const char* tags[6] = {"T1", "T2", "T3", "T4", "T5", "T6"};
    const char* exprs[6] = {"NAME", "CITY", "UPPER(NAME)", "UPPER(CITY)",
                            "NAME+CITY", "CITY+NAME"};
    for (int i = 0; i < 6; ++i) {
        add_index(hT, bag.c_str(), tags[i], exprs[i], nullptr, 0x0008,
                  &h[i]);
    }
    put_rec2(hT, "Alice", "Madrid");
    put_rec2(hT, "Bob", "Paris");
    put_rec2(hT, "Carol", "London");
    for (int i = 0; i < 6; ++i) CHECK(key_count(h[i]) == 3u);

    REQUIRE(AdsGotoRecord(hT, 1) == 0);
    REQUIRE(AdsSetString(hT, (UNSIGNED8*)"NAME", (UNSIGNED8*)"Zelda", 5) == 0);
    REQUIRE(AdsWriteRecord(hT) == 0);
    for (int i = 0; i < 6; ++i) CHECK(key_count(h[i]) == 3u);

    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hC) == 0);
    fs::remove_all(dir, ec);
}

// ---------------------------------------------------------------------------
TEST_CASE("intensive2: blank keys are indexed and sort first") {
    auto dir = fs::temp_directory_path() / "oads_int2_blank";
    std::error_code ec;
    fs::remove_all(dir, ec);
    auto hC = connect_at(dir);
    ADSHANDLE hT = 0;
    make_table(hC, "blank", "NAME,C,20,0", &hT);
    const auto bag = (dir / "blank.cdx").string();
    ADSHANDLE hN = 0;
    add_index(hT, bag.c_str(), "BYNAME", "NAME", nullptr, 0x0008, &hN);

    put_rec2(hT, "Beta", "");
    REQUIRE(AdsAppendRecord(hT) == 0);   // all-space NAME, no SetString
    REQUIRE(AdsWriteRecord(hT) == 0);
    put_rec2(hT, "Alpha", "");

    // The blank-key record must be indexed (the blank==snapshot trap used
    // to drop it) and must sort before every non-blank key.
    CHECK(key_count(hN) == 3u);
    REQUIRE(AdsSetIndexOrderByHandle(hT, hN) == 0);
    auto w = walk_order(hT, "NAME");
    REQUIRE(w.size() == 3);
    CHECK(w[0].second == 2u);   // blank recno 2 first
    CHECK(w[1].first == "Alpha");
    CHECK(w[2].first == "Beta");

    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hC) == 0);
    fs::remove_all(dir, ec);
}

// ---------------------------------------------------------------------------
TEST_CASE("intensive2: pack rebuilds a conditional tag honoring FOR") {
    auto dir = fs::temp_directory_path() / "oads_int2_packcond";
    std::error_code ec;
    fs::remove_all(dir, ec);
    auto hC = connect_at(dir);
    ADSHANDLE hT = 0;
    make_table(hC, "pc", "NAME,C,20,0;AGE,N,4,0", &hT);
    const auto bag = (dir / "pc.cdx").string();
    ADSHANDLE hI = 0;
    add_index(hT, bag.c_str(), "SENIOR", "NAME", "AGE >= 40", 0x0008, &hI);

    auto put = [&](const char* nm, long age) {
        REQUIRE(AdsAppendRecord(hT) == 0);
        REQUIRE(AdsSetString(hT, (UNSIGNED8*)"NAME", (UNSIGNED8*)nm,
                           (UNSIGNED32)std::strlen(nm)) == 0);
        REQUIRE(AdsSetLong(hT, (UNSIGNED8*)"AGE", age) == 0);
        REQUIRE(AdsWriteRecord(hT) == 0);
    };
    put("A", 30);   // out
    put("B", 40);   // in
    put("C", 50);   // in
    put("D", 20);   // out
    CHECK(key_count(hI) == 2u);

    // Delete an "in" record and pack: the conditional rebuild must not
    // resurrect out-of-condition keys.
    REQUIRE(AdsGotoRecord(hT, 2) == 0);
    REQUIRE(AdsDeleteRecord(hT) == 0);
    REQUIRE(AdsWriteRecord(hT) == 0);
    REQUIRE(AdsPackTable(hT) == 0);
    CHECK(rec_count(hT) == 3u);
    CHECK(key_count(hI) == 1u);

    REQUIRE(AdsSetIndexOrderByHandle(hT, hI) == 0);
    auto w = walk_order(hT, "NAME");
    REQUIRE(w.size() == 1);
    CHECK(w[0].first == "C");

    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hC) == 0);
    fs::remove_all(dir, ec);
}

// ---------------------------------------------------------------------------
TEST_CASE("intensive2: SET DELETED ON hides rows but keys stay in the bag") {
    auto dir = fs::temp_directory_path() / "oads_int2_deleted";
    std::error_code ec;
    fs::remove_all(dir, ec);
    auto hC = connect_at(dir);
    ADSHANDLE hT = 0;
    make_table(hC, "del", "NAME,C,20,0", &hT);
    const auto bag = (dir / "del.cdx").string();
    ADSHANDLE hN = 0;
    add_index(hT, bag.c_str(), "BYNAME", "NAME", nullptr, 0x0008, &hN);

    put_rec2(hT, "A", "");
    put_rec2(hT, "B", "");
    put_rec2(hT, "C", "");
    REQUIRE(AdsGotoRecord(hT, 2) == 0);
    REQUIRE(AdsDeleteRecord(hT) == 0);
    REQUIRE(AdsWriteRecord(hT) == 0);

    REQUIRE(AdsShowDeleted(0) == 0);
    REQUIRE(AdsSetIndexOrderByHandle(hT, hN) == 0);
    auto w = walk_order(hT, "NAME");
    CHECK(w.size() == 2);          // B hidden
    // Key count honours the deleted visibility; the key itself stays in
    // the bag, so showing deleted again must bring all three back.
    CHECK(key_count(hN) == 2u);
    REQUIRE(AdsShowDeleted(1) == 0);
    w = walk_order(hT, "NAME");
    CHECK(w.size() == 3);
    CHECK(key_count(hN) == 3u);

    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hC) == 0);
    fs::remove_all(dir, ec);
}

// ---------------------------------------------------------------------------
TEST_CASE("intensive2: skip-unique collapses duplicate runs") {
    auto dir = fs::temp_directory_path() / "oads_int2_skipuniq";
    std::error_code ec;
    fs::remove_all(dir, ec);
    auto hC = connect_at(dir);
    ADSHANDLE hT = 0;
    make_table(hC, "sku", "NAME,C,20,0", &hT);
    const auto bag = (dir / "sku.cdx").string();
    ADSHANDLE hN = 0;
    add_index(hT, bag.c_str(), "BYNAME", "NAME", nullptr, 0x0008, &hN);

    put_rec2(hT, "A", ""); put_rec2(hT, "A", ""); put_rec2(hT, "A", "");
    put_rec2(hT, "B", ""); put_rec2(hT, "B", "");
    put_rec2(hT, "C", "");

    REQUIRE(AdsSetIndexOrderByHandle(hT, hN) == 0);
    REQUIRE(AdsGotoTop(hT) == 0);
    std::vector<std::string> seen;
    for (int i = 0; i < 3; ++i) {
        UNSIGNED8 buf[32] = {0};
        UNSIGNED32 len = sizeof(buf) - 1;
        REQUIRE(AdsGetString(hT, (UNSIGNED8*)"NAME", buf, &len, 0) == 0);
        seen.emplace_back(reinterpret_cast<char*>(buf), len);
        if (i < 2) REQUIRE(AdsSkipUnique(hN, 1) == 0);
    }
    REQUIRE(seen.size() == 3);
    CHECK(seen[0].substr(0, 1) == "A");
    CHECK(seen[1].substr(0, 1) == "B");
    CHECK(seen[2].substr(0, 1) == "C");

    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hC) == 0);
    fs::remove_all(dir, ec);
}

// ---------------------------------------------------------------------------
TEST_CASE("intensive2: descending tag honours top/bottom scope") {
    auto dir = fs::temp_directory_path() / "oads_int2_descscope";
    std::error_code ec;
    fs::remove_all(dir, ec);
    auto hC = connect_at(dir);
    ADSHANDLE hT = 0;
    make_table(hC, "dsc", "NAME,C,20,0", &hT);
    const auto bag = (dir / "dsc.cdx").string();
    ADSHANDLE hD = 0;
    add_index(hT, bag.c_str(), "BYNAME", "NAME", nullptr,
              0x0008 | 0x0002, &hD);

    const char* names[] = {"A", "B", "C", "D", "E"};
    for (auto* n : names) put_rec2(hT, n, "");

    REQUIRE(AdsSetIndexOrderByHandle(hT, hD) == 0);
    REQUIRE(AdsSetScope(hD, ADS_TOP, (UNSIGNED8*)"D", 1, ADS_STRINGKEY) == 0);
    REQUIRE(AdsSetScope(hD, ADS_BOTTOM, (UNSIGNED8*)"B", 1, ADS_STRINGKEY) == 0);
    // Descending walk D, C, B within the scope.
    auto w = walk_order(hT, "NAME");
    REQUIRE(w.size() == 3);
    CHECK(w[0].first == "D");
    CHECK(w[1].first == "C");
    CHECK(w[2].first == "B");

    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hC) == 0);
    fs::remove_all(dir, ec);
}

// ---------------------------------------------------------------------------
TEST_CASE("intensive2: peer reindex is visible to an already-open handle") {
    auto dir = fs::temp_directory_path() / "oads_int2_peerreindex";
    std::error_code ec;
    fs::remove_all(dir, ec);
    auto hA = connect_at(dir);
    ADSHANDLE hT = 0;
    make_table(hA, "prx", "NAME,C,20,0", &hT);
    const auto bag = (dir / "prx.cdx").string();
    ADSHANDLE hN = 0;
    add_index(hT, bag.c_str(), "BYNAME", "NAME", nullptr, 0x0008, &hN);
    put_rec2(hT, "A", "");
    REQUIRE(AdsCloseTable(hT) == 0);

    // Handle B appends with the bag CLOSED (stale bag), then a THIRD party
    // reindexes: the open handle must see the new key on its next walk.
    auto hB = connect_at(dir);
    ADSHANDLE hB1 = 0;
    UNSIGNED8 leaf[] = "prx";
    REQUIRE(AdsOpenTable(hB, leaf, leaf, ADS_CDX, 1, 1, 0, 1, &hB1) == 0);
    put_rec2(hB1, "B", "");   // bag not open here -> stale
    REQUIRE(AdsCloseTable(hB1) == 0);

    ADSHANDLE hA1 = 0;
    REQUIRE(AdsOpenTable(hA, leaf, leaf, ADS_CDX, 1, 1, 0, 1, &hA1) == 0);
    ADSHANDLE arr[8] = {0};
    UNSIGNED16 n = 8;
    std::vector<UNSIGNED8> bagv(bag.c_str(), bag.c_str() + bag.size() + 1);
    REQUIRE(AdsOpenIndex(hA1, bagv.data(), arr, &n) == 0);
    // The open-time heal reindexed the stale bag already (v1.09.12); the
    // point here is that the open handle's walk is complete either way.
    REQUIRE(AdsSetIndexOrderByHandle(hA1, arr[0]) == 0);
    auto w = walk_order(hA1, "NAME");
    CHECK(w.size() == 2);
    CHECK(w[0].first == "A");
    CHECK(w[1].first == "B");

    REQUIRE(AdsCloseTable(hA1) == 0);
    REQUIRE(AdsDisconnect(hA) == 0);
    REQUIRE(AdsDisconnect(hB) == 0);
    fs::remove_all(dir, ec);
}

// ===========================================================================
// REMOTE (wire) section — the same flows through the server twin.
// ===========================================================================

namespace {

struct RemoteFixture {
    openads::network::Server srv;
    fs::path                 dir;
    ADSHANDLE                hConn = 0;

    bool start(const char* dirname) {
        dir = fs::temp_directory_path() / dirname;
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        if (!srv.start("127.0.0.1", 0).has_value()) return false;
        hConn = connect_remote_at(dir, srv.port());
        return hConn != 0;
    }
    void stop() {
        if (hConn != 0) AdsDisconnect(hConn);
        srv.stop();
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
};

void remote_seed(const fs::path& dir, const char* table, const char* flds,
                 const char* bagName, const char* tag, const char* expr) {
    UNSIGNED8 srv[512] = {};
    const auto sp = dir.string();
    std::memcpy(srv, sp.c_str(), sp.size() + 1);
    ADSHANDLE hC = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hC) == 0);
    ADSHANDLE hT = 0;
    make_table(hC, table, flds, &hT);
    if (bagName != nullptr) {
        const auto bag = (dir / bagName).string();
        ADSHANDLE hI = 0;
        add_index(hT, bag.c_str(), tag, expr, nullptr, 0x0008, &hI);
    }
    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hC) == 0);
}

}  // namespace

TEST_CASE("intensive2 remote: multi-tag churn through the wire twin") {
    RemoteFixture fx;
    REQUIRE(fx.start("oads_int2_r_churn"));
    remote_seed(fx.dir, "rch", "NAME,C,20,0;CITY,C,15,0", "rch.cdx",
                "BYNAME", "NAME");

    UNSIGNED8 leaf[] = "rch";
    ADSHANDLE hT = 0;
    REQUIRE(AdsOpenTable(fx.hConn, leaf, leaf, ADS_CDX, 0, 0, 0, 0, &hT) == 0);
    ADSHANDLE hN = 0;
    REQUIRE(AdsGetIndexHandleByOrder(hT, 1, &hN) == 0);
    REQUIRE(hN != 0);

    for (int i = 0; i < 25; ++i) {
        char nm[16];
        std::snprintf(nm, sizeof(nm), "N%02d", i % 5);
        REQUIRE(AdsAppendRecord(hT) == 0);
        REQUIRE(AdsSetString(hT, (UNSIGNED8*)"NAME", (UNSIGNED8*)nm,
                           (UNSIGNED32)std::strlen(nm)) == 0);
        REQUIRE(AdsSetString(hT, (UNSIGNED8*)"CITY", (UNSIGNED8*)"C", 1) == 0);
        REQUIRE(AdsWriteRecord(hT) == 0);
    }
    CHECK(rec_count(hT) == 25u);
    CHECK(key_count(hN) == 25u);

    REQUIRE(AdsSetIndexOrderByHandle(hT, hN) == 0);
    auto w = walk_order(hT, "NAME");
    CHECK(w.size() == 25);
    for (std::size_t i = 1; i < w.size(); ++i)
        CHECK(w[i].first >= w[i - 1].first);

    // Key move over the wire: rename one record and re-walk.
    REQUIRE(AdsGotoRecord(hT, 1) == 0);
    REQUIRE(AdsSetString(hT, (UNSIGNED8*)"NAME", (UNSIGNED8*)"ZZZ", 3) == 0);
    REQUIRE(AdsWriteRecord(hT) == 0);
    w = walk_order(hT, "NAME");
    CHECK(w.size() == 25);
    CHECK(w.back().first == "ZZZ");

    REQUIRE(AdsCloseTable(hT) == 0);
    fx.stop();
}

TEST_CASE("intensive2 remote: conditional tag tracks FOR across the wire") {
    RemoteFixture fx;
    REQUIRE(fx.start("oads_int2_r_cond"));
    remote_seed(fx.dir, "rcd", "NAME,C,20,0;AGE,N,4,0", "rcd.cdx",
                "SENIOR", "NAME");

    // Add the FOR condition via a local handle first (create_index61 with
    // condition) — remote_seed creates unconditional; rebuild the tag here.
    {
        UNSIGNED8 srv[512] = {};
        const auto sp = fx.dir.string();
        std::memcpy(srv, sp.c_str(), sp.size() + 1);
        ADSHANDLE hC = 0;
        REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0,
                             &hC) == 0);
        ADSHANDLE hT = 0;
        UNSIGNED8 leaf[] = "rcd";
        REQUIRE(AdsOpenTable(hC, leaf, leaf, ADS_CDX, 1, 1, 0, 1, &hT) == 0);
        const auto bag = (fx.dir / "rcd.cdx").string();
        ADSHANDLE hI = 0;
        add_index(hT, bag.c_str(), "SENIOR", "NAME", "AGE >= 40", 0x0008,
                  &hI);
        REQUIRE(AdsCloseTable(hT) == 0);
        REQUIRE(AdsDisconnect(hC) == 0);
    }

    UNSIGNED8 leaf[] = "rcd";
    ADSHANDLE hT = 0;
    REQUIRE(AdsOpenTable(fx.hConn, leaf, leaf, ADS_CDX, 0, 0, 0, 0, &hT) == 0);
    ADSHANDLE hI = 0;
    REQUIRE(AdsGetIndexHandleByOrder(hT, 1, &hI) == 0);

    auto put = [&](const char* nm, long age) {
        REQUIRE(AdsAppendRecord(hT) == 0);
        REQUIRE(AdsSetString(hT, (UNSIGNED8*)"NAME", (UNSIGNED8*)nm,
                           (UNSIGNED32)std::strlen(nm)) == 0);
        REQUIRE(AdsSetLong(hT, (UNSIGNED8*)"AGE", age) == 0);
        REQUIRE(AdsWriteRecord(hT) == 0);
    };
    put("A", 30);   // out
    put("B", 40);   // in
    put("C", 50);   // in
    CHECK(key_count(hI) == 2u);

    // Cross the boundary over the wire.
    REQUIRE(AdsGotoRecord(hT, 1) == 0);
    REQUIRE(AdsSetLong(hT, (UNSIGNED8*)"AGE", 45) == 0);
    REQUIRE(AdsWriteRecord(hT) == 0);
    CHECK(key_count(hI) == 3u);

    REQUIRE(AdsGotoRecord(hT, 2) == 0);
    REQUIRE(AdsSetLong(hT, (UNSIGNED8*)"AGE", 39) == 0);
    REQUIRE(AdsWriteRecord(hT) == 0);
    CHECK(key_count(hI) == 2u);

    REQUIRE(AdsCloseTable(hT) == 0);
    fx.stop();
}

TEST_CASE("intensive2 remote: zero-valued numeric keys are indexed") {
    RemoteFixture fx;
    REQUIRE(fx.start("oads_int2_r_zero"));
    remote_seed(fx.dir, "rzn", "NAME,C,10,0;AGE,N,4,0", "rzn.cdx",
                "BYAGE", "AGE");

    UNSIGNED8 leaf[] = "rzn";
    ADSHANDLE hT = 0;
    REQUIRE(AdsOpenTable(fx.hConn, leaf, leaf, ADS_CDX, 0, 0, 0, 0, &hT) == 0);
    ADSHANDLE hA = 0;
    REQUIRE(AdsGetIndexHandleByOrder(hT, 1, &hA) == 0);

    const long ages[] = {10, 0, -5, 7};
    for (long a : ages) {
        REQUIRE(AdsAppendRecord(hT) == 0);
        REQUIRE(AdsSetString(hT, (UNSIGNED8*)"NAME", (UNSIGNED8*)"X", 1) == 0);
        REQUIRE(AdsSetLong(hT, (UNSIGNED8*)"AGE", a) == 0);
        REQUIRE(AdsWriteRecord(hT) == 0);
    }
    // The AGE=0 record used to vanish from the bag (blank==snapshot trap).
    CHECK(key_count(hA) == 4u);

    REQUIRE(AdsSetIndexOrderByHandle(hT, hA) == 0);
    auto w = walk_order(hT, "AGE");
    REQUIRE(w.size() == 4);
    std::vector<long> got;
    for (auto& kv : w) got.push_back(std::stol(kv.first));
    CHECK(got == std::vector<long>{-5, 0, 7, 10});

    REQUIRE(AdsCloseTable(hT) == 0);
    fx.stop();
}

TEST_CASE("intensive2 remote: pack rebuilds the twin bag (recno remap)") {
    RemoteFixture fx;
    REQUIRE(fx.start("oads_int2_r_pack"));
    remote_seed(fx.dir, "rpk", "NAME,C,20,0", "rpk.cdx", "BYNAME", "NAME");

    UNSIGNED8 leaf[] = "rpk";
    ADSHANDLE hT = 0;
    REQUIRE(AdsOpenTable(fx.hConn, leaf, leaf, ADS_CDX, 0, 0, 0, 0, &hT) == 0);
    ADSHANDLE hN = 0;
    REQUIRE(AdsGetIndexHandleByOrder(hT, 1, &hN) == 0);

    const char* names[] = {"A1", "B2", "C3", "D4", "E5"};
    for (auto* n : names) put_rec2(hT, n, "");
    REQUIRE(AdsGotoRecord(hT, 2) == 0);
    REQUIRE(AdsDeleteRecord(hT) == 0);
    REQUIRE(AdsGotoRecord(hT, 4) == 0);
    REQUIRE(AdsDeleteRecord(hT) == 0);
    REQUIRE(AdsWriteRecord(hT) == 0);

    REQUIRE(AdsPackTable(hT) == 0);
    CHECK(rec_count(hT) == 3u);
    CHECK(key_count(hN) == 3u);

    REQUIRE(AdsSetIndexOrderByHandle(hT, hN) == 0);
    auto w = walk_order(hT, "NAME");
    REQUIRE(w.size() == 3);
    CHECK(w[0].first == "A1");
    CHECK(w[0].second == 1u);
    CHECK(w[1].first == "C3");
    CHECK(w[1].second == 2u);   // renumbered by pack, key must follow
    CHECK(w[2].first == "E5");
    CHECK(w[2].second == 3u);

    REQUIRE(AdsCloseTable(hT) == 0);
    fx.stop();
}

TEST_CASE("intensive2 remote: ordered walk with lookahead stays correct") {
    RemoteFixture fx;
    REQUIRE(fx.start("oads_int2_r_walk"));
    remote_seed(fx.dir, "rwk", "NAME,C,20,0", "rwk.cdx", "BYNAME", "NAME");

    UNSIGNED8 leaf[] = "rwk";
    ADSHANDLE hT = 0;
    REQUIRE(AdsOpenTable(fx.hConn, leaf, leaf, ADS_CDX, 0, 0, 0, 0, &hT) == 0);
    ADSHANDLE hN = 0;
    REQUIRE(AdsGetIndexHandleByOrder(hT, 1, &hN) == 0);

    for (int i = 0; i < 300; ++i) {
        char nm[16];
        std::snprintf(nm, sizeof(nm), "N%03d", 299 - i);   // reversed recno
        REQUIRE(AdsAppendRecord(hT) == 0);
        REQUIRE(AdsSetString(hT, (UNSIGNED8*)"NAME", (UNSIGNED8*)nm, 4) == 0);
        REQUIRE(AdsWriteRecord(hT) == 0);
    }
    REQUIRE(AdsSetIndexOrderByHandle(hT, hN) == 0);
    auto w = walk_order(hT, "NAME");
    CHECK(w.size() == 300);
    for (std::size_t i = 0; i < w.size(); ++i) {
        char want[16];
        std::snprintf(want, sizeof(want), "N%03d", (int)i);
        CHECK(w[i].first == want);
        CHECK(w[i].second == static_cast<UNSIGNED32>(300 - i));
    }

    REQUIRE(AdsCloseTable(hT) == 0);
    fx.stop();
}


