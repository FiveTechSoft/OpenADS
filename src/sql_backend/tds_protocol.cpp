#include "sql_backend/tds_protocol.h"
#if defined(OPENADS_WITH_MSSQL)
#include <algorithm>
#include <cstdio>
#include <cstring>
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

// ---------------------------------------------------------------------------
// COLMETADATA ([MS-TDS] §2.2.7.4 / TYPE_INFO §2.2.5.4–5.5)
// ---------------------------------------------------------------------------
//
// Token body layout (pos points at byte immediately after the 0x81 token):
//   Count         (2, LE)     — number of columns; 0xFFFF = no metadata
//   For each column:
//     UserType    (4, LE)
//     Flags       (2, LE)
//     TYPE_INFO   — type-token (1) then 0..N extra bytes per §2.2.5.5
//     ColName     — B_VARCHAR: len(1 byte char-count) + UCS-2LE chars
//
// TYPE_INFO byte counts by token (see §2.2.5.4–5.5):
//   Fixed-length tokens (no extra bytes):
//     INT1  0x30, INT2  0x34, INT4  0x38, INT8  0x7F, BIT   0x32,
//     DATETIM4 0x3A, DATETIME 0x3D, MONEY4 0x7A, MONEY 0x3C,
//     FLT4  0x3B, FLT8  0x3E
//   *N variable (1-byte max-len):
//     INTN 0x26, BITN 0x68, MONEYN 0x6E, FLTN 0x6D, DATETIMN 0x6F
//   Special:
//     DATEN 0x28:      no extra bytes (date type, no len/scale)
//     DATETIME2N 0x2A: 1-byte scale
//   Decimal/Numeric (3 bytes: max-len, precision, scale):
//     DECIMALN 0x6A, NUMERICN 0x6C
//   Char/Nchar/Varchar/Nvarchar (2-byte LE max-len + 5-byte COLLATION):
//     BIGCHAR 0xAF, BIGVARCHR 0xA7, NCHAR 0xEF, NVARCHAR 0xE7
//   All other tokens → unsupported, return false.

/// Helper: read a single byte from [p+pos] with bounds check.
static bool read_u8(const uint8_t* p, size_t n, size_t& pos, uint8_t& out) {
    if (pos >= n) return false;
    out = p[pos++];
    return true;
}

/// Helper: read 2-byte LE uint16 with bounds check.
static bool read_u16le(const uint8_t* p, size_t n, size_t& pos, uint16_t& out) {
    if (pos + 2 > n) return false;
    out = static_cast<uint16_t>(p[pos] | (uint16_t(p[pos+1]) << 8));
    pos += 2;
    return true;
}

/// Helper: skip |nbytes| bytes with bounds check.
static bool skip_bytes(size_t n, size_t& pos, size_t nbytes) {
    if (pos + nbytes > n) return false;
    pos += nbytes;
    return true;
}

