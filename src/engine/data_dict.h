#pragma once

#include "util/result.h"

#include <array>
#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace openads::engine {

// Data Dictionary — stored in an ADT-compatible .add file with a companion
// .am memo file for JSON payloads.
//
// File layout (.add):
//   "Advantage Table" header (400 bytes) + 6 field descriptors (200 bytes each)
//   = 1600-byte header, followed by fixed-length records (342 bytes each).
//
// Record layout:
//   [0]        del flag: 0x04=active, 0x05=deleted
//   [1..4]     null bitmap (4 bytes, always zero)
//   [5..8]     OBJ_ID    (uint32 LE)
//   [9..12]    PARENT_ID (uint32 LE, reserved, always 0)
//   [13..32]   OBJ_TYPE  (CHAR 20, space-padded)
//   [33..232]  OBJ_NAME  (CHAR 200, space-padded)
//   [233..332] OBJ_KEY   (CHAR 100, space-padded) -- secondary lookup key
//   [333..341] OBJ_DATA  (Memo 9: uint32 block_no + uint32 data_len + 0x00)
//
// OBJ_TYPE values and OBJ_NAME / OBJ_KEY semantics:
//   Table     : OBJ_NAME=alias       OBJ_KEY=relative_path  JSON={pk,default_idx,comment,user_defined,validation_expr,validation_msg,auto_create,memo_block_size,caching,...}
//   Index     : OBJ_NAME=table_alias OBJ_KEY=index_path     JSON={comment}
//   User      : OBJ_NAME=username                           JSON={prop_*=value,...}
//   Group     : OBJ_NAME=groupname                          JSON={}
//   Member    : OBJ_NAME=username    OBJ_KEY=groupname      JSON={}
//   Link      : OBJ_NAME=alias                              JSON={path,user,pwd}
//   RI        : OBJ_NAME=ri_name                            JSON={parent,child,parent_tag,...}
//   DbProp    : OBJ_NAME=prop_key                           JSON={value}
//   FieldProp : OBJ_NAME=table       OBJ_KEY=field          JSON={required,default,rule,...}
//   Perm      : OBJ_NAME=obj_name    OBJ_KEY=grantee        JSON={obj_type,bitmask}
//   Trigger   : OBJ_NAME=table::name                        JSON=(trigger JSON blob)
//   Proc      : OBJ_NAME=proc_name                          JSON=(proc JSON blob)
//   Function  : OBJ_NAME=func_name                          JSON=(function JSON blob)
//   View      : OBJ_NAME=view_name                          JSON={sql,comment}
//
// Companion .am file uses ADM format (256-byte blocks, next_avail at header+20).

class DataDict {
public:
    DataDict() = default;

    static util::Result<DataDict> open(const std::string& path);

    // Build a fresh empty DD on disk.
    static util::Result<DataDict> create(const std::string& path);

    // ---- TABLE (M6) ------------------------------------------------------
    struct TableProps {
        std::string primary_key;    // tag name of primary key (ADS_DD_TABLE_PRIMARY_KEY=202)
        std::string default_index;  // tag name of default index (ADS_DD_TABLE_DEFAULT_INDEX=213)
        std::string comment;
        std::string user_defined;
        std::string validation_expr;
        std::string validation_msg;
        // RCB 06/27/2026: Persist SAP-style DD table properties so any
        // administration client can edit them and table-open paths can consume
        // the runtime properties that affect all clients.
        std::string auto_create;     // "0" or "1" (ADS_DD_TABLE_AUTO_CREATE=203)
        std::string memo_block_size; // decimal UNSIGNED16 (ADS_DD_TABLE_MEMO_BLOCK_SIZE=215)
        std::string caching;         // ADS_TABLE_CACHE_* value (ADS_DD_TABLE_CACHING=217)
        std::string encryption;      // "0" or "1" (ADS_DD_TABLE_ENCRYPTION=214)
        std::string permission_level;// decimal UNSIGNED16 (ADS_DD_TABLE_PERMISSION_LEVEL=216)
        std::string txn_free;        // "0" or "1" (ADS_DD_TABLE_TXN_FREE=218)
    };

