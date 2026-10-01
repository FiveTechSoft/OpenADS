#include "doctest.h"
#include "openads/ace.h"
#include <cstring>
#include <filesystem>
#include <string>
TEST_CASE("mtfix21 ADT exclusive append no lock pile and Shared guard preserved") {
 namespace fs=std::filesystem;
 auto dir=fs::temp_directory_path()/"mtfix21_adt_regression";
 std::error_code ec;fs::remove_all(dir,ec);fs::create_directories(dir);
 auto path=dir.string();ADSHANDLE conn=0,t=0;
 REQUIRE(AdsConnect60(reinterpret_cast<UNSIGNED8*>(path.data()),ADS_LOCAL_SERVER,nullptr,nullptr,0,&conn)==0);
 UNSIGNED8 name[]="bulk.adt",fields[]="NAME,Character,30;AGE,Numeric,5",fld[]="NAME";
 REQUIRE(AdsCreateTable(conn,name,nullptr,ADS_ADT,ADS_ANSI,0,0,0,fields,&t)==0);
 REQUIRE(AdsCloseTable(t)==0);
 REQUIRE(AdsOpenTable(conn,name,nullptr,ADS_ADT,ADS_ANSI,ADS_COMPATIBLE_LOCKING,ADS_IGNORERIGHTS,ADS_EXCLUSIVE,&t)==0);
 for(int i=1;i<=10000;++i){
  REQUIRE(AdsAppendRecord(t)==0);auto value=std::to_string(i);
  REQUIRE(AdsSetString(t,fld,reinterpret_cast<UNSIGNED8*>(value.data()),static_cast<UNSIGNED32>(value.size()))==0);
  if(i==1||i==5000||i==10000){UNSIGNED16 locks=999;REQUIRE(AdsGetNumLocks(t,&locks)==0);CHECK(locks==0);}
 }
 REQUIRE(AdsCloseTable(t)==0);
 REQUIRE(AdsOpenTable(conn,name,nullptr,ADS_ADT,ADS_ANSI,ADS_COMPATIBLE_LOCKING,ADS_IGNORERIGHTS,ADS_SHARED,&t)==0);
 UNSIGNED32 count=0;REQUIRE(AdsGetRecordCount(t,ADS_RESPECTFILTERS,&count)==0);CHECK(count==10000);
 for(UNSIGNED32 i:{1u,5000u,10000u}){REQUIRE(AdsGotoRecord(t,i)==0);UNSIGNED8 out[64]{};UNSIGNED32 len=sizeof(out);REQUIRE(AdsGetString(t,fld,out,&len,0)==0);CHECK(std::string(reinterpret_cast<char*>(out),len)==std::to_string(i));}
 UNSIGNED8 v[]="changed";CHECK(AdsSetString(t,fld,v,7)==5035);
 REQUIRE(AdsAppendRecord(t)==0);UNSIGNED16 locks=0;REQUIRE(AdsGetNumLocks(t,&locks)==0);CHECK(locks==1);
 REQUIRE(AdsSetString(t,fld,v,7)==0);REQUIRE(AdsWriteRecord(t)==0);REQUIRE(AdsUnlockRecord(t,0)==0);
 REQUIRE(AdsGetNumLocks(t,&locks)==0);CHECK(locks==0);
 REQUIRE(AdsCloseTable(t)==0);REQUIRE(AdsDisconnect(conn)==0);fs::remove_all(dir,ec);
}
