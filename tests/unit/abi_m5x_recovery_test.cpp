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
    file.push_back('H'); file.push_back('E'); file.push_back('L');
    file.push_back('L'); file.push_back('O');
    file.push_back(0x1A);
    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
    return p;
}

} // namespace

TEST_CASE("M5.x WAL: crash mid-tx, reopen recovers via persistent WAL replay") {
    const auto dir = fs::temp_directory_path() / "openads_m5x_crash";
    std::error_code ec;
    fs::remove_all(dir, ec);
    make_dbf(dir, "data.dbf");

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    UNSIGNED8 leaf[64] = "data.dbf";

    // First connection: begin tx, update, then disconnect WITHOUT
    // commit or rollback — simulating a crash. The WAL captures the
    // UPDATE record so the next open can recover.
    {
        ADSHANDLE hConn = 0;
        REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                             nullptr, nullptr, 0, &hConn) == 0);
        ADSHANDLE hTable = 0;
        REQUIRE(AdsOpenTable(hConn, leaf, nullptr, ADS_CDX,
                             0, 0, 0, 0, &hTable) == 0);
        REQUIRE(AdsBeginTransaction(hConn) == 0);
        REQUIRE(AdsGotoTop(hTable) == 0);
        REQUIRE(AdsLockTable(hTable) == 0);
        UNSIGNED8 fld[16]  = "TAG";
        UNSIGNED8 nval[16] = "WORLD";
        REQUIRE(AdsSetString(hTable, fld, nval, 5) == 0);
        // Force flush so the update lands on disk before the "crash".
        REQUIRE(AdsFlushFileBuffers(hTable) == 0);

        // Confirm the on-disk content is now WORLD before the crash.
        REQUIRE(AdsCloseTable(hTable) == 0);
        REQUIRE(AdsDisconnect(hConn) == 0);  // simulated crash
    }

    // Verify pre-recovery: reading the file directly shows "WORLD".
    {
        std::ifstream f((dir / "data.dbf").string(), std::ios::binary);
        f.seekg(32 + 32 + 1 + 1);   // skip header + field desc + 0x0D + del byte
        std::string buf(5, '\0');
        f.read(buf.data(), 5);
        CHECK(buf == "WORLD");
    }

    // Second connection: AdsConnect60 runs WAL recovery and rolls the
    // orphan UPDATE back to "HELLO" because the tx never committed.
    {
        ADSHANDLE hConn = 0;
        REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                             nullptr, nullptr, 0, &hConn) == 0);
        ADSHANDLE hTable = 0;
        REQUIRE(AdsOpenTable(hConn, leaf, nullptr, ADS_CDX,
                             0, 0, 0, 0, &hTable) == 0);
        REQUIRE(AdsGotoTop(hTable) == 0);
        UNSIGNED8 fld[16]  = "TAG";
        UNSIGNED8 buf[16]  = {0};
        UNSIGNED32 cap = sizeof(buf);
        REQUIRE(AdsGetField(hTable, fld, buf, &cap, 0) == 0);
        CHECK(std::string(reinterpret_cast<const char*>(buf), cap) == "HELLO");
        REQUIRE(AdsCloseTable(hTable) == 0);
        REQUIRE(AdsDisconnect(hConn) == 0);
    }

    fs::remove_all(dir, ec);
}

