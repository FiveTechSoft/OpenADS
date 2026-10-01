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

// Stages a DBF with two character columns (TAG[4] + SEQ[2]) and three
// records, returning the directory path for the test.
fs::path stage_dbf(const fs::path& dir) {
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
    hdr[4]  = 3;
    hdr[8]  = 32 + 32 + 32 + 1;          // header+2 fielddefs+terminator
    hdr[10] = 1 + 4 + 2;                 // delete flag + 4 + 2
    push(hdr.data(), hdr.size());

    std::array<std::uint8_t, 32> tag_fd{};
    std::strncpy(reinterpret_cast<char*>(tag_fd.data()), "TAG", 11);
    tag_fd[11] = 'C'; tag_fd[16] = 4;
    push(tag_fd.data(), tag_fd.size());

    std::array<std::uint8_t, 32> seq_fd{};
    std::strncpy(reinterpret_cast<char*>(seq_fd.data()), "SEQ", 11);
    seq_fd[11] = 'C'; seq_fd[16] = 2;
    push(seq_fd.data(), seq_fd.size());

    file.push_back(0x0D);

    auto rec = [&](const char* tag, const char* seq) {
        file.push_back(' ');
        for (int i = 0; i < 4; ++i)
            file.push_back(i < (int)std::strlen(tag)
                           ? static_cast<std::uint8_t>(tag[i]) : ' ');
        for (int i = 0; i < 2; ++i)
            file.push_back(i < (int)std::strlen(seq)
                           ? static_cast<std::uint8_t>(seq[i]) : ' ');
    };
    rec("BBBB", "03");
    rec("AAAA", "01");
    rec("CCCC", "02");
    file.push_back(0x1A);

    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
    return p;
}

}  // namespace

