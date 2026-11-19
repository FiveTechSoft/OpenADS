#include "doctest.h"
#include "openads/ace.h"
#include "network/server.h"
#include "mgmt/mg_stats.h"
#include "drivers/fpt/fpt_memo.h"
#include <filesystem>
#include <vector>
#include <array>
#include <fstream>
#include <cstring>
#include <iostream>
#include <cstdlib>
namespace {
    namespace fs=std::filesystem;
    void ck(UNSIGNED32 rc,const char* step){
        INFO(step);
        REQUIRE(rc == 0);
    }
    UNSIGNED8* b(std::string& s){
        return reinterpret_cast<UNSIGNED8*>(s.data());
    }
    unsigned sz(int i){
        return i==3?0:(i==4?200000:i*17);
    }
}
// namespace
TEST_CASE("remote memo length follows logical forward and reverse read-ahead position") {
    fs::path dir=fs::temp_directory_path()/"openads_memo_position78";
    fs::remove_all(dir);
    fs::create_directories(dir);
    std::vector<unsigned char> v(97);
    v[0]=0x83;
    v[8]=97;
    v[10]=15;
    std::memcpy(v.data()+32,"ID",2);
    v[43]='C';
    v[48]=4;
    std::memcpy(v.data()+64,"BLOB",4);
    v[75]='M';
    v[80]=10;
    v[96]=13;
    v.push_back(26);
    std::ofstream f(dir/"big.dbf",std::ios::binary);
    f.write((char*)v.data(),v.size());
    f.close();
    REQUIRE(openads::drivers::fpt::FptMemo::create((dir/"big.fpt").string(),64).has_value());
    ADSHANDLE c=0,t=0;
    std::string root=dir.string(),leaf="big.dbf",alias="big",fld="BLOB";
    ck(AdsConnect60(b(root),ADS_LOCAL_SERVER,nullptr,nullptr,0,&c),"connect local");
    ck(AdsOpenTable(c,b(leaf),b(alias),ADS_CDX,0,0,0,0,&t),"open local");
    for(int i=1;
    i<=80;
    i++){
        ck(AdsAppendRecord(t),"append");
        std::string p(sz(i),char('A'+i%26));
        ck(AdsSetString(t,b(fld),b(p),p.size()),"set memo");
        ck(AdsWriteRecord(t),"write");
    }
    ck(AdsCloseTable(t),"close");
    ck(AdsDisconnect(c),"disconnect");
    openads::network::Server srv;
    REQUIRE(srv.start("127.0.0.1",0).has_value());
    std::string uri="tcp://127.0.0.1:"+std::to_string(srv.port())+"/"+dir.string();
    ck(AdsConnect60(b(uri),ADS_REMOTE_SERVER,nullptr,nullptr,0,&c),"connect remote");
    ck(AdsOpenTable(c,b(leaf),b(alias),ADS_CDX,0,0,0,0,&t),"open remote");
    ck(AdsGotoTop(t),"top");
    int bad=0;
    auto& stats=openads::mgmt::process_mg_stats();
    auto check=[&](int i,const char* phase){
        UNSIGNED32 rn=0,n=0;
        ck(AdsGetRecordNum(t,0,&rn),"recno");
        auto before=stats.packets_in.load();
        ck(AdsGetMemoLength(t,b(fld),&n),"memo length");
        auto lenframes=stats.packets_in.load()-before;
        std::vector<UNSIGNED8> out(n+2);
        UNSIGNED32 cap=out.size();
        before=stats.packets_in.load();
        ck(AdsGetString(t,b(fld),out.data(),&cap,0),"memo contents");
        auto strframes=stats.packets_in.load()-before;
        bool match=n==sz(i) && rn==(UNSIGNED32)i;
        for(int j=0;
        j<(int)sz(i) && j<(int)out.size();
        j++)if(out[j]!=(UNSIGNED8)('A'+i%26))match=false;
        CHECK(match);
        if(!match)bad++;
        CHECK(strframes == 0);
        CHECK(lenframes >= 1);
        CHECK(lenframes <= 2);
        std::cout<<phase<<" rec="<<rn<<" len="<<n<<" expected="<<sz(i)<<" lenframes="<<lenframes<<" stringframes="<<strframes<<" correct="<<match<<"\n";
    }
    ;
    check(1,"no-lag");
    for(int i=2;
    i<=20;
    i++){
        ck(AdsSkip(t,1),"skip");
        check(i,"forward");
    }
    for(int i=19;
    i>=1;
    i--){
        ck(AdsSkip(t,-1),"reverse skip");
        check(i,"reverse");
    }
    ck(AdsGotoTop(t),"failure top");
    ck(AdsSkip(t,1),"failure skip");
    srv.stop();
    UNSIGNED32 untouched=777;
    auto er=AdsGetMemoLength(t,b(fld),&untouched);
    std::cout<<"stopped-server rc="<<er<<" output="<<untouched<<"\n";
    CHECK(er != 0);
    CHECK(untouched == 777);
    if(er==0 || untouched!=777)bad++;
    (void)AdsCloseTable(t);
    (void)AdsDisconnect(c);
    srv.stop();
    std::cout<<"wrong_lengths="<<bad<<"\n";
    CHECK(bad == 0);
    fs::remove_all(dir);
}
