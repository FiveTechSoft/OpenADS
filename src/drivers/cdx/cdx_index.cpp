#include "drivers/cdx/cdx_index.h"

#include <algorithm>
#include <cstring>
#include <mutex>
#include <unordered_map>

namespace openads::drivers::cdx {

namespace {

std::mutex g_cdx_alloc_mu;
std::unordered_map<std::string, std::uint64_t> g_cdx_alloc_tail;

std::uint16_t read_u16_le(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0]) |
           static_cast<std::uint16_t>(p[1] << 8);
}

void write_u16_le(std::uint8_t* p, std::uint16_t v) {
    p[0] = static_cast<std::uint8_t>( v       & 0xFFu);
    p[1] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
}

std::uint32_t read_u32_le(const std::uint8_t* p) {
    return  static_cast<std::uint32_t>(p[0])        |
           (static_cast<std::uint32_t>(p[1]) <<  8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

void write_u32_le(std::uint8_t* p, std::uint32_t v) {
    p[0] = static_cast<std::uint8_t>( v        & 0xFFu);
    p[1] = static_cast<std::uint8_t>((v >>  8) & 0xFFu);
    p[2] = static_cast<std::uint8_t>((v >> 16) & 0xFFu);
    p[3] = static_cast<std::uint8_t>((v >> 24) & 0xFFu);
}

std::string trim_nul(const std::uint8_t* p, std::size_t n) {
    std::size_t len = 0;
    while (len < n && p[len] != 0) ++len;
    return std::string(reinterpret_cast<const char*>(p), len);
}

platform::OpenMode map_mode(IndexOpenMode m) {
    switch (m) {
        case IndexOpenMode::ReadOnly:  return platform::OpenMode::ReadOnly;
        case IndexOpenMode::Shared:    return platform::OpenMode::OpenExisting;
        case IndexOpenMode::Exclusive: return platform::OpenMode::OpenExisting;
    }
    return platform::OpenMode::ReadOnly;
}

std::uint8_t bits_for(std::uint32_t n) {
    std::uint8_t b = 0;
    while (n) { ++b; n >>= 1; }
    return b == 0 ? 1 : b;
}

// FoxPro CDX leaf bit layout, mirrors Harbour hb_cdxPageLeafInitSpace
// (src/rdd/dbfcdx/dbfcdx1.c lines 1927-1941). bBits is derived from
// the key length; ReqByte / RNBits then fall out.
struct LeafLayout {
    std::uint8_t  req_byte;   // bytes per packed entry
    std::uint8_t  rn_bits;    // bits for record number
    std::uint8_t  dc_bits;    // bits for duplicate count (= bBits)
    std::uint8_t  tc_bits;    // bits for trailing count  (= bBits)
    std::uint32_t rn_mask;
    std::uint32_t dc_mask;
    std::uint32_t tc_mask;
};

LeafLayout compute_layout(std::uint16_t key_len,
                          std::uint8_t  req_byte_override = 0) {
    LeafLayout out{};
    std::uint16_t v = key_len;
    std::uint8_t  b_bits = 0;
    while (v) { ++b_bits; v >>= 1; }
    out.dc_bits  = b_bits;
    out.tc_bits  = b_bits;
    out.req_byte = req_byte_override
        ? req_byte_override
        : ((b_bits > 12) ? 5 : (b_bits > 8 ? 4 : 3));
    out.rn_bits  = static_cast<std::uint8_t>(
        (out.req_byte << 3) - (b_bits << 1));
    out.dc_mask  = (b_bits >= 32) ? 0xFFFFFFFFu : ((1u << b_bits) - 1);
    out.tc_mask  = out.dc_mask;
    out.rn_mask  = (out.rn_bits >= 32) ? 0xFFFFFFFFu
                                       : ((1u << out.rn_bits) - 1);
    return out;
}

// Static compact-leaf encode/decode helpers parameterised by key_size,
// so the structure tag (key_size = 10) and sub-tag data leaves share
// one implementation.
util::Result<void>
encode_compact_leaf_static(
    CdxIndex::Page& p,
    std::uint16_t   key_size,
    const std::vector<std::pair<std::string, std::uint32_t>>& keys,
    std::uint32_t   left_sib,
    std::uint32_t   right_sib,
    std::uint8_t    req_byte_override = 0)
{
    LeafLayout L = compute_layout(key_size, req_byte_override);
    const std::uint32_t rec_mask = L.rn_mask;
    const std::uint8_t  rec_bits = L.rn_bits;
    const std::uint8_t  dup_bits = L.dc_bits;
    const std::uint8_t  trl_bits = L.tc_bits;
    const std::uint32_t dup_mask = L.dc_mask;
    const std::uint32_t trl_mask = L.tc_mask;
    const std::uint8_t  key_bytes = L.req_byte;

    std::fill(p.begin(), p.end(), std::uint8_t{0});
    write_u16_le(p.data() + 0, CDX_NODE_LEAF);
    write_u16_le(p.data() + 2, static_cast<std::uint16_t>(keys.size()));
    write_u32_le(p.data() + 4,  left_sib);
    write_u32_le(p.data() + 8,  right_sib);
    write_u32_le(p.data() + 14, rec_mask);
    p[18] = static_cast<std::uint8_t>(dup_mask & 0xFFu);
    p[19] = static_cast<std::uint8_t>(trl_mask & 0xFFu);
    p[20] = rec_bits;
    p[21] = dup_bits;
    p[22] = trl_bits;
    p[23] = key_bytes;

    std::uint32_t buf_pos = CDX_PAGE_LEN - CDX_EXT_HEADSIZE;
    std::string prev(key_size, ' ');

    for (std::size_t i = 0; i < keys.size(); ++i) {
        const auto& [key, recno] = keys[i];
        std::string padded = key;
        if (padded.size() < key_size) padded.append(key_size - padded.size(), ' ');
        if (padded.size() > key_size) padded.resize(key_size);

        std::uint32_t dup = 0;
        if (i > 0) {
            while (dup < key_size && padded[dup] == prev[dup]) ++dup;
        }
        std::uint32_t trl = 0;
        while (trl < key_size - dup && padded[key_size - 1 - trl] == ' ') ++trl;
        std::uint32_t suffix_len = key_size - dup - trl;

        if (buf_pos < suffix_len) {
            return util::Error{5000, 0, "CDX leaf encode: page full", ""};
        }
        buf_pos -= suffix_len;
        if (suffix_len > 0) {
            std::memcpy(p.data() + CDX_EXT_HEADSIZE + buf_pos,
                        padded.data() + dup, suffix_len);
        }

        std::uint8_t* entry = p.data() + CDX_EXT_HEADSIZE + i * key_bytes;
        if (entry + key_bytes > p.data() + CDX_PAGE_LEN) {
            return util::Error{5000, 0, "CDX leaf entry overflow", ""};
        }
        // Entry headers grow right; suffix area grows left. They MUST
        // NOT overlap — without this check the encoder silently
        // corrupts the page once `(i+1)*key_bytes > buf_pos`, which
        // surfaces as 6106 dup/trl-out-of-range on the next decode.
        if (static_cast<std::uint32_t>((i + 1) * key_bytes) > buf_pos) {
            return util::Error{5000, 0, "CDX leaf encode: page full", ""};
        }

        // Pack so decoder reads trl from the low `trl_bits` and dup from
        // the next `dup_bits` (matches the decoder layout in
        // decode_compact_leaf_static). Earlier code inverted these and
        // round-tripped only when both dup and trl were zero.
        std::uint32_t bits = ((dup & dup_mask) << trl_bits) | (trl & trl_mask);
        const int top_bytes = (trl_bits + dup_bits + 7) >> 3;
        const int from_byte = key_bytes - top_bytes;
        bits <<= ((top_bytes << 3) - trl_bits - dup_bits);

        std::uint32_t rec = recno & rec_mask;
        for (int byte_i = 0; byte_i < key_bytes; ++byte_i) {
            std::uint8_t b = static_cast<std::uint8_t>(rec & 0xFFu);
            rec >>= 8;
            if (byte_i >= from_byte) {
                b = static_cast<std::uint8_t>(b | (bits & 0xFFu));
                bits >>= 8;
            }
            entry[byte_i] = b;
        }

        prev = padded;
    }

    std::uint32_t entries_end = CDX_EXT_HEADSIZE +
        static_cast<std::uint32_t>(keys.size()) * key_bytes;
    std::uint32_t suffix_start = CDX_EXT_HEADSIZE + buf_pos;
    std::uint32_t free_spc = (suffix_start > entries_end)
        ? (suffix_start - entries_end) : 0;
    write_u16_le(p.data() + 12, static_cast<std::uint16_t>(free_spc));
    return {};
}

util::Result<std::vector<std::pair<std::string, std::uint32_t>>>
decode_compact_leaf_static(const CdxIndex::Page& p, std::uint16_t key_size) {
    std::uint16_t attr = read_u16_le(p.data() + 0);
    if (!(attr & CDX_NODE_LEAF)) {
        return util::Error{6106, 0, "decode_leaf_ on non-leaf page", ""};
    }
    std::uint16_t nkeys     = read_u16_le(p.data() + 2);
    std::uint32_t rec_mask  = read_u32_le(p.data() + 14);
    std::uint8_t  dup_mask  = p[18];
    std::uint8_t  trl_mask  = p[19];
    std::uint8_t  dup_bits  = p[21];
    std::uint8_t  trl_bits  = p[22];
    std::uint8_t  key_bytes = p[23];
    (void)p[20];

    std::vector<std::pair<std::string, std::uint32_t>> out;
    out.reserve(nkeys);

    std::uint32_t buf_pos = CDX_PAGE_LEN - CDX_EXT_HEADSIZE;
    std::string   prev(key_size, ' ');

    for (std::uint16_t i = 0; i < nkeys; ++i) {
        const std::uint8_t* entry = p.data() + CDX_EXT_HEADSIZE + i * key_bytes;
        if (entry + key_bytes > p.data() + CDX_PAGE_LEN) {
            return util::Error{6106, 0, "CDX leaf entry overruns page", ""};
        }

        std::uint32_t recno = read_u32_le(entry) & rec_mask;
        std::uint8_t  shift = static_cast<std::uint8_t>(
            32 - dup_bits - trl_bits);
        std::uint32_t tmp = read_u32_le(entry + key_bytes - 4) >> shift;
        std::uint32_t trl = tmp & trl_mask;
        std::uint32_t dup = (tmp >> trl_bits) & dup_mask;

        if (dup > key_size || trl > key_size || dup + trl > key_size) {
            return util::Error{6106, 0, "CDX dup/trl out of range", ""};
        }
        std::uint32_t suffix_len = key_size - dup - trl;
        if (buf_pos < suffix_len) {
            return util::Error{6106, 0, "CDX suffix area underrun", ""};
        }
        buf_pos -= suffix_len;

        std::string key = prev;
        if (suffix_len > 0) {
            std::memcpy(key.data() + dup,
                        p.data() + CDX_EXT_HEADSIZE + buf_pos,
                        suffix_len);
        }
        for (std::uint32_t t = key_size - trl; t < key_size; ++t) {
            key[t] = ' ';
        }
        prev = key;
        out.emplace_back(key, recno);
    }
    return out;
}

// Branch (interior) node helpers. Layout per FoxPro CDX, mirroring
// what seek_first()/seek_key()'s descent code already reads:
//   header (12 bytes): attr (u16 LE) | nkeys (u16 LE) | left_sib (u32 LE) |
//                      right_sib (u32 LE)
//   each entry (key_size + 8 bytes): key | recno (u32 BE) | child (u32 BE)
struct BranchEntry {
    std::string   key;
    std::uint32_t recno = 0;
    std::uint32_t child = 0;
};

inline std::size_t branch_capacity(std::uint16_t key_size) {
    return (CDX_PAGE_LEN - CDX_INT_HEADSIZE) /
           (static_cast<std::size_t>(key_size) + 8);
}

util::Result<void>
encode_branch_static(CdxIndex::Page& p,
                     std::uint16_t   key_size,
                     const std::vector<BranchEntry>& entries,
                     std::uint32_t   left_sib,
                     std::uint32_t   right_sib)
{
    if (entries.size() > branch_capacity(key_size)) {
        return util::Error{5000, 0, "CDX branch encode: page full", ""};
    }
    std::fill(p.begin(), p.end(), std::uint8_t{0});
    write_u16_le(p.data() + 0, CDX_NODE_BRANCH);
    write_u16_le(p.data() + 2, static_cast<std::uint16_t>(entries.size()));
    write_u32_le(p.data() + 4, left_sib);
    write_u32_le(p.data() + 8, right_sib);

    const std::size_t entry_size = static_cast<std::size_t>(key_size) + 8;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        std::uint8_t* e = p.data() + CDX_INT_HEADSIZE + i * entry_size;
        std::string padded = entries[i].key;
        if (padded.size() < key_size)
            padded.append(key_size - padded.size(), ' ');
        if (padded.size() > key_size)
            padded.resize(key_size);
        std::memcpy(e, padded.data(), key_size);
        std::uint32_t r = entries[i].recno;
        e[key_size + 0] = static_cast<std::uint8_t>((r >> 24) & 0xFFu);
        e[key_size + 1] = static_cast<std::uint8_t>((r >> 16) & 0xFFu);
        e[key_size + 2] = static_cast<std::uint8_t>((r >>  8) & 0xFFu);
        e[key_size + 3] = static_cast<std::uint8_t>( r        & 0xFFu);
        std::uint32_t c = entries[i].child;
        e[key_size + 4] = static_cast<std::uint8_t>((c >> 24) & 0xFFu);
        e[key_size + 5] = static_cast<std::uint8_t>((c >> 16) & 0xFFu);
        e[key_size + 6] = static_cast<std::uint8_t>((c >>  8) & 0xFFu);
        e[key_size + 7] = static_cast<std::uint8_t>( c        & 0xFFu);
    }
    return {};
}

util::Result<std::vector<BranchEntry>>
decode_branch_static(const CdxIndex::Page& p, std::uint16_t key_size)
{
    std::uint16_t attr = read_u16_le(p.data() + 0);
    if (attr & CDX_NODE_LEAF) {
        return util::Error{6106, 0, "decode_branch_ on leaf page", ""};
    }
    std::uint16_t nkeys = read_u16_le(p.data() + 2);
    const std::size_t entry_size = static_cast<std::size_t>(key_size) + 8;
    if (CDX_INT_HEADSIZE + nkeys * entry_size > CDX_PAGE_LEN) {
        return util::Error{6106, 0, "CDX branch nkeys out of range", ""};
    }
    std::vector<BranchEntry> out;
    out.reserve(nkeys);
    for (std::uint16_t i = 0; i < nkeys; ++i) {
        const std::uint8_t* e = p.data() + CDX_INT_HEADSIZE + i * entry_size;
        BranchEntry be;
        be.key.assign(reinterpret_cast<const char*>(e), key_size);
        be.recno =
            (static_cast<std::uint32_t>(e[key_size + 0]) << 24) |
            (static_cast<std::uint32_t>(e[key_size + 1]) << 16) |
            (static_cast<std::uint32_t>(e[key_size + 2]) <<  8) |
             static_cast<std::uint32_t>(e[key_size + 3]);
        be.child =
            (static_cast<std::uint32_t>(e[key_size + 4]) << 24) |
            (static_cast<std::uint32_t>(e[key_size + 5]) << 16) |
            (static_cast<std::uint32_t>(e[key_size + 6]) <<  8) |
             static_cast<std::uint32_t>(e[key_size + 7]);
        out.push_back(std::move(be));
    }
    return out;
}

// Build a fresh sub-tag CDXTAGHEADER (1024 bytes). Mirrors the file
// header layout but is stored at the sub-tag's offset, not at 0.
std::array<std::uint8_t, CDX_HEADER_LEN>
build_subtag_header(std::uint32_t root_page,
                    std::uint16_t key_size,
                    const std::string& key_expr,
                    const std::string& tag_name,
                    bool unique,
                    bool descend)
{
    std::array<std::uint8_t, CDX_HEADER_LEN> hdr{};
    write_u32_le(hdr.data() + 0, root_page);
    write_u32_le(hdr.data() + 4, 0xFFFFFFFFu);
    write_u32_le(hdr.data() + 8, 1);
    write_u16_le(hdr.data() + 12, key_size);
    hdr[14] = static_cast<std::uint8_t>(
        (unique ? 0x01 : 0x00) | 0x20);
    hdr[15] = 0x01;
    write_u16_le(hdr.data() + 16, CDX_HEADER_LEN);
    write_u16_le(hdr.data() + 18, CDX_PAGE_LEN);
    write_u16_le(hdr.data() + 502, descend ? 1 : 0);
    write_u16_le(hdr.data() + 508, 512);
    write_u16_le(hdr.data() + 510,
                 static_cast<std::uint16_t>(key_expr.size()));
    std::memcpy(hdr.data() + 512, key_expr.data(),
                std::min<std::size_t>(key_expr.size(), 511));
    // The sub-tag's name lives in the structure tag's leaf; we also
    // mirror it into reserved2 of the sub-tag header for diagnostics
    // and for legacy single-tag readers.
    std::memcpy(hdr.data() + 24, tag_name.data(),
                std::min<std::size_t>(tag_name.size(), 11));
    return hdr;
}

} // namespace

