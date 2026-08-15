// abi_seek_after_order_change_test.cpp
//
// Comprehensive tests for Seek correctness after controlling index order
// changes. Covers: collation preservation through parked↔active swap,
// ADS_OEM + PL852 + order switch, scope loss on swap, DESCEND + order
// switch, multi-page B-tree, SET DELETED ON, rapid 3-tag alternation,
// binary (ANSI) collation explicit verification.

#include "doctest.h"
#include "openads/ace.h"
#include "engine/oem_collation.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

void set_str(ADSHANDLE h, const char* field, const char* val) {
    UNSIGNED8 f[16];
    std::memcpy(f, field, std::strlen(field) + 1);
    UNSIGNED8 v[64];
    std::memcpy(v, val, std::strlen(val) + 1);
    AdsSetString(h, f, v, static_cast<UNSIGNED32>(std::strlen(val)));
}

void make_tag(ADSHANDLE hTable, const char* tag, const char* expr) {
    UNSIGNED8 fn[16];
    std::memcpy(fn, "data", 5);
    UNSIGNED8 t[64];
    std::memcpy(t, tag, std::strlen(tag) + 1);
    UNSIGNED8 e[64];
    std::memcpy(e, expr, std::strlen(expr) + 1);
    ADSHANDLE hIdx = 0;
    REQUIRE(AdsCreateIndex61(hTable, fn, t, e, nullptr, nullptr,
                             0, 512, &hIdx) == 0);
}

void make_tag_desc(ADSHANDLE hTable, const char* tag, const char* expr) {
    UNSIGNED8 fn[16];
    std::memcpy(fn, "data", 5);
    UNSIGNED8 t[64];
    std::memcpy(t, tag, std::strlen(tag) + 1);
    UNSIGNED8 e[64];
    std::memcpy(e, expr, std::strlen(expr) + 1);
    ADSHANDLE hIdx = 0;
    REQUIRE(AdsCreateIndex61(hTable, fn, t, e, nullptr, nullptr,
                             0x0A, 512, &hIdx) == 0);
}

void make_tag_cond(ADSHANDLE hTable, const char* tag, const char* expr,
                   const char* cond) {
    UNSIGNED8 fn[16];
    std::memcpy(fn, "data", 5);
    UNSIGNED8 t[64];
    std::memcpy(t, tag, std::strlen(tag) + 1);
    UNSIGNED8 e[64];
    std::memcpy(e, expr, std::strlen(expr) + 1);
    UNSIGNED8 c[64];
    std::memcpy(c, cond, std::strlen(cond) + 1);
    ADSHANDLE hIdx = 0;
    REQUIRE(AdsCreateIndex61(hTable, fn, t, e, c, nullptr, 0, 512, &hIdx) == 0);
}

std::string get_field(ADSHANDLE hT, const char* field) {
    UNSIGNED8 buf[64];
    UNSIGNED32 len = sizeof(buf);
    UNSIGNED8 f[16];
    std::memcpy(f, field, std::strlen(field) + 1);
    AdsGetString(hT, f, buf, &len, 0);
    std::string s(reinterpret_cast<char*>(buf), len);
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

bool seek_hit(ADSHANDLE hI, const char* key) {
    UNSIGNED16 found = 0;
    AdsSeek(hI, reinterpret_cast<UNSIGNED8*>(const_cast<char*>(key)),
            static_cast<UNSIGNED16>(std::strlen(key)),
            ADS_STRINGKEY, /*hard*/ 0, &found);
    return found == 1;
}

bool seek_hit_soft(ADSHANDLE hI, const char* key) {
    UNSIGNED16 found = 0;
    AdsSeek(hI, reinterpret_cast<UNSIGNED8*>(const_cast<char*>(key)),
            static_cast<UNSIGNED16>(std::strlen(key)),
            ADS_STRINGKEY, /*soft*/ 1, &found);
    return found == 1;
}

void set_order(ADSHANDLE h, const char* tag) {
    UNSIGNED8 t[64];
    std::memcpy(t, tag, std::strlen(tag) + 1);
    REQUIRE(AdsSetIndexOrder(h, t) == 0);
}

} // namespace

