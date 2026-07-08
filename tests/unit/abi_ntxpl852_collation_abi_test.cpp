#include "doctest.h"
#include "fixtures/polish_oem_fixture.h"
#include "openads/ace.h"

#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {

void connect_with_collation(const fs::path& dir, const char* coll_name,
                            ADSHANDLE* phConn, ADSHANDLE* phTable) {
    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, phConn) == 0);
    if (coll_name != nullptr) {
        UNSIGNED8 coll[32];
        std::memcpy(coll, coll_name, std::strlen(coll_name) + 1);
        REQUIRE(AdsSetCollation(*phConn, coll) == 0);
    }
    UNSIGNED8 def[]   = "NAME,C,8,0";
    UNSIGNED8 tname[] = "names";
    REQUIRE(AdsCreateTable(*phConn, tname, nullptr, ADS_CDX,
                           0, 0, 0, 0, def, phTable) == 0);
}

void seed_polish_rows(ADSHANDLE hT) {
    const char* rows[] = {
        "LACAAAAA",
        openads::test::kPolishLabRow8,
        "MADCCCCC",
        "ZBYDDDDD",
    };
    UNSIGNED8 fName[] = "NAME";
    for (const char* row : rows) {
        REQUIRE(AdsAppendRecord(hT) == 0);
        REQUIRE(AdsSetString(hT, fName,
            reinterpret_cast<UNSIGNED8*>(const_cast<char*>(row)),
            static_cast<UNSIGNED32>(std::strlen(row))) == 0);
        REQUIRE(AdsWriteRecord(hT) == 0);
    }
}

ADSHANDLE create_name_index(ADSHANDLE hT) {
    UNSIGNED8 bag[]  = "names.cdx";
    UNSIGNED8 tag[]  = "BYNAME";
    UNSIGNED8 expr[] = "NAME";
    ADSHANDLE hI = 0;
    REQUIRE(AdsCreateIndex61(hT, bag, tag, expr,
                             nullptr, nullptr, 0, 0, &hI) == 0);
    return hI;
}

std::string soft_seek_first_char(ADSHANDLE hI, ADSHANDLE hT, char ch) {
    std::string key(1, ch);
    UNSIGNED16 found = 0;
    REQUIRE(AdsSeek(hI, reinterpret_cast<UNSIGNED8*>(key.data()),
                    static_cast<UNSIGNED16>(key.size()),
                    ADS_STRING, /*soft=*/1, &found) == 0);
    CHECK(found == 1);  // single-char prefix matches first byte exactly
    UNSIGNED8 got[32];
    UNSIGNED32 got_len = sizeof(got);
    UNSIGNED8 fName[] = "NAME";
    REQUIRE(AdsGetString(hT, fName, got, &got_len, 0) == 0);
    std::string name(reinterpret_cast<char*>(got), got_len);
    while (!name.empty() && name.back() == ' ') name.pop_back();
    return name;
}

} // namespace

TEST_CASE("PL852 alias enables the same soft seek as NTXPL852") {
    auto dir = fs::temp_directory_path() / "openads_pl852_alias";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    ADSHANDLE hConn = 0, hT = 0;
    connect_with_collation(dir, "PL852", &hConn, &hT);
    seed_polish_rows(hT);
    ADSHANDLE hI = create_name_index(hT);

    CHECK(soft_seek_first_char(hI, hT, 'M').substr(0, 3) == "MAD");
    CHECK(soft_seek_first_char(hI, hT, 'Z').substr(0, 3) == "ZBY");

    AdsCloseTable(hT);
    AdsDisconnect(hConn);
    fs::remove_all(dir, ec);
}

TEST_CASE("NTXPL852 soft seek Z skips past L-stroke region") {
    auto dir = fs::temp_directory_path() / "openads_ntxpl852_zseek";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    ADSHANDLE hConn = 0, hT = 0;
    connect_with_collation(dir, "NTXPL852", &hConn, &hT);
    seed_polish_rows(hT);
    ADSHANDLE hI = create_name_index(hT);

    CHECK(soft_seek_first_char(hI, hT, 'Z').substr(0, 3) == "ZBY");

    AdsCloseTable(hT);
    AdsDisconnect(hConn);
    fs::remove_all(dir, ec);
}

