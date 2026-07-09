#include "engine/backup.h"
#include "engine/data_dict.h"
#include "openads/error.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <system_error>

namespace openads::engine::backup {

namespace fs = std::filesystem;

namespace {

std::string lower(std::string s) {
    for (auto& ch : s) ch = static_cast<char>(
        std::tolower(static_cast<unsigned char>(ch)));
    return s;
}

std::string trim(std::string s) {
    std::size_t b = s.find_first_not_of(" \t");
    if (b == std::string::npos) return "";
    std::size_t e = s.find_last_not_of(" \t");
    return s.substr(b, e - b + 1);
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (char ch : s) {
        if (ch == sep) {
            std::string t = trim(cur);
            if (!t.empty()) out.push_back(t);
            cur.clear();
        } else {
            cur.push_back(ch);
        }
    }
    std::string t = trim(cur);
    if (!t.empty()) out.push_back(t);
    return out;
}

// Case-insensitive "*.adt"-style mask match on a file name.
bool mask_match(const std::string& mask, const std::string& name) {
    std::string m = lower(mask), n = lower(name);
    std::size_t mi = 0, ni = 0, star = std::string::npos, mark = 0;
    while (ni < n.size()) {
        if (mi < m.size() && (m[mi] == '?' || m[mi] == n[ni])) {
            ++mi; ++ni;
        } else if (mi < m.size() && m[mi] == '*') {
            star = mi++; mark = ni;
        } else if (star != std::string::npos) {
            mi = star + 1; ni = ++mark;
        } else {
            return false;
        }
    }
    while (mi < m.size() && m[mi] == '*') ++mi;
    return mi == m.size();
}

// Is `name` (table object name or file name, per the SAP docs) in the
// include/exclude list? Matches whole names case-insensitively, with or
// without extension so "table1" also matches "table1.adt".
bool in_list(const std::vector<std::string>& list, const std::string& name) {
    std::string n = lower(name);
    std::string stem = lower(fs::path(name).stem().string());
    for (const auto& e : list) {
        std::string le = lower(e);
        if (le == n || le == stem ||
            lower(fs::path(e).stem().string()) == stem) {
            return true;
        }
    }
    return false;
}

bool selected(const Options& opt, const std::string& name) {
    if (!opt.include.empty() && !in_list(opt.include, name)) return false;
    if (in_list(opt.exclude, name)) return false;
    return true;
}

// Companion files that must travel with a table file: memo + auto-open
// index sidecars sharing the table's stem.
std::vector<fs::path> companions(const fs::path& table) {
    static const char* kExts[] = {".adm", ".adi", ".cdx", ".fpt",
                                  ".dbt", ".ntx", ".idx"};
    std::vector<fs::path> out;
    for (const char* e : kExts) {
        fs::path p = table;
        p.replace_extension(e);
        std::error_code ec;
        if (p != table && fs::exists(p, ec)) {
            out.push_back(p);
            continue;   // don't re-probe the upper-case spelling: on a
                        // case-insensitive FS it is the same file
        }
        // Upper-case variants matter on case-sensitive file systems.
        std::string ue = e;
        for (auto& ch : ue) ch = static_cast<char>(
            std::toupper(static_cast<unsigned char>(ch)));
        fs::path pu = table;
        pu.replace_extension(ue);
        if (pu != table && pu != p && fs::exists(pu, ec)) out.push_back(pu);
    }
    return out;
}

void copy_one(const fs::path& src, const fs::path& dst, bool overwrite,
              Report& rep, const std::string& table_label) {
    std::error_code ec;
    if (!overwrite && fs::exists(dst, ec)) {
        rep.rows.push_back({2, 0,
            "target exists and DontOverwrite was specified; skipped",
            table_label, dst.string()});
        return;
    }
    fs::create_directories(dst.parent_path(), ec);
    ec.clear();
    fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        rep.rows.push_back({8, openads::AE_NO_FILE_FOUND,
            "copy failed: " + ec.message(), table_label, src.string()});
    } else {
        ++rep.files_copied;
    }
}

// Copy a table file plus companions into dest_dir, preserving a relative
// subpath when the source is relative to `base`, else flattening.
void copy_table_set(const fs::path& table_abs, const fs::path& base,
                    const fs::path& dest_dir, bool overwrite,
                    Report& rep, const std::string& label) {
    std::error_code ec;
    fs::path rel = fs::relative(table_abs, base, ec);
    bool flat = ec || rel.empty() ||
                rel.string().rfind("..", 0) == 0 || rel.is_absolute();
    if (flat && !ec) {
        rep.rows.push_back({1, 0,
            "table stored outside the source directory; "
            "flattened to its base name in the image",
            label, table_abs.string()});
    }
    fs::path out_rel = flat ? fs::path(table_abs.filename()) : rel;
    copy_one(table_abs, dest_dir / out_rel, overwrite, rep, label);
    for (const auto& comp : companions(table_abs)) {
        fs::path crel = out_rel;
        crel.replace_extension(comp.extension());
        copy_one(comp, dest_dir / crel, overwrite, rep, label);
    }
}

// The dictionary's own sidecar files (.ai index, .am memo).
std::vector<fs::path> dd_sidecars(const fs::path& add) {
    std::vector<fs::path> out{add};
    for (const char* e : {".ai", ".am"}) {
        fs::path p = add;
        p.replace_extension(e);
        std::error_code ec;
        if (fs::exists(p, ec)) out.push_back(p);
    }
    return out;
}

util::Result<Report> copy_database(const std::string& src_add,
                                   const fs::path& dest_dir,
                                   const fs::path& dest_add_name,
                                   const Options& opt,
                                   bool overwrite) {
    std::error_code ec;
    fs::path add(src_add);
    if (!fs::exists(add, ec)) {
        return util::Error{openads::AE_NO_FILE_FOUND, 0,
                           "dictionary not found", src_add};
    }
    auto dd = DataDict::open(src_add);
    if (!dd) return dd.error();

    Report rep;
    for (const auto& w : opt.warnings)
        rep.rows.push_back({1, 0, w, "", ""});

    fs::create_directories(dest_dir, ec);
    if (!fs::is_directory(dest_dir, ec)) {
        return util::Error{openads::AE_NO_FILE_FOUND, 0,
                           "destination directory unavailable",
                           dest_dir.string()};
    }

    // Dictionary file (+ .ai/.am), honoring a rename via dest_add_name.
    for (const auto& sc : dd_sidecars(add)) {
        fs::path name = dest_add_name.empty()
            ? sc.filename()
            : fs::path(dest_add_name).replace_extension(sc.extension());
        copy_one(sc, dest_dir / name, overwrite, rep, add.stem().string());
    }

    if (!opt.meta_only) {
        fs::path base = add.parent_path();
        for (const auto& [alias, rel] : dd.value().tables()) {
            if (!selected(opt, alias)) continue;
            fs::path t(rel);
            if (t.is_relative()) t = base / t;
            if (!fs::exists(t, ec)) {
                rep.rows.push_back({5, openads::AE_NO_FILE_FOUND,
                    "table file missing; not copied", alias, t.string()});
                continue;
            }
            copy_table_set(t, base, dest_dir, overwrite, rep, alias);
        }
    }
    return rep;
}

}  // namespace

