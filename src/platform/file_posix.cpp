#ifndef _WIN32

#include "platform/file.h"

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace openads::platform {

namespace {

util::Error os_error(const char* op) {
    util::Error e;
    e.code     = (errno == ENOENT) ? 5103 : 5000;
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
    int fd = ::open(path.c_str(), flags, 0644);
    if (fd < 0) return os_error("open");
    if (mode == OpenMode::CreateExclusive) {
        // Advisory exclusive lock for the whole open lifetime (released
        // on close). POSIX has no share modes; cooperating OpenADS
        // creators serialise on this so a create over a file another
        // process holds open fails instead of truncating underneath it.
        if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
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
    if (::flock(fd, LOCK_SH | LOCK_NB) != 0) return os_error("flock");
    return {};
}

} // namespace openads::platform

#endif // !_WIN32
