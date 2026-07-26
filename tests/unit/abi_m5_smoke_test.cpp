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
    hdr[4]  = 1;                                   // 1 record
    hdr[8]  = 32 + 32 + 1; hdr[9]  = 0;
    hdr[10] = 1 + 5;       hdr[11] = 0;
    file.insert(file.end(), hdr.begin(), hdr.end());
    std::array<std::uint8_t, 32> fd{};
    std::strncpy(reinterpret_cast<char*>(fd.data()), "TAG", 11);
    fd[11] = 'C'; fd[16] = 5;
    file.insert(file.end(), fd.begin(), fd.end());
    file.push_back(0x0D);
    // single record: " HELLO"
    file.push_back(' ');
    file.push_back('H'); file.push_back('E'); file.push_back('L');
    file.push_back('L'); file.push_back('O');
    file.push_back(0x1A);
    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
    return p;
}

std::string trim_field(const UNSIGNED8* buf, UNSIGNED32 cap) {
    std::string s(reinterpret_cast<const char*>(buf), cap);
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

struct RelFixture {
    fs::path dir;
    ADSHANDLE hConn = 0;

    explicit RelFixture(const char* leaf)
        : dir(fs::temp_directory_path() / leaf) {}

    void connect() {
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir);
        std::vector<UNSIGNED8> srv(dir.string().size() + 1);
        std::memcpy(srv.data(), dir.string().c_str(), dir.string().size() + 1);
        REQUIRE(AdsConnect60(srv.data(), ADS_LOCAL_SERVER,
                             nullptr, nullptr, 0, &hConn) == 0);
    }

    ADSHANDLE open_ord() {
        ADSHANDLE h = 0;
        UNSIGNED8 def[] =
            "ORDNO,C,8,0;CUSTNO,C,8,0;AMT,N,10,0";
        REQUIRE(AdsCreateTable(hConn, (UNSIGNED8*)"ord", nullptr, ADS_CDX,
                               0, 0, 0, 0, def, &h) == 0);
        ADSHANDLE hIdx = 0;
        REQUIRE(AdsCreateIndex61(h, (UNSIGNED8*)"ord.cdx", (UNSIGNED8*)"ORDNO",
                                 (UNSIGNED8*)"ORDNO", nullptr, nullptr,
                                 0, 0, &hIdx) == 0);
        REQUIRE(AdsCreateIndex61(h, (UNSIGNED8*)"ord.cdx", (UNSIGNED8*)"CUSTNO",
                                 (UNSIGNED8*)"CUSTNO", nullptr, nullptr,
                                 0, 0, &hIdx) == 0);
        REQUIRE(AdsCloseIndex(hIdx) == 0);
        return h;
    }

    ADSHANDLE open_ordln() {
        ADSHANDLE h = 0;
        UNSIGNED8 def[] =
            "ORDNO,C,8,0;LINENO,N,3,0;SKU,C,8,0;QTY,N,5,0";
        REQUIRE(AdsCreateTable(hConn, (UNSIGNED8*)"ordln", nullptr, ADS_CDX,
                               0, 0, 0, 0, def, &h) == 0);
        ADSHANDLE hIdx = 0;
        REQUIRE(AdsCreateIndex61(h, (UNSIGNED8*)"ordln.cdx", (UNSIGNED8*)"ORDNO",
                                 (UNSIGNED8*)"ORDNO", nullptr, nullptr,
                                 0, 0, &hIdx) == 0);
        REQUIRE(AdsCloseIndex(hIdx) == 0);
        return h;
    }

    void teardown() {
        if (hConn) {
            AdsDisconnect(hConn);
            hConn = 0;
        }
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
};

} // namespace

