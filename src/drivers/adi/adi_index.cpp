#include "drivers/adi/adi_index.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

namespace openads::drivers::adi {

namespace {

// ── Byte-level helpers ────────────────────────────────────────────────────────

std::uint16_t u16_le(const std::uint8_t* p) noexcept {
    return static_cast<std::uint16_t>(
        static_cast<unsigned>(p[0]) | (static_cast<unsigned>(p[1]) << 8));
}

std::uint32_t u32_le(const std::uint8_t* p) noexcept {
    return  static_cast<std::uint32_t>(p[0])
         | (static_cast<std::uint32_t>(p[1]) <<  8)
         | (static_cast<std::uint32_t>(p[2]) << 16)
         | (static_cast<std::uint32_t>(p[3]) << 24);
}

std::uint32_t u32_be(const std::uint8_t* p) noexcept {
    return (static_cast<std::uint32_t>(p[0]) << 24)
         | (static_cast<std::uint32_t>(p[1]) << 16)
         | (static_cast<std::uint32_t>(p[2]) <<  8)
         |  static_cast<std::uint32_t>(p[3]);
}

void set_u16_le(std::uint8_t* p, std::uint16_t v) noexcept {
    p[0] = static_cast<std::uint8_t>(v);
    p[1] = static_cast<std::uint8_t>(v >> 8);
}

void set_u32_le(std::uint8_t* p, std::uint32_t v) noexcept {
    p[0] = static_cast<std::uint8_t>(v);
    p[1] = static_cast<std::uint8_t>(v >>  8);
    p[2] = static_cast<std::uint8_t>(v >> 16);
    p[3] = static_cast<std::uint8_t>(v >> 24);
}

void set_u32_be(std::uint8_t* p, std::uint32_t v) noexcept {
    p[0] = static_cast<std::uint8_t>(v >> 24);
    p[1] = static_cast<std::uint8_t>(v >> 16);
    p[2] = static_cast<std::uint8_t>(v >>  8);
    p[3] = static_cast<std::uint8_t>(v);
}

// Dense entry recno from a raw byte buffer (not a Page), given 0-based idx.
// v2 leaf entry (esz >= 4: recno[4 LE] + key[klen]) carries a 4-byte recno
// (supports >65535 records). Legacy entries are 2 or 3 bytes (recno 2B / 1B).
std::uint32_t dense_recno_from_buf(const std::uint8_t* base, std::uint32_t idx,
                                   std::uint32_t esz) noexcept {
    const std::uint8_t* e = base + idx * esz;
    if (esz >= 4)
        return static_cast<std::uint32_t>(e[0])
             | (static_cast<std::uint32_t>(e[1]) << 8)
             | (static_cast<std::uint32_t>(e[2]) << 16)
             | (static_cast<std::uint32_t>(e[3]) << 24);
    if (esz >= 3)
        return static_cast<std::uint32_t>(e[0]) | (static_cast<std::uint32_t>(e[1]) << 8);
    return e[0];
}

// v2 dense-leaf entry key: klen opaque bytes right after the 4-byte recno.
// The full evaluated key lives in the leaf, so navigation/seek never re-read
// the ADT record (and compound/computed keys work without an evaluator).
std::string dense_entry_key_from_buf(const std::uint8_t* base, std::uint32_t idx,
                                     std::uint32_t esz,
                                     std::uint32_t klen) noexcept {
    return std::string(reinterpret_cast<const char*>(base + idx * esz + 4), klen);
}

// ── Front-coding codec for the v2 dense leaf ─────────────────────────────────
// A v2 dense leaf stores VARIABLE-length entries, each front-coded against the
// PREVIOUS entry to elide the shared key prefix (SAP-style dup/trail), so the
// .adi reaches parity with ADS-SAP (~3x smaller than storing the full key per
// entry). On-disk entry, from ADI_DENSE_ENTRY_START, in key order:
//   recno[4 LE] + dup[1] + suffix[(klen - dup) bytes]
//   dup    = number of leading bytes shared with the previous entry (0 = first)
//   suffix = key[dup .. klen]
// Reconstruction:  key = prev_key[0..dup] + suffix. The recno stays at offset 0
// of every entry. dup is a single byte, so the shared prefix is capped at 254;
// a wider key just front-codes less (still correct).

// Bytes of shared prefix between two klen-length keys, as written by the codec.
std::uint8_t fc_dup(const char* prev, const char* cur,
                    std::uint32_t klen) noexcept {
    const std::uint32_t cap = klen < 254u ? klen : 254u;
    std::uint32_t d = 0;
    while (d < cap && prev[d] == cur[d]) ++d;
    return static_cast<std::uint8_t>(d);
}

// Encode key-ordered (recno, key) entries into `body` (at most `cap` bytes).
// Each key must already be exactly klen bytes. Returns bytes written, or 0 if
// the set does not fit in cap (the caller then splits). An empty set writes 0.
std::size_t fc_encode_leaf(
        const std::vector<std::pair<std::uint32_t, std::string>>& es,
        std::uint32_t klen, std::uint8_t* body, std::size_t cap) noexcept {
    std::size_t off = 0;
    const char* prev = nullptr;
    for (const auto& e : es) {
        const char* k = e.second.data();
        std::uint8_t dup = prev ? fc_dup(prev, k, klen) : std::uint8_t{0};
        std::size_t suffix_len = static_cast<std::size_t>(klen) - dup;
        std::size_t need = 5u + suffix_len;
        if (off + need > cap) return 0;
        body[off]     = static_cast<std::uint8_t>( e.first        & 0xFFu);
        body[off + 1] = static_cast<std::uint8_t>((e.first >>  8) & 0xFFu);
        body[off + 2] = static_cast<std::uint8_t>((e.first >> 16) & 0xFFu);
        body[off + 3] = static_cast<std::uint8_t>((e.first >> 24) & 0xFFu);
        body[off + 4] = dup;
        std::memcpy(body + off + 5, k + dup, suffix_len);
        off += need;
        prev = k;
    }
    return off;
}

// Decode `count` front-coded entries from `body` into `out` (recno, full key).
void fc_decode_leaf(const std::uint8_t* body, std::uint16_t count,
                    std::uint32_t klen,
                    std::vector<std::pair<std::uint32_t, std::string>>& out) {
    out.clear();
    out.reserve(count);  // reserve up front: keeps the prev pointer below valid
    const std::string* prev = nullptr;
    std::size_t off = 0;
    for (std::uint16_t i = 0; i < count; ++i) {
        std::uint32_t recno =
              static_cast<std::uint32_t>(body[off])
            | (static_cast<std::uint32_t>(body[off + 1]) <<  8)
            | (static_cast<std::uint32_t>(body[off + 2]) << 16)
            | (static_cast<std::uint32_t>(body[off + 3]) << 24);
        std::uint32_t dup = body[off + 4];
        if (dup > klen) dup = klen;  // defensive against a corrupt dup byte
        std::size_t suffix_len = static_cast<std::size_t>(klen) - dup;
        std::string key;
        key.reserve(klen);
        if (dup && prev) key.assign(*prev, 0, dup);
        key.append(reinterpret_cast<const char*>(body + off + 5), suffix_len);
        if (key.size() < klen) key.append(klen - key.size(), ' ');
        out.emplace_back(recno, std::move(key));
        prev = &out.back().second;
        off += 5u + suffix_len;
    }
}

// Choose a split index in [1, n-1] so BOTH halves front-code within cap, as
// balanced as possible. Returns 0 only when no single-page split exists (one
// entry alone exceeds cap — a degenerate, far-too-wide key).
std::size_t fc_pick_split(
        const std::vector<std::pair<std::uint32_t, std::string>>& es,
        std::uint32_t klen, std::size_t cap) noexcept {
    const std::size_t n = es.size();
    if (n < 2) return 0;
    // The first entry of a leaf always costs 5+klen (dup=0); a later entry i
    // costs 5 + (klen - dup(i-1,i)). size([lo,hi)) = first_cost + Σ inc(lo+1..hi-1).
    const std::size_t first_cost = 5u + klen;
    std::vector<std::size_t> pref(n, 0);  // pref[i] = Σ_{j=1..i} inc(j)
    for (std::size_t i = 1; i < n; ++i) {
        std::uint8_t dup = fc_dup(es[i - 1].second.data(),
                                  es[i].second.data(), klen);
        pref[i] = pref[i - 1] + (5u + (static_cast<std::size_t>(klen) - dup));
    }
    auto fits = [&](std::size_t lo, std::size_t hi) {
        return first_cost + (pref[hi - 1] - pref[lo]) <= cap;
    };
    const std::size_t mid = n / 2;
    for (std::size_t d = 0; d < n; ++d) {
        std::size_t up = mid + d;
        if (up >= 1 && up <= n - 1 && fits(0, up) && fits(up, n)) return up;
        if (mid >= d) {
            std::size_t dn = mid - d;
            if (dn >= 1 && dn <= n - 1 && fits(0, dn) && fits(dn, n)) return dn;
        }
    }
    return 0;
}

platform::OpenMode map_open_mode(IndexOpenMode m) noexcept {
    if (m == IndexOpenMode::ReadOnly) return platform::OpenMode::ReadOnly;
    return platform::OpenMode::OpenExisting;
}

// Derive the companion data-file path from the .adi path.
// Standard ADT uses a .adt data file, but an application may keep ADT data
// under another extension (ExtFile='.DAT' is a common ERP convention), so when
// <stem>.adt is absent fall back to <stem>.DAT / <stem>.dat. This is the safety
// net for every path that derives the data file implicitly -- open / open_named
// / list_tags / add_tag / create -- when the caller passed no explicit adt_path.
std::string adt_path_for(const std::string& adi_path) {
    namespace fs = std::filesystem;
    fs::path p(adi_path);
    p.replace_extension(".adt");
    std::error_code ec;
    if (!fs::exists(p, ec)) {
        for (const char* ext : {".DAT", ".dat"}) {
            fs::path d(adi_path);
            d.replace_extension(ext);
            if (fs::exists(d, ec)) return d.string();
        }
    }
    return p.string();
}

// ── ADI page-header helpers ───────────────────────────────────────────────────

std::uint16_t page_level(const std::uint8_t* pg) noexcept { return u16_le(pg);   }
std::uint16_t page_count(const std::uint8_t* pg) noexcept { return u16_le(pg+2); }
std::uint32_t page_lsib (const std::uint8_t* pg) noexcept { return u32_le(pg+4); }
std::uint32_t page_rsib (const std::uint8_t* pg) noexcept { return u32_le(pg+8); }

// ── Numeric branch entry (level 0 and 1): starts at offset 12 ───────────────
// Format: key[8](BE sign-flipped float64) + cum[4](BE uint32) + page_no[4](BE uint32)

std::uint32_t tree_entry_page(const std::uint8_t* pg, int idx) noexcept {
    const std::uint8_t* e = pg + ADI_TREE_ENTRY_START
                            + static_cast<std::uint32_t>(idx) * ADI_TREE_ENTRY_SIZE;
    return u32_be(e + 12);
}

const std::uint8_t* tree_entry_key(const std::uint8_t* pg, int idx) noexcept {
    return pg + ADI_TREE_ENTRY_START
           + static_cast<std::uint32_t>(idx) * ADI_TREE_ENTRY_SIZE;
}

// ── Character branch entry (level 1 for char-key ADI) ────────────────────────
// Format: padded_key[key_padded_len] + cum[4 LE] + page[1]

std::uint32_t char_tree_entry_page(const std::uint8_t* pg, int idx,
                                   std::uint32_t entry_sz,
                                   std::uint32_t key_padded_len) noexcept {
    const std::uint8_t* e = pg + ADI_TREE_ENTRY_START
                            + static_cast<std::uint32_t>(idx) * entry_sz;
    // Child page number: 4 bytes little-endian (mirrors the numeric branch
    // entry's 4-byte page). A previous 1-byte read capped char-key indexes at
    // 256 pages and silently truncated larger page numbers -> corrupt tree.
    const std::uint8_t* p = e + key_padded_len + 4;
    return static_cast<std::uint32_t>(p[0])
         | (static_cast<std::uint32_t>(p[1]) << 8)
         | (static_cast<std::uint32_t>(p[2]) << 16)
         | (static_cast<std::uint32_t>(p[3]) << 24);
}

// ── Dense-leaf entry: starts at offset 24 ────────────────────────────────────
// Format (entry_sz=3, wider key fields): recno[2 LE] + type_byte[1]
// Format (entry_sz=2, 1-byte key fields): recno[1] + key_flags[1]
// Dense-leaf recno layout verified against reference ADI fixtures.

std::uint32_t dense_entry_recno(const std::uint8_t* pg, int idx,
                                std::uint32_t entry_sz) noexcept {
    const std::uint8_t* e = pg + ADI_DENSE_ENTRY_START
                            + static_cast<std::uint32_t>(idx) * entry_sz;
    if (entry_sz >= 4)  // v2: recno[4 LE] + key[klen]
        return static_cast<std::uint32_t>(e[0])
             | (static_cast<std::uint32_t>(e[1]) << 8)
             | (static_cast<std::uint32_t>(e[2]) << 16)
             | (static_cast<std::uint32_t>(e[3]) << 24);
    if (entry_sz >= 3)
        return static_cast<std::uint32_t>(e[0]) | (static_cast<std::uint32_t>(e[1]) << 8);
    return e[0];  // 2-byte entry: recno in byte 0, byte 1 is key-flags
}

// ── Key encoding ─────────────────────────────────────────────────────────────

} // namespace (helpers above)

// Pack a double into an 8-byte IEEE 754 total-order big-endian ADI key.
// Positive values: flip sign bit only (0x80).
// Negative values: flip all bits so they sort before positives and among
// themselves in the correct order (most-negative first).
std::string pack_double_key(double v) {
    std::uint8_t raw[8];
    std::memcpy(raw, &v, 8);               // raw = IEEE754 LE on x86
    std::reverse(raw, raw + 8);            // LE → BE
    if (raw[0] & 0x80u) {
        for (auto& b : raw) b = static_cast<std::uint8_t>(~b);  // negative: flip all bits
    } else {
        raw[0] ^= 0x80u;                   // non-negative: flip sign bit only
    }
    return std::string(reinterpret_cast<char*>(raw), 8);
}

std::string pack_u64_key(std::uint64_t v) {
    std::uint8_t raw[8];
    for (int i = 0; i < 8; ++i)
        raw[7 - i] = static_cast<std::uint8_t>((v >> (i * 8)) & 0xFFu);
    raw[0] ^= 0x80u;
    return std::string(reinterpret_cast<char*>(raw), 8);
}

namespace {

// Encode an ADT field value to ADI key bytes, given ADT type and field data.
// For character types (CICHAR, CHAR): returns the raw field bytes (length bytes).
// For numeric types: returns an 8-byte IEEE 754 total-order BE key.
std::string encode_adt_key(std::uint16_t adt_type, const std::uint8_t* data,
                           std::uint16_t length) {
    // Character types: key is the raw field data (space-padded in ADT already)
    if (adt_type == ADT_TYPE_CICHAR || adt_type == ADT_TYPE_CHAR) {
        return std::string(reinterpret_cast<const char*>(data), length);
    }

    double val = 0.0;
    switch (adt_type) {
        case ADT_TYPE_DATE: {
            std::uint32_t jdn = u32_le(data);
            val = static_cast<double>(jdn);
            break;
        }
        case ADT_TYPE_AUTOINC:
        case ADT_TYPE_TIME: {
            std::uint32_t v = u32_le(data);
            val = static_cast<double>(v);
            break;
        }
        case ADT_TYPE_INTEGER: {
            std::int32_t v = static_cast<std::int32_t>(u32_le(data));
            val = static_cast<double>(v);
            break;
        }
        case ADT_TYPE_SHORTINT: {
            std::int16_t v = static_cast<std::int16_t>(u16_le(data));
            val = static_cast<double>(v);
            break;
        }
        case ADT_TYPE_LOGICAL: {
            const unsigned char c = length > 0 ? data[0] : 0;
            const bool truthy = (c == 'T' || c == 't' || c == 'Y' || c == 'y' ||
                                 c == '1' || c == 1);
            val = truthy ? 1.0 : 0.0;
            break;
        }
        case ADT_TYPE_MONEY: {
            if (length < 8) break;
            std::int64_t raw = 0;
            std::memcpy(&raw, data, 8);
            return pack_double_key(static_cast<double>(raw));
        }
        case ADT_TYPE_DOUBLE: {
            if (length < 8) break;
            double v;
            std::memcpy(&v, data, 8);
            return pack_double_key(v);
        }
        case ADT_TYPE_TIMESTAMP:
        case ADT_TYPE_MODTIME: {
            if (length < 8) return pack_u64_key(0);
            // Index total-order: JDN in high dword, ms in low (on-disk is the
            // reverse — bytes 0..3 JDN, 4..7 ms).
            const std::uint32_t jdn = u32_le(data);
            const std::uint32_t ms  = u32_le(data + 4);
            const std::uint64_t v =
                (static_cast<std::uint64_t>(jdn) << 32) |
                static_cast<std::uint64_t>(ms);
            return pack_u64_key(v);
        }
        default:
            (void)length;
            return std::string(8, '\0');
    }
    return pack_double_key(val);
}

// ── ADT header parsing (minimal) ─────────────────────────────────────────────

struct AdtFieldDesc {
    std::string   name;
    std::uint16_t type;
    std::uint16_t offset;
    std::uint16_t length;
};

// Read the first 400+num_fields*200 bytes of the ADT file, return field list.
util::Result<std::vector<AdtFieldDesc>>
read_adt_fields(platform::File& f, std::uint32_t& hdr_len_out,
                                   std::uint32_t& rec_len_out) {
    // Minimal header read: first 400 bytes
    std::uint8_t buf[400];
    auto got = f.read_at(0, buf, sizeof(buf));
    if (!got || got.value() < sizeof(buf))
        return util::Error{6106, 0, "ADT header truncated", ""};

    std::uint32_t hdr_len = u32_le(buf + 32);
    std::uint32_t rec_len = u32_le(buf + 36);
    hdr_len_out = hdr_len;
    rec_len_out = rec_len;

    std::uint32_t num_fields = (hdr_len - 400) / 200;
    std::vector<std::uint8_t> fld_buf(num_fields * 200);
    got = f.read_at(400, fld_buf.data(), fld_buf.size());
    if (!got || got.value() < fld_buf.size())
        return util::Error{6106, 0, "ADT field descriptors truncated", ""};

    std::vector<AdtFieldDesc> fields;
    fields.reserve(num_fields);
    for (std::uint32_t i = 0; i < num_fields; ++i) {
        const std::uint8_t* d = fld_buf.data() + i * 200;
        AdtFieldDesc fd;
        // Name: null-terminated at bytes 0-127
        std::size_t nlen = 0;
        while (nlen < 128 && d[nlen]) ++nlen;
        fd.name   = std::string(reinterpret_cast<const char*>(d), nlen);
        fd.type   = u16_le(d + 129);
        fd.offset = u16_le(d + 131);
        fd.length = u16_le(d + 135);
        fields.push_back(std::move(fd));
    }
    return fields;
}

// ── F-marker scanning ─────────────────────────────────────────────────────────

// Read one 512-byte page from an already-open ADI file.
util::Result<AdiIndex::Page> read_one_page(platform::File& f,
                                           std::uint32_t page_no) {
    AdiIndex::Page pg{};
    auto got = f.read_at(static_cast<std::uint64_t>(page_no) * ADI_PAGE_SIZE,
                         pg.data(), pg.size());
    if (!got) return got.error();
    if (got.value() < ADI_PAGE_SIZE)
        return util::Error{6106, 0, "short ADI page read", ""};
    return pg;
}

// Parse an F-marker page and return ALL 1-based field numbers it encodes.
// Single-field example:  "F1\0..."   → [1]
// Compound-field example: "F2;F14\0" → [2, 14]
// Returns an empty vector for invalid/unrecognised markers.
std::vector<std::uint8_t> parse_fmarker_all(const AdiIndex::Page& pg) {
    if (pg[0] != 'F') return {};
    std::vector<std::uint8_t> result;
    std::size_t i = 1;  // skip leading 'F'
    while (i < ADI_PAGE_SIZE) {
        if (pg[i] < '1' || pg[i] > '9') break;
        std::uint32_t n = 0;
        while (i < ADI_PAGE_SIZE && pg[i] >= '0' && pg[i] <= '9')
            n = n * 10 + (pg[i++] - '0');
        if (n > 0 && n <= 255) result.push_back(static_cast<std::uint8_t>(n));
        // Compound separator: ';' followed by 'F'
        if (i + 1 < ADI_PAGE_SIZE && pg[i] == ';' && pg[i+1] == 'F')
            i += 2;
        else
            break;
    }
    return result;
}

// ── v2 (OpenADS-proprietary) per-tag metadata ────────────────────────────────
// Stored in the per-tag header page (page XX). Gives tag identity by NAME and
// persists the key expression + FOR condition + full key length across reopen
// (the legacy format only stored an F-marker field number). Region lives at
// offsets 40..495, clear of the legacy control bytes (0..23) and footer
// (506/510). Pages without the magic are legacy and fall back to field identity.
constexpr std::uint16_t ADI_V2_MAGIC      = 0xAD32;
constexpr std::size_t   ADI_V2_OFF_MAGIC  = 40;   // u16 LE magic
constexpr std::size_t   ADI_V2_OFF_KEYLEN = 42;   // u16 LE full key length (klen)
constexpr std::size_t   ADI_V2_OFF_FLAGS  = 44;   // u8: bit0 unique, bit1 descending
constexpr std::size_t   ADI_V2_OFF_NLEN   = 48;   // u16 LE len(tag_name)
constexpr std::size_t   ADI_V2_OFF_ELEN   = 50;   // u16 LE len(key_expr)
constexpr std::size_t   ADI_V2_OFF_FLEN   = 52;   // u16 LE len(for_expr)
constexpr std::size_t   ADI_V2_OFF_STRS   = 64;   // tag_name | key_expr | for_expr
constexpr std::size_t   ADI_V2_STRS_MAX   = 431;  // 64..495 inclusive

struct AdiV2Meta {
    bool          has = false;
    std::string   tag_name, key_expr, for_expr;
    std::uint16_t key_len = 0;
    bool          unique = false, descending = false;
};

void write_adi_v2_meta(AdiIndex::Page& pg,
                       const AdiIndex::CreateParams& cp) noexcept {
    // Only stamp the v2 region when the caller actually supplied v2 data (the
    // ERP / ACE path sets key_len + tag_name). Legacy callers leave it absent so
    // the page reads as a pure field-identity legacy tag.
    if (cp.key_len == 0 && cp.tag_name.empty()) return;
    std::string nm = cp.tag_name, ke = cp.key_expr, fe = cp.for_expr;
    if (nm.size() > 63) nm.resize(63);
    if (nm.size() + ke.size() + fe.size() > ADI_V2_STRS_MAX) {
        if (ke.size() > 254) ke.resize(254);
        std::size_t used = nm.size() + ke.size();
        std::size_t room = used < ADI_V2_STRS_MAX ? ADI_V2_STRS_MAX - used : 0;
        if (fe.size() > room) fe.resize(room);
    }
    set_u16_le(pg.data() + ADI_V2_OFF_MAGIC, ADI_V2_MAGIC);
    set_u16_le(pg.data() + ADI_V2_OFF_KEYLEN, cp.key_len);
    std::uint8_t flags = 0;
    if (cp.unique)     flags |= 0x01u;
    if (cp.descending) flags |= 0x02u;
    pg[ADI_V2_OFF_FLAGS] = flags;
    set_u16_le(pg.data() + ADI_V2_OFF_NLEN, static_cast<std::uint16_t>(nm.size()));
    set_u16_le(pg.data() + ADI_V2_OFF_ELEN, static_cast<std::uint16_t>(ke.size()));
    set_u16_le(pg.data() + ADI_V2_OFF_FLEN, static_cast<std::uint16_t>(fe.size()));
    std::size_t o = ADI_V2_OFF_STRS;
    std::memcpy(pg.data() + o, nm.data(), nm.size()); o += nm.size();
    std::memcpy(pg.data() + o, ke.data(), ke.size()); o += ke.size();
    std::memcpy(pg.data() + o, fe.data(), fe.size());
}

AdiV2Meta read_adi_v2_meta(const AdiIndex::Page& pg) noexcept {
    AdiV2Meta m;
    if (u16_le(pg.data() + ADI_V2_OFF_MAGIC) != ADI_V2_MAGIC) return m;
    std::size_t nl = u16_le(pg.data() + ADI_V2_OFF_NLEN);
    std::size_t el = u16_le(pg.data() + ADI_V2_OFF_ELEN);
    std::size_t fl = u16_le(pg.data() + ADI_V2_OFF_FLEN);
    if (nl + el + fl > ADI_V2_STRS_MAX) return m;  // corrupt → treat as legacy
    m.has        = true;
    m.key_len    = u16_le(pg.data() + ADI_V2_OFF_KEYLEN);
    std::uint8_t flags = pg[ADI_V2_OFF_FLAGS];
    m.unique     = (flags & 0x01u) != 0;
    m.descending = (flags & 0x02u) != 0;
    std::size_t o = ADI_V2_OFF_STRS;
    m.tag_name.assign(reinterpret_cast<const char*>(pg.data() + o), nl); o += nl;
    m.key_expr.assign(reinterpret_cast<const char*>(pg.data() + o), el); o += el;
    m.for_expr.assign(reinterpret_cast<const char*>(pg.data() + o), fl);
    return m;
}

// One entry in the tag directory scan result.
struct TagEntry {
    std::vector<std::uint8_t> fnums;  // 1-based field numbers (≥1 element)
    std::uint32_t             root_pg;
    bool                      unique = false;  // bit 0 of byte[14] in per-tag header page
    AdiV2Meta                 v2;               // v2 metadata (v2.has == false in legacy)
};

// Scan tag directory (page 2) and return all tag entries.
util::Result<std::vector<TagEntry>>
scan_tagdir(platform::File& adi_f) {
    AdiIndex::Page pg2;
    auto got = adi_f.read_at(2 * ADI_PAGE_SIZE, pg2.data(), pg2.size());
    if (!got || got.value() < ADI_PAGE_SIZE)
        return util::Error{6106, 0, "can't read ADI tag directory", ""};

    std::uint16_t count = u16_le(pg2.data() + 2);
    std::vector<TagEntry> tags;
    tags.reserve(count);

    for (std::uint16_t i = 0; i < count; ++i) {
        std::size_t off = ADI_TAGDIR_ENTRY_START + i * ADI_TAGDIR_ENTRY_SIZE;
        if (off + ADI_TAGDIR_ENTRY_SIZE > ADI_PAGE_SIZE) break;
        // Per-tag header page, u32 LE over the entry's first four bytes.
        // It used to be read (and written) as a single byte, which silently
        // dropped every tag whose header landed past page 255 — i.e. every
        // tag but the first, since each add_tag appends its pages at the end
        // of a bag that is already megabytes long. The three high bytes were
        // always zero in the old layout, so a u32 read of a legacy entry
        // yields exactly the byte it used to yield.
        std::uint32_t xx      = u32_le(pg2.data() + off);
        std::uint32_t fmk_pg  = xx + 1u;
        std::uint32_t root_pg = fmk_pg + 1u;

        auto fmk = read_one_page(adi_f, fmk_pg);
        if (!fmk) continue;
        auto fnums = parse_fmarker_all(fmk.value());
        if (fnums.empty()) continue;

        // Per-tag header is at page xx. Byte[14] bit 0 = unique flag; the v2
        // region (if present) carries tag name / expr / FOR / klen.
        bool uniq = false;
        AdiV2Meta v2;
        auto hdr_pg = read_one_page(adi_f, static_cast<std::uint32_t>(xx));
        if (hdr_pg) {
            uniq = (hdr_pg.value()[14] & 0x01u) != 0;
            v2 = read_adi_v2_meta(hdr_pg.value());
            if (v2.has) uniq = v2.unique;
        }

        tags.push_back({std::move(fnums), root_pg, uniq, std::move(v2)});
    }
    return tags;
}

} // anonymous namespace

// ── AdiIndex::read_adi_page_ ─────────────────────────────────────────────────

util::Result<void> AdiIndex::read_adi_page_(std::uint32_t page_no,
                                            Page& buf) {
    auto got = adi_file_.read_at(
        static_cast<std::uint64_t>(page_no) * ADI_PAGE_SIZE,
        buf.data(), buf.size());
    if (!got) return got.error();
    if (got.value() < ADI_PAGE_SIZE)
        return util::Error{6106, 0, "short ADI page read", ""};
    return {};
}

// ── AdiIndex::load_dense_leaf_ ───────────────────────────────────────────────

// Defined later in this file; needed by render_v2_leaf_ below.
void write_empty_dense_leaf_page(AdiIndex::Page& pg, std::uint16_t adt_type,
                                 std::uint16_t fld_length) noexcept;

util::Result<void> AdiIndex::load_dense_leaf_(std::uint32_t page_no) {
    Page pg{};
    if (auto r = read_adi_page_(page_no, pg); !r) return r;
    adopt_leaf_page_(page_no, pg);
    return {};
}

// ── AdiIndex::adopt_leaf_page_ ───────────────────────────────────────────────

void AdiIndex::adopt_leaf_page_(std::uint32_t page_no, const Page& pg) {
    cur_page_ = pg;
    cur_pg_   = page_no;
    cur_cnt_  = page_count(pg.data());
    cur_lsib_ = page_lsib(pg.data());
    cur_rsib_ = page_rsib(pg.data());
    cur_idx_  = -1;
    if (key_in_leaf_)
        fc_decode_leaf(pg.data() + ADI_DENSE_ENTRY_START, cur_cnt_,
                       key_total_len_, leaf_entries_);
    else
        leaf_entries_.clear();
}

// ── AdiIndex::render_v2_leaf_ ────────────────────────────────────────────────

bool AdiIndex::render_v2_leaf_(
        Page& pg,
        const std::vector<std::pair<std::uint32_t, std::string>>& ents,
        std::uint32_t lsib, std::uint32_t rsib) const {
    // Header + sub-header (and a zeroed body so any unused tail stays clean).
    write_empty_dense_leaf_page(pg, adt_type_, fld_length_);
    set_u16_le(pg.data() + 2, static_cast<std::uint16_t>(ents.size()));
    set_u32_le(pg.data() + 4, lsib);
    set_u32_le(pg.data() + 8, rsib);
    std::size_t n = fc_encode_leaf(ents, key_total_len_,
                                   pg.data() + ADI_DENSE_ENTRY_START,
                                   ADI_PAGE_SIZE - ADI_DENSE_ENTRY_START);
    return ents.empty() || n > 0;  // n==0 with a non-empty run = page overflow
}

// ── AdiIndex::refresh_current_ ───────────────────────────────────────────────

util::Result<void> AdiIndex::refresh_current_() {
    if (cur_pg_ == ADI_INVALID_PAGE || cur_idx_ < 0 ||
        cur_idx_ >= static_cast<std::int32_t>(cur_cnt_)) {
        cur_recno_ = 0;
        current_key_.clear();
        return {};
    }
    if (key_in_leaf_) {
        // v2: the decoded front-coded leaf is the source of truth.
        if (static_cast<std::size_t>(cur_idx_) >= leaf_entries_.size()) {
            cur_recno_ = 0;
            current_key_.clear();
            return {};
        }
        cur_recno_   = leaf_entries_[static_cast<std::size_t>(cur_idx_)].first;
        current_key_ = leaf_entries_[static_cast<std::size_t>(cur_idx_)].second;
        return {};
    }
    cur_recno_ = dense_entry_recno(cur_page_.data(), cur_idx_, entry_size_);
    auto k = key_for_recno_(cur_recno_);
    if (!k) return k.error();
    current_key_ = std::move(k).value();
    return {};
}

// ── AdiIndex::entry_count ────────────────────────────────────────────────────

const std::vector<std::uint32_t>& AdiIndex::ordered_recnos_cached() {
    if (pos_cache_valid_) return ordered_recnos_;
    ordered_recnos_.clear();
    pos_of_recno_.clear();
    // Descend to the leftmost dense leaf.
    Page pg{};
    std::uint32_t cur = root_page_;
    for (;;) {
        if (!read_adi_page_(cur, pg)) { pos_cache_valid_ = true; return ordered_recnos_; }
        if (is_dense_leaf(page_level(pg.data()))) break;
        if (page_count(pg.data()) == 0) { pos_cache_valid_ = true; return ordered_recnos_; }
        cur = branch_entry_page_(pg.data(), 0);
    }
    // Walk the dense-leaf chain left-to-right, collecting recnos in key order.
    std::uint32_t guard = 0;
    while (cur != ADI_INVALID_PAGE) {
        if (!read_adi_page_(cur, pg)) break;
        std::uint16_t cnt = page_count(pg.data());
        if (key_in_leaf_) {
            // v2: front-coded leaf — decode to recover recnos in key order.
            std::vector<std::pair<std::uint32_t, std::string>> ents;
            fc_decode_leaf(pg.data() + ADI_DENSE_ENTRY_START, cnt,
                           key_total_len_, ents);
            for (const auto& e : ents) {
                pos_of_recno_[e.first] =
                    static_cast<std::uint32_t>(ordered_recnos_.size());
                ordered_recnos_.push_back(e.first);
            }
        } else {
            for (std::uint16_t i = 0; i < cnt; ++i) {
                std::uint32_t rn = dense_entry_recno(pg.data(), i, entry_size_);
                pos_of_recno_[rn] =
                    static_cast<std::uint32_t>(ordered_recnos_.size());
                ordered_recnos_.push_back(rn);
            }
        }
        cur = page_rsib(pg.data());
        if (++guard > (1u << 24)) break;  // anti-loop on a corrupt rsib chain
    }
    pos_cache_valid_ = true;
    return ordered_recnos_;
}

std::uint32_t AdiIndex::pos_of_recno_cached(std::uint32_t recno) {
    ordered_recnos_cached();  // ensure built
    auto it = pos_of_recno_.find(recno);
    return it == pos_of_recno_.end() ? 0xFFFFFFFFu : it->second;
}

util::Result<std::uint32_t> AdiIndex::entry_count() {
    return static_cast<std::uint32_t>(ordered_recnos_cached().size());
}

// ── AdiIndex::branch_entry_page_ ────────────────────────────────────────────

std::uint32_t AdiIndex::branch_entry_page_(const std::uint8_t* pg,
                                           int idx) const noexcept {
    if (char_key_)
        return char_tree_entry_page(pg, idx, branch_entry_sz_,
                                    char_key_padded_len_);
    return tree_entry_page(pg, idx);
}

// ── AdiIndex::key_for_recno_ ─────────────────────────────────────────────────

util::Result<std::string> AdiIndex::key_for_recno_(std::uint32_t recno) {
    if (recno == 0 || adt_rec_len_ == 0)
        return std::string(key_total_len_, '\0');

    // ADT records are 1-based; offset past the 1-byte deleted flag is in fld_offset_
    std::uint64_t rec_off = static_cast<std::uint64_t>(adt_hdr_len_)
                          + static_cast<std::uint64_t>(recno - 1) * adt_rec_len_;

    // Build the full (possibly compound) key by concatenating all components.
    std::string result;
    result.reserve(key_total_len_);

    for (const auto& kf : key_fields_) {
        std::vector<std::uint8_t> buf(kf.length);
        auto got = adt_file_.read_at(rec_off + kf.offset, buf.data(), kf.length);
        if (!got || got.value() < kf.length) {
            // Unreadable field: pad with zeros / spaces
            bool is_c = (kf.type == ADT_TYPE_CICHAR || kf.type == ADT_TYPE_CHAR);
            result.append(is_c ? kf.length : 8u, is_c ? ' ' : '\0');
        } else {
            result += encode_adt_key(kf.type, buf.data(), kf.length);
        }
    }
    return result;
}

// ── AdiIndex::compare_keys_ ─────────────────────────────────────────────────

std::string AdiIndex::fold_for_compare_(const std::string& k) const {
    if (!has_ci_component_) return k;
    std::string out = k;
    std::size_t pos = 0;
    for (const auto& kf : key_fields_) {
        const bool is_c =
            (kf.type == ADT_TYPE_CICHAR || kf.type == ADT_TYPE_CHAR);
        const std::size_t seg = is_c ? kf.length : 8u;
        if (kf.type == ADT_TYPE_CICHAR) {
            const std::size_t end = std::min(pos + seg, out.size());
            for (std::size_t i = pos; i < end; ++i)
                out[i] = static_cast<char>(std::toupper(
                    static_cast<unsigned char>(out[i])));
        }
        pos += seg;
    }
    return out;
}

int AdiIndex::compare_keys_(const std::string& a,
                             const std::string& b) const noexcept {
    // CICHAR components collate case-insensitively (SAP semantics); the
    // fold is a no-op for indexes with no CICHAR component.
    const std::string fa = fold_for_compare_(a);
    const std::string fb = fold_for_compare_(b);
    std::size_t len = std::min({fa.size(), fb.size(),
                                static_cast<std::size_t>(key_total_len_)});
    return std::memcmp(fa.data(), fb.data(), len);
}

// ── AdiIndex::make_positioned_ ──────────────────────────────────────────────

SeekOutcome AdiIndex::make_positioned_() const {
    SeekOutcome o;
    o.hit        = SeekHit::Exact;
    o.recno      = cur_recno_;
    o.positioned = true;
    return o;
}

// ── AdiIndex::navigate_leftmost_ ────────────────────────────────────────────

util::Result<SeekOutcome> AdiIndex::navigate_leftmost_() {
    Page pg{};
    std::uint32_t cur = root_page_;
    for (;;) {
        if (auto r = read_adi_page_(cur, pg); !r) return r.error();
        std::uint16_t lv = page_level(pg.data());
        std::uint16_t ct = page_count(pg.data());
        if (ct == 0) {
            SeekOutcome o; o.hit = SeekHit::AfterEnd; return o;
        }
        if (is_dense_leaf(lv)) {
            adopt_leaf_page_(cur, pg);
            cur_idx_ = 0;
            if (auto r = refresh_current_(); !r) return r.error();
            return make_positioned_();
        }
        // Branch or sparse leaf: follow the first entry's child page.
        cur = branch_entry_page_(pg.data(), 0);
    }
}

// ── AdiIndex::navigate_rightmost_ ──────────────────────────────────────────

util::Result<SeekOutcome> AdiIndex::navigate_rightmost_() {
    Page pg{};
    std::uint32_t cur = root_page_;
    for (;;) {
        if (auto r = read_adi_page_(cur, pg); !r) return r.error();
        std::uint16_t lv = page_level(pg.data());
        std::uint16_t ct = page_count(pg.data());
        if (ct == 0) {
            SeekOutcome o; o.hit = SeekHit::AfterEnd; return o;
        }
        if (is_dense_leaf(lv)) {
            adopt_leaf_page_(cur, pg);
            cur_idx_ = static_cast<std::int32_t>(cur_cnt_) - 1;
            if (auto r = refresh_current_(); !r) return r.error();
            return make_positioned_();
        }
        cur = branch_entry_page_(pg.data(), static_cast<int>(ct) - 1);
    }
}

// ── IIndex public navigation ─────────────────────────────────────────────────

// ── AdiIndex::apply_tag_ ────────────────────────────────────────────────────

util::Result<void> AdiIndex::apply_tag_(
    const std::vector<std::uint8_t>& fnums,
    std::uint32_t                    root_pg,
    const std::vector<std::uint16_t>& fd_types,
    const std::vector<std::uint16_t>& fd_offsets,
    const std::vector<std::uint16_t>& fd_lengths,
    const std::vector<std::string>&   fd_names,
    std::uint32_t hlen, std::uint32_t rlen,
    bool unique,
    std::uint32_t v2_key_len)
{
    if (fnums.empty())
        return util::Error{5004, 0, "ADI tag has no field numbers", ""};

    key_fields_.clear();
    has_ci_component_ = false;
    std::uint32_t total_key_len = 0;
    bool first = true;

    for (std::uint8_t fnum : fnums) {
        if (fnum == 0 || fnum > static_cast<unsigned>(fd_types.size()))
            return util::Error{5004, 0, "ADI field number out of range", ""};
        std::size_t fi = static_cast<std::size_t>(fnum) - 1u;  // 1-based → 0-based

        FieldComp kf;
        kf.type   = fd_types[fi];
        kf.offset = fd_offsets[fi];
        kf.length = fd_lengths[fi];
        key_fields_.push_back(kf);

        bool is_c = (kf.type == ADT_TYPE_CICHAR || kf.type == ADT_TYPE_CHAR);
        total_key_len += is_c ? static_cast<std::uint32_t>(kf.length) : 8u;
        if (kf.type == ADT_TYPE_CICHAR) has_ci_component_ = true;

        if (first) {
            tag_name_   = fd_names[fi];
            root_page_  = root_pg;
            adt_type_   = kf.type;
            fld_offset_ = kf.offset;
            fld_length_ = kf.length;
            char_key_   = is_c;
            first = false;
        }
    }

    adt_hdr_len_   = hlen;
    adt_rec_len_   = rlen;
    key_total_len_ = total_key_len;
    entry_size_    = dense_entry_size(fld_length_);
    unique_        = unique;

    if (char_key_) {
        char_key_padded_len_ = (total_key_len + 3u) & ~3u;
        // padded_key + cum[4] + page[4]  (page widened from 1 byte; see
        // char_tree_entry_page / write_branch_entry).
        branch_entry_sz_     = char_key_padded_len_ + 8u;
    } else {
        char_key_padded_len_ = 0;
        branch_entry_sz_     = ADI_TREE_ENTRY_SIZE;
    }

    // v2 leaf: opaque full key stored in the leaf (recno 4B + klen key). The key
    // is the ACE-evaluated expression key, ordered by memcmp — reuse the char
    // branch machinery (full key + 4-byte child page), drop the field-derived
    // geometry. key_for_recno_ is no longer consulted for navigation.
    if (v2_key_len > 0) {
        key_in_leaf_         = true;
        char_key_            = true;
        key_total_len_       = v2_key_len;
        entry_size_          = 4u + v2_key_len;
        char_key_padded_len_ = (v2_key_len + 3u) & ~3u;
        branch_entry_sz_     = char_key_padded_len_ + 8u;
    }
    return {};
}

// ─────────────────────────────────────────────────────────────────────────────

util::Result<void> AdiIndex::open(const std::string& path, IndexOpenMode mode) {
    mode_ = mode;
    auto fi = platform::File::open(path, map_open_mode(mode));
    if (!fi) return fi.error();
    adi_file_ = std::move(fi).value();
    adi_path_ = path;

    auto tags = scan_tagdir(adi_file_);
    if (!tags) return tags.error();
    if (tags.value().empty())
        return util::Error{5004, 0, "ADI has no tags", path};

    std::string adt_p = adt_path_for(path);
    auto fa = platform::File::open(adt_p, platform::OpenMode::ReadOnly);
    if (!fa) return fa.error();
    adt_file_ = std::move(fa).value();

    std::uint32_t hlen = 0, rlen = 0;
    auto fields = read_adt_fields(adt_file_, hlen, rlen);
    if (!fields) return fields.error();

    const TagEntry& tag = tags.value()[0];
    std::vector<std::uint16_t> types, offsets, lengths;
    std::vector<std::string>   names;
    for (const auto& fd : fields.value()) {
        types.push_back(fd.type);
        offsets.push_back(fd.offset);
        lengths.push_back(fd.length);
        names.push_back(fd.name);
    }
    if (auto r = apply_tag_(tag.fnums, tag.root_pg, types, offsets, lengths,
                            names, hlen, rlen, tag.unique,
                            tag.v2.has ? tag.v2.key_len : 0u); !r)
        return r;
    if (tag.v2.has) {
        tag_name_   = tag.v2.tag_name;
        tag_expr_   = tag.v2.key_expr;
        tag_cond_   = tag.v2.for_expr;
        descending_ = tag.v2.descending;
    }
    return {};
}

util::Result<void> AdiIndex::open_named(const std::string& adi_path,
                                        IndexOpenMode       mode,
                                        const std::string&  field_name,
                                        const std::string&  adt_path) {
    mode_ = mode;
    auto fi = platform::File::open(adi_path, map_open_mode(mode));
    if (!fi) return fi.error();
    adi_file_ = std::move(fi).value();
    adi_path_ = adi_path;

    auto tags = scan_tagdir(adi_file_);
    if (!tags) return tags.error();

    std::string adt_p = adt_path.empty() ? adt_path_for(adi_path) : adt_path;
    auto fa = platform::File::open(adt_p, platform::OpenMode::ReadOnly);
    if (!fa) return fa.error();
    adt_file_ = std::move(fa).value();

    std::uint32_t hlen = 0, rlen = 0;
    auto fields = read_adt_fields(adt_file_, hlen, rlen);
    if (!fields) return fields.error();

    auto name_eq = [](const std::string& a, const std::string& b) {
        if (a.size() != b.size()) return false;
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(a[i])) !=
                std::tolower(static_cast<unsigned char>(b[i]))) return false;
        }
        return true;
    };

    std::vector<std::uint16_t> types, offsets, lengths;
    std::vector<std::string>   names;
    for (const auto& fd : fields.value()) {
        types.push_back(fd.type);
        offsets.push_back(fd.offset);
        lengths.push_back(fd.length);
        names.push_back(fd.name);
    }

    for (const auto& tag : tags.value()) {
        // v2: identity by tag NAME (the requested name is the tag name).
        // Legacy: resolve the F-marker field's name.
        bool match = false;
        if (tag.v2.has && !tag.v2.tag_name.empty()) {
            match = name_eq(tag.v2.tag_name, field_name);
        } else if (!tag.fnums.empty()) {
            std::uint8_t fnum = tag.fnums[0];
            if (fnum != 0 && fnum <= static_cast<unsigned>(fields.value().size()))
                match = name_eq(fields.value()[fnum - 1].name, field_name);
        }
        if (!match) continue;
        if (auto r = apply_tag_(tag.fnums, tag.root_pg, types, offsets, lengths,
                                names, hlen, rlen, tag.unique,
                                tag.v2.has ? tag.v2.key_len : 0u); !r)
            return r;
        if (tag.v2.has) {
            tag_name_   = tag.v2.tag_name;
            tag_expr_   = tag.v2.key_expr;
            tag_cond_   = tag.v2.for_expr;
            descending_ = tag.v2.descending;
        }
        return {};
    }
    return util::Error{5004, 0, "ADI tag not found: " + field_name, adi_path};
}

