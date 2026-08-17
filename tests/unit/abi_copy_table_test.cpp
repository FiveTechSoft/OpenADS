#include "doctest.h"
#include "openads/ace.h"

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
    auto push = [&](const void* d, std::size_t n) {
        const auto* b = static_cast<const std::uint8_t*>(d);
        file.insert(file.end(), b, b + n);
    };
    std::array<std::uint8_t, 32> hdr{};
    hdr[0]  = 0x03; hdr[4]  = 4;
    hdr[8]  = 32 + 32 + 1; hdr[10] = 1 + 4;
    push(hdr.data(), hdr.size());
    std::array<std::uint8_t, 32> fd{};
    std::strncpy(reinterpret_cast<char*>(fd.data()), "TAG", 11);
    fd[11] = 'C'; fd[16] = 4;
    push(fd.data(), fd.size());
    file.push_back(0x0D);
    auto rec = [&](std::uint8_t del, const char* s) {
        file.push_back(del);
        for (int i = 0; i < 4; ++i)
            file.push_back(i < (int)std::strlen(s)
                           ? static_cast<std::uint8_t>(s[i]) : ' ');
    };
    rec(' ', "AAAA");
    rec('*', "BBBB");
    rec(' ', "CCCC");
    rec(' ', "DDDD");
    file.push_back(0x1A);
    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
    return p;
}

} // namespace