    util::Result<void> add_table(const std::string& alias,
                                 const std::string& relative_path);
    util::Result<void> remove_table(const std::string& alias);

    std::string resolve(const std::string& alias_or_path) const;
    bool has_alias(const std::string& alias) const noexcept {
        return tables_.find(alias) != tables_.end();
    }
    const std::unordered_map<std::string, std::string>&
        tables() const noexcept { return tables_; }

    std::string get_table_property(const std::string& alias, int prop_code) const;
    void        set_table_property(const std::string& alias, int prop_code,
                                   const std::string& value);

    // ---- INDEX (M10.1) ---------------------------------------------------
    struct IndexEntry {
        std::string table_alias;
        std::string index_path;
        std::string comment;
        // RCB 07/16/2026: per-tag metadata so system.indexes matches SAP ADS
        // (one row per index TAG, not per file). Empty on legacy/file-only
        // entries; populated by import_dd (from SAP system.indexes) and by
        // OpenADS CREATE INDEX. Stored in the Index record's JSON, so old DDs
        // that lack these fields still load (the fields just come back empty).
        std::string tag_name;    // SAP Index Name
        std::string expression;  // SAP Index_Expression (key)
        std::string condition;   // SAP Index_Condition (FOR clause)
        std::string options;     // SAP Index_Options (flags, decimal text)
        std::string key_length;  // SAP Index_Key_Length (decimal text)
        std::string collation;   // SAP Index_Collation
    };
    util::Result<void> add_index_file   (const std::string& table_alias,
                                         const std::string& index_path,
                                         const std::string& comment);
    // Add or replace a per-tag index entry. Dedups by (table_alias, tag_name)
    // when tag_name is set, else by (table_alias, index_path). Pass
    // persist=false to batch many adds (e.g. import) and call save() once.
    util::Result<void> add_index(const IndexEntry& e, bool persist = true);
    // Drop every index entry. Used by import to replace the file-level entries
    // parsed from a SAP DD with the per-tag set read from SAP's system.indexes.
    util::Result<void> clear_indexes(bool persist = true);
    util::Result<void> remove_index_file(const std::string& table_alias,
                                         const std::string& index_path);
    const std::vector<IndexEntry>& indexes() const noexcept { return indexes_; }
    const std::vector<const IndexEntry*>&
        indexes_for_table(const std::string& table_alias) const;

    // ---- USER + MEMBER (M10.1) ------------------------------------------
    util::Result<void> create_user(const std::string& user);
    util::Result<void> delete_user(const std::string& user);
    util::Result<void> add_user_to_group     (const std::string& user,
                                              const std::string& group);
    util::Result<void> remove_user_from_group(const std::string& user,
                                              const std::string& group);
    // RCB 2026-06-27: User names are case-insensitive in ADS DDs, so
    // public user APIs normalize through ci_name() instead of trusting
    // caller-provided casing from imported dictionaries or clients.
    bool has_user(const std::string& user) const noexcept;
    static std::string ci_name(const std::string& s) noexcept;
    // Lookup stays case-insensitive (users_ holds ci_name keys), but SAP
    // preserves the DECLARED spelling for display: system.users.Name and
    // system.permissions.Grantee show "RCB"/"AutoTasks", not "rcb"/"autotasks".
    // Folding for storage used to discard that, and save() then wrote the
    // folded form back out, so the original casing was lost permanently.
    // Returns the declared spelling, or ci_name(user) when none was recorded.
    std::string display_user(const std::string& user) const noexcept;
    bool is_member_of(const std::string& user,
                      const std::string& group) const;
    const std::unordered_set<std::string>& groups_of(const std::string& user) const;
    const std::unordered_set<std::string>& users() const noexcept { return users_; }
    const std::unordered_map<std::string, std::unordered_set<std::string>>&
        memberships() const noexcept { return memberships_; }
    const std::vector<std::string>&
        users_in_group(const std::string& group) const;

