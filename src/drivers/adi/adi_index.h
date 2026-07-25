#pragma once

#include "drivers/index_trait.h"
#include "platform/file.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace openads::drivers::adi {

constexpr std::uint32_t ADI_PAGE_SIZE    = 512;
constexpr std::uint32_t ADI_INVALID_PAGE = 0xFFFFFFFFu;

// ADI B-tree level constants (observed from real ADI files).
// Level 2 and 3 are both dense-leaf levels:
//   level 3 — numeric-key indexes (landlords.adi) or single-page root
//   level 2 — character-key indexes with a branch level above (leases.adi)
// The tag-directory page (page 2) also carries level=3 but is always accessed
// directly by page number, so the collision with dense-leaf detection is harmless.
constexpr std::uint16_t ADI_LVL_SPARSE = 0;  // sparse leaf: ref → dense leaf
constexpr std::uint16_t ADI_LVL_BRANCH = 1;  // root/branch: ref → sparse/dense leaf
constexpr std::uint16_t ADI_LVL_DENSE2 = 2;  // dense leaf (character-key indexes)
constexpr std::uint16_t ADI_LVL_DENSE  = 3;  // dense leaf (numeric-key indexes)
constexpr std::uint16_t ADI_LVL_TAGDIR = 3;  // tag directory (page 2 only)

// Numeric branch entry (level-1 or level-0 for numeric-key ADI):
//   key[8 BE float64] + cum[4 BE uint32] + page[4 BE uint32] = 16 bytes
constexpr std::uint32_t ADI_TREE_ENTRY_SIZE   = 16;
// Character branch entry (level-1 for char-key ADI):
//   padded_key[(key_len+3)&~3] + cum[4 LE uint32] + page[1 uint8]
// The padded_key length and total entry size are computed at open() time and
// stored in char_key_padded_len_ and branch_entry_sz_.

// Dense-leaf entry size depends on field storage width:
//   1-byte fields (LOGICAL): recno(1) + key_value(1)     = 2 bytes
//   wider fields:            recno(1) + dup(1) + trail(1) = 3 bytes
// Use dense_entry_size(fld_length) to get the right value at runtime.
constexpr std::uint32_t ADI_TREE_ENTRY_START  = 12; // right after 12-byte page header
constexpr std::uint32_t ADI_DENSE_ENTRY_START = 24; // after header(12) + sub-header(12)
constexpr std::uint32_t ADI_TAGDIR_ENTRY_SIZE =  6; // XX(1) + zeros(4) + YY(1)
constexpr std::uint32_t ADI_TAGDIR_ENTRY_START= 24; // same as dense leaf

// A page is a dense leaf if its level is ADI_LVL_DENSE2 or ADI_LVL_DENSE.
inline constexpr bool is_dense_leaf(std::uint16_t lv) noexcept {
    return lv == ADI_LVL_DENSE2 || lv == ADI_LVL_DENSE;
}

inline constexpr std::uint32_t dense_entry_size(std::uint16_t fld_length) noexcept {
    return (fld_length == 1u) ? 2u : 3u;
}

// ADT type codes that appear as ADI index keys (numeric → sign-flipped float64)
constexpr std::uint16_t ADT_TYPE_LOGICAL   =  1;
constexpr std::uint16_t ADT_TYPE_DATE      =  3;  // 4-byte uint32 LE JDN
constexpr std::uint16_t ADT_TYPE_CHAR      =  4;
constexpr std::uint16_t ADT_TYPE_MEMO      =  5;
constexpr std::uint16_t ADT_TYPE_BINARY    =  6;
constexpr std::uint16_t ADT_TYPE_DOUBLE    = 10;  // 8-byte IEEE754 LE
constexpr std::uint16_t ADT_TYPE_INTEGER   = 11;  // 4-byte int32 LE
constexpr std::uint16_t ADT_TYPE_SHORTINT  = 12;  // 2-byte int16 LE
constexpr std::uint16_t ADT_TYPE_TIME      = 13;  // 4-byte uint32 LE ms
constexpr std::uint16_t ADT_TYPE_TIMESTAMP = 14;  // 8-byte (4B JDN + 4B ms)
constexpr std::uint16_t ADT_TYPE_AUTOINC   = 15;  // 4-byte uint32 LE
constexpr std::uint16_t ADT_TYPE_MONEY     = 18;  // 8-byte IEEE754 LE (same as Double)
constexpr std::uint16_t ADT_TYPE_CICHAR    = 20;  // case-insensitive char (different key enc)
constexpr std::uint16_t ADT_TYPE_ROWVERSION= 21;  // (unconfirmed)
constexpr std::uint16_t ADT_TYPE_MODTIME   = 22;  // 8-byte modification timestamp

// Pack a numeric value into the 8-byte ADI total-order key (IEEE754 BE).
std::string pack_double_key(double v);

// Pack a uint64 into an 8-byte unsigned total-order key (BE, high-bit flip).
std::string pack_u64_key(std::uint64_t v);

