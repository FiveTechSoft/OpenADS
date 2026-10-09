#include "doctest.h"
#include "openads/ace.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

void write_plain_dbf(const fs::path& path,
                     const std::vector<std::string>& tags) {
    std::vector<std::uint8_t> file;
    std::array<std::uint8_t, 32> hdr{};
    hdr[0]  = 0x03;
    hdr[4]  = static_cast<std::uint8_t>(tags.size());
    hdr[8]  = 32 + 32 + 1;
    hdr[10] = 1 + 5;
    file.insert(file.end(), hdr.begin(), hdr.end());
    std::array<std::uint8_t, 32> fd{};
    std::strncpy(reinterpret_cast<char*>(fd.data()), "TAG", 11);
    fd[11] = 'C'; fd[16] = 5;
    file.insert(file.end(), fd.begin(), fd.end());
    file.push_back(0x0D);
    for (auto& t : tags) {
        file.push_back(' ');
        for (std::size_t i = 0; i < 5; ++i)
            file.push_back(i < t.size()
                           ? static_cast<std::uint8_t>(t[i]) : ' ');
    }
    file.push_back(0x1A);
    std::ofstream(path, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
}

std::string tag_field(ADSHANDLE hCur) {
    UNSIGNED8  buf[16] = {0};
    UNSIGNED32 cap = sizeof(buf);
    REQUIRE(AdsGetField(hCur, (UNSIGNED8*)"TAG", buf, &cap, 0) == 0);
    std::string s((char*)buf, cap);
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

std::string read_first_tag(ADSHANDLE hCur) {
    REQUIRE(AdsGotoTop(hCur) == 0);
    return tag_field(hCur);
}

}  // namespace

TEST_CASE("M11.2 encrypt + reopen with correct password roundtrips") {
    auto dir = fs::temp_directory_path() / "openads_m11_2_ok";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    write_plain_dbf(dir / "data.dbf", {"AAAAA", "BBBBB", "CCCCC"});

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    UNSIGNED8 leaf[64] = "data.dbf";
    UNSIGNED8 pw[64]   = "swordfish";

    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    REQUIRE(AdsSetEncryptionPassword(hConn, pw) == 0);

    ADSHANDLE hTable = 0;
    REQUIRE(AdsOpenTable(hConn, leaf, nullptr, ADS_CDX,
                         0, 0, 0, 0, &hTable) == 0);
    REQUIRE(AdsEncryptTable(hTable) == 0);
    UNSIGNED32 physical_count = 0;
    REQUIRE(AdsGetRecordCount(hTable, ADS_IGNOREFILTERS, &physical_count) == 0);
    CHECK(physical_count == 3); // full encryption retains fixed-length records
    REQUIRE(AdsCloseTable(hTable) == 0);

    // Header byte on disk must now be 0xC3.
    {
        std::ifstream f(dir / "data.dbf", std::ios::binary);
        char ver = 0;
        f.read(&ver, 1);
        CHECK(static_cast<std::uint8_t>(ver) == 0xC4);
    }

    // Reopen + read → first tag is "AAAAA" again (decrypted on read).
    REQUIRE(AdsOpenTable(hConn, leaf, nullptr, ADS_CDX,
                         0, 0, 0, 0, &hTable) == 0);
    CHECK(read_first_tag(hTable) == "AAAAA");
    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);

    // New connection, same password → still readable.
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    REQUIRE(AdsSetEncryptionPassword(hConn, pw) == 0);
    REQUIRE(AdsOpenTable(hConn, leaf, nullptr, ADS_CDX,
                         0, 0, 0, 0, &hTable) == 0);
    CHECK(read_first_tag(hTable) == "AAAAA");
    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("M11.2 wrong password produces garbage (not the plaintext)") {
    auto dir = fs::temp_directory_path() / "openads_m11_2_wrong";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    write_plain_dbf(dir / "data.dbf", {"AAAAA"});

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    UNSIGNED8 leaf[64] = "data.dbf";
    UNSIGNED8 right_pw[64] = "right";
    UNSIGNED8 wrong_pw[64] = "wrong";

    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    REQUIRE(AdsSetEncryptionPassword(hConn, right_pw) == 0);
    ADSHANDLE hTable = 0;
    REQUIRE(AdsOpenTable(hConn, leaf, nullptr, ADS_CDX,
                         0, 0, 0, 0, &hTable) == 0);
    REQUIRE(AdsEncryptTable(hTable) == 0);
    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);

    // Reopen with wrong password — read should NOT return "AAAAA".
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    REQUIRE(AdsSetEncryptionPassword(hConn, wrong_pw) == 0);
    REQUIRE(AdsOpenTable(hConn, leaf, nullptr, ADS_CDX,
                         0, 0, 0, 0, &hTable) == 0);
    auto got = read_first_tag(hTable);
    CHECK(got != "AAAAA");
    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("M11.2 record-level encrypt: single record encrypted, others plain") {
    auto dir = fs::temp_directory_path() / "openads_m11_2_re";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    write_plain_dbf(dir / "data.dbf", {"AAAAA", "BBBBB", "CCCCC"});

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    UNSIGNED8 leaf[64] = "data.dbf";
    UNSIGNED8 pw[64]   = "swordfish";

    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    REQUIRE(AdsSetEncryptionPassword(hConn, pw) == 0);

    ADSHANDLE hTable = 0;
    REQUIRE(AdsOpenTable(hConn, leaf, nullptr, ADS_CDX,
                         0, 0, 0, 0, &hTable) == 0);

    UNSIGNED16 enc = 0;
    CHECK(AdsIsTableEncrypted(hTable, &enc) == 0);
    CHECK(enc == 0);

    REQUIRE(AdsGotoRecord(hTable, 2) == 0);
    REQUIRE(AdsEncryptRecord(hTable) == 0);
    UNSIGNED32 physical_count = 0;
    REQUIRE(AdsGetRecordCount(hTable, ADS_IGNOREFILTERS, &physical_count) == 0);
    CHECK(physical_count == 3); // trailing partial-encryption bitmap is not rows

    CHECK(AdsIsTableEncrypted(hTable, &enc) == 0);
    CHECK(enc == 1);
    UNSIGNED16 rec_enc = 0;
    CHECK(AdsIsRecordEncrypted(hTable, &rec_enc) == 0);
    CHECK(rec_enc == 1);

    REQUIRE(AdsGotoRecord(hTable, 1) == 0);
    rec_enc = 1;
    CHECK(AdsIsRecordEncrypted(hTable, &rec_enc) == 0);
    CHECK(rec_enc == 0);
    CHECK(tag_field(hTable) == "AAAAA");

    REQUIRE(AdsGotoRecord(hTable, 2) == 0);
    CHECK(tag_field(hTable) == "BBBBB");

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);

    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    REQUIRE(AdsSetEncryptionPassword(hConn, pw) == 0);
    REQUIRE(AdsOpenTable(hConn, leaf, nullptr, ADS_CDX,
                         0, 0, 0, 0, &hTable) == 0);
    REQUIRE(AdsGotoRecord(hTable, 2) == 0);
    CHECK(tag_field(hTable) == "BBBBB");
    REQUIRE(AdsGotoRecord(hTable, 1) == 0);
    CHECK(tag_field(hTable) == "AAAAA");

    {
        std::ifstream f(dir / "data.dbf", std::ios::binary);
        char ver = 0;
        f.read(&ver, 1);
        CHECK(static_cast<std::uint8_t>(ver) != 0xC3);
    }

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("Physical count: partial encryption bitmap does not become extra rows") {
    auto dir = fs::temp_directory_path() / "openads_physical_count_bitmap";
    std::error_code ec;
    fs::remove_all(dir, ec); fs::create_directories(dir);
    write_plain_dbf(dir / "data.dbf", std::vector<std::string>(80, "AAAAA"));
    auto path = dir.string();
    ADSHANDLE connection = 0, table = 0;
    REQUIRE(AdsConnect60(reinterpret_cast<UNSIGNED8*>(path.data()),
        ADS_LOCAL_SERVER, nullptr, nullptr, 0, &connection) == AE_SUCCESS);
    UNSIGNED8 password[] = "count-test", name[] = "data.dbf";
    REQUIRE(AdsSetEncryptionPassword(connection, password) == AE_SUCCESS);
    REQUIRE(AdsOpenTable(connection, name, nullptr, ADS_CDX,
        0, 0, 0, 0, &table) == AE_SUCCESS);
    REQUIRE(AdsGotoRecord(table, 80) == AE_SUCCESS);
    REQUIRE(AdsEncryptRecord(table) == AE_SUCCESS);
    // At 80 rows the bitmap is longer than one six-byte record. Treating
    // this format like an ordinary DBF would report phantom extra rows.
    UNSIGNED32 count = 0;
    REQUIRE(AdsGetRecordCount(table, ADS_IGNOREFILTERS, &count) == AE_SUCCESS);
    CHECK(count == 80);
    REQUIRE(AdsCloseTable(table) == AE_SUCCESS);
    REQUIRE(AdsOpenTable(connection, name, nullptr, ADS_CDX,
        0, 0, 0, 0, &table) == AE_SUCCESS);
    REQUIRE(AdsGetRecordCount(table, ADS_IGNOREFILTERS, &count) == AE_SUCCESS);
    CHECK(count == 80);
    REQUIRE(AdsCloseTable(table) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(connection) == AE_SUCCESS);
    fs::remove_all(dir, ec);
}
