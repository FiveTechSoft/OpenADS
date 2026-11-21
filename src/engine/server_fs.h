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
// Whole-file load/save cap (FileLoad / FileSave / fs_load / fs_save).
inline constexpr std::uint64_t kMaxFsWholeFile  = 64ull * 1024ull * 1024ull;

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
util::Result<void>          fs_copy(const std::string& abs_src,
                                    const std::string& abs_dst);
// Whole-file read (hb_vfLoad). Fails with AE_INTERNAL_ERROR when the file
// exceeds kMaxFsWholeFile.
util::Result<std::vector<std::uint8_t>> fs_load(const std::string& abs_path);
// Whole-file write (hb_vfSave): creates or truncates.
util::Result<void>          fs_save(const std::string& abs_path,
                                    const std::uint8_t* data,
                                    std::uint64_t size);
// attr bit 0x01 = readonly (add or remove write permission); other bits
// are ignored. Directories keep their execute bits.
util::Result<void>          fs_set_attr(const std::string& abs_path,
                                        std::uint32_t attr);
// Set modification time from local-time components.
util::Result<void>          fs_set_time(const std::string& abs_path,
                                        std::uint16_t year,
                                        std::uint8_t mon, std::uint8_t day,
                                        std::uint8_t hh, std::uint8_t mm,
                                        std::uint8_t ss);
util::Result<void>          fs_truncate(const std::string& abs_path,
                                        std::uint64_t size);
// Create an empty uniquely named file under abs_dir whose name starts
// with prefix; returns the file name (not the full path).
util::Result<std::string>   fs_tempfile(const std::string& abs_dir,
                                        const std::string& prefix);
// type 0 = free available, 1 = total capacity, 2 = free.
util::Result<std::uint64_t> fs_dir_space(const std::string& abs_dir,
                                         std::uint8_t type);
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
    bool writable = false;
};
util::Result<void> fs_truncate(FsFile& file, std::uint64_t size);

// create=true → create/truncate (FCreate). mode uses ADS_FO_*.
util::Result<std::unique_ptr<FsFile>> fs_open(const std::string& abs_path,
                                              std::uint16_t mode,
                                              bool create);

void pack_dir_entry(const DirEntry& e, std::vector<std::uint8_t>& out);
bool unpack_dir_entry(const std::vector<std::uint8_t>& pl, std::size_t& off,
                      DirEntry& e);

} // namespace openads::engine
