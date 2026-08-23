#include "session/connection.h"

#include "engine/pbkdf2.h"

#include "drivers/adm/adm_memo.h"
#include "drivers/cdx/cdx_driver.h"
#include "drivers/dbt/dbt_memo.h"
#include "drivers/fpt/fpt_memo.h"
#include "drivers/ntx/ntx_driver.h"
#include "platform/dll.h"
#include "platform/fs_sandbox.h"
#include "platform/path.h"
#include "util/log.h"

#include <cstring>

#include "openads/error.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace openads::session {

namespace {

// True when `p`'s directory is `base_dir` itself or lives under it.
// Windows paths are case-insensitive and a caller may spell the same
// directory with either separator, so normalise before comparing.
bool path_is_inside(const std::string&           base_dir,
                    const std::filesystem::path& p) {
    namespace fs = std::filesystem;
    if (base_dir.empty()) return false;
    auto norm = [](const fs::path& in) {
        std::string s = in.lexically_normal().string();
        for (auto& ch : s) {
            if (ch == '\\') ch = '/';
            ch = static_cast<char>(
                std::tolower(static_cast<unsigned char>(ch)));
        }
        while (!s.empty() && s.back() == '/') s.pop_back();
        return s;
    };
    const std::string base = norm(fs::path(base_dir));
    const std::string dir  = norm(p.parent_path());
    if (base.empty() || dir.empty()) return false;
    if (dir == base) return true;
    return dir.size() > base.size() &&
           dir.compare(0, base.size(), base) == 0 &&
           dir[base.size()] == '/';
}

// Read the header magic of an existing table file and report which
// driver family can actually read it. The bytes on disk are the only
// authoritative answer: neither the extension nor the caller-supplied
// table type has to agree with them.
//
// Why this matters (RusSoft, 2026-07-28): Harbour's contrib/rddads only
// forwards the statement table type to AdsStmtSetTableType when it is
// ADS_CDX or ADS_VFP -- ADS_ADT and ADS_NTX are dropped on the floor,
// because on the real Advantage engine an SQL statement already defaults
// to ADT. OpenADS defaults to CDX instead, so every `SELECT ... FROM
// [tbl.dat]` against an ADT table opened the file with the CDX driver,
// parsed the ADT header as a DBF one and reported the first referenced
// column as "Column not found". An ERP that names its ADT tables `.DAT`
// (extension carries no type information) had no way to say otherwise.
//
// Returns nullopt when the file cannot be read or the magic is unknown,
// in which case the caller keeps whatever type it had.
std::optional<openads::engine::TableType>
sniff_table_type(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) return std::nullopt;
    unsigned char hdr[16] = {0};
    std::size_t n = std::fread(hdr, 1, sizeof(hdr), f);
    std::fclose(f);
    if (n < 1) return std::nullopt;

    // ADT files open with the literal "Advantage Table".
    static const char kAdtMagic[] = "Advantage Table";
    if (n >= sizeof(kAdtMagic) - 1 &&
        std::memcmp(hdr, kAdtMagic, sizeof(kAdtMagic) - 1) == 0) {
        return openads::engine::TableType::Adt;
    }

    // DBF family: byte 0 is the dBASE/FoxPro version marker. The set
    // below is the one the CDX/NTX drivers accept; both share the same
    // record container and differ only in their index files, so a DBF
    // magic never tells Cdx and Ntx apart -- report Cdx and let the
    // caller keep an Ntx choice it made deliberately.
    switch (hdr[0]) {
        case 0x02: case 0x03: case 0x04: case 0x05:
        case 0x30: case 0x31: case 0x32: case 0x43:
        case 0x63: case 0x7B: case 0x83: case 0x8B:
        case 0x8E: case 0xB3: case 0xCB: case 0xE5:
        case 0xF5: case 0xFB:
            return openads::engine::TableType::Cdx;
        default:
            return std::nullopt;
    }
}

// Apply the on-disk verdict to `type`, leaving deliberate choices that
// the magic cannot contradict alone (Cdx vs Ntx over the same DBF).
void align_type_with_file(const std::string& path,
                          openads::engine::TableType& type) {
    auto sniffed = sniff_table_type(path);
    if (!sniffed) return;
    if (*sniffed == openads::engine::TableType::Adt) {
        type = openads::engine::TableType::Adt;
    } else if (type == openads::engine::TableType::Adt) {
        // Caller asked for ADT but the file is a DBF container.
        type = openads::engine::TableType::Cdx;
    }
}

}  // namespace

util::Result<Connection> Connection::open(const std::string& data_dir) {
    namespace fs = std::filesystem;
    Connection c;
    c.conn_serial_ = util::make_connection_serial();

    // If the input ends with ".add", treat it as a Data Dictionary path
    // and use its parent as the data directory.
    fs::path p(data_dir);
    bool is_add = p.has_extension() &&
        (p.extension() == ".add" || p.extension() == ".ADD");

    std::string actual_dir;
    if (is_add) {
        actual_dir = p.parent_path().string();
        if (actual_dir.empty()) actual_dir = ".";
    } else {
        actual_dir = data_dir;
    }

    std::error_code ec;
    if (!fs::is_directory(actual_dir, ec)) {
        return util::Error{5103, 0, "data directory not found", actual_dir};
    }
    c.data_dir_ = actual_dir;

    if (is_add) {
        if (fs::exists(p, ec)) {
            c.dd_path_ = fs::absolute(p).string();
            auto dd = engine::DataDict::open(p.string());
            if (!dd) return dd.error();
            c.dd_ = std::move(dd).value();
        }
    }

    fs::path log_path = fs::path(actual_dir) / "openads.txlog";
    auto lr = c.tx_log_.open(log_path.string());
    bool journal_ok = lr.has_value();
    if (!journal_ok) {
        // The data directory is not writable — e.g. --data "C:\" on
        // Windows, where a non-elevated process cannot create files at a
        // system drive root. The connection must still succeed: tables
        // in *subdirectories* remain fully usable; only transactions are
        // unavailable (begin_transaction fails cleanly with a clear
        // error instead of the whole Connect being refused).
        std::fprintf(stderr,
            "[openads] tx journal not writable in '%s' — transactions "
            "disabled for this connection\n", actual_dir.c_str());
    }
    if (journal_ok) {
        fs::path map_path = fs::path(actual_dir) / "openads.lsnmap";
        if (auto mr = c.lsn_map_.open(map_path.string()); !mr) {
            std::fprintf(stderr,
                "[openads] tx recovery map not writable in '%s' — orphan "
                "recovery skipped for this connection\n",
                actual_dir.c_str());
        } else if (auto rr = c.recover_orphan_tx_(); !rr) {
            return rr.error();
        }
    }
    return c;
}

