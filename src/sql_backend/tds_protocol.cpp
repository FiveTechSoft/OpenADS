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
// LOGIN7 ([MS-TDS] §2.2.6.4)
// ---------------------------------------------------------------------------
//
// Structure layout (offsets relative to LOGIN7 structure start, i.e. byte 0
// = the byte immediately after the 8-byte TDS packet header):
//
//   [0..3]   Length       (4, LE) — entire LOGIN7 structure size
//   [4..7]   TDSVersion   (4, LE) — 0x74000004 for TDS 7.4
//   [8..11]  PacketSize   (4, LE) — 0x1000 (4096)
//   [12..15] ClientProgVer(4)     — 0
//   [16..19] ClientPID    (4)     — 0
//   [20..23] ConnectionID (4)     — 0
//   [24]     OptionFlags1 (1)     — 0
//   [25]     OptionFlags2 (1)     — 0
//   [26]     TypeFlags    (1)     — 0
//   [27]     OptionFlags3 (1)     — 0
//   [28..31] ClientTimeZone(4)    — 0
//   [32..35] ClientLCID   (4)     — 0
//                                 = 36 bytes fixed header
//
// OffsetLength table (each pair = ibXxx/cchXxx, 2+2 bytes LE each):
//   [36..37] ibHostName   / [38..39] cchHostName
//   [40..41] ibUserName   / [42..43] cchUserName
//   [44..45] ibPassword   / [46..47] cchPassword
//   [48..49] ibAppName    / [50..51] cchAppName
//   [52..53] ibServerName / [54..55] cchServerName
//   [56..57] ibUnused     / [58..59] cbUnused      (always 0/0)
//   [60..61] ibCltIntName / [62..63] cchCltIntName (always 0/0)
//   [64..65] ibLanguage   / [66..67] cchLanguage   (always 0/0)
//   [68..69] ibDatabase   / [70..71] cchDatabase
//   [72..77] ClientID     (6 bytes, MAC address — all zeros)
//   [78..79] ibSSPI       / [80..81] cbSSPI        (always 0/0)
//   [82..83] ibAtchDBFile / [84..85] cchAtchDBFile (always 0/0)
//   [86..87] ibChangePassword / [88..89] cchChangePassword (always 0/0)
//   [90..93] cbSSPILong   (4, always 0)
//                                 = 58 bytes OffsetLength table
//
// Total fixed part = 36 + 58 = 94 bytes.
// Variable data (UCS-2LE strings) starts at LOGIN7 offset 94.
// ibXxx offsets in the table are relative to the START of the LOGIN7 structure.
// cchXxx is the number of UCS-2LE CHARACTER units (not bytes) — this includes
// cchPassword (a live SQL Server rejects the login with error 18456 if the
// password length is sent as the obfuscated byte count instead).

static void push_le16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

static void push_le32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

/// Append a UTF-8 string as UCS-2LE to |out| (ASCII-only, high byte = 0x00).
static void push_ucs2le(std::vector<uint8_t>& out, const std::string& s) {
    for (unsigned char c : s) {
        out.push_back(static_cast<uint8_t>(c));  // low byte
        out.push_back(0x00);                      // high byte (ASCII)
    }
}

