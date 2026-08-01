#include "drivers/dbf_common.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace openads::drivers {

namespace {

DbfFamily classify(std::uint8_t version) {
    switch (version) {
        case 0x03: case 0x83:
        case 0xC3: case 0xC4:               // M11.2 — encrypted variants
        case 0xF5:                          // FoxPro 2.x with FPT memo
        case 0xFB:                          // FoxBASE
            return DbfFamily::Clipper;
        case 0x30: case 0x31: case 0x32:
            return DbfFamily::Vfp;
        default:
            return DbfFamily::Unknown;
    }
}

std::uint16_t read_u16_le(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0]) |
           static_cast<std::uint16_t>(p[1] << 8);
}

std::uint32_t read_u32_le(const std::uint8_t* p) {
    return  static_cast<std::uint32_t>(p[0])        |
           (static_cast<std::uint32_t>(p[1]) << 8)  |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

} // namespace

util::Result<DbfHeader> parse_dbf_header(const std::uint8_t* data,
                                         std::size_t size) {
    if (size < 32) {
        return util::Error{5103, 0, "DBF header smaller than 32 bytes", ""};
    }
    DbfHeader h;
    h.version = data[0];
    // YY in DBF header is years since 1900. Apply that base.
    h.last_update_year  = static_cast<std::uint16_t>(1900 + data[1]);
    h.last_update_month = data[2];
    h.last_update_day   = data[3];
    h.record_count      = read_u32_le(data + 4);
    h.header_length     = read_u16_le(data + 8);
    h.record_length     = read_u16_le(data + 10);
    h.family            = classify(h.version);
    h.encrypted         = (h.version == 0xC3 || h.version == 0xC4);
    return h;
}

namespace {

DbfFieldType classify_field(char raw) {
    switch (raw) {
        case 'C': return DbfFieldType::Character;
        case 'N': return DbfFieldType::Numeric;
        case 'F': return DbfFieldType::Float;
        case 'D': return DbfFieldType::Date;
        case 'T': return DbfFieldType::DateTime;
        case 'L': return DbfFieldType::Logical;
        case 'M': return DbfFieldType::Memo;
        case 'I': return DbfFieldType::Integer;
        case 'Y': return DbfFieldType::Currency;
        case 'B': return DbfFieldType::Double;
        case 'V': return DbfFieldType::Varchar;     // M11.1
        case 'Q': return DbfFieldType::Varbinary;   // M11.1
        case '+': return DbfFieldType::Integer;     // VFP autoincrement
        default:  return DbfFieldType::Unknown;
    }
}

} // namespace

util::Result<std::vector<DbfField>>
parse_dbf_fields(const std::uint8_t* data, std::size_t size,
                 std::uint8_t version) {
    std::vector<DbfField> out;
    std::uint16_t offset = 1; // skip leading deletion byte

    std::size_t pos = 0;
    while (pos + 32 <= size) {
        if (data[pos] == 0x0D) break;
        DbfField f;
        const char* raw_name = reinterpret_cast<const char*>(data + pos);
        std::size_t name_len = 0;
        while (name_len < 11 && raw_name[name_len] != '\0') ++name_len;
        f.name.assign(raw_name, name_len);
        f.raw_type      = static_cast<char>(data[pos + 11]);
        f.type          = classify_field(f.raw_type);
        f.length        = data[pos + 16];
        f.decimals      = data[pos + 17];
        f.record_offset = offset;
        // VFP autoinc descriptor bytes (M10.11). Non-VFP DBFs have
        // these slots zeroed so the read is harmless.
        std::uint8_t flags = data[pos + 18];
        f.autoinc      = (flags & 0x0Cu) != 0;   // VFP marks autoinc
                                                 // with bits 2+3 set
        // M11.6 — VFP NULL flag = bit 1 (0x02) of the flags byte.
        // Each nullable field gets a position in the table-wide
        // _NullFlags field; the position is assigned in declaration
        // order during parse_dbf_fields.
        f.nullable = (flags & 0x02u) != 0;
        f.autoinc_next =  static_cast<std::uint32_t>(data[pos + 19])        |
                         (static_cast<std::uint32_t>(data[pos + 20]) <<  8) |
                         (static_cast<std::uint32_t>(data[pos + 21]) << 16) |
                         (static_cast<std::uint32_t>(data[pos + 22]) << 24);
        f.autoinc_step = data[pos + 23];
        if (f.autoinc_step == 0) f.autoinc_step = 1;
        offset = static_cast<std::uint16_t>(offset + f.length);
        out.push_back(std::move(f));
        pos += 32;
    }
    // M11.6 — assign null-bit positions to every nullable field in
    // declaration order. Apps that touch _NullFlags directly read
    // these bits via `Table::is_field_null`.
    std::uint16_t nb = 0;
    for (auto& fd : out) {
        if (fd.nullable) fd.null_bit = nb++;
    }

    // VFP tables (0x30/0x31/0x32) with nullable columns reserve a
    // 4-byte null bitmap immediately after the deletion byte. Shift
    // user-field offsets and append the synthetic _NullFlags column
    // that maps to those bytes (see Table::is_field_null).
    if (nb > 0 && classify(version) == DbfFamily::Vfp) {
        for (auto& fd : out) {
            fd.record_offset = static_cast<std::uint16_t>(fd.record_offset + 4);
        }
        DbfField nf;
        nf.name          = "_NullFlags";
        nf.raw_type      = '0';
        nf.type          = DbfFieldType::Integer;
        nf.length        = 4;
        nf.record_offset = 1;
        out.push_back(std::move(nf));
    }

    return out;
}

