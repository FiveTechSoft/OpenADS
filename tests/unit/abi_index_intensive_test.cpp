// Intensive index-usage battery (Pritpal Bedi: "play with the indexes").
// Hammers the CDX implementation across the axes the ERP stresses:
// multi-tag maintenance, key-move edits, delete/recall churn, duplicate
// keys past the split point, unique/conditional/descending tags, numeric
// encodings, scopes, pack/zap with a live bag, peer-handle visibility,
// and the custom .z01 extension end to end. Every case asserts not just
// key counts but ORDER and CONTENT of the walk, plus seek correctness.

#include "doctest.h"
#include "openads/ace.h"

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

void make_table(ADSHANDLE hConn, const char* name, ADSHANDLE* hTbl) {
    UNSIGNED8 flds[] = "NAME,C,20,0;CITY,C,15,0;AGE,N,4,0";
    std::vector<UNSIGNED8> n(name, name + std::strlen(name) + 1);
    REQUIRE(AdsCreateTable(hConn, n.data(), n.data(), ADS_CDX, ADS_ANSI,
                           ADS_CHECKRIGHTS, ADS_DEFAULT, 0, flds, hTbl) == 0);
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

void put_rec(ADSHANDLE hTbl, const char* name, const char* city, long age) {
    REQUIRE(AdsAppendRecord(hTbl) == 0);
    REQUIRE(AdsSetString(hTbl, (UNSIGNED8*)"NAME", (UNSIGNED8*)name,
                       (UNSIGNED32)std::strlen(name)) == 0);
    REQUIRE(AdsSetString(hTbl, (UNSIGNED8*)"CITY", (UNSIGNED8*)city,
                       (UNSIGNED32)std::strlen(city)) == 0);
    REQUIRE(AdsSetLong(hTbl, (UNSIGNED8*)"AGE", age) == 0);
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

// Walk the active order collecting (fieldValue, recno) pairs.
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

bool ordered_unique(const std::vector<std::pair<std::string, UNSIGNED32>>& w) {
    for (std::size_t i = 1; i < w.size(); ++i) {
        if (w[i].first < w[i - 1].first) return false;
        if (w[i].first == w[i - 1].first && w[i].second <= w[i - 1].second)
            return false;
    }
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------
TEST_CASE("intensive: 3 tags stay in sync through append/edit/delete churn") {
    auto dir = fs::temp_directory_path() / "oads_int_churn";
    std::error_code ec;
    fs::remove_all(dir, ec);
    auto hC = connect_at(dir);
    ADSHANDLE hT = 0;
    make_table(hC, "churn", &hT);
    const auto bag = (dir / "churn.cdx").string();
    ADSHANDLE hN = 0, hC2 = 0, hA = 0;
    add_index(hT, bag.c_str(), "BYNAME", "NAME", nullptr, 0x0008, &hN);
    add_index(hT, bag.c_str(), "BYCITY", "CITY", nullptr, 0x0008, &hC2);
    add_index(hT, bag.c_str(), "BYAGE", "AGE", nullptr, 0x0008, &hA);

    put_rec(hT, "Alice", "Madrid", 30);
    put_rec(hT, "Bob", "Paris", 40);
    put_rec(hT, "Carol", "London", 35);
    CHECK(key_count(hN) == 3u);
    CHECK(key_count(hC2) == 3u);
    CHECK(key_count(hA) == 3u);

    // Key moves: rename twice, re-city once, re-age once.
    REQUIRE(AdsGotoRecord(hT, 1) == 0);
    REQUIRE(AdsSetString(hT, (UNSIGNED8*)"NAME", (UNSIGNED8*)"Zelda", 5) == 0);
    REQUIRE(AdsWriteRecord(hT) == 0);
    REQUIRE(AdsGotoRecord(hT, 2) == 0);
    REQUIRE(AdsSetString(hT, (UNSIGNED8*)"CITY", (UNSIGNED8*)"Accra", 5) == 0);
    REQUIRE(AdsWriteRecord(hT) == 0);

    CHECK(key_count(hN) == 3u);
    CHECK(key_count(hC2) == 3u);
    CHECK(key_count(hA) == 3u);

    // Order reflects the moves.
    REQUIRE(AdsSetIndexOrderByHandle(hT, hN) == 0);
    auto w = walk_order(hT, "NAME");
    REQUIRE(w.size() == 3);
    CHECK(w[0].first == "Bob");
    CHECK(w[1].first == "Carol");
    CHECK(w[2].first == "Zelda");

    REQUIRE(AdsSetIndexOrderByHandle(hT, hC2) == 0);
    w = walk_order(hT, "CITY");
    REQUIRE(w.size() == 3);
    CHECK(w[0].first == "Accra");
    CHECK(w[1].first == "London");
    CHECK(w[2].first == "Madrid");

    // Delete one, recall one: keys follow.
    REQUIRE(AdsGotoRecord(hT, 3) == 0);
    REQUIRE(AdsDeleteRecord(hT) == 0);
    REQUIRE(AdsWriteRecord(hT) == 0);
    // Deleted rows keep their keys (DBFCDX convention).
    CHECK(key_count(hN) == 3u);
    REQUIRE(AdsRecallRecord(hT) == 0);
    REQUIRE(AdsWriteRecord(hT) == 0);
    CHECK(key_count(hN) == 3u);

    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hC) == 0);
    fs::remove_all(dir, ec);
}

// ---------------------------------------------------------------------------
TEST_CASE("intensive: duplicate-heavy bulk load splits cleanly and seeks") {
    auto dir = fs::temp_directory_path() / "oads_int_bulk";
    std::error_code ec;
    fs::remove_all(dir, ec);
    auto hC = connect_at(dir);
    ADSHANDLE hT = 0;
    make_table(hC, "bulk", &hT);
    const auto bag = (dir / "bulk.cdx").string();
    ADSHANDLE hN = 0, hA = 0;
    add_index(hT, bag.c_str(), "BYNAME", "NAME", nullptr, 0x0008, &hN);
    add_index(hT, bag.c_str(), "BYAGE", "AGE", nullptr, 0x0008, &hA);

    // 2000 records over only 20 distinct names -> many duplicate keys and
    // forced B+tree splits (the record-151 parent-separator scenario).
    char nm[24];
    for (int i = 0; i < 2000; ++i) {
        std::snprintf(nm, sizeof(nm), "Name%02d", i % 20);
        put_rec(hT, nm, "City", i % 100);
    }
    CHECK(rec_count(hT) == 2000u);
    CHECK(key_count(hN) == 2000u);
    CHECK(key_count(hA) == 2000u);

    REQUIRE(AdsSetIndexOrderByHandle(hT, hN) == 0);
    auto w = walk_order(hT, "NAME");
    CHECK(w.size() == 2000);
    CHECK(ordered_unique(w));

    // Hard seek on a duplicated key lands on the EARLIEST recno of the run.
    UNSIGNED16 found = 0;
    REQUIRE(AdsSeek(hN, (UNSIGNED8*)"Name07", 6, ADS_STRINGKEY,
                    ADS_HARDSEEK, &found) == 0);
    CHECK(found == 1);
    UNSIGNED32 rn = 0;
    REQUIRE(AdsGetRecordNum(hT, 0, &rn) == 0);
    CHECK(rn == 8u);   // recnos 8, 28, 48, ... carry Name07

    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hC) == 0);
    fs::remove_all(dir, ec);
}

