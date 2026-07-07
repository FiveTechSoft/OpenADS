#pragma once

#include "mgmt/mg_snapshot.h"
#include "network/socket.h"
#include "network/transport.h"
#include "network/wire.h"
#include "util/result.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace openads::network {

class WorkerPool;   // enterprise step 3 — sharded reactor (network/worker_pool.h)

// M12.3 — Phase 2 server skeleton. Spawns an accept thread that
// hands each connection to a dedicated session thread. The session
// thread reads framed messages and dispatches a small handful of
// opcodes:
//
//   Hello    → HelloAck (payload = server version string)
//   Connect  → ConnectAck (payload = "connected:<data_dir>")
//   any other → Error (payload = "unsupported opcode")
//
// Real remote table ops (OpenTable, ExecuteSQL, Fetch) plug into
// this dispatcher in M12.4.

class Server {
    // SLICE 3a — Session reaches the shared session registry helpers
    // (register_session, install/erase_session_socket, unregister,
    // set_session_user, add_session_table, build_mg_snapshot,
    // kill_session_by_conn_no), creds_, data_dir_, require_auth().
    friend class Session;

public:
    // Out-of-line ctor/dtor: the unique_ptr<WorkerPool> member is an
    // incomplete type in this header, so the special members that would
    // destroy it must be defined in server.cpp (which sees the full type).
    Server();
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;
    ~Server();

    util::Result<void> start(const std::string& host,
                             std::uint16_t port);
    std::uint16_t      port() const noexcept { return port_; }
    bool               running() const noexcept { return running_.load(); }
    void               stop() noexcept;

    // M12.9 — auth. When at least one credential is registered, every
    // Connect frame must carry a matching user / password pair; an
    // empty map accepts any client (back-compat / dev mode).
    void add_credential(const std::string& user,
                        const std::string& password);
    bool require_auth() const noexcept;

    // Set the data root directory (or directories). Relative paths from
    // Connect frames are resolved under this directory; a Connect whose
    // path escapes it is rejected. `dir` may list more than one root
    // separated by ';' (e.g. "C:\data;D:\more-data") — a Connect path only
    // has to fall under one of them. See platform::split_data_roots /
    // platform::resolve_under_any_root.
    void set_data_dir(const std::string& dir) { data_dir_ = dir; }

    // Enterprise: cap on concurrent session threads (0 = unlimited). Overrides
    // the env-loaded EnterpriseConfig value; call before start(). Production
    // reads OPENADS_SERVER_MAX_SESSIONS; this exists mainly for tests.
    void set_max_sessions(std::uint32_t n) { max_sessions_override_ = n; }
    // Observability: live session-thread count, and the number of connections
    // refused because the cap was reached.
    std::uint32_t active_session_threads() const;
    std::uint32_t rejected_sessions() const noexcept {
        return rejected_sessions_.load();
    }

    // studio.web.0.4 — observable session info exposed for the
    // Studio "Sessions" tab. Snapshot is taken under a mutex so
    // concurrent reads from the HTTP console are safe.
    struct SessionInfo {
        std::uint64_t                                       id = 0;
        std::string                                         peer_ip;
        std::uint16_t                                       peer_port = 0;
        std::string                                         user;
        std::string                                         data_dir;
        std::chrono::system_clock::time_point               connected_at{};
        std::chrono::system_clock::time_point               last_activity{};
        std::uint64_t                                       frames_in   = 0;
        std::uint64_t                                       frames_out  = 0;
        std::uint32_t                                       open_tables = 0;
    };
    std::vector<SessionInfo> sessions_snapshot() const;

    // M9.25 — build a management telemetry snapshot from the live
    // session registry. Used by the MgRequest opcode handler.
    mgmt::MgSnapshot build_mg_snapshot() const;

private:
    void accept_loop();
    void session_loop(Socket s);
    // Join + drop every session thread whose loop has returned. Bounds the
    // live thread set on a long-running server. Caller must NOT hold
    // sessions_mu_ (this takes it).
    void reap_finished_threads_();