// static
util::Result<std::vector<std::string>>
AdiIndex::list_tags(const std::string& adi_path, const std::string& adt_path) {
    auto fi = platform::File::open(adi_path, platform::OpenMode::ReadOnly);
    if (!fi) return fi.error();
    platform::File adi_f = std::move(fi).value();

    auto tags = scan_tagdir(adi_f);
    if (!tags) return tags.error();

    // Report tags in CREATION order, not directory-slot order.
    //
    // A tag directory can be laid out either way: append, which add_tag()
    // writes by default (CreateParams::prepend_tag_dir == false), or
    // prepend, which it writes when that flag is set and which is also what
    // SAP's Advantage Data Architect produces. Both appear in the wild and a
    // bag cannot be assumed to have been written by this engine at all, so
    // reading raw slot order silently reverses the ordinals of every
    // prepend-laid-out bag.
    //
    // Per-tag header pages are allocated at end-of-file, monotonically, in
    // creation order, regardless of which directory slot the entry ends up
    // in. Sorting on that page number therefore recovers one canonical
    // ordering for either layout, with no format detection and no migration.
    //
    // This matters to any caller that navigates orders by NUMBER rather than
    // by name -- OrdSetFocus(n) / DBSETORDER(n), which is what a browse doing
    // click-to-sort on a column does. Such a caller cannot compensate for a
    // reversal without knowing in advance how many tags the bag holds.
    std::sort(tags.value().begin(), tags.value().end(),
              [](const TagEntry& a, const TagEntry& b) {
                  return a.root_pg < b.root_pg;
              });

    std::string adt_p = adt_path.empty() ? adt_path_for(adi_path) : adt_path;
    auto fa = platform::File::open(adt_p, platform::OpenMode::ReadOnly);
    if (!fa) return fa.error();
    platform::File adt_f = std::move(fa).value();

    std::uint32_t hlen = 0, rlen = 0;
    auto fields = read_adt_fields(adt_f, hlen, rlen);
    if (!fields) return fields.error();
    (void)hlen; (void)rlen;

    std::vector<std::string> names;
    names.reserve(tags.value().size());
    for (const auto& tag : tags.value()) {
        // v2: identity by tag NAME (lets N tags share a field). Legacy: the
        // resolved field name.
        if (tag.v2.has && !tag.v2.tag_name.empty()) {
            names.push_back(tag.v2.tag_name);
            continue;
        }
        if (tag.fnums.empty()) continue;
        std::uint8_t fnum = tag.fnums[0];
        if (fnum == 0 || fnum > static_cast<unsigned>(fields.value().size())) continue;
        names.push_back(fields.value()[fnum - 1].name);
    }
    return names;
}