// ---- Test 1: Seek on tag A, switch to tag B, seek on tag B ----

TEST_CASE("seek after order change: char tag A then char tag B") {
    auto dir = fs::temp_directory_path() / "openads_seek_ordchg_char";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn) == 0);

    UNSIGNED8 def[]   = "COD,C,4,0;NAME,C,8,0";
    UNSIGNED8 tname[] = "data";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX,
                           0, 0, 0, 0, def, &hT) == 0);

    struct { const char* cod; const char* name; } rows[] = {
        {"D001", "DELTA"},   {"A002", "ALPHA"},  {"C003", "CHARLIE"},
        {"B004", "BRAVO"},   {"E005", "ECHO"},
    };
    for (auto& r : rows) {
        REQUIRE(AdsAppendRecord(hT) == 0);
        set_str(hT, "COD", r.cod);
        set_str(hT, "NAME", r.name);
    }
    REQUIRE(AdsWriteRecord(hT) == 0);

    make_tag(hT, "BYCOD",  "COD");
    make_tag(hT, "BYNAME", "NAME");

    // Reopen so production CDX auto-attaches all tags.
    REQUIRE(AdsCloseTable(hT) == 0);
    hT = 0;
    UNSIGNED8 onm[] = "data";
    REQUIRE(AdsOpenTable(hConn, onm, onm, ADS_CDX, 1, 1, 0, 1, &hT) == 0);

    ADSHANDLE hByCod = 0, hByName = 0;
    REQUIRE(AdsGetIndexHandle(hT, (UNSIGNED8*)"BYCOD", &hByCod) == 0);
    REQUIRE(AdsGetIndexHandle(hT, (UNSIGNED8*)"BYNAME", &hByName) == 0);

    // Seek on BYNAME tag.
    REQUIRE(AdsSetIndexOrderByHandle(hT, hByName) == 0);
    CHECK(seek_hit(hByName, "ECHO"));
    CHECK(get_field(hT, "NAME") == "ECHO");

    // Switch to BYCOD and seek.
    REQUIRE(AdsSetIndexOrderByHandle(hT, hByCod) == 0);
    CHECK(seek_hit(hByCod, "B004"));
    CHECK(get_field(hT, "NAME") == "BRAVO");

    // Switch back to BYNAME and seek again.
    REQUIRE(AdsSetIndexOrderByHandle(hT, hByName) == 0);
    CHECK(seek_hit(hByName, "ALPHA"));
    CHECK(get_field(hT, "NAME") == "ALPHA");

    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

// ---- Test 2: Seek → SetOrder → Seek — collation preserved through swap ----

TEST_CASE("seek after order change: collation preserved through swap") {
    auto dir = fs::temp_directory_path() / "openads_seek_ordchg_coll";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn) == 0);
    UNSIGNED8 coll[] = "NTXPL852";
    REQUIRE(AdsSetCollation(hConn, coll) == 0);

    UNSIGNED8 def[]   = "NAME,C,8,0;CODE,C,4,0";
    UNSIGNED8 tname[] = "data";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX,
                           ADS_OEM, 0, 0, 0, def, &hT) == 0);

    // PL852 order: LAC < ŁAB (0x9D) < MAD < ZBY.
    struct { const char* name; const char* code; } rows[] = {
        {"LACAAAAA", "L001"}, { "\x9D""ABBBBB", "X002" },
        {"MADCCCCC", "M003"}, {"ZBYDDDDD", "Z004"},
    };
    for (auto& r : rows) {
        REQUIRE(AdsAppendRecord(hT) == 0);
        set_str(hT, "NAME", r.name);
        set_str(hT, "CODE", r.code);
    }
    REQUIRE(AdsWriteRecord(hT) == 0);

    make_tag(hT, "BYNAME", "NAME");
    make_tag(hT, "BYCODE", "CODE");

    // Reopen.
    REQUIRE(AdsCloseTable(hT) == 0);
    hT = 0;
    UNSIGNED8 onm[] = "data";
    REQUIRE(AdsOpenTable(hConn, onm, onm, ADS_CDX, ADS_OEM, 0, 0, 0, &hT) == 0);

    ADSHANDLE hByName = 0, hByCode = 0;
    REQUIRE(AdsGetIndexHandle(hT, (UNSIGNED8*)"BYNAME", &hByName) == 0);
    REQUIRE(AdsGetIndexHandle(hT, (UNSIGNED8*)"BYCODE", &hByCode) == 0);

    // Seek on BYNAME — PL852 order.
    REQUIRE(AdsSetIndexOrderByHandle(hT, hByName) == 0);
    CHECK(seek_hit_soft(hByName, "M"));
    CHECK(get_field(hT, "NAME") == "MADCCCCC");

    // Switch to BYCODE — seek on CODE field.
    REQUIRE(AdsSetIndexOrderByHandle(hT, hByCode) == 0);
    CHECK(seek_hit(hByCode, "Z004"));
    CHECK(get_field(hT, "NAME") == "ZBYDDDDD");

    // Switch back to BYNAME — PL852 Ł must still sort between L and M.
    REQUIRE(AdsSetIndexOrderByHandle(hT, hByName) == 0);
    // Walk: LAC, ŁAB, MAD, ZBY.
    REQUIRE(AdsGotoTop(hT) == 0);
    CHECK(get_field(hT, "NAME") == "LACAAAAA");
    REQUIRE(AdsSkip(hT, 1) == 0);
    CHECK(static_cast<unsigned char>(get_field(hT, "NAME")[0]) == 0x9D);
    REQUIRE(AdsSkip(hT, 1) == 0);
    CHECK(get_field(hT, "NAME") == "MADCCCCC");

    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

