#include "doctest.h"
#include "abi/charset.h"
#include "openads/ace.h"
#include "openads/error.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

fs::path stage_int_dbf(const fs::path& dir) {
    fs::create_directories(dir);
    auto p = dir / "data.dbf";
    fs::remove(p);
    std::vector<std::uint8_t> file;
    auto push = [&](const void* d, std::size_t n) {
        const auto* b = static_cast<const std::uint8_t*>(d);
        file.insert(file.end(), b, b + n);
    };
    std::array<std::uint8_t, 32> hdr{};
    hdr[0]  = 0x03;
    hdr[4]  = 1;
    hdr[8]  = 32 + 32 + 1;
    hdr[10] = 1 + 20;
    push(hdr.data(), hdr.size());
    std::array<std::uint8_t, 32> fd{};
    std::strncpy(reinterpret_cast<char*>(fd.data()), "VAL", 11);
    fd[11] = 'N'; fd[16] = 20; fd[17] = 0;
    push(fd.data(), fd.size());
    file.push_back(0x0D);
    file.push_back(' ');
    const char* num = "        9876543210";
    std::size_t k = std::strlen(num);
    for (std::size_t i = 0; i < 20; ++i) {
        file.push_back(i < k ? static_cast<std::uint8_t>(num[i]) : ' ');
    }
    file.push_back(0x1A);
    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
    return p;
}

fs::path stage_char_dbf(const fs::path& dir,
                        const std::vector<std::string>& records) {
    fs::create_directories(dir);
    auto p = dir / "data.dbf";
    fs::remove(p);
    std::vector<std::uint8_t> file;
    auto push = [&](const void* d, std::size_t n) {
        const auto* b = static_cast<const std::uint8_t*>(d);
        file.insert(file.end(), b, b + n);
    };
    std::array<std::uint8_t, 32> hdr{};
    hdr[0]  = 0x03;
    hdr[4]  = static_cast<std::uint8_t>(records.size());
    hdr[8]  = 32 + 32 + 1;
    hdr[10] = 1 + 4;
    push(hdr.data(), hdr.size());
    std::array<std::uint8_t, 32> fd{};
    std::strncpy(reinterpret_cast<char*>(fd.data()), "TAG", 11);
    fd[11] = 'C'; fd[16] = 4;
    push(fd.data(), fd.size());
    file.push_back(0x0D);
    for (auto& s : records) {
        file.push_back(' ');
        std::size_t k = std::min<std::size_t>(s.size(), 4);
        for (std::size_t i = 0; i < k; ++i)
            file.push_back(static_cast<std::uint8_t>(s[i]));
        for (std::size_t i = k; i < 4; ++i) file.push_back(' ');
    }
    file.push_back(0x1A);
    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
    return p;
}

fs::path stage_date_numeric_dbf(const fs::path& dir) {
    fs::create_directories(dir);
    auto p = dir / "data.dbf";
    fs::remove(p);
    std::vector<std::uint8_t> file;
    auto push = [&](const void* d, std::size_t n) {
        const auto* b = static_cast<const std::uint8_t*>(d);
        file.insert(file.end(), b, b + n);
    };
    std::array<std::uint8_t, 32> hdr{};
    hdr[0] = 0x03;
    hdr[4] = 0;
    hdr[8] = 32 + 32 * 2 + 1;
    hdr[10] = 1 + 8 + 8;
    push(hdr.data(), hdr.size());
    std::array<std::uint8_t, 32> fd{};
    std::strncpy(reinterpret_cast<char*>(fd.data()), "WHEN", 11);
    fd[11] = 'D';
    fd[16] = 8;
    push(fd.data(), fd.size());
    fd = {};
    std::strncpy(reinterpret_cast<char*>(fd.data()), "COUNT", 11);
    fd[11] = 'N';
    fd[16] = 8;
    fd[17] = 0;
    push(fd.data(), fd.size());
    file.push_back(0x0D);
    file.push_back(0x1A);
    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
    return p;
}

}  // namespace

