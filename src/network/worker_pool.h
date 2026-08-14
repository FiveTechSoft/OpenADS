#pragma once

#include "network/socket.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace openads::network {

class Server;

// Enterprise step 3 — sharded reactor. A fixed pool of worker threads
// multiplexes many connections via socket_poll, instead of one thread
// per connection. Each accepted socket is assigned to exactly one worker
// for its whole lifetime (submit -> that worker's poll set), so the ADS
// ABI handles a connection owns are only ever touched by a single thread
// (the same thread-affinity guarantee the legacy thread-per-connection
// path had). Thread count stays bounded at the worker count regardless of
// how many connections are open; idle connections cost one fd in a poll
// set, not a thread + stack.
class WorkerPool {
public:
    // workers == 0 resolves to hardware_concurrency() (min 1).
    WorkerPool(Server& srv, std::uint32_t workers);
    ~WorkerPool();
    WorkerPool(const WorkerPool&)            = delete;
    WorkerPool& operator=(const WorkerPool&) = delete;

    void start();
    // Hand a freshly-accepted connection socket to the least-loaded worker.
    // Takes ownership of the socket. Safe to call from the accept thread.
    void submit(Socket s, std::string default_data_dir,
                std::uint16_t listener_port);
    void stop();

    std::uint32_t worker_count() const noexcept { return nworkers_; }
    // Sum of live connections across all workers (telemetry / tests).
    std::uint32_t live_connections() const noexcept;

private:
    struct Worker;                       // defined in the .cpp
    void worker_loop(Worker& w);

    Server*                              srv_;
    std::uint32_t                        nworkers_ = 1;
    std::vector<std::unique_ptr<Worker>> workers_;
    std::atomic<bool>                    running_{false};
};

} // namespace openads::network
