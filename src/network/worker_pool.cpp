#include "network/worker_pool.h"

#include "network/session.h"
#include "network/socket.h"

#include <mutex>
#include <thread>
#include <utility>

namespace openads::network {

// Per-worker state. `sessions`/`session_socks` are touched ONLY by the
// owning worker thread (affinity invariant); `incoming` is the cross-thread
// handoff queue filled by submit() and drained by the worker.
struct PendingConnection {
    Socket        sock;
    std::string   default_data_dir;
    std::uint16_t listener_port = 0;
};
struct WorkerPool::Worker {
    Socket                                wake_read;   // polled; readable => drain incoming
    Socket                                wake_write;  // submit() pokes a byte here
    std::mutex                            mu;          // guards `incoming`
    std::vector<PendingConnection>        incoming;
    std::vector<std::unique_ptr<Session>> sessions;        // owned by this thread
    std::vector<Socket>                   session_socks;    // parallel to sessions
    std::atomic<std::uint32_t>            live{0};
    std::thread                           th;
};

namespace {

// A connected loopback TCP pair used as a self-wake channel: returns
// {read_end, write_end}. Writing a byte to the write end makes the read
// end poll-readable, so submit()/stop() can interrupt a worker's poll().
util::Result<std::pair<Socket, Socket>> make_wake_pair() {
    ListenerOptions o;
    o.host = "127.0.0.1";
    o.port = 0;
    o.backlog = 1;
    auto l = listen_tcp(o);
    if (!l) return l.error();
    Socket lis = l.value();
    auto port = socket_local_port(lis);
    if (!port) { sock_close(lis); return port.error(); }
    auto c = connect_tcp("127.0.0.1", port.value());      // write end
    if (!c) { sock_close(lis); return c.error(); }
    auto a = accept_one(lis);                              // read end
    sock_close(lis);
    if (!a) { Socket cc = c.value(); sock_close(cc); return a.error(); }
    return std::make_pair(a.value(), c.value());
}

constexpr std::uint8_t kRead = static_cast<std::uint8_t>(PollEvent::Readable);
constexpr std::uint8_t kErr  = static_cast<std::uint8_t>(PollEvent::Error);

} // namespace

WorkerPool::WorkerPool(Server& srv, std::uint32_t workers) : srv_(&srv) {
    std::uint32_t n = workers;
    if (n == 0) {
        n = std::thread::hardware_concurrency();
        if (n == 0) n = 1;
    }
    nworkers_ = n;
}

WorkerPool::~WorkerPool() { stop(); }

void WorkerPool::start() {
    if (running_.load()) return;
    workers_.clear();
    workers_.reserve(nworkers_);
    for (std::uint32_t i = 0; i < nworkers_; ++i) {
        // Only keep a worker that obtained a valid wake pair. A worker started
        // with default (invalid) wake sockets would make WSAPoll fail with
        // WSAENOTSOCK and exit immediately, silently shrinking the pool.
        auto wp = make_wake_pair();
        if (!wp) continue;
        auto w = std::make_unique<Worker>();
        w->wake_read  = wp.value().first;
        w->wake_write = wp.value().second;
        workers_.push_back(std::move(w));
    }
    // Publish running_ only after workers_ is fully populated — otherwise a
    // concurrent submit() could observe running_ == true and index into an
    // empty/half-built workers_ (data race / UB). Threads are spawned after,
    // and worker_loop gates on running_.load().
    running_.store(true);
    for (auto& w : workers_) {
        Worker* wptr = w.get();
        wptr->th = std::thread([this, wptr]() { this->worker_loop(*wptr); });
    }
}

void WorkerPool::submit(Socket s, std::string default_data_dir,
                        std::uint16_t listener_port) {
    if (!running_.load() || workers_.empty()) { sock_close(s); return; }
    // Least-loaded worker wins so connections spread evenly across shards.
    Worker* best = workers_[0].get();
    for (auto& w : workers_) {
        if (w->live.load() < best->live.load()) best = w.get();
    }
    {
        std::lock_guard<std::mutex> lk(best->mu);
        best->incoming.push_back({s, std::move(default_data_dir), listener_port});
    }
    std::uint8_t b = 1;
    Socket ww = best->wake_write;
    (void)sock_send(ww, &b, 1);
}

void WorkerPool::worker_loop(Worker& w) {
    while (running_.load()) {
        // 1. Adopt any newly-submitted sockets BEFORE building the poll set,
        //    so the index mapping items[j+1] <-> sessions[j] stays aligned.
        {
            std::vector<PendingConnection> news;
            { std::lock_guard<std::mutex> lk(w.mu); news.swap(w.incoming); }
            for (auto& pc : news) {
                // Reactor reads are non-blocking: a partial/stalled frame must
                // yield the worker back to its other connections rather than
                // park it in recv (Session::handle_readable buffers partials).
                (void)socket_set_nonblocking(pc.sock, true);
                w.sessions.push_back(std::make_unique<Session>(
                    *srv_, pc.sock, std::move(pc.default_data_dir),
                    pc.listener_port));
                w.session_socks.push_back(pc.sock);
                w.live.fetch_add(1);
            }
        }

        // 2. Poll = wake socket (index 0) + every live connection socket.
        std::vector<PollItem> items;
        items.reserve(w.session_socks.size() + 1);
        items.push_back({w.wake_read, kRead});
        for (Socket sk : w.session_socks) items.push_back({sk, kRead});

        auto pr = socket_poll(items, 200 /*ms*/);
        if (!pr) break;
        if (!running_.load()) break;

        // 3. Consume the wake edge; the next iteration's drain (step 1)
        //    picks up whatever submit() queued.
        if (items[0].events & kRead) {
            std::uint8_t tmp[64];
            Socket wr = w.wake_read;
            (void)sock_recv(wr, tmp, sizeof(tmp));
        }

        // 4. Service every ready connection through the shared per-frame path.
        std::vector<std::size_t> dead;
        for (std::size_t j = 0; j < w.sessions.size(); ++j) {
            if ((items[j + 1].events & (kRead | kErr)) == 0) continue;
            if (!w.sessions[j]->handle_readable()) dead.push_back(j);
        }

        // 5. Tear down dead connections (reverse order keeps indices valid).
        for (auto it = dead.rbegin(); it != dead.rend(); ++it) {
            std::size_t j = *it;
            Socket sk = w.session_socks[j];
            sock_close(sk);
            w.sessions.erase(w.sessions.begin() +
                             static_cast<std::ptrdiff_t>(j));
            w.session_socks.erase(w.session_socks.begin() +
                                  static_cast<std::ptrdiff_t>(j));
            w.live.fetch_sub(1);
        }
    }

    // Teardown: close + destroy every remaining session (dtor unregisters),
    // then the wake channel.
    for (Socket& sk : w.session_socks) sock_close(sk);
    w.sessions.clear();
    w.session_socks.clear();
    w.live.store(0);
    sock_close(w.wake_read);
    sock_close(w.wake_write);
}

void WorkerPool::stop() {
    if (!running_.exchange(false)) return;
    // Poke each worker so its poll() returns without waiting out the timeout.
    for (auto& w : workers_) {
        std::uint8_t b = 1;
        Socket ww = w->wake_write;
        if (ww.valid()) (void)sock_send(ww, &b, 1);
    }
    for (auto& w : workers_) {
        if (w->th.joinable()) w->th.join();
    }
    workers_.clear();
}

std::uint32_t WorkerPool::live_connections() const noexcept {
    std::uint32_t total = 0;
    for (auto& w : workers_) total += w->live.load();
    return total;
}

} // namespace openads::network
