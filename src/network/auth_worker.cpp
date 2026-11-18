#include "network/auth_worker.h"
#include <atomic>
#include <algorithm>
#include <chrono>
#include <thread>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include "network/auth_signal_drain.h"
#include <spawn.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <signal.h>
#include <cerrno>
extern char** environ;
#endif
namespace openads::network {
namespace {
void put(std::string& s,const std::string& v) {
    for(unsigned i=0;i<4;++i)s.push_back(static_cast<char>(v.size()>>(8*i)));
    s+=v;
}
struct Wipe { std::string& s;~Wipe(){volatile char* p=s.empty()?nullptr:&s[0];for(std::size_t i=0;i<s.size();++i)p[i]=0;} };
}
AuthWorker::AuthWorker(std::string executable,std::string module,unsigned concurrency)
    :concurrency_(concurrency),executable_(std::move(executable)),module_(std::move(module)){
    if(concurrency==0 || concurrency>kMaxConcurrency)
        throw std::invalid_argument("auth_worker_concurrency must be 1..256");
}
bool AuthWorker::probe(){std::string out;if(!run("P",out))return false;mutual_required_=out=="READY2";return mutual_required_||out=="READY";}
bool AuthWorker::authenticate(const AuthAttempt::Challenge& c,const std::string& blob,const std::string& ctx,AuthResult& result,bool* capacity_busy){
    result.clear();
    if(capacity_busy)*capacity_busy=false;
    std::string in="A",out;Wipe wi{in},wo{out};
    in.append(reinterpret_cast<const char*>(c.data()),c.size());put(in,blob);put(in,ctx);
    return run(in,out,capacity_busy)&&parse_auth_result(out,result);
}
bool AuthWorker::issue_mutual(const AuthAttempt::Challenge& c,const std::string& transcript,const std::string& context,std::string& proof,bool* busy){
 std::string in="B";Wipe wi{in};in.append(reinterpret_cast<const char*>(c.data()),32);put(in,transcript);put(in,context);
 return run(in,proof,busy)&&!proof.empty()&&proof.size()<=4096;
}
bool AuthWorker::verify_mutual(const AuthAttempt::Challenge& c,const std::string& transcript,const std::string& proof,AuthResult& result,bool* busy){
 result.clear();std::string in="M",out;Wipe wi{in},wo{out};in.append(reinterpret_cast<const char*>(c.data()),32);put(in,transcript);put(in,proof);
 return run(in,out,busy)&&parse_auth_result(out,result);
}
bool AuthWorker::run(const std::string& in,std::string& out,bool* capacity_busy){
    if(capacity_busy)*capacity_busy=false;
    out.clear();if(in.size()>16384)return false;
    // Bounded admission, no queue or reactor-thread sleep. All sessions on
    // this daemon share this AuthWorker instance. Busy is not a bad password.
    auto slots=active_.load();
    do {
        if(slots>=concurrency_){if(capacity_busy)*capacity_busy=true;return false;}
    } while(!active_.compare_exchange_weak(slots,slots+1));
    struct Release{std::atomic<unsigned>& count;~Release(){--count;}} release{active_};
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    bool ok=false;
#ifdef _WIN32
    // Restrict inheritance to exactly the IPC handles via STARTUPINFOEX.
    SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};
    HANDLE ir=nullptr,iw=nullptr,orr=nullptr,ow=nullptr;
    if(!CreatePipe(&ir,&iw,&sa,65536))return false;
    if(!CreatePipe(&orr,&ow,&sa,65536)){CloseHandle(ir);CloseHandle(iw);return false;}
    SetHandleInformation(iw,HANDLE_FLAG_INHERIT,0);SetHandleInformation(orr,HANDLE_FLAG_INHERIT,0);
    STARTUPINFOEXA si{};si.StartupInfo.cb=sizeof(si);si.StartupInfo.dwFlags=STARTF_USESTDHANDLES;
    si.StartupInfo.hStdInput=ir;si.StartupInfo.hStdOutput=ow;si.StartupInfo.hStdError=ow;
    SIZE_T bytes=0;InitializeProcThreadAttributeList(nullptr,1,0,&bytes);
    std::string storage(bytes,'\0');si.lpAttributeList=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    HANDLE handles[]={ir,ow};bool init=InitializeProcThreadAttributeList(si.lpAttributeList,1,0,&bytes)!=0;
    bool attr=init&&UpdateProcThreadAttribute(si.lpAttributeList,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,handles,sizeof(handles),nullptr,nullptr)!=0;
    auto quote=[](const std::string& s){std::string q="\"";unsigned slashes=0;for(char c:s){if(c=='\\'){++slashes;continue;}if(c=='\"')q.append(slashes*2+1,'\\');else q.append(slashes,'\\');q+=c;slashes=0;}q.append(slashes*2,'\\');return q+'\"';};
    std::string cmd=quote(executable_)+" "+quote(module_);PROCESS_INFORMATION pi{};
    // Assign before first instruction: fail closed if the job cannot constrain
    // this child (including on hosts with incompatible enclosing job policy).
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_PROCESS_MEMORY |
        JOB_OBJECT_LIMIT_ACTIVE_PROCESS | JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE |
        JOB_OBJECT_LIMIT_PROCESS_TIME;
    limits.BasicLimitInformation.ActiveProcessLimit = 1;
    limits.BasicLimitInformation.PerProcessUserTimeLimit.QuadPart = 10000000; // 1s
    limits.ProcessMemoryLimit = 256ULL * 1024 * 1024;
    const bool constrained = job && SetInformationJobObject(job,
        JobObjectExtendedLimitInformation, &limits, sizeof(limits));
    bool spawned = attr && constrained && CreateProcessA(executable_.c_str(),
        &cmd[0], nullptr, nullptr, TRUE, CREATE_SUSPENDED | CREATE_NO_WINDOW |
        EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr, &si.StartupInfo, &pi);
    if (spawned && (!AssignProcessToJobObject(job, pi.hProcess) ||
                   ResumeThread(pi.hThread) == static_cast<DWORD>(-1))) {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 2000);
        CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
        spawned = false;
    }
    if(init)DeleteProcThreadAttributeList(si.lpAttributeList);
    CloseHandle(ir);CloseHandle(ow);
    if(spawned){
        DWORD written=0;bool sent=WriteFile(iw,in.data(),static_cast<DWORD>(in.size()),&written,nullptr)&&written==in.size();CloseHandle(iw);iw=nullptr;
        while(sent&&std::chrono::steady_clock::now()<deadline){
            DWORD available=0;if(PeekNamedPipe(orr,nullptr,0,nullptr,&available,nullptr)&&available){char b[1024];DWORD got=0;if(!ReadFile(orr,b,std::min<DWORD>(available,sizeof(b)),&got,nullptr))break;out.append(b,got);if(out.size()>4363)break;}
            if(WaitForSingleObject(pi.hProcess,0)==WAIT_OBJECT_0){DWORD code=1;GetExitCodeProcess(pi.hProcess,&code);DWORD avail=0;while(PeekNamedPipe(orr,nullptr,0,nullptr,&avail,nullptr)&&avail){char b[1024];DWORD got=0;if(!ReadFile(orr,b,std::min<DWORD>(avail,sizeof(b)),&got,nullptr))break;out.append(b,got);if(out.size()>4363)break;}ok=code==0&&out.size()<=4363;break;}
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        if(WaitForSingleObject(pi.hProcess,0)!=WAIT_OBJECT_0){TerminateProcess(pi.hProcess,1);WaitForSingleObject(pi.hProcess,2000);}
        CloseHandle(pi.hProcess);CloseHandle(pi.hThread);
    }
    if (job) CloseHandle(job);
    if(iw)CloseHandle(iw);
    CloseHandle(orr);
#else
    int ip[2],op[2];if(pipe(ip)!=0)return false;if(pipe(op)!=0){close(ip[0]);close(ip[1]);return false;}
    for(int fd:{ip[0],ip[1],op[0],op[1]})fcntl(fd,F_SETFD,FD_CLOEXEC);
    posix_spawn_file_actions_t actions;posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions,ip[0],STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&actions,op[1],STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions,op[1],STDERR_FILENO);
    // Child gets only stdin/stdout/stderr, never daemon sockets/files.
