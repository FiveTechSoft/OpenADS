#pragma once
#include "platform/path.h"
#include <filesystem>
#include <optional>
#include <string>
namespace openads::engine::vf {
// New vf namespace only. Windows canonical() can return an extended spelling
// for a symlink target and an ordinary spelling for its data root. Compare
// equivalent names, never treat a spelling mismatch as permission to escape.
inline std::filesystem::path normalized_path(const std::filesystem::path& path) {
#ifdef _WIN32
    auto value=path.wstring();
    if(value.compare(0,8,L"\\\\?\\UNC\\")==0)value=L"\\\\"+value.substr(8);
    else if(value.compare(0,4,L"\\\\?\\")==0 && value.size()>6 && value[5]==L':')value.erase(0,4);
    return std::filesystem::path(value);
#else
    return path;
#endif
}
inline std::filesystem::path canonical_path(const std::filesystem::path& path,std::error_code& error) {
    // MinGW GCC10 rejects a final '/.' although it names the parent itself.
    auto input=path;
    while(input.filename()=="." && input!=input.root_path())input=input.parent_path();
    return normalized_path(std::filesystem::weakly_canonical(input,error));
}
inline bool inside_path(const std::filesystem::path& path,const std::filesystem::path& root) {
    auto base=root.generic_string();
    while(!base.empty() && base.back()=='/')base.pop_back();
    return platform::path_has_prefix(path.generic_string(),base,
#ifdef _WIN32
        true
#else
        false
#endif
    );
}
inline std::optional<std::string> resolve_path(const std::string& root,const std::string& relative) {
    namespace fs=std::filesystem;std::error_code error;
    auto base=canonical_path(root,error);if(error)return std::nullopt;
    fs::path combined=relative=="."?base:base/fs::path(relative);
    auto target=canonical_path(combined,error);
    if(error || !inside_path(target,base))return std::nullopt;
    return combined.string();
}
}