    // ---- GROUP (M-DD-SQL) ------------------------------------------------
    util::Result<void> create_group(const std::string& group);
    util::Result<void> delete_group(const std::string& group);
    bool has_group(const std::string& group) const noexcept {
        return groups_.find(group) != groups_.end();
    }
    const std::unordered_set<std::string>& groups() const noexcept { return groups_; }

    // ---- LINK (M10.1) ----------------------------------------------------
    struct LinkEntry {
        std::string alias;
        std::string path;
        std::string user;
        std::string pwd;
    };
    util::Result<void> create_link(const std::string& alias,
                                   const std::string& path,
                                   const std::string& user,
                                   const std::string& pwd);
    util::Result<void> drop_link  (const std::string& alias);
    util::Result<void> modify_link(const std::string& alias,
                                   const std::string& path,
                                   const std::string& user,
                                   const std::string& pwd);
    const std::unordered_map<std::string, LinkEntry>&
        links() const noexcept { return links_; }

    // ---- RI rules (M10.1) ----------------------------------------------
    struct RiEntry {
        std::string name;
        std::string parent;
        std::string child;
        std::string parent_tag;
        std::string child_tag;
        std::string update_opt;
        std::string delete_opt;
        std::string fail_table;
    };
    util::Result<void> create_ri(const RiEntry& e);
    util::Result<void> remove_ri(const std::string& name);
    const std::unordered_map<std::string, RiEntry>&
        ri() const noexcept { return ri_; }
    std::unordered_map<std::string, RiEntry>&
        ri()       noexcept {
            invalidate_metadata_indexes_();
            return ri_;
        }
    const std::vector<const RiEntry*>&
        ri_by_parent_table(const std::string& table_alias) const;
    const std::vector<const RiEntry*>&
        ri_by_child_table(const std::string& table_alias) const;

    // ---- Field properties (M-DD-FIELD) ----------------------------------
    util::Result<void> set_field_property(const std::string& table,
                                           const std::string& field,
                                           const std::string& key,
                                           const std::string& value);
    std::string get_field_property(const std::string& table,
                                    const std::string& field,
                                    const std::string& key) const;

    // ---- Triggers (M-DD-TRIGGER) ----------------------------------------
    struct TriggerEntry {
        std::string   name;
        std::string   table_alias;
        std::uint32_t event_mask = 0;
        std::uint32_t timing     = 0;
        std::string   container;
        std::string   procedure;
        std::uint32_t priority = 0;
        bool          enabled  = true;
        std::string   comment;
        std::uint32_t options   = 0x03;
    };
    util::Result<void> create_trigger(const TriggerEntry& e);
    util::Result<void> drop_trigger  (const std::string& name);

    const TriggerEntry* find_trigger(const std::string& key) const noexcept {
        auto it = triggers_.find(key);
        if (it != triggers_.end()) return &it->second;
        if (key.find("::") == std::string::npos) {
            const TriggerEntry* found = nullptr;
            for (const auto& kv : triggers_) {
                auto sep = kv.first.find("::");
                if (sep != std::string::npos && kv.first.substr(sep + 2) == key) {
                    if (found) return nullptr;
                    found = &kv.second;
                }
            }
            return found;
        }
        return nullptr;
    }
    TriggerEntry* find_trigger(const std::string& key) noexcept {
        auto it = triggers_.find(key);
        if (it != triggers_.end()) return &it->second;
        if (key.find("::") == std::string::npos) {
            TriggerEntry* found = nullptr;
            for (auto& kv : triggers_) {
                auto sep = kv.first.find("::");
                if (sep != std::string::npos && kv.first.substr(sep + 2) == key) {
                    if (found) return nullptr;
                    found = &kv.second;
                }
            }
            return found;
        }
        return nullptr;
    }
    bool has_trigger(const std::string& key) const noexcept {
        return find_trigger(key) != nullptr;
    }
    const std::unordered_map<std::string, TriggerEntry>&
        triggers() const noexcept { return triggers_; }
    std::unordered_map<std::string, TriggerEntry>&
        triggers()       noexcept {
            invalidate_metadata_indexes_();
            return triggers_;
        }
    const std::vector<const TriggerEntry*>&
        triggers_for_table(const std::string& table_alias) const;
    const std::vector<const TriggerEntry*>&
        triggers_for_event(const std::string& table_alias,
                           std::uint32_t event_mask,
                           std::uint32_t timing) const;

