// No connection/server is created: local diagnostic only.
#include "openads/ace.h"
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <atomic>
#include <stdexcept>
static UNSIGNED32 mark(const std::string& s) {
    return OAdsTraceMark(reinterpret_cast<const UNSIGNED8*>(s.data()), static_cast<UNSIGNED32>(s.size()));
}
static std::pair<UNSIGNED32,std::string> last_error() {
    UNSIGNED32 code=0; UNSIGNED8 message[2048]={}; UNSIGNED16 bytes=sizeof(message);
    AdsGetLastError(&code,message,&bytes);
    return {code,reinterpret_cast<char*>(message)};
}
int main(int argc,char**argv) {
    const std::string mode=argc>1?argv[1]:"default";
    if(mode!="default") OAdsSetLogging(mode=="master-off"?0:1);
    if(mode=="scope-only") {
        const UNSIGNED64 instance=argc>2 && std::string(argv[2])=="2" ? 2 : 1;
        if(OAdsTraceMarkEx((const UNSIGNED8*)"SAVE_BEGIN",10,instance,7)!=0 ||
           OAdsTraceMarkEx((const UNSIGNED8*)"SAVE_END",8,instance,7)!=0) return 13;
        return 0;
    }
    // Store a real database API error, then ensure diagnostics never replace it.
    UNSIGNED8 field[]="X";
    if(AdsSetDouble(0,field,9)==0) return 1;
    const auto before=last_error();
    const bool enabled=mode!="default" && mode!="master-off" && mode!="wire-off" && mode!="unwritable";
    if((mark("SAVE_BEGIN")==0)!=enabled) return 2;
    const std::vector<std::string> invalid={"",std::string(121,'X'),std::string("NUL\0X",5),"CR\rX","LF\nX","TAB\tX",std::string(1,'\x7f'),std::string(1,'\x01'),std::string(1,'\x85')};
    for(const auto& s:invalid) if(mark(s)==0) return 3;
    if(OAdsTraceMark(nullptr,1)==0 || OAdsTraceMark(nullptr,0)==0) return 4;
    if(OAdsTraceMarkEx((const UNSIGNED8*)"INVALID_ID",10,10000000000000000ULL,0)==0 ||
       OAdsTraceMarkEx((const UNSIGNED8*)"INVALID_ID",10,0,~UNSIGNED64(0))==0) return 10;
    if(enabled) {
        if(OAdsTraceMarkEx((const UNSIGNED8*)"IDS_MAX",7,9999999999999999ULL,9999999999999999ULL)!=0) return 11;
        if(OAdsTraceMarkEx((const UNSIGNED8*)"INSTANCE_A_BEGIN",16,1,7)!=0 ||
           OAdsTraceMarkEx((const UNSIGNED8*)"INSTANCE_B_BEGIN",16,2,7)!=0 ||
           OAdsTraceMarkEx((const UNSIGNED8*)"INSTANCE_A_END",14,1,7)!=0 ||
           OAdsTraceMarkEx((const UNSIGNED8*)"INSTANCE_B_END",14,2,7)!=0) return 12;
        if(mark(std::string(120,'L'))!=0) return 5;
        std::atomic<bool> failed{false};std::vector<std::thread> workers;
        for(int t=0;t<4;++t) workers.emplace_back([&,t]{for(int n=0;n<25;++n) {
            const std::string label="THREAD_"+std::to_string(t)+"_"+std::to_string(n);
            if(OAdsTraceMarkEx(reinterpret_cast<const UNSIGNED8*>(label.data()),static_cast<UNSIGNED32>(label.size()),3,static_cast<UNSIGNED64>(t))!=0)failed=true;
        }});
        for(auto& w:workers) w.join();
        if(failed) return 6;
        OAdsSetLogging(0);if(mark("MASTER_OFF_CANARY")==0)return 7;OAdsSetLogging(1);
        if(mark("SAVE_END")!=0)return 8;
    }
    if(last_error()!=before) return 9;
    std::cout<<"TRACE-MARKER-PASS "<<mode<<" last-error-preserved no-connection\n";
}
