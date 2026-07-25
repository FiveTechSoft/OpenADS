#pragma once

// ============================================================================
// hbnetio VFS Adapter for OpenADS
// ============================================================================
//
// Maps Harbour's hb_vf*() extended virtual file system API to OpenADS's
// network transport layer. When an hbnetio server is running (e.g. on AWS
// EC2), OpenADS clients can transparently access remote files as if they
// were local, including:
//
//   - File open/close/read/write/seek/truncate
//   - Byte-range locking (exclusive + shared)
//   - Directory operations (exists/make/remove/space)
//   - File attributes and timestamps
//   - Hard/symbolic links
//
// Architecture:
//
//   Harbour App                    OpenADS Server               hbnetio Server
//   ───────────                    ──────────────               ──────────────
//   hb_vfOpen("net:...")  ──TCP──> VfsAdapter::open()  ──RPC──> netio_Server()
//   hb_vfLock(h,0,1,...)  ──TCP──> VfsAdapter::lock()  ──RPC──> hb_fileLock()
//   hb_vfRead(h,buf,n)    ──TCP──> VfsAdapter::read()  ──RPC──> hb_fileRead()
//
// Usage from Harbour:
//   netio_Connect("ec2-host", 2941)
//   USE net:ec2-host:2941:chats/mydata.txt
//   // ... normal file operations, all routed through hbnetio
//
// Usage from OpenADS C++ (this adapter):
//   auto adapter = std::make_unique<VfsAdapter>(transport);
//   auto handle = adapter->open("chats/room1.json", FO_READWRITE);
//   adapter->lock(handle, 0, 1, LockKind::Exclusive, /*wait=*/true);
//   adapter->read(handle, buffer, sizeof(buffer));
//   adapter->unlock(handle, 0, 1);
//   adapter->close(handle);
//
// Copyright 2026 OpenADS Contributors
// Licensed under the same terms as OpenADS (see LICENSE)
// ============================================================================

#include "network/transport.h"
#include "util/result.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace openads::network {

// ---------------------------------------------------------------------------
// VFS constants matching hbnetio's wire protocol
// ---------------------------------------------------------------------------
namespace vfs {
    // Message types (mirrors netio.h NETIO_* constants)
    constexpr std::uint32_t MSG_LOGIN      = 1;
    constexpr std::uint32_t MSG_EXISTS     = 2;
    constexpr std::uint32_t MSG_DELETE     = 3;
    constexpr std::uint32_t MSG_RENAME     = 4;
    constexpr std::uint32_t MSG_COMMIT     = 5;
    constexpr std::uint32_t MSG_SIZE       = 6;
    constexpr std::uint32_t MSG_TRUNC      = 7;
    constexpr std::uint32_t MSG_READAT     = 8;
    constexpr std::uint32_t MSG_WRITEAT    = 9;
    constexpr std::uint32_t MSG_LOCK       = 10;
    constexpr std::uint32_t MSG_UNLOCK     = 11;
    constexpr std::uint32_t MSG_OPEN       = 12;
    constexpr std::uint32_t MSG_CLOSE      = 13;
    constexpr std::uint32_t MSG_ERROR      = 14;
    constexpr std::uint32_t MSG_SYNC       = 15;
    constexpr std::uint32_t MSG_TESTLOCK   = 24;
    constexpr std::uint32_t MSG_READ       = 25;
    constexpr std::uint32_t MSG_WRITE      = 26;
    constexpr std::uint32_t MSG_SEEK       = 27;
    constexpr std::uint32_t MSG_EOF        = 28;
    constexpr std::uint32_t MSG_COPY       = 29;
    constexpr std::uint32_t MSG_DIREXISTS  = 30;
    constexpr std::uint32_t MSG_DIRMAKE    = 31;
    constexpr std::uint32_t MSG_DIRREMOVE  = 32;
    constexpr std::uint32_t MSG_DIRECTORY  = 33;
    constexpr std::uint32_t MSG_DIRSPACE   = 34;
    constexpr std::uint32_t MSG_ATTRGET    = 35;
    constexpr std::uint32_t MSG_ATTRSET    = 36;
    constexpr std::uint32_t MSG_FTIMEGET   = 37;
    constexpr std::uint32_t MSG_FTIMESET   = 38;
    constexpr std::uint32_t MSG_LINK       = 39;
    constexpr std::uint32_t MSG_LINKSYM    = 40;
    constexpr std::uint32_t MSG_LINKREAD   = 41;
    constexpr std::uint32_t MSG_OPEN2      = 43;

