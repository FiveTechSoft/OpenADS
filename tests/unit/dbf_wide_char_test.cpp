// Regression: CHARACTER fields wider than 255 bytes.
//
// DBF descriptor bytes 16/17 hold width/decimals for most types, but for
// CHARACTER fields Harbour DBFCDX uses width = byte16 + byte17*256 with
// decimals 0 (dbf1.c hb_dbfOpen). OpenADS read byte 16 only and treated
// byte 17 as decimals, so C(300) opened as C(44), C(266) as C(10), and all
// following field offsets shifted. Reported by Pritpal Bedi (BRWCONFI.FD2).

#include "doctest.h"
#include "drivers/dbf_common.h"
#include "openads/ace.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using openads::drivers::DbfFieldType;
using openads::drivers::parse_dbf_fields;

namespace {
void put(std::vector<std::uint8_t>& b, std::size_t i, const char* name,
         char type, unsigned len, unsigned dec) {
    std::uint8_t* s = b.data() + i * 32;
    std::strncpy(reinterpret_cast<char*>(s), name, 10);
    s[11] = static_cast<std::uint8_t>(type);
    s[16] = static_cast<std::uint8_t>(len & 0xFF);
    s[17] = static_cast<std::uint8_t>(type == 'C' ? (len >> 8) : dec);
}
} // namespace

TEST_CASE("wide C descriptor parses to full width and offsets cascade") {
    std::vector<std::uint8_t> b(8 * 32 + 1, 0);
    put(b, 0, "A",   'C', 10, 0);
    put(b, 1, "FD2", 'C', 300, 0);   // 0x012C: byte16=0x2C byte17=0x01
    put(b, 2, "B",   'N', 12, 2);    // decimals must stay decimals
    put(b, 3, "FD3", 'C', 266, 0);   // 0x010A
    put(b, 4, "D",   'C', 255, 0);
    put(b, 5, "E",   'C', 1, 0);
    put(b, 6, "FD4", 'C', 1024, 0);  // low byte 0, high byte 4
    put(b, 7, "F",   'C', 2, 0);
    b.back() = 0x0D;
    auto p = parse_dbf_fields(b.data(), b.size());
    REQUIRE(p.has_value());
    auto f = p.value();
    REQUIRE(f.size() == 8);
    CHECK(f[1].type == DbfFieldType::Character);
    CHECK(f[1].length == 300);
    CHECK(f[1].decimals == 0);
    CHECK(f[2].length == 12);
    CHECK(f[2].decimals == 2);
    CHECK(f[3].length == 266);
    CHECK(f[4].length == 255);
    CHECK(f[0].record_offset == 1);
    CHECK(f[1].record_offset == 11);
    CHECK(f[2].record_offset == 311);
    CHECK(f[3].record_offset == 323);
    CHECK(f[4].record_offset == 589);
    CHECK(f[5].record_offset == 844);
    CHECK(f[6].length == 1024);
    CHECK(f[6].record_offset == 845);
    CHECK(f[7].record_offset == 1869);
}

