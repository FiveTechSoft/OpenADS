#pragma once

#include "drivers/index_trait.h"
#include "platform/file.h"

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace openads::drivers::cdx {

constexpr std::uint16_t CDX_PAGE_LEN     = 512;
constexpr std::uint16_t CDX_HEADER_LEN   = 1024;
constexpr std::uint16_t CDX_EXT_HEADSIZE = 24;
constexpr std::uint16_t CDX_INT_HEADSIZE = 12;

constexpr std::uint16_t CDX_NODE_BRANCH = 0;
constexpr std::uint16_t CDX_NODE_ROOT   = 1;
constexpr std::uint16_t CDX_NODE_LEAF   = 2;

// FoxPro CDX is a compound index: page 0 is the file header, which
// doubles as the structure tag's CDXTAGHEADER. Its B+tree maps tag
// names (10-byte keys) to per-sub-tag CDXTAGHEADER offsets. Each
// sub-tag has its own 1024-byte header plus an independent B+tree.
//
// OpenADS layout (single sub-tag per file for now):
//   [0      .. 1024)  file / structure-tag header
//   [1024   .. 1536)  structure-tag root leaf (one entry: tag-name -> 1536)
//   [1536   .. 2560)  sub-tag header
//   [2560   ..  ... )  sub-tag B+tree pages
constexpr std::uint16_t CDX_STRUCT_KEY_LEN     = 10;
constexpr std::uint32_t CDX_STRUCT_ROOT_OFFSET = 1024;
constexpr std::uint32_t CDX_SUB_HEADER_OFFSET  = CDX_STRUCT_ROOT_OFFSET + CDX_PAGE_LEN;
constexpr std::uint32_t CDX_SUB_DATA_BASE      = CDX_SUB_HEADER_OFFSET + CDX_HEADER_LEN;

class CdxIndex final : public IIndex {
public:
    util::Result<void> open(const std::string& path, IndexOpenMode mode) override;

    std::string name()       const override { return tag_name_; }
    std::string expression() const override { return key_expr_; }
    bool        descending() const override { return descend_; }
    bool        unique()     const override { return unique_; }
    std::uint16_t key_length() const override { return key_size_; }

    util::Result<SeekOutcome> seek_first() override;
    util::Result<SeekOutcome> seek_last()  override;
    util::Result<SeekOutcome>
        seek_key(const std::string& key, bool soft) override;
    util::Result<SeekOutcome> next()       override;
    util::Result<SeekOutcome> prev()       override;
    std::string current_key() const override { return current_key_; }

    void invalidate_cursor() override {
        cur_state_ = CurState::Initial;
        cur_index_ = -1;
    }

    // Reset this sub-tag's B+tree to empty (drop the existing
    // root) so a CREATE-INDEX-with-existing-tag can rebuild from
    // scratch on top of an old layout. Old leaves stay on disk
    // (page leak); a future M(cdx-compact) milestone can reclaim.
    util::Result<void> clear_data();

    // Overwrite the unique / descend bits in the on-disk sub-tag
    // header. Used when CREATE INDEX overwrites an existing tag
    // with new options (Clipper / Harbour silent overwrite).
    util::Result<void> set_options(bool unique, bool descend);

    // Same as above plus rewrite the key_size in the sub-tag
    // header — covers the case where a re-CREATE INDEX produces a
    // different key length than the prior version of the tag.
    util::Result<void> set_options(bool unique, bool descend,
                                   std::uint16_t new_key_size);

    util::Result<void> insert(std::uint32_t recno,
                              const std::string& key) override;
    util::Result<void> erase (std::uint32_t recno,
                              const std::string& key) override;
    util::Result<void> flush() override;

    // Build a fresh compound CDX on disk with a single sub-tag.
    static util::Result<CdxIndex>
        create(const std::string& path,
               const std::string& tag_name,
               const std::string& key_expr,
               std::uint16_t      key_size,
               bool               unique,
               bool               descend);

    // Append a new sub-tag to an existing compound CDX. Inserts an
    // entry into the structure-tag root leaf, allocates a fresh
    // CDXTAGHEADER at end-of-file (page-aligned), and returns a
    // CdxIndex positioned on the new sub-tag (root_page_ = 0 until
    // first insert).
    static util::Result<CdxIndex>
        add_tag(const std::string& path,
                const std::string& tag_name,
                const std::string& key_expr,
                std::uint16_t      key_size,
                bool               unique,
                bool               descend);

    // Open a specific sub-tag by name. Empty name selects the first
    // entry in the structure-tag leaf (legacy `open` semantics).
    util::Result<void> open_named(const std::string& path,
                                  IndexOpenMode      mode,
                                  const std::string& tag_name);

    // Enumerate the tag names declared in a compound CDX.
    static util::Result<std::vector<std::string>>
        list_tags(const std::string& path);

    using Page = std::array<std::uint8_t, CDX_PAGE_LEN>;

private:
    util::Result<Page*> get_page_(std::uint32_t offset);
    util::Result<void>  flush_page_(std::uint32_t offset);

    util::Result<std::vector<std::pair<std::string, std::uint32_t>>>
        decode_leaf_(std::uint32_t page_off);

    util::Result<void>
        encode_leaf_(std::uint32_t page_off,
                     const std::vector<std::pair<std::string, std::uint32_t>>& keys,
                     std::uint32_t left_sib,
                     std::uint32_t right_sib);

    // M(cdx-split) — multi-level B+tree split. `insert_into_subtree_`
    // descends from `subtree_root` to find the right leaf for `key`,
    // inserts there, and on leaf/branch overflow returns the data
    // needed for the parent to update its old separator and insert a
    // new one (see PromoteOut). Empty have=false on no-split path.
    struct PromoteOut {
        bool          have            = false;
        std::string   left_max_key;
        std::uint32_t left_max_recno  = 0;
        std::string   right_max_key;
        std::uint32_t right_max_recno = 0;
        std::uint32_t new_right_off   = 0;
        std::uint32_t old_left_off    = 0;
    };
    util::Result<void>
        insert_into_subtree_(std::uint32_t subtree_root,
                              std::uint32_t recno,
                              const std::string& padded_key,
                              PromoteOut& promote);

    static std::uint16_t pick_rec_bits_(std::uint32_t max_rec);

    util::Result<void> rewrite_header_();

    // Allocate a fresh 512-byte page at the next free offset for this
    // CDX file. Compound files host several sub-tags; every sub-tag's
    // CdxIndex shares one allocator keyed by path so their page
    // streams cannot collide.
    std::uint32_t allocate_page_();

    platform::File                          file_;
    std::string                             path_;
    IndexOpenMode                           mode_      = IndexOpenMode::ReadOnly;
    std::uint32_t                           root_page_ = 0;
    std::uint32_t                           free_ptr_  = 0xFFFFFFFFu;
    std::uint32_t                           counter_   = 0;
    std::uint16_t                           key_size_  = 0;
    std::uint8_t                            index_opt_ = 0;
    std::uint8_t                            index_sig_ = 0x01;
    bool                                    unique_    = false;
    bool                                    descend_   = false;
    std::string                             key_expr_;
    std::string                             tag_name_;
    std::uint64_t                           file_size_ = 0;

    // Compound layout: offset of this sub-tag's CDXTAGHEADER (1024B
    // block). All header rewrites write here, not at offset 0.
    std::uint32_t                           sub_header_offset_ = 0;

    std::unordered_map<std::uint32_t, Page> page_cache_;
    std::unordered_map<std::uint32_t, bool> dirty_;

    // Cursor: a single (leaf_page, key_index_in_leaf) plus the cached
    // decoded keys for that leaf.
    std::uint32_t                                                 cur_leaf_      = 0;
    // Cursor logical state. cur_index_ alone can't tell apart
    // "uninitialised" (no seek yet) from "ran off the front via
    // prev()" — both used to leave cur_index_ = -1. Track that
    // explicitly so next() after a prev-off-front can resume at
    // the first key (Clipper SKIP(>0) from BoF semantics).
    // Between: cursor virtually parked between cur_index-1 and
    // cur_index (set after a hard-seek miss when the search key
    // lies strictly between two existing entries). next() returns
    // cur_decoded_[cur_index]; prev() returns cur_decoded_[cur_index-1].
    enum class CurState {
        Initial,       // never sought
        Positioned,    // cur_index_ valid
        BeforeBegin,   // walked off front (prev from idx 0); next -> first
        AfterEnd,      // walked off back (next from last); prev -> last
        Between,       // hard-seek miss between two existing entries
        AfterEndKey    // hard-seek miss > every key; next -> wrap to first
    };
    CurState                                                      cur_state_ = CurState::Initial;
    std::int32_t                                                  cur_index_     = -1;
    std::vector<std::pair<std::string, std::uint32_t>>            cur_decoded_;
    std::string                                                   current_key_;
};

} // namespace openads::drivers::cdx
