// Harbour DBFCDX: GoCold, refresh edited row's index position, then skip.
// No automatic RUnlock and no implicit hard-commit on movement.
#include "doctest.h"
#include "network/server.h"
#include "openads/ace.h"
#include "openads/error.h"
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
namespace {
namespace fs = std::filesystem;
struct KeySkipFixture {
 fs::path dir = fs::temp_directory_path()/"openads_dirty_key_skip";
 openads::network::Server server;
 ADSHANDLE conn=0,table=0,index=0;
 KeySkipFixture(bool remote, bool unique=false, unsigned duplicates=0, bool extra_tag=false, bool conditional=false) {
  std::error_code ec;fs::remove_all(dir,ec);fs::create_directories(dir);
  auto path=dir.string();
  REQUIRE(AdsConnect60((UNSIGNED8*)path.c_str(),ADS_LOCAL_SERVER,nullptr,nullptr,0,&conn)==0);
  REQUIRE(AdsCreateTable(conn,(UNSIGNED8*)"K.DBF",nullptr,ADS_CDX,0,0,0,0,(UNSIGNED8*)"K,C,3;V,C,8",&table)==0);
  for(auto key:{"AAA","BBB","CCC","DDD"}) {
   REQUIRE(AdsAppendRecord(table)==0);
   REQUIRE(AdsSetString(table,(UNSIGNED8*)"K",(UNSIGNED8*)key,3)==0);
   REQUIRE(AdsWriteRecord(table)==0);
  }
  if(duplicates){
   for(unsigned i=0;i<duplicates+1;++i){const char* key=i==duplicates?"ZZZ":"BBB";REQUIRE(AdsAppendRecord(table)==0);REQUIRE(AdsSetString(table,(UNSIGNED8*)"K",(UNSIGNED8*)key,3)==0);REQUIRE(AdsWriteRecord(table)==0);}
  }
  REQUIRE(AdsCreateIndex61(table,(UNSIGNED8*)"K.CDX",(UNSIGNED8*)"BYK",(UNSIGNED8*)"K",conditional?(UNSIGNED8*)"K > 'AAA'":nullptr,nullptr,ADS_COMPOUND|(unique?ADS_UNIQUE:0),0,&index)==0);
  if(extra_tag){ADSHANDLE other=0;REQUIRE(AdsCreateIndex61(table,(UNSIGNED8*)"K.CDX",(UNSIGNED8*)"BYK2",(UNSIGNED8*)"K",nullptr,nullptr,ADS_COMPOUND,0,&other)==0);}
  REQUIRE(AdsCloseTable(table)==0);table=0;REQUIRE(AdsDisconnect(conn)==0);conn=0;
  if(remote){REQUIRE(server.start("127.0.0.1",0));path="tcp://127.0.0.1:"+std::to_string(server.port())+"/"+path;}
  REQUIRE(AdsConnect60((UNSIGNED8*)path.c_str(),remote?ADS_REMOTE_SERVER:ADS_LOCAL_SERVER,nullptr,nullptr,0,&conn)==0);
  REQUIRE(AdsOpenTable(conn,(UNSIGNED8*)"K.DBF",nullptr,ADS_CDX,0,0,0,ADS_SHARED,&table)==0);
  REQUIRE(AdsGetIndexHandleByOrder(table,1,&index)==0);
  REQUIRE(AdsSetIndexOrderByHandle(table,index)==0);
 }
 ~KeySkipFixture(){if(table)CHECK(AdsCloseTable(table)==0);if(conn)CHECK(AdsDisconnect(conn)==0);server.stop();std::error_code ec;fs::remove_all(dir,ec);}
 unsigned recno(){UNSIGNED32 n=0;REQUIRE(AdsGetRecordNum(table,0,&n)==0);return n;}
 void edit(unsigned row,const char* key){REQUIRE(AdsGotoRecord(table,row)==0);REQUIRE(AdsLockRecord(table,row)==0);REQUIRE(AdsSetString(table,(UNSIGNED8*)"K",(UNSIGNED8*)key,3)==0);}
 std::string disk_key(unsigned row){std::ifstream f(dir/"K.DBF",std::ios::binary);std::vector<unsigned char>b((std::istreambuf_iterator<char>(f)),{});auto h=b[8]+256*b[9],r=b[10]+256*b[11];return std::string((char*)b.data()+h+(row-1)*r+1,3);}
 void locked(unsigned row){UNSIGNED16 held=0;REQUIRE(AdsIsRecordLocked(table,row,&held)==0);CHECK(held==1);}
};
}
TEST_CASE("dirty key skip: new key anchors forward backward crossing duplicate and zero") {
 struct Scenario{unsigned row;const char* key;int step;unsigned expected;};
 for(bool remote:{false,true})for(bool committed:{false,true})for(auto s:{Scenario{1,"AAZ",1,2},Scenario{3,"CCB",-1,2},Scenario{1,"BBZ",1,3},Scenario{4,"BBA",-1,1},Scenario{1,"BBB",1,2},Scenario{3,"BBB",-1,2},Scenario{1,"AAZ",0,1}}){
  CAPTURE(remote);CAPTURE(committed);CAPTURE(s.row);CAPTURE(s.key);CAPTURE(s.step);
  KeySkipFixture f(remote);const unsigned other=s.row==4?2u:4u;REQUIRE(AdsLockRecord(f.table,other)==0);f.edit(s.row,s.key);
  if(committed)REQUIRE(AdsWriteRecord(f.table)==0);
  REQUIRE(AdsSkip(f.index,s.step)==0);CHECK(f.recno()==s.expected);
  CHECK(f.disk_key(s.row)==s.key);f.locked(s.row);f.locked(other);
 }
}
TEST_CASE("dirty key skip: index update failure returns error without movement or unlock") {
 for(bool remote:{false,true}){
  CAPTURE(remote);KeySkipFixture f(remote,true);f.edit(1,"BBB");
  const auto rc=AdsSkip(f.index,1);CHECK(rc==openads::AE_UNIQUE_INDEX_VIOLATION);CHECK(f.recno()==1);f.locked(1);
 }
}

