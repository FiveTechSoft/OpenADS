#pragma once

#include <cstdint>
#include <optional>

namespace openads::platform {

// Current resident set size (physical memory) of this process, in
// bytes. Returns 0 if it cannot be determined.
std::uint64_t process_rss_bytes();
// Linux process descriptor count and soft RLIMIT_NOFILE; unavailable elsewhere.
std::optional<std::uint64_t> process_fd_count();
std::optional<std::uint64_t> process_fd_limit();

}  // namespace openads::platform