// ---------------------------------------------------------------------------
TEST_CASE("intensive: unique tag rejects duplicates, keeps order on edit") {
    auto dir = fs::temp_directory_path() / "oads_int_unique";
    std::error_code ec;
    fs::remove_all(dir, ec);
    auto hC = connect_at(dir);
    ADSHANDLE hT = 0;
    make_table(hC, "uniq", &hT);
    const auto bag = (dir / "uniq.cdx").string();
    ADSHANDLE hN = 0;
    add_index(hT, bag.c_str(), "BYNAME", "NAME", nullptr,
              0x0008 | 0x0001 /*compound|unique*/, &hN);

    put_rec(hT, "Alice", "Madrid", 30);
    put_rec(hT, "Bob", "Paris", 40);
    // Direct duplicate must fail at set or write time.
    CHECK(AdsAppendRecord(hT) == 0);
    const UNSIGNED32 dup_set = AdsSetString(hT, (UNSIGNED8*)"NAME",
                                            (UNSIGNED8*)"Alice", 5);
    const UNSIGNED32 dup_wrc = AdsWriteRecord(hT);
    CHECK((dup_set != 0 || dup_wrc != 0));

    CHECK(key_count(hN) == 2u);
    REQUIRE(AdsSetIndexOrderByHandle(hT, hN) == 0);
    auto w = walk_order(hT, "NAME");
    CHECK(w.size() >= 2);
    CHECK(w[0].first == "Alice");

    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hC) == 0);
    fs::remove_all(dir, ec);
}