TEST_CASE("M9.14 NTX multi-tag binding: two .ntx files coexist on one table") {
    auto dir = fs::temp_directory_path() / "openads_m9_14_ntx_multi";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_dbf(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);

    UNSIGNED8 leaf[16] = "data";
    ADSHANDLE hTable = 0;
    REQUIRE(AdsOpenTable(hConn, leaf, leaf, ADS_NTX, 1, 1, 0, 1, &hTable) == 0);

    // First NTX: index by TAG → "first.ntx".
    UNSIGNED8 first_file[32] = "first.ntx";
    UNSIGNED8 first_tag[16]  = "TAGORD";
    UNSIGNED8 first_expr[16] = "TAG";
    ADSHANDLE h_first = 0;
    REQUIRE(AdsCreateIndex61(hTable, first_file, first_tag, first_expr,
                             nullptr, nullptr,
                             0, 1024, &h_first) == 0);

    // Second NTX: index by SEQ → "second.ntx". Must NOT replace first.
    UNSIGNED8 second_file[32] = "second.ntx";
    UNSIGNED8 second_tag[16]  = "SEQORD";
    UNSIGNED8 second_expr[16] = "SEQ";
    ADSHANDLE h_second = 0;
    REQUIRE(AdsCreateIndex61(hTable, second_file, second_tag, second_expr,
                             nullptr, nullptr,
                             0, 1024, &h_second) == 0);

    UNSIGNED16 nidx = 0;
    REQUIRE(AdsGetNumIndexes(hTable, &nidx) == 0);
    CHECK(nidx == 2);

    // First is the active order: AdsGotoTop walks TAG order → AAAA(rec2).
    REQUIRE(AdsGotoTop(hTable) == 0);
    UNSIGNED32 recno = 0;
    REQUIRE(AdsGetRecordNum(hTable, 0, &recno) == 0);
    CHECK(recno == 2);

    // Look up second binding by tag name.
    UNSIGNED8 lookup[16] = "SEQORD";
    ADSHANDLE h_seq = 0;
    REQUIRE(AdsGetIndexHandle(hTable, lookup, &h_seq) == 0);
    CHECK(h_seq == h_second);

    // Seek through second binding → activates SEQ order, finds "01" at rec2.
    UNSIGNED8 key[4] = "01";
    UNSIGNED16 found = 0;
    REQUIRE(AdsSeek(h_seq, key, 2, 0, 0, &found) == 0);
    CHECK(found == 1);
    REQUIRE(AdsGetRecordNum(hTable, 0, &recno) == 0);
    CHECK(recno == 2);

    REQUIRE(AdsSetIndexOrderByHandle(hTable, h_seq) == 0);
    // After explicit SEQ focus, GotoTop walks SEQ order → "01"(rec2).
    REQUIRE(AdsGotoTop(hTable) == 0);
    REQUIRE(AdsGetRecordNum(hTable, 0, &recno) == 0);
    CHECK(recno == 2);  // SEQ "01" is rec2

    // Append a new record. Both indices should sync.
    REQUIRE(AdsAppendRecord(hTable) == 0);
    UNSIGNED8 fld_tag[8] = "TAG";
    UNSIGNED8 fld_seq[8] = "SEQ";
    UNSIGNED8 v_tag[8]   = "DDDD";
    UNSIGNED8 v_seq[8]   = "00";
    REQUIRE(AdsSetString(hTable, fld_tag, v_tag, 4) == 0);
    REQUIRE(AdsSetString(hTable, fld_seq, v_seq, 2) == 0);

    // SEQ "00" is now smallest → GotoTop on SEQ order = rec4.
    REQUIRE(AdsGotoTop(hTable) == 0);
    REQUIRE(AdsGetRecordNum(hTable, 0, &recno) == 0);
    CHECK(recno == 4);

    // Swap back to first binding: seek "DDDD" on TAGORD finds rec4.
    UNSIGNED8 key2[8] = "DDDD";
    found = 0;
    REQUIRE(AdsSeek(h_first, key2, 4, 0, 0, &found) == 0);
    CHECK(found == 1);
    REQUIRE(AdsGetRecordNum(hTable, 0, &recno) == 0);
    CHECK(recno == 4);

    REQUIRE(AdsSetIndexOrderByHandle(hTable, h_first) == 0);
    // GotoTop on TAGORD -> AAAA(rec2) again.
    REQUIRE(AdsGotoTop(hTable) == 0);
    REQUIRE(AdsGetRecordNum(hTable, 0, &recno) == 0);
    CHECK(recno == 2);

    // Both .ntx files exist on disk.
    CHECK(fs::exists(dir / "first.ntx"));
    CHECK(fs::exists(dir / "second.ntx"));

    REQUIRE(AdsCloseIndex(h_first)  == 0);
    REQUIRE(AdsCloseIndex(h_second) == 0);
    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("M9.14 NTX multi-tag: AdsOpenIndex re-binds two pre-existing files") {
    auto dir = fs::temp_directory_path() / "openads_m9_14_ntx_open";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_dbf(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    UNSIGNED8 leaf[16] = "data";
    ADSHANDLE hTable = 0;
    REQUIRE(AdsOpenTable(hConn, leaf, leaf, ADS_NTX, 1, 1, 0, 1, &hTable) == 0);

    UNSIGNED8 a_file[32] = "first.ntx";
    UNSIGNED8 a_tag[16]  = "TAGORD";
    UNSIGNED8 a_expr[16] = "TAG";
    ADSHANDLE h_a = 0;
    REQUIRE(AdsCreateIndex61(hTable, a_file, a_tag, a_expr,
                             nullptr, nullptr, 0, 1024, &h_a) == 0);
    UNSIGNED8 b_file[32] = "second.ntx";
    UNSIGNED8 b_tag[16]  = "SEQORD";
    UNSIGNED8 b_expr[16] = "SEQ";
    ADSHANDLE h_b = 0;
    REQUIRE(AdsCreateIndex61(hTable, b_file, b_tag, b_expr,
                             nullptr, nullptr, 0, 1024, &h_b) == 0);

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);

    // Reopen + bind both NTX files via two separate AdsOpenIndex calls.
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    REQUIRE(AdsOpenTable(hConn, leaf, leaf, ADS_NTX, 1, 1, 0, 1, &hTable) == 0);

    ADSHANDLE arr1[1] = {0};
    UNSIGNED16 cap1 = 1;
    REQUIRE(AdsOpenIndex(hTable, a_file, arr1, &cap1) == 0);
    CHECK(cap1 == 1);

    ADSHANDLE arr2[1] = {0};
    UNSIGNED16 cap2 = 1;
    REQUIRE(AdsOpenIndex(hTable, b_file, arr2, &cap2) == 0);
    CHECK(cap2 == 1);

    UNSIGNED16 nidx = 0;
    REQUIRE(AdsGetNumIndexes(hTable, &nidx) == 0);
    CHECK(nidx == 2);

    REQUIRE(AdsSetIndexOrderByHandle(hTable, arr1[0]) == 0);
    // Explicit focus on first binding: TAG(AAAA) is rec2.
    REQUIRE(AdsGotoTop(hTable) == 0);
    UNSIGNED32 recno = 0;
    REQUIRE(AdsGetRecordNum(hTable, 0, &recno) == 0);
    CHECK(recno == 2);

    REQUIRE(AdsCloseAllIndexes(hTable) == 0);
    REQUIRE(AdsGetNumIndexes(hTable, &nidx) == 0);
    CHECK(nidx == 0);

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("M9.14 NTX multi-tag: re-AdsOpenIndex on same path refreshes one binding") {
    auto dir = fs::temp_directory_path() / "openads_m9_14_ntx_refresh";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_dbf(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    UNSIGNED8 leaf[16] = "data";
    ADSHANDLE hTable = 0;
    REQUIRE(AdsOpenTable(hConn, leaf, leaf, ADS_NTX, 1, 1, 0, 1, &hTable) == 0);

    UNSIGNED8 a_file[32] = "first.ntx";
    UNSIGNED8 a_tag[16]  = "TAGORD";
    UNSIGNED8 a_expr[16] = "TAG";
    ADSHANDLE h_a = 0;
    REQUIRE(AdsCreateIndex61(hTable, a_file, a_tag, a_expr,
                             nullptr, nullptr, 0, 1024, &h_a) == 0);

    ADSHANDLE arr[1] = {0};
    UNSIGNED16 cap = 1;
    REQUIRE(AdsOpenIndex(hTable, a_file, arr, &cap) == 0);

    UNSIGNED16 nidx = 0;
    REQUIRE(AdsGetNumIndexes(hTable, &nidx) == 0);
    // Old h_a binding for this path was dropped; the reopen produced
    // a fresh single binding.
    CHECK(nidx == 1);

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}