/// Consume the TYPE_INFO bytes for |type_token|, filling |col|.
/// Returns false on unsupported token or short read.
static bool read_type_info(const uint8_t* p, size_t n, size_t& pos,
                           uint8_t type_token, TdsColumn& col) {
    col.type_token = type_token;
    switch (type_token) {
        // ---- Fixed-length: no extra TYPE_INFO bytes ----
        case 0x30:  // INT1
        case 0x34:  // INT2
        case 0x38:  // INT4
        case 0x7F:  // INT8
        case 0x32:  // BIT
        case 0x3A:  // DATETIM4
        case 0x3D:  // DATETIME
        case 0x7A:  // MONEY4
        case 0x3C:  // MONEY
        case 0x3B:  // FLT4
        case 0x3E:  // FLT8
            // No extra bytes; length is implicit (could be set per type if needed).
            return true;

        // ---- *N variable: 1-byte max-len ----
        case 0x26:  // INTN
        case 0x68:  // BITN
        case 0x6E:  // MONEYN
        case 0x6D:  // FLTN
        case 0x6F:  // DATETIMN
        {
            uint8_t maxlen = 0;
            if (!read_u8(p, n, pos, maxlen)) return false;
            col.length = maxlen;
            return true;
        }

        // ---- DATEN: no extra bytes ----
        case 0x28:  // DATEN (date only, no length/scale byte per spec)
            return true;

        // ---- DATETIME2N: 1-byte scale ----
        case 0x2A:  // DATETIME2N
        {
            uint8_t scale = 0;
            if (!read_u8(p, n, pos, scale)) return false;
            col.scale = scale;
            return true;
        }

        // ---- DECIMALN / NUMERICN: max-len(1) + precision(1) + scale(1) ----
        case 0x6A:  // DECIMALN
        case 0x6C:  // NUMERICN
        {
            uint8_t maxlen = 0, prec = 0, scale = 0;
            if (!read_u8(p, n, pos, maxlen)) return false;
            if (!read_u8(p, n, pos, prec))   return false;
            if (!read_u8(p, n, pos, scale))  return false;
            col.length    = maxlen;
            col.precision = prec;
            col.scale     = scale;
            return true;
        }

        // ---- BIGCHAR/BIGVARCHR/NCHAR/NVARCHAR: max-len(2 LE) + 5-byte COLLATION ----
        case 0xAF:  // BIGCHAR
        case 0xA7:  // BIGVARCHR
        case 0xEF:  // NCHAR
        case 0xE7:  // NVARCHAR
        {
            uint16_t maxlen = 0;
            if (!read_u16le(p, n, pos, maxlen)) return false;
            col.length = maxlen;
            // COLLATION: LCID(3) + CollationFlags(1) + SortId(1) = 5 bytes.
            // The codepage is encoded inside the collation; for now, skip and
            // store the raw codepage field from collation bytes [3..4] (sortid area).
            // Sufficient for downstream row-reading which only needs the length.
            if (pos + 5 > n) return false;
            // Bytes [0..2] = LCID (3 bytes), [3] = CollationFlags, [4] = SortId.
            // codepage lives in a lookup table; store SortId as a hint only.
            col.codepage = p[pos + 4];  // SortId byte (informational)
            pos += 5;
            return true;
        }

        default:
            // Unsupported / unknown type token → fail-closed.
            return false;
    }
}

bool parse_colmetadata(const uint8_t* p, size_t n, size_t& pos,
                       std::vector<TdsColumn>& cols) {
    cols.clear();

    // Count (2, LE): number of columns. 0xFFFF means "no metadata" (not an error
    // in some contexts, but we simply produce 0 columns and succeed).
    uint16_t count = 0;
    if (!read_u16le(p, n, pos, count)) return false;
    if (count == 0xFFFFu) return true;  // no-metadata marker

    cols.reserve(count);
    for (uint16_t ci = 0; ci < count; ++ci) {
        TdsColumn col;

        // UserType (4, LE) — ignored but must be consumed.
        if (!skip_bytes(n, pos, 4)) return false;

        // Flags (2, LE) — ignored but must be consumed.
        if (!skip_bytes(n, pos, 2)) return false;

        // TYPE_INFO: type token (1 byte) then type-specific bytes.
        uint8_t type_token = 0;
        if (!read_u8(p, n, pos, type_token)) return false;
        if (!read_type_info(p, n, pos, type_token, col)) return false;

        // ColName: B_VARCHAR — 1-byte char-count then UCS-2LE chars.
        uint8_t name_len = 0;
        if (!read_u8(p, n, pos, name_len)) return false;
        // Each char is 2 bytes (UCS-2LE).
        if (pos + static_cast<size_t>(name_len) * 2 > n) return false;
        col.name = ucs2le_to_utf8(p + pos, name_len);
        pos += static_cast<size_t>(name_len) * 2;

        cols.push_back(std::move(col));
    }

    return true;
}

// ---------------------------------------------------------------------------
// decode_cell ([MS-TDS] §2.2.5.5)
// ---------------------------------------------------------------------------
//
// All integer reads are little-endian unless noted.
// Type token constants (same values as in parse_colmetadata):
//   INT1  0x30   INT2  0x34   INT4  0x38   INT8  0x7F
//   INTN  0x26   BIT   0x32   BITN  0x68
//   DECIMALN 0x6A  NUMERICN 0x6C
//   MONEY  0x3C   MONEY4  0x7A   MONEYN  0x6E
//   FLT4   0x3B   FLT8    0x3E   FLTN    0x6D
//   BIGCHAR 0xAF  BIGVARCHR 0xA7
//   NCHAR   0xEF  NVARCHAR  0xE7
//   DATEN   0x28  DATETIM4  0x3A  DATETIME 0x3D  DATETIME2N 0x2A