// Resolve a caller-supplied table name (DD alias, bare leaf, or path)
// to the absolute on-disk file the driver opens. `type` is updated when
// the extension implies a different driver than the caller's default.
// Shared by open_table and find_open_table so both agree byte-for-byte
// on which physical file a name maps to.
std::string Connection::resolve_table_file(const std::string& relative_path,
                                           engine::TableType&  type,
                                           bool                for_create) {
    namespace fs = std::filesystem;
    std::string effective = relative_path;
    if (dd_.has_value()) effective = dd_->resolve(relative_path);
    if (conn_serial_.empty()) conn_serial_ = util::make_connection_serial();
    const std::string ts = util::format_log_timestamp();
    const std::string log_alias = fs::path(relative_path).stem().string();
    std::vector<std::string> pending_details;
    auto log_resolved = [&](const std::string& path, const char* tag) {
        // One RESOLVED line per physical file per connection. AdsOpenTable,
        // find_open_table and index-bag resolve all call through here for
        // the same .dbf / .z01 (Pritpal Bedi, Aug 2026). The network
        // Session's ABI twin shares this set (share_logged_resolves_from)
        // so the twin's index/SQL re-open does not log the file again.
        std::string key = path;
        for (char& ch : key) {
            if (ch == '\\') ch = '/';
            ch = static_cast<char>(
                std::tolower(static_cast<unsigned char>(ch)));
        }
        if (!logged_resolves_->insert(std::move(key)).second) return;
        const std::uint32_t entry = next_entry_serial_++;
        const std::uint32_t seq   = util::next_audit_seq();
        last_audit_seq_ = seq;
        for (const auto& d : pending_details) {
            util::write_audit(util::AuditKind::Detail, conn_serial_,
                              entry, seq, d, ts, log_alias);
        }
        std::string norm_path = path;
        for (char& ch : norm_path) { if (ch == '\\') ch = '/'; }
        std::string msg = "RESOLVED=\"";
        msg += norm_path;
        msg += "\" ";
        msg += tag;
        msg += " ASKED=\"";
        msg += relative_path;
        msg += remote_server_ ? "\" VIA=REMOTE" : "\" VIA=LOCAL";
        util::write_audit(util::AuditKind::Resolved, conn_serial_, entry, seq,
                           msg, ts, log_alias);
    };
    // The connection's data_dir is the directory the server owns; every
    // table name is resolved *relative to* it. Clients (Harbour rddads,
    // X# ADSRDD, …) routinely pass an absolute or drive-rooted path that
    // they derived from their own working directory. Joining that
    // verbatim (`data_dir_ / "C:\\app\\foo.dbf"`) yields the absolute
    // client path and silently drops the configured data directory, so
    // DbCreate / AdsCreateTable would write the file next to the
    // application instead of in the ADS-pointed folder. Drop the root and
    // keep only the relative remainder (subdirs + filename) before
    // joining, so the table always lands under data_dir_ and a later
    // AdsOpenTable with the same name resolves to the very same file.
    //
    // OPEN exception (strict / non-legacy only): an absolute path that
    // already names an existing file is honored verbatim — SAP opens free
    // tables by full path even on a data-directory connection, and folding
    // those broke every caller that opens a table it just staged at an
    // absolute location (the DD field-property fixtures do exactly that).
    //
    // CREATE exception (option 2, issue #142; strict / non-legacy only):
    // honour an absolute path when its parent directory is data_dir_ or a
    // subdirectory of it. A bare drive-root name (C:\STRAY.DBF) still folds
    // — the root always exists but is never a deliberate destination
    // (abi_create_table_test pins that case). Paths outside data_dir_ keep
    // folding so DbCreate cannot write next to the application.
    //
    // Legacy ERP mode (server --legacy-paths) is different: client-absolute
    // paths MUST always remount under --data, even when the same spelling
    // happens to exist on the host filesystem. On a Windows server with
    // --data C:/Temp and USE "C:/Creative.RAM/...", a leftover
    // C:\Creative.RAM\... tree must not win over C:\Temp\Creative.RAM\...
    // (Pritpal Bedi, Aug 2026). Linux live verification never hit this
    // because E:\... does not exist as a real path on POSIX.
    fs::path rel = fs::path(effective);
    // Legacy mode adds host-independent absolute detection: a Windows
    // client path like "E:\CLIENT\F.DBF" is not absolute to
    // std::filesystem on a POSIX server (#133), yet it must still enter
    // the fold/prefix-strip path instead of joining verbatim under
    // data_dir_.
    const bool client_absolute =
        rel.is_absolute() || rel.has_root_directory() ||
        (legacy_paths_ && platform::is_client_absolute(effective));
    if (client_absolute) {
        if (legacy_paths_) {
            // Always remap onto the data root; never open/create a host
            // path outside the jail just because it already exists.
            if (auto r = platform::resolve_client_path({data_dir_},
                                                       effective)) {
                rel = fs::path(*r);
            } else {
                rel = fs::path(platform::fold_absolute_to_relative(effective));
            }
        } else if (remote_server_) {
            // Remote is safe storage (Pritpal Bedi, Aug 2026): never
            // stat or open the client's host-absolute spelling, even
            // when that file happens to exist on the server box. Fold
            // under --data; a miss becomes the normal table-not-found
            // error at open_table (Harbour RDD EG_OPEN). --legacy-paths
            // remount is the only way a Creative.RAM-style path lands
            // in the jail (handled above).
            //
            // Exception: a host-absolute path that already lands INSIDE
            // data_dir_ names a file the server owns, so honoring it
            // verbatim is still safe storage. Server-internal callers
            // depend on this -- AdsCreateTable reopens the file it just
            // wrote by its resolved absolute path (the ADT temp cursors
            // behind every remote SELECT that materialises a JOIN /
            // UNION / aggregate result), and folding those produced a
            // doubled data_dir_/Users/.../ path that opened nothing
            // (surfaced as AE_PARSE_ERROR at the SQL boundary).
            if (path_is_inside(data_dir_, rel)) {
                std::string resolved =
                    platform::resolve_case_insensitive(rel.string());
                log_resolved(resolved, "JAILED");
                return resolved;
            }
            rel = rel.relative_path();
        } else if (!for_create) {
            std::error_code ec;
            fs::path cand = rel;
            if (!cand.has_extension()) {
                fs::path dbf = cand;
                dbf.replace_extension(".dbf");
                if (fs::exists(dbf, ec)) {
                    cand = dbf;
                } else {
                    fs::path adt = cand;
                    adt.replace_extension(".adt");
                    if (fs::exists(adt, ec)) {
                        cand = adt;
                        type = engine::TableType::Adt;
                    } else {
                        // RusSoft/Zerus (and other legacy ERPs) keep every
                        // table -- ADT or DBF alike -- under a literal .dat
                        // extension on disk; probe it last so a "real"
                        // .dbf/.adt sibling still wins. align_type_with_file()
                        // below sniffs the header, so we do not guess a type
                        // here.
                        fs::path dat = cand;
                        dat.replace_extension(".dat");
                        if (fs::exists(dat, ec)) {
                            cand = dat;
                        }
                    }
                }
            }
            if (cand.has_extension() && fs::exists(cand, ec)) {
                std::string aext = cand.extension().string();
                for (auto& ch : aext) ch = static_cast<char>(
                    std::tolower(static_cast<unsigned char>(ch)));
                if (aext == ".adt" && type == engine::TableType::Cdx)
                    type = engine::TableType::Adt;
                else if (aext == ".ntx" && type == engine::TableType::Cdx)
                    type = engine::TableType::Ntx;
                {
                    std::string resolved =
                        platform::resolve_case_insensitive(cand.string());
                    align_type_with_file(resolved, type);
                    log_resolved(resolved, "(client)");
                    return resolved;
                }
            }
            rel = rel.relative_path();
        } else {
            // Absolute create under the configured data directory: write
            // exactly there. Parent must exist (we do not mkdir intermediates);
            // ofstream failure then surfaces as a real create error.
            //
            // Same treatment when the path is OUTSIDE data_dir_ but its parent
            // directory already exists, which is what the real ACE engine does:
            // a free-table create writes exactly the file it was given.
            // Applications stage work tables outside the data directory all the
            // time (the ERP that reported this builds scratch copies under the
            // TEMP drive and reads them back by the same absolute name), and
            // folding those produced a path whose intermediate directories
            // nobody creates -- the create failed and the caller could never
            // find its table.
            //
            // The fold below still applies when the parent does NOT exist:
            // that is the case the guard was written for, a client passing a
            // path from its own working directory that means nothing on the
            // server, and re-rooting it keeps the table inside the directory
            // the server owns. A path rooted directly at a drive
            // ("C:\STRAY.DBF") is excluded too: the drive root always exists,
            // but writing a table there is never a deliberate location -- it
            // is the signature of a client that built the name from its own
            // root, and abi_create_table_test pins that it folds.
            std::error_code ec;
            const fs::path parent = rel.parent_path();
            const bool parent_is_drive_root = (parent == parent.root_path());
            if (path_is_inside(data_dir_, rel) ||
                (!parent_is_drive_root && fs::is_directory(parent, ec))) {
                std::string resolved =
                    platform::resolve_case_insensitive(rel.string());
                log_resolved(resolved, path_is_inside(data_dir_, rel)
                                           ? "JAILED"
                                           : "(client)");
                return resolved;
            }
            rel = rel.relative_path();
        }
    }
    fs::path full = fs::path(data_dir_) / rel;
    // Auto-append .dbf when the caller (typically rddads / Clipper)
    // passed a bare table alias without an extension.  If .dbf does not
    // exist but .adt does, open as ADT (e.g. SQL SELECT FROM <alias>
    // against an ADT-only data directory).
    if (!full.has_extension()) {
        std::error_code ec;
        // Probe in the order the caller's type implies: an ADT statement
        // looks for <name>.adt first, everything else keeps the historical
        // .dbf-then-.adt order. Without this an ADT connection that also
        // holds a stale same-named .dbf would silently open the DBF.
        const bool adt_first = (type == engine::TableType::Adt);
        fs::path dbf_cand = full; dbf_cand.replace_extension(".dbf");
        fs::path adt_cand = full; adt_cand.replace_extension(".adt");
        fs::path dat_cand = full; dat_cand.replace_extension(".dat");
        const fs::path& first  = adt_first ? adt_cand : dbf_cand;
        const fs::path& second = adt_first ? dbf_cand : adt_cand;
        if (fs::exists(first, ec)) {
            full = first;
        } else if (fs::exists(second, ec)) {
            full = second;
        } else if (fs::exists(dat_cand, ec)) {
            // RusSoft/Zerus (and other legacy ERPs) keep every table --
            // ADT or DBF alike -- under a literal .dat extension on disk.
            // Probe it last so a "real" .dbf/.adt sibling still wins;
            // align_type_with_file() below sniffs the header to set `type`.
            full = dat_cand;
        } else {
            full = dbf_cand;   // report the .dbf name when nothing exists
        }
        if (full.extension() == ".adt") type = engine::TableType::Adt;
    } else {
        // Override type from extension when the caller used the default
        // (Cdx) but the file is explicitly named with a different type's
        // extension (e.g. "SELECT * FROM tbl.adt" should use the ADT driver).
        std::string ext = full.extension().string();
        for (auto& ch : ext) ch = static_cast<char>(
            std::tolower(static_cast<unsigned char>(ch)));
        if (ext == ".adt" && type == engine::TableType::Cdx)
            type = engine::TableType::Adt;
        else if (ext == ".ntx" && type == engine::TableType::Cdx)
            type = engine::TableType::Ntx;
    }
    std::string resolved = platform::resolve_case_insensitive(full.string());
    // The extension rules above only cover names that spell their type
    // (.adt / .ntx). A table named with any other extension -- .dat is
    // what the RusSoft ERP uses for every table, ADT or DBF alike --
    // reaches here with whatever type the caller guessed. Let the file's
    // own header have the last word, but never on a create: there the
    // path names a file that does not exist yet (and if it does, the
    // caller is deliberately overwriting it with a chosen format).
    if (!for_create) align_type_with_file(resolved, type);
    log_resolved(resolved, "JAILED");
    return resolved;
}