TEST_CASE("ABI M5 smoke: BeginTransaction + update + Rollback restores the original record") {
    const auto dir = fs::temp_directory_path() / "openads_m5_abi_rollback";
    std::error_code ec;
    fs::remove_all(dir, ec);
    make_dbf(dir, "data.dbf");

    ADSHANDLE hConn = 0;
    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);

    ADSHANDLE hTable = 0;
    UNSIGNED8 leaf[64] = "data.dbf";
    REQUIRE(AdsOpenTable(hConn, leaf, nullptr, ADS_CDX,
                         0, 0, 0, 0, &hTable) == 0);

    UNSIGNED16 in_tx = 99;
    REQUIRE(AdsInTransaction(hConn, &in_tx) == 0);
    CHECK(in_tx == 0);

    REQUIRE(AdsBeginTransaction(hConn) == 0);
    REQUIRE(AdsInTransaction(hConn, &in_tx) == 0);
    CHECK(in_tx == 1);

    REQUIRE(AdsGotoTop(hTable) == 0);
    REQUIRE(AdsLockTable(hTable) == 0);
    UNSIGNED8 fld[16]   = "TAG";
    UNSIGNED8 nval[16]  = "WORLD";
    REQUIRE(AdsSetString(hTable, fld, nval, 5) == 0);

    // Read-back inside the tx sees the updated value.
    UNSIGNED8 buf[16] = {0};
    UNSIGNED32 cap = sizeof(buf);
    REQUIRE(AdsGetField(hTable, fld, buf, &cap, 0) == 0);
    CHECK(std::string(reinterpret_cast<const char*>(buf), cap) == "WORLD");

    REQUIRE(AdsRollbackTransaction(hConn) == 0);

    // After rollback, original "HELLO" is back on disk.
    REQUIRE(AdsGotoTop(hTable) == 0);
    cap = sizeof(buf);
    std::memset(buf, 0, sizeof(buf));
    REQUIRE(AdsGetField(hTable, fld, buf, &cap, 0) == 0);
    CHECK(std::string(reinterpret_cast<const char*>(buf), cap) == "HELLO");

    REQUIRE(AdsInTransaction(hConn, &in_tx) == 0);
    CHECK(in_tx == 0);

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("ABI M5 smoke: AppendRecord inside tx + Rollback removes the appended record") {
    const auto dir = fs::temp_directory_path() / "openads_m5_abi_rollback_append";
    std::error_code ec;
    fs::remove_all(dir, ec);
    make_dbf(dir, "data.dbf");

    ADSHANDLE hConn = 0;
    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    ADSHANDLE hTable = 0;
    UNSIGNED8 leaf[64] = "data.dbf";
    REQUIRE(AdsOpenTable(hConn, leaf, nullptr, ADS_CDX,
                         0, 0, 0, 0, &hTable) == 0);

    REQUIRE(AdsBeginTransaction(hConn) == 0);
    REQUIRE(AdsAppendRecord(hTable) == 0);
    UNSIGNED8 fld[16] = "TAG";
    UNSIGNED8 nval[16] = "TEMP";
    REQUIRE(AdsSetString(hTable, fld, nval, 4) == 0);

    UNSIGNED32 rc = 0;
    REQUIRE(AdsGetRecordCount(hTable, 0, &rc) == 0);
    CHECK(rc == 2);

    REQUIRE(AdsRollbackTransaction(hConn) == 0);

    // ADS leaves no trace of a rolled-back AppendRecord: the row is
    // physically dropped, so RECCOUNT returns to its pre-transaction
    // value and only the baseline (live) record remains.
    REQUIRE(AdsGetRecordCount(hTable, 0, &rc) == 0);
    CHECK(rc == 1);
    REQUIRE(AdsGotoTop(hTable) == 0);
    UNSIGNED16 deleted = 0;
    REQUIRE(AdsIsRecordDeleted(hTable, &deleted) == 0);
    CHECK(deleted == 0);

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("ABI M5 smoke: Commit makes changes durable") {
    const auto dir = fs::temp_directory_path() / "openads_m5_abi_commit";
    std::error_code ec;
    fs::remove_all(dir, ec);
    make_dbf(dir, "data.dbf");

    ADSHANDLE hConn = 0;
    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    ADSHANDLE hTable = 0;
    UNSIGNED8 leaf[64] = "data.dbf";
    REQUIRE(AdsOpenTable(hConn, leaf, nullptr, ADS_CDX,
                         0, 0, 0, 0, &hTable) == 0);

    REQUIRE(AdsBeginTransaction(hConn) == 0);
    REQUIRE(AdsGotoTop(hTable) == 0);
    REQUIRE(AdsLockTable(hTable) == 0);
    UNSIGNED8 fld[16]  = "TAG";
    UNSIGNED8 nval[16] = "ABCDE";
    REQUIRE(AdsSetString(hTable, fld, nval, 5) == 0);
    REQUIRE(AdsCommitTransaction(hConn) == 0);

    // Reopen connection: the committed change is still on disk.
    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);

    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    REQUIRE(AdsOpenTable(hConn, leaf, nullptr, ADS_CDX,
                         0, 0, 0, 0, &hTable) == 0);
    REQUIRE(AdsGotoTop(hTable) == 0);
    UNSIGNED8 buf[16] = {0};
    UNSIGNED32 cap = sizeof(buf);
    REQUIRE(AdsGetField(hTable, fld, buf, &cap, 0) == 0);
    CHECK(std::string(reinterpret_cast<const char*>(buf), cap) == "ABCDE");

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("ABI M5 smoke: multi-tag CDX commit keeps indexed order seekable") {
    RelFixture fx("openads_m5_multitag_cm");
    fx.connect();
    ADSHANDLE hOrd = fx.open_ord();

    const char* cOrd = "TXMULCM";
    const char* cCust = "C00002";
    REQUIRE(AdsBeginTransaction(fx.hConn) == 0);
    REQUIRE(AdsAppendRecord(hOrd) == 0);
    REQUIRE(AdsSetString(hOrd, (UNSIGNED8*)"ORDNO",  (UNSIGNED8*)cOrd, 7) == 0);
    REQUIRE(AdsSetString(hOrd, (UNSIGNED8*)"CUSTNO", (UNSIGNED8*)cCust, 6) == 0);
    REQUIRE(AdsSetDouble(hOrd, (UNSIGNED8*)"AMT", 250.0) == 0);
    REQUIRE(AdsWriteRecord(hOrd) == 0);
    REQUIRE(AdsCommitTransaction(fx.hConn) == 0);
    REQUIRE(AdsCloseTable(hOrd) == 0);

    hOrd = 0;
    REQUIRE(AdsOpenTable(fx.hConn, (UNSIGNED8*)"ord", nullptr, ADS_CDX,
                         0, 0, 0, 0, &hOrd) == 0);

    ADSHANDLE hOrdIdx = 0;
    REQUIRE(AdsGetIndexHandle(hOrd, (UNSIGNED8*)"ORDNO", &hOrdIdx) == 0);
    UNSIGNED16 found = 0;
    REQUIRE(AdsSeek(hOrdIdx, (UNSIGNED8*)cOrd, 7, 0, 0, &found) == 0);
    CHECK(found == 1);

    UNSIGNED8 cust[16] = {0};
    UNSIGNED32 cap = sizeof(cust);
    REQUIRE(AdsGetField(hOrd, (UNSIGNED8*)"CUSTNO", cust, &cap, 0) == 0);
    CHECK(trim_field(cust, cap) == cCust);

    REQUIRE(AdsCloseTable(hOrd) == 0);
    fx.teardown();
}