std::vector<uint8_t> build_login7(const Login7Params& p) {
    // Obfuscate the password first (result is bytes, not chars).
    auto pw_obs = obfuscate_password(p.password);

    // Variable-data region starts at LOGIN7 structure offset 94.
    static constexpr size_t VARDATA_OFFSET = 94;

    // Build variable data and collect offsets/lengths.
    // Offsets are relative to LOGIN7 structure start.
    std::vector<uint8_t> var;
    // Helper lambda: append UCS-2LE string and return (offset, char-count).
    auto append_str = [&](const std::string& s) -> std::pair<uint16_t, uint16_t> {
        uint16_t offset = static_cast<uint16_t>(VARDATA_OFFSET + var.size());
        uint16_t cch    = static_cast<uint16_t>(s.size());  // char count (ASCII)
        push_ucs2le(var, s);
        return {offset, cch};
    };
    // Append obfuscated password bytes.  Like every other OffsetLength entry,
    // cchPassword is a CHARACTER count (UCS-2 units), NOT the byte count — the
    // obfuscated blob is 2 bytes per character.  (A live SQL Server rejects the
    // login with error 18456 if this is set to the byte count.)
    auto append_pw = [&]() -> std::pair<uint16_t, uint16_t> {
        uint16_t offset = static_cast<uint16_t>(VARDATA_OFFSET + var.size());
        uint16_t cch    = static_cast<uint16_t>(p.password.size());  // char count
        var.insert(var.end(), pw_obs.begin(), pw_obs.end());
        return {offset, cch};
    };

    auto [ibHostName,   cchHostName]   = append_str(p.hostname);
    auto [ibUserName,   cchUserName]   = append_str(p.username);
    auto [ibPassword,   cchPassword]   = append_pw();
    auto [ibAppName,    cchAppName]    = append_str(p.app_name);
    auto [ibServerName, cchServerName] = append_str(p.server_name);
    // Unused, CltIntName, Language: all zero (empty).
    auto [ibDatabase,   cchDatabase]   = append_str(p.database);

    // Total LOGIN7 structure size.
    uint32_t struct_len = static_cast<uint32_t>(VARDATA_OFFSET + var.size());
    // Total packet size (8-byte TDS header + LOGIN7 structure).
    uint16_t pkt_total  = static_cast<uint16_t>(8 + struct_len);

    std::vector<uint8_t> out;
    out.reserve(pkt_total);

    // --- 8-byte TDS packet header ---
    write_header(out, TDS_PKT_LOGIN7, TDS_STATUS_EOM, pkt_total);

    // --- Fixed LOGIN7 header (36 bytes) ---
    push_le32(out, struct_len);          // Length
    push_le32(out, 0x74000004u);         // TDSVersion (7.4)
    push_le32(out, 0x1000u);             // PacketSize = 4096
    push_le32(out, 0u);                  // ClientProgVer
    push_le32(out, 0u);                  // ClientPID
    push_le32(out, 0u);                  // ConnectionID
    out.push_back(0u);                   // OptionFlags1
    out.push_back(0u);                   // OptionFlags2
    out.push_back(0u);                   // TypeFlags
    out.push_back(0u);                   // OptionFlags3
    push_le32(out, 0u);                  // ClientTimeZone
    push_le32(out, 0u);                  // ClientLCID

    // --- OffsetLength table (58 bytes) ---
    push_le16(out, ibHostName);   push_le16(out, cchHostName);
    push_le16(out, ibUserName);   push_le16(out, cchUserName);
    push_le16(out, ibPassword);   push_le16(out, cchPassword);
    push_le16(out, ibAppName);    push_le16(out, cchAppName);
    push_le16(out, ibServerName); push_le16(out, cchServerName);
    push_le16(out, 0);            push_le16(out, 0);  // ibUnused/cbUnused
    push_le16(out, 0);            push_le16(out, 0);  // ibCltIntName/cchCltIntName
    push_le16(out, 0);            push_le16(out, 0);  // ibLanguage/cchLanguage
    push_le16(out, ibDatabase);   push_le16(out, cchDatabase);
    // ClientID: 6 bytes (MAC address, all zeros)
    for (int i = 0; i < 6; ++i) out.push_back(0);
    push_le16(out, 0);            push_le16(out, 0);  // ibSSPI/cbSSPI
    push_le16(out, 0);            push_le16(out, 0);  // ibAtchDBFile/cchAtchDBFile
    push_le16(out, 0);            push_le16(out, 0);  // ibChangePassword/cchChangePassword
    push_le32(out, 0u);                               // cbSSPILong

    // --- Variable data ---
    out.insert(out.end(), var.begin(), var.end());

    return out;
}

// ---------------------------------------------------------------------------
// Token length-class table ([MS-TDS] §2.2.4)
// ---------------------------------------------------------------------------

TokenLenClass token_length_class(uint8_t token, uint8_t& fixed_len) {
    switch (token) {
        // VarLenUShort — 2-byte LE length prefix, then body
        case 0xAA:  // ERROR
        case 0xAB:  // INFO
        case 0xAD:  // LOGINACK
        case 0xA9:  // ORDER
        case 0xE3:  // ENVCHANGE
            return TokenLenClass::VarLenUShort;

        // Done family — fixed 12-byte body (Status2+CurCmd2+RowCount8)
        case 0xFD:  // DONE
        case 0xFE:  // DONEPROC
        case 0xFF:  // DONEINPROC
            return TokenLenClass::Done;

        // FixedLength — body size set in fixed_len
        case 0x79:  // RETURNSTATUS: 4-byte signed integer
            fixed_len = 4;
            return TokenLenClass::FixedLength;

        // ColMetaDataDriven — structural; caller must parse COLMETADATA first
        case 0x81:  // COLMETADATA
        case 0xD1:  // ROW
        case 0xD2:  // NBCROW
            return TokenLenClass::ColMetaDataDriven;

        default:
            return TokenLenClass::Unknown;
    }
}

// ---------------------------------------------------------------------------
// Login-response token stream parser ([MS-TDS] §2.2.4 / §2.2.7)
// ---------------------------------------------------------------------------
//
// Variable-length tokens have a 2-byte LE length field after the token byte.
// The parser advances by that length to skip unknown/ignored tokens.
//
// Token IDs we care about:
//   0xAD LOGINACK  — authenticated; skip body
//   0xAA ERROR     — Number(4,LE) + State(1) + Class(1) + MsgText(US_VARCHAR)
//                    + ServerName(B_VARCHAR) + ProcName(B_VARCHAR) + Line(4,LE)
//   0xAB INFO      — same structure as ERROR; ignored
//   0xE3 ENVCHANGE — 2-byte LE length then body; ignored
//   0xFD DONE      — 12 bytes fixed (Status2+CurCmd2+RowCount8); stop

