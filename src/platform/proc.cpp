#include "platform/proc.h"

#if defined(_WIN32)
#  include <windows.h>
#  include <psapi.h>
#elif defined(__APPLE__)
#  include <mach/mach.h>
#else
#  include <cstdio>
#  include <unistd.h>
#endif

#if defined(__linux__)
#  include <dirent.h>
#  include <sys/resource.h>
#  include <cerrno>
#endif

namespace openads::platform {

std::uint64_t process_rss_bytes() {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
        return static_cast<std::uint64_t>(pmc.WorkingSetSize);
    return 0;
#elif defined(__APPLE__)
    mach_task_basic_info_data_t info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                  reinterpret_cast<task_info_t>(&info), &count)
            == KERN_SUCCESS)
        return static_cast<std::uint64_t>(info.resident_size);
    return 0;
#else
    std::FILE* f = std::fopen("/proc/self/statm", "r");
    if (!f) return 0;
    long pages = 0, resident = 0;
    int n = std::fscanf(f, "%ld %ld", &pages, &resident);
    std::fclose(f);
    if (n < 2) return 0;
    long pg = sysconf(_SC_PAGESIZE);
    return static_cast<std::uint64_t>(resident) *
           static_cast<std::uint64_t>(pg > 0 ? pg : 4096);
#endif
}

std::optional<std::uint64_t> process_fd_count() {
#if defined(__linux__)
    DIR* dir = opendir("/proc/self/fd");
    if (!dir) return std::nullopt;
    std::uint64_t count = 0;
    errno = 0;
    while (const auto* ent = readdir(dir))
        if (ent->d_name[0] != '.') ++count;
    const bool failed = errno != 0;
    closedir(dir);
    if (failed) return std::nullopt;
    // Exclude the descriptor this query opened to enumerate /proc/self/fd.
    return count > 0 ? count - 1 : 0;
#else
    return std::nullopt;
#endif
}
std::optional<std::uint64_t> process_fd_limit() {
#if defined(__linux__)
    struct rlimit lim{};
    if (getrlimit(RLIMIT_NOFILE, &lim) != 0 || lim.rlim_cur == RLIM_INFINITY)
        return std::nullopt;
    return static_cast<std::uint64_t>(lim.rlim_cur);
#else
    return std::nullopt;
#endif
}

}  // namespace openads::platform
