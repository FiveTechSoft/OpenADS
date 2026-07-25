// ============================================================================
// VFS Adapter Implementation
// ============================================================================
//
// This file implements the hbnetio wire protocol for OpenADS.
// Each method serialises a 24-byte header + optional payload into the
// same byte layout that harbour/contrib/hbnetio/netiocli.c uses, sends
// it over the ITransport, and deserialises the server reply.
//
// Wire format (all little-endian):
//   bytes 0..3  : message type (uint32 LE)
//   bytes 4..23 : message-specific fields (20 bytes)
//   followed by optional payload data
//
// Copyright 2026 OpenADS Contributors
// ============================================================================

#include "vfs_adapter.h"

#include <cstring>
#include <algorithm>

namespace openads::network {

// ===========================================================================
// Helpers for little-endian wire encoding
// ===========================================================================

namespace {

inline void put_u16(std::uint8_t* p, std::uint16_t v) {
    p[0] = static_cast<std::uint8_t>(v & 0xFF);
    p[1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
}

inline void put_u32(std::uint8_t* p, std::uint32_t v) {
    p[0] = static_cast<std::uint8_t>(v & 0xFF);
    p[1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
    p[2] = static_cast<std::uint8_t>((v >> 16) & 0xFF);
    p[3] = static_cast<std::uint8_t>((v >> 24) & 0xFF);
}

inline void put_u64(std::uint8_t* p, std::uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        p[i] = static_cast<std::uint8_t>((v >> (i * 8)) & 0xFF);
    }
}

inline std::uint16_t get_u16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

inline std::uint32_t get_u32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(
        p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}

inline std::uint64_t get_u64(const std::uint8_t* p) {
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i) {
        v |= static_cast<std::uint64_t>(p[i]) << (i * 8);
    }
    return v;
}

} // anonymous namespace


// ===========================================================================
// VfsAdapter
// ===========================================================================

VfsAdapter::VfsAdapter(std::unique_ptr<ITransport> transport)
    : transport_(std::move(transport)) {}

VfsAdapter::~VfsAdapter() {
    disconnect();
}

void VfsAdapter::disconnect() noexcept {
    std::lock_guard<std::mutex> lock(mu_);
    connected_ = false;
    if (transport_) {
        transport_->close();
    }
}

// ---------------------------------------------------------------------------
// LOGIN handshake
// ---------------------------------------------------------------------------
util::Result<void> VfsAdapter::login() {
    std::lock_guard<std::mutex> lock(mu_);

    // Build LOGIN message: [type=1][len=strlen][padding...]
    std::uint8_t hdr[vfs::MSGLEN] = {};
    std::uint32_t id_len = static_cast<std::uint32_t>(
        std::strlen(vfs::LOGINSTRID));
    put_u32(hdr + 0, vfs::MSG_LOGIN);
    put_u16(hdr + 4, static_cast<std::uint16_t>(id_len));

    // Send header + login string
    std::vector<std::uint8_t> payload(vfs::MSGLEN + id_len);
    std::memcpy(payload.data(), hdr, vfs::MSGLEN);
    std::memcpy(payload.data() + vfs::MSGLEN, vfs::LOGINSTRID, id_len);

    auto sent = transport_->send(payload.data(), payload.size());
    if (!sent) return sent.error();

    // Receive reply header
    std::uint8_t reply[vfs::MSGLEN];
    auto recv = transport_->recv(reply, vfs::MSGLEN);
    if (!recv) return recv.error();
    if (*recv != vfs::MSGLEN) {
        return util::Error{1, "LOGIN reply too short"};
    }

    std::uint32_t msg = get_u32(reply + 0);
    if (msg == vfs::MSG_LOGIN && get_u32(reply + 4) == vfs::CONNECTED) {
        connected_ = true;
        return util::Ok();
    }

    return util::Error{2, "LOGIN rejected by server"};
}

// ---------------------------------------------------------------------------
// Wire header builder
// ---------------------------------------------------------------------------
void VfsAdapter::put_header(std::uint8_t* buf, std::uint32_t msg,
                            std::uint32_t extra1, std::uint32_t extra2) {
    std::memset(buf, 0, vfs::MSGLEN);
    put_u32(buf + 0, msg);
    put_u32(buf + 4, extra1);
    put_u32(buf + 8, extra2);
}

// ---------------------------------------------------------------------------
// recv_header — read exactly MSGLEN bytes
// ---------------------------------------------------------------------------
util::Result<void> VfsAdapter::recv_header(std::uint8_t* buf) {
    std::size_t got = 0;
    while (got < vfs::MSGLEN) {
        auto n = transport_->recv(buf + got, vfs::MSGLEN - got);
        if (!n) return n.error();
        if (*n == 0) return util::Error{3, "connection closed during recv"};
        got += *n;
    }
    return util::Ok();
}

// ---------------------------------------------------------------------------
// send_msg — send header + optional payload, optionally wait for reply
// ---------------------------------------------------------------------------
util::Result<bool> VfsAdapter::send_msg(std::uint32_t msg,
                                        const void* data,
                                        std::uint32_t len,
                                        bool wait_reply) {
    std::vector<std::uint8_t> buf(vfs::MSGLEN + len);
    put_u32(buf.data() + 0, msg);
    if (len > 0 && data) {
        std::memcpy(buf.data() + vfs::MSGLEN, data, len);
    }

    auto sent = transport_->send(buf.data(), buf.size());
    if (!sent) return sent.error();

    if (!wait_reply) {
        // For commit/unlock, server sends nothing or a SYNC.
        // Try a non-blocking peek — if we get a frame, consume it.
        return true;
    }

    std::uint8_t reply[vfs::MSGLEN];
    auto recv = recv_header(reply);
    if (!recv) return recv.error();

    std::uint32_t reply_msg = get_u32(reply + 0);
    if (reply_msg == vfs::MSG_ERROR) {
        std::uint32_t err = get_u32(reply + 4);
        return util::Error{static_cast<int>(err), "VFS server error"};
    }

    return true;
}

// ---------------------------------------------------------------------------
// send_simple — header-only round-trip
// ---------------------------------------------------------------------------
util::Result<std::uint32_t> VfsAdapter::send_simple(std::uint32_t msg,
                                                     const void* data,
                                                     std::uint32_t len) {
    std::lock_guard<std::mutex> lock(mu_);

    std::vector<std::uint8_t> buf(vfs::MSGLEN + len);
    put_u32(buf.data() + 0, msg);
    if (len > 0 && data) {
        std::memcpy(buf.data() + vfs::MSGLEN, data, len);
    }

    auto sent = transport_->send(buf.data(), buf.size());
    if (!sent) return sent.error();

    std::uint8_t reply[vfs::MSGLEN];
    auto recv = recv_header(reply);
    if (!recv) return recv.error();

    std::uint32_t reply_msg = get_u32(reply + 0);
    if (reply_msg == vfs::MSG_ERROR) {
        std::uint32_t err = get_u32(reply + 4);
        return util::Error{static_cast<int>(err), "VFS server error"};
    }

    return reply_msg;
}

// ---------------------------------------------------------------------------
// send_recv — send + receive variable-length reply
// ---------------------------------------------------------------------------
util::Result<std::vector<std::uint8_t>>
VfsAdapter::send_recv(std::uint32_t msg, const void* data, std::uint32_t len) {
    std::lock_guard<std::mutex> lock(mu_);

    // Send
    std::vector<std::uint8_t> buf(vfs::MSGLEN + len);
    put_u32(buf.data() + 0, msg);
    if (len > 0 && data) {
        std::memcpy(buf.data() + vfs::MSGLEN, data, len);
    }

    auto sent = transport_->send(buf.data(), buf.size());
    if (!sent) return sent.error();

    // Receive header
    std::uint8_t hdr[vfs::MSGLEN];
    auto recv = recv_header(hdr);
    if (!recv) return recv.error();

    std::uint32_t reply_msg = get_u32(hdr + 0);
    if (reply_msg == vfs::MSG_ERROR) {
        std::uint32_t err = get_u32(hdr + 4);
        return util::Error{static_cast<int>(err), "VFS server error"};
    }

    // Some replies include a payload size in bytes 4..7
    std::uint32_t payload_size = get_u32(hdr + 4);

    std::vector<std::uint8_t> reply_hdr(vfs::MSGLEN);
    std::memcpy(reply_hdr.data(), hdr, vfs::MSGLEN);

    if (payload_size > 0) {
        std::vector<std::uint8_t> payload(payload_size);
        std::size_t got = 0;
        while (got < payload_size) {
            auto n = transport_->recv(payload.data() + got,
                                      payload_size - got);
            if (!n) return n.error();
            if (*n == 0) return util::Error{4, "connection closed"};
            got += *n;
        }
        // Return header + payload as one buffer
        std::vector<std::uint8_t> result(vfs::MSGLEN + payload_size);
        std::memcpy(result.data(), reply_hdr.data(), vfs::MSGLEN);
        std::memcpy(result.data() + vfs::MSGLEN, payload.data(),
                    payload_size);
        return result;
    }

    return reply_hdr;
}


// ===========================================================================
// File open / close
// ===========================================================================

util::Result<VfsHandle> VfsAdapter::open(const std::string& path,
                                          std::uint32_t flags,
                                          const std::string& def_ext) {
    std::lock_guard<std::mutex> lock(mu_);

    std::uint16_t path_len = static_cast<std::uint16_t>(path.size());
    std::uint32_t msg = (flags & 0xFFFF0000) ? vfs::MSG_OPEN2 : vfs::MSG_OPEN;

    // Build header
    std::uint8_t hdr[vfs::MSGLEN] = {};
    put_u32(hdr + 0, msg);
    put_u16(hdr + 4, path_len);

    if (msg == vfs::MSG_OPEN2) {
        put_u32(hdr + 6, flags);
        if (!def_ext.empty()) {
            std::strncpy(reinterpret_cast<char*>(hdr + 10),
                         def_ext.c_str(), vfs::MSGLEN - 11);
        }
    } else {
        put_u16(hdr + 6, static_cast<std::uint16_t>(flags));
        if (!def_ext.empty()) {
            std::strncpy(reinterpret_cast<char*>(hdr + 8),
                         def_ext.c_str(), vfs::MSGLEN - 9);
        }
    }

    // Send header + path
    std::vector<std::uint8_t> payload(vfs::MSGLEN + path_len);
    std::memcpy(payload.data(), hdr, vfs::MSGLEN);
    std::memcpy(payload.data() + vfs::MSGLEN, path.data(), path_len);

    auto sent = transport_->send(payload.data(), payload.size());
    if (!sent) return sent.error();

    // Receive reply
    std::uint8_t reply[vfs::MSGLEN];
    auto recv = recv_header(reply);
    if (!recv) return recv.error();

    std::uint32_t reply_msg = get_u32(reply + 0);
    if (reply_msg == vfs::MSG_ERROR) {
        std::uint32_t err = get_u32(reply + 4);
        return util::Error{static_cast<int>(err), "VFS open failed"};
    }

    VfsHandle h;
    h.fd    = get_u16(reply + 4);
    h.valid = true;
    h.path  = path;
    return h;
}

util::Result<void> VfsAdapter::close(VfsHandle& h) {
    std::lock_guard<std::mutex> lock(mu_);

    std::uint8_t hdr[vfs::MSGLEN] = {};
    put_u32(hdr + 0, vfs::MSG_CLOSE);
    put_u16(hdr + 4, h.fd);

    auto sent = transport_->send(hdr, vfs::MSGLEN);
    if (!sent) return sent.error();

    // Close ack or SYNC
    std::uint8_t reply[vfs::MSGLEN];
    auto recv = recv_header(reply);
    if (!recv) return recv.error();

    h.valid = false;
    return util::Ok();
}


// ===========================================================================
// Byte-range locking
// ===========================================================================

util::Result<bool> VfsAdapter::lock(VfsHandle& h, std::uint64_t offset,
                                     std::uint64_t length,
                                     std::uint16_t flags) {
    std::lock_guard<std::mutex> lock(mu_);

    std::uint8_t hdr[vfs::MSGLEN] = {};
    put_u32(hdr + 0, vfs::MSG_LOCK);
    put_u16(hdr + 4, h.fd);
    put_u64(hdr + 6, offset);
    put_u64(hdr + 14, length);
    put_u16(hdr + 22, flags);

    auto sent = transport_->send(hdr, vfs::MSGLEN);
    if (!sent) return sent.error();

    // For FLX_WAIT locks, server blocks until acquired.
    std::uint8_t reply[vfs::MSGLEN];
    auto recv = recv_header(reply);
    if (!recv) return recv.error();

    std::uint32_t reply_msg = get_u32(reply + 0);
    if (reply_msg == vfs::MSG_ERROR) {
        return false;   // lock failed (non-blocking or denied)
    }

    return true;
}

util::Result<void> VfsAdapter::unlock(VfsHandle& h, std::uint64_t offset,
                                       std::uint64_t length) {
    std::lock_guard<std::mutex> lock(mu_);

    std::uint8_t hdr[vfs::MSGLEN] = {};
    put_u32(hdr + 0, vfs::MSG_UNLOCK);
    put_u16(hdr + 4, h.fd);
    put_u64(hdr + 6, offset);
    put_u64(hdr + 14, length);

    // Unlock doesn't require a reply (fire-and-forget).
    auto sent = transport_->send(hdr, vfs::MSGLEN);
    if (!sent) return sent.error();

    return util::Ok();
}

util::Result<int> VfsAdapter::test_lock(VfsHandle& h, std::uint64_t offset,
                                         std::uint64_t length,
                                         std::uint16_t flags) {
    std::lock_guard<std::mutex> lock(mu_);

    std::uint8_t hdr[vfs::MSGLEN] = {};
    put_u32(hdr + 0, vfs::MSG_TESTLOCK);
    put_u16(hdr + 4, h.fd);
    put_u64(hdr + 6, offset);
    put_u64(hdr + 14, length);
    put_u16(hdr + 22, flags);

    auto sent = transport_->send(hdr, vfs::MSGLEN);
    if (!sent) return sent.error();

    std::uint8_t reply[vfs::MSGLEN];
    auto recv = recv_header(reply);
    if (!recv) return recv.error();

    return static_cast<int>(get_u32(reply + 4));
}


// ===========================================================================
// Sequential read / write
// ===========================================================================

util::Result<std::size_t> VfsAdapter::read(VfsHandle& h, void* buf,
                                            std::size_t n) {
    std::lock_guard<std::mutex> lock(mu_);

    std::uint8_t hdr[vfs::MSGLEN] = {};
    put_u32(hdr + 0, vfs::MSG_READ);
    put_u16(hdr + 4, h.fd);
    put_u32(hdr + 6, static_cast<std::uint32_t>(n));

    auto sent = transport_->send(hdr, vfs::MSGLEN);
    if (!sent) return sent.error();

    std::uint8_t reply[vfs::MSGLEN];
    auto recv = recv_header(reply);
    if (!recv) return recv.error();

    std::uint32_t reply_msg = get_u32(reply + 0);
    if (reply_msg == vfs::MSG_ERROR) {
        std::uint32_t err = get_u32(reply + 4);
        return util::Error{static_cast<int>(err), "VFS read failed"};
    }

    std::uint32_t nread = get_u32(reply + 4);
    if (nread == 0) return static_cast<std::size_t>(0);

    // Read the data payload
    std::size_t got = 0;
    while (got < nread) {
        auto nr = transport_->recv(
            static_cast<std::uint8_t*>(buf) + got, nread - got);
        if (!nr) return nr.error();
        if (*nr == 0) return util::Error{5, "connection closed during read"};
        got += *nr;
    }

    return static_cast<std::size_t>(nread);
}

util::Result<std::size_t> VfsAdapter::write(VfsHandle& h, const void* buf,
                                             std::size_t n) {
    std::lock_guard<std::mutex> lock(mu_);

    std::uint32_t n32 = static_cast<std::uint32_t>(n);

    // Header
    std::uint8_t hdr[vfs::MSGLEN] = {};
    put_u32(hdr + 0, vfs::MSG_WRITE);
    put_u16(hdr + 4, h.fd);
    put_u32(hdr + 6, n32);

    // Send header + data
    std::vector<std::uint8_t> payload(vfs::MSGLEN + n32);
    std::memcpy(payload.data(), hdr, vfs::MSGLEN);
    std::memcpy(payload.data() + vfs::MSGLEN, buf, n32);

    auto sent = transport_->send(payload.data(), payload.size());
    if (!sent) return sent.error();

    std::uint8_t reply[vfs::MSGLEN];
    auto recv = recv_header(reply);
    if (!recv) return recv.error();

    std::uint32_t reply_msg = get_u32(reply + 0);
    if (reply_msg == vfs::MSG_ERROR) {
        std::uint32_t err = get_u32(reply + 4);
        return util::Error{static_cast<int>(err), "VFS write failed"};
    }

    return static_cast<std::size_t>(get_u32(reply + 4));
}


// ===========================================================================
// Positioned read / write
// ===========================================================================

util::Result<std::size_t> VfsAdapter::read_at(VfsHandle& h, void* buf,
                                               std::size_t n,
                                               std::uint64_t offset) {
    std::lock_guard<std::mutex> lock(mu_);

    std::uint8_t hdr[vfs::MSGLEN] = {};
    put_u32(hdr + 0, vfs::MSG_READAT);
    put_u16(hdr + 4, h.fd);
    put_u32(hdr + 6, static_cast<std::uint32_t>(n));
    put_u64(hdr + 10, offset);

    auto sent = transport_->send(hdr, vfs::MSGLEN);
    if (!sent) return sent.error();

    std::uint8_t reply[vfs::MSGLEN];
    auto recv = recv_header(reply);
    if (!recv) return recv.error();

    std::uint32_t reply_msg = get_u32(reply + 0);
    if (reply_msg == vfs::MSG_ERROR) {
        std::uint32_t err = get_u32(reply + 4);
        return util::Error{static_cast<int>(err), "VFS read_at failed"};
    }

    std::uint32_t nread = get_u32(reply + 4);
    if (nread == 0) return static_cast<std::size_t>(0);

    std::size_t got = 0;
    while (got < nread) {
        auto nr = transport_->recv(
            static_cast<std::uint8_t*>(buf) + got, nread - got);
        if (!nr) return nr.error();
        if (*nr == 0) return util::Error{6, "connection closed"};
        got += *nr;
    }

    return static_cast<std::size_t>(nread);
}

util::Result<std::size_t> VfsAdapter::write_at(VfsHandle& h, const void* buf,
                                                std::size_t n,
                                                std::uint64_t offset) {
    std::lock_guard<std::mutex> lock(mu_);

    std::uint32_t n32 = static_cast<std::uint32_t>(n);

    std::uint8_t hdr[vfs::MSGLEN] = {};
    put_u32(hdr + 0, vfs::MSG_WRITEAT);
    put_u16(hdr + 4, h.fd);
    put_u32(hdr + 6, n32);
    put_u64(hdr + 10, offset);

    std::vector<std::uint8_t> payload(vfs::MSGLEN + n32);
    std::memcpy(payload.data(), hdr, vfs::MSGLEN);
    std::memcpy(payload.data() + vfs::MSGLEN, buf, n32);

    auto sent = transport_->send(payload.data(), payload.size());
    if (!sent) return sent.error();

    std::uint8_t reply[vfs::MSGLEN];
    auto recv = recv_header(reply);
    if (!recv) return recv.error();

    std::uint32_t reply_msg = get_u32(reply + 0);
    if (reply_msg == vfs::MSG_ERROR) {
        std::uint32_t err = get_u32(reply + 4);
        return util::Error{static_cast<int>(err), "VFS write_at failed"};
    }

    return static_cast<std::size_t>(get_u32(reply + 4));
}


// ===========================================================================
// Seek / Truncate / Size / EOF / Commit
// ===========================================================================

util::Result<std::int64_t> VfsAdapter::seek(VfsHandle& h,
                                             std::int64_t offset,
                                             std::uint16_t origin) {
    std::lock_guard<std::mutex> lock(mu_);

    std::uint8_t hdr[vfs::MSGLEN] = {};
    put_u32(hdr + 0, vfs::MSG_SEEK);
    put_u16(hdr + 4, h.fd);
    put_u64(hdr + 6, static_cast<std::uint64_t>(offset));
    put_u16(hdr + 14, origin);

    auto sent = transport_->send(hdr, vfs::MSGLEN);
    if (!sent) return sent.error();

    std::uint8_t reply[vfs::MSGLEN];
    auto recv = recv_header(reply);
    if (!recv) return recv.error();

    return static_cast<std::int64_t>(get_u64(reply + 4));
}

util::Result<void> VfsAdapter::truncate(VfsHandle& h,
                                         std::uint64_t offset) {
    std::lock_guard<std::mutex> lock(mu_);

    std::uint8_t hdr[vfs::MSGLEN] = {};
    put_u32(hdr + 0, vfs::MSG_TRUNC);
    put_u16(hdr + 4, h.fd);
    put_u64(hdr + 6, offset);

    auto sent = transport_->send(hdr, vfs::MSGLEN);
    if (!sent) return sent.error();

    std::uint8_t reply[vfs::MSGLEN];
    auto recv = recv_header(reply);
    if (!recv) return recv.error();

    return util::Ok();
}

util::Result<std::int64_t> VfsAdapter::size(VfsHandle& h) {
    std::lock_guard<std::mutex> lock(mu_);

    std::uint8_t hdr[vfs::MSGLEN] = {};
    put_u32(hdr + 0, vfs::MSG_SIZE);
    put_u16(hdr + 4, h.fd);

    auto sent = transport_->send(hdr, vfs::MSGLEN);
    if (!sent) return sent.error();

    std::uint8_t reply[vfs::MSGLEN];
    auto recv = recv_header(reply);
    if (!recv) return recv.error();

    return static_cast<std::int64_t>(get_u64(reply + 4));
}

util::Result<bool> VfsAdapter::eof(VfsHandle& h) {
    std::lock_guard<std::mutex> lock(mu_);

    std::uint8_t hdr[vfs::MSGLEN] = {};
    put_u32(hdr + 0, vfs::MSG_EOF);
    put_u16(hdr + 4, h.fd);

    auto sent = transport_->send(hdr, vfs::MSGLEN);
    if (!sent) return sent.error();

    std::uint8_t reply[vfs::MSGLEN];
    auto recv = recv_header(reply);
    if (!recv) return recv.error();

    return get_u32(reply + 4) != 0;
}

util::Result<void> VfsAdapter::commit(VfsHandle& h) {
    std::lock_guard<std::mutex> lock(mu_);

    std::uint8_t hdr[vfs::MSGLEN] = {};
    put_u32(hdr + 0, vfs::MSG_COMMIT);
    put_u16(hdr + 4, h.fd);

    // Commit is fire-and-forget in hbnetio
    auto sent = transport_->send(hdr, vfs::MSGLEN);
    if (!sent) return sent.error();

    return util::Ok();
}


// ===========================================================================
// High-level file operations
// ===========================================================================

util::Result<bool> VfsAdapter::exists(const std::string& path) {
    std::uint16_t len = static_cast<std::uint16_t>(path.size());
    auto result = send_simple(vfs::MSG_EXISTS, path.data(), len);
    if (!result) return result.error();
    return *result == vfs::MSG_EXISTS;
}

util::Result<void> VfsAdapter::erase(const std::string& path) {
    std::uint16_t len = static_cast<std::uint16_t>(path.size());
    auto result = send_simple(vfs::MSG_DELETE, path.data(), len);
    if (!result) return result.error();
    return util::Ok();
}

util::Result<void> VfsAdapter::rename(const std::string& old_path,
                                       const std::string& new_path) {
    std::lock_guard<std::mutex> lock(mu_);

    std::uint16_t len1 = static_cast<std::uint16_t>(old_path.size());
    std::uint16_t len2 = static_cast<std::uint16_t>(new_path.size());

    std::uint8_t hdr[vfs::MSGLEN] = {};
    put_u32(hdr + 0, vfs::MSG_RENAME);
    put_u16(hdr + 4, len1);
    put_u16(hdr + 6, len2);

    std::vector<std::uint8_t> payload(vfs::MSGLEN + len1 + len2);
    std::memcpy(payload.data(), hdr, vfs::MSGLEN);
    std::memcpy(payload.data() + vfs::MSGLEN, old_path.data(), len1);
    std::memcpy(payload.data() + vfs::MSGLEN + len1, new_path.data(), len2);

    auto sent = transport_->send(payload.data(), payload.size());
    if (!sent) return sent.error();

    std::uint8_t reply[vfs::MSGLEN];
    auto recv = recv_header(reply);
    if (!recv) return recv.error();

    return util::Ok();
}

util::Result<void> VfsAdapter::copy(const std::string& src,
                                     const std::string& dst) {
    std::lock_guard<std::mutex> lock(mu_);

    std::uint16_t len1 = static_cast<std::uint16_t>(src.size());
    std::uint16_t len2 = static_cast<std::uint16_t>(dst.size());

    std::uint8_t hdr[vfs::MSGLEN] = {};
    put_u32(hdr + 0, vfs::MSG_COPY);
    put_u16(hdr + 4, len1);
    put_u16(hdr + 6, len2);

    std::vector<std::uint8_t> payload(vfs::MSGLEN + len1 + len2);
    std::memcpy(payload.data(), hdr, vfs::MSGLEN);
    std::memcpy(payload.data() + vfs::MSGLEN, src.data(), len1);
    std::memcpy(payload.data() + vfs::MSGLEN + len1, dst.data(), len2);

    auto sent = transport_->send(payload.data(), payload.size());
    if (!sent) return sent.error();

    std::uint8_t reply[vfs::MSGLEN];
    auto recv = recv_header(reply);
    if (!recv) return recv.error();

    return util::Ok();
}


// ===========================================================================
// Directory operations
// ===========================================================================

util::Result<bool> VfsAdapter::dir_exists(const std::string& path) {
    std::uint16_t len = static_cast<std::uint16_t>(path.size());
    auto result = send_simple(vfs::MSG_DIREXISTS, path.data(), len);
    if (!result) return result.error();
    return *result == vfs::MSG_DIREXISTS;
}

util::Result<void> VfsAdapter::dir_make(const std::string& path) {
    std::uint16_t len = static_cast<std::uint16_t>(path.size());
    auto result = send_simple(vfs::MSG_DIRMAKE, path.data(), len);
    if (!result) return result.error();
    return util::Ok();
}

util::Result<void> VfsAdapter::dir_remove(const std::string& path) {
    std::uint16_t len = static_cast<std::uint16_t>(path.size());
    auto result = send_simple(vfs::MSG_DIRREMOVE, path.data(), len);
    if (!result) return result.error();
    return util::Ok();
}


// ===========================================================================
// Attribute / Timestamp
// ===========================================================================

util::Result<std::uint32_t> VfsAdapter::attr_get(const std::string& path) {
    std::lock_guard<std::mutex> lock(mu_);

    std::uint16_t len = static_cast<std::uint16_t>(path.size());
    std::uint8_t hdr[vfs::MSGLEN] = {};
    put_u32(hdr + 0, vfs::MSG_ATTRGET);
    put_u16(hdr + 4, len);

    std::vector<std::uint8_t> payload(vfs::MSGLEN + len);
    std::memcpy(payload.data(), hdr, vfs::MSGLEN);
    std::memcpy(payload.data() + vfs::MSGLEN, path.data(), len);

    auto sent = transport_->send(payload.data(), payload.size());
    if (!sent) return sent.error();

    std::uint8_t reply[vfs::MSGLEN];
    auto recv = recv_header(reply);
    if (!recv) return recv.error();

    return get_u32(reply + 4);
}

util::Result<void> VfsAdapter::attr_set(const std::string& path,
                                         std::uint32_t attr) {
    std::lock_guard<std::mutex> lock(mu_);

    std::uint16_t len = static_cast<std::uint16_t>(path.size());
    std::uint8_t hdr[vfs::MSGLEN] = {};
    put_u32(hdr + 0, vfs::MSG_ATTRSET);
    put_u16(hdr + 4, len);
    put_u32(hdr + 6, attr);

    std::vector<std::uint8_t> payload(vfs::MSGLEN + len);
    std::memcpy(payload.data(), hdr, vfs::MSGLEN);
    std::memcpy(payload.data() + vfs::MSGLEN, path.data(), len);

    auto sent = transport_->send(payload.data(), payload.size());
    if (!sent) return sent.error();

    std::uint8_t reply[vfs::MSGLEN];
    auto recv = recv_header(reply);
    if (!recv) return recv.error();

    return util::Ok();
}

util::Result<VfsAdapter::Timestamp>
VfsAdapter::time_get(const std::string& path) {
    std::lock_guard<std::mutex> lock(mu_);

    std::uint16_t len = static_cast<std::uint16_t>(path.size());
    std::uint8_t hdr[vfs::MSGLEN] = {};
    put_u32(hdr + 0, vfs::MSG_FTIMEGET);
    put_u16(hdr + 4, len);

    std::vector<std::uint8_t> payload(vfs::MSGLEN + len);
    std::memcpy(payload.data(), hdr, vfs::MSGLEN);
    std::memcpy(payload.data() + vfs::MSGLEN, path.data(), len);

    auto sent = transport_->send(payload.data(), payload.size());
    if (!sent) return sent.error();

    std::uint8_t reply[vfs::MSGLEN];
    auto recv = recv_header(reply);
    if (!recv) return recv.error();

    Timestamp ts;
    ts.julian   = static_cast<std::int32_t>(get_u32(reply + 4));
    ts.millisec = static_cast<std::int32_t>(get_u32(reply + 8));
    return ts;
}

util::Result<void> VfsAdapter::time_set(const std::string& path,
                                         std::int32_t julian,
                                         std::int32_t millisec) {
    std::lock_guard<std::mutex> lock(mu_);

    std::uint16_t len = static_cast<std::uint16_t>(path.size());
    std::uint8_t hdr[vfs::MSGLEN] = {};
    put_u32(hdr + 0, vfs::MSG_FTIMESET);
    put_u16(hdr + 4, len);
    put_u32(hdr + 6, static_cast<std::uint32_t>(julian));
    put_u32(hdr + 10, static_cast<std::uint32_t>(millisec));

    std::vector<std::uint8_t> payload(vfs::MSGLEN + len);
    std::memcpy(payload.data(), hdr, vfs::MSGLEN);
    std::memcpy(payload.data() + vfs::MSGLEN, path.data(), len);

    auto sent = transport_->send(payload.data(), payload.size());
    if (!sent) return sent.error();

    std::uint8_t reply[vfs::MSGLEN];
    auto recv = recv_header(reply);
    if (!recv) return recv.error();

    return util::Ok();
}


// ===========================================================================
// DistributedLockGuard (RAII wrapper)
// ===========================================================================

DistributedLockGuard::DistributedLockGuard(VfsAdapter& adapter,
                                            VfsHandle& handle,
                                            std::uint64_t offset,
                                            std::uint64_t length)
    : adapter_(adapter), handle_(handle),
      offset_(offset), length_(length) {
    auto result = adapter_.lock(handle_, offset_, length_,
                                vfs::FLX_EXCLUSIVE | vfs::FLX_WAIT);
    locked_ = result.has_value() && *result;
}

DistributedLockGuard::~DistributedLockGuard() {
    release();
}

void DistributedLockGuard::release() {
    if (locked_) {
        adapter_.unlock(handle_, offset_, length_);
        locked_ = false;
    }
}

} // namespace openads::network