std::uint16_t Connection::table_cache_mode_(const std::string& relative_path) const {
    // RCB 06/28/2026: Only DD-managed tables participate in table caching.
    // Free tables remain unchanged because the cache policy is stored as a DD
    // table property, and because per-user/reference-table tuning belongs to
    // the dictionary rather than a process-wide file-name heuristic.
    if (!dd_.has_value()) return 0;
    auto parse_mode = [](const std::string& raw) -> std::uint16_t {
        if (raw.empty()) return 0;
        try {
            auto v = std::stoul(raw);
            return v <= 2 ? static_cast<std::uint16_t>(v) : 0;
        } catch (...) {
            return 0;
        }
    };
    auto find_prop = [&](const std::string& alias) -> std::uint16_t {
        // RCB 06/28/2026: ADS_DD_TABLE_CACHING is 217 in OpenADS' ACE
        // header; keep the lookup local to avoid pulling ace.h into session.
        return parse_mode(dd_->get_table_property(alias, 217));
    };

    if (dd_->has_alias(relative_path)) return find_prop(relative_path);
    const auto resolved = dd_->resolve(relative_path);
    auto norm = [](std::string s) {
        std::replace(s.begin(), s.end(), '\\', '/');
        for (auto& ch : s) ch = static_cast<char>(
            std::tolower(static_cast<unsigned char>(ch)));
        return s;
    };
    const auto wanted = norm(resolved);
    for (const auto& kv : dd_->tables()) {
        if (norm(kv.second) == wanted) return find_prop(kv.first);
    }
    return 0;
}