    // ---- Publications / Articles / Subscriptions (Repl Phase 1) ----------
    struct PublicationEntry {
        std::string name;
        std::string comment;
    };
    struct ArticleEntry {
        std::string name;
        std::string publication;
        std::string source_table;
        std::vector<std::string> identity_cols;
        std::string filter;
        bool enabled = true;
    };
    struct SubscriptionEntry {
        std::string name;
        std::string publication;
        std::string target_uri;
        std::uint64_t last_lsn = 0;
        bool enabled = true;
    };
    util::Result<void> create_publication(const PublicationEntry& e);
    util::Result<void> drop_publication(const std::string& name);
    util::Result<void> create_article(const ArticleEntry& e);
    util::Result<void> drop_article(const std::string& name);
    util::Result<void> create_subscription(const SubscriptionEntry& e);
    util::Result<void> drop_subscription(const std::string& name);
    void set_subscription_last_lsn(const std::string& name, std::uint64_t lsn);
    const std::unordered_map<std::string, PublicationEntry>&
        publications() const noexcept { return publications_; }
    const std::unordered_map<std::string, ArticleEntry>&
        articles() const noexcept { return articles_; }
    std::unordered_map<std::string, SubscriptionEntry>&
        subscriptions() noexcept { return subscriptions_; }
    const std::unordered_map<std::string, SubscriptionEntry>&
        subscriptions() const noexcept { return subscriptions_; }

    // ---- Stored procedures (M-DD-PROC) ----------------------------------
    struct ProcEntry {
        std::string name;
        std::string container;
        std::string procedure;
        std::string input_params;
        std::string output_params;
        std::string comment;
    };
    util::Result<void> create_proc(const ProcEntry& e);
    util::Result<void> drop_proc  (const std::string& name);
    bool has_proc(const std::string& name) const noexcept {
        return procs_.find(name) != procs_.end();
    }
    const std::unordered_map<std::string, ProcEntry>&
        procs() const noexcept { return procs_; }
    std::unordered_map<std::string, ProcEntry>&
        procs()       noexcept { return procs_; }

    // ---- User-defined functions ------------------------------------------
    struct FunctionEntry {
        std::string name;
        std::string container;
        std::string implementation;
        std::string input_params;
        std::string return_type;
        std::string comment;
    };
    util::Result<void> create_function(const FunctionEntry& e);
    util::Result<void> drop_function  (const std::string& name);
    bool has_function(const std::string& name) const noexcept {
        return functions_.find(name) != functions_.end();
    }
    const std::unordered_map<std::string, FunctionEntry>&
        functions() const noexcept { return functions_; }
    std::unordered_map<std::string, FunctionEntry>&
        functions()       noexcept { return functions_; }

    // ---- Views (M-DD-VIEW) ----------------------------------------------
    struct ViewEntry {
        std::string name;
        std::string sql;
        std::string comment;
    };
    util::Result<void> create_view(const ViewEntry& e);
    util::Result<void> drop_view  (const std::string& name);
    bool has_view(const std::string& name) const noexcept {
        return views_.find(name) != views_.end();
    }
    const std::unordered_map<std::string, ViewEntry>&
        views() const noexcept { return views_; }
    std::unordered_map<std::string, ViewEntry>&
        views()       noexcept { return views_; }

    // ---- Permissions (M-ACL) -----------------------------------------------