TEST_CASE("M9.23 AdsGetLongLong reads numeric field as int64") {
    auto dir = fs::temp_directory_path() / "openads_m9_23_longlong";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_int_dbf(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    UNSIGNED8 leaf[16] = "data";
    ADSHANDLE hTable = 0;
    REQUIRE(AdsOpenTable(hConn, leaf, leaf, ADS_CDX,
                         1, 1, 0, 1, &hTable) == 0);
    REQUIRE(AdsGotoTop(hTable) == 0);

    UNSIGNED8 fld[16] = "VAL";
    int64_t v = 0;
    REQUIRE(AdsGetLongLong(hTable, fld, &v) == 0);
    CHECK(v == 9876543210LL);

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("M9.23 AdsSetFieldRaw writes raw bytes verbatim") {
    auto dir = fs::temp_directory_path() / "openads_m9_23_raw";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_char_dbf(dir, {"AAAA"});

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    UNSIGNED8 leaf[16] = "data";
    ADSHANDLE hTable = 0;
    REQUIRE(AdsOpenTable(hConn, leaf, leaf, ADS_CDX,
                         1, 1, 0, 1, &hTable) == 0);
    REQUIRE(AdsGotoTop(hTable) == 0);

    UNSIGNED8 fld[16] = "TAG";
    UNSIGNED8 raw[4]  = {0xDE, 0xAD, 0xBE, 0xEF};
    REQUIRE(AdsSetFieldRaw(hTable, fld, raw, 4) == 0);

    UNSIGNED8 buf[16] = {0};
    UNSIGNED32 cap = sizeof(buf);
    REQUIRE(AdsGetField(hTable, fld, buf, &cap, 0) == 0);
    CHECK(cap == 4);
    CHECK(buf[0] == 0xDE);
    CHECK(buf[1] == 0xAD);
    CHECK(buf[2] == 0xBE);
    CHECK(buf[3] == 0xEF);

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("ACE setters: AdsSetDate and AdsSetLong write table fields") {
    auto dir = fs::temp_directory_path() / "openads_set_date_long";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_date_numeric_dbf(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    UNSIGNED8 leaf[16] = "data";
    ADSHANDLE hTable = 0;
    REQUIRE(AdsOpenTable(hConn, leaf, leaf, ADS_CDX,
                         1, 1, 0, 1, &hTable) == 0);
    REQUIRE(AdsAppendRecord(hTable) == 0);

    UNSIGNED8 date_fld[16] = "WHEN";
    UNSIGNED8 date_val[16] = "2026-07-03";
    REQUIRE(AdsSetDate(hTable, date_fld, date_val, 10) == 0);
    UNSIGNED8 num_fld[16] = "COUNT";
    REQUIRE(AdsSetLong(hTable, num_fld, -1234) == 0);

    // SAP behavior (5af6fafa): AdsGetField formats dates per the process
    // date format (AdsGetString stays raw "YYYYMMDD"). Pin MM/DD/CCYY so
    // this expectation is order-independent.
    UNSIGNED8 deffmt[16] = "MM/DD/CCYY";
    REQUIRE(AdsSetDateFormat(deffmt) == 0);
    UNSIGNED8 out[32] = {0};
    UNSIGNED32 cap = sizeof(out);
    REQUIRE(AdsGetField(hTable, date_fld, out, &cap, 0) == 0);
    CHECK(std::string(reinterpret_cast<char*>(out), cap) == "07/03/2026");

    std::memset(out, 0, sizeof(out));
    cap = sizeof(out);
    REQUIRE(AdsGetField(hTable, num_fld, out, &cap, 0) == 0);
    std::string n(reinterpret_cast<char*>(out), cap);
    while (!n.empty() && n.front() == ' ') n.erase(n.begin());
    CHECK(n == "-1234");

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("ACE SQL timeout accepts connection and statement handles") {
    auto dir = fs::temp_directory_path() / "openads_sql_timeout";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    REQUIRE(AdsSetSQLTimeout(hConn, 7) == 0);

    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);
    REQUIRE(AdsSetSQLTimeout(hStmt, 3) == 0);
    CHECK(AdsSetSQLTimeout(0xABCDEFu, 1) != 0);

    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("M9.23 AdsVerifySQL accepts valid SELECT and rejects garbage") {
    UNSIGNED8 ok_sql[64]  = "SELECT * FROM data";
    UNSIGNED8 bad_sql[64] = "DROP TABLE data";
    CHECK(AdsVerifySQL(0, ok_sql)  == 0);
    CHECK(AdsVerifySQL(0, bad_sql) != 0);

    auto ok_w = openads::abi::utf8_to_utf16le("SELECT * FROM data");
    ok_w.push_back(0);
    auto bad_w = openads::abi::utf8_to_utf16le("DROP TABLE data");
    bad_w.push_back(0);
    CHECK(AdsVerifySQLW(0, ok_w.data()) == 0);
    CHECK(AdsVerifySQLW(0, bad_w.data()) != 0);
}

TEST_CASE("M12 wrapper setters delegate to existing global settings") {
    UNSIGNED8 fmt[16] = "YYYY-MM-DD";
    REQUIRE(AdsSetDateFormat60(0, fmt) == 0);
    UNSIGNED8 out[32] = {0};
    UNSIGNED16 len = sizeof(out);
    REQUIRE(AdsGetDateFormat(out, &len) == 0);
    // SAP normalises a 4-digit year to CCYY on the way in (probed:
    // SetDateFormat("DD.MM.YYYY") reads back "DD.MM.CCYY").
    CHECK(std::string(reinterpret_cast<char*>(out), len) == "CCYY-MM-DD");
    // The format is process-global — restore SAP's default so tests that
    // run later see it.
    UNSIGNED8 def[16] = "MM/DD/CCYY";
    REQUIRE(AdsSetDateFormat(def) == 0);

    REQUIRE(AdsSetExact22(0, 1) == 0);
    UNSIGNED16 exact = 0;
    REQUIRE(AdsGetExact22(0, &exact) == 0);
    CHECK(exact == 1);

    REQUIRE(AdsSetExact22(0, 0) == 0);
    REQUIRE(AdsGetExact22(0, &exact) == 0);
    CHECK(exact == 0);
}

TEST_CASE("M9.23 AdsFailedTransactionRecovery opens + closes a connection") {
    auto dir = fs::temp_directory_path() / "openads_m9_23_recovery";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    REQUIRE(AdsFailedTransactionRecovery(srv) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("M9.23 AdsGetAllLocks reports the current lock view") {
    auto dir = fs::temp_directory_path() / "openads_m9_23_locks";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_char_dbf(dir, {"AAAA", "BBBB", "CCCC"});

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    UNSIGNED8 leaf[16] = "data";
    ADSHANDLE hTable = 0;
    REQUIRE(AdsOpenTable(hConn, leaf, leaf, ADS_CDX,
                         1, 1, 0, 1, &hTable) == 0);

    REQUIRE(AdsLockRecord(hTable, 1) == 0);
    REQUIRE(AdsLockRecord(hTable, 3) == 0);

    UNSIGNED32 buf[8]  = {0};
    UNSIGNED16 cnt     = 8;
    REQUIRE(AdsGetAllLocks(hTable, buf, &cnt) == 0);
    CHECK(cnt == 2);
    std::set<UNSIGNED32> got{buf[0], buf[1]};
    CHECK(got == std::set<UNSIGNED32>{1, 3});

    REQUIRE(AdsUnlockRecord(hTable, 1) == 0);
    REQUIRE(AdsUnlockRecord(hTable, 3) == 0);

    cnt = 8;
    REQUIRE(AdsGetAllLocks(hTable, buf, &cnt) == 0);
    CHECK(cnt == 0);

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("M9.23 AdsSkipUnique steps to the next distinct key") {
    auto dir = fs::temp_directory_path() / "openads_m9_23_skipunique";
    std::error_code ec;
    fs::remove_all(dir, ec);
    // Three records, two share the same TAG value so the unique walk
    // sees only two distinct keys: AAAA and BBBB.
    stage_char_dbf(dir, {"AAAA", "AAAA", "BBBB"});

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    UNSIGNED8 leaf[16] = "data";
    ADSHANDLE hTable = 0;
    REQUIRE(AdsOpenTable(hConn, leaf, leaf, ADS_CDX,
                         1, 1, 0, 1, &hTable) == 0);

    UNSIGNED8 idxfile[32] = "tag.ntx";
    UNSIGNED8 idxname[16] = "T";
    UNSIGNED8 expr[16]    = "TAG";
    ADSHANDLE hIdx = 0;
    REQUIRE(AdsCreateIndex61(hTable, idxfile, idxname, expr,
                             nullptr, nullptr, 0, 1024, &hIdx) == 0);

    REQUIRE(AdsGotoTop(hTable) == 0);
    UNSIGNED32 r = 0;
    REQUIRE(AdsGetRecordNum(hTable, 0, &r) == 0);
    CHECK((r == 1 || r == 2));    // either AAAA row (sort order ties)

    REQUIRE(AdsSkipUnique(hIdx, 1) == 0);
    REQUIRE(AdsGetRecordNum(hTable, 0, &r) == 0);
    CHECK(r == 3);                // BBBB row

    // No further unique key forward → AdsSkipUnique should fail.
    UNSIGNED32 rc = AdsSkipUnique(hIdx, 1);
    CHECK(rc != 0);

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}
