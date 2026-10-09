// Pritpal Bedi's 50-line invoice stock update log motivated this regression.
#include "doctest.h"
#include "openads/ace.h"
#include "network/server.h"
#include <filesystem>
#include <vector>
#include <cstring>
#include <cstdio>
#include <cstdlib>
namespace {
struct StockFixture {
    std::filesystem::path dir = std::filesystem::temp_directory_path()/"openads_locked_stock39";
    openads::network::Server server;
    ADSHANDLE a=0,b=0,ta=0,tb=0,ia=0,ib=0;
    UNSIGNED8 qty[8]="QTY";
    StockFixture() {
        std::error_code ec; std::filesystem::remove_all(dir,ec); std::filesystem::create_directories(dir);
        auto s=dir.string(); std::vector<UNSIGNED8> local(s.begin(),s.end()); local.push_back(0);
        ADSHANDLE c=0,t=0,i=0;
        REQUIRE(AdsConnect60(local.data(),ADS_LOCAL_SERVER,nullptr,nullptr,0,&c)==0);
        UNSIGNED8 name[]="stock.dbf",def[]="SKU,C,8,0;QTY,N,12,2",sku[]="SKU",value[]="ITEM0001";
        REQUIRE(AdsCreateTable(c,name,nullptr,ADS_CDX,ADS_ANSI,0,0,0,def,&t)==0);
        REQUIRE(AdsAppendRecord(t)==0);
        REQUIRE(AdsSetString(t,sku,value,8)==0); REQUIRE(AdsSetDouble(t,qty,100)==0); REQUIRE(AdsWriteRecord(t)==0);
        UNSIGNED8 bag[]="stock.cdx",tag[]="SKU";
        REQUIRE(AdsCreateIndex61(t,bag,tag,sku,nullptr,nullptr,ADS_COMPOUND,0,&i)==0);
        REQUIRE(AdsCloseTable(t)==0); REQUIRE(AdsDisconnect(c)==0);
        REQUIRE(server.start("127.0.0.1",0).has_value());
        auto uri="tcp://127.0.0.1:"+std::to_string(server.port())+"/"+s;
        std::vector<UNSIGNED8> remote(uri.begin(),uri.end()); remote.push_back(0);
        for (auto p : {std::pair<ADSHANDLE*,ADSHANDLE*>{&a,&ta},{&b,&tb}}) {
            REQUIRE(AdsConnect60(remote.data(),ADS_REMOTE_SERVER,nullptr,nullptr,0,p.first)==0);
            REQUIRE(AdsOpenTable(*p.first,name,nullptr,ADS_CDX,ADS_ANSI,ADS_COMPATIBLE_LOCKING,0,ADS_SHARED,p.second)==0);
        }
        REQUIRE(AdsGetIndexHandle(ta,tag,&ia)==0); REQUIRE(AdsGetIndexHandle(tb,tag,&ib)==0);
    }
    void seek(ADSHANDLE i) { UNSIGNED8 key[]="ITEM0001"; UNSIGNED16 found=0; REQUIRE(AdsSeek(i,key,8,ADS_STRINGKEY,ADS_HARDSEEK,&found)==0); REQUIRE(found==1); }
    ~StockFixture(){ if(ta)AdsCloseTable(ta);if(tb)AdsCloseTable(tb);if(a)AdsDisconnect(a);if(b)AdsDisconnect(b); }
};
}
TEST_CASE("mtfix39 locked row is fresh after another station writes") {
    StockFixture f; double n=0;
    f.seek(f.ia); REQUIRE(AdsGetDouble(f.ta,f.qty,&n)==0); REQUIRE(n==100);
    f.seek(f.ib); REQUIRE(AdsLockRecord(f.tb,1)==0); REQUIRE(AdsSetDouble(f.tb,f.qty,95)==0); REQUIRE(AdsUnlockTable(f.tb)==0);
    REQUIRE(AdsLockRecord(f.ta,1)==0);
    REQUIRE(AdsGetDouble(f.ta,f.qty,&n)==0);
    INFO("A read after lock: " << n); CHECK(n==95);
    REQUIRE(AdsSetDouble(f.ta,f.qty,n-2)==0); REQUIRE(AdsUnlockTable(f.ta)==0);
    REQUIRE(AdsRefreshRecord(f.tb)==0); REQUIRE(AdsGetDouble(f.tb,f.qty,&n)==0); CHECK(n==93);
}
TEST_CASE("mtfix39 invoice seek lock subtract replace unlock replay") {
    StockFixture f; double n=0;
    for(int k=0;k<50;++k) {
        f.seek(f.ia); REQUIRE(AdsUnlockTable(f.ta)==0); REQUIRE(AdsLockRecord(f.ta,1)==0);
        REQUIRE(AdsGetDouble(f.ta,f.qty,&n)==0);
        UNSIGNED16 locked=0; REQUIRE(AdsIsRecordLocked(f.ta,0,&locked)==0); REQUIRE(locked==1);
        REQUIRE(AdsSetDouble(f.ta,f.qty,n-1)==0); REQUIRE(AdsUnlockTable(f.ta)==0);
    }
    REQUIRE(AdsRefreshRecord(f.tb)==0); REQUIRE(AdsGetDouble(f.tb,f.qty,&n)==0); CHECK(n==50);
}

