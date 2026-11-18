#include "network/server.h"
#include "network/tls_transport.h"
#include "openads/ace.h"
#include "engine/data_dict.h"
#include <cassert>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <thread>
using namespace openads::network;
namespace fs=std::filesystem;
int main(int argc,char** argv){
    assert(argc==3);auto dir=fs::current_path()/"oads_hrb_auth_spike_proof";
    fs::remove_all(dir);fs::create_directories(dir);
    ADSHANDLE local=0,seed=0;auto directory=dir.string();
    assert(AdsConnect60(reinterpret_cast<UNSIGNED8*>(&directory[0]),ADS_LOCAL_SERVER,nullptr,nullptr,0,&local)==0);
    auto filename=(dir/"ledger.dbf").string();UNSIGNED8 definition[]="ID,C,8;";
    assert(AdsCreateTable(local,reinterpret_cast<UNSIGNED8*>(&filename[0]),nullptr,ADS_CDX,ADS_ANSI,ADS_PROPRIETARY_LOCKING,ADS_IGNORERIGHTS,0,definition,&seed)==0);
    assert(AdsAppendRecord(seed)==0);assert(AdsWriteRecord(seed)==0);AdsCloseTable(seed);AdsDisconnect(local);
    auto ddpath=(dir/"test.add").string();
    // TEST-FIXTURE ONLY: Winlibs10.2 filesystem::rename cannot overwrite.
    // Delete seed files before setup saves; no live dictionary is attached yet.
    auto prepare_seed_save=[&]{fs::remove(ddpath);fs::remove(dir/"test.am");};
    {auto cr=openads::engine::DataDict::create(ddpath);assert(cr);auto dd=std::move(cr).value();
     prepare_seed_save();auto created=dd.create_user("usr");if(!created)std::cerr<<"fixture create_user: "<<created.error().message<<"\n";assert(created);
     prepare_seed_save();assert(dd.add_user_to_group("usr","DB:Admin"));
     prepare_seed_save();assert(dd.add_table("ledger","ledger.dbf"));
     prepare_seed_save();assert(dd.set_user_property("usr","prop_1101","pw"));
     prepare_seed_save();assert(dd.set_db_property("prop_5","1"));}

    auto tls=make_auth_tls_config();assert(tls);
    auto w=std::make_shared<AuthWorker>(argv[1],argv[2]);assert(w->probe());
    Server srv;srv.set_data_dir(dir.string());srv.add_credential("usr","pw");srv.set_auth_worker(w);assert(srv.set_tls(tls.value()));assert(srv.start("127.0.0.1",0));
    auto url="auth://127.0.0.1:"+std::to_string(srv.port())+"/"+ddpath;
    auto connect=[&](ADSHANDLE& h){ADSHANDLE p=0;UNSIGNED8 c[32];assert(OAdsBeginAuth(url.c_str(),&p,c)==0);std::string blob(reinterpret_cast<char*>(c),32);blob+="test-only";auto code=OAdsCompleteAuth(p,reinterpret_cast<const UNSIGNED8*>(blob.data()),static_cast<UNSIGNED32>(blob.size()),&h);if(code)std::cerr<<"CompleteAuth code="<<code<<"\n";return code;};
    ADSHANDLE a=0,b=0;assert(connect(a)==0&&a);AdsDisconnect(a);
    url="auth://127.0.0.1:"+std::to_string(srv.port())+"/"+dir.string();
    assert(connect(a)==0&&a);assert(connect(b)==0&&b);
    UNSIGNED8 table[]="ledger.dbf";ADSHANDLE ta=0,tb=0;
    assert(AdsOpenTable(a,table,nullptr,ADS_CDX,ADS_ANSI,ADS_PROPRIETARY_LOCKING,ADS_IGNORERIGHTS,0,&ta)==0);
    assert(AdsOpenTable(b,table,nullptr,ADS_CDX,ADS_ANSI,ADS_PROPRIETARY_LOCKING,ADS_IGNORERIGHTS,0,&tb)==0);
    AdsSetLockRetryCount(0,0);AdsSetLockCycle(0,1);
    assert(AdsLockRecord(ta,1)==0);assert(AdsLockRecord(tb,1)!=0);
    assert(AdsUnlockRecord(ta,1)==0);assert(AdsLockRecord(tb,1)==0);assert(AdsUnlockRecord(tb,1)==0);
    AdsCloseTable(ta);AdsCloseTable(tb);AdsDisconnect(a);AdsDisconnect(b);
    // Wrong dictionary password must deny even after .hrb approved valid daemon credentials.
    {auto dd=openads::engine::DataDict::open(ddpath);assert(dd);prepare_seed_save();assert(dd.value().set_user_property("usr","prop_1101","changed"));}
    url="auth://127.0.0.1:"+std::to_string(srv.port())+"/"+ddpath;
    ADSHANDLE denied=0;assert(connect(denied)!=0&&denied==0);
    srv.stop();fs::remove_all(dir);
    std::cout<<"DD normal login / wrong-password rejection / two-session DBFCDX record exclusion PASS\n";
}
