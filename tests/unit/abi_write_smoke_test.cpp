#include "doctest.h"
#include "openads/ace.h"
#include "openads/error.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

fs::path make_empty_dbf(const fs::path& dir, const char* leaf) {
    fs::create_directories(dir);
    auto p = dir / leaf;
    fs::remove(p);
    std::vector<std::uint8_t> file;
    std::array<std::uint8_t, 32> hdr{};
    hdr[0]  = 0x03;
    hdr[4]  = 0;
    hdr[8]  = 32 + 32 + 1; hdr[9] = 0;
    hdr[10] = 1 + 4;       hdr[11] = 0;
    file.insert(file.end(), hdr.begin(), hdr.end());
    std::array<std::uint8_t, 32> fd{};
    std::strncpy(reinterpret_cast<char*>(fd.data()), "TAG", 11);
    fd[11] = 'C'; fd[16] = 4;
    file.insert(file.end(), fd.begin(), fd.end());
    file.push_back(0x0D);
    file.push_back(0x1A);
    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
    return p;
}

} // namespace

TEST_CASE("ABI write smoke: append two rows, lock/unlock, mark deleted, read back") {
    const auto dir = fs::temp_directory_path() / "openads_m2_abi_w";
    std::error_code ec;
    fs::remove_all(dir, ec);
    make_empty_dbf(dir, "data.dbf");

    ADSHANDLE hConn = 0;
    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);

    ADSHANDLE hTable = 0;
    UNSIGNED8 leaf[64] = "data.dbf";
    REQUIRE(AdsOpenTable(hConn, leaf, nullptr, ADS_CDX,
                         0, 0, 0, 0, &hTable) == 0);

    REQUIRE(AdsAppendRecord(hTable) == 0);
    UNSIGNED8 fld[64] = "TAG";
    UNSIGNED8 val1[8] = "ABC";
    REQUIRE(AdsSetString(hTable, fld, val1, 3) == 0);

    REQUIRE(AdsAppendRecord(hTable) == 0);
    UNSIGNED8 val2[8] = "WXYZ";
    REQUIRE(AdsSetString(hTable, fld, val2, 4) == 0);

    REQUIRE(AdsLockRecord(hTable, 1) == 0);
    REQUIRE(AdsUnlockRecord(hTable, 1) == 0);

    // Delete + recall require an RLock in shared mode (GoHot write guard).
    REQUIRE(AdsLockRecord(hTable, 1) == 0);
    REQUIRE(AdsDeleteRecord(hTable) == 0);
    UNSIGNED16 deleted = 0;
    REQUIRE(AdsIsRecordDeleted(hTable, &deleted) == 0);
    CHECK(deleted == 1);
    REQUIRE(AdsRecallRecord(hTable) == 0);
    REQUIRE(AdsIsRecordDeleted(hTable, &deleted) == 0);
    CHECK(deleted == 0);
    REQUIRE(AdsUnlockRecord(hTable, 1) == 0);

    REQUIRE(AdsFlushFileBuffers(hTable) == 0);
    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);

    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    REQUIRE(AdsOpenTable(hConn, leaf, nullptr, ADS_CDX,
                         0, 0, 0, 0, &hTable) == 0);

    UNSIGNED32 count = 0;
    REQUIRE(AdsGetRecordCount(hTable, 0, &count) == 0);
    CHECK(count == 2);

    REQUIRE(AdsGotoTop(hTable) == 0);
    UNSIGNED8 buf[16] = {0};
    UNSIGNED32 cap = sizeof(buf);
    REQUIRE(AdsGetField(hTable, fld, buf, &cap, 0) == 0);
    // TAG is C(4); "ABC" stored as "ABC " on disk — AdsGetField must
    // return the full 4-char space-padded value to match declared width.
    CHECK(std::string(reinterpret_cast<const char*>(buf), cap) == "ABC ");

    REQUIRE(AdsSkip(hTable, 1) == 0);
    cap = sizeof(buf);
    std::memset(buf, 0, sizeof(buf));
    REQUIRE(AdsGetField(hTable, fld, buf, &cap, 0) == 0);
    CHECK(std::string(reinterpret_cast<const char*>(buf), cap) == "WXYZ");

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);

    fs::remove_all(dir, ec);
}