// civil_from_days: Howard Hinnant's proleptic Gregorian algorithm.
// |z| = days since 1970-01-01 (may be negative for dates before 1970).
// Returns (year, month, day) in the proleptic Gregorian calendar.
static void civil_from_days(int32_t z,
                             int32_t& year, uint32_t& month, uint32_t& day) {
    // Hinnant's algorithm (shifted epoch to March 1, year 0):
    z += 719468;                           // shift to March 1, 0000
    int32_t era  = (z >= 0 ? z : z - 146096) / 146097;
    uint32_t doe = static_cast<uint32_t>(z - era * 146097);            // [0, 146096]
    uint32_t yoe = (doe - doe/1460 + doe/36524 - doe/146096) / 365;   // [0, 399]
    int32_t  y   = static_cast<int32_t>(yoe) + era * 400;
    uint32_t doy = doe - (365*yoe + yoe/4 - yoe/100);                 // [0, 365]
    uint32_t mp  = (5*doy + 2) / 153;                                  // [0, 11]
    day   = doy  - (153*mp + 2)/5 + 1;                                // [1, 31]
    month = mp   < 10u ? mp + 3u : mp - 9u;                           // [1, 12]
    year  = y    + (month <= 2u ? 1 : 0);
}

// Helpers: zero-pad integer into a fixed-width string (appended to |out|).
static void append_padded(std::string& out, int32_t v, int width) {
    std::string s = std::to_string(v < 0 ? -v : v);
    while ((int)s.size() < width) s = "0" + s;
    if (v < 0) s = "-" + s;
    out += s;
}
static void append_upadded(std::string& out, uint32_t v, int width) {
    std::string s = std::to_string(v);
    while ((int)s.size() < width) s = "0" + s;
    out += s;
}

// Read little-endian signed integer from |data|[0..len-1].
static int64_t read_le_int(const uint8_t* data, size_t len) {
    uint64_t v = 0;
    for (size_t i = 0; i < len && i < 8; ++i)
        v |= static_cast<uint64_t>(data[i]) << (i * 8);
    // Sign-extend.
    if (len < 8) {
        uint64_t sign_bit = uint64_t(1) << (len * 8 - 1);
        if (v & sign_bit) v |= ~(sign_bit - 1 | sign_bit);
    }
    return static_cast<int64_t>(v);
}

// Read little-endian unsigned integer from |data|[0..len-1].
static uint64_t read_le_uint(const uint8_t* data, size_t len) {
    uint64_t v = 0;
    for (size_t i = 0; i < len && i < 8; ++i)
        v |= static_cast<uint64_t>(data[i]) << (i * 8);
    return v;
}

// Build decimal string for NUMERIC/DECIMAL: magnitude over up to 16 bytes.
//
// Algorithm — portable schoolbook long-division by 10 over 32-bit limbs.
// The magnitude bytes (little-endian, 4/8/12/16 bytes) are loaded into an
// array of uint32_t limbs in big-endian order (MSL first) so that the
// standard "divide array by scalar" algorithm produces the LSDigit first.
//
// Each step: iterate limbs MSL→LSL, carry = (carry<<32)|limb, quotient_limb =
// carry/10, carry = carry%10.  After the sweep, carry is the next decimal
// digit (LSDigit first).  Since carry_in < 10 always, carry_in*2^32 + limb
// < 10*2^32 < 2^64, every division is safe in uint64_t.  No __uint128_t needed.
static std::string format_numeric(const uint8_t* mag, size_t mag_len, uint8_t scale) {
    // Maximum precision = 38 → magnitude at most 16 bytes → 4 uint32 limbs.
    static constexpr size_t MAX_MAG = 16;
    static constexpr size_t MAX_LIMBS = MAX_MAG / 4;  // 4

    // Clamp to what we can handle.
    size_t ml = (mag_len < MAX_MAG) ? mag_len : MAX_MAG;

    // Number of 32-bit limbs needed (round up to 4-byte boundary).
    size_t nlimbs = (ml + 3) / 4;

    // Load magnitude into limbs[0..nlimbs-1], MSL first.
    // mag is little-endian, so limb[0] holds the most-significant 4 bytes.
    uint32_t limbs[MAX_LIMBS] = {};
    for (size_t i = 0; i < nlimbs; ++i) {
        // limb index 0 = most significant → bytes at offset (nlimbs-1-i)*4.
        size_t byte_off = (nlimbs - 1 - i) * 4;
        uint32_t v = 0;
        for (size_t b = 0; b < 4; ++b) {
            size_t src = byte_off + b;
            if (src < ml) v |= static_cast<uint32_t>(mag[src]) << (b * 8);
        }
        limbs[i] = v;
    }

    // Check for zero.
    bool is_zero = true;
    for (size_t i = 0; i < nlimbs; ++i) if (limbs[i] != 0) { is_zero = false; break; }

    std::string digits;
    if (is_zero) {
        digits = "0";
    } else {
        while (true) {
            // Check if all limbs are zero.
            bool all_zero = true;
            for (size_t i = 0; i < nlimbs; ++i) if (limbs[i] != 0) { all_zero = false; break; }
            if (all_zero) break;

            // Divide the whole limb array by 10; collect remainder as digit.
            uint64_t carry = 0;
            for (size_t i = 0; i < nlimbs; ++i) {
                // carry < 10 always, so carry*2^32 + limbs[i] < 10*2^32 < 2^64 — no overflow.
                uint64_t acc = (carry << 32) | static_cast<uint64_t>(limbs[i]);
                limbs[i] = static_cast<uint32_t>(acc / 10);
                carry     = acc % 10;
            }
            // carry is the next decimal digit (least significant first).
            digits += static_cast<char>('0' + static_cast<uint8_t>(carry));
        }
        std::reverse(digits.begin(), digits.end());
    }

    if (scale == 0) return digits;
    while (digits.size() <= scale) digits = "0" + digits;
    digits.insert(digits.size() - scale, ".");
    return digits;
}

