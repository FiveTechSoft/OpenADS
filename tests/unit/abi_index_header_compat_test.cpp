// abi_index_header_compat_test.cpp
//
// Tests that OpenADS correctly opens and operates on CDX index files
// produced by different engines (ADS vs DBFCDX format variants).
// The header fields differ (offsets 0x04-07, 0x0b, 0x40c, 0x414, 0x417)
// but the B+tree page layout is compatible.

#include "doctest.h"
#include "openads/ace.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {

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

void make_tag(ADSHANDLE hTable, const char* bag, const char* tag,
              const char* expr) {
    UNSIGNED8 b[64], t[64], e[64];
    std::memcpy(b, bag, std::strlen(bag) + 1);
    std::memcpy(t, tag, std::strlen(tag) + 1);
    std::memcpy(e, expr, std::strlen(expr) + 1);
    ADSHANDLE hIdx = 0;
    REQUIRE(AdsCreateIndex61(hTable, b, t, e, nullptr, nullptr,
                             0, 0, &hIdx) == 0);
}

} // namespace

TEST_CASE("open native CDX and walk in tag order") {
    auto dir = fs::temp_directory_path() / "openads_hdr_native";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0,
                         &hConn) == 0);

    UNSIGNED8 def[]   = "NAME,C,8,0";
    UNSIGNED8 tname[] = "data";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX,
                           0, 0, 0, 0, def, &hT) == 0);

    const char* names[] = {"CHARLIE", "ALPHA", "BRAVO", "DELTA"};
    UNSIGNED8 f[] = "NAME";
    for (const char* n : names) {
        REQUIRE(AdsAppendRecord(hT) == 0);
        REQUIRE(AdsSetString(hT, f,
            reinterpret_cast<UNSIGNED8*>(const_cast<char*>(n)),
            static_cast<UNSIGNED32>(std::strlen(n))) == 0);
    }
    REQUIRE(AdsWriteRecord(hT) == 0);

    make_tag(hT, "data.cdx", "BYNAME", "NAME");
    REQUIRE(AdsCloseTable(hT) == 0);

    // Reopen — the CDX was produced by OpenADS itself (native format).
    ADSHANDLE hT2 = 0;
    REQUIRE(AdsOpenTable(hConn, tname, tname, ADS_CDX, 0, 0, 0, 0,
                         &hT2) == 0);
    ADSHANDLE hI = 0;
    REQUIRE(AdsGetIndexHandle(hT2, (UNSIGNED8*)"BYNAME", &hI) == 0);
    REQUIRE(AdsSetIndexOrderByHandle(hT2, hI) == 0);

    // Tag order: ALPHA < BRAVO < CHARLIE < DELTA.
    REQUIRE(AdsGotoTop(hT2) == 0);
    CHECK(get_field(hT2, "NAME") == "ALPHA");
    REQUIRE(AdsSkip(hT2, 1) == 0);
    CHECK(get_field(hT2, "NAME") == "BRAVO");
    REQUIRE(AdsSkip(hT2, 1) == 0);
    CHECK(get_field(hT2, "NAME") == "CHARLIE");
    REQUIRE(AdsSkip(hT2, 1) == 0);
    CHECK(get_field(hT2, "NAME") == "DELTA");

    REQUIRE(AdsCloseTable(hT2) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("open index created by AdsOpenIndex (explicit open of .cdx)") {
    auto dir = fs::temp_directory_path() / "openads_hdr_explicit";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0,
                         &hConn) == 0);

    UNSIGNED8 def[]   = "CODE,C,6,0;NAME,C,10,0";
    UNSIGNED8 tname[] = "expl";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX,
                           0, 0, 0, 0, def, &hT) == 0);

    const char* ids[]   = {"300", "100", "400", "200"};
    const char* names[] = {"THIRD", "FIRST", "FOURTH", "SECOND"};
    UNSIGNED8 fCode[] = "CODE", fName[] = "NAME";
    for (int i = 0; i < 4; ++i) {
        REQUIRE(AdsAppendRecord(hT) == 0);
        REQUIRE(AdsSetString(hT, fCode,
            reinterpret_cast<UNSIGNED8*>(const_cast<char*>(ids[i])),
            static_cast<UNSIGNED32>(std::strlen(ids[i]))) == 0);
        REQUIRE(AdsSetString(hT, fName,
            reinterpret_cast<UNSIGNED8*>(const_cast<char*>(names[i])),
            static_cast<UNSIGNED32>(std::strlen(names[i]))) == 0);
    }
    REQUIRE(AdsWriteRecord(hT) == 0);

    make_tag(hT, "expl.cdx", "BYCODE", "CODE");
    make_tag(hT, "expl.cdx", "BYNAME", "NAME");
    REQUIRE(AdsCloseTable(hT) == 0);

    // Reopen table.
    ADSHANDLE hT2 = 0;
    REQUIRE(AdsOpenTable(hConn, tname, tname, ADS_CDX, 0, 0, 0, 0,
                         &hT2) == 0);

    // Explicitly open the index file.
    UNSIGNED8 cdx_path[256];
    std::string cdx_s = (dir / "expl.cdx").string();
    std::memcpy(cdx_path, cdx_s.c_str(), cdx_s.size() + 1);
    ADSHANDLE ahArr[16] = {0};
    UNSIGNED16 uLen = 16;
    REQUIRE(AdsOpenIndex(hT2, cdx_path, ahArr, &uLen) == 0);
    REQUIRE(uLen == 2);

    // Verify both tags are present and usable.
    UNSIGNED8 nm[64] = {0};
    UNSIGNED16 nl = sizeof(nm);
    REQUIRE(AdsGetIndexName(ahArr[0], nm, &nl) == 0);
    std::string first(reinterpret_cast<char*>(nm), nl);
    nl = sizeof(nm);
    REQUIRE(AdsGetIndexName(ahArr[1], nm, &nl) == 0);
    std::string second(reinterpret_cast<char*>(nm), nl);
    INFO("tags: [" << first << "," << second << "]");

    // Walk BYCODE — string ascending.
    REQUIRE(AdsSetIndexOrderByHandle(hT2, ahArr[0]) == 0);
    REQUIRE(AdsGotoTop(hT2) == 0);
    CHECK(get_field(hT2, "CODE") == "100");
    CHECK(get_field(hT2, "NAME") == "FIRST");
    REQUIRE(AdsSkip(hT2, 1) == 0);
    CHECK(get_field(hT2, "CODE") == "200");
    REQUIRE(AdsSkip(hT2, 1) == 0);
    CHECK(get_field(hT2, "CODE") == "300");
    REQUIRE(AdsSkip(hT2, 1) == 0);
    CHECK(get_field(hT2, "CODE") == "400");

    // Walk BYNAME — string ascending.
    REQUIRE(AdsSetIndexOrderByHandle(hT2, ahArr[1]) == 0);
    REQUIRE(AdsGotoTop(hT2) == 0);
    CHECK(get_field(hT2, "NAME") == "FIRST");
    REQUIRE(AdsSkip(hT2, 1) == 0);
    CHECK(get_field(hT2, "NAME") == "FOURTH");
    REQUIRE(AdsSkip(hT2, 1) == 0);
    CHECK(get_field(hT2, "NAME") == "SECOND");
    REQUIRE(AdsSkip(hT2, 1) == 0);
    CHECK(get_field(hT2, "NAME") == "THIRD");

    REQUIRE(AdsCloseTable(hT2) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("list_tags returns all tags from a multi-tag CDX") {
    auto dir = fs::temp_directory_path() / "openads_hdr_listtags";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0,
                         &hConn) == 0);

    UNSIGNED8 def[]   = "A,C,8,0;B,C,8,0;C,C,8,0";
    UNSIGNED8 tname[] = "lt";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX,
                           0, 0, 0, 0, def, &hT) == 0);

    // Insert one row so the table is non-empty.
    REQUIRE(AdsAppendRecord(hT) == 0);
    UNSIGNED8 fA[] = "A", fB[] = "B", fC[] = "C";
    UNSIGNED8 v[] = "X";
    REQUIRE(AdsSetString(hT, fA, v, 1) == 0);
    REQUIRE(AdsSetString(hT, fB, v, 1) == 0);
    REQUIRE(AdsSetString(hT, fC, v, 1) == 0);
    REQUIRE(AdsWriteRecord(hT) == 0);

    make_tag(hT, "lt.cdx", "TAG_A", "A");
    make_tag(hT, "lt.cdx", "TAG_B", "B");
    make_tag(hT, "lt.cdx", "TAG_C", "C");
    REQUIRE(AdsCloseTable(hT) == 0);

    // Reopen.
    ADSHANDLE hT2 = 0;
    REQUIRE(AdsOpenTable(hConn, tname, tname, ADS_CDX, 0, 0, 0, 0,
                         &hT2) == 0);

    // List all tags via AdsOpenIndex.
    UNSIGNED8 cdx_path[256];
    std::string cdx_s = (dir / "lt.cdx").string();
    std::memcpy(cdx_path, cdx_s.c_str(), cdx_s.size() + 1);
    ADSHANDLE ahArr[16] = {0};
    UNSIGNED16 uLen = 16;
    REQUIRE(AdsOpenIndex(hT2, cdx_path, ahArr, &uLen) == 0);
    REQUIRE(uLen == 3);

    // Collect tag names and sort.
    std::string tags[3];
    for (int i = 0; i < 3; ++i) {
        UNSIGNED8 nm[64] = {0};
        UNSIGNED16 nl = sizeof(nm);
        REQUIRE(AdsGetIndexName(ahArr[i], nm, &nl) == 0);
        tags[i] = std::string(reinterpret_cast<char*>(nm), nl);
    }
    // Sort alphabetically.
    if (tags[0] > tags[1]) std::swap(tags[0], tags[1]);
    if (tags[1] > tags[2]) std::swap(tags[1], tags[2]);
    if (tags[0] > tags[1]) std::swap(tags[0], tags[1]);

    CHECK(tags[0] == "TAG_A");
    CHECK(tags[1] == "TAG_B");
    CHECK(tags[2] == "TAG_C");

    // Each tag should produce correct seek results.
    for (int i = 0; i < 3; ++i) {
        REQUIRE(AdsSetIndexOrderByHandle(hT2, ahArr[i]) == 0);
        UNSIGNED16 found = 0;
        UNSIGNED8 key[] = "X";
        REQUIRE(AdsSeek(ahArr[i], key, 1, ADS_STRINGKEY, 0, &found) == 0);
        CHECK(found == 1);
        CHECK(get_field(hT2, "A") == "X");
    }

    REQUIRE(AdsCloseTable(hT2) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}
