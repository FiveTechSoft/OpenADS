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
// LOGIN7 ([MS-TDS] §2.2.6.4)
// ---------------------------------------------------------------------------

/// Parameters for the LOGIN7 message.  All strings are UTF-8 (ASCII-only in
/// this v1 implementation — non-ASCII chars must be encoded externally).
struct Login7Params {
    std::string hostname;
    std::string username;
    std::string password;   // stored clear; obfuscated inside build_login7
    std::string app_name;
    std::string server_name;
    std::string database;
};

/// Build a complete LOGIN7 packet (type=0x10, EOM) per [MS-TDS] §2.2.6.4.
/// The password field is obfuscated via obfuscate_password().
/// All string fields are encoded as UCS-2LE.
/// OffsetLength offsets are relative to the start of the LOGIN7 structure
/// (i.e., the byte immediately following the 8-byte TDS packet header).
std::vector<uint8_t> build_login7(const Login7Params& p);

/// Result of parsing a server login-response token stream.
struct LoginResult {
    bool     authenticated = false;
    uint32_t error_number  = 0;
    std::string message;
};

/// Per-token length classification per [MS-TDS] §2.2.4.
/// Used by the login-response parser and (later) the result-set parser to
/// advance over tokens without knowing their full structure.
///
///   ZeroLength        — token has no length field and no body (0 extra bytes)
///   FixedLength       — token body is exactly fixed_len bytes (no length field)
///   VarLenByteCount   — 1-byte LE length prefix, then that many bytes
///   VarLenUShort      — 2-byte LE length prefix, then that many bytes
///   VarLenULong       — 4-byte LE length prefix, then that many bytes
///   ColMetaDataDriven — body layout depends on prior COLMETADATA (structural)
///   Done              — DONE/DONEPROC/DONEINPROC family (fixed 12-byte body)
///   Unknown           — not classified; caller must stop (safe-fail)
enum class TokenLenClass {
    ZeroLength,
    FixedLength,
    VarLenByteCount,
    VarLenUShort,
    VarLenULong,
    ColMetaDataDriven,
    Done,
    Unknown
};

/// Classify a TDS token byte into its length class per [MS-TDS] §2.2.4.
/// For FixedLength tokens, sets |fixed_len| to the body size in bytes.
/// For all other classes, |fixed_len| is left unchanged.
///
/// Mapping (canonical, not exhaustive):
///   0xAA ERROR, 0xAB INFO, 0xAD LOGINACK, 0xE3 ENVCHANGE, 0xA9 ORDER
///       → VarLenUShort
///   0xFD DONE, 0xFE DONEPROC, 0xFF DONEINPROC → Done
///   0x79 RETURNSTATUS → FixedLength, fixed_len=4
///   0x81 COLMETADATA, 0xD1 ROW, 0xD2 NBCROW → ColMetaDataDriven
///   everything else → Unknown
TokenLenClass token_length_class(uint8_t token, uint8_t& fixed_len);

/// Parse a server login-response payload (bytes AFTER the 8-byte TDS header).
/// Walks the token stream per [MS-TDS] §2.2.4/§2.2.7:
///   LOGINACK (0xAD) → authenticated=true
///   ERROR    (0xAA) → authenticated=false, fills error_number+message
///   INFO     (0xAB) → ignored
///   ENVCHANGE(0xE3) → ignored
///   DONE     (0xFD) → stop
/// Returns LoginResult with authenticated=true on success.
LoginResult parse_login_response(const uint8_t* payload, size_t n);

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

// ---------------------------------------------------------------------------
// SQL_BATCH ([MS-TDS] §2.2.6.7)
// ---------------------------------------------------------------------------

/// Build a SQL_BATCH message body (bytes AFTER the 8-byte TDS header).
/// Layout per [MS-TDS] §2.2.6.7: an ALL_HEADERS stream (§2.2.5) —
///   TotalLength(4 LE) then one Transaction Descriptor header:
///   { HeaderLength(4 LE)=18, HeaderType(2 LE)=0x0002,
///     TransactionDescriptor(8)=0, OutstandingRequestCount(4 LE)=1 }
/// — followed by the SQL text as UCS-2LE.
/// Feed the result body to send_tds with packet type TDS_PKT_SQLBATCH.
std::vector<uint8_t> build_sql_batch(const std::string& utf8_sql);