// ---------------------------------------------------------------------------
// Epoch offsets for civil_from_days (which takes days since 1970-01-01):
//   DATEN / DATETIME2N use days since 0001-01-01.
//     0001-01-01 is 719162 days before 1970-01-01  (computed as
//     1969 full years × 365 + 477 leap years = 718685+477 = 719162).
//   DATETIME / DATETIM4 use days since 1900-01-01.
//     1900-01-01 is 25567 days before 1970-01-01  (70 years × 365 + 17 leaps).
// ---------------------------------------------------------------------------
static constexpr int32_t EPOCH_OFFSET_0001 = 719162;  // days from 0001-01-01 to 1970-01-01
static constexpr int32_t EPOCH_OFFSET_1900 = 25567;   // days from 1900-01-01 to 1970-01-01

std::string decode_cell(const TdsColumn& col, const uint8_t* data, size_t len) {
    if (len == 0) return "";

    switch (col.type_token) {
        // ---- Fixed-length signed integers ----
        case 0x30:  // INT1 (tinyint): UNSIGNED 1-byte
            return std::to_string(data[0]);

        case 0x34:  // INT2: signed 2-byte LE
            return std::to_string(static_cast<int16_t>(read_le_int(data, 2)));

        case 0x38:  // INT4: signed 4-byte LE
            return std::to_string(static_cast<int32_t>(read_le_int(data, 4)));

        case 0x7F:  // INT8: signed 8-byte LE
            return std::to_string(read_le_int(data, 8));

        // ---- INTN: dispatch by len ----
        case 0x26:
            if (len == 1) return std::to_string(data[0]);       // tinyint: UNSIGNED
            if (len == 2) return std::to_string(static_cast<int16_t>(read_le_int(data, 2)));
            if (len == 4) return std::to_string(static_cast<int32_t>(read_le_int(data, 4)));
            if (len == 8) return std::to_string(read_le_int(data, 8));
            return "";

        // ---- BIT / BITN ----
        case 0x32:  // BIT
        case 0x68:  // BITN
            return (data[0] != 0) ? "1" : "0";

        // ---- DECIMALN / NUMERICN ----
        // Layout: byte[0] = sign (1=positive, 0=negative),
        //         bytes[1..] = little-endian magnitude (4/8/12/16 bytes)
        case 0x6A:  // DECIMALN
        case 0x6C:  // NUMERICN
        {
            if (len < 2) return "";
            bool positive = (data[0] != 0);
            std::string s = format_numeric(data + 1, len - 1, col.scale);
            if (!positive && s != "0" && s != "0.00" && s.find_first_not_of("0.") != std::string::npos) {
                // Only prefix '-' if the value is non-zero.
                bool all_zero = true;
                for (char c : s) { if (c != '0' && c != '.') { all_zero = false; break; } }
                if (!all_zero) s = "-" + s;
            }
            return s;
        }

        // ---- MONEY (8-byte: high 4 LE then low 4 LE → signed 64-bit, units 1/10000) ----
        case 0x3C:  // MONEY
        {
            if (len < 8) return "";
            uint32_t hi32 = static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8)
                          | (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
            uint32_t lo32 = static_cast<uint32_t>(data[4]) | (static_cast<uint32_t>(data[5]) << 8)
                          | (static_cast<uint32_t>(data[6]) << 16) | (static_cast<uint32_t>(data[7]) << 24);
            int64_t v = (static_cast<int64_t>(static_cast<int32_t>(hi32)) << 32) | lo32;
            bool neg = (v < 0);
            // Two's complement on the unsigned type — safe even for INT64_MIN.
            uint64_t uv = neg ? (uint64_t(0) - static_cast<uint64_t>(v)) : static_cast<uint64_t>(v);
            std::string r;
            if (neg) r += '-';
            r += std::to_string(uv / 10000);
            r += '.';
            uint32_t frac = static_cast<uint32_t>(uv % 10000);
            std::string fs = std::to_string(frac);
            while (fs.size() < 4) fs = "0" + fs;
            r += fs;
            return r;
        }

        // ---- MONEY4 (4-byte signed LE, units 1/10000) ----
        case 0x7A:  // MONEY4
        {
            if (len < 4) return "";
            int32_t v = static_cast<int32_t>(read_le_uint(data, 4));
            bool neg = (v < 0);
            // Two's complement on the unsigned type — safe even for INT32_MIN.
            uint32_t uv = neg ? (uint32_t(0) - static_cast<uint32_t>(v)) : static_cast<uint32_t>(v);
            std::string r;
            if (neg) r += '-';
            r += std::to_string(uv / 10000);
            r += '.';
            std::string fs = std::to_string(uv % 10000);
            while (fs.size() < 4) fs = "0" + fs;
            r += fs;
            return r;
        }

        // ---- MONEYN: dispatch by len 4 or 8 ----
        case 0x6E:  // MONEYN
        {
            TdsColumn c4 = col; c4.type_token = (len == 4) ? 0x7A : 0x3C;
            return decode_cell(c4, data, len);
        }

        // ---- FLT4 (4-byte IEEE float) ----
        case 0x3B:  // FLT4
        {
            if (len < 4) return "";
            float f;
            static_assert(sizeof(float) == 4, "float must be 4 bytes");
            std::memcpy(&f, data, 4);
            // 9 significant decimal digits for round-trip exact float.
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%.9g", static_cast<double>(f));
            return buf;
        }

        // ---- FLT8 (8-byte IEEE double) ----
        case 0x3E:  // FLT8
        {
            if (len < 8) return "";
            double d;
            static_assert(sizeof(double) == 8, "double must be 8 bytes");
            std::memcpy(&d, data, 8);
            // 17 significant decimal digits for round-trip exact double.
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%.17g", d);
            return buf;
        }

        // ---- FLTN: dispatch by len 4 or 8 ----
        case 0x6D:  // FLTN
        {
            TdsColumn c4 = col; c4.type_token = (len == 4) ? 0x3B : 0x3E;
            return decode_cell(c4, data, len);
        }

        // ---- BIGCHAR / BIGVARCHR: passthrough as Latin1/UTF-8 ----
        case 0xAF:  // BIGCHAR
        case 0xA7:  // BIGVARCHR
            return std::string(reinterpret_cast<const char*>(data), len);

        // ---- NCHAR / NVARCHAR: UCS-2LE → UTF-8 ----
        case 0xEF:  // NCHAR
        case 0xE7:  // NVARCHAR
            return ucs2le_to_utf8(data, static_cast<uint16_t>(len / 2));

        // ---- DATEN: 3-byte LE day count since 0001-01-01 → YYYYMMDD (ADS native) ----
        case 0x28:  // DATEN
        {
            if (len < 3) return "";
            uint32_t days_since_0001 = static_cast<uint32_t>(data[0])
                                     | (static_cast<uint32_t>(data[1]) << 8)
                                     | (static_cast<uint32_t>(data[2]) << 16);
            int32_t days_1970 = static_cast<int32_t>(days_since_0001) - EPOCH_OFFSET_0001;
            int32_t year; uint32_t month, day;
            civil_from_days(days_1970, year, month, day);
            std::string r;
            append_padded(r, year, 4);
            append_upadded(r, month, 2);
            append_upadded(r, day, 2);
            return r;
        }

        // ---- DATETIM4 (smalldatetime): days(2 LE)+minutes(2 LE) since 1900-01-01 ----
        // Output: YYYYMMDDHHMMSS (ADS native convention, matches DbfFieldType::DateTime length=14)
        case 0x3A:  // DATETIM4
        {
            if (len < 4) return "";
            uint16_t days16   = static_cast<uint16_t>(data[0] | (data[1] << 8));
            uint16_t minutes16 = static_cast<uint16_t>(data[2] | (data[3] << 8));
            int32_t days_1970 = static_cast<int32_t>(days16) - EPOCH_OFFSET_1900;
            uint32_t hh = minutes16 / 60;
            uint32_t mm = minutes16 % 60;
            int32_t year; uint32_t month, day;
            civil_from_days(days_1970, year, month, day);
            std::string r;
            append_padded(r, year, 4);
            append_upadded(r, month, 2);
            append_upadded(r, day, 2);
            append_upadded(r, hh, 2);
            append_upadded(r, mm, 2);
            r += "00";
            return r;
        }

        // ---- DATETIME: days(4 LE signed)+ticks(4 LE, 1/300 s) since 1900-01-01 ----
        // Output: YYYYMMDDHHMMSS (ADS native convention, matches DbfFieldType::DateTime length=14)
        case 0x3D:  // DATETIME
        {
            if (len < 8) return "";
            int32_t  days32  = static_cast<int32_t>(read_le_uint(data, 4));
            uint32_t ticks   = static_cast<uint32_t>(read_le_uint(data + 4, 4));
            int32_t  days_1970 = days32 - EPOCH_OFFSET_1900;
            // ticks: 1 tick = 1/300 second.  Convert to whole seconds.
            uint32_t total_sec = ticks / 300;
            uint32_t hh = total_sec / 3600;
            uint32_t mm = (total_sec % 3600) / 60;
            uint32_t ss = total_sec % 60;
            int32_t year; uint32_t month, day;
            civil_from_days(days_1970, year, month, day);
            std::string r;
            append_padded(r, year, 4);
            append_upadded(r, month, 2);
            append_upadded(r, day, 2);
            append_upadded(r, hh, 2);
            append_upadded(r, mm, 2);
            append_upadded(r, ss, 2);
            return r;
        }

        // ---- DATETIME2N: time(3-5 bytes per scale)+date(3 bytes) since 0001-01-01 ----
        // Time part byte count per scale:
        //   scale 0-2 → 3 bytes, scale 3-4 → 4 bytes, scale 5-7 → 5 bytes
        // Output: YYYYMMDDHHMMSS (ADS native; fractional seconds dropped to match the
        // 14-char ADS_DATE convention — native path has no sub-second component).
        case 0x2A:  // DATETIME2N
        {
            uint8_t sc = col.scale;
            size_t time_bytes = (sc <= 2) ? 3 : (sc <= 4) ? 4 : 5;
            if (len < time_bytes + 3) return "";
            // Time ticks: little-endian integer over time_bytes bytes.
            uint64_t time_ticks = read_le_uint(data, time_bytes);
            // Date: 3-byte LE days since 0001-01-01.
            const uint8_t* dp = data + time_bytes;
            uint32_t days_since_0001 = static_cast<uint32_t>(dp[0])
                                     | (static_cast<uint32_t>(dp[1]) << 8)
                                     | (static_cast<uint32_t>(dp[2]) << 16);
            int32_t days_1970 = static_cast<int32_t>(days_since_0001) - EPOCH_OFFSET_0001;
            // Convert time_ticks (units of 10^-scale seconds) to H:M:S.
            // ticks_per_second = 10^scale.
            uint64_t pow10 = 1;
            for (int i = 0; i < sc; ++i) pow10 *= 10;
            uint64_t total_sec = (pow10 > 0) ? (time_ticks / pow10) : time_ticks;
            uint32_t hh = static_cast<uint32_t>(total_sec / 3600);
            uint32_t mm = static_cast<uint32_t>((total_sec % 3600) / 60);
            uint32_t ss = static_cast<uint32_t>(total_sec % 60);
            int32_t  year; uint32_t month, day;
            civil_from_days(days_1970, year, month, day);
            std::string r;
            append_padded(r, year, 4);
            append_upadded(r, month, 2);
            append_upadded(r, day, 2);
            append_upadded(r, hh, 2);
            append_upadded(r, mm, 2);
            append_upadded(r, ss, 2);
            // Fractional seconds are dropped: the ADS native DateTime convention
            // is YYYYMMDDHHMMSS (14 chars, no sub-second component).
            return r;
        }

        default:
            return "";  // Unrecognised; defensive fallback.
    }
}