util::Result<CdxIndex::Page*> CdxIndex::get_page_(std::uint32_t offset) {
    auto it = page_cache_.find(offset);
    if (it != page_cache_.end()) return &it->second;
    Page p{};
    auto got = file_.read_at(offset, p.data(), p.size());
    if (!got) return got.error();
    if (got.value() < p.size()) {
        return util::Error{6106, 0, "short read on CDX page", ""};
    }
    auto [ins, _] = page_cache_.emplace(offset, p);
    return &ins->second;
}

util::Result<void> CdxIndex::flush_page_(std::uint32_t offset) {
    auto it = page_cache_.find(offset);
    if (it == page_cache_.end()) return {};
    auto wrote = file_.write_at(offset, it->second.data(), it->second.size());
    if (!wrote) return wrote.error();
    if (wrote.value() != it->second.size()) {
        return util::Error{5000, 0, "short write on CDX page", ""};
    }
    dirty_[offset] = false;
    return {};
}

util::Result<void>
CdxIndex::open(const std::string& path, IndexOpenMode mode) {
    return open_named(path, mode, "");
}

util::Result<void>
CdxIndex::open_named(const std::string& path,
                     IndexOpenMode      mode,
                     const std::string& tag_name) {
    mode_  = mode;
    path_  = path;
    auto fres = platform::File::open(path, map_mode(mode));
    if (!fres) return fres.error();
    file_ = std::move(fres).value();
    auto sz = file_.size();
    if (!sz) return sz.error();
    file_size_ = sz.value();

    // 1) File header (offset 0, 1024 bytes) = structure tag CDXTAGHEADER.
    std::array<std::uint8_t, CDX_HEADER_LEN> file_hdr{};
    auto got = file_.read_at(0, file_hdr.data(), file_hdr.size());
    if (!got) return got.error();
    if (got.value() < CDX_HEADER_LEN) {
        return util::Error{6106, 0, "CDX header truncated", path};
    }
    std::uint32_t struct_root = read_u32_le(file_hdr.data() + 0);
    if (struct_root == 0) {
        return util::Error{6106, 0, "CDX has no structure tag root", path};
    }

    // 2) Structure-tag root leaf at struct_root, key_size = 10.
    Page struct_leaf{};
    auto got2 = file_.read_at(struct_root, struct_leaf.data(), struct_leaf.size());
    if (!got2) return got2.error();
    if (got2.value() < CDX_PAGE_LEN) {
        return util::Error{6106, 0, "CDX struct-tag leaf truncated", path};
    }
    auto sdec = decode_compact_leaf_static(struct_leaf, CDX_STRUCT_KEY_LEN);
    if (!sdec) return sdec.error();
    auto& sentries = sdec.value();
    if (sentries.empty()) {
        return util::Error{6106, 0, "CDX structure tag has no entries", path};
    }

    auto trim_tag = [](const std::string& s) {
        std::string t = s;
        while (!t.empty() && t.back() == ' ') t.pop_back();
        return t;
    };

    const std::pair<std::string, std::uint32_t>* picked = nullptr;
    if (tag_name.empty()) {
        picked = &sentries.front();
    } else {
        for (auto& e : sentries) {
            if (trim_tag(e.first) == tag_name) { picked = &e; break; }
        }
        if (!picked) {
            return util::Error{5044, 0,
                "CDX has no sub-tag with that name", tag_name};
        }
    }
    sub_header_offset_ = picked->second;
    tag_name_          = trim_tag(picked->first);

    // 3) Sub-tag CDXTAGHEADER (1024B) at sub_header_offset_.
    std::array<std::uint8_t, CDX_HEADER_LEN> sub_hdr{};
    auto got3 = file_.read_at(sub_header_offset_,
                              sub_hdr.data(), sub_hdr.size());
    if (!got3) return got3.error();
    if (got3.value() < CDX_HEADER_LEN) {
        return util::Error{6106, 0, "CDX sub-tag header truncated", path};
    }

    root_page_ = read_u32_le(sub_hdr.data() + 0);
    free_ptr_  = read_u32_le(sub_hdr.data() + 4);
    counter_   = read_u32_le(sub_hdr.data() + 8);
    key_size_  = read_u16_le(sub_hdr.data() + 12);
    index_opt_ = sub_hdr[14];
    index_sig_ = sub_hdr[15];
    unique_    = (index_opt_ & 0x01) != 0;
    descend_   = read_u16_le(sub_hdr.data() + 502) != 0;

    std::uint16_t kep_pos = read_u16_le(sub_hdr.data() + 508);
    std::uint16_t kep_len = read_u16_le(sub_hdr.data() + 510);
    if (kep_pos >= 512 && kep_pos + kep_len <= CDX_HEADER_LEN) {
        key_expr_.assign(reinterpret_cast<const char*>(sub_hdr.data() + kep_pos),
                         kep_len);
    } else {
        key_expr_ = trim_nul(sub_hdr.data() + 512, 256);
    }

    return {};
}