// ---------------------------------------------------------------------------
// COLMETADATA ([MS-TDS] §2.2.7.4 / TYPE_INFO §2.2.5.4–5.5)
// ---------------------------------------------------------------------------

/// Descriptor for one column in a COLMETADATA token stream.
/// Populated by parse_colmetadata().
struct TdsColumn {
    std::string name;            ///< Column name (UTF-8; UCS-2LE decoded on parse)
    uint8_t     type_token = 0;  ///< TDS type token byte (e.g. 0x26 INTN, 0xE7 NVARCHAR)
    uint32_t    length     = 0;  ///< Max length in bytes (0 for fixed-length types)
    uint8_t     precision  = 0;  ///< Precision (DECIMALN/NUMERICN only)
    uint8_t     scale      = 0;  ///< Scale (DECIMALN/NUMERICN, DATETIME2N)
    uint16_t    codepage   = 0;  ///< Collation codepage (BIGCHAR/BIGVARCHR/NCHAR/NVARCHAR)
};

/// Parse a COLMETADATA token body per [MS-TDS] §2.2.7.4.
/// |p| and |n| describe the raw byte buffer.
/// |pos| must point at the byte immediately AFTER the 0x81 token id on entry;
/// on success it is advanced past the entire COLMETADATA body and |cols| is filled.
/// Returns false (fail-closed) on any short read or unsupported type token.
bool parse_colmetadata(const uint8_t* p, size_t n, size_t& pos,
                       std::vector<TdsColumn>& cols);

// ---------------------------------------------------------------------------
// decode_cell ([MS-TDS] §2.2.5.5)
// ---------------------------------------------------------------------------

/// Decode one column value from its raw TDS wire bytes to a printable string.
/// |col|  — column descriptor from parse_colmetadata() (type_token, scale).
/// |data| — pointer to the value bytes (already extracted by the row reader).
/// |len|  — byte count of |data|; 0 is allowed (NULL / zero-length).
/// Returns the decimal/text representation, or "" for unrecognised type tokens
/// (those were rejected at COLMETADATA; this is a defensive fallback).
std::string decode_cell(const TdsColumn& col, const uint8_t* data, size_t len);

// ---------------------------------------------------------------------------
// parse_query_response ([MS-TDS] §2.2.7.17 TABULAR_RESULT)
// ---------------------------------------------------------------------------

/// One decoded cell value from a result row.
struct TdsCell {
    std::string value;
    bool        is_null = false;
};

/// Result of parsing a TABULAR_RESULT token stream (response to SQL_BATCH).
struct QueryResult {
    std::vector<TdsColumn>              columns;
    std::vector<std::vector<TdsCell>>   rows;
    bool        ok               = false;
    uint32_t    error_number     = 0;
    std::string message;
    std::string unsupported_type;  ///< set when COLMETADATA rejects an unknown type
};

/// Walk a TABULAR_RESULT token stream ([MS-TDS] §2.2.7.17).
/// |payload| is the raw server payload bytes (after the TDS packet header).
/// Token handling:
///   COLMETADATA(0x81) → parse_colmetadata; unsupported type → unsupported_type + ok=false, stop.
///   ROW(0xD1)         → one cell per column; length from column type wire rule.
///   NBCROW(0xD2)      → null bitmap (ceil(ncols/8) bytes) then non-null column values.
///   ERROR(0xAA)       → error_number + message, ok=false.
///   INFO(0xAB)/ENVCHANGE(0xE3)/ORDER(0xA9)/RETURNSTATUS(0x79) → skipped.
///   DONE/DONEPROC/DONEINPROC(0xFD/0xFE/0xFF) → ok=true (unless error), stop.
/// Any short/over-long read → ok=false, stop (fail-closed, no OOB).
QueryResult parse_query_response(const uint8_t* payload, size_t n);

#endif  // defined(OPENADS_WITH_MSSQL)

}  // namespace openads::sql_backend::tds
