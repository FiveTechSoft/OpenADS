#include "doctest.h"
#if defined(OPENADS_WITH_MSSQL)
#include "sql_backend/tds_protocol.h"
#include <algorithm>
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

// ---------------------------------------------------------------------------
// Task 5: LOGIN7 build + login-response token parse
// ---------------------------------------------------------------------------

TEST_CASE("login7 has TDS 7.4 version and obfuscated password at its offset") {
    Login7Params p;
    p.hostname="h"; p.username="sa"; p.password="abc";
    p.app_name="openads"; p.server_name="srv"; p.database="db";
    auto m = build_login7(p);
    REQUIRE(m.size() > 8 + 36);          // header + fixed LOGIN7 prefix
    CHECK(m[0] == 0x10);                  // LOGIN7 packet type
    // TDSVersion field (LOGIN7 offset 4..7, little-endian 0x74000004).
    // LOGIN7 structure starts at byte 8 (after the 8-byte TDS header).
    size_t base = 8;
    CHECK(m[base+4] == 0x04);   // TDSVersion LE byte 0
    CHECK(m[base+7] == 0x74);   // TDSVersion LE byte 3
    // The obfuscated bytes of "abc" (0xB3 0xA5 0x83 0xA5 0x93 0xA5) appear once.
    std::vector<uint8_t> needle = {0xB3,0xA5,0x83,0xA5,0x93,0xA5};
    bool found = std::search(m.begin(), m.end(), needle.begin(), needle.end()) != m.end();
    CHECK(found);  // obfuscated "abc" needle is present in the LOGIN7 packet
}

TEST_CASE("parse_login_response: LOGINACK+DONE = authenticated") {
    // LOGINACK token 0xAD per [MS-TDS] §2.2.7.6:
    //   Token(1) + Length(2,LE) + Interface(1) + TDSVersion(4) +
    //   ProgName(B_VARCHAR: 1-byte len + UCS-2LE) + ProgVersion(4)
    // Body = Interface(1)+TDSVersion(4)+ProgName-len(1)+ProgVersion(4) = 10 bytes.
    // DONE token 0xFD per §2.2.7.8: Status(2)+CurCmd(2)+RowCount(8) = 12 bytes.
    std::vector<uint8_t> s = {
        // LOGINACK
        0xAD, 0x0A,0x00,                        // token, length=10
        0x01,                                   // Interface (SQL_TSQL)
        0x04,0x00,0x00,0x74,                    // TDSVersion LE (0x74000004)
        0x00,                                   // ProgName B_VARCHAR len=0
        0x00,0x00,0x00,0x00,                    // ProgVersion
        // DONE
        0xFD, 0x00,0x00, 0x00,0x00,             // token, Status, CurCmd
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 // RowCount
    };
    auto r = parse_login_response(s.data(), s.size());
    CHECK(r.authenticated == true);
}

TEST_CASE("parse_login_response: ERROR token = not authenticated with number") {
    // ERROR token 0xAA per [MS-TDS] §2.2.7.10:
    //   Token(1) + Length(2,LE) +
    //   Number(4,LE) + State(1) + Class(1) +
    //   MsgText(US_VARCHAR: 2-byte LE char-count + UCS-2LE) +
    //   ServerName(B_VARCHAR: 1-byte char-count + UCS-2LE) +
    //   ProcName(B_VARCHAR: 1-byte char-count + UCS-2LE) +
    //   LineNumber(4,LE)
    // With all strings empty (zero lengths):
    //   Number(4)+State(1)+Class(1)+MsgLen(2)+ServerLen(1)+ProcLen(1)+Line(4) = 14 bytes
    // 18456 decimal = 0x4818; LE4 = 0x18,0x48,0x00,0x00
    std::vector<uint8_t> s = {
        // ERROR
        0xAA, 0x0E,0x00,                        // token, length=14
        0x18,0x48,0x00,0x00,                    // Number=18456 (login failed)
        0x01, 0x0E,                             // State, Class
        0x00,0x00,                              // MsgText US_VARCHAR len=0 chars
        0x00,                                   // ServerName B_VARCHAR len=0
        0x00,                                   // ProcName B_VARCHAR len=0
        0x01,0x00,0x00,0x00,                    // LineNumber=1
        // DONE
        0xFD, 0x02,0x00, 0x00,0x00,             // token, Status=error, CurCmd
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 // RowCount
    };
    auto r = parse_login_response(s.data(), s.size());
    CHECK(r.authenticated == false);
    CHECK(r.error_number == 18456);
}

