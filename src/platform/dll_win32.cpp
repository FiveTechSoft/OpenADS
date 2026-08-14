#include "platform/dll.h"

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace openads::platform {

util::Result<DllHandle> dll_load(const std::string& path) {
    HMODULE m = LoadLibraryA(path.c_str());
    if (!m) {
        return util::Error{5000, static_cast<int>(GetLastError()),
                           "LoadLibrary failed", path};
    }
    DllHandle h;
    h.native = reinterpret_cast<void*>(m);
    return h;
}

util::Result<void*> dll_symbol(DllHandle h, const std::string& name) {
    if (!dll_valid(h)) {
        return util::Error{5000, 0, "invalid DLL handle", name};
    }
    FARPROC p = GetProcAddress(reinterpret_cast<HMODULE>(h.native),
                               name.c_str());
    if (!p) {
        return util::Error{5000, static_cast<int>(GetLastError()),
                           "GetProcAddress failed", name};
    }
    return reinterpret_cast<void*>(p);
}

void dll_close(DllHandle h) noexcept {
    if (h.native) {
        FreeLibrary(reinterpret_cast<HMODULE>(h.native));
    }
}

std::string dll_probe_ace(const std::string& path) noexcept {
    HMODULE m = LoadLibraryA(path.c_str());
    if (!m) return {};
    // On x86 the OpenADS DLL exports each entry point twice: the plain
    // undecorated name and the __stdcall-decorated (_AdsXxx@N) form, and
    // BOTH alias the same __stdcall wrapper (see abi/ace_stdcall_x86.c's
    // /export:AdsXxx=_AdsXxx@N lines). So GetProcAddress("AdsGetVersion")
    // succeeds on x86 and the target is __stdcall -- calling it through a
    // __cdecl pointer corrupts ESP by 20 bytes on return (serverd crashed
    // at startup whenever an ace32/openace32 DLL sat next to the exe --
    // Pritpal's "Server does not launch"). Declare the pointer __stdcall:
    // harmless on x64 (single convention) and correct on x86. SAP DLLs
    // only export the @N-decorated names, so the probe still skips them.
    using pfnGetVer = unsigned int(__stdcall*)(
        unsigned int*, unsigned int*,
        unsigned char*, unsigned char*, unsigned short*);
    auto* fn = reinterpret_cast<pfnGetVer>(
        GetProcAddress(m, "AdsGetVersion"));
    std::string result;
    if (fn) {
        unsigned char desc[256] = {};
        unsigned short len = static_cast<unsigned short>(sizeof(desc) - 1);
        fn(nullptr, nullptr, nullptr, desc, &len);
        result.assign(reinterpret_cast<char*>(desc),
                      static_cast<std::size_t>(len));
    }
    FreeLibrary(m);
    if (result.find("OpenADS") == std::string::npos) return {};
    return result;
}

} // namespace openads::platform

#endif // _WIN32
