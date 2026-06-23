#pragma once
// Pure TDS (MS-TDS 7.4) byte layer: no sockets, no TLS. Built/parsed buffers
// only. See [MS-TDS] for all field layouts.
#include <cstdint>
#include <string>
#include <vector>

namespace openads::sql_backend::tds {

#if defined(OPENADS_WITH_MSSQL)

// ---------------------------------------------------------------------------
// Packet-type constants ([MS-TDS] 2.2.3.1.1)
// ---------------------------------------------------------------------------
static constexpr uint8_t TDS_PKT_SQLBATCH  = 0x01;
static constexpr uint8_t TDS_PKT_LOGIN7    = 0x10;
static constexpr uint8_t TDS_PKT_PRELOGIN  = 0x12;
static constexpr uint8_t TDS_PKT_REPLY     = 0x04;

// Packet-status constants ([MS-TDS] 2.2.3.1.2)
static constexpr uint8_t TDS_STATUS_EOM    = 0x01;  // End Of Message

// ---------------------------------------------------------------------------
// Packet header structure ([MS-TDS] 2.2.3.1)
// 8 bytes, all fields big-endian on the wire.
// ---------------------------------------------------------------------------
struct TdsPacketHeader {
    uint8_t  type;       // packet type
    uint8_t  status;     // status flags
    uint16_t length;     // total packet length (header + payload), big-endian
    uint16_t spid;       // server process ID (client->server: 0)
    uint8_t  packet_id;  // rolling packet counter
    uint8_t  window;     // unused (always 0)
};

// ---------------------------------------------------------------------------
// Header serialisation helpers
// ---------------------------------------------------------------------------

/// Append an 8-byte TDS packet header to |out|.
/// |total_len| is the TOTAL packet length (header + payload), big-endian on wire.
void write_header(std::vector<uint8_t>& out, uint8_t type, uint8_t status,
                  uint16_t total_len);

/// Parse an 8-byte TDS packet header from |buf| (must have n >= 8 bytes).
/// Returns false if n < 8.
bool read_header(const uint8_t* buf, size_t n, TdsPacketHeader& h);

// ---------------------------------------------------------------------------
// Password obfuscation ([MS-TDS] 2.2.6.4 LOGIN7 Password field)
// Algorithm: encode each code unit as UCS-2LE, then per byte:
//   swapped = (b >> 4) | (b << 4)   (nibble swap)
//   out_byte = swapped ^ 0xA5
// v1 limitation: ASCII passwords only (high UCS-2LE byte is always 0x00).
// ---------------------------------------------------------------------------
std::vector<uint8_t> obfuscate_password(const std::string& utf8_password);

// ---------------------------------------------------------------------------
// PRELOGIN ([MS-TDS] §2.2.6.5)
// ---------------------------------------------------------------------------

/// Encryption negotiation values for the PRELOGIN / PRELOGIN_RESPONSE exchange.
enum class PreloginEncryption : uint8_t {
    Off    = 0,
    On     = 1,
    NotSup = 2,
    Req    = 3,
};

/// Build a complete PRELOGIN packet (type=0x12, EOM) advertising
/// VERSION and ENCRYPTION=ENCRYPT_ON per [MS-TDS] §2.2.6.5.
/// Option table layout:
///   { token(1), offset(2 BE), length(2 BE) } per entry, terminated by 0xFF.
/// Offsets are measured from the start of the message body (byte right after
/// the 8-byte packet header).
std::vector<uint8_t> build_prelogin();

/// Parse a server PRELOGIN response payload (bytes AFTER the 8-byte header).
/// Walks the option table, finds token 0x01 (ENCRYPTION), reads its 1-byte
/// value into |enc|.  Returns false if the option is absent or the payload
/// is malformed.
bool parse_prelogin_response(const uint8_t* payload, size_t n,
                             PreloginEncryption& enc);

#endif  // defined(OPENADS_WITH_MSSQL)

}  // namespace openads::sql_backend::tds