// ---------------------------------------------------------------------------
// Task 2: token_length_class table + parse_login_response refactor
// ---------------------------------------------------------------------------

TEST_CASE("token_length_class classifies the control tokens we skip") {
    uint8_t fx = 0;
    CHECK(token_length_class(0xAA, fx) == TokenLenClass::VarLenUShort); // ERROR
    CHECK(token_length_class(0xAB, fx) == TokenLenClass::VarLenUShort); // INFO
    CHECK(token_length_class(0xAD, fx) == TokenLenClass::VarLenUShort); // LOGINACK
    CHECK(token_length_class(0xE3, fx) == TokenLenClass::VarLenUShort); // ENVCHANGE
    CHECK(token_length_class(0xA9, fx) == TokenLenClass::VarLenUShort); // ORDER
    CHECK(token_length_class(0xFD, fx) == TokenLenClass::Done);         // DONE
    CHECK(token_length_class(0x79, fx) == TokenLenClass::FixedLength);  // RETURNSTATUS (4)
    CHECK((token_length_class(0x79, fx) == TokenLenClass::FixedLength && fx == 4));
    CHECK(token_length_class(0x81, fx) == TokenLenClass::ColMetaDataDriven); // COLMETADATA
    CHECK(token_length_class(0xD1, fx) == TokenLenClass::ColMetaDataDriven); // ROW
    CHECK(token_length_class(0xD2, fx) == TokenLenClass::ColMetaDataDriven); // NBCROW
}
TEST_CASE("parse_login_response still authenticates after refactor (regression)") {
    // Reuse the exact LOGINACK+DONE vector from the connect tests.
    std::vector<uint8_t> s = {
        0xAD, 0x06,0x00, 0x07, 0x74,0x00,0x00,0x04, 0x00,
        0xFD, 0x00,0x00, 0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
    };
    auto r = parse_login_response(s.data(), s.size());
    CHECK(r.authenticated == true);
}

// ---------------------------------------------------------------------------
// Task 1: SQL_BATCH builder (pure)
// ---------------------------------------------------------------------------

TEST_CASE("build_sql_batch: ALL_HEADERS prefix + UCS-2LE text") {
    auto m = build_sql_batch("SELECT 1");
    // ALL_HEADERS: TotalLength(4) = 4 + 18 = 22 (0x16); then one 18-byte header.
    REQUIRE(m.size() == 22 + 8 * 2);            // 22-byte ALL_HEADERS + "SELECT 1" UCS-2LE
    CHECK(m[0] == 0x16); CHECK(m[1] == 0x00); CHECK(m[2] == 0x00); CHECK(m[3] == 0x00);
    CHECK(m[4] == 0x12); CHECK(m[5] == 0x00); CHECK(m[6] == 0x00); CHECK(m[7] == 0x00); // HeaderLength=18
    CHECK(m[8] == 0x02); CHECK(m[9] == 0x00);   // HeaderType = 0x0002 (txn descriptor)
    // OutstandingRequestCount = 1 at bytes 18..21
    CHECK(m[18] == 0x01); CHECK(m[19] == 0x00); CHECK(m[20] == 0x00); CHECK(m[21] == 0x00);
    // SQL text begins at byte 22: 'S' 0x00 'E' 0x00 ...
    CHECK(m[22] == 'S'); CHECK(m[23] == 0x00);
    CHECK(m[24] == 'E'); CHECK(m[25] == 0x00);
}


// ---------------------------------------------------------------------------
// Task 3: parse_colmetadata — column descriptor parsing (pure)
// ---------------------------------------------------------------------------

