#include "network/server.h"

#include "engine/aof_eval.h"
#include "engine/aof_expr.h"
#include "engine/table.h"
#include "mgmt/mg_collector.h"
#include "mgmt/mg_stats.h"
#include "network/mg_wire.h"
#include "network/session.h"
#include "network/worker_pool.h"
#include "platform/proc.h"
#include "openads/ace.h"
#include "openads/error.h"
#include "session/connection.h"
#include "sql_backend/enterprise_config.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace openads::network {

util::Result<void> recv_exact(Socket& s, std::uint8_t* buf,
                              std::size_t n) {
    std::size_t got = 0;
    while (got < n) {
        auto r = sock_recv(s, buf + got, n - got);
        if (!r) return r.error();
        if (r.value() == 0) {
            return util::Error{5000, 0, "peer closed connection", ""};
        }
        got += r.value();
    }
    return {};
}

util::Result<void> recv_exact(ITransport& t, std::uint8_t* buf,
                              std::size_t n) {
    std::size_t got = 0;
    while (got < n) {
        auto r = t.recv(buf + got, n - got);
        if (!r) return r.error();
        if (r.value() == 0) {
            return util::Error{5000, 0, "peer closed connection", ""};
        }
        got += r.value();
    }
    return {};
}

namespace {

template <class ReadPayloadFn>
util::Result<Frame> decode_after_recv(std::uint8_t hdr[5],
                                       ReadPayloadFn&& read_payload) {
    std::uint32_t n =
        (static_cast<std::uint32_t>(hdr[0]) << 24) |
        (static_cast<std::uint32_t>(hdr[1]) << 16) |
        (static_cast<std::uint32_t>(hdr[2]) <<  8) |
         static_cast<std::uint32_t>(hdr[3]);
    if (n > kMaxFramePayload) {
        return util::Error{5000, 0, "frame payload too large", ""};
    }
    Frame f;
    f.opcode = static_cast<Opcode>(hdr[4]);
    if (n > 0) {
        f.payload.resize(n);
        if (auto r = read_payload(f.payload.data(), n); !r) return r.error();
    }
    return f;
}

} // namespace

util::Result<Frame> read_frame(Socket& s) {
    std::uint8_t hdr[5];
    if (auto r = recv_exact(s, hdr, sizeof(hdr)); !r) return r.error();
    return decode_after_recv(hdr, [&](std::uint8_t* b, std::size_t n) {
        return recv_exact(s, b, n);
    });
}

util::Result<Frame> read_frame(ITransport& t) {
    std::uint8_t hdr[5];
    if (auto r = recv_exact(t, hdr, sizeof(hdr)); !r) return r.error();
    return decode_after_recv(hdr, [&](std::uint8_t* b, std::size_t n) {
        return recv_exact(t, b, n);
    });
}

util::Result<void> write_frame(Socket& s, const Frame& f) {
    auto enc = encode_frame(f);
    if (!enc) return enc.error();
    auto& bytes = enc.value();
    std::size_t sent = 0;
    while (sent < bytes.size()) {
        auto r = sock_send(s, bytes.data() + sent, bytes.size() - sent);
        if (!r) return r.error();
        if (r.value() == 0) {
            return util::Error{5000, 0, "send returned 0", ""};
        }
        sent += r.value();
    }
    return {};
}

util::Result<void> write_frame(ITransport& t, const Frame& f) {
    auto enc = encode_frame(f);
    if (!enc) return enc.error();
    auto& bytes = enc.value();
    std::size_t sent = 0;
    while (sent < bytes.size()) {
        auto r = t.send(bytes.data() + sent, bytes.size() - sent);
        if (!r) return r.error();
        if (r.value() == 0) {
            return util::Error{5000, 0, "send returned 0", ""};
        }
        sent += r.value();
    }
    return {};
}

Server::Server() = default;

Server::~Server() { stop(); }

void Server::add_credential(const std::string& user,
                            const std::string& password) {
    std::lock_guard<std::mutex> lk(creds_mu_);
    creds_[user] = password;
}

bool Server::require_auth() const noexcept { return !creds_.empty(); }

std::vector<Server::SessionInfo> Server::sessions_snapshot() const {
    std::lock_guard<std::mutex> lk(info_mu_);
    std::vector<SessionInfo> out;
    out.reserve(sessions_info_.size());
    for (auto& kv : sessions_info_) out.push_back(kv.second);
    return out;
}

