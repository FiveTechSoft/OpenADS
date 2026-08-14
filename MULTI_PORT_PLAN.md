# Multi-Port Support for OpenADS

**Goal**: Allow a single OpenADS server instance to listen on multiple TCP ports, each with its own data directory — matching LetoDB's `Port=` / `DataPath=` capability.

**Use case**: Two apps share the same server machine and storage backend but connect via different ports/URIs. Each port routes to a different endpoint folder. One process, one config file, multiple logical servers.

---

## Architecture Overview

```
                     ┌─────────────────────────────┐
                     │      openads.ini             │
                     │  [server]                    │
                     │  port = 6262                 │
                     │  data = C:\shared            │
                     │                              │
                     │  [port:6263]                 │
                     │  data = C:\app1              │
                     │                              │
                     │  [port:6264]                 │
                     │  data = C:\app2              │
                     └─────────────────────────────┘
                                │
                                v
                     ┌─────────────────────────────┐
                     │   Server (single process)    │
                     │                              │
                     │  listener_ (port 6262) ──────┼──► data = C:\shared
                     │  listeners_[6263] ───────────┼──► data = C:\app1
                     │  listeners_[6264] ───────────┼──► data = C:\app2
                     │                              │
                     │  accept_loop() multiplexes   │
                     │  all listeners               │
                     └─────────────────────────────┘
```

Each accepted socket is tagged with the port it came from. The tag determines which `data_dir` roots are used when jailing the client's Connect path. Everything else (session registry, worker pool, auth, management) is shared.

---

## Phase 1: Data Model & Config (3 files)

### 1.1 `IniConfig` struct — `tools/serverd/config_ini.h`

Add a vector of extra port entries:

```cpp
struct PortEntry {
    std::uint16_t port = 0;
    std::string   data_dir;
};

struct IniConfig {
    // ... existing fields unchanged ...
    std::vector<PortEntry> extra_ports;   // <-- NEW
};
```

### 1.2 INI parser — `tools/serverd/config_ini.cpp`

Add parsing for `[port:NNNN]` section headers. The parser currently skips all `[section]` lines (line 58). Change to:

```cpp
if (t.front() == '[' && t.back() == ']') {
    std::string sec = trim(t.substr(1, t.size() - 2));
    if (sec.rfind("port:", 0) == 0) {           // starts with "port:"
        std::string num = sec.substr(5);         // "6263"
        unsigned long n = 0;
        if (parse_uint(num, 65535, n)) {
            current_port_entry = static_cast<std::uint16_t>(n);
            current_port_data.clear();
        }
    } else {
        current_port_entry = 0;                  // back to [server] or unknown
    }
    continue;
}
```

Inside the key=value loop, when `current_port_entry != 0`, accept `data` / `data_dir` / `datadir` and store into a temporary. On section change or EOF, push the `PortEntry` into `out.extra_ports`.

**New keys per `[port:NNNN]` section** (minimum viable):
- `data` / `data_dir` — the data root for this port

**Future extension** (not in Phase 1):
- `legacy_paths` per port
- `enable_file_func` per port
- `auth_user` per port

### 1.3 Sample INI — `openads.ini.sample`

Add a documented example:

```ini
# Multi-port: additional listeners with their own data directories.
# Each [port:NNNN] section creates a separate listener on TCP port NNNN.
# Clients connecting to that port see only the data= folder below.
# [port:6263]
# data = C:\app1
#
# [port:6264]
# data = C:\app2
```

---

## Phase 2: Server Class (2 files)

### 2.1 `Server` — `src/network/server.h`

**New struct** (inside `Server` or in `openads::network`):

```cpp
struct ListenerEntry {
    std::uint16_t port = 0;
    std::string   data_dir;      // per-port data roots (semicolon-separated)
    Socket        listener = INVALID_SOCKET;
};
```

**New members** in `Server`:

```cpp
private:
    // Existing single-port fields stay for backward compat:
    Socket        listener_;       // <-- kept, now also stored in extra_listeners_[port_]
    std::uint16_t port_ = 0;

    // NEW: extra listeners keyed by port
    std::vector<ListenerEntry> extra_listeners_;

    // Map from listener socket fd → data_dir for that port.
    // Used by accept_loop to tag each accepted connection.
    std::unordered_map<Socket, std::string> listener_data_dir_;
```

**New public methods**:

```cpp
// Add an extra listener port with its own data directory.
// Must be called before start(). Returns error if port is already bound.
util::Result<void> add_listener(const std::string& host,
                                std::uint16_t port,
                                const std::string& data_dir);

// Query which data_dir a given listener socket serves.
// Returns nullopt if the socket is the primary listener (uses data_dir_).
const std::string* data_dir_for_listener(Socket s) const;
```

### 2.2 `Server` — `src/network/server.cpp`

**`add_listener()`**:

```cpp
util::Result<void> Server::add_listener(const std::string& host,
                                        std::uint16_t port,
                                        const std::string& data_dir) {
    // Check for duplicate port
    if (port == port_) return error("port already used by primary listener");
    for (auto& e : extra_listeners_)
        if (e.port == port) return error("port already registered");

    ListenerOptions opts;
    opts.host = host;
    opts.port = port;
    opts.backlog = /* from enterprise config */;
    auto l = listen_tcp(opts);
    if (!l) return l.error();

    auto p = socket_local_port(l.value());
    if (!p) { sock_close(l.value()); return p.error(); }

    ListenerEntry entry;
    entry.port = p.value();
    entry.data_dir = data_dir;
    entry.listener = l.value();
    extra_listeners_.push_back(std::move(entry));
    listener_data_dir_[entry.listener] = entry.data_dir;
    return {};
}
```

**`start()`** — after binding the primary listener, bind extras:

```cpp
util::Result<void> Server::start(const std::string& host, std::uint16_t port) {
    // ... existing primary listener setup (unchanged) ...
    listener_data_dir_[listener_] = data_dir_;  // primary maps to global data_dir_

    // Bind extra listeners
    for (auto& entry : extra_listeners_) {
        // already bound by add_listener(); just record mapping
        listener_data_dir_[entry.listener] = entry.data_dir;
    }

    // ... rest of start() unchanged ...
}
```

**`accept_loop()`** — the critical change. Use `socket_poll` (WSAPoll / epoll) to multiplex across all listeners:

```cpp
void Server::accept_loop() {
    // Build poll set: primary listener + all extras
    std::vector<struct pollfd> pfds;
    std::vector<Socket>        poll_sockets;  // index → Socket
    auto add_listener_to_poll = [&](Socket s) {
        struct pollfd pfd;
        pfd.fd = s;
        pfd.events = POLLIN;
        pfds.push_back(pfd);
        poll_sockets.push_back(s);
    };
    add_listener_to_poll(listener_);
    for (auto& e : extra_listeners_)
        add_listener_to_poll(e.listener);

    while (running_.load()) {
        int n = WSAPoll(pfds.data(), static_cast<ULONG>(pfds.size()), 200);
        if (n <= 0) continue;

        for (std::size_t i = 0; i < pfds.size(); ++i) {
            if (!(pfds[i].revents & POLLIN)) continue;
            Socket listener = poll_sockets[i];

            auto cli = accept_one(listener);
            if (!cli) break;
            if (!running_.load()) { sock_close(cli.value()); return; }

            Socket s = cli.value();

            // Tag the socket with the listener's data_dir
            // (stored in a thread-safe side channel — see below)
            // ... same pool/thread logic as today ...
        }
    }
}
```

**Tagging the data_dir per accepted socket**: The `session_loop` needs to know which `data_dir` to use when the client sends an empty `dir` in the Connect frame. Two approaches:

**Option A (simpler, recommended)**: Store a `std::unordered_map<Socket, std::string>` under a mutex. In `accept_loop`, after `accept_one`, insert `{s → data_dir_for_listener[listener]}`. In `session_loop`, read and erase the entry. The map is short-lived (between accept and first Connect frame).

**Option B (zero-copy)**: Add a `std::string pending_data_dir_` member to `Session`. Set it in the `session_loop` constructor from a parameter passed by `accept_loop`. This avoids the map lookup entirely.

**Recommendation**: Option B — add a `std::string default_data_dir_` parameter to `session_loop()`:

```cpp
void Server::session_loop(Socket s, std::string default_data_dir);
```

And in `accept_loop`:
```cpp
std::string dd = listener_data_dir_[listener];
session_threads_.emplace(tid,
    std::thread([this, s, tid, dd = std::move(dd)]() mutable {
        this->session_loop(s, std::move(dd));
        // ...
    }));
```

**`stop()`** — close all extra listeners:

```cpp
void Server::stop() noexcept {
    // ... existing logic for primary listener ...

    // Close extra listeners
    for (auto& e : extra_listeners_) {
        auto port_r = socket_local_port(e.listener);
        if (port_r) {
            auto wake = connect_tcp("127.0.0.1", port_r.value());
            if (wake) sock_close(wake.value());
        }
        sock_close(e.listener);
    }
    extra_listeners_.clear();
    listener_data_dir_.clear();
    // ...
}
```

---

## Phase 3: Session & Connect Opcode (1 file)

### 3.1 `session_loop` — `src/network/server.cpp`

Change signature:

```cpp
void Server::session_loop(Socket s, std::string default_data_dir);
```

Pass `default_data_dir` down to the Session or use it directly in the Connect opcode.

### 3.2 Connect opcode — `src/network/session.cpp` (lines 983-1148)

The key change is in the data_dir resolution block (lines 180-194 of the analysis):