    // Connection state
    constexpr std::uint32_t CONNECTED      = 0x4321DEAD;

    // Wire header size (matches hbnetio MSGLEN = 24)
    constexpr std::size_t   MSGLEN         = 24;

    // Login string identifier
    constexpr const char*   LOGINSTRID     = "HarbourFileTcpIpServer";

    // Default port
    constexpr std::uint16_t DEFAULT_PORT   = 2941;

    // Max open files per connection
    constexpr std::uint32_t FILES_MAX      = 8192;

    // Lock flags (from Harbour's hbapifs.h)
    constexpr std::uint16_t FLX_EXCLUSIVE  = 0x0000;  // HB_FLX_EXCLUSIVE
    constexpr std::uint16_t FLX_SHARED     = 0x0100;  // HB_FLX_SHARED
    constexpr std::uint16_t FLX_WAIT       = 0x0200;  // HB_FLX_WAIT

    // File open flags (Harbour FO_* and HB_FO_*)
    constexpr std::uint32_t FO_READ        = 0x0000;
    constexpr std::uint32_t FO_WRITE       = 0x0001;
    constexpr std::uint32_t FO_READWRITE   = 0x0002;
    constexpr std::uint32_t FO_CREAT       = 0x0100;  // HB_FO_CREAT
    constexpr std::uint32_t FO_TRUNC       = 0x0200;  // HB_FO_TRUNC
    constexpr std::uint32_t FO_EXCL        = 0x0400;  // HB_FO_EXCL
} // namespace vfs


// ---------------------------------------------------------------------------
// VfsHandle — opaque handle to a remotely-opened file
// ---------------------------------------------------------------------------
struct VfsHandle {
    std::uint16_t fd = 0;          // server-side file number
    bool          valid = false;
    std::string   path;            // original path (for diagnostics)
};


// ---------------------------------------------------------------------------
// VfsAdapter — maps hb_vf*() operations over an ITransport
// ---------------------------------------------------------------------------
//
// The adapter serialises each VFS operation into the hbnetio wire format,
// sends it over the supplied ITransport, and deserialises the server's
// reply. Thread safety: the adapter serialises all calls through a mutex
// so that multiple Harbour threads sharing one connection don't interleave
// half-written frames. This matches hbnetio's own s_fileConLock() pattern.
//
class VfsAdapter {
public:
    explicit VfsAdapter(std::unique_ptr<ITransport> transport);
    ~VfsAdapter();

    VfsAdapter(const VfsAdapter&) = delete;
    VfsAdapter& operator=(const VfsAdapter&) = delete;

    // -- Connection lifecycle -----------------------------------------------

    // Perform the hbnetio LOGIN handshake. Must be called once after
    // construction and before any file operations.
    util::Result<void> login();

    // Gracefully close all open files and the transport.
    void disconnect() noexcept;

    bool connected() const noexcept { return connected_; }

    // -- File open / close --------------------------------------------------

    util::Result<VfsHandle> open(const std::string& path,
                                 std::uint32_t flags,
                                 const std::string& def_ext = "");

    util::Result<void> close(VfsHandle& h);

    // -- Byte-range lock / unlock -------------------------------------------

    util::Result<bool> lock(VfsHandle& h, std::uint64_t offset,
                            std::uint64_t length, std::uint16_t flags);

    util::Result<void> unlock(VfsHandle& h, std::uint64_t offset,
                              std::uint64_t length);

    util::Result<int> test_lock(VfsHandle& h, std::uint64_t offset,
                                std::uint64_t length, std::uint16_t flags);

    // -- Sequential read / write --------------------------------------------

    util::Result<std::size_t> read(VfsHandle& h, void* buf,
                                   std::size_t n);

    util::Result<std::size_t> write(VfsHandle& h, const void* buf,
                                    std::size_t n);

