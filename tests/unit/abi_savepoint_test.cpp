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

fs::path make_dbf(const fs::path& dir, const char* leaf) {
    fs::create_directories(dir);
    auto p = dir / leaf;
    fs::remove(p);
    std::vector<std::uint8_t> file;
    std::array<std::uint8_t, 32> hdr{};
    hdr[0]  = 0x03;
    hdr[4]  = 1;
    hdr[8]  = 32 + 32 + 1; hdr[9]  = 0;
    hdr[10] = 1 + 5;       hdr[11] = 0;
    file.insert(file.end(), hdr.begin(), hdr.end());
    std::array<std::uint8_t, 32> fd{};
    std::strncpy(reinterpret_cast<char*>(fd.data()), "TAG", 11);
    fd[11] = 'C'; fd[16] = 5;
    file.insert(file.end(), fd.begin(), fd.end());
    file.push_back(0x0D);
    file.push_back(' ');
    file.push_back('A'); file.push_back('A'); file.push_back('A');
    file.push_back('A'); file.push_back('A');
    file.push_back(0x1A);
    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
    return p;
}

} // namespace

TEST_CASE("AdsCreateSavepoint + AdsRollbackTransaction80 partial rollback") {
    const auto dir = fs::temp_directory_path() / "openads_m53_sp";
    std::error_code ec;
    fs::remove_all(dir, ec);
    make_dbf(dir, "data.dbf");

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    UNSIGNED8 leaf[64] = "data.dbf";

    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    ADSHANDLE hTable = 0;
    REQUIRE(AdsOpenTable(hConn, leaf, nullptr, ADS_CDX,
                         0, 0, 0, 0, &hTable) == 0);

    REQUIRE(AdsBeginTransaction(hConn) == 0);
    // FLock required for writes in shared mode (server always opens Shared).
    REQUIRE(AdsLockTable(hTable) == 0);
    REQUIRE(AdsGotoTop(hTable) == 0);

    UNSIGNED8 fld[16] = "TAG";
    UNSIGNED8 v1[8]   = "BBBBB";
    REQUIRE(AdsSetString(hTable, fld, v1, 5) == 0);

    UNSIGNED8 sp_name[16] = "sp1";
    REQUIRE(AdsCreateSavepoint(hConn, sp_name, ADS_DEFAULT) == 0);

    UNSIGNED8 v2[8] = "CCCCC";
    REQUIRE(AdsSetString(hTable, fld, v2, 5) == 0);

    // Read-back inside tx after sp1 + second update sees CCCCC.
    UNSIGNED8 buf[16] = {0};
    UNSIGNED32 cap = sizeof(buf);
    REQUIRE(AdsGetField(hTable, fld, buf, &cap, 0) == 0);
    CHECK(std::string(reinterpret_cast<const char*>(buf), cap) == "CCCCC");

    // Partial rollback to sp1 — second update reverts; first update stays.
    REQUIRE(AdsRollbackTransaction80(hConn, sp_name, ADS_DEFAULT) == 0);

    // Refresh the Table's cached record buffer after the disk rewrite.
    REQUIRE(AdsGotoTop(hTable) == 0);
    cap = sizeof(buf); std::memset(buf, 0, sizeof(buf));
    REQUIRE(AdsGetField(hTable, fld, buf, &cap, 0) == 0);
    CHECK(std::string(reinterpret_cast<const char*>(buf), cap) == "BBBBB");

    // Commit — BBBBB persists.
    REQUIRE(AdsCommitTransaction(hConn) == 0);

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);

    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    REQUIRE(AdsOpenTable(hConn, leaf, nullptr, ADS_CDX,
                         0, 0, 0, 0, &hTable) == 0);
    REQUIRE(AdsGotoTop(hTable) == 0);
    cap = sizeof(buf); std::memset(buf, 0, sizeof(buf));
    REQUIRE(AdsGetField(hTable, fld, buf, &cap, 0) == 0);
    CHECK(std::string(reinterpret_cast<const char*>(buf), cap) == "BBBBB");
    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);

    fs::remove_all(dir, ec);
}