util::Result<SeekOutcome> AdiIndex::seek_first() {
    return navigate_leftmost_();
}

util::Result<SeekOutcome> AdiIndex::seek_last() {
    return navigate_rightmost_();
}

util::Result<SeekOutcome> AdiIndex::next() {
    if (cur_pg_ == ADI_INVALID_PAGE) {
        SeekOutcome o; o.hit = SeekHit::AfterEnd; return o;
    }
    ++cur_idx_;
    if (cur_idx_ < static_cast<std::int32_t>(cur_cnt_)) {
        if (auto r = refresh_current_(); !r) return r.error();
        return make_positioned_();
    }
    // Move to right sibling dense leaf
    if (cur_rsib_ == ADI_INVALID_PAGE) {
        cur_pg_ = ADI_INVALID_PAGE; cur_idx_ = -1;
        SeekOutcome o; o.hit = SeekHit::AfterEnd; return o;
    }
    if (auto r = load_dense_leaf_(cur_rsib_); !r) return r.error();
    cur_idx_ = 0;
    if (cur_cnt_ == 0) {
        SeekOutcome o; o.hit = SeekHit::AfterEnd; return o;
    }
    if (auto r = refresh_current_(); !r) return r.error();
    return make_positioned_();
}

util::Result<SeekOutcome> AdiIndex::prev() {
    if (cur_pg_ == ADI_INVALID_PAGE) {
        SeekOutcome o; o.hit = SeekHit::BeforeBegin; return o;
    }
    --cur_idx_;
    if (cur_idx_ >= 0) {
        if (auto r = refresh_current_(); !r) return r.error();
        return make_positioned_();
    }
    // Move to left sibling
    if (cur_lsib_ == ADI_INVALID_PAGE) {
        cur_pg_ = ADI_INVALID_PAGE; cur_idx_ = -1;
        SeekOutcome o; o.hit = SeekHit::BeforeBegin; return o;
    }
    if (auto r = load_dense_leaf_(cur_lsib_); !r) return r.error();
    cur_idx_ = static_cast<std::int32_t>(cur_cnt_) - 1;
    if (cur_idx_ < 0) {
        SeekOutcome o; o.hit = SeekHit::BeforeBegin; return o;
    }
    if (auto r = refresh_current_(); !r) return r.error();
    return make_positioned_();
}

