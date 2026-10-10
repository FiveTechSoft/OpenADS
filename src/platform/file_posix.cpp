#ifndef _WIN32

#include "platform/file.h"
#include "platform/lock.h"

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fcntl.h>
#include <mutex>
#include <vector>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace openads::platform {

namespace {

util::Error os_error(const char* op) {
    util::Error e;
    e.code     = (errno == ENOENT) ? 5103
                 : (errno == EACCES || errno == ETXTBSY ||
                    errno == EAGAIN || errno == EWOULDBLOCK)
                       ? 7040
                       : 5000;
    e.sub_code = errno;
    e.message  = op;
    e.message += ": ";
    e.message += std::strerror(errno);
    return e;
}

// Encode POSIX fds into void* so that the "not open" sentinel (nullptr)
// never collides with a valid fd. fd 0 (stdin) is valid on POSIX and
// can be returned by open() when stdin has been closed. Adding 1
// maps fd 0 → (void*)1, avoiding the collision.
int fd_from_native(void* p) {
    return static_cast<int>(reinterpret_cast<intptr_t>(p) - 1);
}
void* native_from_fd(int fd) {
    return reinterpret_cast<void*>(static_cast<intptr_t>(fd) + 1);
}

struct JailRoot {
    std::string canon;  // realpath() of the configured root
    int         fd = -1;
};

std::mutex            g_jail_mu;
std::vector<JailRoot> g_jail_roots;

struct JailSnapshot {
    std::vector<JailRoot> roots;
    JailSnapshot() = default;
    JailSnapshot(JailSnapshot&& other) noexcept : roots(std::move(other.roots)) {
        other.roots.clear();
    }
    JailSnapshot(const JailSnapshot&) = delete;
    JailSnapshot& operator=(const JailSnapshot&) = delete;
    ~JailSnapshot() {
        for (auto& r : roots) {
            if (r.fd >= 0) ::close(r.fd);
        }
    }
};

JailSnapshot jail_snapshot() {
    JailSnapshot s;
    std::lock_guard<std::mutex> lk(g_jail_mu);
    s.roots.reserve(g_jail_roots.size());
    for (const auto& r : g_jail_roots) {
        int d = ::fcntl(r.fd, F_DUPFD_CLOEXEC, 0);
        if (d >= 0) s.roots.push_back(JailRoot{r.canon, d});
    }
    return s;
}

util::Result<int> open_jailed(const std::string& path, int flags) {
    namespace fs = std::filesystem;
    JailSnapshot snap = jail_snapshot();

    std::error_code ec;
    const fs::path in(path);
    const fs::path abs = in.is_absolute() ? in : fs::absolute(in, ec);
    if (ec) return os_error("open(jail)");
    const std::string norm = abs.string();

    const JailRoot* match = nullptr;
    std::string rel;
    std::size_t prefix_length = 0;
    for (const auto& r : snap.roots) {
        const auto& prefix = r.canon;
        if (prefix == "/") {
            if (!match) { match = &r; rel = norm; }
        } else if (norm.size() > prefix.size() &&
                   norm.compare(0, prefix.size(), prefix) == 0 &&
                   norm[prefix.size()] == '/' && prefix.size() > prefix_length) {
            match = &r; rel = norm.substr(prefix.size() + 1);
            prefix_length = prefix.size();
        }
    }
    if (match == nullptr) {
        errno = EACCES;
        return os_error("open(jail: outside data roots)");
    }

    int dirfd = ::openat(match->fd, ".",
                         O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW);
    if (dirfd < 0) return os_error("open(jail: root)");

    std::vector<std::string> comps;
    std::size_t pos = 0;
    while (pos <= rel.size()) {
        const std::size_t slash = rel.find('/', pos);
        std::string c = rel.substr(
            pos, slash == std::string::npos ? std::string::npos
                                            : slash - pos);
        if (!c.empty() && c != ".") comps.push_back(std::move(c));
        if (slash == std::string::npos) break;
        pos = slash + 1;
    }

    // Pop verified parent handles, never openat("..").
    struct ParentHandles {
        std::vector<int> fds;
        ~ParentHandles() { for (int fd : fds) ::close(fd); }
    } parents;
    std::size_t depth = 0;
    for (std::size_t i = 0; i < comps.size(); ++i) {
        if (comps[i] == "..") {
            if (depth == 0) {
                ::close(dirfd);
                errno = EACCES;
                return os_error("open(jail: '..' escape refused)");
            }
            --depth;
            ::close(dirfd);
            dirfd = parents.fds.back();
            parents.fds.pop_back();
            continue;
        } else {
            ++depth;
        }
        const bool last = (i + 1 == comps.size());
        if (!last) {
            const int next =
                ::openat(dirfd, comps[i].c_str(),
                         O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW);
            if (next < 0) {
                auto e = os_error(errno == ELOOP
                                      ? "open(jail: symlink refused)"
                                      : "open(jail: dir)");
                ::close(dirfd);
                return e;
            }
            parents.fds.push_back(dirfd);
            dirfd = next;
            continue;
        }
        const int fd = ::openat(dirfd, comps[i].c_str(),
                                flags | O_NOFOLLOW | O_CLOEXEC, 0644);
        if (fd < 0) {
            auto e = os_error(errno == ELOOP
                                  ? "open(jail: symlink refused)"
                                  : "open");
            ::close(dirfd);
            return e;
        }
        ::close(dirfd);
        return fd;
    }

    ::close(dirfd);
    errno = ENOENT;
    return os_error("open(jail)");
}

#ifdef __APPLE__
// macOS: flock() and fcntl() byte locks share one lock list, and a held
// flock (even on the very same fd) makes every fcntl byte lock on the
// file fail with EAGAIN (verified with a probe on macos-14: flock SH then
// F_WRLCK -> EAGAIN; flock released -> ok). The engine takes its share
// guard with flock on every open and its append/record locks with fcntl,
// so on macOS appends were refused on tables that were open. Emulate the
// per-open-file-description share guard with an OFD fcntl lock on one
// sentinel byte far from the engine's lock bytes instead: same semantics
// (contends between fds of one process, released on close), but it does
// not collide with the byte locks.
constexpr off_t kShareGuardByte = static_cast<off_t>(0x7FFFFFFFFF00ULL);

int share_guard(int fd, short type) {
    struct flock fl{};
    fl.l_type   = type;
    fl.l_whence = SEEK_SET;
    fl.l_start  = kShareGuardByte;
    fl.l_len    = 1;
    fl.l_pid    = 0;
    return ::fcntl(fd, F_OFD_SETLK, &fl);
}
#else
int share_guard(int fd, short type) {
    return ::flock(fd, type == F_WRLCK ? (LOCK_EX | LOCK_NB)
                     : type == F_RDLCK ? (LOCK_SH | LOCK_NB)
                                       : LOCK_UN);
}
#endif

} // namespace

