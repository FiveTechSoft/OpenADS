#pragma once

#include "drivers/index_trait.h"
#include "platform/file.h"

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace openads::drivers::ntx {

constexpr std::uint16_t NTX_PAGE_SIZE = 1024;
constexpr std::uint16_t NTX_MAX_KEY   = 256;

// Harbour DBFNTX index lock position. DBFNTX defaults to the
// DB_DBFLOCK_CLIPPER scheme, whose index lock is 1 byte at
// IDX_LOCKPOS_CLIPPER = 1000000000 on the .ntx (IDX_LOCKPOOL_CLIPPER = 0,
// so readers and writers share the same single byte). Harbour takes it
// SHARED around every read and EXCLUSIVE around every write, re-reading
// the header (version @2, root @4, next @8) after each lock and dropping
// its page buffers when anything changed; each header save bumps the
// 16-bit version. OpenADS follows the same protocol: exclusive from the
// first mutation of a batch until flush(), shared (best-effort) around
// header refresh + cache rebuild, version bumped on every flushed batch.
// Before this, OpenADS took no .ntx lock at all (roadmap debt "NTX: sin
// lock de archivo de índice interproceso").
constexpr std::uint64_t NTX_LOCK_OFF = 1000000000ULL;

// RAII join handle for the process-wide per-file NTX lock registry.
// Several NtxIndex instances (workareas) in one process can point at the
// same .ntx; the OS byte lock is taken once per file per process and
// in-process access is arbitrated by the registry (POSIX fcntl locks do
// not separate handles of one process, and Win32 LockFileEx makes a
// second handle of the same process fail). Movable, not copyable.
struct NtxWriteLockJoin {
    std::string path;
    bool        joined = false;

    NtxWriteLockJoin() = default;
    NtxWriteLockJoin(const NtxWriteLockJoin&) = delete;
    NtxWriteLockJoin& operator=(const NtxWriteLockJoin&) = delete;
    NtxWriteLockJoin(NtxWriteLockJoin&& o) noexcept
        : path(std::move(o.path)), joined(o.joined) { o.joined = false; }
    NtxWriteLockJoin& operator=(NtxWriteLockJoin&& o) noexcept {
        if (this != &o) {
            release();
            path   = std::move(o.path);
            joined = o.joined;
            o.joined = false;
        }
        return *this;
    }
    ~NtxWriteLockJoin() { release(); }

    // Defined in ntx_index.cpp (process-wide registry lives there).
    void release() noexcept;
};

class NtxIndex final : public IIndex {
public:
    util::Result<void> open(const std::string& path, IndexOpenMode mode) override;

    std::string name()       const override { return tag_name_; }
    std::string expression() const override { return key_expr_; }
    std::string file_path()  const override { return file_path_; }
    bool        descending() const override { return descend_; }
    bool        unique()     const override { return unique_; }
    std::uint16_t key_length() const override { return key_size_; }

    // A numeric NTX index (pinned via set_numeric_format) stores its keys in
    // the native zero-padded / byte-complemented numeric form; the engine
    // consults key_encoding() + key_decimals() to build matching keys.
    KeyEncoding   key_encoding() const override {
        return numeric_ ? KeyEncoding::NtxNumeric : KeyEncoding::Text;
    }
    std::uint16_t key_decimals() const override { return key_dec_; }
    // Restore the numeric mark on a reopened index (the width/decimals are
    // already read back from the NTX header by open()); a freshly created
    // index gets marked via set_numeric_format instead.
    void set_key_encoding(KeyEncoding e) override {
        if (e == KeyEncoding::NtxNumeric) numeric_ = true;
    }

    util::Result<SeekOutcome> seek_first() override;
    util::Result<SeekOutcome> seek_last()  override;
    util::Result<SeekOutcome>
        seek_key(const std::string& key, bool soft) override;
    util::Result<SeekOutcome> next()       override;
    util::Result<SeekOutcome> prev()       override;
    std::string current_key() const override { return current_key_; }

    util::Result<void> insert(std::uint32_t recno,
                               const std::string& key) override;
    util::Result<void> erase (std::uint32_t recno,
                               const std::string& key) override;
    util::Result<void> flush() override;

    // Re-read the header (version / root / next) and drop every cached
    // page when a peer (another process, Harbour DBFNTX or OpenADS)
    // changed the index. Called by Table on absolute repositioning.
    void refresh_from_disk() override;

    // On-disk header version as last seen / written by this instance.
    // Exposed for tests / interop verification.
    std::uint16_t header_version() const { return version_; }
    // True while this instance holds (joins) the exclusive write batch.
    bool write_locked() const { return write_lock_join_.joined; }

    // Logical-position cache for O(1) scrollbar / OrdKeyNo / OrdKeyCount.
    // Mirrors CdxIndex's pos_walk_/pos_map_ but reuses the existing cache_
    // (NTX already walks the full tree into a flat vector).
    const std::vector<std::uint32_t>& ordered_recnos_cached();
    std::uint32_t pos_of_recno_cached(std::uint32_t recno);
    void invalidate_pos_cache() {
        pos_cache_valid_ = false;
        pos_recnos_.clear();
        pos_map_.clear();
    }

    // Pin the key geometry to a numeric field's descriptor (width = field
    // length, dec = field decimals) so the on-disk key matches the native
    // fixed-width, right-justified ASCII form STR(value, width, dec). Must
    // be called on a freshly created, still-empty index (before any
    // insert): it rewrites key_size / item_size / max_keys and stores the
    // decimal count in the NTX header so a native reader reads back the
    // correct geometry. No-op behavioural change for character keys (they
    // never call this). Returns an error if the index already holds data.
    util::Result<void> set_numeric_format(std::uint16_t width,
                                          std::uint16_t dec);

    // Decimal places recorded in the NTX header (0 for character / integer
    // keys). Exposed for tests / interop verification.
    std::uint16_t key_dec() const { return key_dec_; }

    // Static helper used by AdsCreateIndex paths.
    static util::Result<NtxIndex>
        create(const std::string& path,
               const std::string& tag_name,
               const std::string& key_expr,
               std::uint16_t      key_size,
               bool               unique,
               bool               descend);

    using Page = std::array<std::uint8_t, NTX_PAGE_SIZE>;

private:

    struct StackFrame {
        std::uint32_t page;
        std::int32_t  key_index;
    };

    struct CachedKey {
        std::uint32_t recno;
        std::string   key;
    };

    util::Result<void> ensure_cache_();

    // Inter-process lock protocol (see NTX_LOCK_OFF).
    util::Result<void> ensure_write_lock_();
    util::Result<void> reload_header_if_changed_();
    util::Result<void> reload_header_locked_();
    bool               has_dirty_pages_() const;
    util::Result<void> walk_subtree_(std::uint32_t page_off,
                                     std::vector<CachedKey>& out);

    util::Result<Page*> get_page_(std::uint32_t offset);
    util::Result<void>  flush_page_(std::uint32_t offset);
    util::Result<void>  load_current_key_();

    static std::uint16_t get_key_count(const Page& p);
    static void          set_key_count(Page& p, std::uint16_t n);
    static std::uint16_t get_key_offset(const Page& p, std::int32_t i);
    static void          set_key_offset(Page& p, std::int32_t i, std::uint16_t off);
    static std::uint32_t get_left_child(const Page& p, std::int32_t i);
    static std::uint32_t get_recno     (const Page& p, std::int32_t i);
    static const std::uint8_t*
                         get_key_data  (const Page& p, std::int32_t i);
    static void          put_left_child(Page& p, std::int32_t i, std::uint32_t v);
    static void          put_recno     (Page& p, std::int32_t i, std::uint32_t v);

    util::Result<SeekOutcome> descend_leftmost_(std::uint32_t root);
    util::Result<SeekOutcome> descend_rightmost_(std::uint32_t root);
    // Stack-based B-tree descent used by insert()/erase(). NTX stores keys
    // in internal nodes too, so an exact-match key may live in a branch.
    // For erase (and read positioning) we stop at that branch frame. For
    // insert we must NEVER stop at a branch: a B-tree insert always lands
    // on a leaf, then splits upward. Stopping at a branch made insert()
    // write the new entry into an internal node with left_child = 0,
    // orphaning a whole subtree — the cause of incomplete multi-page
    // indexes built over runs of duplicate keys. `descend_to_leaf` (insert)
    // therefore ignores the exact-match branch shortcut and keeps walking
    // down the left child.
    util::Result<SeekOutcome>
        seek_key_for_write_(const std::string& padded_key, bool soft,
                            bool descend_to_leaf = false);

    platform::File                                 file_;
    IndexOpenMode                                  mode_      = IndexOpenMode::ReadOnly;
    std::uint32_t                                  root_page_ = 0;
    std::uint32_t                                  next_avail_= 0;
    std::uint16_t                                  key_size_  = 0;
    std::uint16_t                                  key_dec_   = 0;
    bool                                           numeric_   = false;
    std::uint16_t                                  item_size_ = 0;
    std::uint16_t                                  max_keys_  = 0;
    std::uint16_t                                  half_page_ = 0;
    bool                                           unique_    = false;
    bool                                           descend_   = false;
    std::string                                    key_expr_;
    std::string                                    for_expr_;
    std::string                                    tag_name_;
    std::string                                    file_path_;  // set on open()
    std::string                                    lock_key_;   // canonical path
    std::uint16_t                                  version_   = 0;
    bool                                           hdr_dirty_ = false;
    NtxWriteLockJoin                               write_lock_join_;

    // Per-page cache (read-then-mutate).
    std::unordered_map<std::uint32_t, Page>        page_cache_;
    std::unordered_map<std::uint32_t, bool>        dirty_;

    std::vector<StackFrame>                        stack_;
    std::string                                    current_key_;
    std::uint32_t                                  current_recno_ = 0;

    // M3.8 cache-based traversal. The tree is walked depth-first into
    // a flat vector on first read access; nav methods then walk that
    // vector. Cache is invalidated by any insert / erase.
    std::vector<CachedKey>                         cache_;
    bool                                           cache_dirty_ = true;
    std::int64_t                                   cache_idx_   = -1;

    std::vector<std::uint32_t>                       pos_recnos_;
    std::unordered_map<std::uint32_t, std::uint32_t> pos_map_;
    bool                                             pos_cache_valid_ = false;
};

} // namespace openads::drivers::ntx
