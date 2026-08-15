#include "network/socket.h"

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>
#pragma comment(lib, "ws2_32.lib")

#include <atomic>
#include <cstdio>
#include <cstring>

namespace openads::network {

namespace {

std::atomic<bool> g_inited{false};

// Detect dead peers so Session::cleanup() can release .dbf/.cdx handles
// after a client process dies without a clean Disconnect (Pritpal Bedi's
// report: .cdx stayed "in use" until openads_serverd itself was killed).
// Default Windows keepalive is ~2 hours — far too long for a DB server.
// 15 s idle + 3 s interval ≈ 15+3*10 ≈ 45 s worst case to detect death.
static void enable_keepalive(SOCKET s) {
    BOOL on = TRUE;
    (void)::setsockopt(s, SOL_SOCKET, SO_KEEPALIVE,
                       reinterpret_cast<const char*>(&on), sizeof(on));
    tcp_keepalive ka{};
    ka.onoff             = 1;
    ka.keepalivetime     = 15000;  // ms of idle before first probe
    ka.keepaliveinterval = 3000;   // ms between probes
    DWORD bytes = 0;
    (void)::WSAIoctl(s, SIO_KEEPALIVE_VALS, &ka, sizeof(ka),
                     nullptr, 0, &bytes, nullptr, nullptr);
}

}

util::Result<void> network_init() {
    if (g_inited.load()) return {};
    WSADATA wsa{};
    int rc = WSAStartup(MAKEWORD(2, 2), &wsa);
    if (rc != 0) {
        return util::Error{5000, rc, "WSAStartup failed", ""};
    }
    g_inited.store(true);
    return {};
}

util::Result<Socket> listen_tcp(const ListenerOptions& opts) {
    if (auto r = network_init(); !r) return r.error();
    SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        return util::Error{5000, WSAGetLastError(),
                           "socket() failed", ""};
    }
    BOOL on = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&on), sizeof(on));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(opts.port);
    InetPtonA(AF_INET, opts.host.c_str(), &addr.sin_addr);
    if (::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) ==
        SOCKET_ERROR) {
        int e = WSAGetLastError(); closesocket(s);
        return util::Error{5000, e, "bind() failed", opts.host};
    }
    if (::listen(s, opts.backlog) == SOCKET_ERROR) {
        int e = WSAGetLastError(); closesocket(s);
        return util::Error{5000, e, "listen() failed", ""};
    }
    Socket out;
    out.handle = static_cast<std::uintptr_t>(s);
    return out;
}

util::Result<std::uint16_t> socket_local_port(const Socket& sock) {
    sockaddr_in addr{};
    int len = static_cast<int>(sizeof(addr));
    if (getsockname(static_cast<SOCKET>(sock.handle),
                    reinterpret_cast<sockaddr*>(&addr), &len) ==
        SOCKET_ERROR) {
        return util::Error{5000, WSAGetLastError(),
                           "getsockname failed", ""};
    }
    return static_cast<std::uint16_t>(ntohs(addr.sin_port));
}

util::Result<PeerAddr> socket_peer_addr(const Socket& sock) {
    sockaddr_in addr{};
    int len = static_cast<int>(sizeof(addr));
    if (getpeername(static_cast<SOCKET>(sock.handle),
                    reinterpret_cast<sockaddr*>(&addr), &len) ==
        SOCKET_ERROR) {
        return util::Error{5000, WSAGetLastError(),
                           "getpeername failed", ""};
    }
    char ipbuf[INET_ADDRSTRLEN] = {0};
    InetNtopA(AF_INET, &addr.sin_addr, ipbuf, sizeof(ipbuf));
    return PeerAddr{std::string(ipbuf),
                    static_cast<std::uint16_t>(ntohs(addr.sin_port))};
}

// M12.20 — disable Nagle on every per-connection socket. The
// wire protocol is strict request/response (ping-pong), so the
// kernel's Nagle delay (up to 200 ms accumulating small frames)
// is pure latency tax. xbrowse PgDn × 20 RTT × 40 ms Nagle =
// ~800 ms of pure delay per repaint pre-fix.
static void disable_nagle(SOCKET s) {
    int on = 1;
    (void)::setsockopt(s, IPPROTO_TCP, TCP_NODELAY,
                       reinterpret_cast<const char*>(&on),
                       sizeof(on));
}

util::Result<Socket> accept_one(Socket& listener) {
    sockaddr_in addr{};
    int len = static_cast<int>(sizeof(addr));
    SOCKET c = ::accept(static_cast<SOCKET>(listener.handle),
                        reinterpret_cast<sockaddr*>(&addr), &len);
    if (c == INVALID_SOCKET) {
        return util::Error{5000, WSAGetLastError(),
                           "accept() failed", ""};
    }
    disable_nagle(c);
    enable_keepalive(c);
    Socket out;
    out.handle = static_cast<std::uintptr_t>(c);
    return out;
}