// ── AdiIndex::seek_key ───────────────────────────────────────────────────────
//
// Descends the B-tree from root to a dense leaf page, then linear-scans that
// leaf (and its right sibling if needed) by reading actual ADT field values.
// Handles both char-key (padded, memcmp) and numeric (8-byte sign-flipped BE).

util::Result<SeekOutcome> AdiIndex::seek_key(const std::string& key, bool soft) {
    std::string nkey = key;
    if (!char_key_) {
        auto ascii_to_packed = [&](const std::string& raw) -> bool {
            std::string trimmed = raw;
            while (!trimmed.empty() && trimmed.back() == ' ') trimmed.pop_back();
            char* end = nullptr;
            double dv = std::strtod(trimmed.c_str(), &end);
            if (end == trimmed.c_str()) return false;
            nkey = pack_double_key(dv);
            return true;
        };

        if (nkey.size() == sizeof(double)) {
            // ADI packed keys always have bit 7 of byte 0 set (sign-flip).
            // Raw ADS_DOUBLEKEY bytes are IEEE754 LE with byte 0 usually < 0x80.
            const bool likely_packed =
                (static_cast<unsigned char>(nkey[0]) & 0x80u) != 0;
            if (!likely_packed) {
                double dv = 0.0;
                std::memcpy(&dv, nkey.data(), sizeof(double));
                nkey = pack_double_key(dv);
            }
        } else if (nkey.size() == 8u) {
            // 8 bytes may be CDX-style space-padded ASCII, not a packed key.
            bool printable = true;
            for (char ch : nkey) {
                unsigned char c = static_cast<unsigned char>(ch);
                if (c != ' ' && (c < 0x30 || c > 0x39) && c != '.' && c != '-' &&
                    c != '+') {
                    printable = false;
                    break;
                }
            }
            if (printable && !ascii_to_packed(nkey)) {
                nkey.resize(8, '\0');
            }
        } else if (nkey.size() != 8) {
            if (!ascii_to_packed(nkey)) {
                if (adt_type_ == ADT_TYPE_INTEGER && nkey.size() == 4u)
                    nkey = encode_adt_key(adt_type_,
                                          reinterpret_cast<const std::uint8_t*>(
                                              nkey.data()),
                                          4);
                else
                    nkey.resize(8, '\0');
            }
        }
    }

    Page pg{};
    std::uint32_t dense_pg = ADI_INVALID_PAGE;

    if (char_key_) {
        // Char-key ADI: branch (lv=1) → dense (lv=2); no sparse leaf level.
        // Branch entry key occupies char_key_padded_len_ bytes; incoming key
        // is key_total_len_ bytes (raw field length, ≤ padded length).
        std::uint32_t cur = root_page_;
        for (;;) {
            if (auto r = read_adi_page_(cur, pg); !r) return r.error();
            std::uint16_t lv  = page_level(pg.data());
            std::uint16_t cnt = page_count(pg.data());
            if (cnt == 0) { SeekOutcome o; o.hit = SeekHit::AfterEnd; return o; }
            if (is_dense_leaf(lv)) { dense_pg = cur; break; }
            // Branch: take first entry whose key >= seek key. CICHAR
            // components fold to upper on BOTH sides so a mismatched-case
            // search key descends the same subtree as the stored key.
            const std::string fnkey = fold_for_compare_(nkey);
            // A seek key SHORTER than the index key bounds by prefix, so
            // compare only the bytes the caller supplied — the same rule
            // compare_keys_ applies at the dense leaf. Comparing the full
            // key_total_len_ here read PAST the end of fnkey: with a partial
            // key whose prefix equalled the separator's, the decision was
            // then made on whatever followed the string in memory, and the
            // descent could take the wrong child and miss a key that exists.
            const std::size_t cmp_len =
                std::min<std::size_t>(fnkey.size(), key_total_len_);
            int chosen = static_cast<int>(cnt) - 1;
            for (int i = 0; i < static_cast<int>(cnt); ++i) {
                const std::uint8_t* ek = pg.data() + ADI_TREE_ENTRY_START
                    + static_cast<std::uint32_t>(i) * branch_entry_sz_;
                const std::string fek = fold_for_compare_(
                    std::string(reinterpret_cast<const char*>(ek),
                                key_total_len_));
                if (std::memcmp(fnkey.data(), fek.data(), cmp_len) <= 0) {
                    chosen = i; break;
                }
            }
            cur = branch_entry_page_(pg.data(), chosen);
        }
    } else {
        // Numeric-key ADI: descend until we hit a dense leaf.
        // Works for any depth (branch→dense, branch→sparse→dense, root=dense).
        if (nkey.size() != 8) return navigate_leftmost_();

        std::uint32_t cur = root_page_;
        for (;;) {
            if (auto r = read_adi_page_(cur, pg); !r) return r.error();
            std::uint16_t lv  = page_level(pg.data());
            std::uint16_t cnt = page_count(pg.data());
            if (cnt == 0) { SeekOutcome o; o.hit = SeekHit::AfterEnd; return o; }
            if (is_dense_leaf(lv)) { dense_pg = cur; break; }
            // Branch (lv=1) or sparse leaf (lv=0): pick child by key comparison.
            int chosen = static_cast<int>(cnt) - 1;
            for (int i = 0; i < static_cast<int>(cnt); ++i) {
                const std::uint8_t* ek = tree_entry_key(pg.data(), i);
                if (std::memcmp(nkey.data(), ek, 8) <= 0) { chosen = i; break; }
            }
            cur = tree_entry_page(pg.data(), chosen);
        }
    }

    // ── Dense leaf: linear scan via ADT field read ────────────────────────────
    if (auto r = load_dense_leaf_(dense_pg); !r) return r.error();

    for (int i = 0; i < static_cast<int>(cur_cnt_); ++i) {
        std::uint32_t rno;
        std::string ckv;
        if (key_in_leaf_) {
            rno = leaf_entries_[static_cast<std::size_t>(i)].first;
            ckv = leaf_entries_[static_cast<std::size_t>(i)].second;
        } else {
            rno = dense_entry_recno(cur_page_.data(), i, entry_size_);
            auto ck = key_for_recno_(rno);
            if (!ck) return ck.error();
            ckv = std::move(ck).value();
        }
        int cmp = compare_keys_(ckv, nkey);
        if (cmp > 0) {
            if (soft) {
                cur_idx_     = i;
                cur_recno_   = rno;
                current_key_ = std::move(ckv);
                SeekOutcome o;
                o.hit        = SeekHit::AfterKey;
                o.recno      = cur_recno_;
                o.positioned = true;
                return o;
            }
            SeekOutcome o; o.hit = SeekHit::AfterEnd; return o;
        }
        if (cmp == 0) {
            cur_idx_     = i;
            cur_recno_   = rno;
            current_key_ = std::move(ckv);
            return make_positioned_();
        }
    }
    // Key beyond this dense leaf: try right sibling.
    if (cur_rsib_ != ADI_INVALID_PAGE) {
        if (auto r = load_dense_leaf_(cur_rsib_); !r) return r.error();
        if (cur_cnt_ > 0) {
            cur_idx_ = 0;
            if (auto r = refresh_current_(); !r) return r.error();
            if (soft) {
                SeekOutcome o;
                o.hit        = SeekHit::AfterKey;
                o.recno      = cur_recno_;
                o.positioned = true;
                return o;
            }
        }
    }
    SeekOutcome o; o.hit = SeekHit::AfterEnd; return o;
}

// ── AdiIndex::write_adi_page_ ────────────────────────────────────────────────

util::Result<void> AdiIndex::write_adi_page_(std::uint32_t page_no,
                                             const Page& buf) {
    auto r = adi_file_.write_at(
        static_cast<std::uint64_t>(page_no) * ADI_PAGE_SIZE,
        buf.data(), buf.size());
    if (!r) return r.error();
    if (r.value() != ADI_PAGE_SIZE)
        return util::Error{5000, 0, "short ADI page write", ""};
    return {};
}

// ── AdiIndex::alloc_page_ ───────────────────────────────────────────────────

util::Result<std::uint32_t> AdiIndex::alloc_page_() {
    auto sz = adi_file_.size();
    if (!sz) return sz.error();
    std::uint32_t pno = static_cast<std::uint32_t>(sz.value() / ADI_PAGE_SIZE);
    // Reserve the page NOW by extending the file with a zeroed page. Without
    // this, alloc_page_() only computed end-of-file/PAGE without growing the
    // file, so two allocations issued before the first page was written (or an
    // allocation whose number coincided with a page about to be repurposed as a
    // branch) handed out the SAME page number. That produced a branch entry
    // whose child pointer referenced the branch's own page -> the insert-time
    // descent looped forever, growing the path stack until the build ran away
    // in time and memory once the tree first went multi-level.
    Page zero{};
    if (auto w = write_adi_page_(pno, zero); !w) return w.error();
    return pno;
}

// ── AdiIndex::build_dense_entry_ ────────────────────────────────────────────

void AdiIndex::build_dense_entry_(std::uint8_t* dst, std::uint32_t recno,
                                  const std::string& ikey) const noexcept {
    if (key_in_leaf_) {
        // v2: recno[4 LE] + key[key_total_len_] (the full evaluated key).
        dst[0] = static_cast<std::uint8_t>( recno        & 0xFFu);
        dst[1] = static_cast<std::uint8_t>((recno >>  8) & 0xFFu);
        dst[2] = static_cast<std::uint8_t>((recno >> 16) & 0xFFu);
        dst[3] = static_cast<std::uint8_t>((recno >> 24) & 0xFFu);
        std::size_t n = std::min<std::size_t>(key_total_len_, ikey.size());
        std::memcpy(dst + 4, ikey.data(), n);
        if (n < key_total_len_)
            std::memset(dst + 4 + n, ' ', key_total_len_ - n);  // pad char key
        return;
    }
    if (entry_size_ >= 3) {
        dst[0] = static_cast<std::uint8_t>(recno);
        dst[1] = static_cast<std::uint8_t>(recno >> 8);
        dst[2] = 0x00;  // type/flags byte — unknown; 0 works for new inserts
    } else {
        // 2-byte entry: recno(1) + key_flags(1).  For LOGICAL fields the
        // key_flags byte encodes the boolean value (0x00=false, 0x40=true).
        dst[0] = static_cast<std::uint8_t>(recno);
        dst[1] = ikey.empty() ? std::uint8_t{0}
                              : static_cast<std::uint8_t>(ikey[0]);
    }
}

// ── AdiIndex::branch_key_at_ ────────────────────────────────────────────────

std::string AdiIndex::branch_key_at_(const std::uint8_t* pg, int idx) const noexcept {
    const std::uint8_t* e = pg + ADI_TREE_ENTRY_START
                            + static_cast<std::uint32_t>(idx) * branch_entry_sz_;
    if (char_key_)
        return std::string(reinterpret_cast<const char*>(e), key_total_len_);
    return std::string(reinterpret_cast<const char*>(e), 8);
}

// ── AdiIndex::promote_split_ ─────────────────────────────────────────────────
//
// Push a split result up the path stack.  Called after a leaf split with:
//   left_pg  – page that ALREADY holds the left half (unchanged or rewritten)
//   left_max – max key of left_pg's subtree
//   right_pg – newly allocated page holding the right half
//   right_max – max key of right_pg's subtree
//
// If path is empty the root page is rewritten as a 2-entry branch.
// Otherwise pops the top frame, inserts right_pg into the parent branch,
// and may trigger a branch split (recursive call).

