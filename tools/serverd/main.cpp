// openads_serverd — standalone TCP server CLI / Windows service.
//
// Wraps openads::network::Server in a long-lived process. Parses
// --host / --port / --backlog from argv, prints the bound port,
// blocks on a signal-handled exit so a Harbour client (or any
// rddads app) can reach the server over LAN.
//
// On Windows the same binary doubles as a Windows Service: pass
// `--install-service [extra flags...]` to register with the SCM
// (run elevated; "OpenADS Database Server" appears under
// Services), `--uninstall-service` to drop the registration, or
// `--service` (used internally by the SCM dispatcher; not for
// interactive use).

#include "mgmt/error_log.h"
#include "network/server.h"
#include "platform/dll.h"
#include "openads_version.h"  // OPENADS_VERSION_STR (CMake-generated)
#include "platform/path.h"
#include "tools/serverd/config_ini.h"
#include "tools/serverd/setup_wizard.h"
#if defined(OPENADS_WITH_HTTP)
#include "tools/serverd/http_server.h"
#endif

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

namespace {

std::atomic<bool> g_running{true};

void on_signal(int) { g_running.store(false); }

void usage(const char* argv0) {
    std::fprintf(stderr,
        "usage: %s [--host HOST] [--port PORT] [--backlog N]\n"
        "          [--http_port PORT] [--data DIR] [--auth_user U:P]...\n"
        "          [--http_user U:P]... [--error_log_path DIR]\n"
        "          [--error_log_max KB]\n"
        "          [--listen PORT:DIR]...\n"
        "Notation: '_' everywhere — on the command line both spellings of\n"
        "a phrase are accepted (--http_port == --http-port), but the\n"
        "canonical form is with underscore, matching openads.ini keys and\n"
        "OPENADS_* env variables.\n"
        "  --host       bind address (default 0.0.0.0)\n"
        "  --port       TCP wire port (default 6262, 0 = ephemeral)\n"
        "  --backlog    listen() backlog (default: env OPENADS_SERVER_BACKLOG,\n"
        "               else 256; ini: backlog)\n"
        "  --max_sessions N  cap on concurrent client sessions\n"
        "               (default: env OPENADS_SERVER_MAX_SESSIONS, else 500;\n"
        "               0 = unlimited; ini: max_sessions)\n"
        "  --http_port  if set, expose Studio web console on this port\n"
        "  --data       physical data root(s) on the SERVER (the jail).\n"
        "               Prefer forward slashes: --data C:/Temp\n"
        "               Semicolon-separated for more than one root,\n"
        "               e.g. --data \"C:/data;D:/more-data\"\n"
        "               (default = current working directory). Do NOT put\n"
        "               the URI drive letter here (not \"/Temp/C:\").\n"
        "  --listen PORT:DIR  extra listener on PORT serving tables from DIR.\n"
        "               Repeatable: --listen 6263:C:/app1 --listen 6264:C:/app2\n"
        "               Each port gets its own data jail (LetoDB-style).\n"
        "  --enable_file_func   allow remote oads_*/Ads* filesystem ops\n"
        "               under --data (default OFF; see enable_file_func)\n"
        "  --legacy_paths       remount client-absolute paths onto --data\n"
        "               (case-insensitive, ignore drive letter). ERP apps\n"
        "               that USE \"C:/Creative.RAM/...\" or \"E:/...\" keep\n"
        "               their sources; files land under --data. URI stays\n"
        "               logical (tcp://host:6262/C:/). Default OFF.\n"
        "               See cookbook/docs/local-and-remote.md\n"
        "  --error_log_path DIR  directory for ads_err.dbf (SAP-style\n"
        "               error log). Env OPENADS_ERROR_LOG_PATH also works.\n"
        "               ini: error_log_path / error_assert_logs\n"
        "  --error_log_max KB    max size of ads_err.dbf (default 1000).\n"
        "               ini: error_log_max. See docs/en/error-log.md\n"
        "  --http_user  user:password — register a Studio login\n"
        "               (repeatable; if none given, console is open)\n"
        "  --auth_user  user:password — require this login for TCP\n"
        "               AdsConnect60 connections (repeatable)\n"
        "  --config     read settings from an openads.ini file (CLI flags\n"
        "               given after it still win); see openads.ini.sample\n"
        "  --setup      interactive first-run wizard: asks port/path/etc,\n"
        "               writes an openads.ini and (optionally) a service\n"
        "  --version    print version + exit\n"
#if defined(_WIN32)
        "  --install_service [extra-flags...]   register Windows Service\n"
        "  --uninstall_service                  deregister Windows Service\n"
        "  --service                            (internal — used by SCM)\n"
#endif
        ,
        argv0);
}

// Args parsed from argv. Defaults match the original CLI.
struct Args {
    std::string   host        = "0.0.0.0";
    std::uint16_t port        = 6262;
    int           backlog     = 16;
    // Cap on concurrent sessions (0 = unlimited). Defaults to 0 here so the
    // env-loaded EnterpriseConfig default (500) applies; an explicit
    // --max-sessions / max_sessions= ini key overrides it.
    std::uint32_t max_sessions = 0;
    std::uint16_t http_port   = 0;
    std::string   data_dir    = ".";
    bool          enable_file_func = false;
    bool          legacy_paths     = false;
    // ads_err.dbf error log overrides; empty/0 keeps mgmt::ErrorLog's
    // defaults (OPENADS_ERROR_LOG_PATH env, then the SAP default paths).
    std::string   error_log_path;
    std::uint32_t error_log_max_kb = 0;
    std::vector<std::pair<std::string, std::string>> http_users;
    std::vector<std::pair<std::string, std::string>> auth_users;
    // Multi-port: extra listeners from --listen PORT:DIR or [port:NNNN] INI.
    std::vector<openads::serverd::PortEntry> extra_listeners;
};

bool parse_args(int argc, char** argv, Args& out) {
    // Canonical CLI spelling folds '-' to '_' after the leading "--", so
    // --max_sessions and --max-sessions are the same flag (one phrase,
    // one canonical form with '_', per the uniform-notation rule).
    auto flag_eq = [](const std::string& a, const char* canon) {
        if (a.size() < 2 || a[0] != '-' || a[1] != '-') return false;
        std::string body = a.substr(2);
        for (char& c : body) {
            if (c == '-') c = '_';
        }
        return body == canon;
    };
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        // Skip Windows-service-mode markers consumed by main().
        if (a == "--service") continue;
        // --config is resolved before parse_args() (see load_config_and_args);
        // here we just consume its value so it is not flagged as unknown.
        if (a == "--config" && i + 1 < argc) { ++i; continue; }
        if      (flag_eq(a, "host")      && i + 1 < argc) out.host    = argv[++i];
        else if (flag_eq(a, "port")      && i + 1 < argc) out.port    = static_cast<std::uint16_t>(std::atoi(argv[++i]));
        else if (flag_eq(a, "backlog")   && i + 1 < argc) out.backlog = std::atoi(argv[++i]);
        else if (flag_eq(a, "max_sessions") && i + 1 < argc)
            out.max_sessions = static_cast<std::uint32_t>(std::atoi(argv[++i]));
        else if (flag_eq(a, "http_port") && i + 1 < argc) out.http_port = static_cast<std::uint16_t>(std::atoi(argv[++i]));
        else if (flag_eq(a, "data")      && i + 1 < argc) out.data_dir = argv[++i];
        else if (flag_eq(a, "enable_file_func")) out.enable_file_func = true;
        else if (flag_eq(a, "disable_file_func")) out.enable_file_func = false;
        else if (flag_eq(a, "legacy_paths")) out.legacy_paths = true;
        else if (flag_eq(a, "disable_legacy_paths")) out.legacy_paths = false;
        else if (flag_eq(a, "listen") && i + 1 < argc) {
            // --listen PORT:DIR  (e.g. --listen 6263:C:/app1)
            std::string spec = argv[++i];
            auto colon = spec.find(':');
            if (colon == std::string::npos || colon == 0) {
                std::fprintf(stderr,
                    "--listen expects PORT:DIR (e.g. 6263:C:/app1)\n");
                return false;
            }
            unsigned long pn = 0;
            if (!openads::serverd::parse_port(spec.substr(0, colon), pn)) {
                std::fprintf(stderr,
                    "--listen: invalid port number '%s'\n",
                    spec.substr(0, colon).c_str());
                return false;
            }
            std::string dir = spec.substr(colon + 1);
            if (dir.empty()) {
                std::fprintf(stderr,
                    "--listen: empty data directory for port %lu\n", pn);
                return false;
            }
            openads::serverd::PortEntry pe;
            pe.port = static_cast<std::uint16_t>(pn);
            pe.data_dir = std::move(dir);
            out.extra_listeners.push_back(std::move(pe));
        }
        else if (flag_eq(a, "error_log_path") && i + 1 < argc)
            out.error_log_path = argv[++i];
        else if (flag_eq(a, "error_log_max")  && i + 1 < argc)
            out.error_log_max_kb =
                static_cast<std::uint32_t>(std::atol(argv[++i]));
        else if (flag_eq(a, "http_user") && i + 1 < argc) {
            std::string up = argv[++i];
            auto colon = up.find(':');
            if (colon == std::string::npos) {
                std::fprintf(stderr,
                    "--http-user expects user:password\n");
                return false;
            }
            out.http_users.emplace_back(up.substr(0, colon),
                                         up.substr(colon + 1));
        }
        else if (flag_eq(a, "auth_user") && i + 1 < argc) {
            std::string up = argv[++i];
            auto colon = up.find(':');
            if (colon == std::string::npos || colon == 0) {
                std::fprintf(stderr,
                    "--auth-user expects user:password\n");
                return false;
            }
            out.auth_users.emplace_back(up.substr(0, colon),
                                        up.substr(colon + 1));
        }
        else {
            std::fprintf(stderr, "unknown arg: %s\n", a.c_str());
            return false;
        }
    }
    return true;
}