// Read-only ADI index.  Each instance represents one tag (one indexed field,
// or a compound index on multiple fields).
// Multi-tag discovery:  AdiIndex::list_tags(adi_path) → field names
//                       AdiIndex::open_named(adi_path, mode, field_name) → opens one tag
class AdiIndex final : public IIndex {
public:
    using Page = std::array<std::uint8_t, ADI_PAGE_SIZE>;

    // Per-field component of a (possibly compound) index key.
    struct FieldComp {
        std::uint16_t type;    // ADT_TYPE_* constant
        std::uint16_t offset;  // byte offset within ADT record (past the 1-byte deleted flag)
        std::uint16_t length;  // field storage length in bytes
    };

    // IIndex
    util::Result<void> open(const std::string& path, IndexOpenMode mode) override;

    std::string    name()       const override { return tag_name_; }
    std::string    expression() const override { return tag_name_; }
    std::string    file_path()  const override { return adi_path_; }
    bool           descending() const override { return false; }
    bool           unique()     const override { return unique_; }
    std::uint16_t  key_length() const override {
        return static_cast<std::uint16_t>(key_total_len_);
    }

    util::Result<SeekOutcome> seek_first()                           override;
    util::Result<SeekOutcome> seek_last()                            override;
    util::Result<SeekOutcome> seek_key(const std::string& key,
                                       bool soft)                    override;
    util::Result<SeekOutcome> next()                                 override;
    util::Result<SeekOutcome> prev()                                 override;
    std::string current_key() const override { return current_key_; }

    util::Result<void> insert(std::uint32_t recno,
                              const std::string& key) override;
    util::Result<void> erase (std::uint32_t recno,
                              const std::string& key) override;
    util::Result<void> flush() override;

    // Parameters for writing a fresh single-tag .adi skeleton.
    struct CreateParams {
        std::uint8_t  field_num   = 1;   // 1-based ADT field number (F-marker)
        std::string   field_name;        // indexed field name (footer + yy byte)
        std::uint16_t adt_type    = 0;   // ADT_TYPE_* of indexed field
        std::uint16_t fld_length  = 0;   // byte width of indexed field
        std::uint32_t adt_hdr_len = 0;   // ADT header length (bytes 32..35)
        std::uint32_t adt_rec_len = 0;   // ADT record length
        bool          unique      = false;
        // Full path of the ADT table this index belongs to. Required for a
        // NON-STRUCTURAL bag, whose .adi stem differs from the table's (the
        // `INDEX ON ... TAG ... TO <other path>` form). When empty, the
        // companion ADT path is derived from the .adi stem (structural bag).
        std::string   adt_path;
    };

    // Build a fresh 7-page .adi matching the legacy single-tag layout
    // (file header + tag directory + per-tag header + F-marker + 2 empty
    // dense-leaf pages).  Returns an index ready for insert()/flush().
    static util::Result<AdiIndex>
        create(const std::string& adi_path, const CreateParams& params);

    // Append a new tag to an existing .adi (3 pages at EOF: per-tag
    // header + F-marker + empty dense root).  New entry is prepended in
    // the tag directory (legacy convention observed in dual-tag fixtures).
    static util::Result<AdiIndex>
        add_tag(const std::string& adi_path, const CreateParams& params);

    // Wipe the B+tree for this tag (root dense leaf count → 0) so a
    // CREATE INDEX overwrite can rebuild from scratch.
    util::Result<void> clear_data();

    // Multi-tag API (mirrors CdxIndex). adt_path is the owning table's path;
    // when empty the companion ADT is derived from the .adi stem (structural
    // bag), otherwise it is used as-is (non-structural / separate bag).
    static util::Result<std::vector<std::string>>
        list_tags(const std::string& adi_path,
                  const std::string& adt_path = {});

    util::Result<void> open_named(const std::string& adi_path,
                                  IndexOpenMode       mode,
                                  const std::string&  field_name,
                                  const std::string&  adt_path = {});

    // Create a new ADI file with one tag (expression = comma-separated column names).
    // On return the AdiIndex is positioned on that tag and ready for inserts.
    static util::Result<AdiIndex> create(const std::string& adi_path,
                                         const std::string& adt_path,
                                         const std::string& expression,
                                         bool               unique);

    // Add a new tag to an existing ADI file.
    // On return the AdiIndex is positioned on the new tag and ready for inserts.
    static util::Result<AdiIndex> add_tag(const std::string& adi_path,
                                           const std::string& adt_path,
                                           const std::string& expression,
                                           bool               unique);

private:
    // Read / write a 512-byte page from/to the ADI file
    util::Result<void> read_adi_page_ (std::uint32_t page_no, Page& buf);
    util::Result<void> write_adi_page_(std::uint32_t page_no, const Page& buf);

    // Allocate a new page at end-of-file; returns its page number.
    util::Result<std::uint32_t> alloc_page_();

    // Build a dense-leaf entry (entry_size_ bytes) at dst.
    void build_dense_entry_(std::uint8_t* dst, std::uint32_t recno,
                            const std::string& ikey) const noexcept;

    // Extract key bytes from a branch-page entry at index idx.
    std::string branch_key_at_(const std::uint8_t* pg, int idx) const noexcept;

