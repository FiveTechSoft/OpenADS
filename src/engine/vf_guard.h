#pragma once
#include "util/result.h"
#include <string>
#include <cstdint>
#include <vector>
namespace openads::engine::vf {
// Pins the validated namespace for one Harbour metadata operation. This does
// not affect RDD/native file opens. The object outlives the filesystem call.
class PathGuard {
    std::vector<void*> handles_;
    std::string leaf_name_;
public:
    PathGuard()=default;
    PathGuard(const PathGuard&)=delete;
    PathGuard& operator=(const PathGuard&)=delete;
    ~PathGuard();
#ifndef _WIN32
    int descriptor() const { return handles_.empty()?-1:static_cast<int>(reinterpret_cast<intptr_t>(handles_.back())-1); }
    const std::string& leaf_name() const { return leaf_name_; }
#endif
    // leaf=true follows a validated in-root leaf through a pinned descriptor.
    // leaf=false pins only parents, for unlink/rename/mkdir/readlink and masks.
    util::Result<std::string> pin(const std::string& root,const std::string& name,bool leaf);
};
}