std::uint16_t Server::conn_no_for_session(std::uint64_t id) const {
    auto sessions = sessions_snapshot();
    for (std::size_t i = 0; i < sessions.size(); ++i) {
        if (sessions[i].id == id) return static_cast<std::uint16_t>(i + 1);
    }
    return 0;
}

mgmt::MgSnapshot Server::build_mg_snapshot() const {
    mgmt::MgSnapshot snap;
    auto sessions = sessions_snapshot();

    snap.connections = static_cast<std::uint32_t>(sessions.size());
    snap.server_type = 1;   // 1 = remote server
    snap.server_port = port_;
    snap.rss_bytes   = openads::platform::process_rss_bytes();

    {
        std::lock_guard<std::mutex> g(sessions_mu_);
        snap.worker_threads =
            static_cast<std::uint32_t>(session_threads_.size());
    }

    std::uint32_t conn_no = 1;
    for (const auto& s : sessions) {
        mgmt::MgUser u;
        u.name    = s.user.empty() ? "(anonymous)" : s.user;
        u.address = s.peer_ip + ":" + std::to_string(s.peer_port);
        u.os_login     = u.name;
        u.conn_no      = static_cast<std::uint16_t>(conn_no);
        u.connected_at = s.connected_at;

        for (const auto& tname : s.open_table_names) {
            mgmt::MgTable t;
            t.name    = tname;
            t.user    = u.name;
            t.conn_no = u.conn_no;
            snap.table_list.push_back(std::move(t));
        }

        // One "thread" entry per session — sessions run one dedicated
        // thread per connection here, so the session's last-processed
        // wire opcode is the closest real proxy for "current operation".
        {
            mgmt::MgThread th;
            th.thread_no = static_cast<std::uint32_t>(s.id);
            th.opcode    = s.current_opcode;
            th.user      = u.name;
            th.conn_no   = u.conn_no;
            th.os_login  = u.name;
            snap.thread_list.push_back(std::move(th));
        }

        snap.user_list.push_back(std::move(u));

        snap.tables    += s.open_tables;
        snap.workareas += s.open_tables;
        ++conn_no;
    }
    snap.users = static_cast<std::uint32_t>(snap.user_list.size());

    // Real currently-held record/table locks, attributed via the
    // thread-local LockOwner each session sets right before its own
    // LockRecord/LockTable opcode calls (see session.cpp).
    snap.locks     = mgmt::LockRegistry::instance().count();
    snap.lock_list = mgmt::LockRegistry::instance().snapshot();

    // Fold in this server's cumulative MgStats (uptime, comm totals,
    // high-water marks) so it travels the wire with the live counts.
    mgmt::capture_mg_stats(snap, mgmt::process_mg_stats());
    return snap;
}

std::uint64_t Server::register_session(const SessionInfo& info) {
    std::lock_guard<std::mutex> lk(info_mu_);
    auto id = next_session_id_.fetch_add(1);
    SessionInfo si = info;
    si.id = id;
    sessions_info_.emplace(id, std::move(si));
    // M9.25 — raise the connection high-water mark under info_mu_.
    openads::mgmt::MgStats::bump_max(
        openads::mgmt::process_mg_stats().max_connections,
        static_cast<std::uint32_t>(sessions_info_.size()));
    return id;
}

void Server::unregister_session(std::uint64_t id) {
    std::lock_guard<std::mutex> lk(info_mu_);
    sessions_info_.erase(id);
    // M9.25 — count one disconnect per terminated session. This runs
    // exactly once per session via session_loop's SessionGuard dtor.
    openads::mgmt::process_mg_stats()
        .disconnects.fetch_add(1, std::memory_order_relaxed);
}

void Server::touch_session(std::uint64_t id, bool inbound, bool outbound) {
    std::lock_guard<std::mutex> lk(info_mu_);
    auto it = sessions_info_.find(id);
    if (it == sessions_info_.end()) return;
    it->second.last_activity = std::chrono::system_clock::now();
    if (inbound)  ++it->second.frames_in;
    if (outbound) ++it->second.frames_out;
}

void Server::set_session_opcode(std::uint64_t id, std::uint16_t opcode) {
    std::lock_guard<std::mutex> lk(info_mu_);
    auto it = sessions_info_.find(id);
    if (it == sessions_info_.end()) return;
    it->second.current_opcode = opcode;
}