// ---- Test 3: ADS_OEM PL852 — switch between two tags, seek each ----

TEST_CASE("seek after order change: ADS_OEM PL852 dual-tag switch") {
    auto dir = fs::temp_directory_path() / "openads_seek_ordchg_oem";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    auto _collation_guard = openads::engine::set_default_oem_collation("NTXPL852");
    (void)_collation_guard;

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn) == 0);

    UNSIGNED8 def[]   = "NAME,C,8,0;CAT,C,3,0";
    UNSIGNED8 tname[] = "data";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX,
                           ADS_OEM, 0, 0, 0, def, &hT) == 0);

    struct { const char* name; const char* cat; } rows[] = {
        {"AAAA", "Z01"}, {"\x88""BBBB", "M02"},  // ł(0x88) under PL852
        {"CCCC", "A03"}, {"ZZZZ", "K04"},
    };
    for (auto& r : rows) {
        REQUIRE(AdsAppendRecord(hT) == 0);
        set_str(hT, "NAME", r.name);
        set_str(hT, "CAT", r.cat);
    }
    REQUIRE(AdsWriteRecord(hT) == 0);

    make_tag(hT, "BYNAME", "NAME");
    make_tag(hT, "BYCAT",  "CAT");

    // Reopen with ADS_OEM.
    REQUIRE(AdsCloseTable(hT) == 0);
    hT = 0;
    UNSIGNED8 onm[] = "data";
    REQUIRE(AdsOpenTable(hConn, onm, onm, ADS_CDX, ADS_OEM, 0, 0, 0, &hT) == 0);

    ADSHANDLE hByName = 0, hByCat = 0;
    REQUIRE(AdsGetIndexHandle(hT, (UNSIGNED8*)"BYNAME", &hByName) == 0);
    REQUIRE(AdsGetIndexHandle(hT, (UNSIGNED8*)"BYCAT", &hByCat) == 0);

    // BYNAME: PL852 walk order must be AAAA, CCCC, łBBB, ZZZZ
    // (ł sorts between L and M, not between A and C).
    REQUIRE(AdsSetIndexOrderByHandle(hT, hByName) == 0);
    REQUIRE(AdsGotoTop(hT) == 0);
    CHECK(get_field(hT, "NAME") == "AAAA");
    REQUIRE(AdsSkip(hT, 1) == 0);
    CHECK(get_field(hT, "NAME") == "CCCC");
    REQUIRE(AdsSkip(hT, 1) == 0);
    CHECK(static_cast<unsigned char>(get_field(hT, "NAME")[0]) == 0x88);
    REQUIRE(AdsSkip(hT, 1) == 0);
    CHECK(get_field(hT, "NAME") == "ZZZZ");

    // Switch to BYCAT and seek.
    REQUIRE(AdsSetIndexOrderByHandle(hT, hByCat) == 0);
    CHECK(seek_hit(hByCat, "M02"));
    CHECK(get_field(hT, "NAME") == "\x88""BBBB");

    // Back to BYNAME — verify walk is still correct.
    REQUIRE(AdsSetIndexOrderByHandle(hT, hByName) == 0);
    CHECK(seek_hit(hByName, "CCCC"));
    CHECK(get_field(hT, "NAME") == "CCCC");

    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    openads::engine::set_default_oem_collation("");
    fs::remove_all(dir, ec);
}

