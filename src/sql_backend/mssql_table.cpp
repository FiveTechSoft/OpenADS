#include "sql_backend/mssql_table.h"

#if defined(OPENADS_WITH_MSSQL)

// ADS field-type constants (ADS_STRING=4, ADS_DOUBLE=10, etc.)
// Must be included BEFORE entering any namespace because ace.h wraps
// all its declarations in `extern "C" { ... }` which would break an
// enclosing namespace context.
#include "openads/ace.h"
#include "openads/error.h"
#include "sql_backend/mssql_connection.h"
#include "sql_backend/sql_common.h"

#include <algorithm>
#include <limits>
#include <string>

namespace openads::sql_backend {

// ---------------------------------------------------------------------------
// TDS type_token → ADS field-type mapping (mirrors odbc_backend.cpp)
// ---------------------------------------------------------------------------
//
// TDS type tokens we handle:
//   INTN  0x26   (1/2/4/8-byte integers)
//   INT1  0x30   (TINYINT fixed 1 byte)
//   INT2  0x34   (SMALLINT fixed 2 bytes)
//   INT4  0x38   (INT fixed 4 bytes)
//   INT8  0x7F   (BIGINT fixed 8 bytes)
//   FLTN  0x6D   (float/real nullable)
//   FLT4  0x3B   (REAL fixed 4 bytes)
//   FLT8  0x3E   (FLOAT fixed 8 bytes)
//   DECIMALN  0x6A
//   NUMERICN  0x6C
//   MONEYN    0x6E  (MONEY/MONEY4 nullable)
//   MONEY     0x3C  (fixed 8-byte MONEY)
//   MONEY4    0x7A  (fixed 4-byte SMALLMONEY)
//   BITN  0x68   (BIT nullable)
//   BIT   0x32   (BIT fixed)
//   DATETIMN  0x6F
//   DATETIME  0x3D
//   DATETIME4 0x3A  (SMALLDATETIME)
//   DATETIME2N 0x2A
//   DATEN     0x28
//   TIMEN     0x29
//   BIGCHAR/BIGVARCHR 0xAF/0xA7  (CHAR/VARCHAR)
//   NCHAR/NVARCHAR    0xEF/0xE7
//   BIGBINARY/BIGVARBINARY 0xAD/0xA5
//   TEXT      0x23
//   NTEXT     0x63
//   IMAGE     0x22
//
// Mapping:
//   integers / float / decimal / money  → ADS_DOUBLE (10)
//   bit                                 → ADS_LOGICAL (1)
//   date / time / datetime              → ADS_DATE (3)  [YYYYMMDD]
//   binary / image                      → ADS_BINARY (6)
//   everything else (char, nchar, text) → ADS_STRING (4)

static std::uint16_t ads_type_from_tds(std::uint8_t type_token) {
    switch (type_token) {
        // --- integers ---
        case 0x26:  // INTN
        case 0x30:  // INT1 (TINYINT)
        case 0x34:  // INT2 (SMALLINT)
        case 0x38:  // INT4 (INT)
        case 0x7F:  // INT8 (BIGINT)
        // --- float / real ---
        case 0x6D:  // FLTN
        case 0x3B:  // FLT4 (REAL)
        case 0x3E:  // FLT8 (FLOAT)
        // --- decimal / numeric / money ---
        case 0x6A:  // DECIMALN
        case 0x6C:  // NUMERICN
        case 0x6E:  // MONEYN
        case 0x3C:  // MONEY (fixed)
        case 0x7A:  // MONEY4 (SMALLMONEY)
            return ADS_DOUBLE;

        // --- bit ---
        case 0x68:  // BITN
        case 0x32:  // BIT (fixed)
            return ADS_LOGICAL;

        // --- date / time / datetime ---
        case 0x6F:  // DATETIMN
        case 0x3D:  // DATETIME (fixed)
        case 0x3A:  // DATETIME4 (SMALLDATETIME)
        case 0x2A:  // DATETIME2N
        case 0x28:  // DATEN
        case 0x29:  // TIMEN
            return ADS_DATE;

        // --- binary ---
        case 0xAD:  // BIGBINARY (BINARY)
        case 0xA5:  // BIGVARBINARY (VARBINARY)
        case 0x22:  // IMAGE
            return ADS_BINARY;

        // --- string / text / everything else ---
        default:
            return ADS_STRING;
    }
}

// Return a v1 display length for the ADS type, using the TDS column length.
static std::uint32_t ads_length_from_tds(std::uint8_t type_token,
                                          std::uint32_t tds_length) {
    switch (ads_type_from_tds(type_token)) {
        case ADS_LOGICAL: return 1;
        case ADS_DATE:    return 8;   // "YYYYMMDD"
        case ADS_BINARY:  return tds_length ? tds_length : 10;
        case ADS_DOUBLE:  return 8;   // sizeof(double)
        default:
            // String: TDS length for NVARCHAR is in bytes (2× chars); halve it.
            // Clamp to a sensible max of 254; never return 0.
            if (type_token == 0xE7 || type_token == 0xEF) {
                // NVARCHAR / NCHAR — length is in bytes; char count = length/2.
                std::uint32_t chars = tds_length / 2;
                return (chars > 0 && chars <= 32767u) ? chars : 64u;
            }
            return (tds_length > 0 && tds_length <= 32767u) ? tds_length : 64u;
    }
}

static std::uint16_t ads_decimals_from_tds(std::uint8_t type_token,
                                            std::uint8_t tds_scale) {
    switch (type_token) {
        case 0x6A: case 0x6C:  // DECIMALN / NUMERICN
            return static_cast<std::uint16_t>(tds_scale);
        case 0x3C: case 0x6E: case 0x7A:  // MONEY / MONEYN / MONEY4
            return 4;
        case 0x6D: case 0x3B: case 0x3E:  // float types
            return 6;
        default:
            return 0;
    }
}

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------

util::Result<std::unique_ptr<MssqlTable>>
MssqlTable::open(MssqlConnection& c, const std::string& table_name) {
    if (!is_safe_identifier(table_name)) {
        return util::Error{static_cast<std::int32_t>(openads::AE_INTERNAL_ERROR),
                           0, "unsafe table name", table_name};
    }

    std::string sql = "SELECT * FROM [" + table_name + "]";
    auto qr = c.query(sql);
    if (!qr) return qr.error();

    tds::QueryResult result = std::move(qr).value();
    if (!result.ok) {
        return util::Error{static_cast<std::int32_t>(result.error_number), 0,
                           result.message, sql};
    }

    return from_result(std::move(result));
}

std::unique_ptr<MssqlTable> MssqlTable::from_result(tds::QueryResult qr) {
    auto t = std::make_unique<MssqlTable>();
    t->data = std::move(qr);
    t->pos  = 0;
    // Position to BOF state (empty cursor or before first row).
    if (t->data.rows.empty()) {
        t->bof = true;
        t->eof = true;
    } else {
        t->bof = true;
        t->eof = false;
    }
    return t;
}

// ---------------------------------------------------------------------------
// Navigation
// ---------------------------------------------------------------------------

void MssqlTable::go_top() {
    if (data.rows.empty()) {
        pos = 0;
        bof = true;
        eof = true;
    } else {
        pos = 0;
        bof = false;
        eof = false;
    }
}

void MssqlTable::go_bottom() {
    if (data.rows.empty()) {
        pos = 0;
        bof = true;
        eof = true;
    } else {
        pos = data.rows.size() - 1;
        bof = false;
        eof = false;
    }
}

void MssqlTable::skip(long n) {
    if (data.rows.empty()) {
        bof = true;
        eof = true;
        return;
    }

    if (n > 0) {
        if (bof) {
            // First forward skip from BOF lands at row 0.
            n -= 1;
            pos = 0;
            bof = false;
        }
        // Advance pos by remaining n.
        std::size_t remaining = static_cast<std::size_t>(n);
        if (remaining >= data.rows.size() - pos) {
            pos = data.rows.size();
            eof = true;
        } else {
            pos += remaining;
            eof = false;
        }
    } else if (n < 0) {
        if (eof) {
            // First backward skip from EOF lands at last row.
            n += 1;
            pos = data.rows.size() - 1;
            eof = false;
        }
        long abs_n = -n;
        if (static_cast<std::size_t>(abs_n) >= pos) {
            pos = 0;
            bof = true;
        } else {
            pos -= static_cast<std::size_t>(abs_n);
            bof = false;
        }
    }
}

bool MssqlTable::at_bof() const { return bof; }
bool MssqlTable::at_eof() const { return eof; }

// ---------------------------------------------------------------------------
// Schema
// ---------------------------------------------------------------------------

std::size_t MssqlTable::field_count() const {
    return data.columns.size();
}

std::string MssqlTable::field_name(std::size_t i) const {
    if (i >= data.columns.size()) return {};
    return data.columns[i].name;
}

std::uint16_t MssqlTable::field_type(std::size_t i) const {
    if (i >= data.columns.size()) return ADS_STRING;
    return ads_type_from_tds(data.columns[i].type_token);
}

std::uint32_t MssqlTable::field_length(std::size_t i) const {
    if (i >= data.columns.size()) return 0;
    const auto& c = data.columns[i];
    return ads_length_from_tds(c.type_token, c.length);
}

std::uint16_t MssqlTable::field_decimals(std::size_t i) const {
    if (i >= data.columns.size()) return 0;
    const auto& c = data.columns[i];
    return ads_decimals_from_tds(c.type_token, c.scale);
}

// ---------------------------------------------------------------------------
// Position / count
// ---------------------------------------------------------------------------

std::uint32_t MssqlTable::record_num() const {
    if (bof || data.rows.empty() || eof) return 0;
    return static_cast<std::uint32_t>(pos + 1);
}

std::uint32_t MssqlTable::record_count() const {
    return static_cast<std::uint32_t>(data.rows.size());
}

// ---------------------------------------------------------------------------
// Data access
// ---------------------------------------------------------------------------

bool MssqlTable::get_field(std::size_t i, std::string& out, bool& is_null) const {
    if (bof || eof) return false;
    if (pos >= data.rows.size()) return false;
    const auto& row = data.rows[pos];
    if (i >= row.size()) return false;
    out     = row[i].value;
    is_null = row[i].is_null;
    return true;
}

} // namespace openads::sql_backend

#endif // defined(OPENADS_WITH_MSSQL)
