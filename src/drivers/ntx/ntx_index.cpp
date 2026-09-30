#include "drivers/ntx/ntx_index.h"

#include "platform/lock.h"
#include "platform/time.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

namespace openads::drivers::ntx {

namespace {

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


// ---------------------------------------------------------------------------
// Process-wide per-file NTX lock registry (see NTX_LOCK_OFF in the header).
//
// One entry per canonical .ntx path. In-process access is a reader/writer
// lock layered over the OS byte lock:
//   - a write BATCH has a single owner thread (other threads wait, the same
//     thread's other workareas on the same file join it); the OS byte is
//     held EXCLUSIVE from the first mutation until the last joined flush();
//   - READERS (header refresh + key-cache rebuild) take the OS byte SHARED
//     once for all concurrent in-process readers and never while a batch is
//     open. Writers wait for readers to drain before locking the OS byte.
// Keeping both under one entry matters on POSIX, where fcntl locks merge
// per process: a same-process shared request would otherwise downgrade an
// in-flight exclusive batch, and a reader's unlock would drop it.
struct NtxLockEntry {
    std::mutex                          mu;
    std::condition_variable             cv;
    std::size_t                         users   = 0;   // joined batch refs
    std::thread::id                     owner{};
    std::size_t                         readers = 0;   // in-process readers
    std::optional<platform::ByteLock>   excl;          // held while users>0
    std::optional<platform::ByteLock>   shared;        // held while readers>0
};

std::mutex& ntx_registry_mu() {
    static std::mutex mu;
    return mu;
}

std::unordered_map<std::string, std::shared_ptr<NtxLockEntry>>&
ntx_registry() {
    static std::unordered_map<std::string, std::shared_ptr<NtxLockEntry>> m;
    return m;
}

std::shared_ptr<NtxLockEntry> ntx_entry(const std::string& key, bool create) {
    std::lock_guard<std::mutex> lk(ntx_registry_mu());
    auto& m  = ntx_registry();
    auto  it = m.find(key);
    if (it != m.end()) return it->second;
    if (!create) return nullptr;
    auto e = std::make_shared<NtxLockEntry>();
    m.emplace(key, e);
    return e;
}

std::string ntx_canonical(const std::string& path) {
    try {
        // weakly_canonical resolves symlinks (/tmp -> /private/tmp on macOS)
        // so two spellings of one file share a registry entry.
        return std::filesystem::weakly_canonical(
                   std::filesystem::path(path)).string();
    } catch (...) {
        return path;
    }
}

// Harbour peers lock with FLX_WAIT (no give-up). Writers use a generous
// budget (~10 s of solid contention); readers a small one and fall back to
// the in-memory / unlocked view rather than stall navigation.
util::Result<platform::ByteLock>
acquire_ntx_os_lock(platform::File& f, platform::LockKind kind,
                    int max_retries) {
    util::Error last_err{5035, 0, "NTX index lock not acquired", ""};
    for (int i = 0; i < max_retries; ++i) {
        auto lk = platform::ByteLock::try_acquire(f, NTX_LOCK_OFF, 1, kind);
        if (lk) return std::move(lk).value();
        last_err = lk.error();
        const auto us = 50 + (i * 25);
        std::this_thread::sleep_for(
            std::chrono::microseconds(us > 5000 ? 5000 : us));
    }
    return last_err;
}

constexpr int kNtxWriteRetries = 2000;
constexpr int kNtxReadRetries  = 400;

// RAII in-process reader registration. `locked` is true when the OS byte
// is held shared for this reader (directly or through a concurrent
// in-process reader); false means "proceed best-effort without a lock"
// (a batch is open in this process, or the peer never released).
class NtxReadGuard {
public:
    NtxReadGuard(const std::string& key, platform::File& f) {
        e_ = ntx_entry(key, true);
        std::lock_guard<std::mutex> elk(e_->mu);
        if (e_->users > 0) { e_.reset(); return; }   // batch open: defer
        if (e_->readers == 0) {
            auto l = acquire_ntx_os_lock(f, platform::LockKind::Shared,
                                         kNtxReadRetries);
            if (!l) { e_.reset(); return; }          // best-effort fallback
            e_->shared = std::move(l).value();
        }
        ++e_->readers;
        locked_ = true;
    }
    ~NtxReadGuard() {
        if (!e_ || !locked_) return;
        try {
            std::lock_guard<std::mutex> elk(e_->mu);
            if (e_->readers > 0 && --e_->readers == 0) {
                e_->shared.reset();
                e_->cv.notify_all();
            }
        } catch (...) {
        }
    }
    NtxReadGuard(const NtxReadGuard&) = delete;
    NtxReadGuard& operator=(const NtxReadGuard&) = delete;