util::Result<std::vector<std::pair<std::string, std::uint32_t>>>
CdxIndex::decode_leaf_(std::uint32_t page_off) {
    auto page = get_page_(page_off);
    if (!page) return page.error();
    return decode_compact_leaf_static(*page.value(), key_size_);
}

util::Result<void>
CdxIndex::encode_leaf_(std::uint32_t page_off,
                       const std::vector<std::pair<std::string, std::uint32_t>>& keys,
                       std::uint32_t left_sib,
                       std::uint32_t right_sib) {
    auto page = get_page_(page_off);
    if (!page) return page.error();
    auto r = encode_compact_leaf_static(*page.value(), key_size_, keys,
                                        left_sib, right_sib);
    if (!r) return r.error();
    dirty_[page_off] = true;
    return {};
}

util::Result<SeekOutcome> CdxIndex::seek_first() {
    cur_leaf_   = 0;
    cur_index_  = -1;
    cur_decoded_.clear();
    if (root_page_ == 0) return SeekOutcome{SeekHit::AfterEnd, 0, false};

    auto page = get_page_(root_page_);
    if (!page) return page.error();
    std::uint16_t attr = read_u16_le(page.value()->data());
    if (attr & CDX_NODE_LEAF) {
        cur_leaf_ = root_page_;
    } else {
        std::uint32_t cur = root_page_;
        while (true) {
            auto pg = get_page_(cur);
            if (!pg) return pg.error();
            std::uint16_t at = read_u16_le(pg.value()->data());
            if (at & CDX_NODE_LEAF) { cur_leaf_ = cur; break; }
            std::uint8_t* base = pg.value()->data();
            std::uint16_t nkeys = read_u16_le(base + 2);
            if (nkeys == 0) return SeekOutcome{SeekHit::AfterEnd, 0, false};
            const std::uint8_t* child_ptr =
                base + CDX_INT_HEADSIZE + key_size_ + 4;
            cur =  (static_cast<std::uint32_t>(child_ptr[0]) << 24) |
                   (static_cast<std::uint32_t>(child_ptr[1]) << 16) |
                   (static_cast<std::uint32_t>(child_ptr[2]) <<  8) |
                    static_cast<std::uint32_t>(child_ptr[3]);
        }
    }

    auto dec = decode_leaf_(cur_leaf_);
    if (!dec) return dec.error();
    cur_decoded_ = std::move(dec).value();
    if (cur_decoded_.empty()) {
        return SeekOutcome{SeekHit::AfterEnd, 0, false};
    }
    cur_index_ = 0;
    cur_state_ = CurState::Positioned;
    current_key_ = cur_decoded_[0].first;
    return SeekOutcome{SeekHit::Exact, cur_decoded_[0].second, true};
}