// Return a table already open on this connection that resolves to the
// same physical file as `relative_path`, or nullptr if none is open.
// Used by RI enforcement so a cascade/restrict acts on the very buffer
// the application already holds, instead of opening a second instance
// of the same file (two instances race on the OS file cache and on
// share-mode locks, producing intermittent missed cascades).
engine::Table* Connection::find_open_table(const std::string& relative_path,
                                           engine::TableType  type) {
    std::string resolved = resolve_table_file(relative_path, type);
    for (auto& [h, holder] : tables_) {
        (void)h;
        if (holder && holder->path() == resolved) return holder.get();
    }
    return nullptr;
}

util::Result<Handle> Connection::open_table(const std::string& relative_path,
                                            engine::TableType  type,
                                            engine::OpenMode   mode,
                                            engine::LockingMode locking) {
    namespace fs = std::filesystem;
    auto resolved = resolve_table_file(relative_path, type);

    auto t = engine::Table::open(resolved, type, mode, locking);
    if (!t) return t.error();

    auto holder = std::make_unique<engine::Table>(std::move(t).value());

    // M11.2 — propagate the connection's encryption key to the CDX
    // driver whenever the session has a password set.  Full-table
    // encryption needs it on open; record-level encryption needs it
    // before AdsEncryptRecord can touch a plain table.
    if (encryption_key_set_) {
        if (auto* cdx = dynamic_cast<
                openads::drivers::cdx::CdxDriver*>(holder->driver())) {
            std::array<std::uint8_t, 32> key = encryption_key_legacy_;
            if (cdx->header_version() == 0xC4) {
                key = encryption_key_pbkdf2_;
            }
            (void)cdx->set_encryption_key(key);
        }
    }
    holder->set_owner(this);

    // Auto-attach a memo store if the table has M-fields or Binary fields.
    bool has_memo_field = false;
    for (std::uint16_t i = 0; i < holder->field_count(); ++i) {
        auto ftype = holder->field_descriptor(i).type;
        if (ftype == openads::drivers::DbfFieldType::Memo ||
            ftype == openads::drivers::DbfFieldType::Binary) {
            has_memo_field = true;
            break;
        }
    }
    if (has_memo_field) {
        fs::path stem = fs::path(resolved);
        auto memo_open_mode =
            (mode == engine::OpenMode::Read) ? openads::drivers::MemoOpenMode::ReadOnly
                                             : openads::drivers::MemoOpenMode::Shared;
        if (type == engine::TableType::Adt) {
            stem.replace_extension(".adm");
            std::error_code ec;
            if (fs::exists(stem, ec)) {
                auto m = std::make_unique<openads::drivers::adm::AdmMemo>();
                if (m->open(stem.string(), memo_open_mode)) {
                    holder->attach_memo(std::move(m));
                }
            }
        } else if (type == engine::TableType::Ntx) {
            stem.replace_extension(".dbt");
            std::error_code ec;
            if (fs::exists(stem, ec)) {
                auto m = std::make_unique<openads::drivers::dbt::DbtMemo>();
                if (m->open(stem.string(), memo_open_mode)) {
                    holder->attach_memo(std::move(m));
                }
            }
        } else {
            stem.replace_extension(".fpt");
            std::error_code ec;
            if (fs::exists(stem, ec)) {
                auto m = std::make_unique<openads::drivers::fpt::FptMemo>();
                if (m->open(stem.string(), memo_open_mode)) {
                    holder->attach_memo(std::move(m));
                }
            }
        }
    }

    if (auto cache_mode = table_cache_mode_(relative_path); cache_mode != 0) {
        if (auto cr = holder->enable_cache(cache_mode); !cr) return cr.error();
    }

    Handle h = next_table_handle_++;
    if (tx_.active()) {
        holder->attach_tx(&tx_, static_cast<engine::Tx::TableId>(h));
        tx_.register_table(static_cast<engine::Tx::TableId>(h), relative_path);
    }
    tables_.emplace(h, std::move(holder));
    table_paths_.emplace(h, relative_path);
    return h;
}

