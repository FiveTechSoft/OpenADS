#pragma once

#include "util/result.h"

#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace openads::engine {

inline constexpr std::uint32_t kMaxSessionFiles = 32;
inline constexpr std::uint32_t kMaxFsIoChunk    = 1024u * 1024u;

// FOpen mode bits (Harbour FO_* -ish).
inline constexpr std::uint16_t ADS_FO_READ      = 0x0000;
inline constexpr std::uint16_t ADS_FO_WRITE     = 0x0001;
inline constexpr std::uint16_t ADS_FO_READWRITE = 0x0002;

struct DirEntry {
    std::string   name;
    std::uint64_t size = 0;
    std::uint16_t year = 0;
    std::uint8_t  mon = 0, day = 0, hh = 0, mm = 0, ss = 0;
    std::uint32_t attr = 0; // 0x10 directory, 0x01 readonly
};

util::Result<bool>          fs_exists(const std::string& abs_path);
util::Result<void>          fs_erase(const std::string& abs_path);
util::Result<void>          fs_rename(const std::string& abs_old,
                                      const std::string& abs_new);
util::Result<std::uint64_t> fs_size(const std::string& abs_path);
util::Result<DirEntry>      fs_stat_entry(const std::string& abs_path);
// mask may be "subdir/*.dbf" or "*.txt"; base_dir is the jail-resolved
// directory when mask is basename-only.
util::Result<std::vector<DirEntry>> fs_directory(const std::string& base_dir,
                                                 const std::string& mask);
util::Result<bool>          fs_dir_exist(const std::string& abs_path);
util::Result<void>          fs_dir_make(const std::string& abs_path);
util::Result<void>          fs_dir_remove(const std::string& abs_path);

struct FsFile {
    std::fstream stream;
    std::string  path;
};

// create=true → create/truncate (FCreate). mode uses ADS_FO_*.
util::Result<std::unique_ptr<FsFile>> fs_open(const std::string& abs_path,
                                              std::uint16_t mode,
                                              bool create);

void pack_dir_entry(const DirEntry& e, std::vector<std::uint8_t>& out);
bool unpack_dir_entry(const std::vector<std::uint8_t>& pl, std::size_t& off,
                      DirEntry& e);

} // namespace openads::engine