util::Result<void> AdiIndex::promote_split_(
    std::vector<PathFrame>& path,
    std::uint32_t left_pg,  const std::string& left_max,
    std::uint32_t right_pg, const std::string& right_max)
{
    const std::uint32_t max_branch = (ADI_PAGE_SIZE - ADI_TREE_ENTRY_START)
                                     / branch_entry_sz_;

    auto write_branch_entry = [&](std::uint8_t* dst, const std::string& key,
                                  std::uint32_t page_no) {
        if (char_key_) {
            // padded_key[char_key_padded_len_] + cum[4 LE]=0 + page[1]
            std::memset(dst, 0, branch_entry_sz_);
            std::size_t klen = std::min((std::size_t)key_total_len_, key.size());
            std::memcpy(dst, key.data(), klen);
            std::uint8_t* pp = dst + char_key_padded_len_ + 4;
            pp[0] = static_cast<std::uint8_t>( page_no        & 0xFFu);
            pp[1] = static_cast<std::uint8_t>((page_no >>  8) & 0xFFu);
            pp[2] = static_cast<std::uint8_t>((page_no >> 16) & 0xFFu);
            pp[3] = static_cast<std::uint8_t>((page_no >> 24) & 0xFFu);
        } else {
            // key[8 BE] + cum[4 BE]=0 + page[4 BE]
            std::memset(dst, 0, 16);
            std::size_t klen = std::min<std::size_t>(8, key.size());
            std::memcpy(dst, key.data(), klen);
            set_u32_be(dst + 12, page_no);
        }
    };

    if (path.empty()) {
        // Root was the leaf (or we've bubbled all the way up).
        // Rewrite root_page_ as a 2-entry branch.
        Page root{};
        set_u16_le(root.data(), ADI_LVL_BRANCH);
        set_u16_le(root.data() + 2, 2);
        set_u32_le(root.data() + 4, ADI_INVALID_PAGE);
        set_u32_le(root.data() + 8, ADI_INVALID_PAGE);
        write_branch_entry(root.data() + ADI_TREE_ENTRY_START,
                           left_max,  left_pg);
        write_branch_entry(root.data() + ADI_TREE_ENTRY_START + branch_entry_sz_,
                           right_max, right_pg);
        return write_adi_page_(root_page_, root);
    }

    // Pop parent frame.
    PathFrame frame = path.back();
    path.pop_back();

    // Read the parent branch page.
    Page parent{};
    if (auto r = read_adi_page_(frame.page_no, parent); !r) return r;

    std::uint16_t par_cnt = page_count(parent.data());

    // Build a combined branch-entry buffer: all existing entries plus the new
    // right child entry.  We also update the existing entry[frame.entry_idx]
    // key to reflect the new left_max.
    std::uint32_t total = par_cnt + 1;
    std::vector<std::uint8_t> combo(total * branch_entry_sz_);

    std::uint8_t* src = parent.data() + ADI_TREE_ENTRY_START;
    // Copy entries [0..entry_idx], updating the chosen entry's key.
    for (int i = 0; i <= frame.entry_idx; ++i) {
        auto ui = static_cast<std::uint32_t>(i);
        std::uint8_t* dst = combo.data() + ui * branch_entry_sz_;
        std::memcpy(dst, src + ui * branch_entry_sz_, branch_entry_sz_);
        if (i == frame.entry_idx) {
            // Update this entry's key to left_max; page pointer stays.
            std::size_t klen = std::min((std::size_t)(char_key_ ? key_total_len_ : 8u),
                                        left_max.size());
            std::memcpy(dst, left_max.data(), klen);
            if (char_key_ && key_total_len_ < char_key_padded_len_)
                std::memset(dst + key_total_len_, 0,
                            char_key_padded_len_ - key_total_len_);
        }
    }
    // New entry for right child at entry_idx+1
    write_branch_entry(combo.data() + (static_cast<std::uint32_t>(frame.entry_idx) + 1u) * branch_entry_sz_,
                       right_max, right_pg);
    // Copy remaining entries [entry_idx+1..par_cnt-1] shifted right by one.
    for (std::uint32_t i = static_cast<std::uint32_t>(frame.entry_idx) + 1u; i < par_cnt; ++i) {
        std::memcpy(combo.data() + (i + 1) * branch_entry_sz_,
                    src + i * branch_entry_sz_, branch_entry_sz_);
    }

    if (total <= max_branch) {
        // Fits: write updated parent.
        set_u16_le(parent.data() + 2, static_cast<std::uint16_t>(total));
        std::memcpy(src, combo.data(), total * branch_entry_sz_);
        return write_adi_page_(frame.page_no, parent);
    }

    // Branch is full: split it.
    std::uint32_t left_cnt  = total / 2;
    std::uint32_t right_cnt = total - left_cnt;

    // Extract max key from a combo-buffer branch entry (no ADT read needed).
    auto combo_key = [&](std::uint32_t idx) -> std::string {
        const std::uint8_t* e = combo.data() + idx * branch_entry_sz_;
        std::size_t klen = char_key_ ? key_total_len_ : 8u;
        return std::string(reinterpret_cast<const char*>(e), klen);
    };
    std::string new_left_max  = combo_key(left_cnt - 1);
    std::string new_right_max = combo_key(total - 1);

    // Allocate right branch page.
    auto rp_r = alloc_page_();
    if (!rp_r) return rp_r.error();
    std::uint32_t right_branch_pg = rp_r.value();

    // Where does the LEFT half live? Normally the branch stays in place at
    // frame.page_no (its parent already points there). BUT when this branch
    // IS the root (no parent left on the path), promote_split_ will rewrite
    // root_page_ (== frame.page_no) as the *new* root branch — so the left
    // half must move to a FRESH page, otherwise the new root's first child
    // would point at root_page_ itself (a self-referential child that makes
    // the insert-time descent loop forever). Mirrors the root dense-leaf
    // split, which likewise pushes both halves onto new pages.
    const bool splitting_root = path.empty();
    std::uint32_t left_branch_pg = frame.page_no;
    if (splitting_root) {
        auto lp_r = alloc_page_();
        if (!lp_r) return lp_r.error();
        left_branch_pg = lp_r.value();
    }

    // Write the left half (to its fresh page when splitting the root, else
    // back in place at frame.page_no).
    set_u16_le(parent.data() + 2, static_cast<std::uint16_t>(left_cnt));
    std::memcpy(src, combo.data(), left_cnt * branch_entry_sz_);
    if (auto r = write_adi_page_(left_branch_pg, parent); !r) return r;

    // Write right branch page.
    Page right_branch{};
    set_u16_le(right_branch.data(), ADI_LVL_BRANCH);
    set_u16_le(right_branch.data() + 2, static_cast<std::uint16_t>(right_cnt));
    set_u32_le(right_branch.data() + 4, ADI_INVALID_PAGE);
    set_u32_le(right_branch.data() + 8, ADI_INVALID_PAGE);
    std::memcpy(right_branch.data() + ADI_TREE_ENTRY_START,
                combo.data() + left_cnt * branch_entry_sz_,
                right_cnt * branch_entry_sz_);
    if (auto r = write_adi_page_(right_branch_pg, right_branch); !r) return r;

    // Recurse: promote branch split. When splitting the root, promote_split_
    // sees an empty path and rewrites root_page_ as a 2-entry branch pointing
    // at the two halves (left_branch_pg + right_branch_pg).
    return promote_split_(path,
                          left_branch_pg, new_left_max,
                          right_branch_pg, new_right_max);
}

// ── AdiIndex::insert ─────────────────────────────────────────────────────────

util::Result<void> AdiIndex::insert(std::uint32_t recno,
                                    const std::string& key) {
    if (mode_ == IndexOpenMode::ReadOnly)
        return util::Error{5000, 0, "ADI index is read-only", ""};
    invalidate_pos_cache();

    // Normalise key.  Numeric tags always index from live ADT bytes —
    // evaluate_index_expr may supply ASCII padding that does not match
    // encode_adt_key() used at navigation time.
    std::string ikey;
    if (char_key_) {
        ikey = key;
        if (ikey.size() < key_total_len_)
            ikey.append(key_total_len_ - ikey.size(), ' ');
        else
            ikey.resize(key_total_len_);
    } else {
        auto kr = key_for_recno_(recno);
        if (!kr) return kr.error();
        ikey = std::move(kr).value();
    }

    // ── Descend from root, building the path stack ───────────────────────────
    std::vector<PathFrame> path;
    Page pg{};
    std::uint32_t cur = root_page_;

    for (;;) {
        // Defense-in-depth: a healthy B-tree is only a handful of levels deep.
        // If the descent ever exceeds a sane bound, the index is corrupt
        // (e.g. a self-referential child pointer); fail loudly instead of
        // looping forever and exhausting memory.
        // Defense-in-depth: a healthy B-tree is only a handful of levels deep.
        // If the descent ever exceeds a sane bound the index is corrupt (e.g. a
        // self-referential child pointer); fail loudly instead of looping
        // forever and exhausting memory.
        if (path.size() > 128) {
            return util::Error{6106, 0,
                "ADI index corrupt: descent exceeded max depth", ""};
        }
        if (auto r = read_adi_page_(cur, pg); !r) return r.error();
        std::uint16_t lv  = page_level(pg.data());
        std::uint16_t cnt = page_count(pg.data());
        if (is_dense_leaf(lv)) break;

        int chosen = cnt ? static_cast<int>(cnt) - 1 : 0;
        if (char_key_) {
            for (int i = 0; i < static_cast<int>(cnt); ++i) {
                const std::uint8_t* ek = pg.data() + ADI_TREE_ENTRY_START
                    + static_cast<std::uint32_t>(i) * branch_entry_sz_;
                if (std::memcmp(ikey.data(), ek, key_total_len_) <= 0) {
                    chosen = i; break;
                }
            }
        } else {
            for (int i = 0; i < static_cast<int>(cnt); ++i) {
                if (std::memcmp(ikey.data(), tree_entry_key(pg.data(), i), 8) <= 0) {
                    chosen = i; break;
                }
            }
        }
        path.push_back({cur, cnt, chosen});
        cur = branch_entry_page_(pg.data(), chosen);
    }

    std::uint16_t leaf_lv  = page_level(pg.data());
    std::uint16_t leaf_cnt = page_count(pg.data());

    // ── v2 front-coded leaf: decode → ordered insert → re-encode (or split) ──
    if (key_in_leaf_) {
        std::vector<std::pair<std::uint32_t, std::string>> ents;
        fc_decode_leaf(pg.data() + ADI_DENSE_ENTRY_START, leaf_cnt,
                       key_total_len_, ents);

        // Insertion position by (key, recno) — the same order navigation uses.
        std::size_t pos = 0;
        while (pos < ents.size()) {
            int cmp = compare_keys_(ents[pos].second, ikey);
            if (cmp < 0 || (cmp == 0 && ents[pos].first < recno)) ++pos;
            else break;
        }
        ents.insert(ents.begin() + static_cast<std::ptrdiff_t>(pos), {recno, ikey});

        const std::uint32_t orig_lsib = page_lsib(pg.data());
        const std::uint32_t orig_rsib = page_rsib(pg.data());

        // Fits in this page?  Re-encode in place.
        Page np{};
        if (render_v2_leaf_(np, ents, orig_lsib, orig_rsib)) {
            if (auto r = write_adi_page_(cur, np); !r) return r;
            if (cur_pg_ == cur) {
                cur_page_     = np;
                leaf_entries_ = std::move(ents);
                cur_cnt_      = static_cast<std::uint16_t>(leaf_entries_.size());
                if (cur_idx_ >= static_cast<std::int32_t>(pos)) ++cur_idx_;
            }
            return {};
        }

        // Overflow → split the run into two front-coded leaves.
        const std::size_t cap = ADI_PAGE_SIZE - ADI_DENSE_ENTRY_START;
        std::size_t mid = fc_pick_split(ents, key_total_len_, cap);
        if (mid == 0)
            return util::Error{5000, 0, "ADI v2 leaf: key too wide to split", ""};
        std::vector<std::pair<std::uint32_t, std::string>>
            left(ents.begin(), ents.begin() + static_cast<std::ptrdiff_t>(mid)),
            right(ents.begin() + static_cast<std::ptrdiff_t>(mid), ents.end());
        std::string left_max  = left.back().second;
        std::string right_max = right.back().second;

        if (path.empty()) {
            // Root is the dense leaf: two fresh pages, root becomes a branch.
            // Allocate+write left BEFORE allocating right (alloc grows the file).
            auto lp = alloc_page_(); if (!lp) return lp.error();
            std::uint32_t left_pg = lp.value();
            Page lpg{};
            if (!render_v2_leaf_(lpg, left, orig_lsib, ADI_INVALID_PAGE))
                return util::Error{5000, 0, "ADI v2 split: left overflow", ""};
            if (auto r = write_adi_page_(left_pg, lpg); !r) return r;
            auto rp = alloc_page_(); if (!rp) return rp.error();
            std::uint32_t right_pg = rp.value();
            set_u32_le(lpg.data() + 8, right_pg);          // patch left.rsib
            if (auto r = write_adi_page_(left_pg, lpg); !r) return r;
            Page rpg{};
            if (!render_v2_leaf_(rpg, right, left_pg, orig_rsib))
                return util::Error{5000, 0, "ADI v2 split: right overflow", ""};
            if (auto r = write_adi_page_(right_pg, rpg); !r) return r;
            if (orig_rsib != ADI_INVALID_PAGE) {
                Page rsib{};
                if (auto r = read_adi_page_(orig_rsib, rsib); !r) return r;
                set_u32_le(rsib.data() + 4, right_pg);
                if (auto r = write_adi_page_(orig_rsib, rsib); !r) return r;
            }
            cur_pg_ = ADI_INVALID_PAGE; cur_idx_ = -1; cur_cnt_ = 0;
            return promote_split_(path, left_pg, left_max, right_pg, right_max);
        }

        // Non-root: left half stays in `cur`, right half goes to a new page.
        auto rp = alloc_page_(); if (!rp) return rp.error();
        std::uint32_t right_pg = rp.value();
        Page lpg{};
        if (!render_v2_leaf_(lpg, left, orig_lsib, right_pg))
            return util::Error{5000, 0, "ADI v2 split: left overflow", ""};
        if (auto r = write_adi_page_(cur, lpg); !r) return r;
        Page rpg{};
        if (!render_v2_leaf_(rpg, right, cur, orig_rsib))
            return util::Error{5000, 0, "ADI v2 split: right overflow", ""};
        if (auto r = write_adi_page_(right_pg, rpg); !r) return r;
        if (orig_rsib != ADI_INVALID_PAGE) {
            Page rsib{};
            if (auto r = read_adi_page_(orig_rsib, rsib); !r) return r;
            set_u32_le(rsib.data() + 4, right_pg);
            if (auto r = write_adi_page_(orig_rsib, rsib); !r) return r;
        }
        cur_pg_ = ADI_INVALID_PAGE; cur_idx_ = -1; cur_cnt_ = 0;
        return promote_split_(path, cur, left_max, right_pg, right_max);
    }

    const std::uint32_t max_ents =
        (ADI_PAGE_SIZE - ADI_DENSE_ENTRY_START) / entry_size_;

    // ── Binary-search for the insertion position ─────────────────────────────
    int pos;
    {
        int lo = 0, hi = static_cast<int>(leaf_cnt);
        while (lo < hi) {
            int mid = (lo + hi) / 2;
            std::uint32_t mrec = dense_entry_recno(pg.data(), mid, entry_size_);
            std::string mkv;
            if (key_in_leaf_) {
                mkv = dense_entry_key_from_buf(pg.data() + ADI_DENSE_ENTRY_START,
                                               static_cast<std::uint32_t>(mid),
                                               entry_size_, key_total_len_);
            } else {
                auto mk = key_for_recno_(mrec);
                if (!mk) return mk.error();
                mkv = std::move(mk).value();
            }
            int cmp = compare_keys_(mkv, ikey);
            if (cmp < 0 || (cmp == 0 && mrec < recno)) lo = mid + 1;
            else hi = mid;
        }
        pos = lo;
    }

    // ── Simple insert (leaf not full) ────────────────────────────────────────
    if (leaf_cnt < max_ents) {
        std::uint8_t* base = pg.data() + ADI_DENSE_ENTRY_START;
        std::uint32_t move_n = leaf_cnt - static_cast<std::uint32_t>(pos);
        if (move_n > 0)
            std::memmove(base + (static_cast<std::uint32_t>(pos) + 1u) * entry_size_,
                         base + static_cast<std::uint32_t>(pos) * entry_size_,
                         move_n * entry_size_);
        build_dense_entry_(base + static_cast<std::uint32_t>(pos) * entry_size_, recno, ikey);
        set_u16_le(pg.data() + 2, leaf_cnt + 1);

        // Keep cursor consistent.
        if (cur_pg_ == cur) {
            cur_page_ = pg;
            cur_cnt_  = leaf_cnt + 1;
            if (cur_idx_ >= pos) ++cur_idx_;
        }
        return write_adi_page_(cur, pg);
    }

    // ── Leaf is full: build combined buffer and split ─────────────────────────
    std::vector<std::uint8_t> combo((max_ents + 1) * entry_size_);
    std::uint8_t* base = pg.data() + ADI_DENSE_ENTRY_START;
    std::memcpy(combo.data(),
                base,
                static_cast<std::uint32_t>(pos) * entry_size_);
    build_dense_entry_(combo.data() + static_cast<std::uint32_t>(pos) * entry_size_, recno, ikey);
    std::memcpy(combo.data() + (static_cast<std::uint32_t>(pos) + 1u) * entry_size_,
                base + static_cast<std::uint32_t>(pos) * entry_size_,
                (max_ents - static_cast<std::uint32_t>(pos)) * entry_size_);

    const std::uint32_t total   = max_ents + 1;
    const std::uint32_t lft_cnt = total / 2;
    const std::uint32_t rgt_cnt = total - lft_cnt;

    std::uint32_t orig_lsib = page_lsib(pg.data());
    std::uint32_t orig_rsib = page_rsib(pg.data());

    if (path.empty()) {
        // Root is the dense leaf.  Allocate TWO new pages; root becomes branch.
        // Must allocate and WRITE left page before allocating right (file size grows).
        auto lp_r = alloc_page_();
        if (!lp_r) return lp_r.error();
        std::uint32_t left_pg = lp_r.value();

        // We don't know right_pg yet, so write left with a placeholder rsib,
        // then patch it after allocating right.
        Page left_page = pg;  // copy header (lv, sub-header, etc.)
        set_u16_le(left_page.data() + 2, static_cast<std::uint16_t>(lft_cnt));
        set_u32_le(left_page.data() + 4, orig_lsib);
        set_u32_le(left_page.data() + 8, ADI_INVALID_PAGE);  // filled in below
        std::memcpy(left_page.data() + ADI_DENSE_ENTRY_START,
                    combo.data(), lft_cnt * entry_size_);
        if (auto r = write_adi_page_(left_pg, left_page); !r) return r;
        // File has grown; now allocate right page.
        auto rp_r = alloc_page_();
        if (!rp_r) return rp_r.error();
        std::uint32_t right_pg = rp_r.value();
        // Patch left_page.rsib.
        set_u32_le(left_page.data() + 8, right_pg);
        if (auto r = write_adi_page_(left_pg, left_page); !r) return r;

        // Build right leaf.
        Page right_page{};
        set_u16_le(right_page.data(), leaf_lv);
        set_u16_le(right_page.data() + 2, static_cast<std::uint16_t>(rgt_cnt));
        set_u32_le(right_page.data() + 4, left_pg);
        set_u32_le(right_page.data() + 8, orig_rsib);
        std::memcpy(right_page.data() + 12, pg.data() + 12, 12);  // sub-header
        std::memcpy(right_page.data() + ADI_DENSE_ENTRY_START,
                    combo.data() + lft_cnt * entry_size_,
                    rgt_cnt * entry_size_);
        if (auto r = write_adi_page_(right_pg, right_page); !r) return r;

        // Update old right sibling's lsib pointer.
        if (orig_rsib != ADI_INVALID_PAGE) {
            Page rsib_pg{};
            if (auto r = read_adi_page_(orig_rsib, rsib_pg); !r) return r;
            set_u32_le(rsib_pg.data() + 4, right_pg);
            if (auto r = write_adi_page_(orig_rsib, rsib_pg); !r) return r;
        }

        // Invalidate cursor (root content changed completely).
        cur_pg_ = ADI_INVALID_PAGE; cur_idx_ = -1; cur_cnt_ = 0;

        // Get max keys then rewrite root as branch.
        std::string left_max, right_max;
        if (key_in_leaf_) {
            left_max  = dense_entry_key_from_buf(combo.data(), lft_cnt - 1,
                                                 entry_size_, key_total_len_);
            right_max = dense_entry_key_from_buf(combo.data(), total - 1,
                                                 entry_size_, key_total_len_);
        } else {
            auto lm = key_for_recno_(dense_recno_from_buf(
                combo.data(), lft_cnt - 1, entry_size_));
            if (!lm) return lm.error();
            auto rm = key_for_recno_(dense_recno_from_buf(
                combo.data(), total - 1, entry_size_));
            if (!rm) return rm.error();
            left_max = std::move(lm).value();
            right_max = std::move(rm).value();
        }

        return promote_split_(path, left_pg,  left_max,
                                    right_pg, right_max);
    }

    // Non-root split: left half stays in cur, right half goes to new page.
    auto rp_r = alloc_page_();
    if (!rp_r) return rp_r.error();
    std::uint32_t right_pg = rp_r.value();

    // Rewrite cur (left leaf) with left half.
    set_u16_le(pg.data() + 2, static_cast<std::uint16_t>(lft_cnt));
    set_u32_le(pg.data() + 8, right_pg);
    std::memcpy(pg.data() + ADI_DENSE_ENTRY_START,
                combo.data(), lft_cnt * entry_size_);
    if (auto r = write_adi_page_(cur, pg); !r) return r;

    // Build and write right leaf.
    Page right_page{};
    set_u16_le(right_page.data(), leaf_lv);
    set_u16_le(right_page.data() + 2, static_cast<std::uint16_t>(rgt_cnt));
    set_u32_le(right_page.data() + 4, cur);
    set_u32_le(right_page.data() + 8, orig_rsib);
    std::memcpy(right_page.data() + 12, pg.data() + 12, 12);  // sub-header
    std::memcpy(right_page.data() + ADI_DENSE_ENTRY_START,
                combo.data() + lft_cnt * entry_size_,
                rgt_cnt * entry_size_);
    if (auto r = write_adi_page_(right_pg, right_page); !r) return r;

    // Update orig_rsib.lsib.
    if (orig_rsib != ADI_INVALID_PAGE) {
        Page rsib_pg{};
        if (auto r = read_adi_page_(orig_rsib, rsib_pg); !r) return r;
        set_u32_le(rsib_pg.data() + 4, right_pg);
        if (auto r = write_adi_page_(orig_rsib, rsib_pg); !r) return r;
    }

    // Cursor may be in either half; invalidate to be safe.
    if (cur_pg_ == cur) {
        cur_page_ = pg;
        cur_cnt_  = static_cast<std::uint16_t>(lft_cnt);
        cur_rsib_ = right_pg;
        if (cur_idx_ >= static_cast<std::int32_t>(lft_cnt)) {
            cur_pg_  = ADI_INVALID_PAGE;
            cur_idx_ = -1;
        }
    }

    std::string left_max, right_max;
    if (key_in_leaf_) {
        left_max  = dense_entry_key_from_buf(combo.data(), lft_cnt - 1,
                                             entry_size_, key_total_len_);
        right_max = dense_entry_key_from_buf(combo.data(), total - 1,
                                             entry_size_, key_total_len_);
    } else {
        auto lm = key_for_recno_(dense_recno_from_buf(
            combo.data(), lft_cnt - 1, entry_size_));
        if (!lm) return lm.error();
        auto rm = key_for_recno_(dense_recno_from_buf(
            combo.data(), total - 1, entry_size_));
        if (!rm) return rm.error();
        left_max = std::move(lm).value();
        right_max = std::move(rm).value();
    }

    return promote_split_(path, cur,      left_max,
                                right_pg, right_max);
}