File::File(File&& other) noexcept : native_(other.native_) {
    other.native_ = nullptr;
}

File& File::operator=(File&& other) noexcept {
    if (this != &other) {
        close_();
        native_ = other.native_;
        other.native_ = nullptr;
    }
    return *this;
}

File::~File() { close_(); }

void File::close_() noexcept {
    if (native_ != nullptr) {
#ifdef __APPLE__
        forget_byte_locks(native_);
#endif
        ::close(fd_from_native(native_));
        native_ = nullptr;
    }
}

util::Result<File> File::open(const std::string& path, OpenMode mode) {
    int flags = 0;
    switch (mode) {
        case OpenMode::ReadOnly:     flags = O_RDONLY; break;
        case OpenMode::ReadWrite:    flags = O_RDWR;   break;
        case OpenMode::CreateRW:     flags = O_RDWR | O_CREAT | O_TRUNC; break;
        case OpenMode::OpenExisting: flags = O_RDWR;   break;
        case OpenMode::CreateExclusive:
            // No O_TRUNC here: the file is only truncated AFTER the
            // flock succeeds, so a create over a file another process
            // holds open fails without destroying its contents.
            flags = O_RDWR | O_CREAT;
            break;
    }
    int fd = -1;
    if (data_jail_active()) {
        auto j = open_jailed(path, flags);
        if (!j) return j.error();
        fd = j.value();
    } else {
        fd = ::open(path.c_str(), flags, 0644);
        if (fd < 0) return os_error("open");
    }
    if (mode == OpenMode::CreateExclusive) {
        // Advisory exclusive lock for the whole open lifetime (released
        // on close). POSIX has no share modes; cooperating OpenADS
        // creators serialise on this so a create over a file another
        // process holds open fails instead of truncating underneath it.
        if (share_guard(fd, F_WRLCK) != 0) {
            util::Error e = os_error("flock");
            ::close(fd);
            return e;
        }
        if (::ftruncate(fd, 0) != 0) {
            util::Error e = os_error("ftruncate");
            ::close(fd);
            return e;
        }
    }
    return File{native_from_fd(fd)};
}