util::Result<Handle> Connection::adopt_table(engine::Table table,
                                             const std::string& relative_path) {
    auto holder = std::make_unique<engine::Table>(std::move(table));
    holder->set_owner(this);
    Handle h = next_table_handle_++;
    if (tx_.active()) {
        holder->attach_tx(&tx_, static_cast<engine::Tx::TableId>(h));
        tx_.register_table(static_cast<engine::Tx::TableId>(h), relative_path);
    }
    tables_.emplace(h, std::move(holder));
    table_paths_.emplace(h, relative_path);
    return h;
}

util::Result<void> Connection::begin_tx() {
    // M11.3 — nested BEGIN simply increments depth without starting
    // a fresh inner transaction. Only the outermost call activates
    // tx_ and writes the begin record to the log.
    if (tx_.active()) {
        ++tx_nest_depth_;
        return {};
    }
    // Settle coalesced dirty records BEFORE activating the tx: edits made
    // before BEGIN were never journaled under write-through, so they must
    // not become undoable now. Writeback with tx_ inactive skips
    // note_before_image entirely.
    if (auto s = settle_dirty_tables_(); !s) return s.error();
    if (!tx_log_.is_open()) {
        // Data directory is not writable (see Connection::open) — the
        // journal could not be created, so transactions are unavailable
        // on this connection.
        return util::Error{5000, 0,
            "transactions unavailable: tx journal not writable in data "
            "directory", data_dir_};
    }
    std::uint64_t tid = next_tx_id_++;
    tx_.activate(tid, &tx_log_);
    tx_nest_depth_ = 1;
    if (auto r = tx_log_.append_begin(tid); !r) return r.error();
    for (auto& [h, holder] : tables_) {
        holder->attach_tx(&tx_, static_cast<engine::Tx::TableId>(h));
        auto pit = table_paths_.find(h);
        if (pit != table_paths_.end()) {
            tx_.register_table(static_cast<engine::Tx::TableId>(h), pit->second);
        }
    }
    return {};
}

util::Result<void> Connection::settle_dirty_tables_() {
    for (auto& [h, holder] : tables_) {
        if (!holder->record_dirty()) continue;
        if (auto r = holder->commit_dirty_record(); !r) return r.error();
    }
    return {};
}

util::Result<void> Connection::commit_tx() {
    if (!tx_.active()) {
        return util::Error{5000, 0, "no active transaction", ""};
    }
    // M11.3 — nested COMMIT just decrements depth; only the outermost
    // commit flushes pending writes and truncates the log.
    if (tx_nest_depth_ > 1) {
        --tx_nest_depth_;
        return {};
    }
    // Settle coalesced dirty records first: their writeback is what
    // journals the before-image / update op, and it must land in the
    // log before the commit record for crash recovery to be sound.
    if (auto s = settle_dirty_tables_(); !s) return s.error();
    if (auto r = tx_log_.append_commit(tx_.id()); !r) return r.error();
    std::unordered_set<Handle> touched;
    for (const auto& op : tx_.ops()) {
        touched.insert(static_cast<Handle>(op.table));
    }
    for (auto& [h, holder] : tables_) {
        holder->detach_tx();
        if (touched.count(h) == 0) continue;
        (void)holder->flush();
    }
    tx_.clear();
    tx_nest_depth_ = 0;
    (void)tx_log_.truncate();
    return {};
}

util::Result<void> Connection::rollback_tx() {
    if (!tx_.active()) {
        return util::Error{5000, 0, "no active transaction", ""};
    }
    // Settle coalesced dirty records first so their before-images reach
    // the journal before we walk it; otherwise an edit that never hit
    // disk would survive the rollback in the record buffer (and be
    // flushed by the next navigation — silent data loss).
    if (auto s = settle_dirty_tables_(); !s) return s.error();
    std::unordered_set<Handle> touched;
    tx_.for_each_before_image(
        [&](const engine::Tx::RecordKey& k,
            const std::vector<std::uint8_t>&) {
            touched.insert(static_cast<Handle>(k.table));
        });
    tx_.for_each_append([&](const engine::Tx::RecordKey& k) {
        touched.insert(static_cast<Handle>(k.table));
    });
    std::optional<util::Error> rollback_err;
    tx_.for_each_before_image(
        [&](const engine::Tx::RecordKey& k,
            const std::vector<std::uint8_t>& bytes) {
            if (rollback_err) return;
            auto it = tables_.find(static_cast<Handle>(k.table));
            if (it == tables_.end()) return;
            if (auto r = it->second->apply_tx_rollback(k.recno, bytes.data(),
                                                       bytes.size()); !r) {
                rollback_err = r.error();
            }
        });
    // Undo appends: physically pop the trailing rows (de-indexing them)
    // so RECCOUNT returns to its pre-transaction value, the way ADS
    // leaves no trace of a rolled-back AppendRecord. Group by table so
    // each table peels its rows high-to-low in one pass.
    std::unordered_map<Handle, std::vector<std::uint32_t>> appends_by_table;
    tx_.for_each_append([&](const engine::Tx::RecordKey& k) {
        appends_by_table[static_cast<Handle>(k.table)].push_back(k.recno);
    });
    for (auto& [h, recnos] : appends_by_table) {
        auto it = tables_.find(h);
        if (it == tables_.end()) continue;
        (void)it->second->rollback_appends(std::move(recnos));
    }
    if (auto r = tx_log_.append_abort(tx_.id()); !r) return r.error();
    for (auto& [h, holder] : tables_) {
        holder->detach_tx();
        if (touched.count(h) == 0) continue;
        (void)holder->refresh_record_buffer();
        (void)holder->flush();
    }
    tx_.clear();
    // M11.3 — rollback aborts every nested level in one shot, matching
    // ADS / SQL Server semantics where an inner ROLLBACK kills the
    // entire transaction regardless of nesting depth.
    tx_nest_depth_ = 0;
    (void)tx_log_.truncate();
    return {};
}