util::Result<Socket> connect_tcp(const std::string& host,
                                  std::uint16_t port) {
    if (auto r = network_init(); !r) return r.error();

    // Use getaddrinfo so hostnames (e.g. "localhost") are resolved properly;
    // InetPtonA only accepts dotted-decimal strings and silently produces
    // addr 0.0.0.0 for symbolic names.
    char portbuf[8];
    std::snprintf(portbuf, sizeof(portbuf), "%u", port);
    addrinfo hints{};
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    addrinfo* ai = nullptr;
    int gai = ::getaddrinfo(host.c_str(), portbuf, &hints, &ai);
    if (gai != 0 || ai == nullptr) {
        return util::Error{5000, gai, "getaddrinfo() failed", host};
    }

    SOCKET s = INVALID_SOCKET;
    for (addrinfo* cur = ai; cur != nullptr; cur = cur->ai_next) {
        s = ::socket(cur->ai_family, cur->ai_socktype, cur->ai_protocol);
        if (s == INVALID_SOCKET) continue;
        if (::connect(s, cur->ai_addr, static_cast<int>(cur->ai_addrlen)) != SOCKET_ERROR)
            break;
        closesocket(s);
        s = INVALID_SOCKET;
    }
    ::freeaddrinfo(ai);

    if (s == INVALID_SOCKET) {
        return util::Error{5000, WSAGetLastError(), "connect() failed", host};
    }
    disable_nagle(s);
    // M12.33 — set receive timeout so blocking recv() doesn't hang forever
    // when the remote side is slow or the connection drops silently.
    {
        DWORD timeout_ms = 30000;  // 30 seconds
        ::setsockopt(s, SOL_SOCKET, SO_RCVTIMEO,
                     reinterpret_cast<const char*>(&timeout_ms),
                     sizeof(timeout_ms));
    }
    Socket out;
    out.handle = static_cast<std::uintptr_t>(s);
    return out;
}

util::Result<std::size_t> sock_send(Socket& sock,
                                     const std::uint8_t* buf,
                                     std::size_t n) {
    int sent = ::send(static_cast<SOCKET>(sock.handle),
                      reinterpret_cast<const char*>(buf),
                      static_cast<int>(n), 0);
    if (sent == SOCKET_ERROR) {
        return util::Error{5000, WSAGetLastError(),
                           "send() failed", ""};
    }
    return static_cast<std::size_t>(sent);
}

util::Result<std::size_t> sock_recv(Socket& sock,
                                     std::uint8_t* buf,
                                     std::size_t n) {
    int got = ::recv(static_cast<SOCKET>(sock.handle),
                     reinterpret_cast<char*>(buf),
                     static_cast<int>(n), 0);
    if (got == SOCKET_ERROR) {
        return util::Error{5000, WSAGetLastError(),
                           "recv() failed", ""};
    }
    return static_cast<std::size_t>(got);
}

util::Result<void> socket_set_nonblocking(Socket& sock, bool enable) {
    u_long mode = enable ? 1u : 0u;
    if (::ioctlsocket(static_cast<SOCKET>(sock.handle),
                      static_cast<long>(FIONBIO),
                      &mode) != 0) {
        return util::Error{5000, WSAGetLastError(),
                           "ioctlsocket(FIONBIO) failed", ""};
    }
    return {};
}

bool socket_recv_would_block(const util::Error& e) noexcept {
    return e.sub_code == WSAEWOULDBLOCK;
}

void sock_close(Socket& sock) noexcept {
    if (sock.valid()) {
        closesocket(static_cast<SOCKET>(sock.handle));
        sock.handle = static_cast<std::uintptr_t>(-1);
    }
}

util::Result<int> socket_poll(std::vector<PollItem>& items, int timeout_ms) {
    if (items.empty()) return 0;
    std::vector<WSAPOLLFD> fds(items.size());
    for (std::size_t i = 0; i < items.size(); ++i) {
        fds[i].fd      = static_cast<SOCKET>(items[i].sock.handle);
        fds[i].events  = POLLRDNORM;
        fds[i].revents = 0;
    }
    int rc = WSAPoll(fds.data(), static_cast<ULONG>(fds.size()), timeout_ms);
    if (rc == SOCKET_ERROR) {
        return util::Error{5000, WSAGetLastError(), "WSAPoll failed", ""};
    }
    for (std::size_t i = 0; i < items.size(); ++i) {
        std::uint8_t ev = 0;
        if (fds[i].revents & (POLLRDNORM | POLLIN))
            ev |= static_cast<std::uint8_t>(PollEvent::Readable);
        if (fds[i].revents & (POLLERR | POLLHUP | POLLNVAL))
            ev |= static_cast<std::uint8_t>(PollEvent::Error);
        items[i].events = ev;
    }
    return rc;
}

} // namespace openads::network

#endif // _WIN32
