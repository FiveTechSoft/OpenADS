#ifdef _WIN32

#include "platform/file.h"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cwctype>
#include <filesystem>
#include <memory>
#include <mutex>
#include <vector>

namespace openads::platform {

namespace {

util::Error os_error(const char* op) {
    DWORD code = ::GetLastError();
    util::Error e;
    e.code = (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND)
                 ? 5103   // AE_TABLE_NOT_FOUND-style placeholder
                 : (code == ERROR_SHARING_VIOLATION ||
                    code == ERROR_LOCK_VIOLATION)
                       ? 7040  // AE_FILE_IN_USE (create/open vs exclusive hold)
                       : 5000;  // AE_INTERNAL_ERROR placeholder
    e.sub_code = static_cast<std::int32_t>(code);
    char buf[256] = {};
    ::FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                     nullptr, code, 0, buf, sizeof(buf) - 1, nullptr);
    // Strip trailing CR/LF that FormatMessage appends
    for (int i = static_cast<int>(strlen(buf)) - 1; i >= 0 && (buf[i] == '\r' || buf[i] == '\n'); --i)
        buf[i] = '\0';
    e.message = std::string(op) + ": " + (buf[0] ? buf : "error " + std::to_string(code));
    return e;
}


struct LockedDirectories {
    std::vector<HANDLE> handles;
    ~LockedDirectories() { for (HANDLE h : handles) ::CloseHandle(h); }
};
struct JailRootW {
    std::wstring canon;
    std::wstring lower;
    std::shared_ptr<LockedDirectories> anchor;
};
std::mutex g_jail_mu;
std::vector<JailRootW> g_jail_roots;

std::wstring to_wide(const std::string& in) {
    if (in.empty()) return {};
    const int n = ::MultiByteToWideChar(CP_ACP, 0, in.c_str(), -1, nullptr, 0);
    if (n <= 1) return {};
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    ::MultiByteToWideChar(CP_ACP, 0, in.c_str(), -1, w.data(), n);
    w.resize(static_cast<std::size_t>(n) - 1);
    return w;
}

std::wstring jail_lower(std::wstring w) {
    for (auto& ch : w) {
        if (ch == L'/') ch = L'\\';
        ch = static_cast<wchar_t>(::towlower(ch));
    }
    return w;
}

util::Error jail_refused(const char* what) {
    ::SetLastError(ERROR_ACCESS_DENIED);
    return os_error(what);
}

util::Result<void> lock_directory(const std::wstring& path,
                                  LockedDirectories& locked) {
    HANDLE h = ::CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (h == INVALID_HANDLE_VALUE) return os_error("open(jail: directory)");
    BY_HANDLE_FILE_INFORMATION info{};
    if (!::GetFileInformationByHandle(h, &info) ||
        !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        ::CloseHandle(h);
        return jail_refused("open(jail: directory/reparse refused)");
    }
    locked.handles.push_back(h);
    return {};
}

util::Result<HANDLE> open_jailed(const std::string& path, DWORD access,
                              DWORD share, DWORD disp) {
    namespace fs = std::filesystem;
    std::vector<JailRootW> roots;
    { std::lock_guard<std::mutex> lk(g_jail_mu); roots = g_jail_roots; }
    std::error_code ec;
    fs::path input(to_wide(path));
    fs::path full = input.is_absolute() ? input : fs::absolute(input, ec);
    if (ec || full.empty()) return jail_refused("open(jail: invalid path)");
    const std::wstring spelled = full.wstring();
    const std::wstring lower = jail_lower(spelled);
    const JailRootW* match = nullptr;
    for (const auto& root : roots) {
        if (lower.size() > root.lower.size() &&
            lower.compare(0, root.lower.size(), root.lower) == 0 &&
            lower[root.lower.size()] == L'\\' &&
            (!match || root.lower.size() > match->lower.size())) match = &root;
    }
    if (!match) return jail_refused("open(jail: outside data roots)");
    fs::path rel(spelled.substr(match->canon.size() + 1));
    std::vector<fs::path> comps;
    for (const auto& part : rel) if (!part.empty() && part != L".") comps.push_back(part);
    if (comps.empty()) return jail_refused("open(jail: no file)");
    LockedDirectories locked;
    fs::path current(match->canon + L"\\");
    std::vector<fs::path> parents;
    for (std::size_t i = 0; i + 1 < comps.size(); ++i) {
        if (comps[i] == L"..") {
            if (parents.empty()) return jail_refused("open(jail: '..' escape refused)");
            current = parents.back(); parents.pop_back();
        } else {
            const std::wstring part = comps[i].wstring();
            if (part.find(L':') != std::wstring::npos || part.back() == L'.' || part.back() == L' ')
                return jail_refused("open(jail: invalid directory name)");
            fs::path next = current / comps[i];
            auto r = lock_directory(next.wstring(), locked);
            if (!r) return r.error();
            parents.push_back(current); current = next;
        }
    }
    const fs::path leaf = comps.back();
    const std::wstring name = leaf.wstring();
    if (leaf == L".." || name.find(L':') != std::wstring::npos ||
        name.empty() || name.back() == L'.' || name.back() == L' ')
        return jail_refused("open(jail: invalid leaf)");
    const bool truncating = disp == CREATE_ALWAYS;
    HANDLE h = ::CreateFileW((current / leaf).c_str(), access, share, nullptr,
        truncating ? OPEN_ALWAYS : disp,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (h == INVALID_HANDLE_VALUE) return os_error("CreateFileW(jail)");
    BY_HANDLE_FILE_INFORMATION info{};
    if (!::GetFileInformationByHandle(h, &info) ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY))) {
        ::CloseHandle(h);
        return jail_refused("open(jail: reparse/file refused)");
    }
    if (truncating) {
        LARGE_INTEGER zero{};
        if (!::SetFilePointerEx(h, zero, nullptr, FILE_BEGIN) || !::SetEndOfFile(h)) {
            auto e = os_error("open(jail: truncate)"); ::CloseHandle(h); return e;
        }
    }
    return h;
}

} // namespace

