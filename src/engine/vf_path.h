#pragma once
#include "platform/path.h"
#include <filesystem>
#include <optional>
#include <string>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <vector>
#endif
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
    auto input=normalized_path(path);
    while(input.filename()=="." && input!=input.root_path())input=input.parent_path();
#ifdef _WIN32
    // GCC10 canonical/read_symlink can retain a reparse spelling instead of
    // its final target. Resolve the existing ancestor through a native handle,
    // append only missing suffixes, then let PathGuard/native open pin it.
    input=std::filesystem::absolute(input,error);
    if(error)return {};
    std::vector<std::filesystem::path> missing;
    for(;;) {
        HANDLE handle=::CreateFileW(input.c_str(),FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,
            OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,nullptr);
        if(handle!=INVALID_HANDLE_VALUE) {
            const DWORD needed=::GetFinalPathNameByHandleW(handle,nullptr,0,0);
            if(!needed){const auto code=::GetLastError();::CloseHandle(handle);error=std::error_code(code,std::system_category());return {};}
            std::vector<wchar_t> text(static_cast<std::size_t>(needed)+1);
            const DWORD used=::GetFinalPathNameByHandleW(handle,text.data(),static_cast<DWORD>(text.size()),0);
            const auto code=::GetLastError();::CloseHandle(handle);
            if(!used || used>=text.size()){error=std::error_code(used?ERROR_INSUFFICIENT_BUFFER:code,std::system_category());return {};}
            auto result=normalized_path(std::filesystem::path(std::wstring(text.data(),used)));
            for(auto i=missing.rbegin();i!=missing.rend();++i)result/=*i;
            error.clear();return result.lexically_normal();
        }
        const auto code=::GetLastError();
        if((code!=ERROR_FILE_NOT_FOUND && code!=ERROR_PATH_NOT_FOUND && code!=ERROR_INVALID_NAME) || input==input.root_path() || input.filename().empty()) {
            error=std::error_code(code,std::system_category());return {};
        }
        missing.push_back(input.filename());input=input.parent_path();
    }
#else
    return normalized_path(std::filesystem::weakly_canonical(input,error));
#endif
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
