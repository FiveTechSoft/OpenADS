#pragma once

#include "network/socket.h"
#include "util/result.h"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace openads::network {

// M12.13 — transport abstraction. The Phase 2 wire layer (server +
// RemoteConnection) used to operate directly on `Socket`; this
// header introduces a polymorphic `ITransport` so a future TLS
// transport (mbedtls / OpenSSL, slated for v0.4.0) can plug in
// without touching the server / client business logic.
//
// PlainTransport is the only concrete impl today; it owns a Socket
// and forwards send/recv/close to sock_send / sock_recv / sock_close.

class ITransport {
public:
    virtual ~ITransport() = default;
    virtual util::Result<std::size_t>
        send(const std::uint8_t* buf, std::size_t n) = 0;
    virtual util::Result<std::size_t>
        recv(std::uint8_t* buf, std::size_t n) = 0;
    virtual void close() noexcept            = 0;
    virtual bool valid() const noexcept      = 0;
    virtual bool wants_write() const noexcept { return false; }
    virtual bool buffered_read() const noexcept { return false; }
};

inline bool transport_would_block(const util::Error& error) noexcept {
    return error.context == "transport would block" || socket_recv_would_block(error);
}

class PlainTransport : public ITransport {
public:
    explicit PlainTransport(Socket s) : sock_(s) {}
    ~PlainTransport() override { close(); }
    PlainTransport(const PlainTransport&) = delete;
    PlainTransport& operator=(const PlainTransport&) = delete;

    util::Result<std::size_t>
        send(const std::uint8_t* buf, std::size_t n) override {
        return sock_send(sock_, buf, n);
    }
    util::Result<std::size_t>
        recv(std::uint8_t* buf, std::size_t n) override {
        return sock_recv(sock_, buf, n);
    }
    void close() noexcept override {
        if (sock_.valid()) {
            sock_close(sock_);
            sock_ = Socket{};
        }
    }
    bool valid() const noexcept override { return sock_.valid(); }

    // Escape hatch — server's stop() needs the underlying handle to
    // wake a blocked accept on macOS via a self-connect probe.
    Socket& socket() noexcept { return sock_; }

private:
    Socket sock_;
};

inline std::unique_ptr<ITransport> make_plain_transport(Socket s) {
    return std::make_unique<PlainTransport>(s);
}

} // namespace openads::network