#include "engine/table.h"
namespace {
// Fault injection at the owning dirty-record write, not a fabricated ACK.
class SkipWriteFailureDriver : public openads::drivers::IDriver {
 openads::platform::File file_;
 std::vector<openads::drivers::DbfField> fields_;
public:
 unsigned writes=0,flushes=0;
 bool fail_writes=true;
 SkipWriteFailureDriver(){openads::drivers::DbfField f;f.name="K";f.type=openads::drivers::DbfFieldType::Character;f.length=3;f.record_offset=1;fields_.push_back(f);}
 openads::util::Result<void> open(const std::string&,openads::drivers::DriverOpenMode)override{return {};}
 std::uint32_t record_count()const noexcept override{return 3;}
 std::uint16_t record_length()const noexcept override{return 4;}
 std::uint16_t header_length()const noexcept override{return 65;}
 const std::vector<openads::drivers::DbfField>& fields()const noexcept override{return fields_;}
 openads::platform::File& file()override{return file_;}
 openads::util::Result<std::vector<std::uint8_t>> read_record_raw(std::uint32_t)override{return std::vector<std::uint8_t>{' ','A','A','A'};}
 openads::util::Result<void> write_record_raw(std::uint32_t,const std::uint8_t*,std::size_t)override{++writes;if(!fail_writes)return {};return openads::util::Error{5010,0,"injected dirty write failure",""};}
 openads::util::Result<std::uint32_t> append_record_raw(const std::uint8_t*,std::size_t)override{return 4u;}
 openads::util::Result<void> flush()override{++flushes;return {};}
 openads::util::Result<void> zap()override{return {};}
};
}
TEST_CASE("dirty key skip: failed old record write stops zero forward backward before motion") {
 for(int step:{0,1,-1}) {
  CAPTURE(step);auto driver=std::make_unique<SkipWriteFailureDriver>();auto* fault=driver.get();
  auto table=openads::engine::Table::from_driver(std::move(driver),"skip-failure",openads::engine::TableType::Cdx,openads::engine::OpenMode::Exclusive);
  REQUIRE(table.goto_record(2));REQUIRE(table.set_field(0,std::string("NEW")));
  auto result=table.skip(step);REQUIRE_FALSE(result);CHECK(result.error().code==5010);
  CHECK(table.recno()==2);CHECK(table.record_dirty());CHECK(fault->writes==1);CHECK(fault->flushes==0);
 }
}

