#pragma once

#include "engine/data_dict.h"
#include "engine/lsn_map.h"
#include "engine/repl_catalog.h"
#include "engine/repl_queue.h"
#include "engine/table.h"
#include "engine/tx.h"
#include "engine/tx_log.h"
#include "platform/dll.h"
#include "session/handle_registry.h"
#include "util/result.h"

#include <array>
#include <cctype>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace openads::session {

class Connection {
public:
    Connection() = default;
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    Connection(Connection&&) noexcept = default;
    Connection& operator=(Connection&&) noexcept = default;

    static util::Result<Connection> open(const std::string& data_dir);

    util::Result<Handle>
        open_table(const std::string& relative_path,
                   engine::TableType  type,
                   engine::OpenMode   mode = engine::OpenMode::Shared,
                   engine::LockingMode locking = engine::LockingMode::Compatible);
    util::Result<Handle> adopt_table(engine::Table table,
                                     const std::string& relative_path);

    void close_table(Handle h);
    void close_table_ptr(const engine::Table* t);

    engine::Table* lookup_table(Handle h);

    // Return a table already open on this connection that maps to the
    // same physical file as `relative_path`, or nullptr. RI enforcement
    // uses this to act on the application's open buffer rather than a
    // second instance of the same file.
    engine::Table* find_open_table(const std::string& relative_path,
                                   engine::TableType  type =
                                       engine::TableType::Cdx);

    const std::string& data_dir()  const noexcept { return data_dir_; }
    // Full path to the .add DD file if one was opened (empty otherwise).
    const std::string& dd_path()   const noexcept { return dd_path_; }

    // Transaction surface (M5).
    util::Result<void> begin_tx();
    util::Result<void> commit_tx();
    util::Result<void> rollback_tx();
    util::Result<void> create_savepoint(const std::string& name);
    util::Result<void> rollback_to_savepoint(const std::string& name);
    util::Result<void> release_savepoint(const std::string& name);
    bool               in_tx() const noexcept { return tx_.active(); }
    int                tx_nest_depth() const noexcept { return tx_nest_depth_; }

    // Data Dictionary surface (M6).
    bool has_dd() const noexcept { return dd_.has_value(); }
    engine::DataDict* dd() noexcept {
        return dd_.has_value() ? &*dd_ : nullptr;
    }

    // Table directory iteration (M9.12). Glob mask is matched against
    // each entry of `data_dir_` with `*` and `?` wildcards, case
    // insensitive on Windows. Returns AE_NO_FILE_FOUND if nothing
    // matches at the start; otherwise emits the first hit and a handle
    // the caller threads into find_next_table / find_close.
    struct TableFind {
        std::vector<std::string> matches;
        std::size_t              cursor = 0;
    };

    util::Result<std::pair<TableFind*, std::string>>
        find_first_table(const std::string& mask);
    util::Result<std::string> find_next_table(TableFind* find);
    util::Result<void>        find_close(TableFind* find);

    // M12.33 — return all matching file names at once (for wire protocol).
    util::Result<std::vector<std::string>>
        find_tables(const std::string& mask);

    // M11.4 — AEP host. Procedures are registered against a
    // connection (DLL handle owned here, freed at disconnect) and
    // invoked through execute_procedure. Procedure ABI:
    //   extern "C" int proc(const char* args,
    //                       char* out_buf, std::size_t out_cap);
    // `args` is a 0x1F-separated UTF-8 string; `out_buf` is the
    // procedure's writable result buffer; the return value lands as
    // the proc's status code (0 on success).
    using ExtProcFn =
        int (*)(const char* args, char* out_buf, std::size_t out_cap);
    struct Procedure {
        std::string         dll_path;
        std::string         symbol;
        platform::DllHandle dll;
        ExtProcFn           fn = nullptr;
    };

    util::Result<void>
        register_procedure(const std::string& name,
                           const std::string& dll_path,
                           const std::string& symbol);
    util::Result<std::string>
        execute_procedure(const std::string& name,
                          const std::string& packed_args);
    bool has_procedure(const std::string& name) const;