util::Result<SeekOutcome> CdxIndex::seek_last() {
    if (root_page_ == 0) return SeekOutcome{SeekHit::AfterEnd, 0, false};
    cur_leaf_ = 0; cur_index_ = -1; cur_decoded_.clear();

    auto first = seek_first();
    if (!first) return first.error();
    if (!first.value().positioned) return first;

    while (true) {
        auto pg = get_page_(cur_leaf_);
        if (!pg) return pg.error();
        std::uint32_t right = read_u32_le(pg.value()->data() + 8);
        if (right == 0xFFFFFFFFu || right == 0) break;
        cur_leaf_ = right;
        auto dec = decode_leaf_(cur_leaf_);
        if (!dec) return dec.error();
        cur_decoded_ = std::move(dec).value();
        if (cur_decoded_.empty()) break;
    }
    if (cur_decoded_.empty()) {
        return SeekOutcome{SeekHit::AfterEnd, 0, false};
    }
    cur_index_ = static_cast<std::int32_t>(cur_decoded_.size() - 1);
    cur_state_ = CurState::Positioned;
    current_key_ = cur_decoded_[static_cast<std::size_t>(cur_index_)].first;
    return SeekOutcome{SeekHit::Exact, cur_decoded_[static_cast<std::size_t>(cur_index_)].second, true};
}

util::Result<SeekOutcome>
CdxIndex::seek_key(const std::string& key, bool soft) {
    if (root_page_ == 0) return SeekOutcome{SeekHit::AfterEnd, 0, false};
    std::string padded = key;
    if (padded.size() < key_size_) padded.append(key_size_ - padded.size(), ' ');
    if (padded.size() > key_size_) padded.resize(key_size_);

    auto first = seek_first();
    if (!first) return first.error();
    if (!first.value().positioned) {
        return SeekOutcome{SeekHit::AfterEnd, 0, false};
    }

    while (true) {
        for (std::size_t i = 0; i < cur_decoded_.size(); ++i) {
            int cmp = std::memcmp(padded.data(), cur_decoded_[i].first.data(),
                                  key_size_);
            if (cmp == 0) {
                cur_index_ = static_cast<std::int32_t>(i);
                cur_state_ = CurState::Positioned;
                current_key_ = cur_decoded_[i].first;
                return SeekOutcome{SeekHit::Exact, cur_decoded_[i].second, true};
            }
            if (cmp < 0) {
                if (!soft) {
                    // Hard seek miss with key strictly less than
                    // cur_decoded_[i]: park cursor "between"
                    // cur_decoded_[i-1] and cur_decoded_[i]. The
                    // Between state lets next() advance onto idx i
                    // and prev() retreat onto idx i-1, matching
                    // Clipper SKIP after a failed hard seek.
                    if (i == 0) {
                        cur_state_ = CurState::BeforeBegin;
                        cur_index_ = -1;
                    } else {
                        cur_index_ = static_cast<std::int32_t>(i);
                        cur_state_ = CurState::Between;
                    }
                    return SeekOutcome{SeekHit::AfterEnd, 0, false};
                }
                cur_index_ = static_cast<std::int32_t>(i);
                cur_state_ = CurState::Positioned;
                current_key_ = cur_decoded_[i].first;
                return SeekOutcome{SeekHit::AfterKey, cur_decoded_[i].second, true};
            }
        }
        auto pg = get_page_(cur_leaf_);
        if (!pg) return pg.error();
        std::uint32_t right = read_u32_le(pg.value()->data() + 8);
        if (right == 0xFFFFFFFFu || right == 0) break;
        cur_leaf_ = right;
        auto dec = decode_leaf_(cur_leaf_);
        if (!dec) return dec.error();
        cur_decoded_ = std::move(dec).value();
        if (cur_decoded_.empty()) break;
    }
    // Search key was strictly greater than every key in the tree.
    // For SOFT seeks: park at AfterEnd (real past-end behaviour).
    // For HARD seeks (eg. goto_record's index resync on a recno
    // whose key is past every indexed key): use AfterEndKey so
    // SKIP(+1) wraps to the first entry and SKIP(-1) resumes from
    // the last entry. Mirrors Clipper / DBFCDX SKIP-from-out-of-
    // range-recno semantics.
    cur_state_ = soft ? CurState::AfterEnd : CurState::AfterEndKey;
    cur_index_ = -1;
    return SeekOutcome{SeekHit::AfterEnd, 0, false};
}