// ---------------------------------------------------------------------------
TEST_CASE("intensive: descending tag walks backwards and stays maintained") {
    auto dir = fs::temp_directory_path() / "oads_int_desc";
    std::error_code ec;
    fs::remove_all(dir, ec);
    auto hC = connect_at(dir);
    ADSHANDLE hT = 0;
    make_table(hC, "descr", &hT);
    const auto bag = (dir / "descr.cdx").string();
    ADSHANDLE hD = 0;
    add_index(hT, bag.c_str(), "BYNAME", "NAME", nullptr,
              0x0008 | 0x0002 /*compound|descending*/, &hD);

    put_rec(hT, "Alice", "Madrid", 30);
    put_rec(hT, "Carol", "London", 35);
    put_rec(hT, "Bob", "Paris", 40);
    put_rec(hT, "Dave", "Rome", 25);

    REQUIRE(AdsSetIndexOrderByHandle(hT, hD) == 0);
    auto w = walk_order(hT, "NAME");
    REQUIRE(w.size() == 4);
    CHECK(w[0].first == "Dave");
    CHECK(w[1].first == "Carol");
    CHECK(w[2].first == "Bob");
    CHECK(w[3].first == "Alice");

    put_rec(hT, "Aaron", "Oslo", 50);
    w = walk_order(hT, "NAME");
    REQUIRE(w.size() == 5);
    CHECK(w[0].first == "Dave");
    CHECK(w[1].first == "Carol");
    CHECK(w[4].first == "Aaron");

    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hC) == 0);
    fs::remove_all(dir, ec);
}

// ---------------------------------------------------------------------------
TEST_CASE("intensive: conditional tag tracks edits across the FOR boundary") {
    auto dir = fs::temp_directory_path() / "oads_int_cond";
    std::error_code ec;
    fs::remove_all(dir, ec);
    auto hC = connect_at(dir);
    ADSHANDLE hT = 0;
    make_table(hC, "cond", &hT);
    const auto bag = (dir / "cond.cdx").string();
    ADSHANDLE hI = 0;
    add_index(hT, bag.c_str(), "SENIOR", "NAME", "AGE >= 40", 0x0008, &hI);

    put_rec(hT, "Alice", "Madrid", 30);   // out
    put_rec(hT, "Bob", "Paris", 40);      // in
    put_rec(hT, "Carol", "London", 35);   // out
    CHECK(key_count(hI) == 1u);

    // Cross the boundary both ways.
    REQUIRE(AdsGotoRecord(hT, 1) == 0);
    REQUIRE(AdsSetLong(hT, (UNSIGNED8*)"AGE", 41) == 0);
    REQUIRE(AdsWriteRecord(hT) == 0);
    CHECK(key_count(hI) == 2u);
    REQUIRE(AdsGotoRecord(hT, 2) == 0);
    REQUIRE(AdsSetLong(hT, (UNSIGNED8*)"AGE", 39) == 0);
    REQUIRE(AdsWriteRecord(hT) == 0);
    CHECK(key_count(hI) == 1u);

    REQUIRE(AdsSetIndexOrderByHandle(hT, hI) == 0);
    auto w = walk_order(hT, "NAME");
    REQUIRE(w.size() == 1);
    CHECK(w[0].first == "Alice");

    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hC) == 0);
    fs::remove_all(dir, ec);
}

