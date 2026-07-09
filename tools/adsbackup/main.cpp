// adsbackup — OpenADS command-line backup/restore utility.
//
// Drop-in shaped like SAP's adsbackup.exe (see "The adsbackup Utility" in
// the Advantage help): same positional arguments and option letters, built
// on the same openads::engine::backup core the sp_BackupDatabase /
// sp_BackupFreeTables / sp_Restore* system procedures use, so the CLI and
// the SQL path cannot drift apart.
//
//   backup a data dictionary:    adsbackup [options] <src.add> <dest dir>
//   backup free tables:          adsbackup [options] <src dir> [mask] <dest dir>
//   restore a data dictionary:   adsbackup -r [options] <image.add> <dest.add>
//   restore free tables:         adsbackup -r [options] <image dir> <dest dir>
//
// Paths are plain filesystem paths (UNC works on Windows; POSIX paths work
// on Linux/macOS — no drive-letter assumptions).

#include "engine/backup.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace bk = openads::engine::backup;

namespace {

void usage() {
    std::fprintf(stderr,
        "adsbackup — OpenADS backup/restore utility\n"
        "\n"
        "  backup dictionary:   adsbackup [options] <src.add> <dest dir>\n"
        "  backup free tables:  adsbackup [options] <src dir> [mask] <dest dir>\n"
        "  restore dictionary:  adsbackup -r [options] <image.add> <dest.add>\n"
        "  restore free tables: adsbackup -r [options] <image dir> <dest dir>\n"
        "\n"
        "options (SAP adsbackup compatible):\n"
        "  -r             restore instead of backup\n"
        "  -i <t1,..,tn>  include only these tables\n"
        "  -e <t1,..,tn>  exclude these tables\n"
        "  -m             metadata only (dictionary file, no tables)\n"
        "  -d             don't overwrite existing target files\n"
        "  -p <password>  accepted for compatibility (file-level backup\n"
        "                 needs no credentials; encrypted tables are\n"
        "                 copied byte-for-byte)\n"
        "  -a, -f         differential prepare/backup: not supported —\n"
        "                 a full backup is performed, with a warning\n"
        "  -c/-h/-q/-u/-v/-w/-s/-t/-n/-o  accepted and ignored (they\n"
        "                 configure SAP's record-level transfer / log\n"
        "                 table, which do not apply to this utility)\n");
}

bool ends_with_ci(const std::string& s, const char* suffix) {
    std::string a = s, b = suffix;
    for (auto& ch : a) ch = static_cast<char>(std::tolower(
        static_cast<unsigned char>(ch)));
    return a.size() >= b.size() &&
           a.compare(a.size() - b.size(), b.size(), b) == 0;
}

std::vector<std::string> split_commas(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char ch : s) {
        if (ch == ',') {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else if (ch != ' ') {
            cur.push_back(ch);
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

int print_report(const bk::Report& rep, int min_severity) {
    int worst = 0;
    for (const auto& r : rep.rows) {
        if (r.severity < min_severity) continue;
        std::fprintf(r.severity >= 5 ? stderr : stdout,
                     "[%s %d] code %d%s%s%s%s\n",
                     r.severity >= 5 ? "ERROR" : "warn",
                     r.severity, r.error_code,
                     r.message.empty() ? "" : ": ",
                     r.message.c_str(),
                     r.table.empty() ? "" : " — ",
                     r.table.c_str());
        if (r.severity > worst) worst = r.severity;
    }
    std::printf("%zu file(s) copied\n", rep.files_copied);
    return worst >= 5 ? 1 : 0;
}

}  // namespace

int main(int argc, char** argv) {
    bool restore = false;
    int  min_severity = 1;
    bk::Options opt;
    std::vector<std::string> positional;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto flag_value = [&](const char* letter) -> std::string {
            // SAP style allows both "-psecret" and "-p secret".
            if (a.size() > 2) return a.substr(2);
            if (i + 1 < argc) return argv[++i];
            std::fprintf(stderr, "missing value for -%s\n", letter);
            std::exit(2);
        };
        if (a == "--help" || a == "-?" || a == "/?") { usage(); return 0; }
        if (a.size() >= 2 && (a[0] == '-' || a[0] == '/')) {
            switch (std::tolower(static_cast<unsigned char>(a[1]))) {
                case 'r': restore = true; break;
                case 'm': opt.meta_only = true; break;
                case 'd': opt.dont_overwrite = true; break;
                case 'i': opt.include = split_commas(flag_value("i")); break;
                case 'e': opt.exclude = split_commas(flag_value("e")); break;
                case 'p': (void)flag_value("p");
                    std::fprintf(stderr,
                        "note: -p accepted for compatibility; the "
                        "file-level backup needs no credentials\n");
                    break;
                case 'a': case 'f':
                    opt.warnings.push_back(
                        "differential backups are not supported by "
                        "OpenADS; a full backup was performed");
                    break;
                case 'v':
                    min_severity = std::atoi(flag_value("v").c_str());
                    if (min_severity < 1) min_severity = 1;
                    break;
                case 'c': case 'h': case 'q': case 'u': case 'w':
                case 's': case 't': case 'n': case 'o':
                    (void)flag_value("x");   // consume + ignore
                    break;
                default:
                    std::fprintf(stderr, "unknown option: %s\n", a.c_str());
                    usage();
                    return 2;
            }
        } else {
            positional.push_back(a);
        }
    }

    if (positional.size() < 2 || positional.size() > 3) {
        usage();
        return 2;
    }

    const std::string& src  = positional.front();
    const std::string& dest = positional.back();
    std::string mask = positional.size() == 3 ? positional[1] : "";
    const bool dd = ends_with_ci(src, ".add");

    if (dd && positional.size() == 3) {
        std::fprintf(stderr,
            "a file mask is only valid for free-table operations\n");
        return 2;
    }

    openads::util::Result<bk::Report> res =
        openads::util::Error{1, 0, "", ""};
    if (!restore) {
        res = dd ? bk::backup_database(src, dest, opt)
                 : bk::backup_free_tables(src, mask, dest, opt);
    } else {
        res = dd ? bk::restore_database(src, dest, opt)
                 : bk::restore_free_tables(src, dest, opt);
    }
    if (!res.has_value()) {
        std::fprintf(stderr, "%s failed: %s (%d)\n",
                     restore ? "restore" : "backup",
                     res.error().message.c_str(),
                     static_cast<int>(res.error().code));
        return 1;
    }
    return print_report(res.value(), min_severity);
}
