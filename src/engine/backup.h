#pragma once

#include "util/result.h"

#include <string>
#include <vector>

namespace openads::engine::backup {

// Parsed form of the sp_Backup*/sp_Restore* Options argument and the
// adsbackup command-line flags — one option set shared by both entry
// points so their behavior can't drift.
struct Options {
    std::vector<std::string> include;   // table object names / file names
    std::vector<std::string> exclude;
    bool meta_only      = false;        // MetaOnly / -m: dictionary only
    bool dont_overwrite = false;        // DontOverwrite / -d (restore only)
    // Recognized-but-unsupported options (ArchiveFile*, Diff/PrepareDiff,
    // TableTypeMap, User, DDPassword, NoWarnings, ...) surface here so
    // callers can report them without failing the whole operation.
    std::vector<std::string> warnings;
};

// Parse a semicolon-separated SAP Options string ("include=a,b;MetaOnly").
// Never fails: unknown/unsupported options become Options::warnings.
Options parse_options(const std::string& text);

// One result-set row, shaped like SAP's backup/restore canned-procedure
// result sets: an EMPTY report means complete success.
struct ReportRow {
    int          severity   = 1;   // 0..10
    std::int32_t error_code = 0;
    std::string  message;
    std::string  table;
    std::string  extra;
};

struct Report {
    std::vector<ReportRow> rows;
    std::size_t files_copied = 0;
};

// Copy a data dictionary (.add + .ai/.am sidecars) and its bound tables
// (with their index/memo companion files) into dest_dir. Table paths that
// resolve outside the dictionary's directory are flattened to their base
// name in the image, with a warning row. Consistency note: this is a
// file-level image; tables being actively written during the copy are
// crash-consistent only (the sp_ entry point flushes open tables first).
util::Result<Report> backup_database(const std::string& add_path,
                                     const std::string& dest_dir,
                                     const Options& opt);

// Copy free tables matching `masks` ("*.adt;*.dbf"; empty = that default)
// from src_dir into dest_dir, along with companion index/memo files.
util::Result<Report> backup_free_tables(const std::string& src_dir,
                                        const std::string& masks,
                                        const std::string& dest_dir,
                                        const Options& opt);

// Restore a database image: src_add is the .add inside the backup image,
// dest_add the .add path to create (its directory receives the tables).
util::Result<Report> restore_database(const std::string& src_add,
                                      const std::string& dest_add,
                                      const Options& opt);

// Restore a free-table image from src_dir into dest_dir.
util::Result<Report> restore_free_tables(const std::string& src_dir,
                                         const std::string& dest_dir,
                                         const Options& opt);

}  // namespace openads::engine::backup
