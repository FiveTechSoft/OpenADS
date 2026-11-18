#include "doctest.h"
#ifndef _WIN32
#include "network/auth_signal_drain.h"
#include <pthread.h>
#include <signal.h>
#include <unistd.h>

TEST_CASE("auth signal drain is nonblocking when no signal is pending") {
    sigset_t set, old, pending;
    sigemptyset(&set);sigaddset(&set,SIGPIPE);
    REQUIRE(pthread_sigmask(SIG_BLOCK,&set,&old)==0);
    CHECK(openads::network::detail::drain_blocked_signal(SIGPIPE));
    CHECK(sigpending(&pending)==0);
    CHECK(sigismember(&pending,SIGPIPE)==0);
    CHECK(pthread_sigmask(SIG_SETMASK,&old,nullptr)==0);
}

TEST_CASE("auth signal drain consumes broken-pipe signal before restoring mask") {
    sigset_t set, old, pending;
    sigemptyset(&set);sigaddset(&set,SIGPIPE);
    REQUIRE(pthread_sigmask(SIG_BLOCK,&set,&old)==0);
    // CI launchers may inherit SIG_IGN. Use the normal disposition so EPIPE
    // must generate a pending SIGPIPE, then restore the inherited disposition.
    struct sigaction action{}, previous{};
    action.sa_handler=SIG_DFL;sigemptyset(&action.sa_mask);
    REQUIRE(sigaction(SIGPIPE,&action,&previous)==0);
    int fds[2];REQUIRE(pipe(fds)==0);close(fds[0]);
    CHECK(write(fds[1],"x",1)==-1);close(fds[1]);
    CHECK(sigpending(&pending)==0);
    CHECK(sigismember(&pending,SIGPIPE)==1);
    CHECK(openads::network::detail::drain_blocked_signal(SIGPIPE));
    CHECK(sigpending(&pending)==0);
    CHECK(sigismember(&pending,SIGPIPE)==0);
    CHECK(sigaction(SIGPIPE,&previous,nullptr)==0);
    CHECK(pthread_sigmask(SIG_SETMASK,&old,nullptr)==0);
}
#endif
