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

TEST_CASE("prelogin request is a valid 0x12 message advertising encryption") {
    auto m = build_prelogin();
    REQUIRE(m.size() > 8);
    CHECK(m[0] == 0x12);                 // PRELOGIN packet type
    CHECK((m[1] & 0x01) == 0x01);        // EOM
    // The option table starts at byte 8; first option token is VERSION (0x00),
    // and a terminator token (0xFF) ends the table. Assert the table is
    // terminated and an ENCRYPTION option (token 0x01) is present.
    bool has_enc = false, terminated = false;
    for (size_t i = 8; i + 1 < m.size(); ++i) {
        if (m[i] == 0xFF) { terminated = true; break; }
        if (m[i] == 0x01) has_enc = true;
    }
    CHECK(has_enc);
    CHECK(terminated);
}

TEST_CASE("parse_prelogin_response reads the ENCRYPTION option") {
    // Minimal server PRELOGIN response payload: ENCRYPTION option (token 0x01)
    // at offset, value ENCRYPT_ON(0x01), terminated by 0xFF.
    // Option table entry: token(1) offset(2,BE) length(2,BE); then 0xFF; then data.
    std::vector<uint8_t> p = {
        0x01, 0x00, 0x06, 0x00, 0x01,   // ENCRYPTION @ offset 6, len 1
        0xFF,                           // terminator
        0x01                            // data: ENCRYPT_ON
    };
    PreloginEncryption enc{};
    REQUIRE(parse_prelogin_response(p.data(), p.size(), enc));
    CHECK(enc == PreloginEncryption::On);
}
#endif