TEST_CASE("parse_colmetadata: int + nvarchar columns") {
    // 2 columns: [INTN maxlen 4] "id", [NVARCHAR maxlen 100 chars = 200 bytes, collation] "nome".
    // Per [MS-TDS] §2.2.5.5:
    //   INTN(0x26): 1-byte max-len (0x04)
    //   NVARCHAR(0xE7): 2-byte LE max-len in BYTES (100 chars × 2 = 200 = 0xC8,0x00)
    //                   + 5-byte COLLATION
    //   ColName: B_VARCHAR = 1-byte char-count + UCS-2LE chars
    std::vector<uint8_t> p = {
        0x02,0x00,                              // Count = 2
        // col 1: INTN(4) named "id"
        0x00,0x00,0x00,0x00,                   // UserType (4 LE)
        0x00,0x00,                              // Flags (2 LE)
        0x26,                                   // type token = INTN
        0x04,                                   // max-len = 4
        0x02,                                   // ColName len = 2 chars
        'i',0x00,'d',0x00,                      // "id" UCS-2LE
        // col 2: NVARCHAR(100 chars) named "nome"
        0x00,0x00,0x00,0x00,                   // UserType (4 LE)
        0x00,0x00,                              // Flags (2 LE)
        0xE7,                                   // type token = NVARCHAR
        0xC8,0x00,                              // max-len = 200 bytes (100 chars × 2), LE
        0x09,0x04,0xD0,0x00,0x34,              // 5-byte COLLATION (LCID/flags/sortid)
        0x04,                                   // ColName len = 4 chars
        'n',0x00,'o',0x00,'m',0x00,'e',0x00,   // "nome" UCS-2LE
    };
    std::vector<TdsColumn> cols;
    size_t pos = 0;
    REQUIRE(parse_colmetadata(p.data(), p.size(), pos, cols));
    REQUIRE(cols.size() == 2);
    CHECK(cols[0].name == "id");
    CHECK(cols[0].type_token == 0x26);
    CHECK(cols[0].length == 4);
    CHECK(cols[1].name == "nome");
    CHECK(cols[1].type_token == 0xE7);
    CHECK(cols[1].length == 200);
    CHECK(pos == p.size());
}

TEST_CASE("parse_colmetadata: unsupported type fails closed") {
    // GUIDTYPE 0x24 is not in our supported set → must return false.
    // Count=1, one column with type 0x24, followed by a length byte and name.
    std::vector<uint8_t> p = {
        0x01,0x00,                              // Count = 1
        0x00,0x00,0x00,0x00,                   // UserType
        0x00,0x00,                              // Flags
        0x24,                                   // GUIDTYPE = unsupported
        0x10,                                   // (would-be length byte, but we return false first)
        0x01,                                   // ColName len = 1
        'g',0x00                               // "g" UCS-2LE
    };
    std::vector<TdsColumn> cols;
    size_t pos = 0;
    CHECK(parse_colmetadata(p.data(), p.size(), pos, cols) == false);
}

// ---------------------------------------------------------------------------
// Task 4: decode_cell — column value bytes to printable string
// ---------------------------------------------------------------------------

static TdsColumn col(uint8_t t, uint8_t scale=0){ TdsColumn c; c.type_token=t; c.scale=scale; return c; }

TEST_CASE("decode int / bit / nvarchar") {
    uint8_t i4[] = {0x2A,0x00,0x00,0x00};                 // 42 LE
    CHECK(decode_cell(col(0x26), i4, 4) == "42");          // INTN(4)
    uint8_t b[] = {0x01};
    CHECK(decode_cell(col(0x68), b, 1) == "1");            // BITN
    uint8_t nv[] = {'n',0x00,'o',0x00};                    // "no" UCS-2LE
    CHECK(decode_cell(col(0xE7), nv, 4) == "no");          // NVARCHAR
}
TEST_CASE("decode decimal scale and datetime epoch") {
    // NUMERIC(10,2) value 123.45 -> sign(1)=positive + magnitude 12345 LE.
    uint8_t dec[] = {0x01, 0x39,0x30,0x00,0x00};           // 1=positive, 12345 LE
    CHECK(decode_cell(col(0x6C, /*scale*/2), dec, 5) == "123.45");
    // DATETIME = 1900-01-02 00:00:00 -> days=1, ticks=0.
    uint8_t dt[] = {0x01,0x00,0x00,0x00, 0x00,0x00,0x00,0x00};
    CHECK(decode_cell(col(0x3D), dt, 8) == "1900-01-02 00:00:00");
}

#endif