util::Result<void>
Connection::create_savepoint(const std::string& name) {
    if (!tx_.active()) {
        return util::Error{5000, 0, "no active transaction", ""};
    }
    // Settle coalesced dirty records so the savepoint marks a journal
    // position that already includes every edit made so far (a pending
    // buffer has no journal entry to roll back to).
    if (auto s = settle_dirty_tables_(); !s) return s.error();
    tx_.create_savepoint(name);
    return {};
}

util::Result<void>
Connection::rollback_to_savepoint(const std::string& name) {
    if (!tx_.active()) {
        return util::Error{5000, 0, "no active transaction", ""};
    }
    // Settle coalesced dirty records first: their writeback appends the
    // journal op (before-image = current disk state) that the undo walk
    // below reverts, restoring the exact state the savepoint marked.
    if (auto s = settle_dirty_tables_(); !s) return s.error();
    std::size_t idx = tx_.savepoint_index(name);
    if (idx == static_cast<std::size_t>(-1)) {
        return util::Error{5000, 0, "savepoint not found", name};
    }
    const auto& ops = tx_.ops();
    // Collect the appends made after the savepoint, per table. They are
    // de-indexed and physically removed first (using their current
    // bytes for the index keys), so RECCOUNT drops just like a full
    // rollback — and so an in-place update op on the same row isn't
    // written back over a row we're about to delete.
    std::unordered_map<Handle, std::vector<std::uint32_t>> appends_by_table;
    std::unordered_set<std::uint64_t> appended_keys;
    auto rk = [](Handle h, std::uint32_t r) {
        return (static_cast<std::uint64_t>(h) << 32) | r;
    };
    for (std::size_t i = idx; i < ops.size(); ++i) {
        const auto& op = ops[i];
        if (!op.is_append) continue;
        appends_by_table[static_cast<Handle>(op.table)].push_back(op.recno);
        appended_keys.insert(rk(static_cast<Handle>(op.table), op.recno));
    }
    for (auto& [h, recnos] : appends_by_table) {
        auto it = tables_.find(h);
        if (it != tables_.end()) {
            (void)it->second->rollback_appends(std::move(recnos));
        }
    }
    // Revert in-place updates newest-first, skipping rows that were
    // appended after the savepoint (already physically gone).
    for (std::size_t i = ops.size(); i > idx; --i) {
        const auto& op = ops[i - 1];
        if (op.is_append) continue;
        if (appended_keys.count(rk(static_cast<Handle>(op.table), op.recno))) {
            continue;
        }
        auto it = tables_.find(static_cast<Handle>(op.table));
        if (it == tables_.end()) continue;
        auto* drv = it->second->driver();
        if (!drv) continue;
        (void)drv->write_record_raw(op.recno,
                                    op.before.data(), op.before.size());
    }
    tx_.truncate_ops_to(idx);
    std::unordered_set<Handle> touched;
    for (const auto& op : tx_.ops()) {
        touched.insert(static_cast<Handle>(op.table));
    }
    for (auto& [h, holder] : tables_) {
        if (touched.count(h) == 0) continue;
        (void)holder->refresh_record_buffer();
    }
    return {};
}

util::Result<void>
Connection::release_savepoint(const std::string& name) {
    // M11.3 — drop the named savepoint marker, keeping its operations
    // as part of the outer transaction. Standard SQL "RELEASE
    // SAVEPOINT" semantics.
    if (!tx_.active()) {
        return util::Error{5000, 0, "no active transaction", ""};
    }
    if (!tx_.release_savepoint(name)) {
        return util::Error{5000, 0, "savepoint not found", name};
    }
    return {};
}