// Overlay the keys present in an openads.ini onto `out`. Only fields the
// file actually set are touched, so this sits cleanly between the built-in
// defaults (Args ctor) and the command line: defaults < config file < CLI.
void apply_ini(const openads::serverd::IniConfig& cfg, Args& out) {
    if (cfg.has_host)      out.host      = cfg.host;
    if (cfg.has_port)      out.port      = cfg.port;
    if (cfg.has_backlog)   out.backlog   = cfg.backlog;
    if (cfg.has_max_sessions) out.max_sessions = cfg.max_sessions;
    if (cfg.has_http_port) out.http_port = cfg.http_port;
    if (cfg.has_data)      out.data_dir  = cfg.data_dir;
    if (cfg.has_enable_file_func) out.enable_file_func = cfg.enable_file_func;
    if (cfg.has_legacy_paths)     out.legacy_paths     = cfg.legacy_paths;
    if (cfg.has_error_log_path) out.error_log_path   = cfg.error_log_path;
    if (cfg.has_error_log_max)  out.error_log_max_kb = cfg.error_log_max_kb;
    for (const auto& u : cfg.http_users) out.http_users.push_back(u);
    for (const auto& u : cfg.auth_users) out.auth_users.push_back(u);
    for (const auto& pe : cfg.extra_ports) out.extra_listeners.push_back(pe);
}