util::Result<std::size_t> File::read_at(std::uint64_t offset,
                                        void* buf, std::size_t n) {
    int fd = fd_from_native(native_);
    ssize_t got;
    do {
        got = ::pread(fd, buf, n, static_cast<off_t>(offset));
    } while (got < 0 && errno == EINTR);
    if (got < 0) return os_error("pread");
    return static_cast<std::size_t>(got);
}

util::Result<std::size_t> File::write_at(std::uint64_t offset,
                                         const void* buf, std::size_t n) {
    int fd = fd_from_native(native_);
    ssize_t wrote;
    do {
        wrote = ::pwrite(fd, buf, n, static_cast<off_t>(offset));
    } while (wrote < 0 && errno == EINTR);
    if (wrote < 0) return os_error("pwrite");
    return static_cast<std::size_t>(wrote);
}

util::Result<std::uint64_t> File::size() const {
    int fd = fd_from_native(native_);
    struct stat st{};
    if (::fstat(fd, &st) != 0) return os_error("fstat");
    return static_cast<std::uint64_t>(st.st_size);
}

util::Result<void> File::sync() {
    int fd = fd_from_native(native_);
    if (::fsync(fd) != 0) return os_error("fsync");
    return {};
}

util::Result<void> File::truncate(std::uint64_t size) {
    int fd = fd_from_native(native_);
    if (::ftruncate(fd, static_cast<off_t>(size)) != 0)
        return os_error("ftruncate");
    return {};
}

util::Result<void> File::try_lock_shared() {
    int fd = fd_from_native(native_);
    // flock locks are per open-file-description, so two fds from separate
    // open() calls in the SAME process still contend — exactly the Win32
    // share=0 semantics the create path relies on. LOCK_NB: callers retry.
    if (share_guard(fd, F_RDLCK) != 0) return os_error("flock");
    return {};
}

void File::release_lock_shared() {
    if (native_ == nullptr) return;
    int fd = fd_from_native(native_);
    share_guard(fd, F_UNLCK);
}

util::Result<void> File::set_data_jail(std::vector<std::string> roots) {
    std::vector<JailRoot> anchored;
    for (const auto& r : roots) {
        if (r.empty()) continue;
        char* c = ::realpath(r.c_str(), nullptr);
        if (c == nullptr) {
            util::Error e;
            e.code     = 5000;
            e.sub_code = errno;
            e.message  = "set_data_jail: cannot resolve root '" + r +
                         "': " + std::strerror(errno);
            for (auto& a : anchored) ::close(a.fd);
            return e;
        }
        std::string canon(c);
        ::free(c);
        while (canon.size() > 1 && canon.back() == '/') canon.pop_back();
        int fd = ::open("/", O_RDONLY | O_CLOEXEC | O_DIRECTORY);
        std::error_code ec;
        const auto root_path = std::filesystem::absolute(r, ec);
        if (ec) { if (fd >= 0) ::close(fd); fd = -1; errno = EINVAL; }
        for (const auto& part : root_path.relative_path()) {
            if (fd < 0) break;
            if (part == "..") { ::close(fd); fd = -1; errno = EACCES; break; }
            const int next = ::openat(fd, part.c_str(),
                O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW);
            const int saved = errno;
            ::close(fd); fd = next; errno = saved;
        }
        if (fd < 0) {
            util::Error e;
            e.code     = 5000;
            e.sub_code = errno;
            e.message  = "set_data_jail: cannot anchor root '" + r +
                         "': " + std::strerror(errno);
            for (auto& a : anchored) ::close(a.fd);
            return e;
        }
        anchored.push_back(JailRoot{std::move(canon), fd});
    }
    std::vector<JailRoot> old;
    {
        std::lock_guard<std::mutex> lk(g_jail_mu);
        old = std::move(g_jail_roots);
        g_jail_roots = std::move(anchored);
    }
    for (auto& o : old) ::close(o.fd);
    return {};
}

void File::clear_data_jail() noexcept {
    std::vector<JailRoot> old;
    {
        std::lock_guard<std::mutex> lk(g_jail_mu);
        old = std::move(g_jail_roots);
        g_jail_roots.clear();
    }
    for (auto& o : old) ::close(o.fd);
}

bool File::data_jail_active() noexcept {
    std::lock_guard<std::mutex> lk(g_jail_mu);
    return !g_jail_roots.empty();
}

} // namespace openads::platform

#endif // !_WIN32