TEST_CASE("AdsRollbackTransaction80 with null savepoint = full rollback") {
    const auto dir = fs::temp_directory_path() / "openads_m53_sp_null";
    std::error_code ec;
    fs::remove_all(dir, ec);
    make_dbf(dir, "data.dbf");
    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    UNSIGNED8 leaf[64] = "data.dbf";

    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    ADSHANDLE hTable = 0;
    REQUIRE(AdsOpenTable(hConn, leaf, nullptr, ADS_CDX,
                         0, 0, 0, 0, &hTable) == 0);
    REQUIRE(AdsBeginTransaction(hConn) == 0);
    REQUIRE(AdsLockTable(hTable) == 0);
    REQUIRE(AdsGotoTop(hTable) == 0);
    UNSIGNED8 fld[16] = "TAG";
    UNSIGNED8 v1[8]   = "ZZZZZ";
    REQUIRE(AdsSetString(hTable, fld, v1, 5) == 0);

    REQUIRE(AdsRollbackTransaction80(hConn, nullptr, ADS_DEFAULT) == 0);

    UNSIGNED8 buf[16] = {0};
    UNSIGNED32 cap = sizeof(buf);
    REQUIRE(AdsGotoTop(hTable) == 0);
    REQUIRE(AdsGetField(hTable, fld, buf, &cap, 0) == 0);
    CHECK(std::string(reinterpret_cast<const char*>(buf), cap) == "AAAAA");

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("M11.3 AdsReleaseSavepoint drops marker, keeps work") {
    const auto dir = fs::temp_directory_path() / "openads_m11_3_release";
    std::error_code ec;
    fs::remove_all(dir, ec);
    make_dbf(dir, "data.dbf");

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    UNSIGNED8 leaf[64] = "data.dbf";

    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    ADSHANDLE hTable = 0;
    REQUIRE(AdsOpenTable(hConn, leaf, nullptr, ADS_CDX,
                         0, 0, 0, 0, &hTable) == 0);

    REQUIRE(AdsBeginTransaction(hConn) == 0);
    REQUIRE(AdsLockTable(hTable) == 0);
    REQUIRE(AdsGotoTop(hTable) == 0);
    UNSIGNED8 fld[16] = "TAG";
    UNSIGNED8 v1[8]   = "BBBBB";
    REQUIRE(AdsSetString(hTable, fld, v1, 5) == 0);

    UNSIGNED8 sp_name[16] = "sp1";
    REQUIRE(AdsCreateSavepoint(hConn, sp_name, ADS_DEFAULT) == 0);
    UNSIGNED8 v2[8] = "CCCCC";
    REQUIRE(AdsSetString(hTable, fld, v2, 5) == 0);

    // Release sp1 — marker disappears, second update remains.
    REQUIRE(AdsReleaseSavepoint(hConn, sp_name) == 0);

    // Rolling back to sp1 now must fail (marker gone).
    CHECK(AdsRollbackTransaction80(hConn, sp_name, ADS_DEFAULT) != 0);

    REQUIRE(AdsCommitTransaction(hConn) == 0);
    REQUIRE(AdsGotoTop(hTable) == 0);
    UNSIGNED8 buf[16] = {0};
    UNSIGNED32 cap = sizeof(buf);
    REQUIRE(AdsGetField(hTable, fld, buf, &cap, 0) == 0);
    CHECK(std::string(reinterpret_cast<const char*>(buf), cap) == "CCCCC");

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("M11.3 nested BEGIN / COMMIT only flushes outermost") {
    const auto dir = fs::temp_directory_path() / "openads_m11_3_nest";
    std::error_code ec;
    fs::remove_all(dir, ec);
    make_dbf(dir, "data.dbf");

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    UNSIGNED8 leaf[64] = "data.dbf";

    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    ADSHANDLE hTable = 0;
    REQUIRE(AdsOpenTable(hConn, leaf, nullptr, ADS_CDX,
                         0, 0, 0, 0, &hTable) == 0);

    REQUIRE(AdsBeginTransaction(hConn) == 0);            // depth = 1
    REQUIRE(AdsBeginTransaction(hConn) == 0);            // depth = 2
    UNSIGNED16 inTx = 0;
    REQUIRE(AdsInTransaction(hConn, &inTx) == 0);
    CHECK(inTx == 1);

    REQUIRE(AdsLockTable(hTable) == 0);
    REQUIRE(AdsGotoTop(hTable) == 0);
    UNSIGNED8 fld[16] = "TAG";
    UNSIGNED8 v1[8]   = "BBBBB";
    REQUIRE(AdsSetString(hTable, fld, v1, 5) == 0);

    REQUIRE(AdsCommitTransaction(hConn) == 0);           // depth = 1
    REQUIRE(AdsInTransaction(hConn, &inTx) == 0);
    CHECK(inTx == 1);                                     // still active

    REQUIRE(AdsCommitTransaction(hConn) == 0);           // depth = 0 → flush
    REQUIRE(AdsInTransaction(hConn, &inTx) == 0);
    CHECK(inTx == 0);

    REQUIRE(AdsGotoTop(hTable) == 0);
    UNSIGNED8 buf[16] = {0};
    UNSIGNED32 cap = sizeof(buf);
    REQUIRE(AdsGetField(hTable, fld, buf, &cap, 0) == 0);
    CHECK(std::string(reinterpret_cast<const char*>(buf), cap) == "BBBBB");

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("M11.3 nested BEGIN + ROLLBACK kills entire transaction") {
    const auto dir = fs::temp_directory_path() / "openads_m11_3_nest_rb";
    std::error_code ec;
    fs::remove_all(dir, ec);
    make_dbf(dir, "data.dbf");

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    UNSIGNED8 leaf[64] = "data.dbf";

    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    ADSHANDLE hTable = 0;
    REQUIRE(AdsOpenTable(hConn, leaf, nullptr, ADS_CDX,
                         0, 0, 0, 0, &hTable) == 0);

    REQUIRE(AdsBeginTransaction(hConn) == 0);            // depth = 1
    REQUIRE(AdsBeginTransaction(hConn) == 0);            // depth = 2
    REQUIRE(AdsLockTable(hTable) == 0);
    REQUIRE(AdsGotoTop(hTable) == 0);
    UNSIGNED8 fld[16] = "TAG";
    UNSIGNED8 v1[8]   = "BBBBB";
    REQUIRE(AdsSetString(hTable, fld, v1, 5) == 0);

    // Inner ROLLBACK aborts entire transaction at any depth.
    REQUIRE(AdsRollbackTransaction(hConn) == 0);
    UNSIGNED16 inTx = 1;
    REQUIRE(AdsInTransaction(hConn, &inTx) == 0);
    CHECK(inTx == 0);

    REQUIRE(AdsGotoTop(hTable) == 0);
    UNSIGNED8 buf[16] = {0};
    UNSIGNED32 cap = sizeof(buf);
    REQUIRE(AdsGetField(hTable, fld, buf, &cap, 0) == 0);
    CHECK(std::string(reinterpret_cast<const char*>(buf), cap) == "AAAAA");

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}
