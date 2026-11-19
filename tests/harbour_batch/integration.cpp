#include "hbapi.h"
#include "rddads.h"
#include "abi/runtime.h"
#include "network/client.h"
#include "network/server.h"
#include "util/log.h"
#include "openads/ace.h"
#include <filesystem>
#include <algorithm>
#include "network/transport.h"
#include <memory>
#include <string>
static std::unique_ptr<openads::network::Server> server;
static ADSHANDLE connection=0;
HB_FUNC(TESTSTART) {
 auto dir=std::filesystem::temp_directory_path()/"rdd_batch_error_integration";
 std::error_code ec; std::filesystem::remove_all(dir,ec);std::filesystem::create_directories(dir);
 ADSHANDLE local=0, table=0; auto path=dir.string();
 if(AdsConnect60((UNSIGNED8*)path.c_str(),ADS_LOCAL_SERVER,nullptr,nullptr,0,&local) ||
 AdsCreateTable(local,(UNSIGNED8*)"CF.DBF",nullptr,ADS_CDX,0,0,0,0,(UNSIGNED8*)"NM,C,20,0",&table)) {hb_retnint(0);return;}
 AdsCloseTable(table);AdsDisconnect(local);
 server=std::make_unique<openads::network::Server>();
 if(!server->start("127.0.0.1",0)){hb_retnint(0);return;}
 auto uri="tcp://127.0.0.1:"+std::to_string(server->port())+"/"+path;
 if(AdsConnect60((UNSIGNED8*)uri.c_str(),ADS_REMOTE_SERVER,nullptr,nullptr,0,&connection)){hb_retnint(0);return;}
 hb_retnint(connection);
}
HB_FUNC(TESTFAULT) {
 auto* area=(ADSAREAP)hb_rddGetCurrentWorkAreaPointer();
 ADSHANDLE table=area ? area->hTable : 0; auto rc=table ? 0 : 1;
 auto* rt=openads::abi::detail::state().registry.lookup<openads::network::RemoteTable>(table,openads::session::HandleKind::RemoteTable);
 if(rc||!rt){hb_retl(false);return;}
 rt->pending_sets.emplace_back("BAD_FIELD","bad"); hb_retl(true);
}
HB_FUNC(TESTSTOP) {if(connection)AdsDisconnect(connection);connection=0;if(server)server->stop();server.reset();}
static openads::network::RemoteTable* armed_table = nullptr;
static unsigned armed_opcode = 0;
static void inject_after_move(const void*, std::uint8_t op, std::uint32_t,
                             std::size_t, std::uint8_t, std::size_t, long long) {
 if(armed_table && op==armed_opcode) {
  armed_table->pending_sets.emplace_back("BAD_FIELD","refresh-tail-fault");
  armed_table=nullptr; openads::network::set_frame_trace_hook(nullptr);
 }
}
HB_FUNC(TESTARMREFRESH) {
 auto* area=(ADSAREAP)hb_rddGetCurrentWorkAreaPointer();
 armed_table=area ? openads::abi::detail::state().registry.lookup<openads::network::RemoteTable>(area->hTable,openads::session::HandleKind::RemoteTable) : nullptr;
 armed_opcode=hb_parni(1);openads::util::set_logging_enabled(true);
 openads::network::set_frame_trace_hook(&inject_after_move);hb_retl(armed_table!=nullptr);
}
class Error5025Transport : public openads::network::ITransport {
 std::vector<std::uint8_t> incoming,outgoing; bool live=true;
public:
 unsigned sets=0,gotos=0;
 openads::util::Result<std::size_t> send(const std::uint8_t* data,std::size_t n) override {
  using namespace openads::network;
  incoming.insert(incoming.end(),data,data+n);std::size_t used=0;
  auto f=decode_frame(incoming.data(),incoming.size(),&used);if(!f)return n;
  Frame reply;
  switch(f.value().opcode){
   case Opcode::Hello:reply.opcode=Opcode::HelloAck;break;
   case Opcode::Connect:{reply.opcode=Opcode::ConnectAck;std::string text="connected:test";reply.payload.assign(text.begin(),text.end());
    auto caps=kCapSetFieldsBatch;for(int i=0;i<4;++i)reply.payload.push_back((std::uint8_t)(caps>>(i*8)));break;}
   case Opcode::SetFields:++sets;reply.opcode=Opcode::Error;reply.payload={0xA1,0x13,0,0};break;
   case Opcode::GotoRecord:++gotos;reply.opcode=Opcode::Error;reply.payload={0xA1,0x13,0,0};break;
   default:reply.opcode=Opcode::Disconnect;break;
  }
  auto bytes=encode_frame(reply);if(bytes)outgoing=bytes.value();incoming.clear();return n;
 }
 openads::util::Result<std::size_t> recv(std::uint8_t* p,std::size_t n) override {
  auto count=std::min(n,outgoing.size());std::copy_n(outgoing.begin(),count,p);outgoing.erase(outgoing.begin(),outgoing.begin()+count);return count;
 }
 void close() noexcept override{live=false;} bool valid() const noexcept override{return live;}
};
static std::unique_ptr<openads::network::RemoteConnection> fault_connection;
static Error5025Transport* fault_transport=nullptr;
static openads::network::RemoteTable* fault_table=nullptr;
static openads::network::RemoteConnection* original_connection=nullptr;
HB_FUNC(TESTARM5025) {
 auto* area=(ADSAREAP)hb_rddGetCurrentWorkAreaPointer();
 fault_table=openads::abi::detail::state().registry.lookup<openads::network::RemoteTable>(area->hTable,openads::session::HandleKind::RemoteTable);
 fault_connection=std::make_unique<openads::network::RemoteConnection>();
 auto transport=std::make_unique<Error5025Transport>();fault_transport=transport.get();
 bool ok=(bool)fault_connection->connect_with_transport(std::move(transport),"test");
 original_connection=fault_table->conn;fault_table->conn=fault_connection.get();hb_retl(ok);
}
HB_FUNC(TESTRESTORE5025) {
 bool ok=fault_transport->sets==1&&fault_transport->gotos==0;
 fault_table->conn=original_connection;fault_connection.reset();fault_table=nullptr;hb_retl(ok);
}
static unsigned goto_compat_calls=0;static bool goto_compat=false;
extern "C" UNSIGNED32 AdsGotoRecord(ADSHANDLE,UNSIGNED32);
extern "C" UNSIGNED32 test_AdsGotoRecord(ADSHANDLE h,UNSIGNED32 n) {
 if(goto_compat) {++goto_compat_calls;if(goto_compat_calls==1)return AE_INVALID_RECORD_NUMBER;}
 return AdsGotoRecord(h,n);
}
HB_FUNC(TESTARMGOTOCOMPAT){goto_compat_calls=0;goto_compat=true;}
HB_FUNC(TESTCHECKGOTOCOMPAT){goto_compat=false;hb_retl(goto_compat_calls==2);}
HB_FUNC(TESTSQLATTACH) {
 auto* area=(ADSAREAP)hb_rddGetCurrentWorkAreaPointer();ADSHANDLE stmt=0;
 auto rc=AdsCreateSQLStatement(connection,&stmt);
 if(!rc)rc=AdsStmtSetTableType(stmt,ADS_CDX);
 if(!rc)area->hStatement=stmt;
 hb_retnint(rc);
}
HB_FUNC(TESTSQLRETAINED) {
 auto* area=(ADSAREAP)hb_rddGetCurrentWorkAreaPointer();hb_retl(area&&area->hTable&&area->hStatement);
}
extern "C" UNSIGNED32 test_AdsExecuteSQLDirect(ADSHANDLE,UNSIGNED8*,ADSHANDLE* out){*out=0;return 0;}
extern "C" UNSIGNED32 test_AdsExecuteSQL(ADSHANDLE,ADSHANDLE* out){*out=0;return 0;}
static unsigned append_fault_code=0,append_calls=0;
extern "C" UNSIGNED32 AdsAppendRecord(ADSHANDLE);
extern "C" UNSIGNED32 test_AdsAppendRecord(ADSHANDLE h) {
 ++append_calls;
 if(append_fault_code)return append_fault_code;
 return AdsAppendRecord(h);
}
HB_FUNC(TESTAPPENDFAULT){append_fault_code=hb_parni(1);append_calls=0;}
HB_FUNC(TESTAPPENDCALLS){hb_retni(append_calls);append_fault_code=0;}