TEST_CASE("mtfix39 repeated lock releases once and failures do not steal") {
    StockFixture f;
    f.seek(f.ia); f.seek(f.ib);
    REQUIRE(AdsLockRecord(f.ta,1)==0);
    REQUIRE(AdsLockRecord(f.ta,1)==0);
    CHECK(AdsLockRecord(f.tb,1)!=0);
    UNSIGNED16 held=1; REQUIRE(AdsIsRecordLocked(f.tb,1,&held)==0); CHECK(held==0);
    REQUIRE(AdsUnlockRecord(f.ta,1)==0);
    REQUIRE(AdsLockRecord(f.tb,1)==0);
    REQUIRE(AdsUnlockRecord(f.tb,1)==0);
}

TEST_CASE("mtfix39 old server without kCapLockedRow refreshes after lock") {
    // Simulated pre-fix server: no capability echo, no row trailer on the
    // LockRecord ack. The client must negotiate down and do a real refresh
    // after acquiring the lock, never serve the stale cached row.
    struct EnvGuard {
        EnvGuard() {
#ifdef _WIN32
            _putenv_s("OPENADS_NO_LOCKED_ROW_CAP", "1");
#else
            setenv("OPENADS_NO_LOCKED_ROW_CAP", "1", 1);
#endif
        }
        ~EnvGuard() {
#ifdef _WIN32
            _putenv_s("OPENADS_NO_LOCKED_ROW_CAP", "");
#else
            unsetenv("OPENADS_NO_LOCKED_ROW_CAP");
#endif
        }
    } guard;
    StockFixture f; double n=0;
    f.seek(f.ia); REQUIRE(AdsGetDouble(f.ta,f.qty,&n)==0); REQUIRE(n==100);
    f.seek(f.ib); REQUIRE(AdsLockRecord(f.tb,1)==0); REQUIRE(AdsSetDouble(f.tb,f.qty,95)==0); REQUIRE(AdsUnlockTable(f.tb)==0);
    REQUIRE(AdsLockRecord(f.ta,1)==0);
    UNSIGNED16 held=0; REQUIRE(AdsIsRecordLocked(f.ta,1,&held)==0); CHECK(held==1);
    REQUIRE(AdsGetDouble(f.ta,f.qty,&n)==0);
    INFO("A read after lock (old server): " << n); CHECK(n==95);
    REQUIRE(AdsSetDouble(f.ta,f.qty,n-2)==0); REQUIRE(AdsUnlockTable(f.ta)==0);
    REQUIRE(AdsRefreshRecord(f.tb)==0); REQUIRE(AdsGetDouble(f.tb,f.qty,&n)==0); CHECK(n==93);
}

TEST_CASE("mtfix39 contended lock failure keeps cached row and steals nothing") {
    StockFixture f; double n=0;
    f.seek(f.ia); REQUIRE(AdsGetDouble(f.ta,f.qty,&n)==0); REQUIRE(n==100);
    f.seek(f.ib);
    REQUIRE(AdsLockRecord(f.tb,1)==0);
    CHECK(AdsLockRecord(f.ta,1)!=0);
    UNSIGNED16 held=1; REQUIRE(AdsIsRecordLocked(f.ta,1,&held)==0); CHECK(held==0);
    // A failed lock changes no client state: the cached row is served as-is,
    // never refreshed and never presented as locked-fresh.
    REQUIRE(AdsGetDouble(f.ta,f.qty,&n)==0);
    INFO("A cached read after failed lock: " << n); CHECK(n==100);
    // B's lock survives A's failed attempt, and A releasing a record it
    // does not hold must not release B's lock (the unlock reply itself is
    // lenient - pre-existing wire semantics, not mtfix39 scope).
    (void)AdsUnlockRecord(f.ta,1);
    UNSIGNED16 bheld=0; REQUIRE(AdsIsRecordLocked(f.tb,1,&bheld)==0); CHECK(bheld==1);
    REQUIRE(AdsSetDouble(f.tb,f.qty,95)==0); REQUIRE(AdsUnlockTable(f.tb)==0);
    REQUIRE(AdsLockRecord(f.ta,1)==0);
    REQUIRE(AdsGetDouble(f.ta,f.qty,&n)==0);
    INFO("A read after acquiring post-contention: " << n); CHECK(n==95);
    REQUIRE(AdsUnlockTable(f.ta)==0);
}

