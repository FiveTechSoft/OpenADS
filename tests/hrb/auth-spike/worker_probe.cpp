#include "network/auth_worker.h"
#include <cassert>
#include <iostream>
int main(int argc,char** argv){
    assert(argc==3);
    openads::network::AuthWorker w(argv[1],argv[2]);
    assert(w.probe());openads::network::AuthAttempt::Challenge c{};openads::network::AuthResult r;
    std::string b(reinterpret_cast<char*>(c.data()),32);b+="test-only";
    assert(w.authenticate(c,b,"server-owned",r));assert(r.allow&&r.user=="usr"&&r.password=="pw");
    assert(w.authenticate(c,"wrong","server-owned",r)&&!r.allow);
    assert(!w.authenticate(c,"malformed","server-owned",r));
    auto t=std::chrono::steady_clock::now();assert(!w.authenticate(c,"hang","server-owned",r));
    assert(std::chrono::steady_clock::now()-t<std::chrono::seconds(4));assert(w.probe());
    std::cout<<"worker ready/accept/deny/malformed/hang/recovery PASS\n";
}
