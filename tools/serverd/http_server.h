#pragma once

#if defined(OPENADS_WITH_HTTP)

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>

namespace httplib {
class Server;
}

namespace openads::network {
class Server;
}

namespace openads::studio {

// studio.web.0.1 — embedded HTTP web console hosted inside the
// openads_serverd CLI. The Phase 2 wire (`tcp://` / `tls://`)
// keeps running on its own port; this class adds a parallel HTTP
// listener that:
//
//   * serves a single-page admin UI (HTML / CSS / JS) embedded
//     in the binary at compile time;
//   * exposes a small REST surface under `/api/` that wraps the
//     same `Ads*` C ABI surface the rest of the engine uses.
//
// The HTTP listener runs the database operations through a
// short-lived ABI connection per request (M12.7 pattern). State
// (open tables / cursors) lives only for the duration of the
// response.

class HttpConsole {
public:
    HttpConsole();
    ~HttpConsole();
    HttpConsole(const HttpConsole&) = delete;
    HttpConsole& operator=(const HttpConsole&) = delete;

    // `data_dir` is the on-disk directory the engine connects to
    // for every request. `host` / `port` follow the same defaults
    // as the wire server.
    bool start(const std::string& host,
               std::uint16_t      port,
               const std::string& data_dir,
               openads::network::Server* wire_srv = nullptr);
    void stop();
    bool running() const noexcept { return running_.load(); }

    // studio.web.0.8 — HTTP Basic-auth credentials. When at least
    // one user/password pair is registered, every request must
    // carry an `Authorization: Basic …` header that matches.
    void add_user(const std::string& user, const std::string& password);

    // Background reverse-DNS cache for client IPs (computer name of
    // clients that do not report one). Defined in http_server.cpp.
    struct HostCache;

private:
    std::unique_ptr<HostCache>       hosts_;
    std::unique_ptr<httplib::Server> srv_;
    std::thread                      thread_;
    std::atomic<bool>                running_{false};
    std::string                      data_dir_;
    openads::network::Server*        wire_srv_ = nullptr;
    std::unordered_map<std::string, std::string> users_;
};

} // namespace openads::studio

#endif // OPENADS_WITH_HTTP
