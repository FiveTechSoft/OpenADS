// Server-side ZIP/UNZIP for backup archiving under --data.
// See OAds_Zip/OAds_UnZip (Harbour wrappers) and AdsZipFiles/
// AdsUnzipFiles (ACE). Classic minizip (ZipCrypto-compatible —
// what xBase zip tools read), operating on absolute already-jailed
// paths; callers resolve the jail (platform::resolve_fs_path).

#pragma once

#include "util/result.h"

#include <cstdint>
#include <string>
#include <vector>

namespace openads::engine::zip_arch {

struct Stats {
    std::uint32_t files         = 0;
    std::uint64_t bytes         = 0;  // sum of source/extracted sizes
    std::uint64_t archive_bytes = 0;  // archive file size on disk
};

struct ZipOptions {
    int         level     = 6;     // 0..9 (0 = store)
    bool        overwrite = false;  // replace existing archive
    std::string password;           // empty = none (ZipCrypto when set)
    bool        with_path = false;  // store entry paths (else basenames)
    std::vector<std::string> exclude;  // basename masks, * and ?
};

struct UnzipOptions {
    bool        overwrite = false;  // replace existing files
    std::string password;           // empty = none
    bool        with_path = false;  // recreate archived dirs (else flat)
};

// Archive `abs_files` (absolute, jailed) into `archive_abs`.
// Entry names are basenames, or srcdir-relative paths when with_path.
// Missing sources fail loud (AE_NO_FILE_FOUND); an existing archive
// with overwrite off fails loud; empty file list fails loud.
util::Result<Stats> zip_files(const std::vector<std::string>& abs_files,
                              const std::string& src_dir_abs,
                              const std::string& archive_abs,
                              const ZipOptions& opt);

// Extract `archive_abs` into `dest_dir_abs` (created when missing).
// Zip-Slip guarded: absolute entries and ".." components are rejected
// (AE_ACCESS_DENIED). Existing targets with overwrite off fail loud;
// flat extraction with basename collisions fails loud.
// Note: entry timestamps are NOT restored (v1).
util::Result<Stats> unzip_files(const std::string& archive_abs,
                                const std::string& dest_dir_abs,
                                const UnzipOptions& opt);

// Central-directory entry names (raw, '/'-separated) without
// extracting. Used to pre-check extraction targets (open files).
// Needs no password (only entry contents are encrypted).
util::Result<std::vector<std::string>> list_entries(
    const std::string& archive_abs);

// One central-directory entry with the fields hb_GetFilesInZip's
// verbose form reports (plus the raw attribute words). Reads the
// central directory only — needs no password.
struct ZipEntry {
    std::string   name;                 // '/'-separated, as stored
    std::uint64_t size      = 0;        // uncompressed bytes
    std::uint64_t comp_size = 0;        // stored bytes
    std::uint16_t method    = 0;        // 0 = store, 8 = deflate
    std::uint32_t crc       = 0;        // IEEE CRC-32 of the data
    std::uint16_t year = 0;             // full year, 0 = unknown
    std::uint8_t  mon = 0, day = 0, hh = 0, mm = 0, ss = 0;
    std::uint16_t internal_attr = 0;
    std::uint32_t external_attr = 0;    // DOS attrs in the low byte
    bool          encrypted = false;    // general-purpose flag bit 0
    std::string comment;                // per-file comment (often empty)
};

util::Result<std::vector<ZipEntry>> list_detailed(
    const std::string& archive_abs);

// Packed entry layout shared by the ZipList wire payload and the
// AdsZipListFiles buffer (all integers little-endian):
//   [u16 nameLen][name][u64 size][u64 compSize][u16 method][u32 crc]
//   [u16 year][u8 mon][u8 day][u8 hh][u8 mm][u8 ss]
//   [u16 internalAttr][u32 externalAttr][u8 encrypted]
//   [u16 commentLen][comment]
void pack_zip_entry(const ZipEntry& e, std::vector<std::uint8_t>& out);
bool unpack_zip_entry(const std::vector<std::uint8_t>& pl,
                      std::size_t& off, ZipEntry& e);

}  // namespace openads::engine::zip_arch
