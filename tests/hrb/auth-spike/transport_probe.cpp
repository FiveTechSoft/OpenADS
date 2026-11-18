#include "network/server.h"
#include "network/tls_transport.h"
#include <chrono>
#include <thread>
#include <iostream>
#include <cassert>
using namespace openads::network;
int main(int argc,char**argv){bool tls=argc>1; (void)argv;auto cfg=make_auth_tls_config();assert(cfg);auto l=listen_tcp({"127.0.0.1",0,16});assert(l);auto listener=l.value();auto p=socket_local_port(listener);assert(p);std::thread server([&]{auto a=accept_one(listener);assert(a);std::unique_ptr<ITransport> t;if(tls){auto q=accept_tls(a.value(),cfg.value());assert(q);t=std::move(q).value();}else t=make_plain_transport(a.value());for(int i=0;i<1000;i++){auto f=read_frame(*t);assert(f);assert(write_frame(*t,f.value()));}t->close();});
auto t0=std::chrono::steady_clock::now();std::unique_ptr<ITransport> t;if(tls){auto c=cfg.value();c.insecure_skip_verify=true;auto q=connect_tls("127.0.0.1",p.value(),c);assert(q);t=std::move(q).value();}else{auto q=connect_tcp("127.0.0.1",p.value());assert(q);t=make_plain_transport(q.value());}auto t1=std::chrono::steady_clock::now();Frame f;f.opcode=Opcode::Hello;f.payload.assign(24,1);for(int i=0;i<1000;i++){assert(write_frame(*t,f));auto r=read_frame(*t);assert(r&&r.value().payload==f.payload);}auto t2=std::chrono::steady_clock::now();t->close();server.join();sock_close(listener);std::cout<<(tls?"tls":"plain")<<" completed_rtts=1000 login_us="<<std::chrono::duration_cast<std::chrono::microseconds>(t1-t0).count()<<" workload_us="<<std::chrono::duration_cast<std::chrono::microseconds>(t2-t1).count()<<"\n";}