util::Result<SeekOutcome> CdxIndex::next() {
    // From BeforeBegin: clamp back to the first leaf's first key
    // so a Clipper-style SKIP(>0) after the cursor ran off the
    // front (prev() loop) resumes scanning forward.
    if (cur_state_ == CurState::BeforeBegin) {
        if (!cur_decoded_.empty()) {
            cur_index_ = 0;
            cur_state_ = CurState::Positioned;
            current_key_ = cur_decoded_[0].first;
            return SeekOutcome{SeekHit::Exact,
                cur_decoded_[0].second, true};
        }
        return seek_first();
    }
    // AfterEndKey: hard-seek miss past every key. next() wraps
    // around to the first entry (Clipper SKIP(+1)-from-out-of-
    // range-recno). After wrap, transitions to Positioned.
    if (cur_state_ == CurState::AfterEndKey) {
        return seek_first();
    }
    // Between(cur_index): next() returns the entry AT cur_index
    // (the first key strictly greater than the search key that
    // produced this state) and transitions to Positioned.
    if (cur_state_ == CurState::Between) {
        if (cur_index_ >= 0 &&
            static_cast<std::size_t>(cur_index_) < cur_decoded_.size()) {
            cur_state_ = CurState::Positioned;
            current_key_ = cur_decoded_[
                static_cast<std::size_t>(cur_index_)].first;
            return SeekOutcome{SeekHit::Exact,
                cur_decoded_[static_cast<std::size_t>(cur_index_)].second, true};
        }
        return SeekOutcome{SeekHit::AfterEnd, 0, false};
    }
    if (cur_state_ == CurState::AfterEnd ||
        cur_state_ == CurState::Initial) {
        return SeekOutcome{SeekHit::AfterEnd, 0, false};
    }
    if (cur_index_ < 0) return SeekOutcome{SeekHit::AfterEnd, 0, false};
    if (static_cast<std::size_t>(cur_index_ + 1) < cur_decoded_.size()) {
        cur_index_ += 1;
        current_key_ = cur_decoded_[static_cast<std::size_t>(cur_index_)].first;
        return SeekOutcome{SeekHit::Exact, cur_decoded_[static_cast<std::size_t>(cur_index_)].second, true};
    }
    auto pg = get_page_(cur_leaf_);
    if (!pg) return pg.error();
    std::uint32_t right = read_u32_le(pg.value()->data() + 8);
    if (right == 0xFFFFFFFFu || right == 0) {
        cur_index_ = -1;
        cur_state_ = CurState::AfterEnd;
        return SeekOutcome{SeekHit::AfterEnd, 0, false};
    }
    cur_leaf_ = right;
    auto dec = decode_leaf_(cur_leaf_);
    if (!dec) return dec.error();
    cur_decoded_ = std::move(dec).value();
    if (cur_decoded_.empty()) {
        cur_index_ = -1;
        cur_state_ = CurState::AfterEnd;
        return SeekOutcome{SeekHit::AfterEnd, 0, false};
    }
    cur_index_ = 0;
    cur_state_ = CurState::Positioned;
    current_key_ = cur_decoded_[0].first;
    return SeekOutcome{SeekHit::Exact, cur_decoded_[0].second, true};
}

util::Result<SeekOutcome> CdxIndex::prev() {
    if (cur_state_ == CurState::AfterEnd ||
        cur_state_ == CurState::AfterEndKey) {
        // Resume from last key.
        return seek_last();
    }
    if (cur_state_ == CurState::BeforeBegin ||
        cur_state_ == CurState::Initial) {
        return SeekOutcome{SeekHit::BeforeBegin, 0, false};
    }
    // Between(cur_index): prev() returns the entry AT cur_index-1
    // (last lesser entry) and transitions to Positioned there.
    if (cur_state_ == CurState::Between) {
        if (cur_index_ > 0 &&
            static_cast<std::size_t>(cur_index_ - 1) < cur_decoded_.size()) {
            cur_index_ -= 1;
            cur_state_ = CurState::Positioned;
            current_key_ = cur_decoded_[
                static_cast<std::size_t>(cur_index_)].first;
            return SeekOutcome{SeekHit::Exact,
                cur_decoded_[static_cast<std::size_t>(cur_index_)].second, true};
        }
        cur_state_ = CurState::BeforeBegin;
        cur_index_ = -1;
        return SeekOutcome{SeekHit::BeforeBegin, 0, false};
    }
    if (cur_index_ < 0) return SeekOutcome{SeekHit::BeforeBegin, 0, false};
    if (cur_index_ > 0) {
        cur_index_ -= 1;
        current_key_ = cur_decoded_[static_cast<std::size_t>(cur_index_)].first;
        return SeekOutcome{SeekHit::Exact, cur_decoded_[static_cast<std::size_t>(cur_index_)].second, true};
    }
    auto pg = get_page_(cur_leaf_);
    if (!pg) return pg.error();
    std::uint32_t left = read_u32_le(pg.value()->data() + 4);
    if (left == 0xFFFFFFFFu || left == 0) {
        cur_index_ = -1;
        cur_state_ = CurState::BeforeBegin;
        return SeekOutcome{SeekHit::BeforeBegin, 0, false};
    }
    cur_leaf_ = left;
    auto dec = decode_leaf_(cur_leaf_);
    if (!dec) return dec.error();
    cur_decoded_ = std::move(dec).value();
    if (cur_decoded_.empty()) {
        cur_index_ = -1;
        cur_state_ = CurState::BeforeBegin;
        return SeekOutcome{SeekHit::BeforeBegin, 0, false};
    }
    cur_index_ = static_cast<std::int32_t>(cur_decoded_.size() - 1);
    cur_state_ = CurState::Positioned;
    current_key_ = cur_decoded_[static_cast<std::size_t>(cur_index_)].first;
    return SeekOutcome{SeekHit::Exact, cur_decoded_[static_cast<std::size_t>(cur_index_)].second, true};
}