    Socket                   listener_;
    std::uint16_t            port_ = 0;
    std::atomic<bool>        running_{false};
    std::thread              accept_thread_;
    mutable std::mutex       sessions_mu_;
    // Live session threads keyed by a spawn-time id, plus the ids of threads
    // whose session_loop has returned. accept_loop reaps finished threads each
    // iteration so the set stays bounded — it previously only grew (joined
    // only at shutdown), leaking thread handles on a 24x7 server. Guarded by
    // sessions_mu_.
    std::unordered_map<std::uint64_t, std::thread> session_threads_;
    std::vector<std::uint64_t>                     finished_threads_;
    std::atomic<std::uint64_t>                     thread_seq_{1};
    // Enterprise step 3 — when the sharded-reactor pool is enabled
    // (OPENADS_SERVER_POOL), accept_loop hands each accepted socket to this
    // pool instead of spawning a per-connection thread. Null = legacy path.
    std::unique_ptr<WorkerPool>                    pool_;
    // Enterprise limits resolved at start() (0 = unlimited). The override lets
    // tests inject a small cap without touching the env-loaded singleton.
    std::uint32_t                                  max_sessions_ = 0;
    std::uint32_t                                  max_sessions_override_ = 0;
    std::atomic<std::uint32_t>                     rejected_sessions_{0};

    // Data root directory: relative client paths are resolved under it.
    std::string                                   data_dir_;

    // M12.9 — credential map (user -> password). Protected by creds_mu_
    // because add_credential() may run while sessions authenticate.
    mutable std::mutex                           creds_mu_;
    std::unordered_map<std::string, std::string> creds_;

    // studio.web.0.4 — live session registry. session_loop
    // updates these counters; the HTTP console reads them via
    // sessions_snapshot().
    mutable std::mutex                          info_mu_;
    std::unordered_map<std::uint64_t, SessionInfo> sessions_info_;
    std::atomic<std::uint64_t>                  next_session_id_{1};

public:
    // Internal helpers used by session_loop; public so the
    // anonymous-namespace functions in server.cpp can reach
    // them without exposing extra friend declarations.
    std::uint64_t register_session(const SessionInfo& info);
    void          unregister_session(std::uint64_t id);
    void          touch_session(std::uint64_t id, bool inbound,
                                 bool outbound);
    void          set_session_user(std::uint64_t id,
                                    const std::string& user,
                                    const std::string& data_dir);
    void          add_session_table(std::uint64_t id,
                                     std::int32_t delta);

    // studio.web.0.11 — drop a single session by id (closes its
    // socket; the session_loop's next read_frame returns and the
    // session thread exits cleanly). Returns true if the id was
    // known.
    bool          kill_session(std::uint64_t id);
    // Internal — session_loop's RAII guard uses these to install /
    // remove its accepted socket from the kill registry.
    void          install_session_socket(std::uint64_t id, Socket s);
    void          erase_session_socket  (std::uint64_t id);

private:
    // Kill the session whose 1-based position in sessions_snapshot()
    // order equals conn_no. Returns true if one was found.
    bool kill_session_by_conn_no(std::uint16_t conn_no);

    // Track each session_loop's accepted socket so kill_session
    // can close it from outside the thread that owns it.
    std::unordered_map<std::uint64_t, Socket> sockets_;
};

// Read exactly `n` bytes into `buf` (handles partial recvs).
util::Result<void> recv_exact(Socket& s, std::uint8_t* buf,
                              std::size_t n);
util::Result<void> recv_exact(ITransport& t, std::uint8_t* buf,
                              std::size_t n);

// Read one wire frame (4-byte BE length + opcode + payload).
util::Result<Frame> read_frame(Socket& s);
util::Result<Frame> read_frame(ITransport& t);

// Write `f` to `s` as a single wire frame.
util::Result<void> write_frame(Socket& s, const Frame& f);
util::Result<void> write_frame(ITransport& t, const Frame& f);

} // namespace openads::network