// ---------------------------------------------------------------------------
TEST_CASE("intensive: numeric keys sort numerically incl. negatives") {
    auto dir = fs::temp_directory_path() / "oads_int_num";
    std::error_code ec;
    fs::remove_all(dir, ec);
    auto hC = connect_at(dir);
    ADSHANDLE hT = 0;
    make_table(hC, "num", &hT);
    const auto bag = (dir / "num.cdx").string();
    ADSHANDLE hA = 0;
    add_index(hT, bag.c_str(), "BYAGE", "AGE", nullptr, 0x0008, &hA);

    const long ages[] = {10, -5, 100, 0, -50, 7, 1000, 3};
    for (long a : ages) put_rec(hT, "X", "Y", a);

    REQUIRE(AdsSetIndexOrderByHandle(hT, hA) == 0);
    auto w = walk_order(hT, "AGE");
    REQUIRE(w.size() == 8);
    std::vector<long> got;
    for (auto& kv : w) got.push_back(std::stol(kv.first));
    CHECK(got == std::vector<long>{-50, -5, 0, 3, 7, 10, 100, 1000});

    // Seek a negative key.
    UNSIGNED16 found = 0;
    REQUIRE(AdsSeek(hA, (UNSIGNED8*)"-5", 2, ADS_STRINGKEY, ADS_HARDSEEK,
                    &found) == 0);
    CHECK(found == 1);
    SIGNED32 av = 0;
    REQUIRE(AdsGetLong(hT, (UNSIGNED8*)"AGE", &av) == 0);
    CHECK(av == -5);

    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hC) == 0);
    fs::remove_all(dir, ec);
}

// ---------------------------------------------------------------------------
TEST_CASE("intensive: scopes constrain walk and keycount; clear restores") {
    auto dir = fs::temp_directory_path() / "oads_int_scope";
    std::error_code ec;
    fs::remove_all(dir, ec);
    auto hC = connect_at(dir);
    ADSHANDLE hT = 0;
    make_table(hC, "scope", &hT);
    const auto bag = (dir / "scope.cdx").string();
    ADSHANDLE hN = 0;
    add_index(hT, bag.c_str(), "BYNAME", "NAME", nullptr, 0x0008, &hN);

    const char* names[] = {"Alpha", "Beta", "Gamma", "Delta", "Epsilon"};
    for (auto* n : names) put_rec(hT, n, "C", 1);

    REQUIRE(AdsSetIndexOrderByHandle(hT, hN) == 0);
    REQUIRE(AdsSetScope(hN, ADS_TOP, (UNSIGNED8*)"Beta", 4, ADS_STRINGKEY) == 0);
    REQUIRE(AdsSetScope(hN, ADS_BOTTOM, (UNSIGNED8*)"Delta", 5,
                        ADS_STRINGKEY) == 0);
    // Key order is Alpha Beta Delta Epsilon Gamma: Beta..Delta covers two.
    auto w = walk_order(hT, "NAME");
    REQUIRE(w.size() == 2);
    CHECK(w[0].first == "Beta");
    CHECK(w[1].first == "Delta");
    CHECK(key_count(hN) == 2u);

    REQUIRE(AdsClearScope(hN, ADS_TOP) == 0);
    REQUIRE(AdsClearScope(hN, ADS_BOTTOM) == 0);
    w = walk_order(hT, "NAME");
    CHECK(w.size() == 5);
    CHECK(key_count(hN) == 5u);

    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hC) == 0);
    fs::remove_all(dir, ec);
}

// ---------------------------------------------------------------------------
TEST_CASE("intensive: pack with live bag rebuilds recnos; zap empties it") {
    auto dir = fs::temp_directory_path() / "oads_int_pack";
    std::error_code ec;
    fs::remove_all(dir, ec);
    auto hC = connect_at(dir);
    ADSHANDLE hT = 0;
    make_table(hC, "packme", &hT);
    const auto bag = (dir / "packme.cdx").string();
    ADSHANDLE hN = 0;
    add_index(hT, bag.c_str(), "BYNAME", "NAME", nullptr, 0x0008, &hN);

    const char* names[] = {"A1", "B2", "C3", "D4", "E5"};
    for (auto* n : names) put_rec(hT, n, "C", 1);
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

    REQUIRE(AdsZapTable(hT) == 0);
    CHECK(rec_count(hT) == 0u);
    CHECK(key_count(hN) == 0u);
    put_rec(hT, "New", "C", 1);
    CHECK(key_count(hN) == 1u);

    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hC) == 0);
    fs::remove_all(dir, ec);
}

