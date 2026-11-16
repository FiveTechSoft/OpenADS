#pragma once
#include "util/result.h"
#include <string>
#include <vector>
namespace openads::engine::vf {
// Pins the validated namespace for one Harbour metadata operation. This does
// not affect RDD/native file opens. The object outlives the filesystem call.
class PathGuard {
    std::vector<void*> handles_;
public:
    PathGuard()=default;
    PathGuard(const PathGuard&)=delete;
    PathGuard& operator=(const PathGuard&)=delete;
    ~PathGuard();
    // leaf=true follows a validated in-root leaf through a pinned descriptor.
    // leaf=false pins only parents, for unlink/rename/mkdir/readlink and masks.
    util::Result<std::string> pin(const std::string& root,const std::string& name,bool leaf);
};
}
