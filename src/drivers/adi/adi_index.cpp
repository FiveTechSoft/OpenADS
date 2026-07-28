#include "drivers/adi/adi_index.h"

#include <algorithm>
#include <cctype>
#include <cmath>
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
std::uint32_t dense_recno_from_buf(const std::uint8_t* base, std::uint32_t idx,
                                   std::uint32_t esz) noexcept {
    const std::uint8_t* e = base + idx * esz;
    if (esz >= 3)
        return static_cast<std::uint32_t>(e[0]) | (static_cast<std::uint32_t>(e[1]) << 8);
    return e[0];
}

platform::OpenMode map_open_mode(IndexOpenMode m) noexcept {
    if (m == IndexOpenMode::ReadOnly) return platform::OpenMode::ReadOnly;
    return platform::OpenMode::OpenExisting;
}

// Derive .adt path from .adi path
std::string adt_path_for(const std::string& adi_path) {
    namespace fs = std::filesystem;
    fs::path p(adi_path);
    p.replace_extension(".adt");
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

// One entry in the tag directory scan result.
struct TagEntry {
    std::vector<std::uint8_t> fnums;  // 1-based field numbers (≥1 element)
    std::uint32_t             root_pg;
    bool                      unique = false;  // bit 0 of byte[14] in per-tag header page
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

        // Per-tag header is at page xx. Byte[14] bit 0 = unique flag.
        bool uniq = false;
        auto hdr_pg = read_one_page(adi_f, static_cast<std::uint32_t>(xx));
        if (hdr_pg) uniq = (hdr_pg.value()[14] & 0x01u) != 0;

        tags.push_back({std::move(fnums), root_pg, uniq});
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

util::Result<void> AdiIndex::load_dense_leaf_(std::uint32_t page_no) {
    if (auto r = read_adi_page_(page_no, cur_page_); !r) return r;
    cur_pg_   = page_no;
    cur_cnt_  = page_count(cur_page_.data());
    cur_lsib_ = page_lsib(cur_page_.data());
    cur_rsib_ = page_rsib(cur_page_.data());
    cur_idx_  = -1;
    return {};
}

// ── AdiIndex::refresh_current_ ───────────────────────────────────────────────

util::Result<void> AdiIndex::refresh_current_() {
    if (cur_pg_ == ADI_INVALID_PAGE || cur_idx_ < 0 ||
        cur_idx_ >= static_cast<std::int32_t>(cur_cnt_)) {
        cur_recno_ = 0;
        current_key_.clear();
        return {};
    }
    cur_recno_ = dense_entry_recno(cur_page_.data(), cur_idx_, entry_size_);
    auto k = key_for_recno_(cur_recno_);
    if (!k) return k.error();
    current_key_ = std::move(k).value();
    return {};
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
            cur_page_ = pg;
            cur_pg_   = cur;
            cur_cnt_  = ct;
            cur_lsib_ = page_lsib(pg.data());
            cur_rsib_ = page_rsib(pg.data());
            cur_idx_  = 0;
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
            cur_page_ = pg;
            cur_pg_   = cur;
            cur_cnt_  = ct;
            cur_lsib_ = page_lsib(pg.data());
            cur_rsib_ = page_rsib(pg.data());
            cur_idx_  = static_cast<std::int32_t>(ct) - 1;
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
    bool unique)
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
    return apply_tag_(tag.fnums, tag.root_pg, types, offsets, lengths, names,
                      hlen, rlen, tag.unique);
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
        if (tag.fnums.empty()) continue;
        std::uint8_t fnum = tag.fnums[0];
        if (fnum == 0 || fnum > static_cast<unsigned>(fields.value().size())) continue;
        const auto& fd = fields.value()[fnum - 1];
        if (!name_eq(fd.name, field_name)) continue;
        return apply_tag_(tag.fnums, tag.root_pg, types, offsets, lengths, names,
                          hlen, rlen, tag.unique);
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
            int chosen = static_cast<int>(cnt) - 1;
            for (int i = 0; i < static_cast<int>(cnt); ++i) {
                const std::uint8_t* ek = pg.data() + ADI_TREE_ENTRY_START
                    + static_cast<std::uint32_t>(i) * branch_entry_sz_;
                const std::string fek = fold_for_compare_(
                    std::string(reinterpret_cast<const char*>(ek),
                                key_total_len_));
                if (std::memcmp(fnkey.data(), fek.data(),
                                key_total_len_) <= 0) {
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
        std::uint32_t rno = dense_entry_recno(cur_page_.data(), i, entry_size_);
        auto ck = key_for_recno_(rno);
        if (!ck) return ck.error();
        int cmp = compare_keys_(ck.value(), nkey);
        if (cmp > 0) {
            if (soft) {
                cur_idx_     = i;
                cur_recno_   = rno;
                current_key_ = std::move(ck).value();
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
            current_key_ = std::move(ck).value();
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
    const std::uint32_t max_ents =
        (ADI_PAGE_SIZE - ADI_DENSE_ENTRY_START) / entry_size_;

    // ── Binary-search for the insertion position ─────────────────────────────
    int pos;
    {
        int lo = 0, hi = static_cast<int>(leaf_cnt);
        while (lo < hi) {
            int mid = (lo + hi) / 2;
            std::uint32_t mrec = dense_entry_recno(pg.data(), mid, entry_size_);
            auto mk = key_for_recno_(mrec);
            if (!mk) return mk.error();
            int cmp = compare_keys_(mk.value(), ikey);
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
        auto left_max = key_for_recno_(dense_recno_from_buf(
            combo.data(), lft_cnt - 1, entry_size_));
        if (!left_max) return left_max.error();
        auto right_max = key_for_recno_(dense_recno_from_buf(
            combo.data(), total - 1, entry_size_));
        if (!right_max) return right_max.error();

        return promote_split_(path, left_pg,  left_max.value(),
                                    right_pg, right_max.value());
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

    auto left_max = key_for_recno_(dense_recno_from_buf(
        combo.data(), lft_cnt - 1, entry_size_));
    if (!left_max) return left_max.error();
    auto right_max = key_for_recno_(dense_recno_from_buf(
        combo.data(), total - 1, entry_size_));
    if (!right_max) return right_max.error();

    return promote_split_(path, cur,      left_max.value(),
                                right_pg, right_max.value());
}

// ── AdiIndex::erase ──────────────────────────────────────────────────────────

util::Result<void> AdiIndex::erase(std::uint32_t recno, const std::string& key) {
    if (mode_ == IndexOpenMode::ReadOnly)
        return util::Error{5000, 0, "ADI index is read-only", ""};

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

    // Seek to the correct dense leaf (soft=true: positions at or after key).
    auto sk = seek_key(ikey, /*soft=*/true);
    if (!sk) return sk.error();
    if (sk.value().hit == SeekHit::AfterEnd || !sk.value().positioned)
        return util::Error{5044, 0, "ADI: key not found for erase", ""};

    // Scan forward from the seek position to find the exact (key, recno) entry.
    for (;;) {
        if (cur_pg_ == ADI_INVALID_PAGE) break;
        for (int i = cur_idx_; i < static_cast<int>(cur_cnt_); ++i) {
            std::uint32_t erec = dense_entry_recno(cur_page_.data(), i, entry_size_);
            auto ek = key_for_recno_(erec);
            if (!ek) return ek.error();
            int cmp = compare_keys_(ek.value(), ikey);
            if (cmp > 0)
                return util::Error{5044, 0, "ADI: key not found for erase", ""};
            if (cmp == 0 && erec == recno) {
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
        }
        // Not on this leaf: advance to right sibling.
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
    auto fa = platform::File::open(adt_p, platform::OpenMode::ReadOnly);
    if (!fa) return fa.error();
    ix.adt_file_ = std::move(fa).value();

    std::vector<std::uint16_t> types(1, params.adt_type);
    std::vector<std::uint16_t> offsets(1, 0);
    std::vector<std::uint16_t> lengths(1, params.fld_length);
    std::vector<std::string>   names(1, params.field_name);

    std::uint32_t hlen = params.adt_hdr_len, rlen = params.adt_rec_len;
    auto fields = read_adt_fields(ix.adt_file_, hlen, rlen);
    if (!fields) return fields.error();

    types.clear();
    offsets.clear();
    lengths.clear();
    names.clear();
    for (const auto& fd : fields.value()) {
        types.push_back(fd.type);
        offsets.push_back(fd.offset);
        lengths.push_back(fd.length);
        names.push_back(fd.name);
    }

    std::vector<std::uint8_t> fnums{params.field_num};
    constexpr std::uint32_t kRootPage = 5;
    if (auto r = ix.apply_tag_(fnums, kRootPage, types, offsets, lengths, names,
                               params.adt_hdr_len, params.adt_rec_len,
                               params.unique);
        !r) {
        return r.error();
    }
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

    auto existing = list_tags(adi_path);
    if (!existing) return existing.error();
    for (const auto& tn : existing.value()) {
        if (tn.size() != params.field_name.size()) continue;
        bool eq = true;
        for (std::size_t i = 0; i < tn.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(tn[i])) !=
                std::tolower(static_cast<unsigned char>(params.field_name[i]))) {
                eq = false;
                break;
            }
        }
        if (eq) {
            return util::Error{5044, 0,
                "ADI already has a tag for field: " + params.field_name, ""};
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

    // Prepend tag-directory entry (legacy dual-tag layout).
    for (std::int32_t i = static_cast<std::int32_t>(count) - 1; i >= 0; --i) {
        std::size_t src = ADI_TAGDIR_ENTRY_START
                        + static_cast<std::size_t>(i) * ADI_TAGDIR_ENTRY_SIZE;
        std::size_t dst = src + ADI_TAGDIR_ENTRY_SIZE;
        std::memmove(pg2.data() + dst, pg2.data() + src, ADI_TAGDIR_ENTRY_SIZE);
    }
    // u32 LE: hdr_pg is a page number in a bag that is already several MB
    // by the time the second tag is added, so a one-byte store silently
    // lost it (see scan_tagdir). Entry bytes 1..3 were unused zeros, so
    // widening the field keeps every existing bag readable.
    set_u32_le( pg2.data() + ADI_TAGDIR_ENTRY_START, hdr_pg );
    if (!params.field_name.empty()) {
        pg2[ADI_TAGDIR_ENTRY_START + 5] =
            static_cast<std::uint8_t>(params.field_name[0]);
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
                               hlen, rlen, params.unique);
        !r) {
        return r.error();
    }
    return ix;
}

// ── AdiIndex::clear_data ─────────────────────────────────────────────────────

util::Result<void> AdiIndex::clear_data() {
    if (mode_ == IndexOpenMode::ReadOnly)
        return util::Error{5000, 0, "ADI index is read-only", ""};

    Page pg{};
    if (auto r = read_adi_page_(root_page_, pg); !r) return r;
    if (!is_dense_leaf(page_level(pg.data())))
        return util::Error{5000, 0, "ADI clear_data: root is not a dense leaf", ""};

    set_u16_le(pg.data() + 2, 0);
    cur_pg_   = root_page_;
    cur_page_ = pg;
    cur_cnt_  = 0;
    cur_idx_  = -1;
    cur_lsib_ = page_lsib(pg.data());
    cur_rsib_ = page_rsib(pg.data());
    cur_recno_   = 0;
    current_key_.clear();
    return write_adi_page_(root_page_, pg);
}

// ── ADI creation helpers ──────────────────────────────────────────────────────

namespace {

// Case-insensitive string comparison helper
bool ci_eq(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) return false;
    }
    return true;
}

// Split a comma-separated expression into individual trimmed column names.
std::vector<std::string> split_expr(const std::string& expr) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= expr.size(); ++i) {
        if (i == expr.size() || expr[i] == ',') {
            std::string s = expr.substr(start, i - start);
            // trim whitespace
            while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
                s.erase(s.begin());
            while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
                s.pop_back();
            if (!s.empty()) parts.push_back(std::move(s));
            start = i + 1;
        }
    }
    return parts;
}

// Write one 512-byte ADI page.
util::Result<void> write_page(platform::File& f, std::uint32_t page_no,
                               const AdiIndex::Page& pg) {
    auto r = f.write_at(static_cast<std::uint64_t>(page_no) * ADI_PAGE_SIZE,
                        pg.data(), pg.size());
    if (!r) return r.error();
    if (r.value() != ADI_PAGE_SIZE)
        return util::Error{5000, 0, "short ADI page write in create", ""};
    return {};
}

// Build the F-marker string for a list of 1-based field numbers.
std::string build_fmarker(const std::vector<std::uint8_t>& fnums) {
    std::string s;
    for (std::size_t i = 0; i < fnums.size(); ++i) {
        if (i > 0) s += ";F";
        else s += "F";
        s += std::to_string(fnums[i]);
    }
    return s;
}

// Write the 3 pages for one ADI tag (per-tag header, F-marker, empty root leaf)
// starting at page hdr_pg.  Returns nothing.
util::Result<void> write_tag_pages(platform::File& f,
                                   std::uint32_t hdr_pg,
                                   const std::vector<std::uint8_t>& fnums,
                                   bool unique,
                                   bool char_key) {
    // Per-tag header page
    AdiIndex::Page hdr{};
    hdr[14] = unique ? 0x01u : 0x00u;
    if (auto r = write_page(f, hdr_pg, hdr); !r) return r;

    // F-marker page
    AdiIndex::Page fmk{};
    std::string fm = build_fmarker(fnums);
    std::memcpy(fmk.data(), fm.data(), std::min(fm.size(), static_cast<std::size_t>(ADI_PAGE_SIZE - 1u)));
    if (auto r = write_page(f, hdr_pg + 1, fmk); !r) return r;

    // Empty root dense leaf
    AdiIndex::Page root{};
    std::uint16_t lv = char_key ? ADI_LVL_DENSE2 : ADI_LVL_DENSE;
    set_u16_le(root.data(),     lv);
    set_u16_le(root.data() + 2, 0);
    set_u32_le(root.data() + 4, ADI_INVALID_PAGE);
    set_u32_le(root.data() + 8, ADI_INVALID_PAGE);
    if (auto r = write_page(f, hdr_pg + 2, root); !r) return r;

    return {};
}

// Resolve expression (comma-separated column names) against ADT field list.
// Returns 1-based field numbers.
util::Result<std::vector<std::uint8_t>>
resolve_fnums(const std::vector<AdtFieldDesc>& fields,
              const std::string& expression) {
    auto names = split_expr(expression);
    if (names.empty())
        return util::Error{7200, 0, "empty index expression", expression};

    std::vector<std::uint8_t> fnums;
    for (const auto& name : names) {
        bool found = false;
        for (std::uint32_t i = 0; i < fields.size(); ++i) {
            if (ci_eq(fields[i].name, name)) {
                fnums.push_back(static_cast<std::uint8_t>(i + 1));
                found = true;
                break;
            }
        }
        if (!found)
            return util::Error{7200, 0, "column not found in ADT: " + name, expression};
    }
    return fnums;
}

} // anonymous namespace

// ── AdiIndex::create ─────────────────────────────────────────────────────────
// Creates a new .adi file with one tag.

// static
util::Result<AdiIndex>
AdiIndex::create(const std::string& adi_path,
                 const std::string& adt_path,
                 const std::string& expression,
                 bool               unique) {
    // Open ADT and read field descriptors
    auto fa = platform::File::open(adt_path, platform::OpenMode::ReadOnly);
    if (!fa) return fa.error();
    platform::File adt_f = std::move(fa).value();
    std::uint32_t hlen = 0, rlen = 0;
    auto fields_r = read_adt_fields(adt_f, hlen, rlen);
    if (!fields_r) return fields_r.error();
    const auto& fields = fields_r.value();

    // Resolve expression to field numbers
    auto fnums_r = resolve_fnums(fields, expression);
    if (!fnums_r) return fnums_r.error();
    const auto& fnums = fnums_r.value();

    // Determine key type from first field
    bool char_key = (fields[fnums[0] - 1].type == ADT_TYPE_CICHAR ||
                     fields[fnums[0] - 1].type == ADT_TYPE_CHAR);

    // Create new ADI file
    auto fi = platform::File::open(adi_path, platform::OpenMode::CreateRW);
    if (!fi) return fi.error();
    platform::File adi_f = std::move(fi).value();

    // Pages 0-1: zeros (file header placeholder)
    AdiIndex::Page zero{};
    if (auto r = write_page(adi_f, 0, zero); !r) return r.error();
    if (auto r = write_page(adi_f, 1, zero); !r) return r.error();

    // Page 2: tag directory — 1 tag, xx=3 (per-tag header at page 3)
    AdiIndex::Page tagdir{};
    set_u16_le(tagdir.data(),     ADI_LVL_TAGDIR);   // level = 3
    set_u16_le(tagdir.data() + 2, 1);                 // count = 1
    set_u32_le(tagdir.data() + 4, ADI_INVALID_PAGE);  // lsib
    set_u32_le(tagdir.data() + 8, ADI_INVALID_PAGE);  // rsib
    tagdir[ADI_TAGDIR_ENTRY_START] = 3;               // xx = 3 → hdr at pg 3
    if (auto r = write_page(adi_f, 2, tagdir); !r) return r.error();

    // Pages 3-5: per-tag header, F-marker, empty root leaf
    if (auto r = write_tag_pages(adi_f, 3, fnums, unique, char_key); !r)
        return r.error();

    if (auto s = adi_f.sync(); !s) return s.error();

    // Build and return the AdiIndex
    AdiIndex idx;
    idx.mode_     = IndexOpenMode::Shared;
    idx.adi_file_ = std::move(adi_f);
    idx.adt_file_ = std::move(adt_f);
    idx.adi_path_ = adi_path;

    std::vector<std::uint16_t> types, offsets, lengths;
    std::vector<std::string>   names;
    for (const auto& fd : fields) {
        types.push_back(fd.type);
        offsets.push_back(fd.offset);
        lengths.push_back(fd.length);
        names.push_back(fd.name);
    }
    if (auto r = idx.apply_tag_(fnums, 5, types, offsets, lengths, names,
                                 hlen, rlen, unique); !r)
        return r.error();

    return idx;
}

// ── AdiIndex::add_tag ────────────────────────────────────────────────────────
// Adds a new tag to an existing .adi file.

// static
util::Result<AdiIndex>
AdiIndex::add_tag(const std::string& adi_path,
                  const std::string& adt_path,
                  const std::string& expression,
                  bool               unique) {
    // Open ADT and read field descriptors
    auto fa = platform::File::open(adt_path, platform::OpenMode::ReadOnly);
    if (!fa) return fa.error();
    platform::File adt_f = std::move(fa).value();
    std::uint32_t hlen = 0, rlen = 0;
    auto fields_r = read_adt_fields(adt_f, hlen, rlen);
    if (!fields_r) return fields_r.error();
    const auto& fields = fields_r.value();

    // Resolve expression to field numbers
    auto fnums_r = resolve_fnums(fields, expression);
    if (!fnums_r) return fnums_r.error();
    const auto& fnums = fnums_r.value();

    bool char_key = (fields[fnums[0] - 1].type == ADT_TYPE_CICHAR ||
                     fields[fnums[0] - 1].type == ADT_TYPE_CHAR);

    // Open existing ADI file for read+write
    auto fi = platform::File::open(adi_path, platform::OpenMode::OpenExisting);
    if (!fi) return fi.error();
    platform::File adi_f = std::move(fi).value();

    // Read tag directory (page 2) to find current tag count
    AdiIndex::Page tagdir{};
    {
        auto got = adi_f.read_at(2 * ADI_PAGE_SIZE, tagdir.data(), tagdir.size());
        if (!got || got.value() < ADI_PAGE_SIZE)
            return util::Error{6106, 0, "can't read ADI tag directory for add_tag", adi_path};
    }
    std::uint16_t cur_count = u16_le(tagdir.data() + 2);

    // Each tag uses 3 pages (header, fmarker, root).  After 6 pages of
    // prefix (pages 0-2 = 3 header + 3 for first tag), subsequent tags
    // start at page 3 + cur_count * 3.
    std::uint32_t new_hdr_pg = 3u + static_cast<std::uint32_t>(cur_count) * 3u;
    std::uint32_t new_root_pg = new_hdr_pg + 2u;
    if (new_hdr_pg > 255u)
        return util::Error{7200, 0, "ADI tag count exceeds capacity", adi_path};

    // Write new tag pages at end of file
    if (auto r = write_tag_pages(adi_f, new_hdr_pg, fnums, unique, char_key); !r)
        return r.error();

    // Update tag directory: increment count, add entry
    std::size_t entry_off = ADI_TAGDIR_ENTRY_START
                          + static_cast<std::size_t>(cur_count) * ADI_TAGDIR_ENTRY_SIZE;
    if (entry_off + 1 < ADI_PAGE_SIZE) {
        tagdir[entry_off] = static_cast<std::uint8_t>(new_hdr_pg);
    }
    set_u16_le(tagdir.data() + 2, cur_count + 1);
    {
        auto w = adi_f.write_at(2 * ADI_PAGE_SIZE, tagdir.data(), tagdir.size());
        if (!w) return w.error();
    }

    if (auto s = adi_f.sync(); !s) return s.error();

    // Build and return the AdiIndex
    AdiIndex idx;
    idx.mode_     = IndexOpenMode::Shared;
    idx.adi_file_ = std::move(adi_f);
    idx.adt_file_ = std::move(adt_f);
    idx.adi_path_ = adi_path;

    std::vector<std::uint16_t> types, offsets, lengths;
    std::vector<std::string>   names;
    for (const auto& fd : fields) {
        types.push_back(fd.type);
        offsets.push_back(fd.offset);
        lengths.push_back(fd.length);
        names.push_back(fd.name);
    }
    if (auto r = idx.apply_tag_(fnums, new_root_pg, types, offsets, lengths, names,
                                 hlen, rlen, unique); !r)
        return r.error();

    return idx;
}

} // namespace openads::drivers::adi