    // Frame recorded while descending a branch during insert.
    struct PathFrame { std::uint32_t page_no; std::uint16_t cnt; int entry_idx; };

    // After a leaf or branch split: push (left_max_key, right_pg, right_max_key)
    // into the parent level, splitting branches recursively as needed.
    util::Result<void> promote_split_(
        std::vector<PathFrame>& path,
        std::uint32_t left_pg,  const std::string& left_max,
        std::uint32_t right_pg, const std::string& right_max);

    // Read a 512-byte page from the ADI file into buf

    // Load the dense leaf at page_no into cur_page_ and update cursor metadata
    util::Result<void> load_dense_leaf_(std::uint32_t page_no);

    // Navigate to the first (leftmost) entry of the B-tree
    util::Result<SeekOutcome> navigate_leftmost_();

    // Navigate to the last (rightmost) entry of the B-tree
    util::Result<SeekOutcome> navigate_rightmost_();

    // Build a SeekOutcome from the current cursor position
    SeekOutcome make_positioned_() const;

    // Update cur_recno_ and current_key_ from cur_page_ at cur_idx_
    util::Result<void> refresh_current_();

    // Build the full index key (may be compound) from an ADT record
    util::Result<std::string> key_for_recno_(std::uint32_t recno);

    // Return the child page pointer from a branch page entry.
    // For numeric indexes: reads 4-byte BE uint32 at offset 12 within the entry.
    // For character indexes: reads 1-byte uint8 at offset char_key_padded_len_+4.
    std::uint32_t branch_entry_page_(const std::uint8_t* pg, int idx) const noexcept;

    // Compare two keys.  For numeric keys 8-byte memcmp; for char keys
    // key_total_len_ bytes (memcmp; CICHAR case-insensitivity deferred).
    int compare_keys_(const std::string& a, const std::string& b) const noexcept;

    // CICHAR collation: return a comparison-normalized copy of a key with
    // CICHAR components upper-cased (CHAR and numeric bytes untouched), so
    // seeks/descents on a case-insensitive key match regardless of case.
    // No-op (returns the input) when the key has no CICHAR component.
    std::string fold_for_compare_(const std::string& k) const;

    // Initialise all tag-related state from a list of 1-based field numbers,
    // a root page, a field-descriptor table, and the ADT layout sizes.
    // Field descriptors are passed as four parallel arrays to avoid exposing
    // the internal AdtFieldDesc type in this header.
    util::Result<void> apply_tag_(
        const std::vector<std::uint8_t>& fnums,
        std::uint32_t                    root_pg,
        const std::vector<std::uint16_t>& fd_types,
        const std::vector<std::uint16_t>& fd_offsets,
        const std::vector<std::uint16_t>& fd_lengths,
        const std::vector<std::string>&   fd_names,
        std::uint32_t hlen, std::uint32_t rlen,
        bool unique);


    // Open mode (set by open / open_named)
    IndexOpenMode   mode_ = IndexOpenMode::ReadOnly;

    // ADI file + ADT companion file
    platform::File  adi_file_;
    platform::File  adt_file_;
    std::string     adi_path_;

    // Tag metadata (primary / first-component field)
    std::string     tag_name_;        // ADT field name of first component
    std::uint32_t   root_page_  = 0;
    std::uint16_t   adt_type_   = 0;  // type of first-component field
    std::uint16_t   fld_offset_ = 0;  // offset of first-component field in ADT record
    std::uint16_t   fld_length_ = 0;  // length of first-component field
    bool            unique_     = false;

    // All key components (1 entry for simple, >1 for compound indexes).
    std::vector<FieldComp> key_fields_;

    // True when any key component is CICHAR — enables case-insensitive
    // seek/compare (fold_for_compare_).
    bool has_ci_component_ = false;

    // Key type and branch-entry geometry (computed at open time).
    bool          char_key_          = false; // first component is CICHAR or CHAR
    std::uint32_t key_total_len_     = 8;     // total key bytes (8 for numeric, field_len[s] for char)
    std::uint32_t char_key_padded_len_ = 0;   // (key_total_len_+3)&~3 (char keys only)
    std::uint32_t branch_entry_sz_   = ADI_TREE_ENTRY_SIZE; // bytes per branch-page entry

    // ADT layout for record reads
    std::uint32_t   adt_hdr_len_ = 0;
    std::uint32_t   adt_rec_len_ = 0;

    // Dense-leaf cursor
    std::uint32_t   entry_size_ = 3;  // dense_entry_size(fld_length_)
    std::uint32_t   cur_pg_    = ADI_INVALID_PAGE;
    std::int32_t    cur_idx_   = -1;
    std::uint16_t   cur_cnt_   = 0;
    std::uint32_t   cur_lsib_  = ADI_INVALID_PAGE;
    std::uint32_t   cur_rsib_  = ADI_INVALID_PAGE;
    std::uint32_t   cur_recno_ = 0;
    std::string     current_key_;
    Page            cur_page_{};
};

} // namespace openads::drivers::adi
