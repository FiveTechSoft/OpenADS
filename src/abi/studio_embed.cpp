// OpenADS-only extension: in-process Studio web console.
//
// This translation unit is compiled into the openads_ace SHARED
// target (ace64.dll / ace32.dll) when -DOPENADS_WITH_HTTP=ON. It
// glues three new public exports — AdsStudioStart, AdsStudioStop
// and AdsStudioPort — onto the existing studio HTTP console
// implementation in tools/serverd/http_server.{cpp,h}, so a
// LocalServer application (the one loading the DLL directly,
// without spawning openads_serverd.exe) gets the same web UI in
// its own process.
//
// Auto-start. If OPENADS_STUDIO_PORT is set in the environment at
// DLL load time, we boot the console immediately on that port.
// OPENADS_STUDIO_DATA picks the data dir (default ".") and
// OPENADS_STUDIO_HOST picks the bind address (default
// "127.0.0.1"). On Windows we hook DllMain DLL_PROCESS_ATTACH;
// on POSIX we use a constructor attribute. Without the env var,
// nothing is bound — the host has to call AdsStudioStart()
// explicitly. This split keeps the DLL silent by default
// (no surprise localhost listener) while making "set one env var
// and you get a console" trivial for end users / X# devs.
//
// The console is shared between openads_serverd and ace64.dll;
// the implementation lives once in tools/serverd/http_server.cpp
// and is compiled into both targets via separate translation
// units.

#include "openads/ace.h"
#include "openads/error.h"

#if defined(OPENADS_WITH_HTTP)
#include "tools/serverd/http_server.h"
#endif

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

namespace {

#if defined(OPENADS_WITH_HTTP)

std::mutex g_mu;
std::unique_ptr<openads::studio::HttpConsole> g_console;
std::uint16_t g_port = 0;

// Resolve env var without leaking platform branches into the
// auto-start path. Returns empty string when unset / blank.
std::string env_or(const char* name, const char* fallback) {
#if defined(_WIN32)
    char  buf[2048];
    DWORD n = ::GetEnvironmentVariableA(name, buf, sizeof(buf));
    if (n == 0 || n >= sizeof(buf)) return fallback ? fallback : "";
    return std::string(buf, buf + n);
#else
    const char* v = std::getenv(name);
    if (v == nullptr || *v == '\0') return fallback ? fallback : "";
    return v;
#endif
}

void try_auto_start() {
    std::string port_s = env_or("OPENADS_STUDIO_PORT", "");
    if (port_s.empty()) return;                  // opt-in only

    int port_i = std::atoi(port_s.c_str());
    if (port_i <= 0 || port_i > 65535) return;

    // Auth is now mandatory for the in-process console, same rule as
    // AdsStudioStart() below (see there for the full rationale).
    // Auto-start stays silent-by-default in spirit — it just refuses
    // to bind at all without credentials, instead of ever exposing an
    // unauthenticated listener.
    std::string user = env_or("OPENADS_STUDIO_USER", "");
    std::string pass = env_or("OPENADS_STUDIO_PASSWORD", "");
    if (user.empty() || pass.empty()) return;

    std::string host = env_or("OPENADS_STUDIO_HOST", "127.0.0.1");
    std::string data = env_or("OPENADS_STUDIO_DATA", ".");

    std::lock_guard<std::mutex> lk(g_mu);
    if (g_console) return;                       // already running
    auto c = std::make_unique<openads::studio::HttpConsole>();
    if (!c->start(host, static_cast<std::uint16_t>(port_i),
                  data, /*wire_srv=*/nullptr)) {
        // Auto-start is best-effort: bind failures are silent so a
        // host app loading the DLL on a box where 8080 is taken
        // doesn't crash on load. The host can call AdsStudioStart()
        // explicitly later and inspect the return code.
        return;
    }
    c->add_user(user, pass);
    g_console = std::move(c);
    g_port    = static_cast<std::uint16_t>(port_i);
}

#endif // OPENADS_WITH_HTTP

} // namespace

