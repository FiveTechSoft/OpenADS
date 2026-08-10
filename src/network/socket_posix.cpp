#include "network/socket.h"

#ifndef _WIN32

#include <arpa/inet.h>
#include <cstring>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace openads::network {

util::Result<void> network_init() { return {}; }

util::Result<Socket> listen_tcp(const ListenerOptions& opts) {
    int s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        return util::Error{5000, errno, "socket() failed", ""};
    }
    int on = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(opts.port);
    inet_pton(AF_INET, opts.host.c_str(), &addr.sin_addr);
    if (::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        int e = errno; ::close(s);
        return util::Error{5000, e, "bind() failed", opts.host};
    }
    if (::listen(s, opts.backlog) < 0) {
        int e = errno; ::close(s);
        return util::Error{5000, e, "listen() failed", ""};
    }
    Socket out;
    out.handle = static_cast<std::uintptr_t>(s);
    return out;
}

util::Result<std::uint16_t> socket_local_port(const Socket& sock) {
    sockaddr_in addr{};
    socklen_t len = sizeof(addr);
    if (getsockname(static_cast<int>(sock.handle),
                    reinterpret_cast<sockaddr*>(&addr), &len) < 0) {
        return util::Error{5000, errno, "getsockname failed", ""};
    }
    return static_cast<std::uint16_t>(ntohs(addr.sin_port));
}

util::Result<PeerAddr> socket_peer_addr(const Socket& sock) {
    sockaddr_in addr{};
    socklen_t len = sizeof(addr);
    if (getpeername(static_cast<int>(sock.handle),
                    reinterpret_cast<sockaddr*>(&addr), &len) < 0) {
        return util::Error{5000, errno, "getpeername failed", ""};
    }
    char ipbuf[INET_ADDRSTRLEN] = {0};
    inet_ntop(AF_INET, &addr.sin_addr, ipbuf, sizeof(ipbuf));
    return PeerAddr{std::string(ipbuf),
                    static_cast<std::uint16_t>(ntohs(addr.sin_port))};
}

// M12.20 — disable Nagle on every per-connection socket. The
// wire protocol is strict request/response (ping-pong), so the
// kernel's Nagle delay (up to 200 ms accumulating small frames)
// is pure latency tax. xbrowse PgDn × 20 RTT × 40 ms Nagle =
// ~800 ms of pure delay per repaint pre-fix.
static void disable_nagle(int s) {
    int on = 1;
    (void)::setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
}

// Detect dead peers so Session::cleanup() releases .dbf/.cdx after a
// client dies without Disconnect (matches Windows SIO_KEEPALIVE_VALS
// tuning: 15 s idle, 3 s interval, 5 probes).
static void enable_keepalive(int s) {
    int on = 1;
    (void)::setsockopt(s, SOL_SOCKET, SO_KEEPALIVE, &on, sizeof(on));
#if defined(TCP_KEEPIDLE)
    int idle = 15;
    (void)::setsockopt(s, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));
#elif defined(TCP_KEEPALIVE)
    // macOS: seconds until the first keepalive probe
    int idle = 15;
    (void)::setsockopt(s, IPPROTO_TCP, TCP_KEEPALIVE, &idle, sizeof(idle));
#endif
#if defined(TCP_KEEPINTVL)
    int intvl = 3;
    (void)::setsockopt(s, IPPROTO_TCP, TCP_KEEPINTVL, &intvl, sizeof(intvl));
#endif
#if defined(TCP_KEEPCNT)
    int cnt = 5;
    (void)::setsockopt(s, IPPROTO_TCP, TCP_KEEPCNT, &cnt, sizeof(cnt));
#endif
}

// POSIX raises SIGPIPE when send() targets a peer that already closed —
// fatal to the whole process, where Windows just fails the call. Linux
// suppresses it per-call with MSG_NOSIGNAL (see sock_send); macOS has no
// MSG_NOSIGNAL and needs SO_NOSIGPIPE on the socket instead.
static void suppress_sigpipe(int s) {
#ifdef SO_NOSIGPIPE
    int on = 1;
    (void)::setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#else
    (void)s;
#endif
}