// ── AdiIndex::erase ──────────────────────────────────────────────────────────

util::Result<void> AdiIndex::erase(std::uint32_t recno, const std::string& key) {
    if (mode_ == IndexOpenMode::ReadOnly)
        return util::Error{5000, 0, "ADI index is read-only", ""};
    invalidate_pos_cache();

    // Normalise key.
    std::string ikey = key;
    if (char_key_) {
        if (ikey.size() < key_total_len_)
            ikey.append(key_total_len_ - ikey.size(), ' ');
        else
            ikey.resize(key_total_len_);
    } else {
        ikey.resize(8, '\0');
    }

    if (key_in_leaf_) return erase_v2_(recno, ikey);

    // Seek to the correct dense leaf (soft=true: positions at or after key).
    auto sk = seek_key(ikey, /*soft=*/true);
    if (!sk) return sk.error();
    if (sk.value().hit == SeekHit::AfterEnd || !sk.value().positioned)
        return util::Error{5044, 0, "ADI: key not found for erase", ""};

    // Scan the leaf for the entry to remove.  A v1 dense entry IS its recno —
    // the key is re-derived from the LIVE record bytes on every compare, and
    // on an update the engine has already written the NEW field values before
    // syncing indexes (Table::commit_dirty_record: writeback, then
    // sync_all_indexes_), so the stale entry's live key no longer equals
    // ikey.  Match by recno first: a recno appears at most once in the bag,
    // so erec == recno identifies the entry even when its key just moved.
    // Scan from 0 (not cur_idx_): with a decreased key the soft seek lands
    // PAST the stale entry.  The key compare stays, but only as an early
    // exit once entries sort past ikey.
    for (;;) {
        if (cur_pg_ == ADI_INVALID_PAGE) break;
        for (int i = 0; i < static_cast<int>(cur_cnt_); ++i) {
            std::uint32_t erec = dense_entry_recno(cur_page_.data(), i, entry_size_);
            if (erec != recno) {
                auto ek = key_for_recno_(erec);
                if (!ek) return ek.error();
                if (compare_keys_(ek.value(), ikey) > 0)
                    return util::Error{5044, 0, "ADI: key not found for erase", ""};
                continue;
            }
            // Found — remove entry i.
            std::uint8_t* base = cur_page_.data() + ADI_DENSE_ENTRY_START;
            std::uint32_t move_n = static_cast<std::uint32_t>(cur_cnt_) - 1
                                   - static_cast<std::uint32_t>(i);
            if (move_n > 0)
                std::memmove(base + static_cast<std::uint32_t>(i) * entry_size_,
                             base + (static_cast<std::uint32_t>(i) + 1u) * entry_size_,
                             move_n * entry_size_);
            --cur_cnt_;
            set_u16_le(cur_page_.data() + 2, cur_cnt_);

            // Adjust cursor index.
            if (cur_idx_ >= static_cast<int>(cur_cnt_)) {
                cur_idx_ = static_cast<int>(cur_cnt_) - 1;
            }

            // Remember the page number before we potentially clear cur_pg_.
            std::uint32_t write_pg = cur_pg_;

            if (cur_cnt_ == 0) {
                // Page is now empty: bypass it in sibling links.
                std::uint32_t lsib = page_lsib(cur_page_.data());
                std::uint32_t rsib = page_rsib(cur_page_.data());
                if (lsib != ADI_INVALID_PAGE) {
                    Page lp{};
                    if (auto r = read_adi_page_(lsib, lp); !r) return r;
                    set_u32_le(lp.data() + 8, rsib);
                    if (auto r = write_adi_page_(lsib, lp); !r) return r;
                }
                if (rsib != ADI_INVALID_PAGE) {
                    Page rp{};
                    if (auto r = read_adi_page_(rsib, rp); !r) return r;
                    set_u32_le(rp.data() + 4, lsib);
                    if (auto r = write_adi_page_(rsib, rp); !r) return r;
                }
                cur_pg_  = ADI_INVALID_PAGE;
                cur_idx_ = -1;
            }

            return write_adi_page_(write_pg, cur_page_);
        }
        // Not on this leaf: advance to right sibling.
        if (cur_rsib_ == ADI_INVALID_PAGE) break;
        if (auto r = load_dense_leaf_(cur_rsib_); !r) return r.error();
        cur_idx_ = 0;
    }
    return util::Error{5044, 0, "ADI: key not found for erase", ""};
}

// ── AdiIndex::erase_v2_ ──────────────────────────────────────────────────────

util::Result<void> AdiIndex::erase_v2_(std::uint32_t recno,
                                       const std::string& ikey) {
    // Position at/after the key; seek_key decodes the owning leaf into
    // leaf_entries_ and leaves cur_idx_ on the first entry >= ikey.
    auto sk = seek_key(ikey, /*soft=*/true);
    if (!sk) return sk.error();
    if (sk.value().hit == SeekHit::AfterEnd || !sk.value().positioned)
        return util::Error{5044, 0, "ADI: key not found for erase", ""};

    for (;;) {
        if (cur_pg_ == ADI_INVALID_PAGE) break;
        for (std::size_t i = static_cast<std::size_t>(cur_idx_ < 0 ? 0 : cur_idx_);
             i < leaf_entries_.size(); ++i) {
            int cmp = compare_keys_(leaf_entries_[i].second, ikey);
            if (cmp > 0)
                return util::Error{5044, 0, "ADI: key not found for erase", ""};
            if (cmp == 0 && leaf_entries_[i].first == recno) {
                const std::uint32_t write_pg = cur_pg_;
                const std::uint32_t lsib = cur_lsib_;
                const std::uint32_t rsib = cur_rsib_;
                std::vector<std::pair<std::uint32_t, std::string>> ents =
                    leaf_entries_;
                ents.erase(ents.begin() + static_cast<std::ptrdiff_t>(i));

                if (ents.empty()) {
                    // Bypass the now-empty leaf in the sibling chain, then write
                    // it back empty (abandoned in place, like the legacy erase).
                    if (lsib != ADI_INVALID_PAGE) {
                        Page lp{};
                        if (auto r = read_adi_page_(lsib, lp); !r) return r;
                        set_u32_le(lp.data() + 8, rsib);
                        if (auto r = write_adi_page_(lsib, lp); !r) return r;
                    }
                    if (rsib != ADI_INVALID_PAGE) {
                        Page rp{};
                        if (auto r = read_adi_page_(rsib, rp); !r) return r;
                        set_u32_le(rp.data() + 4, lsib);
                        if (auto r = write_adi_page_(rsib, rp); !r) return r;
                    }
                    Page ep{};
                    render_v2_leaf_(ep, ents, lsib, rsib);  // count=0
                    if (auto r = write_adi_page_(write_pg, ep); !r) return r;
                    cur_pg_ = ADI_INVALID_PAGE;
                    cur_idx_ = -1;
                    cur_cnt_ = 0;
                    leaf_entries_.clear();
                    return {};
                }

                Page np{};
                if (!render_v2_leaf_(np, ents, lsib, rsib))
                    return util::Error{5000, 0, "ADI v2 erase: render overflow", ""};
                if (auto r = write_adi_page_(write_pg, np); !r) return r;
                cur_page_     = np;
                leaf_entries_ = std::move(ents);
                cur_cnt_      = static_cast<std::uint16_t>(leaf_entries_.size());
                if (cur_idx_ > static_cast<std::int32_t>(i)) --cur_idx_;
                if (cur_idx_ >= static_cast<std::int32_t>(cur_cnt_))
                    cur_idx_ = static_cast<std::int32_t>(cur_cnt_) - 1;
                return {};
            }
        }
        // Key may continue on the right sibling.
        if (cur_rsib_ == ADI_INVALID_PAGE) break;
        if (auto r = load_dense_leaf_(cur_rsib_); !r) return r.error();
        cur_idx_ = 0;
    }
    return util::Error{5044, 0, "ADI: key not found for erase", ""};
}

// ── AdiIndex::flush ──────────────────────────────────────────────────────────

util::Result<void> AdiIndex::flush() {
    return adi_file_.sync();
}

// ── ADI create helpers (legacy single-tag layout) ───────────────────────────

bool adt_type_is_char_key(std::uint16_t adt_type) noexcept {
    return adt_type == ADT_TYPE_CICHAR || adt_type == ADT_TYPE_CHAR;
}

void write_adi_file_header_page(AdiIndex::Page& pg) noexcept {
    pg.fill(0);
    set_u16_le(pg.data(), 2);
    set_u16_le(pg.data() + 2, 0);
    set_u32_le(pg.data() + 8, 1);
    pg[12] = 0x80;
    pg[14] = 0x60;
    pg[15] = 0x20;
    pg[17] = 0x04;
    pg[19] = 0x02;
    pg[20] = 0x29;
    pg[21] = 0xC4;
    pg[22] = 0xF6;
    pg[23] = 0x1E;
    pg[506] = 0x01;
    pg[510] = 0x01;
}

void write_adi_tag_directory_page(AdiIndex::Page& pg,
                                const AdiIndex::CreateParams& cp) noexcept {
    pg.fill(0);
    set_u16_le(pg.data(), ADI_LVL_TAGDIR);
    set_u16_le(pg.data() + 2, 1);
    for (std::size_t i = 4; i < 12; ++i) pg[i] = 0xFF;
    if (cp.adt_hdr_len >= 528u) {
        set_u16_le(pg.data() + 12,
                   static_cast<std::uint16_t>(cp.adt_hdr_len - 528u));
    }
    for (std::size_t i = 14; i < 20; ++i) pg[i] = 0xFF;
    pg[20] = 0x20;
    pg[21] = 0x08;
    pg[22] = 0x08;
    pg[23] = 0x06;

    // xx=3 → per-tag header page; F-marker page 4; root dense leaf page 5.
    // Written as u32 LE (see scan_tagdir): the first tag always fits in one
    // byte, later ones do not, and both paths must agree on the width.
    constexpr std::uint32_t kTagHdrPg = 3;
    set_u32_le( pg.data() + ADI_TAGDIR_ENTRY_START, kTagHdrPg );
    if (!cp.field_name.empty())
        pg[ADI_TAGDIR_ENTRY_START + 5] =
            static_cast<std::uint8_t>(cp.field_name[0]);

    std::string footer;
    footer.reserve(cp.field_name.size());
    for (char c : cp.field_name)
        footer.push_back(static_cast<char>(std::toupper(
            static_cast<unsigned char>(c))));
    if (footer.size() > 10) footer.resize(10);
    if (!footer.empty()) {
        const std::size_t off = ADI_PAGE_SIZE - footer.size();
        std::memcpy(pg.data() + off, footer.data(), footer.size());
    }
}

