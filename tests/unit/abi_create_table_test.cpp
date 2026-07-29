#include "doctest.h"
#include "openads/ace.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

TEST_CASE("M9.5 AdsCreateTable: parse field defs + write DBF + open") {
    const auto dir = fs::temp_directory_path() / "openads_m95_create";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);

    UNSIGNED8 name[64]   = "newtable";
    UNSIGNED8 alias[64]  = "newtable";
    UNSIGNED8 fields[256] =
        "ID,Numeric,5,0;NAME,Character,12;ACTIVE,Logical;BORN,Date";

    ADSHANDLE hTable = 0;
    REQUIRE(AdsCreateTable(hConn, name, alias,
                           ADS_CDX, 0, 0, 0, 64,
                           fields, &hTable) == 0);

    UNSIGNED16 nflds = 0;
    REQUIRE(AdsGetNumFields(hTable, &nflds) == 0);
    CHECK(nflds == 4);

    UNSIGNED32 reclen = 0;
    REQUIRE(AdsGetRecordLength(hTable, &reclen) == 0);
    // 1 (delete) + 5 (ID) + 12 (NAME) + 1 (ACTIVE) + 8 (BORN) = 27
    CHECK(reclen == 27u);

    UNSIGNED16 type = 0;
    UNSIGNED8 fld[16] = "NAME";
    REQUIRE(AdsGetFieldType(hTable, fld, &type) == 0);
    CHECK(type == ADS_STRING);

    UNSIGNED32 flen = 0;
    REQUIRE(AdsGetFieldLength(hTable, fld, &flen) == 0);
    CHECK(flen == 12u);
    flen = 0;
    REQUIRE(AdsGetFieldLength100(hTable, fld, 0, &flen) == 0);
    CHECK(flen == 12u);

    UNSIGNED16 field_num = 0;
    REQUIRE(AdsGetFieldNum(hTable, fld, &field_num) == 0);
    CHECK(field_num == 2);

    UNSIGNED32 offset = 0;
    REQUIRE(AdsGetFieldOffset(hTable, fld, &offset) == 0);
    CHECK(offset == 6u);

    REQUIRE(AdsGetFieldNum(hTable, reinterpret_cast<UNSIGNED8*>(3),
                           &field_num) == 0);
    CHECK(field_num == 3);
    REQUIRE(AdsGetFieldOffset(hTable, reinterpret_cast<UNSIGNED8*>(3),
                              &offset) == 0);
    CHECK(offset == 18u);
    CHECK(AdsGetFieldNum(hTable, reinterpret_cast<UNSIGNED8*>(99),
                         &field_num) != 0);
    CHECK(AdsGetFieldOffset(hTable, reinterpret_cast<UNSIGNED8*>(99),
                            &offset) != 0);

    UNSIGNED16 lock_type = 0;
    REQUIRE(AdsGetTableLockType(hTable, &lock_type) == 0);
    CHECK(lock_type == ADS_COMPATIBLE_LOCKING);

    REQUIRE(AdsPackTable120(hTable, ADS_DEFAULT, 0) == 0);
    CHECK(AdsPackTable120(hTable, ADS_DEFAULT, 1) != 0);

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("AdsCreateTable: drive-rooted client path lands under the data dir") {
    const auto dir = fs::temp_directory_path() / "openads_abs_create";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);

    // Client (rddads / X#) passes an absolute path rooted at the data
    // dir's own drive. Its relative remainder is a single leaf, so the
    // fix must strip the root and write the table under the data dir —
    // NOT at the drive root the client happened to name.
    const std::string root = fs::path(dir).root_path().string();  // e.g. "C:\"
    const std::string leaf = "openads_abs_stray";
    const std::string abs_name = (fs::path(root) / (leaf + ".dbf")).string();

    UNSIGNED8 name[512];
    std::memcpy(name, abs_name.c_str(), abs_name.size() + 1);
    UNSIGNED8 alias[64]   = "stray";
    UNSIGNED8 fields[128] = "ID,Numeric,4,0;NAME,Character,8";

    ADSHANDLE hTable = 0;
    REQUIRE(AdsCreateTable(hConn, name, alias, ADS_CDX, 0, 0, 0, 64,
                           fields, &hTable) == 0);
    REQUIRE(AdsCloseTable(hTable) == 0);

    // Landed under the data dir, not at the drive root the client named.
    CHECK(fs::exists(dir / (leaf + ".dbf")));
    CHECK_FALSE(fs::exists(fs::path(abs_name)));

    // A later open by the bare leaf must resolve to the very same file.
    UNSIGNED8 openname[64];
    std::memcpy(openname, leaf.c_str(), leaf.size() + 1);
    hTable = 0;
    REQUIRE(AdsOpenTable(hConn, openname, alias, ADS_CDX, 0, 0, 0, 0,
                         &hTable) == 0);
    UNSIGNED16 nflds = 0;
    REQUIRE(AdsGetNumFields(hTable, &nflds) == 0);
    CHECK(nflds == 2);
    REQUIRE(AdsCloseTable(hTable) == 0);

    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

