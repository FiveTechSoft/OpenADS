#include "engine/vf_guard.h"
#include <filesystem>
#include <cerrno>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#endif
namespace openads::engine::vf {
namespace {
util::Error denied(int error) { return util::Error{5000,error,"vf metadata namespace pin failed",{}}; }
}
PathGuard::~PathGuard() {
    for(auto h:handles_) {
#ifdef _WIN32
        ::CloseHandle(static_cast<HANDLE>(h));
#else
        ::close(static_cast<int>(reinterpret_cast<intptr_t>(h)-1));
#endif
    }
}
util::Result<std::string> PathGuard::pin(const std::string& root,const std::string& name,bool leaf) {
    namespace fs=std::filesystem;
    if(root.empty())return denied(5);
    std::error_code ec;
    auto base=fs::weakly_canonical(root,ec);
    if(ec)return denied(5);
    // Destructive operations act on the named leaf itself, not its target.
    // Follow-only operations first validate the target, then open NOFOLLOW.
    auto path=leaf?fs::weakly_canonical(name,ec):fs::path(name).lexically_normal();
    if(ec)return denied(5);
    auto rel=path.lexically_relative(base);
    if(rel.empty() && path!=base)return denied(5);
    std::vector<fs::path> parts;
    for(auto& p:rel) {
        if(p==".." || p.is_absolute())return denied(5);
        if(p!="." && !p.empty()) {
#ifdef _WIN32
            const auto spelling=p.string();
            if(spelling.find(':')!=std::string::npos || spelling.back()=='.' || spelling.back()==' ')return denied(5);
#endif
            parts.push_back(p);
        }
    }
    leaf_name_=parts.empty()?".":parts.back().string();
#ifdef _WIN32
    fs::path current=base;
    const auto count=leaf?parts.size():(parts.empty()?0:parts.size()-1);
    for(std::size_t i=0;i<=count;++i) {
        if(i)current/=parts[i-1];
        HANDLE h=::CreateFileW(current.c_str(),FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if(h==INVALID_HANDLE_VALUE)return denied(static_cast<int>(::GetLastError()));
        handles_.push_back(h);
        BY_HANDLE_FILE_INFORMATION info{};
        if(!::GetFileInformationByHandle(h,&info) || (info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT) ||
           (i<count && !(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)))return denied(5);
    }
    if(!leaf && !parts.empty())current/=parts.back();
    return current.string();
#else
    int fd=::open(base.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    if(fd<0)return denied(errno);
    handles_.push_back(reinterpret_cast<void*>(static_cast<intptr_t>(fd)+1));
    const auto count=leaf?parts.size():(parts.empty()?0:parts.size()-1);
    for(std::size_t i=0;i<count;++i) {
        int flags=O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK;
        if(i+1<count || !leaf)flags|=O_DIRECTORY;
#if defined(__linux__)
        else flags=O_PATH|O_NOFOLLOW|O_CLOEXEC;
#endif
        const int next=::openat(fd,parts[i].c_str(),flags);
        if(next<0)return denied(errno);
        handles_.push_back(reinterpret_cast<void*>(static_cast<intptr_t>(next)+1));fd=next;
        // O_PATH|O_NOFOLLOW opens the symlink itself; refuse that leaf.
        struct stat st{};
        if(::fstat(fd,&st)!=0 || S_ISLNK(st.st_mode))return denied(EACCES);
    }
#ifdef __linux__
    std::string result="/proc/self/fd/"+std::to_string(fd);
#else
    std::string result="/dev/fd/"+std::to_string(fd);
#endif
    if(!leaf && !parts.empty())result+="/"+parts.back().string();
    return result;
#endif
}
}