TEST_CASE("dirty key skip: duplicate run above 1024 anchors exact edited row") {
 for(bool remote:{false,true}){CAPTURE(remote);KeySkipFixture f(remote,false,1050);f.edit(1055,"BBB");REQUIRE(AdsSkip(f.index,1)==0);CHECK(f.recno()==3);CHECK(f.disk_key(1055)=="BBB");f.locked(1055);}
}
TEST_CASE("dirty key skip: local descending traversal retains own lock") {
 for(bool remote:{false}){CAPTURE(remote);KeySkipFixture f(remote);REQUIRE(AdsSetIndexDirection(f.index,1)==0);f.edit(3,"CCB");REQUIRE(AdsSkip(f.index,1)==0);CHECK(f.recno()==2);f.locked(3);}
}

TEST_CASE("dirty key skip: scope fence remains active after key refresh") {
 for(bool remote:{false,true}){CAPTURE(remote);KeySkipFixture f(remote);REQUIRE(AdsSetScope(f.index,ADS_TOP,(UNSIGNED8*)"AAA",3,ADS_STRINGKEY)==0);REQUIRE(AdsSetScope(f.index,ADS_BOTTOM,(UNSIGNED8*)"BBB",3,ADS_STRINGKEY)==0);f.edit(1,"AAZ");REQUIRE(AdsSkip(f.index,1)==0);CHECK(f.recno()==2);REQUIRE(AdsSkip(f.index,1)==0);UNSIGNED16 eof=0;REQUIRE(AdsAtEOF(f.table,&eof)==0);CHECK(eof==1);f.locked(1);}
}

#include "network/client.h"
#include "util/log.h"
#include <atomic>
namespace {
std::atomic<unsigned> nav_flushes{0},nav_unlocks{0};
void key_skip_trace(const void*,std::uint8_t op,std::uint32_t,std::size_t,std::uint8_t,std::size_t,long long){if(op==0x5A)++nav_flushes;if(op==0x74||op==0x78)++nav_unlocks;}
struct KeyTraceScope {
 bool old=openads::util::logging_enabled();
 KeyTraceScope(){nav_flushes=0;nav_unlocks=0;openads::util::set_logging_enabled(true);openads::network::set_frame_trace_hook(key_skip_trace);}
 ~KeyTraceScope(){openads::network::set_frame_trace_hook(nullptr);openads::util::set_logging_enabled(old);}
};
}
TEST_CASE("dirty key skip: GoCold keeps explicit durability and unlock boundaries") {
 KeySkipFixture f(true);f.edit(1,"AAZ");KeyTraceScope trace;
 REQUIRE(AdsSkip(f.index,1)==0);CHECK(f.recno()==2);CHECK(nav_flushes==0);CHECK(nav_unlocks==0);f.locked(1);
 REQUIRE(AdsWriteRecord(f.table)==0);CHECK(nav_flushes==1);CHECK(nav_unlocks==0);
 REQUIRE(AdsWriteRecord(f.table)==0);CHECK(nav_flushes==1);
 REQUIRE(AdsUnlockTable(f.table)==0);CHECK(nav_unlocks==1);
 REQUIRE(AdsUnlockTable(f.table)==0);CHECK(nav_unlocks==1);
}

TEST_CASE("dirty key skip: pending refresh survives tag switch and other-tag navigation") {
 for(bool remote:{false,true}){CAPTURE(remote);KeySkipFixture f(remote,false,0,true);f.edit(1,"AAZ");REQUIRE(AdsWriteRecord(f.table)==0);ADSHANDLE other=0;REQUIRE(AdsGetIndexHandleByOrder(f.table,2,&other)==0);REQUIRE(AdsSetIndexOrderByHandle(f.table,other)==0);REQUIRE(AdsGotoBottom(other)==0);REQUIRE(AdsGotoRecord(f.table,1)==0);REQUIRE(AdsSetIndexOrderByHandle(f.table,f.index)==0);REQUIRE(AdsSkip(f.index,1)==0);CHECK(f.recno()==2);f.locked(1);}
}
#include "drivers/cdx/cdx_index.h"
TEST_CASE("dirty key skip: reanchor state belongs to index lifetime not replacement tag") {
 auto old=std::make_unique<openads::drivers::cdx::CdxIndex>();old->require_record_reanchor();CHECK(old->record_reanchor_pending());auto parked=std::move(old);CHECK(parked->record_reanchor_pending());openads::drivers::cdx::CdxIndex replacement;CHECK_FALSE(replacement.record_reanchor_pending());parked->require_record_reanchor(false);CHECK_FALSE(parked->record_reanchor_pending());
}