#if defined(__GLIBC__)
    posix_spawn_file_actions_addclosefrom_np(&actions,3);
#else
    // Unsupported close-from platforms fail closed in this spike.
    posix_spawn_file_actions_destroy(&actions);close(ip[0]);close(ip[1]);close(op[0]);close(op[1]);return false;
#endif
    char* args[]={const_cast<char*>(executable_.c_str()),const_cast<char*>(module_.c_str()),nullptr};pid_t pid=-1;
    int rc=posix_spawn(&pid,executable_.c_str(),&actions,nullptr,args,environ);
    posix_spawn_file_actions_destroy(&actions);close(ip[0]);close(op[1]);
    if(rc==0){
        fcntl(ip[1],F_SETFL,O_NONBLOCK);fcntl(op[0],F_SETFL,O_NONBLOCK);
        std::size_t sent=0;int status=0;bool exited=false;
        // SIGPIPE must not kill the daemon if worker rejects input early.
        sigset_t set,old;sigemptyset(&set);sigaddset(&set,SIGPIPE);pthread_sigmask(SIG_BLOCK,&set,&old);
        while(std::chrono::steady_clock::now()<deadline){
            if(ip[1]>=0&&sent<in.size()){auto n=write(ip[1],in.data()+sent,in.size()-sent);if(n>0)sent+=static_cast<std::size_t>(n);else if(n<0&&errno!=EAGAIN&&errno!=EINTR)break;}
            if(ip[1]>=0&&sent==in.size()){close(ip[1]);ip[1]=-1;}
            char b[1024];for(;;){auto n=read(op[0],b,sizeof(b));if(n<=0)break;out.append(b,static_cast<std::size_t>(n));if(out.size()>4363)break;}
            if(out.size()>4363)break;
            if(waitpid(pid,&status,WNOHANG)==pid){exited=true;for(;;){auto n=read(op[0],b,sizeof(b));if(n<=0)break;out.append(b,static_cast<std::size_t>(n));}ok=sent==in.size()&&WIFEXITED(status)&&WEXITSTATUS(status)==0&&out.size()<=4363;break;}
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        if(!exited){kill(pid,SIGKILL);while(waitpid(pid,&status,0)<0&&errno==EINTR){}}
        detail::drain_blocked_signal(SIGPIPE);pthread_sigmask(SIG_SETMASK,&old,nullptr);
    }
    if(ip[1]>=0)close(ip[1]);
    close(op[0]);
#endif
    return ok;
}
}