TEST_CASE("create + open + save + read of wide C fields via ADSCDX") {
    const auto dir = fs::temp_directory_path() / "openads_wide_char";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    UNSIGNED8 srv[260]{};
    std::memcpy(srv, dir.string().c_str(), dir.string().size());
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0,
                         &hConn) == AE_SUCCESS);
    UNSIGNED8 tbl[] = "wide.dbf";
    UNSIGNED8 flddef[] =
        "ID,Character,10;FD2,Character,300;QTY,Numeric,8,2;FD3,Character,266;"
        "FD4,Character,1024;TAIL,Character,5";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tbl, nullptr, ADS_CDX, ADS_ANSI, 0, 0, 0,
                           flddef, &hT) == AE_SUCCESS);
    REQUIRE(AdsCloseTable(hT) == AE_SUCCESS);

    // Descriptor bytes on disk.
    {
        std::ifstream in(dir / "wide.dbf", std::ios::binary);
        std::vector<std::uint8_t> d((std::istreambuf_iterator<char>(in)),
                                    std::istreambuf_iterator<char>());
        REQUIRE(d.size() > 32 + 6 * 32);
        const std::uint8_t* fd2 = d.data() + 32 + 1 * 32;
        CHECK(fd2[11] == 'C');
        CHECK(fd2[16] == 0x2C);
        CHECK(fd2[17] == 0x01);
        CHECK(fd2[12] == 11);                // displacement after C(10)
        CHECK(fd2[13] == 0);
        const std::uint8_t* qty = d.data() + 32 + 2 * 32;
        CHECK(qty[16] == 8);
        CHECK(qty[17] == 2);
        const std::uint8_t* fd4 = d.data() + 32 + 4 * 32;
        CHECK(fd4[16] == 0x00);
        CHECK(fd4[17] == 0x04);
        const std::uint8_t* fd3 = d.data() + 32 + 3 * 32;
        CHECK(fd3[16] == 0x0A);
        CHECK(fd3[17] == 0x01);
        const std::uint16_t reclen = d[10] | (d[11] << 8);
        CHECK(reclen == 1 + 10 + 300 + 8 + 266 + 1024 + 5);
    }

    const std::string w300(300, 'x');
    const std::string w1024 = std::string(1023, 'q') + "!";
    const std::string w266 = std::string(265, 'y') + "Z";
    REQUIRE(AdsOpenTable(hConn, tbl, tbl, ADS_CDX, 1, 1, 0, 1, &hT)
            == AE_SUCCESS);
    UNSIGNED32 flen = 0;
    REQUIRE(AdsGetFieldLength(hT, (UNSIGNED8*)"FD2", &flen) == AE_SUCCESS);
    CHECK(flen == 300);
    REQUIRE(AdsGetFieldLength(hT, (UNSIGNED8*)"FD3", &flen) == AE_SUCCESS);
    CHECK(flen == 266);
    REQUIRE(AdsAppendRecord(hT) == AE_SUCCESS);
    REQUIRE(AdsSetString(hT, (UNSIGNED8*)"ID", (UNSIGNED8*)"R1", 2)
            == AE_SUCCESS);
    REQUIRE(AdsSetString(hT, (UNSIGNED8*)"FD2", (UNSIGNED8*)w300.c_str(),
                         (UNSIGNED32)w300.size()) == AE_SUCCESS);
    REQUIRE(AdsSetString(hT, (UNSIGNED8*)"FD3", (UNSIGNED8*)w266.c_str(),
                         (UNSIGNED32)w266.size()) == AE_SUCCESS);
    REQUIRE(AdsSetString(hT, (UNSIGNED8*)"FD4", (UNSIGNED8*)w1024.c_str(),
                         (UNSIGNED32)w1024.size()) == AE_SUCCESS);
    REQUIRE(AdsSetString(hT, (UNSIGNED8*)"TAIL", (UNSIGNED8*)"END", 3)
            == AE_SUCCESS);
    REQUIRE(AdsWriteRecord(hT) == AE_SUCCESS);
    REQUIRE(AdsCloseTable(hT) == AE_SUCCESS);

    // Reopen and read back.
    REQUIRE(AdsOpenTable(hConn, tbl, tbl, ADS_CDX, 1, 1, 0, 1, &hT)
            == AE_SUCCESS);
    REQUIRE(AdsGotoTop(hT) == AE_SUCCESS);
    auto get = [&](const char* n) {
        std::vector<UNSIGNED8> buf(2048, 0);
        UNSIGNED32 l = (UNSIGNED32)buf.size();
        REQUIRE(AdsGetField(hT, (UNSIGNED8*)n, buf.data(), &l, ADS_NONE)
                == AE_SUCCESS);
        return std::string(reinterpret_cast<char*>(buf.data()), l);
    };
    CHECK(get("FD2") == w300);
    CHECK(get("FD3") == w266);
    CHECK(get("FD4") == w1024);
    std::string tail = get("TAIL");
    CHECK(tail.substr(0, 3) == "END");
    std::string id = get("ID");
    CHECK(id.substr(0, 2) == "R1");
    REQUIRE(AdsCloseTable(hT) == AE_SUCCESS);
    AdsDisconnect(hConn);
    fs::remove_all(dir, ec);
}