util::Result<void>
CdxIndex::insert_into_subtree_(std::uint32_t      subtree_root,
                                std::uint32_t      recno,
                                const std::string& padded,
                                PromoteOut&        promote)
{
    promote.have = false;

    auto pg = get_page_(subtree_root);
    if (!pg) return pg.error();
    std::uint16_t attr = read_u16_le(pg.value()->data());

    if (attr & CDX_NODE_LEAF) {
        // ---- Leaf ----
        std::uint32_t left_sib  = read_u32_le(pg.value()->data() + 4);
        std::uint32_t right_sib = read_u32_le(pg.value()->data() + 8);

        auto dec = decode_compact_leaf_static(*pg.value(), key_size_);
        if (!dec) return dec.error();
        auto keys = std::move(dec).value();

        // Ordering must be (key ASC, recno ASC). FoxPro / SAP-ACE
        // resolve duplicate keys by recno, so a forward seek_key for
        // a non-unique key always returns the first-inserted recno.
        // upper_bound here lands AFTER any equal-key entries with
        // smaller recno; lower_bound landed BEFORE them, which
        // reversed the recno order and made seek_key surface the
        // last-inserted recno.
        auto pos = std::upper_bound(keys.begin(), keys.end(),
            std::pair<std::string, std::uint32_t>{padded, recno},
            [](const auto& a, const auto& b) {
                int c = std::memcmp(a.first.data(), b.first.data(),
                                     std::min(a.first.size(), b.first.size()));
                if (c != 0) return c < 0;
                return a.second < b.second;
            });
        if (unique_) {
            // Walk back to confirm whether the predecessor has the
            // same key (upper_bound puts us PAST equal-key entries).
            if (pos != keys.begin()) {
                auto prev = pos - 1;
                if (prev->first == padded) {
                    return util::Error{5044, 0, "CDX duplicate key", ""};
                }
            }
        }
        keys.insert(pos, {padded, recno});

        // Try to fit in the existing page.
        if (auto e = encode_leaf_(subtree_root, keys, left_sib, right_sib); e) {
            return {};
        }

        // Overflow → split. Find the largest mid such that BOTH halves
        // re-encode within a 512-byte page. Compact-leaf packing is
        // dup/trl-prefix dependent so a fixed midpoint isn't enough.
        std::size_t mid = keys.size() / 2;
        std::vector<std::pair<std::string, std::uint32_t>> left_keys;
        std::vector<std::pair<std::string, std::uint32_t>> right_keys;
        bool placed = false;
        Page tmp_left{};
        Page tmp_right{};
        while (mid > 0 && mid < keys.size()) {
            using diff_t =
                std::vector<std::pair<std::string, std::uint32_t>>::difference_type;
            left_keys.assign(keys.begin(),
                             keys.begin() + static_cast<diff_t>(mid));
            right_keys.assign(keys.begin() + static_cast<diff_t>(mid),
                              keys.end());
            auto el = encode_compact_leaf_static(tmp_left,  key_size_,
                                                  left_keys,  0u, 0u);
            auto er = encode_compact_leaf_static(tmp_right, key_size_,
                                                  right_keys, 0u, 0u);
            if (el && er) { placed = true; break; }
            // Shift mid towards center if the imbalance is wrong.
            if (!el && er)        { mid -= 1; }
            else if (el && !er)   { mid += 1; }
            else                  { break; }
        }
        if (!placed) {
            return util::Error{5000, 0,
                "CDX leaf split: cannot fit both halves", ""};
        }

        std::uint32_t new_off = allocate_page_();

        if (auto e = encode_leaf_(subtree_root, left_keys,
                                   left_sib, new_off); !e)
            return e.error();
        if (auto e = encode_leaf_(new_off, right_keys,
                                   subtree_root, right_sib); !e)
            return e.error();

        // Patch the right-sibling-of-old-leaf's left pointer to the
        // new leaf so doubly-linked walks stay consistent.
        if (right_sib != 0xFFFFFFFFu && right_sib != 0) {
            auto rs = get_page_(right_sib);
            if (!rs) return rs.error();
            write_u32_le(rs.value()->data() + 4, new_off);
            dirty_[right_sib] = true;
        }

        promote.have            = true;
        promote.left_max_key    = left_keys.back().first;
        promote.left_max_recno  = left_keys.back().second;
        promote.right_max_key   = right_keys.back().first;
        promote.right_max_recno = right_keys.back().second;
        promote.new_right_off   = new_off;
        promote.old_left_off    = subtree_root;
        return {};
    }

    // ---- Branch ----
    auto dec_b = decode_branch_static(*pg.value(), key_size_);
    if (!dec_b) return dec_b.error();
    auto entries = std::move(dec_b).value();
    if (entries.empty()) {
        return util::Error{6106, 0, "CDX branch with no entries", ""};
    }

    // Pick the first entry whose key >= padded; if none, use last.
    std::size_t idx = 0;
    while (idx < entries.size() &&
           std::memcmp(entries[idx].key.data(), padded.data(), key_size_) < 0) {
        ++idx;
    }
    if (idx == entries.size()) idx = entries.size() - 1;

    PromoteOut child_promote;
    if (auto e = insert_into_subtree_(entries[idx].child, recno,
                                       padded, child_promote); !e)
        return e.error();

    // If the picked child key was the rightmost entry, the inserted
    // key may exceed the old subtree max. Refresh the parent entry's
    // key from the (possibly-updated) child even on the no-split path.
    if (!child_promote.have) {
        // No structural change. But the entry's key may need refresh
        // if the inserted key is larger than the subtree's prior max.
        if (idx == entries.size() - 1) {
            if (std::memcmp(padded.data(), entries[idx].key.data(),
                            key_size_) > 0) {
                entries[idx].key   = padded;
                entries[idx].recno = recno;
                auto epg = get_page_(subtree_root);
                if (!epg) return epg.error();
                std::uint32_t l = read_u32_le(epg.value()->data() + 4);
                std::uint32_t r = read_u32_le(epg.value()->data() + 8);
                if (auto e = encode_branch_static(*epg.value(), key_size_,
                                                   entries, l, r); !e)
                    return e.error();
                dirty_[subtree_root] = true;
            }
        }
        return {};
    }

    // Child split: update entries[idx] (the OLD pointer keeps its
    // child offset but its key shrinks to the left half's max), then
    // insert a new entry right after it for the new right child.
    entries[idx].key   = child_promote.left_max_key;
    entries[idx].recno = child_promote.left_max_recno;
    BranchEntry new_entry;
    new_entry.key   = child_promote.right_max_key;
    new_entry.recno = child_promote.right_max_recno;
    new_entry.child = child_promote.new_right_off;
    {
        using diff_t = std::vector<BranchEntry>::difference_type;
        entries.insert(entries.begin() +
                       static_cast<diff_t>(idx + 1), new_entry);
    }

    // Re-fetch sibling pointers (page may have been moved by a get_).
    auto epg = get_page_(subtree_root);
    if (!epg) return epg.error();
    std::uint32_t left_sib  = read_u32_le(epg.value()->data() + 4);
    std::uint32_t right_sib = read_u32_le(epg.value()->data() + 8);

    if (entries.size() <= branch_capacity(key_size_)) {
        if (auto e = encode_branch_static(*epg.value(), key_size_,
                                           entries, left_sib, right_sib); !e)
            return e.error();
        dirty_[subtree_root] = true;
        return {};
    }

    // Branch overflow → split midpoint. Branch encode is fixed-stride
    // so a single midpoint always works.
    std::size_t mid = entries.size() / 2;
    using diff_t = std::vector<BranchEntry>::difference_type;
    std::vector<BranchEntry> left_be(entries.begin(),
                                     entries.begin() + static_cast<diff_t>(mid));
    std::vector<BranchEntry> right_be(entries.begin() + static_cast<diff_t>(mid),
                                      entries.end());

    std::uint32_t new_off = allocate_page_();

    auto lpg = get_page_(subtree_root);
    if (!lpg) return lpg.error();
    if (auto e = encode_branch_static(*lpg.value(), key_size_,
                                       left_be, left_sib, new_off); !e)
        return e.error();
    dirty_[subtree_root] = true;

    auto rpg = get_page_(new_off);
    if (!rpg) return rpg.error();
    if (auto e = encode_branch_static(*rpg.value(), key_size_,
                                       right_be, subtree_root, right_sib); !e)
        return e.error();
    dirty_[new_off] = true;

    if (right_sib != 0xFFFFFFFFu && right_sib != 0) {
        auto rs = get_page_(right_sib);
        if (!rs) return rs.error();
        write_u32_le(rs.value()->data() + 4, new_off);
        dirty_[right_sib] = true;
    }

    promote.have            = true;
    promote.left_max_key    = left_be.back().key;
    promote.left_max_recno  = left_be.back().recno;
    promote.right_max_key   = right_be.back().key;
    promote.right_max_recno = right_be.back().recno;
    promote.new_right_off   = new_off;
    promote.old_left_off    = subtree_root;
    return {};
}

util::Result<void>
CdxIndex::insert(std::uint32_t recno, const std::string& key) {
    if (mode_ == IndexOpenMode::ReadOnly) {
        return util::Error{5000, 0, "CDX opened read-only", ""};
    }
    std::string padded = key;
    if (padded.size() < key_size_) padded.append(key_size_ - padded.size(), ' ');
    if (padded.size() > key_size_) padded.resize(key_size_);

    if (root_page_ == 0) {
        std::uint32_t off = allocate_page_();
        std::vector<std::pair<std::string, std::uint32_t>> keys{{padded, recno}};
        if (auto e = encode_leaf_(off, keys, 0xFFFFFFFFu, 0xFFFFFFFFu); !e) {
            return e.error();
        }
        root_page_ = off;
        return rewrite_header_();
    }

    PromoteOut promote;
    if (auto e = insert_into_subtree_(root_page_, recno, padded, promote); !e)
        return e.error();

    if (!promote.have) return {};

    // Root split → allocate new branch root with two children.
    std::uint32_t new_root = allocate_page_();

    std::vector<BranchEntry> entries = {
        { promote.left_max_key,  promote.left_max_recno,  promote.old_left_off },
        { promote.right_max_key, promote.right_max_recno, promote.new_right_off }
    };
    auto rp = get_page_(new_root);
    if (!rp) return rp.error();
    if (auto e = encode_branch_static(*rp.value(), key_size_,
                                       entries, 0xFFFFFFFFu, 0xFFFFFFFFu); !e)
        return e.error();
    dirty_[new_root] = true;
    root_page_ = new_root;
    return rewrite_header_();
}

