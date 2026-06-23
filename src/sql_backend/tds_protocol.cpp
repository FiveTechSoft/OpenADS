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

}  // namespace openads::sql_backend::tds
#endif  // defined(OPENADS_WITH_MSSQL)
