#pragma once

#include "util/result.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace openads::platform {

enum class OpenMode {
    ReadOnly,
    ReadWrite,
    CreateRW,    // create or truncate, read + write
    OpenExisting,// read + write, fail if missing
    // Create or truncate with NO sharing: the open fails (sharing
    // violation on Win32, flock EWOULDBLOCK on POSIX) while any other
    // handle holds the file. Used by table-create so a create over a
    // file that is open elsewhere fails instead of truncating it
    // underneath the openers, and no reader can observe a partially
    // written header.
    CreateExclusive
};

class File {
public:
    File() = default;
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    File(File&&) noexcept;
    File& operator=(File&&) noexcept;
    ~File();

    static util::Result<File> open(const std::string& path, OpenMode mode);

    util::Result<std::size_t> read_at (std::uint64_t offset,
                                       void* buf, std::size_t n);
    util::Result<std::size_t> write_at(std::uint64_t offset,
                                       const void* buf, std::size_t n);
    util::Result<std::uint64_t> size() const;
    util::Result<void> sync();
    util::Result<void> truncate(std::uint64_t size);

    // POSIX only: flock(LOCK_SH | LOCK_NB), held until close. Table
    // drivers take this for the lifetime of an open so that a concurrent
    // exclusive create (OpenMode::CreateExclusive, flock LOCK_EX) fails
    // with EWOULDBLOCK instead of truncating the file underneath the
    // openers. On Win32 this is a no-op: share=0 on the creating handle
    // already provides the exclusion.
    util::Result<void> try_lock_shared();

    // Native handle access for the lock + mmap layers below.
    void*    native_handle() const noexcept { return native_; }
    bool     is_open()       const noexcept { return native_ != nullptr; }

private:
    explicit File(void* native) noexcept : native_(native) {}
    void close_() noexcept;
    void* native_ = nullptr;
};

} // namespace openads::platform