Options parse_options(const std::string& text) {
    Options o;
    for (const auto& raw : split(text, ';')) {
        std::string item = trim(raw);
        if (item.empty()) continue;
        std::string key = item, val;
        auto eq = item.find('=');
        if (eq != std::string::npos) {
            key = trim(item.substr(0, eq));
            val = trim(item.substr(eq + 1));
        }
        std::string k = lower(key);
        if (k == "include")             o.include = split(val, ',');
        else if (k == "exclude")        o.exclude = split(val, ',');
        else if (k == "metaonly")       o.meta_only = true;
        else if (k == "dontoverwrite")  o.dont_overwrite = true;
        else if (k == "nowarnings") {
            // Accepted: OpenADS doesn't emit the table-created info
            // entries this suppresses, so it is already satisfied.
        } else if (k == "archivefile" || k == "archivefilecompressed" ||
                   k == "forcearchiveextract") {
            o.warnings.push_back(
                "option '" + key + "' is not supported by OpenADS; "
                "the image is written as individual files");
        } else if (k == "diff" || k == "preparediff") {
            o.warnings.push_back(
                "differential backups are not supported by OpenADS; "
                "a full backup was performed");
        } else if (k == "tabletypemap" || k == "user" ||
                   k == "ddpassword") {
            o.warnings.push_back(
                "option '" + key + "' is accepted but has no effect in "
                "OpenADS (file-level backup)");
        } else {
            o.warnings.push_back("unknown option '" + key + "' ignored");
        }
    }
    return o;
}