// ---------------------------------------------------------------------------
TEST_CASE("intensive: two handles alternate key moves on the same records") {
    auto dir = fs::temp_directory_path() / "oads_int_peer";
    std::error_code ec;
    fs::remove_all(dir, ec);
    auto hA = connect_at(dir);
    ADSHANDLE hT = 0;
    make_table(hA, "peer", &hT);
    const auto bag = (dir / "peer.cdx").string();
    ADSHANDLE hN = 0;
    add_index(hT, bag.c_str(), "BYNAME", "NAME", nullptr, 0x0008, &hN);
    put_rec(hT, "Alpha", "C", 1);
    put_rec(hT, "Beta", "C", 1);
    REQUIRE(AdsCloseTable(hT) == 0);

    auto hB = connect_at(dir);
    ADSHANDLE hT2 = 0;
    UNSIGNED8 leaf[] = "peer";
    REQUIRE(AdsOpenTable(hB, leaf, leaf, ADS_CDX, 1, 1, 0, 1, &hT2) == 0);
    ADSHANDLE arr[8] = {0};
    UNSIGNED16 n = 8;
    std::vector<UNSIGNED8> bagv(bag.c_str(), bag.c_str() + bag.size() + 1);
    REQUIRE(AdsOpenIndex(hT2, bagv.data(), arr, &n) == 0);
    REQUIRE(n >= 1u);

    // Handle A moves a key; handle B must see the new order immediately.
    ADSHANDLE hT1 = 0;
    REQUIRE(AdsOpenTable(hA, leaf, leaf, ADS_CDX, 1, 1, 0, 1, &hT1) == 0);
    ADSHANDLE arr1[8] = {0};
    UNSIGNED16 n1 = 8;
    REQUIRE(AdsOpenIndex(hT1, bagv.data(), arr1, &n1) == 0);
    REQUIRE(AdsGotoRecord(hT1, 1) == 0);
    REQUIRE(AdsSetString(hT1, (UNSIGNED8*)"NAME", (UNSIGNED8*)"Zulu", 4) == 0);
    REQUIRE(AdsWriteRecord(hT1) == 0);

    REQUIRE(AdsSetIndexOrderByHandle(hT2, arr[0]) == 0);
    auto w = walk_order(hT2, "NAME");
    REQUIRE(w.size() == 2);
    CHECK(w[0].first == "Beta");
    CHECK(w[1].first == "Zulu");

    REQUIRE(AdsCloseTable(hT1) == 0);
    REQUIRE(AdsCloseTable(hT2) == 0);
    REQUIRE(AdsDisconnect(hA) == 0);
    REQUIRE(AdsDisconnect(hB) == 0);
    fs::remove_all(dir, ec);
}

// ---------------------------------------------------------------------------
TEST_CASE("intensive: OrdKeyNo/OrdKeyCount/RelKeyPos consistent after churn") {
    auto dir = fs::temp_directory_path() / "oads_int_keyno";
    std::error_code ec;
    fs::remove_all(dir, ec);
    auto hC = connect_at(dir);
    ADSHANDLE hT = 0;
    make_table(hC, "keyno", &hT);
    const auto bag = (dir / "keyno.cdx").string();
    ADSHANDLE hN = 0;
    add_index(hT, bag.c_str(), "BYNAME", "NAME", nullptr, 0x0008, &hN);

    const char* names[] = {"E", "A", "D", "B", "C"};
    for (auto* n : names) put_rec(hT, n, "C", 1);
    // Churn: move A -> F (last), then append AA (second).
    REQUIRE(AdsGotoRecord(hT, 2) == 0);
    REQUIRE(AdsSetString(hT, (UNSIGNED8*)"NAME", (UNSIGNED8*)"F", 1) == 0);
    REQUIRE(AdsWriteRecord(hT) == 0);
    put_rec(hT, "AA", "C", 1);

    REQUIRE(AdsSetIndexOrderByHandle(hT, hN) == 0);
    // Order: AA B C D E F -> key numbers 1..6.
    const char* want[] = {"AA", "B", "C", "D", "E", "F"};
    REQUIRE(AdsGotoTop(hT) == 0);
    for (UNSIGNED32 i = 1; i <= 6; ++i) {
        UNSIGNED32 kn = 0;
        REQUIRE(AdsGetKeyNum(hT, 0, &kn) == 0);
        CHECK(kn == i);
        UNSIGNED8 buf[8] = {0};
        UNSIGNED32 len = sizeof(buf) - 1;
        REQUIRE(AdsGetString(hT, (UNSIGNED8*)"NAME", buf, &len, 0) == 0);
        CHECK(std::string(reinterpret_cast<char*>(buf), len).substr(0, 2)
              == std::string(want[i - 1]).substr(0, 2));
        double pos = -1;
        REQUIRE(AdsGetRelKeyPos(hT, &pos) == 0);
        // Engine convention: the record sits at the center of its slice.
        CHECK(pos == doctest::Approx((i - 0.5) / 6).epsilon(0.01));
        REQUIRE(AdsSkip(hT, 1) == 0);
    }
    CHECK(key_count(hN) == 6u);

    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hC) == 0);
    fs::remove_all(dir, ec);
}