static std::string ucs2le_to_utf8(const uint8_t* p, uint16_t nchars) {
    // v1: ASCII-range only (high byte always 0x00 for ASCII); fallback = '?'.
    std::string out;
    out.reserve(nchars);
    for (uint16_t i = 0; i < nchars; ++i) {
        uint8_t lo = p[i * 2];
        // uint8_t hi = p[i * 2 + 1];  // ignored in ASCII-only v1
        out.push_back(lo < 0x80 ? static_cast<char>(lo) : '?');
    }
    return out;
}

LoginResult parse_login_response(const uint8_t* payload, size_t n) {
    LoginResult res;
    size_t pos = 0;

    while (pos < n) {
        uint8_t token = payload[pos];
        ++pos;

        if (token == 0xFD) {
            // DONE: 12 fixed bytes (Status(2)+CurCmd(2)+RowCount(8)); stop.
            break;
        }

        if (token == 0xAD) {
            // LOGINACK: Length(2,LE) then body — just skip the body.
            if (pos + 2 > n) break;
            uint16_t len = static_cast<uint16_t>(payload[pos] | (payload[pos+1] << 8));
            pos += 2;
            if (pos + len > n) break;
            pos += len;
            res.authenticated = true;
            continue;
        }

        if (token == 0xAA || token == 0xAB) {
            // ERROR (0xAA) or INFO (0xAB): Length(2,LE) then structured body.
            if (pos + 2 > n) break;
            uint16_t len = static_cast<uint16_t>(payload[pos] | (payload[pos+1] << 8));
            pos += 2;
            if (pos + len > n) break;
            const uint8_t* body = payload + pos;
            size_t body_len = len;
            pos += len;

            if (token == 0xAA && body_len >= 4) {
                // Number (4, LE)
                res.error_number = static_cast<uint32_t>(body[0])
                                 | (static_cast<uint32_t>(body[1]) << 8)
                                 | (static_cast<uint32_t>(body[2]) << 16)
                                 | (static_cast<uint32_t>(body[3]) << 24);
                // State(1) + Class(1) = 2 bytes at body[4..5].
                // MsgText: US_VARCHAR at body[6]: len(2,LE chars) + UCS-2LE.
                if (body_len >= 8) {
                    uint16_t mlen = static_cast<uint16_t>(body[6] | (body[7] << 8));
                    if (body_len >= static_cast<size_t>(8 + mlen * 2)) {
                        res.message = ucs2le_to_utf8(body + 8, mlen);
                    }
                }
            }
            continue;
        }

        if (token == 0xE3) {
            // ENVCHANGE: Length(2,LE) then body; ignored.
            if (pos + 2 > n) break;
            uint16_t len = static_cast<uint16_t>(payload[pos] | (payload[pos+1] << 8));
            pos += 2;
            if (pos + len > n) break;
            pos += len;
            continue;
        }

        // Unknown token — consult the length-class table instead of blindly
        // assuming a 2-byte LE length (the previous heuristic).
        uint8_t fixed_len = 0;
        auto lc = token_length_class(token, fixed_len);
        if (lc == TokenLenClass::VarLenUShort) {
            if (pos + 2 > n) break;
            uint16_t len = static_cast<uint16_t>(payload[pos] | (payload[pos+1] << 8));
            pos += 2;
            if (pos + len > n) break;
            pos += len;
        } else if (lc == TokenLenClass::Done) {
            break;  // stop (e.g. DONEPROC 0xFE / DONEINPROC 0xFF)
        } else if (lc == TokenLenClass::FixedLength) {
            if (pos + fixed_len > n) break;
            pos += fixed_len;
        } else {
            // Unknown or ColMetaDataDriven — cannot advance safely; stop.
            break;
        }
    }

    return res;
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

// ---------------------------------------------------------------------------
// SQL_BATCH ([MS-TDS] §2.2.6.7)
// ---------------------------------------------------------------------------

std::vector<uint8_t> build_sql_batch(const std::string& utf8_sql) {
    std::vector<uint8_t> out;
    auto put_u32le = [&out](uint32_t v) {
        out.push_back(uint8_t(v & 0xFF));        out.push_back(uint8_t((v >> 8) & 0xFF));
        out.push_back(uint8_t((v >> 16) & 0xFF)); out.push_back(uint8_t((v >> 24) & 0xFF));
    };
    // ALL_HEADERS (§2.2.5): TotalLength then one Transaction Descriptor header.
    put_u32le(4 + 18);          // TotalLength = its own 4 bytes + the 18-byte header
    put_u32le(18);              // HeaderLength
    out.push_back(0x02); out.push_back(0x00);    // HeaderType = 2 (txn descriptor)
    for (int i = 0; i < 8; ++i) out.push_back(0);// TransactionDescriptor = 0
    put_u32le(1);               // OutstandingRequestCount = 1
    push_ucs2le(out, utf8_sql); // SQL text, UCS-2LE
    return out;
}

}  // namespace openads::sql_backend::tds
#endif  // defined(OPENADS_WITH_MSSQL)
