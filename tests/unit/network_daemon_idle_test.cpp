// Pritpal Bedi's mtfix40 tea-break report: healthy ERP silence is not death.
#include "doctest.h"
#include "network/client.h"
#include "network/session.h"
#include "network/worker_pool.h"
#include "openads/ace.h"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <thread>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#else
#include <sys/socket.h>
#endif

namespace {
using namespace openads::network;
using Clock = std::chrono::steady_clock;
std::atomic<long long> idle_offset{0};
Clock::time_point idle_now() noexcept {
    return Clock::now() + std::chrono::seconds(idle_offset.load());
}
struct IdleFixture {
    std::filesystem::path dir = std::filesystem::temp_directory_path()/"openads_daemon_idle41";
    Server server;
    std::unique_ptr<WorkerPool> pool;
    Socket listener;
    std::uint16_t port = 0;
    explicit IdleFixture(bool reactor, bool fake = true, std::uint32_t idle_seconds = 0) {
        idle_offset.store(0);
        std::error_code ec; std::filesystem::remove_all(dir,ec); std::filesystem::create_directories(dir);
        auto str=dir.string(); std::vector<UNSIGNED8> path(str.begin(),str.end()); path.push_back(0);
        ADSHANDLE c=0,t=0;
        REQUIRE(AdsConnect60(path.data(),ADS_LOCAL_SERVER,nullptr,nullptr,0,&c)==0);
        UNSIGNED8 name[]="login.dbf",def[]="QTY,N,12,2",qty[]="QTY";
        REQUIRE(AdsCreateTable(c,name,nullptr,ADS_CDX,ADS_ANSI,0,0,0,def,&t)==0);
        REQUIRE(AdsAppendRecord(t)==0); REQUIRE(AdsSetDouble(t,qty,100)==0);
        REQUIRE(AdsWriteRecord(t)==0); REQUIRE(AdsCloseTable(t)==0); REQUIRE(AdsDisconnect(c)==0);
        server.set_established_session_idle_seconds(idle_seconds);
        if(fake) server.set_session_clock(idle_now);
        if(reactor) {
            auto l=listen_tcp({"127.0.0.1",0,16}); REQUIRE(l); listener=l.value();
            auto p=socket_local_port(listener); REQUIRE(p); port=p.value();
            pool=std::make_unique<WorkerPool>(server,1); pool->start();
        } else { REQUIRE(server.start("127.0.0.1",0)); port=server.port(); }
    }
    void connect(RemoteConnection& c) {
        auto s=connect_tcp("127.0.0.1",port); REQUIRE(s);
        if(pool) { auto a=accept_one(listener); REQUIRE(a); pool->submit(a.value(),dir.string(),port); }
        REQUIRE(c.connect_with_transport(make_plain_transport(s.value()),dir.string()));
    }
    std::uint32_t open(RemoteConnection& c) {
        auto t=c.open_table("login.dbf",static_cast<std::uint16_t>(openads::engine::OpenMode::Shared)); REQUIRE(t); return t.value().id;
    }
    ~IdleFixture() {
        if(pool) pool->stop(); else server.stop();
        if(listener.valid()) sock_close(listener);
        idle_offset.store(0);
        std::error_code ec; std::filesystem::remove_all(dir,ec);
    }
};
bool empty_sessions(Server& s) {
    for(int i=0;i<100;++i) {
        if(s.sessions_snapshot().empty()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return false;
}
void idle_replay(bool reactor, bool fake, std::chrono::seconds pause) {
    IdleFixture f(reactor,fake);
    RemoteConnection holder, challenger; f.connect(holder); f.connect(challenger);
    auto a=f.open(holder), b=f.open(challenger);
    REQUIRE(holder.goto_record(a,1)); REQUIRE(challenger.goto_record(b,1));
    REQUIRE(holder.lock_record(a,1));
    if(fake) { idle_offset.store(pause.count()); std::this_thread::sleep_for(std::chrono::milliseconds(450)); }
    else std::this_thread::sleep_for(pause);
    // A real wire operation, not a client cached read, proves idle resume.
    REQUIRE(holder.goto_record(a,1));
    CHECK_FALSE(challenger.lock_record(b,1));
    REQUIRE(holder.set_field(a,"QTY","97")); REQUIRE(holder.flush_table(a));
    holder.disconnect(); // peer EOF/Disconnect must release its physical lock.
    bool acquired=false;
    for(int i=0;i<100 && !acquired;++i) {
        acquired=challenger.lock_record(b,1).has_value();
        if(!acquired) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    REQUIRE(acquired); REQUIRE(challenger.unlock_table(b)); challenger.disconnect();
    CHECK(empty_sessions(f.server));
}
}
TEST_CASE("mtfix41 daemon healthy idle resume and login ownership dedicated") { idle_replay(false,true,std::chrono::minutes(45)); }
TEST_CASE("mtfix41 daemon healthy idle resume and login ownership reactor") { idle_replay(true,true,std::chrono::minutes(45)); }
TEST_CASE("mtfix41 daemon real clock idle soak" * doctest::skip(std::getenv("OPENADS_IDLE_SOAK")==nullptr)) {
    idle_replay(false,false,std::chrono::seconds(310));
}
TEST_CASE("mtfix41 daemon expiry keeps handshake management and partial clocks") {
    Server s; s.set_session_clock(idle_now); idle_offset.store(0);
    const auto before=idle_now();
    Session handshake(s,Socket{},"",0);
    CHECK_FALSE(handshake.expired_at(before+std::chrono::seconds(29)));
    CHECK(handshake.expired_at(before+std::chrono::seconds(31)));
    // Upstream applies unfinished-handshake timeout to embedded listeners too.
    Server local; Session embedded(local,Socket{},"",0);
    CHECK(embedded.expired_at(Clock::now()+std::chrono::hours(24)));
}

TEST_CASE("mtfix41 daemon management idle limit retained") {
    auto listener=listen_tcp({"127.0.0.1",0,4}); REQUIRE(listener);
    auto port=socket_local_port(listener.value()); REQUIRE(port);
    auto client=connect_tcp("127.0.0.1",port.value()); REQUIRE(client);
    auto peer=accept_one(listener.value()); REQUIRE(peer);
    Server srv; srv.set_session_clock(idle_now); idle_offset.store(0);
    {
        Session session(srv,peer.value(),"",0);
        Frame mg; mg.opcode=Opcode::MgConnect;
        auto response=session.dispatch(mg); REQUIRE(response.reply);
        REQUIRE(response.reply->opcode==Opcode::MgConnectAck);
        CHECK_FALSE(session.expired_at(idle_now()+std::chrono::minutes(4)));
        CHECK(session.expired_at(idle_now()+std::chrono::minutes(6)));
    }
    sock_close(peer.value()); sock_close(client.value()); sock_close(listener.value());
}
TEST_CASE("mtfix41 daemon partial frame still expires while DBF connected") {
    IdleFixture f(false);
    auto client=connect_tcp("127.0.0.1",f.port); REQUIRE(client);
    RemoteConnection connection; REQUIRE(connection.connect_with_transport(make_plain_transport(client.value()),f.dir.string()));
    // A partial next header must not gain the established idle exemption.
    std::uint8_t byte=0; auto sent=sock_send(client.value(),&byte,1); REQUIRE(sent);
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    idle_offset.store(31); CHECK(empty_sessions(f.server));
}
TEST_CASE("mtfix41 daemon dead peer releases login lock in both schedulers") {
    for(bool reactor : {false,true}) {
        IdleFixture f(reactor);
        auto sock=connect_tcp("127.0.0.1",f.port); REQUIRE(sock);
        if(f.pool) { auto peer=accept_one(f.listener); REQUIRE(peer); f.pool->submit(peer.value(),f.dir.string(),f.port); }
        RemoteConnection dead,other;
        REQUIRE(dead.connect_with_transport(make_plain_transport(sock.value()),f.dir.string()));
        f.connect(other); auto a=f.open(dead),b=f.open(other);
        REQUIRE(dead.goto_record(a,1)); REQUIRE(other.goto_record(b,1)); REQUIRE(dead.lock_record(a,1));
        CHECK_FALSE(other.lock_record(b,1));
        // Simulate a process closing transport without a Disconnect frame.
        // Release through shutdown, then let RemoteConnection own final close.
#ifdef _WIN32
        ::shutdown(static_cast<SOCKET>(sock.value().handle),SD_BOTH);
#else
        ::shutdown(static_cast<int>(sock.value().handle),SHUT_RDWR);
#endif
        bool acquired=false;
        for(int i=0;i<100 && !acquired;++i) {
            acquired=other.lock_record(b,1).has_value();
            if(!acquired) std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        REQUIRE(acquired); REQUIRE(other.unlock_table(b)); other.disconnect(); dead.disconnect();
        CHECK(empty_sessions(f.server));
    }
}

TEST_CASE("mtfix41 daemon opt in established idle expiry releases login lock") {
    for(bool reactor : {false,true}) {
        IdleFixture f(reactor,true,60);
        RemoteConnection holder; f.connect(holder); auto a=f.open(holder);
        REQUIRE(holder.goto_record(a,1)); REQUIRE(holder.lock_record(a,1));
        idle_offset.store(61); CHECK(empty_sessions(f.server));
        // A new live session can acquire the expired owner's physical lock.
        RemoteConnection next; f.connect(next); auto b=f.open(next);
        REQUIRE(next.goto_record(b,1)); REQUIRE(next.lock_record(b,1));
        REQUIRE(next.unlock_table(b)); next.disconnect(); holder.disconnect();
        CHECK(empty_sessions(f.server));
    }
}