    // Bitmask bit positions (same layout used on disk in JSON "bitmask" field).
    static constexpr uint32_t DD_PERM_SELECT    = 0x0001u;  // read rows
    static constexpr uint32_t DD_PERM_UPDATE    = 0x0002u;  // modify rows
    static constexpr uint32_t DD_PERM_EXECUTE   = 0x0004u;  // call proc/func
    static constexpr uint32_t DD_PERM_INSERT    = 0x0010u;  // add rows
    static constexpr uint32_t DD_PERM_DELETE    = 0x0020u;  // remove rows
    static constexpr uint32_t DD_PERM_REFERENCE = 0x0040u;  // FK parent
    static constexpr uint32_t DD_PERM_GRANT     = 0x0080u;  // can re-grant
    static constexpr uint32_t DD_PERM_FULL      = 0x80000000u; // all ops

    struct PermissionEntry {
        std::string  object_name;
        std::string  object_type;
        int          object_type_code = 0;
        std::string  grantee;
        bool         grantee_is_group = false;
        uint32_t     bitmask = 0;
        // RCB 07/16/2026: for a COLUMN grant (object_type_code == 4) this is the
        // parent table; the column name is object_name. Empty for table/proc/
        // view/function grants. A column name alone is not unique across tables,
        // so column permissions must carry their table.
        std::string  parent;
    };

    struct EffectiveOps {
        bool select_  = false;
        bool update_  = false;
        bool insert_  = false;
        bool delete_  = false;
        bool execute_ = false;
        bool open     = false;
        bool any() const noexcept {
            return open || select_ || update_ || insert_ || delete_ || execute_;
        }
    };

    // Build (or rebuild) the per-user effective-permission cache for the
    // named user.  Call this once per authenticated session after connect.
    // Subsequent check_perm() / get_effective_ops() calls are O(1).
    void build_perm_cache(const std::string& username) const;

    // O(1) permission check using the pre-built cache.
    // Returns true when the user holds at least the bits in `required`.
    // If no ACL is defined for the object, returns true (open access).
    bool check_perm(const std::string& username,
                    const std::string& object_name,
                    uint32_t           required) const;

    EffectiveOps get_effective_ops(const std::string& username,
                                    const std::string& object_name) const;

    // ---- COLUMN-LEVEL permissions (RCB 07/16/2026) ----------------------
    // Grant a column-level permission: `table` is the parent table, `column`
    // the field name. Batch with persist=false and call save() once (import).
    util::Result<void> grant_column_permission(const std::string& table,
                                               const std::string& column,
                                               const std::string& grantee,
                                               uint32_t bitmask,
                                               bool persist = true);

    // The columns `username` may access on `table` for the operation `op_bit`
    // (a single DD_PERM_* bit, e.g. DD_PERM_SELECT). Returns:
    //   * nullopt  → NO column-level restriction for this user/op on this table;
    //                the table-level grant applies to every column (unchanged
    //                behaviour). This is the common case.
    //   * a set    → the ONLY columns the user may touch for this op (SAP
    //                column-level security). Column names are lower-cased.
    // Used by the SQL/ISAM layer to project or deny columns. Fail-secure: when
    // a table has column grants for a user's op, columns not in the set are
    // hidden even though the user holds table-level access.
    std::optional<std::unordered_set<std::string>>
        permitted_columns(const std::string& username,
                          const std::string& table,
                          uint32_t op_bit) const;

    // True if any column-level grant exists at all (fast opt-out for the
    // common no-column-security case).
    bool has_any_column_acl() const noexcept { return has_column_perms_; }

    bool has_any_acl() const noexcept { return !permissions_.empty(); }
    bool has_acl_for_object(const std::string& object_name) const;

    struct EffectivePermEntry {
        std::string object_name;
        std::string object_type;
        int         object_type_code = 0;
        std::string grantee;
        EffectiveOps ops;
    };
    std::vector<EffectivePermEntry>
        get_all_effective_perms(const std::string& username) const;

    util::Result<void> grant_permission(const std::string& obj_type,
                                        const std::string& obj_name,
                                        const std::string& grantee,
                                        uint32_t bitmask);

