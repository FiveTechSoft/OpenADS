#include "sql_backend/tds_protocol.h"
#if defined(OPENADS_WITH_MSSQL)
namespace openads::sql_backend::tds {

void write_header(std::vector<uint8_t>& out, uint8_t type, uint8_t status,
                  uint16_t total_len) {
    out.push_back(type);
    out.push_back(status);
    out.push_back(static_cast<uint8_t>((total_len >> 8) & 0xFF));  // big-endian high
    out.push_back(static_cast<uint8_t>(total_len & 0xFF));         // big-endian low
    out.push_back(0); out.push_back(0);   // SPID (client->server: 0)
    out.push_back(0);                     // PacketID
    out.push_back(0);                     // Window
}

bool read_header(const uint8_t* buf, size_t n, TdsPacketHeader& h) {
    if (n < 8) return false;
    h.type      = buf[0];
    h.status    = buf[1];
    h.length    = static_cast<uint16_t>((buf[2] << 8) | buf[3]);
    h.spid      = static_cast<uint16_t>((buf[4] << 8) | buf[5]);
    h.packet_id = buf[6];
    h.window    = buf[7];
    return true;
}

std::vector<uint8_t> obfuscate_password(const std::string& pw) {
    // v1: ASCII passwords only — UCS-2LE high byte is 0x00 for all code points.
    std::vector<uint8_t> out;
    out.reserve(pw.size() * 2);
    for (unsigned char c : pw) {
        // UCS-2LE encoding: low byte = character value, high byte = 0.
        for (uint8_t b : {static_cast<uint8_t>(c), uint8_t{0}}) {
            uint8_t swapped = static_cast<uint8_t>((b >> 4) | (b << 4));
            out.push_back(static_cast<uint8_t>(swapped ^ 0xA5));
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// PRELOGIN ([MS-TDS] §2.2.6.5)
// ---------------------------------------------------------------------------
//
// Message body layout (all offsets measured from byte 0 of the body, i.e.
// the byte immediately after the 8-byte packet header):
//
//   [0]  VERSION  entry : 0x00  off_hi off_lo  0x00  0x06   (5 bytes)
//   [5]  ENCRYPT  entry : 0x01  off_hi off_lo  0x00  0x01   (5 bytes)
//   [10] Terminator     : 0xFF                               (1 byte)
//                                              ──── table = 11 bytes
//   [11..16] VERSION data  : 0x00 0x00 0x00 0x00  0x00 0x00 (6 bytes)
//   [17]     ENCRYPTION data: 0x01  (ENCRYPT_ON)             (1 byte)
//                                              ──── body = 18 bytes
//
// Offsets: VERSION = 11, ENCRYPTION = 17.
// Total packet length = 8 + 18 = 26.

std::vector<uint8_t> build_prelogin() {
    // Token constants
    static constexpr uint8_t PRELOGIN_TOKEN_VERSION    = 0x00;
    static constexpr uint8_t PRELOGIN_TOKEN_ENCRYPTION = 0x01;
    static constexpr uint8_t PRELOGIN_TOKEN_TERMINATOR = 0xFF;

    // Data sizes
    static constexpr uint16_t VERSION_DATA_LEN    = 6;
    static constexpr uint16_t ENCRYPTION_DATA_LEN = 1;

    // Option-table size: 2 entries × 5 bytes + 1 terminator = 11 bytes.
    static constexpr uint16_t TABLE_SIZE = 5 + 5 + 1;  // 11

    // Data offsets from body byte 0 (= right after packet header).
    static constexpr uint16_t VERSION_OFFSET    = TABLE_SIZE;                    // 11
    static constexpr uint16_t ENCRYPTION_OFFSET = TABLE_SIZE + VERSION_DATA_LEN; // 17

    // Total packet length: 8-byte header + 18-byte body.
    static constexpr uint16_t TOTAL_LEN = 8 + TABLE_SIZE + VERSION_DATA_LEN + ENCRYPTION_DATA_LEN; // 26

    std::vector<uint8_t> out;
    out.reserve(TOTAL_LEN);

    // 8-byte TDS packet header.
    write_header(out, TDS_PKT_PRELOGIN, TDS_STATUS_EOM, TOTAL_LEN);

    // --- Option table ---

    // VERSION entry (token 0x00, offset BE, length BE)
    out.push_back(PRELOGIN_TOKEN_VERSION);
    out.push_back(static_cast<uint8_t>(VERSION_OFFSET >> 8));
    out.push_back(static_cast<uint8_t>(VERSION_OFFSET & 0xFF));
    out.push_back(static_cast<uint8_t>(VERSION_DATA_LEN >> 8));
    out.push_back(static_cast<uint8_t>(VERSION_DATA_LEN & 0xFF));

    // ENCRYPTION entry (token 0x01, offset BE, length BE)
    out.push_back(PRELOGIN_TOKEN_ENCRYPTION);
    out.push_back(static_cast<uint8_t>(ENCRYPTION_OFFSET >> 8));
    out.push_back(static_cast<uint8_t>(ENCRYPTION_OFFSET & 0xFF));
    out.push_back(static_cast<uint8_t>(ENCRYPTION_DATA_LEN >> 8));
    out.push_back(static_cast<uint8_t>(ENCRYPTION_DATA_LEN & 0xFF));

    // Terminator
    out.push_back(PRELOGIN_TOKEN_TERMINATOR);

    // --- Data region ---

    // VERSION data: 4-byte version (all zeros for client) + 2-byte sub-build.
    out.push_back(0x00); out.push_back(0x00);
    out.push_back(0x00); out.push_back(0x00);
    out.push_back(0x00); out.push_back(0x00);

    // ENCRYPTION data: ENCRYPT_ON = 0x01.
    out.push_back(static_cast<uint8_t>(PreloginEncryption::On));

    return out;
}

bool parse_prelogin_response(const uint8_t* payload, size_t n,
                             PreloginEncryption& enc) {
    // Walk the option table.  Each entry: token(1), offset(2 BE), length(2 BE).
    // Terminator: 0xFF.  Offsets are from byte 0 of |payload|.
    static constexpr uint8_t TOKEN_ENCRYPTION = 0x01;
    static constexpr uint8_t TOKEN_TERMINATOR = 0xFF;
    static constexpr size_t  ENTRY_SIZE       = 5;

    size_t i = 0;
    while (i < n) {
        uint8_t token = payload[i];
        if (token == TOKEN_TERMINATOR) {
            break;
        }
        // Need 4 more bytes for offset + length.
        if (i + ENTRY_SIZE > n) return false;

        uint16_t offset = static_cast<uint16_t>((payload[i + 1] << 8) | payload[i + 2]);
        uint16_t length = static_cast<uint16_t>((payload[i + 3] << 8) | payload[i + 4]);

        if (token == TOKEN_ENCRYPTION) {
            if (length < 1 || static_cast<size_t>(offset) >= n) return false;
            enc = static_cast<PreloginEncryption>(payload[offset]);
            return true;
        }
        i += ENTRY_SIZE;
    }
    // ENCRYPTION option not found.
    return false;
}

}  // namespace openads::sql_backend::tds
#endif  // defined(OPENADS_WITH_MSSQL)