// Resolve the effective Args from defaults, an optional `--config <path>`
// file, and the command line — in that precedence order. The config file is
// loaded first (over the defaults) and then parse_args() overlays the CLI so
// an explicit flag always wins over the file. Used by both the interactive
// path (main) and the Windows service path (svc_main).
bool load_config_and_args(int argc, char** argv, Args& out) {
    // First pass: honour the last --config <path> on the line.
    std::string config_path;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--config" && i + 1 < argc) {
            config_path = argv[++i];
        }
    }
    if (!config_path.empty()) {
        openads::serverd::IniConfig cfg;
        std::string err;
        if (!openads::serverd::load_ini_file(config_path, cfg, err)) {
            std::fprintf(stderr, "config: %s\n", err.c_str());
            return false;
        }
        apply_ini(cfg, out);
    }
    // Second pass: CLI flags overlay the file.
    return parse_args(argc, argv, out);
}

// Best-effort absolute path to this executable, for embedding in a generated
// systemd/launchd unit. Falls back to argv0 if it cannot be resolved.
std::string current_exe_path(const char* argv0) {
#if defined(_WIN32)
    char buf[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (n > 0 && n < MAX_PATH) return std::string(buf, n);
#else
    char buf[4096];
    if (char* r = realpath(argv0, buf)) return std::string(r);
#endif
    return argv0 ? std::string(argv0) : std::string("openads_serverd");
}

// Probe the local ACE DLL landscape and print a one-line report.
// Checks (in order): openace64.dll, ace64.dll, openace32.dll, ace32.dll
// in the executable's directory and the current working directory.
// Warns if a SAP ace64.dll is found (potential conflict / wrong binary).
static void probe_ace_dlls(bool console) {
    if (!console) return;
    static const char* kCandidates[] = {
        "openace64.dll", "ace64.dll",
        "openace32.dll", "ace32.dll",
        nullptr
    };
    bool any = false;
    for (int i = 0; kCandidates[i]; ++i) {
        std::string desc =
            openads::platform::dll_probe_ace(kCandidates[i]);
        if (!desc.empty()) {
            std::printf("  [dll] %s — OpenADS ACE engine (%s)\n",
                        kCandidates[i], desc.c_str());
            any = true;
        } else {
            // DLL exists but is not OpenADS (SAP or missing AdsGetVersion).
            // Distinguish "not found" from "found but SAP" by attempting a
            // raw load.
            auto h = openads::platform::dll_load(kCandidates[i]);
            if (h) {
                openads::platform::dll_close(h.value());
                std::fprintf(stderr,
                    "  [dll] WARNING: %s found but is NOT an OpenADS "
                    "build (SAP Advantage DLL?). AEP procedures that "
                    "reference this name will be rejected.\n",
                    kCandidates[i]);
                any = true;
            }
        }
    }
    if (!any) {
        std::printf("  [dll] no ACE DLL found in working directory "
                    "(ace64.dll / openace64.dll). AEP external "
                    "procedures will require an explicit full path.\n");
    }
    std::fflush(stdout);
}

#ifndef OPENADS_VERSION_STR
#  define OPENADS_VERSION_STR "unknown"
#endif

// Run the actual server. Returns when g_running flips to false
// (signal handler on POSIX / SCM stop control on Windows).
int run_server(const Args& args, bool console) {
    // Error log configuration must land before Server::start(), whose
    // "server started" entry is the log's first row.
    if (!args.error_log_path.empty())
        openads::mgmt::ErrorLog::instance().set_directory(
            args.error_log_path);
    if (args.error_log_max_kb != 0)
        openads::mgmt::ErrorLog::instance().set_max_kbytes(
            args.error_log_max_kb);

    openads::network::Server srv;
    if (!args.data_dir.empty() && args.data_dir != ".")
        srv.set_data_dir(args.data_dir);
    srv.set_enable_file_func(args.enable_file_func);
    srv.set_legacy_paths(args.legacy_paths);
    // listen() backlog override (0 = env default OPENADS_SERVER_BACKLOG/256).
    srv.set_backlog(args.backlog);
    // Session cap: an explicit --max-sessions / ini key wins (0 = unlimited);
    // otherwise the env default (OPENADS_SERVER_MAX_SESSIONS, 500) applies.
    srv.set_max_sessions(args.max_sessions);
    for (const auto& u : args.auth_users)
        srv.add_credential(u.first, u.second);

    // The HTTP Studio console browses one directory; when --data lists
    // several roots (";"-separated, for the wire-protocol jail — see
    // Server::set_data_dir), the console gets the first one.
    auto data_roots = openads::platform::split_data_roots(args.data_dir);
    const std::string http_data_dir = data_roots.empty() ? args.data_dir : data_roots.front();
    auto r = srv.start(args.host, args.port);
    if (!r) {
        std::fprintf(stderr, "server start failed: %s (sub=%d)\n",
                     r.error().message.c_str(), r.error().sub_code);
        // The default OpenADS / SAP-ACE wire port is 6262. Port
        // collisions with the SAP Advantage Database Server service
        // (when both run on the same host) surface as a generic
        // bind failure; flag that case explicitly so the operator
        // knows to either stop the ADS service or pick a free port.
        if (args.port == 6262) {
            std::fprintf(stderr,
                "hint: port 6262 is the SAP Advantage Database Server\n"
                "      default. If ADS is running on this host you'll\n"
                "      hit a bind clash. Either stop the Advantage\n"
                "      Database Server service first, or pick a free\n"
                "      port via `--port <N>` (eg. --port 6263).\n");
        } else {
            std::fprintf(stderr,
                "hint: another process is already bound to port %u.\n"
                "      Either stop it or pick a free port via\n"
                "      `--port <N>`.\n", args.port);
        }
        return 1;
    }

    // Multi-port: bind extra listeners.
    for (const auto& pe : args.extra_listeners) {
        auto er = srv.add_listener(args.host, pe.port, pe.data_dir);
        if (!er) {
            std::fprintf(stderr, "extra listener on port %u failed: %s\n",
                         pe.port, er.error().message.c_str());
            return 1;
        }
    }

    if (console) {
        std::printf("openads_serverd %s listening on %s:%u (backlog=%d)\n",
                    OPENADS_VERSION_STR, args.host.c_str(), srv.port(),
                    args.backlog);
        for (const auto& pe : args.extra_listeners) {
            std::printf("  extra listener on port %u (data=%s)\n",
                        pe.port, pe.data_dir.c_str());
        }
        probe_ace_dlls(console);
    }

#if defined(OPENADS_WITH_HTTP)
    openads::studio::HttpConsole http;
    for (auto& u : args.http_users) http.add_user(u.first, u.second);
    if (args.http_port != 0) {
        if (!http.start(args.host, args.http_port, http_data_dir, &srv)) {
            std::fprintf(stderr,
                "Studio HTTP console: bind to %s:%u failed\n",
                args.host.c_str(), args.http_port);
        } else if (console) {
            std::printf("Studio web console on http://%s:%u/  (data=%s)\n",
                        args.host.c_str(), args.http_port, http_data_dir.c_str());
            std::fflush(stdout);
        }
    }
#else
    if (args.http_port != 0) {
        std::fprintf(stderr,
            "--http-port set but build lacks OPENADS_WITH_HTTP=ON\n");
    }
#endif

    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    if (console) {
        std::printf("openads_serverd: shutdown signal received\n");
    }
#if defined(OPENADS_WITH_HTTP)
    if (args.http_port != 0) http.stop();
#endif
    srv.stop();
    return 0;
}

#if defined(_WIN32)

// ---- Windows Service plumbing ---------------------------------------
//
// `--install-service` / `--uninstall-service` register / drop the
// service with the Service Control Manager. `--service` is the
// entry SCM hands us when launching the registered binary; we then
// call StartServiceCtrlDispatcher and let SCM call svc_main().

constexpr const char* kServiceName    = "openads_serverd";
constexpr const char* kServiceDisplay = "OpenADS Database Server";

static SERVICE_STATUS        g_svc_status{};
static SERVICE_STATUS_HANDLE g_svc_handle = nullptr;
static int                   g_svc_argc   = 0;
static char**                g_svc_argv   = nullptr;

VOID WINAPI svc_ctrl_handler(DWORD ctrl) {
    switch (ctrl) {
        case SERVICE_CONTROL_STOP:
        case SERVICE_CONTROL_SHUTDOWN:
            g_svc_status.dwCurrentState     = SERVICE_STOP_PENDING;
            g_svc_status.dwWaitHint         = 5000;
            SetServiceStatus(g_svc_handle, &g_svc_status);
            g_running.store(false);
            break;
        default:
            break;
    }
}

VOID WINAPI svc_main(DWORD /*argc*/, LPSTR* /*argv*/) {
    g_svc_handle = RegisterServiceCtrlHandlerA(
        kServiceName, svc_ctrl_handler);
    if (!g_svc_handle) return;

    g_svc_status.dwServiceType      = SERVICE_WIN32_OWN_PROCESS;
    g_svc_status.dwCurrentState     = SERVICE_START_PENDING;
    g_svc_status.dwControlsAccepted = 0;
    g_svc_status.dwWin32ExitCode    = NO_ERROR;
    g_svc_status.dwServiceSpecificExitCode = 0;
    g_svc_status.dwCheckPoint       = 0;
    g_svc_status.dwWaitHint         = 5000;
    SetServiceStatus(g_svc_handle, &g_svc_status);

    Args args;
    if (!load_config_and_args(g_svc_argc, g_svc_argv, args)) {
        g_svc_status.dwCurrentState  = SERVICE_STOPPED;
        g_svc_status.dwWin32ExitCode = ERROR_INVALID_PARAMETER;
        SetServiceStatus(g_svc_handle, &g_svc_status);
        return;
    }

    g_svc_status.dwCurrentState     = SERVICE_RUNNING;
    g_svc_status.dwControlsAccepted =
        SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    SetServiceStatus(g_svc_handle, &g_svc_status);

    int rc = run_server(args, /*console=*/false);

    g_svc_status.dwCurrentState  = SERVICE_STOPPED;
    g_svc_status.dwWin32ExitCode = (rc == 0) ? NO_ERROR
                                              : ERROR_SERVICE_SPECIFIC_ERROR;
    g_svc_status.dwServiceSpecificExitCode =
        (rc == 0) ? 0 : static_cast<DWORD>(rc);
    SetServiceStatus(g_svc_handle, &g_svc_status);
}

int install_service(int argc, char** argv) {
    char path[MAX_PATH];
    if (!GetModuleFileNameA(nullptr, path, MAX_PATH)) {
        std::fprintf(stderr, "GetModuleFileNameA failed (err=%lu)\n",
                     GetLastError());
        return 1;
    }
    // Compose binPath with --service plus any extras the user wants
    // baked into the registration ("--install-service --port 6263
    // --data c:\app\data" → registered binPath includes those).
    std::string cmd = "\"";
    cmd += path;
    cmd += "\" --service";
    for (int i = 2; i < argc; ++i) {
        cmd += " ";
        cmd += "\"";
        cmd += argv[i];
        cmd += "\"";
    }
    SC_HANDLE scm = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
    if (!scm) {
        std::fprintf(stderr,
            "OpenSCManager failed (err=%lu) — run from an elevated "
            "Administrator prompt.\n", GetLastError());
        return 1;
    }
    SC_HANDLE svc = CreateServiceA(
        scm, kServiceName, kServiceDisplay,
        SERVICE_ALL_ACCESS,
        SERVICE_WIN32_OWN_PROCESS,
        SERVICE_AUTO_START,
        SERVICE_ERROR_NORMAL,
        cmd.c_str(),
        nullptr, nullptr, nullptr, nullptr, nullptr);
    if (!svc) {
        DWORD e = GetLastError();
        if (e == ERROR_SERVICE_EXISTS) {
            std::fprintf(stderr, "service '%s' is already installed\n",
                         kServiceName);
        } else {
            std::fprintf(stderr,
                "CreateService failed (err=%lu) — run elevated.\n", e);
        }
        CloseServiceHandle(scm);
        return 1;
    }
    std::printf("service '%s' installed.\n  binPath: %s\n"
                "  display: %s\n  startup: SERVICE_AUTO_START\n"
                "Start it via: sc start %s\n",
                kServiceName, cmd.c_str(), kServiceDisplay, kServiceName);
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return 0;
}

int uninstall_service() {
    SC_HANDLE scm = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
    if (!scm) {
        std::fprintf(stderr,
            "OpenSCManager failed (err=%lu) — run elevated.\n",
            GetLastError());
        return 1;
    }
    SC_HANDLE svc = OpenServiceA(
        scm, kServiceName,
        DELETE | SERVICE_STOP | SERVICE_QUERY_STATUS);
    if (!svc) {
        std::fprintf(stderr,
            "OpenService failed (err=%lu) — service not installed?\n",
            GetLastError());
        CloseServiceHandle(scm);
        return 1;
    }
    SERVICE_STATUS status{};
    // Best-effort stop; ignore errors (already stopped is fine).
    ControlService(svc, SERVICE_CONTROL_STOP, &status);
    if (!DeleteService(svc)) {
        std::fprintf(stderr,
            "DeleteService failed (err=%lu)\n", GetLastError());
        CloseServiceHandle(svc);
        CloseServiceHandle(scm);
        return 1;
    }
    std::printf("service '%s' uninstalled.\n", kServiceName);
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return 0;
}
#endif // _WIN32

} // namespace