namespace {

std::string make_string(const std::uint8_t* p, std::size_t n) {
    std::string s(reinterpret_cast<const char*>(p), n);
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

double parse_numeric(const std::uint8_t* p, std::size_t n) {
    char tmp[64];
    if (n >= sizeof(tmp)) n = sizeof(tmp) - 1;
    std::memcpy(tmp, p, n);
    tmp[n] = '\0';
    char* end = nullptr;
    return std::strtod(tmp, &end);
}

// VFP I (Integer) — 4-byte little-endian signed int32.
std::int32_t read_i32_le(const std::uint8_t* p) {
    std::uint32_t u =
         static_cast<std::uint32_t>(p[0])        |
        (static_cast<std::uint32_t>(p[1]) <<  8) |
        (static_cast<std::uint32_t>(p[2]) << 16) |
        (static_cast<std::uint32_t>(p[3]) << 24);
    return static_cast<std::int32_t>(u);
}

// VFP Y (Currency) — 8-byte little-endian signed int64 of money * 10000.
std::int64_t read_i64_le(const std::uint8_t* p) {
    std::uint64_t u = 0;
    for (int i = 0; i < 8; ++i) {
        u |= static_cast<std::uint64_t>(p[i]) << (i * 8);
    }
    return static_cast<std::int64_t>(u);
}

// VFP B (Double) — 8-byte IEEE 754 little-endian double.
double read_f64_le(const std::uint8_t* p) {
    std::uint64_t u = static_cast<std::uint64_t>(read_i64_le(p));
    double out;
    std::memcpy(&out, &u, sizeof(out));
    return out;
}

void write_i32_le(std::uint8_t* p, std::int32_t v) {
    auto u = static_cast<std::uint32_t>(v);
    p[0] = static_cast<std::uint8_t>( u        & 0xFFu);
    p[1] = static_cast<std::uint8_t>((u >>  8) & 0xFFu);
    p[2] = static_cast<std::uint8_t>((u >> 16) & 0xFFu);
    p[3] = static_cast<std::uint8_t>((u >> 24) & 0xFFu);
}

void write_i64_le(std::uint8_t* p, std::int64_t v) {
    auto u = static_cast<std::uint64_t>(v);
    for (int i = 0; i < 8; ++i) {
        p[i] = static_cast<std::uint8_t>((u >> (i * 8)) & 0xFFu);
    }
}

void write_f64_le(std::uint8_t* p, double v) {
    std::uint64_t u;
    std::memcpy(&u, &v, sizeof(u));
    write_i64_le(p, static_cast<std::int64_t>(u));
}

// ADT Julian Day Number ↔ calendar (Fliegel-Van Flandern algorithm).
void jdn_to_ymd(std::uint32_t jdn, int& y, int& m, int& d) {
    std::int64_t l = static_cast<std::int64_t>(jdn) + 68569;
    std::int64_t n = (4 * l) / 146097;
    l -= (146097 * n + 3) / 4;
    std::int64_t i = (4000 * (l + 1)) / 1461001;
    l -= (1461 * i) / 4 - 31;
    std::int64_t j = (80 * l) / 2447;
    d = static_cast<int>(l - (2447 * j) / 80);
    l = j / 11;
    m = static_cast<int>(j + 2 - 12 * l);
    y = static_cast<int>(100 * (n - 49) + i + l);
}

std::uint32_t ymd_to_jdn(int y, int m, int d) {
    std::int64_t a  = (14 - m) / 12;
    std::int64_t yy = y + 4800 - a;
    std::int64_t mm = m + 12 * a - 3;
    return static_cast<std::uint32_t>(
        d + (153 * mm + 2) / 5 + 365 * yy + yy / 4 - yy / 100 + yy / 400 - 32045);
}

} // namespace

util::Result<DbfFieldValue> decode_field(const DbfField& field,
                                         const std::uint8_t* record_buf,
                                         std::size_t record_size) {
    DbfFieldValue v;
    if (static_cast<std::size_t>(field.record_offset) +
        static_cast<std::size_t>(field.length) > record_size) {
        return util::Error{5000, 0, "field range past record buffer", ""};
    }
    const std::uint8_t* p = record_buf + field.record_offset;

    // M13 — ADT NULL sentinel (see adt_field_is_null). Without this, a
    // SAP-written NULL INTEGER (0x80000000) decodes as -2147483648 and a
    // NULL DOUBLE as a garbage denormal.
    if (field.adt && adt_field_is_null(field, record_buf, record_size)) {
        DbfFieldValue nul;
        nul.is_null = true;
        return nul;
    }

    switch (field.type) {
        case DbfFieldType::Character:
            v.as_string = make_string(p, field.length);
            break;

        case DbfFieldType::Numeric:
        case DbfFieldType::Float:
            v.as_double = parse_numeric(p, field.length);
            v.as_string = make_string(p, field.length);
            break;

        case DbfFieldType::Date:
            v.as_string = make_string(p, field.length);
            break;

        case DbfFieldType::DateTime: {
            // DBF T — 4-byte Julian Day Number (LE) + 4-byte ms-since-midnight (LE).
            // Decode to "YYYYMMDDHHMMSS" to match AdtTimestamp output format.
            if (field.length < 8) { v.as_string = make_string(p, field.length); break; }
            std::uint32_t jdn = static_cast<std::uint32_t>(p[0])        |
                               (static_cast<std::uint32_t>(p[1]) <<  8) |
                               (static_cast<std::uint32_t>(p[2]) << 16) |
                               (static_cast<std::uint32_t>(p[3]) << 24);
            std::uint32_t ms  = static_cast<std::uint32_t>(p[4])        |
                               (static_cast<std::uint32_t>(p[5]) <<  8) |
                               (static_cast<std::uint32_t>(p[6]) << 16) |
                               (static_cast<std::uint32_t>(p[7]) << 24);
            if (jdn == 0 && ms == 0) { v.is_null = true; break; }
            int y, mo, d;
            jdn_to_ymd(jdn, y, mo, d);
            unsigned hh  = ms / 3600000u;
            unsigned mmv = (ms % 3600000u) / 60000u;
            unsigned ss  = (ms % 60000u)   / 1000u;
            char tmp[48];
            std::snprintf(tmp, sizeof(tmp), "%04d%02d%02d%02u%02u%02u",
                          y, mo, d, hh, mmv, ss);
            v.as_string = tmp;
            break;
        }

        case DbfFieldType::Logical: {
            char c = static_cast<char>(p[0]);
            v.as_bool   = (c == 'T' || c == 't' || c == 'Y' || c == 'y');
            v.as_string = std::string(1, c);
            break;
        }

        case DbfFieldType::Memo:
            // M1 deliberately does not load memo blocks; they land in M4.
            v.as_string.clear();
            break;

        case DbfFieldType::Integer: {
            // VFP I — 4-byte little-endian signed int32.
            if (field.length < 4) {
                v.as_string = make_string(p, field.length);
                break;
            }
            std::int32_t n = read_i32_le(p);
            v.as_double = static_cast<double>(n);
            char tmp[32];
            std::snprintf(tmp, sizeof(tmp), "%d",
                          static_cast<int>(n));
            v.as_string = tmp;
            break;
        }
        case DbfFieldType::Currency: {
            // VFP Y — 8-byte little-endian signed int64, money * 10000.
            if (field.length < 8) {
                v.as_string = make_string(p, field.length);
                break;
            }
            std::int64_t raw = read_i64_le(p);
            double money = static_cast<double>(raw) / 10000.0;
            v.as_double = money;
            char tmp[64];
            std::snprintf(tmp, sizeof(tmp), "%.4f", money);
            v.as_string = tmp;
            break;
        }
        case DbfFieldType::Double: {
            // VFP B — 8-byte IEEE 754 little-endian double.
            if (field.length < 8) {
                v.as_string = make_string(p, field.length);
                break;
            }
            v.as_double = read_f64_le(p);
            // A DOUBLE is a binary IEEE value. When the descriptor carries a
            // decimal count (VFP "B" fields) honour it for the display string.
            // When it is 0 -- which is what a conforming ADT DOUBLE descriptor
            // carries -- render at full round-trip precision (the SQLite
            // convention) instead of truncating to zero decimals; otherwise
            // the wire path, which ships this string and re-parses it, would
            // silently lose all the fractional digits.
            char tmp[64];
            if (field.decimals > 0) {
                std::snprintf(tmp, sizeof(tmp), "%.*f",
                              static_cast<int>(field.decimals), v.as_double);
            } else {
                // 17 significant digits guarantees a lossless round-trip for
                // an IEEE-754 double (15 can lose the last ulp on the wire).
                std::snprintf(tmp, sizeof(tmp), "%.17g", v.as_double);
            }
            v.as_string = tmp;
            break;
        }
        case DbfFieldType::Varchar: {
            // VFP V — fixed-width slot trimmed of trailing NULs. Apps
            // see only the meaningful prefix; everything after the
            // first 0x00 is padding.
            std::size_t actual = field.length;
            while (actual > 0 && p[actual - 1] == 0x00) --actual;
            v.as_string = std::string(
                reinterpret_cast<const char*>(p), actual);
            break;
        }
        case DbfFieldType::Varbinary: {
            // VFP Q — raw bytes; trailing 0x00 also trimmed for
            // string view (binary callers should use raw_record_raw
            // for byte-exact access).
            std::size_t actual = field.length;
            while (actual > 0 && p[actual - 1] == 0x00) --actual;
            v.as_string = std::string(
                reinterpret_cast<const char*>(p), actual);
            break;
        }
        case DbfFieldType::ShortInt: {
            // ADT type 12: 2-byte signed int16 LE.
            if (field.length < 2) { v.as_string = make_string(p, field.length); break; }
            auto n = static_cast<std::int16_t>(
                         static_cast<std::uint16_t>(p[0]) |
                        (static_cast<std::uint16_t>(p[1]) << 8));
            v.as_double = static_cast<double>(n);
            char tmp[16];
            std::snprintf(tmp, sizeof(tmp), "%d", static_cast<int>(n));
            v.as_string = tmp;
            break;
        }
        case DbfFieldType::Binary:
            // ADT type 6: 9-byte block ref into .adm store; resolved at driver layer.
            v.as_string.clear();
            break;

        case DbfFieldType::CiCharacter:
            // ADT type 20: same wire encoding as Character; CI only in index comparisons.
            v.as_string = make_string(p, field.length);
            break;

        case DbfFieldType::AutoInc: {
            // ADT type 15: 4-byte unsigned int32 LE, read-only counter.
            if (field.length < 4) { v.as_string = make_string(p, field.length); break; }
            std::uint32_t n = static_cast<std::uint32_t>(p[0])        |
                             (static_cast<std::uint32_t>(p[1]) <<  8) |
                             (static_cast<std::uint32_t>(p[2]) << 16) |
                             (static_cast<std::uint32_t>(p[3]) << 24);
            v.as_double = static_cast<double>(n);
            char tmp[16];
            std::snprintf(tmp, sizeof(tmp), "%u", static_cast<unsigned>(n));
            v.as_string = tmp;
            break;
        }
        case DbfFieldType::Time: {
            // ADT type 13: 4-byte uint32 milliseconds since midnight → "HH:MM:SS.mmm".
            if (field.length < 4) { v.as_string = make_string(p, field.length); break; }
            std::uint32_t ms = static_cast<std::uint32_t>(p[0])        |
                              (static_cast<std::uint32_t>(p[1]) <<  8) |
                              (static_cast<std::uint32_t>(p[2]) << 16) |
                              (static_cast<std::uint32_t>(p[3]) << 24);
            v.as_double = static_cast<double>(ms);
            unsigned hh   = ms / 3600000u;
            unsigned mmv  = (ms % 3600000u) / 60000u;
            unsigned ss   = (ms % 60000u)   / 1000u;
            unsigned ms3  =  ms % 1000u;
            char tmp[16];
            std::snprintf(tmp, sizeof(tmp), "%02u:%02u:%02u.%03u", hh, mmv, ss, ms3);
            v.as_string = tmp;
            break;
        }
        case DbfFieldType::AdtDate: {
            // ADT type 3: 4-byte uint32 Julian Day Number → "YYYYMMDD". JDN 0 = null.
            if (field.length < 4) { v.as_string = make_string(p, field.length); break; }
            std::uint32_t jdn = static_cast<std::uint32_t>(p[0])        |
                               (static_cast<std::uint32_t>(p[1]) <<  8) |
                               (static_cast<std::uint32_t>(p[2]) << 16) |
                               (static_cast<std::uint32_t>(p[3]) << 24);
            if (jdn == 0) { v.is_null = true; break; }
            int y, mo, d;
            jdn_to_ymd(jdn, y, mo, d);
            char tmp[48];
            std::snprintf(tmp, sizeof(tmp), "%04d%02d%02d", y, mo, d);
            v.as_string = tmp;
            break;
        }
        case DbfFieldType::AdtTimestamp: {
            // ADT type 14: 4-byte JDN + 4-byte ms-since-midnight → "YYYYMMDDHHMMSS".
            if (field.length < 8) { v.as_string = make_string(p, field.length); break; }
            std::uint32_t jdn = static_cast<std::uint32_t>(p[0])        |
                               (static_cast<std::uint32_t>(p[1]) <<  8) |
                               (static_cast<std::uint32_t>(p[2]) << 16) |
                               (static_cast<std::uint32_t>(p[3]) << 24);
            std::uint32_t ms  = static_cast<std::uint32_t>(p[4])        |
                               (static_cast<std::uint32_t>(p[5]) <<  8) |
                               (static_cast<std::uint32_t>(p[6]) << 16) |
                               (static_cast<std::uint32_t>(p[7]) << 24);
            if (jdn == 0 && ms == 0) { v.is_null = true; break; }
            int y, mo, d;
            jdn_to_ymd(jdn, y, mo, d);
            unsigned hh  = ms / 3600000u;
            unsigned mmv = (ms % 3600000u) / 60000u;
            unsigned ss  = (ms % 60000u)   / 1000u;
            char tmp[48];
            std::snprintf(tmp, sizeof(tmp), "%04d%02d%02d%02u%02u%02u",
                          y, mo, d, hh, mmv, ss);
            v.as_string = tmp;
            break;
        }
        case DbfFieldType::AdtMoney: {
            // ADT type 18: 8-byte little-endian SIGNED64, value * 10000.
            // Same wire encoding as VFP Currency ('Y'), not IEEE754 double.
            if (field.length < 8) { v.as_string = make_string(p, field.length); break; }
            std::int64_t raw = read_i64_le(p);
            double money = static_cast<double>(raw) / 10000.0;
            char tmp[64];
            std::snprintf(tmp, sizeof(tmp), "%.4f", money);
            v.as_string  = tmp;
            v.as_double  = money;
            break;
        }
        case DbfFieldType::ModTime: {
            // ADT type 22: same physical layout as AdtTimestamp (4-byte JDN + 4-byte ms).
            if (field.length < 8) { v.as_string = make_string(p, field.length); break; }
            std::uint32_t jdn = static_cast<std::uint32_t>(p[0])        |
                               (static_cast<std::uint32_t>(p[1]) <<  8) |
                               (static_cast<std::uint32_t>(p[2]) << 16) |
                               (static_cast<std::uint32_t>(p[3]) << 24);
            std::uint32_t ms  = static_cast<std::uint32_t>(p[4])        |
                               (static_cast<std::uint32_t>(p[5]) <<  8) |
                               (static_cast<std::uint32_t>(p[6]) << 16) |
                               (static_cast<std::uint32_t>(p[7]) << 24);
            if (jdn == 0 && ms == 0) { v.is_null = true; break; }
            int y, mo, d;
            jdn_to_ymd(jdn, y, mo, d);
            unsigned hh  = ms / 3600000u;
            unsigned mmv = (ms % 3600000u) / 60000u;
            unsigned ss  = (ms % 60000u)   / 1000u;
            char tmp[48];
            std::snprintf(tmp, sizeof(tmp), "%04d%02d%02d%02u%02u%02u",
                          y, mo, d, hh, mmv, ss);
            v.as_string = tmp;
            break;
        }
        case DbfFieldType::RowVersion: {
            // ADT type 21: 8-byte little-endian uint64 record-version counter.
            if (field.length < 8) { v.as_string = make_string(p, field.length); break; }
            std::uint64_t rv = static_cast<std::uint64_t>(p[0])
                             | (static_cast<std::uint64_t>(p[1]) <<  8)
                             | (static_cast<std::uint64_t>(p[2]) << 16)
                             | (static_cast<std::uint64_t>(p[3]) << 24)
                             | (static_cast<std::uint64_t>(p[4]) << 32)
                             | (static_cast<std::uint64_t>(p[5]) << 40)
                             | (static_cast<std::uint64_t>(p[6]) << 48)
                             | (static_cast<std::uint64_t>(p[7]) << 56);
            v.as_string = std::to_string(rv);
            break;
        }

        case DbfFieldType::Unknown:
            v.as_string = make_string(p, field.length);
            break;
    }
    return v;
}

// M13 — test a field region against the ADT per-type NULL sentinel
// (representations documented at the declaration in dbf_common.h).
bool adt_field_is_null(const DbfField& f,
                       const std::uint8_t* rec, std::size_t rec_size) {
    if (static_cast<std::size_t>(f.record_offset) +
        static_cast<std::size_t>(f.length) > rec_size) {
        return false;
    }
    const std::uint8_t* p = rec + f.record_offset;
    auto all_bytes = [&](std::uint8_t b) {
        for (std::uint16_t i = 0; i < f.length; ++i) {
            if (p[i] != b) return false;
        }
        return f.length > 0;
    };
    switch (f.type) {
        case DbfFieldType::Character:
        case DbfFieldType::CiCharacter:
        case DbfFieldType::Numeric:
        case DbfFieldType::Float:
        case DbfFieldType::Memo:
        case DbfFieldType::Binary:
            return all_bytes(0x00);
        case DbfFieldType::Integer:
            return f.length >= 4 &&
                   static_cast<std::uint32_t>(read_i32_le(p)) == 0x80000000u;
        case DbfFieldType::ShortInt:
            return f.length >= 2 &&
                   (static_cast<std::uint16_t>(p[0]) |
                    (static_cast<std::uint16_t>(p[1]) << 8)) == 0x8000u;
        case DbfFieldType::Double:
            return f.length >= 8 && p[0] == 0x20 && p[1] == 0 && p[2] == 0 &&
                   p[3] == 0 && p[4] == 0 && p[5] == 0 && p[6] == 0 &&
                   p[7] == 0x80;
        case DbfFieldType::AdtMoney:
        case DbfFieldType::Currency:
            return f.length >= 8 &&
                   static_cast<std::uint64_t>(read_i64_le(p)) ==
                       0x8000000000000000ull;
        case DbfFieldType::AdtDate:
            return f.length >= 4 && read_i32_le(p) == 0;
        case DbfFieldType::Time:
            return f.length >= 4 &&
                   static_cast<std::uint32_t>(read_i32_le(p)) == 0xFFFFFFFFu;
        case DbfFieldType::AdtTimestamp:
        case DbfFieldType::ModTime:
        case DbfFieldType::DateTime:
            return all_bytes(0x00);
        case DbfFieldType::Logical:
            return f.length >= 1 && p[0] == 0x20;
        // AutoInc and RowVersion can never be NULL; Varchar/Varbinary use a
        // trailing length word (0xFFFF = NULL) but ADT VARCHAR (type 23) is
        // not classified yet, so no field reaches here with those types.
        default:
            return false;
    }
}

// M13 — write the ADT NULL sentinel for `f` into the record buffer.
util::Result<void> encode_field_null(const DbfField& f,
                                     std::uint8_t* rec, std::size_t rec_size) {
    if (static_cast<std::size_t>(f.record_offset) +
        static_cast<std::size_t>(f.length) > rec_size) {
        return util::Error{5000, 0, "field range past record buffer", ""};
    }
    std::uint8_t* p = rec + f.record_offset;
    switch (f.type) {
        case DbfFieldType::AutoInc:
        case DbfFieldType::RowVersion:
            // 5147 = AE_COLUMN_CANNOT_BE_NULL (SAP SDK).
            return util::Error{5147, 0, "column cannot be NULL", f.name};
        case DbfFieldType::Integer:
            if (f.length >= 4) { write_i32_le(p, static_cast<std::int32_t>(0x80000000u)); return {}; }
            break;
        case DbfFieldType::ShortInt:
            if (f.length >= 2) { p[0] = 0x00; p[1] = 0x80; return {}; }
            break;
        case DbfFieldType::Double:
            if (f.length >= 8) {
                std::memset(p, 0, 8);
                p[0] = 0x20;
                p[7] = 0x80;
                return {};
            }
            break;
        case DbfFieldType::AdtMoney:
        case DbfFieldType::Currency:
            if (f.length >= 8) {
                write_i64_le(p, static_cast<std::int64_t>(0x8000000000000000ull));
                return {};
            }
            break;
        case DbfFieldType::Time:
            if (f.length >= 4) { std::memset(p, 0xFF, 4); return {}; }
            break;
        case DbfFieldType::Logical:
            if (f.length >= 1) { p[0] = 0x20; return {}; }
            break;
        default:
            break;
    }
    // Character/CiCharacter/Numeric/AdtDate/AdtTimestamp/ModTime/Memo/
    // Binary — and any short-length fallthrough above — NULL as all zeros.
    std::memset(p, 0x00, f.length);
    return {};
}

bool record_is_deleted(const std::uint8_t* record_buf,
                       std::size_t record_size) noexcept {
    if (record_size == 0) return false;
    return record_buf[0] == '*';
}

std::vector<std::uint8_t> make_empty_record(std::uint16_t record_length) {
    return std::vector<std::uint8_t>(record_length, ' ');
}

// True when every character is a decimal digit (an empty string is not).
static bool all_digits_(const std::string& s) {
    return !s.empty() &&
           s.find_first_not_of("0123456789") == std::string::npos;
}

// "YYYYMMDD..." -> JDN, or 0 when the text is blank / not a valid date.
// 0 is what ADT stores for an empty date field.
static std::uint32_t parse_ymd_jdn_or_zero(const std::string& value) {
    if (value.size() < 8) return 0;
    const std::string sy = value.substr(0, 4);
    const std::string sm = value.substr(4, 2);
    const std::string sd = value.substr(6, 2);
    if (!all_digits_(sy) || !all_digits_(sm) || !all_digits_(sd)) return 0;
    const int y  = std::stoi(sy);
    const int mo = std::stoi(sm);
    const int d  = std::stoi(sd);
    if (y == 0 || mo < 1 || mo > 12 || d < 1 || d > 31) return 0;
    return ymd_to_jdn(y, mo, d);
}

util::Result<void> encode_field_string(const DbfField& f,
                                       std::uint8_t* rec, std::size_t rec_size,
                                       const std::string& value) {
    if (static_cast<std::size_t>(f.record_offset) +
        static_cast<std::size_t>(f.length) > rec_size) {
        return util::Error{5000, 0, "field range past record buffer", ""};
    }
    std::uint8_t* dst = rec + f.record_offset;

    // ADT binary-encoded date/timestamp: parse string and write raw LE.
    if (f.type == DbfFieldType::AdtDate && f.length >= 4 && value.size() >= 8) {
        // An EMPTY date arrives as 8 blanks (that is what AdsSetJulian(0) and a
        // blank DBF date field produce) and passes the size check above. Feeding
        // that to std::stoi throws std::invalid_argument, which escapes through
        // the C ABI and kills the caller's process — a COPY TO of any table with
        // one blank date crashed the application. Parse defensively and store
        // the ADT empty-date marker (0) for anything that is not a real date.
        std::uint32_t jdn = parse_ymd_jdn_or_zero(value);
        dst[0] = static_cast<std::uint8_t>( jdn        & 0xFF);
        dst[1] = static_cast<std::uint8_t>((jdn >>  8) & 0xFF);
        dst[2] = static_cast<std::uint8_t>((jdn >> 16) & 0xFF);
        dst[3] = static_cast<std::uint8_t>((jdn >> 24) & 0xFF);
        return {};
    }
    if (f.type == DbfFieldType::AdtTimestamp && f.length >= 8 && value.size() >= 14) {
        // Same defence as the date branch above: a blank timestamp must not
        // throw out of the C ABI.
        std::uint32_t jdn = parse_ymd_jdn_or_zero(value);
        int hh = 0, mmv = 0, ss = 0;
        if (all_digits_(value.substr(8, 2)))  hh  = std::stoi(value.substr(8, 2));
        if (all_digits_(value.substr(10, 2))) mmv = std::stoi(value.substr(10, 2));
        if (all_digits_(value.substr(12, 2))) ss  = std::stoi(value.substr(12, 2));
        std::uint32_t ms  = static_cast<std::uint32_t>(
                                hh * 3600000 + mmv * 60000 + ss * 1000);
        dst[0] = static_cast<std::uint8_t>( jdn        & 0xFF);
        dst[1] = static_cast<std::uint8_t>((jdn >>  8) & 0xFF);
        dst[2] = static_cast<std::uint8_t>((jdn >> 16) & 0xFF);
        dst[3] = static_cast<std::uint8_t>((jdn >> 24) & 0xFF);
        dst[4] = static_cast<std::uint8_t>( ms         & 0xFF);
        dst[5] = static_cast<std::uint8_t>((ms  >>  8) & 0xFF);
        dst[6] = static_cast<std::uint8_t>((ms  >> 16) & 0xFF);
        dst[7] = static_cast<std::uint8_t>((ms  >> 24) & 0xFF);
        return {};
    }

    // ADT/VFP binary-encoded numeric types: a string arriving over the
    // wire (remote SetField routes everything through AdsSetString on the
    // twin, see Session SetField) must be parsed and packed as binary,
    // not stored as ASCII — the generic memcpy branch below wrote the
    // literal characters into the 8-byte field and read-back as a binary
    // double/int came out as garbage (remote ADT numeric round-trip,
    // abi_adt_scope_validation_test).
    switch (f.type) {
        case DbfFieldType::Double:
            if (f.length >= 8) {
                write_f64_le(dst, value.empty() ? 0.0 : std::strtod(value.c_str(), nullptr));
                return {};
            }
            break;
        case DbfFieldType::Integer:
            if (f.length >= 4) {
                write_i32_le(dst, static_cast<std::int32_t>(
                    value.empty() ? 0 : std::strtol(value.c_str(), nullptr, 10)));
                return {};
            }
            break;
        case DbfFieldType::Currency:
        case DbfFieldType::AdtMoney:
            if (f.length >= 8) {
                write_i64_le(dst, static_cast<std::int64_t>(
                    (value.empty() ? 0.0 : std::strtod(value.c_str(), nullptr)) * 10000.0));
                return {};
            }
            break;
        case DbfFieldType::ShortInt:
            if (f.length >= 2) {
                auto sv = static_cast<std::int16_t>(
                    value.empty() ? 0 : std::strtol(value.c_str(), nullptr, 10));
                dst[0] = static_cast<std::uint8_t>( sv        & 0xFF);
                dst[1] = static_cast<std::uint8_t>((sv >>  8) & 0xFF);
                return {};
            }
            break;
        case DbfFieldType::AutoInc:
        case DbfFieldType::Time:
            if (f.length >= 4) {
                auto uv = static_cast<std::uint32_t>(
                    value.empty() ? 0 : std::strtoul(value.c_str(), nullptr, 10));
                dst[0] = static_cast<std::uint8_t>( uv        & 0xFF);
                dst[1] = static_cast<std::uint8_t>((uv >>  8) & 0xFF);
                dst[2] = static_cast<std::uint8_t>((uv >> 16) & 0xFF);
                dst[3] = static_cast<std::uint8_t>((uv >> 24) & 0xFF);
                return {};
            }
            break;
        default:
            break;
    }

    std::size_t n = std::min<std::size_t>(value.size(), f.length);
    std::memcpy(dst, value.data(), n);
    // M11.1 — VFP V / Q pad the unused tail with NUL so callers can
    // recover the actual length on read; everything else (Character,
    // Numeric, Date, ...) keeps the legacy space-pad behavior.
    std::uint8_t pad =
        (f.type == DbfFieldType::Varchar ||
         f.type == DbfFieldType::Varbinary)
            ? 0x00 : ' ';
    for (std::size_t i = n; i < f.length; ++i) dst[i] = pad;
    return {};
}

util::Result<void> encode_field_double(const DbfField& f,
                                       std::uint8_t* rec, std::size_t rec_size,
                                       double value) {
    if (static_cast<std::size_t>(f.record_offset) +
        static_cast<std::size_t>(f.length) > rec_size) {
        return util::Error{5000, 0, "field range past record buffer", ""};
    }
    std::uint8_t* dst = rec + f.record_offset;

    // VFP binary types take a fixed-width little-endian payload, not
    // an ASCII representation; route them through the right packer
    // before falling back to the Clipper-style ASCII numeric encoding.
    switch (f.type) {
        case DbfFieldType::Integer:
            if (f.length >= 4) {
                write_i32_le(dst, static_cast<std::int32_t>(value));
                return {};
            }
            break;
        case DbfFieldType::Currency:
            if (f.length >= 8) {
                write_i64_le(dst,
                    static_cast<std::int64_t>(value * 10000.0));
                return {};
            }
            break;
        case DbfFieldType::Double:
            if (f.length >= 8) {
                write_f64_le(dst, value);
                return {};
            }
            break;
        case DbfFieldType::AdtMoney:
            if (f.length >= 8) {
                write_i64_le(dst, static_cast<std::int64_t>(value * 10000.0));
                return {};
            }
            break;
        case DbfFieldType::ShortInt:
            if (f.length >= 2) {
                auto sv = static_cast<std::int16_t>(value);
                dst[0] = static_cast<std::uint8_t>( sv        & 0xFF);
                dst[1] = static_cast<std::uint8_t>((sv >>  8) & 0xFF);
                return {};
            }
            break;
        case DbfFieldType::AutoInc:
        case DbfFieldType::Time:
            if (f.length >= 4) {
                auto uv = static_cast<std::uint32_t>(value);
                dst[0] = static_cast<std::uint8_t>( uv        & 0xFF);
                dst[1] = static_cast<std::uint8_t>((uv >>  8) & 0xFF);
                dst[2] = static_cast<std::uint8_t>((uv >> 16) & 0xFF);
                dst[3] = static_cast<std::uint8_t>((uv >> 24) & 0xFF);
                return {};
            }
            break;
        default:
            break;
    }

    char tmp[64];
    int written = std::snprintf(tmp, sizeof(tmp), "%*.*f",
                                static_cast<int>(f.length),
                                static_cast<int>(f.decimals),
                                value);
    if (written < 0) {
        return util::Error{5000, 0, "snprintf failed encoding numeric", ""};
    }
    std::size_t n = static_cast<std::size_t>(written);
    if (n > f.length) n = f.length;
    std::memcpy(dst, tmp, n);
    for (std::size_t i = n; i < f.length; ++i) dst[i] = ' ';
    return {};
}

util::Result<void> encode_field_logical(const DbfField& f,
                                        std::uint8_t* rec, std::size_t rec_size,
                                        bool value) {
    if (static_cast<std::size_t>(f.record_offset) +
        static_cast<std::size_t>(f.length) > rec_size) {
        return util::Error{5000, 0, "field range past record buffer", ""};
    }
    rec[f.record_offset] = value ? 'T' : 'F';
    return {};
}

void set_record_deleted(std::uint8_t* rec, std::size_t rec_size,
                        bool deleted) noexcept {
    if (rec_size == 0) return;
    rec[0] = deleted ? '*' : ' ';
}

} // namespace openads::drivers
