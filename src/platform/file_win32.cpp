#ifdef _WIN32

#include "platform/file.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

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

} // namespace openads::platform

#endif // _WIN32