util::Result<void> Connection::recover_orphan_tx_() {
    auto recs_r = tx_log_.read_all();
    if (!recs_r) return recs_r.error();
    auto recs = std::move(recs_r).value();
    if (recs.empty()) return {};

    // Group records by tx_id and find which transactions ended.
    std::unordered_set<std::uint64_t> ended;
    for (const auto& r : recs) {
        if (r.type == engine::TxRecordType::Commit ||
            r.type == engine::TxRecordType::Abort) {
            ended.insert(r.tx_id);
        }
    }

    // For each orphan tx, walk its UPDATEs in reverse and write back
    // the before-image directly through the driver. Open each
    // referenced table on demand.
    std::unordered_map<std::string, std::unique_ptr<engine::Table>> opened;
    auto open_for = [&](const std::string& path) -> engine::Table* {
        auto it = opened.find(path);
        if (it != opened.end()) return it->second.get();
        std::filesystem::path full =
            std::filesystem::path(data_dir_) / path;
        auto resolved = platform::resolve_case_insensitive(full.string());
        // Detect type by extension. For now any non-NTX is treated as CDX.
        auto type = engine::TableType::Cdx;
        if (resolved.size() >= 4 &&
            (resolved.substr(resolved.size() - 4) == ".ntx")) {
            type = engine::TableType::Ntx;
        }
        auto t = engine::Table::open(resolved, type, engine::OpenMode::Shared);
        if (!t) return nullptr;
        auto h = std::make_unique<engine::Table>(std::move(t).value());
        engine::Table* raw = h.get();
        opened.emplace(path, std::move(h));
        return raw;
    };

    // Undo orphan UPDATE records in reverse log order (restore before-images).
    std::vector<const engine::TxRecord*> orphan_updates;
    for (auto it = recs.rbegin(); it != recs.rend(); ++it) {
        if (it->type == engine::TxRecordType::Update &&
            ended.find(it->tx_id) == ended.end()) {
            orphan_updates.push_back(&*it);
        }
    }
    for (const engine::TxRecord* up : orphan_updates) {
        // Skip records that a previous (interrupted) recovery pass has
        // already applied. lsn_map_ tracks the highest LSN applied per
        // (table_path, recno); any record whose LSN is <= the stored
        // value is idempotent and can be skipped.
        if (lsn_map_.get(up->update.table_path, up->update.recno) >= up->lsn) {
            continue;
        }
        engine::Table* t = open_for(up->update.table_path);
        if (!t || !t->driver()) continue;
        if (!up->update.before.empty()) {
            (void)t->driver()->write_record_raw(
                up->update.recno,
                up->update.before.data(),
                up->update.before.size());
            lsn_map_.put(up->update.table_path, up->update.recno, up->lsn);
        }
    }

    // Undo orphan APPEND records by marking the appended rows deleted.
    // We de-duplicate by (table, recno) so that if the record was also
    // updated within the same orphan tx, the UPDATE undo above already
    // handled the data bytes — we only need to set the delete flag here.
    std::unordered_set<std::string> seen_appends; // "path:recno"
    for (const auto& r : recs) {
        if (r.type == engine::TxRecordType::Append &&
            ended.find(r.tx_id) == ended.end()) {
            std::string key = r.update.table_path + ":" +
                              std::to_string(r.update.recno);
            if (!seen_appends.insert(key).second) continue;
            engine::Table* t = open_for(r.update.table_path);
            if (!t || !t->driver()) continue;
            auto rec = t->driver()->read_record_raw(r.update.recno);
            if (!rec) continue;
            auto buf = std::move(rec).value();
            openads::drivers::set_record_deleted(buf.data(), buf.size(), true);
            (void)t->driver()->write_record_raw(
                r.update.recno, buf.data(), buf.size());
        }
    }
    // Append ABORT for each orphan tx so the log is well-formed.
    std::unordered_set<std::uint64_t> orphan_ids;
    for (const auto& r : recs) {
        if (ended.find(r.tx_id) == ended.end()) orphan_ids.insert(r.tx_id);
    }
    for (auto id : orphan_ids) (void)tx_log_.append_abort(id);

    // Flush any open tables, persist the lsn_map, then truncate the
    // log. If the process crashes between the lsn_map flush and the
    // log truncate, the next recovery pass will see the same orphan
    // records, look up their LSN in the lsn_map, and skip the redo —
    // making the recovery path crash-safe across multiple iterations.
    for (auto& [_, t] : opened) (void)t->flush();
    (void)lsn_map_.flush();
    (void)tx_log_.truncate();
    // Once the log is empty, the lsn_map's contents are no longer
    // needed for correctness; drop them so the sidecar does not grow
    // unbounded across the connection's lifetime.
    lsn_map_.clear();
    (void)lsn_map_.flush();
    return {};
}

void Connection::close_table(Handle h) {
    tables_.erase(h);
    table_paths_.erase(h);
}

void Connection::close_table_ptr(const engine::Table* t) {
    for (auto it = tables_.begin(); it != tables_.end(); ++it) {
        if (it->second.get() == t) {
            it->second->detach_tx();
            table_paths_.erase(it->first);
            tables_.erase(it);
            return;
        }
    }
}

engine::Table* Connection::lookup_table(Handle h) {
    auto it = tables_.find(h);
    if (it == tables_.end()) return nullptr;
    return it->second.get();
}

namespace {

// FoxPro-style glob match with `*` (zero or more chars) and `?`
// (exactly one char). Comparison is ASCII case-insensitive so the
// Windows convention of case-blind matching against the on-disk
// `MIXED.DBF` keeps working when the caller asks for `*.dbf`.
bool glob_match(const char* pat, const char* str) {
    auto eq = [](unsigned char a, unsigned char b) {
        if (a >= 'A' && a <= 'Z') a = static_cast<unsigned char>(a + ('a' - 'A'));
        if (b >= 'A' && b <= 'Z') b = static_cast<unsigned char>(b + ('a' - 'A'));
        return a == b;
    };
    const char* p = pat;
    const char* s = str;
    const char* star_p = nullptr;
    const char* star_s = nullptr;
    while (*s) {
        if (*p == '*') {
            star_p = ++p;
            star_s = s;
            if (*p == '\0') return true;
        } else if (*p == '?' || eq(static_cast<unsigned char>(*p),
                                   static_cast<unsigned char>(*s))) {
            ++p;
            ++s;
        } else if (star_p) {
            p = star_p;
            s = ++star_s;
        } else {
            return false;
        }
    }
    while (*p == '*') ++p;
    return *p == '\0';
}

}  // namespace

util::Result<std::pair<Connection::TableFind*, std::string>>
Connection::find_first_table(const std::string& mask) {
    namespace fs = std::filesystem;
    auto find = std::make_unique<TableFind>();

    std::error_code ec;
    if (fs::exists(data_dir_, ec) && fs::is_directory(data_dir_, ec)) {
        for (auto& entry : fs::directory_iterator(data_dir_, ec)) {
            if (!entry.is_regular_file(ec)) continue;
            std::string name = entry.path().filename().string();
            if (glob_match(mask.c_str(), name.c_str())) {
                find->matches.push_back(std::move(name));
            }
        }
    }

    if (find->matches.empty()) {
        return util::Error{openads::AE_NO_FILE_FOUND, 0,
                           "no matching file", mask};
    }

    std::sort(find->matches.begin(), find->matches.end());
    std::string first = find->matches.front();
    find->cursor = 1;
    TableFind* raw = find.get();
    finds_.push_back(std::move(find));
    return std::make_pair(raw, std::move(first));
}