namespace {
class ReanchorFaultIndex : public openads::drivers::IIndex {
public:
 int mode=0;unsigned invalidations=0;std::string for_expr;
 openads::util::Result<void> open(const std::string&,openads::drivers::IndexOpenMode)override{return {};}
 std::string name()const override{return "fault";}std::string expression()const override{return "K";}std::string condition()const override{return for_expr;}
 bool descending()const override{return false;}bool unique()const override{return false;}std::uint16_t key_length()const override{return 3;}
 openads::util::Result<openads::drivers::SeekOutcome> seek_first()override{return openads::drivers::SeekOutcome{openads::drivers::SeekHit::Exact,1,true};}
 openads::util::Result<openads::drivers::SeekOutcome> seek_last()override{return openads::drivers::SeekOutcome{openads::drivers::SeekHit::Exact,3,true};}
 openads::util::Result<openads::drivers::SeekOutcome> seek_key(const std::string&,bool)override{if(mode==0)return openads::util::Error{5011,0,"injected anchor seek failure",""};return openads::drivers::SeekOutcome{openads::drivers::SeekHit::Exact,1,true};}
 openads::util::Result<openads::drivers::SeekOutcome> next()override{if(mode==1)return openads::util::Error{5012,0,"injected duplicate walk failure",""};return openads::drivers::SeekOutcome{};}
 openads::util::Result<openads::drivers::SeekOutcome> prev()override{return openads::drivers::SeekOutcome{};}
 void invalidate_cursor()override{++invalidations;}std::string current_key()const override{return "AAA";}
 openads::util::Result<void> insert(std::uint32_t,const std::string&)override{return {};}
 openads::util::Result<void> erase(std::uint32_t,const std::string&)override{return {};}
 openads::util::Result<void> flush()override{return {};}
};
}
TEST_CASE("dirty key skip: failed reanchor leaves obligation and invalidates cursor") {
 for(int mode:{0,1,2}){CAPTURE(mode);auto driver=std::make_unique<SkipWriteFailureDriver>();auto table=openads::engine::Table::from_driver(std::move(driver),"anchor-fault",openads::engine::TableType::Cdx,openads::engine::OpenMode::Exclusive);REQUIRE(table.goto_record(2));auto index=std::make_unique<ReanchorFaultIndex>();auto* fault=index.get();fault->mode=mode;fault->require_record_reanchor();table.set_order(std::move(index));auto result=table.skip(1);REQUIRE_FALSE(result);CHECK(result.error().code==(mode==0?5011:mode==1?5012:5044));CHECK(table.recno()==2);CHECK(fault->record_reanchor_pending());CHECK(fault->invalidations==1);}
}
TEST_CASE("dirty key skip: conditional membership discards old obligation and marks reentry") {
 auto driver=std::make_unique<SkipWriteFailureDriver>();driver->fail_writes=false;auto table=openads::engine::Table::from_driver(std::move(driver),"membership",openads::engine::TableType::Cdx,openads::engine::OpenMode::Exclusive);REQUIRE(table.goto_record(1));auto index=std::make_unique<ReanchorFaultIndex>();auto* view=index.get();view->for_expr="K = 'AAA'";view->require_record_reanchor();table.set_order(std::move(index));REQUIRE(table.set_field(0,std::string("BBB")));REQUIRE(table.commit_dirty_record());CHECK_FALSE(view->record_reanchor_pending());REQUIRE(table.set_field(0,std::string("AAA")));REQUIRE(table.commit_dirty_record());CHECK(view->record_reanchor_pending());
}

TEST_CASE("dirty key skip: physical goto outside conditional tag does not reuse edited row obligation") {
 for(bool remote:{false,true}){CAPTURE(remote);KeySkipFixture f(remote,false,0,false,true);f.edit(2,"BBZ");REQUIRE(AdsWriteRecord(f.table)==0);REQUIRE(AdsGotoRecord(f.table,1)==0);REQUIRE(AdsSkip(f.index,1)==0);CHECK(f.recno()==2);f.locked(2);}
}
