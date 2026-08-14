#include "doctest.h"

#include "network/server.h"
#include "network/socket.h"
#include "network/wire.h"
#include "network/worker_pool.h"

#include <cstdint>
#include <vector>

using openads::network::Server;
using openads::network::WorkerPool;
using openads::network::Socket;
using openads::network::listen_tcp;
using openads::network::socket_local_port;
using openads::network::accept_one;
using openads::network::connect_tcp;
using openads::network::sock_close;
using openads::network::read_frame;
using openads::network::write_frame;
using openads::network::Frame;
using openads::network::Opcode;

// The pool multiplexes many connections over a FIXED, small number of
// worker threads. We drive WorkerPool directly: a Server instance acts
// purely as the session registry (never started), and we hand it the
// server-side ends of K loopback connections.
TEST_CASE("WorkerPool serves connections over a bounded worker set") {
    Server srv;                       // registry only; not started
    const std::uint32_t M = 2;
    WorkerPool pool(srv, M);
    pool.start();

    auto l = listen_tcp({"127.0.0.1", 0, 16});
    REQUIRE(l.has_value());
    auto port = socket_local_port(l.value());
    REQUIRE(port.has_value());

    const int K = 8;                  // far more connections than workers
    std::vector<Socket> clients;
    for (int i = 0; i < K; ++i) {
        auto c = connect_tcp("127.0.0.1", port.value());
        REQUIRE(c.has_value());
        auto a = accept_one(l.value());
        REQUIRE(a.has_value());
        pool.submit(a.value(), std::string(), port.value());       // pool now owns the accepted socket
        clients.push_back(c.value());
    }

    // Every client gets a correct Hello -> HelloAck, proving all K
    // connections are being serviced by the M-thread pool.
    for (auto& cs : clients) {
        Frame hello;
        hello.opcode = Opcode::Hello;
        REQUIRE(write_frame(cs, hello).has_value());
        auto r = read_frame(cs);
        REQUIRE(r.has_value());
        CHECK(r.value().opcode == Opcode::HelloAck);
    }

    // Threads are bounded at M even though K connections are open: the
    // reactor decouples connection count from thread count.
    CHECK(pool.worker_count() == M);
    CHECK(pool.live_connections() == static_cast<std::uint32_t>(K));

    for (auto& cs : clients) sock_close(cs);
    pool.stop();
    sock_close(l.value());
}

// Idle connections must not consume worker threads: open many, send
// nothing, and confirm the thread count is still M.
TEST_CASE("WorkerPool keeps thread count at M with idle connections") {
    Server srv;
    const std::uint32_t M = 2;
    WorkerPool pool(srv, M);
    pool.start();

    auto l = listen_tcp({"127.0.0.1", 0, 16});
    REQUIRE(l.has_value());
    auto port = socket_local_port(l.value());
    REQUIRE(port.has_value());

    std::vector<Socket> clients;
    for (int i = 0; i < 16; ++i) {
        auto c = connect_tcp("127.0.0.1", port.value());
        REQUIRE(c.has_value());
        auto a = accept_one(l.value());
        REQUIRE(a.has_value());
        pool.submit(a.value(), std::string(), port.value());
        clients.push_back(c.value());
    }

    CHECK(pool.worker_count() == M);   // 16 idle connections, still 2 threads

    for (auto& cs : clients) sock_close(cs);
    pool.stop();
    sock_close(l.value());
}