namespace {
struct MultiFixture {
    std::filesystem::path dir = std::filesystem::temp_directory_path()/"openads_locked_multi39";
    openads::network::Server server;
    ADSHANDLE a=0,b=0,ta=0,tb=0;
    UNSIGNED8 qty[8]="QTY";
    MultiFixture() {
        std::error_code ec; std::filesystem::remove_all(dir,ec); std::filesystem::create_directories(dir);
        auto s=dir.string(); std::vector<UNSIGNED8> local(s.begin(),s.end()); local.push_back(0);
        ADSHANDLE c=0,t=0;
        REQUIRE(AdsConnect60(local.data(),ADS_LOCAL_SERVER,nullptr,nullptr,0,&c)==0);
        UNSIGNED8 name[]="stock.dbf",def[]="SKU,C,8,0;QTY,N,12,2",sku[]="SKU";
        REQUIRE(AdsCreateTable(c,name,nullptr,ADS_CDX,ADS_ANSI,0,0,0,def,&t)==0);
        for (int k=1;k<=30;++k) {
            char v[9]; std::snprintf(v,sizeof v,"ITEM%04d",k);
            REQUIRE(AdsAppendRecord(t)==0);
            REQUIRE(AdsSetString(t,sku,reinterpret_cast<UNSIGNED8*>(v),8)==0);
            REQUIRE(AdsSetDouble(t,qty,100)==0); REQUIRE(AdsWriteRecord(t)==0);
        }
        UNSIGNED8 bag[]="stock.cdx",tag[]="SKU";
        ADSHANDLE i0=0;
        REQUIRE(AdsCreateIndex61(t,bag,tag,sku,nullptr,nullptr,ADS_COMPOUND,0,&i0)==0);
        REQUIRE(AdsCloseTable(t)==0); REQUIRE(AdsDisconnect(c)==0);
        REQUIRE(server.start("127.0.0.1",0).has_value());
        auto uri="tcp://127.0.0.1:"+std::to_string(server.port())+"/"+s;
        std::vector<UNSIGNED8> remote(uri.begin(),uri.end()); remote.push_back(0);
        for (auto p : {std::pair<ADSHANDLE*,ADSHANDLE*>{&a,&ta},{&b,&tb}}) {
            REQUIRE(AdsConnect60(remote.data(),ADS_REMOTE_SERVER,nullptr,nullptr,0,p.first)==0);
            REQUIRE(AdsOpenTable(*p.first,name,nullptr,ADS_CDX,ADS_ANSI,ADS_COMPATIBLE_LOCKING,0,ADS_SHARED,p.second)==0);
        }
        UNSIGNED8 tagn[]="SKU";
        REQUIRE(AdsGetIndexHandle(tb,tagn,&ib)==0);
    }
    ADSHANDLE ib=0;
    ~MultiFixture(){ if(ta)AdsCloseTable(ta);if(tb)AdsCloseTable(tb);if(a)AdsDisconnect(a);if(b)AdsDisconnect(b); }
};
}
TEST_CASE("mtfix39 lock ack row is fresh when the row sat in the prefetch block") {
    // Same stale-read regression as the first case, with the row ALSO
    // sitting stale in the client read-ahead block and row cache: the lock
    // ack trailer must replace every stale copy.
    MultiFixture f; double n=0;
    REQUIRE(AdsGotoTop(f.ta)==0);
    REQUIRE(AdsSkip(f.ta,1)==0);   // forward lookahead block cached client-side
    REQUIRE(AdsSkip(f.ta,1)==0);
    REQUIRE(AdsGotoRecord(f.ta,8)==0);
    REQUIRE(AdsGetDouble(f.ta,f.qty,&n)==0); REQUIRE(n==100);
    // B rewrites row 8 through the index while A holds the stale copy.
    UNSIGNED8 key[]="ITEM0008"; UNSIGNED16 found=0;
    REQUIRE(AdsSeek(f.ib,key,8,ADS_STRINGKEY,ADS_HARDSEEK,&found)==0); REQUIRE(found==1);
    REQUIRE(AdsLockRecord(f.tb,8)==0);
    REQUIRE(AdsSetDouble(f.tb,f.qty,7)==0);
    REQUIRE(AdsUnlockTable(f.tb)==0);
    REQUIRE(AdsLockRecord(f.ta,8)==0);
    REQUIRE(AdsGetDouble(f.ta,f.qty,&n)==0);
    INFO("A read of locked row 8: " << n); CHECK(n==7);
    REQUIRE(AdsUnlockTable(f.ta)==0);
}