// ---- Test 4: SetScope + order change + seek ----

TEST_CASE("seek after order change: scope lost on swap") {
    auto dir = fs::temp_directory_path() / "openads_seek_ordchg_scope";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn) == 0);

    UNSIGNED8 def[]   = "COD,C,4,0;NAME,C,8,0";
    UNSIGNED8 tname[] = "data";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX,
                           0, 0, 0, 0, def, &hT) == 0);

    for (int i = 0; i < 10; ++i) {
        REQUIRE(AdsAppendRecord(hT) == 0);
        char cod[5];
        std::snprintf(cod, sizeof(cod), "C%03d", i);
        set_str(hT, "COD", cod);
        char nm[9];
        std::snprintf(nm, sizeof(nm), "N%07d", i);
        set_str(hT, "NAME", nm);
    }
    REQUIRE(AdsWriteRecord(hT) == 0);

    make_tag(hT, "BYCOD",  "COD");
    make_tag(hT, "BYNAME", "NAME");

    REQUIRE(AdsCloseTable(hT) == 0);
    hT = 0;
    UNSIGNED8 onm[] = "data";
    REQUIRE(AdsOpenTable(hConn, onm, onm, ADS_CDX, 1, 1, 0, 1, &hT) == 0);

    ADSHANDLE hByCod = 0, hByName = 0;
    REQUIRE(AdsGetIndexHandle(hT, (UNSIGNED8*)"BYCOD", &hByCod) == 0);
    REQUIRE(AdsGetIndexHandle(hT, (UNSIGNED8*)"BYNAME", &hByName) == 0);

    // Set scope on BYCOD.
    REQUIRE(AdsSetIndexOrderByHandle(hT, hByCod) == 0);
    UNSIGNED8 lo[] = "C003", hi[] = "C006";
    REQUIRE(AdsSetScope(hByCod, ADS_TOP, lo, 4, ADS_STRINGKEY) == 0);
    REQUIRE(AdsSetScope(hByCod, ADS_BOTTOM, hi, 4, ADS_STRINGKEY) == 0);

    UNSIGNED32 cnt = 0;
    REQUIRE(AdsGetKeyCount(hByCod, ADS_RESPECTSCOPES, &cnt) == 0);
    CHECK(cnt == 4);  // C003, C004, C005, C006

    // Switch to BYNAME — scope should be cleared (Order is recreated).
    REQUIRE(AdsSetIndexOrderByHandle(hT, hByName) == 0);
    REQUIRE(AdsGotoTop(hT) == 0);
    UNSIGNED32 rn = 0;
    REQUIRE(AdsGetRecordNum(hT, 0, &rn) == 0);
    // First record by NAME is C009 (N0000009 sorts last for N-prefix but
    // actually N0000000 < N0000001 ... so first by name is N0000000).
    CHECK(get_field(hT, "NAME") == "N0000000");

    // BYCOD scope is gone — full key count.
    REQUIRE(AdsGetKeyCount(hByName, ADS_RESPECTSCOPES, &cnt) == 0);
    CHECK(cnt == 10);

    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

// ---- Test 5: DESCEND order + switch + seek ----