void Server::set_session_user(std::uint64_t id,
                               const std::string& user,
                               const std::string& data_dir) {
    std::lock_guard<std::mutex> lk(info_mu_);
    auto it = sessions_info_.find(id);
    if (it == sessions_info_.end()) return;
    it->second.user     = user;
    it->second.data_dir = data_dir;
}

void Server::add_session_table(std::uint64_t id, std::int32_t delta,
                                const std::string& table_name) {
    std::lock_guard<std::mutex> lk(info_mu_);
    auto it = sessions_info_.find(id);
    if (it == sessions_info_.end()) return;
    auto& n = it->second.open_tables;
    if (delta < 0) {
        std::uint32_t d = static_cast<std::uint32_t>(-delta);
        n = (n > d) ? n - d : 0;
        if (!table_name.empty()) {
            auto& names = it->second.open_table_names;
            auto nit = std::find(names.begin(), names.end(), table_name);
            if (nit != names.end()) names.erase(nit);
        }
    } else {
        n += static_cast<std::uint32_t>(delta);
        if (!table_name.empty()) {
            it->second.open_table_names.push_back(table_name);
        }
    }
}

void Server::install_session_socket(std::uint64_t id, Socket s) {
    std::lock_guard<std::mutex> lk(info_mu_);
    sockets_[id] = s;
}

void Server::erase_session_socket(std::uint64_t id) {
    std::lock_guard<std::mutex> lk(info_mu_);
    sockets_.erase(id);
}

bool Server::kill_session(std::uint64_t id) {
    Socket sock;
    {
        std::lock_guard<std::mutex> lk(info_mu_);
        auto it = sockets_.find(id);
        if (it == sockets_.end()) return false;
        sock = it->second;
        sockets_.erase(it);
    }
    // Closing the socket from outside the owning thread wakes its
    // blocking recv() with EINTR / connection reset; session_loop
    // breaks out of its while-loop and the unregister cleanup
    // runs as normal.
    if (sock.valid()) sock_close(sock);
    return true;
}

bool Server::kill_session_by_conn_no(std::uint16_t conn_no) {
    auto sessions = sessions_snapshot();
    if (conn_no == 0 || conn_no > sessions.size()) return false;
    return kill_session(sessions[conn_no - 1].id);
}

util::Result<void> Server::start(const std::string& host,
                                 std::uint16_t port) {
    if (running_.load()) return {};
    // Resolve enterprise limits once at start. A test/embedder override wins;
    // otherwise take the env-loaded EnterpriseConfig (OPENADS_SERVER_*).
    const auto& ecfg = openads::sql_backend::enterprise_config();
    max_sessions_ = (max_sessions_override_ != 0)
                        ? max_sessions_override_
                        : ecfg.server_max_sessions;
    ListenerOptions opts;
    opts.host = host;
    opts.port = port;
    opts.backlog = static_cast<int>(ecfg.server_listen_backlog);
    auto l = listen_tcp(opts);
    if (!l) return l.error();
    listener_ = l.value();
    auto p = socket_local_port(listener_);
    if (!p) {
        sock_close(listener_);
        return p.error();
    }
    port_ = p.value();
    // M9.25 — fix the telemetry uptime origin at server start.
    openads::mgmt::process_mg_stats().start_time =
        std::chrono::system_clock::now();
    running_.store(true);
    // Enterprise step 3 — if the sharded-reactor pool is enabled, stand it up
    // before the accept loop so accept_loop hands sockets to it. Env-read (not
    // the cached config singleton) so it is honored even when the singleton was
    // already materialized earlier in the process.
    if (openads::sql_backend::enterprise_server_pool_enabled()) {
        pool_ = std::make_unique<WorkerPool>(
            *this, openads::sql_backend::enterprise_server_pool_workers());
        pool_->start();
    }
    accept_thread_ = std::thread([this]() { this->accept_loop(); });
    return {};
}