File::File(File&& other) noexcept : native_(other.native_) {
    other.native_ = nullptr;
}

File& File::operator=(File&& other) noexcept {
    if (this != &other) {
        close_();
        native_ = other.native_;
        other.native_ = nullptr;
    }
    return *this;
}

File::~File() { close_(); }

void File::close_() noexcept {
    if (native_ != nullptr) {
        ::CloseHandle(reinterpret_cast<HANDLE>(native_));
        native_ = nullptr;
    }
}

util::Result<File> File::open(const std::string& path, OpenMode mode) {
    DWORD access = 0;
    DWORD share  = FILE_SHARE_READ | FILE_SHARE_WRITE;
    DWORD disp   = OPEN_EXISTING;
    switch (mode) {
        case OpenMode::ReadOnly:
            access = GENERIC_READ;
            break;
        case OpenMode::ReadWrite:
            access = GENERIC_READ | GENERIC_WRITE;
            break;
        case OpenMode::CreateRW:
            access = GENERIC_READ | GENERIC_WRITE;
            disp   = CREATE_ALWAYS;
            break;
        case OpenMode::OpenExisting:
            access = GENERIC_READ | GENERIC_WRITE;
            disp   = OPEN_EXISTING;
            break;
        case OpenMode::CreateExclusive:
            access = GENERIC_READ | GENERIC_WRITE;
            share  = 0;
            disp   = CREATE_ALWAYS;
            break;
    }
    if (data_jail_active()) {
        auto opened = open_jailed(path, access, share, disp);
        if (!opened) return opened.error();
        return File{opened.value()};
    }
    HANDLE h = ::CreateFileA(path.c_str(), access, share, nullptr, disp,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return os_error("CreateFileA");
    return File{h};
}

util::Result<std::size_t> File::read_at(std::uint64_t offset,
                                        void* buf, std::size_t n) {
    OVERLAPPED ov{};
    ov.Offset     = static_cast<DWORD>(offset & 0xFFFFFFFFu);
    ov.OffsetHigh = static_cast<DWORD>(offset >> 32);
    DWORD got = 0;
    if (!::ReadFile(reinterpret_cast<HANDLE>(native_), buf,
                    static_cast<DWORD>(n), &got, &ov)) {
        DWORD code = ::GetLastError();
        if (code != ERROR_HANDLE_EOF) return os_error("ReadFile");
    }
    return static_cast<std::size_t>(got);
}

util::Result<std::size_t> File::write_at(std::uint64_t offset,
                                         const void* buf, std::size_t n) {
    OVERLAPPED ov{};
    ov.Offset     = static_cast<DWORD>(offset & 0xFFFFFFFFu);
    ov.OffsetHigh = static_cast<DWORD>(offset >> 32);
    DWORD wrote = 0;
    if (!::WriteFile(reinterpret_cast<HANDLE>(native_), buf,
                     static_cast<DWORD>(n), &wrote, &ov)) {
        return os_error("WriteFile");
    }
    return static_cast<std::size_t>(wrote);
}

util::Result<std::uint64_t> File::size() const {
    LARGE_INTEGER li{};
    if (!::GetFileSizeEx(reinterpret_cast<HANDLE>(native_), &li)) {
        return os_error("GetFileSizeEx");
    }
    return static_cast<std::uint64_t>(li.QuadPart);
}

util::Result<void> File::sync() {
    if (!::FlushFileBuffers(reinterpret_cast<HANDLE>(native_))) {
        return os_error("FlushFileBuffers");
    }
    return {};
}

util::Result<void> File::truncate(std::uint64_t size) {
    LARGE_INTEGER li{};
    li.QuadPart = static_cast<LONGLONG>(size);
    if (!::SetFilePointerEx(reinterpret_cast<HANDLE>(native_), li, nullptr,
                            FILE_BEGIN))
        return os_error("SetFilePointerEx");
    if (!::SetEndOfFile(reinterpret_cast<HANDLE>(native_)))
        return os_error("SetEndOfFile");
    return {};
}

util::Result<void> File::try_lock_shared() {
    // No-op on Win32: the creating handle's share=0 already excludes
    // every other open for the duration of a create.
    return {};
}

void File::release_lock_shared() {
    // No-op on Win32: try_lock_shared is also a no-op.
}

util::Result<void> File::set_data_jail(std::vector<std::string> roots) {
    std::vector<JailRootW> anchored;
    for (const auto& r : roots) {
        if (r.empty()) continue;
        const std::wstring w = to_wide(r);
        for (const auto& part : std::filesystem::path(w)) {
            if (part == L"..") return jail_refused("set_data_jail: parent root refused");
        }
        std::wstring full;
        {
            const DWORD need =
                ::GetFullPathNameW(w.c_str(), 0, nullptr, nullptr);
            if (need == 0) {
                util::Error e = os_error("set_data_jail(GetFullPathName)");
                e.message += ": '" + r + "'";
                return e;
            }
            full.resize(need);
            const DWORD got =
                ::GetFullPathNameW(w.c_str(), need, full.data(), nullptr);
            if (got == 0 || got >= need) return os_error("set_data_jail(GetFullPathName)");
            full.resize(got);
        }
        namespace fs = std::filesystem;
        const fs::path root_path(full);
        auto anchor = std::make_shared<LockedDirectories>();
        fs::path component = root_path.root_path();
        auto first = lock_directory(component.wstring(), *anchor);
        if (!first) return first.error();
        for (const auto& part : root_path.relative_path()) {
            if (part == L".") continue;
            if (part == L"..") return jail_refused("set_data_jail: parent root refused");
            component /= part;
            auto rlock = lock_directory(component.wstring(), *anchor);
            if (!rlock) return rlock.error();
        }
        for (auto& ch : full) {
            if (ch == L'/') ch = L'\\';
        }
        // Uniform boundary logic: "C:\" stores as "C:"; anything else
        // drops a trailing backslash.
        while (full.size() > 3 && full.back() == L'\\') full.pop_back();
        if (full.size() == 3 && full.back() == L'\\') full.pop_back();  // "C:\" -> "C:"
        anchored.push_back(JailRootW{full, jail_lower(full), std::move(anchor)});
    }
    std::lock_guard<std::mutex> lk(g_jail_mu);
    g_jail_roots = std::move(anchored);
    return {};
}

void File::clear_data_jail() noexcept {
    std::lock_guard<std::mutex> lk(g_jail_mu);
    g_jail_roots.clear();
}

bool File::data_jail_active() noexcept {
    std::lock_guard<std::mutex> lk(g_jail_mu);
    return !g_jail_roots.empty();
}

} // namespace openads::platform

#endif // _WIN32