TEST_CASE("seek after order change: DESCEND + ASCEND switch") {
    auto dir = fs::temp_directory_path() / "openads_seek_ordchg_desc";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn) == 0);

    UNSIGNED8 def[]   = "COD,C,4,0;NAME,C,8,0";
    UNSIGNED8 tname[] = "data";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX,
                           0, 0, 0, 0, def, &hT) == 0);

    for (int i = 0; i < 5; ++i) {
        REQUIRE(AdsAppendRecord(hT) == 0);
        char cod[5];
        std::snprintf(cod, sizeof(cod), "C%03d", i);
        set_str(hT, "COD", cod);
        char nm[9];
        std::snprintf(nm, sizeof(nm), "N%07d", i);
        set_str(hT, "NAME", nm);
    }
    REQUIRE(AdsWriteRecord(hT) == 0);

    make_tag(hT, "BYCOD",  "COD");
    make_tag_desc(hT, "BYNAME_D", "NAME");  // DESCEND

    REQUIRE(AdsCloseTable(hT) == 0);
    hT = 0;
    UNSIGNED8 onm[] = "data";
    REQUIRE(AdsOpenTable(hConn, onm, onm, ADS_CDX, 1, 1, 0, 1, &hT) == 0);

    ADSHANDLE hByCod = 0, hByNameD = 0;
    REQUIRE(AdsGetIndexHandle(hT, (UNSIGNED8*)"BYCOD", &hByCod) == 0);
    REQUIRE(AdsGetIndexHandle(hT, (UNSIGNED8*)"BYNAME_D", &hByNameD) == 0);

    // DESCEND: walk should go N0000004, N0000003, ..., N0000000.
    REQUIRE(AdsSetIndexOrderByHandle(hT, hByNameD) == 0);
    REQUIRE(AdsGotoTop(hT) == 0);
    CHECK(get_field(hT, "NAME") == "N0000004");

    // Switch to ASCEND BYCOD.
    REQUIRE(AdsSetIndexOrderByHandle(hT, hByCod) == 0);
    REQUIRE(AdsGotoTop(hT) == 0);
    CHECK(get_field(hT, "COD") == "C000");

    // Back to DESCEND — must still walk backward.
    REQUIRE(AdsSetIndexOrderByHandle(hT, hByNameD) == 0);
    REQUIRE(AdsGotoTop(hT) == 0);
    CHECK(get_field(hT, "NAME") == "N0000004");
    REQUIRE(AdsSkip(hT, 1) == 0);
    CHECK(get_field(hT, "NAME") == "N0000003");

    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

// ---- Test 6: Multi-page index: switch order + seek ----

TEST_CASE("seek after order change: multi-page B-tree") {
    auto dir = fs::temp_directory_path() / "openads_seek_ordchg_multipage";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn) == 0);

    UNSIGNED8 def[]   = "COD,C,8,0;NAME,C,8,0";
    UNSIGNED8 tname[] = "data";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX,
                           0, 0, 0, 0, def, &hT) == 0);

    const int N = 500;
    for (int i = 0; i < N; ++i) {
        REQUIRE(AdsAppendRecord(hT) == 0);
        char cod[9], nm[9];
        std::snprintf(cod, sizeof(cod), "C%07d", (i * 53) % N);
        std::snprintf(nm, sizeof(nm), "N%07d", (i * 37) % N);
        set_str(hT, "COD", cod);
        set_str(hT, "NAME", nm);
    }
    REQUIRE(AdsWriteRecord(hT) == 0);

    make_tag(hT, "BYCOD",  "COD");
    make_tag(hT, "BYNAME", "NAME");

    REQUIRE(AdsCloseTable(hT) == 0);
    hT = 0;
    UNSIGNED8 onm[] = "data";
    REQUIRE(AdsOpenTable(hConn, onm, onm, ADS_CDX, 1, 1, 0, 1, &hT) == 0);

    ADSHANDLE hByCod = 0, hByName = 0;
    REQUIRE(AdsGetIndexHandle(hT, (UNSIGNED8*)"BYCOD", &hByCod) == 0);
    REQUIRE(AdsGetIndexHandle(hT, (UNSIGNED8*)"BYNAME", &hByName) == 0);

    // Walk full BYNAME order to collect expected.
    REQUIRE(AdsSetIndexOrderByHandle(hT, hByName) == 0);
    std::vector<std::string> name_order;
    REQUIRE(AdsGotoTop(hT) == 0);
    for (int i = 0; i < N; ++i) {
        name_order.push_back(get_field(hT, "NAME"));
        if (AdsSkip(hT, 1) != 0) break;
    }

    // Switch to BYCOD, seek, walk some.
    REQUIRE(AdsSetIndexOrderByHandle(hT, hByCod) == 0);
    REQUIRE(AdsGotoTop(hT) == 0);
    std::vector<std::string> cod_order;
    for (int i = 0; i < N; ++i) {
        cod_order.push_back(get_field(hT, "COD"));
        if (AdsSkip(hT, 1) != 0) break;
    }

    // Back to BYNAME — walk must match the original.
    REQUIRE(AdsSetIndexOrderByHandle(hT, hByName) == 0);
    REQUIRE(AdsGotoTop(hT) == 0);
    for (std::size_t i = 0; i < name_order.size(); ++i) {
        CHECK(get_field(hT, "NAME") == name_order[i]);
        if (AdsSkip(hT, 1) != 0) break;
    }

    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

