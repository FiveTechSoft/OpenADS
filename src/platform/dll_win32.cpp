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
    // Identify an OpenADS build WITHOUT calling anything: the oads_* VFS
    // helpers are exported only by OpenADS (SAP's DLL has no such names).
    // Pretty-printing the version requires calling AdsGetVersion, whose
    // calling convention on x86 differs per vendor: in the OpenADS DLL
    // the undecorated export is a __cdecl implementation (v1.8.74
    // behavior, restored after the stdcall-alias regression), while SAP's
    // undecorated exports are __stdcall. Probing with the wrong
    // convention corrupts ESP and crashes serverd at startup (Pritpal's
    // "Server does not launch") -- so only make the call once the DLL is
    // known to be OpenADS, and use __cdecl.
    const bool is_openads =
        GetProcAddress(m, "oads_CheckExistence") != nullptr;
    std::string result;
    if (is_openads) {
        using pfnGetVer = unsigned int(__cdecl*)(
            unsigned int*, unsigned int*,
            unsigned char*, unsigned char*, unsigned short*);
        auto* fn = reinterpret_cast<pfnGetVer>(
            GetProcAddress(m, "AdsGetVersion"));
        if (fn) {
            unsigned char desc[256] = {};
            unsigned short len = static_cast<unsigned short>(sizeof(desc) - 1);
            fn(nullptr, nullptr, nullptr, desc, &len);
            result.assign(reinterpret_cast<char*>(desc),
                          static_cast<std::size_t>(len));
        }
        if (result.find("OpenADS") == std::string::npos)
            result = "OpenADS";
    }
    FreeLibrary(m);
    return result;
}

} // namespace openads::platform

#endif // _WIN32
