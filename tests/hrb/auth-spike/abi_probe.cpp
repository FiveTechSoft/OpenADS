#include "openads/ace.h"
#include <cassert>
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
int main(int argc,char** argv){
    assert(argc==2);
    ADSHANDLE p=0,h=0;UNSIGNED8 c[32];
    auto rc=OAdsBeginAuth(argv[1],&p,c);
    std::cout<<"begin="<<rc<<" pending="<<p<<"\n";assert(rc==0&&p);
    std::string b(reinterpret_cast<char*>(c),32);b+="test-only";
    auto first=b;
    rc=OAdsCompleteAuth(p,reinterpret_cast<const UNSIGNED8*>(b.data()),b.size(),&h);
    std::cout<<"complete="<<rc<<" connection="<<h<<"\n";assert(rc==0&&h);
    assert(OAdsCompleteAuth(p,reinterpret_cast<const UNSIGNED8*>(b.data()),b.size(),&h)!=0&&h==0);
    assert(OAdsBeginAuth(argv[1],&p,c)==0);
    assert(OAdsCompleteAuth(p,reinterpret_cast<const UNSIGNED8*>(first.data()),first.size(),&h)!=0&&h==0);
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    assert(OAdsBeginAuth(argv[1],&p,c)==0);assert(OAdsCancelAuth(p)==0);
    assert(OAdsCompleteAuth(p,reinterpret_cast<const UNSIGNED8*>(b.data()),b.size(),&h)!=0&&h==0);
    std::cout<<"auth ABI normal checks/single use/replay/cancel PASS\n";
}