// ---- Test 7: SET DELETED ON + order change + skip ----

TEST_CASE("seek after order change: SET DELETED ON + order switch") {
    auto dir = fs::temp_directory_path() / "openads_seek_ordchg_del";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn) == 0);

    UNSIGNED8 def[]   = "COD,C,4,0;NAME,C,8,0";
    UNSIGNED8 tname[] = "data";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX,
                           0, 0, 0, 0, def, &hT) == 0);

    struct { const char* cod; const char* name; } rows[] = {
        {"D001", "DELTA"},   {"A002", "ALPHA"},  {"C003", "CHARLIE"},
        {"B004", "BRAVO"},   {"E005", "ECHO"},
    };
    for (auto& r : rows) {
        REQUIRE(AdsAppendRecord(hT) == 0);
        set_str(hT, "COD", r.cod);
        set_str(hT, "NAME", r.name);
    }
    REQUIRE(AdsWriteRecord(hT) == 0);

    make_tag(hT, "BYCOD",  "COD");
    make_tag(hT, "BYNAME", "NAME");

    // Delete record 3 (CHARLIE).
    REQUIRE(AdsGotoRecord(hT, 3) == 0);
    REQUIRE(AdsDeleteRecord(hT) == 0);

    UNSIGNED16 del = 0;
    REQUIRE(AdsShowDeleted(del) == 0);

    // Reopen.
    REQUIRE(AdsCloseTable(hT) == 0);
    hT = 0;
    UNSIGNED8 onm[] = "data";
    REQUIRE(AdsOpenTable(hConn, onm, onm, ADS_CDX, 1, 1, 0, 1, &hT) == 0);

    ADSHANDLE hByCod = 0, hByName = 0;
    REQUIRE(AdsGetIndexHandle(hT, (UNSIGNED8*)"BYCOD", &hByCod) == 0);
    REQUIRE(AdsGetIndexHandle(hT, (UNSIGNED8*)"BYNAME", &hByName) == 0);

    // BYNAME walk: should skip CHARLIE.
    REQUIRE(AdsSetIndexOrderByHandle(hT, hByName) == 0);
    REQUIRE(AdsGotoTop(hT) == 0);
    std::vector<std::string> names;
    for (int i = 0; i < 10; ++i) {
        UNSIGNED16 eof = 0;
        AdsAtEOF(hT, &eof);
        if (eof) break;
        names.push_back(get_field(hT, "NAME"));
        AdsSkip(hT, 1);
    }
    REQUIRE(names.size() == 4u);
    CHECK(names[0] == "ALPHA");
    CHECK(names[1] == "BRAVO");
    CHECK(names[2] == "DELTA");
    CHECK(names[3] == "ECHO");

    // Switch to BYCOD — CHARLIE should also be hidden.
    REQUIRE(AdsSetIndexOrderByHandle(hT, hByCod) == 0);
    REQUIRE(AdsGotoTop(hT) == 0);
    std::vector<std::string> codes;
    for (int i = 0; i < 10; ++i) {
        UNSIGNED16 eof = 0;
        AdsAtEOF(hT, &eof);
        if (eof) break;
        codes.push_back(get_field(hT, "COD"));
        AdsSkip(hT, 1);
    }
    REQUIRE(codes.size() == 4u);
    CHECK(codes[0] == "A002");
    CHECK(codes[1] == "B004");
    CHECK(codes[2] == "D001");
    CHECK(codes[3] == "E005");

    UNSIGNED16 del_off = 1;
    REQUIRE(AdsShowDeleted(del_off) == 0);
    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