    util::Result<void> set_table_permission(const std::string& table,
                                             const std::string& user_or_group,
                                             int level);
    int get_effective_permission(const std::string& username,
                                  const std::string& table) const;
    bool has_table_acl(const std::string& table) const noexcept {
        return table_perms_.find(table) != table_perms_.end();
    }
    // True when the .add file was opened in SAP proprietary binary format.
    // Callers (AdsConnect60) use this to reject the connection and direct
    // the user to run import_dd before connecting.
    bool has_sap_permissions() const noexcept { return binary_format_; }
    // No-op — kept for import_dd compatibility.
    util::Result<void> clear_sap_permissions() { return {}; }

    const std::unordered_map<std::string, std::unordered_map<std::string, int>>&
        table_perms() const noexcept { return table_perms_; }

    const std::vector<PermissionEntry>&
        permissions() const noexcept { return permissions_; }
    const std::vector<const PermissionEntry*>&
        permissions_by_grantee(const std::string& grantee) const;
    const std::vector<const PermissionEntry*>&
        permissions_by_object(const std::string& object_name) const;
    const PermissionEntry* find_permission(const std::string& grantee,
                                           const std::string& obj_name,
                                           int object_type_code) const;
    uint32_t get_permission_mask(const std::string& grantee,
                                 const std::string& obj_name,
                                 int object_type_code,
                                 bool include_inherited) const;

    using FieldPropsMap = std::unordered_map<std::string,
                              std::unordered_map<std::string,
                                  std::unordered_map<std::string, std::string>>>;
    const FieldPropsMap& field_props() const noexcept { return field_props_; }

    // ---- DB / user properties (M10.1) ----------------------------------
    util::Result<void> set_db_property(const std::string& key,
                                       const std::string& value);
    std::string get_db_property(const std::string& key) const;
    const std::unordered_map<std::string, std::string>&
        db_props() const noexcept { return db_props_; }
    util::Result<void> set_user_property(const std::string& user,
                                         const std::string& key,
                                         const std::string& value);
    std::string get_user_property(const std::string& user,
                                  const std::string& key) const;

    util::Result<void> save();

    const std::string& path() const noexcept { return path_; }

private:
    util::Result<void> load_();
    util::Result<void> load_add_binary_(const std::string& buf);

    std::string                                  path_;
    std::unordered_map<std::string, std::string> tables_;
    std::unordered_map<std::string, TableProps>  table_props_;
    std::vector<IndexEntry>                      indexes_;
    std::unordered_set<std::string>              users_;
    // ci_name(user) -> declared spelling, for display only. Never used for
    // lookup; users_ remains the authority on existence.
    std::unordered_map<std::string, std::string>  user_display_;
    std::unordered_set<std::string>              groups_;
    std::unordered_map<std::string,
                       std::unordered_set<std::string>> memberships_;
    std::unordered_map<std::string, LinkEntry>   links_;
    std::unordered_map<std::string, RiEntry>     ri_;
    std::unordered_map<std::string, std::string> db_props_;
    std::unordered_map<std::string,
                       std::unordered_map<std::string, std::string>>
                                                 user_props_;
    std::unordered_map<std::string,
                       std::unordered_map<std::string, int>>
                                                 table_perms_;
    std::vector<PermissionEntry>                 permissions_;
    // RCB 07/16/2026: true once any column-level (object_type_code==4) grant is
    // stored — lets the SQL/ISAM hot path skip column checks entirely when a DD
    // has no column security (the overwhelmingly common case).
    bool                                         has_column_perms_ = false;
    mutable bool                                 perm_indexes_valid_ = false;
    mutable std::unordered_map<std::string,
                               std::vector<const PermissionEntry*>>
                                                 permissions_by_grantee_ci_;
    mutable std::unordered_map<std::string,
                               std::vector<const PermissionEntry*>>
                                                 permissions_by_object_ci_;
    mutable std::unordered_map<std::string,
                               const PermissionEntry*>
                                                 permissions_by_grantee_object_;
    std::unordered_map<std::string,
                       std::unordered_map<std::string,
                                          std::unordered_map<std::string, std::string>>>
                                                 field_props_;
    std::unordered_map<std::string, TriggerEntry>  triggers_;
    std::unordered_map<std::string, ProcEntry>     procs_;
    std::unordered_map<std::string, FunctionEntry> functions_;
    std::unordered_map<std::string, ViewEntry>     views_;
    std::unordered_map<std::string, PublicationEntry>  publications_;
    std::unordered_map<std::string, ArticleEntry>      articles_;
    std::unordered_map<std::string, SubscriptionEntry> subscriptions_;