TEST_CASE("M9.11 AdsCopyTable copies live records to a new DBF") {
    auto dir = fs::temp_directory_path() / "openads_m911_copy";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_dbf(dir, "src.dbf");

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);

    ADSHANDLE hSrc = 0;
    UNSIGNED8 src_name[16] = "src";
    REQUIRE(AdsOpenTable(hConn, src_name, src_name,
                         ADS_CDX, 1, 1, 0, 1, &hSrc) == 0);

    UNSIGNED8 target[64] = "dst.dbf";
    REQUIRE(AdsCopyTable(hSrc, /*ADS_RESPECTFILTERS*/ 1, target) == 0);

    REQUIRE(AdsCloseTable(hSrc) == 0);

    // Open the copy and verify the live records survived (3 not 4
    // because the deleted BBBB row was filtered out).
    ADSHANDLE hDst = 0;
    UNSIGNED8 dst_name[16] = "dst";
    REQUIRE(AdsOpenTable(hConn, dst_name, dst_name,
                         ADS_CDX, 1, 1, 0, 1, &hDst) == 0);
    UNSIGNED32 cnt = 0;
    REQUIRE(AdsGetRecordCount(hDst, 0, &cnt) == 0);
    CHECK(cnt == 3u);

    REQUIRE(AdsGotoRecord(hDst, 1) == 0);
    UNSIGNED8 fld[16] = "TAG";
    UNSIGNED8 buf[16] = {0};
    UNSIGNED32 cap = sizeof(buf);
    REQUIRE(AdsGetField(hDst, fld, buf, &cap, 0) == 0);
    CHECK(std::string(reinterpret_cast<const char*>(buf), cap) == "AAAA");

    REQUIRE(AdsGotoRecord(hDst, 2) == 0);
    cap = sizeof(buf); std::memset(buf, 0, sizeof(buf));
    REQUIRE(AdsGetField(hDst, fld, buf, &cap, 0) == 0);
    CHECK(std::string(reinterpret_cast<const char*>(buf), cap) == "CCCC");

    REQUIRE(AdsCloseTable(hDst) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("AdsCopyTableContent copies matching fields by name") {
    // Source has TAG C(4). Destination has TAG C(4) + EXTRA C(8).
    // After the copy, TAG must match; EXTRA (absent in source) stays blank.
    auto dir = fs::temp_directory_path() / "openads_copy_content";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_dbf(dir, "src.dbf");   // 4 records: AAAA live, BBBB deleted, CCCC live, DDDD live

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);

    // Create destination with wider schema.
    UNSIGNED8 dst_name[16] = "dst2";
    UNSIGNED8 dst_alias[16]= "dst2";
    UNSIGNED8 fields[64]   = "TAG,Character,4;EXTRA,Character,8";
    ADSHANDLE hDst = 0;
    REQUIRE(AdsCreateTable(hConn, dst_name, dst_alias,
                           ADS_CDX, 0, 0, 0, 64, fields, &hDst) == 0);

    ADSHANDLE hSrc = 0;
    UNSIGNED8 src_name[16] = "src";
    REQUIRE(AdsOpenTable(hConn, src_name, src_name,
                         ADS_CDX, 0, 0, 0, 0, &hSrc) == 0);

    REQUIRE(AdsCopyTableContent(hSrc, hDst) == 0);

    // SAP (ace_adscopytablecontents): with "show deleted" True — the
    // default — "records marked for deletion will be copied in DBF
    // tables"; only AdsShowDeleted(0) filters them out.
    UNSIGNED32 cnt = 0;
    REQUIRE(AdsGetRecordCount(hDst, 0, &cnt) == 0);
    CHECK(cnt == 4u);

    REQUIRE(AdsGotoRecord(hDst, 1) == 0);
    UNSIGNED8 tag_fld[8]   = "TAG";
    UNSIGNED8 extra_fld[8] = "EXTRA";
    UNSIGNED8 buf[16] = {};
    UNSIGNED32 cap = sizeof(buf);
    REQUIRE(AdsGetField(hDst, tag_fld, buf, &cap, 0) == 0);
    CHECK(std::string(reinterpret_cast<const char*>(buf), cap) == "AAAA");

    // EXTRA field must be blank — it has no match in the source schema.
    std::memset(buf, 0xFF, sizeof(buf));
    cap = sizeof(buf);
    REQUIRE(AdsGetField(hDst, extra_fld, buf, &cap, 0) == 0);
    for (UNSIGNED32 i = 0; i < cap; ++i) CHECK(buf[i] == ' ');

    REQUIRE(AdsGotoRecord(hDst, 2) == 0);
    cap = sizeof(buf); std::memset(buf, 0, sizeof(buf));
    REQUIRE(AdsGetField(hDst, tag_fld, buf, &cap, 0) == 0);
    CHECK(std::string(reinterpret_cast<const char*>(buf), cap) == "BBBB");

    REQUIRE(AdsCloseTable(hSrc) == 0);
    REQUIRE(AdsCloseTable(hDst) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("AdsCloneTable produces an independent copy with all records") {
    // Source has 4 records (3 live + 1 deleted). The clone must carry
    // all 4 records including the deleted tombstone.
    auto dir = fs::temp_directory_path() / "openads_clone_table";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_dbf(dir, "src.dbf");

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);

    ADSHANDLE hSrc = 0;
    UNSIGNED8 src_name[16] = "src";
    REQUIRE(AdsOpenTable(hConn, src_name, src_name,
                         ADS_CDX, 0, 0, 0, 0, &hSrc) == 0);

    ADSHANDLE hClone = 0;
    REQUIRE(AdsCloneTable(hSrc, &hClone) == 0);
    CHECK(hClone != 0u);
    CHECK(hClone != hSrc);

    // Clone must have all 4 records (including the deleted one).
    UNSIGNED32 cnt = 0;
    REQUIRE(AdsGetRecordCount(hClone, 0, &cnt) == 0);
    CHECK(cnt == 4u);

    // Verify live record values.
    UNSIGNED8 tag_fld[8] = "TAG";
    UNSIGNED8 buf[16] = {};
    UNSIGNED32 cap = sizeof(buf);
    REQUIRE(AdsGotoRecord(hClone, 1) == 0);
    REQUIRE(AdsGetField(hClone, tag_fld, buf, &cap, 0) == 0);
    CHECK(std::string(reinterpret_cast<const char*>(buf), cap) == "AAAA");

    REQUIRE(AdsGotoRecord(hClone, 3) == 0);
    cap = sizeof(buf); std::memset(buf, 0, sizeof(buf));
    REQUIRE(AdsGetField(hClone, tag_fld, buf, &cap, 0) == 0);
    CHECK(std::string(reinterpret_cast<const char*>(buf), cap) == "CCCC");

    // Clone is independent — appending a record to the clone must not
    // affect the source.
    REQUIRE(AdsAppendRecord(hClone) == 0);
    UNSIGNED8 new_val[5] = "ZZZZ";
    UNSIGNED8 tag_fn[4] = "TAG";
    AdsSetString(hClone, tag_fn, new_val, 4);
    REQUIRE(AdsWriteRecord(hClone) == 0);

    UNSIGNED32 src_cnt = 0;
    REQUIRE(AdsGetRecordCount(hSrc, 0, &src_cnt) == 0);
    CHECK(src_cnt == 4u);   // source unchanged

    REQUIRE(AdsCloseTable(hClone) == 0);
    REQUIRE(AdsCloseTable(hSrc) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("AdsCopyTableStructure writes empty file with same schema") {
    // Source has TAG C(4) with 3 live records. The structure copy
    // must produce a DBF with the same field definition but 0 records.
    auto dir = fs::temp_directory_path() / "openads_copy_structure";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_dbf(dir, "src.dbf");

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);

    ADSHANDLE hSrc = 0;
    UNSIGNED8 src_name[16] = "src";
    REQUIRE(AdsOpenTable(hConn, src_name, src_name,
                         ADS_CDX, 0, 0, 0, 0, &hSrc) == 0);

    UNSIGNED8 dst_path[32] = "struct.dbf";
    REQUIRE(AdsCopyTableStructure(hSrc, dst_path) == 0);

    // Open the schema-only copy and verify: 0 records, 1 field, field is TAG C(4).
    ADSHANDLE hStr = 0;
    UNSIGNED8 str_name[16] = "struct";
    REQUIRE(AdsOpenTable(hConn, str_name, str_name,
                         ADS_CDX, 0, 0, 0, 0, &hStr) == 0);

    UNSIGNED32 cnt = 0;
    REQUIRE(AdsGetRecordCount(hStr, 0, &cnt) == 0);
    CHECK(cnt == 0u);

    UNSIGNED16 fcount = 0;
    REQUIRE(AdsGetNumFields(hStr, &fcount) == 0);
    CHECK(fcount == 1u);

    UNSIGNED8 fname[16] = {};
    UNSIGNED16 flen = sizeof(fname);
    REQUIRE(AdsGetFieldName(hStr, 1, fname, &flen) == 0);
    CHECK(std::string(reinterpret_cast<const char*>(fname)) == "TAG");

    UNSIGNED16 ftype = 0;
    UNSIGNED32 fsize = 0;
    REQUIRE(AdsGetFieldType(hStr, fname, &ftype) == 0);
    REQUIRE(AdsGetFieldLength(hStr, fname, &fsize) == 0);
    CHECK(ftype == ADS_STRING);
    CHECK(fsize == 4u);

    REQUIRE(AdsCloseTable(hStr) == 0);
    REQUIRE(AdsCloseTable(hSrc) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("M9.11 AdsCopyTableContents appends src rows into dst") {
    auto dir = fs::temp_directory_path() / "openads_m911_contents";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_dbf(dir, "src.dbf");

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);

    // Stage an empty dst table with the same schema via AdsCreateTable.
    UNSIGNED8 dst_name[64] = "dst";
    UNSIGNED8 dst_alias[64]= "dst";
    UNSIGNED8 fields[256]  = "TAG,Character,4";
    ADSHANDLE hDst = 0;
    REQUIRE(AdsCreateTable(hConn, dst_name, dst_alias,
                           ADS_CDX, 0, 0, 0, 64,
                           fields, &hDst) == 0);

    ADSHANDLE hSrc = 0;
    UNSIGNED8 src_name[16] = "src";
    REQUIRE(AdsOpenTable(hConn, src_name, src_name,
                         ADS_CDX, 1, 1, 0, 1, &hSrc) == 0);

    REQUIRE(AdsCopyTableContents(hSrc, hDst, ADS_IGNOREFILTERS) == 0);

    UNSIGNED32 cnt = 0;
    REQUIRE(AdsGetRecordCount(hDst, 0, &cnt) == 0);
    CHECK(cnt == 3u);   // BBBB filtered as deleted

    REQUIRE(AdsCloseTable(hSrc) == 0);
    REQUIRE(AdsCloseTable(hDst) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}