extern "C" {

UNSIGNED32 ENTRYPOINT AdsStudioStart(UNSIGNED16 usPort,
                                     UNSIGNED8* pucDataDir) {
#if defined(OPENADS_WITH_HTTP)
    if (pucDataDir == nullptr) return openads::AE_INTERNAL_ERROR;
    std::string data(reinterpret_cast<const char*>(pucDataDir));
    std::string host = ::env_or("OPENADS_STUDIO_HOST", "127.0.0.1");

    // Auth is now MANDATORY for the in-process console, not opt-in.
    // The previous behaviour (bind first, only add_user() if the env
    // vars happened to be set) meant any other process on the box
    // that loads this DLL and calls AdsStudioStart() *without* setting
    // OPENADS_STUDIO_USER/PASSWORD got a fully open console — a host's
    // own gates (a license check, say) don't protect an export any
    // caller can invoke directly (ctypes/P-Invoke/a 10-line .c).
    // Refusing to bind at all without credentials closes that at the
    // one choke point every caller has to go through, instead of
    // trusting each host to remember to opt in. No in-repo caller
    // currently relies on the old no-auth default.
    std::string user = ::env_or("OPENADS_STUDIO_USER", "");
    std::string pass = ::env_or("OPENADS_STUDIO_PASSWORD", "");
    if (user.empty() || pass.empty()) {
        return openads::AE_LOGIN_FAILED;
    }

    std::lock_guard<std::mutex> lk(g_mu);
    if (g_console) return openads::AE_SUCCESS;   // idempotent
    auto c = std::make_unique<openads::studio::HttpConsole>();
    if (!c->start(host, usPort, data, /*wire_srv=*/nullptr)) {
        return openads::AE_INTERNAL_ERROR;
    }
    c->add_user(user, pass);
    g_console = std::move(c);
    g_port    = usPort;
    return openads::AE_SUCCESS;
#else
    (void)usPort; (void)pucDataDir;
    return openads::AE_FUNCTION_NOT_AVAILABLE;
#endif
}

UNSIGNED32 ENTRYPOINT AdsStudioStop(void) {
#if defined(OPENADS_WITH_HTTP)
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_console) {
        g_console->stop();
        g_console.reset();
        g_port = 0;
    }
    return openads::AE_SUCCESS;
#else
    return openads::AE_FUNCTION_NOT_AVAILABLE;
#endif
}

UNSIGNED32 ENTRYPOINT AdsStudioPort(UNSIGNED16* pusPort) {
#if defined(OPENADS_WITH_HTTP)
    if (pusPort == nullptr) return openads::AE_INTERNAL_ERROR;
    std::lock_guard<std::mutex> lk(g_mu);
    *pusPort = g_console ? g_port : 0;
    return openads::AE_SUCCESS;
#else
    if (pusPort != nullptr) *pusPort = 0;
    return openads::AE_FUNCTION_NOT_AVAILABLE;
#endif
}

} // extern "C"

// --------------------------------------------------------------------
// Auto-start hook. On Windows we drive it from DllMain so the
// console is up before the host's first AdsConnect60(). On POSIX
// the constructor attribute on a free function inside a SHARED
// library has the same effect. In both cases the env var
// OPENADS_STUDIO_PORT is the on/off switch — without it the
// hook is a no-op.
// --------------------------------------------------------------------

#if defined(OPENADS_WITH_HTTP)

#if defined(_WIN32)
extern "C" BOOL WINAPI DllMain(HINSTANCE /*h*/, DWORD reason,
                               LPVOID /*reserved*/) {
    if (reason == DLL_PROCESS_ATTACH) {
        ::try_auto_start();
    } else if (reason == DLL_PROCESS_DETACH) {
        // Best effort: stop the listener so the OS releases the
        // port immediately when the host process exits.
        std::lock_guard<std::mutex> lk(g_mu);
        if (g_console) {
            g_console->stop();
            g_console.reset();
        }
    }
    return TRUE;
}
#else
__attribute__((constructor))
static void openads_studio_autostart_ctor() { ::try_auto_start(); }

__attribute__((destructor))
static void openads_studio_autostart_dtor() {
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_console) { g_console->stop(); g_console.reset(); }
}
#endif

#endif // OPENADS_WITH_HTTP