    bool locked() const noexcept { return locked_; }

private:
    std::shared_ptr<NtxLockEntry> e_;
    bool locked_ = false;
};

} // namespace

void NtxWriteLockJoin::release() noexcept {
    if (!joined) return;
    joined = false;
    try {
        auto e = ntx_entry(path, false);
        if (!e) return;
        std::lock_guard<std::mutex> elk(e->mu);
        if (e->users > 0 && --e->users == 0) {
            e->owner = std::thread::id{};
            e->excl.reset();          // let peers (other processes) in
            e->cv.notify_all();
        }
    } catch (...) {
        // noexcept: a registry hiccup must never throw through a dtor.
    }
}

bool NtxIndex::has_dirty_pages_() const {
    for (const auto& kv : dirty_) {
        if (kv.second) return true;
    }
    return false;
}

util::Result<void> NtxIndex::ensure_write_lock_() {
    if (write_lock_join_.joined) return {};
    if (lock_key_.empty()) lock_key_ = ntx_canonical(file_path_);
    auto e = ntx_entry(lock_key_, true);
    const auto self = std::this_thread::get_id();
    // Bound our in-process wait (Harbour peers wait forever): long enough
    // for a big REINDEX on a peer thread, short enough to surface a
    // deadlock between two workareas writing two files in opposite order.
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(60);
    {
        std::unique_lock<std::mutex> elk(e->mu);
        for (;;) {
            if (e->users > 0 && e->owner == self) {
                ++e->users;             // same thread, another workarea
                break;
            }
            if (e->users == 0 && e->readers == 0) {
                // Take the OS byte while holding e->mu so no in-process
                // reader or writer can race us onto the same region.
                auto l = acquire_ntx_os_lock(file_,
                                             platform::LockKind::Exclusive,
                                             kNtxWriteRetries);
                if (!l) return l.error();
                e->excl  = std::move(l).value();
                e->owner = self;
                e->users = 1;
                break;
            }
            if (e->cv.wait_until(elk, deadline) == std::cv_status::timeout &&
                !(e->users == 0 && e->readers == 0) &&
                !(e->users > 0 && e->owner == self)) {
                return util::Error{5012, 0,
                    "NTX write lock: in-process wait timed out", file_path_};
            }
        }
    }
    write_lock_join_.path   = lock_key_;
    write_lock_join_.joined = true;
    // We may have waited behind a peer's batch: adopt its tree before
    // mutating (Harbour hb_ntxIndexLockWrite + hb_ntxIndexHeaderRead).
    if (has_dirty_pages_()) return {};
    return reload_header_locked_();
}

util::Result<void> NtxIndex::reload_header_locked_() {
    std::uint8_t h[12]{};
    auto got = file_.read_at(0, h, sizeof(h));
    if (!got) return got.error();
    if (got.value() < sizeof(h)) {
        return util::Error{6106, 0, "NTX header truncated on refresh",
                           file_path_};
    }
    const std::uint16_t ver  = read_u16_le(h + 2);
    const std::uint32_t root = read_u32_le(h + 4);
    const std::uint32_t next = read_u32_le(h + 8);
    if (ver == version_ && root == root_page_ && next == next_avail_) {
        return {};
    }
    // Peer rewrite: drop every cached page / key so the next access
    // re-reads from disk (hb_ntxDiscardBuffers).
    page_cache_.clear();
    dirty_.clear();
    stack_.clear();
    version_    = ver;
    root_page_  = root;
    next_avail_ = next;
    cache_.clear();
    cache_dirty_ = true;
    cache_idx_   = -1;
    invalidate_pos_cache();
    return {};
}

util::Result<void> NtxIndex::reload_header_if_changed_() {
    // Our own un-flushed batch owns the tree: keep local state.
    if (has_dirty_pages_()) return {};
    // Holding the batch (exclusive) already: the header is ours to read.
    if (write_lock_join_.joined) return reload_header_locked_();
    if (lock_key_.empty()) lock_key_ = ntx_canonical(file_path_);
    NtxReadGuard g(lock_key_, file_);
    if (!g.locked()) {
        // An in-process batch is open, or a peer held the byte past our
        // read budget: keep the current, consistent in-memory snapshot;
        // the next call picks the change up.
        return {};
    }
    return reload_header_locked_();
}

void NtxIndex::refresh_from_disk() {
    (void)reload_header_if_changed_();
}

std::uint16_t NtxIndex::get_key_count(const Page& p) {
    return read_u16_le(p.data());
}

void NtxIndex::set_key_count(Page& p, std::uint16_t n) {
    write_u16_le(p.data(), n);
}

std::uint16_t NtxIndex::get_key_offset(const Page& p, std::int32_t i) {
    return read_u16_le(p.data() + 2 + i * 2);
}

void NtxIndex::set_key_offset(Page& p, std::int32_t i, std::uint16_t off) {
    write_u16_le(p.data() + 2 + i * 2, off);
}

std::uint32_t NtxIndex::get_left_child(const Page& p, std::int32_t i) {
    return read_u32_le(p.data() + get_key_offset(p, i));
}

std::uint32_t NtxIndex::get_recno(const Page& p, std::int32_t i) {
    return read_u32_le(p.data() + get_key_offset(p, i) + 4);
}

const std::uint8_t* NtxIndex::get_key_data(const Page& p, std::int32_t i) {
    return p.data() + get_key_offset(p, i) + 8;
}

void NtxIndex::put_left_child(Page& p, std::int32_t i, std::uint32_t v) {
    write_u32_le(p.data() + get_key_offset(p, i), v);
}

void NtxIndex::put_recno(Page& p, std::int32_t i, std::uint32_t v) {
    write_u32_le(p.data() + get_key_offset(p, i) + 4, v);
}

util::Result<NtxIndex::Page*> NtxIndex::get_page_(std::uint32_t offset) {
    auto it = page_cache_.find(offset);
    if (it != page_cache_.end()) return &it->second;
    Page p{};
    auto got = file_.read_at(offset, p.data(), p.size());
    if (!got) return got.error();
    if (got.value() < p.size()) {
        return util::Error{6106, 0, "short read on NTX page", ""};
    }
    auto [ins, _] = page_cache_.emplace(offset, p);
    return &ins->second;
}

util::Result<void> NtxIndex::flush_page_(std::uint32_t offset) {
    auto it = page_cache_.find(offset);
    if (it == page_cache_.end()) return {};
    auto wrote = file_.write_at(offset, it->second.data(), it->second.size());
    if (!wrote) return wrote.error();
    if (wrote.value() != it->second.size()) {
        return util::Error{5000, 0, "short write on NTX page", ""};
    }
    dirty_[offset] = false;
    return {};
}

util::Result<void> NtxIndex::open(const std::string& path, IndexOpenMode mode) {
    mode_ = mode;
    file_path_ = path;
    auto fres = platform::File::open(path, map_mode(mode));
    if (!fres) return fres.error();
    file_ = std::move(fres).value();

    Page hdr{};
    auto got = file_.read_at(0, hdr.data(), hdr.size());
    if (!got) return got.error();
    if (got.value() < NTX_PAGE_SIZE) {
        return util::Error{6106, 0, "NTX header truncated", path};
    }

    version_    = read_u16_le(hdr.data() + 2);
    lock_key_   = ntx_canonical(path);
    root_page_  = read_u32_le(hdr.data() + 4);
    next_avail_ = read_u32_le(hdr.data() + 8);
    item_size_  = read_u16_le(hdr.data() + 12);
    key_size_   = read_u16_le(hdr.data() + 14);
    key_dec_    = read_u16_le(hdr.data() + 16);
    max_keys_   = read_u16_le(hdr.data() + 18);
    half_page_  = read_u16_le(hdr.data() + 20);
    key_expr_   = trim_nul(hdr.data() + 22, 256);
    unique_     = hdr[278] != 0;
    descend_    = hdr[280] != 0;
    for_expr_   = trim_nul(hdr.data() + 282, 256);
    tag_name_   = trim_nul(hdr.data() + 538, 12);

    return {};
}

util::Result<void> NtxIndex::load_current_key_() {
    if (stack_.empty()) {
        current_key_.clear();
        current_recno_ = 0;
        return {};
    }
    const auto& frame = stack_.back();
    auto page = get_page_(frame.page);
    if (!page) return page.error();
    Page& p = *page.value();
    current_recno_ = get_recno(p, frame.key_index);
    current_key_.assign(reinterpret_cast<const char*>(get_key_data(p, frame.key_index)),
                        key_size_);
    return {};
}

util::Result<SeekOutcome>
NtxIndex::descend_leftmost_(std::uint32_t root) {
    stack_.clear();
    if (root == 0) return SeekOutcome{SeekHit::AfterEnd, 0, false};
    std::uint32_t cur = root;
    while (cur != 0) {
        auto page = get_page_(cur);
        if (!page) return page.error();
        Page& p = *page.value();
        std::uint16_t kc = get_key_count(p);
        if (kc == 0) {
            return SeekOutcome{SeekHit::AfterEnd, 0, false};
        }
        std::uint32_t child = get_left_child(p, 0);
        if (child == 0) {
            stack_.push_back({cur, 0});
            if (auto r = load_current_key_(); !r) return r.error();
            return SeekOutcome{SeekHit::Exact, current_recno_, true};
        }
        stack_.push_back({cur, 0});
        cur = child;
    }
    return SeekOutcome{SeekHit::AfterEnd, 0, false};
}

util::Result<SeekOutcome>
NtxIndex::descend_rightmost_(std::uint32_t root) {
    stack_.clear();
    if (root == 0) return SeekOutcome{SeekHit::AfterEnd, 0, false};
    std::uint32_t cur = root;
    while (cur != 0) {
        auto page = get_page_(cur);
        if (!page) return page.error();
        Page& p = *page.value();
        std::uint16_t kc = get_key_count(p);
        if (kc == 0) {
            return SeekOutcome{SeekHit::AfterEnd, 0, false};
        }
        std::uint32_t child = get_left_child(p, kc);
        if (child == 0) {
            stack_.push_back({cur, kc - 1});
            if (auto r = load_current_key_(); !r) return r.error();
            return SeekOutcome{SeekHit::Exact, current_recno_, true};
        }
        stack_.push_back({cur, kc});
        cur = child;
    }
    return SeekOutcome{SeekHit::AfterEnd, 0, false};
}

util::Result<void>
NtxIndex::walk_subtree_(std::uint32_t page_off,
                        std::vector<CachedKey>& out) {
    if (page_off == 0) return {};
    auto pg = get_page_(page_off);
    if (!pg) return pg.error();
    Page p_copy = *pg.value();   // copy: nested get_page_ may invalidate refs
    Page& p = p_copy;
    std::uint16_t kc = get_key_count(p);
    if (kc == 0) return {};

    std::uint32_t lc0 = get_left_child(p, 0);
    if (lc0 == 0) {
        for (std::int32_t i = 0; i < kc; ++i) {
            CachedKey ck;
            ck.recno = get_recno(p, i);
            ck.key.assign(reinterpret_cast<const char*>(get_key_data(p, i)),
                          key_size_);
            out.push_back(std::move(ck));
        }
        return {};
    }
    for (std::int32_t i = 0; i < kc; ++i) {
        if (auto r = walk_subtree_(get_left_child(p, i), out); !r) {
            return r.error();
        }
        CachedKey ck;
        ck.recno = get_recno(p, i);
        ck.key.assign(reinterpret_cast<const char*>(get_key_data(p, i)),
                      key_size_);
        out.push_back(std::move(ck));
    }
    if (auto r = walk_subtree_(get_left_child(p, kc), out); !r) {
        return r.error();
    }
    return {};
}

util::Result<void> NtxIndex::ensure_cache_() {
    if (!cache_dirty_) return {};
    // Rebuild under the shared index lock (hb_ntxIndexLockRead) so a peer
    // writer cannot interleave page writes with our walk, and adopt any
    // peer change first. Skipped while we own dirty pages / the batch.
    std::optional<NtxReadGuard> rg;
    if (!has_dirty_pages_() && !write_lock_join_.joined) {
        if (lock_key_.empty()) lock_key_ = ntx_canonical(file_path_);
        rg.emplace(lock_key_, file_);
        if (rg->locked()) {
            if (auto r = reload_header_locked_(); !r) return r.error();
        }
    }
    cache_.clear();
    if (root_page_ != 0) {
        if (auto r = walk_subtree_(root_page_, cache_); !r) return r.error();
    }
    cache_dirty_ = false;
    cache_idx_   = -1;
    return {};
}

util::Result<SeekOutcome> NtxIndex::seek_first() {
    if (auto r = reload_header_if_changed_(); !r) return r.error();
    if (auto r = ensure_cache_(); !r) return r.error();
    if (cache_.empty()) return SeekOutcome{SeekHit::AfterEnd, 0, false};
    cache_idx_     = 0;
    current_recno_ = cache_[0].recno;
    current_key_   = cache_[0].key;
    return SeekOutcome{SeekHit::Exact, current_recno_, true};
}

util::Result<SeekOutcome> NtxIndex::seek_last() {
    if (auto r = reload_header_if_changed_(); !r) return r.error();
    if (auto r = ensure_cache_(); !r) return r.error();
    if (cache_.empty()) return SeekOutcome{SeekHit::AfterEnd, 0, false};
    cache_idx_     = static_cast<std::int64_t>(cache_.size() - 1);
    current_recno_ = cache_.back().recno;
    current_key_   = cache_.back().key;
    return SeekOutcome{SeekHit::Exact, current_recno_, true};
}

util::Result<SeekOutcome>
NtxIndex::seek_key_for_write_(const std::string& padded, bool soft,
                             bool descend_to_leaf) {
    stack_.clear();
    if (root_page_ == 0) return SeekOutcome{SeekHit::AfterEnd, 0, false};
    std::uint32_t cur = root_page_;
    bool found_exact = false;
    while (cur != 0) {
        auto page = get_page_(cur);
        if (!page) return page.error();
        Page& p = *page.value();
        std::uint16_t kc = get_key_count(p);
        std::int32_t i = 0;
        while (i < kc) {
            const std::uint8_t* kdata = get_key_data(p, i);
            int cmp = std::memcmp(padded.data(), kdata, key_size_);
            if (cmp == 0) { found_exact = true; break; }
            if (cmp < 0)  break;
            ++i;
        }
        std::uint32_t child = get_left_child(p, i);
        if (child == 0) {
            if (i >= kc) {
                if (!soft) return SeekOutcome{SeekHit::AfterEnd, 0, false};
                if (kc == 0) {
                    // Empty-but-rooted leaf: reindex()/erase can clear every
                    // key without resetting root_page_, leaving a 0-key root.
                    // That is a valid insertion target — hand insert() the
                    // leaf frame so it places the first key here, instead of
                    // returning an empty descent stack that insert() rejects
                    // with 5004 ("empty stack post-seek").
                    stack_.push_back({cur, 0});
                    return SeekOutcome{SeekHit::AfterEnd, 0, false};
                }
                stack_.push_back({cur, kc - 1});
                if (auto r = load_current_key_(); !r) return r.error();
                return SeekOutcome{SeekHit::AfterKey, current_recno_, true};
            }
            stack_.push_back({cur, i});
            if (auto r = load_current_key_(); !r) return r.error();
            return SeekOutcome{found_exact ? SeekHit::Exact : SeekHit::AfterKey,
                               current_recno_, true};
        }
        stack_.push_back({cur, i});
        if (found_exact && !descend_to_leaf) {
            // erase / read positioning: the key lives in this internal node.
            if (auto r = load_current_key_(); !r) return r.error();
            return SeekOutcome{SeekHit::Exact, current_recno_, true};
        }
        // insert (descend_to_leaf): an exact match in a branch must NOT stop
        // the descent — keep walking down so the new key is placed in a leaf.
        cur = child;
    }
    return SeekOutcome{SeekHit::AfterEnd, 0, false};
}

util::Result<SeekOutcome>
NtxIndex::seek_key(const std::string& key, bool soft) {
    if (auto r = reload_header_if_changed_(); !r) return r.error();
    if (auto r = ensure_cache_(); !r) return r.error();
    if (cache_.empty()) return SeekOutcome{SeekHit::AfterEnd, 0, false};
    std::string padded = key;
    if (padded.size() < key_size_) padded.append(key_size_ - padded.size(), ' ');
    else if (padded.size() > key_size_) padded.resize(key_size_);

    for (std::size_t i = 0; i < cache_.size(); ++i) {
        int cmp = cache_[i].key.compare(padded);
        if (cmp == 0) {
            cache_idx_     = static_cast<std::int64_t>(i);
            current_recno_ = cache_[i].recno;
            current_key_   = cache_[i].key;
            return SeekOutcome{SeekHit::Exact, current_recno_, true};
        }
        if (cmp > 0) {
            if (!soft) return SeekOutcome{SeekHit::AfterEnd, 0, false};
            cache_idx_     = static_cast<std::int64_t>(i);
            current_recno_ = cache_[i].recno;
            current_key_   = cache_[i].key;
            return SeekOutcome{SeekHit::AfterKey, current_recno_, true};
        }
    }
    if (!soft) return SeekOutcome{SeekHit::AfterEnd, 0, false};
    cache_idx_     = static_cast<std::int64_t>(cache_.size() - 1);
    current_recno_ = cache_.back().recno;
    current_key_   = cache_.back().key;
    return SeekOutcome{SeekHit::AfterKey, current_recno_, true};
}

util::Result<SeekOutcome> NtxIndex::next() {
    if (auto r = ensure_cache_(); !r) return r.error();
    if (cache_idx_ < 0) return SeekOutcome{SeekHit::AfterEnd, 0, false};
    std::int64_t nxt = cache_idx_ + 1;
    if (nxt >= static_cast<std::int64_t>(cache_.size())) {
        cache_idx_ = -1;
        return SeekOutcome{SeekHit::AfterEnd, 0, false};
    }
    cache_idx_     = nxt;
    current_recno_ = cache_[static_cast<std::size_t>(nxt)].recno;
    current_key_   = cache_[static_cast<std::size_t>(nxt)].key;
    return SeekOutcome{SeekHit::Exact, current_recno_, true};
}

util::Result<SeekOutcome> NtxIndex::prev() {
    if (auto r = ensure_cache_(); !r) return r.error();
    if (cache_idx_ <= 0) {
        cache_idx_ = -1;
        return SeekOutcome{SeekHit::BeforeBegin, 0, false};
    }
    --cache_idx_;
    current_recno_ = cache_[static_cast<std::size_t>(cache_idx_)].recno;
    current_key_   = cache_[static_cast<std::size_t>(cache_idx_)].key;
    return SeekOutcome{SeekHit::Exact, current_recno_, true};
}

namespace {

// Layout an empty leaf page: key_count = 0, offset table populated with
// slot offsets pointing to consecutive entry slots (so the page is ready
// to grow without recomputing offsets on every insert).
void format_empty_page(NtxIndex::Page& p,
                       std::uint16_t max_keys,
                       std::uint16_t entry_size) {
    std::fill(p.begin(), p.end(), std::uint8_t{0});
    write_u16_le(p.data(), 0);
    std::size_t blocks_start = 2 + static_cast<std::size_t>(max_keys + 1) * 2;
    for (std::uint16_t i = 0; i <= max_keys; ++i) {
        std::uint16_t off = static_cast<std::uint16_t>(
            blocks_start + static_cast<std::size_t>(i) * entry_size);
        write_u16_le(p.data() + 2 + i * 2, off);
    }
}

} // namespace

util::Result<void>
NtxIndex::insert(std::uint32_t recno, const std::string& key) {
    if (mode_ == IndexOpenMode::ReadOnly) {
        return util::Error{5000, 0, "NTX opened read-only", ""};
    }
    if (auto wl = ensure_write_lock_(); !wl) return wl.error();
    hdr_dirty_ = true;
    cache_dirty_ = true;
    invalidate_pos_cache();
    std::string padded = key;
    if (padded.size() < key_size_) padded.append(key_size_ - padded.size(), ' ');
    else if (padded.size() > key_size_) padded.resize(key_size_);

    // First insert into an empty index: allocate root leaf.
    if (root_page_ == 0) {
        Page p{};
        format_empty_page(p, max_keys_, item_size_);
        // Place the single key into entry 0.
        std::uint16_t off0 = get_key_offset(p, 0);
        write_u32_le(p.data() + off0,     0);    // left_child
        write_u32_le(p.data() + off0 + 4, recno);
        std::memcpy(p.data() + off0 + 8, padded.data(), key_size_);
        set_key_count(p, 1);

        std::uint32_t new_root = next_avail_;
        next_avail_ += NTX_PAGE_SIZE;
        page_cache_[new_root] = p;
        dirty_[new_root]      = true;
        root_page_            = new_root;

        // Persist header update.
        Page hdr{};
        auto got = file_.read_at(0, hdr.data(), hdr.size());
        if (!got) return got.error();
        write_u32_le(hdr.data() + 4, root_page_);
        write_u32_le(hdr.data() + 8, next_avail_);
        auto wrote = file_.write_at(0, hdr.data(), hdr.size());
        if (!wrote) return wrote.error();
        return flush_page_(new_root);
    }

    // Locate insertion point via stack-based descent (cache is read-only path).
    // descend_to_leaf=true: never stop at an internal-node exact match — a
    // B-tree insert must always reach a leaf (otherwise a duplicate key that
    // was promoted to a branch would be inserted into that branch with a null
    // child, orphaning its subtree).
    auto seek = seek_key_for_write_(padded, true, /*descend_to_leaf=*/true);
    if (!seek) return seek.error();

    if (stack_.empty()) {
        return util::Error{5004, 0, "NTX insert: empty stack post-seek", ""};
    }
    StackFrame leaf_frame = stack_.back();
    auto page = get_page_(leaf_frame.page);
    if (!page) return page.error();
    Page& leaf = *page.value();
    std::uint16_t kc = get_key_count(leaf);

    // Determine insertion index inside the leaf.
    std::int32_t insert_at = leaf_frame.key_index;
    if (kc > 0) {
        // A 0-key leaf has no entry to compare against; insert at slot 0.
        const std::uint8_t* kdata = get_key_data(leaf, insert_at);
        int cmp = std::memcmp(padded.data(), kdata, key_size_);
        if (cmp > 0) ++insert_at;
        else if (cmp == 0 && unique_) {
            return util::Error{5044, 0, "NTX duplicate key", ""};
        }
    }

    if (kc + 1 <= max_keys_) {
        // Simple insert: shift offset entries [insert_at .. kc] right by one.
        std::uint16_t free_off = get_key_offset(leaf, kc);
        for (std::int32_t i = static_cast<std::int32_t>(kc); i > insert_at; --i) {
            std::uint16_t prev = get_key_offset(leaf, i - 1);
            set_key_offset(leaf, i, prev);
        }
        set_key_offset(leaf, insert_at, free_off);
        std::uint16_t new_tail_off = static_cast<std::uint16_t>(
            free_off + item_size_);
        set_key_offset(leaf, kc + 1, new_tail_off);

        write_u32_le(leaf.data() + free_off,     0);
        write_u32_le(leaf.data() + free_off + 4, recno);
        std::memcpy (leaf.data() + free_off + 8, padded.data(), key_size_);

        set_key_count(leaf, kc + 1);
        dirty_[leaf_frame.page] = true;
        return {};
    }

    // Leaf is full — split. The split may need to propagate up through
    // any number of branch levels; M9.10 closes the M3.7 single-level
    // limitation by walking the stack from leaf upward and splitting
    // each parent that is also full.

    // Build a sorted vector of (recno, key) for the kc existing entries
    // plus the new one. Both halves below are leaves so left_child = 0
    // for every promoted entry.
    struct Entry {
        std::uint32_t recno;
        std::string   key;
    };
    std::vector<Entry> all;
    all.reserve(kc + 1);
    for (std::int32_t i = 0; i < kc; ++i) {
        Entry e;
        e.recno = get_recno(leaf, i);
        e.key.assign(reinterpret_cast<const char*>(get_key_data(leaf, i)),
                     key_size_);
        all.push_back(std::move(e));
    }
    Entry inserted;
    inserted.recno = recno;
    inserted.key   = padded;
    all.insert(all.begin() + insert_at, std::move(inserted));

    std::size_t mid = all.size() / 2;

    // Allocate a new sibling page and re-format both halves.
    std::uint32_t left_off  = leaf_frame.page;
    std::uint32_t right_off = next_avail_;
    next_avail_ += NTX_PAGE_SIZE;

    auto fill_leaf = [&](std::uint32_t page_off,
                         const std::vector<Entry>& subset) {
        Page p{};
        format_empty_page(p, max_keys_, item_size_);
        for (std::size_t i = 0; i < subset.size(); ++i) {
            std::uint16_t off = get_key_offset(p, static_cast<std::int32_t>(i));
            write_u32_le(p.data() + off,     0);
            write_u32_le(p.data() + off + 4, subset[i].recno);
            std::memcpy (p.data() + off + 8, subset[i].key.data(), key_size_);
        }
        set_key_count(p, static_cast<std::uint16_t>(subset.size()));
        page_cache_[page_off] = p;
        dirty_[page_off]      = true;
    };

    // Promote all[mid] up. left_half stays in the original page;
    // right_half holds entries strictly after mid.
    Entry separator = all[mid];
    std::vector<Entry> left_half (all.begin(),
        all.begin() + static_cast<std::ptrdiff_t>(mid));
    std::vector<Entry> right_half(
        all.begin() + static_cast<std::ptrdiff_t>(mid) + 1, all.end());
    fill_leaf(left_off,  left_half);
    fill_leaf(right_off, right_half);

    // Walk the stack upward, propagating the split into every full
    // parent. `level` is the index of the parent being modified at
    // each iteration. After the loop either the separator has been
    // absorbed into an existing parent or every level has split and
    // we need to grow a new root above.
    std::uint32_t prop_left  = left_off;
    std::uint32_t prop_right = right_off;
    Entry         prop_sep   = separator;
    bool          need_new_root = false;
    for (std::int32_t level = static_cast<std::int32_t>(stack_.size()) - 2;
         level >= 0; --level) {
        StackFrame parent_frame = stack_[static_cast<std::size_t>(level)];
        auto pp = get_page_(parent_frame.page);
        if (!pp) return pp.error();
        Page& parent = *pp.value();
        std::uint16_t pkc = get_key_count(parent);
        std::int32_t pos = parent_frame.key_index;

        if (pkc + 1 <= max_keys_) {
            // Room in parent: insert (lchild=prop_left, prop_sep) at
            // position `pos` and update the entry now at pos+1 to
            // point its lchild at prop_right.
            std::uint16_t free_off = get_key_offset(parent, pkc);
            // The parent's rightmost child lives in the sentinel slot
            // offset[pkc] (= free_off) — the very slot we are about to
            // reuse for the inserted key. Capture it BEFORE the shift so
            // it can be reinstated as the new sentinel; otherwise a
            // separator inserted anywhere left of the end (pos < pkc, the
            // common case for runs of duplicate keys, which sort to the
            // left subtree) drops the original right subtree, leaving an
            // unreachable branch with a 0 right child.
            std::uint32_t old_rightmost = get_left_child(parent, pkc);
            for (std::int32_t i = static_cast<std::int32_t>(pkc); i > pos; --i) {
                set_key_offset(parent, i, get_key_offset(parent, i - 1));
            }
            set_key_offset(parent, pos, free_off);
            std::uint16_t new_tail = static_cast<std::uint16_t>(
                free_off + item_size_);
            set_key_offset(parent, pkc + 1, new_tail);

            write_u32_le(parent.data() + free_off,     prop_left);
            write_u32_le(parent.data() + free_off + 4, prop_sep.recno);
            std::memcpy (parent.data() + free_off + 8,
                         prop_sep.key.data(), key_size_);
            // The entry that was at pos has moved to pos+1; its
            // left_child should now be prop_right (the new sibling).
            put_left_child(parent, pos + 1, prop_right);
            if (pos < static_cast<std::int32_t>(pkc)) {
                // Inserted left of the end: pos+1 is an interior entry, so
                // the new sentinel must carry the node's original rightmost
                // child. (When pos == pkc the put_left_child above already
                // wrote prop_right into the sentinel.)
                put_left_child(parent, pkc + 1, old_rightmost);
            }

            set_key_count(parent, pkc + 1);
            dirty_[parent_frame.page] = true;
            need_new_root = false;
            break;
        }

        // Parent is full — split it. Build the full sequence of
        // (lchild, recno, key) entries for the parent, insert our new
        // (prop_left, prop_sep) at `pos`, then split into left/right
        // halves around mid. The mid entry's recno+key becomes the
        // grandparent separator; its lchild becomes the new left
        // sibling (the original parent page); the new right sibling
        // becomes a fresh page.
        struct InternalEntry {
            std::uint32_t lchild;
            std::uint32_t recno;
            std::string   key;
        };
        std::vector<InternalEntry> ents;
        ents.reserve(static_cast<std::size_t>(pkc) + 1);
        for (std::int32_t i = 0; i < pkc; ++i) {
            InternalEntry e;
            e.lchild = get_left_child(parent, i);
            e.recno  = get_recno(parent, i);
            e.key.assign(reinterpret_cast<const char*>(get_key_data(parent, i)),
                         key_size_);
            ents.push_back(std::move(e));
        }
        // Capture rightmost child pointer (sentinel at offset[pkc]).
        std::uint32_t right_sentinel = get_left_child(parent, pkc);
        InternalEntry pinsert;
        pinsert.lchild = prop_left;
        pinsert.recno  = prop_sep.recno;
        pinsert.key    = prop_sep.key;
        ents.insert(ents.begin() +
            static_cast<std::ptrdiff_t>(pos), std::move(pinsert));
        // Wire prop_right (the new right sibling produced below us) into the
        // node. If the separator landed before the end, the entry that was at
        // `pos` now lives at pos+1 and takes prop_right as its left child, and
        // the node keeps its original rightmost child. If it landed at the
        // very end (pos == pkc, the common ascending-key case), there is no
        // entry after it: prop_right becomes the node's new rightmost child.
        // Indexing ents[pos+1] in that case would read past the vector.
        std::uint32_t node_rightmost;
        if (static_cast<std::size_t>(pos) + 1 < ents.size()) {
            ents[static_cast<std::size_t>(pos) + 1].lchild = prop_right;
            node_rightmost = right_sentinel;
        } else {
            node_rightmost = prop_right;
        }

        std::size_t pmid = ents.size() / 2;
        std::vector<InternalEntry> p_left (ents.begin(),
            ents.begin() + static_cast<std::ptrdiff_t>(pmid));
        std::vector<InternalEntry> p_right(
            ents.begin() + static_cast<std::ptrdiff_t>(pmid) + 1,
            ents.end());
        InternalEntry psep = ents[pmid];

        // Allocate the new right-sibling parent page.
        std::uint32_t parent_left  = parent_frame.page;
        std::uint32_t parent_right = next_avail_;
        next_avail_ += NTX_PAGE_SIZE;

        auto fill_internal = [&](std::uint32_t page_off,
                                 const std::vector<InternalEntry>& subset,
                                 std::uint32_t rightmost) {
            Page np{};
            format_empty_page(np, max_keys_, item_size_);
            for (std::size_t i = 0; i < subset.size(); ++i) {
                std::uint16_t off = get_key_offset(np,
                                       static_cast<std::int32_t>(i));
                write_u32_le(np.data() + off,     subset[i].lchild);
                write_u32_le(np.data() + off + 4, subset[i].recno);
                std::memcpy (np.data() + off + 8,
                             subset[i].key.data(), key_size_);
            }
            // Sentinel at offset[subset.size()] holds the rightmost
            // child (lchild slot of the would-be next entry).
            std::uint16_t sentinel_off =
                get_key_offset(np, static_cast<std::int32_t>(subset.size()));
            write_u32_le(np.data() + sentinel_off, rightmost);
            set_key_count(np, static_cast<std::uint16_t>(subset.size()));
            page_cache_[page_off] = np;
            dirty_[page_off]      = true;
        };

        // Left half's rightmost child = the lchild of the promoted
        // separator (since p_left contains entries strictly before
        // psep, and the next "boundary" is psep's lchild).
        fill_internal(parent_left,  p_left,  psep.lchild);
        // Right half's rightmost child = the whole node's rightmost child
        // (the original sentinel, or prop_right when the separator was
        // appended at the end).
        fill_internal(parent_right, p_right, node_rightmost);

        prop_left  = parent_left;
        prop_right = parent_right;
        prop_sep   = Entry{psep.recno, psep.key};
        need_new_root = (level == 0);
    }

    if (stack_.size() == 1) {
        need_new_root = true;
    }

    if (need_new_root) {
        Page root{};
        format_empty_page(root, max_keys_, item_size_);
        std::uint16_t off = get_key_offset(root, 0);
        write_u32_le(root.data() + off,     prop_left);
        write_u32_le(root.data() + off + 4, prop_sep.recno);
        std::memcpy (root.data() + off + 8, prop_sep.key.data(), key_size_);
        std::uint16_t tail = get_key_offset(root, 1);
        write_u32_le(root.data() + tail, prop_right);
        set_key_count(root, 1);

        std::uint32_t new_root_off = next_avail_;
        next_avail_ += NTX_PAGE_SIZE;
        page_cache_[new_root_off] = root;
        dirty_[new_root_off]      = true;
        root_page_                = new_root_off;
    }

    // Persist header (root_page_ may have changed; next_avail_ may
    // have advanced regardless).
    Page hdr{};
    auto got = file_.read_at(0, hdr.data(), hdr.size());
    if (!got) return got.error();
    write_u32_le(hdr.data() + 4, root_page_);
    write_u32_le(hdr.data() + 8, next_avail_);
    auto wrote = file_.write_at(0, hdr.data(), hdr.size());
    if (!wrote) return wrote.error();

    return {};
}

util::Result<void>
NtxIndex::erase(std::uint32_t recno, const std::string& key) {
    if (mode_ == IndexOpenMode::ReadOnly) {
        return util::Error{5000, 0, "NTX opened read-only", ""};
    }
    if (auto wl = ensure_write_lock_(); !wl) return wl.error();
    hdr_dirty_ = true;
    cache_dirty_ = true;
    invalidate_pos_cache();
    std::string padded = key;
    if (padded.size() < key_size_) padded.append(key_size_ - padded.size(), ' ');
    else if (padded.size() > key_size_) padded.resize(key_size_);
    auto seek = seek_key_for_write_(padded, false);
    if (!seek) return seek.error();
    if (!seek.value().positioned) {
        return util::Error{5044, 0, "NTX key not found", key};
    }
    if (recno != 0 && current_recno_ != recno) {
        return util::Error{5044, 0, "NTX recno mismatch on erase", ""};
    }

    StackFrame leaf_frame = stack_.back();
    auto page = get_page_(leaf_frame.page);
    if (!page) return page.error();
    Page& leaf = *page.value();
    std::uint16_t kc = get_key_count(leaf);
    std::int32_t at = leaf_frame.key_index;

    // Stash the freed slot offset; shift later offsets left; reinstate
    // the freed offset at the trailing slot.
    std::uint16_t freed = get_key_offset(leaf, at);
    for (std::int32_t i = at; i + 1 < kc + 1; ++i) {
        std::uint16_t nxt = get_key_offset(leaf, i + 1);
        set_key_offset(leaf, i, nxt);
    }
    set_key_offset(leaf, kc - 1, freed);
    set_key_offset(leaf, kc, get_key_offset(leaf, kc));   // tail unchanged

    set_key_count(leaf, kc - 1);
    if (kc - 1 == 0) {
        // The leaf is now empty. The swap-to-tail scheme above leaves
        // offset[0] pointing at the physical slot of the *last-removed*
        // key, not the pristine first slot. A subsequent insert into this
        // empty-but-rooted leaf (the PACK/reindex re-insert handled by the
        // empty-leaf branch of seek_key_for_write_) takes offset[kc] as its
        // free slot and marches the free pointer forward from there — off
        // the end of the 1024-byte page once enough keys are re-inserted,
        // corrupting the heap (surfaces as 6106 "short read on NTX page"
        // and then a crash in flush()). Reset the page to a pristine empty
        // layout so every free-slot offset is valid again.
        format_empty_page(leaf, max_keys_, item_size_);
    }
    dirty_[leaf_frame.page] = true;
    return {};
}

util::Result<void> NtxIndex::flush() {
    // Always drop our batch join on every exit path so a failed flush
    // cannot pin the OS lock and starve every peer.
    struct ReleaseJoin {
        NtxWriteLockJoin& j;
        ~ReleaseJoin() { j.release(); }
    } guard{write_lock_join_};

    // Read-only navigation leaves nothing dirty: write nothing. The old
    // code rewrote every CACHED page here, clean ones included — without
    // any lock that could put stale pages over a peer's fresh writes.
    if (!has_dirty_pages_() && !hdr_dirty_) return {};
    // A batch mutated the tree: write every cached page as before (all of
    // them were re-read under this batch's exclusive lock, so none is
    // stale), then publish the header.
    for (auto& [off, _] : page_cache_) {
        auto r = flush_page_(off);
        if (!r) return r.error();
    }
    // Harbour bumps the 16-bit header version on every header save; peers
    // compare it after taking the index lock and discard their buffers.
    version_ = static_cast<std::uint16_t>(version_ + 1);
    std::uint8_t h[10]{};
    write_u16_le(h + 0, version_);
    write_u32_le(h + 2, root_page_);
    write_u32_le(h + 6, next_avail_);
    auto wrote = file_.write_at(2, h, sizeof(h));
    if (!wrote) return wrote.error();
    hdr_dirty_ = false;
    return file_.sync();
}

util::Result<NtxIndex>
NtxIndex::create(const std::string& path,
                 const std::string& tag_name,
                 const std::string& key_expr,
                 std::uint16_t      key_size,
                 bool               unique,
                 bool               descend) {
    auto fres = platform::File::open(path, platform::OpenMode::CreateRW);
    if (!fres) return fres.error();
    platform::File file = std::move(fres).value();

    // Header (1024 bytes).
    Page hdr{};
    write_u16_le(hdr.data() + 0,  0x0006);              // type
    write_u16_le(hdr.data() + 2,  0x0001);              // version
    write_u32_le(hdr.data() + 4,  0);                   // root (none yet)
    write_u32_le(hdr.data() + 8,  NTX_PAGE_SIZE);       // next_avail
    std::uint16_t item_sz = static_cast<std::uint16_t>(key_size + 8);
    write_u16_le(hdr.data() + 12, item_sz);             // item_size
    write_u16_le(hdr.data() + 14, key_size);            // key_size
    write_u16_le(hdr.data() + 16, 0);                   // key_dec
    // max_keys: floor((NTX_PAGE_SIZE - 2 - 2) / (item_sz + 2)) - 1
    // (2 bytes for kc, 2 bytes per offset slot, item_sz per entry, +1 trail).
    std::uint16_t max_keys = 0;
    if (item_sz + 2 > 0) {
        std::int32_t avail = NTX_PAGE_SIZE - 2;
        std::int32_t per   = item_sz + 2;
        max_keys = static_cast<std::uint16_t>((avail / per) - 1);
        if (max_keys < 4) max_keys = 4;
    }
    write_u16_le(hdr.data() + 18, max_keys);
    write_u16_le(hdr.data() + 20, max_keys / 2);
    std::memcpy(hdr.data() + 22, key_expr.data(),
                std::min<std::size_t>(key_expr.size(), 255));
    hdr[278] = unique  ? 1 : 0;
    hdr[280] = descend ? 1 : 0;
    std::memcpy(hdr.data() + 538, tag_name.data(),
                std::min<std::size_t>(tag_name.size(), 11));

    auto wrote = file.write_at(0, hdr.data(), hdr.size());
    if (!wrote) return wrote.error();
    if (auto s = file.sync(); !s) return s.error();

    NtxIndex ix;
    ix.file_       = std::move(file);
    ix.file_path_  = path;
    ix.lock_key_   = ntx_canonical(path);
    ix.version_    = 0x0001;
    ix.mode_       = IndexOpenMode::Shared;
    ix.root_page_  = 0;
    ix.next_avail_ = NTX_PAGE_SIZE;
    ix.item_size_  = item_sz;
    ix.key_size_   = key_size;
    ix.max_keys_   = max_keys;
    ix.half_page_  = max_keys / 2;
    ix.unique_     = unique;
    ix.descend_    = descend;
    ix.key_expr_   = key_expr;
    ix.tag_name_   = tag_name;
    return ix;
}

util::Result<void>
NtxIndex::set_numeric_format(std::uint16_t width, std::uint16_t dec) {
    if (mode_ == IndexOpenMode::ReadOnly) {
        return util::Error{5000, 0, "NTX opened read-only", ""};
    }
    // Only valid on a still-empty index — changing the key geometry after
    // entries exist would invalidate every stored item.
    if (root_page_ != 0) {
        return util::Error{5000, 0,
            "set_numeric_format on non-empty NTX index", ""};
    }
    if (width == 0) return {};
    // A 1024-byte NTX page must hold the forced minimum of 4 keys; an item is
    // width+8 bytes plus a 2-byte offset slot, so 4*(width+10) must fit in
    // NTX_PAGE_SIZE-2. Beyond width 245 that overflows the page on insert.
    if (width > 245) {
        return util::Error{5000, 0,
            "NTX numeric key width exceeds the page limit (max 245)", ""};
    }

    // Recompute geometry exactly as create() does, but from the field
    // width instead of the probed / default key size.
    std::uint16_t item_sz  = static_cast<std::uint16_t>(width + 8);
    std::uint16_t max_keys = 0;
    {
        std::int32_t avail = NTX_PAGE_SIZE - 2;
        std::int32_t per   = item_sz + 2;
        max_keys = static_cast<std::uint16_t>((avail / per) - 1);
        if (max_keys < 4) max_keys = 4;
    }

    key_size_  = width;
    key_dec_   = dec;
    numeric_   = true;   // keys are the native zero-padded numeric form
    item_size_ = item_sz;
    max_keys_  = max_keys;
    half_page_ = static_cast<std::uint16_t>(max_keys / 2);

    // Patch the on-disk header so a native reader (and our own reopen)
    // sees the field-derived key width + decimals. Read-modify-write the
    // 1024-byte header rather than rebuilding it, to preserve every other
    // field create() wrote (expression, tag name, options, ...).
    if (auto wl = ensure_write_lock_(); !wl) return wl.error();
    struct ReleaseJoin {
        NtxWriteLockJoin& j;
        ~ReleaseJoin() { j.release(); }
    } guard{write_lock_join_};
    Page hdr{};
    auto got = file_.read_at(0, hdr.data(), hdr.size());
    if (!got) return got.error();
    write_u16_le(hdr.data() + 12, item_size_);
    write_u16_le(hdr.data() + 14, key_size_);
    write_u16_le(hdr.data() + 16, key_dec_);
    write_u16_le(hdr.data() + 18, max_keys_);
    write_u16_le(hdr.data() + 20, half_page_);
    auto wrote = file_.write_at(0, hdr.data(), hdr.size());
    if (!wrote) return wrote.error();
    return file_.sync();
}

const std::vector<std::uint32_t>& NtxIndex::ordered_recnos_cached() {
    if (pos_cache_valid_) return pos_recnos_;
    if (cache_dirty_) {
        if (auto r = ensure_cache_(); !r) {
            static const std::vector<std::uint32_t> empty;
            return empty;
        }
    }
    pos_recnos_.reserve(cache_.size());
    pos_recnos_.clear();
    pos_map_.clear();
    std::uint32_t pos = 0;
    for (const auto& ck : cache_) {
        pos_recnos_.push_back(ck.recno);
        pos_map_[ck.recno] = pos++;
    }
    pos_cache_valid_ = true;
    return pos_recnos_;
}

std::uint32_t NtxIndex::pos_of_recno_cached(std::uint32_t recno) {
    (void)ordered_recnos_cached();
    auto it = pos_map_.find(recno);
    return it != pos_map_.end() ? it->second : 0xFFFFFFFFu;
}

} // namespace openads::drivers::ntx