    // RCB 06/30/2026: Metadata consumers often need rows scoped to one table,
    // group, RI parent/child, or trigger event. Keep these reverse lookups in
    // the DD core so every API client avoids repeated full metadata scans.
    mutable bool metadata_indexes_valid_ = false;
    mutable std::unordered_map<std::string, std::vector<const IndexEntry*>>
                                                 indexes_by_table_ci_;
    mutable std::unordered_map<std::string, std::vector<std::string>>
                                                 users_by_group_ci_;
    mutable std::unordered_map<std::string, std::vector<const RiEntry*>>
                                                 ri_by_parent_table_ci_;
    mutable std::unordered_map<std::string, std::vector<const RiEntry*>>
                                                 ri_by_child_table_ci_;
    mutable std::unordered_map<std::string, std::vector<const TriggerEntry*>>
                                                 triggers_by_table_ci_;
    mutable std::unordered_map<std::string, std::vector<const TriggerEntry*>>
                                                 triggers_by_event_ci_;

    // RCB 2026-06-27: The cache key is the normalized DD user name; this
    // keeps effective permissions stable for AdsSys/adssys/ADSSYS logins.
    // Per-user effective-permission cache: username -> object_name -> merged_bits.
    // Built lazily on first check_perm() call for each user, or eagerly via
    // build_perm_cache().  Invalidated on every grant_permission() / set_table_permission().
    mutable std::unordered_map<std::string,
                std::unordered_map<std::string, uint32_t>> perm_cache_;

    void build_permission_indexes_() const;
    void build_metadata_indexes_() const;
    void invalidate_metadata_indexes_() noexcept {
        metadata_indexes_valid_ = false;
        indexes_by_table_ci_.clear();
        users_by_group_ci_.clear();
        ri_by_parent_table_ci_.clear();
        ri_by_child_table_ci_.clear();
        triggers_by_table_ci_.clear();
        triggers_by_event_ci_.clear();
    }
    static std::string permission_key_(const std::string& grantee,
                                       const std::string& obj_name,
                                       int object_type_code);
    static std::string trigger_event_key_(const std::string& table_alias,
                                          std::uint32_t event_mask,
                                          std::uint32_t timing);
    void invalidate_perm_cache_() noexcept {
        perm_cache_.clear();
        perm_indexes_valid_ = false;
        permissions_by_grantee_ci_.clear();
        permissions_by_object_ci_.clear();
        permissions_by_grantee_object_.clear();
    }

    // SAP proprietary binary .add format state (set by load_add_binary_()).
    struct BinaryRecord {
        bool                         active       = false;
        std::uint32_t                obj_id       = 0;
        std::uint32_t                parent_id    = 0;
        std::string                  obj_type;
        std::string                  obj_name;
        bool                         prop_null    = false;
        std::string                  property;
        std::uint16_t                prop_plen    = 0;
        std::array<std::uint8_t, 9>  more_property{};
        std::uint32_t                info1        = 0;
        std::uint32_t                info2        = 0;
        std::array<std::uint8_t, 9>  comment{};
    };

    bool                         binary_format_       = false;
    std::uint32_t                binary_hdr_len_      = 0;
    std::uint32_t                binary_rec_len_      = 0;
    std::string                  binary_hdr_;
    bool                         has_sap_permissions_ = false;
    std::vector<BinaryRecord>    binary_recs_;
};

} // namespace openads::engine
