#include "doctest.h"
#if defined(OPENADS_WITH_MSSQL)
#include "sql_backend/tds_protocol.h"
using namespace openads::sql_backend::tds;

TEST_CASE("tds header write/read round-trip, length is big-endian incl header") {
    std::vector<uint8_t> out;
    write_header(out, TDS_PKT_PRELOGIN, TDS_STATUS_EOM, 8 + 5);
    REQUIRE(out.size() == 8);
    CHECK(out[0] == 0x12);
    CHECK(out[1] == 0x01);
    CHECK(out[2] == 0x00);          // length high byte (big-endian)
    CHECK(out[3] == 0x0D);          // length low byte = 13
    TdsPacketHeader h{};
    REQUIRE(read_header(out.data(), out.size(), h));
    CHECK(h.type == 0x12);
    CHECK(h.status == 0x01);
    CHECK(h.length == 13);
}

TEST_CASE("tds password obfuscation: swap nibbles then XOR 0xA5 over UCS-2LE") {
    // "abc" -> UCS-2LE bytes 61 00 62 00 63 00 ; each byte: swap nibbles, ^0xA5.
    //   0x61 -> swap 0x16 -> ^0xA5 = 0xB3 ;  0x00 -> swap 0x00 -> ^0xA5 = 0xA5
    //   0x62 -> swap 0x26 -> ^0xA5 = 0x83 ;  0x63 -> swap 0x36 -> ^0xA5 = 0x93
    auto o = obfuscate_password("abc");
    REQUIRE(o.size() == 6);
    CHECK(o[0] == 0xB3);  CHECK(o[1] == 0xA5);
    CHECK(o[2] == 0x83);  CHECK(o[3] == 0xA5);
    CHECK(o[4] == 0x93);  CHECK(o[5] == 0xA5);
}
#endif