util::Result<void>
CdxIndex::erase(std::uint32_t recno, const std::string& key) {
    if (mode_ == IndexOpenMode::ReadOnly) {
        return util::Error{5000, 0, "CDX opened read-only", ""};
    }
    if (root_page_ == 0) return util::Error{5044, 0, "CDX empty", ""};

    std::string padded = key;
    if (padded.size() < key_size_) padded.append(key_size_ - padded.size(), ' ');
    if (padded.size() > key_size_) padded.resize(key_size_);

    // Locate (key, recno) via the same leaf-chain walk seek_key uses.
    // The old root-only decode path silently failed once insert_into_
    // subtree_ promoted a branch root, leaving stale keys behind.
    auto sk = seek_key(padded, /*soft=*/false);
    if (!sk) return sk.error();

    bool found = false;
    if (sk.value().positioned && sk.value().hit == SeekHit::Exact) {
        std::uint32_t guard = 0;
        while (guard++ < 4096) {
            if (cur_index_ >= 0 &&
                static_cast<std::size_t>(cur_index_) < cur_decoded_.size()) {
                const auto& e =
                    cur_decoded_[static_cast<std::size_t>(cur_index_)];
                if (e.first == padded && (recno == 0 || e.second == recno)) {
                    found = true;
                    break;
                }
                if (e.first != padded) break;
            }
            auto nx = next();
            if (!nx || !nx.value().positioned) break;
            if (nx.value().hit != SeekHit::Exact) break;
            std::string ck = current_key();
            if (ck.size() < padded.size())
                ck.append(padded.size() - ck.size(), ' ');
            if (ck.size() > padded.size()) ck.resize(padded.size());
            if (ck != padded) break;
        }
    }
    if (!found) {
        return util::Error{5044, 0, "CDX key not found", ""};
    }

    auto pg = get_page_(cur_leaf_);
    if (!pg) return pg.error();
    std::uint32_t left_sib  = read_u32_le(pg.value()->data() + 4);
    std::uint32_t right_sib = read_u32_le(pg.value()->data() + 8);

    auto dec = decode_leaf_(cur_leaf_);
    if (!dec) return dec.error();
    auto keys = std::move(dec).value();

    auto it = std::find_if(keys.begin(), keys.end(),
        [&](const auto& kv) {
            return kv.first == padded && (recno == 0 || kv.second == recno);
        });
    if (it == keys.end()) {
        return util::Error{5044, 0, "CDX key not found", ""};
    }
    keys.erase(it);
    invalidate_cursor();
    return encode_leaf_(cur_leaf_, keys, left_sib, right_sib);
}

util::Result<void> CdxIndex::flush() {
    for (auto& [off, _] : page_cache_) {
        auto r = flush_page_(off);
        if (!r) return r.error();
    }
    // Persist root_page_ / counter — `insert` already does this on
    // every root change, but a paranoia rewrite here keeps the
    // sub-tag header in sync if the caller reaches `flush` via a
    // different path (eg. set_options + clear_data + insert chain).
    if (sub_header_offset_ != 0) {
        if (auto r = rewrite_header_(); !r) return r.error();
    }
    return file_.sync();
}

util::Result<void> CdxIndex::set_options(bool unique, bool descend) {
    return set_options(unique, descend, key_size_);
}

util::Result<void> CdxIndex::set_options(bool unique, bool descend,
                                         std::uint16_t new_key_size) {
    if (sub_header_offset_ == 0) {
        return util::Error{6106, 0,
            "CDX sub-tag header offset uninitialised", ""};
    }
    unique_  = unique;
    descend_ = descend;
    if (new_key_size != 0) key_size_ = new_key_size;
    index_opt_ = static_cast<std::uint8_t>(
        (unique ? 0x01 : 0x00) | 0x20);
    std::array<std::uint8_t, CDX_HEADER_LEN> hdr{};
    auto got = file_.read_at(sub_header_offset_, hdr.data(), hdr.size());
    if (!got) return got.error();
    hdr[14] = index_opt_;
    if (new_key_size != 0)
        write_u16_le(hdr.data() + 12, new_key_size);
    write_u16_le(hdr.data() + 502, descend ? 1 : 0);
    auto wrote = file_.write_at(sub_header_offset_, hdr.data(), hdr.size());
    if (!wrote) return wrote.error();
    return {};
}

util::Result<void> CdxIndex::clear_data() {
    root_page_ = 0;
    page_cache_.clear();
    dirty_.clear();
    cur_leaf_  = 0;
    cur_index_ = -1;
    cur_state_ = CurState::Initial;
    cur_decoded_.clear();
    return rewrite_header_();
}

std::uint32_t CdxIndex::allocate_page_() {
    std::lock_guard<std::mutex> lk(g_cdx_alloc_mu);
    auto& tail = g_cdx_alloc_tail[path_];
    if (auto sz = file_.size()) {
        tail = std::max(tail, sz.value());
    }
    tail = std::max(tail, file_size_);
    tail = std::max(tail, static_cast<std::uint64_t>(CDX_SUB_DATA_BASE));
    const std::uint32_t off = static_cast<std::uint32_t>(tail);
    tail += CDX_PAGE_LEN;
    file_size_ = tail;
    page_cache_.emplace(off, Page{});
    return off;
}

util::Result<void> CdxIndex::rewrite_header_() {
    if (sub_header_offset_ == 0) {
        return util::Error{6106, 0, "CDX sub-tag header offset uninitialised", ""};
    }
    std::array<std::uint8_t, CDX_HEADER_LEN> hdr{};
    auto got = file_.read_at(sub_header_offset_, hdr.data(), hdr.size());
    if (!got) return got.error();
    write_u32_le(hdr.data() + 0, root_page_);
    write_u32_le(hdr.data() + 4, free_ptr_);
    write_u32_le(hdr.data() + 8, ++counter_);
    auto wrote = file_.write_at(sub_header_offset_, hdr.data(), hdr.size());
    if (!wrote) return wrote.error();
    return {};
}

util::Result<CdxIndex>
CdxIndex::create(const std::string& path,
                 const std::string& tag_name,
                 const std::string& key_expr,
                 std::uint16_t      key_size,
                 bool               unique,
                 bool               descend) {
    auto fres = platform::File::open(path, platform::OpenMode::CreateRW);
    if (!fres) return fres.error();
    platform::File file = std::move(fres).value();

    // 1) File / structure-tag header at offset 0 (1024B).
    //    Its root_page points to the structure tag's leaf at CDX_STRUCT_ROOT_OFFSET.
    //    The structure tag itself uses key_size = 10 (tag-name length).
    {
        std::array<std::uint8_t, CDX_HEADER_LEN> file_hdr{};
        write_u32_le(file_hdr.data() + 0, CDX_STRUCT_ROOT_OFFSET);
        write_u32_le(file_hdr.data() + 4, 0xFFFFFFFFu);
        write_u32_le(file_hdr.data() + 8, 1);
        write_u16_le(file_hdr.data() + 12, CDX_STRUCT_KEY_LEN);
        file_hdr[14] = 0x60; // CDX_TYPE_COMPACT | CDX_TYPE_COMPOUND
        file_hdr[15] = 0x01;
        write_u16_le(file_hdr.data() + 16, CDX_HEADER_LEN);
        write_u16_le(file_hdr.data() + 18, CDX_PAGE_LEN);
        write_u16_le(file_hdr.data() + 502, 0);
        write_u16_le(file_hdr.data() + 508, 0);
        write_u16_le(file_hdr.data() + 510, 0);
        auto wrote = file.write_at(0, file_hdr.data(), file_hdr.size());
        if (!wrote) return wrote.error();
    }

    // 2) Structure-tag root leaf at CDX_STRUCT_ROOT_OFFSET (512B).
    //    One entry mapping the tag name to the sub-tag header offset.
    {
        Page leaf{};
        std::string padded = tag_name;
        if (padded.size() < CDX_STRUCT_KEY_LEN)
            padded.append(CDX_STRUCT_KEY_LEN - padded.size(), ' ');
        if (padded.size() > CDX_STRUCT_KEY_LEN)
            padded.resize(CDX_STRUCT_KEY_LEN);
        std::vector<std::pair<std::string, std::uint32_t>> entries{
            { padded, CDX_SUB_HEADER_OFFSET }
        };
        auto enc = encode_compact_leaf_static(leaf, CDX_STRUCT_KEY_LEN,
                                              entries,
                                              0xFFFFFFFFu, 0xFFFFFFFFu,
                                              /*req_byte=*/5);
        if (!enc) return enc.error();
        auto wrote = file.write_at(CDX_STRUCT_ROOT_OFFSET,
                                   leaf.data(), leaf.size());
        if (!wrote) return wrote.error();
    }

    // 3) Sub-tag CDXTAGHEADER at CDX_SUB_HEADER_OFFSET (1024B).
    //    root_page = 0 (no data yet); first insert allocates the leaf.
    {
        auto sub_hdr = build_subtag_header(0, key_size, key_expr,
                                           tag_name, unique, descend);
        auto wrote = file.write_at(CDX_SUB_HEADER_OFFSET,
                                   sub_hdr.data(), sub_hdr.size());
        if (!wrote) return wrote.error();
    }

    if (auto s = file.sync(); !s) return s.error();

    CdxIndex ix;
    ix.path_              = path;
    ix.file_              = std::move(file);
    ix.mode_              = IndexOpenMode::Shared;
    ix.root_page_         = 0;
    ix.free_ptr_          = 0xFFFFFFFFu;
    ix.counter_           = 1;
    ix.key_size_          = key_size;
    ix.index_opt_         = static_cast<std::uint8_t>(
        (unique ? 0x01 : 0x00) | 0x20);
    ix.unique_            = unique;
    ix.descend_           = descend;
    ix.key_expr_          = key_expr;
    ix.tag_name_          = tag_name;
    ix.sub_header_offset_ = CDX_SUB_HEADER_OFFSET;
    ix.file_size_         = CDX_SUB_DATA_BASE;
    {
        std::lock_guard<std::mutex> lk(g_cdx_alloc_mu);
        g_cdx_alloc_tail[path] = std::max(g_cdx_alloc_tail[path],
            static_cast<std::uint64_t>(CDX_SUB_DATA_BASE));
    }
    return ix;
}

