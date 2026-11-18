#pragma once
#ifndef _WIN32
#include <signal.h>

namespace openads::network::detail {
// The caller blocks signal_number while writing to the worker pipe.
// POSIX sigpending + sigwait also work on macOS, unlike sigtimedwait.
inline bool drain_blocked_signal(int signal_number) {
    sigset_t pending, one;
    sigemptyset(&one);
    sigaddset(&one, signal_number);
    for (;;) {
        if (sigpending(&pending) != 0) return false;
        if (sigismember(&pending, signal_number) != 1) return true;
        int received = 0;
        if (sigwait(&one, &received) != 0) return false;
    }
}
}
#endif