util::Result<std::string>
Connection::find_next_table(TableFind* find) {
    if (find == nullptr) {
        return util::Error{openads::AE_INTERNAL_ERROR, 0,
                           "invalid find handle", ""};
    }
    if (find->cursor >= find->matches.size()) {
        return util::Error{openads::AE_NO_FILE_FOUND, 0,
                           "no more files", ""};
    }
    return find->matches[find->cursor++];
}

util::Result<void>
Connection::find_close(TableFind* find) {
    if (find == nullptr) {
        return util::Error{openads::AE_INTERNAL_ERROR, 0,
                           "invalid find handle", ""};
    }
    auto it = std::find_if(finds_.begin(), finds_.end(),
        [&](const std::unique_ptr<TableFind>& p) {
            return p.get() == find;
        });
    if (it == finds_.end()) {
        return util::Error{openads::AE_INTERNAL_ERROR, 0,
                           "unknown find handle", ""};
    }
    finds_.erase(it);
    return {};
}

util::Result<std::vector<std::string>>
Connection::find_tables(const std::string& mask) {
    namespace fs = std::filesystem;
    std::vector<std::string> matches;
    std::error_code ec;
    if (fs::exists(data_dir_, ec) && fs::is_directory(data_dir_, ec)) {
        for (auto& entry : fs::directory_iterator(data_dir_, ec)) {
            if (!entry.is_regular_file(ec)) continue;
            std::string name = entry.path().filename().string();
            if (glob_match(mask.c_str(), name.c_str())) {
                matches.push_back(std::move(name));
            }
        }
    }
    std::sort(matches.begin(), matches.end());
    return matches;
}

bool Connection::owns_table_ptr(const engine::Table* t) const {
    for (auto& [_, holder] : tables_) {
        if (holder.get() == t) return true;
    }
    return false;
}

void Connection::set_encryption_password(const std::string& password) {
    // M11.2 — PBKDF2-HMAC-SHA256 for new tables (header 0xC4). Legacy
    // tables (0xC3) still use zero-padded password bytes.
    encryption_key_legacy_ = engine::derive_legacy_encryption_key(password);
    encryption_key_pbkdf2_ = engine::derive_encryption_key(password);
    encryption_key_       = encryption_key_pbkdf2_;
    encryption_key_set_   = true;
}

Connection::~Connection() {
    // M11.4 — free every registered procedure's DLL handle.
    for (auto& [_, proc] : procedures_) {
        platform::dll_close(proc.dll);
    }
}

// Returns the lowercase base-name of a path (no directory, no extension).
static std::string dll_basename_lower(const std::string& p) {
    namespace fs = std::filesystem;
    std::string stem = fs::path(p).stem().string();
    for (char& c : stem) c = static_cast<char>(std::tolower((unsigned char)c));
    return stem;
}

// Returns true if the base-name suggests an ACE-compatible DLL that might
// be either OpenADS or SAP Advantage (ace64 / ace32 / openace64 / openace32).
static bool is_ace_dll_name(const std::string& stem) {
    return stem == "ace64" || stem == "ace32"
        || stem == "openace64" || stem == "openace32";
}

util::Result<void>
Connection::register_procedure(const std::string& name,
                               const std::string& dll_path,
                               const std::string& symbol) {
    auto it = procedures_.find(name);
    if (it != procedures_.end()) {
        platform::dll_close(it->second.dll);
        procedures_.erase(it);
    }

    // For ACE-named DLLs, verify the candidate is OpenADS's own engine.
    // If ace64.dll is SAP's, also try openace64.dll as a fallback so that
    // stored procs referencing the legacy name can still work.
    std::string effective_path = dll_path;
    if (is_ace_dll_name(dll_basename_lower(dll_path))) {
        std::string desc = platform::dll_probe_ace(dll_path);
        if (desc.empty()) {
            // Not OpenADS (SAP DLL or not found). Try openace64 fallback.
            namespace fs = std::filesystem;
            std::string fb = (fs::path(dll_path).parent_path()
                              / "openace64.dll").string();
            std::string fb_desc = platform::dll_probe_ace(fb);
            if (!fb_desc.empty()) {
                effective_path = fb;
            } else {
                return util::Error{5004, 0,
                    "ACE DLL at the given path is either SAP Advantage "
                    "(which OpenADS cannot load as a dependency) or was "
                    "not found. Install openace64.dll alongside the server "
                    "binary or point the procedure to the correct path.",
                    dll_path};
            }
        }
    }

    auto h = platform::dll_load(effective_path);
    if (!h) return h.error();
    auto sym = platform::dll_symbol(h.value(), symbol);
    if (!sym) {
        platform::dll_close(h.value());
        return sym.error();
    }
    Procedure p;
    p.dll_path = dll_path;
    p.symbol   = symbol;
    p.dll      = h.value();
    p.fn       = reinterpret_cast<ExtProcFn>(sym.value());
    procedures_.emplace(name, std::move(p));
    return {};
}

bool Connection::has_procedure(const std::string& name) const {
    return procedures_.find(name) != procedures_.end();
}

util::Result<std::string>
Connection::execute_procedure(const std::string& name,
                              const std::string& packed_args) {
    auto it = procedures_.find(name);
    if (it == procedures_.end()) {
        // Name rides in the MESSAGE (last_error drops the detail field);
        // the SQL error envelope extracts it after the colon.
        return util::Error{5000, 0, "procedure not registered: " + name,
                           name};
    }
    if (!it->second.fn) {
        return util::Error{5000, 0, "procedure has null fn pointer", name};
    }
    constexpr std::size_t cap = 1024;
    std::string out;
    out.resize(cap, '\0');
    int rc = it->second.fn(packed_args.c_str(), out.data(), cap);
    if (rc != 0) {
        return util::Error{rc, 0,
                           "procedure returned non-zero", name};
    }
    auto z = out.find('\0');
    if (z != std::string::npos) out.resize(z);
    return out;
}

} // namespace openads::session