```cpp
// CURRENT:
std::string resolved = dir;
if (!srv_->data_dir_.empty()) {
    auto roots = openads::platform::split_data_roots(srv_->data_dir_);
    // ... resolve ...
}

// NEW: use port-specific data_dir when client sends empty dir
std::string effective_data_dir = default_data_dir_.empty()
                                    ? srv_->data_dir_
                                    : default_data_dir_;
std::string resolved = dir;
if (!effective_data_dir.empty()) {
    auto roots = openads::platform::split_data_roots(effective_data_dir);
    // ... resolve ...
}
```

The `default_data_dir_` is set from the listener's data_dir (passed via `session_loop` parameter or stored in the Session).

### 3.3 SessionInfo — `src/network/server.h`

Add the listening port to `SessionInfo` so the Studio console can show which port a session connected to:

```cpp
struct SessionInfo {
    // ... existing fields ...
    std::uint16_t listener_port = 0;   // <-- NEW: which port the client connected to
};
```

---

## Phase 4: CLI & main() (1 file)

### 4.1 `Args` struct — `tools/serverd/main.cpp`

Add repeatable `--port` and `--data` for extra listeners:

```cpp
struct Args {
    // ... existing fields ...
    std::vector<PortEntry> extra_listeners;   // <-- NEW
};
```

Where `PortEntry` is:

```cpp
struct PortEntry {
    std::uint16_t port;
    std::string   data_dir;
};
```

### 4.2 CLI parsing — `parse_args()`

Support `--port 6263:data=C:\app1` or two-flag syntax:

```
--port 6263 --data C:\app1 --port 6264 --data C:\app2
```

**Recommended syntax** (unambiguous, backward-compatible):

```
--listen 6263:C:\app1 --listen 6264:C:\app2
```

Or keep the existing `--port`/`--data` pair behavior and add:

```
--extra-port 6263 --extra-data C:\app1
--extra-port 6264 --extra-data C:\app2
```

### 4.3 `run_server()` — `tools/serverd/main.cpp`

After creating the Server and calling `set_data_dir()`:

```cpp
for (auto& e : args.extra_listeners) {
    auto r = srv.add_listener(args.host, e.port, e.data_dir);
    if (!r) {
        std::fprintf(stderr, "extra listener on port %u failed: %s\n",
                     e.port, r.error().message.c_str());
        return 1;
    }
}
```

### 4.4 `usage()` — update help text

Add documentation for the new `--listen` or `--extra-port` flags.

---

## Phase 5: Management & Observability (2 files)

### 5.1 `sessions_snapshot()` — `src/network/server.cpp`

Include `listener_port` in the snapshot so Studio shows which port each session uses.

### 5.2 HTTP Studio console — `tools/serverd/http_server.h`

Add a "Listeners" tab or extend the "Server" tab to show all bound ports and their data directories.

### 5.3 `AdsMgGetServerInfo` — management opcode

Return the list of bound ports in the server info response so clients can discover available endpoints.

---

## Phase 6: Build & Test

### 6.1 CMakeLists.txt

No new source files needed — all changes are in existing files. No new dependencies.

### 6.2 Unit tests

Add tests in `tests/`:
- `test_multi_port_config.cpp`: parse INI with `[port:NNNN]` sections
- `test_multi_port_server.cpp`: bind two listeners, connect to each, verify data_dir isolation
- `test_multi_port_connect.cpp`: client on port A cannot see tables from port B's data_dir

### 6.3 Integration test

```bash
# Start server with multi-port config
openads_serverd --config multi_port.ini

# Terminal 1: connect to port 6262
# Terminal 2: connect to port 6263
# Verify each sees different tables
```

---

## Files Changed (summary)

| File | Change |
|------|--------|
| `tools/serverd/config_ini.h` | Add `PortEntry` struct, `extra_ports` vector |
| `tools/serverd/config_ini.cpp` | Parse `[port:NNNN]` sections |
| `src/network/server.h` | Add `ListenerEntry`, `extra_listeners_`, `add_listener()`, `listener_port` in SessionInfo |
| `src/network/server.cpp` | `add_listener()`, `accept_loop()` multiplex, `stop()` cleanup, `session_loop()` signature |
| `src/network/session.cpp` | Connect opcode: use port-specific `data_dir` |
| `tools/serverd/main.cpp` | `--listen` CLI flag, `run_server()` extra listeners |
| `openads.ini.sample` | Document `[port:NNNN]` sections |
| `tests/test_multi_port_*.cpp` | New test files |

---

## Backward Compatibility

- **Fully backward compatible**: single-port config works unchanged
- `[port:NNNN]` sections are only created when explicitly configured
- `--listen` flag is optional; omitting it gives current behavior
- Primary port (default 6262) always uses the `[server]` data= setting
- No wire protocol changes — clients are unaware of multi-port

---

## Implementation Order

1. **Config parsing** (Phase 1) — smallest, testable independently
2. **Server class** (Phase 2) — core multi-listener support
3. **Session/Connect** (Phase 3) — wire the port-specific data_dir through
4. **CLI** (Phase 4) — expose to operators
5. **Management** (Phase 5) — observability
6. **Tests** (Phase 6) — validate everything

Estimated effort: 2-3 focused sessions.
