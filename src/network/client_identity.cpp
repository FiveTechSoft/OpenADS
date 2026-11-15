#include "network/client_identity.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <pwd.h>
#include <unistd.h>
#include <fstream>
#endif

namespace openads::network {

#if defined(_WIN32)
namespace {
std::string to_utf8(const wchar_t* w, int len) {
    if (len <= 0) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, len, nullptr, 0,
                                      nullptr, nullptr);
    if (n <= 0) return {};
    std::string out(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, len, out.data(), n, nullptr, nullptr);
    return out;
}
}  // namespace

ClientIdentity local_client_identity() {
    ClientIdentity id;
    wchar_t buf[MAX_PATH + 1];
    DWORD n = MAX_PATH;
    if (GetComputerNameW(buf, &n)) id.computer = to_utf8(buf, static_cast<int>(n));
    n = MAX_PATH;
    if (GetUserNameW(buf, &n) && n > 0)  // n counts the terminating NUL
        id.os_user = to_utf8(buf, static_cast<int>(n - 1));
    n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        const wchar_t* base = buf;
        for (const wchar_t* p = buf; *p; ++p)
            if (*p == L'\\' || *p == L'/') base = p + 1;
        id.program = to_utf8(base, static_cast<int>(buf + n - base));
    }
    return id;
}
#else
ClientIdentity local_client_identity() {
    ClientIdentity id;
    char host[256] = {0};
    if (gethostname(host, sizeof host - 1) == 0) id.computer = host;
    if (const passwd* pw = getpwuid(geteuid())) id.os_user = pw->pw_name;
    std::ifstream comm("/proc/self/comm");  // Linux; empty elsewhere
    std::getline(comm, id.program);
    return id;
}
#endif

}  // namespace openads::network