int main(int argc, char** argv) {
    // The remote_only_access guard (v1.8.94+) targets CLIENT apps loading
    // the ACE DLL. This daemon's in-process engine (the session ABI twin)
    // legitimately opens tables locally for every wire request, so an
    // openads.ini next to the server exe must never arm the guard here --
    // deny mode would make the twin's opens fail with AE_ACCESS_DENIED.
    // Env wins over ini, so force-disable unless the operator set the env
    // var explicitly before launching.
    if (std::getenv("OPENADS_REMOTE_ONLY_ACCESS") == nullptr) {
#if defined(_WIN32)
        _putenv_s("OPENADS_REMOTE_ONLY_ACCESS", "0");
#else
        setenv("OPENADS_REMOTE_ONLY_ACCESS", "0", 0);
#endif
    }
    // Top-level switches that short-circuit the normal serve loop.
    if (argc > 1) {
        std::string a1 = argv[1];
        if (a1 == "--version") {
            std::printf("openads_serverd %s\n", OPENADS_VERSION_STR);
            return 0;
        }
        if (a1 == "-h" || a1 == "--help") { usage(argv[0]); return 0; }
        if (a1 == "--setup") {
            openads::serverd::SetupResult sr;
            if (!openads::serverd::run_setup_wizard(
                    current_exe_path(argv[0]), sr)) {
                return 1;
            }
#if defined(_WIN32)
            if (sr.want_service) {
                // Reuse install_service(): it bakes everything after argv[1]
                // into the registered binPath, so pass --config <ini>.
                const char* svc_argv[] = {argv[0], "--install-service",
                                          "--config", sr.ini_path.c_str()};
                return install_service(4, const_cast<char**>(svc_argv));
            }
#endif
            return 0;
        }
#if defined(_WIN32)
        if (a1 == "--install-service" || a1 == "--install_service") {
            return install_service(argc, argv);
        }
        if (a1 == "--uninstall-service" || a1 == "--uninstall_service") {
            return uninstall_service();
        }
        if (a1 == "--service") {
            // Hand control over to the SCM dispatcher; it calls
            // svc_main() once the service is started by the SCM.
            g_svc_argc = argc;
            g_svc_argv = argv;
            SERVICE_TABLE_ENTRYA tbl[] = {
                {const_cast<LPSTR>(kServiceName), &svc_main},
                {nullptr, nullptr}
            };
            if (!StartServiceCtrlDispatcherA(tbl)) {
                DWORD e = GetLastError();
                if (e == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT) {
                    std::fprintf(stderr,
                        "openads_serverd was launched with --service "
                        "but the process is not running under the\n"
                        "Service Control Manager. This flag is meant "
                        "to be set by the SCM itself; for an\n"
                        "interactive run leave --service off.\n");
                } else {
                    std::fprintf(stderr,
                        "StartServiceCtrlDispatcher failed (err=%lu)\n", e);
                }
                return 1;
            }
            return 0;
        }
#endif
    }

    Args args;
    if (!load_config_and_args(argc, argv, args)) {
        usage(argv[0]);
        return 2;
    }

    std::signal(SIGINT,  on_signal);
    std::signal(SIGTERM, on_signal);

    return run_server(args, /*console=*/true);
}
