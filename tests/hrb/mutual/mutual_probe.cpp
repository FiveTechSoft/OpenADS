#include "network/server.h"
#include "network/client.h"
#include "network/tls_transport.h"
#include "network/mutual_auth.h"
#include "openads/ace.h"
#include "openads_version.h"
#include <mbedtls/md.h>
#include <cassert>
#include <algorithm>
#include <filesystem>
#include <thread>
#include <iostream>
using namespace openads::network;
const std::string key="OpenADS-public-mock-v2-NOT-A-LICENSE-KEY";
std::string mac(const std::string& direction,const std::string& transcript,const std::string& k=key){
 std::string data=direction+std::string(1,'\0')+transcript;unsigned char out[32];
 assert(mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),reinterpret_cast<const unsigned char*>(k.data()),k.size(),reinterpret_cast<const unsigned char*>(data.data()),data.size(),out)==0);
 return (direction=="server"?"SERVER:":"CLIENT:")+std::string(reinterpret_cast<char*>(out),32);
}
int main(int argc,char**argv){
 assert(argc==3);auto dir=std::filesystem::current_path()/"mutual-v2-proof";std::filesystem::create_directories(dir);
 auto cfg=make_auth_tls_config();assert(cfg);auto worker=std::make_shared<AuthWorker>(argv[1],argv[2]);assert(worker->probe()&&worker->mutual_required());
 std::unique_ptr<Server> srv;auto fresh=[&]{if(srv)srv->stop();srv=std::make_unique<Server>();srv->set_data_dir(dir.string());srv->add_credential("usr","pw");srv->set_auth_worker(worker);assert(srv->set_tls(cfg.value()));assert(srv->start("127.0.0.1",0));};fresh();
 auto tls=cfg.value();tls.insecure_skip_verify=true;tls.auth_tls13_only=true;
 auto connect=[&]{auto t=connect_tls("127.0.0.1",srv->port(),tls);assert(t);return std::move(t).value();};
 const std::string path=dir.string(),firm="fixture-firm";
 std::array<std::uint8_t,32> c{},b{},lastb{};std::string prior;
 for(int mode=0;mode<8;++mode){
  fresh();std::cerr<<"mode="<<mode<<"\n";RemoteConnection client;auto proof=client.begin_mutual(connect(),path,firm,c,b);if(!proof)std::cerr<<proof.error().message<<" context="<<proof.error().context<<"\n";assert(proof);auto transcript=mutual_transcript(c,b,firm,path);assert(proof.value()==mac("server",transcript));
  if(mode) { assert(b!=lastb); }
  lastb=b;
  std::string response=mac("client",transcript);
  if(mode==1)response=proof.value(); // reflection
  if(mode==2)response=prior; // replay from another TLS session
  if(mode==3)response=mac("client",transcript,"wrong-key");
  if(mode==4)response[10]^=1;
  if(mode==5)response=std::string(reinterpret_cast<char*>(c.data()),32); // echo is NOT auth
  if(mode==6)response.resize(4097,'x');
  if(mode==7)response=mac("client",mutual_transcript(c,b,"other-firm",path));
  auto done=client.complete_auth(response);assert(bool(done)==(mode==0));assert(!client.complete_auth(response));
  if(mode==0){assert(client.server_version().find("openads/")==0);prior=response;}
 }
 fresh();
 // Exercise the exported C ABI, not just the C++ wrapper.
 {fresh();auto url="auth://127.0.0.1:"+std::to_string(srv->port())+"/"+path;
 ADSHANDLE pending=0,connection=0;unsigned char nonce[32],binding[32],proof[4096];UNSIGNED32 n=sizeof(proof);
 assert(OAdsBeginMutualAuth(url.c_str(),firm.c_str(),&pending,nonce,binding,proof,&n)==0&&pending&&n>0);
 std::copy_n(nonce,32,c.begin());std::copy_n(binding,32,b.begin());auto transcript=mutual_transcript(c,b,firm,path);
 assert(std::string(reinterpret_cast<char*>(proof),n)==mac("server",transcript));auto response=mac("client",transcript);
 assert(OAdsCompleteAuth(pending,reinterpret_cast<const unsigned char*>(response.data()),static_cast<UNSIGNED32>(response.size()),&connection)==0&&connection);
 UNSIGNED8 desc[256]{};UNSIGNED16 len=sizeof(desc);
 assert(AdsGetServerVersion(connection,desc,&len)==0&&std::string(reinterpret_cast<char*>(desc)).find(OPENADS_VERSION_STR)==0);
 assert(AdsDisconnect(connection)==0);
 assert(OAdsCompleteAuth(pending,reinterpret_cast<const unsigned char*>(response.data()),static_cast<UNSIGNED32>(response.size()),&connection)!=0&&connection==0);
 }
 fresh();
 // Legacy marker stripping must not downgrade a V2 worker.
 {RemoteConnection client;assert(!client.begin_auth(connect(),path));}
 // Missing local TLS binding cannot be bypassed with plain transport.
 {auto s=connect_tcp("127.0.0.1",srv->port());assert(s);RemoteConnection client;assert(!client.begin_mutual(make_plain_transport(s.value()),path,firm,c,b));}
 // Live TLS-terminating relay. Distinct endpoints have distinct bindings.
 auto listen=listen_tcp({"127.0.0.1",0,16});assert(listen);auto listener=listen.value();auto port=socket_local_port(listener);assert(port);
 std::thread relay([&]{auto a=accept_one(listener);assert(a);auto local=accept_tls(a.value(),cfg.value());assert(local);auto upstream=connect();auto begin=read_frame(*local.value());assert(begin);assert(write_frame(*upstream,begin.value()));auto proof=read_frame(*upstream);assert(proof);assert(write_frame(*local.value(),proof.value()));auto finish=read_frame(*local.value());assert(finish);assert(write_frame(*upstream,finish.value()));auto denied=read_frame(*upstream);assert(denied);assert(denied.value().opcode==Opcode::Error);assert(write_frame(*local.value(),denied.value()));});
 {auto t=connect_tls("127.0.0.1",port.value(),tls);assert(t);RemoteConnection client;auto proof=client.begin_mutual(std::move(t).value(),path,firm,c,b);if(!proof)std::cerr<<proof.error().message<<" context="<<proof.error().context<<"\n";assert(proof);auto transcript=mutual_transcript(c,b,firm,path);assert(proof.value()!=mac("server",transcript));
  // Normal Vouch MUST stop here. Even deliberately ignoring that failure,
  // its proof on its own channel is rejected by the real server.
  assert(!client.complete_auth(mac("client",transcript)));
 }
 relay.join();sock_close(listener);
 // Bounds/framing and expiry are deterministic, not wall-clock sleeps.
 {std::string field;std::size_t p=0;assert(!mutual_read(std::string("\xff\xff\xff\x7f",4),p,field,4096));AuthAttempt attempt;auto now=AuthAttempt::Clock::now();assert(attempt.begin(c,true,now,std::chrono::seconds(30)));AuthAttempt::Challenge got{};assert(!attempt.take(now+std::chrono::seconds(31),39,got));}
 srv->stop();std::filesystem::remove_all(dir);std::cout<<"mutual v2 proof, reflection, replay, wrong key, tamper, nonce echo, oversized, selector, downgrade, plain, real TLS relay, expiry PASS\n";
}