util::Result<Report> backup_database(const std::string& add_path,
                                     const std::string& dest_dir,
                                     const Options& opt) {
    return copy_database(add_path, fs::path(dest_dir), fs::path(),
                         opt, /*overwrite=*/!opt.dont_overwrite);
}

util::Result<Report> backup_free_tables(const std::string& src_dir,
                                        const std::string& masks,
                                        const std::string& dest_dir,
                                        const Options& opt) {
    std::error_code ec;
    if (!fs::is_directory(src_dir, ec)) {
        return util::Error{openads::AE_NO_FILE_FOUND, 0,
                           "source directory not found", src_dir};
    }
    Report rep;
    for (const auto& w : opt.warnings)
        rep.rows.push_back({1, 0, w, "", ""});
    fs::create_directories(dest_dir, ec);

    std::vector<std::string> mlist =
        split(masks.empty() ? "*.adt;*.dbf" : masks, ';');
    for (const auto& de : fs::directory_iterator(src_dir, ec)) {
        if (!de.is_regular_file(ec)) continue;
        std::string fname = de.path().filename().string();
        bool hit = false;
        for (const auto& m : mlist) {
            if (mask_match(m, fname)) { hit = true; break; }
        }
        if (!hit) continue;
        if (!selected(opt, fname)) continue;
        copy_table_set(de.path(), fs::path(src_dir), fs::path(dest_dir),
                       !opt.dont_overwrite, rep, fname);
    }
    return rep;
}

util::Result<Report> restore_database(const std::string& src_add,
                                      const std::string& dest_add,
                                      const Options& opt) {
    fs::path dst(dest_add);
    return copy_database(src_add, dst.parent_path(), dst.filename(),
                         opt, /*overwrite=*/!opt.dont_overwrite);
}

util::Result<Report> restore_free_tables(const std::string& src_dir,
                                         const std::string& dest_dir,
                                         const Options& opt) {
    // A free-table image is a flat directory: every file in it (tables
    // AND their companion index/memo files) comes back verbatim, so this
    // is a plain masked copy without the companion-chasing the backup
    // direction needs.
    std::error_code ec;
    if (!fs::is_directory(src_dir, ec)) {
        return util::Error{openads::AE_NO_FILE_FOUND, 0,
                           "backup image directory not found", src_dir};
    }
    Report rep;
    for (const auto& w : opt.warnings)
        rep.rows.push_back({1, 0, w, "", ""});
    fs::create_directories(dest_dir, ec);
    for (const auto& de : fs::directory_iterator(src_dir, ec)) {
        if (!de.is_regular_file(ec)) continue;
        std::string fname = de.path().filename().string();
        if (!selected(opt, fname)) continue;
        copy_one(de.path(), fs::path(dest_dir) / fname,
                 !opt.dont_overwrite, rep, fname);
    }
    return rep;
}

}  // namespace openads::engine::backup