void Server::stop() noexcept {
    if (!running_.exchange(false)) return;
    // Closing the listener wakes blocking accept() on Linux + Win32,
    // but macOS BSD sockets don't always abort a pending accept on
    // close/shutdown. Force a self-connect on the listener's port
    // first so accept() returns; the accept-loop then notices
    // running_ == false and exits.
    auto port_r = socket_local_port(listener_);
    if (port_r) {
        auto wake = connect_tcp("127.0.0.1", port_r.value());
        if (wake) sock_close(wake.value());
    }
    sock_close(listener_);
    if (accept_thread_.joinable()) accept_thread_.join();
    // Enterprise step 3 — tear the reactor pool down (joins its workers, which
    // close + unregister every live session). No-op in the legacy path.
    if (pool_) {
        pool_->stop();
        pool_.reset();
    }
    // Wake any session thread blocked in recv() by closing its socket from the
    // outside (same mechanism kill_session uses). Without this, a client that
    // connected but never sent another frame leaves its session thread parked
    // in read_frame forever, and the join() below would hang on it.
    {
        std::lock_guard<std::mutex> lk(info_mu_);
        for (auto& kv : sockets_) {
            Socket s = kv.second;
            sock_close(s);
        }
        sockets_.clear();
    }
    // Move the session-thread set out UNDER sessions_mu_, then join with the
    // lock RELEASED. A session thread, right after session_loop returns, takes
    // sessions_mu_ to record itself in finished_threads_; joining it while we
    // still hold sessions_mu_ would deadlock (it blocks on the mutex, we block
    // on the join).
    std::unordered_map<std::uint64_t, std::thread> to_join;
    {
        std::lock_guard<std::mutex> lk(sessions_mu_);
        to_join.swap(session_threads_);
        finished_threads_.clear();
    }
    for (auto& kv : to_join) {
        if (kv.second.joinable()) kv.second.join();
    }
}

void Server::accept_loop() {
    while (running_.load()) {
        auto cli = accept_one(listener_);
        if (!cli) {
            // accept failed — listener closed by stop().
            break;
        }
        // M12.3 — stop() may have used a self-connect to drain a
        // BSD accept that doesn't honor close-during-accept. If
        // running_ is now false, the connection is the wake-up
        // probe; drop it and exit.
        if (!running_.load()) {
            Socket s = cli.value();
            sock_close(s);
            break;
        }
        Socket s = cli.value();
        // Enterprise step 3 — sharded-reactor path: hand the socket to the
        // least-loaded worker instead of spawning a per-connection thread.
        // The cap counts live pooled connections rather than session threads.
        if (pool_) {
            if (max_sessions_ != 0 &&
                pool_->live_connections() >= max_sessions_) {
                rejected_sessions_.fetch_add(1);
                sock_close(s);
                continue;
            }
            pool_->submit(s);
            continue;
        }
        // Reap threads whose session_loop already returned so the live set
        // stays bounded on a long-running server.
        reap_finished_threads_();
        {
            std::lock_guard<std::mutex> lk(sessions_mu_);
            if (max_sessions_ != 0 &&
                session_threads_.size() >= max_sessions_) {
                // At capacity: refuse this connection instead of spawning an
                // unbounded number of threads. The client sees a dropped
                // connection (a future milestone can send a "busy" frame).
                rejected_sessions_.fetch_add(1);
                sock_close(s);
                continue;
            }
            std::uint64_t tid = thread_seq_.fetch_add(1);
            // The session records its own id in finished_threads_ when its loop
            // returns; the next reap joins and drops it. The push takes
            // sessions_mu_, which this scope holds during emplace, so the id is
            // in the map before the thread can mark itself finished.
            session_threads_.emplace(tid,
                std::thread([this, s, tid]() mutable {
                    this->session_loop(s);
                    std::lock_guard<std::mutex> lk2(sessions_mu_);
                    finished_threads_.push_back(tid);
                }));
        }
    }
}

void Server::reap_finished_threads_() {
    // Collect the finished threads UNDER sessions_mu_, then join them with the
    // lock RELEASED. Joining under the lock would block accept_loop and other
    // exiting session threads (which need sessions_mu_ to register themselves)
    // for the duration of every join — a latency spike under load.
    std::vector<std::thread> to_join;
    {
        std::lock_guard<std::mutex> lk(sessions_mu_);
        for (std::uint64_t id : finished_threads_) {
            auto it = session_threads_.find(id);
            if (it == session_threads_.end()) continue;
            to_join.push_back(std::move(it->second));
            session_threads_.erase(it);
        }
        finished_threads_.clear();
    }
    for (auto& t : to_join) {
        if (t.joinable()) t.join();
    }
}

std::uint32_t Server::active_session_threads() const {
    std::lock_guard<std::mutex> lk(sessions_mu_);
    return static_cast<std::uint32_t>(session_threads_.size());
}

void Server::session_loop(Socket s) {
    // The per-frame contract (read → dispatch → reply → telemetry) lives in
    // Session::handle_readable so the reactor WorkerPool shares it verbatim.
    Session sess(*this, s);
    while (sess.handle_readable()) {}
    sock_close(s);
}


} // namespace openads::network
