// Pritpal Bedi: GoBottom's internal count dependency, not explicit counts.
#include "doctest.h"
#include "network/client.h"
#include "network/server.h"
#include "network/transport.h"
#include "openads/ace.h"
#include "util/log.h"
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>
namespace {
using namespace openads::network;
namespace fs=std::filesystem;
std::atomic<unsigned> bottoms{0}, counts{0}, records{0}, flushes{0}, fields{0};
void bottom_trace(const void*, std::uint8_t op,std::uint32_t,std::size_t,std::uint8_t,std::size_t,long long){
 if(op==0x64)++bottoms;if(op==0xB0)++counts;if(op==0x46)++records;if(op==0x5A)++flushes;if(op==0x5E||op==0x52)++fields;
}
struct BottomEnv {
 BottomEnv(bool legacy){
#ifdef _WIN32
 _putenv_s("OPENADS_NO_BOTTOM_KEYCOUNT_CAP",legacy?"1":"");
#else
 if(legacy)setenv("OPENADS_NO_BOTTOM_KEYCOUNT_CAP","1",1);else unsetenv("OPENADS_NO_BOTTOM_KEYCOUNT_CAP");
#endif
 }
 ~BottomEnv(){
#ifdef _WIN32
 _putenv_s("OPENADS_NO_BOTTOM_KEYCOUNT_CAP","");
#else
 unsetenv("OPENADS_NO_BOTTOM_KEYCOUNT_CAP");
#endif
 }
};
struct BottomFixture {
 fs::path dir=fs::temp_directory_path()/"openads_bottom_count";
 Server srv;ADSHANDLE conn=0,table=0,index=0;
 bool oldlog=openads::util::logging_enabled();
 BottomFixture(){
  std::error_code ec;fs::remove_all(dir,ec);fs::create_directories(dir);
  ADSHANDLE c=0,t=0,ix=0;auto path=dir.string();
  REQUIRE(AdsConnect60((UNSIGNED8*)path.c_str(),ADS_LOCAL_SERVER,nullptr,nullptr,0,&c)==0);
  REQUIRE(AdsCreateTable(c,(UNSIGNED8*)"BC.DBF",nullptr,ADS_CDX,0,0,0,0,(UNSIGNED8*)"K,C,1;V,C,8",&t)==0);
  for(char ch='A';ch<='E';++ch){REQUIRE(AdsAppendRecord(t)==0);REQUIRE(AdsSetString(t,(UNSIGNED8*)"K",(UNSIGNED8*)&ch,1)==0);REQUIRE(AdsWriteRecord(t)==0);}
  REQUIRE(AdsGotoRecord(t,3)==0);REQUIRE(AdsDeleteRecord(t)==0);REQUIRE(AdsWriteRecord(t)==0);
  REQUIRE(AdsCreateIndex61(t,(UNSIGNED8*)"BC.CDX",(UNSIGNED8*)"BYK",(UNSIGNED8*)"K",nullptr,nullptr,ADS_COMPOUND,0,&ix)==0);
  REQUIRE(AdsCloseTable(t)==0);REQUIRE(AdsDisconnect(c)==0);
  REQUIRE(srv.start("127.0.0.1",0).has_value());
  auto uri="tcp://127.0.0.1:"+std::to_string(srv.port())+"/"+path;
  REQUIRE(AdsConnect60((UNSIGNED8*)uri.c_str(),ADS_REMOTE_SERVER,nullptr,nullptr,0,&conn)==0);
  REQUIRE(AdsOpenTable(conn,(UNSIGNED8*)"BC.DBF",nullptr,ADS_CDX,0,0,0,ADS_SHARED,&table)==0);
  REQUIRE(AdsGetIndexHandleByOrder(table,1,&index)==0);
  REQUIRE(AdsSetIndexOrderByHandle(table,index)==0);
  UNSIGNED32 k=0;REQUIRE(AdsGetKeyCount(index,0,&k)==0); // settle deferred focus, not an internal cache
  openads::util::set_logging_enabled(true);set_frame_trace_hook(bottom_trace);reset();
 }
 void reset(){bottoms=0;counts=0;records=0;flushes=0;fields=0;}
 void scope(const char* lo,const char* hi){REQUIRE(AdsSetScope(index,ADS_TOP,(UNSIGNED8*)lo,1,ADS_STRINGKEY)==0);REQUIRE(AdsSetScope(index,ADS_BOTTOM,(UNSIGNED8*)hi,1,ADS_STRINGKEY)==0);}
 unsigned keyno(){UNSIGNED32 k=0;REQUIRE(AdsGetKeyNum(index,0,&k)==0);return k;}
 std::string disk(){std::ifstream f(dir/"BC.DBF",std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
 ~BottomFixture(){set_frame_trace_hook(nullptr);openads::util::set_logging_enabled(oldlog);if(conn)AdsDisconnect(conn);srv.stop();AdsShowDeleted(1);std::error_code ec;fs::remove_all(dir,ec);}
};
}
TEST_CASE("bottom count: scoped landing single-use metadata; explicit count baseline differences retained"){
 for(bool legacy:{false,true}){CAPTURE(legacy);BottomEnv env(legacy);BottomFixture f;f.scope("B","D");REQUIRE(AdsShowDeleted(0)==0);f.reset();
  const auto disk=f.disk();REQUIRE(AdsGotoBottom(f.index)==0);CHECK(bottoms==1);CHECK(counts==(legacy?1u:0u));CHECK(flushes==0);CHECK(fields==0);CHECK(f.disk()==disk);CHECK(f.keyno()==2);
  UNSIGNED32 rn=0;REQUIRE(AdsGetRecordNum(f.table,0,&rn)==0);CHECK(rn==4);
  f.reset();UNSIGNED32 kc=0;REQUIRE(AdsGetKeyCount(f.index,0,&kc)==0);CHECK(kc==2);CHECK(counts==(legacy?0u:1u)); // full bottom+same-index count is two frames either way
  UNSIGNED32 rc=0;REQUIRE(AdsGetRecordCount(f.table,0,&rc)==0);CHECK(rc==5); // baseline may serve a previously certified physical count
  REQUIRE(AdsShowDeleted(1)==0);f.reset();REQUIRE(AdsGotoBottom(f.index)==0);CHECK(f.keyno()==3);CHECK(counts==(legacy?1u:0u));
  f.scope("A","B");f.reset();REQUIRE(AdsGotoBottom(f.index)==0);CHECK(f.keyno()==2);CHECK(counts==(legacy?1u:0u));
  f.scope("X","Z");f.reset();REQUIRE(AdsGotoBottom(f.index)==0);CHECK(counts==(legacy?1u:0u));UNSIGNED16 eof=0;REQUIRE(AdsAtEOF(f.table,&eof)==0);CHECK(eof==1);
 }
}
TEST_CASE("bottom count: dirty GoCold applies fields but adds no explicit commit or unlock"){
 for(bool legacy:{false,true}){BottomEnv env(legacy);BottomFixture f;REQUIRE(AdsLockRecord(f.table,1)==0);REQUIRE(AdsGotoRecord(f.table,1)==0);
 REQUIRE(AdsSetString(f.table,(UNSIGNED8*)"V",(UNSIGNED8*)"one",3)==0);REQUIRE(AdsSetString(f.table,(UNSIGNED8*)"V",(UNSIGNED8*)"two",3)==0);f.reset();
 REQUIRE(AdsGotoBottom(f.index)==0);CHECK(flushes==0);CHECK(bottoms==1);CHECK(counts==(legacy?1u:0u));REQUIRE(AdsUnlockRecord(f.table,0)==0);
 }
}
namespace {
void write_u32_le(std::uint32_t x,std::vector<std::uint8_t>& out){for(unsigned i=0;i<4;++i)out.push_back(static_cast<std::uint8_t>(x>>(i*8)));}
class BottomReplyTransport : public ITransport {
 std::vector<std::uint8_t> incoming,outgoing;bool live=true;
public:
 int mode;unsigned nav=0,b0=0;explicit BottomReplyTransport(int m):mode(m){}
 openads::util::Result<std::size_t> send(const std::uint8_t* data,std::size_t n) override {
 incoming.insert(incoming.end(),data,data+n);std::size_t used=0;auto f=decode_frame(incoming.data(),incoming.size(),&used);if(!f)return n;Frame reply;
 if(f.value().opcode==Opcode::Hello)reply.opcode=Opcode::HelloAck;
 else if(f.value().opcode==Opcode::Connect){reply.opcode=Opcode::ConnectAck;std::string echo="connected:fixture";reply.payload.assign(echo.begin(),echo.end());write_u32_le(kCapBottomKeyCount,reply.payload);}
 else if(f.value().opcode==Opcode::GotoBottom){++nav;reply.opcode=Opcode::GotoBottomAck;
  CHECK(f.value().payload.size()==5);CHECK((f.value().payload[4]&4)!=0);
  if(mode!=1){write_u32_le(mode==2?0u:kBottomKeyCountMagic,reply.payload);write_u32_le(mode==4?5035u:0u,reply.payload);write_u32_le(7,reply.payload);write_u32_le(mode==3?99u:1u,reply.payload);}
  if(mode==5){reply.opcode=Opcode::Error;reply.payload.clear();write_u32_le(5035,reply.payload);}
 }
 else if(f.value().opcode==Opcode::GetKeyCount){++b0;reply.opcode=Opcode::GetKeyCountAck;write_u32_le(8,reply.payload);}
 else reply.opcode=Opcode::Disconnect;
 auto bytes=encode_frame(reply);if(bytes)outgoing=bytes.value();incoming.clear();return n;
 }
 openads::util::Result<std::size_t> recv(std::uint8_t* data,std::size_t n) override {auto count=std::min(n,outgoing.size());std::copy_n(outgoing.begin(),count,data);outgoing.erase(outgoing.begin(),outgoing.begin()+static_cast<std::ptrdiff_t>(count));return count;}
 void close() noexcept override{live=false;}bool valid() const noexcept override{return live;}
};
}
TEST_CASE("bottom count: optional status never certifies failures; malformed metadata fails without replay"){
 for(int mode:{0,1,2,3,4,5}){CAPTURE(mode);RemoteConnection conn;auto transport=std::make_unique<BottomReplyTransport>(mode);auto* wire=transport.get();REQUIRE(conn.connect_with_transport(std::move(transport),"fixture").has_value());
 RemoteTable rt;rt.id=1;rt.conn=&conn;rt.active_index_id=2;auto result=conn.goto_bottom(&rt);
 CHECK(result.has_value()==(mode==0||mode==4));CHECK(wire->nav==1);CHECK(wire->b0==0);CHECK(rt.bottom_count_valid==(mode==0));CHECK_FALSE(rt.key_count_cached);CHECK(rt.key_counts.empty());
 if(mode==0)CHECK(rt.bottom_count==7);if(mode==5)CHECK(result.error().code==5035);
 if(mode==4){auto fresh=conn.key_count(1);REQUIRE(fresh.has_value());CHECK(fresh.value()==8);CHECK(wire->b0==1);}
 }
}
TEST_CASE("bottom count: ordinary natural-order landing does not request scoped count"){
 BottomEnv env(false);BottomFixture f;REQUIRE(AdsSetIndexOrderByHandle(f.table,0)==0);f.reset();REQUIRE(AdsGotoBottom(f.table)==0);CHECK(bottoms==1);CHECK(counts==0);UNSIGNED32 rn=0;REQUIRE(AdsGetRecordNum(f.table,0,&rn)==0);CHECK(rn==5);
}