// ---------------------------------------------------------------------------
TEST_CASE("intensive: .z01 custom extension end to end") {
    auto dir = fs::temp_directory_path() / "oads_int_z01";
    std::error_code ec;
    fs::remove_all(dir, ec);
    auto hC = connect_at(dir);
    ADSHANDLE hT = 0;
    make_table(hC, "vouch", &hT);
    const auto bag = (dir / "vouch.z01").string();   // custom extension
    ADSHANDLE hN = 0;
    add_index(hT, bag.c_str(), "BYNAME", "NAME", nullptr, 0x0008, &hN);

    const char* names[] = {"V1", "V3", "V2"};
    for (auto* n : names) put_rec(hT, n, "C", 1);
    CHECK(key_count(hN) == 3u);

    REQUIRE(AdsSetIndexOrderByHandle(hT, hN) == 0);
    auto w = walk_order(hT, "NAME");
    REQUIRE(w.size() == 3);
    CHECK(w[0].first == "V1");
    CHECK(w[2].first == "V3");

    put_rec(hT, "V4", "C", 1);
    CHECK(key_count(hN) == 4u);
    REQUIRE(AdsGotoBottom(hT) == 0);
    UNSIGNED8 buf[8] = {0};
    UNSIGNED32 len = sizeof(buf) - 1;
    REQUIRE(AdsGetString(hT, (UNSIGNED8*)"NAME", buf, &len, 0) == 0);
    CHECK(std::string(reinterpret_cast<char*>(buf), len).substr(0, 2) == "V4");

    // Reopen the bag by content (custom ext sniff) and re-walk.
    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsOpenTable(hC, (UNSIGNED8*)"vouch", (UNSIGNED8*)"vouch",
                         ADS_CDX, 1, 1, 0, 1, &hT) == 0);
    ADSHANDLE arr[8] = {0};
    UNSIGNED16 n = 8;
    std::vector<UNSIGNED8> bagv(bag.c_str(), bag.c_str() + bag.size() + 1);
    REQUIRE(AdsOpenIndex(hT, bagv.data(), arr, &n) == 0);
    REQUIRE(AdsSetIndexOrderByHandle(hT, arr[0]) == 0);
    w = walk_order(hT, "NAME");
    CHECK(w.size() == 4);

    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hC) == 0);
    fs::remove_all(dir, ec);
}

// ---------------------------------------------------------------------------
TEST_CASE("intensive: empty -> refill -> empty cycles keep the bag sane") {
    auto dir = fs::temp_directory_path() / "oads_int_cycles";
    std::error_code ec;
    fs::remove_all(dir, ec);
    auto hC = connect_at(dir);
    ADSHANDLE hT = 0;
    make_table(hC, "cycles", &hT);
    const auto bag = (dir / "cycles.cdx").string();
    ADSHANDLE hN = 0;
    add_index(hT, bag.c_str(), "BYNAME", "NAME", nullptr, 0x0008, &hN);

    for (int round = 0; round < 3; ++round) {
        char nm[16];
        for (int i = 0; i < 50; ++i) {
            std::snprintf(nm, sizeof(nm), "R%dN%02d", round, i);
            put_rec(hT, nm, "C", i);
        }
        CHECK(key_count(hN) == 50u);
        REQUIRE(AdsZapTable(hT) == 0);
        CHECK(rec_count(hT) == 0u);
        CHECK(key_count(hN) == 0u);
    }
    put_rec(hT, "Final", "C", 1);
    CHECK(key_count(hN) == 1u);
    REQUIRE(AdsGotoTop(hT) == 0);
    UNSIGNED16 bof = 1, eof = 1;
    REQUIRE(AdsAtBOF(hT, &bof) == 0);
    REQUIRE(AdsAtEOF(hT, &eof) == 0);
    CHECK(bof == 0);
    CHECK(eof == 0);

    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hC) == 0);
    fs::remove_all(dir, ec);
}