// ---------------------------------------------------------------------------
// parse_query_response — TABULAR_RESULT token-stream walker
// ---------------------------------------------------------------------------
//
// Per-column "wire length rule" (MS-TDS §2.2.7.17):
//
//  Fixed-length types (no length prefix on the wire; fixed byte size):
//    INT1   0x30 → 1 byte      INT2   0x34 → 2 bytes
//    INT4   0x38 → 4 bytes     INT8   0x7F → 8 bytes
//    BIT    0x32 → 1 byte      DATETIM4 0x3A → 4 bytes
//    DATETIME 0x3D → 8 bytes   MONEY4 0x7A → 4 bytes
//    MONEY  0x3C → 8 bytes     FLT4   0x3B → 4 bytes
//    FLT8   0x3E → 8 bytes
//
//  1-byte length prefix (*N / DATE / DATETIME2N / DECIMAL / NUMERIC):
//    INTN   0x26    BITN   0x68    MONEYN 0x6E    FLTN   0x6D
//    DATETIMN 0x6F  DATEN  0x28   DATETIME2N 0x2A
//    DECIMALN 0x6A  NUMERICN 0x6C
//    Sentinel 0xFF (255) → NULL (no value bytes follow)
//
//  2-byte LE (USHORT) length prefix (char/nchar types):
//    BIGCHAR 0xAF   BIGVARCHR 0xA7   NCHAR 0xEF   NVARCHAR 0xE7
//    Sentinel 0xFFFF → NULL

