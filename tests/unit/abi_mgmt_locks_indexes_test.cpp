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

// Minimal 1-record DBF (same staging as abi_lock_retry_test.cpp).
fs::path stage_dbf(const fs::path& dir, const char* name) {
    std::error_code ec;
    fs::create_directories(dir);
    auto p = dir / name;
    fs::remove(p, ec);
    std::vector<std::uint8_t> file;
    std::array<std::uint8_t, 32> hdr{};
    hdr[0]  = 0x03;
    hdr[4]  = 1;
    hdr[8]  = 32 + 32 + 1; hdr[10] = 1 + 5;
    file.insert(file.end(), hdr.begin(), hdr.end());
    std::array<std::uint8_t, 32> fd{};
    std::strncpy(reinterpret_cast<char*>(fd.data()), "TAG", 11);
    fd[11] = 'C'; fd[16] = 5;
    file.insert(file.end(), fd.begin(), fd.end());
    file.push_back(0x0D);
    file.push_back(' ');
    for (int i = 0; i < 5; ++i) file.push_back('A');
    file.push_back(0x1A);
    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
    return p;
}

ADSHANDLE mg_local() {
    UNSIGNED8 srv[8] = "local";
    UNSIGNED8 usr[2] = "u";
    UNSIGNED8 pwd[2] = "p";
    ADSHANDLE h = 0;
    REQUIRE(AdsMgConnect(srv, usr, pwd, &h) == 0);
    return h;
}

UNSIGNED16 count_locks(ADSHANDLE hMg, const char* table) {
    std::array<ADS_MGMT_LOCK_INFO, 32> buf{};
    UNSIGNED16 cnt = static_cast<UNSIGNED16>(buf.size());
    UNSIGNED16 sz  = sizeof(ADS_MGMT_LOCK_INFO);
    std::vector<UNSIGNED8> tb;
    UNSIGNED8* tp = nullptr;
    if (table != nullptr) {
        tb.assign(table, table + std::strlen(table));
        tb.push_back(0);
        tp = tb.data();
    }
    REQUIRE(AdsMgGetLocks(hMg, tp, nullptr, 0, buf.data(), &cnt, &sz) == 0);
    return cnt;
}

}  // namespace

TEST_CASE("mgmt locks carry their owning table: AdsMgGetLocks filters by it") {
    const auto dir = fs::temp_directory_path() / "openads_mg_locks_tbl";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_dbf(dir, "locka.dbf");

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    UNSIGNED8 leaf[16] = "locka";
    ADSHANDLE hTable = 0;
    REQUIRE(AdsOpenTable(hConn, leaf, leaf, ADS_CDX,
                         1, 1, 0, 1, &hTable) == 0);

    ADSHANDLE hMg = mg_local();
    const UNSIGNED16 base_all   = count_locks(hMg, nullptr);
    const UNSIGNED16 base_locka = count_locks(hMg, "locka.dbf");

    REQUIRE(AdsLockRecord(hTable, 1) == 0);

    // Basename filter sees the new lock; an unrelated table doesn't.
    CHECK(count_locks(hMg, "locka.dbf") == base_locka + 1);
    CHECK(count_locks(hMg, nullptr)     == base_all + 1);
    CHECK(count_locks(hMg, "no_such_table.dbf") == 0);

    // Owner lookup honors the same filter and reports the record.
    ADS_MGMT_LOCK_INFO owner{};
    UNSIGNED16 osz = sizeof(owner);
    UNSIGNED16 lt  = 0;
    UNSIGNED8  tname[16] = "locka.dbf";
    REQUIRE(AdsMgGetLockOwner(hMg, tname, 1, &owner, &osz, &lt) == 0);
    CHECK(owner.ulRecordNumber == 1);
    UNSIGNED8 other[24] = "no_such_table.dbf";
    ADS_MGMT_LOCK_INFO none{};
    UNSIGNED16 nsz = sizeof(none);
    REQUIRE(AdsMgGetLockOwner(hMg, other, 1, &none, &nsz, &lt) == 0);
    CHECK(none.ulRecordNumber == 0);

    REQUIRE(AdsUnlockRecord(hTable, 1) == 0);
    CHECK(count_locks(hMg, "locka.dbf") == base_locka);

    REQUIRE(AdsMgDisconnect(hMg) == 0);
    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
}

TEST_CASE("mgmt open indexes: AdsMgGetOpenIndexes enumerates + filters") {
    const auto dir = fs::temp_directory_path() / "openads_mg_open_idx";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_dbf(dir, "idxa.dbf");

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    UNSIGNED8 leaf[16] = "idxa";
    ADSHANDLE hTable = 0;
    REQUIRE(AdsOpenTable(hConn, leaf, leaf, ADS_CDX,
                         1, 1, 0, 1, &hTable) == 0);

    // Build a tag so the table has an open index to report.
    fs::path bag = dir / "idxa.cdx";
    std::vector<UNSIGNED8> bag_buf(bag.string().size() + 1, 0);
    std::memcpy(bag_buf.data(), bag.string().c_str(), bag.string().size());
    UNSIGNED8 tag[8]  = "bytag";
    UNSIGNED8 expr[8] = "TAG";
    ADSHANDLE hIdx = 0;
    REQUIRE(AdsCreateIndex61(hTable, bag_buf.data(), tag, expr,
                             nullptr, nullptr, ADS_COMPOUND, 512,
                             &hIdx) == 0);

    ADSHANDLE hMg = mg_local();
    auto count_indexes = [&](const char* table) {
        std::array<ADS_MGMT_INDEX_INFO, 32> buf{};
        UNSIGNED16 cnt = static_cast<UNSIGNED16>(buf.size());
        UNSIGNED16 sz  = sizeof(ADS_MGMT_INDEX_INFO);
        std::vector<UNSIGNED8> tb;
        UNSIGNED8* tp = nullptr;
        if (table != nullptr) {
            tb.assign(table, table + std::strlen(table));
            tb.push_back(0);
            tp = tb.data();
        }
        REQUIRE(AdsMgGetOpenIndexes(hMg, tp, nullptr, 0,
                                    buf.data(), &cnt, &sz) == 0);
        // Confirm any reported entry carries a real name.
        for (UNSIGNED16 i = 0; i < cnt && i < buf.size(); ++i)
            CHECK(buf[i].aucIndexName[0] != 0);
        return cnt;
    };

    CHECK(count_indexes("idxa.dbf") >= 1);
    CHECK(count_indexes(nullptr)    >= 1);
    CHECK(count_indexes("no_such_table.dbf") == 0);

    REQUIRE(AdsMgDisconnect(hMg) == 0);
    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
}
