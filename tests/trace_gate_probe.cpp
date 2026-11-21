// Synthetic, isolated-process regression probe. Run with all diagnostic envs set.
#include "openads/ace.h"
#include "network/server.h"
#include "network/client.h"
#include "util/log.h"
#include "mgmt/error_log.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstring>
#include <vector>
#include <stdexcept>
namespace fs=std::filesystem;
void checked(UNSIGNED32 rc,int line){if(rc)throw std::runtime_error("ACE failure "+std::to_string(rc)+" at line "+std::to_string(line));}
#define ok(x) checked((x),__LINE__)
int main(int argc,char**argv){
 std::string mode=argc>1?argv[1]:"default";
 const bool marks = mode == "markers";
 if (marks) mode = "caller";
 auto mark = [&](const char* label) {
  if (marks) ok(OAdsTraceMark(reinterpret_cast<const UNSIGNED8*>(label), static_cast<UNSIGNED32>(std::strlen(label))));
 };

 bool was= openads::util::logging_enabled();
 if(mode=="on"||mode=="lifecycle"||mode=="caller")ok(OAdsSetLogging(1));
 else if(mode=="off")ok(OAdsSetLogging(0));
 if(mode=="on" && !openads::util::logging_enabled())return 8;
 mark("APP_BEGIN");
 auto dir=fs::temp_directory_path()/"openads_trace_gate_probe";fs::remove_all(dir);fs::create_directories(dir);
 auto ds=dir.string();std::vector<UNSIGNED8> srv(ds.begin(),ds.end());srv.push_back(0);ADSHANDLE c=0,t=0,ix=0;
 ok(AdsConnect60(srv.data(),ADS_LOCAL_SERVER,nullptr,nullptr,0,&c));
 UNSIGNED8 name[]="PRIVATE_TABLE_CANARY.DBF",def[]="FIELD_X,N,8,0",fld[]="FIELD_X",bag[]="PRIVATE_TABLE_CANARY.CDX",tag[]="TAGX";
 ok(AdsCreateTable(c,name,nullptr,ADS_CDX,0,0,0,0,def,&t));ok(AdsAppendRecord(t));ok(AdsSetDouble(t,fld,1234567));ok(AdsWriteRecord(t));
 ok(AdsCreateIndex61(t,bag,tag,fld,nullptr,nullptr,ADS_COMPOUND,512,&ix));ok(AdsCloseTable(t));ok(AdsDisconnect(c));
 openads::network::Server server;if(!server.start("127.0.0.1",0))return 3;
 auto uri="tcp://127.0.0.1:"+std::to_string(server.port())+"/"+ds;std::vector<UNSIGNED8> u(uri.begin(),uri.end());u.push_back(0);
 ok(AdsConnect60(u.data(),ADS_REMOTE_SERVER,nullptr,nullptr,0,&c));if(mode=="late")ok(OAdsSetLogging(1));ok(AdsOpenTable(c,name,nullptr,ADS_CDX,0,0,0,0,&t));
 ok(AdsGotoTop(t));double val=0;ok(AdsGetDouble(t,fld,&val));if(val!=1234567)return 4;
 ok(AdsGetIndexHandle(t,tag,&ix));ok(AdsGotoTop(ix));
 UNSIGNED16 found=0;ok(AdsSeek(ix,reinterpret_cast<UNSIGNED8*>(&val),sizeof(val),ADS_DOUBLEKEY,ADS_HARDSEEK,&found));if(!found)return 9;
 if(mode=="caller"){
  // Existing first field probes immediately; the repeated assignment batches.
  ok(AdsAppendRecord(t));ok(AdsSetDouble(t,fld,1234567));ok(AdsSetDouble(t,fld,1234567));mark("PENDING_BEFORE_COMMIT");ok(AdsWriteRecord(t));ok(AdsUnlockTable(t));
  ok(AdsAppendRecord(t));ok(AdsSetDouble(t,fld,1234567));ok(AdsSetDouble(t,fld,1234567));mark("PENDING_BEFORE_UNLOCK");ok(AdsUnlockTable(t));
  UNSIGNED32 count=0;ok(AdsGetRecordCount(t,0,&count));ok(AdsGotoRecord(t,1));
  ok(AdsGotoBottom(ix));
 }
 if(mode=="lifecycle"){
  ok(OAdsSetLogging(0));auto path=std::getenv("OPENADS_WIRE_TRACE_FILE");auto size=fs::exists(path)?fs::file_size(path):0;
  ok(AdsSkip(t,0));ok(AdsRefreshRecord(t));if((fs::exists(path)?fs::file_size(path):0)!=size)return 5;
  ok(OAdsSetLogging(1));ok(AdsSkip(t,0));if(!fs::exists(path)||fs::file_size(path)==size)return 6;
 }
 ok(AdsCloseTable(t));UNSIGNED8 alias[]="SAFE_ALIAS";ok(AdsOpenTable(c,name,alias,ADS_CDX,0,0,0,0,&t));ok(AdsGotoTop(t));ok(AdsCloseTable(t));
 openads::mgmt::ErrorLog::instance().log(5000,"ABI",99,"PRIVATE_ERROR_CANARY");
 ok(AdsDisconnect(c));
 if(mode=="caller"){
  // No ACE entry at this direct transport use: do not reuse the last API.
  openads::network::RemoteConnection direct;
  if(!direct.connect("127.0.0.1",server.port(),ds))return 10;
  direct.disconnect();
 }
 server.stop();fs::remove_all(dir);mark("APP_END");
 std::cout<<"initial_master="<<was<<" mode="<<mode<<" values_unchanged=1\n";
 return mode=="default" && was?7:0;
}