void write_adi_per_tag_header_page(AdiIndex::Page& pg,
                                 const AdiIndex::CreateParams& cp,
                                 std::uint16_t tag_ordinal = 0) noexcept {
    pg.fill(0);
    const bool is_char = adt_type_is_char_key(cp.adt_type);
    std::uint16_t lvl = 5;
    if (tag_ordinal == 0)
        lvl = is_char ? 6 : 5;
    else
        lvl = is_char ? 8 : 6;
    set_u16_le(pg.data(), lvl);
    pg[12] = static_cast<std::uint8_t>(cp.fld_length & 0xFFu);
    pg[14] = 0x60;
    pg[15] = is_char ? 0x26 : 0x00;
    if (cp.unique) pg[14] |= 0x01u;
    pg[17] = 0x04;
    pg[506] = 0x01;
    pg[510] = 0x03;
    // v2 metadata (tag name / key expr / FOR / klen). Additive: legacy readers
    // ignore offsets 40..495; v2 readers prefer it over the F-marker identity.
    write_adi_v2_meta(pg, cp);
}

void write_fmarker_page(AdiIndex::Page& pg, std::uint8_t field_num) noexcept {
    pg.fill(0);
    pg[0] = 'F';
    std::string nums = std::to_string(static_cast<unsigned>(field_num));
    if (nums.size() > 8) nums.resize(8);
    std::memcpy(pg.data() + 1, nums.data(), nums.size());
}

void write_empty_dense_leaf_page(AdiIndex::Page& pg,
                                 std::uint16_t adt_type,
                                 std::uint16_t fld_length) noexcept {
    pg.fill(0);
    set_u16_le(pg.data(), ADI_LVL_DENSE);
    set_u16_le(pg.data() + 2, 0);
    set_u32_le(pg.data() + 4, ADI_INVALID_PAGE);
    set_u32_le(pg.data() + 8, ADI_INVALID_PAGE);
    pg[12] = 0xE8;
    pg[13] = 0x01;
    const bool is_char = adt_type_is_char_key(adt_type);
    if (is_char && fld_length >= 20u) {
        pg[14] = 0xFF;
        pg[15] = 0x3F;
        pg[18] = 0x1F;
        pg[19] = 0x1F;
        pg[20] = 0x0E;
        pg[21] = 0x05;
        pg[22] = 0x05;
        pg[23] = 0x03;
    } else {
        pg[14] = 0xFF;
        pg[15] = 0xFF;
        pg[18] = 0x0F;
        pg[19] = 0x0F;
        pg[20] = 0x10;
        pg[21] = 0x04;
        pg[22] = 0x04;
        pg[23] = 0x03;
    }
}

// ── AdiIndex::create ─────────────────────────────────────────────────────────

util::Result<AdiIndex> AdiIndex::create(const std::string& adi_path,
                                        const CreateParams& params) {
    if (params.field_num == 0)
        return util::Error{5004, 0, "ADI create: field_num must be >= 1", ""};
    if (params.field_name.empty())
        return util::Error{5004, 0, "ADI create: field_name required", ""};
    if (params.adt_hdr_len < 400 || params.adt_rec_len == 0)
        return util::Error{5004, 0, "ADI create: invalid ADT layout", ""};

    // Ensure the parent directory for the .ADI exists. In some PRG contexts
    // (different cPatTem, temp copies, or path construction in _Indexar),
    // the index bag path may point to a dir that wasn't explicitly created.
    // This makes creation more robust (real ADS would also need the dir).
    {
        namespace fs = std::filesystem;
        fs::path ap(adi_path);
        fs::path par = ap.parent_path();
        if (!par.empty()) {
            std::error_code ec;
            fs::create_directories(par, ec);
            // ignore ec; if it fails the subsequent open will report it
        }
    }

    // Remove any existing file (stale from previous failed attempt may be locked or partial).
    {
        std::error_code ec;
        std::filesystem::remove(adi_path, ec);
    }

    auto fres = platform::File::open(adi_path, platform::OpenMode::CreateRW);
    if (!fres) return fres.error();
    platform::File file = std::move(fres).value();

    const bool is_char = adt_type_is_char_key(params.adt_type);
    // Char-key first tag: 7 pages (spare dense leaf at pg 6).
    // Numeric-key first tag: 6 pages (root dense leaf at pg 5 only).
    const std::uint32_t kPages = is_char ? 7u : 6u;
    for (std::uint32_t pgno = 0; pgno < kPages; ++pgno) {
        Page pg{};
        switch (pgno) {
            case 0: write_adi_file_header_page(pg); break;
            case 1: break;
            case 2: write_adi_tag_directory_page(pg, params); break;
            case 3: write_adi_per_tag_header_page(pg, params, 0); break;
            case 4: write_fmarker_page(pg, params.field_num); break;
            case 5:
                write_empty_dense_leaf_page(pg, params.adt_type,
                                            params.fld_length);
                break;
            case 6:
                if (is_char) {
                    write_empty_dense_leaf_page(pg, params.adt_type,
                                                params.fld_length);
                }
                break;
            default: break;
        }
        auto wrote = file.write_at(static_cast<std::uint64_t>(pgno) * ADI_PAGE_SIZE,
                                   pg.data(), pg.size());
        if (!wrote) return wrote.error();
        if (wrote.value() != ADI_PAGE_SIZE)
            return util::Error{5000, 0, "short ADI page write", adi_path};
    }
    if (auto s = file.sync(); !s) return s.error();

    AdiIndex ix;
    ix.adi_file_ = std::move(file);
    ix.adi_path_ = adi_path;
    ix.mode_     = IndexOpenMode::Shared;

    // A non-structural bag's .adi stem differs from the table's, so the
    // companion ADT path cannot be derived from the .adi name — use the
    // caller-supplied table path when present, else the structural default.
    std::string adt_p = params.adt_path.empty()
        ? adt_path_for(adi_path) : params.adt_path;
    std::uint32_t hlen = params.adt_hdr_len, rlen = params.adt_rec_len;

    auto fa = platform::File::open(adt_p, platform::OpenMode::ReadOnly);
    std::vector<AdtFieldDesc> fields_vec;
    if (fa) {
        ix.adt_file_ = std::move(fa).value();
        auto fields = read_adt_fields(ix.adt_file_, hlen, rlen);
        if (fields) {
            fields_vec = std::move(fields).value();
        }
    }
    if (fields_vec.empty()) {
        // Fallback when re-opening the ADT data file fails (e.g. share/lock
        // issues with .DAT + ADS_ADT, or path casing). Use the info from
        // CreateParams (populated from the already-open Table).
        AdtFieldDesc fd;
        fd.type = params.adt_type;
        fd.offset = params.record_offset;
        fd.length = params.fld_length;
        fd.name = params.field_name;
        fields_vec.push_back(fd);
    }

    std::vector<std::uint16_t> types;
    std::vector<std::uint16_t> offsets;
    std::vector<std::uint16_t> lengths;
    std::vector<std::string>   names;
    for (const auto& fd : fields_vec) {
        types.push_back(fd.type);
        offsets.push_back(fd.offset);
        lengths.push_back(fd.length);
        names.push_back(fd.name);
    }

    // Always try to have adt_file_ open for later key_for_recno_ during insert and navigation.
    if (!fa) {
        auto fa2 = platform::File::open(adt_p, platform::OpenMode::ReadOnly);
        if (fa2) {
            ix.adt_file_ = std::move(fa2).value();
        }
    }

    std::vector<std::uint8_t> fnums{params.field_num};
    constexpr std::uint32_t kRootPage = 5;
    if (auto r = ix.apply_tag_(fnums, kRootPage, types, offsets, lengths, names,
                               hlen, rlen,
                               params.unique, params.key_len);
        !r) {
        return r.error();
    }
    // v2 identity / expression in memory (matches what was persisted on disk).
    if (!params.tag_name.empty()) ix.tag_name_ = params.tag_name;
    ix.tag_expr_   = params.key_expr;
    ix.tag_cond_   = params.for_expr;
    ix.descending_ = params.descending;
    return ix;
}

std::string read_adi_footer_field_names(const AdiIndex::Page& pg2) {
    std::string foot;
    for (std::size_t i = 500; i < ADI_PAGE_SIZE; ++i) {
        if (pg2[i] != 0)
            foot.push_back(static_cast<char>(pg2[i]));
    }
    return foot;
}

void append_adi_footer_field_name(AdiIndex::Page& pg2,
                                  const std::string& field_name) {
    std::string foot = read_adi_footer_field_names(pg2);
    for (char c : field_name)
        foot.push_back(static_cast<char>(std::toupper(
            static_cast<unsigned char>(c))));
    if (foot.size() > 10) foot.resize(10);
    std::memset(pg2.data() + 500, 0, 12);
    if (!foot.empty()) {
        const std::size_t off = ADI_PAGE_SIZE - foot.size();
        std::memcpy(pg2.data() + off, foot.data(), foot.size());
    }
}

// ── AdiIndex::add_tag ────────────────────────────────────────────────────────

util::Result<AdiIndex> AdiIndex::add_tag(const std::string& adi_path,
                                         const CreateParams& params) {
    if (params.field_num == 0)
        return util::Error{5004, 0, "ADI add_tag: field_num must be >= 1", ""};
    if (params.field_name.empty())
        return util::Error{5004, 0, "ADI add_tag: field_name required", ""};

    auto fres = platform::File::open(adi_path, platform::OpenMode::OpenExisting);
    if (!fres) return fres.error();
    platform::File file = std::move(fres).value();

    // Dedup by TAG NAME in v2 (lets N tags share a field, e.g. ORD1/ORD3/ORD4
    // all over field 0); legacy callers without a tag_name dedup by field name.
    const std::string& dedup_key =
        params.tag_name.empty() ? params.field_name : params.tag_name;
    auto existing = list_tags(adi_path, params.adt_path);
    if (!existing) return existing.error();
    for (const auto& tn : existing.value()) {
        if (tn.size() != dedup_key.size()) continue;
        bool eq = true;
        for (std::size_t i = 0; i < tn.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(tn[i])) !=
                std::tolower(static_cast<unsigned char>(dedup_key[i]))) {
                eq = false;
                break;
            }
        }
        if (eq) {
            return util::Error{5044, 0,
                "ADI already has a tag: " + dedup_key, ""};
        }
    }

    Page pg2{};
    auto got = file.read_at(2 * ADI_PAGE_SIZE, pg2.data(), pg2.size());
    if (!got || got.value() < ADI_PAGE_SIZE)
        return util::Error{6106, 0, "can't read ADI tag directory", adi_path};

    std::uint16_t count = page_count(pg2.data());
    constexpr std::uint32_t kMaxTags =
        (ADI_PAGE_SIZE - ADI_TAGDIR_ENTRY_START) / ADI_TAGDIR_ENTRY_SIZE;
    if (count >= kMaxTags)
        return util::Error{5000, 0, "ADI tag directory full", adi_path};

    auto sz = file.size();
    if (!sz) return sz.error();
    const std::uint32_t hdr_pg =
        static_cast<std::uint32_t>(sz.value() / ADI_PAGE_SIZE);
    const std::uint32_t fmk_pg  = hdr_pg + 1u;
    const std::uint32_t root_pg = hdr_pg + 2u;

    Page hdr_pg_buf{};
    write_adi_per_tag_header_page(hdr_pg_buf, params, count);
    if (auto w = file.write_at(static_cast<std::uint64_t>(hdr_pg) * ADI_PAGE_SIZE,
                               hdr_pg_buf.data(), hdr_pg_buf.size());
        !w || w.value() != ADI_PAGE_SIZE) {
        return util::Error{5000, 0, "ADI add_tag: short per-tag header write", ""};
    }

    Page fmk_pg_buf{};
    write_fmarker_page(fmk_pg_buf, params.field_num);
    if (auto w = file.write_at(static_cast<std::uint64_t>(fmk_pg) * ADI_PAGE_SIZE,
                               fmk_pg_buf.data(), fmk_pg_buf.size());
        !w || w.value() != ADI_PAGE_SIZE) {
        return util::Error{5000, 0, "ADI add_tag: short F-marker write", ""};
    }

    Page dense_pg_buf{};
    write_empty_dense_leaf_page(dense_pg_buf, params.adt_type, params.fld_length);
    if (auto w = file.write_at(static_cast<std::uint64_t>(root_pg) * ADI_PAGE_SIZE,
                               dense_pg_buf.data(), dense_pg_buf.size());
        !w || w.value() != ADI_PAGE_SIZE) {
        return util::Error{5000, 0, "ADI add_tag: short dense leaf write", ""};
    }

    // Tag directory entry ordering: append (default) or prepend.
    //   append  — ordinals follow creation order (matches CDX/Harbour convention)
    //   prepend — ordinals are reversed (matches SAP Advantage Data Architect)
    if (params.prepend_tag_dir) {
        // Prepend: shift existing entries down and write new entry at start.
        for (std::int32_t i = static_cast<std::int32_t>(count) - 1; i >= 0; --i) {
            std::size_t src = ADI_TAGDIR_ENTRY_START
                            + static_cast<std::size_t>(i) * ADI_TAGDIR_ENTRY_SIZE;
            std::size_t dst = src + ADI_TAGDIR_ENTRY_SIZE;
            std::memmove(pg2.data() + dst, pg2.data() + src, ADI_TAGDIR_ENTRY_SIZE);
        }
        set_u32_le( pg2.data() + ADI_TAGDIR_ENTRY_START, hdr_pg );
        if (!params.field_name.empty()) {
            pg2[ADI_TAGDIR_ENTRY_START + 5] = static_cast<std::uint8_t>(params.field_name[0]);
        }
    } else {
        // Append: new entry goes at the end (default, creation-order).
        std::size_t new_off = ADI_TAGDIR_ENTRY_START
                            + static_cast<std::size_t>(count) * ADI_TAGDIR_ENTRY_SIZE;
        set_u32_le( pg2.data() + new_off, hdr_pg );
        if (!params.field_name.empty()) {
            pg2[new_off + 5] = static_cast<std::uint8_t>(params.field_name[0]);
        }
    }
    set_u16_le(pg2.data() + 2, count + 1);

    std::uint16_t meta = u16_le(pg2.data() + 12);
    if (meta >= 2) set_u16_le(pg2.data() + 12, meta - 2);

    append_adi_footer_field_name(pg2, params.field_name);

    if (auto w = file.write_at(2 * ADI_PAGE_SIZE, pg2.data(), pg2.size());
        !w || w.value() != ADI_PAGE_SIZE) {
        return util::Error{5000, 0, "ADI add_tag: short tag directory write", ""};
    }
    if (auto s = file.sync(); !s) return s.error();

    AdiIndex ix;
    ix.adi_file_ = std::move(file);
    ix.adi_path_ = adi_path;
    ix.mode_     = IndexOpenMode::Shared;

    // See AdiIndex::create — a non-structural bag needs the real table path.
    std::string adt_p = params.adt_path.empty()
        ? adt_path_for(adi_path) : params.adt_path;
    auto fa = platform::File::open(adt_p, platform::OpenMode::ReadOnly);
    if (!fa) return fa.error();
    ix.adt_file_ = std::move(fa).value();

    std::uint32_t hlen = params.adt_hdr_len, rlen = params.adt_rec_len;
    auto fields = read_adt_fields(ix.adt_file_, hlen, rlen);
    if (!fields) return fields.error();

    std::vector<std::uint16_t> types, offsets, lengths;
    std::vector<std::string>   names;
    for (const auto& fd : fields.value()) {
        types.push_back(fd.type);
        offsets.push_back(fd.offset);
        lengths.push_back(fd.length);
        names.push_back(fd.name);
    }

    std::vector<std::uint8_t> fnums{params.field_num};
    if (auto r = ix.apply_tag_(fnums, root_pg, types, offsets, lengths, names,
                               hlen, rlen, params.unique, params.key_len);
        !r) {
        return r.error();
    }
    if (!params.tag_name.empty()) ix.tag_name_ = params.tag_name;
    ix.tag_expr_   = params.key_expr;
    ix.tag_cond_   = params.for_expr;
    ix.descending_ = params.descending;
    return ix;
}