// ---- Test 8: Seek after AdsOpenIndex (re-open scenario) ----

TEST_CASE("seek after order change: production CDX re-open") {
    auto dir = fs::temp_directory_path() / "openads_seek_ordchg_reopen";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn) == 0);

    UNSIGNED8 def[]   = "COD,C,4,0;NAME,C,8,0";
    UNSIGNED8 tname[] = "data";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX,
                           0, 0, 0, 0, def, &hT) == 0);

    struct { const char* cod; const char* name; } rows[] = {
        {"D001", "DELTA"}, {"A002", "ALPHA"}, {"B003", "BRAVO"},
    };
    for (auto& r : rows) {
        REQUIRE(AdsAppendRecord(hT) == 0);
        set_str(hT, "COD", r.cod);
        set_str(hT, "NAME", r.name);
    }
    REQUIRE(AdsWriteRecord(hT) == 0);

    make_tag(hT, "BYCOD",  "COD");
    make_tag(hT, "BYNAME", "NAME");

    // Close and reopen via AdsOpenTable (production CDX auto-open).
    REQUIRE(AdsCloseTable(hT) == 0);
    hT = 0;
    UNSIGNED8 onm[] = "data";
    REQUIRE(AdsOpenTable(hConn, onm, onm, ADS_CDX, 1, 1, 0, 1, &hT) == 0);

    // Also open the index bag explicitly (like rddads path).
    ADSHANDLE ah[8] = {0};
    UNSIGNED16 ac = 8;
    UNSIGNED8 bag[] = "data.cdx";
    REQUIRE(AdsOpenIndex(hT, bag, ah, &ac) == 0);
    REQUIRE(ac >= 2);

    // Use the first handle (BYCOD) for seek.
    REQUIRE(AdsSetIndexOrderByHandle(hT, ah[0]) == 0);
    CHECK(seek_hit(ah[0], "A002"));
    CHECK(get_field(hT, "NAME") == "ALPHA");

    // Switch to second handle (BYNAME).
    REQUIRE(AdsSetIndexOrderByHandle(hT, ah[1]) == 0);
    CHECK(seek_hit(ah[1], "DELTA"));
    CHECK(get_field(hT, "COD") == "D001");

    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

// ---- Test 9: Rapid alternation between 3+ tags ----

TEST_CASE("seek after order change: rapid 3-tag alternation") {
    auto dir = fs::temp_directory_path() / "openads_seek_ordchg_rapid";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn) == 0);

    UNSIGNED8 def[]   = "COD,C,4,0;NAME,C,8,0;CAT,C,3,0";
    UNSIGNED8 tname[] = "data";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX,
                           0, 0, 0, 0, def, &hT) == 0);

    for (int i = 0; i < 20; ++i) {
        REQUIRE(AdsAppendRecord(hT) == 0);
        char cod[5], nm[9], cat[4];
        std::snprintf(cod, sizeof(cod), "C%03d", i);
        std::snprintf(nm, sizeof(nm), "N%07d", (i * 7) % 20);
        std::snprintf(cat, sizeof(cat), "T%d", i % 3);
        set_str(hT, "COD", cod);
        set_str(hT, "NAME", nm);
        set_str(hT, "CAT", cat);
    }
    REQUIRE(AdsWriteRecord(hT) == 0);

    make_tag(hT, "BYCOD",  "COD");
    make_tag(hT, "BYNAME", "NAME");
    make_tag(hT, "BYCAT",  "CAT");

    REQUIRE(AdsCloseTable(hT) == 0);
    hT = 0;
    UNSIGNED8 onm[] = "data";
    REQUIRE(AdsOpenTable(hConn, onm, onm, ADS_CDX, 1, 1, 0, 1, &hT) == 0);

    ADSHANDLE hByCod = 0, hByName = 0, hByCat = 0;
    REQUIRE(AdsGetIndexHandle(hT, (UNSIGNED8*)"BYCOD", &hByCod) == 0);
    REQUIRE(AdsGetIndexHandle(hT, (UNSIGNED8*)"BYNAME", &hByName) == 0);
    REQUIRE(AdsGetIndexHandle(hT, (UNSIGNED8*)"BYCAT", &hByCat) == 0);

    // Rapid alternation: seek on each tag in sequence, multiple rounds.
    for (int round = 0; round < 3; ++round) {
        REQUIRE(AdsSetIndexOrderByHandle(hT, hByCod) == 0);
        CHECK(seek_hit(hByCod, "C010"));

        REQUIRE(AdsSetIndexOrderByHandle(hT, hByName) == 0);
        CHECK(seek_hit(hByName, "N0000014"));

        REQUIRE(AdsSetIndexOrderByHandle(hT, hByCat) == 0);
        CHECK(seek_hit_soft(hByCat, "T1"));

        // Also verify walk order on BYCAT.
        REQUIRE(AdsGotoTop(hT) == 0);
        std::string first_cat = get_field(hT, "CAT");
        REQUIRE(AdsSkip(hT, 1) == 0);
        std::string second_cat = get_field(hT, "CAT");
        CHECK(first_cat <= second_cat);
    }

    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