// Issue #142: an absolute path that already lives under data_dir_ must
// create the file at that exact location (not fold into data_dir_/rel).
// ERP apps name their company folder by full path; COPY TO <abs> VIA "ADS"
// used to rewrite F:\z\BASES\T.DAT as F:\z\BASES\z\BASES\T.DAT.
TEST_CASE("AdsCreateTable: absolute path under data_dir writes there") {
    const auto dir = fs::temp_directory_path() / "openads_abs_under_datadir";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir / "company");

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);

    const auto target = dir / "company" / "T.DAT";
    const std::string abs_name = target.string();
    UNSIGNED8 name[512];
    std::memcpy(name, abs_name.c_str(), abs_name.size() + 1);
    UNSIGNED8 fields[] = "ID,Numeric,4,0;NAME,Character,8";
    ADSHANDLE hTable = 0;
    REQUIRE(AdsCreateTable(hConn, name, nullptr, ADS_CDX, 0, 0, 0, 0,
                           fields, &hTable) == 0);
    REQUIRE(AdsCloseTable(hTable) == 0);

    CHECK(fs::exists(target));
    // Must not have been folded into a nested data_dir_ mirror.
    CHECK_FALSE(fs::exists(dir / "company" / "company" / "T.DAT"));

    // Reopen by the same absolute name.
    hTable = 0;
    REQUIRE(AdsOpenTable(hConn, name, nullptr, ADS_CDX, 0, 0, 0, 0,
                         &hTable) == 0);
    UNSIGNED16 nflds = 0;
    REQUIRE(AdsGetNumFields(hTable, &nflds) == 0);
    CHECK(nflds == 2);
    REQUIRE(AdsCloseTable(hTable) == 0);

    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("M12.23 AdsCreateTable with an M field stages an empty .fpt; memo writes work") {
    const auto dir = fs::temp_directory_path() / "openads_m1223_creatememo";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn) == 0);

    UNSIGNED8 name[64]    = "withmemo";
    UNSIGNED8 alias[64]   = "withmemo";
    UNSIGNED8 fields[128] = "ID,Numeric,4,0;NOTE,Memo,10";
    ADSHANDLE hTable = 0;
    REQUIRE(AdsCreateTable(hConn, name, alias, ADS_CDX, 0, 0, 0, 64,
                           fields, &hTable) == 0);
    REQUIRE(AdsCloseTable(hTable) == 0);

    // The .fpt must exist next to the .dbf.
    CHECK(fs::exists(dir / "withmemo.dbf"));
    CHECK(fs::exists(dir / "withmemo.fpt"));

    hTable = 0;
    REQUIRE(AdsOpenTable(hConn, name, alias, ADS_CDX, 0, 0, 0, 0, &hTable) == 0);
    REQUIRE(AdsAppendRecord(hTable) == 0);
    UNSIGNED8 fNOTE[8] = "NOTE";
    UNSIGNED8 payload[] = "hello memo from OpenADS";
    REQUIRE(AdsSetString(hTable, fNOTE, payload,
                         static_cast<UNSIGNED32>(std::strlen(
                             reinterpret_cast<const char*>(payload)))) == 0);
    REQUIRE(AdsWriteRecord(hTable) == 0);

    UNSIGNED8 buf[64] = {0};
    UNSIGNED32 cap = sizeof(buf);
    REQUIRE(AdsGetField(hTable, fNOTE, buf, &cap, 0) == 0);
    CHECK(std::string(reinterpret_cast<char*>(buf), cap) == "hello memo from OpenADS");

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}