TEST_CASE("M5.x WAL: crash after append, reopen marks orphan record deleted") {
    const auto dir = fs::temp_directory_path() / "openads_m5x_crash_append";
    std::error_code ec;
    fs::remove_all(dir, ec);
    make_dbf(dir, "data.dbf");

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    UNSIGNED8 leaf[64] = "data.dbf";

    // First connection: begin tx, append a record with value "EXTRA",
    // then disconnect WITHOUT commit — simulating a crash.
    {
        ADSHANDLE hConn = 0;
        REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                             nullptr, nullptr, 0, &hConn) == 0);
        ADSHANDLE hTable = 0;
        REQUIRE(AdsOpenTable(hConn, leaf, nullptr, ADS_CDX,
                             0, 0, 0, 0, &hTable) == 0);
        REQUIRE(AdsBeginTransaction(hConn) == 0);
        REQUIRE(AdsAppendRecord(hTable) == 0);
        UNSIGNED8 fld[16]  = "TAG";
        UNSIGNED8 nval[16] = "EXTRA";
        REQUIRE(AdsSetString(hTable, fld, nval, 5) == 0);
        REQUIRE(AdsFlushFileBuffers(hTable) == 0);
        REQUIRE(AdsCloseTable(hTable) == 0);
        REQUIRE(AdsDisconnect(hConn) == 0);  // simulated crash
    }

    // The file now has 2 records (original + appended).  The appended
    // one should become deleted after recovery.
    {
        ADSHANDLE hConn = 0;
        REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                             nullptr, nullptr, 0, &hConn) == 0);
        ADSHANDLE hTable = 0;
        REQUIRE(AdsOpenTable(hConn, leaf, nullptr, ADS_CDX,
                             0, 0, 0, 0, &hTable) == 0);

        // Only the original "HELLO" record is visible (non-deleted).
        UNSIGNED32 count = 0;
        REQUIRE(AdsGetRecordCount(hTable, ADS_IGNOREFILTERS, &count) == 0);
        // record_count includes deleted rows; we need the live count.
        // Walk forward to see only non-deleted rows.
        REQUIRE(AdsGotoTop(hTable) == 0);
        UNSIGNED32 live = 0;
        while (true) {
            UNSIGNED16 eof = 0;
            AdsAtEOF(hTable, &eof);
            if (eof) break;
            UNSIGNED16 del = 0;
            AdsIsRecordDeleted(hTable, &del);
            if (!del) ++live;
            AdsSkip(hTable, 1);
        }
        CHECK(live == 1);

        // The surviving record should be the original "HELLO".
        REQUIRE(AdsGotoTop(hTable) == 0);
        UNSIGNED8 fld[16] = "TAG";
        UNSIGNED8 buf[16] = {0};
        UNSIGNED32 cap = sizeof(buf);
        REQUIRE(AdsGetField(hTable, fld, buf, &cap, 0) == 0);
        CHECK(std::string(reinterpret_cast<const char*>(buf), cap) == "HELLO");

        REQUIRE(AdsCloseTable(hTable) == 0);
        REQUIRE(AdsDisconnect(hConn) == 0);
    }

    fs::remove_all(dir, ec);
}

TEST_CASE("M5.x WAL: committed tx survives reconnect unmodified") {
    const auto dir = fs::temp_directory_path() / "openads_m5x_crash_commit";
    std::error_code ec;
    fs::remove_all(dir, ec);
    make_dbf(dir, "data.dbf");

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    UNSIGNED8 leaf[64] = "data.dbf";

    // Commit a change within a proper transaction; reconnect should
    // see the committed value, not revert it.
    {
        ADSHANDLE hConn = 0;
        REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                             nullptr, nullptr, 0, &hConn) == 0);
        ADSHANDLE hTable = 0;
        REQUIRE(AdsOpenTable(hConn, leaf, nullptr, ADS_CDX,
                             0, 0, 0, 0, &hTable) == 0);
        REQUIRE(AdsBeginTransaction(hConn) == 0);
        REQUIRE(AdsGotoTop(hTable) == 0);
        REQUIRE(AdsLockTable(hTable) == 0);
        UNSIGNED8 fld[16]  = "TAG";
        UNSIGNED8 nval[16] = "COMMT";
        REQUIRE(AdsSetString(hTable, fld, nval, 5) == 0);
        REQUIRE(AdsCommitTransaction(hConn) == 0);
        REQUIRE(AdsCloseTable(hTable) == 0);
        REQUIRE(AdsDisconnect(hConn) == 0);
    }

    // Reconnect — the committed value "COMMT" must still be present.
    {
        ADSHANDLE hConn = 0;
        REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                             nullptr, nullptr, 0, &hConn) == 0);
        ADSHANDLE hTable = 0;
        REQUIRE(AdsOpenTable(hConn, leaf, nullptr, ADS_CDX,
                             0, 0, 0, 0, &hTable) == 0);
        REQUIRE(AdsGotoTop(hTable) == 0);
        UNSIGNED8 fld[16] = "TAG";
        UNSIGNED8 buf[16] = {0};
        UNSIGNED32 cap = sizeof(buf);
        REQUIRE(AdsGetField(hTable, fld, buf, &cap, 0) == 0);
        CHECK(std::string(reinterpret_cast<const char*>(buf), cap) == "COMMT");
        REQUIRE(AdsCloseTable(hTable) == 0);
        REQUIRE(AdsDisconnect(hConn) == 0);
    }

    fs::remove_all(dir, ec);
}
