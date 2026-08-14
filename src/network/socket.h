#pragma once

#include "util/result.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace openads::network {

// M12.2 — minimal cross-platform TCP socket layer for the Phase 2
// server. Just enough surface to: bind+listen on an ephemeral port,
// accept a single connection, connect from a client, send + recv
// raw bytes, and close. Server multiplexing + threaded accept loop
// build on top of these primitives.

struct Socket {
    // Underlying handle. Win32 SOCKET is a UINT_PTR; POSIX uses int.
    // We carry the wider type so both implementations fit.
    std::uintptr_t handle = static_cast<std::uintptr_t>(-1);
    bool valid() const noexcept {
        return handle != static_cast<std::uintptr_t>(-1);
    }
    bool operator==(const Socket& o) const noexcept { return handle == o.handle; }
    bool operator!=(const Socket& o) const noexcept { return handle != o.handle; }
};

struct ListenerOptions {
    std::string host = "127.0.0.1";
    std::uint16_t port = 0;          // 0 = ephemeral
    int           backlog = 16;
};

util::Result<Socket>      listen_tcp(const ListenerOptions& opts);
util::Result<std::uint16_t> socket_local_port(const Socket& sock);

// studio.web.0.4 — return peer "ip:port" of a connected socket
// (as accepted from a listener). Used by the Server to populate
// SessionInfo for the Studio sessions panel.
struct PeerAddr {
    std::string   ip;
    std::uint16_t port = 0;
};
util::Result<PeerAddr>      socket_peer_addr(const Socket& sock);
util::Result<Socket>      accept_one(Socket& listener);
util::Result<Socket>      connect_tcp(const std::string& host,
                                       std::uint16_t port);
util::Result<std::size_t> sock_send(Socket& sock,
                                     const std::uint8_t* buf,
                                     std::size_t n);
util::Result<std::size_t> sock_recv(Socket& sock,
                                     std::uint8_t* buf,
                                     std::size_t n);

// Toggle non-blocking I/O. When enabled, sock_recv returns a would-block error
// (classified by socket_recv_would_block) instead of blocking when no data is
// available — the reactor pool relies on this so one stalled client cannot
// freeze a worker thread (no head-of-line blocking).
util::Result<void> socket_set_nonblocking(Socket& sock, bool enable);
bool               socket_recv_would_block(const util::Error& e) noexcept;

void                      sock_close(Socket& sock) noexcept;

// Reactor multiplexing primitive (M-enterprise step 3). socket_poll
// blocks up to `timeout_ms` (-1 = infinite, 0 = return immediately)
// until at least one of `items` is readable or has errored, then
// fills each item's `events` with the ready set. Returns the number
// of ready sockets (0 on timeout). Backed by WSAPoll on Windows and
// poll() on POSIX.
enum class PollEvent : std::uint8_t {
    None     = 0,
    Readable = 1,   // data (or EOF / a pending accept) available to read
    Error    = 2,   // POLLERR / POLLHUP / POLLNVAL
};
struct PollItem {
    Socket       sock;
    std::uint8_t events = 0;   // in: requested (currently always Readable);
                               // out: returned ready set (Readable | Error)
};
util::Result<int> socket_poll(std::vector<PollItem>& items, int timeout_ms);

// Process-wide network init (Winsock2 WSAStartup on Windows; no-op
// on POSIX). Idempotent.
util::Result<void>        network_init();

} // namespace openads::network