TEST_CASE("AdsReindex preserves NTXPL852 index walk order") {
    auto dir = fs::temp_directory_path() / "openads_ntxpl852_reindex";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    ADSHANDLE hConn = 0, hT = 0;
    connect_with_collation(dir, "NTXPL852", &hConn, &hT);
    seed_polish_rows(hT);
    ADSHANDLE hI = create_name_index(hT);

    REQUIRE(AdsGotoRecord(hT, 3) == 0);
    UNSIGNED8 fName[] = "NAME";
    REQUIRE(AdsSetString(hT, fName,
        reinterpret_cast<UNSIGNED8*>(const_cast<char*>("MZZZZZZZ")),
        8u) == 0);
    REQUIRE(AdsWriteRecord(hT) == 0);
    REQUIRE(AdsReindex(hT) == 0);

    CHECK(soft_seek_first_char(hI, hT, 'M').substr(0, 3) == "MZZ");

    AdsCloseTable(hT);
    AdsDisconnect(hConn);
    fs::remove_all(dir, ec);
}

TEST_CASE("AdsSetCollation rejects unknown OEM collation names") {
    auto dir = fs::temp_directory_path() / "openads_coll_reject";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    UNSIGNED8 bad[] = "NTXPL1250";
    CHECK(AdsSetCollation(hConn, bad) != 0);

    AdsDisconnect(hConn);
    fs::remove_all(dir, ec);
}

TEST_CASE("binary collation index walk places L-stroke after Z") {
    auto dir = fs::temp_directory_path() / "openads_binary_lstroke";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    ADSHANDLE hConn = 0, hT = 0;
    connect_with_collation(dir, "BINARY", &hConn, &hT);
    seed_polish_rows(hT);
    create_name_index(hT);

    // Without OEM collation, 0x9D (157) sorts after Z (90): L, M, Z, Ł.
    UNSIGNED8 tag[] = "BYNAME";
    REQUIRE(AdsSetIndexOrder(hT, tag) == 0);
    REQUIRE(AdsGotoTop(hT) == 0);

    const unsigned char expect_first[] = { 'L', 'M', 'Z', 0x9D };
    UNSIGNED8 fName[] = "NAME";
    for (unsigned char ef : expect_first) {
        UNSIGNED8 got[32];
        UNSIGNED32 got_len = sizeof(got);
        REQUIRE(AdsGetString(hT, fName, got, &got_len, 0) == 0);
        CHECK(static_cast<unsigned char>(got[0]) == ef);
        if (ef != 0x9D) REQUIRE(AdsSkip(hT, 1) == 0);
    }

    AdsCloseTable(hT);
    AdsDisconnect(hConn);
    fs::remove_all(dir, ec);
}

TEST_CASE("NTXPL852 index walk via tag name matches collation order") {
    auto dir = fs::temp_directory_path() / "openads_ntxpl852_tagwalk";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    ADSHANDLE hConn = 0, hT = 0;
    connect_with_collation(dir, "NTXPL852", &hConn, &hT);
    seed_polish_rows(hT);
    create_name_index(hT);

    UNSIGNED8 tag[] = "BYNAME";
    REQUIRE(AdsSetIndexOrder(hT, tag) == 0);
    REQUIRE(AdsGotoTop(hT) == 0);

    const unsigned char expect_first[] = { 'L', 0x9D, 'M', 'Z' };
    UNSIGNED8 fName[] = "NAME";
    for (unsigned char ef : expect_first) {
        UNSIGNED8 got[32];
        UNSIGNED32 got_len = sizeof(got);
        REQUIRE(AdsGetString(hT, fName, got, &got_len, 0) == 0);
        CHECK(static_cast<unsigned char>(got[0]) == ef);
        if (ef != 'Z') REQUIRE(AdsSkip(hT, 1) == 0);
    }

    AdsCloseTable(hT);
    AdsDisconnect(hConn);
    fs::remove_all(dir, ec);
}