util::Result<Socket> accept_one(Socket& listener) {
    sockaddr_in addr{};
    socklen_t len = sizeof(addr);
    int c = ::accept(static_cast<int>(listener.handle),
                     reinterpret_cast<sockaddr*>(&addr), &len);
    if (c < 0) {
        return util::Error{5000, errno, "accept() failed", ""};
    }
    disable_nagle(c);
    enable_keepalive(c);
    suppress_sigpipe(c);
    Socket out;
    out.handle = static_cast<std::uintptr_t>(c);
    return out;
}

util::Result<Socket> connect_tcp(const std::string& host,
                                  std::uint16_t port) {
    int s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        return util::Error{5000, errno, "socket() failed", ""};
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    inet_pton(AF_INET, host.c_str(), &addr.sin_addr);
    if (::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        int e = errno; ::close(s);
        return util::Error{5000, e, "connect() failed", host};
    }
    disable_nagle(s);
    suppress_sigpipe(s);
    Socket out;
    out.handle = static_cast<std::uintptr_t>(s);
    return out;
}

util::Result<std::size_t> sock_send(Socket& sock,
                                     const std::uint8_t* buf,
                                     std::size_t n) {
    // MSG_NOSIGNAL: a peer that already closed must yield EPIPE as an
    // error, never a process-killing SIGPIPE (Windows semantics).
#ifdef MSG_NOSIGNAL
    ssize_t sent = ::send(static_cast<int>(sock.handle), buf, n,
                          MSG_NOSIGNAL);
#else
    ssize_t sent = ::send(static_cast<int>(sock.handle), buf, n, 0);
#endif
    if (sent < 0) {
        return util::Error{5000, errno, "send() failed", ""};
    }
    return static_cast<std::size_t>(sent);
}

util::Result<std::size_t> sock_recv(Socket& sock,
                                     std::uint8_t* buf,
                                     std::size_t n) {
    ssize_t got = ::recv(static_cast<int>(sock.handle), buf, n, 0);
    if (got < 0) {
        return util::Error{5000, errno, "recv() failed", ""};
    }
    return static_cast<std::size_t>(got);
}

util::Result<void> socket_set_nonblocking(Socket& sock, bool enable) {
    int fd = static_cast<int>(sock.handle);
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return util::Error{5000, errno, "fcntl(F_GETFL) failed", ""};
    }
    flags = enable ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
    if (::fcntl(fd, F_SETFL, flags) < 0) {
        return util::Error{5000, errno, "fcntl(F_SETFL) failed", ""};
    }
    return {};
}

bool socket_recv_would_block(const util::Error& e) noexcept {
    return e.sub_code == EWOULDBLOCK || e.sub_code == EAGAIN;
}

void sock_close(Socket& sock) noexcept {
    if (sock.valid()) {
        // Force-shutdown first so any thread blocked in accept() /
        // recv() on this fd wakes immediately. Linux close() alone
        // doesn't always interrupt other threads on the same fd.
        ::shutdown(static_cast<int>(sock.handle), SHUT_RDWR);
        ::close(static_cast<int>(sock.handle));
        sock.handle = static_cast<std::uintptr_t>(-1);
    }
}

util::Result<int> socket_poll(std::vector<PollItem>& items, int timeout_ms) {
    if (items.empty()) return 0;
    std::vector<pollfd> fds(items.size());
    for (std::size_t i = 0; i < items.size(); ++i) {
        fds[i].fd      = static_cast<int>(items[i].sock.handle);
        fds[i].events  = POLLIN;
        fds[i].revents = 0;
    }
    int rc = ::poll(fds.data(), static_cast<nfds_t>(fds.size()), timeout_ms);
    if (rc < 0) {
        if (errno == EINTR) return 0;   // interrupted; caller re-polls
        return util::Error{5000, errno, "poll failed", ""};
    }
    for (std::size_t i = 0; i < items.size(); ++i) {
        std::uint8_t ev = 0;
        if (fds[i].revents & POLLIN)
            ev |= static_cast<std::uint8_t>(PollEvent::Readable);
        if (fds[i].revents & (POLLERR | POLLHUP | POLLNVAL))
            ev |= static_cast<std::uint8_t>(PollEvent::Error);
        items[i].events = ev;
    }
    return rc;
}

} // namespace openads::network

#endif // !_WIN32