    // -- Positioned read / write --------------------------------------------

    util::Result<std::size_t> read_at(VfsHandle& h, void* buf,
                                      std::size_t n, std::uint64_t offset);

    util::Result<std::size_t> write_at(VfsHandle& h, const void* buf,
                                       std::size_t n, std::uint64_t offset);

    // -- Seek / Truncate / Size / EOF / Commit ------------------------------

    util::Result<std::int64_t> seek(VfsHandle& h, std::int64_t offset,
                                    std::uint16_t origin);

    util::Result<void> truncate(VfsHandle& h, std::uint64_t offset);

    util::Result<std::int64_t> size(VfsHandle& h);

    util::Result<bool> eof(VfsHandle& h);

    util::Result<void> commit(VfsHandle& h);

    // -- High-level file operations -----------------------------------------

    util::Result<bool> exists(const std::string& path);

    util::Result<void> erase(const std::string& path);

    util::Result<void> rename(const std::string& old_path,
                              const std::string& new_path);

    util::Result<void> copy(const std::string& src, const std::string& dst);

    // -- Directory operations -----------------------------------------------

    util::Result<bool> dir_exists(const std::string& path);

    util::Result<void> dir_make(const std::string& path);

    util::Result<void> dir_remove(const std::string& path);

    // -- Attribute / Timestamp ----------------------------------------------

    util::Result<std::uint32_t> attr_get(const std::string& path);

    util::Result<void> attr_set(const std::string& path, std::uint32_t attr);

    struct Timestamp {
        std::int32_t julian   = 0;
        std::int32_t millisec = 0;
    };

    util::Result<Timestamp> time_get(const std::string& path);

    util::Result<void> time_set(const std::string& path,
                                std::int32_t julian, std::int32_t millisec);

private:
    // Serialise a 24-byte hbnetio message header.
    static void put_header(std::uint8_t* buf, std::uint32_t msg,
                           std::uint32_t extra1 = 0,
                           std::uint32_t extra2 = 0);

    // Read exactly MSGLEN bytes from the transport.
    util::Result<void> recv_header(std::uint8_t* buf);

    // Send a header-only message and wait for reply header.
    util::Result<std::uint32_t> send_simple(std::uint32_t msg,
                                            const void* data,
                                            std::uint32_t len);

    // Send a message and receive a variable-length reply.
    util::Result<std::vector<std::uint8_t>> send_recv(std::uint32_t msg,
                                                      const void* data,
                                                      std::uint32_t len);

    // Low-level: send msg+data, optionally wait for reply.
    util::Result<bool> send_msg(std::uint32_t msg, const void* data,
                                std::uint32_t len, bool wait_reply);

    std::unique_ptr<ITransport> transport_;
    mutable std::mutex          mu_;
    bool                        connected_ = false;
};


// ---------------------------------------------------------------------------
// VfsLockGuard — RAII wrapper for remote file locking
// ---------------------------------------------------------------------------
//
// Mirrors the pattern from hbnetio's __isLocked() function:
//
//   if (hb_vfLock(handle, 0, 1, HB_FLX_EXCLUSIVE + HB_FLX_WAIT))
//   {
//       // ... critical section ...
//       hb_vfUnlock(handle, 0, 1);
//   }
//
// Usage:
//   VfsLockGuard guard(adapter, handle, 0, 1);
//   if (guard.locked()) {
//       // ... exclusive access to file ...
//   }
//   // guard destructor calls unlock()
//
class VfsLockGuard {
public:
    VfsLockGuard(VfsAdapter& adapter, VfsHandle& handle,
                         std::uint64_t offset, std::uint64_t length);
    ~VfsLockGuard();

    VfsLockGuard(const VfsLockGuard&) = delete;
    VfsLockGuard& operator=(const VfsLockGuard&) = delete;

    bool locked() const noexcept { return locked_; }

    // Manually release before destructor (e.g. early exit).
    void release();

private:
    VfsAdapter&  adapter_;
    VfsHandle&   handle_;
    std::uint64_t offset_;
    std::uint64_t length_;
    bool          locked_ = false;
};

} // namespace openads::network
