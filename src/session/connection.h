#pragma once

#include "engine/data_dict.h"
#include "engine/lsn_map.h"
#include "engine/table.h"
#include "engine/tx.h"
#include "engine/tx_log.h"
#include "platform/dll.h"
#include "session/handle_registry.h"
#include "util/result.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
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
    bool owns_table_ptr(const engine::Table* t) const;

    // M11.7 — string-compare collation. `Binary` (default) compares
    // raw bytes; `NoCase` lowercases ASCII A-Z before compare.
    enum class Collation { Binary, NoCase };
    void       set_collation(Collation c) noexcept { collation_ = c; }
    Collation  collation() const noexcept { return collation_; }

    // DD authentication — set after credential validation in AdsConnect60.
    void               set_username(std::string name) { username_ = std::move(name); }
    const std::string& username() const noexcept { return username_; }

private:
    util::Result<void> recover_orphan_tx_();
    std::string resolve_table_file(const std::string& relative_path,
                                   engine::TableType&  type);

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

    // M11.2 — encryption key derived from the connection password.
    std::array<std::uint8_t, 32>                               encryption_key_{};
    bool                                                       encryption_key_set_ = false;
    // M11.7 — string compare collation (default = byte-exact).
    Collation                                                  collation_ =
        Collation::Binary;
    // Authenticated username (empty = anonymous / unauthenticated).
    std::string                                                username_;
    // Per-connection trigger disable flag (sp_DisableTriggers / sp_EnableTriggers
    // with CURRENT USER scope). Not persisted; reset when the connection closes.
    bool                                                       triggers_disabled_ = false;

public:
    // Trigger disable / enable for this connection (current-user scope).
    void set_triggers_disabled(bool v) noexcept { triggers_disabled_ = v; }
    bool triggers_disabled()    const noexcept  { return triggers_disabled_; }

    ~Connection();
};

} // namespace openads::session