// ── AdiIndex::build_bulk ─────────────────────────────────────────────────────

util::Result<void> AdiIndex::build_bulk(
    std::vector<std::pair<std::string, std::uint32_t>> keys) {
    if (mode_ == IndexOpenMode::ReadOnly)
        return util::Error{5000, 0, "ADI index is read-only", ""};

    if (!key_in_leaf_) {
        // Legacy field-derived tag: pack fixed-size dense leaves full (SAP-
        // compatible 2/3-byte entries). The old path fell back to per-record
        // insert(), which 50/50-splits full leaves and left the bag ~2× larger
        // than a bottom-up pack (10k char keys: 143 pages insert vs ~70 packed).
        const std::uint32_t klen = key_total_len_;
        auto norm = [this, klen](const std::string& s) {
            std::string k = s;
            if (k.size() < klen)
                k.append(klen - k.size(), char_key_ ? ' ' : '\0');
            else
                k.resize(klen);
            return k;
        };
        for (auto& kv : keys) kv.first = norm(kv.first);
        std::sort(keys.begin(), keys.end(),
                  [this](const std::pair<std::string, std::uint32_t>& a,
                         const std::pair<std::string, std::uint32_t>& b) {
                      int c = compare_keys_(a.first, b.first);
                      if (c != 0) return c < 0;
                      return a.second < b.second;
                  });

        invalidate_pos_cache();
        ordered_recnos_.clear();
        pos_of_recno_.clear();
        ordered_recnos_.reserve(keys.size());
        for (const auto& kv : keys) {
            pos_of_recno_[kv.second] =
                static_cast<std::uint32_t>(ordered_recnos_.size());
            ordered_recnos_.push_back(kv.second);
        }
        pos_cache_valid_ = true;

        if (keys.empty()) return clear_data();

        const std::uint32_t max_ents =
            (ADI_PAGE_SIZE - ADI_DENSE_ENTRY_START) / entry_size_;
        if (max_ents < 1)
            return util::Error{5000, 0, "ADI build_bulk: zero dense capacity", ""};

        std::vector<std::pair<std::size_t, std::size_t>> runs;
        {
            const std::size_t n = keys.size();
            for (std::size_t lo = 0; lo < n; lo += max_ents)
                runs.push_back({lo, std::min(n, lo + max_ents)});
        }

        auto write_legacy_leaf = [&](std::uint32_t page_no,
                                     std::size_t lo, std::size_t hi,
                                     std::uint32_t lsib, std::uint32_t rsib)
                -> util::Result<std::string> {
            Page pg{};
            write_empty_dense_leaf_page(pg, adt_type_, fld_length_);
            set_u16_le(pg.data() + 2, static_cast<std::uint16_t>(hi - lo));
            set_u32_le(pg.data() + 4, lsib);
            set_u32_le(pg.data() + 8, rsib);
            std::uint8_t* base = pg.data() + ADI_DENSE_ENTRY_START;
            for (std::size_t i = lo; i < hi; ++i) {
                build_dense_entry_(base + (i - lo) * entry_size_,
                                   keys[i].second, keys[i].first);
            }
            if (auto w = write_adi_page_(page_no, pg); !w) return w.error();
            return keys[hi - 1].first;
        };

        struct Node { std::string max_key; std::uint32_t page; };

        auto write_branch = [&](std::uint32_t page_no, const std::vector<Node>& lv,
                                std::size_t lo, std::size_t hi)
                -> util::Result<std::string> {
            Page pg{};
            set_u16_le(pg.data(), ADI_LVL_BRANCH);
            set_u16_le(pg.data() + 2, static_cast<std::uint16_t>(hi - lo));
            set_u32_le(pg.data() + 4, ADI_INVALID_PAGE);
            set_u32_le(pg.data() + 8, ADI_INVALID_PAGE);
            for (std::size_t i = lo; i < hi; ++i) {
                std::uint8_t* dst = pg.data() + ADI_TREE_ENTRY_START
                                  + (i - lo) * branch_entry_sz_;
                std::memset(dst, 0, branch_entry_sz_);
                const std::string& k = lv[i].max_key;
                if (char_key_) {
                    std::memcpy(dst, k.data(),
                                std::min<std::size_t>(klen, k.size()));
                    std::uint8_t* pp = dst + char_key_padded_len_ + 4;
                    std::uint32_t pno = lv[i].page;
                    pp[0] = static_cast<std::uint8_t>( pno        & 0xFFu);
                    pp[1] = static_cast<std::uint8_t>((pno >>  8) & 0xFFu);
                    pp[2] = static_cast<std::uint8_t>((pno >> 16) & 0xFFu);
                    pp[3] = static_cast<std::uint8_t>((pno >> 24) & 0xFFu);
                } else {
                    std::memcpy(dst, k.data(),
                                std::min<std::size_t>(8, k.size()));
                    set_u32_be(dst + 12, lv[i].page);
                }
            }
            if (auto w = write_adi_page_(page_no, pg); !w) return w.error();
            return lv[hi - 1].max_key;
        };

        if (runs.size() == 1) {
            auto mk = write_legacy_leaf(root_page_, 0, keys.size(),
                                        ADI_INVALID_PAGE, ADI_INVALID_PAGE);
            if (!mk) return mk.error();
            cur_pg_ = ADI_INVALID_PAGE; cur_idx_ = -1;
            return {};
        }

        std::vector<Node> level;
        {
            const std::size_t nleaves = runs.size();
            std::vector<std::uint32_t> pages(nleaves);
            for (std::size_t i = 0; i < nleaves; ++i) {
                auto p = alloc_page_(); if (!p) return p.error();
                pages[i] = p.value();
            }
            level.reserve(nleaves);
            for (std::size_t i = 0; i < nleaves; ++i) {
                std::uint32_t lsib = (i == 0) ? ADI_INVALID_PAGE : pages[i - 1];
                std::uint32_t rsib =
                    (i + 1 < nleaves) ? pages[i + 1] : ADI_INVALID_PAGE;
                auto mk = write_legacy_leaf(pages[i], runs[i].first,
                                            runs[i].second, lsib, rsib);
                if (!mk) return mk.error();
                level.push_back({std::move(mk).value(), pages[i]});
            }
        }

        const std::uint32_t max_branch =
            (ADI_PAGE_SIZE - ADI_TREE_ENTRY_START) / branch_entry_sz_;
        if (max_branch < 2)
            return util::Error{5000, 0,
                "ADI build_bulk: branch fanout < 2 (key too wide)", ""};

        while (level.size() > max_branch) {
            std::vector<Node> next;
            const std::size_t m = level.size();
            const std::size_t nbr = (m + max_branch - 1) / max_branch;
            std::vector<std::uint32_t> pages(nbr);
            for (std::size_t i = 0; i < nbr; ++i) {
                auto p = alloc_page_(); if (!p) return p.error();
                pages[i] = p.value();
            }
            next.reserve(nbr);
            for (std::size_t i = 0; i < nbr; ++i) {
                std::size_t lo = i * max_branch;
                std::size_t hi = std::min(m, lo + max_branch);
                auto mk = write_branch(pages[i], level, lo, hi);
                if (!mk) return mk.error();
                next.push_back({std::move(mk).value(), pages[i]});
            }
            level = std::move(next);
        }
        if (auto top = write_branch(root_page_, level, 0, level.size()); !top)
            return top.error();
        cur_pg_ = ADI_INVALID_PAGE; cur_idx_ = -1;
        return {};
    }

    const std::uint32_t klen = key_total_len_;
    // Sort by (key, recno): opaque memcmp on klen bytes, recno tie-break — the
    // SAME order the per-record insert produces, so navigation is identical.
    auto norm = [klen](const std::string& s) {
        std::string k = s;
        if (k.size() < klen) k.append(klen - k.size(), ' ');
        else k.resize(klen);
        return k;
    };
    for (auto& kv : keys) kv.first = norm(kv.first);
    std::sort(keys.begin(), keys.end(),
              [](const std::pair<std::string, std::uint32_t>& a,
                 const std::pair<std::string, std::uint32_t>& b) {
                  if (a.first != b.first) return a.first < b.first;
                  return a.second < b.second;
              });

    // Seed the position cache directly from the sorted set (no re-walk needed).
    invalidate_pos_cache();
    ordered_recnos_.reserve(keys.size());
    for (const auto& kv : keys) {
        pos_of_recno_[kv.second] = static_cast<std::uint32_t>(ordered_recnos_.size());
        ordered_recnos_.push_back(kv.second);
    }
    pos_cache_valid_ = true;

    if (keys.empty()) return clear_data();

    const std::size_t cap = ADI_PAGE_SIZE - ADI_DENSE_ENTRY_START;

    // Partition the sorted keys into front-coded leaf runs: greedily extend a
    // run while its encoded size stays within one page (always ≥1 entry/leaf).
    // The per-leaf entry count is now VARIABLE — compression decides how many
    // keys fit, so we can't slice by a fixed max_leaf any more.
    std::vector<std::pair<std::size_t, std::size_t>> runs;  // [lo, hi)
    {
        const std::size_t n = keys.size();
        std::size_t lo = 0;
        while (lo < n) {
            std::size_t sz = 5u + klen;          // first entry of a leaf: dup=0
            std::size_t hi = lo + 1;
            while (hi < n) {
                std::uint8_t dup = fc_dup(keys[hi - 1].first.data(),
                                          keys[hi].first.data(), klen);
                std::size_t add = 5u + (static_cast<std::size_t>(klen) - dup);
                if (sz + add > cap) break;
                sz += add;
                ++hi;
            }
            runs.push_back({lo, hi});
            lo = hi;
        }
    }

    auto write_leaf_run = [&](std::uint32_t page_no, std::size_t lo, std::size_t hi,
                              std::uint32_t lsib, std::uint32_t rsib)
            -> util::Result<std::string> {
        std::vector<std::pair<std::uint32_t, std::string>> ents;
        ents.reserve(hi - lo);
        for (std::size_t i = lo; i < hi; ++i)
            ents.emplace_back(keys[i].second, keys[i].first);
        Page pg{};
        if (!render_v2_leaf_(pg, ents, lsib, rsib))
            return util::Error{5000, 0, "ADI build_bulk: leaf run overflow", ""};
        if (auto w = write_adi_page_(page_no, pg); !w) return w.error();
        return keys[hi - 1].first;  // max key of this leaf (already klen padded)
    };

    // Single dense leaf fits in the root.
    if (runs.size() == 1) {
        auto mk = write_leaf_run(root_page_, 0, keys.size(),
                                 ADI_INVALID_PAGE, ADI_INVALID_PAGE);
        if (!mk) return mk.error();
        cur_pg_ = ADI_INVALID_PAGE; cur_idx_ = -1;
        return {};
    }

    // Multi-level: build leaves in NEW pages (linked), then branch levels, with
    // the TOP node written into root_page_ (the tag-directory-derived root, so a
    // reopen finds it without a stored pointer).
    struct Node { std::string max_key; std::uint32_t page; };
    std::vector<Node> level;
    {
        const std::size_t nleaves = runs.size();
        std::vector<std::uint32_t> pages(nleaves);
        for (std::size_t i = 0; i < nleaves; ++i) {
            auto p = alloc_page_(); if (!p) return p.error();
            pages[i] = p.value();
        }
        level.reserve(nleaves);
        for (std::size_t i = 0; i < nleaves; ++i) {
            std::uint32_t lsib = (i == 0) ? ADI_INVALID_PAGE : pages[i - 1];
            std::uint32_t rsib = (i + 1 < nleaves) ? pages[i + 1] : ADI_INVALID_PAGE;
            auto mk = write_leaf_run(pages[i], runs[i].first, runs[i].second,
                                     lsib, rsib);
            if (!mk) return mk.error();
            level.push_back({std::move(mk).value(), pages[i]});
        }
    }

    const std::uint32_t max_branch =
        (ADI_PAGE_SIZE - ADI_TREE_ENTRY_START) / branch_entry_sz_;
    if (max_branch < 2)
        return util::Error{5000, 0, "ADI build_bulk: branch fanout < 2 (key too wide)", ""};

    auto write_branch = [&](std::uint32_t page_no, const std::vector<Node>& lv,
                            std::size_t lo, std::size_t hi)
            -> util::Result<std::string> {
        Page pg{};
        set_u16_le(pg.data(), ADI_LVL_BRANCH);
        set_u16_le(pg.data() + 2, static_cast<std::uint16_t>(hi - lo));
        set_u32_le(pg.data() + 4, ADI_INVALID_PAGE);
        set_u32_le(pg.data() + 8, ADI_INVALID_PAGE);
        for (std::size_t i = lo; i < hi; ++i) {
            std::uint8_t* dst = pg.data() + ADI_TREE_ENTRY_START
                              + (i - lo) * branch_entry_sz_;
            std::memset(dst, 0, branch_entry_sz_);  // padded_key + cum[4]=0 + page[4]
            const std::string& k = lv[i].max_key;
            std::memcpy(dst, k.data(), std::min<std::size_t>(klen, k.size()));
            std::uint8_t* pp = dst + char_key_padded_len_ + 4;
            std::uint32_t pno = lv[i].page;
            pp[0] = static_cast<std::uint8_t>( pno        & 0xFFu);
            pp[1] = static_cast<std::uint8_t>((pno >>  8) & 0xFFu);
            pp[2] = static_cast<std::uint8_t>((pno >> 16) & 0xFFu);
            pp[3] = static_cast<std::uint8_t>((pno >> 24) & 0xFFu);
        }
        if (auto w = write_adi_page_(page_no, pg); !w) return w.error();
        return lv[hi - 1].max_key;
    };

    // Reduce branch levels until the top fits in one page → write it at root_page_.
    while (level.size() > max_branch) {
        std::vector<Node> next;
        const std::size_t m = level.size();
        const std::size_t nbr = (m + max_branch - 1) / max_branch;
        std::vector<std::uint32_t> pages(nbr);
        for (std::size_t i = 0; i < nbr; ++i) {
            auto p = alloc_page_(); if (!p) return p.error();
            pages[i] = p.value();
        }
        next.reserve(nbr);
        for (std::size_t i = 0; i < nbr; ++i) {
            std::size_t lo = i * max_branch;
            std::size_t hi = std::min(m, lo + max_branch);
            auto mk = write_branch(pages[i], level, lo, hi);
            if (!mk) return mk.error();
            next.push_back({std::move(mk).value(), pages[i]});
        }
        level = std::move(next);
    }
    if (auto top = write_branch(root_page_, level, 0, level.size()); !top)
        return top.error();
    cur_pg_ = ADI_INVALID_PAGE; cur_idx_ = -1;
    return {};
}

// ── AdiIndex::clear_data ─────────────────────────────────────────────────────

util::Result<void> AdiIndex::clear_data() {
    if (mode_ == IndexOpenMode::ReadOnly)
        return util::Error{5000, 0, "ADI index is read-only", ""};
    invalidate_pos_cache();  // emptied

    // Reset the tag's root to a single EMPTY dense leaf, regardless of the
    // current B-tree depth. For a large index (>1 level) the root page is a
    // BRANCH, not a dense leaf — the previous code rejected that with
    // "root is not a dense leaf" and aborted (ADSCDX/5000) when a CREATE INDEX
    // overwrite landed on a multi-level tag (e.g. reindexing ESTAELEC, 441k
    // recs). The root lives at a fixed page (fmk_pg+1) and promote_split_ keeps
    // root_page_ on root splits, so overwriting it with an empty dense leaf is
    // exactly the state a freshly created tag starts from; the caller's
    // per-record insert loop then rebuilds the tree. The old branch/leaf pages
    // are abandoned in the file (reclaimed on the next full REINDEX that
    // recreates the bag) — same trade-off as a CDX clear/rebuild.
    Page pg{};
    write_empty_dense_leaf_page(pg, adt_type_, fld_length_);  // count=0, lsib/rsib=INVALID
    cur_pg_   = root_page_;
    cur_page_ = pg;
    cur_cnt_  = 0;
    cur_idx_  = -1;
    cur_lsib_ = ADI_INVALID_PAGE;
    cur_rsib_ = ADI_INVALID_PAGE;
    cur_recno_   = 0;
    current_key_.clear();
    return write_adi_page_(root_page_, pg);
}

} // namespace openads::drivers::adi
