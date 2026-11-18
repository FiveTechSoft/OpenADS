#include "doctest.h"
#include "network/auth_attempt.h"
using openads::network::AuthAttempt;
TEST_CASE("auth pending rejects plaintext and consumes single challenge") {
    AuthAttempt a;
    AuthAttempt::Challenge c{},out{};c[0]=42;c[1]=0;c[31]=255;
    auto now=AuthAttempt::Clock::time_point{};
    CHECK_FALSE(a.begin(c,false,now,std::chrono::seconds(30)));
    CHECK_FALSE(a.begin(c,true,now,std::chrono::seconds(0)));
    CHECK_FALSE(a.begin(c,true,now,std::chrono::seconds(61)));
    REQUIRE(a.begin(c,true,now,std::chrono::seconds(30)));
    CHECK_FALSE(a.begin(c,true,now,std::chrono::seconds(30)));
    REQUIRE(a.take(now+std::chrono::seconds(29),32,out));CHECK(out==c);
    CHECK(a.state()==AuthAttempt::State::Consumed);
    CHECK_FALSE(a.take(now,32,out));CHECK(out==AuthAttempt::Challenge{});
    CHECK_FALSE(a.begin(c,true,now,std::chrono::seconds(30)));
}
TEST_CASE("auth deadline and cancellation reject completion") {
    auto now=AuthAttempt::Clock::time_point{};
    AuthAttempt::Challenge c{},out{};
    for(int mode=0;mode<3;++mode) {
        AuthAttempt a;REQUIRE(a.begin(c,true,now,std::chrono::seconds(30)));
        if(mode==0)a.cancel();
        CHECK_FALSE(a.take(now+std::chrono::seconds(mode==2?31:30),32,out));
        CHECK(a.state()==(mode==0?AuthAttempt::State::Cancelled:AuthAttempt::State::Expired));
    }
}
TEST_CASE("auth malformed blob consumes attempt and connections do not cross") {
    auto now=AuthAttempt::Clock::time_point{};
    AuthAttempt::Challenge c{},out{};
    for(auto size:{std::size_t(0),std::size_t(4097)}) {
        AuthAttempt a;REQUIRE(a.begin(c,true,now,std::chrono::seconds(30)));
        CHECK_FALSE(a.take(now,size,out));CHECK(a.state()==AuthAttempt::State::Consumed);
        CHECK_FALSE(a.take(now,1,out));
    }
    AuthAttempt a,b;auto c2=c;c2[9]=77;
    REQUIRE(a.begin(c,true,now,std::chrono::seconds(30)));
    REQUIRE(b.begin(c2,true,now,std::chrono::seconds(30)));
    a.cancel();REQUIRE(b.take(now,4096,out));CHECK(out==c2);
}
#include "network/auth_result.h"
TEST_CASE("auth result exact versioned binary schema denies malformed identity") {
    using openads::network::AuthResult;using openads::network::parse_auth_result;
    AuthResult out;
    std::string s="OAUTH";s.append({1,1,3,0});s+="usr";s.append({2,0});s+="pw";
    REQUIRE(parse_auth_result(s,out));CHECK(out.allow);CHECK(out.user=="usr");CHECK(out.password=="pw");
    for(std::size_t n=0;n<s.size();++n)CHECK_FALSE(parse_auth_result(std::string_view(s).substr(0,n),out));
    auto bad=s;bad+='x';CHECK_FALSE(parse_auth_result(bad,out));
    bad=s;bad[5]=2;CHECK_FALSE(parse_auth_result(bad,out));
    bad=s;bad[6]=2;CHECK_FALSE(parse_auth_result(bad,out));
    bad=s;bad[6]=0;CHECK_FALSE(parse_auth_result(bad,out));
    bad=s;bad[9]=0;CHECK_FALSE(parse_auth_result(bad,out));
    CHECK_FALSE(out.allow);CHECK(out.password.empty());
    std::string deny="OAUTH";deny.append({1,0,0,0,0,0});
    REQUIRE(parse_auth_result(deny,out));CHECK_FALSE(out.allow);
    std::string anon="OAUTH";anon.append({1,1,0,0,0,0});CHECK_FALSE(parse_auth_result(anon,out));
}