    // M11.2 — encryption password used by the OpenADS-encrypted DBF
    // variant (header byte 0xC3, AES-256-CTR per record). The 32-byte
    // key is derived deterministically from the password and applied
    // to any encrypted table opened through this connection.
    void set_encryption_password(const std::string& password);
    bool has_encryption_key() const noexcept { return encryption_key_set_; }
    const std::array<std::uint8_t, 32>&
        encryption_key() const noexcept { return encryption_key_; }
    const std::array<std::uint8_t, 32>&
        encryption_key_legacy() const noexcept {
        return encryption_key_legacy_;
    }
    const std::array<std::uint8_t, 32>&
        encryption_key_pbkdf2() const noexcept {
        return encryption_key_pbkdf2_;
    }

    // Per-connection SET DELETED visibility (default: show deleted).
    bool show_deleted() const noexcept { return show_deleted_; }
    void set_show_deleted(bool v) noexcept { show_deleted_ = v; }
    bool owns_table_ptr(const engine::Table* t) const;

    // Legacy ERP path mode (server --legacy-paths): resolve_table_file
    // routes client-absolute paths through platform::resolve_client_path
    // (case-insensitive, drive-letter-ignoring prefix strip against the
    // connection's data directory) instead of the plain root fold, so
    // remote sessions mirror what the network Connect jail accepted.
    bool legacy_paths() const noexcept { return legacy_paths_; }
    void set_legacy_paths(bool v) noexcept { legacy_paths_ = v; }

    // Set on connections owned by openads_serverd (a remote session).
    // Remote is safe storage: never probe a client-absolute host path
    // (the SAP free-table OPEN exception). Legacy remount under --data
    // still applies when --legacy-paths is on; otherwise the open fails
    // with the normal table-not-found / file-not-found RDD error.
    bool remote_server() const noexcept { return remote_server_; }
    void set_remote_server(bool v) noexcept { remote_server_ = v; }

    // 6-char [0-9A-Z] serial assigned at Connection::open. Shared by
    // every audit line of this connection.
    const std::string& connection_serial() const noexcept {
        return conn_serial_;
    }

    // Application ID set via sp_SetApplicationID / read back with
    // sp_GetApplicationID. Free-form client-supplied label; empty until set.
    const std::string& application_id() const noexcept { return app_id_; }
    void set_application_id(std::string v) { app_id_ = std::move(v); }

    // M11.7 — string-compare collation. `Binary` (default) compares
    // raw bytes; `NoCase` lowercases ASCII A-Z before compare.
    enum class Collation { Binary, NoCase };
    void       set_collation(Collation c) noexcept { collation_ = c; }
    Collation  collation() const noexcept { return collation_; }

    // Replication state accessors (Phase 1).
    engine::ReplQueue&       repl_queue()       noexcept { return repl_queue_; }
    engine::ReplCatalog&     repl_catalog()     noexcept { return repl_catalog_; }
    bool                     repl_queue_open() const noexcept { return repl_queue_open_; }
    void                     set_repl_queue_open(bool v) noexcept { repl_queue_open_ = v; }
    std::uint64_t            tx_id() const noexcept { return next_tx_id_ - 1; }

    // M12.34 — server origin identity for replication loop prevention.
    void                     set_origin_id(const std::string& id) noexcept { origin_id_ = id; }
    const std::string&       origin_id() const noexcept { return origin_id_; }

    // OEM national collation for CDX/NTX index keys (NTXPL852, PL852, …).
    // nullptr = raw byte order. Set via AdsSetCollation.
    const std::uint8_t* oem_sort_table() const noexcept {
        return oem_sort_;
    }
    void set_oem_sort_table(const std::uint8_t* tab) noexcept {
        oem_sort_ = tab;
    }

    // OEM upper-case table for national collations (parallel to sort table).
    // Used for UPPER() in index key expressions so Polish NTXPL852 etc. upper
    // matches what Harbour/Clipper produce under the same OEM charset.
    const std::uint8_t* oem_upper_table() const noexcept {
        return oem_upper_;
    }
    void set_oem_upper_table(const std::uint8_t* tab) noexcept {
        oem_upper_ = tab;
    }