// ---------------------------------------------------------------------------
TEST_CASE("intensive: seek last and skip-unique over duplicate runs") {
    auto dir = fs::temp_directory_path() / "oads_int_seeklast";
    std::error_code ec;
    fs::remove_all(dir, ec);
    auto hC = connect_at(dir);
    ADSHANDLE hT = 0;
    make_table(hC, "seekl", &hT);
    const auto bag = (dir / "seekl.cdx").string();
    ADSHANDLE hN = 0;
    add_index(hT, bag.c_str(), "BYNAME", "NAME", nullptr, 0x0008, &hN);

    for (int i = 0; i < 5; ++i) put_rec(hT, "Dup", "C", i);
    put_rec(hT, "Solo", "C", 9);
    for (int i = 0; i < 3; ++i) put_rec(hT, "Zed", "C", i);

    REQUIRE(AdsSetIndexOrderByHandle(hT, hN) == 0);
    // SeekLast on the duplicated key lands on the LAST of the run.
    UNSIGNED16 found = 0;
    REQUIRE(AdsSeekLast(hN, (UNSIGNED8*)"Dup", 3, ADS_STRINGKEY, &found) == 0);
    CHECK(found == 1);
    UNSIGNED32 rn = 0;
    REQUIRE(AdsGetRecordNum(hT, 0, &rn) == 0);
    CHECK(rn == 5u);

    // SeekLast on the final key -> last record of the table.
    REQUIRE(AdsSeekLast(hN, (UNSIGNED8*)"Zed", 3, ADS_STRINGKEY, &found) == 0);
    CHECK(found == 1);
    REQUIRE(AdsGetRecordNum(hT, 0, &rn) == 0);
    CHECK(rn == 9u);

    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hC) == 0);
    fs::remove_all(dir, ec);
}

// ---------------------------------------------------------------------------
TEST_CASE("intensive: key edit from duplicate to duplicate keeps (key,recno)") {
    auto dir = fs::temp_directory_path() / "oads_int_dupedit";
    std::error_code ec;
    fs::remove_all(dir, ec);
    auto hC = connect_at(dir);
    ADSHANDLE hT = 0;
    make_table(hC, "dupedit", &hT);
    const auto bag = (dir / "dupedit.cdx").string();
    ADSHANDLE hN = 0;
    add_index(hT, bag.c_str(), "BYNAME", "NAME", nullptr, 0x0008, &hN);

    put_rec(hT, "A", "C", 1);   // 1
    put_rec(hT, "B", "C", 1);   // 2
    put_rec(hT, "B", "C", 1);   // 3
    put_rec(hT, "C", "C", 1);   // 4
    // Move rec1 A -> B: run becomes B(1), B(2), B(3) ordered by recno.
    REQUIRE(AdsGotoRecord(hT, 1) == 0);
    REQUIRE(AdsSetString(hT, (UNSIGNED8*)"NAME", (UNSIGNED8*)"B", 1) == 0);
    REQUIRE(AdsWriteRecord(hT) == 0);

    REQUIRE(AdsSetIndexOrderByHandle(hT, hN) == 0);
    auto w = walk_order(hT, "NAME");
    REQUIRE(w.size() == 4);
    CHECK(w[0].first == "B");
    CHECK(w[0].second == 1u);
    CHECK(w[1].first == "B");
    CHECK(w[1].second == 2u);
    CHECK(w[2].first == "B");
    CHECK(w[2].second == 3u);
    CHECK(w[3].first == "C");
    CHECK(ordered_unique(w));

    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hC) == 0);
    fs::remove_all(dir, ec);
}