std::uint16_t CdxIndex::pick_rec_bits_(std::uint32_t max_rec) {
    return bits_for(max_rec);
}

util::Result<std::vector<std::string>>
CdxIndex::list_tags(const std::string& path) {
    auto fres = platform::File::open(path, platform::OpenMode::ReadOnly);
    if (!fres) return fres.error();
    platform::File f = std::move(fres).value();

    std::array<std::uint8_t, CDX_HEADER_LEN> hdr{};
    auto got = f.read_at(0, hdr.data(), hdr.size());
    if (!got) return got.error();
    if (got.value() < CDX_HEADER_LEN) {
        return util::Error{6106, 0, "CDX header truncated", path};
    }
    std::uint32_t struct_root = read_u32_le(hdr.data() + 0);
    if (struct_root == 0) return std::vector<std::string>{};

    Page leaf{};
    auto got2 = f.read_at(struct_root, leaf.data(), leaf.size());
    if (!got2) return got2.error();
    if (got2.value() < CDX_PAGE_LEN) {
        return util::Error{6106, 0, "CDX struct-tag leaf truncated", path};
    }
    auto dec = decode_compact_leaf_static(leaf, CDX_STRUCT_KEY_LEN);
    if (!dec) return dec.error();
    std::vector<std::string> out;
    out.reserve(dec.value().size());
    for (auto& e : dec.value()) {
        std::string t = e.first;
        while (!t.empty() && t.back() == ' ') t.pop_back();
        out.push_back(std::move(t));
    }
    return out;
}

util::Result<CdxIndex>
CdxIndex::add_tag(const std::string& path,
                  const std::string& tag_name,
                  const std::string& key_expr,
                  std::uint16_t      key_size,
                  bool               unique,
                  bool               descend) {
    auto fres = platform::File::open(path, platform::OpenMode::OpenExisting);
    if (!fres) return fres.error();
    platform::File file = std::move(fres).value();

    auto sz = file.size();
    if (!sz) return sz.error();
    std::uint64_t file_size = sz.value();

    // 1) Read file header to find struct_root.
    std::array<std::uint8_t, CDX_HEADER_LEN> fh{};
    auto got = file.read_at(0, fh.data(), fh.size());
    if (!got) return got.error();
    if (got.value() < CDX_HEADER_LEN) {
        return util::Error{6106, 0, "CDX header truncated", path};
    }
    std::uint32_t struct_root = read_u32_le(fh.data() + 0);
    if (struct_root == 0) {
        return util::Error{6106, 0, "CDX has no structure tag root", path};
    }

    // 2) Read + decode struct-tag leaf.
    Page leaf{};
    auto got2 = file.read_at(struct_root, leaf.data(), leaf.size());
    if (!got2) return got2.error();
    auto dec = decode_compact_leaf_static(leaf, CDX_STRUCT_KEY_LEN);
    if (!dec) return dec.error();
    auto entries = std::move(dec).value();

    // Reject duplicates.
    std::string padded = tag_name;
    if (padded.size() < CDX_STRUCT_KEY_LEN)
        padded.append(CDX_STRUCT_KEY_LEN - padded.size(), ' ');
    if (padded.size() > CDX_STRUCT_KEY_LEN)
        padded.resize(CDX_STRUCT_KEY_LEN);
    for (auto& e : entries) {
        if (e.first == padded) {
            return util::Error{5044, 0,
                "CDX already has a sub-tag with that name", tag_name};
        }
    }

    // 3) Allocate sub-tag header at end-of-file, page-aligned to CDX_PAGE_LEN.
    std::uint64_t new_off = (file_size + CDX_PAGE_LEN - 1)
                          / CDX_PAGE_LEN * CDX_PAGE_LEN;
    if (new_off < CDX_SUB_DATA_BASE) new_off = CDX_SUB_DATA_BASE;

    // 4) Append struct-tag entry, re-encode, write back. Keep
    // creation order (no sort) so tag-ordinal lookups match
    // Harbour rddads / Clipper convention.
    entries.emplace_back(padded, static_cast<std::uint32_t>(new_off));
    Page new_leaf{};
    auto enc = encode_compact_leaf_static(new_leaf, CDX_STRUCT_KEY_LEN,
                                          entries,
                                          0xFFFFFFFFu, 0xFFFFFFFFu,
                                          /*req_byte=*/5);
    if (!enc) return enc.error();
    auto wrote = file.write_at(struct_root, new_leaf.data(), new_leaf.size());
    if (!wrote) return wrote.error();

    // 5) Write fresh sub-tag CDXTAGHEADER at new_off.
    auto sub_hdr = build_subtag_header(0, key_size, key_expr,
                                       tag_name, unique, descend);
    auto wrote2 = file.write_at(new_off, sub_hdr.data(), sub_hdr.size());
    if (!wrote2) return wrote2.error();

    if (auto s = file.sync(); !s) return s.error();

    CdxIndex ix;
    ix.path_              = path;
    ix.file_              = std::move(file);
    ix.mode_              = IndexOpenMode::Shared;
    ix.root_page_         = 0;
    ix.free_ptr_          = 0xFFFFFFFFu;
    ix.counter_           = 1;
    ix.key_size_          = key_size;
    ix.index_opt_         = static_cast<std::uint8_t>(
        (unique ? 0x01 : 0x00) | 0x20);
    ix.unique_            = unique;
    ix.descend_           = descend;
    ix.key_expr_          = key_expr;
    ix.tag_name_          = tag_name;
    ix.sub_header_offset_ = static_cast<std::uint32_t>(new_off);
    ix.file_size_         = new_off + CDX_HEADER_LEN;
    {
        std::lock_guard<std::mutex> lk(g_cdx_alloc_mu);
        g_cdx_alloc_tail[path] = std::max(g_cdx_alloc_tail[path],
            new_off + CDX_HEADER_LEN);
    }
    return ix;
}

} // namespace openads::drivers::cdx
