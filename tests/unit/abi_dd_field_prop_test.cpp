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

// Minimal DBF with two fields: NAME C(10), AGE N(3,0).
fs::path make_dbf_two_fields(const fs::path& dir, const char* leaf) {
    fs::create_directories(dir);
    auto p = dir / leaf;
    std::vector<std::uint8_t> file;

    const std::uint16_t rec_len = 1 + 10 + 3;
    auto put_u16 = [](std::vector<std::uint8_t>& b, std::uint16_t v) {
        b.push_back(v & 0xFF); b.push_back((v >> 8) & 0xFF);
    };

    std::array<std::uint8_t, 32> hdr{};
    hdr[0] = 0x03;
    hdr[4] = 0; hdr[5] = 0; hdr[6] = 0; hdr[7] = 0;  // record count
    const std::uint16_t hdr_size = 32 + 2*32 + 1;
    hdr[8] = hdr_size & 0xFF; hdr[9] = (hdr_size >> 8) & 0xFF;
    hdr[10] = rec_len & 0xFF; hdr[11] = (rec_len >> 8) & 0xFF;
    file.insert(file.end(), hdr.begin(), hdr.end());

    // Field 1: NAME C(10)
    std::array<std::uint8_t, 32> fd1{};
    std::strncpy(reinterpret_cast<char*>(fd1.data()), "NAME", 11);
    fd1[11] = 'C'; fd1[16] = 10; fd1[17] = 0;
    file.insert(file.end(), fd1.begin(), fd1.end());

    // Field 2: AGE N(3,0)
    std::array<std::uint8_t, 32> fd2{};
    std::strncpy(reinterpret_cast<char*>(fd2.data()), "AGE", 11);
    fd2[11] = 'N'; fd2[16] = 3; fd2[17] = 0;
    file.insert(file.end(), fd2.begin(), fd2.end());

    file.push_back(0x0D); // terminator
    file.push_back(0x1A); // EOF marker

    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
    (void)put_u16; // suppress warning
    return p;
}

struct FpFixture {
    fs::path   dir;
    ADSHANDLE  hConn = 0;
    ADSHANDLE  hTable = 0;

    FpFixture() {
        dir = fs::temp_directory_path() / "openads_dd_fp";
        std::error_code ec;
        fs::remove_all(dir, ec);
        make_dbf_two_fields(dir, "people.dbf");

        auto add_path = (dir / "openads.add").string();
        UNSIGNED8 add_buf[260];
        std::memcpy(add_buf, add_path.c_str(), add_path.size() + 1);
        REQUIRE(AdsDDCreate(add_buf, 0, nullptr, &hConn) == 0);

        UNSIGNED8 alias[16] = "people";
        UNSIGNED8 path[32]  = "people.dbf";
        REQUIRE(AdsDDAddTable(hConn, alias, path, 0, 0, nullptr, nullptr) == 0);

        // Open the table so structural field lookups work.
        UNSIGNED8 tbl_path[260];
        auto full = (dir / "people.dbf").string();
        std::memcpy(tbl_path, full.c_str(), full.size() + 1);
        REQUIRE(AdsOpenTable(hConn, tbl_path, nullptr, 0, 0, 0, 0, 0, &hTable) == 0);
    }

    ~FpFixture() {
        if (hTable) AdsCloseTable(hTable);
        if (hConn)  AdsDisconnect(hConn);
    }
};

} // namespace

TEST_CASE("AdsDDGetFieldProperty — structural: type and definition") {
    FpFixture f;
    UNSIGNED8 tbl[16] = "people";
    UNSIGNED8 fld[16] = "NAME";

    // SAP ABI: ADS_DD_FIELD_TYPE = 306, UNSIGNED16 ADS data type.
    UNSIGNED16 type_val = 0;
    UNSIGNED16 len = sizeof(type_val);
    REQUIRE(AdsDDGetFieldProperty(f.hConn, tbl, fld, ADS_DD_FIELD_TYPE,
                                   &type_val, &len) == 0);
    CHECK(type_val == ADS_STRING);

    // ADS_DD_FIELD_DEFINITION = 305 — field-definition text.
    char buf[64];
    len = sizeof(buf);
    REQUIRE(AdsDDGetFieldProperty(f.hConn, tbl, fld, ADS_DD_FIELD_DEFINITION,
                                   buf, &len) == 0);
    CHECK(std::string(buf, len).find("NAME") == 0);
}

TEST_CASE("AdsDDGetFieldProperty — structural: length and decimals") {
    FpFixture f;
    UNSIGNED8 tbl[16] = "people";
    UNSIGNED8 fld_age[8] = "AGE";

    UNSIGNED16 len_val = 0, dec_val = 0;
    UNSIGNED16 cap = sizeof(len_val);
    REQUIRE(AdsDDGetFieldProperty(f.hConn, tbl, fld_age, ADS_DD_FIELD_LENGTH,
                                   &len_val, &cap) == 0);
    CHECK(len_val == 3);

    cap = sizeof(dec_val);
    REQUIRE(AdsDDGetFieldProperty(f.hConn, tbl, fld_age, ADS_DD_FIELD_DECIMAL,
                                   &dec_val, &cap) == 0);
    CHECK(dec_val == 0);
}