    // DD authentication: set after credential validation in AdsConnect60.
    // RCB 2026-06-27: Keep the connection username normalized so every
    // conn->username() permission check sees one canonical DD user name.
    void set_username(std::string name) {
        for (auto& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        username_ = std::move(name);
    }
    const std::string& username() const noexcept { return username_; }

    // Resolve a caller-supplied table name (DD alias, bare leaf, or path)
    // to the absolute on-disk file the driver opens. Public so the ACE
    // create path can land a new table in the very file a later
    // AdsOpenTable(name) will resolve to. `type` may be updated when the
    // extension implies a different driver than the caller's default.
    // `for_create` folds rooted paths into the data directory
    // unconditionally (new tables always land there); opens honor an
    // absolute path verbatim when the file already exists at it (SAP
    // opens free tables by full path even on a data-dir connection).
    std::string resolve_table_file(const std::string& relative_path,
                                   engine::TableType&  type,
                                   bool                for_create = false);

private:
    util::Result<void> recover_orphan_tx_();
    // GoCold every table with a coalesced dirty record. Field setters
    // defer writeback + before-image capture to commit_dirty_record();
    // tx boundary events (commit / rollback / savepoint) must settle
    // first so the journal sees the edits in the right order and no
    // pending buffer survives a rollback.
    util::Result<void> settle_dirty_tables_();
    std::uint16_t table_cache_mode_(const std::string& relative_path) const;
    std::string                                                data_dir_;
    std::string                                                dd_path_;   // full .add path if DD opened
    std::unordered_map<Handle, std::unique_ptr<engine::Table>> tables_;
    std::unordered_map<Handle, std::string>                    table_paths_;
    Handle                                                     next_table_handle_ = 1;
    std::vector<std::unique_ptr<TableFind>>                    finds_;

    engine::TxLog                                              tx_log_;
    engine::LsnMap                                             lsn_map_;
    engine::Tx                                                 tx_;
    std::uint64_t                                              next_tx_id_ = 1;
    // M11.3 — nested BEGIN/COMMIT depth. Outer BEGIN sets it to 1;
    // each nested BEGIN bumps it; each nested COMMIT decrements;
    // only the outermost commit triggers real flush + log truncate.
    int                                                        tx_nest_depth_ = 0;

    std::optional<engine::DataDict>                            dd_;

    // M11.4 — registered AEP procedures keyed by name (case-sensitive
    // for now). DLL handles freed in destructor.
    std::unordered_map<std::string, Procedure>                 procedures_;

    // M11.2 — encryption keys derived from the connection password.
    // legacy (0xC3 header) and PBKDF2 (0xC4 header).
    std::array<std::uint8_t, 32>                               encryption_key_{};
    std::array<std::uint8_t, 32>                               encryption_key_legacy_{};
    std::array<std::uint8_t, 32>                               encryption_key_pbkdf2_{};
    bool                                                       encryption_key_set_ = false;
    bool                                                       show_deleted_ = true;
    bool                                                       legacy_paths_ = false;
    bool                                                       remote_server_ = false;
    std::string                                                conn_serial_;
    std::uint32_t                                              next_entry_serial_ = 1;
    // Normalized RESOLVED paths already emitted on this connection.
    // open_table + find_open_table + index-bag resolve the same file
    // more than once; the audit trail keeps one line per file.
    std::unordered_set<std::string>                            logged_resolves_;
    std::unordered_set<std::string>                            logged_opens_;
    // M11.7 — string compare collation (default = byte-exact).
    Collation                                                  collation_ =
        Collation::Binary;
    const std::uint8_t*                                        oem_sort_ =
        nullptr;
    const std::uint8_t*                                        oem_upper_ =
        nullptr;
    // Authenticated username (empty = anonymous / unauthenticated).
    std::string                                                username_;
    // Per-connection trigger disable flag (sp_DisableTriggers / sp_EnableTriggers
    // with CURRENT USER scope). Not persisted; reset when the connection closes.
    bool                                                       triggers_disabled_ = false;
    // sp_SetApplicationID / sp_GetApplicationID label.
    std::string                                                app_id_;

    // Replication state (Phase 1: capture).
    engine::ReplQueue                                          repl_queue_;
    bool                                                       repl_queue_open_ = false;
    engine::ReplCatalog                                        repl_catalog_;
    std::string                                                origin_id_;  // M12.34

public:
    // Trigger disable / enable for this connection (current-user scope).
    void set_triggers_disabled(bool v) noexcept { triggers_disabled_ = v; }
    bool triggers_disabled()    const noexcept  { return triggers_disabled_; }

    ~Connection();
};

} // namespace openads::session