/// Return the fixed byte size for fixed-length type tokens; 0 = not fixed.
static size_t fixed_type_size(uint8_t tok) {
    switch (tok) {
        case 0x30: return 1;   // INT1
        case 0x34: return 2;   // INT2
        case 0x38: return 4;   // INT4
        case 0x7F: return 8;   // INT8
        case 0x32: return 1;   // BIT
        case 0x3A: return 4;   // DATETIM4
        case 0x3D: return 8;   // DATETIME
        case 0x7A: return 4;   // MONEY4
        case 0x3C: return 8;   // MONEY
        case 0x3B: return 4;   // FLT4
        case 0x3E: return 8;   // FLT8
        default:   return 0;
    }
}

/// Read one ROW/NBCROW column value from [p+pos..p+n).
/// Returns false on any short read. Sets cell.is_null=true + no advance if NULL sentinel.
static bool read_column_value(const uint8_t* p, size_t n, size_t& pos,
                              const TdsColumn& col, TdsCell& cell) {
    uint8_t tok = col.type_token;

    // ---- Fixed-length types ----
    size_t flen = fixed_type_size(tok);
    if (flen > 0) {
        if (pos + flen > n) return false;
        cell.value  = decode_cell(col, p + pos, flen);
        cell.is_null = false;
        pos += flen;
        return true;
    }

    // ---- 1-byte length prefix (*N / DATE / DATETIME2N / DECIMAL / NUMERIC) ----
    switch (tok) {
        case 0x26: case 0x68: case 0x6E: case 0x6D: case 0x6F:
        case 0x28: case 0x2A: case 0x6A: case 0x6C:
        {
            if (pos >= n) return false;
            uint8_t vlen = p[pos++];
            if (vlen == 0xFF) {
                cell.is_null = true;
                return true;
            }
            if (pos + vlen > n) return false;
            cell.value   = decode_cell(col, p + pos, vlen);
            cell.is_null = false;
            pos += vlen;
            return true;
        }

        // ---- 2-byte LE length prefix (char / nchar) ----
        case 0xAF: case 0xA7: case 0xEF: case 0xE7:
        {
            if (pos + 2 > n) return false;
            uint16_t vlen = static_cast<uint16_t>(p[pos] | (uint16_t(p[pos+1]) << 8));
            pos += 2;
            if (vlen == 0xFFFF) {
                cell.is_null = true;
                return true;
            }
            if (pos + vlen > n) return false;
            cell.value   = decode_cell(col, p + pos, vlen);
            cell.is_null = false;
            pos += vlen;
            return true;
        }

        default:
            // Unrecognised type — fail-closed (should have been rejected at COLMETADATA).
            return false;
    }
}

