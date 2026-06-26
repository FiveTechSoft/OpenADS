#pragma once

#include "sql_backend/postgres_table.h"
#include "sql_backend/postgres_uri.h"
#include "engine/aggregate.h"
#include "util/result.h"

#include <vector>

#include <cstdint>
#include <memory>
#include <string>

namespace openads::sql_backend {

class PostgresConnection {
public:
    PostgresConnection();
    ~PostgresConnection();

    PostgresConnection(PostgresConnection&&) noexcept;
    PostgresConnection& operator=(PostgresConnection&&) noexcept;

    PostgresConnection(const PostgresConnection&)            = delete;
    PostgresConnection& operator=(const PostgresConnection&) = delete;

    static util::Result<PostgresConnection> open(const PostgresUri& uri);

    void disconnect() noexcept;
    bool valid() const noexcept;

    util::Result<std::unique_ptr<PostgresTable>>
        open_table(const std::string& table_name);

    util::Result<void> goto_top(PostgresTable* tbl);
    util::Result<void> goto_bottom(PostgresTable* tbl);
    util::Result<void> skip(PostgresTable* tbl, std::int32_t step);

    // Tier-2 push-down: install (where non-empty) or clear (where empty) a SQL
    // WHERE fragment and reload the PK snapshot so navigation walks only the
    // matching rows. `where` must be a trusted, already-translated SQL boolean
    // expression (see engine::try_emit_sql_where). Resets the cursor.
    util::Result<void> set_filter(PostgresTable* tbl, const std::string& where);

    // Tier-3 push-down: compute COUNT/SUM/AVG/MIN/MAX over the rows matching
    // `where_sql` (already-translated SQL, empty = all rows) with one SELECT in
    // PostgreSQL. Returns one scalar per spec, in order (see engine::AggValue).
    util::Result<std::vector<engine::AggValue>>
        aggregate(PostgresTable* tbl,
                  const std::string& where_sql,
                  const std::vector<engine::AggSpec>& specs);

    util::Result<bool>          at_eof(PostgresTable* tbl) const;
    util::Result<bool>          at_bof(PostgresTable* tbl) const;
    util::Result<std::uint32_t> record_count(PostgresTable* tbl);

    util::Result<std::vector<PostgresTable::FieldDesc>>
        describe_table(PostgresTable* tbl);

    util::Result<void> read_field(PostgresTable* tbl,
                                  const std::string& field_name,
                                  std::string& buf,
                                  bool& is_null) const;

    util::Result<bool> seek_index(PostgresTable* tbl,
                                  const std::string& column,
                                  const std::string& key,
                                  bool soft,
                                  bool last_key);

    // Write surface (mirrors FirebirdConnection): append_blank stages a fresh
    // blank row, set_field stages one column value, flush_record turns the
    // staged row into an INSERT (pending_append) or a PK-keyed UPDATE, and
    // delete_record removes the positioned row by its PK.
    util::Result<void> append_blank(PostgresTable* tbl);
    util::Result<void> set_field(PostgresTable* tbl,
                                 const std::string& field_name,
                                 const std::string& value);
    util::Result<void> flush_record(PostgresTable* tbl);
    util::Result<void> delete_record(PostgresTable* tbl);

    // Record/file locking emulated with PostgreSQL session advisory locks
    // (pg_try_advisory_lock / pg_advisory_unlock): cross-connection, held
    // across statements, released by an explicit unlock — matching xBase
    // rLock()/fLock() semantics. recno is the 1-based ACE record number
    // (0 = current row). lock_record fails if another session holds it.
    util::Result<void> lock_record(PostgresTable* tbl, std::uint32_t recno);
    util::Result<void> unlock_record(PostgresTable* tbl, std::uint32_t recno);
    util::Result<void> lock_table(PostgresTable* tbl);
    util::Result<void> unlock_table(PostgresTable* tbl);

    const std::string& conninfo() const noexcept { return conninfo_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::string           conninfo_;
};

} // namespace openads::sql_backend