TEST_CASE("AdsDDSetFieldProperty + AdsDDGetFieldProperty — stored props round-trip") {
    FpFixture f;
    UNSIGNED8 tbl[16] = "people";
    UNSIGNED8 fld[16] = "NAME";

    // Set comment (fields use the generic ADS_DD_COMMENT property — SAP
    // has no field-specific comment id).
    const char* cmt = "The person's name";
    REQUIRE(AdsDDSetFieldProperty(f.hConn, tbl, fld, ADS_DD_COMMENT,
                                   const_cast<char*>(cmt),
                                   static_cast<UNSIGNED16>(std::strlen(cmt))) == 0);
    // Mark NOT NULL: ADS_DD_FIELD_CAN_NULL = 301, UNSIGNED16 (0 = NULL
    // not allowed).
    UNSIGNED16 can_null = 0;
    REQUIRE(AdsDDSetFieldProperty(f.hConn, tbl, fld, ADS_DD_FIELD_CAN_NULL,
                                   &can_null, sizeof(can_null)) == 0);
    // Set default value (ADS_DD_FIELD_DEFAULT_VALUE = 300).
    const char* def = "Unknown";
    REQUIRE(AdsDDSetFieldProperty(f.hConn, tbl, fld, ADS_DD_FIELD_DEFAULT_VALUE,
                                   const_cast<char*>(def),
                                   static_cast<UNSIGNED16>(std::strlen(def))) == 0);

    // Read back
    char buf[128];
    UNSIGNED16 len = sizeof(buf);
    REQUIRE(AdsDDGetFieldProperty(f.hConn, tbl, fld, ADS_DD_COMMENT,
                                   buf, &len) == 0);
    CHECK(std::string(buf, len) == "The person's name");

    UNSIGNED16 got_null = 99;
    len = sizeof(got_null);
    REQUIRE(AdsDDGetFieldProperty(f.hConn, tbl, fld, ADS_DD_FIELD_CAN_NULL,
                                   &got_null, &len) == 0);
    CHECK(got_null == 0);            // NOT NULL round-trips

    len = sizeof(buf);
    REQUIRE(AdsDDGetFieldProperty(f.hConn, tbl, fld, ADS_DD_FIELD_DEFAULT_VALUE,
                                   buf, &len) == 0);
    CHECK(std::string(buf, len) == "Unknown");

    // A field with no default reports AE_PROPERTY_NOT_SET (SAP behavior).
    UNSIGNED8 fld_age[8] = "AGE";
    len = sizeof(buf);
    CHECK(AdsDDGetFieldProperty(f.hConn, tbl, fld_age,
                                ADS_DD_FIELD_DEFAULT_VALUE, buf, &len) ==
          openads::AE_PROPERTY_NOT_SET);
}

TEST_CASE("AdsDDGetFieldProperty — unknown field returns error") {
    FpFixture f;
    UNSIGNED8 tbl[16] = "people";
    UNSIGNED8 fld[16] = "NOPE";
    char buf[64];
    UNSIGNED16 len = sizeof(buf);
    UNSIGNED32 rc = AdsDDGetFieldProperty(f.hConn, tbl, fld, ADS_DD_FIELD_TYPE,
                                           buf, &len);
    CHECK(rc != 0);
}

TEST_CASE("AdsDDSetFieldProperty — structural prop returns not-available") {
    FpFixture f;
    UNSIGNED8 tbl[16] = "people";
    UNSIGNED8 fld[16] = "NAME";
    const char* val = "NewName";
    UNSIGNED32 rc = AdsDDSetFieldProperty(f.hConn, tbl, fld, ADS_DD_FIELD_TYPE,
                                           const_cast<char*>(val), 7);
    CHECK(rc == openads::AE_FUNCTION_NOT_AVAILABLE);
}

TEST_CASE("AdsDDGetFieldProperty — persists across DD reopen") {
    const auto dir = fs::temp_directory_path() / "openads_dd_fp_persist";
    std::error_code ec;
    fs::remove_all(dir, ec);
    make_dbf_two_fields(dir, "emp.dbf");

    auto add_path = (dir / "openads.add").string();
    UNSIGNED8 add_buf[260];
    std::memcpy(add_buf, add_path.c_str(), add_path.size() + 1);

    {
        ADSHANDLE hConn = 0;
        REQUIRE(AdsDDCreate(add_buf, 0, nullptr, &hConn) == 0);
        UNSIGNED8 alias[8] = "emp";
        UNSIGNED8 path[16] = "emp.dbf";
        REQUIRE(AdsDDAddTable(hConn, alias, path, 0, 0, nullptr, nullptr) == 0);
        UNSIGNED8 tbl[8] = "emp", fld[8] = "NAME";
        const char* rule = "LEN(NAME)>0";
        REQUIRE(AdsDDSetFieldProperty(hConn, tbl, fld, ADS_DD_OA_FIELD_VALIDATION_RULE,
                                       const_cast<char*>(rule),
                                       static_cast<UNSIGNED16>(std::strlen(rule))) == 0);
        AdsDisconnect(hConn);
    }

    {
        ADSHANDLE hConn = 0;
        REQUIRE(AdsConnect60(add_buf, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn) == 0);
        UNSIGNED8 tbl[8] = "emp", fld[8] = "NAME";
        char buf[128];
        UNSIGNED16 len = sizeof(buf);
        REQUIRE(AdsDDGetFieldProperty(hConn, tbl, fld, ADS_DD_OA_FIELD_VALIDATION_RULE,
                                       buf, &len) == 0);
        CHECK(std::string(buf, len) == "LEN(NAME)>0");
        AdsDisconnect(hConn);
    }
}
