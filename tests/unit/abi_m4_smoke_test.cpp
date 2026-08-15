#include "doctest.h"
#include "abi/charset.h"
#include "openads/ace.h"
#include "openads/error.h"
#include "drivers/dbt/dbt_memo.h"
#include "drivers/fpt/fpt_memo.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

fs::path make_dbf_with_memo(const fs::path& dir, const char* leaf,
                            std::uint8_t version) {
    fs::create_directories(dir);
    auto p = dir / leaf;
    fs::remove(p);
    std::vector<std::uint8_t> file;
    std::array<std::uint8_t, 32> hdr{};
    hdr[0]  = version;
    hdr[4]  = 0;
    hdr[8]  = 32 + 64 + 1; hdr[9]  = 0;
    hdr[10] = 1 + 5 + 10; hdr[11] = 0;
    file.insert(file.end(), hdr.begin(), hdr.end());
    std::array<std::uint8_t, 32> name_fd{};
    std::strncpy(reinterpret_cast<char*>(name_fd.data()), "NAME", 11);
    name_fd[11] = 'C'; name_fd[16] = 5;
    file.insert(file.end(), name_fd.begin(), name_fd.end());
    std::array<std::uint8_t, 32> notes_fd{};
    std::strncpy(reinterpret_cast<char*>(notes_fd.data()), "NOTES", 11);
    notes_fd[11] = 'M'; notes_fd[16] = 10;
    file.insert(file.end(), notes_fd.begin(), notes_fd.end());
    file.push_back(0x0D);
    file.push_back(0x1A);
    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
    return p;
}

} // namespace

TEST_CASE("ABI M4 smoke: open table with auto-attached FPT, write memo, read back") {
    const auto dir = fs::temp_directory_path() / "openads_m4_abi_smoke";
    std::error_code ec;
    fs::remove_all(dir, ec);
    make_dbf_with_memo(dir, "data.dbf", 0x83);

    // Pre-create the .fpt sibling so Connection auto-attach finds it.
    auto fpt_path = (dir / "data.fpt");
    REQUIRE(openads::drivers::fpt::FptMemo::create(fpt_path.string(), 64)
            .has_value());

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
    UNSIGNED8 fld_name[16] = "NAME";
    UNSIGNED8 val_name[8]  = "Anna";
    REQUIRE(AdsSetString(hTable, fld_name, val_name, 4) == 0);

    UNSIGNED8 fld_notes[16] = "NOTES";
    const char* memo_text = "ABI smoke memo content";
    UNSIGNED8 val_notes[64];
    std::memcpy(val_notes, memo_text, std::strlen(memo_text));
    REQUIRE(AdsSetString(hTable, fld_notes, val_notes,
                         static_cast<UNSIGNED32>(std::strlen(memo_text)))
            == 0);

    UNSIGNED32 mlen = 0;
    REQUIRE(AdsGetMemoLength(hTable, fld_notes, &mlen) == 0);
    CHECK(mlen == std::strlen(memo_text));

    UNSIGNED16 mtype = 0;
    REQUIRE(AdsGetMemoDataType(hTable, fld_notes, &mtype) == 0);
    CHECK(mtype == ADS_MEMO_TEXT);

    // BinaryToFile / FileToBinary round-trip.
    auto extracted = (dir / "extracted.txt").string();
    UNSIGNED8 extracted_buf[260];
    std::memcpy(extracted_buf, extracted.c_str(), extracted.size() + 1);
    REQUIRE(AdsBinaryToFile(hTable, fld_notes, extracted_buf) == 0);

    std::ifstream in(extracted, std::ios::binary);
    std::string disk_content((std::istreambuf_iterator<char>(in)),
                              std::istreambuf_iterator<char>());
    in.close();
    CHECK(disk_content == memo_text);

    auto wide_input = (dir / "wide_input.bin").string();
    {
        std::ofstream out(wide_input, std::ios::binary);
        out << "wide memo payload";
    }
    auto wide_input_w = openads::abi::utf8_to_utf16le(wide_input);
    wide_input_w.push_back(0);
    REQUIRE(AdsFileToBinaryW(hTable, fld_notes, ADS_BINARY,
                             wide_input_w.data()) == 0);

    auto wide_output = (dir / "wide_output.bin").string();
    auto wide_output_w = openads::abi::utf8_to_utf16le(wide_output);
    wide_output_w.push_back(0);
    REQUIRE(AdsBinaryToFileW(hTable, fld_notes, wide_output_w.data()) == 0);

    std::ifstream wide_in(wide_output, std::ios::binary);
    std::string wide_disk_content((std::istreambuf_iterator<char>(wide_in)),
                                  std::istreambuf_iterator<char>());
    wide_in.close();
    CHECK(wide_disk_content == "wide memo payload");

    // Encryption thunks behave correctly: enable returns 5004 (pending),
    // is_*_encrypted returns 0.
    UNSIGNED8 pwd[16] = "secret";
    CHECK(AdsEnableEncryption(hConn, pwd) == openads::AE_FUNCTION_NOT_AVAILABLE);
    UNSIGNED16 enc = 99;
    REQUIRE(AdsIsTableEncrypted(hTable, &enc) == 0);
    CHECK(enc == 0);
    REQUIRE(AdsIsEncryptionEnabled(hConn, &enc) == 0);
    CHECK(enc == 0);

    // Autoinc returns 0 stub.
    UNSIGNED32 ai = 99;
    REQUIRE(AdsGetLastAutoinc(hTable, &ai) == 0);
    CHECK(ai == 0);

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);

    fs::remove_all(dir, ec);
}