QueryResult parse_query_response(const uint8_t* payload, size_t n) {
    QueryResult res;
    size_t pos = 0;
    bool error_seen = false;

    while (pos < n) {
        if (pos >= n) break;
        uint8_t token = payload[pos++];

        // ---- COLMETADATA (0x81) ----
        if (token == 0x81) {
            res.columns.clear();
            bool ok = parse_colmetadata(payload, n, pos, res.columns);
            if (!ok) {
                // Distinguish unsupported type from short read by noting that
                // parse_colmetadata returns false for both; we set a generic label.
                res.unsupported_type = "unsupported_or_short_read";
                res.ok = false;
                return res;
            }
            continue;
        }

        // ---- ROW (0xD1) ----
        if (token == 0xD1) {
            std::vector<TdsCell> row;
            row.reserve(res.columns.size());
            for (const auto& col : res.columns) {
                TdsCell cell;
                if (!read_column_value(payload, n, pos, col, cell)) {
                    res.ok = false;
                    return res;
                }
                row.push_back(std::move(cell));
            }
            res.rows.push_back(std::move(row));
            continue;
        }

        // ---- NBCROW (0xD2) ----
        if (token == 0xD2) {
            size_t ncols = res.columns.size();
            // Null bitmap: ceil(ncols/8) bytes.
            size_t bitmap_bytes = (ncols + 7) / 8;
            if (pos + bitmap_bytes > n) {
                res.ok = false;
                return res;
            }
            // Read null bitmap bytes.
            std::vector<uint8_t> bitmap(payload + pos, payload + pos + bitmap_bytes);
            pos += bitmap_bytes;

            std::vector<TdsCell> row(ncols);
            for (size_t ci = 0; ci < ncols; ++ci) {
                // Bit ci (0-based) in the bitmap, LSB-first within each byte.
                bool is_null = ((bitmap[ci / 8] >> (ci % 8)) & 1) != 0;
                if (is_null) {
                    row[ci].is_null = true;
                } else {
                    if (!read_column_value(payload, n, pos, res.columns[ci], row[ci])) {
                        res.ok = false;
                        return res;
                    }
                }
            }
            res.rows.push_back(std::move(row));
            continue;
        }

        // ---- ERROR (0xAA) ----
        if (token == 0xAA) {
            // Length(2,LE) then body.
            if (pos + 2 > n) { res.ok = false; return res; }
            uint16_t len = static_cast<uint16_t>(payload[pos] | (uint16_t(payload[pos+1]) << 8));
            pos += 2;
            if (pos + len > n) { res.ok = false; return res; }
            const uint8_t* body = payload + pos;
            size_t body_len = len;
            pos += len;
            if (body_len >= 4) {
                res.error_number = static_cast<uint32_t>(body[0])
                                 | (static_cast<uint32_t>(body[1]) << 8)
                                 | (static_cast<uint32_t>(body[2]) << 16)
                                 | (static_cast<uint32_t>(body[3]) << 24);
                // State(1)+Class(1)+MsgText(US_VARCHAR: 2-byte LE char-count + UCS-2LE)
                if (body_len >= 8) {
                    uint16_t mlen = static_cast<uint16_t>(body[6] | (uint16_t(body[7]) << 8));
                    if (body_len >= static_cast<size_t>(8 + mlen * 2)) {
                        res.message = ucs2le_to_utf8(body + 8, mlen);
                    }
                }
            }
            error_seen = true;
            res.ok = false;
            continue;
        }

        // ---- DONE / DONEPROC / DONEINPROC (0xFD/0xFE/0xFF) — fixed 12-byte body ----
        if (token == 0xFD || token == 0xFE || token == 0xFF) {
            // Skip the 12-byte body (Status2+CurCmd2+RowCount8).
            // Not bounds-checking strictly: we stop regardless.
            if (!error_seen) {
                res.ok = true;
            }
            return res;
        }

        // ---- Skippable tokens: INFO/ENVCHANGE/ORDER/RETURNSTATUS ----
        {
            uint8_t fixed_len = 0;
            auto lc = token_length_class(token, fixed_len);
            if (lc == TokenLenClass::VarLenUShort) {
                if (pos + 2 > n) { res.ok = false; return res; }
                uint16_t len = static_cast<uint16_t>(payload[pos] | (uint16_t(payload[pos+1]) << 8));
                pos += 2;
                if (pos + len > n) { res.ok = false; return res; }
                pos += len;
            } else if (lc == TokenLenClass::FixedLength) {
                if (pos + fixed_len > n) { res.ok = false; return res; }
                pos += fixed_len;
            } else if (lc == TokenLenClass::Done) {
                // Shouldn't arrive here (handled above) but treat as stop.
                if (!error_seen) res.ok = true;
                return res;
            } else {
                // Unknown or ColMetaDataDriven (ROW/NBCROW already handled).
                // Cannot advance safely — stop fail-closed.
                res.ok = false;
                return res;
            }
        }
    }

    // Fell off the end without a DONE token.
    if (!error_seen) {
        // Incomplete stream — fail-closed.
        res.ok = false;
    }
    return res;
}

}  // namespace openads::sql_backend::tds
#endif  // defined(OPENADS_WITH_MSSQL)
