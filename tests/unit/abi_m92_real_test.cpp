#include "doctest.h"
#include "openads/ace.h"
#include "openads/error.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

namespace {

fs::path stage_dbf(const fs::path& dir, const char* leaf) {
    fs::create_directories(dir);
    auto p = dir / leaf;
    fs::remove(p);
    std::vector<std::uint8_t> file;
    std::array<std::uint8_t, 32> hdr{};
    hdr[0]  = 0x03;
    hdr[4]  = 3;                      // 3 records
    hdr[8]  = 32 + 32 + 1; hdr[9]  = 0;
    hdr[10] = 1 + 4;       hdr[11] = 0;
    file.insert(file.end(), hdr.begin(), hdr.end());
    std::array<std::uint8_t, 32> fd{};
    std::strncpy(reinterpret_cast<char*>(fd.data()), "TAG", 11);
    fd[11] = 'C'; fd[16] = 4;
    file.insert(file.end(), fd.begin(), fd.end());
    file.push_back(0x0D);
    auto push_rec = [&](const char* k){
        file.push_back(' ');
        for (int i = 0; i < 4; ++i)
            file.push_back(i < (int)std::strlen(k)
                           ? static_cast<std::uint8_t>(k[i]) : ' ');
    };
    push_rec("AAAA");
    push_rec("BBBB");
    push_rec("CCCC");
    file.push_back(0x1A);
    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
    return p;
}

} // namespace

TEST_CASE("M9.4 AdsGotoRecord positions on a valid recno") {
    const auto dir = fs::temp_directory_path() / "openads_m94_goto";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_dbf(dir, "data.dbf");

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);

    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);

    ADSHANDLE hTable = 0;
    UNSIGNED8 name[16] = "data";
    REQUIRE(AdsOpenTable(hConn, name, name, ADS_CDX, 1, 1, 0, 1, &hTable) == 0);

    REQUIRE(AdsGotoRecord(hTable, 2) == 0);
    UNSIGNED32 recno = 0;
    REQUIRE(AdsGetRecordNum(hTable, 0, &recno) == 0);
    CHECK(recno == 2);

    REQUIRE(AdsGotoRecord(hTable, 3) == 0);
    REQUIRE(AdsGetRecordNum(hTable, 0, &recno) == 0);
    CHECK(recno == 3);

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("M9.4 AdsGetRecordLength + AdsGetTableType + AdsGetTableFilename") {
    const auto dir = fs::temp_directory_path() / "openads_m94_meta";
    std::error_code ec;
    fs::remove_all(dir, ec);
    auto dbf = stage_dbf(dir, "data.dbf");

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);

    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);

    ADSHANDLE hTable = 0;
    UNSIGNED8 name[16] = "data";
    REQUIRE(AdsOpenTable(hConn, name, name, ADS_CDX, 1, 1, 0, 1, &hTable) == 0);

    UNSIGNED32 rec_len = 0;
    REQUIRE(AdsGetRecordLength(hTable, &rec_len) == 0);
    CHECK(rec_len == 5u);   // 1 byte delete + 4 byte TAG

    UNSIGNED16 ttype = 0;
    REQUIRE(AdsGetTableType(hTable, &ttype) == 0);
    CHECK(ttype == ADS_CDX);

    UNSIGNED8 fname_buf[260] = {0};
    UNSIGNED16 fname_cap = sizeof(fname_buf);
    REQUIRE(AdsGetTableFilename(hTable, 0, fname_buf, &fname_cap) == 0);
    std::string got(reinterpret_cast<const char*>(fname_buf), fname_cap);
    // Path resolution may have normalised separators; just check the
    // leaf survives.
    CHECK(got.find("data.dbf") != std::string::npos);

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("M9.4 AdsCheckExistence + AdsDeleteFile") {
    // The server filesystem API (b06cf6a0) sandboxes AdsCheckExistence /
    // AdsDeleteFile under the connection's data directory: client paths
    // are jailed relative to the root, so this exercises them with a
    // connection rooted at the temp dir and a relative file name (the
    // absolute-path-via-hConn=0 form the old test used is intentionally
    // no longer honored — it would escape the jail).
    const auto dir = fs::temp_directory_path() / "openads_m94_files";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    std::ofstream(dir / "a.dbf") << "x";

    std::string root = dir.string();
    std::vector<UNSIGNED8> rbuf(root.begin(), root.end());
    rbuf.push_back(0);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(rbuf.data(), ADS_LOCAL_SERVER, nullptr, nullptr, 0,
                         &hConn) == 0);

    UNSIGNED8 name_buf[64] = "a.dbf";
    UNSIGNED16 exists = 0;
    REQUIRE(AdsCheckExistence(hConn, name_buf, &exists) == 0);
    CHECK(exists == 1);

    REQUIRE(AdsDeleteFile(hConn, name_buf) == 0);

    REQUIRE(AdsCheckExistence(hConn, name_buf, &exists) == 0);
    CHECK(exists == 0);

    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}