// ---- Test 10: Binary (ANSI) collation explicit test ----

TEST_CASE("seek after order change: binary ANSI collation correct") {
    auto dir = fs::temp_directory_path() / "openads_seek_ordchg_ansi";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn) == 0);
    // Explicitly BINARY collation.
    UNSIGNED8 coll[] = "BINARY";
    REQUIRE(AdsSetCollation(hConn, coll) == 0);

    UNSIGNED8 def[]   = "COD,C,4,0;NAME,C,8,0";
    UNSIGNED8 tname[] = "data";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX,
                           0, 0, 0, 0, def, &hT) == 0);

    // lowercase 'a' = 0x61, uppercase 'A' = 0x41. Binary: uppercase < lowercase.
    struct { const char* cod; const char* name; } rows[] = {
        {"bb", "alice"}, {"aa", "ALICE"}, {"cc", "Bob"},
    };
    for (auto& r : rows) {
        REQUIRE(AdsAppendRecord(hT) == 0);
        set_str(hT, "COD", r.cod);
        set_str(hT, "NAME", r.name);
    }
    REQUIRE(AdsWriteRecord(hT) == 0);

    make_tag(hT, "BYCOD",  "COD");
    make_tag(hT, "BYNAME", "NAME");

    REQUIRE(AdsCloseTable(hT) == 0);
    hT = 0;
    UNSIGNED8 onm[] = "data";
    REQUIRE(AdsOpenTable(hConn, onm, onm, ADS_CDX, 1, 1, 0, 1, &hT) == 0);

    ADSHANDLE hByCod = 0, hByName = 0;
    REQUIRE(AdsGetIndexHandle(hT, (UNSIGNED8*)"BYCOD", &hByCod) == 0);
    REQUIRE(AdsGetIndexHandle(hT, (UNSIGNED8*)"BYNAME", &hByName) == 0);

    // BYNAME binary order: "ALICE" (A=0x41) < "Bob" (B=0x42) < "alice" (a=0x61).
    REQUIRE(AdsSetIndexOrderByHandle(hT, hByName) == 0);
    REQUIRE(AdsGotoTop(hT) == 0);
    CHECK(get_field(hT, "NAME") == "ALICE");
    REQUIRE(AdsSkip(hT, 1) == 0);
    CHECK(get_field(hT, "NAME") == "Bob");
    REQUIRE(AdsSkip(hT, 1) == 0);
    CHECK(get_field(hT, "NAME") == "alice");

    // Seek on BYNAME with exact binary match.
    CHECK(seek_hit(hByName, "ALICE"));
    CHECK(seek_hit(hByName, "alice"));

    // Switch to BYCOD.
    REQUIRE(AdsSetIndexOrderByHandle(hT, hByCod) == 0);
    CHECK(seek_hit(hByCod, "bb"));
    CHECK(get_field(hT, "NAME") == "alice");

    // Back to BYNAME.
    REQUIRE(AdsSetIndexOrderByHandle(hT, hByName) == 0);
    CHECK(seek_hit(hByName, "Bob"));

    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}
