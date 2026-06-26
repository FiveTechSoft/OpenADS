#include "openads/ace.h"
#include "openads/error.h"

#include <atomic>

#include "abi/backend_table_ops.h"
#include "abi/backend_registry.h"
#include "abi/charset.h"
#include "abi/last_error.h"

#include "engine/aof_eval.h"
#include "engine/aof_expr.h"
#include "engine/codepage.h"
#include "engine/fts.h"
#include "engine/index_expr.h"
#include "engine/table.h"

#include "network/client.h"
#if defined(OPENADS_WITH_TLS)
#include "network/tls_transport.h"
#endif
#include "network/mg_wire.h"
#include "network/server.h"
#include "network/socket.h"
#include "mgmt/mg_collector.h"
#include "mgmt/mg_stats.h"
#include "session/connection.h"
#include "session/handle_registry.h"
#if defined(OPENADS_WITH_SQLITE)
#include "sql_backend/sqlite_connection.h"
#include "sql_backend/sqlite_index.h"
#include "sql_backend/uri.h"
#endif
#if defined(OPENADS_WITH_POSTGRESQL)
#include "sql_backend/postgres_connection.h"
#include "sql_backend/postgres_index.h"
#include "sql_backend/postgres_uri.h"
#endif
#if defined(OPENADS_WITH_MARIADB)
#include "sql_backend/maria_connection.h"
#include "sql_backend/maria_index.h"
#include "sql_backend/maria_uri.h"
#endif
#if defined(OPENADS_WITH_ODBC)
#include "sql_backend/odbc_connection.h"
#include "sql_backend/odbc_index.h"
#include "sql_backend/odbc_uri.h"
#endif
#if defined(OPENADS_WITH_MSSQL)
#include "sql_backend/mssql_connection.h"
#include "sql_backend/mssql_table.h"
#include "sql_backend/mssql_uri.h"
#endif
#if defined(OPENADS_WITH_FIREBIRD)
#include "sql_backend/firebird_connection.h"
#include "sql_backend/firebird_index.h"
#include "sql_backend/firebird_uri.h"
#endif
#include "drivers/dbf_common.h"
#include "drivers/index_trait.h"
#include "drivers/ntx/ntx_index.h"
#include "drivers/cdx/cdx_driver.h"
#include "drivers/cdx/cdx_index.h"
#include "drivers/adi/adi_index.h"
#include "drivers/adm/adm_memo.h"
#include "drivers/fpt/fpt_memo.h"
#include "platform/proc.h"
#include "platform/time.h"
#include "sql/parser.h"

#include <algorithm>
#include <chrono>
#include <functional>
#include <limits>
#include <thread>

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace {

using openads::engine::Table;
using openads::session::Connection;
using openads::session::Handle;
using openads::session::HandleKind;

struct ProcessState {
    // M10.36 — recursive_mutex so UNION dispatch can re-enter
    // AdsExecuteSQLDirect (used to materialise each member's cursor)
    // while still holding the outer lock.
    std::recursive_mutex                                          mu;
    openads::session::HandleRegistry                              registry;
    std::unordered_map<Handle, std::unique_ptr<Connection>>       conns;
};

ProcessState& state() {
    static ProcessState s;
    return s;
}

UNSIGNED32 ok() {
    openads::abi::clear_last_error();
    return openads::AE_SUCCESS;
}

UNSIGNED32 fail(const openads::util::Error& e) {
    openads::abi::set_last_error(e);
    return static_cast<UNSIGNED32>(e.code);
}

UNSIGNED32 fail(int code, const char* msg) {
    return fail(openads::util::Error{code, 0, msg ? msg : "", ""});
}

openads::engine::TableType map_type(UNSIGNED16 t) {
    switch (t) {
        case ADS_NTX: return openads::engine::TableType::Ntx;
        case ADS_CDX: return openads::engine::TableType::Cdx;
        case ADS_ADT: return openads::engine::TableType::Adt;
        case ADS_VFP: return openads::engine::TableType::Vfp;
        default:      return openads::engine::TableType::Cdx;
    }
}

// Stamp DBF header bytes [1..3] (YY MM DD, year as offset from 1900)
// with today's UTC date — what a real ADS server records when it
// creates or modifies a table. Without this a freshly-created table
// reports a "1900-00-00" last-update stamp until its first record
// write triggers CdxDriver::rewrite_header_(). UTC keeps the two paths
// consistent. `hdr` must point at the start of the 32-byte header.
void stamp_dbf_header_today(std::uint8_t* hdr) {
    std::time_t secs = static_cast<std::time_t>(
        openads::platform::utc_unix_micros() / 1'000'000);
    std::tm tm_utc{};
#ifdef _WIN32
    gmtime_s(&tm_utc, &secs);
#else
    gmtime_r(&secs, &tm_utc);
#endif
    hdr[1] = static_cast<std::uint8_t>(tm_utc.tm_year);
    hdr[2] = static_cast<std::uint8_t>(tm_utc.tm_mon + 1);
    hdr[3] = static_cast<std::uint8_t>(tm_utc.tm_mday);
}

UNSIGNED16 map_field_type(openads::drivers::DbfFieldType t) {
    using openads::drivers::DbfFieldType;
    // Constants verified empirically (M8.4) against
    // c:\harbour\lib\win\msvc64\rddads.lib — see include/openads/ace.h
    // for the full sweep table.
    switch (t) {
        case DbfFieldType::Character: return ADS_STRING;        //  4
        case DbfFieldType::Numeric:
        case DbfFieldType::Float:     return ADS_NUMERIC;       //  2
        case DbfFieldType::Logical:   return ADS_LOGICAL;       //  1
        case DbfFieldType::Date:      return ADS_DATE;          //  3
        case DbfFieldType::DateTime:  return ADS_TIMESTAMP;     // 14
        case DbfFieldType::Memo:      return ADS_MEMO;          //  5
        case DbfFieldType::Integer:   return ADS_INTEGER;       // 11
        case DbfFieldType::Currency:  return ADS_MONEY;         // 18
        case DbfFieldType::Double:    return ADS_DOUBLE;        // 10
        case DbfFieldType::Varchar:   return ADS_STRING;        // M11.1
        case DbfFieldType::Varbinary: return ADS_RAW;           // M11.1
        // ADT-native types (M4)
        case DbfFieldType::ShortInt:     return ADS_SHORTINT;   // 12
        case DbfFieldType::Binary:       return ADS_BINARY;     //  6
        case DbfFieldType::CiCharacter:  return ADS_CISTRING;   // 25
        case DbfFieldType::AutoInc:      return ADS_AUTOINC;    // 15
        case DbfFieldType::Time:         return ADS_TIME;       // 13
        case DbfFieldType::AdtDate:      return ADS_DATE;       //  3
        case DbfFieldType::AdtTimestamp: return ADS_TIMESTAMP;  // 14
        case DbfFieldType::AdtMoney:     return ADS_MONEY;       // 18
        case DbfFieldType::RowVersion:   return ADS_ROWVERSION;  // 21
        case DbfFieldType::ModTime:      return ADS_MODTIME;     // 22
        case DbfFieldType::Unknown:      return ADS_FIELD_TYPE_UNKNOWN;
    }
    return ADS_FIELD_TYPE_UNKNOWN;
}

[[maybe_unused]] const openads::drivers::DbfField*
find_field(Table* tbl, const std::string& name) {
    for (std::uint16_t i = 0; i < tbl->field_count(); ++i) {
        const auto& f = tbl->field_descriptor(i);
        if (f.name == name) return &f;
    }
    return nullptr;
}

// Cursor projections (M10.8). When a SELECT carries a projection
// list, the cursor handle's entry in this map holds the source-field
// indices (in projection order) so AdsGetNumFields / AdsGetFieldName
// / AdsGetField with ADSFIELD(n) report the projected schema instead
// of the underlying table's full layout.
std::unordered_map<ADSHANDLE, std::vector<std::uint16_t>>&
cursor_projections() {
    static std::unordered_map<ADSHANDLE, std::vector<std::uint16_t>> m;
    return m;
}

// Remote SQL cursors map — moved out of AdsExecuteSQLDirect so that
// AdsDisconnect can reach it to null out rt->conn before the
// RemoteConnection is freed, preventing use-after-free in AdsCloseTable.
std::unordered_map<Handle,
    std::unique_ptr<openads::network::RemoteTable>>&
remote_sql_cursors_map() {
    static std::unordered_map<Handle,
        std::unique_ptr<openads::network::RemoteTable>> m;
    return m;
}

const std::vector<std::uint16_t>*
projection_for(ADSHANDLE h) {
    auto& m = cursor_projections();
    auto it = m.find(h);
    if (it == m.end() || it->second.empty()) return nullptr;
    return &it->second;
}

// Projection-aware variant. Called by Get* entry points that take
// hTable + pucField; routes ADSFIELD(n) numeric handles through the
// projection map (n = position within projection, translated to the
// underlying field index). Bare-name lookups stay direct — rddads
// only ever asks for projected names so the underlying schema's
// extra columns aren't reachable through the cursor anyway.
bool resolve_field_index(Table* tbl, UNSIGNED8* pucField, std::uint16_t* out);
bool resolve_field_index_h(ADSHANDLE h, Table* tbl,
                           UNSIGNED8* pucField, std::uint16_t* out) {
    auto p = reinterpret_cast<std::uintptr_t>(pucField);
    const auto* proj = projection_for(h);
    if (proj != nullptr && p != 0 && p < 0x10000u) {
        std::uint16_t one_based = static_cast<std::uint16_t>(p);
        if (one_based >= 1 && one_based <= proj->size()) {
            *out = (*proj)[one_based - 1];
            return true;
        }
        return false;
    }
    return resolve_field_index(tbl, pucField, out);
}

// Resolve a pucField argument to a 0-based field index. Real ACE.h
// defines `ADSFIELD(n)` as `((UNSIGNED8*)(UNSIGNED_PTR)(n))`, so
// callers compiled against that header pass small integers cast to
// pointers. Anything below 0x10000 cannot be a valid string address in
// any real process layout, so we treat it as a 1-based field index.
// Otherwise pucField is a NUL-terminated field name.
bool resolve_field_index(Table* tbl, UNSIGNED8* pucField, std::uint16_t* out) {
    if (tbl == nullptr || out == nullptr) return false;
    auto p = reinterpret_cast<std::uintptr_t>(pucField);
    if (p != 0 && p < 0x10000u) {
        std::uint16_t one_based = static_cast<std::uint16_t>(p);
        if (one_based >= 1 && one_based <= tbl->field_count()) {
            *out = static_cast<std::uint16_t>(one_based - 1);
            return true;
        }
        return false;
    }
    if (pucField == nullptr) return false;
    auto name = openads::abi::to_internal(pucField, 0);
    // Delegate to Table::field_index — case-insensitive (matches native
    // ACE semantics) and cached. Field names in DBF/ADT storage are
    // upper-cased, but callers (and CDX/NTX index expressions) may use
    // any case; an exact-case compare here spuriously missed them.
    std::int32_t idx = tbl->field_index(name);
    if (idx < 0) return false;
    *out = static_cast<std::uint16_t>(idx);
    return true;
}

// lookup_table_by_index — defined further down once IndexBinding is
// known. Returns the Table bound to the given index handle, or null.
Table* lookup_table_by_index(ADSHANDLE h);
openads::drivers::IIndex* iindex_for_handle(ADSHANDLE h);
openads::util::Result<void> activate_binding(ADSHANDLE h);
void purge_bindings_for_table(Table* t);

// M12.5 — remote-table lookup helper. Returns nullptr when the
// handle isn't a TCP-routed table.
openads::network::RemoteTable* get_remote_table(ADSHANDLE h) {
    auto& s = state();
    return s.registry.lookup<openads::network::RemoteTable>(
        h, HandleKind::RemoteTable);
}

// M12.21 option C — settle the sequential-prefetch lag before any op
// that reads or mutates the server's CURRENT record. Rows served
// locally from the lookahead queue left the server cursor behind by
// prefetch_consumed; a Skip(0) (which the client sends as
// Skip(prefetch_consumed)) walks the server cursor up to the client's
// logical row and the ack resets the counter. A no-op when nothing was
// prefetched (the common cold-cache write path pays nothing).
void remote_settle_cursor(openads::network::RemoteTable* rt) {
    if (rt != nullptr && rt->conn != nullptr && rt->prefetch_consumed > 0) {
        (void)rt->conn->skip(rt, 0);
    }
}

#if defined(OPENADS_WITH_SQLITE)
std::unordered_map<Handle,
    std::unique_ptr<openads::sql_backend::SqliteConnection>>&
sqlite_conns_map() {
    static std::unordered_map<Handle,
        std::unique_ptr<openads::sql_backend::SqliteConnection>> m;
    return m;
}

std::unordered_map<Handle,
    std::unique_ptr<openads::sql_backend::SqliteTable>>&
sqlite_tables_map() {
    static std::unordered_map<Handle,
        std::unique_ptr<openads::sql_backend::SqliteTable>> m;
    return m;
}

openads::sql_backend::SqliteTable* get_sqlite_table(ADSHANDLE h) {
    auto& s = state();
    return s.registry.lookup<openads::sql_backend::SqliteTable>(
        h, HandleKind::SqliteTable);
}

std::unordered_map<Handle,
    std::unique_ptr<openads::sql_backend::SqliteIndex>>&
sqlite_indexes_map() {
    static std::unordered_map<Handle,
        std::unique_ptr<openads::sql_backend::SqliteIndex>> m;
    return m;
}

openads::sql_backend::SqliteIndex* get_sqlite_index(ADSHANDLE h) {
    auto& s = state();
    return s.registry.lookup<openads::sql_backend::SqliteIndex>(
        h, HandleKind::SqliteIndex);
}

std::size_t sqlite_field_index(openads::sql_backend::SqliteTable* st,
                               UNSIGNED8* pucField) {
    if (!st->fields_cached) {
        // st->conn is nulled by AdsDisconnect on still-open tables to
        // avoid use-after-free; guard before dereferencing it.
        if (st->conn == nullptr) {
            return std::numeric_limits<std::size_t>::max();
        }
        auto r = st->conn->describe_table(st);
        if (!r) return std::numeric_limits<std::size_t>::max();
    }
    {
        auto p = reinterpret_cast<std::uintptr_t>(pucField);
        if (p != 0 && p < 0x10000u) {
            std::size_t one_based = static_cast<std::size_t>(p);
            if (one_based >= 1 && one_based <= st->fields.size()) {
                return one_based - 1;
            }
            return std::numeric_limits<std::size_t>::max();
        }
    }
    if (pucField == nullptr) {
        return std::numeric_limits<std::size_t>::max();
    }
    std::string want = openads::abi::to_internal(pucField, 0);
    for (auto& c : want) {
        c = static_cast<char>(
            std::toupper(static_cast<unsigned char>(c)));
    }
    for (std::size_t i = 0; i < st->fields.size(); ++i) {
        std::string have = st->fields[i].name;
        for (auto& c : have) {
            c = static_cast<char>(
                std::toupper(static_cast<unsigned char>(c)));
        }
        if (have == want) return i;
    }
    return std::numeric_limits<std::size_t>::max();
}
#endif // OPENADS_WITH_SQLITE

#if defined(OPENADS_WITH_ODBC)
std::unordered_map<Handle,
    std::unique_ptr<openads::sql_backend::OdbcConnection>>&
odbc_conns_map() {
    static std::unordered_map<Handle,
        std::unique_ptr<openads::sql_backend::OdbcConnection>> m;
    return m;
}

std::unordered_map<Handle,
    std::unique_ptr<openads::sql_backend::OdbcTable>>&
odbc_tables_map() {
    static std::unordered_map<Handle,
        std::unique_ptr<openads::sql_backend::OdbcTable>> m;
    return m;
}

openads::sql_backend::OdbcTable* get_odbc_table(ADSHANDLE h) {
    auto& s = state();
    return s.registry.lookup<openads::sql_backend::OdbcTable>(
        h, HandleKind::OdbcTable);
}

std::unordered_map<Handle,
    std::unique_ptr<openads::sql_backend::OdbcIndex>>&
odbc_indexes_map() {
    static std::unordered_map<Handle,
        std::unique_ptr<openads::sql_backend::OdbcIndex>> m;
    return m;
}

openads::sql_backend::OdbcIndex* get_odbc_index(ADSHANDLE h) {
    auto& s = state();
    return s.registry.lookup<openads::sql_backend::OdbcIndex>(
        h, HandleKind::OdbcIndex);
}

std::size_t odbc_field_index(openads::sql_backend::OdbcTable* st,
                             UNSIGNED8* pucField) {
    if (!st->fields_cached) {
        if (st->conn == nullptr) {
            return std::numeric_limits<std::size_t>::max();
        }
        auto r = st->conn->describe_table(st);
        if (!r) return std::numeric_limits<std::size_t>::max();
    }
    {
        auto p = reinterpret_cast<std::uintptr_t>(pucField);
        if (p != 0 && p < 0x10000u) {
            std::size_t one_based = static_cast<std::size_t>(p);
            if (one_based >= 1 && one_based <= st->fields.size()) {
                return one_based - 1;
            }
            return std::numeric_limits<std::size_t>::max();
        }
    }
    std::string want = openads::abi::to_internal(pucField, 0);
    for (auto& c : want) {
        c = static_cast<char>(
            std::toupper(static_cast<unsigned char>(c)));
    }
    for (std::size_t i = 0; i < st->fields.size(); ++i) {
        std::string have = st->fields[i].name;
        for (auto& c : have) {
            c = static_cast<char>(
                std::toupper(static_cast<unsigned char>(c)));
        }
        if (have == want) return i;
    }
    return std::numeric_limits<std::size_t>::max();
}
#endif // OPENADS_WITH_ODBC

#if defined(OPENADS_WITH_MSSQL)
std::unordered_map<Handle,
    std::unique_ptr<openads::sql_backend::MssqlConnection>>&
mssql_conns_map() {
    static std::unordered_map<Handle,
        std::unique_ptr<openads::sql_backend::MssqlConnection>> m;
    return m;
}

std::unordered_map<Handle,
    std::unique_ptr<openads::sql_backend::MssqlTable>>&
mssql_tables_map() {
    static std::unordered_map<Handle,
        std::unique_ptr<openads::sql_backend::MssqlTable>> m;
    return m;
}

openads::sql_backend::MssqlTable* get_mssql_table(ADSHANDLE h) {
    auto& s = state();
    return s.registry.lookup<openads::sql_backend::MssqlTable>(
        h, HandleKind::MssqlTable);
}

std::size_t mssql_field_index(openads::sql_backend::MssqlTable* st,
                               UNSIGNED8* pucField) {
    if (pucField == nullptr) return std::numeric_limits<std::size_t>::max();
    auto p = reinterpret_cast<std::uintptr_t>(pucField);
    if (p != 0 && p < 0x10000u) {
        auto idx = static_cast<std::size_t>(p) - 1;
        if (idx < st->field_count()) return idx;
        return std::numeric_limits<std::size_t>::max();
    }
    std::string fname(reinterpret_cast<const char*>(pucField));
    for (std::size_t i = 0; i < st->field_count(); ++i) {
        if (st->field_name(i) == fname) return i;
    }
    return std::numeric_limits<std::size_t>::max();
}
#endif // OPENADS_WITH_MSSQL

#if defined(OPENADS_WITH_FIREBIRD)
std::unordered_map<Handle,
    std::unique_ptr<openads::sql_backend::FirebirdConnection>>&
firebird_conns_map() {
    static std::unordered_map<Handle,
        std::unique_ptr<openads::sql_backend::FirebirdConnection>> m;
    return m;
}

std::unordered_map<Handle,
    std::unique_ptr<openads::sql_backend::FirebirdTable>>&
firebird_tables_map() {
    static std::unordered_map<Handle,
        std::unique_ptr<openads::sql_backend::FirebirdTable>> m;
    return m;
}

openads::sql_backend::FirebirdTable* get_firebird_table(ADSHANDLE h) {
    auto& s = state();
    return s.registry.lookup<openads::sql_backend::FirebirdTable>(
        h, HandleKind::FirebirdTable);
}

std::unordered_map<Handle,
    std::unique_ptr<openads::sql_backend::FirebirdIndex>>&
firebird_indexes_map() {
    static std::unordered_map<Handle,
        std::unique_ptr<openads::sql_backend::FirebirdIndex>> m;
    return m;
}

openads::sql_backend::FirebirdIndex* get_firebird_index(ADSHANDLE h) {
    auto& s = state();
    return s.registry.lookup<openads::sql_backend::FirebirdIndex>(
        h, HandleKind::FirebirdIndex);
}

std::size_t firebird_field_index(openads::sql_backend::FirebirdTable* st,
                                 UNSIGNED8* pucField) {
    if (!st->fields_cached) {
        if (st->conn == nullptr) {
            return std::numeric_limits<std::size_t>::max();
        }
        auto r = st->conn->describe_table(st);
        if (!r) return std::numeric_limits<std::size_t>::max();
    }
    {
        auto p = reinterpret_cast<std::uintptr_t>(pucField);
        if (p != 0 && p < 0x10000u) {
            std::size_t one_based = static_cast<std::size_t>(p);
            if (one_based >= 1 && one_based <= st->fields.size()) {
                return one_based - 1;
            }
            return std::numeric_limits<std::size_t>::max();
        }
    }
    std::string want = openads::abi::to_internal(pucField, 0);
    for (auto& c : want) {
        c = static_cast<char>(
            std::toupper(static_cast<unsigned char>(c)));
    }
    for (std::size_t i = 0; i < st->fields.size(); ++i) {
        std::string have = st->fields[i].name;
        for (auto& c : have) {
            c = static_cast<char>(
                std::toupper(static_cast<unsigned char>(c)));
        }
        if (have == want) return i;
    }
    return std::numeric_limits<std::size_t>::max();
}
#endif // OPENADS_WITH_FIREBIRD

#if defined(OPENADS_WITH_MARIADB)
std::unordered_map<Handle,
    std::unique_ptr<openads::sql_backend::MariaConnection>>&
maria_conns_map() {
    static std::unordered_map<Handle,
        std::unique_ptr<openads::sql_backend::MariaConnection>> m;
    return m;
}

std::unordered_map<Handle,
    std::unique_ptr<openads::sql_backend::MariaTable>>&
maria_tables_map() {
    static std::unordered_map<Handle,
        std::unique_ptr<openads::sql_backend::MariaTable>> m;
    return m;
}

openads::sql_backend::MariaTable* get_maria_table(ADSHANDLE h) {
    auto& s = state();
    return s.registry.lookup<openads::sql_backend::MariaTable>(
        h, HandleKind::MariaTable);
}

std::unordered_map<Handle,
    std::unique_ptr<openads::sql_backend::MariaIndex>>&
maria_indexes_map() {
    static std::unordered_map<Handle,
        std::unique_ptr<openads::sql_backend::MariaIndex>> m;
    return m;
}

openads::sql_backend::MariaIndex* get_maria_index(ADSHANDLE h) {
    auto& s = state();
    return s.registry.lookup<openads::sql_backend::MariaIndex>(
        h, HandleKind::MariaIndex);
}

std::size_t maria_field_index(openads::sql_backend::MariaTable* st,
                              UNSIGNED8* pucField) {
    if (!st->fields_cached) {
        if (st->conn == nullptr) {
            return std::numeric_limits<std::size_t>::max();
        }
        auto r = st->conn->describe_table(st);
        if (!r) return std::numeric_limits<std::size_t>::max();
    }
    {
        auto p = reinterpret_cast<std::uintptr_t>(pucField);
        if (p != 0 && p < 0x10000u) {
            std::size_t one_based = static_cast<std::size_t>(p);
            if (one_based >= 1 && one_based <= st->fields.size()) {
                return one_based - 1;
            }
            return std::numeric_limits<std::size_t>::max();
        }
    }
    if (pucField == nullptr) {
        return std::numeric_limits<std::size_t>::max();
    }
    std::string want = openads::abi::to_internal(pucField, 0);
    for (auto& c : want) {
        c = static_cast<char>(
            std::toupper(static_cast<unsigned char>(c)));
    }
    for (std::size_t i = 0; i < st->fields.size(); ++i) {
        std::string have = st->fields[i].name;
        for (auto& c : have) {
            c = static_cast<char>(
                std::toupper(static_cast<unsigned char>(c)));
        }
        if (have == want) return i;
    }
    return std::numeric_limits<std::size_t>::max();
}
#endif // OPENADS_WITH_MARIADB

#if defined(OPENADS_WITH_POSTGRESQL)
std::unordered_map<Handle,
    std::unique_ptr<openads::sql_backend::PostgresConnection>>&
postgres_conns_map() {
    static std::unordered_map<Handle,
        std::unique_ptr<openads::sql_backend::PostgresConnection>> m;
    return m;
}

std::unordered_map<Handle,
    std::unique_ptr<openads::sql_backend::PostgresTable>>&
postgres_tables_map() {
    static std::unordered_map<Handle,
        std::unique_ptr<openads::sql_backend::PostgresTable>> m;
    return m;
}

openads::sql_backend::PostgresTable* get_postgres_table(ADSHANDLE h) {
    auto& s = state();
    return s.registry.lookup<openads::sql_backend::PostgresTable>(
        h, HandleKind::PostgresTable);
}

std::unordered_map<Handle,
    std::unique_ptr<openads::sql_backend::PostgresIndex>>&
postgres_indexes_map() {
    static std::unordered_map<Handle,
        std::unique_ptr<openads::sql_backend::PostgresIndex>> m;
    return m;
}

openads::sql_backend::PostgresIndex* get_postgres_index(ADSHANDLE h) {
    auto& s = state();
    return s.registry.lookup<openads::sql_backend::PostgresIndex>(
        h, HandleKind::PostgresIndex);
}

std::size_t postgres_field_index(openads::sql_backend::PostgresTable* st,
                               UNSIGNED8* pucField) {
    if (!st->fields_cached) {
        if (st->conn == nullptr) {
            return std::numeric_limits<std::size_t>::max();
        }
        auto r = st->conn->describe_table(st);
        if (!r) return std::numeric_limits<std::size_t>::max();
    }
    {
        auto p = reinterpret_cast<std::uintptr_t>(pucField);
        if (p != 0 && p < 0x10000u) {
            std::size_t one_based = static_cast<std::size_t>(p);
            if (one_based >= 1 && one_based <= st->fields.size()) {
                return one_based - 1;
            }
            return std::numeric_limits<std::size_t>::max();
        }
    }
    std::string want = openads::abi::to_internal(pucField, 0);
    for (auto& c : want) {
        c = static_cast<char>(
            std::toupper(static_cast<unsigned char>(c)));
    }
    for (std::size_t i = 0; i < st->fields.size(); ++i) {
        std::string have = st->fields[i].name;
        for (auto& c : have) {
            c = static_cast<char>(
                std::toupper(static_cast<unsigned char>(c)));
        }
        if (have == want) return i;
    }
    return std::numeric_limits<std::size_t>::max();
}
#endif // OPENADS_WITH_POSTGRESQL

// M12.16 — same dispatch helper for remote-index handles. Returns
// nullptr when `h` is a local IIndex / Connection / unknown.
openads::network::RemoteIndex* get_remote_index(ADSHANDLE h) {
    auto& s = state();
    return s.registry.lookup<openads::network::RemoteIndex>(
        h, HandleKind::RemoteIndex);
}

// Latch set by AdsSeekLast. AdsSeek consults this to suppress its
// empty-key always-found quirk when called as part of rddads'
// AdsSeekLast retry chain. AdsSkip clears it.
bool& seek_last_retry_latch() {
    static thread_local bool v = false;
    return v;
}

// Harbour rddads' default connection handle is 0 when the caller
// never AdsConnect'd. SAP-ACE in this mode auto-connects against the
// current working directory; mirror that by lazily creating one
// process-wide Connection rooted at fs::current_path. Returned handle
// is cached so subsequent calls reuse the same Connection.
ADSHANDLE get_or_create_default_connection() {
    static ADSHANDLE cached = 0;
    auto& s = state();
    if (cached != 0) {
        if (s.registry.lookup<Connection>(cached, HandleKind::Connection))
            return cached;
        cached = 0;
    }
    namespace fs = std::filesystem;
    auto opened = Connection::open(fs::current_path().string());
    if (!opened) return 0;
    auto holder = std::make_unique<Connection>(std::move(opened).value());
    Connection* raw = holder.get();
    Handle h = s.registry.register_object(HandleKind::Connection, raw);
    s.conns.emplace(h, std::move(holder));
    cached = h;
    return h;
}

// Harbour rddads: AdsConnect stores the handle globally; BEGIN/COMMIT/
// ROLLBACK call AdsBeginTransaction(0). Resolve 0 to the last AdsConnect
// handle before falling back to cwd auto-connect.
ADSHANDLE& rddads_default_connection() noexcept {
    thread_local ADSHANDLE h = 0;
    return h;
}

ADSHANDLE resolve_connection_handle(ADSHANDLE hConnect) {
    if (hConnect != 0) return hConnect;
    ADSHANDLE h = rddads_default_connection();
    if (h != 0) {
        auto& s = state();
        if (s.registry.lookup<Connection>(h, HandleKind::Connection))
            return h;
        rddads_default_connection() = 0;
    }
    return get_or_create_default_connection();
}

Connection* lookup_connection(ADSHANDLE hConnect) {
    auto& s = state();
    return s.registry.lookup<Connection>(
        resolve_connection_handle(hConnect), HandleKind::Connection);
}

Table* get_table(ADSHANDLE h) {
    auto& s = state();
    Table* t = s.registry.lookup<Table>(h, HandleKind::Table);
    if (t != nullptr) return t;
    // Real ACE accepts an index handle anywhere a table handle is
    // expected — rddads' adsGoTop calls AdsGotoTop(hOrdCurrent) when
    // an order is active. The bound Table is the same as the table's
    // own; we additionally swap the binding's parked IIndex into the
    // Table's active order so navigation actually walks the requested
    // tag (multi-tag CDX support, M8.9).
    Table* via_idx = lookup_table_by_index(h);
    if (via_idx != nullptr) {
        (void)activate_binding(h);
    }
    return via_idx;
}

// DBF/xbase CHARACTER fields are fixed-width space-padded. The internal
// decode path (make_string / decode_field) trims trailing spaces because
// the SQL engine, index keys, and AOF filters need trimmed values. On the
// way out to an ABI caller, re-pad to the declared field width so that
// FieldGet of a C(20) field always returns exactly 20 characters — the
// behaviour expected by rddads, Clipper, and X# (Pritpal's xbrowse bug).
// Never truncates: a value already at or above width is returned as-is.
std::string pad_char_field(std::string s, std::size_t width) {
    if (s.size() < width)
        s.append(width - s.size(), ' ');
    return s;
}

// ---------------------------------------------------------------------------
// Task 3: Lifted SQLite table ops + accessor
// Placed after pad_char_field (sqlite_get_field uses it).
// ---------------------------------------------------------------------------
#if defined(OPENADS_WITH_SQLITE)

UNSIGNED32 sqlite_close_table(ADSHANDLE hTable) {
    auto* st = get_sqlite_table(hTable);
    (void)st;
    auto& s2 = state();
    std::lock_guard<std::recursive_mutex> lk2(s2.mu);
    sqlite_tables_map().erase(hTable);
    s2.registry.release(hTable);
    return ok();
}

UNSIGNED32 sqlite_goto_top(ADSHANDLE hTable) {
    auto* st = get_sqlite_table(hTable);
    if (st->conn == nullptr) return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    auto r = st->conn->goto_top(st);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 sqlite_goto_bottom(ADSHANDLE hTable) {
    auto* st = get_sqlite_table(hTable);
    if (st->conn == nullptr) return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    auto r = st->conn->goto_bottom(st);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 sqlite_skip(ADSHANDLE hTable, SIGNED32 lRows) {
    auto* st = get_sqlite_table(hTable);
    if (st->conn == nullptr) return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    auto r = st->conn->skip(st, lRows);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 sqlite_at_eof(ADSHANDLE hTable, UNSIGNED16* pbAtEnd) {
    auto* st = get_sqlite_table(hTable);
    if (pbAtEnd == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    if (st->conn == nullptr) return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    auto r = st->conn->at_eof(st);
    if (!r) return fail(r.error());
    *pbAtEnd = r.value() ? 1 : 0;
    return ok();
}

UNSIGNED32 sqlite_at_bof(ADSHANDLE hTable, UNSIGNED16* pbAtBof) {
    auto* st = get_sqlite_table(hTable);
    if (st->conn == nullptr) return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    auto r = st->conn->at_bof(st);
    if (!r) return fail(r.error());
    *pbAtBof = r.value() ? 1 : 0;
    return ok();
}

UNSIGNED32 sqlite_num_fields(ADSHANDLE hTable, UNSIGNED16* pusCnt) {
    auto* st = get_sqlite_table(hTable);
    if (st->conn == nullptr) return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    if (!st->fields_cached) {
        auto r = st->conn->describe_table(st);
        if (!r) return fail(r.error());
    }
    *pusCnt = static_cast<UNSIGNED16>(st->fields.size());
    return ok();
}

UNSIGNED32 sqlite_field_name(ADSHANDLE hTable, UNSIGNED16 n,
                             UNSIGNED8* pucBuf, UNSIGNED16* pusLen) {
    auto* st = get_sqlite_table(hTable);
    if (st->conn == nullptr) return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    if (!st->fields_cached) {
        auto r = st->conn->describe_table(st);
        if (!r) return fail(r.error());
    }
    if (n == 0 || n > st->fields.size()) {
        return fail(openads::AE_COLUMN_NOT_FOUND, "");
    }
    openads::abi::copy_to_caller(pucBuf, pusLen, st->fields[n - 1].name);
    return ok();
}

UNSIGNED32 sqlite_field_type(ADSHANDLE hTable, UNSIGNED8* pucField,
                             UNSIGNED16* pusType) {
    auto* st = get_sqlite_table(hTable);
    auto i = sqlite_field_index(st, pucField);
    if (i == std::numeric_limits<std::size_t>::max()) {
        return fail(openads::AE_COLUMN_NOT_FOUND, "");
    }
    *pusType = st->fields[i].type;
    return ok();
}

UNSIGNED32 sqlite_field_length(ADSHANDLE hTable, UNSIGNED8* pucField,
                               UNSIGNED32* pulLen) {
    auto* st = get_sqlite_table(hTable);
    auto i = sqlite_field_index(st, pucField);
    if (i == std::numeric_limits<std::size_t>::max()) {
        return fail(openads::AE_COLUMN_NOT_FOUND, "");
    }
    *pulLen = st->fields[i].length;
    return ok();
}

UNSIGNED32 sqlite_field_decimals(ADSHANDLE hTable, UNSIGNED8* pucField,
                                 UNSIGNED16* pusDec) {
    auto* st = get_sqlite_table(hTable);
    auto i = sqlite_field_index(st, pucField);
    if (i == std::numeric_limits<std::size_t>::max()) {
        return fail(openads::AE_COLUMN_NOT_FOUND, "");
    }
    *pusDec = st->fields[i].decimals;
    return ok();
}

UNSIGNED32 sqlite_record_num(ADSHANDLE hTable, UNSIGNED32* pulRec) {
    auto* st = get_sqlite_table(hTable);
    if (!st->positioned || !st->row_valid) {
        return fail(5026, "no current record");
    }
    *pulRec = static_cast<UNSIGNED32>(st->current_rowid);
    return ok();
}

UNSIGNED32 sqlite_record_count(ADSHANDLE hTable, UNSIGNED32* pulCount,
                               UNSIGNED16 /*usFilterOption*/) {
    auto* st = get_sqlite_table(hTable);
    if (pulCount == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    if (st->conn == nullptr) return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    if (st->rec_count_cached) {
        *pulCount = st->cached_rec_count;
        return ok();
    }
    auto r = st->conn->record_count(st);
    if (!r) return fail(r.error());
    st->cached_rec_count = r.value();
    st->rec_count_cached = true;
    *pulCount = st->cached_rec_count;
    return ok();
}

UNSIGNED32 sqlite_get_field(ADSHANDLE hTable, UNSIGNED8* pucField,
                            UNSIGNED8* pucBuf, UNSIGNED32* pulLen,
                            UNSIGNED16 /*usOption*/) {
    auto* st = get_sqlite_table(hTable);
    if (pulLen == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    if (st->conn == nullptr) return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    auto fname = openads::abi::to_internal(pucField, 0);
    bool is_null = false;
    std::string val;
    auto r = st->conn->read_field(st, fname, val, is_null);
    if (!r) return fail(r.error());
    if (is_null) val.clear();
    auto fi = sqlite_field_index(st, pucField);
    if (fi != std::numeric_limits<std::size_t>::max() &&
        st->fields[fi].type == ADS_STRING) {
        val = pad_char_field(std::move(val), st->fields[fi].length);
    }
    openads::abi::copy_to_caller(pucBuf, pulLen, val);
    return ok();
}

UNSIGNED32 sqlite_is_record_deleted(ADSHANDLE hTable, UNSIGNED16* pbDeleted) {
    auto* st = get_sqlite_table(hTable);
    *pbDeleted = st->current_deleted ? 1 : 0;
    return ok();
}

UNSIGNED32 sqlite_open_index(ADSHANDLE hTable, UNSIGNED8* pucName,
                             ADSHANDLE* ahIndex, UNSIGNED16* pu16ArrayLen) {
    auto* st = get_sqlite_table(hTable);
    if (pu16ArrayLen != nullptr && *pu16ArrayLen < 1) {
        return fail(openads::AE_INTERNAL_ERROR, "index array too small");
    }
    std::string tag = openads::abi::to_internal(pucName, 0);
    if (tag.empty()) {
        return fail(openads::AE_INTERNAL_ERROR, "empty index tag");
    }
    const auto dot = tag.find_last_of("./\\");
    if (dot != std::string::npos) {
        tag = tag.substr(dot + 1);
    }
    const auto dot2 = tag.find('.');
    if (dot2 != std::string::npos) {
        tag = tag.substr(0, dot2);
    }
    auto si = std::make_unique<openads::sql_backend::SqliteIndex>();
    si->parent = st;
    si->column = tag;
    auto& s = state();
    std::lock_guard<std::recursive_mutex> lk(s.mu);
    Handle gh = s.registry.register_object(
        HandleKind::SqliteIndex, si.get());
    ahIndex[0] = gh;
    if (pu16ArrayLen != nullptr) {
        *pu16ArrayLen = 1;
    }
    sqlite_indexes_map().emplace(gh, std::move(si));
    return ok();
}

UNSIGNED32 sqlite_is_found(ADSHANDLE hTable, UNSIGNED16* pbFound) {
    auto* st = get_sqlite_table(hTable);
    *pbFound = st->last_seek_found ? 1 : 0;
    return ok();
}

const openads::abi::BackendTableOps* sqlite_table_ops() {
    static const openads::abi::BackendTableOps ops = [] {
        openads::abi::BackendTableOps o{};
        o.close_table       = &sqlite_close_table;
        o.goto_top          = &sqlite_goto_top;
        o.goto_bottom       = &sqlite_goto_bottom;
        o.skip              = &sqlite_skip;
        o.at_eof            = &sqlite_at_eof;
        o.at_bof            = &sqlite_at_bof;
        o.num_fields        = &sqlite_num_fields;
        o.field_name        = &sqlite_field_name;
        o.field_type        = &sqlite_field_type;
        o.field_length      = &sqlite_field_length;
        o.field_decimals    = &sqlite_field_decimals;
        o.record_num        = &sqlite_record_num;
        o.record_count      = &sqlite_record_count;
        o.get_field         = &sqlite_get_field;
        o.is_record_deleted = &sqlite_is_record_deleted;
        o.open_index        = &sqlite_open_index;
        o.is_found          = &sqlite_is_found;
        return o;
    }();
    return &ops;
}

#endif // OPENADS_WITH_SQLITE (lifted ops)

// ---------------------------------------------------------------------------
// Lifted ODBC table ops + accessor (mirrors the SQLite lift; bodies are
// the per-function inline ODBC dispatch moved verbatim behind the
// registry so the 17 ABI functions stay backend-agnostic).
// ---------------------------------------------------------------------------
#if defined(OPENADS_WITH_ODBC)

UNSIGNED32 odbc_close_table(ADSHANDLE hTable) {
    auto* st = get_odbc_table(hTable);
    (void)st;
    auto& s2 = state();
    std::lock_guard<std::recursive_mutex> lk2(s2.mu);
    odbc_tables_map().erase(hTable);
    s2.registry.release(hTable);
    return ok();
}

UNSIGNED32 odbc_goto_top(ADSHANDLE hTable) {
    auto* st = get_odbc_table(hTable);
    if (st->conn == nullptr) return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    auto r = st->conn->goto_top(st);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 odbc_goto_bottom(ADSHANDLE hTable) {
    auto* st = get_odbc_table(hTable);
    if (st->conn == nullptr) return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    auto r = st->conn->goto_bottom(st);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 odbc_skip(ADSHANDLE hTable, SIGNED32 lRows) {
    auto* st = get_odbc_table(hTable);
    if (st->conn == nullptr) return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    auto r = st->conn->skip(st, lRows);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 odbc_at_eof(ADSHANDLE hTable, UNSIGNED16* pbAtEnd) {
    auto* st = get_odbc_table(hTable);
    if (pbAtEnd == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    if (st->conn == nullptr) return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    auto r = st->conn->at_eof(st);
    if (!r) return fail(r.error());
    *pbAtEnd = r.value() ? 1 : 0;
    return ok();
}

UNSIGNED32 odbc_at_bof(ADSHANDLE hTable, UNSIGNED16* pbAtBof) {
    auto* st = get_odbc_table(hTable);
    if (st->conn == nullptr) return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    auto r = st->conn->at_bof(st);
    if (!r) return fail(r.error());
    *pbAtBof = r.value() ? 1 : 0;
    return ok();
}

UNSIGNED32 odbc_num_fields(ADSHANDLE hTable, UNSIGNED16* pusCnt) {
    auto* st = get_odbc_table(hTable);
    if (st->conn == nullptr) return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    if (!st->fields_cached) {
        auto r = st->conn->describe_table(st);
        if (!r) return fail(r.error());
    }
    *pusCnt = static_cast<UNSIGNED16>(st->fields.size());
    return ok();
}

UNSIGNED32 odbc_field_name(ADSHANDLE hTable, UNSIGNED16 n,
                               UNSIGNED8* pucBuf, UNSIGNED16* pusLen) {
    auto* st = get_odbc_table(hTable);
    if (st->conn == nullptr) return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    if (!st->fields_cached) {
        auto r = st->conn->describe_table(st);
        if (!r) return fail(r.error());
    }
    if (n == 0 || n > st->fields.size()) {
        return fail(openads::AE_COLUMN_NOT_FOUND, "");
    }
    openads::abi::copy_to_caller(pucBuf, pusLen, st->fields[n - 1].name);
    return ok();
}

UNSIGNED32 odbc_field_type(ADSHANDLE hTable, UNSIGNED8* pucField,
                               UNSIGNED16* pusType) {
    auto* st = get_odbc_table(hTable);
    auto i = odbc_field_index(st, pucField);
    if (i == std::numeric_limits<std::size_t>::max()) {
        return fail(openads::AE_COLUMN_NOT_FOUND, "");
    }
    *pusType = st->fields[i].type;
    return ok();
}

UNSIGNED32 odbc_field_length(ADSHANDLE hTable, UNSIGNED8* pucField,
                                 UNSIGNED32* pulLen) {
    auto* st = get_odbc_table(hTable);
    auto i = odbc_field_index(st, pucField);
    if (i == std::numeric_limits<std::size_t>::max()) {
        return fail(openads::AE_COLUMN_NOT_FOUND, "");
    }
    *pulLen = st->fields[i].length;
    return ok();
}

UNSIGNED32 odbc_field_decimals(ADSHANDLE hTable, UNSIGNED8* pucField,
                                   UNSIGNED16* pusDec) {
    auto* st = get_odbc_table(hTable);
    auto i = odbc_field_index(st, pucField);
    if (i == std::numeric_limits<std::size_t>::max()) {
        return fail(openads::AE_COLUMN_NOT_FOUND, "");
    }
    *pusDec = st->fields[i].decimals;
    return ok();
}

UNSIGNED32 odbc_record_num(ADSHANDLE hTable, UNSIGNED32* pulRec) {
    auto* st = get_odbc_table(hTable);
    if (!st->positioned || !st->row_valid) {
        return fail(5026, "no current record");
    }
    *pulRec = static_cast<UNSIGNED32>(st->current_recno);
    return ok();
}

UNSIGNED32 odbc_record_count(ADSHANDLE hTable, UNSIGNED32* pulCount,
                                 UNSIGNED16 /*usFilterOption*/) {
    auto* st = get_odbc_table(hTable);
    if (pulCount == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    if (st->conn == nullptr) return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    if (st->rec_count_cached) {
        *pulCount = st->cached_rec_count;
        return ok();
    }
    auto r = st->conn->record_count(st);
    if (!r) return fail(r.error());
    st->cached_rec_count = r.value();
    st->rec_count_cached = true;
    *pulCount = st->cached_rec_count;
    return ok();
}

UNSIGNED32 odbc_get_field(ADSHANDLE hTable, UNSIGNED8* pucField,
                              UNSIGNED8* pucBuf, UNSIGNED32* pulLen,
                              UNSIGNED16 /*usOption*/) {
    auto* st = get_odbc_table(hTable);
    if (pulLen == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    if (st->conn == nullptr) return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    auto fname = openads::abi::to_internal(pucField, 0);
    bool is_null = false;
    std::string val;
    auto r = st->conn->read_field(st, fname, val, is_null);
    if (!r) return fail(r.error());
    if (is_null) val.clear();
    auto fi = odbc_field_index(st, pucField);
    if (fi != std::numeric_limits<std::size_t>::max() &&
        st->fields[fi].type == ADS_STRING) {
        val = pad_char_field(std::move(val), st->fields[fi].length);
    }
    openads::abi::copy_to_caller(pucBuf, pulLen, val);
    return ok();
}

UNSIGNED32 odbc_is_record_deleted(ADSHANDLE hTable, UNSIGNED16* pbDeleted) {
    auto* st = get_odbc_table(hTable);
    *pbDeleted = st->current_deleted ? 1 : 0;
    return ok();
}

UNSIGNED32 odbc_open_index(ADSHANDLE hTable, UNSIGNED8* pucName,
                               ADSHANDLE* ahIndex, UNSIGNED16* pu16ArrayLen) {
    auto* st = get_odbc_table(hTable);
    if (pu16ArrayLen != nullptr && *pu16ArrayLen < 1) {
        return fail(openads::AE_INTERNAL_ERROR, "index array too small");
    }
    std::string tag = openads::abi::to_internal(pucName, 0);
    if (tag.empty()) {
        return fail(openads::AE_INTERNAL_ERROR, "empty index tag");
    }
    const auto dot = tag.find_last_of("./\\");
    if (dot != std::string::npos) {
        tag = tag.substr(dot + 1);
    }
    const auto dot2 = tag.find('.');
    if (dot2 != std::string::npos) {
        tag = tag.substr(0, dot2);
    }
    auto si = std::make_unique<openads::sql_backend::OdbcIndex>();
    si->parent = st;
    si->column = tag;
    auto& s = state();
    std::lock_guard<std::recursive_mutex> lk(s.mu);
    Handle gh = s.registry.register_object(
        HandleKind::OdbcIndex, si.get());
    ahIndex[0] = gh;
    if (pu16ArrayLen != nullptr) {
        *pu16ArrayLen = 1;
    }
    odbc_indexes_map().emplace(gh, std::move(si));
    return ok();
}

UNSIGNED32 odbc_is_found(ADSHANDLE hTable, UNSIGNED16* pbFound) {
    auto* st = get_odbc_table(hTable);
    *pbFound = st->last_seek_found ? 1 : 0;
    return ok();
}

const openads::abi::BackendTableOps* odbc_table_ops() {
    static const openads::abi::BackendTableOps ops = [] {
        openads::abi::BackendTableOps o{};
        o.close_table       = &odbc_close_table;
        o.goto_top          = &odbc_goto_top;
        o.goto_bottom       = &odbc_goto_bottom;
        o.skip              = &odbc_skip;
        o.at_eof            = &odbc_at_eof;
        o.at_bof            = &odbc_at_bof;
        o.num_fields        = &odbc_num_fields;
        o.field_name        = &odbc_field_name;
        o.field_type        = &odbc_field_type;
        o.field_length      = &odbc_field_length;
        o.field_decimals    = &odbc_field_decimals;
        o.record_num        = &odbc_record_num;
        o.record_count      = &odbc_record_count;
        o.get_field         = &odbc_get_field;
        o.is_record_deleted = &odbc_is_record_deleted;
        o.open_index        = &odbc_open_index;
        o.is_found          = &odbc_is_found;
        return o;
    }();
    return &ops;
}

#endif // OPENADS_WITH_ODBC (lifted ops)

// ---------------------------------------------------------------------------
// Lifted MSSQL table ops + accessor
// ---------------------------------------------------------------------------
#if defined(OPENADS_WITH_MSSQL)

UNSIGNED32 mssql_close_table(ADSHANDLE hTable) {
    auto* st = get_mssql_table(hTable);
    (void)st;
    auto& s2 = state();
    std::lock_guard<std::recursive_mutex> lk2(s2.mu);
    mssql_tables_map().erase(hTable);
    s2.registry.release(hTable);
    return ok();
}

UNSIGNED32 mssql_goto_top(ADSHANDLE hTable) {
    auto* st = get_mssql_table(hTable);
    if (!st) return fail(openads::AE_INTERNAL_ERROR, "");
    st->go_top();
    return ok();
}

UNSIGNED32 mssql_goto_bottom(ADSHANDLE hTable) {
    auto* st = get_mssql_table(hTable);
    if (!st) return fail(openads::AE_INTERNAL_ERROR, "");
    st->go_bottom();
    return ok();
}

UNSIGNED32 mssql_skip(ADSHANDLE hTable, SIGNED32 lRows) {
    auto* st = get_mssql_table(hTable);
    if (!st) return fail(openads::AE_INTERNAL_ERROR, "");
    st->skip(static_cast<long>(lRows));
    return ok();
}

UNSIGNED32 mssql_at_eof(ADSHANDLE hTable, UNSIGNED16* pbAtEnd) {
    auto* st = get_mssql_table(hTable);
    if (!st) return fail(openads::AE_INTERNAL_ERROR, "");
    if (pbAtEnd == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    *pbAtEnd = st->at_eof() ? 1 : 0;
    return ok();
}

UNSIGNED32 mssql_at_bof(ADSHANDLE hTable, UNSIGNED16* pbAtBegin) {
    auto* st = get_mssql_table(hTable);
    if (!st) return fail(openads::AE_INTERNAL_ERROR, "");
    *pbAtBegin = st->at_bof() ? 1 : 0;
    return ok();
}

UNSIGNED32 mssql_num_fields(ADSHANDLE hTable, UNSIGNED16* pusCnt) {
    auto* st = get_mssql_table(hTable);
    if (!st) return fail(openads::AE_INTERNAL_ERROR, "");
    if (pusCnt == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    *pusCnt = static_cast<UNSIGNED16>(st->field_count());
    return ok();
}

UNSIGNED32 mssql_field_name(ADSHANDLE hTable, UNSIGNED16 n,
                               UNSIGNED8* pucBuf, UNSIGNED16* pusLen) {
    auto* st = get_mssql_table(hTable);
    if (!st) return fail(openads::AE_INTERNAL_ERROR, "");
    if (n == 0 || n > st->field_count()) {
        return fail(openads::AE_COLUMN_NOT_FOUND, "");
    }
    auto name = st->field_name(n - 1);
    openads::abi::copy_to_caller(pucBuf, pusLen, name);
    return ok();
}

UNSIGNED32 mssql_field_type(ADSHANDLE hTable, UNSIGNED8* pucField,
                               UNSIGNED16* pusType) {
    auto* st = get_mssql_table(hTable);
    if (!st) return fail(openads::AE_INTERNAL_ERROR, "");
    auto i = mssql_field_index(st, pucField);
    if (i == std::numeric_limits<std::size_t>::max()) {
        return fail(openads::AE_COLUMN_NOT_FOUND, "");
    }
    if (pusType) *pusType = st->field_type(i);
    return ok();
}

UNSIGNED32 mssql_field_length(ADSHANDLE hTable, UNSIGNED8* pucField,
                                 UNSIGNED32* pulLen) {
    auto* st = get_mssql_table(hTable);
    if (!st) return fail(openads::AE_INTERNAL_ERROR, "");
    auto i = mssql_field_index(st, pucField);
    if (i == std::numeric_limits<std::size_t>::max()) {
        return fail(openads::AE_COLUMN_NOT_FOUND, "");
    }
    if (pulLen) *pulLen = st->field_length(i);
    return ok();
}

UNSIGNED32 mssql_field_decimals(ADSHANDLE hTable, UNSIGNED8* pucField,
                                   UNSIGNED16* pusDec) {
    auto* st = get_mssql_table(hTable);
    if (!st) return fail(openads::AE_INTERNAL_ERROR, "");
    auto i = mssql_field_index(st, pucField);
    if (i == std::numeric_limits<std::size_t>::max()) {
        return fail(openads::AE_COLUMN_NOT_FOUND, "");
    }
    if (pusDec) *pusDec = st->field_decimals(i);
    return ok();
}

UNSIGNED32 mssql_record_num(ADSHANDLE hTable, UNSIGNED32* pulRec) {
    auto* st = get_mssql_table(hTable);
    if (!st) return fail(openads::AE_INTERNAL_ERROR, "");
    if (pulRec == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    *pulRec = st->record_num();
    return ok();
}

UNSIGNED32 mssql_record_count(ADSHANDLE hTable, UNSIGNED32* pulCount,
                                 UNSIGNED16 /*usFilterOption*/) {
    auto* st = get_mssql_table(hTable);
    if (!st) return fail(openads::AE_INTERNAL_ERROR, "");
    if (pulCount == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    *pulCount = st->record_count();
    return ok();
}

UNSIGNED32 mssql_get_field(ADSHANDLE hTable, UNSIGNED8* pucField,
                              UNSIGNED8* pucBuf, UNSIGNED32* pulLen,
                              UNSIGNED16 /*usOption*/) {
    auto* st = get_mssql_table(hTable);
    if (!st) return fail(openads::AE_INTERNAL_ERROR, "");
    if (pulLen == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    auto fi = mssql_field_index(st, pucField);
    if (fi == std::numeric_limits<std::size_t>::max()) {
        return fail(openads::AE_COLUMN_NOT_FOUND, "");
    }
    bool is_null = false;
    std::string val;
    if (!st->get_field(fi, val, is_null)) {
        return fail(openads::AE_INTERNAL_ERROR, "");
    }
    if (is_null) val.clear();
    openads::abi::copy_to_caller(pucBuf, pulLen, val);
    return ok();
}

UNSIGNED32 mssql_is_record_deleted(ADSHANDLE hTable, UNSIGNED16* pbDeleted) {
    auto* st = get_mssql_table(hTable);
    if (!st) return fail(openads::AE_INTERNAL_ERROR, "");
    if (pbDeleted) *pbDeleted = 0;
    return ok();
}

const openads::abi::BackendTableOps* mssql_table_ops() {
    static const openads::abi::BackendTableOps ops = [] {
        openads::abi::BackendTableOps o{};
        o.close_table       = &mssql_close_table;
        o.goto_top          = &mssql_goto_top;
        o.goto_bottom       = &mssql_goto_bottom;
        o.skip              = &mssql_skip;
        o.at_eof            = &mssql_at_eof;
        o.at_bof            = &mssql_at_bof;
        o.num_fields        = &mssql_num_fields;
        o.field_name        = &mssql_field_name;
        o.field_type        = &mssql_field_type;
        o.field_length      = &mssql_field_length;
        o.field_decimals    = &mssql_field_decimals;
        o.record_num        = &mssql_record_num;
        o.record_count      = &mssql_record_count;
        o.get_field         = &mssql_get_field;
        o.is_record_deleted = &mssql_is_record_deleted;
        return o;
    }();
    return &ops;
}
#endif // OPENADS_WITH_MSSQL (lifted ops)

// ---------------------------------------------------------------------------
// Lifted Firebird table ops + accessor (mirrors the ODBC lift exactly; the
// native Firebird backend exposes the same read-navigation surface — its
// extra write / run_sql methods are not part of BackendTableOps and are not
// wired at the ABI border in this slice, matching the ODBC read-only border).
// ---------------------------------------------------------------------------
#if defined(OPENADS_WITH_FIREBIRD)

UNSIGNED32 firebird_close_table(ADSHANDLE hTable) {
    auto* st = get_firebird_table(hTable);
    (void)st;
    auto& s2 = state();
    std::lock_guard<std::recursive_mutex> lk2(s2.mu);
    firebird_tables_map().erase(hTable);
    s2.registry.release(hTable);
    return ok();
}

UNSIGNED32 firebird_goto_top(ADSHANDLE hTable) {
    auto* st = get_firebird_table(hTable);
    if (st->conn == nullptr) return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    auto r = st->conn->goto_top(st);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 firebird_goto_bottom(ADSHANDLE hTable) {
    auto* st = get_firebird_table(hTable);
    if (st->conn == nullptr) return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    auto r = st->conn->goto_bottom(st);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 firebird_skip(ADSHANDLE hTable, SIGNED32 lRows) {
    auto* st = get_firebird_table(hTable);
    if (st->conn == nullptr) return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    auto r = st->conn->skip(st, lRows);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 firebird_at_eof(ADSHANDLE hTable, UNSIGNED16* pbAtEnd) {
    auto* st = get_firebird_table(hTable);
    if (pbAtEnd == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    if (st->conn == nullptr) return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    auto r = st->conn->at_eof(st);
    if (!r) return fail(r.error());
    *pbAtEnd = r.value() ? 1 : 0;
    return ok();
}

UNSIGNED32 firebird_at_bof(ADSHANDLE hTable, UNSIGNED16* pbAtBof) {
    auto* st = get_firebird_table(hTable);
    if (st->conn == nullptr) return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    auto r = st->conn->at_bof(st);
    if (!r) return fail(r.error());
    *pbAtBof = r.value() ? 1 : 0;
    return ok();
}

UNSIGNED32 firebird_num_fields(ADSHANDLE hTable, UNSIGNED16* pusCnt) {
    auto* st = get_firebird_table(hTable);
    if (st->conn == nullptr) return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    if (!st->fields_cached) {
        auto r = st->conn->describe_table(st);
        if (!r) return fail(r.error());
    }
    *pusCnt = static_cast<UNSIGNED16>(st->fields.size());
    return ok();
}

UNSIGNED32 firebird_field_name(ADSHANDLE hTable, UNSIGNED16 n,
                               UNSIGNED8* pucBuf, UNSIGNED16* pusLen) {
    auto* st = get_firebird_table(hTable);
    if (st->conn == nullptr) return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    if (!st->fields_cached) {
        auto r = st->conn->describe_table(st);
        if (!r) return fail(r.error());
    }
    if (n == 0 || n > st->fields.size()) {
        return fail(openads::AE_COLUMN_NOT_FOUND, "");
    }
    openads::abi::copy_to_caller(pucBuf, pusLen, st->fields[n - 1].name);
    return ok();
}

UNSIGNED32 firebird_field_type(ADSHANDLE hTable, UNSIGNED8* pucField,
                               UNSIGNED16* pusType) {
    auto* st = get_firebird_table(hTable);
    auto i = firebird_field_index(st, pucField);
    if (i == std::numeric_limits<std::size_t>::max()) {
        return fail(openads::AE_COLUMN_NOT_FOUND, "");
    }
    *pusType = st->fields[i].type;
    return ok();
}

UNSIGNED32 firebird_field_length(ADSHANDLE hTable, UNSIGNED8* pucField,
                                 UNSIGNED32* pulLen) {
    auto* st = get_firebird_table(hTable);
    auto i = firebird_field_index(st, pucField);
    if (i == std::numeric_limits<std::size_t>::max()) {
        return fail(openads::AE_COLUMN_NOT_FOUND, "");
    }
    *pulLen = st->fields[i].length;
    return ok();
}

UNSIGNED32 firebird_field_decimals(ADSHANDLE hTable, UNSIGNED8* pucField,
                                   UNSIGNED16* pusDec) {
    auto* st = get_firebird_table(hTable);
    auto i = firebird_field_index(st, pucField);
    if (i == std::numeric_limits<std::size_t>::max()) {
        return fail(openads::AE_COLUMN_NOT_FOUND, "");
    }
    *pusDec = st->fields[i].decimals;
    return ok();
}

UNSIGNED32 firebird_record_num(ADSHANDLE hTable, UNSIGNED32* pulRec) {
    auto* st = get_firebird_table(hTable);
    if (!st->positioned || !st->row_valid) {
        return fail(5026, "no current record");
    }
    *pulRec = static_cast<UNSIGNED32>(st->current_recno);
    return ok();
}

UNSIGNED32 firebird_record_count(ADSHANDLE hTable, UNSIGNED32* pulCount,
                                 UNSIGNED16 /*usFilterOption*/) {
    auto* st = get_firebird_table(hTable);
    if (pulCount == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    if (st->conn == nullptr) return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    if (st->rec_count_cached) {
        *pulCount = st->cached_rec_count;
        return ok();
    }
    auto r = st->conn->record_count(st);
    if (!r) return fail(r.error());
    st->cached_rec_count = r.value();
    st->rec_count_cached = true;
    *pulCount = st->cached_rec_count;
    return ok();
}

UNSIGNED32 firebird_get_field(ADSHANDLE hTable, UNSIGNED8* pucField,
                              UNSIGNED8* pucBuf, UNSIGNED32* pulLen,
                              UNSIGNED16 /*usOption*/) {
    auto* st = get_firebird_table(hTable);
    if (pulLen == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    if (st->conn == nullptr) return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    auto fname = openads::abi::to_internal(pucField, 0);
    bool is_null = false;
    std::string val;
    auto r = st->conn->read_field(st, fname, val, is_null);
    if (!r) return fail(r.error());
    if (is_null) val.clear();
    auto fi = firebird_field_index(st, pucField);
    if (fi != std::numeric_limits<std::size_t>::max() &&
        st->fields[fi].type == ADS_STRING) {
        val = pad_char_field(std::move(val), st->fields[fi].length);
    }
    openads::abi::copy_to_caller(pucBuf, pulLen, val);
    return ok();
}

UNSIGNED32 firebird_is_record_deleted(ADSHANDLE hTable, UNSIGNED16* pbDeleted) {
    auto* st = get_firebird_table(hTable);
    *pbDeleted = st->current_deleted ? 1 : 0;
    return ok();
}

UNSIGNED32 firebird_open_index(ADSHANDLE hTable, UNSIGNED8* pucName,
                               ADSHANDLE* ahIndex, UNSIGNED16* pu16ArrayLen) {
    auto* st = get_firebird_table(hTable);
    if (pu16ArrayLen != nullptr && *pu16ArrayLen < 1) {
        return fail(openads::AE_INTERNAL_ERROR, "index array too small");
    }
    std::string tag = openads::abi::to_internal(pucName, 0);
    if (tag.empty()) {
        return fail(openads::AE_INTERNAL_ERROR, "empty index tag");
    }
    const auto dot = tag.find_last_of("./\\");
    if (dot != std::string::npos) {
        tag = tag.substr(dot + 1);
    }
    const auto dot2 = tag.find('.');
    if (dot2 != std::string::npos) {
        tag = tag.substr(0, dot2);
    }
    auto si = std::make_unique<openads::sql_backend::FirebirdIndex>();
    si->parent = st;
    si->column = tag;
    auto& s = state();
    std::lock_guard<std::recursive_mutex> lk(s.mu);
    Handle gh = s.registry.register_object(
        HandleKind::FirebirdIndex, si.get());
    ahIndex[0] = gh;
    if (pu16ArrayLen != nullptr) {
        *pu16ArrayLen = 1;
    }
    firebird_indexes_map().emplace(gh, std::move(si));
    return ok();
}

UNSIGNED32 firebird_is_found(ADSHANDLE hTable, UNSIGNED16* pbFound) {
    auto* st = get_firebird_table(hTable);
    *pbFound = st->last_seek_found ? 1 : 0;
    return ok();
}

const openads::abi::BackendTableOps* firebird_table_ops() {
    static const openads::abi::BackendTableOps ops = [] {
        openads::abi::BackendTableOps o{};
        o.close_table       = &firebird_close_table;
        o.goto_top          = &firebird_goto_top;
        o.goto_bottom       = &firebird_goto_bottom;
        o.skip              = &firebird_skip;
        o.at_eof            = &firebird_at_eof;
        o.at_bof            = &firebird_at_bof;
        o.num_fields        = &firebird_num_fields;
        o.field_name        = &firebird_field_name;
        o.field_type        = &firebird_field_type;
        o.field_length      = &firebird_field_length;
        o.field_decimals    = &firebird_field_decimals;
        o.record_num        = &firebird_record_num;
        o.record_count      = &firebird_record_count;
        o.get_field         = &firebird_get_field;
        o.is_record_deleted = &firebird_is_record_deleted;
        o.open_index        = &firebird_open_index;
        o.is_found          = &firebird_is_found;
        return o;
    }();
    return &ops;
}

#endif // OPENADS_WITH_FIREBIRD (lifted ops)

// ---------------------------------------------------------------------------
// Lifted MariaDB table ops + accessor (mirrors the SQLite lift; bodies are
// the per-function inline MariaDB dispatch moved verbatim behind the
// registry so the 17 ABI functions stay backend-agnostic).
// ---------------------------------------------------------------------------
#if defined(OPENADS_WITH_MARIADB)

UNSIGNED32 maria_close_table(ADSHANDLE hTable) {
    auto* st = get_maria_table(hTable);
    (void)st;
    auto& s2 = state();
    std::lock_guard<std::recursive_mutex> lk2(s2.mu);
    maria_tables_map().erase(hTable);
    s2.registry.release(hTable);
    return ok();
}

UNSIGNED32 maria_goto_top(ADSHANDLE hTable) {
    auto* st = get_maria_table(hTable);
    if (st->conn == nullptr) return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    auto r = st->conn->goto_top(st);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 maria_goto_bottom(ADSHANDLE hTable) {
    auto* st = get_maria_table(hTable);
    if (st->conn == nullptr) return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    auto r = st->conn->goto_bottom(st);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 maria_skip(ADSHANDLE hTable, SIGNED32 lRows) {
    auto* st = get_maria_table(hTable);
    if (st->conn == nullptr) return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    auto r = st->conn->skip(st, lRows);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 maria_at_eof(ADSHANDLE hTable, UNSIGNED16* pbAtEnd) {
    auto* st = get_maria_table(hTable);
    if (pbAtEnd == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    if (st->conn == nullptr) return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    auto r = st->conn->at_eof(st);
    if (!r) return fail(r.error());
    *pbAtEnd = r.value() ? 1 : 0;
    return ok();
}

UNSIGNED32 maria_at_bof(ADSHANDLE hTable, UNSIGNED16* pbAtBof) {
    auto* st = get_maria_table(hTable);
    if (st->conn == nullptr) return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    auto r = st->conn->at_bof(st);
    if (!r) return fail(r.error());
    *pbAtBof = r.value() ? 1 : 0;
    return ok();
}

UNSIGNED32 maria_num_fields(ADSHANDLE hTable, UNSIGNED16* pusCnt) {
    auto* st = get_maria_table(hTable);
    if (st->conn == nullptr) return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    if (!st->fields_cached) {
        auto r = st->conn->describe_table(st);
        if (!r) return fail(r.error());
    }
    *pusCnt = static_cast<UNSIGNED16>(st->fields.size());
    return ok();
}

UNSIGNED32 maria_field_name(ADSHANDLE hTable, UNSIGNED16 n,
                               UNSIGNED8* pucBuf, UNSIGNED16* pusLen) {
    auto* st = get_maria_table(hTable);
    if (st->conn == nullptr) return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    if (!st->fields_cached) {
        auto r = st->conn->describe_table(st);
        if (!r) return fail(r.error());
    }
    if (n == 0 || n > st->fields.size()) {
        return fail(openads::AE_COLUMN_NOT_FOUND, "");
    }
    openads::abi::copy_to_caller(pucBuf, pusLen, st->fields[n - 1].name);
    return ok();
}

UNSIGNED32 maria_field_type(ADSHANDLE hTable, UNSIGNED8* pucField,
                               UNSIGNED16* pusType) {
    auto* st = get_maria_table(hTable);
    auto i = maria_field_index(st, pucField);
    if (i == std::numeric_limits<std::size_t>::max()) {
        return fail(openads::AE_COLUMN_NOT_FOUND, "");
    }
    *pusType = st->fields[i].type;
    return ok();
}

UNSIGNED32 maria_field_length(ADSHANDLE hTable, UNSIGNED8* pucField,
                                 UNSIGNED32* pulLen) {
    auto* st = get_maria_table(hTable);
    auto i = maria_field_index(st, pucField);
    if (i == std::numeric_limits<std::size_t>::max()) {
        return fail(openads::AE_COLUMN_NOT_FOUND, "");
    }
    *pulLen = st->fields[i].length;
    return ok();
}

UNSIGNED32 maria_field_decimals(ADSHANDLE hTable, UNSIGNED8* pucField,
                                   UNSIGNED16* pusDec) {
    auto* st = get_maria_table(hTable);
    auto i = maria_field_index(st, pucField);
    if (i == std::numeric_limits<std::size_t>::max()) {
        return fail(openads::AE_COLUMN_NOT_FOUND, "");
    }
    *pusDec = st->fields[i].decimals;
    return ok();
}

UNSIGNED32 maria_record_num(ADSHANDLE hTable, UNSIGNED32* pulRec) {
    auto* st = get_maria_table(hTable);
    if (!st->positioned || !st->row_valid) {
        return fail(5026, "no current record");
    }
    *pulRec = static_cast<UNSIGNED32>(st->current_recno);
    return ok();
}

UNSIGNED32 maria_record_count(ADSHANDLE hTable, UNSIGNED32* pulCount,
                                 UNSIGNED16 /*usFilterOption*/) {
    auto* st = get_maria_table(hTable);
    if (pulCount == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    if (st->conn == nullptr) return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    if (st->rec_count_cached) {
        *pulCount = st->cached_rec_count;
        return ok();
    }
    auto r = st->conn->record_count(st);
    if (!r) return fail(r.error());
    st->cached_rec_count = r.value();
    st->rec_count_cached = true;
    *pulCount = st->cached_rec_count;
    return ok();
}

UNSIGNED32 maria_get_field(ADSHANDLE hTable, UNSIGNED8* pucField,
                              UNSIGNED8* pucBuf, UNSIGNED32* pulLen,
                              UNSIGNED16 /*usOption*/) {
    auto* st = get_maria_table(hTable);
    if (pulLen == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    if (st->conn == nullptr) return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    auto fname = openads::abi::to_internal(pucField, 0);
    bool is_null = false;
    std::string val;
    auto r = st->conn->read_field(st, fname, val, is_null);
    if (!r) return fail(r.error());
    if (is_null) val.clear();
    auto fi = maria_field_index(st, pucField);
    if (fi != std::numeric_limits<std::size_t>::max() &&
        st->fields[fi].type == ADS_STRING) {
        val = pad_char_field(std::move(val), st->fields[fi].length);
    }
    openads::abi::copy_to_caller(pucBuf, pulLen, val);
    return ok();
}

UNSIGNED32 maria_is_record_deleted(ADSHANDLE hTable, UNSIGNED16* pbDeleted) {
    auto* st = get_maria_table(hTable);
    *pbDeleted = st->current_deleted ? 1 : 0;
    return ok();
}

UNSIGNED32 maria_open_index(ADSHANDLE hTable, UNSIGNED8* pucName,
                               ADSHANDLE* ahIndex, UNSIGNED16* pu16ArrayLen) {
    auto* st = get_maria_table(hTable);
    if (pu16ArrayLen != nullptr && *pu16ArrayLen < 1) {
        return fail(openads::AE_INTERNAL_ERROR, "index array too small");
    }
    std::string tag = openads::abi::to_internal(pucName, 0);
    if (tag.empty()) {
        return fail(openads::AE_INTERNAL_ERROR, "empty index tag");
    }
    const auto dot = tag.find_last_of("./\\");
    if (dot != std::string::npos) {
        tag = tag.substr(dot + 1);
    }
    const auto dot2 = tag.find('.');
    if (dot2 != std::string::npos) {
        tag = tag.substr(0, dot2);
    }
    auto si = std::make_unique<openads::sql_backend::MariaIndex>();
    si->parent = st;
    si->column = tag;
    auto& s = state();
    std::lock_guard<std::recursive_mutex> lk(s.mu);
    Handle gh = s.registry.register_object(
        HandleKind::MariaIndex, si.get());
    ahIndex[0] = gh;
    if (pu16ArrayLen != nullptr) {
        *pu16ArrayLen = 1;
    }
    maria_indexes_map().emplace(gh, std::move(si));
    return ok();
}

UNSIGNED32 maria_is_found(ADSHANDLE hTable, UNSIGNED16* pbFound) {
    auto* st = get_maria_table(hTable);
    *pbFound = st->last_seek_found ? 1 : 0;
    return ok();
}

const openads::abi::BackendTableOps* maria_table_ops() {
    static const openads::abi::BackendTableOps ops = [] {
        openads::abi::BackendTableOps o{};
        o.close_table       = &maria_close_table;
        o.goto_top          = &maria_goto_top;
        o.goto_bottom       = &maria_goto_bottom;
        o.skip              = &maria_skip;
        o.at_eof            = &maria_at_eof;
        o.at_bof            = &maria_at_bof;
        o.num_fields        = &maria_num_fields;
        o.field_name        = &maria_field_name;
        o.field_type        = &maria_field_type;
        o.field_length      = &maria_field_length;
        o.field_decimals    = &maria_field_decimals;
        o.record_num        = &maria_record_num;
        o.record_count      = &maria_record_count;
        o.get_field         = &maria_get_field;
        o.is_record_deleted = &maria_is_record_deleted;
        o.open_index        = &maria_open_index;
        o.is_found          = &maria_is_found;
        return o;
    }();
    return &ops;
}

#endif // OPENADS_WITH_MARIADB (lifted ops)

// ---------------------------------------------------------------------------
// Lifted PostgreSQL table ops + accessor (mirrors the SQLite lift; bodies are
// the per-function inline PostgreSQL dispatch moved verbatim behind the
// registry so the 17 ABI functions stay backend-agnostic).
// ---------------------------------------------------------------------------
#if defined(OPENADS_WITH_POSTGRESQL)

UNSIGNED32 postgres_close_table(ADSHANDLE hTable) {
    auto* st = get_postgres_table(hTable);
    (void)st;
    auto& s2 = state();
    std::lock_guard<std::recursive_mutex> lk2(s2.mu);
    postgres_tables_map().erase(hTable);
    s2.registry.release(hTable);
    return ok();
}

UNSIGNED32 postgres_goto_top(ADSHANDLE hTable) {
    auto* st = get_postgres_table(hTable);
    if (st->conn == nullptr) return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    auto r = st->conn->goto_top(st);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 postgres_goto_bottom(ADSHANDLE hTable) {
    auto* st = get_postgres_table(hTable);
    if (st->conn == nullptr) return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    auto r = st->conn->goto_bottom(st);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 postgres_skip(ADSHANDLE hTable, SIGNED32 lRows) {
    auto* st = get_postgres_table(hTable);
    if (st->conn == nullptr) return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    auto r = st->conn->skip(st, lRows);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 postgres_at_eof(ADSHANDLE hTable, UNSIGNED16* pbAtEnd) {
    auto* st = get_postgres_table(hTable);
    if (pbAtEnd == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    if (st->conn == nullptr) return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    auto r = st->conn->at_eof(st);
    if (!r) return fail(r.error());
    *pbAtEnd = r.value() ? 1 : 0;
    return ok();
}

UNSIGNED32 postgres_at_bof(ADSHANDLE hTable, UNSIGNED16* pbAtBof) {
    auto* st = get_postgres_table(hTable);
    if (st->conn == nullptr) return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    auto r = st->conn->at_bof(st);
    if (!r) return fail(r.error());
    *pbAtBof = r.value() ? 1 : 0;
    return ok();
}

UNSIGNED32 postgres_num_fields(ADSHANDLE hTable, UNSIGNED16* pusCnt) {
    auto* st = get_postgres_table(hTable);
    if (st->conn == nullptr) return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    if (!st->fields_cached) {
        auto r = st->conn->describe_table(st);
        if (!r) return fail(r.error());
    }
    *pusCnt = static_cast<UNSIGNED16>(st->fields.size());
    return ok();
}

UNSIGNED32 postgres_field_name(ADSHANDLE hTable, UNSIGNED16 n,
                               UNSIGNED8* pucBuf, UNSIGNED16* pusLen) {
    auto* st = get_postgres_table(hTable);
    if (st->conn == nullptr) return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    if (!st->fields_cached) {
        auto r = st->conn->describe_table(st);
        if (!r) return fail(r.error());
    }
    if (n == 0 || n > st->fields.size()) {
        return fail(openads::AE_COLUMN_NOT_FOUND, "");
    }
    openads::abi::copy_to_caller(pucBuf, pusLen, st->fields[n - 1].name);
    return ok();
}

UNSIGNED32 postgres_field_type(ADSHANDLE hTable, UNSIGNED8* pucField,
                               UNSIGNED16* pusType) {
    auto* st = get_postgres_table(hTable);
    auto i = postgres_field_index(st, pucField);
    if (i == std::numeric_limits<std::size_t>::max()) {
        return fail(openads::AE_COLUMN_NOT_FOUND, "");
    }
    *pusType = st->fields[i].type;
    return ok();
}

UNSIGNED32 postgres_field_length(ADSHANDLE hTable, UNSIGNED8* pucField,
                                 UNSIGNED32* pulLen) {
    auto* st = get_postgres_table(hTable);
    auto i = postgres_field_index(st, pucField);
    if (i == std::numeric_limits<std::size_t>::max()) {
        return fail(openads::AE_COLUMN_NOT_FOUND, "");
    }
    *pulLen = st->fields[i].length;
    return ok();
}

UNSIGNED32 postgres_field_decimals(ADSHANDLE hTable, UNSIGNED8* pucField,
                                   UNSIGNED16* pusDec) {
    auto* st = get_postgres_table(hTable);
    auto i = postgres_field_index(st, pucField);
    if (i == std::numeric_limits<std::size_t>::max()) {
        return fail(openads::AE_COLUMN_NOT_FOUND, "");
    }
    *pusDec = st->fields[i].decimals;
    return ok();
}

UNSIGNED32 postgres_record_num(ADSHANDLE hTable, UNSIGNED32* pulRec) {
    auto* st = get_postgres_table(hTable);
    if (!st->positioned || !st->row_valid) {
        return fail(5026, "no current record");
    }
    *pulRec = static_cast<UNSIGNED32>(st->current_recno);
    return ok();
}

UNSIGNED32 postgres_record_count(ADSHANDLE hTable, UNSIGNED32* pulCount,
                                 UNSIGNED16 /*usFilterOption*/) {
    auto* st = get_postgres_table(hTable);
    if (pulCount == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    if (st->conn == nullptr) return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    if (st->rec_count_cached) {
        *pulCount = st->cached_rec_count;
        return ok();
    }
    auto r = st->conn->record_count(st);
    if (!r) return fail(r.error());
    st->cached_rec_count = r.value();
    st->rec_count_cached = true;
    *pulCount = st->cached_rec_count;
    return ok();
}

UNSIGNED32 postgres_get_field(ADSHANDLE hTable, UNSIGNED8* pucField,
                              UNSIGNED8* pucBuf, UNSIGNED32* pulLen,
                              UNSIGNED16 /*usOption*/) {
    auto* st = get_postgres_table(hTable);
    if (pulLen == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    if (st->conn == nullptr) return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    auto fname = openads::abi::to_internal(pucField, 0);
    bool is_null = false;
    std::string val;
    auto r = st->conn->read_field(st, fname, val, is_null);
    if (!r) return fail(r.error());
    if (is_null) val.clear();
    auto fi = postgres_field_index(st, pucField);
    if (fi != std::numeric_limits<std::size_t>::max() &&
        st->fields[fi].type == ADS_STRING) {
        val = pad_char_field(std::move(val), st->fields[fi].length);
    }
    openads::abi::copy_to_caller(pucBuf, pulLen, val);
    return ok();
}

UNSIGNED32 postgres_is_record_deleted(ADSHANDLE hTable, UNSIGNED16* pbDeleted) {
    auto* st = get_postgres_table(hTable);
    *pbDeleted = st->current_deleted ? 1 : 0;
    return ok();
}

UNSIGNED32 postgres_open_index(ADSHANDLE hTable, UNSIGNED8* pucName,
                               ADSHANDLE* ahIndex, UNSIGNED16* pu16ArrayLen) {
    auto* st = get_postgres_table(hTable);
    if (pu16ArrayLen != nullptr && *pu16ArrayLen < 1) {
        return fail(openads::AE_INTERNAL_ERROR, "index array too small");
    }
    std::string tag = openads::abi::to_internal(pucName, 0);
    if (tag.empty()) {
        return fail(openads::AE_INTERNAL_ERROR, "empty index tag");
    }
    const auto dot = tag.find_last_of("./\\");
    if (dot != std::string::npos) {
        tag = tag.substr(dot + 1);
    }
    const auto dot2 = tag.find('.');
    if (dot2 != std::string::npos) {
        tag = tag.substr(0, dot2);
    }
    auto si = std::make_unique<openads::sql_backend::PostgresIndex>();
    si->parent = st;
    si->column = tag;
    auto& s = state();
    std::lock_guard<std::recursive_mutex> lk(s.mu);
    Handle gh = s.registry.register_object(
        HandleKind::PostgresIndex, si.get());
    ahIndex[0] = gh;
    if (pu16ArrayLen != nullptr) {
        *pu16ArrayLen = 1;
    }
    postgres_indexes_map().emplace(gh, std::move(si));
    return ok();
}

UNSIGNED32 postgres_is_found(ADSHANDLE hTable, UNSIGNED16* pbFound) {
    auto* st = get_postgres_table(hTable);
    *pbFound = st->last_seek_found ? 1 : 0;
    return ok();
}

const openads::abi::BackendTableOps* postgres_table_ops() {
    static const openads::abi::BackendTableOps ops = [] {
        openads::abi::BackendTableOps o{};
        o.close_table       = &postgres_close_table;
        o.goto_top          = &postgres_goto_top;
        o.goto_bottom       = &postgres_goto_bottom;
        o.skip              = &postgres_skip;
        o.at_eof            = &postgres_at_eof;
        o.at_bof            = &postgres_at_bof;
        o.num_fields        = &postgres_num_fields;
        o.field_name        = &postgres_field_name;
        o.field_type        = &postgres_field_type;
        o.field_length      = &postgres_field_length;
        o.field_decimals    = &postgres_field_decimals;
        o.record_num        = &postgres_record_num;
        o.record_count      = &postgres_record_count;
        o.get_field         = &postgres_get_field;
        o.is_record_deleted = &postgres_is_record_deleted;
        o.open_index        = &postgres_open_index;
        o.is_found          = &postgres_is_found;
        return o;
    }();
    return &ops;
}

#endif // OPENADS_WITH_POSTGRESQL (lifted ops)

// ---------------------------------------------------------------------------
// Referential Integrity enforcement
// ---------------------------------------------------------------------------

// "AdsAppendRecord called but AdsWriteRecord hasn't fired yet" is tracked
// per-Table via Table::pending_append() — see table.h. It used to be a
// global std::unordered_set<Table*>, but a freed table's heap address
// could be reused by a different table that still carried the stale
// "pending append" flag, making a plain UPDATE take the INSERT path and
// silently skip RI cascade/restrict enforcement (intermittent, heap-
// layout dependent). Storing the flag on the Table removes that aliasing.

// Recursion guard: prevents RI cascade actions from triggering a second
// round of RI checks on the child table.
bool& in_ri_check() {
    static thread_local bool flag = false;
    return flag;
}

// Find the Connection that owns Table* t.
Connection* conn_for_table(Table* t) {
    auto& s = state();
    Connection* found = nullptr;
    s.registry.for_each_handle([&](Handle, HandleKind k, void* p) {
        if (k != HandleKind::Connection || found) return;
        auto* c = static_cast<Connection*>(p);
        if (c->owns_table_ptr(t)) found = c;
    });
    return found;
}

// Find the DD alias for a table given its resolved absolute path.
std::string ri_alias_for_path(Connection* conn, const std::string& abs_path) {
    namespace fs = std::filesystem;
    auto* dd = conn->dd();
    if (!dd) return {};
    std::error_code ec;
    auto cb = fs::weakly_canonical(fs::path(abs_path), ec).string();
    for (auto& [alias, rel] : dd->tables()) {
        fs::path full = fs::path(conn->data_dir()) / rel;
        auto ca = fs::weakly_canonical(full, ec).string();
        if (ca.size() != cb.size()) continue;
        bool eq = true;
        for (std::size_t i = 0; i < ca.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(ca[i])) !=
                std::tolower(static_cast<unsigned char>(cb[i]))) {
                eq = false; break;
            }
        }
        if (eq) return alias;
    }
    return {};
}

// Right-trim spaces from a DBF field value.
std::string ri_trim(const std::string& s) {
    auto i = s.find_last_not_of(' ');
    return (i == std::string::npos) ? std::string{} : s.substr(0, i + 1);
}

// Read a named field from the current record buffer.
std::string ri_read_field(Table& tbl, const std::string& name) {
    auto idx = tbl.field_index(name);
    if (idx < 0) return {};
    auto v = tbl.read_field(static_cast<std::uint16_t>(idx));
    return v ? v.value().as_string : std::string{};
}

// Scan `tbl` for any live row where field `fname` equals `key` (trimmed).
// Returns true if at least one match is found; if `recnos` is non-null
// it collects ALL matching record numbers.
bool ri_scan(Table& tbl, const std::string& fname, const std::string& key,
             std::vector<std::uint32_t>* recnos) {
    bool any = false;
    if (auto r = tbl.goto_top(); !r) return false;
    while (!tbl.eof()) {
        if (!tbl.is_deleted()) {
            if (ri_trim(ri_read_field(tbl, fname)) == key) {
                any = true;
                if (recnos) recnos->push_back(tbl.recno());
                else        return true;   // RESTRICT: one match is enough
            }
        }
        (void)tbl.skip(1);
    }
    return any;
}

// Called from AdsWriteRecord when the record was freshly appended.
// Validates that every FK field exists in the referenced parent table.
openads::util::Result<void> ri_check_insert(Connection* conn, Table& child) {
    if (in_ri_check()) return {};
    auto* dd = conn->dd();
    if (!dd || dd->ri().empty()) return {};
    std::string child_alias = ri_alias_for_path(conn, child.path());
    if (child_alias.empty()) return {};

    for (auto& [rname, rule] : dd->ri()) {
        if (rule.child != child_alias) continue;
        // rule.parent_tag is the parent index tag name; by convention (single-field
        // PK/FK) the same name identifies the FK field in the child.
        std::string fk_val = ri_trim(ri_read_field(child, rule.parent_tag));
        if (fk_val.empty()) continue;   // NULL / blank FK → skip

        auto ph = conn->open_table(rule.parent,
                                   openads::engine::TableType::Cdx,
                                   openads::engine::OpenMode::Read);
        if (!ph) {
            return openads::util::Error{
                openads::AE_RI_VIOLATION, 0,
                "RI: cannot open parent table '" + rule.parent + "'", rname};
        }
        Table* parent = conn->lookup_table(ph.value());
        bool found = parent && ri_scan(*parent, rule.parent_tag, fk_val, nullptr);
        conn->close_table(ph.value());
        if (!found) {
            return openads::util::Error{
                openads::AE_RI_VIOLATION, 0,
                "RI violation: FK value '" + fk_val +
                    "' not found in parent '" + rule.parent + "'",
                rname};
        }
    }
    return {};
}

// Called from AdsDeleteRecord before marking the row deleted.
// Enforces delete_opt rules for every RI rule where this table is the parent.
openads::util::Result<void> ri_enforce_delete(Connection* conn, Table& parent) {
    if (in_ri_check()) return {};
    auto* dd = conn->dd();
    if (!dd || dd->ri().empty()) return {};
    std::string parent_alias = ri_alias_for_path(conn, parent.path());
    if (parent_alias.empty()) return {};

    for (auto& [rname, rule] : dd->ri()) {
        if (rule.parent != parent_alias) continue;
        std::string pk_val = ri_trim(ri_read_field(parent, rule.parent_tag));
        if (pk_val.empty()) continue;

        unsigned del_opt = ADS_DD_RI_RESTRICT;
        if (!rule.delete_opt.empty()) {
            try { del_opt = static_cast<unsigned>(
                    std::stoul(rule.delete_opt)); } catch (...) {}
        }

        bool need_write = (del_opt == ADS_DD_RI_CASCADE ||
                           del_opt == ADS_DD_RI_SETNULL ||
                           del_opt == ADS_DD_RI_SETDEFAULT);
        // Reuse the application's already-open child instance when present
        // (see ri_enforce_update for why a second open races and drops the
        // action). Open a fresh one only as a fallback.
        Table* child = conn->find_open_table(rule.child);
        bool   opened_here = false;
        Handle ch_handle = 0;
        if (!child) {
            auto ch = conn->open_table(rule.child,
                                       openads::engine::TableType::Cdx,
                                       need_write ? openads::engine::OpenMode::Shared
                                                  : openads::engine::OpenMode::Read);
            if (!ch) continue;   // can't open child → skip rule
            child = conn->lookup_table(ch.value());
            if (!child) { conn->close_table(ch.value()); continue; }
            opened_here = true;
            ch_handle   = ch.value();
        }

        if (del_opt == ADS_DD_RI_RESTRICT) {
            bool any = ri_scan(*child, rule.parent_tag, pk_val, nullptr);
            if (opened_here) conn->close_table(ch_handle);
            if (any) {
                return openads::util::Error{
                    openads::AE_RI_VIOLATION, 0,
                    "RI violation: child rows exist in '" + rule.child + "'",
                    rname};
            }
        } else {
            // Collect matching recnos first, then apply action.
            std::vector<std::uint32_t> matches;
            ri_scan(*child, rule.parent_tag, pk_val, &matches);
            in_ri_check() = true;
            for (std::uint32_t rec : matches) {
                if (auto gr = child->goto_record(rec); !gr) continue;
                if (del_opt == ADS_DD_RI_CASCADE) {
                    (void)child->mark_deleted();
                } else {
                    // SETNULL / SETDEFAULT: blank the FK field.
                    auto fi = child->field_index(rule.parent_tag);
                    if (fi >= 0) {
                        auto fi16 = static_cast<std::uint16_t>(fi);
                        std::uint32_t flen =
                            child->field_descriptor(fi16).length;
                        (void)child->set_field(fi16,
                                               std::string(flen, ' '));
                    }
                }
            }
            in_ri_check() = false;
            if (!matches.empty()) (void)child->flush();
            if (opened_here) conn->close_table(ch_handle);
        }
    }
    return {};
}

// Called after every successful local-table navigation.
// If the table is a parent in any RI rule, snapshot its PK fields onto
// the Table itself (Table::ri_snapshot()). Storing the snapshot on the
// Table — rather than in a global Table*-keyed map — means it lives and
// dies with the table, so a freed-then-reallocated table can never
// inherit a previous table's stale snapshot (the cause of intermittent
// missed cascades/restrictions seen only in the full-suite run).
void snapshot_ri_pks(Table* t) {
    if (!t || t->eof()) {
        if (t) t->ri_snapshot().clear();
        return;
    }
    Connection* conn = conn_for_table(t);
    if (!conn) return;
    auto* dd = conn->dd();
    if (!dd || dd->ri().empty()) return;
    std::string alias = ri_alias_for_path(conn, t->path());
    if (alias.empty()) return;
    bool is_parent = false;
    for (auto& [rname, rule] : dd->ri()) {
        if (rule.parent == alias) { is_parent = true; break; }
    }
    if (!is_parent) return;
    auto& snap = t->ri_snapshot();
    snap.clear();
    for (auto& [rname, rule] : dd->ri()) {
        if (rule.parent != alias) continue;
        snap[rule.parent_tag] = ri_trim(ri_read_field(*t, rule.parent_tag));
    }
}

// Called from AdsWriteRecord for non-append writes (i.e. plain UPDATE).
// For every RI rule where this table is the parent, compares the new PK
// value (in the dirty buffer) against the old PK value snapshotted at
// navigation time. If they differ, enforces update_opt on the child table.
openads::util::Result<void> ri_enforce_update(Connection* conn, Table& parent) {
    if (in_ri_check()) return {};
    auto* dd = conn->dd();
    if (!dd || dd->ri().empty()) return {};
    std::string parent_alias = ri_alias_for_path(conn, parent.path());
    if (parent_alias.empty()) return {};

    for (auto& [rname, rule] : dd->ri()) {
        if (rule.parent != parent_alias) continue;

        unsigned upd_opt = ADS_DD_RI_RESTRICT;
        if (!rule.update_opt.empty()) {
            try { upd_opt = static_cast<unsigned>(
                    std::stoul(rule.update_opt)); } catch (...) {}
        }
        if (upd_opt == 0) continue;

        // New PK value is in the dirty buffer.
        std::string new_pk = ri_trim(ri_read_field(parent, rule.parent_tag));

        // Old PK value was snapshotted onto the parent Table at nav time.
        auto& snap = parent.ri_snapshot();
        auto fit = snap.find(rule.parent_tag);
        if (fit == snap.end()) continue;
        std::string old_pk = fit->second;

        if (old_pk == new_pk) continue;   // no PK change for this rule
        if (old_pk.empty()) continue;      // was blank (NULL) — skip

        bool need_write = (upd_opt == ADS_DD_RI_CASCADE ||
                           upd_opt == ADS_DD_RI_SETNULL ||
                           upd_opt == ADS_DD_RI_SETDEFAULT);
        // Prefer the child instance the application already has open on
        // this connection — cascading into a *second* open of the same
        // file races the OS file cache and share-mode locks, which
        // intermittently dropped the cascade/restrict. Only open (and
        // later close) a fresh instance when the child isn't already open.
        Table* child = conn->find_open_table(rule.child);
        bool   opened_here = false;
        Handle ch_handle = 0;
        if (!child) {
            auto ch = conn->open_table(rule.child,
                                       openads::engine::TableType::Cdx,
                                       need_write ? openads::engine::OpenMode::Shared
                                                  : openads::engine::OpenMode::Read);
            if (!ch) continue;
            child = conn->lookup_table(ch.value());
            if (!child) { conn->close_table(ch.value()); continue; }
            opened_here = true;
            ch_handle   = ch.value();
        }

        if (upd_opt == ADS_DD_RI_RESTRICT) {
            bool any = ri_scan(*child, rule.parent_tag, old_pk, nullptr);
            if (opened_here) conn->close_table(ch_handle);
            if (any) {
                // Restore parent's old PK to disk. set_field wrote the new
                // value immediately (writeback_record_), so we must undo it.
                auto rfi = parent.field_index(rule.parent_tag);
                if (rfi >= 0) {
                    auto rfi16 = static_cast<std::uint16_t>(rfi);
                    std::uint32_t rflen = parent.field_descriptor(rfi16).length;
                    std::string padded = old_pk;
                    padded.resize(rflen, ' ');
                    (void)parent.set_field(rfi16, padded);
                }
                return openads::util::Error{
                    openads::AE_RI_VIOLATION, 0,
                    "RI violation: child rows reference old PK '" + old_pk +
                        "' in '" + rule.child + "'",
                    rname};
            }
        } else {
            std::vector<std::uint32_t> matches;
            ri_scan(*child, rule.parent_tag, old_pk, &matches);
            in_ri_check() = true;
            auto fi = child->field_index(rule.parent_tag);
            if (fi >= 0) {
                auto fi16 = static_cast<std::uint16_t>(fi);
                std::uint32_t flen = child->field_descriptor(fi16).length;
                for (std::uint32_t rec : matches) {
                    if (auto gr = child->goto_record(rec); !gr) continue;
                    if (upd_opt == ADS_DD_RI_CASCADE) {
                        std::string padded = new_pk;
                        padded.resize(flen, ' ');
                        (void)child->set_field(fi16, padded);
                    } else {
                        // SETNULL / SETDEFAULT: blank the FK field.
                        (void)child->set_field(fi16, std::string(flen, ' '));
                    }
                }
            }
            in_ri_check() = false;
            if (!matches.empty()) (void)child->flush();
            if (opened_here) conn->close_table(ch_handle);
        }
    }
    return {};
}

// Returns effective permission level (0-4) for the authenticated user on a
// DD table alias. Returns 4 (full) when no ACL or no DD is present.
// Legacy — kept for callers that only need a coarse level.
[[maybe_unused]] int table_perm_level(Connection* conn, const std::string& alias) {
    if (!conn || !conn->has_dd()) return 4;
    auto* dd = conn->dd();
    if (!dd->has_table_acl(alias)) return 4;
    return dd->get_effective_permission(conn->username(), alias);
}

// Returns per-operation effective permissions for the connected user on object.
// If no DD / no ACL defined → all ops open.
openads::engine::DataDict::EffectiveOps
eff_ops(Connection* conn, const std::string& object_name) {
    openads::engine::DataDict::EffectiveOps full;
    full.open = full.select_ = full.update_ =
        full.insert_ = full.delete_ = full.execute_ = true;
    if (!conn || !conn->has_dd()) return full;
    auto* dd = conn->dd();
    if (!dd->has_any_acl()) return full;
    return dd->get_effective_ops(conn->username(), object_name);
}

// Resolve an AdsOpenTable name (alias, bare filename, or path) to a DD alias.
std::string name_to_alias(const openads::engine::DataDict* dd,
                           const std::string& name) {
    if (!dd) return {};
    if (dd->has_alias(name)) return name;
    namespace fs = std::filesystem;
    std::string stem = fs::path(name).stem().string();
    if (dd->has_alias(stem)) return stem;
    return {};
}

} // namespace

// RCB 2026-05-22 17:03 — set_stmt_param is defined later in the file alongside
// SqlStatement and stmt_map.  AdsSetString / AdsSetDouble / AdsSetLogical all
// call it before that definition is reached, so a forward declaration is needed
// here to satisfy the compiler.  It must be inside a namespace{} block to match
// the anonymous-namespace linkage of the definition; a file-scope static and an
// anonymous-namespace function are distinct symbols as far as MSVC is concerned.
namespace {
bool set_stmt_param(ADSHANDLE h, const char* pname, std::string literal);
} // namespace

// M9.16: chunked AdsSetBinary keeps a per-(table, field) accumulator;
// table teardown drains it. Forward-declared here so the close /
// disconnect paths above can call it before the definition arrives.
void purge_pending_binaries_for_table(openads::engine::Table* t);

// ---------------------------------------------------------------------------
// Task 3: register_builtin_backends + backend_table_ops_for
// Defined here (same TU as state() and sqlite_table_ops()) so both symbols
// are reachable without exposing new globals or changing the headers.
// ---------------------------------------------------------------------------
namespace openads::abi {

void register_builtin_backends() {
#if defined(OPENADS_WITH_SQLITE)
    register_backend_table_ops(openads::session::HandleKind::SqliteTable,
                               sqlite_table_ops());
#endif
#if defined(OPENADS_WITH_ODBC)
    register_backend_table_ops(openads::session::HandleKind::OdbcTable,
                               odbc_table_ops());
#endif
#if defined(OPENADS_WITH_MSSQL)
    register_backend_table_ops(openads::session::HandleKind::MssqlTable,
                               mssql_table_ops());
#endif
#if defined(OPENADS_WITH_FIREBIRD)
    register_backend_table_ops(openads::session::HandleKind::FirebirdTable,
                               firebird_table_ops());
#endif
#if defined(OPENADS_WITH_MARIADB)
    register_backend_table_ops(openads::session::HandleKind::MariaTable,
                               maria_table_ops());
#endif
#if defined(OPENADS_WITH_POSTGRESQL)
    register_backend_table_ops(openads::session::HandleKind::PostgresTable,
                               postgres_table_ops());
#endif
}

const BackendTableOps* backend_table_ops_for(ADSHANDLE h) {
    static const bool _ = (register_builtin_backends(), true);
    (void)_;
    return ops_for_kind(state().registry.kind_of(h));
}

}  // namespace openads::abi

extern "C" {

UNSIGNED32 AdsConnect60(UNSIGNED8* pucServer, UNSIGNED16 /*usServerType*/,
                        UNSIGNED8* pucUser, UNSIGNED8* pucPwd,
                        UNSIGNED32 /*ulOptions*/, ADSHANDLE* phConnect) {
    if (phConnect == nullptr) return fail(openads::AE_INTERNAL_ERROR,
                                          "phConnect is null");
    auto path = openads::abi::to_internal(pucServer, 0);
    // M12.5 — `tcp://host:port/<data_dir>` routes the connection
    // through the wire client; every Ads* function that recognises
    // the connection handle's RemoteConnection kind dispatches to
    // the server instead of touching a local Connection.
    // M12.9 — pucUser / pucPwd are forwarded into the Connect frame;
    // the server validates them when it has credentials registered.
    {
        // M12.12 — `tls://host:port/<dir>` URI. When the engine was
        // built with -DOPENADS_WITH_TLS=ON we open a real TLS client
        // through vendored mbedtls; otherwise we surface a clear
        // AE_FUNCTION_NOT_AVAILABLE so apps don't silently downgrade
        // to plaintext.
        std::string thost, tdir;
        std::uint16_t tport = 0;
        if (openads::network::parse_tls_uri(path, thost, tport, tdir)) {
#if defined(OPENADS_WITH_TLS)
            std::string user = pucUser ? openads::abi::to_internal(pucUser, 0)
                                       : std::string();
            std::string pw   = pucPwd  ? openads::abi::to_internal(pucPwd, 0)
                                       : std::string();
            openads::network::TlsConfig cfg;
            // For now, no CA bundle plumbed through the public ABI —
            // dev / self-signed setups skip verification. A future
            // milestone will let the caller pass a CA cert via an
            // AdsSetTlsCa-style entry point.
            cfg.insecure_skip_verify = true;
            cfg.sni_hostname         = thost;
            auto tt = openads::network::connect_tls(thost, tport, cfg);
            if (!tt) return fail(tt.error());
            auto rc = std::make_unique<openads::network::RemoteConnection>();
            if (auto r = rc->connect_with_transport(
                    std::move(tt).value(), tdir, user, pw); !r) {
                return fail(r.error());
            }
            auto& s = state();
            std::lock_guard<std::recursive_mutex> lk(s.mu);
            Handle h = s.registry.register_object(
                HandleKind::RemoteConnection, rc.get());
            static std::unordered_map<Handle,
                std::unique_ptr<openads::network::RemoteConnection>>
                remote_tls_conns;
            remote_tls_conns.emplace(h, std::move(rc));
            *phConnect = h;
            return ok();
#else
            return fail(openads::AE_FUNCTION_NOT_AVAILABLE,
                "tls:// URI requires building OpenADS with "
                "-DOPENADS_WITH_TLS=ON (vendors mbedtls 3.6 LTS)");
#endif
        }
    }
    {
        std::string host, dir;
        std::uint16_t port = 0;
        if (openads::network::parse_tcp_uri(path, host, port, dir)) {
            std::string user = pucUser ? openads::abi::to_internal(pucUser, 0)
                                       : std::string();
            std::string pw   = pucPwd  ? openads::abi::to_internal(pucPwd, 0)
                                       : std::string();
            auto rc = std::make_unique<openads::network::RemoteConnection>();
            if (auto r = rc->connect(host, port, dir, user, pw); !r) {
                return fail(r.error());
            }
            auto& s = state();
            std::lock_guard<std::recursive_mutex> lk(s.mu);
            Handle h = s.registry.register_object(
                HandleKind::RemoteConnection, rc.get());
            // Keep RemoteConnection alive in a side container.
            static std::unordered_map<Handle,
                std::unique_ptr<openads::network::RemoteConnection>>
                remote_conns;
            remote_conns.emplace(h, std::move(rc));
            *phConnect = h;
            return ok();
        }
    }
#if defined(OPENADS_WITH_SQLITE)
    {
        openads::sql_backend::SqliteUri suri;
        if (openads::sql_backend::parse_sqlite_uri(path, suri)) {
            auto opened = openads::sql_backend::SqliteConnection::open(suri);
            if (!opened) return fail(opened.error());
            auto holder = std::make_unique<openads::sql_backend::SqliteConnection>(
                std::move(opened).value());
            openads::sql_backend::SqliteConnection* raw = holder.get();
            auto& s = state();
            std::lock_guard<std::recursive_mutex> lk(s.mu);
            Handle h = s.registry.register_object(
                HandleKind::SqliteConnection, raw);
            sqlite_conns_map().emplace(h, std::move(holder));
            *phConnect = h;
            return ok();
        }
    }
#endif
#if defined(OPENADS_WITH_ODBC)
    {
        openads::sql_backend::OdbcUri ouri;
        if (openads::sql_backend::parse_odbc_uri(path, ouri)) {
            auto opened = openads::sql_backend::OdbcConnection::open(ouri);
            if (!opened) return fail(opened.error());
            auto holder =
                std::make_unique<openads::sql_backend::OdbcConnection>(
                    std::move(opened).value());
            openads::sql_backend::OdbcConnection* raw = holder.get();
            auto& s = state();
            std::lock_guard<std::recursive_mutex> lk(s.mu);
            Handle h = s.registry.register_object(
                HandleKind::OdbcConnection, raw);
            odbc_conns_map().emplace(h, std::move(holder));
            *phConnect = h;
            return ok();
        }
    }
#else
    {
        static constexpr const char* kOdbcPrefixes[] = {
            "odbc://", "odbc:",
        };
        for (const char* prefix : kOdbcPrefixes) {
            const auto plen = std::char_traits<char>::length(prefix);
            if (path.size() >= plen && path.compare(0, plen, prefix) == 0) {
                return fail(openads::AE_FUNCTION_NOT_AVAILABLE,
                            "odbc URI requires OPENADS_WITH_ODBC=ON");
            }
        }
    }
#endif
#if defined(OPENADS_WITH_MSSQL)
    {
        openads::sql_backend::MssqlUri muri;
        if (openads::sql_backend::parse_mssql_uri(path, muri)) {
            auto opened = openads::sql_backend::MssqlConnection::open(muri);
            if (!opened) return fail(opened.error());
            auto holder =
                std::make_unique<openads::sql_backend::MssqlConnection>(
                    std::move(opened).value());
            openads::sql_backend::MssqlConnection* raw = holder.get();
            auto& s = state();
            std::lock_guard<std::recursive_mutex> lk(s.mu);
            Handle h = s.registry.register_object(
                HandleKind::MssqlConnection, raw);
            mssql_conns_map().emplace(h, std::move(holder));
            *phConnect = h;
            return ok();
        }
    }
#else
    {
        static constexpr const char* kMssqlPrefixes[] = {
            "mssql://", "tds://",
        };
        for (const char* prefix : kMssqlPrefixes) {
            const auto plen = std::char_traits<char>::length(prefix);
            if (path.size() >= plen && path.compare(0, plen, prefix) == 0) {
                return fail(openads::AE_FUNCTION_NOT_AVAILABLE,
                            "mssql/tds URI requires OPENADS_WITH_MSSQL=ON");
            }
        }
    }
#endif
#if defined(OPENADS_WITH_FIREBIRD)
    // Native Firebird driver (firebird:// / fb://) — embedded `.fdb`
    // in-process via libfbclient, or a TCP server. Parsed into a
    // FirebirdUri (structured user/password/charset/role) and torn down
    // in AdsDisconnect.
    {
        openads::sql_backend::FirebirdUri furi;
        if (openads::sql_backend::parse_firebird_uri(path, furi)) {
            auto opened = openads::sql_backend::FirebirdConnection::open(furi);
            if (!opened) return fail(opened.error());
            auto holder =
                std::make_unique<openads::sql_backend::FirebirdConnection>(
                    std::move(opened).value());
            openads::sql_backend::FirebirdConnection* raw = holder.get();
            auto& s = state();
            std::lock_guard<std::recursive_mutex> lk(s.mu);
            Handle h = s.registry.register_object(
                HandleKind::FirebirdConnection, raw);
            firebird_conns_map().emplace(h, std::move(holder));
            *phConnect = h;
            return ok();
        }
    }
#else
    {
        static constexpr const char* kFirebirdPrefixes[] = {
            "firebird://", "firebird:", "fb://",
        };
        for (const char* prefix : kFirebirdPrefixes) {
            const auto plen = std::char_traits<char>::length(prefix);
            if (path.size() >= plen && path.compare(0, plen, prefix) == 0) {
                return fail(openads::AE_FUNCTION_NOT_AVAILABLE,
                            "firebird URI requires OPENADS_WITH_FIREBIRD=ON");
            }
        }
    }
#endif
#if defined(OPENADS_WITH_MARIADB)
    {
        openads::sql_backend::MariaUri muri;
        if (openads::sql_backend::parse_maria_uri(path, muri)) {
            auto opened = openads::sql_backend::MariaConnection::open(muri);
            if (!opened) return fail(opened.error());
            auto holder = std::make_unique<openads::sql_backend::MariaConnection>(
                std::move(opened).value());
            openads::sql_backend::MariaConnection* raw = holder.get();
            auto& s = state();
            std::lock_guard<std::recursive_mutex> lk(s.mu);
            Handle h = s.registry.register_object(
                HandleKind::MariaConnection, raw);
            maria_conns_map().emplace(h, std::move(holder));
            *phConnect = h;
            return ok();
        }
    }
#else
    {
        static constexpr const char* kMariaPrefixes[] = {
            "mariadb://", "mysql://",
        };
        for (const char* prefix : kMariaPrefixes) {
            const auto plen = std::char_traits<char>::length(prefix);
            if (path.size() >= plen && path.compare(0, plen, prefix) == 0) {
                return fail(openads::AE_FUNCTION_NOT_AVAILABLE,
                            "mariadb URI requires "
                            "OPENADS_WITH_MARIADB=ON");
            }
        }
    }
#endif
#if defined(OPENADS_WITH_POSTGRESQL)
    {
        openads::sql_backend::PostgresUri suri;
        if (openads::sql_backend::parse_postgres_uri(path, suri)) {
            auto opened = openads::sql_backend::PostgresConnection::open(suri);
            if (!opened) return fail(opened.error());
            auto holder = std::make_unique<openads::sql_backend::PostgresConnection>(
                std::move(opened).value());
            openads::sql_backend::PostgresConnection* raw = holder.get();
            auto& s = state();
            std::lock_guard<std::recursive_mutex> lk(s.mu);
            Handle h = s.registry.register_object(
                HandleKind::PostgresConnection, raw);
            postgres_conns_map().emplace(h, std::move(holder));
            *phConnect = h;
            return ok();
        }
    }
#else
    {
        static constexpr const char* kPgPrefixes[] = {
            "postgresql://", "postgres://", "pgsql://",
        };
        for (const char* prefix : kPgPrefixes) {
            const auto plen = std::char_traits<char>::length(prefix);
            if (path.size() >= plen && path.compare(0, plen, prefix) == 0) {
                return fail(openads::AE_FUNCTION_NOT_AVAILABLE,
                            "postgresql URI requires "
                            "OPENADS_WITH_POSTGRESQL=ON");
            }
        }
    }
#endif
    auto opened = Connection::open(path);
    if (!opened) return fail(opened.error());
    auto holder = std::make_unique<Connection>(std::move(opened).value());
    Connection* raw = holder.get();
    // DD authentication: if the DD has LOG_IN_REQUIRED set, validate
    // the supplied credentials before registering the connection.
    if (raw->has_dd()) {
        auto* dd = raw->dd();
        std::string login_req = dd->get_db_property("prop_5");
        // Stored as decimal string by import tool and UI: "0" = not required,
        // "1" = required.  Keep raw-byte fallback for any old imports.
        bool is_raw_zero = (login_req.size() >= 2 &&
                            static_cast<unsigned char>(login_req[0]) == 0 &&
                            static_cast<unsigned char>(login_req[1]) == 0);
        bool require_login = (!login_req.empty() && login_req != "0" &&
                              login_req != "False" && !is_raw_zero);
        std::string user = pucUser ? openads::abi::to_internal(pucUser, 0)
                                   : std::string();
        std::string pwd  = pucPwd  ? openads::abi::to_internal(pucPwd, 0)
                                   : std::string();
        if (require_login) {
            if (user.empty())
                return fail(openads::AE_LOGIN_FAILED,
                            "login required but no username supplied");
            if (!dd->has_user(user))
                return fail(openads::AE_LOGIN_FAILED, "unknown user");
            std::string stored = dd->get_user_property(user, "prop_1101");
            if (stored != pwd)
                return fail(openads::AE_LOGIN_FAILED, "invalid password");
        }
        if (!user.empty()) {
            raw->set_username(user);
            // Pre-build effective-permission cache for this user so that
            // subsequent AdsOpenTable / AdsExecuteSQLDirect checks are O(1).
            if (dd->has_any_acl())
                dd->build_perm_cache(user);
        }
    }
    auto& s = state();
    std::lock_guard<std::recursive_mutex> lk(s.mu);
    Handle h = s.registry.register_object(HandleKind::Connection, raw);
    s.conns.emplace(h, std::move(holder));
    *phConnect = h;
    rddads_default_connection() = h;
    // Reject connections to SAP proprietary binary .add files.  OpenADS
    // can read them (load_add_binary_) but cannot safely write them back
    // (format is closed and permission fields are encrypted).  Direct the
    // caller to run the import_dd tool to produce an OpenADS-format DD.
    if (raw->has_dd() && raw->dd()->has_sap_permissions()) {
        s.conns.erase(h);   // destroys the Connection object
        s.registry.release(h);
        *phConnect = 0;
        return fail(openads::AE_SAP_PERMS_NEED_IMPORT,
            "This is a SAP Advantage Data Dictionary in proprietary binary format. "
            "OpenADS cannot open it directly. "
            "Run: import_dd <source.add> <dest.add>  "
            "to convert it to OpenADS format, then connect to the converted file.");
    }
    return ok();
}

UNSIGNED32 AdsDisconnect(ADSHANDLE hConnect) {
    {
        auto& s_local = state();
        std::lock_guard<std::recursive_mutex> lk_local(s_local.mu);
#if defined(OPENADS_WITH_SQLITE)
        if (auto* sc = s_local.registry.lookup<openads::sql_backend::SqliteConnection>(
                hConnect, HandleKind::SqliteConnection)) {
            for (auto& kv : sqlite_tables_map()) {
                if (kv.second && kv.second->conn == sc) {
                    kv.second->conn = nullptr;
                }
            }
            sc->disconnect();
            sqlite_conns_map().erase(hConnect);
            s_local.registry.release(hConnect);
            return ok();
        }
#endif
#if defined(OPENADS_WITH_ODBC)
        if (auto* sc = s_local.registry.lookup<
                openads::sql_backend::OdbcConnection>(
                hConnect, HandleKind::OdbcConnection)) {
            for (auto& kv : odbc_tables_map()) {
                if (kv.second && kv.second->conn == sc) {
                    kv.second->conn = nullptr;
                }
            }
            sc->disconnect();
            odbc_conns_map().erase(hConnect);
            s_local.registry.release(hConnect);
            return ok();
        }
#endif
#if defined(OPENADS_WITH_MSSQL)
        if (auto* mc = s_local.registry.lookup<
                openads::sql_backend::MssqlConnection>(
                hConnect, HandleKind::MssqlConnection)) {
            mc->disconnect();
            mssql_conns_map().erase(hConnect);
            s_local.registry.release(hConnect);
            return ok();
        }
#endif
#if defined(OPENADS_WITH_FIREBIRD)
        if (auto* sc = s_local.registry.lookup<
                openads::sql_backend::FirebirdConnection>(
                hConnect, HandleKind::FirebirdConnection)) {
            for (auto& kv : firebird_tables_map()) {
                if (kv.second && kv.second->conn == sc) {
                    kv.second->conn = nullptr;
                }
            }
            sc->disconnect();
            firebird_conns_map().erase(hConnect);
            s_local.registry.release(hConnect);
            return ok();
        }
#endif
#if defined(OPENADS_WITH_MARIADB)
        if (auto* sc = s_local.registry.lookup<openads::sql_backend::MariaConnection>(
                hConnect, HandleKind::MariaConnection)) {
            for (auto& kv : maria_tables_map()) {
                if (kv.second && kv.second->conn == sc) {
                    kv.second->conn = nullptr;
                }
            }
            sc->disconnect();
            maria_conns_map().erase(hConnect);
            s_local.registry.release(hConnect);
            return ok();
        }
#endif
#if defined(OPENADS_WITH_POSTGRESQL)
        if (auto* sc = s_local.registry.lookup<openads::sql_backend::PostgresConnection>(
                hConnect, HandleKind::PostgresConnection)) {
            for (auto& kv : postgres_tables_map()) {
                if (kv.second && kv.second->conn == sc) {
                    kv.second->conn = nullptr;
                }
            }
            sc->disconnect();
            postgres_conns_map().erase(hConnect);
            s_local.registry.release(hConnect);
            return ok();
        }
#endif
        if (auto* rc = s_local.registry.lookup<openads::network::RemoteConnection>(
                hConnect, HandleKind::RemoteConnection)) {
            // Null out rt->conn on any open SQL cursors that reference this
            // connection so AdsCloseTable can detect the dangling case and
            // skip the wire op rather than crashing with a use-after-free.
            for (auto& kv : remote_sql_cursors_map()) {
                if (kv.second && kv.second->conn == rc)
                    kv.second->conn = nullptr;
            }
            rc->disconnect();
            return ok();
        }
    }
    auto& s = state();
    std::lock_guard<std::recursive_mutex> lk(s.mu);
    // Purge any index bindings whose Table* belongs to a table owned
    // by this connection — otherwise the bindings outlive the conns
    // entry that owned the Table and leave dangling pointers behind.
    Connection* c = s.registry.lookup<Connection>(hConnect, HandleKind::Connection);
    if (c != nullptr) {
        // Collect this connection's still-open Table handles. `s.conns.erase`
        // below frees the Connection — and with it every Table it owns — so
        // any registry slot still pointing at one of those Tables would dangle.
        // A later allocation reusing that heap address then aliases the stale
        // slot, which surfaces as AdsGetAllTables over-counting and, worse, a
        // use-after-free on any Ads* call that looks the stale handle up.
        // owns_table_ptr() identifies the owned tables precisely (the old
        // heuristic could not, so it purged every registered Table*).
        std::vector<Handle> owned_handles;
        std::vector<Table*> to_purge;
        s.registry.for_each_handle([&](Handle h, HandleKind k, void* p) {
            if (k != HandleKind::Table) return;
            Table* tp = static_cast<Table*>(p);
            if (tp == nullptr || !c->owns_table_ptr(tp)) return;
            owned_handles.push_back(h);
            to_purge.push_back(tp);
        });
        for (Table* tp : to_purge) {
            purge_bindings_for_table(tp);
            purge_pending_binaries_for_table(tp);
        }
        for (Handle h : owned_handles) {
            cursor_projections().erase(h);
            s.registry.release(h);
        }
    }
    if (hConnect == rddads_default_connection())
        rddads_default_connection() = 0;
    s.registry.release(hConnect);
    s.conns.erase(hConnect);
    return ok();
}

UNSIGNED32 AdsOpenTable(ADSHANDLE  hConnect,
                        UNSIGNED8* pucName,
                        UNSIGNED8* /*pucAlias*/,
                        UNSIGNED16 usTableType,
                        UNSIGNED16 /*usCharType*/,
                        UNSIGNED16 /*usLockType*/,
                        UNSIGNED16 usCheckRights,
                        UNSIGNED16 usMode,
                        ADSHANDLE* phTable) {
    if (phTable == nullptr) return fail(openads::AE_INTERNAL_ERROR,
                                        "phTable is null");
    auto& s = state();
    std::lock_guard<std::recursive_mutex> lk(s.mu);
    // M12.5 — remote connection handle: route through wire client.
    if (auto* rc = s.registry.lookup<openads::network::RemoteConnection>(
            hConnect, HandleKind::RemoteConnection)) {
        auto name = openads::abi::to_internal(pucName, 0);
        // Callers (incl. X#'s ADSRDD) pass the bare table name without
        // an extension; mirror the local path and default to .dbf so
        // the server resolves it.
        if (!std::filesystem::path(name).has_extension()) name += ".dbf";
        auto id = rc->open_table(name);
        if (!id) return fail(id.error());
        static std::unordered_map<Handle,
            std::unique_ptr<openads::network::RemoteTable>> remote_tables;
        auto rt = std::make_unique<openads::network::RemoteTable>();
        rt->conn = rc;
        rt->id   = id.value();
        rt->name = name;
        Handle gh = s.registry.register_object(
            HandleKind::RemoteTable, rt.get());
        remote_tables.emplace(gh, std::move(rt));
        *phTable = gh;
        return ok();
    }
#if defined(OPENADS_WITH_SQLITE)
    if (auto* sc = s.registry.lookup<openads::sql_backend::SqliteConnection>(
            hConnect, HandleKind::SqliteConnection)) {
        auto name = openads::abi::to_internal(pucName, 0);
        auto tbl = sc->open_table(name);
        if (!tbl) return fail(tbl.error());
        auto st = std::move(tbl).value();
        st->conn = sc;
        Handle gh = s.registry.register_object(
            HandleKind::SqliteTable, st.get());
        sqlite_tables_map().emplace(gh, std::move(st));
        *phTable = gh;
        return ok();
    }
#endif
#if defined(OPENADS_WITH_ODBC)
    if (auto* sc = s.registry.lookup<openads::sql_backend::OdbcConnection>(
            hConnect, HandleKind::OdbcConnection)) {
        auto name = openads::abi::to_internal(pucName, 0);
        auto tbl = sc->open_table(name);
        if (!tbl) return fail(tbl.error());
        auto st = std::move(tbl).value();
        st->conn = sc;
        Handle gh = s.registry.register_object(
            HandleKind::OdbcTable, st.get());
        odbc_tables_map().emplace(gh, std::move(st));
        *phTable = gh;
        return ok();
    }
#endif
#if defined(OPENADS_WITH_MSSQL)
    if (auto* mc = s.registry.lookup<openads::sql_backend::MssqlConnection>(
            hConnect, HandleKind::MssqlConnection)) {
        auto name = openads::abi::to_internal(pucName, 0);
        auto tbl = openads::sql_backend::MssqlTable::open(*mc, name);
        if (!tbl) return fail(tbl.error());
        auto st = std::move(tbl).value();
        Handle gh = s.registry.register_object(
            HandleKind::MssqlTable, st.get());
        mssql_tables_map().emplace(gh, std::move(st));
        *phTable = gh;
        return ok();
    }
#endif
#if defined(OPENADS_WITH_FIREBIRD)
    if (auto* sc = s.registry.lookup<openads::sql_backend::FirebirdConnection>(
            hConnect, HandleKind::FirebirdConnection)) {
        auto name = openads::abi::to_internal(pucName, 0);
        auto tbl = sc->open_table(name);
        if (!tbl) return fail(tbl.error());
        auto st = std::move(tbl).value();
        st->conn = sc;
        Handle gh = s.registry.register_object(
            HandleKind::FirebirdTable, st.get());
        firebird_tables_map().emplace(gh, std::move(st));
        *phTable = gh;
        return ok();
    }
#endif
#if defined(OPENADS_WITH_MARIADB)
    if (auto* sc = s.registry.lookup<openads::sql_backend::MariaConnection>(
            hConnect, HandleKind::MariaConnection)) {
        auto name = openads::abi::to_internal(pucName, 0);
        auto tbl = sc->open_table(name);
        if (!tbl) return fail(tbl.error());
        auto st = std::move(tbl).value();
        st->conn = sc;
        Handle gh = s.registry.register_object(
            HandleKind::MariaTable, st.get());
        maria_tables_map().emplace(gh, std::move(st));
        *phTable = gh;
        return ok();
    }
#endif
#if defined(OPENADS_WITH_POSTGRESQL)
    if (auto* sc = s.registry.lookup<openads::sql_backend::PostgresConnection>(
            hConnect, HandleKind::PostgresConnection)) {
        auto name = openads::abi::to_internal(pucName, 0);
        auto tbl = sc->open_table(name);
        if (!tbl) return fail(tbl.error());
        auto st = std::move(tbl).value();
        st->conn = sc;
        Handle gh = s.registry.register_object(
            HandleKind::PostgresTable, st.get());
        postgres_tables_map().emplace(gh, std::move(st));
        *phTable = gh;
        return ok();
    }
#endif
    auto* conn = s.registry.lookup<Connection>(hConnect, HandleKind::Connection);
    if (conn == nullptr) {
        ADSHANDLE def = get_or_create_default_connection();
        conn = s.registry.lookup<Connection>(def, HandleKind::Connection);
        if (conn == nullptr) {
            return fail(openads::AE_INVALID_CONNECTION_HANDLE,
                        "unknown connection");
        }
    }
    auto name = openads::abi::to_internal(pucName, 0);
    // View alias expansion: if the requested name matches a DD view, execute
    // the view's SQL and return the resulting cursor as the table handle.
    if (conn->has_dd()) {
        std::string uname = name;
        for (auto& ch : uname) ch = static_cast<char>(
            std::toupper(static_cast<unsigned char>(ch)));
        for (const auto& kv : conn->dd()->views()) {
            std::string vn = kv.first;
            for (auto& ch : vn) ch = static_cast<char>(
                std::toupper(static_cast<unsigned char>(ch)));
            if (vn == uname) {
                ADSHANDLE stmt_h = 0;
                UNSIGNED32 rc = AdsCreateSQLStatement(hConnect, &stmt_h);
                if (rc != 0) return rc;
                const std::string& vsql = kv.second.sql;
                std::vector<UNSIGNED8> sqlbuf(vsql.size() + 1);
                std::memcpy(sqlbuf.data(), vsql.data(), vsql.size());
                rc = AdsExecuteSQLDirect(stmt_h, sqlbuf.data(), phTable);
                AdsCloseSQLStatement(stmt_h);
                return rc;
            }
        }
    }
    // Per-table ACL check: when usCheckRights is non-zero and the connection
    // has an authenticated user with a DD ACL for this table, verify the
    // Check per-operation permissions for the open mode requested.
    if (usCheckRights != 0 && conn->has_dd() && !conn->username().empty()) {
        std::string alias = name_to_alias(conn->dd(), name);
        if (!alias.empty()) {
            auto ops = eff_ops(conn, alias);
            bool denied = (usMode == ADS_READONLY) ? !ops.select_
                                                    : !ops.update_ && !ops.insert_;
            if (denied)
                return fail(openads::AE_ACCESS_DENIED, alias.c_str());
        }
    }
    auto th = conn->open_table(name, map_type(usTableType));
    if (!th) return fail(th.error());
    Table* tbl = conn->lookup_table(th.value());
    Handle gh = s.registry.register_object(HandleKind::Table, tbl);
    *phTable = gh;

    // M-AOF.6 — production-CDX auto-open. ADS / rddads convention:
    // opening `<base>.dbf` auto-binds `<base>.cdx` if it exists, so
    // every tag inside it becomes navigable on this Table without
    // an explicit AdsOpenIndex60 call. Without this, the AOF
    // matcher in evaluate_optimised() never finds the index and
    // every leaf falls back to the per-record evaluation —
    // AdsGetAOFOptLevel reports NONE forever even after a
    // CREATE INDEX SQL ran in a prior session.
    namespace fs = std::filesystem;
    fs::path tp(tbl->path());
    if (tp.extension() == ".dbf" || tp.extension() == ".DBF") {
        fs::path cdx = tp; cdx.replace_extension(".cdx");
        std::error_code ec;
        if (fs::exists(cdx, ec)) {
            std::string cdxs = cdx.string();
            std::vector<UNSIGNED8> b(cdxs.size() + 1);
            std::memcpy(b.data(), cdxs.data(), cdxs.size());
            // Up to 64 tag handles is plenty for a production CDX.
            ADSHANDLE arr[64] = {0};
            UNSIGNED16 alen = 64;
            (void)AdsOpenIndex(gh, b.data(), arr, &alen);
        }
    }
    // ADI auto-open: same convention for ADT tables — opening `<base>.adt`
    // auto-binds `<base>.adi` if it exists, so every tag inside it becomes
    // navigable without an explicit AdsOpenIndex call.
    if (tp.extension() == ".adt" || tp.extension() == ".ADT") {
        fs::path adi = tp; adi.replace_extension(".adi");
        std::error_code ec;
        if (fs::exists(adi, ec)) {
            std::string adis = adi.string();
            std::vector<UNSIGNED8> b(adis.size() + 1);
            std::memcpy(b.data(), adis.data(), adis.size());
            ADSHANDLE arr[64] = {0};
            UNSIGNED16 alen = 64;
            (void)AdsOpenIndex(gh, b.data(), arr, &alen);
        }
    }
    return ok();
}

UNSIGNED32 AdsGetTableType(ADSHANDLE hTable, UNSIGNED16* pusType) {
    if (pusType == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    if (auto* rt = get_remote_table(hTable)) {
        auto r = rt->conn->get_table_type(rt->id);
        if (!r) return fail(r.error());
        *pusType = r.value();
        return ok();
    }
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "");
    // Map our internal TableType back to ACE constants. We only own
    // CDX and NTX today; the rest are out of scope for phase 1.
    namespace fs = std::filesystem;
    fs::path p(t->path());
    auto ext = p.extension().string();
    for (auto& c : ext) c = static_cast<char>(std::tolower(
                            static_cast<unsigned char>(c)));
    if (ext == ".dbf") {
        // Distinguishing CDX vs NTX vs VFP from a bare DBF requires
        // probing the matching index file alongside it, which we
        // don't do yet. Return CDX (the most common case).
        *pusType = ADS_CDX;
    } else if (ext == ".adt") {
        *pusType = ADS_ADT;
    } else {
        *pusType = ADS_CDX;
    }
    return ok();
}

UNSIGNED32 AdsGetTableFilename(ADSHANDLE hTable, UNSIGNED16 /*usOption*/,
                               UNSIGNED8* pucBuf, UNSIGNED16* pusLen) {
    if (pusLen == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    if (auto* rt = get_remote_table(hTable)) {
        openads::abi::copy_to_caller(pucBuf, pusLen, rt->name);
        return ok();
    }
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "");
    openads::abi::copy_to_caller(pucBuf, pusLen, t->path());
    return ok();
}

UNSIGNED32 AdsGetRecordLength(ADSHANDLE hTable, UNSIGNED32* pulLen) {
    if (pulLen == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    if (auto* rt = get_remote_table(hTable)) {
        auto r = rt->conn->get_record_length(rt->id);
        if (!r) return fail(r.error());
        *pulLen = r.value();
        return ok();
    }
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "");
    if (t->driver() == nullptr) { *pulLen = 0; return ok(); }
    *pulLen = t->driver()->record_length();
    return ok();
}

extern "C++" {
namespace {

// M10.33 — standard SQL LIKE pattern. `%` matches any sequence
// (including empty), `_` matches a single character. Greedy match
// with backtracking — adequate for short DBF cells.
static inline bool sql_like_match(const std::string& s,
                                  const std::string& pat) {
    std::size_t si = 0, pi = 0;
    std::size_t star = std::string::npos, ss = 0;
    while (si < s.size()) {
        if (pi < pat.size() &&
            (pat[pi] == '_' || pat[pi] == s[si])) {
            ++si; ++pi;
        } else if (pi < pat.size() && pat[pi] == '%') {
            star = pi++;
            ss   = si;
        } else if (star != std::string::npos) {
            pi = star + 1;
            si = ++ss;
        } else {
            return false;
        }
    }
    while (pi < pat.size() && pat[pi] == '%') ++pi;
    return pi == pat.size();
}

// ADS dialect — apply the optional UPPER()/LOWER() case-fold from a
// WHERE left-hand side to a cell value before it is compared. A no-op
// for WhereFn::None, so callers can apply it unconditionally.
static inline std::string apply_where_fn(std::string s,
                                         openads::sql::WhereFn fn) {
    if (fn == openads::sql::WhereFn::Upper) {
        for (char& ch : s)
            ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    } else if (fn == openads::sql::WhereFn::Lower) {
        for (char& ch : s)
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return s;
}

// Map an rddads type name (Character, Numeric, Date, ...) to the
// DBF field-descriptor type byte plus a default length / decimals
// when those aren't explicit in the field-def string.
struct DbfTypeSpec {
    char         type   = 'C';
    std::uint8_t length = 0;
    std::uint8_t dec    = 0;
    bool         needs_memo = false;
};

DbfTypeSpec dbf_type_for(const std::string& name) {
    auto eq = [&](const char* k) {
        if (name.size() != std::strlen(k)) return false;
        for (std::size_t i = 0; i < name.size(); ++i) {
            char a = static_cast<char>(std::tolower(
                            static_cast<unsigned char>(name[i])));
            char b = static_cast<char>(std::tolower(
                            static_cast<unsigned char>(k[i])));
            if (a != b) return false;
        }
        return true;
    };
    // Single-letter DBF type codes (the dBASE convention every
    // raw-DBF tool uses). Both the rddads verbose names AND the
    // one-letter codes flow through here so callers can pick
    // either style.
    if (name.size() == 1) {
        char c = static_cast<char>(std::toupper(
                     static_cast<unsigned char>(name[0])));
        switch (c) {
            case 'C': return {'C', 0,  0, false};
            case 'N': return {'N', 0,  0, false};
            case 'L': return {'L', 1,  0, false};
            case 'D': return {'D', 8,  0, false};
            case 'M': return {'M', 10, 0, true };
            case 'I': return {'N', 0,  0, false};   // integer-as-text
            case 'Y': return {'N', 0,  2, false};   // currency-as-text
            case 'B': return {'N', 0,  0, false};   // double-as-text
            case 'V': return {'V', 0,  0, false};   // varchar (M11.1)
            case 'Q': return {'Q', 0,  0, false};   // varbinary (M11.1)
            case 'T': return {'C',23,  0, false};   // ISO-8601 fallback
            default:  break;
        }
    }
    if (eq("Character") || eq("Char"))
        return {'C', 0, 0, false};
    if (eq("Numeric") || eq("Long") || eq("Number"))
        return {'N', 0, 0, false};
    if (eq("Logical") || eq("Bool"))
        return {'L', 1, 0, false};
    if (eq("Date") || eq("ShortDate"))
        return {'D', 8, 0, false};
    if (eq("Memo") || eq("NMemo"))
        return {'M', 10, 0, true};
    if (eq("Binary"))
        return {'Q', 9, 0, true};
    if (eq("Image"))
        return {'I', 9, 0, true};
    if (eq("Integer") || eq("LongLong"))
        return {'N', 0, 0, false};
    if (eq("Double") || eq("CurDouble"))
        return {'N', 0, 0, false};
    if (eq("ModTime"))
        return {'C', 23, 0, false};   // store as ISO-8601 string for now
    // ── ADT-specific type names: use sentinel chars handled by adt_spec_for ──
    if (eq("CICHARACTER") || eq("CiCharacter") || eq("CICHAR"))
        return {'W', 0, 0, false};    // ADT type 20: case-insensitive char
    if (eq("ShortInt") || eq("SmallInt") || eq("SMALLINT"))
        return {'S', 2, 0, false};    // ADT type 12: 2-byte int16
    if (eq("Money") || eq("Currency"))
        return {'$', 8, 0, false};    // ADT type 18: 8-byte int64 * 10000
    if (eq("Timestamp"))
        return {'P', 8, 0, false};    // ADT type 14: 8-byte JDN+ms
    if (eq("AutoInc"))
        return {'A', 4, 0, false};    // ADT type 15: 4-byte uint32 auto-increment
    if (eq("Time"))
        return {'Z', 4, 0, false};    // ADT type 13: 4-byte ms since midnight
    return {'C', 0, 0, false};        // unknown -> Character
}

// Trim leading/trailing whitespace.
std::string trim(std::string s) {
    while (!s.empty() && std::isspace(
                static_cast<unsigned char>(s.front()))) s.erase(s.begin());
    while (!s.empty() && std::isspace(
                static_cast<unsigned char>(s.back())))  s.pop_back();
    return s;
}

// rddads `NAME,Type,Len,Dec;…` parser. Empty `defs` returns an empty
// vector. Used by AdsCreateTable (M9.5) and AdsRestructureTable (M9.26).
struct FieldOut {
    std::string  name;
    char         type   = 'C';
    std::uint8_t length = 0;
    std::uint8_t dec    = 0;
};

std::vector<FieldOut> parse_rddads_field_defs(const std::string& defs) {
    std::vector<FieldOut> fields;
    std::string buf;
    auto flush = [&] {
        if (buf.empty()) return;
        std::vector<std::string> parts;
        std::string p;
        for (char c2 : buf) {
            if (c2 == ',') { parts.push_back(trim(p)); p.clear(); }
            else p.push_back(c2);
        }
        parts.push_back(trim(p));
        if (parts.size() >= 2) {
            DbfTypeSpec ts = dbf_type_for(parts[1]);
            FieldOut f;
            f.name = parts[0];
            // Each write path enforces its own limit:
            //   DBF: std::min(name.size(), 10) at the header-write site
            //   ADT: std::min(name.size(), 127u) at the field-descriptor site
            if (f.name.size() > 128) f.name.resize(128);
            f.type = ts.type;
            f.length = ts.length;
            f.dec    = ts.dec;
            if (parts.size() >= 3) {
                int n = std::atoi(parts[2].c_str());
                if (n > 0 && n < 256) f.length = static_cast<std::uint8_t>(n);
            }
            if (parts.size() >= 4) {
                int d = std::atoi(parts[3].c_str());
                if (d >= 0 && d < 256) f.dec = static_cast<std::uint8_t>(d);
            }
            if (f.length == 0) f.length = 10;
            fields.push_back(std::move(f));
        }
        buf.clear();
    };
    for (std::size_t i = 0; i <= defs.size(); ++i) {
        char ch = (i < defs.size()) ? defs[i] : ';';
        if (ch == ';') flush();
        else           buf.push_back(ch);
    }
    return fields;
}

// ADT field spec: ADT type code + fixed storage length for the creation path.
struct AdtFieldSpec {
    std::uint16_t adt_type;
    std::uint16_t adt_length;
    std::uint8_t  adt_dec;
    bool          needs_memo;
};

AdtFieldSpec adt_spec_for(const FieldOut& f) {
    switch (f.type) {
        case 'L': return { 1, 1,          0,     false};  // LOGICAL
        case 'D': return { 3, 4,          0,     false};  // DATE (JDN uint32)
        case 'M': return { 5, 9,          0,     true };  // MEMO  (9-byte ref)
        case 'Q': return { 6, 9,          0,     true };  // BINARY (9-byte ref)
        case 'I': return { 7, 9,          0,     true };  // IMAGE  (9-byte ref)
        case 'N':
            if (f.dec > 0) return {10, 8, f.dec, false}; // DOUBLE
            return              {11, 4, 0,     false};     // INTEGER
        // ADT-specific sentinels from dbf_type_for:
        case 'W': return {20, static_cast<std::uint16_t>(f.length ? f.length : 10u),
                          0, false};                       // CICHARACTER
        case 'S': return {12, 2,          0,     false};  // SHORTINT
        case '$': return {18, 8,          0,     false};  // MONEY (int64 * 10000)
        case 'P': return {14, 8,          0,     false};  // TIMESTAMP (JDN+ms)
        case 'A': return {15, 4,          0,     false};  // AUTOINC
        case 'Z': return {13, 4,          0,     false};  // TIME (ms since midnight)
        case 'C':
        default:
            return { 4, static_cast<std::uint16_t>(f.length ? f.length : 10u),
                    0, false};                             // CHAR
    }
}

} // namespace
} // extern "C++"

UNSIGNED32 AdsCreateTable(ADSHANDLE     hConn,
                          UNSIGNED8*    pucName,
                          UNSIGNED8*    /*pucAlias*/,
                          UNSIGNED16    usTableType,
                          UNSIGNED16    /*usCharType*/,
                          UNSIGNED16    /*usLockType*/,
                          UNSIGNED16    /*usCheckRights*/,
                          UNSIGNED16    usMemoBlockSize,
                          UNSIGNED8*    pucFields,
                          ADSHANDLE*    phTable) {
    if (pucName == nullptr || pucFields == nullptr || phTable == nullptr) {
        return fail(openads::AE_INTERNAL_ERROR, "null arg");
    }
    auto& s = state();
    Connection* c = s.registry.lookup<Connection>(hConn,
                            HandleKind::Connection);
    if (c == nullptr) {
        // rddads passes 0 when the host PRG never AdsConnect'd —
        // fall back to a CWD-rooted default connection.
        ADSHANDLE def = get_or_create_default_connection();
        c = s.registry.lookup<Connection>(def, HandleKind::Connection);
        if (c == nullptr) {
            return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
        }
    }

    auto rel  = openads::abi::to_internal(pucName, 0);
    auto defs = openads::abi::to_internal(pucFields, 0);

    namespace fs = std::filesystem;
    fs::path full = fs::path(c->data_dir()) / rel;
    const bool is_adt = (usTableType == ADS_ADT);
    if (!full.has_extension()) full.replace_extension(is_adt ? ".adt" : ".dbf");

    auto fields = parse_rddads_field_defs(defs);
    if (fields.empty()) {
        return fail(openads::AE_INTERNAL_ERROR, "no fields");
    }

    if (is_adt) {
        // ── ADT creation path ───────────────────────────────────────────────
        if (full.extension() != ".adt") full.replace_extension(".adt");

        std::vector<AdtFieldSpec> specs;
        specs.reserve(fields.size());
        // Header offset 88 holds the count of DISTINCT companion-stream types
        // present in the table (memo / binary / image), each stored in the side
        // companion file. Stamping a flat 1 whenever any memo field existed made
        // a conforming reader reject tables that mix several companion types (it
        // expects N distinct streams but the header claims one) -- the table was
        // reported as corrupt. Count the distinct types actually present.
        bool has_memo = false, has_binary = false, has_image = false;
        for (auto& f : fields) {
            AdtFieldSpec sp = adt_spec_for(f);
            switch (sp.adt_type) {
                case ADS_MEMO:   has_memo   = true; break;  // .adm companion
                case ADS_BINARY: has_binary = true; break;  // .adm companion
                case ADS_IMAGE:  has_image  = true; break;  // .adm companion
                default: break;
            }
            specs.push_back(sp);
        }
        const bool has_companion = has_memo || has_binary || has_image;

        // Record: 5-byte prefix (delete-flag byte + 4-byte null bitmap) + fields
        std::uint32_t rec_len = 5;
        for (auto& sp : specs) rec_len += sp.adt_length;
        if (rec_len > 0xFFFFu)
            return fail(openads::AE_INTERNAL_ERROR, "ADT record too long");

        std::uint32_t hdr_len = 400u +
                                static_cast<std::uint32_t>(fields.size()) * 200u;

        // 400-byte file header
        std::vector<std::uint8_t> adt_hdr(400, 0);
        std::memcpy(adt_hdr.data(), "Advantage Table", 15);
        auto w32 = [&](std::size_t off, std::uint32_t v) {
            adt_hdr[off+0] = static_cast<std::uint8_t>(v);
            adt_hdr[off+1] = static_cast<std::uint8_t>(v >>  8);
            adt_hdr[off+2] = static_cast<std::uint8_t>(v >> 16);
            adt_hdr[off+3] = static_cast<std::uint8_t>(v >> 24);
        };
        w32(24, 0);        // rec_count = 0
        w32(32, hdr_len);  // hdr_len
        w32(36, rec_len);  // rec_len
        // Proprietary header tail observed in reference fixtures.
        adt_hdr[20]  = 1;
        adt_hdr[356] = 4;
        adt_hdr[358] = static_cast<std::uint8_t>(fields.size() & 0xFFu);
        adt_hdr[359] = static_cast<std::uint8_t>((fields.size() >> 8) & 0xFFu);
        adt_hdr[88] = static_cast<std::uint8_t>(
            has_memo + has_binary + has_image);

        // 200-byte field descriptors
        std::vector<std::uint8_t> fds(fields.size() * 200, 0);
        std::uint16_t fld_off = 5;  // first field starts after the 5-byte prefix
        for (std::size_t i = 0; i < fields.size(); ++i) {
            const auto& f  = fields[i];
            const auto& sp = specs[i];
            std::uint8_t* fd = fds.data() + i * 200;
            // name: null-terminated, bytes 0-127
            std::size_t n = std::min<std::size_t>(f.name.size(), 127u);
            std::memcpy(fd, f.name.data(), n);
            // fd[128] = flags (0 = not nullable)
            fd[129] = static_cast<std::uint8_t>(sp.adt_type & 0xFFu);
            fd[130] = static_cast<std::uint8_t>((sp.adt_type >> 8) & 0xFFu);
            fd[131] = static_cast<std::uint8_t>(fld_off & 0xFFu);
            fd[132] = static_cast<std::uint8_t>((fld_off >> 8) & 0xFFu);
            fd[135] = static_cast<std::uint8_t>(sp.adt_length & 0xFFu);
            fd[136] = static_cast<std::uint8_t>((sp.adt_length >> 8) & 0xFFu);
            // DOUBLE (type 10) is an IEEE 8-byte binary value: the real ADS
            // engine stores NO decimal count in its field descriptor (fd[137]
            // and fd[139] stay 0). Stamping the Harbour decimals there made the
            // engine reject the whole table as corrupt (error 7016). The value
            // is full-precision binary, so dropping the descriptor "decimals"
            // is loss-free and matches the ADT on-disk layout a conforming
            // reader expects. fd[139] is the AUTOINC counter slot anyway,
            // never a DOUBLE field.
            if (sp.adt_type != 10u)
                fd[137] = sp.adt_dec;
            // AUTOINC tail (bytes 139-143) stays zero on disk; counter is
            // seeded from existing data when the table is opened.
            fld_off = static_cast<std::uint16_t>(fld_off + sp.adt_length);
        }

        // Write the .adt file
        { std::error_code ec; fs::remove(full, ec); }
        {
            std::ofstream out(full, std::ios::binary);
            if (!out) return fail(openads::AE_INTERNAL_ERROR,
                                  "AdsCreateTable: ADT open for write failed");
            out.write(reinterpret_cast<const char*>(adt_hdr.data()),
                      static_cast<std::streamsize>(adt_hdr.size()));
            out.write(reinterpret_cast<const char*>(fds.data()),
                      static_cast<std::streamsize>(fds.size()));
            if (!out) return fail(openads::AE_INTERNAL_ERROR,
                                  "AdsCreateTable: ADT write failed");
        }

        // Create a companion .adm for MEMO/BINARY/IMAGE fields
        if (has_companion) {
            fs::path adm = full;
            adm.replace_extension(".adm");
            { std::error_code ec; fs::remove(adm, ec); }
            auto mr = openads::drivers::adm::AdmMemo::create(adm.string());
            if (!mr) return fail(mr.error());
        }

        // Open via the standard path so the caller gets a usable handle
        std::string rel_adt = fs::path(rel).replace_extension(".adt").string();
        UNSIGNED8 adt_namebuf[260] = {0};
        std::size_t adt_nb = std::min<std::size_t>(rel_adt.size(),
                                                    sizeof(adt_namebuf) - 1);
        std::memcpy(adt_namebuf, rel_adt.data(), adt_nb);
        return AdsOpenTable(hConn, adt_namebuf, adt_namebuf,
                            ADS_ADT, 0, 0, 0, 1, phTable);
    }

    // ── DBF creation path (existing) ────────────────────────────────────────
    // Compute header + record sizes.
    std::uint16_t header_len = static_cast<std::uint16_t>(
        32 + 32 * fields.size() + 1);
    std::uint32_t rec_len = 1; // delete-flag byte
    for (auto& f : fields) rec_len += f.length;
    if (rec_len > 0xFFFF) {
        return fail(openads::AE_INTERNAL_ERROR, "record too long");
    }

    // Detect a memo (M) field up front: it drives both the version byte and
    // the companion memo file written below. OpenADS writes a FoxPro .fpt
    // memo, so per the dBASE/FoxPro format spec a table WITH a memo must
    // declare FoxPro 2.x (0xF5) in the version byte. Writing 0x03 (dBASE III
    // "no memo") made conforming readers (DBFCDX/DBFNTX) look for a .dbt that
    // does not exist and fail to open with subcode 1056; no-memo tables stay
    // 0x03.
    bool has_memo = false;
    for (auto& f : fields) {
        if (f.type == 'M' || f.type == 'm') { has_memo = true; break; }
    }

    std::vector<std::uint8_t> hdr(32, 0);
    hdr[0]  = has_memo ? 0xF5 : 0x03;             // FoxPro 2.x (FPT) / dBASE III
    stamp_dbf_header_today(hdr.data());                        // last-update
    hdr[4]  = 0; hdr[5] = 0; hdr[6] = 0; hdr[7] = 0;           // 0 records
    hdr[8]  = static_cast<std::uint8_t>(header_len & 0xFFu);
    hdr[9]  = static_cast<std::uint8_t>((header_len >> 8) & 0xFFu);
    hdr[10] = static_cast<std::uint8_t>(rec_len & 0xFFu);
    hdr[11] = static_cast<std::uint8_t>((rec_len >> 8) & 0xFFu);

    std::vector<std::uint8_t> file = hdr;
    for (auto& f : fields) {
        std::vector<std::uint8_t> fd(32, 0);
        std::size_t n = std::min<std::size_t>(f.name.size(), 10);
        std::memcpy(fd.data(), f.name.data(), n);
        fd[11] = static_cast<std::uint8_t>(f.type);
        fd[16] = f.length;
        fd[17] = f.dec;
        file.insert(file.end(), fd.begin(), fd.end());
    }
    file.push_back(0x0D);
    file.push_back(0x1A);

    // Atomic-ish write: just truncate-create.
    {
        std::error_code ec;
        fs::remove(full, ec);
    }
    {
        std::ofstream out(full, std::ios::binary);
        if (!out) return fail(openads::AE_INTERNAL_ERROR,
                              "AdsCreateTable: open for write failed");
        out.write(reinterpret_cast<const char*>(file.data()),
                  static_cast<std::streamsize>(file.size()));
        if (!out) return fail(openads::AE_INTERNAL_ERROR,
                              "AdsCreateTable: write failed");
    }

    // If the field list declares any memo (M) field, stage an empty
    // .fpt next to the .dbf — Connection::open_table auto-attaches it,
    // and without it any write to the M field fails "memo store not
    // attached" (e.g. X#'s ADSRDD on FieldPut to a memo column).
    {
        if (has_memo) {
            fs::path fpt = full;
            fpt.replace_extension(".fpt");
            { std::error_code ec; fs::remove(fpt, ec); }
            // Default FPT block size 512 (the FoxPro 2.x default). The old
            // 64-byte default is a Visual FoxPro value that stricter readers
            // reject on open; a conforming reader honours the size from the
            // header either way. The caller can still override via
            // usMemoBlockSize.
            std::uint16_t bs = usMemoBlockSize != 0 ? usMemoBlockSize : 512;
            auto mr = openads::drivers::fpt::FptMemo::create(fpt.string(), bs);
            if (!mr) return fail(mr.error());
        }
    }

    // Open the freshly-created table through the regular path so the
    // caller gets a usable handle.
    UNSIGNED8 namebuf[260] = {0};
    std::size_t nb = std::min<std::size_t>(rel.size(), sizeof(namebuf) - 1);
    std::memcpy(namebuf, rel.data(), nb);
    return AdsOpenTable(hConn, namebuf, namebuf,
                        ADS_CDX,    // table type
                        0, 0, 0, 1, // char/lock/checkrights/mode
                        phTable);
}

// --- M9.26 AdsRestructureTable (ADD-only) ----------------------------------
//
// Real ACE rebuilds the DBF with three field-def strings — add,
// delete, and change. The most common rddads call site only feeds
// the "add" list (`pucDeleteFields` / `pucChangeFields` empty), which
// is what 0.2.x supports. Non-empty delete / change lists return
// AE_FUNCTION_NOT_AVAILABLE until the 0.3.x VFP / ADT structural
// extensions land.
//
// Indexes are NOT auto-rebuilt (real ACE handles that internally).
// Apps that depend on a bound index after a restructure should
// follow up with AdsReindex; the on-disk record format changed, so
// stale entries point at the wrong recnos.

UNSIGNED32 AdsRestructureTable(ADSHANDLE   hConnect,
                               UNSIGNED8*  pucTableName,
                               UNSIGNED8*  /*pucAlias*/,
                               UNSIGNED16  /*usFileType*/,
                               UNSIGNED16  /*usCharType*/,
                               UNSIGNED16  /*usLockType*/,
                               UNSIGNED16  /*usCheckRights*/,
                               UNSIGNED8*  pucAddFields,
                               UNSIGNED8*  pucDeleteFields,
                               UNSIGNED8*  pucChangeFields) {
    if (pucTableName == nullptr) {
        return fail(openads::AE_INTERNAL_ERROR,
                    "AdsRestructureTable: null table name");
    }
    auto& s = state();
    Connection* c = s.registry.lookup<Connection>(hConnect, HandleKind::Connection);
    if (c == nullptr) {
        return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    }
    auto del = pucDeleteFields ? openads::abi::to_internal(pucDeleteFields, 0)
                               : std::string();
    auto chg = pucChangeFields ? openads::abi::to_internal(pucChangeFields, 0)
                               : std::string();

    auto add = pucAddFields ? openads::abi::to_internal(pucAddFields, 0)
                            : std::string();
    auto add_fields = parse_rddads_field_defs(add);

    // CHANGE list (M10.12): same shape as ADD (NAME,Type,Len,Dec;…).
    // Each entry replaces the same-named existing field's length /
    // decimals. The Type must match the existing field — type
    // conversion (rename / retype) needs a clean-room ADS spec and
    // stays deferred. Apps that need it can issue DELETE + ADD.
    auto change_fields = parse_rddads_field_defs(chg);
    std::unordered_map<std::string, FieldOut> change_map;
    for (auto& cf : change_fields) {
        change_map[cf.name] = cf;
    }

    // DELETE list is a `;`-separated list of bare field names —
    // unlike pucAddFields the entries carry no type / len info.
    std::unordered_set<std::string> del_set;
    {
        std::string buf;
        auto flush = [&] {
            std::string trimmed = buf;
            while (!trimmed.empty() &&
                   std::isspace(static_cast<unsigned char>(trimmed.front()))) {
                trimmed.erase(trimmed.begin());
            }
            while (!trimmed.empty() &&
                   std::isspace(static_cast<unsigned char>(trimmed.back()))) {
                trimmed.pop_back();
            }
            if (!trimmed.empty()) del_set.insert(trimmed);
            buf.clear();
        };
        for (std::size_t i = 0; i <= del.size(); ++i) {
            char ch = (i < del.size()) ? del[i] : ';';
            if (ch == ';' || ch == ',') flush();
            else buf.push_back(ch);
        }
    }

    if (add_fields.empty() && del_set.empty() && change_fields.empty()) {
        return ok();   // nothing to do
    }

    auto rel = openads::abi::to_internal(pucTableName, 0);
    namespace fs = std::filesystem;
    fs::path full = fs::path(c->data_dir()) / rel;
    if (!full.has_extension()) full.replace_extension(".dbf");

    fs::path tmp = full;
    tmp += ".restructure.tmp";
    {
        std::error_code ec;
        fs::remove(tmp, ec);
    }

    // Read the source schema + record bytes inside an inner scope so
    // the engine's File handle on `full` is closed before the rename.
    {
        auto opened = openads::engine::Table::open(
            full.string(), openads::engine::TableType::Cdx,
            openads::engine::OpenMode::Read);
        if (!opened) return fail(opened.error());
        auto& t = opened.value();

        // Per-field copy plan: keep the source order, drop fields that
        // appear in the DELETE list, apply the CHANGE list's
        // length/decimals overrides for matching surviving fields,
        // then append the ADD list. Each surviving field tracks where
        // to copy from in the old record (or no source for newly-added
        // columns).
        struct PerField {
            FieldOut       descriptor;
            bool           from_old      = false;
            std::uint16_t  old_offset    = 0;
            std::uint8_t   old_length    = 0;
            char           old_type      = '\0';  // non-'\0' → type conversion
            char           new_type      = '\0';  // target raw type
        };
        std::vector<PerField> plan;
        for (std::uint16_t i = 0; i < t.field_count(); ++i) {
            const auto& src = t.field_descriptor(i);
            if (del_set.find(src.name) != del_set.end()) continue;
            PerField p;
            p.descriptor.name   = src.name;
            p.descriptor.type   = src.raw_type;
            p.descriptor.length = static_cast<std::uint8_t>(src.length);
            p.descriptor.dec    = src.decimals;
            p.from_old   = true;
            p.old_offset = src.record_offset;
            p.old_length = static_cast<std::uint8_t>(src.length);

            auto cit = change_map.find(src.name);
            if (cit != change_map.end()) {
                if (cit->second.type != src.raw_type) {
                    p.old_type        = src.raw_type;
                    p.new_type        = cit->second.type;
                    p.descriptor.type = cit->second.type;
                }
                p.descriptor.length = cit->second.length;
                p.descriptor.dec    = cit->second.dec;
            }
            plan.push_back(std::move(p));
        }
        for (auto& nf : add_fields) {
            for (auto& existing : plan) {
                if (existing.descriptor.name == nf.name) {
                    return fail(openads::AE_INTERNAL_ERROR,
                                "AdsRestructureTable: duplicate field name");
                }
            }
            PerField p;
            p.descriptor = nf;
            p.from_old   = false;
            plan.push_back(std::move(p));
        }
        if (plan.empty()) {
            return fail(openads::AE_INTERNAL_ERROR,
                        "AdsRestructureTable: every field deleted "
                        "without an ADD — would leave the table empty");
        }
        std::vector<FieldOut> merged;
        merged.reserve(plan.size());
        for (auto& p : plan) merged.push_back(p.descriptor);

        std::uint16_t header_len = static_cast<std::uint16_t>(
            32 + 32 * merged.size() + 1);
        std::uint32_t rec_len = 1;
        for (auto& f : merged) rec_len += f.length;
        if (rec_len > 0xFFFF) {
            return fail(openads::AE_INTERNAL_ERROR,
                        "AdsRestructureTable: record exceeds 64 KiB");
        }

        std::vector<std::uint8_t> hdr(32, 0);
        hdr[0]  = 0x03;
        stamp_dbf_header_today(hdr.data());
        std::uint32_t rcount = t.record_count();
        hdr[4]  = static_cast<std::uint8_t>( rcount        & 0xFFu);
        hdr[5]  = static_cast<std::uint8_t>((rcount >> 8)  & 0xFFu);
        hdr[6]  = static_cast<std::uint8_t>((rcount >> 16) & 0xFFu);
        hdr[7]  = static_cast<std::uint8_t>((rcount >> 24) & 0xFFu);
        hdr[8]  = static_cast<std::uint8_t>(header_len & 0xFFu);
        hdr[9]  = static_cast<std::uint8_t>((header_len >> 8) & 0xFFu);
        hdr[10] = static_cast<std::uint8_t>(rec_len & 0xFFu);
        hdr[11] = static_cast<std::uint8_t>((rec_len >> 8) & 0xFFu);

        std::vector<std::uint8_t> file_bytes = hdr;
        for (auto& f : merged) {
            std::vector<std::uint8_t> fd(32, 0);
            std::size_t n = std::min<std::size_t>(f.name.size(), 10);
            std::memcpy(fd.data(), f.name.data(), n);
            fd[11] = static_cast<std::uint8_t>(f.type);
            fd[16] = f.length;
            fd[17] = f.dec;
            file_bytes.insert(file_bytes.end(), fd.begin(), fd.end());
        }
        file_bytes.push_back(0x0D);

        std::uint16_t old_rec_len = t.driver()->record_length();
        for (std::uint32_t r = 1; r <= rcount; ++r) {
            auto rec = t.driver()->read_record_raw(r);
            if (!rec) return fail(rec.error());
            std::vector<std::uint8_t> old_buf = std::move(rec).value();

            std::vector<std::uint8_t> new_buf(rec_len, ' ');
            new_buf[0] = old_buf.empty() ? ' ' : old_buf[0];
            std::uint16_t out_off = 1;
            for (auto& p : plan) {
                if (p.from_old && p.new_type != '\0') {
                    // Type conversion: read old bytes, convert, write new.
                    std::string old_data(p.old_length, ' ');
                    if (old_buf.size() >= static_cast<std::size_t>(p.old_offset)
                                         + p.old_length) {
                        std::memcpy(old_data.data(),
                                    old_buf.data() + p.old_offset, p.old_length);
                    }
                    const std::uint8_t nlen = p.descriptor.length;
                    const std::uint8_t ndec = p.descriptor.dec;
                    if (p.old_type == 'C' && p.new_type == 'N') {
                        auto first = old_data.find_first_not_of(' ');
                        const char* src_ptr = (first == std::string::npos)
                            ? "" : old_data.c_str() + first;
                        double val = *src_ptr ? std::strtod(src_ptr, nullptr) : 0.0;
                        char fmt[32];
                        std::snprintf(fmt, sizeof(fmt), "%%%u.%uf",
                                      static_cast<unsigned>(nlen),
                                      static_cast<unsigned>(ndec));
                        char nb[64] = {};
                        std::snprintf(nb, sizeof(nb), fmt, val);
                        std::size_t slen = std::strlen(nb);
                        std::size_t copy = std::min<std::size_t>(slen, nlen);
                        std::size_t soff = slen > nlen ? slen - nlen : 0u;
                        std::memcpy(new_buf.data() + out_off, nb + soff, copy);
                    } else if (p.old_type == 'N' && p.new_type == 'C') {
                        auto first = old_data.find_first_not_of(' ');
                        if (first != std::string::npos) {
                            std::size_t copy = std::min<std::size_t>(
                                old_data.size() - first,
                                static_cast<std::size_t>(nlen));
                            std::memcpy(new_buf.data() + out_off,
                                        old_data.data() + first, copy);
                        }
                    } else if (p.new_type == 'L') {
                        char lval = 'F';
                        if (p.old_type == 'C') {
                            char ch = old_data.empty() ? ' ' : old_data[0];
                            if (ch=='T'||ch=='t'||ch=='Y'||ch=='y'||ch=='1') lval='T';
                        } else if (p.old_type == 'N') {
                            auto f2 = old_data.find_first_not_of(' ');
                            if (f2 != std::string::npos &&
                                std::strtod(old_data.c_str() + f2, nullptr) != 0.0)
                                lval = 'T';
                        }
                        new_buf[out_off] = static_cast<std::uint8_t>(lval);
                    } else if (p.old_type == 'L') {
                        char lc = old_data.empty() ? 'F' : old_data[0];
                        bool is_true = (lc == 'T' || lc == 't');
                        if (p.new_type == 'C') {
                            new_buf[out_off] =
                                static_cast<std::uint8_t>(is_true ? 'T' : 'F');
                        } else if (p.new_type == 'N') {
                            char fmt[32];
                            std::snprintf(fmt, sizeof(fmt), "%%%u.%uf",
                                          static_cast<unsigned>(nlen),
                                          static_cast<unsigned>(ndec));
                            char nb[64] = {};
                            std::snprintf(nb, sizeof(nb), fmt, is_true ? 1.0 : 0.0);
                            std::size_t copy =
                                std::min<std::size_t>(std::strlen(nb), nlen);
                            std::memcpy(new_buf.data() + out_off, nb, copy);
                        }
                    } else {
                        // D↔C and other pairs: raw copy up to min length.
                        std::uint8_t copy_len =
                            std::min<std::uint8_t>(p.old_length, nlen);
                        std::memcpy(new_buf.data() + out_off,
                                    old_data.data(), copy_len);
                    }
                } else if (p.from_old) {
                    std::uint8_t copy_len =
                        std::min<std::uint8_t>(p.old_length,
                                               p.descriptor.length);
                    if (old_buf.size() >=
                        static_cast<std::size_t>(p.old_offset) +
                        static_cast<std::size_t>(copy_len)) {
                        std::memcpy(new_buf.data() + out_off,
                                    old_buf.data() + p.old_offset,
                                    copy_len);
                    }
                    // Tail bytes (when new length > old length) stay
                    // as the blank-pad already in new_buf.
                }
                out_off = static_cast<std::uint16_t>(
                    out_off + p.descriptor.length);
            }
            (void)old_rec_len;
            file_bytes.insert(file_bytes.end(),
                              new_buf.begin(), new_buf.end());
        }
        file_bytes.push_back(0x1A);

        std::ofstream out(tmp, std::ios::binary);
        if (!out) return fail(openads::AE_INTERNAL_ERROR,
                              "AdsRestructureTable: tmp open failed");
        out.write(reinterpret_cast<const char*>(file_bytes.data()),
                  static_cast<std::streamsize>(file_bytes.size()));
        if (!out) return fail(openads::AE_INTERNAL_ERROR,
                              "AdsRestructureTable: tmp write failed");
    }   // engine handle on `full` closes here

    {
        std::error_code ec;
        fs::remove(full, ec);
        fs::rename(tmp, full, ec);
        if (ec) {
            return fail(openads::AE_INTERNAL_ERROR,
                        "AdsRestructureTable: rename failed");
        }
    }
    return ok();
}

UNSIGNED32 AdsRefreshRecord(ADSHANDLE hTable) {
    if (auto* rt = get_remote_table(hTable)) {
        remote_settle_cursor(rt);                   // M12.21 option C
        rt->row_valid = false;                      // M12.17 cache invalidation
        auto r = rt->conn->refresh_record(rt->id);
        if (!r) return fail(r.error());
        return ok();
    }
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    if (t->eof() || t->bof() || t->recno() == 0) return ok();
    auto r = t->goto_record(t->recno());
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsExtractKey(ADSHANDLE hIndex, UNSIGNED8* pucBuf,
                         UNSIGNED16* pusLen) {
    if (pusLen == nullptr) return fail(openads::AE_INTERNAL_ERROR, "null len");
    Table* t = lookup_table_by_index(hIndex);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown index");
    openads::drivers::IIndex* idx = iindex_for_handle(hIndex);
    if (!idx) return fail(openads::AE_INTERNAL_ERROR, "index not loaded");
    auto k = openads::engine::evaluate_index_expr(*t, idx->expression(),
                                                  idx->key_length());
    if (!k) return fail(k.error());
    openads::abi::copy_to_caller(pucBuf, pusLen, k.value());
    return ok();
}

UNSIGNED32 AdsGotoRecord(ADSHANDLE hTable, UNSIGNED32 ulRecord) {
    if (auto* rt = get_remote_table(hTable)) {
        rt->found_cached = true; rt->current_found = false;  // M12.21: GoTo clears Found()
        auto r = rt->conn->goto_record(rt, ulRecord);
        if (!r) return fail(r.error());
        return ok();
    }
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    auto r = t->goto_record(ulRecord);
    if (!r) return fail(r.error());
    snapshot_ri_pks(t);
    return ok();
}

UNSIGNED32 AdsCheckExistence(ADSHANDLE /*hConn*/, UNSIGNED8* pucName,
                             UNSIGNED16* pbExists) {
    if (pbExists == nullptr) {
        return fail(openads::AE_INTERNAL_ERROR, "null out");
    }
    if (pucName == nullptr) {
        *pbExists = 0;
        return ok();
    }
    auto path = openads::abi::to_internal(pucName, 0);
    std::error_code ec;
    *pbExists = std::filesystem::exists(path, ec) ? 1 : 0;
    return ok();
}

UNSIGNED32 AdsDeleteFile(ADSHANDLE /*hConn*/, UNSIGNED8* pucName) {
    if (pucName == nullptr) return fail(openads::AE_INTERNAL_ERROR, "null name");
    auto path = openads::abi::to_internal(pucName, 0);
    std::error_code ec;
    if (!std::filesystem::remove(path, ec)) {
        return fail(openads::AE_INTERNAL_ERROR,
                    "AdsDeleteFile: file not found / cannot remove");
    }
    return ok();
}

// SAP's ace.h declares `AdsCloseAllTables(void)`: close every table
// the calling process has opened. We accept the same 0-arg form;
// per-connection close still works through AdsCloseTable().
UNSIGNED32 AdsCloseAllTables(void) {
    auto& s = state();
    std::lock_guard<std::recursive_mutex> lk(s.mu);
    (void)0;
    // Walk every Table handle that points to a Table belonging to
    // this connection and release it. Connection's tables_ map owns
    // the unique_ptrs; the handle registry just borrows pointers.
    std::vector<Handle> to_release;
    s.registry.for_each_handle([&](Handle h, HandleKind k, void* p) {
        if (k != HandleKind::Table) return;
        Table* tp = static_cast<Table*>(p);
        if (tp == nullptr) return;
        // Table belongs to this connection if c->lookup_table on its
        // handle returns the same pointer. We don't track the
        // back-edge directly, so iterate all Connection's table
        // handles instead.
        (void)tp;
        to_release.push_back(h);
    });
    for (Handle h : to_release) {
        Table* t = s.registry.lookup<Table>(h, HandleKind::Table);
        if (t) {
            Connection* owning = nullptr;
            s.registry.for_each_handle([&](Handle, HandleKind k, void* p) {
                if (k != HandleKind::Connection || owning) return;
                auto* cc = static_cast<Connection*>(p);
                if (cc->owns_table_ptr(t)) owning = cc;
            });
            (void)t->flush();
            purge_bindings_for_table(t);
            purge_pending_binaries_for_table(t);
            if (owning) owning->close_table_ptr(t);
        }
        s.registry.release(h);
    }
    return ok();
}

UNSIGNED32 AdsCloseTable(ADSHANDLE hTable) {
    {
        if (auto* rt = get_remote_table(hTable)) {
            // conn is nulled out by AdsDisconnect before the RemoteConnection
            // is freed; skip the wire close op if the connection is already gone.
            if (rt->conn != nullptr)
                (void)rt->conn->close_table(rt->id);
            auto& s2 = state();
            std::lock_guard<std::recursive_mutex> lk2(s2.mu);
            s2.registry.release(hTable);
            remote_sql_cursors_map().erase(hTable);
            return ok();
        }
        if (auto* ops = openads::abi::backend_table_ops_for(hTable))
            if (ops->close_table) return ops->close_table(hTable);
    }
    auto& s = state();
    std::lock_guard<std::recursive_mutex> lk(s.mu);
    // Flush the table (driver + active order + extra index views)
    // before releasing the handle. Without an explicit transaction,
    // this is the only point at which mutations made since open
    // reach disk; with one, commit_tx already flushed and this is
    // a no-op. Also drop any index bindings tied to this Table so
    // a future Table allocation at the same heap address doesn't
    // inherit stale entries.
    Table* t = s.registry.lookup<Table>(hTable, HandleKind::Table);
    if (t != nullptr) {
        Connection* owning = nullptr;
        s.registry.for_each_handle([&](Handle, HandleKind k, void* p) {
            if (k != HandleKind::Connection || owning) return;
            auto* cc = static_cast<Connection*>(p);
            if (cc->owns_table_ptr(t)) owning = cc;
        });
        (void)t->flush();
        purge_bindings_for_table(t);
        purge_pending_binaries_for_table(t);
        t->ri_snapshot().clear();
        if (owning) owning->close_table_ptr(t);
    }
    cursor_projections().erase(hTable);
    s.registry.release(hTable);
    return ok();
}

UNSIGNED32 AdsGotoTop(ADSHANDLE hTable) {
    if (auto* rt = get_remote_table(hTable)) {
        // M12.18 — rt-aware overload parses the row trailer in the
        // same RTT, so AdsGetField immediately after GoTop hits
        // the cache.
        rt->found_cached = true; rt->current_found = false;  // M12.21: GoTop clears Found()
        auto r = rt->conn->goto_top(rt);
        if (!r) return fail(r.error());
        return ok();
    }
    if (auto* ops = openads::abi::backend_table_ops_for(hTable))
        if (ops->goto_top) return ops->goto_top(hTable);
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    auto r = t->goto_top();
    if (!r) return fail(r.error());
    snapshot_ri_pks(t);
    return ok();
}

UNSIGNED32 AdsGotoBottom(ADSHANDLE hTable) {
    if (auto* rt = get_remote_table(hTable)) {
        rt->found_cached = true; rt->current_found = false;  // M12.21: GoBottom clears Found()
        auto r = rt->conn->goto_bottom(rt);
        if (!r) return fail(r.error());
        return ok();
    }
    if (auto* ops = openads::abi::backend_table_ops_for(hTable))
        if (ops->goto_bottom) return ops->goto_bottom(hTable);
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    auto r = t->goto_bottom();
    if (!r) return fail(r.error());
    snapshot_ri_pks(t);
    return ok();
}

UNSIGNED32 AdsSkip(ADSHANDLE hTable, SIGNED32 lRows) {
    seek_last_retry_latch() = false;
    if (auto* rt = get_remote_table(hTable)) {
        rt->found_cached = true; rt->current_found = false;  // M12.21: Skip clears Found()
        // M12.21 — sequential prefetch: Skip(1) drains the queue
        // populated by the previous Skip's lookahead block. Zero
        // RTT for every cached step.
        if (lRows == 1 && !rt->prefetch_queue.empty()) {
            auto pr = std::move(rt->prefetch_queue.front());
            rt->prefetch_queue.pop_front();
            rt->current_recno   = pr.recno;
            rt->current_deleted = pr.deleted;
            rt->current_row     = std::move(pr.fields);
            rt->row_valid       = true;
            // M12.21 option C — the server cursor did not move; remember
            // we are one logical row further ahead so the next wire op
            // resyncs by (step + prefetch_consumed).
            ++rt->prefetch_consumed;
            return ok();
        }
        // Any non-sequential nav drops the queue (handled inside
        // parse_row_trailer_into when the new ack arrives).
        auto r = rt->conn->skip(rt, lRows);
        if (!r) return fail(r.error());
        return ok();
    }
    if (auto* ops = openads::abi::backend_table_ops_for(hTable))
        if (ops->skip) return ops->skip(hTable, lRows);
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    auto r = t->skip(lRows);
    if (!r) return fail(r.error());
    snapshot_ri_pks(t);
    return ok();
}

UNSIGNED32 AdsAtEOF(ADSHANDLE hTable, UNSIGNED16* pbAtEnd) {
    if (auto* rt = get_remote_table(hTable)) {
        if (pbAtEnd == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
        // M12.21 option C — a valid cached current row (including one
        // served locally from the prefetch queue) means the cursor is
        // on a record, so it cannot be at EOF: answer with no round
        // trip. This is what lets a prefetched scan loop, which polls
        // Eof() every iteration, actually shed its per-step round trips.
        if (rt->row_valid) { *pbAtEnd = 0; return ok(); }
        auto r = rt->conn->at_eof(rt->id);
        if (!r) return fail(r.error());
        *pbAtEnd = r.value() ? 1 : 0;
        return ok();
    }
    if (auto* ops = openads::abi::backend_table_ops_for(hTable))
        if (ops->at_eof) return ops->at_eof(hTable, pbAtEnd);
    Table* t = get_table(hTable);
    if (!t || pbAtEnd == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    *pbAtEnd = t->eof() ? 1 : 0;
    return ok();
}

UNSIGNED32 AdsAtBOF(ADSHANDLE hTable, UNSIGNED16* pbAtBegin) {
    if (pbAtBegin == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    if (auto* rt = get_remote_table(hTable)) {
        // M12.21 option C — a valid cached current row means the cursor
        // is on a record, so it cannot be at BOF: answer with no round
        // trip (see AdsAtEOF).
        if (rt->row_valid) { *pbAtBegin = 0; return ok(); }
        auto r = rt->conn->at_bof(rt->id);
        if (!r) return fail(r.error());
        *pbAtBegin = r.value() ? 1 : 0;
        return ok();
    }
    if (auto* ops = openads::abi::backend_table_ops_for(hTable))
        if (ops->at_bof) return ops->at_bof(hTable, pbAtBegin);
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "");
    *pbAtBegin = t->bof() ? 1 : 0;
    return ok();
}

UNSIGNED32 AdsGetNumFields(ADSHANDLE hTable, UNSIGNED16* pusFields) {
    if (pusFields == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    if (auto* rt = get_remote_table(hTable)) {
        if (!rt->fields_cached) {
            auto r = rt->conn->describe_table(rt->id);
            if (!r) return fail(r.error());
            rt->fields = std::move(r).value();
            rt->fields_cached = true;
        }
        *pusFields = static_cast<UNSIGNED16>(rt->fields.size());
        return ok();
    }
    if (auto* ops = openads::abi::backend_table_ops_for(hTable))
        if (ops->num_fields) return ops->num_fields(hTable, pusFields);
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "");
    if (auto* p = projection_for(hTable); p != nullptr) {
        *pusFields = static_cast<UNSIGNED16>(p->size());
    } else {
        *pusFields = t->field_count();
    }
    return ok();
}

UNSIGNED32 AdsGetFieldName(ADSHANDLE hTable, UNSIGNED16 usFieldNum,
                           UNSIGNED8* pucBuf, UNSIGNED16* pusLen) {
    if (auto* rt = get_remote_table(hTable)) {
        if (!rt->fields_cached) {
            auto r = rt->conn->describe_table(rt->id);
            if (!r) return fail(r.error());
            rt->fields = std::move(r).value();
            rt->fields_cached = true;
        }
        if (usFieldNum == 0 || usFieldNum > rt->fields.size()) {
            return fail(openads::AE_COLUMN_NOT_FOUND, "");
        }
        openads::abi::copy_to_caller(pucBuf, pusLen,
            rt->fields[usFieldNum - 1].name);
        return ok();
    }
    if (auto* ops = openads::abi::backend_table_ops_for(hTable))
        if (ops->field_name) return ops->field_name(hTable, usFieldNum, pucBuf, pusLen);
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    auto* p = projection_for(hTable);
    std::uint16_t src_idx = 0;
    if (p != nullptr) {
        if (usFieldNum == 0 || usFieldNum > p->size()) {
            return fail(openads::AE_COLUMN_NOT_FOUND, "");
        }
        src_idx = (*p)[usFieldNum - 1];
    } else {
        if (usFieldNum == 0 || usFieldNum > t->field_count()) {
            return fail(openads::AE_COLUMN_NOT_FOUND, "field index out of range");
        }
        src_idx = static_cast<std::uint16_t>(usFieldNum - 1);
    }
    const auto& f = t->field_descriptor(src_idx);
    openads::abi::copy_to_caller(pucBuf, pusLen, f.name);
    return ok();
}

// Cache the remote schema on `rt` if not already; return index of
// the field whose name (case-insensitive) matches `pucField`, or
// SIZE_MAX when not found. Used by the three remote field-by-name
// metadata bridges below.
namespace {

std::size_t remote_field_index(openads::network::RemoteTable* rt,
                                UNSIGNED8* pucField) {
    if (!rt->fields_cached) {
        auto r = rt->conn->describe_table(rt->id);
        if (!r) return std::numeric_limits<std::size_t>::max();
        rt->fields = std::move(r).value();
        rt->fields_cached = true;
    }
    // ACE "field name OR 1-based ordinal cast to a pointer" idiom — X#'s
    // ADSRDD calls AdsGetFieldType/Length/Decimals (and the value
    // getters) by ordinal. A tiny pointer value is the ordinal; reading
    // it as a string address would fault.
    {
        auto p = reinterpret_cast<std::uintptr_t>(pucField);
        if (p != 0 && p < 0x10000u) {
            std::size_t one_based = static_cast<std::size_t>(p);
            if (one_based >= 1 && one_based <= rt->fields.size()) {
                return one_based - 1;
            }
            return std::numeric_limits<std::size_t>::max();
        }
    }
    if (pucField == nullptr) {
        return std::numeric_limits<std::size_t>::max();
    }
    std::string want = openads::abi::to_internal(pucField, 0);
    for (auto& c : want) {
        c = static_cast<char>(
            std::toupper(static_cast<unsigned char>(c)));
    }
    for (std::size_t i = 0; i < rt->fields.size(); ++i) {
        std::string have = rt->fields[i].name;
        for (auto& c : have) {
            c = static_cast<char>(
                std::toupper(static_cast<unsigned char>(c)));
        }
        if (have == want) return i;
    }
    return std::numeric_limits<std::size_t>::max();
}

} // namespace

UNSIGNED32 AdsGetFieldType(ADSHANDLE hTable, UNSIGNED8* pucField,
                           UNSIGNED16* pusType) {
    if (pusType == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    if (auto* rt = get_remote_table(hTable)) {
        auto i = remote_field_index(rt, pucField);
        if (i == std::numeric_limits<std::size_t>::max()) {
            return fail(openads::AE_COLUMN_NOT_FOUND, "");
        }
        *pusType = rt->fields[i].type;
        return ok();
    }
    if (auto* ops = openads::abi::backend_table_ops_for(hTable))
        if (ops->field_type) return ops->field_type(hTable, pucField, pusType);
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "");
    std::uint16_t idx = 0;
    if (!resolve_field_index_h(hTable, t, pucField, &idx)) {
        return fail(openads::AE_COLUMN_NOT_FOUND, "");
    }
    *pusType = map_field_type(t->field_descriptor(idx).type);
    // ADT IMAGE (raw type 7) vs BINARY (raw type 6): both map to
    // DbfFieldType::Binary internally, but the ABI type differs.
    const auto& fd = t->field_descriptor(idx);
    if (fd.type == openads::drivers::DbfFieldType::Binary) {
        auto raw = static_cast<unsigned char>(fd.raw_type);
        if (raw == 7u) *pusType = static_cast<UNSIGNED16>(ADS_IMAGE);
        else if (raw == 6u) *pusType = static_cast<UNSIGNED16>(ADS_BINARY);
    }
    return ok();
}

UNSIGNED32 AdsGetFieldLength(ADSHANDLE hTable, UNSIGNED8* pucField,
                             UNSIGNED32* pulLen) {
    if (pulLen == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    if (auto* rt = get_remote_table(hTable)) {
        auto i = remote_field_index(rt, pucField);
        if (i == std::numeric_limits<std::size_t>::max()) {
            return fail(openads::AE_COLUMN_NOT_FOUND, "");
        }
        *pulLen = rt->fields[i].length;
        return ok();
    }
    if (auto* ops = openads::abi::backend_table_ops_for(hTable))
        if (ops->field_length) return ops->field_length(hTable, pucField, pulLen);
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "");
    std::uint16_t idx = 0;
    if (!resolve_field_index_h(hTable, t, pucField, &idx)) {
        return fail(openads::AE_COLUMN_NOT_FOUND, "");
    }
    const auto& fd = t->field_descriptor(idx);
    // Return the string representation length, not the raw field length, for
    // types where decode_field() produces a longer formatted string. This lets
    // callers (e.g. the PHP extension) allocate the right buffer size before
    // calling AdsGetString.
    using openads::drivers::DbfFieldType;
    switch (fd.type) {
        case DbfFieldType::DateTime:
        case DbfFieldType::AdtTimestamp:
        case DbfFieldType::ModTime:
            *pulLen = 14;  // "YYYYMMDDHHMMSS"
            break;
        case DbfFieldType::AdtDate:
            *pulLen = 8;   // "YYYYMMDD"
            break;
        case DbfFieldType::RowVersion:
            *pulLen = 20;  // up to 20 digits for uint64_max
            break;
        default:
            *pulLen = fd.length;
            break;
    }
    return ok();
}

UNSIGNED32 AdsGetFieldDecimals(ADSHANDLE hTable, UNSIGNED8* pucField,
                               UNSIGNED16* pusDec) {
    if (pusDec == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    if (auto* rt = get_remote_table(hTable)) {
        auto i = remote_field_index(rt, pucField);
        if (i == std::numeric_limits<std::size_t>::max()) {
            return fail(openads::AE_COLUMN_NOT_FOUND, "");
        }
        *pusDec = rt->fields[i].decimals;
        return ok();
    }
    if (auto* ops = openads::abi::backend_table_ops_for(hTable))
        if (ops->field_decimals) return ops->field_decimals(hTable, pucField, pusDec);
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "");
    std::uint16_t idx = 0;
    if (!resolve_field_index_h(hTable, t, pucField, &idx)) {
        return fail(openads::AE_COLUMN_NOT_FOUND, "");
    }
    *pusDec = t->field_descriptor(idx).decimals;
    return ok();
}

UNSIGNED32 AdsGetLong(ADSHANDLE hTable, UNSIGNED8* pucField, SIGNED32* plVal) {
    if (plVal == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    if (auto* rt = get_remote_table(hTable)) {
        if (!rt->row_valid) {
            auto fr = rt->conn->fetch_current_row(rt);
            if (!fr) return fail(fr.error());
        }
        std::string vstr;
        if (rt->row_valid) {
            auto i = remote_field_index(rt, pucField);
            if (i == std::numeric_limits<std::size_t>::max()) {
                return fail(openads::AE_COLUMN_NOT_FOUND, "");
            }
            vstr = rt->current_row[i];
        } else {
            std::string fname = openads::abi::to_internal(pucField, 0);
            auto v = rt->conn->get_field(rt->id, fname);
            if (!v) return fail(v.error());
            vstr = std::move(v).value();
        }
        try { *plVal = static_cast<SIGNED32>(std::stol(vstr)); }
        catch (...) { *plVal = 0; }
        return ok();
    }
    // SQL backend (e.g. postgresql): read text via the per-backend ops
    // vtable then parse. Mirrors the AdsGetDouble fix — without it a PG
    // handle fell through to the native get_table() path and errored.
    if (auto* ops = openads::abi::backend_table_ops_for(hTable)) {
        if (ops->get_field) {
            UNSIGNED8 buf[64] = {0};
            UNSIGNED32 cap = sizeof(buf);
            UNSIGNED32 rc = ops->get_field(hTable, pucField, buf, &cap, 0);
            if (rc != 0) return rc;
            std::string vstr(reinterpret_cast<const char*>(buf),
                             std::min<UNSIGNED32>(cap, sizeof(buf)));
            try { *plVal = static_cast<SIGNED32>(std::stol(vstr)); }
            catch (...) { *plVal = 0; }
            return ok();
        }
    }
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "");
    std::uint16_t idx = 0;
    if (!resolve_field_index(t, pucField, &idx)) {
        return fail(openads::AE_COLUMN_NOT_FOUND, "");
    }
    auto v = t->read_field(idx);
    if (!v) return fail(v.error());
    *plVal = static_cast<SIGNED32>(v.value().as_double);
    return ok();
}

UNSIGNED32 AdsGetDouble(ADSHANDLE hTable, UNSIGNED8* pucField, double* pdVal) {
    if (pdVal == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    if (auto* rt = get_remote_table(hTable)) {
        if (!rt->row_valid) {
            auto fr = rt->conn->fetch_current_row(rt);
            if (!fr) return fail(fr.error());
        }
        std::string vstr;
        if (rt->row_valid) {
            auto i = remote_field_index(rt, pucField);
            if (i == std::numeric_limits<std::size_t>::max()) {
                return fail(openads::AE_COLUMN_NOT_FOUND, "");
            }
            vstr = rt->current_row[i];
        } else {
            std::string fname = openads::abi::to_internal(pucField, 0);
            auto v = rt->conn->get_field(rt->id, fname);
            if (!v) return fail(v.error());
            vstr = std::move(v).value();
        }
        try { *pdVal = std::stod(vstr); }
        catch (...) { *pdVal = 0.0; }
        return ok();
    }
    // SQL backend (e.g. postgresql): read the field as text through the
    // per-backend ops vtable, then parse the numeric. Without this branch a
    // PG handle fell through to the native get_table() path below, which
    // returns null for a non-native table -> AdsGetDouble yielded an error.
    if (auto* ops = openads::abi::backend_table_ops_for(hTable)) {
        if (ops->get_field) {
            UNSIGNED8 buf[64] = {0};
            UNSIGNED32 cap = sizeof(buf);
            UNSIGNED32 rc = ops->get_field(hTable, pucField, buf, &cap, 0);
            if (rc != 0) return rc;
            std::string vstr(reinterpret_cast<const char*>(buf),
                             std::min<UNSIGNED32>(cap, sizeof(buf)));
            try { *pdVal = std::stod(vstr); }
            catch (...) { *pdVal = 0.0; }
            return ok();
        }
    }
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "");
    std::uint16_t idx = 0;
    if (!resolve_field_index(t, pucField, &idx)) {
        return fail(openads::AE_COLUMN_NOT_FOUND, "");
    }
    auto v = t->read_field(idx);
    if (!v) return fail(v.error());
    *pdVal = v.value().as_double;
    return ok();
}

namespace {

// Gregorian -> Clipper/Harbour Julian Day Number. Same formula as
// hb_dateEncode in Harbour core.
SIGNED32 to_julian(int y, int m, int d) {
    long y32 = y;
    long m32 = m;
    long d32 = d;
    return static_cast<SIGNED32>(
        (1461 * (y32 + 4800 + (m32 - 14) / 12)) / 4
      + (367  * (m32 - 2 - 12 * ((m32 - 14) / 12))) / 12
      - (3    * ((y32 + 4900 + (m32 - 14) / 12) / 100)) / 4
      + d32 - 32075);
}

} // namespace

UNSIGNED32 AdsGetJulian(ADSHANDLE hTable, UNSIGNED8* pucField, SIGNED32* plDate) {
    if (plDate == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    auto decode_date = [](const std::string& s) -> SIGNED32 {
        if (s.size() < 8) return 0;
        int y = (s[0] - '0') * 1000 + (s[1] - '0') * 100
              + (s[2] - '0') * 10   + (s[3] - '0');
        int m = (s[4] - '0') * 10   + (s[5] - '0');
        int d = (s[6] - '0') * 10   + (s[7] - '0');
        if (y > 0 && m >= 1 && m <= 12 && d >= 1 && d <= 31) {
            return to_julian(y, m, d);
        }
        return 0;
    };
    if (auto* rt = get_remote_table(hTable)) {
        std::string fname = openads::abi::to_internal(pucField, 0);
        auto v = rt->conn->get_field(rt->id, fname);
        if (!v) return fail(v.error());
        *plDate = decode_date(v.value());
        return ok();
    }
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "");
    std::uint16_t idx = 0;
    if (!resolve_field_index(t, pucField, &idx)) {
        return fail(openads::AE_COLUMN_NOT_FOUND, "");
    }
    auto v = t->read_field(idx);
    if (!v) return fail(v.error());
    const std::string& s = v.value().as_string;
    *plDate = 0;
    if (s.size() >= 8) {
        int y = (s[0] - '0') * 1000 + (s[1] - '0') * 100
              + (s[2] - '0') * 10   + (s[3] - '0');
        int m = (s[4] - '0') * 10   + (s[5] - '0');
        int d = (s[6] - '0') * 10   + (s[7] - '0');
        if (y > 0 && m >= 1 && m <= 12 && d >= 1 && d <= 31) {
            *plDate = to_julian(y, m, d);
        }
    }
    return ok();
}

UNSIGNED32 AdsGetRecordNum(ADSHANDLE hTable, UNSIGNED16 /*bFilterOption*/,
                           UNSIGNED32* pulRecordNum) {
    if (pulRecordNum == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    if (auto* rt = get_remote_table(hTable)) {
        // M12.18 — recno is part of the row trailer that arrives
        // with every nav ack, so the cache hit avoids a separate
        // GetRecordNum RTT after a nav.
        if (rt->row_valid) {
            *pulRecordNum = rt->current_recno;
            return ok();
        }
        auto r = rt->conn->get_record_num(rt->id);
        if (!r) return fail(r.error());
        *pulRecordNum = r.value();
        return ok();
    }
    if (auto* ops = openads::abi::backend_table_ops_for(hTable))
        if (ops->record_num) return ops->record_num(hTable, pulRecordNum);
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "");
    *pulRecordNum = t->recno();
    return ok();
}

UNSIGNED32 AdsGetRecordCount(ADSHANDLE hTable, UNSIGNED16 bFilterOption,
                             UNSIGNED32* pulRecordCount) {
    if (auto* rt = get_remote_table(hTable)) {
        if (pulRecordCount == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
        // M12.19 — record count is invariant outside of explicit
        // writes (AppendBlank / DeleteRecord / RecallRecord / Pack
        // / Zap), so cache the value on first hit and serve every
        // subsequent AdsGetRecordCount + AdsGetRelKeyPos (scrollbar)
        // call from cache. Each cache hit saves one wire RTT.
        if (rt->rec_count_cached) {
            *pulRecordCount = rt->cached_rec_count;
            return ok();
        }
        auto r = rt->conn->record_count(rt->id);
        if (!r) return fail(r.error());
        rt->cached_rec_count = static_cast<UNSIGNED32>(r.value());
        rt->rec_count_cached = true;
        *pulRecordCount = rt->cached_rec_count;
        return ok();
    }
    if (auto* ops = openads::abi::backend_table_ops_for(hTable))
        if (ops->record_count) return ops->record_count(hTable, pulRecordCount, 0);
    Table* t = get_table(hTable);
    if (!t || pulRecordCount == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    // M10.31 / M10.32 — when SQL has materialised a traversal sequence
    // (DISTINCT / LIMIT / OFFSET / ORDER BY), report that sequence's
    // length so apps that drive walking by record-count get the
    // post-clause row count.
    if (t->has_recno_sequence()) {
        *pulRecordCount = static_cast<UNSIGNED32>(t->recno_sequence().size());
    } else if (t->has_filter()) {
        // M10.33 — WHERE-filtered cursor without an installed
        // sequence (no ORDER BY / DISTINCT / LIMIT). Count
        // matching live rows on demand so BETWEEN / LIKE / regular
        // predicates surface their cardinality through GetRecordCount.
        std::uint32_t rc = t->record_count();
        std::uint32_t pass = 0;
        for (std::uint32_t r = 1; r <= rc; ++r) {
            if (auto g = t->goto_record(r); !g) continue;
            if (t->is_deleted()) continue;
            if (!t->passes_filter()) continue;
            ++pass;
        }
        *pulRecordCount = pass;
    } else if (bFilterOption == ADS_RESPECTFILTERS &&
               !openads::engine::show_deleted()) {
        // ADS_RESPECTFILTERS: count live records only when deleted records
        // are hidden (SET DELETED ON). Walk the raw record range, skipping
        // deleted rows, then restore the cursor to its original position.
        const std::uint32_t saved = t->recno();
        std::uint32_t rc   = t->record_count();
        std::uint32_t live = 0;
        for (std::uint32_t r = 1; r <= rc; ++r) {
            if (auto g = t->goto_record(r); !g) continue;
            if (!t->is_deleted()) ++live;
        }
        t->goto_record(saved);
        *pulRecordCount = live;
    } else {
        *pulRecordCount = t->record_count();
    }
    return ok();
}

UNSIGNED32 AdsGetField(ADSHANDLE hTable, UNSIGNED8* pucField,
                       UNSIGNED8* pucBuf, UNSIGNED32* pulLen,
                       UNSIGNED16 /*usOption*/) {
    if (auto* rt = get_remote_table(hTable)) {
        if (pulLen == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
        // M12.17/18 — serve from row cache. Cache populated either
        // by piggyback on the prior nav-op ack (M12.18) or by a
        // standalone FetchCurrentRow call here on first access.
        // xbrowse-style W cols × H rows repaint: 1 RTT per row,
        // zero RTT per cell.
        if (!rt->row_valid) {
            auto fr = rt->conn->fetch_current_row(rt);
            if (!fr) return fail(fr.error());
        }
        if (rt->row_valid) {
            auto i = remote_field_index(rt, pucField);
            if (i == std::numeric_limits<std::size_t>::max()) {
                return fail(openads::AE_COLUMN_NOT_FOUND, "");
            }
            // ADS_STRING == 4; pad CHARACTER fields to declared width.
            std::string val = rt->current_row[i];
            if (rt->fields[i].type == ADS_STRING)
                val = pad_char_field(std::move(val), rt->fields[i].length);
            openads::abi::copy_to_caller(pucBuf, pulLen, val);
            return ok();
        }
        // EoF / no row — fall through to a plain GetField round-
        // trip; preserves the prior behaviour for callers that
        // probe past the end of the table.
        auto fname = openads::abi::to_internal(pucField, 0);
        auto r = rt->conn->get_field(rt->id, fname);
        if (!r) return fail(r.error());
        // Pad if we can resolve the field descriptor from the cached schema.
        std::string val = r.value();
        auto fi = remote_field_index(rt, pucField);
        if (fi != std::numeric_limits<std::size_t>::max() &&
            rt->fields[fi].type == ADS_STRING) {
            val = pad_char_field(std::move(val), rt->fields[fi].length);
        }
        openads::abi::copy_to_caller(pucBuf, pulLen, val);
        return ok();
    }
    if (auto* ops = openads::abi::backend_table_ops_for(hTable))
        if (ops->get_field) return ops->get_field(hTable, pucField, pucBuf, pulLen, 0);
    Table* t = get_table(hTable);
    if (!t || pulLen == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    std::uint16_t idx = 0;
    if (!resolve_field_index_h(hTable, t, pucField, &idx)) {
        return fail(openads::AE_COLUMN_NOT_FOUND, "");
    }
    auto v = t->read_field(idx);
    if (!v) return fail(v.error());
    // Re-pad CHARACTER fields to the declared field width on the way out.
    // The internal decode (make_string) trims for the SQL/index engine;
    // ABI callers expect the full fixed-width value.
    std::string val = v.value().as_string;
    const auto& fd = t->field_descriptor(idx);
    if (fd.type == openads::drivers::DbfFieldType::Character)
        val = pad_char_field(std::move(val), fd.length);
    openads::abi::copy_to_caller(pucBuf, pulLen, val);
    return ok();
}

UNSIGNED32 AdsGetLastError(UNSIGNED32* pulCode, UNSIGNED8* pucBuf,
                           UNSIGNED16* pusBufLen) {
    if (pulCode != nullptr) *pulCode = static_cast<UNSIGNED32>(
        openads::abi::last_error_code());
    if (pucBuf != nullptr && pusBufLen != nullptr) {
        openads::abi::copy_to_caller(pucBuf, pusBufLen,
                                     openads::abi::last_error_message());
    }
    return openads::AE_SUCCESS;
}

// SAP / rddads signature: 5 args.
//   AdsGetVersion(&ulMajor, &ulMinor, &ucLetter, ucDesc, &usDescLen)
//
// pucLetter : single ASCII letter (NOT a UNSIGNED32 codepoint) —
//             writing 4 bytes into a 1-byte slot was undefined.
// pucDesc / pusDescLen : caller-allocated description buffer +
//             in/out length. We write the OpenADS version string
//             and truncate to the caller's capacity.
UNSIGNED32 AdsGetVersion(UNSIGNED32* pulMajor, UNSIGNED32* pulMinor,
                         UNSIGNED8*  pucLetter, UNSIGNED8* pucDesc,
                         UNSIGNED16* pusDescLen) {
    if (pulMajor  != nullptr) *pulMajor  = 0;
    if (pulMinor  != nullptr) *pulMinor  = 0;
    if (pucLetter != nullptr) *pucLetter = 'a';
    static const char kDesc[] = "OpenADS ACE-compatible engine";
    if (pucDesc != nullptr && pusDescLen != nullptr) {
        UNSIGNED16 cap = *pusDescLen;
        UNSIGNED16 n = static_cast<UNSIGNED16>(
            sizeof(kDesc) - 1 < cap ? sizeof(kDesc) - 1 : cap);
        if (n > 0) std::memcpy(pucDesc, kDesc, n);
        if (cap > n) pucDesc[n] = '\0';
        *pusDescLen = n;
    } else if (pusDescLen != nullptr) {
        *pusDescLen = sizeof(kDesc) - 1;
    }
    return openads::AE_SUCCESS;
}

// --- M9.15 server info -----------------------------------------------------
//
// Local-mode connections now report the host name + the local wall clock
// instead of empty strings / 0. AdsGetServerTime returns a six-arg shape
// matching the ACE 6.x signature rddads' ADSGETSERVERTIME function expects
// (date string, time string, milliseconds since midnight) — the previous
// 2-arg stub left rddads' on-stack pucDateBuf / pucTimeBuf uninitialised.

namespace {

UNSIGNED32 emit_text_with_u16len(UNSIGNED8* pucBuf, UNSIGNED16* pusLen,
                                 const std::string& s) {
    if (pusLen == nullptr) return openads::AE_INTERNAL_ERROR;
    UNSIGNED16 cap = *pusLen;
    UNSIGNED16 n = static_cast<UNSIGNED16>(s.size() < cap ? s.size() : cap);
    if (pucBuf != nullptr && cap > 0) {
        if (n > 0) std::memcpy(pucBuf, s.data(), n);
        if (n < cap) pucBuf[n] = '\0';
    }
    *pusLen = static_cast<UNSIGNED16>(s.size());
    return openads::AE_SUCCESS;
}

}  // namespace

UNSIGNED32 AdsGetServerName(ADSHANDLE /*hConnect*/,
                            UNSIGNED8* pucBuf, UNSIGNED16* pusLen) {
    return emit_text_with_u16len(pucBuf, pusLen,
                                 openads::platform::host_name());
}

UNSIGNED32 AdsGetServerTime(ADSHANDLE  /*hConnect*/,
                            UNSIGNED8* pucDateBuf, UNSIGNED16* pusDateLen,
                            SIGNED32*  plTime,
                            UNSIGNED8* pucTimeBuf, UNSIGNED16* pusTimeLen) {
    auto wc = openads::platform::now_local();
    auto rc = emit_text_with_u16len(pucDateBuf, pusDateLen, wc.date);
    if (rc != openads::AE_SUCCESS) return rc;
    rc = emit_text_with_u16len(pucTimeBuf, pusTimeLen, wc.time);
    if (rc != openads::AE_SUCCESS) return rc;
    if (plTime != nullptr) *plTime = wc.ms_of_day;
    return openads::AE_SUCCESS;
}

// ── Trigger execution ──────────────────────────────────────────────────────────
// Fires enabled triggers on `table_alias` matching `event_mask` (1=INSERT
// 2=UPDATE 3=DELETE) and `timing` (1=BEFORE 2=INSTEAD_OF 4=AFTER).
// Triggers are sorted by priority (ascending) before firing.
// Nesting is tracked with a thread-local depth counter; execution aborts
// when depth exceeds 64 to prevent infinite recursion.
// Returns true if at least one INSTEAD OF trigger was fired (caller should
// skip the actual DML in that case).

namespace {
thread_local int tl_trigger_depth = 0;
static constexpr int kTrigMaxDepth = 64;

// Find the connection handle for a given Connection pointer.
Handle handle_for_conn(Connection* c) {
    auto& s = state();
    Handle found = 0;
    s.registry.for_each_handle([&](Handle h, HandleKind k, void* p) {
        if (found) return;
        if (k == HandleKind::Connection && static_cast<Connection*>(p) == c)
            found = h;
    });
    return found;
}

// Return the SQL body to execute for a trigger — prefer container unless it
// looks like a short type code (e.g. "1"), in which case fall back to procedure.
static const std::string& trigger_sql_body(const openads::engine::DataDict::TriggerEntry& e) {
    if (e.container.size() > 4) return e.container;
    if (!e.procedure.empty())   return e.procedure;
    return e.container;
}

// ── Procedural trigger body executor ────────────────────────────────────────
// Implements a minimal interpreter for SAP ADS trigger body SQL:
//   DECLARE @var TYPE       — declares a local variable (ignored, just tracked)
//   SET @var = expr         — assigns a value; expr may be __new.field, __old.field,
//                             a string literal, a numeric literal, or a SQL expression
//   __new.fieldname         — value of the new record's field (INSERT/UPDATE)
//   __old.fieldname         — value of the old record's field (UPDATE/DELETE)
// All other statements are executed via AdsExecuteSQLDirect after substitution.

// Type alias to avoid MSVC C2562 when returning std::map<> from a function
// inside an extern "C" / anonymous-namespace block.
//
// The trig_* helpers below return C++ types (std::string / std::vector /
// TrigError_). Give them C++ linkage so MSVC does not raise C4190 (C-linkage
// function returning a C++-incompatible type) under /W4 /WX.
extern "C++" {

using TrigFieldMap_ = std::map<std::string, std::string>;

// SQL-quote a raw string value (escape embedded quotes, wrap in single quotes).
static std::string trig_sql_quote_(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    out += '\'';
    for (char c : s) {
        if (c == '\'') out += "''";
        else out += c;
    }
    out += '\'';
    return out;
}

// Trim leading and trailing whitespace from a string.
static std::string trig_trim_(const std::string& s) {
    std::size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n')) ++b;
    while (e > b && (s[e-1] == ' ' || s[e-1] == '\t' || s[e-1] == '\r' || s[e-1] == '\n')) --e;
    return s.substr(b, e - b);
}

// Case-insensitive prefix check.
static bool trig_ci_pfx_(const std::string& s, const char* prefix, std::size_t plen) {
    if (s.size() < plen) return false;
    for (std::size_t i = 0; i < plen; ++i) {
        if (std::tolower(static_cast<unsigned char>(s[i])) !=
            std::tolower(static_cast<unsigned char>(prefix[i]))) return false;
    }
    return true;
}

// Split trigger body into statements at ';' boundaries, respecting string literals.
static std::vector<std::string> trig_split_stmts_(const std::string& body) {
    std::vector<std::string> out;
    std::string cur;
    bool in_sq = false;
    for (std::size_t i = 0; i < body.size(); ++i) {
        char c = body[i];
        if (in_sq) {
            cur += c;
            if (c == '\'' && i + 1 < body.size() && body[i+1] == '\'') {
                cur += body[++i];  // escaped ''
            } else if (c == '\'') {
                in_sq = false;
            }
        } else if (c == '\'') {
            in_sq = true;
            cur += c;
        } else if (c == ';') {
            auto ts = trig_trim_(cur);
            if (!ts.empty()) out.push_back(std::move(ts));
            cur.clear();
        } else {
            cur += c;
        }
    }
    auto ts = trig_trim_(cur);
    if (!ts.empty()) out.push_back(std::move(ts));
    return out;
}

// Collect all field values from a Table into a lowercase-keyed string map.
// Char fields are space-trimmed; numeric/date fields use as_string representation.
static void trig_collect_row_(Table* t, TrigFieldMap_& m) {
    if (!t) return;
    std::uint16_t nf = t->field_count();
    for (std::uint16_t i = 0; i < nf; ++i) {
        const auto& fd = t->field_descriptor(i);
        std::string name = fd.name;
        for (auto& ch : name)
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        auto v = t->read_field(i);
        if (!v) { m[name] = ""; continue; }
        std::string sv = v.value().as_string;
        while (!sv.empty() && sv.back() == ' ') sv.pop_back();
        m[name] = std::move(sv);
    }
}

// Evaluate a SET expression RHS, returning a SQL-ready value string.
// __new/old field refs → SQL-quoted string.  SQL functions/literals → as-is.
static std::string trig_eval_rhs_(
    const std::string& rhs,
    const TrigFieldMap_& new_f,
    const TrigFieldMap_& old_f,
    const TrigFieldMap_& vars)
{
    auto lc = [](char x) { return static_cast<char>(std::tolower(static_cast<unsigned char>(x))); };
    std::string t = trig_trim_(rhs);
    // __new.field or __old.field
    if (t.size() > 6) {
        bool is_new = (lc(t[0])=='_' && lc(t[1])=='_' && lc(t[2])=='n' &&
                       lc(t[3])=='e' && lc(t[4])=='w' && t[5]=='.');
        bool is_old = (lc(t[0])=='_' && lc(t[1])=='_' && lc(t[2])=='o' &&
                       lc(t[3])=='l' && lc(t[4])=='d' && t[5]=='.');
        if (is_new || is_old) {
            std::string fname = trig_trim_(t.substr(6));
            for (auto& c : fname) c = lc(c);
            const auto& fmap = is_new ? new_f : old_f;
            auto it = fmap.find(fname);
            return (it != fmap.end()) ? trig_sql_quote_(it->second) : "NULL";
        }
    }
    // @var reference
    if (!t.empty() && t[0] == '@') {
        std::string vname = t.substr(1);
        for (auto& c : vname) c = lc(c);
        auto it = vars.find(vname);
        return (it != vars.end()) ? it->second : "NULL";
    }
    // Subquery: ( SELECT field FROM __new ) or ( SELECT field FROM __old ) or __input
    // Pattern: ( SELECT <field> FROM __new|__old|__input )
    if (!t.empty() && t.front() == '(') {
        std::string inner = trig_trim_(t.substr(1, t.size() - 2));
        if (trig_ci_pfx_(inner, "SELECT", 6)) {
            std::size_t p = 6;
            while (p < inner.size() && inner[p] == ' ') ++p;
            // Extract field name (up to whitespace or FROM)
            std::size_t fs = p;
            while (p < inner.size() &&
                   (std::isalnum(static_cast<unsigned char>(inner[p])) ||
                    inner[p] == '_' || inner[p] == '[' || inner[p] == ']')) ++p;
            std::string field = inner.substr(fs, p - fs);
            // Strip brackets
            if (!field.empty() && field.front() == '[') field = field.substr(1);
            if (!field.empty() && field.back() == ']') field.pop_back();
            for (auto& c : field) c = lc(c);
            while (p < inner.size() && inner[p] == ' ') ++p;
            if (p + 4 <= inner.size() &&
                lc(inner[p])=='f' && lc(inner[p+1])=='r' &&
                lc(inner[p+2])=='o' && lc(inner[p+3])=='m') {
                p += 4;
                while (p < inner.size() && inner[p] == ' ') ++p;
                std::size_t ts = p;
                while (p < inner.size() && !std::isspace(static_cast<unsigned char>(inner[p]))) ++p;
                std::string src = inner.substr(ts, p - ts);
                for (auto& c : src) c = lc(c);
                const TrigFieldMap_* fmap = nullptr;
                if (src == "__new")   fmap = &new_f;
                else if (src == "__old")   fmap = &old_f;
                else if (src == "__input") fmap = &new_f;  // __input: params passed as new_f
                if (fmap) {
                    auto it = fmap->find(field);
                    return (it != fmap->end()) ? trig_sql_quote_(it->second) : "NULL";
                }
            }
        }
    }
    // String literal (already SQL-quoted) or SQL expression/function: pass through
    return t;
}

// Substitute __new.field, __old.field, and @var references in a SQL statement.
// String literals inside the statement are passed through unchanged.
static std::string trig_substitute_(
    const std::string& stmt,
    const TrigFieldMap_& new_f,
    const TrigFieldMap_& old_f,
    const TrigFieldMap_& vars)
{
    auto lc = [](char x) { return static_cast<char>(std::tolower(static_cast<unsigned char>(x))); };
    std::string out;
    out.reserve(stmt.size() * 2);
    std::size_t i = 0;
    bool in_str = false;
    while (i < stmt.size()) {
        char c = stmt[i];
        if (in_str) {
            out += c; ++i;
            if (c == '\'' && i < stmt.size() && stmt[i] == '\'') {
                out += stmt[i++];  // escaped ''
            } else if (c == '\'') {
                in_str = false;
            }
            continue;
        }
        if (c == '\'') { in_str = true; out += c; ++i; continue; }
        // __new.field or __old.field
        if (c == '_' && i + 6 <= stmt.size()) {
            bool is_new = (lc(stmt[i+0])=='_' && lc(stmt[i+1])=='_' && lc(stmt[i+2])=='n' &&
                           lc(stmt[i+3])=='e' && lc(stmt[i+4])=='w' && stmt[i+5]=='.');
            bool is_old = (!is_new && lc(stmt[i+0])=='_' && lc(stmt[i+1])=='_' &&
                           lc(stmt[i+2])=='o' && lc(stmt[i+3])=='l' &&
                           lc(stmt[i+4])=='d' && stmt[i+5]=='.');
            if (is_new || is_old) {
                i += 6;
                std::size_t fs = i;
                while (i < stmt.size() &&
                       (std::isalnum(static_cast<unsigned char>(stmt[i])) || stmt[i]=='_'))
                    ++i;
                std::string fname = stmt.substr(fs, i - fs);
                for (auto& ch : fname) ch = lc(ch);
                const auto& fmap = is_new ? new_f : old_f;
                auto it = fmap.find(fname);
                out += (it != fmap.end()) ? trig_sql_quote_(it->second) : "NULL";
                continue;
            }
        }
        // @var reference
        if (c == '@') {
            std::size_t vs = i + 1, ve = vs;
            while (ve < stmt.size() &&
                   (std::isalnum(static_cast<unsigned char>(stmt[ve])) || stmt[ve]=='_'))
                ++ve;
            if (ve > vs) {
                std::string vname = stmt.substr(vs, ve - vs);
                for (auto& ch : vname) ch = lc(ch);
                auto it = vars.find(vname);
                if (it != vars.end()) { out += it->second; i = ve; continue; }
            }
        }
        out += c; ++i;
    }
    return out;
}

// Error info returned from a trigger body (via INSERT INTO __error).
struct TrigError_ {
    bool        has_error = false;
    std::uint32_t errno_val = 0;
    std::string   message;
};

// Parse: INSERT INTO __error [(errno, message)] VALUES (num, 'msg')
// or INSERT INTO __error (message) VALUES ('msg')
static TrigError_ trig_parse_error_insert_(const std::string& ts) {
    TrigError_ e;
    // Locate VALUES keyword
    auto vu = ts; for (auto& c : vu) c = static_cast<char>(std::toupper((unsigned char)c));
    auto vpos = vu.find("VALUES");
    if (vpos == std::string::npos) return e;
    // Find the opening paren after VALUES
    auto p = ts.find('(', vpos + 6);
    if (p == std::string::npos) return e;
    auto q = ts.rfind(')');
    if (q == std::string::npos || q <= p) return e;
    std::string inner = trig_trim_(ts.substr(p + 1, q - p - 1));
    // Try to parse: <num> , 'message'  OR  'message'
    e.has_error = true;
    // Check if first token is numeric
    std::size_t i = 0;
    bool neg = (i < inner.size() && inner[i] == '-'); if (neg) ++i;
    bool is_num = (i < inner.size() && std::isdigit((unsigned char)inner[i]));
    if (is_num) {
        std::size_t ns = i;
        while (i < inner.size() && std::isdigit((unsigned char)inner[i])) ++i;
        e.errno_val = static_cast<std::uint32_t>(
            std::atoi(inner.substr(neg ? 1 : ns, i - ns).c_str()));
        // Skip comma
        while (i < inner.size() && (inner[i] == ' ' || inner[i] == ',')) ++i;
    }
    // Remaining is the message string literal
    if (i < inner.size() && inner[i] == '\'') {
        ++i;
        while (i < inner.size() && inner[i] != '\'') {
            if (inner[i] == '\'' && i + 1 < inner.size() && inner[i+1] == '\'') {
                e.message += '\''; i += 2;
            } else {
                e.message += inner[i++];
            }
        }
    }
    return e;
}

// Execute an ADS procedural trigger body.  Handles DECLARE @var, SET @var = expr,
// __new/__old field substitution, and @variable substitution before SQL execution.
// When is_instead_of=true, INSERT...SELECT...FROM __new is executed (the trigger
// body must manually write the row).  Returns error info if the body wrote to __error.
static TrigError_ trig_execute_body_(
    Handle hConn,
    const std::string& body,
    const TrigFieldMap_& new_f,
    const TrigFieldMap_& old_f,
    bool is_instead_of = false)
{
    auto stmts = trig_split_stmts_(body);
    TrigFieldMap_ vars;
    auto lc = [](char x) { return static_cast<char>(std::tolower(static_cast<unsigned char>(x))); };

    for (const auto& raw : stmts) {
        std::string ts = trig_trim_(raw);
        if (ts.empty()) continue;

        // DECLARE @var [TYPE] — register variable; DECLARE name CURSOR — skip
        if (trig_ci_pfx_(ts, "DECLARE", 7)) {
            std::size_t p = 7;
            while (p < ts.size() && ts[p] == ' ') ++p;
            if (p < ts.size() && ts[p] == '@') {
                std::size_t ns = p + 1, ne = ns;
                while (ne < ts.size() &&
                       (std::isalnum(static_cast<unsigned char>(ts[ne])) || ts[ne]=='_')) ++ne;
                std::string vname = ts.substr(ns, ne - ns);
                for (auto& c : vname) c = lc(c);
                vars.emplace(vname, "NULL");
            }
            // Cursor declarations (DECLARE name CURSOR AS SELECT ...) are skipped entirely
            continue;
        }

        // SET @var = expr
        if (trig_ci_pfx_(ts, "SET", 3) && ts.size() > 3 &&
            (ts[3] == ' ' || ts[3] == '\t')) {
            std::size_t p = 3;
            while (p < ts.size() && ts[p] == ' ') ++p;
            if (p < ts.size() && ts[p] == '@') {
                std::size_t ns = p + 1, ne = ns;
                while (ne < ts.size() &&
                       (std::isalnum(static_cast<unsigned char>(ts[ne])) || ts[ne]=='_')) ++ne;
                std::string vname = ts.substr(ns, ne - ns);
                for (auto& c : vname) c = lc(c);
                std::size_t eq = ne;
                while (eq < ts.size() && ts[eq] == ' ') ++eq;
                if (eq < ts.size() && ts[eq] == '=') {
                    ++eq;
                    while (eq < ts.size() && ts[eq] == ' ') ++eq;
                    vars[vname] = trig_eval_rhs_(ts.substr(eq), new_f, old_f, vars);
                }
            }
            continue;
        }

        // OPEN cursor AS SELECT ... — cursor loops not supported; skip
        if (trig_ci_pfx_(ts, "OPEN", 4)) continue;
        // FETCH / CLOSE cursor — skip
        if (trig_ci_pfx_(ts, "FETCH", 5)) continue;
        if (trig_ci_pfx_(ts, "CLOSE", 5)) continue;
        // WHILE ... DO ... END (cursor loop) — skip entire block
        if (trig_ci_pfx_(ts, "WHILE", 5)) continue;
        // IF ... THEN / ELSE / END — skip conditional blocks
        if (trig_ci_pfx_(ts, "IF ", 3) || trig_ci_pfx_(ts, "ELSEIF", 6) ||
            trig_ci_pfx_(ts, "ELSE", 4) || trig_ci_pfx_(ts, "END", 3)) continue;
        // EXECUTE IMMEDIATE / EXECUTE PROCEDURE — not supported; skip
        if (trig_ci_pfx_(ts, "EXECUTE", 7)) continue;
        // DROP TABLE #... — session temp tables; skip
        if (trig_ci_pfx_(ts, "DROP TABLE #", 12)) continue;
        // INSERT INTO __error (errno, message) VALUES (...) — capture error and stop
        {
            std::string tsu = ts;
            for (auto& c : tsu) c = static_cast<char>(std::toupper((unsigned char)c));
            if (tsu.find("__ERROR") != std::string::npos &&
                trig_ci_pfx_(ts, "INSERT", 6)) {
                return trig_parse_error_insert_(ts);
            }
        }
        // For non-INSTEAD OF triggers: INSERT ... SELECT ... FROM __new or __old is
        // a trigger trying to re-insert the source row — skip to avoid duplicate writes.
        // For INSTEAD OF triggers: this INSERT is the actual write the trigger performs;
        // fall through and execute it.
        if (!is_instead_of && trig_ci_pfx_(ts, "INSERT", 6) &&
            (ts.find("__new") != std::string::npos ||
             ts.find("__old") != std::string::npos)) continue;

        // Plain SQL: substitute references and execute
        std::string sql = trig_trim_(trig_substitute_(ts, new_f, old_f, vars));
        if (sql.empty()) continue;

        ADSHANDLE hStmt = 0;
        if (AdsCreateSQLStatement(hConn, &hStmt) != openads::AE_SUCCESS) continue;
        ADSHANDLE hCursor = 0;
        AdsExecuteSQLDirect(
            hStmt,
            reinterpret_cast<UNSIGNED8*>(const_cast<char*>(sql.c_str())),
            &hCursor);
        if (hCursor) AdsCloseTable(hCursor);
        AdsCloseSQLStatement(hStmt);
    }
    return TrigError_{};
}

}  // extern "C++"  — trig_ helpers regain C++ linkage (silences C4190)

// fire_triggers_ — fire all enabled, matching triggers for a given event + timing.
// timing: 1=BEFORE  2=INSTEAD_OF  4=AFTER
// Returns true if an INSTEAD OF trigger was fired (caller should skip the actual DML).
bool fire_triggers_(Handle hConn, Connection* conn,
                    const std::string& table_alias, std::uint32_t event_mask,
                    std::uint32_t timing,
                    Table* new_tbl = nullptr, Table* old_tbl = nullptr) {
    if (tl_trigger_depth >= kTrigMaxDepth) return false; // depth limit
    if (conn->triggers_disabled()) return false;          // connection-level disable
    auto* dd = conn->dd();
    if (!dd) return false;

    using TE = openads::engine::DataDict::TriggerEntry;
    std::vector<const TE*> matched;
    for (const auto& [name, trig] : dd->triggers()) {
        if (!trig.enabled) continue;
        if (trig.event_mask != event_mask) continue;
        if (trig.timing != timing) continue;
        // case-insensitive alias compare
        const auto& ta = trig.table_alias;
        if (ta.size() != table_alias.size()) continue;
        bool match = true;
        for (std::size_t i = 0; i < ta.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(ta[i])) !=
                std::tolower(static_cast<unsigned char>(table_alias[i]))) {
                match = false; break;
            }
        }
        if (!match) continue;
        matched.push_back(&trig);
    }
    if (matched.empty()) return false;

    // Sort by priority ascending (lower priority number fires first)
    std::sort(matched.begin(), matched.end(),
              [](const TE* a, const TE* b){ return a->priority < b->priority; });

    // Collect __new / __old field values if WANT_VALUES option is set (bit 0x01).
    // If any trigger in the set requires values, collect them for all.
    bool want_values = false;
    bool want_memos  = false;
    for (const auto* e : matched) {
        if (e->options & 0x01u) { want_values = true; }
        if (e->options & 0x02u) { want_memos  = true; }
    }
    TrigFieldMap_ new_fields, old_fields;
    if (want_values) {
        if (want_memos) {
            trig_collect_row_(new_tbl, new_fields);
            trig_collect_row_(old_tbl, old_fields);
        } else {
            // NO MEMOS/BLOBS option: skip memo and binary fields
            auto collect_no_memo = [](Table* t, TrigFieldMap_& m) {
                if (!t) return;
                std::uint16_t nf = t->field_count();
                for (std::uint16_t i = 0; i < nf; ++i) {
                    const auto& fd = t->field_descriptor(i);
                    // Skip memo (M), blob (B), binary (U/W) field types
                    char ft = static_cast<char>(
                        std::toupper(static_cast<unsigned char>(fd.type)));
                    if (ft == 'M' || ft == 'B' || ft == 'U' || ft == 'W') continue;
                    std::string name = fd.name;
                    for (auto& ch : name)
                        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                    auto v = t->read_field(i);
                    if (!v) { m[name] = ""; continue; }
                    std::string sv = v.value().as_string;
                    while (!sv.empty() && sv.back() == ' ') sv.pop_back();
                    m[name] = std::move(sv);
                }
            };
            collect_no_memo(new_tbl, new_fields);
            collect_no_memo(old_tbl, old_fields);
        }
    }

    bool instead_of_fired = false;
    ++tl_trigger_depth;
    for (const auto* e : matched) {
        const auto& sql_body = trigger_sql_body(*e);
        if (sql_body.empty()) continue;

        // Strip trailing non-printable garbage (binary .am file padding)
        std::string body_copy = sql_body;
        while (!body_copy.empty()) {
            unsigned char last = static_cast<unsigned char>(body_copy.back());
            if (last >= 0x20u || last == '\t' || last == '\n' || last == '\r') break;
            body_copy.pop_back();
        }

        TrigError_ err = trig_execute_body_(hConn, body_copy, new_fields, old_fields,
                                             timing == 2u /*is_instead_of*/);
        if (timing == 2u) instead_of_fired = true;
        // If the trigger wrote to __error, propagate the error but continue
        // (per SAP semantics, remaining triggers still fire; the error is
        // returned to the client after all triggers complete).
        (void)err; // future: propagate error code back to client
    }
    --tl_trigger_depth;
    return instead_of_fired;
}
} // anonymous namespace

UNSIGNED32 AdsAppendRecord(ADSHANDLE hTable) {
    if (auto* rt = get_remote_table(hTable)) {
        remote_settle_cursor(rt);                   // M12.21 option C
        rt->row_valid        = false;               // M12.17
        rt->rec_count_cached = false;               // M12.19
        auto r = rt->conn->append_blank(rt->id);
        if (!r) return fail(r.error());
        return ok();
    }
#if defined(OPENADS_WITH_MSSQL)
    if (get_mssql_table(hTable)) {
        return fail(openads::AE_FUNCTION_NOT_AVAILABLE,
                    "MssqlTable: write not available in v1");
    }
#endif
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    auto r = t->append_record();
    if (!r) return fail(r.error());
    // ACE semantics: a freshly-appended record in a non-exclusive table
    // is automatically locked. X#'s ADSRDD relies on this — its GoHot
    // refuses to write a record it sees as unlocked. Best-effort: the
    // lock layer no-ops in read/exclusive modes, and a lock contention
    // here doesn't invalidate the append itself.
    (void)t->try_lock_record_excl(t->recno());
    t->set_pending_append(true);
    return ok();
}

UNSIGNED32 AdsWriteRecord(ADSHANDLE hTable) {
    if (auto* rt = get_remote_table(hTable)) {
        rt->row_valid = false;                      // M12.17 cache invalidation
        auto r = rt->conn->flush_table(rt->id);
        if (!r) return fail(r.error());
        return ok();
    }
#if defined(OPENADS_WITH_MSSQL)
    if (get_mssql_table(hTable)) {
        return fail(openads::AE_FUNCTION_NOT_AVAILABLE,
                    "MssqlTable: write not available in v1");
    }
#endif
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    bool is_insert = t->pending_append();
    t->set_pending_append(false);
    std::uint32_t event_mask = is_insert ? 1u : 2u;

    if (is_insert) {
        if (Connection* conn = conn_for_table(t)) {
            if (auto ri = ri_check_insert(conn, *t); !ri)
                return fail(ri.error());
        }
    } else {
        if (Connection* conn = conn_for_table(t)) {
            if (auto ri = ri_enforce_update(conn, *t); !ri)
                return fail(ri.error());
        }
    }

    // Trigger firing order: INSTEAD OF → (skip flush) OR BEFORE → flush → AFTER
    if (Connection* conn = conn_for_table(t)) {
        std::string alias = ri_alias_for_path(conn, t->path());
        if (!alias.empty()) {
            Handle hConn = handle_for_conn(conn);
            if (hConn) {
                // INSTEAD OF trigger: fire and skip the actual write
                if (fire_triggers_(hConn, conn, alias, event_mask, 2u /*INSTEAD_OF*/, t))
                    return ok();
                // BEFORE trigger: fire before the write
                fire_triggers_(hConn, conn, alias, event_mask, 1u /*BEFORE*/, t);
            }
        }
    }

    if (!t->deferred_flush()) {
        auto r = t->flush();
        if (!r) return fail(r.error());
    }

    if (Connection* conn = conn_for_table(t)) {
        std::string alias = ri_alias_for_path(conn, t->path());
        if (!alias.empty()) {
            Handle hConn = handle_for_conn(conn);
            // AFTER trigger: __new = current record (new values for UPDATE, inserted for INSERT)
            if (hConn) fire_triggers_(hConn, conn, alias, event_mask, 4u /*AFTER*/, t);
        }
    }
    return ok();
}

UNSIGNED32 AdsDeleteRecord(ADSHANDLE hTable) {
    if (auto* rt = get_remote_table(hTable)) {
        remote_settle_cursor(rt);                   // M12.21 option C
        rt->row_valid        = false;               // M12.17
        rt->rec_count_cached = false;               // M12.19 (Pack drops the row)
        auto r = rt->conn->delete_record(rt->id);
        if (!r) return fail(r.error());
        return ok();
    }
#if defined(OPENADS_WITH_MSSQL)
    if (get_mssql_table(hTable)) {
        return fail(openads::AE_FUNCTION_NOT_AVAILABLE,
                    "MssqlTable: write not available in v1");
    }
#endif
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    t->set_pending_append(false);   // abandon any in-flight append
    if (Connection* conn = conn_for_table(t)) {
        if (auto ri = ri_enforce_delete(conn, *t); !ri)
            return fail(ri.error());
    }

    // Trigger firing order for DELETE: INSTEAD OF → (skip delete) OR BEFORE → delete → AFTER
    if (Connection* conn = conn_for_table(t)) {
        std::string alias = ri_alias_for_path(conn, t->path());
        if (!alias.empty()) {
            Handle hConn = handle_for_conn(conn);
            if (hConn) {
                // INSTEAD OF DELETE: __old = current record
                if (fire_triggers_(hConn, conn, alias, 3u, 2u /*INSTEAD_OF*/, nullptr, t))
                    return ok();
                // BEFORE DELETE: __old = current record (about to be deleted)
                fire_triggers_(hConn, conn, alias, 3u, 1u /*BEFORE*/, nullptr, t);
            }
        }
    }

    auto r = t->mark_deleted();
    if (!r) return fail(r.error());

    if (Connection* conn = conn_for_table(t)) {
        std::string alias = ri_alias_for_path(conn, t->path());
        if (!alias.empty()) {
            Handle hConn = handle_for_conn(conn);
            // AFTER DELETE: __old = the deleted record
            if (hConn) fire_triggers_(hConn, conn, alias, 3u, 4u /*AFTER*/, nullptr, t);
        }
    }
    return ok();
}

UNSIGNED32 AdsRecallRecord(ADSHANDLE hTable) {
    if (auto* rt = get_remote_table(hTable)) {
        remote_settle_cursor(rt);                   // M12.21 option C
        rt->row_valid        = false;               // M12.17
        rt->rec_count_cached = false;               // M12.19
        auto r = rt->conn->recall_record(rt->id);
        if (!r) return fail(r.error());
        return ok();
    }
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    auto r = t->recall_deleted();
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsIsRecordDeleted(ADSHANDLE hTable, UNSIGNED16* pbDeleted) {
    if (pbDeleted == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    if (auto* rt = get_remote_table(hTable)) {
        // M12.18 — deleted flag rides with the row trailer.
        if (rt->row_valid) {
            *pbDeleted = rt->current_deleted ? 1 : 0;
            return ok();
        }
        auto r = rt->conn->is_record_deleted(rt->id);
        if (!r) return fail(r.error());
        *pbDeleted = r.value() ? 1 : 0;
        return ok();
    }
    if (auto* ops = openads::abi::backend_table_ops_for(hTable))
        if (ops->is_record_deleted) return ops->is_record_deleted(hTable, pbDeleted);
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "");
    *pbDeleted = t->is_deleted() ? 1 : 0;
    return ok();
}

UNSIGNED32 AdsSetString(ADSHANDLE hTable, UNSIGNED8* pucField,
                        UNSIGNED8* pucValue, UNSIGNED32 ulLen) {
    // RCB 2026-05-22 17:03 — AdsSet* previously had no awareness of statement
    // handles.  get_table() only queries the HandleRegistry for HandleKind::Table
    // and returns nullptr for anything else, so calls against a prepared statement
    // handle always failed with [5000] unknown table.  We check set_stmt_param
    // first; if the handle is in stmt_map the value is stored as a quoted SQL
    // string literal and we return immediately without touching the table path.
    // Single quotes inside the value are doubled to produce a valid SQL literal.
    if (pucField != nullptr) {
        std::string val(pucValue ? reinterpret_cast<const char*>(pucValue) : "",
                        pucValue ? static_cast<std::size_t>(ulLen) : 0u);
        std::string escaped;
        escaped.reserve(val.size() + 2);
        for (char c : val) { escaped += c; if (c == '\'') escaped += c; }
        if (set_stmt_param(hTable,
                           reinterpret_cast<const char*>(pucField),
                           "'" + escaped + "'"))
            return ok();
    }
    if (auto* rt = get_remote_table(hTable)) {
        if (pucField == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
        std::string fname(reinterpret_cast<const char*>(pucField));
        std::string val;
        if (pucValue != nullptr && ulLen > 0) {
            val.assign(reinterpret_cast<const char*>(pucValue), ulLen);
        }
        remote_settle_cursor(rt);                   // M12.21 option C
        rt->row_valid = false;                      // M12.17 cache invalidation
        auto r = rt->conn->set_field(rt->id, fname, val);
        if (!r) return fail(r.error());
        return ok();
    }
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    std::uint16_t idx = 0;
    if (!resolve_field_index(t, pucField, &idx)) {
        return fail(openads::AE_COLUMN_NOT_FOUND, "");
    }
    std::string val(reinterpret_cast<const char*>(pucValue), ulLen);
    auto r = t->set_field(idx, val);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsSetLogical(ADSHANDLE hTable, UNSIGNED8* pucField,
                         UNSIGNED16 bValue) {
    // RCB 2026-05-22 17:03 — same statement-handle gap as AdsSetString.
    // Logical fields in DBF are stored as 'T'/'F' but the SQL parser accepts
    // 1 and 0 in INSERT/UPDATE VALUES, so we emit those as the literal.
    if (pucField != nullptr)
        if (set_stmt_param(hTable,
                           reinterpret_cast<const char*>(pucField),
                           bValue ? "1" : "0"))
            return ok();
    if (auto* rt = get_remote_table(hTable)) {
        if (pucField == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
        std::string fname(reinterpret_cast<const char*>(pucField));
        remote_settle_cursor(rt);                   // M12.21 option C
        rt->row_valid = false;
        auto r = rt->conn->set_field(rt->id, fname, bValue ? "1" : "0");
        if (!r) return fail(r.error());
        return ok();
    }
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    std::uint16_t idx = 0;
    if (!resolve_field_index(t, pucField, &idx)) {
        return fail(openads::AE_COLUMN_NOT_FOUND, "");
    }
    auto r = t->set_field(idx, bValue != 0);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsSetDouble(ADSHANDLE hTable, UNSIGNED8* pucField,
                        double dValue) {
    // RCB 2026-05-22 17:03 — same statement-handle gap as AdsSetString.
    // AdsSetLongLong also routes through here (it casts to double before calling
    // us), so fixing this one covers both numeric bind types.  We use a char
    // buffer with snprintf rather than std::to_string to avoid locale-dependent
    // decimal separators that would break the SQL parser.
    if (pucField != nullptr) {
        char nbuf[64];
        std::snprintf(nbuf, sizeof(nbuf), "%.17g", dValue);
        if (set_stmt_param(hTable,
                           reinterpret_cast<const char*>(pucField),
                           std::string(nbuf)))
            return ok();
    }
    if (auto* rt = get_remote_table(hTable)) {
        if (pucField == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
        std::string fname(reinterpret_cast<const char*>(pucField));
        char nbuf[64];
        std::snprintf(nbuf, sizeof(nbuf), "%.17g", dValue);
        remote_settle_cursor(rt);                   // M12.21 option C
        rt->row_valid = false;
        auto r = rt->conn->set_field(rt->id, fname, std::string(nbuf));
        if (!r) return fail(r.error());
        return ok();
    }
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    std::uint16_t idx = 0;
    if (!resolve_field_index(t, pucField, &idx)) {
        return fail(openads::AE_COLUMN_NOT_FOUND, "");
    }
    auto r = t->set_field(idx, dValue);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsSetLongLong(ADSHANDLE hTable, UNSIGNED8* pucField,
                          std::int64_t llValue) {
    return AdsSetDouble(hTable, pucField, static_cast<double>(llValue));
}

namespace {

// Inverse of to_julian — convert a Clipper Julian Day Number back to
// a Gregorian (Y, M, D) triple.
void julian_to_ymd(SIGNED32 jd, int& y, int& m, int& d) {
    long L = static_cast<long>(jd) + 68569;
    long N = (4 * L) / 146097;
    L = L - (146097 * N + 3) / 4;
    long I = (4000 * (L + 1)) / 1461001;
    L = L - (1461 * I) / 4 + 31;
    long J = (80 * L) / 2447;
    d = static_cast<int>(L - (2447 * J) / 80);
    L = J / 11;
    m = static_cast<int>(J + 2 - 12 * L);
    y = static_cast<int>(100 * (N - 49) + I + L);
}

} // namespace

// Memo readers: rddads' adsGetValue routes HB_FT_MEMO fields through
// AdsGetMemoDataType + AdsGetMemoLength + AdsGetString. The first
// reports the memo's content type (text vs binary), the second the
// payload length, and the third copies the bytes into the caller's
// buffer. We resolve the field as before, fetch the memo block via
// the attached IMemoStore, and answer in kind.
UNSIGNED32 AdsGetMemoLength(ADSHANDLE hTable, UNSIGNED8* pucField,
                            UNSIGNED32* pulLen) {
    if (pulLen == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    if (auto* rt = get_remote_table(hTable)) {
        // Reuse the existing GetField wire op — the returned string
        // is the full memo content; size() is the memo length.
        std::string fname = openads::abi::to_internal(pucField, 0);
        auto v = rt->conn->get_field(rt->id, fname);
        if (!v) return fail(v.error());
        *pulLen = static_cast<UNSIGNED32>(v.value().size());
        return ok();
    }
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "");
    std::uint16_t idx = 0;
    if (!resolve_field_index(t, pucField, &idx)) {
        return fail(openads::AE_COLUMN_NOT_FOUND, "");
    }
    auto v = t->read_field(idx);
    if (!v) return fail(v.error());
    *pulLen = static_cast<UNSIGNED32>(v.value().as_string.size());
    return ok();
}

UNSIGNED32 AdsGetMemoDataType(ADSHANDLE hTable, UNSIGNED8* pucField,
                              UNSIGNED16* pusType) {
    Table* t = get_table(hTable);
    if (!t || pusType == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    std::uint16_t idx = 0;
    if (!resolve_field_index(t, pucField, &idx)) {
        return fail(openads::AE_COLUMN_NOT_FOUND, "");
    }
    auto r = t->field_memo_type(idx);
    if (!r) return fail(r.error());
    switch (r.value()) {
        case openads::drivers::MemoBlockType::Text:
            *pusType = static_cast<UNSIGNED16>(ADS_STRING);
            break;
        case openads::drivers::MemoBlockType::Picture:
            *pusType = static_cast<UNSIGNED16>(ADS_IMAGE);
            break;
        case openads::drivers::MemoBlockType::Object:
            *pusType = static_cast<UNSIGNED16>(ADS_BINARY);
            break;
    }
    return ok();
}

UNSIGNED32 AdsGetString(ADSHANDLE hTable, UNSIGNED8* pucField,
                        UNSIGNED8* pucBuf, UNSIGNED32* pulLen,
                        UNSIGNED16 usOption) {
    // Remote cursor OR a SQL backend (e.g. postgresql) that exposes a
    // get_field op: delegate through AdsGetField (which already routes the
    // remote row cache and the per-backend ops vtable) then apply the
    // ADS_TRIM trailing-space behaviour AdsGetString promises. Without this
    // a backend handle fell through to the native get_table path below and
    // AdsGetString returned an error / empty string for SQL backends.
    bool delegate = (get_remote_table(hTable) != nullptr);
    if (!delegate) {
        if (auto* ops = openads::abi::backend_table_ops_for(hTable))
            delegate = (ops->get_field != nullptr);
    }
    if (delegate) {
        UNSIGNED32 raw_len = (pulLen && *pulLen > 0) ? *pulLen : 65536;
        std::vector<UNSIGNED8> tmp(raw_len + 1, 0);
        if (AdsGetField(hTable, pucField, tmp.data(), &raw_len, usOption) != 0)
            return fail(openads::AE_COLUMN_NOT_FOUND, "");
        // Trim trailing spaces (ADS_TRIM behaviour) then copy to caller.
        std::string s(reinterpret_cast<char*>(tmp.data()), raw_len);
        auto last = s.find_last_not_of(' ');
        if (last != std::string::npos) s.erase(last + 1);
        else s.clear();
        UNSIGNED32 cap = pulLen ? *pulLen : 0;
        UNSIGNED32 n   = cap > 0 ? std::min<UNSIGNED32>(cap - 1,
                                    static_cast<UNSIGNED32>(s.size())) : 0;
        if (pucBuf != nullptr && cap > 0) {
            if (n > 0) std::memcpy(pucBuf, s.data(), n);
            pucBuf[n] = '\0';
        }
        if (pulLen) *pulLen = static_cast<UNSIGNED32>(s.size());
        return ok();
    }
    Table* t = get_table(hTable);
    if (!t || pulLen == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    std::uint16_t idx = 0;
    if (!resolve_field_index(t, pucField, &idx)) {
        return fail(openads::AE_COLUMN_NOT_FOUND, "");
    }
    auto v = t->read_field(idx);
    if (!v) return fail(v.error());
    const std::string& s = v.value().as_string;
    UNSIGNED32 cap = *pulLen;
    UNSIGNED32 n   = cap > 0 ? std::min<UNSIGNED32>(cap - 1,
                                static_cast<UNSIGNED32>(s.size()))
                             : 0;
    if (pucBuf != nullptr && cap > 0) {
        if (n > 0) std::memcpy(pucBuf, s.data(), n);
        pucBuf[n] = '\0';
    }
    *pulLen = static_cast<UNSIGNED32>(s.size());
    return ok();
}

// --- M9.17 Unicode (*W) variants ------------------------------------------
//
// rddads' ADS_LIB_VERSION >= 1000 path routes UNICODE-flagged columns
// through AdsSetStringW / AdsGetStringW / AdsGetFieldW with WCHAR*
// buffers (UTF-16LE on Windows). The engine stores byte sequences
// without a fixed codepage assumption, so the W variants transcode
// at the boundary: UTF-16LE -> UTF-8 on the way in, UTF-8 -> UTF-16LE
// on the way out. Field names are 7-bit ASCII inside the DBF, so the
// pucFieldW name is dropped to ASCII via the same converter.

namespace {

// Resolve an ASCII / numeric `pucField` to a 0-based field index
// for the W-variant entry points. SAP keeps field names ASCII
// (UNSIGNED8*) even on the W variants; only the value buffer is
// wide. Small pointer values are interpreted as a 1-based field
// number (rddads' ADSFIELD macro), otherwise the value is a NUL-
// terminated ASCII field name.
bool resolve_field_index_w(Table* tbl, UNSIGNED8* pucField,
                           std::uint16_t* out) {
    if (tbl == nullptr || out == nullptr) return false;
    auto p = reinterpret_cast<std::uintptr_t>(pucField);
    if (p != 0 && p < 0x10000u) {
        std::uint16_t one_based = static_cast<std::uint16_t>(p);
        if (one_based >= 1 && one_based <= tbl->field_count()) {
            *out = static_cast<std::uint16_t>(one_based - 1);
            return true;
        }
        return false;
    }
    if (pucField == nullptr) return false;
    auto name = openads::abi::to_internal(pucField, 0);
    // Delegate to Table::field_index — case-insensitive (matches native
    // ACE semantics) and cached. Field names in DBF/ADT storage are
    // upper-cased, but callers (and CDX/NTX index expressions) may use
    // any case; an exact-case compare here spuriously missed them.
    std::int32_t idx = tbl->field_index(name);
    if (idx < 0) return false;
    *out = static_cast<std::uint16_t>(idx);
    return true;
}

UNSIGNED32 emit_utf16(UNSIGNED16* pucBufW, UNSIGNED32* pulLenW,
                      const std::string& utf8) {
    if (pulLenW == nullptr) return openads::AE_INTERNAL_ERROR;
    auto units = openads::abi::utf8_to_utf16le(utf8);
    UNSIGNED32 cap = *pulLenW;
    UNSIGNED32 n   = cap > 0
        ? std::min<UNSIGNED32>(cap - 1,
                               static_cast<UNSIGNED32>(units.size()))
        : 0;
    if (pucBufW != nullptr && cap > 0) {
        if (n > 0) {
            std::memcpy(pucBufW, units.data(),
                        n * sizeof(std::uint16_t));
        }
        pucBufW[n] = 0;
    }
    *pulLenW = static_cast<UNSIGNED32>(units.size());
    return openads::AE_SUCCESS;
}

}  // namespace

UNSIGNED32 AdsSetStringW(ADSHANDLE hTable, UNSIGNED8* pucField,
                         UNSIGNED16* pucValueW, UNSIGNED32 ulLen) {
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    std::uint16_t idx = 0;
    if (!resolve_field_index_w(t, pucField, &idx)) {
        return fail(openads::AE_COLUMN_NOT_FOUND, "");
    }
    std::size_t units = ulLen;
    if (units == 0 && pucValueW != nullptr) {
        while (pucValueW[units] != 0) ++units;
    }
    std::string utf8 = openads::abi::utf16le_to_utf8(
        reinterpret_cast<const std::uint16_t*>(pucValueW), units);
    auto r = t->set_field(idx, utf8);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsGetStringW(ADSHANDLE hTable, UNSIGNED8* pucField,
                         UNSIGNED16* pucBufW, UNSIGNED32* pulLenW,
                         UNSIGNED16 /*usOption*/) {
    if (pulLenW == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    if (auto* _rt = get_remote_table(hTable); _rt != nullptr) {
        (void)_rt;
        UNSIGNED8 tmp[4096] = {0};
        UNSIGNED32 cap = sizeof(tmp);
        auto rc = AdsGetField(hTable, pucField, tmp, &cap, 0);
        if (rc != 0) return rc;
        return emit_utf16(pucBufW, pulLenW,
                          std::string(reinterpret_cast<char*>(tmp), cap));
    }
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "");
    std::uint16_t idx = 0;
    if (!resolve_field_index_w(t, pucField, &idx)) {
        return fail(openads::AE_COLUMN_NOT_FOUND, "");
    }
    auto v = t->read_field(idx);
    if (!v) return fail(v.error());
    return emit_utf16(pucBufW, pulLenW, v.value().as_string);
}

UNSIGNED32 AdsGetFieldW(ADSHANDLE hTable, UNSIGNED8* pucField,
                        UNSIGNED16* pucBufW, UNSIGNED32* pulLenW,
                        UNSIGNED16 /*usOption*/) {
    return AdsGetStringW(hTable, pucField, pucBufW, pulLenW, 0);
}

UNSIGNED32 AdsSetJulian(ADSHANDLE hTable, UNSIGNED8* pucField,
                        SIGNED32 lDate) {
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    std::uint16_t idx = 0;
    if (!resolve_field_index(t, pucField, &idx)) {
        return fail(openads::AE_COLUMN_NOT_FOUND, "");
    }
    char buf[9];
    if (lDate <= 0) {
        std::memset(buf, ' ', 8); buf[8] = '\0';
    } else {
        int y = 0, m = 0, d = 0;
        julian_to_ymd(lDate, y, m, d);
        if (y < 0) { y = 0; }
        if (y > 9999) { y = 9999; }
        m = ((m % 100) + 100) % 100;
        d = ((d % 100) + 100) % 100;
        std::snprintf(buf, sizeof(buf), "%04d%02d%02d", y, m, d);
    }
    std::string val(buf, 8);
    auto r = t->set_field(idx, val);
    if (!r) return fail(r.error());
    return ok();
}

// --- M9.18 lock retry policy ----------------------------------------------
//
// Real ACE exposes a per-connection (cycle_ms, retry_count) tuple that
// callers tune via AdsSetLockCycle / AdsSetLockRetryCount; AdsLockTable
// and AdsLockRecord then re-attempt a contended lock up to that limit
// before reporting AE_LOCK_FAILED. OpenADS keeps a process-global
// policy (the hConnect arg is accepted for ABI compat but the value is
// shared across connections in this build); the retry loop sleeps
// `cycle_ms` between attempts and gives up after `retry_count` cycles.

namespace {

struct LockPolicy {
    UNSIGNED32 cycle_ms    = 100;   // ACE default
    UNSIGNED16 retry_count = 10;
};

// extern "C++" silences clang's `-Wreturn-type-c-linkage` warning
// (returning an anonymous-namespace type from inside the surrounding
// extern "C" block isn't ABI-meaningful, but is harmless here since
// `lock_policy` is only called from C++ code in this TU).
extern "C++" LockPolicy& lock_policy() {
    static LockPolicy p;
    return p;
}

UNSIGNED32 lock_with_retry(std::function<openads::util::Result<void>()> fn) {
    LockPolicy p = lock_policy();
    for (UNSIGNED16 i = 0; ; ++i) {
        auto r = fn();
        if (r) return openads::AE_SUCCESS;
        if (i >= p.retry_count) return fail(r.error());
        if (p.cycle_ms > 0) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(p.cycle_ms));
        }
    }
}

}  // namespace

UNSIGNED32 AdsSetLockCycle(ADSHANDLE /*hConnect*/, UNSIGNED32 ulCycle) {
    auto& s = state();
    std::lock_guard<std::recursive_mutex> lk(s.mu);
    lock_policy().cycle_ms = ulCycle;
    return ok();
}

UNSIGNED32 AdsGetLockCycle(ADSHANDLE /*hConnect*/, UNSIGNED32* pulCycle) {
    if (pulCycle == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    auto& s = state();
    std::lock_guard<std::recursive_mutex> lk(s.mu);
    *pulCycle = lock_policy().cycle_ms;
    return ok();
}

UNSIGNED32 AdsSetLockRetryCount(ADSHANDLE /*hConnect*/, UNSIGNED16 usRetryCount) {
    auto& s = state();
    std::lock_guard<std::recursive_mutex> lk(s.mu);
    lock_policy().retry_count = usRetryCount;
    return ok();
}

UNSIGNED32 AdsGetLockRetryCount(ADSHANDLE /*hConnect*/, UNSIGNED16* pusRetryCount) {
    if (pusRetryCount == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    auto& s = state();
    std::lock_guard<std::recursive_mutex> lk(s.mu);
    *pusRetryCount = lock_policy().retry_count;
    return ok();
}

UNSIGNED32 AdsLockRecord(ADSHANDLE hTable, UNSIGNED32 ulRecord) {
    if (auto* rt = get_remote_table(hTable)) {
        auto r = rt->conn->lock_record(rt->id, ulRecord);
        if (!r) return fail(r.error());
        return ok();
    }
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    // ulRecord == 0 → the current record (ACE convention). Resolving it
    // also keeps the CDX record-lock byte (FILE_BASE - recno) clear of
    // the file/table lock byte (FILE_BASE) when recno would be 0.
    std::uint32_t rec = (ulRecord == 0) ? t->recno() : ulRecord;
    return lock_with_retry([t, rec]() {
        return t->try_lock_record_excl(rec);
    });
}

UNSIGNED32 AdsUnlockRecord(ADSHANDLE hTable, UNSIGNED32 ulRecord) {
    if (auto* rt = get_remote_table(hTable)) {
        auto r = rt->conn->unlock_record(rt->id, ulRecord);
        if (!r) return fail(r.error());
        return ok();
    }
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    std::uint32_t rec = (ulRecord == 0) ? t->recno() : ulRecord;
    auto r = t->unlock_record(rec);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsLockTable(ADSHANDLE hTable) {
    if (auto* rt = get_remote_table(hTable)) {
        auto r = rt->conn->lock_table(rt->id);
        if (!r) return fail(r.error());
        return ok();
    }
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    return lock_with_retry([t]() { return t->try_lock_table_excl(); });
}

UNSIGNED32 AdsUnlockTable(ADSHANDLE hTable) {
    if (auto* rt = get_remote_table(hTable)) {
        auto r = rt->conn->unlock_table(rt->id);
        if (!r) return fail(r.error());
        return ok();
    }
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    auto r = t->unlock_table();
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsFlushFileBuffers(ADSHANDLE hTable) {
    if (auto* rt = get_remote_table(hTable)) {
        auto r = rt->conn->flush_file_buffers(rt->id);
        if (!r) return fail(r.error());
        return ok();
    }
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    auto r = t->flush();
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsSetDeferredFlush(ADSHANDLE hTable, UNSIGNED16 usDeferred) {
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    t->set_deferred_flush(usDeferred != 0);
    return ok();
}

// --- M3 index / scope / seek -----------------------------------------------

extern "C++" {

namespace {

// Index handles: a single "logical" handle wraps the table-bound order
// since openads::engine::Table owns the active IIndex via Order. We keep a
// per-process map so the L1 thunks can resolve the table from the index
// handle.
// Binding for one open tag. Multi-tag CDX files create one binding
// per tag. At most ONE binding per table is "live" — its `idx` has
// been moved into Table::order_; the rest park their IIndex here so
// OrdSetFocus / AdsGetIndexHandle can swap them in on demand.
struct IndexBinding {
    Table*                                  table = nullptr;
    std::string                             tag_name;
    std::unique_ptr<openads::drivers::IIndex> parked;  // nullptr when this is the active binding
    std::string                             path;     // resolved index file path (M9.14)
};

std::unordered_map<ADSHANDLE, IndexBinding>& index_bindings() {
    static std::unordered_map<ADSHANDLE, IndexBinding> m;
    return m;
}

// Records, per table, which binding handle currently owns the active
// IIndex (i.e. the one moved into Table::order_).
std::unordered_map<Table*, ADSHANDLE>& active_binding_for() {
    static std::unordered_map<Table*, ADSHANDLE> m;
    return m;
}

// Drop every binding tied to `t`. Called from AdsCloseTable / AdsCloseAllTables
// / AdsDisconnect — without this, a Connection teardown leaves the bindings
// behind, so a later test (or app reconnect) that allocates a Table at the
// same heap slot inherits the stale entries and table_has_active misfires.
void purge_bindings_for_table(Table* t) {
    auto& m   = index_bindings();
    auto& act = active_binding_for();
    for (auto it = m.begin(); it != m.end(); ) {
        if (it->second.table == t) it = m.erase(it);
        else                       ++it;
    }
    act.erase(t);
}

Table* lookup_table_by_index(ADSHANDLE h) {
    auto& m = index_bindings();
    auto it = m.find(h);
    if (it == m.end()) return nullptr;
    return it->second.table;
}

openads::drivers::IIndex* iindex_for_handle(ADSHANDLE h) {
    auto& m = index_bindings();
    auto it = m.find(h);
    if (it == m.end()) return nullptr;
    if (it->second.parked) return it->second.parked.get();
    Table* t = it->second.table;
    if (t && t->order()) return t->order()->index();
    return nullptr;
}

// Make `h` the active order for its table. If another binding is
// currently active, park its IIndex back into that binding before
// stealing the requested one. No-op when `h` is already active.
openads::util::Result<void> activate_binding(ADSHANDLE h) {
    auto& m = index_bindings();
    auto it = m.find(h);
    if (it == m.end()) {
        return openads::util::Error{
            openads::AE_INTERNAL_ERROR, 0, "unknown index", ""};
    }
    Table* t = it->second.table;
    auto& act = active_binding_for();
    auto act_it = act.find(t);
    if (act_it != act.end() && act_it->second == h) return {};   // already live

    // Park the currently-active binding's index back into its slot
    // and register it as an extra view (so multi-index sync still
    // touches it after the swap). When act_it points to a handle
    // that's no longer in the binding map (stale entry left by a
    // previous AdsCloseAllIndexes / test cleanup that didn't tidy
    // act_), drop the act entry but leave Table::order_ alone — the
    // current code may have set it via the legacy AdsCreateIndex path
    // that doesn't populate `act_`.
    if (act_it != act.end()) {
        auto prev = m.find(act_it->second);
        if (prev != m.end()) {
            auto taken = t->take_order();
            openads::drivers::IIndex* raw = taken.get();
            prev->second.parked = std::move(taken);
            if (raw) t->register_extra_index_view(raw);
        }
        // else: stale act entry; leave Table::order_ untouched.
    }

    // Move the parked IIndex from this binding into the table; drop
    // its extra-view entry since the active order's IIndex is already
    // walked by the sync loop.
    if (it->second.parked) {
        openads::drivers::IIndex* raw = it->second.parked.get();
        t->unregister_extra_index_view(raw);
        t->set_order(std::move(it->second.parked));
    }
    act[t] = h;
    return {};
}

ADSHANDLE next_index_handle() {
    static std::uint64_t n = 0x40000000ULL;  // disjoint from table handles
    return ++n;
}

Table* table_for_index(ADSHANDLE hIndex) {
    auto it = index_bindings().find(hIndex);
    if (it == index_bindings().end()) return nullptr;
    // Activate this binding so the Table's order_ reflects the
    // requested index — AdsSeek / AdsGotoTop / etc. always operate
    // through the Table's active order, and rddads passes the index
    // handle (pArea->hOrdCurrent) as the operand.
    (void)activate_binding(hIndex);
    return it->second.table;
}

// Mark a freshly opened CDX index FoxNumeric when its key is a bare
// numeric field, so seek/append on a reopened numeric index builds the
// same 8-byte order-preserving key the file was written with. No-op for
// character keys / non-CDX drivers.
void mark_cdx_key_encoding(Table* t, openads::drivers::IIndex* idx) {
    if (t == nullptr || idx == nullptr) return;
    const std::string bare =
        openads::engine::strip_alias_qualifiers(idx->expression());
    std::int32_t fi = t->field_index(bare);
    if (fi < 0) return;
    using FT = openads::drivers::DbfFieldType;
    FT ft = t->field_descriptor(static_cast<std::uint16_t>(fi)).type;
    if (ft == FT::Numeric || ft == FT::Float || ft == FT::Integer ||
        ft == FT::Double  || ft == FT::Currency || ft == FT::AdtMoney) {
        idx->set_key_encoding(openads::drivers::KeyEncoding::FoxNumeric);
    }
}

// Re-mark a reopened NTX index NtxNumeric so a later append writes the native
// zero-padded numeric key. The create path marks it via set_numeric_format; a
// reopen through AdsOpenIndex must restore the flag (open() already reads the
// width + decimal count back from the NTX header). No-op for character keys.
void mark_ntx_key_encoding(Table* t, openads::drivers::IIndex* idx) {
    if (t == nullptr || idx == nullptr) return;
    const std::string bare =
        openads::engine::strip_alias_qualifiers(idx->expression());
    std::int32_t fi = t->field_index(bare);
    if (fi < 0) return;
    using FT = openads::drivers::DbfFieldType;
    FT ft = t->field_descriptor(static_cast<std::uint16_t>(fi)).type;
    if (ft == FT::Numeric || ft == FT::Float) {
        idx->set_key_encoding(openads::drivers::KeyEncoding::NtxNumeric);
    }
}

bool path_ends_with_ci(const std::string& s, const char* suffix) {
    auto n = std::strlen(suffix);
    if (s.size() < n) return false;
    for (std::size_t i = 0; i < n; ++i) {
        char a = static_cast<char>(std::tolower(
            static_cast<unsigned char>(s[s.size() - n + i])));
        char b = static_cast<char>(std::tolower(
            static_cast<unsigned char>(suffix[i])));
        if (a != b) return false;
    }
    return true;
}

std::unique_ptr<openads::drivers::IIndex>
make_index_for(const std::string& path) {
    if (path_ends_with_ci(path, ".cdx")) {
        return std::make_unique<openads::drivers::cdx::CdxIndex>();
    }
    if (path_ends_with_ci(path, ".adi")) {
        return std::make_unique<openads::drivers::adi::AdiIndex>();
    }
    return std::make_unique<openads::drivers::ntx::NtxIndex>();
}

} // namespace

} // extern "C++"

// Compare two filesystem paths for the "is this the same on-disk
// file?" question. Falls back to a case-insensitive lexical compare
// when canonical resolution fails (e.g. file doesn't exist yet).
namespace {
bool same_index_path(const std::string& a, const std::string& b) {
    namespace fs = std::filesystem;
    std::error_code ec;
    auto ca = fs::weakly_canonical(fs::path(a), ec);
    auto cb = fs::weakly_canonical(fs::path(b), ec);
    auto sa = ec ? a : ca.string();
    auto sb = ec ? b : cb.string();
    if (sa.size() != sb.size()) {
        if (a.size() != b.size()) return false;
        for (std::size_t i = 0; i < a.size(); ++i) {
            char ca2 = static_cast<char>(std::tolower(
                static_cast<unsigned char>(a[i])));
            char cb2 = static_cast<char>(std::tolower(
                static_cast<unsigned char>(b[i])));
            if (ca2 != cb2) return false;
        }
        return true;
    }
    for (std::size_t i = 0; i < sa.size(); ++i) {
        char ca2 = static_cast<char>(std::tolower(
            static_cast<unsigned char>(sa[i])));
        char cb2 = static_cast<char>(std::tolower(
            static_cast<unsigned char>(sb[i])));
        if (ca2 != cb2) return false;
    }
    return true;
}
}  // namespace

// Real-ACE 4-arg signature: opens an index FILE, registers one handle
// per tag, and writes the handles into ahIndex[] / *pu16ArrayLen.
//
// M9.14 made this additive: a second AdsOpenIndex against a different
// file path no longer wipes the prior bindings. Instead, the new
// indices land as parked extra views (their writes still sync) and
// the first one only steals Table::order_ when no active order is
// currently bound. Repeated calls with the SAME path drop the prior
// bindings for that path (refresh semantics) so reopening the same
// .ntx / .cdx leaves at most one binding per tag.
UNSIGNED32 AdsOpenIndex(ADSHANDLE hTable, UNSIGNED8* pucName,
                        ADSHANDLE* ahIndex, UNSIGNED16* pu16ArrayLen) {
    if (ahIndex == nullptr) {
        return fail(openads::AE_INTERNAL_ERROR, "null out");
    }
    if (auto* rt = get_remote_table(hTable)) {
        std::string path = openads::abi::to_internal(pucName, 0);
        auto r = rt->conn->open_index(rt->id, path);
        if (!r) return fail(r.error());
        auto& s = state();
        std::lock_guard<std::recursive_mutex> lk(s.mu);
        // Persist RemoteIndex objects in a side container keyed by
        // their ADSHANDLE; the registry only stores raw pointers.
        static std::unordered_map<Handle,
            std::unique_ptr<openads::network::RemoteIndex>> remote_indexes;
        const auto& ids = r.value();
        std::uint16_t out_n = static_cast<std::uint16_t>(ids.size());
        if (pu16ArrayLen != nullptr && *pu16ArrayLen < out_n) {
            out_n = *pu16ArrayLen;
        }
        for (std::uint16_t i = 0; i < out_n; ++i) {
            auto ri = std::make_unique<openads::network::RemoteIndex>();
            ri->conn   = rt->conn;
            ri->id     = ids[i];
            ri->tbl_id = rt->id;
            ri->parent = rt;
            Handle gh = s.registry.register_object(
                HandleKind::RemoteIndex, ri.get());
            ahIndex[i] = gh;
            remote_indexes.emplace(gh, std::move(ri));
        }
        if (pu16ArrayLen != nullptr) *pu16ArrayLen = out_n;
        return ok();
    }
    if (auto* ops = openads::abi::backend_table_ops_for(hTable))
        if (ops->open_index) return ops->open_index(hTable, pucName, ahIndex, pu16ArrayLen);
    Table* t = get_table(hTable);
    if (!t) {
        return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    }
    auto bag_name = openads::abi::to_internal(pucName, 0);
    namespace fs = std::filesystem;
    fs::path p(bag_name);
    if (!p.is_absolute()) {
        fs::path table_dir = fs::path(t->path()).parent_path();
        // ADS places index files next to the table; when the caller uses a
        // subdirectory-qualified path (e.g. "sub/table.adx") and the table's
        // parent is already "sub/", avoid double-prefix by falling back to
        // basename.  Example: table opened as "sub/t.adt" makes table_dir =
        // ".../sub"; if the caller also passes "sub/t.adx" the naive join
        // ".../sub/sub/t.adx" does not exist, so retry with just the
        // filename ".../sub/t.adx".  A single-component relative path
        // (p.filename() == p) takes the same first branch and is unaffected.
        fs::path candidate = table_dir / p;
        if (!fs::exists(candidate)) {
            fs::path by_name = table_dir / p.filename();
            if (fs::exists(by_name)) {
                p = by_name;
            } else {
                p = candidate;  // preserve original path for the real "not found" error
            }
        } else {
            p = candidate;
        }
    }
    if (!p.has_extension()) {
        p.replace_extension(".cdx");
    }
    auto path = p.string();

    auto& m   = index_bindings();
    auto& act = active_binding_for();

    // Refresh: drop any prior bindings for this Table that came from
    // the same file path. If the active binding was among them, also
    // surrender Table::order_; the caller's reopen will repopulate it.
    bool active_dropped = false;
    auto act_it = act.find(t);
    ADSHANDLE active_h = act_it != act.end() ? act_it->second : 0;
    for (auto it = m.begin(); it != m.end(); ) {
        if (it->second.table == t && same_index_path(it->second.path, path)) {
            if (it->first == active_h) {
                active_dropped = true;
            } else if (it->second.parked) {
                t->unregister_extra_index_view(it->second.parked.get());
            }
            it = m.erase(it);
        } else {
            ++it;
        }
    }
    if (active_dropped) {
        act.erase(t);
        t->clear_order();
    }
    bool table_has_active = act.find(t) != act.end();

    // Enumerate tags. CDX/ADI expose list_tags; NTX has only its single
    // tag, which open() reports via name().
    std::vector<std::string> tags;
    bool is_adi = path_ends_with_ci(path, ".adi");
    if (path_ends_with_ci(path, ".cdx")) {
        auto r = openads::drivers::cdx::CdxIndex::list_tags(path);
        if (!r) return fail(r.error());
        tags = std::move(r).value();
    } else if (is_adi) {
        auto r = openads::drivers::adi::AdiIndex::list_tags(path);
        if (!r) return fail(r.error());
        tags = std::move(r).value();
    }
    if (tags.empty()) {
        // NTX or empty CDX: open once via the legacy path. M9.14 lets
        // multiple NTX files coexist on the same Table — when the
        // table already has an active order, the new NTX parks as an
        // extra view instead of replacing it.
        auto idx = make_index_for(path);
        if (auto r = idx->open(path, openads::drivers::IndexOpenMode::Shared); !r) {
            return fail(r.error());
        }
        std::string tag_name = idx->name();
        if (path_ends_with_ci(path, ".ntx"))
            mark_ntx_key_encoding(t, idx.get());
        ADSHANDLE h = next_index_handle();
        if (!table_has_active) {
            t->set_order(std::move(idx));
            m[h] = IndexBinding{t, tag_name, nullptr, path};
            act[t] = h;
        } else {
            openads::drivers::IIndex* raw = idx.get();
            m[h] = IndexBinding{t, tag_name, std::move(idx), path};
            t->register_extra_index_view(raw);
        }
        ahIndex[0] = h;
        if (pu16ArrayLen != nullptr) *pu16ArrayLen = 1;
        return ok();
    }

    // CDX / ADI with one or more tags: open each by name. The first tag's
    // IIndex moves into Table::order_ (becomes default order) only
    // when the table doesn't already have an active order; the rest
    // (and the first tag in the additive case) park as extra views.
    UNSIGNED16 cap = (pu16ArrayLen != nullptr && *pu16ArrayLen > 0)
                   ? *pu16ArrayLen : 1;
    UNSIGNED16 count = 0;
    for (const auto& name : tags) {
        if (count >= cap) break;
        std::unique_ptr<openads::drivers::IIndex> sub;
        if (is_adi) {
            auto idx = std::make_unique<openads::drivers::adi::AdiIndex>();
            if (auto r = idx->open_named(path,
                              openads::drivers::IndexOpenMode::Shared,
                              name); !r) {
                return fail(r.error());
            }
            sub = std::move(idx);
        } else {
            auto idx = std::make_unique<openads::drivers::cdx::CdxIndex>();
            if (auto r = idx->open_named(path,
                              openads::drivers::IndexOpenMode::Shared,
                              name); !r) {
                return fail(r.error());
            }
            mark_cdx_key_encoding(t, idx.get());
            sub = std::move(idx);
        }
        ADSHANDLE h = next_index_handle();
        if (!table_has_active) {
            t->set_order(std::move(sub));
            m[h] = IndexBinding{t, name, nullptr, path};
            act[t] = h;
            table_has_active = true;
        } else {
            openads::drivers::IIndex* raw = sub.get();
            m[h] = IndexBinding{t, name, std::move(sub), path};
            t->register_extra_index_view(raw);
        }
        ahIndex[count++] = h;
    }
    if (pu16ArrayLen != nullptr) *pu16ArrayLen = count;
    return ok();
}

UNSIGNED32 AdsCloseIndex(ADSHANDLE hIndex) {
#if defined(OPENADS_WITH_SQLITE)
    if (auto* si = get_sqlite_index(hIndex)) {
        (void)si;
        auto& s = state();
        std::lock_guard<std::recursive_mutex> lk(s.mu);
        sqlite_indexes_map().erase(hIndex);
        s.registry.release(hIndex);
        return ok();
    }
#endif
#if defined(OPENADS_WITH_ODBC)
    if (auto* si = get_odbc_index(hIndex)) {
        (void)si;
        auto& s = state();
        std::lock_guard<std::recursive_mutex> lk(s.mu);
        odbc_indexes_map().erase(hIndex);
        s.registry.release(hIndex);
        return ok();
    }
#endif
#if defined(OPENADS_WITH_FIREBIRD)
    if (auto* si = get_firebird_index(hIndex)) {
        (void)si;
        auto& s = state();
        std::lock_guard<std::recursive_mutex> lk(s.mu);
        firebird_indexes_map().erase(hIndex);
        s.registry.release(hIndex);
        return ok();
    }
#endif
#if defined(OPENADS_WITH_MARIADB)
    if (auto* si = get_maria_index(hIndex)) {
        (void)si;
        auto& s = state();
        std::lock_guard<std::recursive_mutex> lk(s.mu);
        maria_indexes_map().erase(hIndex);
        s.registry.release(hIndex);
        return ok();
    }
#endif
#if defined(OPENADS_WITH_POSTGRESQL)
    if (auto* si = get_postgres_index(hIndex)) {
        (void)si;
        auto& s = state();
        std::lock_guard<std::recursive_mutex> lk(s.mu);
        postgres_indexes_map().erase(hIndex);
        s.registry.release(hIndex);
        return ok();
    }
#endif
    if (auto* ri = get_remote_index(hIndex)) {
        auto r = ri->conn->close_index(ri->id);
        if (!r) return fail(r.error());
        return ok();
    }
    auto& m = index_bindings();
    auto& act = active_binding_for();
    auto it = m.find(hIndex);
    if (it == m.end()) return fail(openads::AE_INTERNAL_ERROR, "unknown index");
    Table* t = it->second.table;
    if (t != nullptr) {
        auto act_it = act.find(t);
        if (act_it != act.end() && act_it->second == hIndex) {
            t->clear_order();
            act.erase(act_it);
        } else if (it->second.parked) {
            t->unregister_extra_index_view(it->second.parked.get());
        }
    }
    m.erase(it);
    return ok();
}

UNSIGNED32 AdsCloseAllIndexes(ADSHANDLE hTable) {
    if (auto* rt = get_remote_table(hTable)) {
        auto r = rt->conn->close_all_indexes(rt->id);
        if (!r) return fail(r.error());
        return ok();
    }
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    auto& m = index_bindings();
    auto& act = active_binding_for();
    for (auto it = m.begin(); it != m.end(); ) {
        if (it->second.table == t) {
            if (it->second.parked) {
                t->unregister_extra_index_view(it->second.parked.get());
            }
            it = m.erase(it);
        } else {
            ++it;
        }
    }
    act.erase(t);
    t->clear_order();
    t->clear_extra_index_views();
    return ok();
}

UNSIGNED32 AdsCreateIndex61(ADSHANDLE   hTable,
                            UNSIGNED8*  pucFileName,
                            UNSIGNED8*  pucIndexName,
                            UNSIGNED8*  pucExpr,
                            UNSIGNED8*  pucCondition,
                            UNSIGNED8*  pucKeyFilter,
                            UNSIGNED32  ulOptions,
                            UNSIGNED16  usPageSize,
                            ADSHANDLE*  phIndex) {
    if (phIndex == nullptr || pucFileName == nullptr ||
        pucIndexName == nullptr || pucExpr == nullptr) {
        return fail(openads::AE_INTERNAL_ERROR, "null arg");
    }
#if defined(OPENADS_WITH_ODBC)
    if (auto* st = get_odbc_table(hTable)) {
        auto expr = openads::abi::to_internal(pucExpr, 0);
        auto parsed = openads::sql_backend::parse_index_expr(expr);
        if (!parsed) return fail(parsed.error());
        const auto& px = parsed.value();
        auto si = std::make_unique<openads::sql_backend::OdbcIndex>();
        si->parent    = st;
        si->column    = px.column;
        si->expr_kind = px.kind;
        auto& s = state();
        std::lock_guard<std::recursive_mutex> lk(s.mu);
        Handle gh = s.registry.register_object(
            HandleKind::OdbcIndex, si.get());
        *phIndex = gh;
        odbc_indexes_map().emplace(gh, std::move(si));
        (void)pucFileName;
        (void)pucIndexName;
        (void)pucCondition;
        (void)pucKeyFilter;
        (void)ulOptions;
        (void)usPageSize;
        return ok();
    }
#endif
#if defined(OPENADS_WITH_FIREBIRD)
    if (auto* st = get_firebird_table(hTable)) {
        auto expr = openads::abi::to_internal(pucExpr, 0);
        auto parsed = openads::sql_backend::parse_index_expr(expr);
        if (!parsed) return fail(parsed.error());
        const auto& px = parsed.value();
        auto si = std::make_unique<openads::sql_backend::FirebirdIndex>();
        si->parent    = st;
        si->column    = px.column;
        si->expr_kind = px.kind;
        auto& s = state();
        std::lock_guard<std::recursive_mutex> lk(s.mu);
        Handle gh = s.registry.register_object(
            HandleKind::FirebirdIndex, si.get());
        *phIndex = gh;
        firebird_indexes_map().emplace(gh, std::move(si));
        (void)pucFileName;
        (void)pucIndexName;
        (void)pucCondition;
        (void)pucKeyFilter;
        (void)ulOptions;
        (void)usPageSize;
        return ok();
    }
#endif
    if (auto* rt = get_remote_table(hTable)) {
        std::string path = openads::abi::to_internal(pucFileName, 0);
        std::string tag  = openads::abi::to_internal(pucIndexName, 0);
        std::string expr = openads::abi::to_internal(pucExpr, 0);
        std::string cond = pucCondition
            ? openads::abi::to_internal(pucCondition, 0) : std::string();
        std::string kf   = pucKeyFilter
            ? openads::abi::to_internal(pucKeyFilter, 0) : std::string();
        auto r = rt->conn->create_index(rt->id, path, tag, expr,
                                         cond, kf,
                                         ulOptions, usPageSize);
        if (!r) return fail(r.error());
        auto& s = state();
        std::lock_guard<std::recursive_mutex> lk(s.mu);
        static std::unordered_map<Handle,
            std::unique_ptr<openads::network::RemoteIndex>> remote_indexes;
        auto ri = std::make_unique<openads::network::RemoteIndex>();
        ri->conn   = rt->conn;
        ri->id     = r.value();
        ri->tbl_id = rt->id;
        ri->parent = rt;
        Handle gh = s.registry.register_object(
            HandleKind::RemoteIndex, ri.get());
        *phIndex = gh;
        remote_indexes.emplace(gh, std::move(ri));
        return ok();
    }
    Table* t = get_table(hTable);
    if (!t) {
        return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    }
    (void)pucKeyFilter; (void)usPageSize;
    auto bag  = openads::abi::to_internal(pucFileName, 0);
    auto tag  = openads::abi::to_internal(pucIndexName, 0);
    auto expr = openads::abi::to_internal(pucExpr, 0);
    std::string for_expr = pucCondition != nullptr
        ? openads::abi::to_internal(pucCondition, 0)
        : std::string{};

    namespace fs = std::filesystem;
    const bool is_adt_table = path_ends_with_ci(t->path(), ".adt");
    const char* default_ext = is_adt_table ? ".adi" : ".cdx";
    fs::path p;
    if (bag.empty()) {
        // No bag name supplied → structural index bag: same stem as the
        // table (.cdx for DBF, .adi for ADT).  Mirrors AdsOpenTable90.
        p = fs::path(t->path()).replace_extension(default_ext);
    } else {
        p = fs::path(bag);
        if (!p.is_absolute()) {
            fs::path tdir = fs::path(t->path()).parent_path();
            p = tdir / p;
        }
        if (!p.has_extension()) p.replace_extension(default_ext);
    }
    bool is_cdx = path_ends_with_ci(p.string(), ".cdx");
    bool is_adi = path_ends_with_ci(p.string(), ".adi");

    // ACE AdsCreateIndex* option bits. include/openads/ace.h carries the
    // SDK-standard values (ADS_UNIQUE 0x01, ADS_COMPOUND 0x02, ADS_CUSTOM
    // 0x04, ADS_DESCENDING 0x08) — but the two RDD clients we interop with
    // put the "compound" and "descending" flags on SWAPPED bits, measured
    // by instrumenting this function:
    //
    //   client          ascending tag   descending tag
    //   X#  ADSRDD       0x02            0x0A   (compound 0x02 | descending 0x08)
    //   Harbour rddads   0x08            0x0A   (compound 0x08 | descending 0x02)
    //
    // Each client always sets ITS compound bit on EVERY tag (cdx and ntx),
    // and adds the OTHER bit of the {0x02,0x08} pair to mean descending. So a
    // lone 0x02 OR a lone 0x08 is ascending (it is just that client's
    // "compound" marker), and "descending" is the one case where BOTH bits
    // are set (0x0A). Reading a lone 0x08 (or 0x02) as descending built every
    // Harbour (resp. X#) order reversed — AdsGotoTop landing on the last key,
    // SKIP walking backward. The internal SQL CREATE INDEX path (below) emits
    // 0x0A for a descending tag so it round-trips through this same decode.
    const bool opt_compound_bit   = (ulOptions & ADS_COMPOUND) != 0;   // 0x02
    const bool opt_descending_bit = (ulOptions & ADS_DESCENDING) != 0; // 0x08
    bool unique  = (ulOptions & ADS_UNIQUE) != 0;
    bool descend = opt_compound_bit && opt_descending_bit;

    // Validate the key expression: a bare identifier that is not a column
    // is a bug in the caller's PRG (typo / renamed field). Native
    // Harbour/Clipper raises "Variable does not exist" at INDEX time;
    // silently accepting it builds an all-blank, useless index and hides
    // the mistake. Mirror evaluate_index_expr()'s bare-field detection
    // (single alnum/underscore token) so computed expressions still pass.
    {
        std::string ident = expr;
        while (!ident.empty() &&
               std::isspace(static_cast<unsigned char>(ident.front())))
            ident.erase(ident.begin());
        while (!ident.empty() &&
               std::isspace(static_cast<unsigned char>(ident.back())))
            ident.pop_back();
        const bool bare_ident = !ident.empty() &&
            std::all_of(ident.begin(), ident.end(), [](char c) {
                return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
            }) &&
            !std::isdigit(static_cast<unsigned char>(ident.front()));
        if (bare_ident && t->field_index(ident) < 0) {
            return fail(openads::util::Error{
                openads::AE_COLUMN_NOT_FOUND, 0,
                "index expression references unknown column", ident});
        }
    }

    // CDX numeric keys: FoxPro/Harbour store a bare numeric field as an
    // 8-byte order-preserving binary key (keySize 8), not text. Detect a
    // bare numeric field so the index is created with keySize=8 and marked
    // FoxNumeric; the engine then emits the binary key at write time.
    // STR()/character expressions stay text. (Date deferred — DTOS-style
    // text indexes already interop.)
    bool cdx_numeric_key = false;
    if (is_cdx) {
        const std::string bare = openads::engine::strip_alias_qualifiers(expr);
        std::int32_t fi = t->field_index(bare);
        if (fi >= 0) {
            using FT = openads::drivers::DbfFieldType;
            FT ft = t->field_descriptor(static_cast<std::uint16_t>(fi)).type;
            cdx_numeric_key =
                ft == FT::Numeric || ft == FT::Float   || ft == FT::Integer ||
                ft == FT::Double  || ft == FT::Currency || ft == FT::AdtMoney;
        }
    }

    // NTX numeric keys: a native xBase NTX stores a numeric field's key as
    // fixed-width, right-justified ASCII = STR(value, fieldLen, fieldDec),
    // and records the decimal count in the index header. The key stays
    // TEXT (unlike the compound-index 8-byte binary form), but the width
    // and decimals MUST come from the field descriptor, not from a probed
    // key length (which is wrong/empty on an empty table). Detect a bare
    // ASCII-stored numeric field (N / F) so the index width is pinned to
    // the field length and the decimals land in the header. VFP binary
    // numerics (I/B/Y) are not ASCII on disk — left for a follow-up.
    bool          ntx_numeric_key  = false;
    std::uint16_t ntx_num_width     = 0;
    std::uint16_t ntx_num_dec       = 0;
    if (!is_cdx && !is_adi) {
        const std::string bare = openads::engine::strip_alias_qualifiers(expr);
        std::int32_t fi = t->field_index(bare);
        if (fi >= 0) {
            using FT = openads::drivers::DbfFieldType;
            const auto& fd =
                t->field_descriptor(static_cast<std::uint16_t>(fi));
            if ((fd.type == FT::Numeric || fd.type == FT::Float) &&
                fd.length > 0) {
                ntx_numeric_key = true;
                ntx_num_width   = fd.length;
                ntx_num_dec     = static_cast<std::uint16_t>(fd.decimals);
            }
        }
    }

    // Determine the on-disk key length. Numeric CDX keys use the 8-byte
    // binary width; numeric NTX keys use the field's own width so the key
    // matches the native reader.
    std::uint16_t klen = cdx_numeric_key ? 8
                       : ntx_numeric_key ? ntx_num_width
                       : 0;
    if (!cdx_numeric_key && !ntx_numeric_key) {
        // A character key is fixed-width on disk. For a BARE character field
        // the width is the declared field length: deriving it from the
        // *trimmed* value of the first record truncates every later key that
        // shares a prefix beyond that width (a short first row collapses
        // longer rows onto the same key), corrupting ordering and seeks in
        // both the index itself and native FoxPro/Clipper readers.
        const std::string bare = openads::engine::strip_alias_qualifiers(expr);
        std::int32_t fi = t->field_index(bare);
        if (fi >= 0) {
            klen = t->field_descriptor(static_cast<std::uint16_t>(fi)).length;
        } else if (t->record_count() > 0) {
            // Composite expression: probe the first record's key. Note
            // evaluate_index_expr right-pads its result to the probe width, so
            // trim the padding back off — otherwise every composite key would
            // be pinned to the 254-byte probe length.
            if (auto g = t->goto_record(1); g) {
                if (auto k = openads::engine::evaluate_index_expr(*t, expr, 254)) {
                    std::string s = std::move(k).value();
                    while (!s.empty() && s.back() == ' ') s.pop_back();
                    if (!s.empty())
                        klen = static_cast<std::uint16_t>(
                            std::min<std::size_t>(s.size(), 254));
                }
            }
        }
        if (klen == 0) klen = 32;  // empty table, composite expression
    }

    std::unique_ptr<openads::drivers::IIndex> idx_owner;
    bool exists = false;
    {
        std::error_code ec;
        exists = (is_cdx || is_adi) && fs::exists(p, ec);
    }

    if (is_adi && is_adt_table) {
        // ADT tables use a single .adi bag; each tag indexes one field.
        const std::string bare = openads::engine::strip_alias_qualifiers(expr);
        std::int32_t fidx = t->field_index(bare);
        if (fidx < 0) {
            return fail(openads::AE_COLUMN_NOT_FOUND,
                        "ADI index expression must be a bare field name");
        }
        if (fidx + 1 > 255) {
            return fail(openads::AE_INTERNAL_ERROR,
                        "ADI index does not support field numbers greater than 255");
        }
        const auto& fd = t->field_descriptor(static_cast<std::uint16_t>(fidx));
        openads::drivers::adi::AdiIndex::CreateParams cp{};
        cp.field_num   = static_cast<std::uint8_t>(fidx + 1);
        cp.field_name  = fd.name;
        cp.adt_type    = static_cast<std::uint16_t>(
            static_cast<unsigned char>(fd.raw_type));
        cp.fld_length  = fd.length;
        cp.adt_hdr_len = t->driver()->header_length();
        cp.adt_rec_len = t->driver()->record_length();
        cp.unique      = unique;

        const bool is_char_key =
            cp.adt_type == openads::drivers::adi::ADT_TYPE_CHAR ||
            cp.adt_type == openads::drivers::adi::ADT_TYPE_CICHAR;
        klen = is_char_key ? fd.length : 8;

        if (exists) {
            auto tags = openads::drivers::adi::AdiIndex::list_tags(p.string());
            bool have_tag = false;
            if (tags) {
                for (const auto& tn : tags.value()) {
                    if (tn.size() == fd.name.size()) {
                        bool eq = true;
                        for (std::size_t i = 0; i < tn.size(); ++i) {
                            if (std::tolower(static_cast<unsigned char>(tn[i])) !=
                                std::tolower(static_cast<unsigned char>(
                                    fd.name[i]))) {
                                eq = false;
                                break;
                            }
                        }
                        if (eq) { have_tag = true; break; }
                    }
                }
            }
            if (have_tag) {
                openads::drivers::adi::AdiIndex existing;
                auto reopen = existing.open_named(
                    p.string(), openads::drivers::IndexOpenMode::Shared,
                    fd.name);
                if (!reopen) return fail(reopen.error());
                if (auto cl = existing.clear_data(); !cl) return fail(cl.error());
                idx_owner = std::make_unique<
                    openads::drivers::adi::AdiIndex>(std::move(existing));
            } else {
                auto added = openads::drivers::adi::AdiIndex::add_tag(
                    p.string(), cp);
                if (!added) return fail(added.error());
                idx_owner = std::make_unique<
                    openads::drivers::adi::AdiIndex>(std::move(added).value());
            }
        } else {
            auto created = openads::drivers::adi::AdiIndex::create(
                p.string(), cp);
            if (!created) return fail(created.error());
            idx_owner = std::make_unique<openads::drivers::adi::AdiIndex>(
                std::move(created).value());
        }
    } else if (is_cdx && exists) {
        // Harbour rddads / Clipper semantics: re-creating an
        // existing tag is a silent overwrite, not an error. If the
        // tag already exists, open it and clear its B+tree so the
        // caller's per-record insert loop rebuilds it fresh.
        auto added = openads::drivers::cdx::CdxIndex::add_tag(
            p.string(), tag, expr, klen, unique, descend, for_expr);
        if (!added && added.error().code == 5044) {
            openads::drivers::cdx::CdxIndex existing;
            auto reopen = existing.open_named(p.string(),
                openads::drivers::IndexOpenMode::Shared, tag);
            if (!reopen) return fail(reopen.error());
            // Wipe the old B+tree before the per-record insert
            // loop rebuilds it; otherwise duplicates from the
            // prior CREATE INDEX accumulate and break SKIP walks.
            if (auto cl = existing.clear_data(); !cl)
                return fail(cl.error());
            // CREATE INDEX overwrite must also pick up the new
            // UNIQUE / DESCEND options + the freshly-probed key
            // size; the sub-header was loaded from disk with the
            // prior options.
            if (auto so = existing.set_options(unique, descend, klen); !so)
                return fail(so.error());
            // Re-creating an existing tag overwrites its FOR clause too
            // (a new condition, or none, replaces the old one) so the
            // on-disk header matches the just-issued CREATE INDEX.
            if (auto sc = existing.set_condition(for_expr); !sc)
                return fail(sc.error());
            idx_owner = std::make_unique<openads::drivers::cdx::CdxIndex>(
                std::move(existing));
        } else if (!added) {
            return fail(added.error());
        } else {
            idx_owner = std::make_unique<openads::drivers::cdx::CdxIndex>(
                std::move(added).value());
        }
    } else if (is_cdx) {
        auto created = openads::drivers::cdx::CdxIndex::create(
            p.string(), tag, expr, klen, unique, descend, for_expr);
        if (!created) return fail(created.error());
        idx_owner = std::make_unique<openads::drivers::cdx::CdxIndex>(
            std::move(created).value());
    } else if (is_adi) {
        return fail(openads::AE_INTERNAL_ERROR,
                    "ADI index bag requires an ADT table");
    } else {
        auto created = openads::drivers::ntx::NtxIndex::create(
            p.string(), tag, expr, klen, unique, descend);
        if (!created) return fail(created.error());
        auto ntx_owner = std::make_unique<openads::drivers::ntx::NtxIndex>(
            std::move(created).value());
        // Pin the key geometry to the numeric field descriptor so the
        // on-disk key is the native fixed-width STR(value,width,dec) form
        // (and the header carries the decimal count) — independent of any
        // probed key length, which is absent on an empty table.
        if (ntx_numeric_key) {
            if (auto sf = ntx_owner->set_numeric_format(
                    ntx_num_width, ntx_num_dec); !sf) {
                return fail(sf.error());
            }
        }
        idx_owner = std::move(ntx_owner);
    }
    // Mark a numeric CDX index FoxNumeric so every key-build path (this
    // create loop, the engine's sync_all_indexes_, seek) emits the 8-byte
    // binary key a native reader expects.
    if (cdx_numeric_key && idx_owner) {
        idx_owner->set_key_encoding(openads::drivers::KeyEncoding::FoxNumeric);
    }
    auto rec_count = t->record_count();
    // Fast path: for CDX, collect the whole key stream and bulk-load the
    // B+tree in one bottom-up pass (sort -> pack leaves -> branch levels)
    // instead of record-by-record top-down insertion. Each page is encoded
    // once rather than decoded+re-encoded on every key, ~10x faster on a
    // full CREATE INDEX / REINDEX. NTX keeps the incremental path.
    openads::drivers::cdx::CdxIndex* cdx_bulk =
        is_cdx ? static_cast<openads::drivers::cdx::CdxIndex*>(idx_owner.get())
               : nullptr;
    std::vector<std::pair<std::string, std::uint32_t>> bulk_keys;
    if (cdx_bulk) bulk_keys.reserve(rec_count);
    for (std::uint32_t r = 1; r <= rec_count; ++r) {
        if (auto g = t->goto_record(r); !g) return fail(g.error());
        // DBFCDX inserts deleted rows too — the index is a logical
        // mirror of the table, not a "live-only" view. SET DELETED
        // hides them at navigation time. Only the FOR clause filters
        // entries out at build time.
        if (!for_expr.empty()) {
            if (!openads::engine::evaluate_index_expr_truthy(*t, for_expr))
                continue;
        }
        std::string kbytes;
        if (cdx_numeric_key) {
            double dv = 0.0;
            if (!openads::engine::evaluate_index_expr_number(*t, expr, dv))
                return fail(openads::AE_INTERNAL_ERROR,
                            "failed to evaluate numeric index expression");
            kbytes = openads::engine::fox_numeric_key(dv);
        } else if (ntx_numeric_key) {
            double dv = 0.0;
            if (!openads::engine::evaluate_index_expr_number(*t, expr, dv))
                return fail(openads::AE_INTERNAL_ERROR,
                            "failed to evaluate numeric index expression");
            kbytes = openads::engine::ntx_numeric_key(dv, ntx_num_width,
                                                      ntx_num_dec);
        } else {
            auto k = openads::engine::evaluate_index_expr(*t, expr, klen);
            if (!k) return fail(k.error());
            kbytes = std::move(k).value();
        }
        if (cdx_bulk) {
            bulk_keys.emplace_back(std::move(kbytes), r);
        } else if (auto ins = idx_owner->insert(r, kbytes); !ins) {
            return fail(ins.error());
        }
    }
    if (cdx_bulk) {
        if (auto b = cdx_bulk->build_bulk(std::move(bulk_keys)); !b)
            return fail(b.error());
    }
    if (auto fl = idx_owner->flush(); !fl) return fail(fl.error());

    auto& m   = index_bindings();
    auto& act = active_binding_for();
    // Sibling tag refresh: a fresh CREATE INDEX implicitly resyncs
    // every tag in the bag (rddads' ORDLSTCLEAR + INDEX cycle drops
    // every other tag's binding so sync_all_indexes_ skipped them
    // on subsequent dbappend / replace, leaving stale or empty
    // B+trees on disk). Re-build each sibling tag's B+tree from the
    // current DBF, then register it as a parked extra binding so
    // later dbappend / dbreplace cycles update it via
    // sync_all_indexes_ instead of leaving it stale again.
    auto& m_pre   = index_bindings();
    auto& act_pre = active_binding_for();
    if (is_cdx) {
        auto sibs = openads::drivers::cdx::CdxIndex::list_tags(p.string());
        if (sibs) {
            for (const auto& sib : sibs.value()) {
                if (sib == tag) continue;
                // Skip siblings that still hold a live binding: they are
                // already tracked by sync_all_indexes_ (so their B+tree is
                // current) and re-parking them here would (a) duplicate the
                // binding — AdsGetNumIndexes / ordinal lookups would then
                // double-count the tag — and (b) pay an O(records) rebuild
                // on every CREATE INDEX, turning N tags into O(N*records*N)
                // work. Worse, the duplicate parked view double-writes every
                // dbAppend into the same on-disk tag, so the index outgrows
                // the table and keeps growing. Only tags that LOST their
                // binding across an rddads ORDLSTCLEAR cycle need the
                // rebuild-and-re-park below.
                bool already_bound = false;
                for (auto& [h0, b0] : m_pre) {
                    if (b0.table == t && b0.tag_name == sib) {
                        already_bound = true;
                        break;
                    }
                }
                if (already_bound) continue;
                auto sub = std::make_unique<
                    openads::drivers::cdx::CdxIndex>();
                if (auto r0 = sub->open_named(p.string(),
                        openads::drivers::IndexOpenMode::Shared,
                        sib); !r0) continue;
                mark_cdx_key_encoding(t, sub.get());
                // The sibling lost its binding (rddads ORDLSTCLEAR) but its
                // on-disk B+tree is already current when it was built earlier
                // in this same INDEX / REINDEX run — the common case, since a
                // full reindex builds every tag in turn. Rebuilding it again
                // on each subsequent CREATE INDEX is the O(tags^2) re-scan
                // that made INDEXAR of a 10-tag table take minutes. So if the
                // tag already has a populated tree, just RE-PARK the binding
                // (cheap) so sync_all_indexes_ tracks it again. Only rebuild
                // from the DBF when the tree is empty (never built / cleared).
                bool sib_has_data = false;
                if (auto sf = sub->seek_first(); sf)
                    sib_has_data = sf.value().positioned;
                sub->invalidate_cursor();
                if (!sib_has_data) {
                    if (auto cl = sub->clear_data(); !cl) continue;
                    std::string sib_expr = sub->expression();
                    std::string sib_for  = sub->condition();
                    std::uint16_t sib_klen = sub->key_length();
                    const bool sib_fox = sub->key_encoding() ==
                        openads::drivers::KeyEncoding::FoxNumeric;
                    std::vector<std::pair<std::string, std::uint32_t>> sib_keys;
                    sib_keys.reserve(rec_count);
                    for (std::uint32_t r2 = 1; r2 <= rec_count; ++r2) {
                        if (auto g = t->goto_record(r2); !g) continue;
                        // Honor the sibling tag's own FOR clause so a
                        // conditional tag is not silently rebuilt
                        // unconditional during this resync.
                        if (!sib_for.empty() &&
                            !openads::engine::evaluate_index_expr_truthy(
                                *t, sib_for))
                            continue;
                        std::string k2b;
                        if (sib_fox) {
                            double dv = 0.0;
                            if (!openads::engine::evaluate_index_expr_number(
                                    *t, sib_expr, dv))
                                continue;
                            k2b = openads::engine::fox_numeric_key(dv);
                        } else {
                            auto k2 = openads::engine::evaluate_index_expr(
                                *t, sib_expr, sib_klen);
                            if (!k2) continue;
                            k2b = std::move(k2).value();
                        }
                        sib_keys.emplace_back(std::move(k2b), r2);
                    }
                    (void)sub->build_bulk(std::move(sib_keys));
                    (void)sub->flush();
                }
                // Park as extra binding (persists across rddads
                // ORDLSTCLEAR cycles? - it gets dropped, but at
                // least sync_all_indexes_ can see it until then).
                ADSHANDLE sh = next_index_handle();
                openads::drivers::IIndex* raw = sub.get();
                m_pre[sh] = IndexBinding{t, sib, std::move(sub),
                                         p.string()};
                t->register_extra_index_view(raw);
                (void)act_pre;
            }
        }
    }

    // Drop ANY existing binding whose tag matches the one we're
    // re-creating: a CREATE INDEX command on an existing tag is a
    // silent overwrite (clear_data already wiped the on-disk
    // B+tree) so the stale binding must vanish too — otherwise
    // ordinal lookups iterate over both the old and new bindings.
    for (auto it = m.begin(); it != m.end(); ) {
        if (it->second.table == t && it->second.tag_name == tag) {
            // If the stale entry was the active one, take the
            // active IIndex back from the Table so the next
            // set_order doesn't double-free.
            if (act.count(t) && act[t] == it->first) {
                (void)t->take_order();
                act.erase(t);
            } else if (it->second.parked) {
                t->unregister_extra_index_view(
                    it->second.parked.get());
            }
            it = m.erase(it);
        } else {
            ++it;
        }
    }
    ADSHANDLE h = next_index_handle();
    // Each new CREATE INDEX makes itself the active order
    // (Clipper / Harbour convention). Park the previous active
    // (if any) so it stays openable + survives ORDSETFOCUS(N).
    auto prev = act.find(t);
    if (prev != act.end()) {
        auto pit = m.find(prev->second);
        if (pit != m.end()) {
            auto displaced = t->take_order();
            pit->second.parked = std::move(displaced);
            t->register_extra_index_view(pit->second.parked.get());
        }
    }
    t->set_order(std::move(idx_owner));
    m[h] = IndexBinding{t, tag, nullptr, p.string()};
    act[t] = h;
    // Clipper / Harbour: a fresh CREATE INDEX leaves the cursor
    // positioned at the new order's TOP key.
    (void)t->goto_top();
    *phIndex = h;
    return ok();
}

UNSIGNED32 AdsCreateIndex(ADSHANDLE hTable, UNSIGNED8* pucFile,
                          UNSIGNED8* pucTag, UNSIGNED8* pucExpr,
                          UNSIGNED8* /*pucCondition*/, UNSIGNED32 /*ulOptions*/,
                          UNSIGNED16 /*usKeyType*/, ADSHANDLE* phIndex) {
    Table* t = get_table(hTable);
    if (!t || phIndex == nullptr) {
        return fail(openads::AE_INTERNAL_ERROR, "unknown table or null out");
    }
    auto file = openads::abi::to_internal(pucFile, 0);
    auto tag  = openads::abi::to_internal(pucTag,  0);
    auto expr = openads::abi::to_internal(pucExpr, 0);

    // Resolve the field referenced by the expression to determine key length.
    std::int32_t fidx = t->field_index(expr);
    if (fidx < 0) {
        return fail(openads::AE_COLUMN_NOT_FOUND,
                    "AdsCreateIndex: expression must be a bare field name");
    }
    std::uint16_t klen = t->field_descriptor(static_cast<std::uint16_t>(fidx)).length;

    std::unique_ptr<openads::drivers::IIndex> idx;
    if (path_ends_with_ci(file, ".cdx")) {
        auto created = openads::drivers::cdx::CdxIndex::create(
            file, tag, expr, klen, false, false);
        if (!created) return fail(created.error());
        idx = std::make_unique<openads::drivers::cdx::CdxIndex>(
            std::move(created).value());
    } else {
        auto created = openads::drivers::ntx::NtxIndex::create(
            file, tag, expr, klen, false, false);
        if (!created) return fail(created.error());
        idx = std::make_unique<openads::drivers::ntx::NtxIndex>(
            std::move(created).value());
    }

    // Populate from existing live records in primary order. Deleted
    // records are skipped so AdsSeek over the new index never returns
    // phantom recnos. For CDX, collect the key stream and bulk-load the
    // B+tree bottom-up (one encode per page) instead of record-by-record
    // insertion — ~10x faster on a full build.
    auto rec_count = t->record_count();
    openads::drivers::cdx::CdxIndex* cdx_bulk =
        path_ends_with_ci(file, ".cdx")
            ? static_cast<openads::drivers::cdx::CdxIndex*>(idx.get())
            : nullptr;
    std::vector<std::pair<std::string, std::uint32_t>> bulk_keys;
    if (cdx_bulk) bulk_keys.reserve(rec_count);
    for (std::uint32_t r = 1; r <= rec_count; ++r) {
        if (auto rr = t->goto_record(r); !rr) return fail(rr.error());
        if (t->is_deleted()) continue;
        auto v = t->read_field(static_cast<std::uint16_t>(fidx));
        if (!v) return fail(v.error());
        std::string padded = v.value().as_string;
        if (padded.size() < klen) padded.append(klen - padded.size(), ' ');
        if (padded.size() > klen) padded.resize(klen);
        if (cdx_bulk) {
            bulk_keys.emplace_back(std::move(padded), r);
        } else if (auto ins = idx->insert(r, padded); !ins) {
            return fail(ins.error());
        }
    }
    if (cdx_bulk) {
        if (auto b = cdx_bulk->build_bulk(std::move(bulk_keys)); !b)
            return fail(b.error());
    }
    if (auto fl = idx->flush(); !fl) return fail(fl.error());

    auto& m   = index_bindings();
    auto& act = active_binding_for();
    bool table_has_active = act.find(t) != act.end();
    ADSHANDLE h = next_index_handle();
    if (!table_has_active) {
        t->set_order(std::move(idx));
        m[h] = IndexBinding{t, tag, nullptr, file};
        act[t] = h;
    } else {
        openads::drivers::IIndex* raw = idx.get();
        m[h] = IndexBinding{t, tag, std::move(idx), file};
        t->register_extra_index_view(raw);
    }
    *phIndex = h;
    return ok();
}

UNSIGNED32 AdsDeleteIndex(ADSHANDLE hIndex) {
    return AdsCloseIndex(hIndex);
}

// --- M9.20 custom-key indexes ---------------------------------------------
//
// rddads' DBOI_KEYADD / DBOI_KEYDELETE branches call AdsAddCustomKey
// / AdsDeleteCustomKey with just an index handle and expect the call
// to operate on the **current record**. Real ACE evaluates the
// index's expression against the positioned row and inserts (or
// erases) the resulting (key, recno) entry — the "custom" wording
// comes from the surrounding `ADS_CUSTOM` flag on the index, which
// disables the engine's auto-sync so apps drive the index manually
// through these two entry points.
//
// OpenADS today doesn't separately track the `ADS_CUSTOM` flag, so
// these calls always evaluate + insert/erase. Apps that opt into
// custom mode get correct behaviour because they're the ones
// explicitly invoking these functions; expression-driven apps stay
// out of the call site.

namespace {

openads::drivers::IIndex* iindex_for_binding(IndexBinding& b) {
    if (b.parked) return b.parked.get();
    if (auto* o = b.table ? b.table->order() : nullptr; o) {
        return const_cast<openads::engine::Order*>(o)->index();
    }
    return nullptr;
}

}  // namespace

UNSIGNED32 AdsAddCustomKey(ADSHANDLE hIndex) {
    auto& s = state();
    std::lock_guard<std::recursive_mutex> lk(s.mu);
    auto& m = index_bindings();
    auto it = m.find(hIndex);
    if (it == m.end()) {
        return fail(openads::AE_INTERNAL_ERROR, "unknown index handle");
    }
    Table* t = it->second.table;
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    auto* idx = iindex_for_binding(it->second);
    if (!idx) return fail(openads::AE_INTERNAL_ERROR, "no IIndex for binding");

    std::uint16_t klen = idx->key_length();
    if (klen == 0) klen = 32;
    std::string kb;
    if (idx->key_encoding() == openads::drivers::KeyEncoding::FoxNumeric) {
        double dv = 0.0;
        if (!openads::engine::evaluate_index_expr_number(
                *t, idx->expression(), dv))
            return fail(openads::AE_INTERNAL_ERROR,
                        "failed to evaluate numeric index expression");
        kb = openads::engine::fox_numeric_key(dv);
    } else if (idx->key_encoding() ==
               openads::drivers::KeyEncoding::NtxNumeric) {
        double dv = 0.0;
        if (!openads::engine::evaluate_index_expr_number(
                *t, idx->expression(), dv))
            return fail(openads::AE_INTERNAL_ERROR,
                        "failed to evaluate numeric index expression");
        kb = openads::engine::ntx_numeric_key(dv, idx->key_length(),
                                              idx->key_decimals());
    } else {
        auto k = openads::engine::evaluate_index_expr(*t, idx->expression(), klen);
        if (!k) return fail(k.error());
        kb = std::move(k).value();
    }
    auto r = idx->insert(t->recno(), kb);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsDeleteCustomKey(ADSHANDLE hIndex) {
    auto& s = state();
    std::lock_guard<std::recursive_mutex> lk(s.mu);
    auto& m = index_bindings();
    auto it = m.find(hIndex);
    if (it == m.end()) {
        return fail(openads::AE_INTERNAL_ERROR, "unknown index handle");
    }
    Table* t = it->second.table;
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    auto* idx = iindex_for_binding(it->second);
    if (!idx) return fail(openads::AE_INTERNAL_ERROR, "no IIndex for binding");

    std::uint16_t klen = idx->key_length();
    if (klen == 0) klen = 32;
    std::string kb;
    if (idx->key_encoding() == openads::drivers::KeyEncoding::FoxNumeric) {
        double dv = 0.0;
        if (!openads::engine::evaluate_index_expr_number(
                *t, idx->expression(), dv))
            return fail(openads::AE_INTERNAL_ERROR,
                        "failed to evaluate numeric index expression");
        kb = openads::engine::fox_numeric_key(dv);
    } else if (idx->key_encoding() ==
               openads::drivers::KeyEncoding::NtxNumeric) {
        double dv = 0.0;
        if (!openads::engine::evaluate_index_expr_number(
                *t, idx->expression(), dv))
            return fail(openads::AE_INTERNAL_ERROR,
                        "failed to evaluate numeric index expression");
        kb = openads::engine::ntx_numeric_key(dv, idx->key_length(),
                                              idx->key_decimals());
    } else {
        auto k = openads::engine::evaluate_index_expr(*t, idx->expression(), klen);
        if (!k) return fail(k.error());
        kb = std::move(k).value();
    }
    auto r = idx->erase(t->recno(), kb);
    if (!r) return fail(r.error());
    return ok();
}

// --- M9.19 Full-text search ------------------------------------------------
//
// Creates an OpenADS-native `.fts` inverted-index file alongside the
// table. The format is plain UTF-8 text — clean-room, NOT derived
// from any proprietary ADS FTS layout. Search support (token lookup
// at query time) is a follow-up milestone; today the create path
// gives apps a stable artefact to commit and visit.
//
// Most of the optional configuration knobs are honoured: min/max
// word length, custom delimiter / noise-word arrays. The page-size,
// drop-char, conditional-char, and reserved arguments are accepted
// (so the ABI shape matches rddads' ADSCREATEFTSINDEX call) and
// don't affect today's text emitter.

}  // extern "C"

namespace {

openads::engine::FtsOptions
build_fts_options(UNSIGNED32  ulMinWordLen, UNSIGNED32  ulMaxWordLen,
                  UNSIGNED16  usUseDefaultDelim,
                  const UNSIGNED8* pucDelimiters,
                  UNSIGNED16  usUseDefaultNoise,
                  const UNSIGNED8* pucNoiseWords) {
    openads::engine::FtsOptions opts;
    if (ulMinWordLen > 0) opts.min_word_len = ulMinWordLen;
    if (ulMaxWordLen > 0) opts.max_word_len = ulMaxWordLen;

    if (!usUseDefaultDelim && pucDelimiters != nullptr) {
        opts.extra_delims = openads::abi::to_internal(
            const_cast<UNSIGNED8*>(pucDelimiters), 0);
    }

    if (usUseDefaultNoise) {
        // Standard English stop-word seed; apps can override.
        for (auto* w : {"a", "an", "the", "and", "or", "but", "if",
                        "of", "in", "on", "at", "to", "for", "is",
                        "are", "was", "were", "be", "by", "with",
                        "as", "from", "this", "that", "these",
                        "those", "it", "its", "not"}) {
            opts.noise_words.insert(w);
        }
    } else if (pucNoiseWords != nullptr) {
        auto raw = openads::abi::to_internal(
            const_cast<UNSIGNED8*>(pucNoiseWords), 0);
        std::string cur;
        for (char c : raw) {
            if (c == ' ' || c == '\t' || c == ',' || c == ';' || c == '\n') {
                if (!cur.empty()) { opts.noise_words.insert(cur); cur.clear(); }
            } else {
                cur.push_back(static_cast<char>(std::tolower(
                    static_cast<unsigned char>(c))));
            }
        }
        if (!cur.empty()) opts.noise_words.insert(cur);
    }
    return opts;
}

}  // namespace

extern "C" {

// --- M9.23 fill in remaining MISS exports ---------------------------------

UNSIGNED32 AdsGetLongLong(ADSHANDLE hTable, UNSIGNED8* pucField,
                          std::int64_t* pllValue) {
    if (pllValue == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    // SQL backend (e.g. postgresql): read text via the per-backend ops
    // vtable then parse, instead of falling through to the native path.
    if (auto* ops = openads::abi::backend_table_ops_for(hTable)) {
        if (ops->get_field) {
            UNSIGNED8 buf[64] = {0};
            UNSIGNED32 cap = sizeof(buf);
            UNSIGNED32 rc = ops->get_field(hTable, pucField, buf, &cap, 0);
            if (rc != 0) return rc;
            std::string s(reinterpret_cast<const char*>(buf),
                          std::min<UNSIGNED32>(cap, sizeof(buf)));
            std::size_t j = 0;
            while (j < s.size() &&
                   std::isspace(static_cast<unsigned char>(s[j]))) ++j;
            *pllValue = static_cast<std::int64_t>(
                std::strtoll(s.c_str() + j, nullptr, 10));
            return ok();
        }
    }
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "");
    std::uint16_t idx = 0;
    if (!resolve_field_index(t, pucField, &idx)) {
        return fail(openads::AE_COLUMN_NOT_FOUND, "");
    }
    auto v = t->read_field(idx);
    if (!v) return fail(v.error());
    auto& s = v.value().as_string;
    // Strip leading whitespace; strtoll handles signed prefix and digits.
    std::size_t i = 0;
    while (i < s.size() &&
           std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    *pllValue = static_cast<std::int64_t>(
        std::strtoll(s.c_str() + i, nullptr, 10));
    return ok();
}

UNSIGNED32 AdsSetFieldRaw(ADSHANDLE hTable, UNSIGNED8* pucField,
                          UNSIGNED8* pucBuf, UNSIGNED32 ulLen) {
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    std::uint16_t idx = 0;
    if (!resolve_field_index(t, pucField, &idx)) {
        return fail(openads::AE_COLUMN_NOT_FOUND, "");
    }
    std::string raw;
    if (pucBuf != nullptr && ulLen > 0) {
        raw.assign(reinterpret_cast<const char*>(pucBuf), ulLen);
    }
    auto r = t->set_field(idx, raw);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsVerifySQL(ADSHANDLE /*hStatement*/, UNSIGNED8* pucSQL) {
    if (pucSQL == nullptr) return fail(openads::AE_PARSE_ERROR, "null SQL");
    auto sql = openads::abi::to_internal(pucSQL, 0);
    auto r = openads::sql::parse_select(sql);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsFailedTransactionRecovery(UNSIGNED8* pucServer) {
    if (pucServer == nullptr) {
        return fail(openads::AE_INTERNAL_ERROR, "null server path");
    }
    auto path = openads::abi::to_internal(pucServer, 0);
    // Recovery happens automatically on Connection::open — the open
    // path scans openads.txlog, replays orphan transactions' before-
    // images, and truncates the log. Open + close gives the caller a
    // single explicit recovery pass.
    auto opened = Connection::open(path);
    if (!opened) return fail(opened.error());
    return ok();
}

UNSIGNED32 AdsGetAllLocks(ADSHANDLE hTable, UNSIGNED32* paRecnos,
                          UNSIGNED16* pusCount) {
    Table* t = get_table(hTable);
    if (!t || pusCount == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    auto held = t->held_record_locks();
    UNSIGNED16 cap = *pusCount;
    UNSIGNED16 n   = static_cast<UNSIGNED16>(
        std::min<std::size_t>(held.size(), cap));
    if (paRecnos != nullptr && n > 0) {
        for (UNSIGNED16 i = 0; i < n; ++i) {
            paRecnos[i] = static_cast<UNSIGNED32>(held[i]);
        }
    }
    *pusCount = static_cast<UNSIGNED16>(held.size());
    return ok();
}

// M12.16c — switch the active order on `hTable` to the binding
// whose tag matches `pucName` (case-insensitive). Mirrors the
// rddads adsOrdSetActive(cTagName) flow. Empty / NULL name flips
// the table back to natural-record-order (clear active binding).
UNSIGNED32 AdsSetIndexOrder(ADSHANDLE hTable, UNSIGNED8* pucName) {
    if (auto* rt = get_remote_table(hTable)) {
        std::string name = pucName
            ? openads::abi::to_internal(pucName, 0) : std::string();
        auto r = rt->conn->set_order_by_name(rt->id, name);
        if (!r) return fail(r.error());
        return ok();
    }
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    std::string name = pucName
        ? openads::abi::to_internal(pucName, 0) : std::string();
    if (name.empty()) {
        // Empty tag = natural order. Park the active binding back
        // into its slot so a subsequent AdsSetIndexOrder picks the
        // current Table::order_ up cleanly.
        auto& act = active_binding_for();
        auto act_it = act.find(t);
        if (act_it != act.end()) {
            auto& m = index_bindings();
            auto bit = m.find(act_it->second);
            if (bit != m.end()) {
                auto taken = t->take_order();
                openads::drivers::IIndex* raw = taken.get();
                bit->second.parked = std::move(taken);
                if (raw) t->register_extra_index_view(raw);
            }
            act.erase(act_it);
        }
        return ok();
    }
    auto upper_eq = [](const std::string& a, const std::string& b) {
        if (a.size() != b.size()) return false;
        for (std::size_t i = 0; i < a.size(); ++i) {
            char ca = static_cast<char>(std::toupper(
                static_cast<unsigned char>(a[i])));
            char cb = static_cast<char>(std::toupper(
                static_cast<unsigned char>(b[i])));
            if (ca != cb) return false;
        }
        return true;
    };
    auto& m = index_bindings();
    for (auto& [h, b] : m) {
        if (b.table == t && upper_eq(b.tag_name, name)) {
            auto r = activate_binding(h);
            if (!r) return fail(r.error());
            return ok();
        }
    }
    return fail(openads::AE_INTERNAL_ERROR,
                ("AdsSetIndexOrder: no tag '" + name + "' on table").c_str());
}

UNSIGNED32 AdsSetIndexOrderByHandle(ADSHANDLE hTable, ADSHANDLE hIndex) {
    if (auto* rt = get_remote_table(hTable)) {
        if (auto* ri = get_remote_index(hIndex)) {
            auto r = rt->conn->set_order(rt->id, ri->id);
            if (!r) return fail(r.error());
            return ok();
        }
        return fail(openads::AE_INTERNAL_ERROR,
                    "AdsSetIndexOrderByHandle: hIndex is not a remote index");
    }
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    auto& m = index_bindings();
    auto it = m.find(hIndex);
    if (it == m.end() || it->second.table != t) {
        return fail(openads::AE_INTERNAL_ERROR,
                    "AdsSetIndexOrderByHandle: index not bound to table");
    }
    auto r = activate_binding(hIndex);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsSkipUnique(ADSHANDLE hIndex, SIGNED32 lDirection) {
    if (auto* ri = get_remote_index(hIndex)) {
        if (ri->parent) ri->parent->row_valid = false;   // M12.17
        auto r = ri->conn->skip_unique(ri->id, lDirection);
        if (!r) return fail(r.error());
        return ok();
    }
    auto& s = state();
    std::lock_guard<std::recursive_mutex> lk(s.mu);
    auto* idx = iindex_for_handle(hIndex);
    Table* t  = lookup_table_by_index(hIndex);
    if (!idx || !t) return fail(openads::AE_INTERNAL_ERROR, "unknown index");
    (void)activate_binding(hIndex);

    std::string start_key = idx->current_key();
    auto step = [&]() {
        return lDirection < 0 ? idx->prev() : idx->next();
    };

    for (;;) {
        auto r = step();
        if (!r) return fail(r.error());
        if (!r.value().positioned) {
            return fail(openads::AE_INTERNAL_ERROR, "no more unique keys");
        }
        if (idx->current_key() != start_key) {
            // Position the table on the new recno.
            (void)t->goto_record(r.value().recno);
            return ok();
        }
    }
}

// AdsFTSSearch is an OpenADS extension. The original ACE SDK doesn't
// export an entry point with this exact shape that rddads' Harbour
// surface ever reached (rddads is silent on FTS query semantics), so
// OpenADS publishes a small clean-room API: load the .fts file at
// `pucFile`, tokenise the query with the standard rules, intersect
// the per-token recno lists, and write up to `*pulCount` recnos into
// `paRecnos`. `*pulCount` is treated as in/out — the caller passes
// the array capacity and reads back the total number of matches
// (which may be larger than the buffer).
UNSIGNED32 AdsFTSSearch(ADSHANDLE   /*hConnect*/,
                        UNSIGNED8*  pucFile,
                        UNSIGNED8*  pucQuery,
                        UNSIGNED32* paRecnos,
                        UNSIGNED32* pulCount) {
    if (pucFile == nullptr || pucQuery == nullptr || pulCount == nullptr) {
        return fail(openads::AE_INTERNAL_ERROR, "AdsFTSSearch: null arg");
    }
    auto path  = openads::abi::to_internal(pucFile,  0);
    auto query = openads::abi::to_internal(pucQuery, 0);

    auto loaded = openads::engine::Fts::load(path);
    if (!loaded) return fail(loaded.error());

    openads::engine::FtsOptions opts;
    auto hits = openads::engine::Fts::search(loaded.value(), query, opts);

    UNSIGNED32 cap = *pulCount;
    UNSIGNED32 n   = static_cast<UNSIGNED32>(
        std::min<std::size_t>(hits.size(), cap));
    if (paRecnos != nullptr && n > 0) {
        std::memcpy(paRecnos, hits.data(), n * sizeof(UNSIGNED32));
    }
    *pulCount = static_cast<UNSIGNED32>(hits.size());
    return ok();
}

// --- M10.1 Data-Dictionary CRUD --------------------------------------------
//
// Real persistence in OpenADS' clean-room DD text format. When the
// caller's connection has no DD attached (i.e. the connection was
// opened against a plain data directory, not a `.add` file), the
// CRUD calls report AE_SUCCESS and no-op — matching the "everything
// quiescent" contract used for AdsMg* in M9.24. Apps that opened
// the DD via `Connection::open(<.add>)` (M6) get round-trip
// persistence.

namespace {

Connection* conn_from_handle(ADSHANDLE hConn) {
    auto& s = state();
    return s.registry.lookup<Connection>(hConn, HandleKind::Connection);
}

openads::engine::DataDict* dd_from_handle(ADSHANDLE hConn) {
    Connection* c = conn_from_handle(hConn);
    if (c == nullptr || !c->has_dd()) return nullptr;
    return c->dd();
}

}  // namespace

UNSIGNED32 AdsDDAddIndexFile(ADSHANDLE hConn,
                             UNSIGNED8* pucTable, UNSIGNED8* pucIndex,
                             UNSIGNED8* pucComment) {
    auto* dd = dd_from_handle(hConn);
    if (dd == nullptr) return ok();
    auto tbl  = openads::abi::to_internal(pucTable, 0);
    auto idx  = openads::abi::to_internal(pucIndex, 0);
    auto cmt  = pucComment ? openads::abi::to_internal(pucComment, 0)
                           : std::string();
    auto r = dd->add_index_file(tbl, idx, cmt);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsDDRemoveIndexFile(ADSHANDLE hConn,
                                UNSIGNED8* pucTable, UNSIGNED8* pucIndex,
                                UNSIGNED16 /*opt*/) {
    auto* dd = dd_from_handle(hConn);
    if (dd == nullptr) return ok();
    auto tbl = openads::abi::to_internal(pucTable, 0);
    auto idx = openads::abi::to_internal(pucIndex, 0);
    auto r = dd->remove_index_file(tbl, idx);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsDDCreateUser(ADSHANDLE hConn, UNSIGNED8* pucGroup,
                           UNSIGNED8* pucUser, UNSIGNED8* pucPwd,
                           UNSIGNED8* pucDesc) {
    auto* dd = dd_from_handle(hConn);
    if (dd == nullptr) return ok();
    auto user = openads::abi::to_internal(pucUser, 0);
    auto r = dd->create_user(user);
    if (!r) return fail(r.error());
    if (pucPwd && pucPwd[0] != '\0') {
        auto pwd = openads::abi::to_internal(pucPwd, 0);
        dd->set_user_property(user, "prop_1101", pwd);
    }
    if (pucDesc && pucDesc[0] != '\0') {
        auto desc = openads::abi::to_internal(pucDesc, 0);
        dd->set_user_property(user, "prop_1", desc);
    }
    if (pucGroup && pucGroup[0] != '\0') {
        auto grp = openads::abi::to_internal(pucGroup, 0);
        dd->add_user_to_group(user, grp);
    }
    return ok();
}

UNSIGNED32 AdsDDDeleteUser(ADSHANDLE hConn, UNSIGNED8* pucUser) {
    auto* dd = dd_from_handle(hConn);
    if (dd == nullptr) return ok();
    auto user = openads::abi::to_internal(pucUser, 0);
    auto r = dd->delete_user(user);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsDDAddUserToGroup(ADSHANDLE hConn,
                               UNSIGNED8* pucGroup, UNSIGNED8* pucUser) {
    auto* dd = dd_from_handle(hConn);
    if (dd == nullptr) return ok();
    auto group = openads::abi::to_internal(pucGroup, 0);
    auto user  = openads::abi::to_internal(pucUser, 0);
    auto r = dd->add_user_to_group(user, group);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsDDRemoveUserFromGroup(ADSHANDLE hConn,
                                    UNSIGNED8* pucGroup, UNSIGNED8* pucUser) {
    auto* dd = dd_from_handle(hConn);
    if (dd == nullptr) return ok();
    auto group = openads::abi::to_internal(pucGroup, 0);
    auto user  = openads::abi::to_internal(pucUser, 0);
    auto r = dd->remove_user_from_group(user, group);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsDDCreateLink(ADSHANDLE hConn, UNSIGNED8* pucAlias,
                           UNSIGNED8* pucPath, UNSIGNED8* pucUser,
                           UNSIGNED8* pucPwd, UNSIGNED16 /*opt*/) {
    auto* dd = dd_from_handle(hConn);
    if (dd == nullptr) return ok();
    auto alias = openads::abi::to_internal(pucAlias, 0);
    auto path  = openads::abi::to_internal(pucPath, 0);
    auto user  = pucUser ? openads::abi::to_internal(pucUser, 0) : std::string();
    auto pwd   = pucPwd  ? openads::abi::to_internal(pucPwd, 0)  : std::string();
    auto r = dd->create_link(alias, path, user, pwd);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsDDDropLink(ADSHANDLE hConn, UNSIGNED8* pucAlias,
                         UNSIGNED16 /*opt*/) {
    auto* dd = dd_from_handle(hConn);
    if (dd == nullptr) return ok();
    auto alias = openads::abi::to_internal(pucAlias, 0);
    auto r = dd->drop_link(alias);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsDDModifyLink(ADSHANDLE hConn, UNSIGNED8* pucAlias,
                           UNSIGNED8* pucPath, UNSIGNED8* pucUser,
                           UNSIGNED8* pucPwd, UNSIGNED16 /*opt*/) {
    auto* dd = dd_from_handle(hConn);
    if (dd == nullptr) return ok();
    auto alias = openads::abi::to_internal(pucAlias, 0);
    auto path  = pucPath ? openads::abi::to_internal(pucPath, 0) : std::string();
    auto user  = pucUser ? openads::abi::to_internal(pucUser, 0) : std::string();
    auto pwd   = pucPwd  ? openads::abi::to_internal(pucPwd, 0)  : std::string();
    auto r = dd->modify_link(alias, path, user, pwd);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsDDCreateRefIntegrity(ADSHANDLE hConn,
                                   UNSIGNED8* pucName, UNSIGNED8* pucFail,
                                   UNSIGNED8* pucParent, UNSIGNED8* pucParentTag,
                                   UNSIGNED8* pucChild, UNSIGNED8* pucChildTag,
                                   UNSIGNED16 usUpdate, UNSIGNED16 usDelete) {
    auto* dd = dd_from_handle(hConn);
    if (dd == nullptr) return ok();
    openads::engine::DataDict::RiEntry e;
    e.name        = openads::abi::to_internal(pucName,   0);
    e.parent      = pucParent    ? openads::abi::to_internal(pucParent,    0) : std::string();
    e.child       = pucChild     ? openads::abi::to_internal(pucChild,     0) : std::string();
    e.parent_tag  = pucParentTag ? openads::abi::to_internal(pucParentTag, 0) : std::string();
    e.child_tag   = pucChildTag  ? openads::abi::to_internal(pucChildTag,  0) : std::string();
    e.update_opt  = std::to_string(static_cast<unsigned>(usUpdate));
    e.delete_opt  = std::to_string(static_cast<unsigned>(usDelete));
    e.fail_table  = pucFail      ? openads::abi::to_internal(pucFail,      0) : std::string();
    auto r = dd->create_ri(e);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsDDRemoveRefIntegrity(ADSHANDLE hConn, UNSIGNED8* pucName) {
    auto* dd = dd_from_handle(hConn);
    if (dd == nullptr) return ok();
    auto name = openads::abi::to_internal(pucName, 0);
    auto r = dd->remove_ri(name);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsDDSetDatabaseProperty(ADSHANDLE hConn, UNSIGNED16 usProp,
                                    void* pBuf, UNSIGNED16 usLen) {
    auto* dd = dd_from_handle(hConn);
    if (dd == nullptr) return ok();
    std::string key = "prop_" + std::to_string(static_cast<unsigned>(usProp));
    std::string val;
    if (pBuf != nullptr && usLen > 0) {
        val.assign(reinterpret_cast<const char*>(pBuf), usLen);
    }
    auto r = dd->set_db_property(key, val);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsDDGetDatabaseProperty(ADSHANDLE hConn, UNSIGNED16 usProp,
                                    void* pBuf, UNSIGNED16* pusLen) {
    if (pusLen == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    auto* dd = dd_from_handle(hConn);
    UNSIGNED16 cap = *pusLen;
    if (pBuf != nullptr && cap > 0) {
        std::memset(pBuf, 0, cap);
    }
    if (dd == nullptr) { *pusLen = 0; return ok(); }
    std::string key = "prop_" + std::to_string(static_cast<unsigned>(usProp));
    auto val = dd->get_db_property(key);
    UNSIGNED16 n = static_cast<UNSIGNED16>(
        std::min<std::size_t>(val.size(), cap));
    if (pBuf != nullptr && n > 0) std::memcpy(pBuf, val.data(), n);
    *pusLen = static_cast<UNSIGNED16>(val.size());
    return ok();
}

UNSIGNED32 AdsDDGetUserProperty(ADSHANDLE hConn, UNSIGNED8* pucUser,
                                UNSIGNED16 usProp, void* pBuf,
                                UNSIGNED16* pusLen) {
    if (pusLen == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    auto* dd = dd_from_handle(hConn);
    UNSIGNED16 cap = *pusLen;
    if (pBuf != nullptr && cap > 0) std::memset(pBuf, 0, cap);
    if (dd == nullptr) { *pusLen = 0; return ok(); }
    auto user = openads::abi::to_internal(pucUser, 0);

    // ADS_DD_USER_BAD_LOGINS (1103) — always 0, returned as uint16.
    if (usProp == 1103) {
        UNSIGNED16 zero = 0;
        UNSIGNED16 n = std::min<UNSIGNED16>(cap, sizeof(UNSIGNED16));
        if (pBuf && n > 0) std::memcpy(pBuf, &zero, n);
        *pusLen = sizeof(UNSIGNED16);
        return ok();
    }
    // ADS_DD_USER_GROUP_MEMBERSHIP (1102) — comma-separated group list.
    if (usProp == 1102) {
        std::string groups;
        for (const auto& g : dd->groups_of(user)) {
            if (!groups.empty()) groups += ',';
            groups += g;
        }
        UNSIGNED16 n = static_cast<UNSIGNED16>(
            std::min<std::size_t>(groups.size(), cap));
        if (pBuf && n > 0) std::memcpy(pBuf, groups.data(), n);
        *pusLen = static_cast<UNSIGNED16>(groups.size());
        return ok();
    }

    std::string key = "prop_" + std::to_string(static_cast<unsigned>(usProp));
    auto val = dd->get_user_property(user, key);
    UNSIGNED16 n = static_cast<UNSIGNED16>(
        std::min<std::size_t>(val.size(), cap));
    if (pBuf != nullptr && n > 0) std::memcpy(pBuf, val.data(), n);
    *pusLen = static_cast<UNSIGNED16>(val.size());
    return ok();
}

UNSIGNED32 AdsDDSetUserProperty(ADSHANDLE hConn, UNSIGNED8* pucUser,
                                UNSIGNED16 usProp, void* pvBuf,
                                UNSIGNED16 usLen) {
    auto* dd = dd_from_handle(hConn);
    if (dd == nullptr) return ok();
    auto user = openads::abi::to_internal(pucUser, 0);
    if (!dd->has_user(user))
        return fail(static_cast<int>(openads::AE_TABLE_NOT_FOUND), user.c_str());

    // ADS_DD_USER_BAD_LOGINS (1103) — read-only counter, silently ignore sets.
    if (usProp == 1103) return ok();

    // ADS_DD_USER_GROUP_MEMBERSHIP (1102) — add user to the named group.
    if (usProp == 1102) {
        if (pvBuf == nullptr || usLen == 0) return ok();
        std::string grp(reinterpret_cast<const char*>(pvBuf), usLen);
        if (!grp.empty() && grp.back() == '\0') grp.pop_back();
        if (grp.empty()) return ok();
        auto r = dd->add_user_to_group(user, grp);
        if (!r) return fail(r.error());
        return ok();
    }

    // All other codes (including 1 for comment, 1101 for password): store as
    // string property keyed by "prop_N" so Get/Set round-trip symmetrically.
    std::string val;
    if (pvBuf != nullptr && usLen > 0)
        val.assign(reinterpret_cast<const char*>(pvBuf), usLen);
    std::string key = "prop_" + std::to_string(static_cast<unsigned>(usProp));
    auto r = dd->set_user_property(user, key, val);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsDDGetTableProperty(ADSHANDLE hConn, UNSIGNED8* pucTable,
                                 UNSIGNED16 usProp, void* pBuf,
                                 UNSIGNED16* pusLen) {
    namespace fs = std::filesystem;
    if (pusLen == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    Connection* c = conn_from_handle(hConn);
    UNSIGNED16 cap = *pusLen;
    if (pBuf != nullptr && cap > 0) std::memset(pBuf, 0, cap);

    auto* dd = (c != nullptr && c->has_dd()) ? c->dd() : nullptr;
    if (dd == nullptr) { *pusLen = 0; return ok(); }

    auto alias = openads::abi::to_internal(pucTable, 0);
    if (!dd->has_alias(alias)) {
        *pusLen = 0;
        return fail(static_cast<int>(openads::AE_TABLE_NOT_FOUND),
                    alias.c_str());
    }

    std::string rel = dd->resolve(alias);

    auto put_str = [&](const std::string& s) -> UNSIGNED32 {
        UNSIGNED16 n = static_cast<UNSIGNED16>(
            std::min<std::size_t>(s.size(), cap));
        if (pBuf != nullptr && n > 0) std::memcpy(pBuf, s.data(), n);
        *pusLen = static_cast<UNSIGNED16>(s.size());
        return ok();
    };
    auto put_u16 = [&](std::uint16_t v) -> UNSIGNED32 {
        const UNSIGNED16 need = 2;
        if (pBuf != nullptr && cap >= need) {
            auto* b = static_cast<std::uint8_t*>(pBuf);
            b[0] = static_cast<std::uint8_t>(v & 0xFFu);
            b[1] = static_cast<std::uint8_t>(v >> 8);
        }
        *pusLen = need;
        return ok();
    };
    auto put_u32 = [&](std::uint32_t v) -> UNSIGNED32 {
        const UNSIGNED16 need = 4;
        if (pBuf != nullptr && cap >= need) {
            auto* b = static_cast<std::uint8_t*>(pBuf);
            b[0] = static_cast<std::uint8_t>( v        & 0xFFu);
            b[1] = static_cast<std::uint8_t>((v >>  8) & 0xFFu);
            b[2] = static_cast<std::uint8_t>((v >> 16) & 0xFFu);
            b[3] = static_cast<std::uint8_t>((v >> 24) & 0xFFu);
        }
        *pusLen = need;
        return ok();
    };

    switch (usProp) {
        case ADS_DD_TABLE_RELATIVE_PATH:       // 211
            return put_str(rel);

        case ADS_DD_TABLE_PATH: {              // 205 — absolute path
            fs::path abs = fs::path(c->data_dir()) / rel;
            return put_str(abs.string());
        }

        case ADS_DD_TABLE_TYPE: {              // 204 — infer from extension
            fs::path p(rel);
            std::string ext = p.extension().string();
            for (auto& ch : ext)
                ch = static_cast<char>(
                    std::tolower(static_cast<unsigned char>(ch)));
            UNSIGNED16 ttype = ADS_CDX;
            if (ext == ".adt") ttype = ADS_ADT;
            return put_u16(ttype);
        }

        case ADS_DD_TABLE_CHAR_TYPE:           // 212
            return put_u16(ADS_ANSI);

        case ADS_DD_TABLE_OBJ_ID:             // 208
            return put_u32(0);

        case ADS_DD_TABLE_FIELD_COUNT:         // 206 — requires opening table
            return put_u32(0);

        case ADS_DD_TABLE_ENCRYPTION:          // 214
        case ADS_DD_TABLE_AUTO_CREATE:         // 203
        case ADS_DD_TABLE_IS_RI_PARENT:        // 210
        case ADS_DD_TABLE_MEMO_BLOCK_SIZE:     // 215
            return put_u16(0);

        case ADS_DD_TABLE_PERMISSION_LEVEL: {  // 216
            int lvl = (c && !c->username().empty())
                ? dd->get_effective_permission(c->username(), alias)
                : 4;
            return put_u16(static_cast<std::uint16_t>(lvl));
        }

        case ADS_DD_TABLE_VALIDATION_EXPR:     // 200
        case ADS_DD_TABLE_VALIDATION_MSG:      // 201
            return put_str({});

        case ADS_DD_TABLE_PRIMARY_KEY:         // 202
            return put_str(dd->get_table_property(alias, 202));

        case ADS_DD_TABLE_DEFAULT_INDEX:       // 213
            return put_str(dd->get_table_property(alias, 213));

        default:
            *pusLen = 0;
            return fail(openads::AE_FUNCTION_NOT_AVAILABLE, "");
    }
}

UNSIGNED32 AdsDDSetTableProperty(ADSHANDLE hConn, UNSIGNED8* pucTable,
                                 UNSIGNED16 usProp, void* pBuf,
                                 UNSIGNED16 usLen) {
    auto* dd = dd_from_handle(hConn);
    if (dd == nullptr) return ok();
    auto alias = openads::abi::to_internal(pucTable, 0);
    if (!dd->has_alias(alias))
        return fail(static_cast<int>(openads::AE_TABLE_NOT_FOUND),
                    alias.c_str());
    // Only string properties are supported for set
    if (usProp == ADS_DD_TABLE_PRIMARY_KEY || usProp == ADS_DD_TABLE_DEFAULT_INDEX) {
        std::string val;
        if (pBuf != nullptr && usLen > 0)
            val.assign(static_cast<const char*>(pBuf), usLen);
        dd->set_table_property(alias, static_cast<int>(usProp), val);
        return ok();
    }
    return fail(static_cast<int>(openads::AE_FUNCTION_NOT_AVAILABLE),
                "AdsDDSetTableProperty");
}

UNSIGNED32 AdsDDSetUserTableRights(ADSHANDLE hConn, UNSIGNED8* pucTable,
                                   UNSIGNED8* pucUser, UNSIGNED32 ulLevel) {
    auto* dd = dd_from_handle(hConn);
    if (dd == nullptr) return ok();
    auto tbl  = openads::abi::to_internal(pucTable, 0);
    auto user = pucUser ? openads::abi::to_internal(pucUser, 0) : std::string{};
    if (tbl.empty() || user.empty())
        return fail(openads::AE_INTERNAL_ERROR, "table or user is empty");
    auto r = dd->set_table_permission(tbl, user, static_cast<int>(ulLevel));
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsDDGetUserTableRights(ADSHANDLE hConn, UNSIGNED8* pucTable,
                                   UNSIGNED8* pucUser, UNSIGNED32* pulLevel) {
    if (pulLevel == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    Connection* c = conn_from_handle(hConn);
    if (c == nullptr || !c->has_dd()) { *pulLevel = 4; return ok(); }
    auto tbl  = openads::abi::to_internal(pucTable, 0);
    auto user = pucUser ? openads::abi::to_internal(pucUser, 0) : std::string{};
    *pulLevel = static_cast<UNSIGNED32>(
        c->dd()->get_effective_permission(user, tbl));
    return ok();
}

// ---------------------------------------------------------------------------
// AdsDDGetFieldProperty / AdsDDSetFieldProperty
// ---------------------------------------------------------------------------

UNSIGNED32 AdsDDGetFieldProperty(ADSHANDLE hConn, UNSIGNED8* pucTable,
                                  UNSIGNED8* pucField, UNSIGNED16 usProp,
                                  void* pBuf, UNSIGNED16* pusLen) {
    if (pusLen == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    Connection* c = conn_from_handle(hConn);
    UNSIGNED16 cap = *pusLen;
    if (pBuf != nullptr && cap > 0) std::memset(pBuf, 0, cap);

    auto put_str = [&](const std::string& s) -> UNSIGNED32 {
        UNSIGNED16 n = static_cast<UNSIGNED16>(
            std::min<std::size_t>(s.size(), cap));
        if (pBuf != nullptr && n > 0) std::memcpy(pBuf, s.data(), n);
        *pusLen = static_cast<UNSIGNED16>(s.size());
        return ok();
    };

    auto* dd = (c != nullptr && c->has_dd()) ? c->dd() : nullptr;
    if (dd == nullptr) {
#if defined(OPENADS_WITH_POSTGRESQL)
        // No Advantage Data Dictionary on this connection: a SQL backend with a
        // live catalog (PostgreSQL information_schema) can still answer REQUIRED
        // (=> nullable) and DEFAULT per field. The value uses the same encoding as
        // the dictionary path ("T"/"F" for REQUIRED, the literal for DEFAULT) so
        // the caller need not know which source provided it.
        if (usProp == ADS_DD_FIELD_REQUIRED || usProp == ADS_DD_FIELD_DEFAULT) {
            if (auto* pc = state().registry
                    .lookup<openads::sql_backend::PostgresConnection>(
                        hConn, HandleKind::PostgresConnection)) {
                openads::sql_backend::PostgresTable probe;
                probe.name = openads::abi::to_internal(pucTable, 0);
                auto fr = pc->describe_table(&probe);
                if (fr) {
                    const auto want = openads::abi::to_internal(pucField, 0);
                    for (const auto& fd2 : fr.value()) {
                        if (fd2.name.size() == want.size() &&
                            std::equal(fd2.name.begin(), fd2.name.end(),
                                       want.begin(), [](char a, char b) {
                                           return std::toupper((unsigned char) a) ==
                                                  std::toupper((unsigned char) b);
                                       })) {
                            return usProp == ADS_DD_FIELD_REQUIRED
                                ? put_str(fd2.nullable ? "F" : "T")
                                : put_str(fd2.default_value);
                        }
                    }
                }
            }
        }
#endif
        *pusLen = 0;
        return ok();
    }

    auto alias  = openads::abi::to_internal(pucTable, 0);
    auto field  = openads::abi::to_internal(pucField, 0);
    if (!dd->has_alias(alias)) {
        *pusLen = 0;
        return fail(static_cast<int>(openads::AE_TABLE_NOT_FOUND), alias.c_str());
    }

    auto put_u16 = [&](std::uint16_t v) -> UNSIGNED32 {
        if (pBuf != nullptr && cap >= 2) {
            auto* b = static_cast<std::uint8_t*>(pBuf);
            b[0] = static_cast<std::uint8_t>(v & 0xFFu);
            b[1] = static_cast<std::uint8_t>(v >> 8);
        }
        *pusLen = 2;
        return ok();
    };

    // Structural properties: open the table briefly, read the field descriptor.
    if (usProp == ADS_DD_FIELD_NAME   || usProp == ADS_DD_FIELD_TYPE  ||
        usProp == ADS_DD_FIELD_LENGTH || usProp == ADS_DD_FIELD_DECIMAL) {

        std::string rel = dd->resolve(alias);
        auto th = c->open_table(rel, openads::engine::TableType::Cdx,
                                 openads::engine::OpenMode::Read);
        if (!th) { *pusLen = 0; return fail(th.error()); }

        auto* tbl = c->lookup_table(th.value());
        UNSIGNED32 ret = ok();
        if (tbl != nullptr) {
            std::int32_t fi = tbl->field_index(field);
            if (fi < 0) {
                ret = fail(static_cast<int>(openads::AE_NO_FILE_FOUND), field.c_str());
            } else {
                const auto& fd = tbl->field_descriptor(static_cast<std::uint16_t>(fi));
                if (usProp == ADS_DD_FIELD_NAME) {
                    ret = put_str(fd.name);
                } else if (usProp == ADS_DD_FIELD_TYPE) {
                    ret = put_u16(map_field_type(fd.type));
                } else if (usProp == ADS_DD_FIELD_LENGTH) {
                    ret = put_u16(static_cast<std::uint16_t>(fd.length));
                } else if (usProp == ADS_DD_FIELD_DECIMAL) {
                    ret = put_u16(static_cast<std::uint16_t>(fd.decimals));
                }
            }
        }
        c->close_table(th.value());
        return ret;
    }

    // Stored properties: read from field_props_.
    std::string key;
    switch (usProp) {
        case ADS_DD_FIELD_REQUIRED:        key = "required"; break;
        case ADS_DD_FIELD_DEFAULT:         key = "default"; break;
        case ADS_DD_FIELD_VALIDATION_RULE: key = "rule"; break;
        case ADS_DD_FIELD_VALIDATION_MSG:  key = "msg"; break;
        case ADS_DD_FIELD_COMMENT:         key = "comment"; break;
        default:
            *pusLen = 0;
            return fail(openads::AE_FUNCTION_NOT_AVAILABLE, "");
    }
    return put_str(dd->get_field_property(alias, field, key));
}

UNSIGNED32 AdsDDSetFieldProperty(ADSHANDLE hConn, UNSIGNED8* pucTable,
                                  UNSIGNED8* pucField, UNSIGNED16 usProp,
                                  void* pBuf, UNSIGNED16 usLen) {
    auto* dd = dd_from_handle(hConn);
    if (dd == nullptr) return ok();
    auto alias = openads::abi::to_internal(pucTable, 0);
    auto field = openads::abi::to_internal(pucField, 0);
    if (!dd->has_alias(alias))
        return fail(static_cast<int>(openads::AE_TABLE_NOT_FOUND), alias.c_str());

    // Structural props are read-only.
    if (usProp >= ADS_DD_FIELD_NAME && usProp <= ADS_DD_FIELD_DECIMAL)
        return fail(openads::AE_FUNCTION_NOT_AVAILABLE, "read-only field prop");

    std::string key;
    switch (usProp) {
        case ADS_DD_FIELD_REQUIRED:        key = "required"; break;
        case ADS_DD_FIELD_DEFAULT:         key = "default"; break;
        case ADS_DD_FIELD_VALIDATION_RULE: key = "rule"; break;
        case ADS_DD_FIELD_VALIDATION_MSG:  key = "msg"; break;
        case ADS_DD_FIELD_COMMENT:         key = "comment"; break;
        default:
            return fail(openads::AE_FUNCTION_NOT_AVAILABLE, "");
    }
    std::string val = pBuf && usLen > 0
        ? std::string(static_cast<const char*>(pBuf), usLen) : std::string{};
    auto r = dd->set_field_property(alias, field, key, val);
    if (!r) return fail(r.error());
    return ok();
}

// ---------------------------------------------------------------------------
// AdsDDGetIndexProperty / AdsDDSetIndexProperty
// ---------------------------------------------------------------------------

UNSIGNED32 AdsDDGetIndexProperty(ADSHANDLE hConn, UNSIGNED8* pucTable,
                                  UNSIGNED8* pucIndex, UNSIGNED16 usProp,
                                  void* pBuf, UNSIGNED16* pusLen) {
    if (pusLen == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    Connection* c = conn_from_handle(hConn);
    UNSIGNED16 cap = *pusLen;
    if (pBuf != nullptr && cap > 0) std::memset(pBuf, 0, cap);

    auto put_str = [&](const std::string& s) -> UNSIGNED32 {
        UNSIGNED16 n = static_cast<UNSIGNED16>(std::min<std::size_t>(s.size(), cap));
        if (pBuf != nullptr && n > 0) std::memcpy(pBuf, s.data(), n);
        *pusLen = static_cast<UNSIGNED16>(s.size());
        return ok();
    };
    auto put_u16 = [&](std::uint16_t v) -> UNSIGNED32 {
        if (pBuf != nullptr && cap >= 2) {
            auto* b = static_cast<std::uint8_t*>(pBuf);
            b[0] = static_cast<std::uint8_t>(v & 0xFFu);
            b[1] = static_cast<std::uint8_t>(v >> 8);
        }
        *pusLen = 2; return ok();
    };

    // Look in index_bindings() for a binding with this tag name that
    // belongs to a table matching pucTable (alias or open table).
    auto idx_name = openads::abi::to_internal(pucIndex, 0);
    auto tbl_name = pucTable ? openads::abi::to_internal(pucTable, 0) : std::string{};

    ADSHANDLE idx_h = 0;
    openads::drivers::IIndex* found_idx = nullptr;
    std::string found_path;

    for (auto& [h, b] : index_bindings()) {
        if (b.tag_name != idx_name) continue;
        // If a table name is given, check that the binding's table is that table.
        if (!tbl_name.empty() && c != nullptr) {
            if (!c->owns_table_ptr(b.table)) continue;
        }
        idx_h     = h;
        found_path = b.path;
        found_idx  = iindex_for_handle(h);
        break;
    }

    if (idx_h == 0) {
        *pusLen = 0;
        return fail(openads::AE_NO_FILE_FOUND, idx_name.c_str());
    }
    if (found_idx == nullptr) { *pusLen = 0; return ok(); }

    switch (usProp) {
        case ADS_DD_INDEX_FILE_NAME:  return put_str(found_path);
        case ADS_DD_INDEX_EXPR:       return put_str(found_idx->expression());
        case ADS_DD_INDEX_UNIQUE:     return put_u16(found_idx->unique()     ? 1 : 0);
        case ADS_DD_INDEX_DESCENDING: return put_u16(found_idx->descending() ? 1 : 0);
        case ADS_DD_INDEX_CONDITION:  return put_str({});  // not exposed by IIndex
        case ADS_DD_INDEX_KEY_LENGTH: return put_u16(found_idx->key_length());
        case ADS_DD_INDEX_TYPE:       return put_u16(0);   // CDX=0 (not exposed by IIndex)
        default:
            *pusLen = 0;
            return fail(openads::AE_FUNCTION_NOT_AVAILABLE, "");
    }
}

UNSIGNED32 AdsDDSetIndexProperty(ADSHANDLE /*hConn*/, UNSIGNED8* /*pucTable*/,
                                  UNSIGNED8* /*pucIndex*/, UNSIGNED16 /*usProp*/,
                                  void* /*pBuf*/, UNSIGNED16 /*usLen*/) {
    return fail(openads::AE_FUNCTION_NOT_AVAILABLE, "AdsDDSetIndexProperty");
}

// ---------------------------------------------------------------------------
// AdsDDCreateTrigger / AdsDDDropTrigger / AdsDDGet/SetTriggerProperty
// ---------------------------------------------------------------------------

UNSIGNED32 AdsDDCreateTrigger(ADSHANDLE hConn, UNSIGNED8* pucName,
                               UNSIGNED8* pucTable, UNSIGNED32 ulType,
                               UNSIGNED32 /*ulOptions*/, UNSIGNED8* pucContainer,
                               UNSIGNED8* pucProcedure, UNSIGNED32 ulPriority) {
    auto* dd = dd_from_handle(hConn);
    if (dd == nullptr) return ok();
    openads::engine::DataDict::TriggerEntry e;
    e.name        = openads::abi::to_internal(pucName, 0);
    e.table_alias = openads::abi::to_internal(pucTable, 0);
    // Decode combined ADS trigger type constant into internal (event_mask, timing).
    // ADS_BEFORE_INSERT=0x0001 ADS_AFTER_INSERT=0x0002 ADS_BEFORE_UPDATE=0x0004
    // ADS_AFTER_UPDATE=0x0008 ADS_BEFORE_DELETE=0x0010 ADS_AFTER_DELETE=0x0020
    // ADS_INSTEAD_OF_INSERT=0x0040 ADS_INSTEAD_OF_UPDATE=0x0080
    // ADS_INSTEAD_OF_DELETE=0x0100
    switch (ulType) {
        case 0x0001: e.event_mask = 1; e.timing = 1; break; // BEFORE INSERT
        case 0x0002: e.event_mask = 1; e.timing = 4; break; // AFTER INSERT
        case 0x0040: e.event_mask = 1; e.timing = 2; break; // INSTEAD OF INSERT
        case 0x0004: e.event_mask = 2; e.timing = 1; break; // BEFORE UPDATE
        case 0x0008: e.event_mask = 2; e.timing = 4; break; // AFTER UPDATE
        case 0x0080: e.event_mask = 2; e.timing = 2; break; // INSTEAD OF UPDATE
        case 0x0010: e.event_mask = 3; e.timing = 1; break; // BEFORE DELETE
        case 0x0020: e.event_mask = 3; e.timing = 4; break; // AFTER DELETE
        case 0x0100: e.event_mask = 3; e.timing = 2; break; // INSTEAD OF DELETE
        default:
            return fail(openads::AE_INTERNAL_ERROR, "unknown trigger type");
    }
    e.container   = pucContainer ? openads::abi::to_internal(pucContainer, 0) : "";
    e.procedure   = pucProcedure ? openads::abi::to_internal(pucProcedure, 0) : "";
    e.priority    = ulPriority;
    e.enabled     = true;
    if (e.name.empty() || e.table_alias.empty())
        return fail(openads::AE_INTERNAL_ERROR, "trigger name/table empty");
    auto r = dd->create_trigger(e);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsDDDropTrigger(ADSHANDLE hConn, UNSIGNED8* pucName) {
    auto* dd = dd_from_handle(hConn);
    if (dd == nullptr) return ok();
    auto name = openads::abi::to_internal(pucName, 0);
    if (!dd->has_trigger(name))
        return fail(static_cast<int>(openads::AE_NO_FILE_FOUND), name.c_str());
    auto r = dd->drop_trigger(name);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsDDGetTriggerProperty(ADSHANDLE hConn, UNSIGNED8* pucName,
                                    UNSIGNED16 usProp, void* pBuf,
                                    UNSIGNED16* pusLen) {
    if (pusLen == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    auto* dd = dd_from_handle(hConn);
    UNSIGNED16 cap = *pusLen;
    if (pBuf != nullptr && cap > 0) std::memset(pBuf, 0, cap);
    if (dd == nullptr) { *pusLen = 0; return ok(); }
    auto name = openads::abi::to_internal(pucName, 0);
    const auto* ep = dd->find_trigger(name);
    if (ep == nullptr) {
        *pusLen = 0;
        return fail(static_cast<int>(openads::AE_NO_FILE_FOUND), name.c_str());
    }
    const auto& e = *ep;

    auto put_str = [&](const std::string& s) -> UNSIGNED32 {
        UNSIGNED16 n = static_cast<UNSIGNED16>(std::min<std::size_t>(s.size(), cap));
        if (pBuf != nullptr && n > 0) std::memcpy(pBuf, s.data(), n);
        *pusLen = static_cast<UNSIGNED16>(s.size());
        return ok();
    };
    auto put_u32 = [&](std::uint32_t v) -> UNSIGNED32 {
        if (pBuf != nullptr && cap >= 4) {
            auto* b = static_cast<std::uint8_t*>(pBuf);
            b[0]=static_cast<std::uint8_t>(v&0xFF); b[1]=static_cast<std::uint8_t>((v>>8)&0xFF); b[2]=static_cast<std::uint8_t>((v>>16)&0xFF); b[3]=static_cast<std::uint8_t>((v>>24)&0xFF);
        }
        *pusLen = 4; return ok();
    };

    switch (usProp) {
        case ADS_DD_TRIGGER_TABLE:
        case 1408: /* ADS_DD_TRIG_TABLENAME (SAP ACE) */
            return put_str(e.table_alias);
        case ADS_DD_TRIGGER_EVENT:
        {
            // ABI callers (AdsDDCreateTrigger/SetTriggerProperty) use combined ADS type constants.
            std::uint32_t combined = 0;
            if      (e.event_mask==1 && e.timing==1) combined = 0x0001;
            else if (e.event_mask==1 && e.timing==4) combined = 0x0002;
            else if (e.event_mask==1 && e.timing==2) combined = 0x0040;
            else if (e.event_mask==2 && e.timing==1) combined = 0x0004;
            else if (e.event_mask==2 && e.timing==4) combined = 0x0008;
            else if (e.event_mask==2 && e.timing==2) combined = 0x0080;
            else if (e.event_mask==3 && e.timing==1) combined = 0x0010;
            else if (e.event_mask==3 && e.timing==4) combined = 0x0020;
            else if (e.event_mask==3 && e.timing==2) combined = 0x0100;
            return put_u32(combined);
        }
        case 1401: /* ADS_DD_TRIG_EVENT_TYPE (SAP ACE) — 1=INSERT 2=UPDATE 3=DELETE */
            return put_u32(static_cast<std::uint32_t>(e.event_mask));
        case 1402: /* ADS_DD_TRIG_TIMING (SAP ACE extension) */
            return put_u32(e.timing);
        case ADS_DD_TRIGGER_CONTAINER:
        case 1404: /* ADS_DD_TRIG_CONTAINER (SAP ACE) */
            return put_str(e.container);
        case ADS_DD_TRIGGER_PROC_NAME:
        case 1405: /* ADS_DD_TRIG_FUNCTION_NAME (SAP ACE) */
            return put_str(e.procedure);
        case ADS_DD_TRIGGER_ENABLED:   return put_u32(e.enabled ? 1u : 0u);
        case ADS_DD_TRIGGER_PRIORITY:
        case 1406: /* ADS_DD_TRIG_PRIORITY (SAP ACE) */
            return put_u32(e.priority);
        case ADS_DD_TRIGGER_COMMENT:   return put_str(e.comment);
        case 1407: /* ADS_DD_TRIG_OPTIONS (SAP ACE): 0x01=WANT_VALUES 0x02=WANT_MEMOS 0x04=NO_TRANSACTION */
            return put_u32(e.options);
        default: *pusLen = 0; return fail(openads::AE_FUNCTION_NOT_AVAILABLE, "");
    }
}

UNSIGNED32 AdsDDSetTriggerProperty(ADSHANDLE hConn, UNSIGNED8* pucName,
                                    UNSIGNED16 usProp, void* pBuf,
                                    UNSIGNED16 usLen) {
    auto* dd = dd_from_handle(hConn);
    if (dd == nullptr) return ok();
    auto name = openads::abi::to_internal(pucName, 0);
    auto* ep = dd->find_trigger(name);
    if (ep == nullptr)
        return fail(static_cast<int>(openads::AE_NO_FILE_FOUND), name.c_str());
    auto& e = *ep;
    std::string val = pBuf && usLen > 0
        ? std::string(static_cast<const char*>(pBuf), usLen) : std::string{};
    // Helper: parse val as uint32 (numeric string or 4-byte LE binary).
    auto parse_u32 = [&](std::uint32_t& out) {
        if (usLen >= 4 && (val[0] < '0' || val[0] > '9')) {
            // Binary 4-byte LE
            auto* b = static_cast<const std::uint8_t*>(pBuf);
            out = static_cast<std::uint32_t>(b[0]) | (static_cast<std::uint32_t>(b[1]) << 8) | (static_cast<std::uint32_t>(b[2]) << 16) | (static_cast<std::uint32_t>(b[3]) << 24);
        } else if (!val.empty()) {
            try { out = static_cast<std::uint32_t>(std::stoul(val)); } catch (...) {}
        }
    };
    switch (usProp) {
        case ADS_DD_TRIGGER_TABLE:
        case 1408: /* ADS_DD_TRIG_TABLENAME (SAP ACE) */
            e.table_alias = val; break;
        case ADS_DD_TRIGGER_EVENT:
        case 1401: /* ADS_DD_TRIG_EVENT_TYPE (SAP ACE) — decode combined ADS constant */
        {
            std::uint32_t combined = 0;
            parse_u32(combined);
            switch (combined) {
                case 0x0001: e.event_mask=1; e.timing=1; break;
                case 0x0002: e.event_mask=1; e.timing=4; break;
                case 0x0040: e.event_mask=1; e.timing=2; break;
                case 0x0004: e.event_mask=2; e.timing=1; break;
                case 0x0008: e.event_mask=2; e.timing=4; break;
                case 0x0080: e.event_mask=2; e.timing=2; break;
                case 0x0010: e.event_mask=3; e.timing=1; break;
                case 0x0020: e.event_mask=3; e.timing=4; break;
                case 0x0100: e.event_mask=3; e.timing=2; break;
                default: break;
            }
            break;
        }
        case 1402: /* timing: 1=BEFORE 2=INSTEAD_OF 4=AFTER */
            parse_u32(e.timing); break;
        case ADS_DD_TRIGGER_CONTAINER:
        case 1404: /* ADS_DD_TRIG_CONTAINER (SAP ACE) */
            e.container   = val; break;
        case ADS_DD_TRIGGER_PROC_NAME:
        case 1405: /* ADS_DD_TRIG_FUNCTION_NAME (SAP ACE) */
            e.procedure   = val; break;
        case ADS_DD_TRIGGER_COMMENT:   e.comment     = val; break;
        case ADS_DD_TRIGGER_ENABLED: {
            std::uint32_t v = 0;
            parse_u32(v);
            // Also accept "T"/"F", "True"/"False", "1"/"0" as strings
            if (val == "T" || val == "True"  || val == "true"  || val == "yes" || val == "Yes") v = 1;
            if (val == "F" || val == "False" || val == "false" || val == "no"  || val == "No")  v = 0;
            e.enabled = (v != 0);
            break;
        }
        case ADS_DD_TRIGGER_PRIORITY:
        case 1406: /* ADS_DD_TRIG_PRIORITY (SAP ACE) */
            parse_u32(e.priority); break;
        case 1407: /* ADS_DD_TRIG_OPTIONS (SAP ACE): 0x01=WANT_VALUES 0x02=WANT_MEMOS 0x04=NO_TRANSACTION */
            parse_u32(e.options); break;
        default:
            return fail(openads::AE_FUNCTION_NOT_AVAILABLE, "");
    }
    auto r = dd->save();
    if (!r) return fail(r.error());
    return ok();
}

// ---------------------------------------------------------------------------
// AdsDDCreateProcedure / AdsDDDropProcedure / AdsDDGet/SetProcProperty
// ---------------------------------------------------------------------------

UNSIGNED32 AdsDDCreateProcedure(ADSHANDLE hConn, UNSIGNED8* pucName,
                                 UNSIGNED8* pucContainer,
                                 UNSIGNED8* pucProcName,
                                 UNSIGNED32 /*ulInvokeOption*/,
                                 UNSIGNED8* pucInParams,
                                 UNSIGNED8* pucOutParams,
                                 UNSIGNED8* pucComments) {
    auto* dd = dd_from_handle(hConn);
    if (dd == nullptr) return ok();
    openads::engine::DataDict::ProcEntry e;
    e.name          = openads::abi::to_internal(pucName, 0);
    e.container     = pucContainer ? openads::abi::to_internal(pucContainer, 0) : "";
    e.procedure     = pucProcName  ? openads::abi::to_internal(pucProcName,  0) : "";
    e.input_params  = pucInParams  ? openads::abi::to_internal(pucInParams,  0) : "";
    e.output_params = pucOutParams ? openads::abi::to_internal(pucOutParams, 0) : "";
    e.comment       = pucComments  ? openads::abi::to_internal(pucComments,  0) : "";
    if (e.name.empty())
        return fail(openads::AE_INTERNAL_ERROR, "proc name empty");
    auto r = dd->create_proc(e);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsDDDropProcedure(ADSHANDLE hConn, UNSIGNED8* pucName) {
    auto* dd = dd_from_handle(hConn);
    if (dd == nullptr) return ok();
    auto name = openads::abi::to_internal(pucName, 0);
    if (!dd->has_proc(name))
        return fail(static_cast<int>(openads::AE_NO_FILE_FOUND), name.c_str());
    auto r = dd->drop_proc(name);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsDDGetProcProperty(ADSHANDLE hConn, UNSIGNED8* pucName,
                                 UNSIGNED16 usProp, void* pBuf,
                                 UNSIGNED16* pusLen) {
    if (pusLen == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    auto* dd = dd_from_handle(hConn);
    UNSIGNED16 cap = *pusLen;
    if (pBuf != nullptr && cap > 0) std::memset(pBuf, 0, cap);
    if (dd == nullptr) { *pusLen = 0; return ok(); }
    auto name = openads::abi::to_internal(pucName, 0);
    if (!dd->has_proc(name)) {
        *pusLen = 0;
        return fail(static_cast<int>(openads::AE_NO_FILE_FOUND), name.c_str());
    }
    const auto& e = dd->procs().at(name);
    auto put_str = [&](const std::string& s) -> UNSIGNED32 {
        UNSIGNED16 n = static_cast<UNSIGNED16>(std::min<std::size_t>(s.size(), cap));
        if (pBuf != nullptr && n > 0) std::memcpy(pBuf, s.data(), n);
        *pusLen = static_cast<UNSIGNED16>(s.size());
        return ok();
    };
    switch (usProp) {
        case ADS_DD_PROC_INPUT:
        case 800: /* ADS_DD_PROC_INPUT (SAP ACE) */
            return put_str(e.input_params);
        case ADS_DD_PROC_OUTPUT:
        case 801: /* ADS_DD_PROC_OUTPUT (SAP ACE) */
            return put_str(e.output_params);
        case ADS_DD_PROC_CONTAINER:
        case 802: /* ADS_DD_PROC_DLL_NAME (SAP ACE) */
            return put_str(e.container);
        case ADS_DD_PROC_PROC_NAME:
        case 803: /* ADS_DD_PROC_DLL_FUNCTION_NAME (SAP ACE) */
            return put_str(e.procedure);
        case ADS_DD_PROC_COMMENT:
        case 805: /* ADS_DD_PROC_SCRIPT (SAP ACE) */
            return put_str(e.comment);
        default: *pusLen = 0; return fail(openads::AE_FUNCTION_NOT_AVAILABLE, "");
    }
}

UNSIGNED32 AdsDDSetProcProperty(ADSHANDLE hConn, UNSIGNED8* pucName,
                                 UNSIGNED16 usProp, void* pBuf,
                                 UNSIGNED16 usLen) {
    auto* dd = dd_from_handle(hConn);
    if (dd == nullptr) return ok();
    auto name = openads::abi::to_internal(pucName, 0);
    if (!dd->has_proc(name))
        return fail(static_cast<int>(openads::AE_NO_FILE_FOUND), name.c_str());
    auto& e = dd->procs().at(name);
    std::string val = pBuf && usLen > 0
        ? std::string(static_cast<const char*>(pBuf), usLen) : std::string{};
    switch (usProp) {
        case ADS_DD_PROC_INPUT:
        case 800: /* ADS_DD_PROC_INPUT (SAP ACE) */
            e.input_params  = val; break;
        case ADS_DD_PROC_OUTPUT:
        case 801: /* ADS_DD_PROC_OUTPUT (SAP ACE) */
            e.output_params = val; break;
        case ADS_DD_PROC_CONTAINER:
        case 802: /* ADS_DD_PROC_DLL_NAME (SAP ACE) */
            e.container     = val; break;
        case ADS_DD_PROC_PROC_NAME:
        case 803: /* ADS_DD_PROC_DLL_FUNCTION_NAME (SAP ACE) */
            e.procedure     = val; break;
        case ADS_DD_PROC_COMMENT:
        case 805: /* ADS_DD_PROC_SCRIPT (SAP ACE) */
            e.comment       = val; break;
        default: return fail(openads::AE_FUNCTION_NOT_AVAILABLE, "");
    }
    auto r = dd->save();
    if (!r) return fail(r.error());
    return ok();
}

// ---------------------------------------------------------------------------
// AdsDDCreateFunction / AdsDDDropFunction / AdsDDGet/SetFunctionProperty
// Property codes: 700=body(implementation), 701=input_params, 702=return_type,
//                 703=container, 704=comment
// ---------------------------------------------------------------------------

UNSIGNED32 AdsDDCreateFunction(ADSHANDLE hConn, UNSIGNED8* pucName,
                                UNSIGNED8* pucContainer,
                                UNSIGNED8* pucImplementation,
                                UNSIGNED8* pucRetType,
                                UNSIGNED8* pucInParams,
                                UNSIGNED8* pucComment) {
    auto* dd = dd_from_handle(hConn);
    if (dd == nullptr) return ok();
    openads::engine::DataDict::FunctionEntry e;
    e.name           = openads::abi::to_internal(pucName, 0);
    e.container      = pucContainer      ? openads::abi::to_internal(pucContainer,      0) : "";
    e.implementation = pucImplementation ? openads::abi::to_internal(pucImplementation, 0) : "";
    e.return_type    = pucRetType        ? openads::abi::to_internal(pucRetType,        0) : "";
    e.input_params   = pucInParams       ? openads::abi::to_internal(pucInParams,       0) : "";
    e.comment        = pucComment        ? openads::abi::to_internal(pucComment,        0) : "";
    if (e.name.empty())
        return fail(openads::AE_INTERNAL_ERROR, "function name empty");
    auto r = dd->create_function(e);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsDDDropFunction(ADSHANDLE hConn, UNSIGNED8* pucName) {
    auto* dd = dd_from_handle(hConn);
    if (dd == nullptr) return ok();
    auto name = openads::abi::to_internal(pucName, 0);
    if (!dd->has_function(name))
        return fail(static_cast<int>(openads::AE_NO_FILE_FOUND), name.c_str());
    auto r = dd->drop_function(name);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsDDGetFunctionProperty(ADSHANDLE hConn, UNSIGNED8* pucName,
                                     UNSIGNED16 usProp, void* pBuf,
                                     UNSIGNED16* pusLen) {
    if (pusLen == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    auto* dd = dd_from_handle(hConn);
    UNSIGNED16 cap = *pusLen;
    if (pBuf != nullptr && cap > 0) std::memset(pBuf, 0, cap);
    if (dd == nullptr) { *pusLen = 0; return ok(); }
    auto name = openads::abi::to_internal(pucName, 0);
    if (!dd->has_function(name)) {
        *pusLen = 0;
        return fail(static_cast<int>(openads::AE_NO_FILE_FOUND), name.c_str());
    }
    const auto& e = dd->functions().at(name);
    auto put_str = [&](const std::string& s) -> UNSIGNED32 {
        UNSIGNED16 n = static_cast<UNSIGNED16>(std::min<std::size_t>(s.size(), cap));
        if (pBuf != nullptr && n > 0) std::memcpy(pBuf, s.data(), n);
        *pusLen = static_cast<UNSIGNED16>(s.size());
        return ok();
    };
    switch (usProp) {
        case 700: return put_str(e.implementation);
        case 701: return put_str(e.input_params);
        case 702: return put_str(e.return_type);
        case 703: return put_str(e.container);
        case 704: return put_str(e.comment);
        default: *pusLen = 0; return fail(openads::AE_FUNCTION_NOT_AVAILABLE, "");
    }
}

UNSIGNED32 AdsDDSetFunctionProperty(ADSHANDLE hConn, UNSIGNED8* pucName,
                                     UNSIGNED16 usProp, void* pBuf,
                                     UNSIGNED16 usLen) {
    auto* dd = dd_from_handle(hConn);
    if (dd == nullptr) return ok();
    auto name = openads::abi::to_internal(pucName, 0);
    if (!dd->has_function(name))
        return fail(static_cast<int>(openads::AE_NO_FILE_FOUND), name.c_str());
    auto& e = dd->functions().at(name);
    std::string val = pBuf && usLen > 0
        ? std::string(static_cast<const char*>(pBuf), usLen) : std::string{};
    switch (usProp) {
        case 700: e.implementation = val; break;
        case 701: e.input_params   = val; break;
        case 702: e.return_type    = val; break;
        case 703: e.container      = val; break;
        case 704: e.comment        = val; break;
        default: return fail(openads::AE_FUNCTION_NOT_AVAILABLE, "");
    }
    auto r = dd->save();
    if (!r) return fail(r.error());
    return ok();
}

// ---------------------------------------------------------------------------
// AdsDDCreateView / AdsDDDropView / AdsDDGet/SetViewProperty
// ---------------------------------------------------------------------------

UNSIGNED32 AdsDDCreateView(ADSHANDLE hConn, UNSIGNED8* pucName,
                            UNSIGNED8* pucComments, UNSIGNED8* pucSQL) {
    auto* dd = dd_from_handle(hConn);
    if (dd == nullptr) return ok();
    openads::engine::DataDict::ViewEntry e;
    e.name    = openads::abi::to_internal(pucName, 0);
    e.comment = pucComments ? openads::abi::to_internal(pucComments, 0) : "";
    e.sql     = pucSQL      ? openads::abi::to_internal(pucSQL,      0) : "";
    if (e.name.empty())
        return fail(openads::AE_INTERNAL_ERROR, "view name empty");
    auto r = dd->create_view(e);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsDDDropView(ADSHANDLE hConn, UNSIGNED8* pucName) {
    auto* dd = dd_from_handle(hConn);
    if (dd == nullptr) return ok();
    auto name = openads::abi::to_internal(pucName, 0);
    if (!dd->has_view(name))
        return fail(static_cast<int>(openads::AE_NO_FILE_FOUND), name.c_str());
    auto r = dd->drop_view(name);
    if (!r) return fail(r.error());
    return ok();
}

// ---------------------------------------------------------------------------
// SAP ACE aliases not yet covered above
// AdsDDAddView / AdsDDRemoveView — thin aliases for Create/Drop.
// AdsDDGetPermissions / AdsDDGrantPermission / AdsDDRevokePermission —
//   fine-grained object ACL helpers used by the php_advantage extension.
// ---------------------------------------------------------------------------

static const char* dd_type_name_from_code(UNSIGNED16 code) {
    switch (code) {
        case  1: return "Table";
        case  6: return "View";
        case 10: return "StoredProc";
        case 18: return "Function";
        default: return "Table";
    }
}

UNSIGNED32 AdsDDAddView(ADSHANDLE hConn, UNSIGNED8* pucName,
                         UNSIGNED8* pucComments, UNSIGNED8* pucSQL) {
    return AdsDDCreateView(hConn, pucName, pucComments, pucSQL);
}

UNSIGNED32 AdsDDRemoveView(ADSHANDLE hConn, UNSIGNED8* pucName) {
    return AdsDDDropView(hConn, pucName);
}

UNSIGNED32 AdsDDGetPermissions(ADSHANDLE hConn,
                                UNSIGNED8*  pucGrantee,
                                UNSIGNED16  usObjectType,
                                UNSIGNED8*  pucObjectName,
                                UNSIGNED8*  /*pucParentName*/,
                                UNSIGNED16  /*usGetInherited*/,
                                UNSIGNED32* pulPermissions) {
    if (pulPermissions == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    *pulPermissions = 0;
    auto* dd = dd_from_handle(hConn);
    if (dd == nullptr) return ok();
    auto grantee = openads::abi::to_internal(pucGrantee,     0);
    auto objname = openads::abi::to_internal(pucObjectName,  0);
    auto objtype = dd_type_name_from_code(usObjectType);
    for (const auto& pe : dd->permissions()) {
        if (pe.grantee == grantee && pe.object_type == objtype && pe.object_name == objname) {
            *pulPermissions = pe.bitmask;
            break;
        }
    }
    return ok();
}

UNSIGNED32 AdsDDGrantPermission(ADSHANDLE  hConn,
                                 UNSIGNED16  usObjectType,
                                 UNSIGNED8*  pucObjectName,
                                 UNSIGNED8*  /*pucParentName*/,
                                 UNSIGNED8*  pucGrantee,
                                 UNSIGNED32  ulPermissions) {
    auto* dd = dd_from_handle(hConn);
    if (dd == nullptr) return ok();
    auto objname = openads::abi::to_internal(pucObjectName, 0);
    auto grantee = openads::abi::to_internal(pucGrantee,    0);
    auto objtype = dd_type_name_from_code(usObjectType);
    auto r = dd->grant_permission(objtype, objname, grantee, ulPermissions);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsDDRevokePermission(ADSHANDLE  hConn,
                                  UNSIGNED16  usObjectType,
                                  UNSIGNED8*  pucObjectName,
                                  UNSIGNED8*  pucParentName,
                                  UNSIGNED8*  pucGrantee,
                                  UNSIGNED32  /*ulPermissions*/) {
    // Revoke by granting a zero bitmask (deactivates existing record).
    // The caller typically follows with a fresh AdsDDGrantPermission call.
    return AdsDDGrantPermission(hConn, usObjectType, pucObjectName,
                                pucParentName, pucGrantee, 0);
}

UNSIGNED32 AdsDDGetViewProperty(ADSHANDLE hConn, UNSIGNED8* pucName,
                                 UNSIGNED16 usProp, void* pBuf,
                                 UNSIGNED16* pusLen) {
    if (pusLen == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    auto* dd = dd_from_handle(hConn);
    UNSIGNED16 cap = *pusLen;
    if (pBuf != nullptr && cap > 0) std::memset(pBuf, 0, cap);
    if (dd == nullptr) { *pusLen = 0; return ok(); }
    auto name = openads::abi::to_internal(pucName, 0);
    if (!dd->has_view(name)) {
        *pusLen = 0;
        return fail(static_cast<int>(openads::AE_NO_FILE_FOUND), name.c_str());
    }
    const auto& e = dd->views().at(name);
    auto put_str = [&](const std::string& s) -> UNSIGNED32 {
        UNSIGNED16 n = static_cast<UNSIGNED16>(std::min<std::size_t>(s.size(), cap));
        if (pBuf != nullptr && n > 0) std::memcpy(pBuf, s.data(), n);
        *pusLen = static_cast<UNSIGNED16>(s.size());
        return ok();
    };
    switch (usProp) {
        case ADS_DD_VIEW_STMT:    return put_str(e.sql);
        case ADS_DD_VIEW_COMMENT: return put_str(e.comment);
        default: *pusLen = 0; return fail(openads::AE_FUNCTION_NOT_AVAILABLE, "");
    }
}

UNSIGNED32 AdsDDSetViewProperty(ADSHANDLE hConn, UNSIGNED8* pucName,
                                 UNSIGNED16 usProp, void* pBuf,
                                 UNSIGNED16 usLen) {
    auto* dd = dd_from_handle(hConn);
    if (dd == nullptr) return ok();
    auto name = openads::abi::to_internal(pucName, 0);
    if (!dd->has_view(name))
        return fail(static_cast<int>(openads::AE_NO_FILE_FOUND), name.c_str());
    auto& e = dd->views().at(name);
    std::string val = pBuf && usLen > 0
        ? std::string(static_cast<const char*>(pBuf), usLen) : std::string{};
    switch (usProp) {
        case ADS_DD_VIEW_STMT:    e.sql     = val; break;
        case ADS_DD_VIEW_COMMENT: e.comment = val; break;
        default: return fail(openads::AE_FUNCTION_NOT_AVAILABLE, "");
    }
    auto r = dd->save();
    if (!r) return fail(r.error());
    return ok();
}

// ---------------------------------------------------------------------------
// AdsDDGetRefIntegrityProperty / AdsDDSetRefIntegrityProperty
// ---------------------------------------------------------------------------

UNSIGNED32 AdsDDGetRefIntegrityProperty(ADSHANDLE hConn, UNSIGNED8* pucName,
                                         UNSIGNED16 usProp, void* pBuf,
                                         UNSIGNED16* pusLen) {
    if (pusLen == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    auto* dd = dd_from_handle(hConn);
    UNSIGNED16 cap = *pusLen;
    if (pBuf != nullptr && cap > 0) std::memset(pBuf, 0, cap);
    if (dd == nullptr) { *pusLen = 0; return ok(); }
    auto name = openads::abi::to_internal(pucName, 0);
    const auto& ri = dd->ri();
    auto it = ri.find(name);
    if (it == ri.end()) {
        *pusLen = 0;
        return fail(static_cast<int>(openads::AE_NO_FILE_FOUND), name.c_str());
    }
    const auto& e = it->second;
    auto put_str = [&](const std::string& s) -> UNSIGNED32 {
        UNSIGNED16 n = static_cast<UNSIGNED16>(std::min<std::size_t>(s.size(), cap));
        if (pBuf != nullptr && n > 0) std::memcpy(pBuf, s.data(), n);
        *pusLen = static_cast<UNSIGNED16>(s.size());
        return ok();
    };
    auto put_u32 = [&](std::uint32_t v) -> UNSIGNED32 {
        if (pBuf != nullptr && cap >= 4) {
            auto* b = static_cast<std::uint8_t*>(pBuf);
            b[0]=static_cast<uint8_t>(v&0xFF); b[1]=static_cast<uint8_t>((v>>8)&0xFF); b[2]=static_cast<uint8_t>((v>>16)&0xFF); b[3]=static_cast<uint8_t>((v>>24)&0xFF);
        }
        *pusLen = 4; return ok();
    };

    switch (usProp) {
        case ADS_DD_RI_PARENT:      return put_str(e.parent);
        case ADS_DD_RI_CHILD:       return put_str(e.child);
        case ADS_DD_RI_PARENT_TAG:  return put_str(e.parent_tag);
        case ADS_DD_RI_CHILD_TAG:   return put_str(e.child_tag);
        case ADS_DD_RI_UPDATE_RULE: {
            try { return put_u32(static_cast<std::uint32_t>(std::stoul(e.update_opt))); }
            catch (...) { return put_u32(0); }
        }
        case ADS_DD_RI_DELETE_RULE: {
            try { return put_u32(static_cast<std::uint32_t>(std::stoul(e.delete_opt))); }
            catch (...) { return put_u32(0); }
        }
        case ADS_DD_RI_FAIL_TABLE:  return put_str(e.fail_table);
        default: *pusLen = 0; return fail(openads::AE_FUNCTION_NOT_AVAILABLE, "");
    }
}

UNSIGNED32 AdsDDSetRefIntegrityProperty(ADSHANDLE hConn, UNSIGNED8* pucName,
                                         UNSIGNED16 usProp, void* pBuf,
                                         UNSIGNED16 usLen) {
    auto* dd = dd_from_handle(hConn);
    if (dd == nullptr) return ok();
    auto name = openads::abi::to_internal(pucName, 0);
    auto& ri = dd->ri();
    auto it = ri.find(name);
    if (it == ri.end())
        return fail(static_cast<int>(openads::AE_NO_FILE_FOUND), name.c_str());
    auto& e = it->second;
    std::string val = pBuf && usLen > 0
        ? std::string(static_cast<const char*>(pBuf), usLen) : std::string{};
    switch (usProp) {
        case ADS_DD_RI_PARENT:      e.parent     = val; break;
        case ADS_DD_RI_CHILD:       e.child      = val; break;
        case ADS_DD_RI_PARENT_TAG:  e.parent_tag = val; break;
        case ADS_DD_RI_CHILD_TAG:   e.child_tag  = val; break;
        case ADS_DD_RI_UPDATE_RULE:
            if (pBuf && usLen >= 4) {
                auto* b = static_cast<const std::uint8_t*>(pBuf);
                std::uint32_t v = static_cast<std::uint32_t>(b[0]) | (static_cast<std::uint32_t>(b[1]) << 8) | (static_cast<std::uint32_t>(b[2]) << 16) | (static_cast<std::uint32_t>(b[3]) << 24);
                e.update_opt = std::to_string(v);
            }
            break;
        case ADS_DD_RI_DELETE_RULE:
            if (pBuf && usLen >= 4) {
                auto* b = static_cast<const std::uint8_t*>(pBuf);
                std::uint32_t v = static_cast<std::uint32_t>(b[0]) | (static_cast<std::uint32_t>(b[1]) << 8) | (static_cast<std::uint32_t>(b[2]) << 16) | (static_cast<std::uint32_t>(b[3]) << 24);
                e.delete_opt = std::to_string(v);
            }
            break;
        case ADS_DD_RI_FAIL_TABLE:  e.fail_table = val; break;
        default: return fail(openads::AE_FUNCTION_NOT_AVAILABLE, "");
    }
    auto r = dd->save();
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsCreateFTSIndex(ADSHANDLE   hTable,
                             UNSIGNED8*  pucFileName,
                             UNSIGNED8*  pucTag,
                             UNSIGNED8*  pucField,
                             UNSIGNED32  /*ulPageSize*/,
                             UNSIGNED32  ulMinWordLen,
                             UNSIGNED32  ulMaxWordLen,
                             UNSIGNED16  usUseDefaultDelim,
                             UNSIGNED8*  pucDelimiters,
                             UNSIGNED16  usUseDefaultNoise,
                             UNSIGNED8*  pucNoiseWords,
                             UNSIGNED16  /*usUseDefaultDrop*/,
                             UNSIGNED8*  /*pucDropChars*/,
                             UNSIGNED16  /*usUseDefaultConditionals*/,
                             UNSIGNED8*  /*pucConditionalChars*/,
                             UNSIGNED8*  /*pucReserved1*/,
                             UNSIGNED8*  /*pucReserved2*/,
                             UNSIGNED32  /*ulOptions*/) {
    Table* t = get_table(hTable);
    if (!t || pucTag == nullptr || pucField == nullptr) {
        return fail(openads::AE_INTERNAL_ERROR, "AdsCreateFTSIndex: null arg");
    }
    auto tag   = openads::abi::to_internal(pucTag, 0);
    auto field = openads::abi::to_internal(pucField, 0);

    namespace fs = std::filesystem;
    fs::path p;
    if (pucFileName != nullptr && pucFileName[0] != '\0') {
        p = openads::abi::to_internal(pucFileName, 0);
    } else {
        // Compound auto-open form: file lives next to the table with
        // the table's stem and a `.fts` extension.
        p = fs::path(t->path()).replace_extension(".fts");
    }
    if (!p.is_absolute()) {
        fs::path tdir = fs::path(t->path()).parent_path();
        p = tdir / p;
    }
    if (!p.has_extension()) p.replace_extension(".fts");

    auto opts = build_fts_options(ulMinWordLen, ulMaxWordLen,
                                  usUseDefaultDelim, pucDelimiters,
                                  usUseDefaultNoise, pucNoiseWords);
    auto r = openads::engine::Fts::create(*t, p.string(), tag, field, opts);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsGetNumIndexes(ADSHANDLE hTable, UNSIGNED16* pusCount) {
    if (pusCount == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    if (auto* rt = get_remote_table(hTable)) {
        auto r = rt->conn->get_num_indexes(rt->id);
        if (!r) return fail(r.error());
        *pusCount = r.value();
        return ok();
    }
    Table* t = get_table(hTable);
    if (!t || pusCount == nullptr) {
        return fail(openads::AE_INTERNAL_ERROR, "");
    }
    UNSIGNED16 n = 0;
    for (auto& [_, b] : index_bindings()) {
        if (b.table == t) ++n;
    }
    *pusCount = n;
    return ok();
}

UNSIGNED32 AdsGetIndexHandle(ADSHANDLE hTable, UNSIGNED8* pucName,
                             ADSHANDLE* phIndex) {
    if (phIndex == nullptr) {
        return fail(openads::AE_INTERNAL_ERROR, "");
    }
    auto name = openads::abi::to_internal(pucName, 0);
    // Strip trailing whitespace + nulls (rddads space-pads tag names
    // up to ADS_MAX_TAG_NAME before passing them to us).
    while (!name.empty() && (name.back() == ' ' || name.back() == '\0')) {
        name.pop_back();
    }
#if defined(OPENADS_WITH_POSTGRESQL)
    // SQL backend (postgresql): resolve an already-open PG index by its
    // tag/column name. AdsOpenIndex creates the PostgresIndex handle; this
    // is the by-name lookup path the ORM uses after opening. A PG handle has
    // no native Table*, so without this branch the function fell through to
    // get_table() below and errored for every PG table. Match the tag the
    // way postgres_open_index derives it (strip path + extension).
    if (auto* st = get_postgres_table(hTable)) {
        std::string tag = name;
        const auto dot = tag.find_last_of("./\\");
        if (dot != std::string::npos) tag = tag.substr(dot + 1);
        const auto dot2 = tag.find('.');
        if (dot2 != std::string::npos) tag = tag.substr(0, dot2);
        for (auto& [h, si] : postgres_indexes_map()) {
            if (si && si->parent == st && si->column == tag) {
                *phIndex = h;
                return ok();
            }
        }
        return fail(openads::AE_INTERNAL_ERROR, "index name not found");
    }
#endif
    Table* t = get_table(hTable);
    if (!t) {
        return fail(openads::AE_INTERNAL_ERROR, "");
    }
    for (auto& [h, b] : index_bindings()) {
        if (b.table == t && b.tag_name == name) { *phIndex = h; return ok(); }
    }
    return fail(openads::AE_INTERNAL_ERROR, "index name not found");
}


// Authoritative ordinal sequence of index-binding handles for table `t`.
// For a CDX bag this is the file's struct-tag (creation) order — what
// ADS / rddads expose through ORDSETFOCUS(N) and OrdNumber(); for NTX it
// is handle-id order. AdsGetIndexHandleByOrder and
// AdsGetIndexOrderByHandle MUST share this so they stay exact inverses.
//
// Harbour rddads' INDEX command (default fAll && !fAdditive) calls
// ORDLSTCLEAR before each AdsCreateIndex61, wiping every binding we held
// for the table even though the on-disk CDX bag still lists all prior
// tags. So any tag in the active CDX that lost its binding is lazily
// re-bound here.
//
// extern "C++": this file's ACE exports live in an extern "C" block, but
// this internal helper returns a C++ UDT (std::vector) — give it C++
// linkage so MSVC doesn't warn C4190.
extern "C++" std::vector<ADSHANDLE> ordered_index_handles_for(Table* t) {
    auto& m   = index_bindings();
    auto& act = active_binding_for();
    std::string bag_path;
    auto act_it = act.find(t);
    if (act_it != act.end()) {
        auto bit = m.find(act_it->second);
        if (bit != m.end()) bag_path = bit->second.path;
    }
    // No active binding (e.g. every tag parked after an ORDLSTCLEAR): fall
    // back to any CDX binding's bag so we can still recover file order.
    if (bag_path.empty()) {
        for (auto& [h, b] : m) {
            if (b.table == t && path_ends_with_ci(b.path, ".cdx")) {
                bag_path = b.path;
                break;
            }
        }
    }
    std::vector<ADSHANDLE> ordered;
    if (!bag_path.empty()
        && path_ends_with_ci(bag_path, ".cdx")) {
        auto tags = openads::drivers::cdx::CdxIndex::list_tags(bag_path);
        if (tags) {
            for (const auto& tag : tags.value()) {
                ADSHANDLE found = 0;
                for (auto& [h, b] : m) {
                    if (b.table == t && b.tag_name == tag) {
                        found = h; break;
                    }
                }
                if (!found) {
                    auto sub = std::make_unique<openads::drivers::cdx::CdxIndex>();
                    if (auto r = sub->open_named(bag_path,
                            openads::drivers::IndexOpenMode::Shared,
                            tag); !r) continue;
                    mark_cdx_key_encoding(t, sub.get());
                    ADSHANDLE nh = next_index_handle();
                    openads::drivers::IIndex* raw = sub.get();
                    m[nh] = IndexBinding{t, tag, std::move(sub), bag_path};
                    t->register_extra_index_view(raw);
                    found = nh;
                }
                ordered.push_back(found);
            }
        }
    }
    if (ordered.empty()) {
        // Fall back to handle-id ordering for non-CDX (NTX) cases.
        for (auto& [h, b] : index_bindings()) {
            if (b.table == t) ordered.push_back(h);
        }
        std::sort(ordered.begin(), ordered.end());
    }
    return ordered;
}

UNSIGNED32 AdsGetIndexHandleByOrder(ADSHANDLE hTable, UNSIGNED16 usOrder,
                                    ADSHANDLE* phIndex) {
    Table* t = get_table(hTable);
    if (!t || phIndex == nullptr) {
        return fail(openads::AE_INTERNAL_ERROR, "");
    }
    std::vector<ADSHANDLE> ordered = ordered_index_handles_for(t);
    if (ordered.empty()) {
        return fail(openads::AE_INTERNAL_ERROR, "no active index");
    }
    if (usOrder == 0 || usOrder > ordered.size()) {
        // Fall back to first entry on out-of-range ordinal so legacy
        // callers that pass 0 or 1 still resolve to *some* index.
        *phIndex = ordered.front();
    } else {
        *phIndex = ordered[usOrder - 1];
    }
    return ok();
}

UNSIGNED32 AdsGetIndexExpr(ADSHANDLE hIndex, UNSIGNED8* pucBuf,
                           UNSIGNED16* pusBufLen) {
    auto& m = index_bindings();
    auto it = m.find(hIndex);
    if (it == m.end()) {
        return fail(openads::AE_INTERNAL_ERROR, "unknown index");
    }
    // Pull the expression from whichever IIndex carries it: parked
    // binding has its own; the active one's IIndex sits on the Table.
    std::string expr;
    if (it->second.parked) {
        expr = it->second.parked->expression();
    } else if (it->second.table && it->second.table->order()
            && it->second.table->order()->index()) {
        expr = it->second.table->order()->index()->expression();
    }
    openads::abi::copy_to_caller(pucBuf, pusBufLen, expr);
    return ok();
}

UNSIGNED32 AdsGetIndexName(ADSHANDLE hIndex, UNSIGNED8* pucBuf,
                           UNSIGNED16* pusBufLen) {
    auto& m = index_bindings();
    auto it = m.find(hIndex);
    if (it == m.end()) {
        return fail(openads::AE_INTERNAL_ERROR, "unknown index");
    }
    openads::abi::copy_to_caller(pucBuf, pusBufLen, it->second.tag_name);
    return ok();
}

UNSIGNED32 AdsSetIndexDirection(ADSHANDLE hIndex, UNSIGNED16 usDir) {
    auto& s = state();
    std::lock_guard<std::recursive_mutex> lk(s.mu);
    auto it = index_bindings().find(hIndex);
    if (it == index_bindings().end()) {
        return fail(openads::AE_INTERNAL_ERROR, "unknown index");
    }
    Table* t = it->second.table;
    if (t == nullptr) return fail(openads::AE_INTERNAL_ERROR, "no table");
    (void)activate_binding(hIndex);
    auto* o = t->order();
    if (o == nullptr) {
        return fail(openads::AE_INTERNAL_ERROR, "no active order");
    }
    // ACE convention: usDir == 0 (ADS_ASCENDING) → forward; non-zero
    // (ADS_DESCENDING) → reverse.
    const_cast<openads::engine::Order*>(o)->set_descending_traverse(
        usDir != 0);
    return ok();
}

// ACE / rddads signature: 6 args.
//   AdsSeek(hIndex, pucKey, u16KeyLen, u16KeyType, u16SeekType, &u16Found)
//
// u16KeyType  : ADS_STRINGKEY / ADS_NUMERICKEY / ... — describes
//               pucKey's encoding. We accept whatever the caller sends
//               and pass the bytes through as-is; the engine compares
//               on raw bytes after padding to the index's key length.
// u16SeekType : 0 = exact (hard), 1 = soft. Bit 1 = AfterKey.
// rddads' hb_adsUpdateAreaFlags asks AdsIsFound after every seek to
// decide whether Found() should report .T. — return the flag the
// engine set inside seek_key.
UNSIGNED32 AdsIsFound(ADSHANDLE hTable, UNSIGNED16* pbFound) {
    if (pbFound == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    if (auto* rt = get_remote_table(hTable)) {
        // M12.21 option C — serve Found() locally when a nav/seek op set
        // it (the common scan case: Skip clears it), saving a round-trip
        // on every step. Fall back to the server only when uncached.
        if (rt->found_cached) { *pbFound = rt->current_found ? 1 : 0; return ok(); }
        auto r = rt->conn->is_found(rt->id);
        if (!r) return fail(r.error());
        *pbFound = r.value() ? 1 : 0;
        return ok();
    }
    if (auto* ops = openads::abi::backend_table_ops_for(hTable))
        if (ops->is_found) return ops->is_found(hTable, pbFound);
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    if (pbFound != nullptr) *pbFound = t->last_seek_found() ? 1 : 0;
    return ok();
}

UNSIGNED32 AdsSeek(ADSHANDLE hIndex,
                   UNSIGNED8* pucKey,
                   UNSIGNED16 u16KeyLen,
                   UNSIGNED16 u16KeyType,
                   UNSIGNED16 u16SeekType,
                   UNSIGNED16* pbFound) {
#if defined(OPENADS_WITH_SQLITE)
    if (auto* si = get_sqlite_index(hIndex)) {
        if (si->parent == nullptr || si->parent->conn == nullptr) {
            return fail(openads::AE_INTERNAL_ERROR, "sqlite index orphan");
        }
        std::string key(reinterpret_cast<const char*>(pucKey), u16KeyLen);
        const bool soft = (u16SeekType & 1u) != 0;
        si->parent->row_valid = false;
        auto r = si->parent->conn->seek_index(
            si->parent, si->column, key, soft, /*last=*/false);
        if (!r) return fail(r.error());
        const bool found = r.value();
        si->last_seek_found = found;
        if (pbFound) *pbFound = found ? 1 : 0;
        (void)u16KeyType;
        return ok();
    }
#endif
#if defined(OPENADS_WITH_ODBC)
    if (auto* si = get_odbc_index(hIndex)) {
        if (si->parent == nullptr || si->parent->conn == nullptr) {
            return fail(openads::AE_INTERNAL_ERROR, "odbc index orphan");
        }
        std::string key(reinterpret_cast<const char*>(pucKey), u16KeyLen);
        const bool soft = (u16SeekType & 1u) != 0;
        si->parent->row_valid = false;
        auto r = si->parent->conn->seek_index(
            si->parent, si->column, si->expr_kind, key, soft,
            /*last=*/false);
        if (!r) return fail(r.error());
        const bool found = r.value();
        si->last_seek_found = found;
        if (pbFound) *pbFound = found ? 1 : 0;
        (void)u16KeyType;
        return ok();
    }
#endif
#if defined(OPENADS_WITH_FIREBIRD)
    if (auto* si = get_firebird_index(hIndex)) {
        if (si->parent == nullptr || si->parent->conn == nullptr) {
            return fail(openads::AE_INTERNAL_ERROR, "firebird index orphan");
        }
        std::string key(reinterpret_cast<const char*>(pucKey), u16KeyLen);
        const bool soft = (u16SeekType & 1u) != 0;
        si->parent->row_valid = false;
        auto r = si->parent->conn->seek_index(
            si->parent, si->column, si->expr_kind, key, soft,
            /*last=*/false);
        if (!r) return fail(r.error());
        const bool found = r.value();
        si->last_seek_found = found;
        if (pbFound) *pbFound = found ? 1 : 0;
        (void)u16KeyType;
        return ok();
    }
#endif
#if defined(OPENADS_WITH_MARIADB)
    if (auto* si = get_maria_index(hIndex)) {
        if (si->parent == nullptr || si->parent->conn == nullptr) {
            return fail(openads::AE_INTERNAL_ERROR, "mariadb index orphan");
        }
        std::string key(reinterpret_cast<const char*>(pucKey), u16KeyLen);
        const bool soft = (u16SeekType & 1u) != 0;
        si->parent->row_valid = false;
        auto r = si->parent->conn->seek_index(
            si->parent, si->column, key, soft, /*last=*/false);
        if (!r) return fail(r.error());
        const bool found = r.value();
        si->last_seek_found = found;
        if (pbFound) *pbFound = found ? 1 : 0;
        (void)u16KeyType;
        return ok();
    }
#endif
#if defined(OPENADS_WITH_POSTGRESQL)
    if (auto* si = get_postgres_index(hIndex)) {
        if (si->parent == nullptr || si->parent->conn == nullptr) {
            return fail(openads::AE_INTERNAL_ERROR, "postgres index orphan");
        }
        std::string key(reinterpret_cast<const char*>(pucKey), u16KeyLen);
        const bool soft = (u16SeekType & 1u) != 0;
        si->parent->row_valid = false;
        auto r = si->parent->conn->seek_index(
            si->parent, si->column, key, soft, /*last=*/false);
        if (!r) return fail(r.error());
        const bool found = r.value();
        si->last_seek_found = found;
        if (pbFound) *pbFound = found ? 1 : 0;
        (void)u16KeyType;
        return ok();
    }
#endif
    if (auto* ri = get_remote_index(hIndex)) {
        std::string key(reinterpret_cast<const char*>(pucKey),
                        u16KeyLen);
        if (ri->parent) ri->parent->row_valid = false;   // M12.17
        auto r = ri->conn->seek(ri->id, key,
            static_cast<std::uint8_t>(u16SeekType),
            /*last=*/0);
        if (!r) return fail(r.error());
        if (pbFound) *pbFound = r.value().hit;
        if (ri->parent) {                            // M12.21 option C
            ri->parent->found_cached  = true;
            ri->parent->current_found = (r.value().hit != 0);
        }
        (void)u16KeyType;
        return ok();
    }
    Table* t = table_for_index(hIndex);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown index");
    std::string key;
    (void)u16KeyType;
    // Numeric seek keys arrive as sizeof(double) raw IEEE bytes. The ADS SDK
    // names this ADS_DOUBLEKEY (2), but Harbour's rddads tags a numeric dbSeek
    // with the field DATA type (ADS_STRING==4 in this ABI) — so gating on
    // u16KeyType==ADS_DOUBLEKEY missed EVERY rddads numeric seek (the key fell
    // through as 8 raw double bytes and never matched the stored ASCII key).
    // Detect a double key by length + a numeric (ASCII-stored) active index
    // field instead, and convert to the same right-aligned ASCII the index
    // holds. Character / non-numeric indexes keep the raw-bytes path.
    auto* dk_idx = (t->order() != nullptr) ? t->order()->index() : nullptr;
    // Strip the `ALIAS->` qualifier the way the write side does
    // (evaluate_index_expr -> strip_alias_qualifiers) so a tag built from
    // `FIELD->ID` still resolves the field.
    std::int32_t dk_fidx = (dk_idx != nullptr)
        ? t->field_index(
              openads::engine::strip_alias_qualifiers(dk_idx->expression()))
        : -1;
    bool dk_numeric = false;
    if (dk_fidx >= 0) {
        auto dkt = t->field_descriptor(
            static_cast<std::uint16_t>(dk_fidx)).type;
        dk_numeric = (dkt == openads::drivers::DbfFieldType::Numeric ||
                      dkt == openads::drivers::DbfFieldType::Float);
    }
    const bool dk_foxnum =
        dk_idx != nullptr &&
        dk_idx->key_encoding() == openads::drivers::KeyEncoding::FoxNumeric &&
        u16KeyLen == sizeof(double);
    const bool dk_ntxnum =
        dk_idx != nullptr &&
        dk_idx->key_encoding() == openads::drivers::KeyEncoding::NtxNumeric &&
        u16KeyLen == sizeof(double);
    if (dk_foxnum) {
        // Numeric CDX key: encode the seek value the same 8-byte
        // order-preserving way the stored keys were written.
        double dv = 0;
        std::memcpy(&dv, pucKey, sizeof(double));
        key = openads::engine::fox_numeric_key(dv);
    } else if (dk_ntxnum) {
        // Numeric NTX key: encode the seek value the same native zero-padded
        // (negatives byte-complemented) way the stored keys were written.
        double dv = 0;
        std::memcpy(&dv, pucKey, sizeof(double));
        key = openads::engine::ntx_numeric_key(dv, dk_idx->key_length(),
                                               dk_idx->key_decimals());
    } else if (dk_idx != nullptr && dk_numeric && u16KeyLen == sizeof(double)) {
        double dv = 0;
        std::memcpy(&dv, pucKey, sizeof(double));
        std::uint16_t klen = dk_idx->key_length();
        // Format width matches the FIELD width (eg N,10,0 -> 10), not a stale
        // index key_length (eg INDEX-on-empty-table), so the right-aligned
        // padding equals what evaluate_index_expr wrote at build/sync time.
        const auto& fd = t->field_descriptor(
            static_cast<std::uint16_t>(dk_fidx));
        std::uint16_t dec   = static_cast<std::uint16_t>(fd.decimals);
        std::uint16_t fmt_w = (fd.length > 0)
            ? static_cast<std::uint16_t>(fd.length) : klen;
        // Buffer holds the widest valid xBase field width (255) plus
        // sign, decimal point and NUL. snprintf is length-bounded and we
        // assign only what it actually produced (clamped to the buffer),
        // so an out-of-range fmt_w can never overread the stack buffer.
        char buf[264];
        int n = (dec > 0)
            ? std::snprintf(buf, sizeof(buf), "%*.*f",
                            static_cast<int>(fmt_w),
                            static_cast<int>(dec), dv)
            : std::snprintf(buf, sizeof(buf), "%*.0f",
                            static_cast<int>(fmt_w), dv);
        std::size_t take = (n < 0)
            ? 0u
            : std::min<std::size_t>(static_cast<std::size_t>(n),
                                    sizeof(buf) - 1);
        // Pad to klen with trailing spaces (matches how
        // evaluate_index_expr right-pads the field's raw bytes).
        key.assign(buf, take);
        if (key.size() < klen) key.append(klen - key.size(), ' ');
    } else {
        key.assign(reinterpret_cast<const char*>(pucKey),
                   static_cast<std::size_t>(u16KeyLen));
    }
    bool soft = (u16SeekType & 0x01) != 0;
    bool zero_length_key = (u16KeyLen == 0);
    // Clipper / DBFCDX quirk: a zero-length seek key (`DBSEEK( "" )`)
    // matches every record. Skip the underlying B+tree compare and
    // walk straight to the first / last record (depending on the
    // SeekLast retry latch). The seek_last_retry_latch is set only
    // by AdsSeekLast — meaning the caller is bFindLast=TRUE, in
    // which case we want the LAST entry in ASC traversal direction
    // (DBFCDX hb_cdxSeek with fLast = TRUE returns the same record
    // as soft + skip-to-end-of-key-group; the empty key has only
    // one "group" so first/last collapse to first under our walk).
    if (zero_length_key && t->record_count() > 0) {
        // DBFCDX: empty key always matches; soft-or-hard, asc-only.
        // For DESCEND orders the empty key falls through to the
        // regular seek path so DBFCDX's CDX_MAX_REC_NUM / fLast
        // inversion takes effect — `DBSEEK("",T,T)` on a DESCEND
        // tag is expected to miss (Eof), not land on the bottom.
        bool desc = (t->order() != nullptr &&
                     t->order()->descending_traverse());
        if (!desc) {
            auto gt = t->goto_top();
            if (!gt) return fail(gt.error());
            if (pbFound != nullptr) *pbFound = 1;
            t->set_last_seek_found(true);
            snapshot_ri_pks(t);
            return ok();
        }
    }
    auto r = t->seek_key(key, soft);
    if (!r) return fail(r.error());
    bool found = r.value();
    if (pbFound != nullptr) *pbFound = found ? 1 : 0;
    snapshot_ri_pks(t);
    return ok();
}

UNSIGNED32 AdsSeekLast(ADSHANDLE hIndex,
                       UNSIGNED8* pucKey,
                       UNSIGNED16 u16KeyLen,
                       UNSIGNED16 u16KeyType,
                       UNSIGNED16* pbFound) {
#if defined(OPENADS_WITH_SQLITE)
    if (auto* si = get_sqlite_index(hIndex)) {
        if (si->parent == nullptr || si->parent->conn == nullptr) {
            return fail(openads::AE_INTERNAL_ERROR, "sqlite index orphan");
        }
        std::string key(reinterpret_cast<const char*>(pucKey), u16KeyLen);
        si->parent->row_valid = false;
        auto r = si->parent->conn->seek_index(
            si->parent, si->column, key, /*soft=*/false, /*last=*/true);
        if (!r) return fail(r.error());
        const bool found = r.value();
        si->last_seek_found = found;
        if (pbFound) *pbFound = found ? 1 : 0;
        (void)u16KeyType;
        return ok();
    }
#endif
#if defined(OPENADS_WITH_ODBC)
    if (auto* si = get_odbc_index(hIndex)) {
        if (si->parent == nullptr || si->parent->conn == nullptr) {
            return fail(openads::AE_INTERNAL_ERROR, "odbc index orphan");
        }
        std::string key(reinterpret_cast<const char*>(pucKey), u16KeyLen);
        si->parent->row_valid = false;
        auto r = si->parent->conn->seek_index(
            si->parent, si->column, si->expr_kind, key,
            /*soft=*/false, /*last=*/true);
        if (!r) return fail(r.error());
        const bool found = r.value();
        si->last_seek_found = found;
        if (pbFound) *pbFound = found ? 1 : 0;
        (void)u16KeyType;
        return ok();
    }
#endif
#if defined(OPENADS_WITH_FIREBIRD)
    if (auto* si = get_firebird_index(hIndex)) {
        if (si->parent == nullptr || si->parent->conn == nullptr) {
            return fail(openads::AE_INTERNAL_ERROR, "firebird index orphan");
        }
        std::string key(reinterpret_cast<const char*>(pucKey), u16KeyLen);
        si->parent->row_valid = false;
        auto r = si->parent->conn->seek_index(
            si->parent, si->column, si->expr_kind, key,
            /*soft=*/false, /*last=*/true);
        if (!r) return fail(r.error());
        const bool found = r.value();
        si->last_seek_found = found;
        if (pbFound) *pbFound = found ? 1 : 0;
        (void)u16KeyType;
        return ok();
    }
#endif
#if defined(OPENADS_WITH_MARIADB)
    if (auto* si = get_maria_index(hIndex)) {
        if (si->parent == nullptr || si->parent->conn == nullptr) {
            return fail(openads::AE_INTERNAL_ERROR, "mariadb index orphan");
        }
        std::string key(reinterpret_cast<const char*>(pucKey), u16KeyLen);
        si->parent->row_valid = false;
        auto r = si->parent->conn->seek_index(
            si->parent, si->column, key, /*soft=*/false, /*last=*/true);
        if (!r) return fail(r.error());
        const bool found = r.value();
        si->last_seek_found = found;
        if (pbFound) *pbFound = found ? 1 : 0;
        (void)u16KeyType;
        return ok();
    }
#endif
#if defined(OPENADS_WITH_POSTGRESQL)
    if (auto* si = get_postgres_index(hIndex)) {
        if (si->parent == nullptr || si->parent->conn == nullptr) {
            return fail(openads::AE_INTERNAL_ERROR, "postgres index orphan");
        }
        std::string key(reinterpret_cast<const char*>(pucKey), u16KeyLen);
        si->parent->row_valid = false;
        auto r = si->parent->conn->seek_index(
            si->parent, si->column, key, /*soft=*/false, /*last=*/true);
        if (!r) return fail(r.error());
        const bool found = r.value();
        si->last_seek_found = found;
        if (pbFound) *pbFound = found ? 1 : 0;
        (void)u16KeyType;
        return ok();
    }
#endif
    if (auto* ri = get_remote_index(hIndex)) {
        std::string key(reinterpret_cast<const char*>(pucKey),
                        u16KeyLen);
        if (ri->parent) ri->parent->row_valid = false;   // M12.17
        auto r = ri->conn->seek(ri->id, key,
            /*soft=*/0,
            /*last=*/1);
        if (!r) return fail(r.error());
        if (pbFound) *pbFound = r.value().hit;
        if (ri->parent) {                            // M12.21 option C
            ri->parent->found_cached  = true;
            ri->parent->current_found = (r.value().hit != 0);
        }
        (void)u16KeyType;
        return ok();
    }
    // Latch the "we're inside an AdsSeekLast cycle" flag. rddads'
    // adsSeek retries via AdsSeek soft + AdsSkip(-1) when this hard
    // seek misses; AdsSeek consults the latch to suppress its
    // empty-key always-found quirk. AdsSkip clears the latch.
    seek_last_retry_latch() = true;
    auto rc = AdsSeek(hIndex, pucKey, u16KeyLen, u16KeyType,
                      /*soft*/ 0, pbFound);
    return rc;
}

// SAP / rddads signature: 5 args.
//   AdsSetScope(hIndex, usScope, pucScope, usLen, usDataType)
//
// usLen       : explicit scope-key length. Required because typed
//               keys (ADS_DOUBLEKEY, ADS_RAWKEY) legally contain
//               embedded NULs that strlen() would truncate.
// usDataType  : ADS_STRINGKEY / ADS_RAWKEY / ADS_DOUBLEKEY / ... —
//               matches AdsSeek's u16KeyType. We mirror AdsSeek's
//               ADS_DOUBLEKEY -> ASCII-padded conversion so a scope
//               set with a double compares apples-to-apples against
//               the index's stored key bytes.
UNSIGNED32 AdsSetScope(ADSHANDLE hIndex, UNSIGNED16 usScope,
                       UNSIGNED8* pucScope, UNSIGNED16 usLen,
                       UNSIGNED16 usDataType) {
    if (auto* ri = get_remote_index(hIndex)) {
        std::string key = pucScope
            ? openads::abi::to_internal(pucScope, usLen) : std::string();
        auto r = ri->conn->set_scope(ri->id, usScope, key,
                                     usDataType);
        if (!r) return fail(r.error());
        return ok();
    }
    Table* t = table_for_index(hIndex);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown index");
    std::string key;
    if (usDataType == ADS_DOUBLEKEY && usLen == sizeof(double) &&
        pucScope != nullptr &&
        t->order() != nullptr && t->order()->index() != nullptr) {
        double dv = 0;
        std::memcpy(&dv, pucScope, sizeof(double));
        auto* idx = t->order()->index();
        std::uint16_t klen = idx->key_length();
        std::uint16_t fmt_w = klen;
        std::uint16_t dec = 0;
        if (idx->key_encoding() ==
            openads::drivers::KeyEncoding::FoxNumeric) {
            // Numeric CDX scope key: same 8-byte order-preserving encoding
            // as the stored keys, not ASCII.
            key = openads::engine::fox_numeric_key(dv);
            auto rsc = t->set_scope(usScope == ADS_TOP, key);
            if (!rsc) return fail(rsc.error());
            return ok();
        }
        // Strip the `ALIAS->` qualifier the way the write side does
        // (evaluate_index_expr -> strip_alias_qualifiers); otherwise a
        // tag built from `FIELD->ID` never resolves the field, fmt_w
        // stays at the stale key_length, and the numeric seek key is
        // padded to a different width than the stored key.
        std::int32_t fidx = t->field_index(
            openads::engine::strip_alias_qualifiers(idx->expression()));
        if (fidx >= 0) {
            const auto& fd = t->field_descriptor(
                static_cast<std::uint16_t>(fidx));
            dec = static_cast<std::uint16_t>(fd.decimals);
            if (fd.length > 0)
                fmt_w = static_cast<std::uint16_t>(fd.length);
        }
        // Length-bounded format + clamped assign: an out-of-range fmt_w
        // can never overread the buffer (buf sized for the widest valid
        // xBase field width plus sign, separator and NUL).
        char buf[264];
        int n = (dec > 0)
            ? std::snprintf(buf, sizeof(buf), "%*.*f",
                            static_cast<int>(fmt_w),
                            static_cast<int>(dec), dv)
            : std::snprintf(buf, sizeof(buf), "%*.0f",
                            static_cast<int>(fmt_w), dv);
        std::size_t take = (n < 0)
            ? 0u
            : std::min<std::size_t>(static_cast<std::size_t>(n),
                                    sizeof(buf) - 1);
        key.assign(buf, take);
        if (key.size() < klen) key.append(klen - key.size(), ' ');
    } else {
        key = pucScope
            ? openads::abi::to_internal(pucScope, usLen) : std::string();
    }
    auto r = t->set_scope(usScope == ADS_TOP, key);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsClearScope(ADSHANDLE hIndex, UNSIGNED16 usScope) {
    if (auto* ri = get_remote_index(hIndex)) {
        auto r = ri->conn->clear_scope(ri->id, usScope);
        if (!r) return fail(r.error());
        return ok();
    }
    Table* t = table_for_index(hIndex);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown index");
    auto r = t->clear_scope(usScope == ADS_TOP);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsGetScope(ADSHANDLE hIndex, UNSIGNED16 usScope,
                       UNSIGNED8* pucBuf, UNSIGNED16* pusLen) {
    Table* t = table_for_index(hIndex);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown index");
    auto s = t->get_scope(usScope == ADS_TOP);
    openads::abi::copy_to_caller(pucBuf, pusLen, s.value_or(""));
    return ok();
}

UNSIGNED32 AdsPackTable(ADSHANDLE hTable) {
    if (auto* rt = get_remote_table(hTable)) {
        rt->row_valid        = false;               // M12.17/19
        rt->rec_count_cached = false;
        auto r = rt->conn->pack_table(rt->id);
        if (!r) return fail(r.error());
        return ok();
    }
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    auto r = t->pack();
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsZapTable(ADSHANDLE hTable) {
    if (auto* rt = get_remote_table(hTable)) {
        rt->row_valid        = false;               // M12.17/19
        rt->rec_count_cached = false;
        auto r = rt->conn->zap_table(rt->id);
        if (!r) return fail(r.error());
        return ok();
    }
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    auto r = t->zap();
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsCopyTable(ADSHANDLE   hHandle,
                        UNSIGNED16  /*usFilterOption*/,
                        UNSIGNED8*  pucFile) {
    if (pucFile == nullptr) {
        return fail(openads::AE_INTERNAL_ERROR, "null target");
    }
    Table* t = get_table(hHandle);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    if (!t->driver()) return fail(openads::AE_INTERNAL_ERROR, "no driver");

    namespace fs = std::filesystem;
    auto raw  = openads::abi::to_internal(pucFile, 0);
    fs::path dst(raw);
    if (!dst.is_absolute()) {
        fs::path src_dir = fs::path(t->path()).parent_path();
        dst = src_dir / dst;
    }
    if (!dst.has_extension()) dst.replace_extension(".dbf");

    // Build a new DBF that mirrors the source schema. Copy live
    // records (deleted rows skipped — filter options beyond
    // ADS_RESPECTFILTERS land later).
    const auto& src_fields = t->driver()->fields();
    if (src_fields.empty()) {
        return fail(openads::AE_INTERNAL_ERROR, "source has no fields");
    }
    std::uint16_t header_len = static_cast<std::uint16_t>(
        32 + 32 * src_fields.size() + 1);
    std::uint16_t rec_len = t->driver()->record_length();

    std::vector<std::uint8_t> file;
    std::vector<std::uint8_t> hdr(32, 0);
    hdr[0]  = 0x03;
    stamp_dbf_header_today(hdr.data());
    hdr[8]  = static_cast<std::uint8_t>(header_len & 0xFFu);
    hdr[9]  = static_cast<std::uint8_t>((header_len >> 8) & 0xFFu);
    hdr[10] = static_cast<std::uint8_t>(rec_len & 0xFFu);
    hdr[11] = static_cast<std::uint8_t>((rec_len >> 8) & 0xFFu);
    file = hdr;
    for (const auto& f : src_fields) {
        std::vector<std::uint8_t> fd(32, 0);
        std::size_t n = std::min<std::size_t>(f.name.size(), 10);
        std::memcpy(fd.data(), f.name.data(), n);
        fd[11] = static_cast<std::uint8_t>(f.raw_type ? f.raw_type : 'C');
        fd[16] = static_cast<std::uint8_t>(f.length);
        fd[17] = f.decimals;
        file.insert(file.end(), fd.begin(), fd.end());
    }
    file.push_back(0x0D);

    // Walk source records, append live ones to the buffered file.
    auto src_count = t->driver()->record_count();
    std::uint32_t live = 0;
    for (std::uint32_t r = 1; r <= src_count; ++r) {
        auto rec = t->driver()->read_record_raw(r);
        if (!rec) return fail(rec.error());
        const auto& buf = rec.value();
        if (!buf.empty() && buf[0] == '*') continue;   // deleted
        ++live;
        file.insert(file.end(), buf.begin(), buf.end());
    }
    file.push_back(0x1A);

    // Patch the record count.
    file[4] = static_cast<std::uint8_t>( live        & 0xFFu);
    file[5] = static_cast<std::uint8_t>((live >>  8) & 0xFFu);
    file[6] = static_cast<std::uint8_t>((live >> 16) & 0xFFu);
    file[7] = static_cast<std::uint8_t>((live >> 24) & 0xFFu);

    {
        std::error_code ec;
        fs::remove(dst, ec);
    }
    {
        std::ofstream out(dst, std::ios::binary);
        if (!out) return fail(openads::AE_INTERNAL_ERROR,
                              "AdsCopyTable: open for write failed");
        out.write(reinterpret_cast<const char*>(file.data()),
                  static_cast<std::streamsize>(file.size()));
        if (!out) return fail(openads::AE_INTERNAL_ERROR,
                              "AdsCopyTable: write failed");
    }
    return ok();
}

// SAP / rddads signature: 3 args.
//   AdsCopyTableContents(hSrc, hDst, usFilterOption)
//
// usFilterOption : ADS_IGNOREFILTERS (0) / ADS_RESPECTFILTERS (1).
// We iterate raw records and skip the DBF tombstone byte — that
// matches IGNOREFILTERS (the default Harbour passes). RESPECT
// will land alongside AOF-aware copy in a follow-up; until then
// the param is accepted for signature parity and noted.
UNSIGNED32 AdsCopyTableContents(ADSHANDLE hSrc, ADSHANDLE hDst,
                                UNSIGNED16 usFilterOption) {
    (void)usFilterOption;
    Table* src = get_table(hSrc);
    Table* dst = get_table(hDst);
    if (!src || !dst) return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    if (!src->driver() || !dst->driver()) {
        return fail(openads::AE_INTERNAL_ERROR, "no driver");
    }
    if (src->driver()->record_length() != dst->driver()->record_length()) {
        return fail(openads::AE_INTERNAL_ERROR,
                    "record length mismatch between src and dst");
    }
    auto src_count = src->driver()->record_count();
    for (std::uint32_t r = 1; r <= src_count; ++r) {
        auto rec = src->driver()->read_record_raw(r);
        if (!rec) return fail(rec.error());
        const auto& buf = rec.value();
        if (!buf.empty() && buf[0] == '*') continue;
        auto a = dst->driver()->append_record_raw(buf.data(), buf.size());
        if (!a) return fail(a.error());
    }
    if (auto fl = dst->flush(); !fl) return fail(fl.error());
    return ok();
}

UNSIGNED32 AdsConvertTable(ADSHANDLE   hHandle,
                           UNSIGNED16  usFilterOption,
                           UNSIGNED8*  pucFile,
                           UNSIGNED16  /*usTargetType*/) {
    // Single-format engine for now (CDX-flavoured DBF). Convert is a
    // copy that mirrors the source format; once ADT / VFP land the
    // target type will pick a different writer.
    return AdsCopyTable(hHandle, usFilterOption, pucFile);
}

UNSIGNED32 AdsReindex(ADSHANDLE hTable) {
    if (auto* rt = get_remote_table(hTable)) {
        auto r = rt->conn->reindex(rt->id);
        if (!r) return fail(r.error());
        return ok();
    }
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    auto r = t->reindex();
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsPackTable_DEFERRED(ADSHANDLE /*hTable*/) {
    return fail(openads::AE_FUNCTION_NOT_AVAILABLE,
                "AdsPackTable lands in M4 alongside memo store");
}

UNSIGNED32 AdsZapTable_DEFERRED(ADSHANDLE /*hTable*/) {
    return fail(openads::AE_FUNCTION_NOT_AVAILABLE,
                "AdsZapTable lands in M4 alongside memo store");
}

// M-AOF.3 — wire AdsSetAOF / AdsClearAOF to the
// engine::aof::evaluate full-scan bitmap evaluator and install the
// resulting per-record bitmap as the table-level filter predicate.
// Skip / GoTop / GoBottom already honour the predicate, so the
// ABI surface gets correct AOF semantics today; M-AOF.4 will swap
// individual leaves to index range scans without changing this
// entry-point contract.
//
// AdsGetAOFOptLevel still reports ADS_OPTIMIZED_NONE because the
// V1 bitmap is built by a full table scan — no indexes are
// consulted yet. M-AOF.4 will start reporting PART / FULL based
// on per-leaf coverage. The "is an AOF currently installed at
// all?" signal is exposed separately so the ABI layer can keep
// AdsSetFilter and AdsSetAOF distinct.
UNSIGNED32 AdsSetAOF(ADSHANDLE hTable, UNSIGNED8* pucCondition,
                     UNSIGNED16 /*usResolve*/) {
    if (pucCondition == nullptr) {
        return fail(openads::AE_INTERNAL_ERROR, "AdsSetAOF: NULL condition");
    }
    if (auto* rt = get_remote_table(hTable)) {
        std::string cond = openads::abi::to_internal(pucCondition, 0);
        auto r = rt->conn->set_aof(rt->id, cond);
        if (!r) return fail(r.error());
        return ok();
    }
    Table* t = get_table(hTable);
    if (t == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    if (pucCondition == nullptr) {
        return fail(openads::AE_INTERNAL_ERROR, "AdsSetAOF: NULL condition");
    }
    auto cond = openads::abi::to_internal(pucCondition, 0);
    auto ast = openads::engine::aof::parse(cond);
    if (!ast) {
        // An expression outside the optimisable AOF subset (e.g.
        // `Empty(NAME)`, `UPPER(NAME) = 'A'`) is not an error — ADS
        // just declines to optimise it and the client RDD applies the
        // filter itself. Drop any prior AOF, report OPTIMIZED_NONE,
        // and succeed so the caller's own row filter takes over.
        t->clear_filter();
        return ok();
    }
    // Route through the M-AOF.4 index-accelerated evaluator: every
    // leaf that hits an open CDX/NTX index whose key expression is
    // the field name turns into a range scan; the rest fall back
    // to a per-record AST evaluation. The cached OptLevel feeds
    // AdsGetAOFOptLevel so callers see PART/FULL when their leaves
    // were served by an index.
    auto rep = openads::engine::aof::evaluate_optimised(*ast.value(), *t);
    if (!rep) return fail(rep.error());
    t->install_aof_bitmap(std::move(rep.value().bm));
    int lvl = ADS_OPTIMIZED_NONE;
    switch (rep.value().level) {
        case openads::engine::aof::OptLevel::None: lvl = ADS_OPTIMIZED_NONE; break;
        case openads::engine::aof::OptLevel::Part: lvl = ADS_OPTIMIZED_PART; break;
        case openads::engine::aof::OptLevel::Full: lvl = ADS_OPTIMIZED_FULL; break;
    }
    t->set_aof_opt_level(lvl);
    return ok();
}

UNSIGNED32 AdsGetAOFOptLevel(ADSHANDLE hTable, UNSIGNED16* pusLevel,
                             UNSIGNED8* /*pucBuf*/, UNSIGNED16* /*pusLen*/) {
    if (auto* rt = get_remote_table(hTable)) {
        auto r = rt->conn->get_aof_opt_level(rt->id);
        if (!r) return fail(r.error());
        if (pusLevel != nullptr) *pusLevel = r.value();
        return ok();
    }
    Table* t = get_table(hTable);
    int lvl = ADS_OPTIMIZED_NONE;
    if (t != nullptr && t->aof_active()) {
        lvl = t->aof_opt_level();
        if (lvl == 0) lvl = ADS_OPTIMIZED_NONE;
    }
    if (pusLevel != nullptr) {
        *pusLevel = static_cast<UNSIGNED16>(lvl);
    }
    return ok();
}

UNSIGNED32 AdsClearAOF(ADSHANDLE hTable) {
    if (auto* rt = get_remote_table(hTable)) {
        auto r = rt->conn->clear_aof(rt->id);
        if (!r) return fail(r.error());
        return ok();
    }
    Table* t = get_table(hTable);
    if (t == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    t->clear_filter();
    return ok();
}

// --- M4 memo + autoinc + encryption ----------------------------------------

UNSIGNED32 AdsBinaryToFile(ADSHANDLE hTable, UNSIGNED8* pucField,
                           UNSIGNED8* pucPath) {
    if (pucPath == nullptr) {
        return fail(openads::AE_INTERNAL_ERROR, "");
    }
    auto write_path = [&](const std::string& payload) -> UNSIGNED32 {
        auto path = openads::abi::to_internal(pucPath, 0);
        auto fres = openads::platform::File::open(
            path, openads::platform::OpenMode::CreateRW);
        if (!fres) return fail(fres.error());
        auto file = std::move(fres).value();
        auto wrote = file.write_at(0, payload.data(), payload.size());
        if (!wrote) return fail(wrote.error());
        return ok();
    };
    if (auto* rt = get_remote_table(hTable)) {
        std::string fname = openads::abi::to_internal(pucField, 0);
        auto v = rt->conn->get_field(rt->id, fname);
        if (!v) return fail(v.error());
        return write_path(v.value());
    }
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "");
    auto name = openads::abi::to_internal(pucField, 0);
    std::int32_t idx = t->field_index(name);
    if (idx < 0) return fail(openads::AE_COLUMN_NOT_FOUND, name.c_str());
    auto v = t->read_field(static_cast<std::uint16_t>(idx));
    if (!v) return fail(v.error());
    return write_path(v.value().as_string);
}

UNSIGNED32 AdsFileToBinary(ADSHANDLE hTable, UNSIGNED8* pucField,
                           UNSIGNED16 /*usType*/, UNSIGNED8* pucPath) {
    if (pucPath == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    auto path = openads::abi::to_internal(pucPath, 0);
    auto fres = openads::platform::File::open(
        path, openads::platform::OpenMode::ReadOnly);
    if (!fres) return fail(fres.error());
    auto file = std::move(fres).value();
    auto sz = file.size();
    if (!sz) return fail(sz.error());
    std::string payload;
    payload.resize(static_cast<std::size_t>(sz.value()));
    if (!payload.empty()) {
        auto rd = file.read_at(0, payload.data(), payload.size());
        if (!rd) return fail(rd.error());
    }
    if (auto* rt = get_remote_table(hTable)) {
        remote_settle_cursor(rt);                   // M12.21 option C
        std::string fname = openads::abi::to_internal(pucField, 0);
        auto r = rt->conn->set_field(rt->id, fname, payload);
        if (!r) return fail(r.error());
        return ok();
    }
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "");
    auto name = openads::abi::to_internal(pucField, 0);
    std::int32_t idx = t->field_index(name);
    if (idx < 0) return fail(openads::AE_COLUMN_NOT_FOUND, name.c_str());
    auto r = t->set_field(static_cast<std::uint16_t>(idx), payload);
    if (!r) return fail(r.error());
    return ok();
}

// --- M9.13 binary memo (ADS_BINARY / ADS_IMAGE) ----------------------------
//
// rddads' adsGetValue / adsPutValue branch for ADS_BINARY+ADS_IMAGE
// fields call this trio instead of AdsGetString/AdsSetString so the
// payload is treated as raw bytes (length-prefixed, no NUL trimming,
// embedded zeros preserved). The engine stores the bytes through the
// existing memo store with an explicit FPT block-type tag, and reads
// back the field as bytes plus an offset window so the caller can do
// chunked reads through a small fixed-size buffer.

UNSIGNED32 AdsGetBinaryLength(ADSHANDLE hTable, UNSIGNED8* pucField,
                              UNSIGNED32* pulLength) {
    Table* t = get_table(hTable);
    if (!t || pulLength == nullptr) {
        return fail(openads::AE_INTERNAL_ERROR, "");
    }
    std::uint16_t idx = 0;
    if (!resolve_field_index(t, pucField, &idx)) {
        return fail(openads::AE_COLUMN_NOT_FOUND, "");
    }
    auto v = t->read_field(idx);
    if (!v) return fail(v.error());
    *pulLength = static_cast<UNSIGNED32>(v.value().as_string.size());
    return ok();
}

UNSIGNED32 AdsGetBinary(ADSHANDLE hTable, UNSIGNED8* pucField,
                        UNSIGNED32 ulOffset, UNSIGNED8* pucBuf,
                        UNSIGNED32* pulLen) {
    Table* t = get_table(hTable);
    if (!t || pulLen == nullptr) {
        return fail(openads::AE_INTERNAL_ERROR, "");
    }
    std::uint16_t idx = 0;
    if (!resolve_field_index(t, pucField, &idx)) {
        return fail(openads::AE_COLUMN_NOT_FOUND, "");
    }
    auto v = t->read_field(idx);
    if (!v) return fail(v.error());
    const std::string& s = v.value().as_string;
    UNSIGNED32 cap = *pulLen;
    UNSIGNED32 n = 0;
    if (ulOffset < s.size()) {
        UNSIGNED32 remaining = static_cast<UNSIGNED32>(s.size() - ulOffset);
        n = cap < remaining ? cap : remaining;
        if (pucBuf != nullptr && n > 0) {
            std::memcpy(pucBuf, s.data() + ulOffset, n);
        }
    }
    *pulLen = n;
    return ok();
}

// M9.16: chunked AdsSetBinary writes. The accumulator below holds
// pending bytes per (Table*, field_idx) pair until the caller has
// delivered every byte of `ulTotalBytes`; only then does the payload
// land in the memo store via set_field_binary. Stale accumulators are
// scrubbed when the table closes (purge_pending_binaries_for_table).

namespace {

struct PendingBinaryKey {
    Table*        table;
    std::uint16_t field;
    bool operator==(const PendingBinaryKey& o) const noexcept {
        return table == o.table && field == o.field;
    }
};
struct PendingBinaryHash {
    std::size_t operator()(const PendingBinaryKey& k) const noexcept {
        return std::hash<void*>{}(k.table) ^
               (static_cast<std::size_t>(k.field) << 1);
    }
};
struct PendingBinary {
    std::string                   payload;
    std::uint32_t                 total = 0;
    openads::drivers::MemoBlockType type =
        openads::drivers::MemoBlockType::Object;
};

extern "C++"
std::unordered_map<PendingBinaryKey, PendingBinary, PendingBinaryHash>&
pending_binaries() {
    static std::unordered_map<PendingBinaryKey, PendingBinary,
                              PendingBinaryHash> m;
    return m;
}

openads::drivers::MemoBlockType
map_binary_type(UNSIGNED16 usBinaryType) {
    if (usBinaryType == ADS_IMAGE) {
        return openads::drivers::MemoBlockType::Picture;
    }
    if (usBinaryType == ADS_STRING || usBinaryType == ADS_MEMO) {
        return openads::drivers::MemoBlockType::Text;
    }
    return openads::drivers::MemoBlockType::Object;
}

}  // namespace

}  // extern "C"

void purge_pending_binaries_for_table(openads::engine::Table* t) {
    using openads::engine::Table;
    auto& m = pending_binaries();
    for (auto it = m.begin(); it != m.end(); ) {
        if (it->first.table == t) it = m.erase(it);
        else                      ++it;
    }
}

extern "C" {

UNSIGNED32 AdsSetBinary(ADSHANDLE hTable, UNSIGNED8* pucField,
                        UNSIGNED16 usBinaryType,
                        UNSIGNED32 ulTotalBytes, UNSIGNED32 ulOffset,
                        UNSIGNED8* pucBuf, UNSIGNED32 ulBytes) {
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "");
    std::uint16_t idx = 0;
    if (!resolve_field_index(t, pucField, &idx)) {
        return fail(openads::AE_COLUMN_NOT_FOUND, "");
    }
    auto type = map_binary_type(usBinaryType);

    // Single-shot fast path. No accumulator state is created when the
    // caller delivers the whole payload in one go.
    if (ulOffset == 0 && ulBytes == ulTotalBytes) {
        // Drop any stale accumulator from a prior aborted chunked write.
        pending_binaries().erase(PendingBinaryKey{t, idx});
        std::string payload;
        if (pucBuf != nullptr && ulBytes > 0) {
            payload.assign(reinterpret_cast<const char*>(pucBuf), ulBytes);
        }
        auto r = t->set_field_binary(idx, payload, type);
        if (!r) return fail(r.error());
        return ok();
    }

    // Chunked path. Accumulate at the caller's offset; flush when the
    // payload reaches the announced total.
    auto& m = pending_binaries();
    PendingBinaryKey key{t, idx};
    auto it = m.find(key);
    if (ulOffset == 0) {
        // First chunk — reset (or create) the accumulator and lock in
        // the announced total + binary type.
        if (it != m.end()) it->second = PendingBinary{};
        else               it = m.emplace(key, PendingBinary{}).first;
        it->second.total = ulTotalBytes;
        it->second.type  = type;
        it->second.payload.assign(static_cast<std::size_t>(ulTotalBytes),
                                  '\0');
    } else {
        if (it == m.end()) {
            return fail(openads::AE_INTERNAL_ERROR,
                        "chunked AdsSetBinary: no pending payload");
        }
        if (ulTotalBytes != it->second.total) {
            return fail(openads::AE_INTERNAL_ERROR,
                        "chunked AdsSetBinary: total bytes changed mid-write");
        }
    }
    if (static_cast<std::uint64_t>(ulOffset) +
        static_cast<std::uint64_t>(ulBytes) >
        static_cast<std::uint64_t>(it->second.total)) {
        m.erase(it);
        return fail(openads::AE_INTERNAL_ERROR,
                    "chunked AdsSetBinary: chunk runs past total");
    }
    if (pucBuf != nullptr && ulBytes > 0) {
        std::memcpy(it->second.payload.data() + ulOffset, pucBuf, ulBytes);
    }

    if (ulOffset + ulBytes == it->second.total) {
        std::string payload = std::move(it->second.payload);
        auto pending_type = it->second.type;
        m.erase(it);
        auto r = t->set_field_binary(idx, payload, pending_type);
        if (!r) return fail(r.error());
    }
    return ok();
}

UNSIGNED32 AdsGetLastAutoinc(ADSHANDLE hTable, UNSIGNED32* pulValue) {
    if (pulValue == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    if (auto* rt = get_remote_table(hTable)) {
        auto r = rt->conn->get_last_autoinc(rt->id);
        if (!r) return fail(r.error());
        *pulValue = r.value();
        return ok();
    }
    Table* t = get_table(hTable);
    if (!t || pulValue == nullptr) {
        return fail(openads::AE_INTERNAL_ERROR, "");
    }
    // ADT/VFP autoinc tracking lands when those drivers gain extended
    // type support. For now report 0 — the field still reads as part
    // of the record buffer for non-autoinc types.
    *pulValue = 0;
    return ok();
}

// Encryption thunks. The AES primitive is real (engine::Aes, validated
// against FIPS-197 / NIST SP 800-38A); the record-level boundary that
// marks a table encrypted on disk and re-keys per-record is part of a
// later milestone alongside ADT, since ADS-original encryption mode
// is not yet documented byte-for-byte. The thunks below behave as
// no-ops or fail with AE_FUNCTION_NOT_AVAILABLE.

UNSIGNED32 AdsEnableEncryption(ADSHANDLE /*hConnect*/, UNSIGNED8* /*pucPassword*/) {
    return fail(openads::AE_FUNCTION_NOT_AVAILABLE,
                "AdsEnableEncryption pending ADS encryption-mode RE");
}

UNSIGNED32 AdsDisableEncryption(ADSHANDLE /*hConnect*/) {
    return ok();
}

UNSIGNED32 AdsIsEncryptionEnabled(ADSHANDLE /*hConnect*/, UNSIGNED16* pbEnabled) {
    if (pbEnabled != nullptr) *pbEnabled = 0;
    return ok();
}

UNSIGNED32 AdsIsTableEncrypted(ADSHANDLE /*hTable*/, UNSIGNED16* pbEncrypted) {
    if (pbEncrypted != nullptr) *pbEncrypted = 0;
    return ok();
}

UNSIGNED32 AdsIsRecordEncrypted(ADSHANDLE /*hTable*/, UNSIGNED16* pbEncrypted) {
    if (pbEncrypted != nullptr) *pbEncrypted = 0;
    return ok();
}

// M11.2 — convert a plain CDX table to OpenADS-encrypted in place.
// Requires AdsSetEncryptionPassword to have been called on the
// owning connection (located by walking the registry for the
// connection whose tables include this Table*).
UNSIGNED32 AdsEncryptTable(ADSHANDLE hTable) {
    auto& s = state();
    std::lock_guard<std::recursive_mutex> lk(s.mu);
    Table* t = s.registry.lookup<Table>(hTable, HandleKind::Table);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "invalid table handle");
    Connection* owning = nullptr;
    s.registry.for_each_handle([&](Handle, HandleKind k, void* p) {
        if (k != HandleKind::Connection || owning) return;
        auto* cc = static_cast<Connection*>(p);
        if (cc->owns_table_ptr(t)) owning = cc;
    });
    if (!owning) return fail(openads::AE_INVALID_CONNECTION_HANDLE,
                             "table not owned by any connection");
    if (!owning->has_encryption_key()) {
        return fail(openads::AE_FUNCTION_NOT_AVAILABLE,
                    "AdsSetEncryptionPassword required first");
    }
    auto* cdx = dynamic_cast<openads::drivers::cdx::CdxDriver*>(t->driver());
    if (!cdx) {
        return fail(openads::AE_FUNCTION_NOT_AVAILABLE,
                    "encryption supported on CdxDriver tables only");
    }
    auto r = cdx->encrypt_in_place(owning->encryption_key());
    if (!r) return fail(r.error());
    if (auto fl = t->flush(); !fl) return fail(fl.error());
    return ok();
}

UNSIGNED32 AdsDecryptTable(ADSHANDLE /*hTable*/) {
    return fail(openads::AE_FUNCTION_NOT_AVAILABLE,
                "AdsDecryptTable pending ADS encryption-mode RE");
}

UNSIGNED32 AdsEncryptRecord(ADSHANDLE /*hTable*/) {
    return fail(openads::AE_FUNCTION_NOT_AVAILABLE,
                "AdsEncryptRecord pending ADS encryption-mode RE");
}

UNSIGNED32 AdsDecryptRecord(ADSHANDLE /*hTable*/) {
    return fail(openads::AE_FUNCTION_NOT_AVAILABLE,
                "AdsDecryptRecord pending ADS encryption-mode RE");
}

// --- M5 transaction surface -------------------------------------------------

UNSIGNED32 AdsBeginTransaction(ADSHANDLE hConnect) {
    auto& s = state();
    std::lock_guard<std::recursive_mutex> lk(s.mu);
    Connection* c = lookup_connection(hConnect);
    if (!c) return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    auto r = c->begin_tx();
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsCommitTransaction(ADSHANDLE hConnect) {
    auto& s = state();
    std::lock_guard<std::recursive_mutex> lk(s.mu);
    Connection* c = lookup_connection(hConnect);
    if (!c) return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    auto r = c->commit_tx();
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsRollbackTransaction(ADSHANDLE hConnect) {
    auto& s = state();
    std::lock_guard<std::recursive_mutex> lk(s.mu);
    Connection* c = lookup_connection(hConnect);
    if (!c) return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    auto r = c->rollback_tx();
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsInTransaction(ADSHANDLE hConnect, UNSIGNED16* pbInTx) {
    auto& s = state();
    std::lock_guard<std::recursive_mutex> lk(s.mu);
    Connection* c = lookup_connection(hConnect);
    if (!c || pbInTx == nullptr) {
        return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    }
    *pbInTx = c->in_tx() ? 1 : 0;
    return ok();
}

// M11.2 — set the encryption password on a connection. Affects
// every subsequent table open: encrypted tables (header byte 0xC3)
// transparently decrypt on read / encrypt on write using AES-256-CTR
// keyed off the (zero-padded) password bytes. OpenADS-only format —
// not byte-compatible with SAP ADS encrypted .adt files.
// M11.8 — OEM (CP437) ↔ ANSI (UTF-8 in this build) conversion
// helpers. `pucBuf` is read until a NUL byte. Output is written
// in place into the same buffer (caller must size for worst case
// — UTF-8 may grow up to 3x); `pulLen` carries the input length
// in and the output length out.
UNSIGNED32 AdsConvertOemToAnsi(UNSIGNED8* pucBuf, UNSIGNED32* pulLen) {
    if (pucBuf == nullptr || pulLen == nullptr) {
        return fail(openads::AE_INTERNAL_ERROR, "");
    }
    auto utf8 = openads::engine::cp437_to_utf8(
        pucBuf, static_cast<std::size_t>(*pulLen));
    std::size_t out_len = utf8.size();
    std::memcpy(pucBuf, utf8.data(), out_len);
    if (out_len < *pulLen) pucBuf[out_len] = '\0';
    *pulLen = static_cast<UNSIGNED32>(out_len);
    return ok();
}

UNSIGNED32 AdsConvertAnsiToOem(UNSIGNED8* pucBuf, UNSIGNED32* pulLen) {
    if (pucBuf == nullptr || pulLen == nullptr) {
        return fail(openads::AE_INTERNAL_ERROR, "");
    }
    auto cp = openads::engine::utf8_to_cp437(
        reinterpret_cast<const char*>(pucBuf),
        static_cast<std::size_t>(*pulLen));
    std::size_t out_len = cp.size();
    std::memcpy(pucBuf, cp.data(), out_len);
    if (out_len < *pulLen) pucBuf[out_len] = '\0';
    *pulLen = static_cast<UNSIGNED32>(out_len);
    return ok();
}

// M11.7 — set the connection's string-compare collation. Names:
// `binary` (default) or `nocase`. Affects equality / range
// comparisons for Character columns in SQL WHERE.
UNSIGNED32 AdsSetCollation(ADSHANDLE hConnect, UNSIGNED8* pucName) {
    auto& s = state();
    std::lock_guard<std::recursive_mutex> lk(s.mu);
    Connection* c = s.registry.lookup<Connection>(
        hConnect, HandleKind::Connection);
    if (!c || pucName == nullptr) {
        return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    }
    auto name = openads::abi::to_internal(pucName, 0);
    std::string upper;
    upper.reserve(name.size());
    for (char ch : name) upper.push_back(static_cast<char>(
        std::toupper(static_cast<unsigned char>(ch))));
    if (upper == "BINARY") {
        c->set_collation(Connection::Collation::Binary);
    } else if (upper == "NOCASE") {
        c->set_collation(Connection::Collation::NoCase);
    } else {
        return fail(openads::AE_FUNCTION_NOT_AVAILABLE,
                    "unknown collation name (expected BINARY / NOCASE)");
    }
    return ok();
}

UNSIGNED32 AdsSetEncryptionPassword(ADSHANDLE hConnect,
                                    UNSIGNED8* pucPassword) {
    auto& s = state();
    std::lock_guard<std::recursive_mutex> lk(s.mu);
    Connection* c = s.registry.lookup<Connection>(
        hConnect, HandleKind::Connection);
    if (!c || pucPassword == nullptr) {
        return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    }
    auto pw = openads::abi::to_internal(pucPassword, 0);
    c->set_encryption_password(pw);
    return ok();
}

// SAP / rddads signature: 3 args. ulOptions is reserved on the
// real ACE (must be ADS_DEFAULT); accept and ignore.
UNSIGNED32 AdsCreateSavepoint(ADSHANDLE hConnect, UNSIGNED8* pucName,
                              UNSIGNED32 ulOptions) {
    (void)ulOptions;
    auto& s = state();
    std::lock_guard<std::recursive_mutex> lk(s.mu);
    Connection* c = lookup_connection(hConnect);
    if (!c || pucName == nullptr) {
        return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    }
    auto name = openads::abi::to_internal(pucName, 0);
    auto r = c->create_savepoint(name);
    if (!r) return fail(r.error());
    return ok();
}

// M11.3 — release a savepoint without rolling back. The work done
// since CreateSavepoint stays part of the enclosing transaction.
UNSIGNED32 AdsReleaseSavepoint(ADSHANDLE hConnect, UNSIGNED8* pucName) {
    auto& s = state();
    std::lock_guard<std::recursive_mutex> lk(s.mu);
    Connection* c = lookup_connection(hConnect);
    if (!c || pucName == nullptr) {
        return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    }
    auto name = openads::abi::to_internal(pucName, 0);
    auto r = c->release_savepoint(name);
    if (!r) return fail(r.error());
    return ok();
}

// SAP / rddads signature: 3 args. ulOptions is reserved on real
// ACE; accept and ignore. Null pucSavepoint => full rollback.
UNSIGNED32 AdsRollbackTransaction80(ADSHANDLE hConnect, UNSIGNED8* pucSavepoint,
                                    UNSIGNED32 ulOptions) {
    (void)ulOptions;
    auto& s = state();
    std::lock_guard<std::recursive_mutex> lk(s.mu);
    Connection* c = lookup_connection(hConnect);
    if (!c) return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    if (pucSavepoint == nullptr) {
        // Full rollback if no savepoint name supplied (matches ACE legacy).
        auto r = c->rollback_tx();
        if (!r) return fail(r.error());
        return ok();
    }
    auto name = openads::abi::to_internal(pucSavepoint, 0);
    auto r = c->rollback_to_savepoint(name);
    if (!r) return fail(r.error());
    return ok();
}

// --- M9.12 Table directory iteration ---------------------------------------
//
// AdsFindFirstTable / AdsFindNextTable / AdsFindClose walk the
// connection's data directory and emit each entry whose name matches
// `pucMask` (FoxPro-style glob with `*` and `?`, case insensitive).
// Matches AE_SUCCESS while names remain; once exhausted, returns
// AE_NO_FILE_FOUND so the caller breaks out of its loop. The find
// handle is registered in the global registry under HandleKind::Find
// so it round-trips through ADSHANDLE without aliasing tables/cursors.

namespace {

// Truncate the matched filename into `pucBuf`, NUL-terminate when
// there's room, and report the on-wire length back through `pusLen`
// (matching the ACE convention rddads' AdsFindFirstTable/NextTable
// callers depend on).
UNSIGNED32 emit_name(UNSIGNED8* pucBuf, UNSIGNED16* pusLen,
                     const std::string& name) {
    if (pusLen == nullptr) return openads::AE_INTERNAL_ERROR;
    UNSIGNED16 cap = *pusLen;
    UNSIGNED16 n = static_cast<UNSIGNED16>(
        name.size() < cap ? name.size() : cap);
    if (pucBuf != nullptr && cap > 0) {
        std::memcpy(pucBuf, name.data(), n);
        if (n < cap) pucBuf[n] = '\0';
    }
    *pusLen = static_cast<UNSIGNED16>(name.size());
    return openads::AE_SUCCESS;
}

}  // namespace

UNSIGNED32 AdsFindFirstTable(ADSHANDLE   hConnect,
                             UNSIGNED8*  pucMask,
                             UNSIGNED8*  pucFileName,
                             UNSIGNED16* pusFileNameLen,
                             ADSHANDLE*  phFind) {
    auto& s = state();
    std::lock_guard<std::recursive_mutex> lk(s.mu);
    Connection* c = s.registry.lookup<Connection>(hConnect, HandleKind::Connection);
    if (c == nullptr || phFind == nullptr || pusFileNameLen == nullptr) {
        return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    }
    std::string mask = pucMask
        ? openads::abi::to_internal(pucMask, 0)
        : std::string("*.dbf");
    if (mask.empty()) mask = "*.dbf";

    auto r = c->find_first_table(mask);
    if (!r) return fail(r.error());

    auto [find_ptr, name] = std::move(r).value();
    Handle gh = s.registry.register_object(HandleKind::Find, find_ptr);
    *phFind = gh;
    return emit_name(pucFileName, pusFileNameLen, name);
}

UNSIGNED32 AdsFindNextTable(ADSHANDLE   hConnect,
                            ADSHANDLE   hFind,
                            UNSIGNED8*  pucFileName,
                            UNSIGNED16* pusFileNameLen) {
    auto& s = state();
    std::lock_guard<std::recursive_mutex> lk(s.mu);
    Connection* c = s.registry.lookup<Connection>(hConnect, HandleKind::Connection);
    if (c == nullptr || pusFileNameLen == nullptr) {
        return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    }
    auto* find = s.registry.lookup<Connection::TableFind>(hFind, HandleKind::Find);
    if (find == nullptr) {
        return fail(openads::AE_INTERNAL_ERROR, "invalid find handle");
    }
    auto r = c->find_next_table(find);
    if (!r) return fail(r.error());
    return emit_name(pucFileName, pusFileNameLen, r.value());
}

UNSIGNED32 AdsFindClose(ADSHANDLE hConnect, ADSHANDLE hFind) {
    auto& s = state();
    std::lock_guard<std::recursive_mutex> lk(s.mu);
    Connection* c = s.registry.lookup<Connection>(hConnect, HandleKind::Connection);
    if (c == nullptr) return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    auto* find = s.registry.lookup<Connection::TableFind>(hFind, HandleKind::Find);
    if (find == nullptr) {
        return fail(openads::AE_INTERNAL_ERROR, "invalid find handle");
    }
    (void)c->find_close(find);
    s.registry.release(hFind);
    return ok();
}

// --- M6 Data Dictionary ----------------------------------------------------

UNSIGNED32 AdsDDCreate(UNSIGNED8* pucDictionary, UNSIGNED16 /*bEncrypt*/,
                       UNSIGNED8* /*pucAdminPassword*/,
                       ADSHANDLE* phConnect) {
    if (pucDictionary == nullptr || phConnect == nullptr) {
        return fail(openads::AE_INTERNAL_ERROR, "null DD args");
    }
    auto path = openads::abi::to_internal(pucDictionary, 0);
    // Materialise an empty DD on disk, then open a Connection rooted at it.
    auto created = openads::engine::DataDict::create(path);
    if (!created) return fail(created.error());

    auto opened = Connection::open(path);
    if (!opened) return fail(opened.error());

    auto holder = std::make_unique<Connection>(std::move(opened).value());
    Connection* raw = holder.get();
    auto& s = state();
    std::lock_guard<std::recursive_mutex> lk(s.mu);
    Handle h = s.registry.register_object(HandleKind::Connection, raw);
    s.conns.emplace(h, std::move(holder));
    *phConnect = h;
    return ok();
}

// SAP signature (rddads): (hConn, name, file, fileType, charType,
// indexFile, comment). Matches Harbour's HB_FUNC(ADSDDADDTABLE).
UNSIGNED32 AdsDDAddTable(ADSHANDLE hConnect, UNSIGNED8* pucAlias,
                         UNSIGNED8* pucTablePath,
                         UNSIGNED16 /*usFileType*/,
                         UNSIGNED16 /*usCharType*/,
                         UNSIGNED8* /*pucIndexPath*/,
                         UNSIGNED8* /*pucComment*/) {
    auto& s = state();
    std::lock_guard<std::recursive_mutex> lk(s.mu);
    Connection* c = s.registry.lookup<Connection>(hConnect, HandleKind::Connection);
    if (!c) return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    if (!c->has_dd()) {
        return fail(openads::AE_FUNCTION_NOT_AVAILABLE,
                    "connection has no data dictionary");
    }
    if (pucAlias == nullptr || pucTablePath == nullptr) {
        return fail(openads::AE_INTERNAL_ERROR, "null DD-AddTable args");
    }
    auto alias = openads::abi::to_internal(pucAlias, 0);
    auto path  = openads::abi::to_internal(pucTablePath, 0);
    auto r = c->dd()->add_table(alias, path);
    if (!r) return fail(r.error());
    return ok();
}

UNSIGNED32 AdsDDRemoveTable(ADSHANDLE hConnect, UNSIGNED8* pucAlias,
                            UNSIGNED16 /*usDeleteFiles*/) {
    auto& s = state();
    std::lock_guard<std::recursive_mutex> lk(s.mu);
    Connection* c = s.registry.lookup<Connection>(hConnect, HandleKind::Connection);
    if (!c) return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    if (!c->has_dd()) {
        return fail(openads::AE_FUNCTION_NOT_AVAILABLE,
                    "connection has no data dictionary");
    }
    if (pucAlias == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    auto alias = openads::abi::to_internal(pucAlias, 0);
    auto r = c->dd()->remove_table(alias);
    if (!r) return fail(r.error());
    return ok();
}

// --- M7.1 SQL surface -------------------------------------------------------

extern "C++" {

namespace {

struct SqlStatement {
    Connection*                            conn   = nullptr;
    openads::network::RemoteConnection*    remote = nullptr;
    openads::sql_backend::SqliteConnection* sqlite = nullptr;
#if defined(OPENADS_WITH_MSSQL)
    openads::sql_backend::MssqlConnection*     mssql_conn  = nullptr;
#endif
    std::string                            sql;
    // RCB 2026-05-22 17:03 — The original struct stored only the raw SQL string.
    // AdsSet* functions never had a place to write named parameter values because
    // no parameter map existed here.  AdsPrepareSQL and AdsExecuteSQL had no
    // substitution step, so calling bindXxx() on a prepared statement always
    // fell through to get_table(), which knows nothing about statement handles,
    // and returned [5000] unknown table.  Adding params here gives AdsSet* a
    // place to store :name -> SQL-literal pairs that AdsExecuteSQL can substitute
    // before handing the final SQL to the parser.
    std::unordered_map<std::string, std::string> params;
    // Per-statement table-open overrides set by AdsStmt* helpers.
    UNSIGNED16  table_type   = 0;   // 0 = ADS_DEFAULT → CDX
    UNSIGNED16  lock_type    = 0;   // 0 = default → compatible locking
    UNSIGNED16  char_type    = 0;
    UNSIGNED16  read_only    = 0;   // non-zero → open read-only
    UNSIGNED16  check_rights = 0;
    bool        disable_enc  = false;
    std::string collation;
    std::vector<std::pair<std::string, std::string>> passwords;
};

std::unordered_map<ADSHANDLE, std::unique_ptr<SqlStatement>>& stmt_map() {
    static std::unordered_map<ADSHANDLE, std::unique_ptr<SqlStatement>> m;
    return m;
}

// stmt_map() is a process-wide table shared by every connection. Its
// structural operations (insert / find / erase) must all hold stmt_mu();
// without it, an insert that rehashes the map while another thread walks a
// bucket corrupts the structure (the load-tested crash/hang at >= 8 threads).
// A dedicated mutex (not the global state lock) keeps query execution off the
// lock and avoids any cross-ordering with it.
std::mutex& stmt_mu() {
    static std::mutex m;
    return m;
}

ADSHANDLE next_stmt_handle() {
    // Atomic so concurrent AdsCreateSQLStatement calls never hand out a
    // duplicate handle (the old non-atomic ++ could race two callers onto the
    // same value).
    static std::atomic<std::uint64_t> n{0x60000000ULL};
    return static_cast<ADSHANDLE>(++n);
}

// Look up a statement and return the raw pointer under the lock, then release:
// a statement is only ever executed by the thread that owns its handle, so the
// pointer stays valid for that thread while long query execution runs free of
// the lock. Returns nullptr if the handle is unknown.
SqlStatement* stmt_lookup(ADSHANDLE h) {
    std::lock_guard<std::mutex> lk(stmt_mu());
    auto& m = stmt_map();
    auto it = m.find(h);
    return it == m.end() ? nullptr : it->second.get();
}

ADSHANDLE stmt_register(std::unique_ptr<SqlStatement> stmt) {
    ADSHANDLE h = next_stmt_handle();
    std::lock_guard<std::mutex> lk(stmt_mu());
    stmt_map()[h] = std::move(stmt);
    return h;
}

void stmt_unregister(ADSHANDLE h) {
    std::lock_guard<std::mutex> lk(stmt_mu());
    stmt_map().erase(h);
}

// RCB 2026-05-22 17:03 — Statement handles live in stmt_map() which is a plain
// unordered_map keyed on the handle value (starting at 0x60000000).  They are
// completely invisible to get_table(), which queries the separate HandleRegistry
// for HandleKind::Table.  This helper is the single check point: if h is in
// stmt_map we store the value as a SQL literal string against the parameter name
// and return true so the caller skips the table path entirely.  The parameter
// name may arrive with or without a leading colon depending on the caller.
bool set_stmt_param(ADSHANDLE h, const char* pname, std::string literal) {
    std::lock_guard<std::mutex> lk(stmt_mu());
    auto& m = stmt_map();
    auto it = m.find(h);
    if (it == m.end()) return false;
    std::string key(pname ? pname : "");
    if (!key.empty() && key[0] == ':') key.erase(0, 1);
    it->second->params[key] = std::move(literal);
    return true;
}

openads::engine::TableType stmt_table_type(const SqlStatement& s) {
    return map_type(s.table_type);
}

openads::engine::OpenMode stmt_open_mode(const SqlStatement& s, bool for_write) {
    if (s.read_only != 0) return openads::engine::OpenMode::Read;
    return for_write ? openads::engine::OpenMode::Shared
                     : openads::engine::OpenMode::Read;
}

openads::engine::LockingMode stmt_locking_mode(const SqlStatement& s) {
    return (s.lock_type == ADS_PROPRIETARY_LOCKING)
           ? openads::engine::LockingMode::Proprietary
           : openads::engine::LockingMode::Compatible;
}

} // namespace

} // extern "C++"

UNSIGNED32 AdsCreateSQLStatement(ADSHANDLE hConnect, ADSHANDLE* phStatement) {
    if (phStatement == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    auto& s = state();
    std::lock_guard<std::recursive_mutex> lk(s.mu);
    if (auto* rc = s.registry.lookup<openads::network::RemoteConnection>(
            hConnect, HandleKind::RemoteConnection)) {
        auto stmt = std::make_unique<SqlStatement>();
        stmt->remote = rc;
        *phStatement = stmt_register(std::move(stmt));
        return ok();
    }
#if defined(OPENADS_WITH_SQLITE)
    if (auto* sc = s.registry.lookup<openads::sql_backend::SqliteConnection>(
            hConnect, HandleKind::SqliteConnection)) {
        auto stmt = std::make_unique<SqlStatement>();
        stmt->sqlite = sc;
        *phStatement = stmt_register(std::move(stmt));
        return ok();
    }
#endif
#if defined(OPENADS_WITH_MSSQL)
    if (auto* mc = s.registry.lookup<openads::sql_backend::MssqlConnection>(
            hConnect, HandleKind::MssqlConnection)) {
        auto stmt = std::make_unique<SqlStatement>();
        stmt->mssql_conn = mc;
        *phStatement = stmt_register(std::move(stmt));
        return ok();
    }
#endif
    Connection* c = s.registry.lookup<Connection>(hConnect, HandleKind::Connection);
    if (!c) return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    auto stmt = std::make_unique<SqlStatement>();
    stmt->conn = c;
    *phStatement = stmt_register(std::move(stmt));
    return ok();
}

UNSIGNED32 AdsCloseSQLStatement(ADSHANDLE hStatement) {
    stmt_unregister(hStatement);
    return ok();
}

UNSIGNED32 AdsPrepareSQL(ADSHANDLE hStatement, UNSIGNED8* pucSQL) {
    SqlStatement* st = stmt_lookup(hStatement);
    if (st == nullptr) return fail(openads::AE_INTERNAL_ERROR, "unknown stmt");
    st->sql = openads::abi::to_internal(pucSQL, 0);
    return ok();
}

UNSIGNED32 AdsGetNumParams(ADSHANDLE hStatement, UNSIGNED16* pusNumParams) {
    if (!pusNumParams) return fail(openads::AE_INTERNAL_ERROR, "");
    SqlStatement* st = stmt_lookup(hStatement);
    if (st == nullptr) return fail(openads::AE_INTERNAL_ERROR, "unknown stmt");
    const std::string& sql = st->sql;
    std::unordered_set<std::string> names;
    for (std::size_t i = 0; i < sql.size(); ) {
        if (sql[i] == ':' && i + 1 < sql.size() &&
            (std::isalpha((unsigned char)sql[i + 1]) || sql[i + 1] == '_')) {
            std::size_t j = i + 1;
            while (j < sql.size() &&
                   (std::isalnum((unsigned char)sql[j]) || sql[j] == '_'))
                ++j;
            names.insert(sql.substr(i + 1, j - (i + 1)));
            i = j;
        } else {
            ++i;
        }
    }
    *pusNumParams = static_cast<UNSIGNED16>(names.size());
    return ok();
}

UNSIGNED32 AdsExecuteSQL(ADSHANDLE hStatement, ADSHANDLE* phCursor) {
    SqlStatement* st_ptr = stmt_lookup(hStatement);
    if (st_ptr == nullptr) return fail(openads::AE_INTERNAL_ERROR, "unknown stmt");
    // Alias so the body below keeps its `it->second->...` accesses unchanged;
    // the lookup is now serialised and the pointer is used off the lock.
    std::pair<const ADSHANDLE, SqlStatement*> it_kv{hStatement, st_ptr};
    auto* it = &it_kv;
    if (it->second->sql.empty()) {
        return fail(openads::AE_PARSE_ERROR, "no prepared SQL");
    }
    // AdsSet* stores literal values in SqlStatement::params keyed by the bare
    // name (no colon); here we substitute every :name placeholder with its
    // stored literal before the SQL reaches the parser.
    //
    // The substitution is a single left-to-right pass that, at each ':',
    // consumes the WHOLE identifier (same boundary rule as AdsGetNumParams)
    // and replaces it by exact-name lookup.  A naive per-key find/replace was
    // wrong on two counts: (1) ":p1" matched as a prefix of ":p10"/":p11"/...,
    // so with >= 10 named params the double-digit placeholders were corrupted;
    // and (2) a substituted literal could itself contain ":name" text and get
    // re-scanned by a later key.  A single pass that never re-scans emitted
    // text and matches whole identifiers fixes both.  The output is built in a
    // std::string (no fixed size cap) and handed to AdsExecuteSQLDirect via a
    // buffer sized to the result, so large multi-row INSERTs are not truncated.
    const std::string& src = it->second->sql;
    const auto& params = it->second->params;
    std::string sql;
    sql.reserve(src.size() + 64);
    for (std::size_t i = 0; i < src.size(); ) {
        if (src[i] == ':' && i + 1 < src.size() &&
            (std::isalpha((unsigned char)src[i + 1]) || src[i + 1] == '_')) {
            std::size_t j = i + 1;
            while (j < src.size() &&
                   (std::isalnum((unsigned char)src[j]) || src[j] == '_'))
                ++j;
            std::string name = src.substr(i + 1, j - (i + 1));
            auto pit = params.find(name);
            if (pit != params.end()) {
                sql += pit->second;          // bound literal
            } else {
                sql.append(src, i, j - i);   // unknown :name — leave verbatim
            }
            i = j;
        } else {
            sql += src[i];
            ++i;
        }
    }
    std::vector<UNSIGNED8> buf(sql.size() + 1);
    std::memcpy(buf.data(), sql.data(), sql.size());
    buf[sql.size()] = '\0';
    return AdsExecuteSQLDirect(hStatement, buf.data(), phCursor);
}

// Build a read-only temp DBF in c->data_dir() that materialises one of
// the system.* virtual tables from the connection's DataDict state.
// `sys_name` is the part after "system." (already lower-cased by the caller).
// Returns the basename of the temp file, or "" if the name is unknown.
extern "C++" std::string build_system_dbf(Connection* c, std::string sys_name) {
    for (auto& ch : sys_name) ch = static_cast<char>(
        std::tolower(static_cast<unsigned char>(ch)));

    auto* dd = c->dd();
    if (!dd) return "";

    namespace fs = std::filesystem;

    struct Col {
        const char*    colname;
        char           type;      // 'C', 'N', 'L'
        std::uint16_t  length;
        std::uint8_t   decimals;
    };

    // Builds a temporary ADT file (full-length field names, no 10-char truncation).
    auto build = [&](const std::vector<Col>& cols,
                     const std::vector<std::vector<std::string>>& rows)
                     -> std::string {
        static const char kSig[] = "Advantage Table";  // 15 chars, no NUL

        // Compute ADT storage sizes: CICHAR → col.length bytes, INTEGER → 4 bytes
        struct FI { std::uint16_t adt_type; std::uint16_t storage; std::uint16_t rec_off; };
        std::vector<FI> fi;
        std::uint32_t rlen = 1;  // 1 byte delete flag
        for (const auto& col : cols) {
            FI f{};
            if (col.type == 'N') { f.adt_type = 11; f.storage = 4; }
            else if (col.type == 'L') { f.adt_type = 1; f.storage = 1; }
            else { f.adt_type = 20; f.storage = col.length; }  // 'C' → CICHAR
            f.rec_off = static_cast<std::uint16_t>(rlen);
            rlen += f.storage;
            fi.push_back(f);
        }

        auto nf      = static_cast<std::uint32_t>(cols.size());
        auto hdr_len = static_cast<std::uint32_t>(400u + nf * 200u);
        auto nr      = static_cast<std::uint32_t>(rows.size());

        std::vector<std::uint8_t> file;

        // 400-byte ADT main header
        std::array<std::uint8_t, 400> hdr{};
        std::memcpy(hdr.data(), kSig, 15);
        // rec_count at 24
        hdr[24] = static_cast<std::uint8_t>( nr        & 0xFFu);
        hdr[25] = static_cast<std::uint8_t>((nr >>  8) & 0xFFu);
        hdr[26] = static_cast<std::uint8_t>((nr >> 16) & 0xFFu);
        hdr[27] = static_cast<std::uint8_t>((nr >> 24) & 0xFFu);
        // hdr_len at 32
        hdr[32] = static_cast<std::uint8_t>( hdr_len        & 0xFFu);
        hdr[33] = static_cast<std::uint8_t>((hdr_len >>  8) & 0xFFu);
        hdr[34] = static_cast<std::uint8_t>((hdr_len >> 16) & 0xFFu);
        hdr[35] = static_cast<std::uint8_t>((hdr_len >> 24) & 0xFFu);
        // rec_len at 36
        hdr[36] = static_cast<std::uint8_t>( rlen        & 0xFFu);
        hdr[37] = static_cast<std::uint8_t>((rlen >>  8) & 0xFFu);
        hdr[38] = static_cast<std::uint8_t>((rlen >> 16) & 0xFFu);
        hdr[39] = static_cast<std::uint8_t>((rlen >> 24) & 0xFFu);
        file.insert(file.end(), hdr.begin(), hdr.end());

        // Field descriptors (200 bytes each)
        for (std::size_t ci = 0; ci < cols.size(); ++ci) {
            std::array<std::uint8_t, 200> fd{};
            const char* nm = cols[ci].colname;
            std::size_t nlen = std::strlen(nm);
            if (nlen > 127u) nlen = 127u;
            std::memcpy(fd.data(), nm, nlen);       // null-terminated within bytes 0-127
            // fd[128] = flags = 0 (not nullable)
            fd[129] = static_cast<std::uint8_t>( fi[ci].adt_type       & 0xFFu);
            fd[130] = static_cast<std::uint8_t>((fi[ci].adt_type >>  8) & 0xFFu);
            fd[131] = static_cast<std::uint8_t>( fi[ci].rec_off        & 0xFFu);
            fd[132] = static_cast<std::uint8_t>((fi[ci].rec_off >>  8) & 0xFFu);
            fd[135] = static_cast<std::uint8_t>( fi[ci].storage        & 0xFFu);
            fd[136] = static_cast<std::uint8_t>((fi[ci].storage >>  8) & 0xFFu);
            file.insert(file.end(), fd.begin(), fd.end());
        }

        // Records
        for (const auto& row : rows) {
            std::vector<std::uint8_t> rec(rlen, 0x20u);  // space-fill
            rec[0] = 0x04u;  // ADT active-record flag
            for (std::size_t ci = 0; ci < cols.size(); ++ci) {
                const std::string& val = ci < row.size() ? row[ci] : "";
                std::uint8_t* dst = rec.data() + fi[ci].rec_off;
                if (fi[ci].adt_type == 11u) {  // INTEGER: 4-byte LE int32
                    std::int32_t iv = val.empty() ? 0
                        : static_cast<std::int32_t>(std::stol(val));
                    auto uiv = static_cast<std::uint32_t>(iv);
                    dst[0] = static_cast<std::uint8_t>( uiv        & 0xFFu);
                    dst[1] = static_cast<std::uint8_t>((uiv >>  8) & 0xFFu);
                    dst[2] = static_cast<std::uint8_t>((uiv >> 16) & 0xFFu);
                    dst[3] = static_cast<std::uint8_t>((uiv >> 24) & 0xFFu);
                } else if (fi[ci].adt_type == 1u) {  // LOGICAL: 1 byte — 'T'(0x54)/'F'(0x46)
                    dst[0] = (val == "1" || val == "T" || val == "t") ? 'T' : 'F';
                } else {  // CICHAR: space-padded
                    std::size_t n2 = std::min<std::size_t>(val.size(), fi[ci].storage);
                    std::memcpy(dst, val.data(), n2);
                }
            }
            file.insert(file.end(), rec.begin(), rec.end());
        }

        char nb[64];
        std::snprintf(nb, sizeof(nb), "_sys_%llx.adt",
            static_cast<unsigned long long>(
                openads::platform::monotonic_nanos()));
        fs::path tmp = fs::path(c->data_dir()) / nb;
        {
            std::ofstream out(tmp, std::ios::binary);
            if (!out) return "";
            out.write(reinterpret_cast<const char*>(file.data()),
                      static_cast<std::streamsize>(file.size()));
        }
        return nb;
    };

    if (sys_name == "tables") {
        // Column set matches SAP ADS system.tables for compatibility.
        const std::vector<Col> cols = {
            {"Name",                    'C', 200, 0},
            {"Table_Relative_Path",     'C', 250, 0},
            {"Table_Type",              'N',   5, 0},
            {"Table_Auto_Create",       'C',   6, 0},
            {"Table_Primary_Key",       'C', 200, 0},
            {"Table_Default_Index",     'C', 200, 0},
            {"Table_Encryption",        'C',   6, 0},
            {"Table_Permission_Level",  'N',   5, 0},
            {"Table_Memo_Block_Size",   'N',   5, 0},
            {"Table_Validation_Expr",   'C', 250, 0},
            {"Table_Validation_Msg",    'C', 250, 0},
            {"Comment",                 'C', 250, 0},
            {"Triggers_Disabled",       'C',   6, 0},
            {"Table_Caching",           'N',   5, 0},
            {"Table_Trans_Free",        'C',   6, 0},
            {"Table_WEB_delta",         'C',   6, 0},
            {"Table_Concurrency_Enabled", 'C', 6, 0},
        };
        namespace fs = std::filesystem;
        std::vector<std::vector<std::string>> rows;
        for (const auto& kv : dd->tables()) {
            const std::string& alias = kv.first;
            const std::string& rel   = kv.second;
            // Infer table type from extension (3=ADT, 2=CDX/NTX)
            std::string ext = fs::path(rel).extension().string();
            for (auto& ch : ext) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            int ttype = (ext == ".adt") ? 3 : 2;
            int perm = (c != nullptr && !c->username().empty())
                       ? dd->get_effective_permission(c->username(), alias) : 4;
            std::string pk   = dd->get_table_property(alias, 202);
            std::string didx = dd->get_table_property(alias, 213);
            rows.push_back({alias, rel, std::to_string(ttype),
                            "True", pk, didx,
                            "False", std::to_string(perm), "0",
                            "", "", "",
                            "False", "0", "False", "False", "False"});
        }
        return build(cols, rows);
    }
    if (sys_name == "indexes") {
        const std::vector<Col> cols = {
            {"TABLE_NAME", 'C', 200, 0},
            {"INDEX_FILE", 'C', 250, 0},
            {"COMMENT",    'C', 200, 0},
        };
        std::vector<std::vector<std::string>> rows;
        for (const auto& e : dd->indexes())
            rows.push_back({e.table_alias, e.index_path, e.comment});
        return build(cols, rows);
    }
    if (sys_name == "users") {
        const std::vector<Col> cols = {{"USER_NAME", 'C', 200, 0}};
        std::vector<std::vector<std::string>> rows;
        for (const auto& u : dd->users())
            rows.push_back({u});
        return build(cols, rows);
    }
    if (sys_name == "groups") {
        const std::vector<Col> cols = {{"GROUP_NAME", 'C', 200, 0}};
        std::vector<std::vector<std::string>> rows;
        for (const auto& g : dd->groups())
            rows.push_back({g});
        return build(cols, rows);
    }
    if (sys_name == "usergroups") {
        // Lists all defined groups (SAP: system.usergroups = group catalogue).
        // Include both explicit group records AND groups referenced in memberships,
        // so text-format DDs that only have MEMBER entries still list their groups.
        const std::vector<Col> cols = {{"GROUP_NAME", 'C', 200, 0}};
        std::unordered_set<std::string> seen;
        std::vector<std::vector<std::string>> rows;
        for (const auto& g : dd->groups())
            if (seen.insert(g).second) rows.push_back({g});
        for (const auto& kv : dd->memberships())
            for (const auto& g : kv.second)
                if (seen.insert(g).second) rows.push_back({g});
        return build(cols, rows);
    }
    if (sys_name == "usergroupmembers") {
        // Lists which users belong to which group.
        const std::vector<Col> cols = {
            {"GROUP_NAME", 'C', 200, 0},
            {"USER_NAME",  'C', 200, 0},
        };
        std::vector<std::vector<std::string>> rows;
        for (const auto& kv : dd->memberships())
            for (const auto& grp : kv.second)
                rows.push_back({grp, kv.first});   // group, user
        return build(cols, rows);
    }
    if (sys_name == "permissions") {
        // Columns match SAP system.permissions:
        //   OBJ_NAME, OBJ_TYPE (numeric), PARENT, GRANTEE
        //   then 10 permission flag columns (0=denied, 1=granted, ""=N/A for type)
        // PARENT is empty for top-level objects; table name for field rows (OBJ_TYPE=4).
        //
        // ADS_PERMISSION_* bitmask constants (ace.h):
        //   0x001=READ/SELECT  0x002=UPDATE   0x004=EXECUTE  0x008=INHERIT(meta)
        //   0x010=INSERT       0x020=DELETE   0x040=LINK_ACCESS
        //   0x080=CREATE       0x100=ALTER    0x200=DROP
        //   0x80000000=WITH_GRANT / SAP sentinel
        //
        // Column display values:
        //   ""  = not applicable for this object/grantee type
        //   "0" = permission not granted
        //   "1" = permission granted to a GROUP grantee
        //   "2" = permission granted directly to a USER grantee
        //
        // SAP binary .add files store 0x80000000 as a constant info2 sentinel
        // for ALL Permission records; per-bit rights are in encrypted blobs
        // OpenADS cannot decode.  We therefore treat the SAP sentinel as
        // granting full DML for group records.
        const std::vector<Col> cols = {
            {"OBJ_NAME",  'C', 200, 0},
            {"OBJ_TYPE",  'N',   3, 0},
            {"PARENT",    'C', 200, 0},
            {"GRANTEE",   'C', 200, 0},
            {"SELECT",    'C',   1, 0},
            {"UPDATE",    'C',   1, 0},
            {"INSERT",    'C',   1, 0},
            {"DELETE",    'C',   1, 0},
            {"EXECUTE",   'C',   1, 0},
            {"ACCESS",    'C',   1, 0},
            {"INHERIT",   'C',   1, 0},
            {"CREATE",    'C',   1, 0},
            {"ALTER",     'C',   1, 0},
            {"DROP",      'C',   1, 0},
        };

        const uint32_t SAP_SENTINEL = 0x80000000u;

        // Return "1" or "2" depending on grantee type, or "0" if not set.
        // Groups use "1"; users use "2".
        auto perm_val = [](bool set, bool is_grp) -> std::string {
            if (!set) return "0";
            return is_grp ? "1" : "2";
        };

        // Decode a DML bitmask for a single logical column.
        // sap_bit: actual bit position in the ADS_PERMISSION mask.
        auto dml_col = [&perm_val](
                            uint32_t mask, bool is_grp, int sap_bit) -> std::string {
            if (mask & SAP_SENTINEL) {
                // SAP sentinel: cannot decode actual level from encrypted blobs.
                // For groups: approximate as full DML (SELECT+UPDATE+INSERT+DELETE).
                // For users: SAP sentinel alone = INHERIT-only, no direct DML.
                return perm_val(is_grp, is_grp);
            }
            return perm_val((mask >> sap_bit) & 1u, is_grp);
        };

        // Execute column (ADS_PERMISSION_EXECUTE = bit 2 = 0x004).
        auto exe_col = [&perm_val](uint32_t mask, bool is_grp) -> std::string {
            if (mask & SAP_SENTINEL) return perm_val(is_grp, is_grp);
            return perm_val((mask >> 2) & 1u, is_grp);
        };

        std::vector<std::vector<std::string>> rows;

        auto top_key = [](const std::string& grantee,
                          const std::string& obj_name,
                          const std::string& type_code) -> std::string {
            return grantee + '\x1f' + obj_name + '\x1f' + type_code;
        };

        auto emit_top_row = [&](const std::string& obj_name,
                                const std::string& type_code,
                                const std::string& obj_type,
                                const std::string& grantee,
                                bool is_grp,
                                uint32_t m) {
            bool is_table    = (obj_type == "Table");
            bool is_exec     = (obj_type == "StoredProc" || obj_type == "Function");
            bool is_obj_user = (obj_type == "User"  || obj_type == "Group");
            bool is_db       = (obj_type == "Database");

            bool show_dml    = (is_table || is_db);
            bool show_exec   = (is_exec  || is_db);
            bool show_inherit = !is_grp && !is_obj_user;
            auto inherit_val  = [&]() -> std::string {
                if (!show_inherit) return "";
                bool set = (m & SAP_SENTINEL) || (m & 0x008u);
                return set ? "1" : "0";
            };
            auto alt_val = [&]() -> std::string {
                if (is_obj_user || is_exec) return "";
                if (m & SAP_SENTINEL) return perm_val(is_grp, is_grp);
                return perm_val((m >> 8) & 1u, is_grp);
            };
            auto drop_val = [&]() -> std::string {
                if (is_obj_user) return "";
                if (m & SAP_SENTINEL) return perm_val(is_grp, is_grp);
                return perm_val((m >> 9) & 1u, is_grp);
            };

            rows.push_back({
                obj_name,
                type_code,
                "",
                grantee,
                show_dml  ? dml_col(m, is_grp, 0)  : "",
                show_dml  ? dml_col(m, is_grp, 1)  : "",
                show_dml  ? dml_col(m, is_grp, 4)  : "",
                show_dml  ? dml_col(m, is_grp, 5)  : "",
                show_exec ? exe_col(m, is_grp)      : "",
                (is_db && is_grp) ? dml_col(m, is_grp, 6) : "",
                inherit_val(),
                (is_db && is_grp) ? dml_col(m, is_grp, 7) : "",
                alt_val(),
                drop_val(),
            });
        };

        std::unordered_set<std::string> seen_top;

        for (const auto& pe : dd->permissions()) {
            const std::string type_code = std::to_string(pe.object_type_code);
            if (!seen_top.insert(top_key(pe.grantee, pe.object_name,
                                         type_code)).second)
                continue;
            emit_top_row(pe.object_name, type_code, pe.object_type,
                         pe.grantee, pe.grantee_is_group, pe.bitmask);

            // Field-level rows: OBJ_TYPE=4, PARENT=table.  Skip fields that
            // only carry the binary-load ordinal placeholder (not registered
            // FIELDPROP / DD field metadata).
            if (pe.object_type == "Table") {
                auto fp_it = dd->field_props().find(pe.object_name);
                if (fp_it != dd->field_props().end()) {
                    std::vector<std::string> fnames;
                    fnames.reserve(fp_it->second.size());
                    for (const auto& [fn, fprops] : fp_it->second) {
                        if (fprops.size() == 1 && fprops.count("ordinal"))
                            continue;
                        fnames.push_back(fn);
                    }
                    std::sort(fnames.begin(), fnames.end());
                    std::string fsel = dml_col(pe.bitmask, pe.grantee_is_group, 0);
                    std::string fupd = dml_col(pe.bitmask, pe.grantee_is_group, 1);
                    std::string fins = dml_col(pe.bitmask, pe.grantee_is_group, 4);
                    for (const auto& fname : fnames) {
                        rows.push_back({
                            fname,
                            "4",
                            pe.object_name,
                            pe.grantee,
                            fsel,
                            fupd,
                            fins,
                            "",
                            "",
                            "",
                            "",
                            "",
                            "",
                            "",
                        });
                    }
                }
            }
        }

        // Legacy parity: emit a zero-permission row for every (grantee, object)
        // pair that has no Permission record (flag columns "0", not omitted).
        struct SecObj {
            std::string name;
            std::string type;
            std::string code;
        };
        std::vector<SecObj> objects;
        objects.reserve(dd->tables().size() + dd->views().size() +
                        dd->procs().size() + dd->functions().size() +
                        dd->links().size() + 8);
        for (const auto& [alias, _] : dd->tables())
            objects.push_back({alias, "Table", "1"});
        for (const auto& [name, _] : dd->views())
            objects.push_back({name, "View", "6"});
        for (const auto& [name, _] : dd->procs())
            objects.push_back({name, "StoredProc", "10"});
        for (const auto& [name, _] : dd->functions())
            objects.push_back({name, "Function", "18"});
        for (const auto& [alias, _] : dd->links())
            objects.push_back({alias, "Link", "12"});
        for (const auto& u : dd->users())
            objects.push_back({u, "User", "8"});
        for (const auto& g : dd->groups())
            objects.push_back({g, "Group", "9"});
        if (!dd->users().empty() || !dd->groups().empty())
            objects.push_back({"Database", "Database", "11"});

        struct Grantee { std::string name; bool is_group; };
        std::vector<Grantee> grantees;
        grantees.reserve(dd->users().size() + dd->groups().size());
        for (const auto& u : dd->users())
            grantees.push_back({u, false});
        for (const auto& g : dd->groups())
            grantees.push_back({g, true});

        for (const auto& gr : grantees) {
            for (const auto& obj : objects) {
                const auto key = top_key(gr.name, obj.name, obj.code);
                if (!seen_top.insert(key).second) continue;
                emit_top_row(obj.name, obj.code, obj.type, gr.name, gr.is_group,
                             0u);
            }
        }

        return build(cols, rows);
    }
    if (sys_name == "effectivepermissions") {
        // Effective permissions for the connected user: direct grants OR-ed with
        // every group the user belongs to.  Same columns as system.permissions.
        // Only objects where an ACL entry exists are listed; objects without any
        // ACL entry are open-access and are omitted (caller may infer full access).
        const std::vector<Col> cols = {
            {"OBJ_NAME",  'C', 200, 0},
            {"OBJ_TYPE",  'N',   3, 0},
            {"GRANTEE",   'C', 200, 0},
            {"SELECT",    'C',   1, 0},
            {"UPDATE",    'C',   1, 0},
            {"INSERT",    'C',   1, 0},
            {"DELETE",    'C',   1, 0},
            {"EXECUTE",   'C',   1, 0},
            {"ACCESS",    'C',   1, 0},
            {"INHERIT",   'C',   1, 0},
            {"CREATE",    'C',   1, 0},
            {"ALTER",     'C',   1, 0},
            {"DROP",      'C',   1, 0},
        };
        const std::string& user = c->username();
        if (user.empty()) {
            // No logged-in user — return empty (caller treats as open access).
            return build(cols, {});
        }
        auto entries = dd->get_all_effective_perms(user);
        std::vector<std::vector<std::string>> rows;
        rows.reserve(entries.size());
        auto b1 = [](bool v) -> std::string { return v ? "1" : "0"; };
        for (const auto& e : entries) {
            const auto& t = e.object_type;
            bool is_table = (t == "Table");
            bool is_exec  = (t == "StoredProc" || t == "Function");
            bool is_db    = (t == "Database");
            const auto& o = e.ops;
            rows.push_back({
                e.object_name,
                std::to_string(e.object_type_code),
                e.grantee,
                (is_table || is_db) ? b1(o.select_)  : "",  // SELECT
                (is_table || is_db) ? b1(o.update_)  : "",  // UPDATE
                (is_table || is_db) ? b1(o.insert_)  : "",  // INSERT
                (is_table || is_db) ? b1(o.delete_)  : "",  // DELETE
                (is_exec  || is_db) ? b1(o.execute_) : "",  // EXECUTE
                "",                                          // ACCESS
                "",                                          // INHERIT (N/A for effective)
                "",                                          // CREATE
                "",                                          // ALTER
                "",                                          // DROP
            });
        }
        return build(cols, rows);
    }
    if (sys_name == "relations") {
        const std::vector<Col> cols = {
            {"RI_NAME",    'C', 200, 0},
            {"PARENT",     'C', 200, 0},
            {"CHILD",      'C', 200, 0},
            {"PARENT_TAG", 'C', 200, 0},
            {"CHILD_TAG",  'C', 200, 0},
            {"UPDATE_OPT", 'C',  10, 0},
            {"DELETE_OPT", 'C',  10, 0},
            {"FAIL_TABLE", 'C', 200, 0},
        };
        std::vector<std::vector<std::string>> rows;
        for (const auto& kv : dd->ri()) {
            const auto& e = kv.second;
            rows.push_back({e.name, e.parent, e.child,
                            e.parent_tag, e.child_tag,
                            e.update_opt, e.delete_opt, e.fail_table});
        }
        return build(cols, rows);
    }
    if (sys_name == "referentialintegrity") {
        // SAP-compatible alias for system.relations.
        const std::vector<Col> cols = {
            {"RI_NAME",      'C', 200, 0},
            {"PARENT_TABLE", 'C', 200, 0},
            {"CHILD_TABLE",  'C', 200, 0},
            {"PARENT_TAG",   'C', 200, 0},
            {"CHILD_TAG",    'C', 200, 0},
            {"UPDATE_RULE",  'N',  10, 0},
            {"DELETE_RULE",  'N',  10, 0},
            {"FAIL_TABLE",   'C', 200, 0},
        };
        std::vector<std::vector<std::string>> rows;
        for (const auto& kv : dd->ri()) {
            const auto& e = kv.second;
            rows.push_back({e.name, e.parent, e.child,
                            e.parent_tag, e.child_tag,
                            e.update_opt, e.delete_opt, e.fail_table});
        }
        return build(cols, rows);
    }
    if (sys_name == "links") {
        const std::vector<Col> cols = {
            {"LINK_NAME", 'C', 200, 0},
            {"LINK_PATH", 'C', 250, 0},
            {"LINK_USER", 'C', 200, 0},
        };
        std::vector<std::vector<std::string>> rows;
        for (const auto& kv : dd->links())
            rows.push_back({kv.second.alias, kv.second.path, kv.second.user});
        return build(cols, rows);
    }
    if (sys_name == "triggers") {
        // TIMING decodes SAP binary timing byte: 1=BEFORE 2=INSTEAD OF 4=AFTER
        // EVENT_MASK is the SAP event type byte: 1=INSERT 2=UPDATE 3=DELETE
        const std::vector<Col> cols = {
            {"TRIG_NAME",    'C', 200, 0},
            {"TABLE_NAME",   'C', 200, 0},
            {"EVENT_MASK",   'N',  10, 0},
            {"TIMING",       'C',  15, 0},
            {"EVENT",        'C',  20, 0},
            {"CONTAINER",    'C', 4096, 0},
            {"PROC",         'C', 200, 0},
            {"PRIORITY",     'N',  10, 0},
            {"ENABLED",      'L',   1, 0},
            {"TRIG_OPTIONS", 'N',  10, 0},
        };
        auto timing_str = [](std::uint32_t t) -> std::string {
            if (t == 1) return "BEFORE";
            if (t == 2) return "INSTEAD OF";
            if (t == 4) return "AFTER";
            return "";
        };
        auto event_str = [](std::uint32_t ev) -> std::string {
            if (ev == 1) return "INSERT";
            if (ev == 2) return "UPDATE";
            if (ev == 3) return "DELETE";
            return "";
        };
        std::vector<std::vector<std::string>> rows;
        for (const auto& kv : dd->triggers()) {
            const auto& e = kv.second;
            rows.push_back({e.name, e.table_alias,
                            std::to_string(e.event_mask),
                            timing_str(e.timing),
                            event_str(e.event_mask),
                            e.container, e.procedure,
                            std::to_string(e.priority),
                            e.enabled ? "T" : "F",
                            std::to_string(e.options)});
        }
        return build(cols, rows);
    }
    if (sys_name == "storedprocedures") {
        const std::vector<Col> cols = {
            {"PROC_NAME",  'C', 200, 0},
            {"CONTAINER",  'C', 250, 0},
            {"PROCEDURE",  'C', 255, 0},
            {"INPUT",      'C', 250, 0},
            {"OUTPUT",     'C', 250, 0},
        };
        std::vector<std::vector<std::string>> rows;
        for (const auto& kv : dd->procs()) {
            const auto& e = kv.second;
            rows.push_back({e.name, e.container, e.procedure,
                            e.input_params, e.output_params});
        }
        return build(cols, rows);
    }
    if (sys_name == "functions") {
        // Column names capped at 10 chars (DBF field descriptor limit in build()).
        const std::vector<Col> cols = {
            {"FUNC_NAME",  'C', 200, 0},
            {"CONTAINER",  'C', 250, 0},
            {"RET_TYPE",   'C',  50, 0},
            {"IN_PARAMS",  'C', 200, 0},
            {"FUNC_BODY",  'C', 255, 0},
            {"COMMENT",    'C', 200, 0},
        };
        std::vector<std::vector<std::string>> rows;
        for (const auto& kv : dd->functions()) {
            const auto& e = kv.second;
            rows.push_back({e.name, e.container, e.return_type,
                            e.input_params, e.implementation, e.comment});
        }
        return build(cols, rows);
    }
    if (sys_name == "views") {
        const std::vector<Col> cols = {
            {"VIEW_NAME", 'C', 200, 0},
            {"VIEW_SQL",  'C', 250, 0},
            {"COMMENT",   'C', 200, 0},
        };
        std::vector<std::vector<std::string>> rows;
        for (const auto& kv : dd->views()) {
            const auto& e = kv.second;
            rows.push_back({e.name, e.sql, e.comment});
        }
        return build(cols, rows);
    }
    if (sys_name == "dictionary") {
        const std::vector<Col> cols = {
            {"PROP_NAME",  'C', 200, 0},
            {"PROP_VALUE", 'C', 250, 0},
        };
        std::vector<std::vector<std::string>> rows;
        for (const auto& kv : dd->db_props())
            rows.push_back({kv.first, kv.second});
        return build(cols, rows);
    }
    if (sys_name == "iota") {
        const std::vector<Col> cols = {{"IOTA", 'C', 1, 0}};
        return build(cols, {{std::vector<std::string>{" "}}});
    }
    if (sys_name == "columns") {
        const std::vector<Col> cols = {
            {"TABLE_NAME", 'C', 200, 0},
            {"COL_NAME",   'C', 200, 0},
            {"COL_NUM",    'N',   5, 0},
            {"COL_TYPE",   'C',  10, 0},
            {"COL_LEN",    'N',   5, 0},
            {"COL_DEC",    'N',   3, 0},
        };
        std::vector<std::vector<std::string>> rows;
        for (const auto& kv : dd->tables()) {
            std::string rel = kv.second;
            auto th = c->open_table(rel, openads::engine::TableType::Cdx,
                                    openads::engine::OpenMode::Read);
            if (!th) continue;
            openads::engine::Table* tbl = c->lookup_table(th.value());
            if (tbl) {
                std::uint16_t nf = tbl->field_count();
                for (std::uint16_t i = 0; i < nf; ++i) {
                    const auto& fd = tbl->field_descriptor(i);
                    rows.push_back({
                        kv.first,
                        fd.name,
                        std::to_string(i + 1),
                        std::string(1, fd.raw_type),
                        std::to_string(fd.length),
                        std::to_string(fd.decimals),
                    });
                }
            }
            c->close_table(th.value());
        }
        return build(cols, rows);
    }
    return "";
}

// Dispatch for ADS built-in sp_* stored procedures. Returns true and sets *prc
// if the name was recognized; caller falls through to the DLL path otherwise.
extern "C++" bool dispatch_sp_builtin(
        Connection* c,
        const std::string& uname,
        const std::vector<openads::sql::ExecuteProcedureArg>& args,
        UNSIGNED32* prc) {
    auto* dd = c->has_dd() ? c->dd() : nullptr;
    auto arg = [&](std::size_t i) -> const std::string& {
        static const std::string empty;
        return (i < args.size()) ? args[i].text : empty;
    };
    auto ri_int = [&](std::size_t i) -> std::int32_t {
        return (i < args.size() && args[i].is_numeric)
            ? static_cast<std::int32_t>(args[i].number) : 0;
    };

    if (uname == "SP_CREATEUSER") {
        if (!dd) { *prc = fail(openads::AE_FUNCTION_NOT_AVAILABLE, "no DD"); return true; }
        if (auto r = dd->create_user(arg(0)); !r) { *prc = fail(r.error()); return true; }
        if (!arg(1).empty()) dd->set_user_property(arg(0), "prop_1101", arg(1));
        if (!arg(2).empty()) dd->set_user_property(arg(0), "prop_1",    arg(2));
        *prc = ok(); return true;
    }
    if (uname == "SP_DROPUSER") {
        if (!dd) { *prc = fail(openads::AE_FUNCTION_NOT_AVAILABLE, "no DD"); return true; }
        if (auto r = dd->delete_user(arg(0)); !r) { *prc = fail(r.error()); return true; }
        *prc = ok(); return true;
    }
    if (uname == "SP_CREATEGROUP") {
        if (!dd) { *prc = fail(openads::AE_FUNCTION_NOT_AVAILABLE, "no DD"); return true; }
        if (auto r = dd->create_group(arg(0)); !r) { *prc = fail(r.error()); return true; }
        if (!arg(1).empty()) dd->set_user_property(arg(0), "prop_1", arg(1));
        *prc = ok(); return true;
    }
    if (uname == "SP_DROPGROUP") {
        if (!dd) { *prc = fail(openads::AE_FUNCTION_NOT_AVAILABLE, "no DD"); return true; }
        if (auto r = dd->delete_group(arg(0)); !r) { *prc = fail(r.error()); return true; }
        *prc = ok(); return true;
    }
    if (uname == "SP_ADDUSERTOGROUP") {
        if (!dd) { *prc = fail(openads::AE_FUNCTION_NOT_AVAILABLE, "no DD"); return true; }
        if (auto r = dd->add_user_to_group(arg(0), arg(1)); !r) { *prc = fail(r.error()); return true; }
        *prc = ok(); return true;
    }
    if (uname == "SP_REMOVEUSERFROMGROUP") {
        if (!dd) { *prc = fail(openads::AE_FUNCTION_NOT_AVAILABLE, "no DD"); return true; }
        if (auto r = dd->remove_user_from_group(arg(0), arg(1)); !r) { *prc = fail(r.error()); return true; }
        *prc = ok(); return true;
    }
    if (uname == "SP_MODIFYUSERPROPERTY") {
        if (!dd) { *prc = fail(openads::AE_FUNCTION_NOT_AVAILABLE, "no DD"); return true; }
        std::string upr = arg(1);
        for (auto& ch : upr) ch = static_cast<char>(
            std::toupper(static_cast<unsigned char>(ch)));
        std::string key;
        if      (upr == "USER_PASSWORD" || upr == "PASSWORD") key = "prop_1101";
        else if (upr == "COMMENT")    key = "prop_1";
        else if (upr == "ENABLE_INTERNET") key = "prop_1104";
        else if (upr == "BAD_LOGINS") { *prc = ok(); return true; }  // read-only
        else                           key = "prop_" + arg(1);
        if (auto r = dd->set_user_property(arg(0), key, arg(2)); !r) { *prc = fail(r.error()); return true; }
        *prc = ok(); return true;
    }
    if (uname == "SP_MODIFYGROUPPROPERTY") {
        if (!dd) { *prc = fail(openads::AE_FUNCTION_NOT_AVAILABLE, "no DD"); return true; }
        std::string upr = arg(1);
        for (auto& ch : upr) ch = static_cast<char>(
            std::toupper(static_cast<unsigned char>(ch)));
        std::string key = (upr == "COMMENT") ? "prop_1" : ("prop_" + arg(1));
        if (auto r = dd->set_user_property(arg(0), key, arg(2)); !r) { *prc = fail(r.error()); return true; }
        *prc = ok(); return true;
    }
    if (uname == "SP_ADDTABLETODATABASE") {
        if (!dd) { *prc = fail(openads::AE_FUNCTION_NOT_AVAILABLE, "no DD"); return true; }
        if (auto r = dd->add_table(arg(0), arg(1)); !r) { *prc = fail(r.error()); return true; }
        // arg(4) may contain semicolon-separated index file paths
        std::string idxlist = arg(4);
        if (!idxlist.empty()) {
            std::string cur;
            idxlist.push_back(';');
            for (char ch : idxlist) {
                if (ch == ';') {
                    if (!cur.empty()) { dd->add_index_file(arg(0), cur, ""); cur.clear(); }
                } else cur.push_back(ch);
            }
        }
        *prc = ok(); return true;
    }
    if (uname == "SP_ADDINDEXFILETODATABASE") {
        if (!dd) { *prc = fail(openads::AE_FUNCTION_NOT_AVAILABLE, "no DD"); return true; }
        if (auto r = dd->add_index_file(arg(0), arg(1), arg(2)); !r) { *prc = fail(r.error()); return true; }
        *prc = ok(); return true;
    }
    if (uname == "SP_MODIFYTABLEPROPERTY") {
        if (!dd) { *prc = fail(openads::AE_FUNCTION_NOT_AVAILABLE, "no DD"); return true; }
        dd->set_db_property("TABLEPROP." + arg(0) + "." + arg(1), arg(2));
        *prc = ok(); return true;
    }
    if (uname == "SP_MODIFYFIELDPROPERTY") {
        if (!dd) { *prc = fail(openads::AE_FUNCTION_NOT_AVAILABLE, "no DD"); return true; }
        std::string upr = arg(2);
        for (auto& ch : upr) ch = static_cast<char>(
            std::toupper(static_cast<unsigned char>(ch)));
        std::string key;
        if      (upr == "FIELD_CAN_BE_NULL" || upr == "REQUIRED")          key = "required";
        else if (upr == "FIELD_DEFAULT_VALUE" || upr == "DEFAULT")         key = "default";
        else if (upr == "FIELD_VALIDATION_RULE" || upr == "VALIDATION_RULE") key = "rule";
        else if (upr == "FIELD_VALIDATION_MSG" || upr == "VALIDATION_MSG") key = "msg";
        else if (upr == "FIELD_MAX_VALUE")                                  key = "max";
        else if (upr == "FIELD_MIN_VALUE")                                  key = "min";
        else if (upr == "COMMENT")                                          key = "comment";
        else                                                                 key = arg(2);
        if (auto r = dd->set_field_property(arg(0), arg(1), key, arg(3)); !r) { *prc = fail(r.error()); return true; }
        *prc = ok(); return true;
    }
    if (uname == "SP_CREATEREFERENTIALINTEGRITY") {
        if (!dd) { *prc = fail(openads::AE_FUNCTION_NOT_AVAILABLE, "no DD"); return true; }
        openads::engine::DataDict::RiEntry e;
        e.name       = arg(0);
        e.parent     = arg(1);
        e.child      = arg(2);
        e.parent_tag = arg(3);
        e.update_opt = std::to_string(ri_int(4));
        e.delete_opt = std::to_string(ri_int(5));
        e.fail_table = arg(6);
        if (auto r = dd->create_ri(e); !r) { *prc = fail(r.error()); return true; }
        *prc = ok(); return true;
    }
    if (uname == "SP_DROPREFERENTIALINTEGRITY") {
        if (!dd) { *prc = fail(openads::AE_FUNCTION_NOT_AVAILABLE, "no DD"); return true; }
        if (auto r = dd->remove_ri(arg(0)); !r) { *prc = fail(r.error()); return true; }
        *prc = ok(); return true;
    }
    if (uname == "SP_CREATELINK") {
        if (!dd) { *prc = fail(openads::AE_FUNCTION_NOT_AVAILABLE, "no DD"); return true; }
        // sp_CreateLink(Name, Dictionary, Global, StaticPath, AuthenticateActiveUser, UserName, Password)
        if (auto r = dd->create_link(arg(0), arg(1), arg(5), arg(6)); !r) { *prc = fail(r.error()); return true; }
        *prc = ok(); return true;
    }
    if (uname == "SP_DROPLINK") {
        if (!dd) { *prc = fail(openads::AE_FUNCTION_NOT_AVAILABLE, "no DD"); return true; }
        if (auto r = dd->drop_link(arg(0)); !r) { *prc = fail(r.error()); return true; }
        *prc = ok(); return true;
    }

    // sp_DisableTriggers([scope[, object]])
    // sp_EnableTriggers([scope[, object]])
    // scope: "CURRENT USER" | "ALL" | table_name | trigger_name (auto-detected)
    // When scope is omitted or "CURRENT USER": disable/enable for this connection only.
    // When scope is a table name: disable/enable all triggers on that table (persisted in DD).
    // When scope is a trigger name: disable/enable that single trigger (persisted in DD).
    // "ALL" disables/enables all triggers for all users (persisted in DD).
    if (uname == "SP_DISABLETRIGGERS" || uname == "SP_ENABLETRIGGERS") {
        bool enable = (uname == "SP_ENABLETRIGGERS");
        std::string scope_raw = arg(0);
        std::string scope_u = scope_raw;
        for (auto& ch : scope_u) ch = static_cast<char>(std::toupper((unsigned char)ch));

        if (scope_raw.empty() || scope_u == "CURRENT USER") {
            // Connection-level disable (non-persistent, this connection only)
            c->set_triggers_disabled(!enable);
            *prc = ok(); return true;
        }
        if (!dd) { *prc = fail(openads::AE_FUNCTION_NOT_AVAILABLE, "no DD"); return true; }
        if (scope_u == "ALL") {
            // All triggers in the DD — persist
            for (auto& [key, trig] : dd->triggers())
                trig.enabled = enable;
            *prc = ok(); return true;
        }
        // Check if scope_raw matches a table alias → disable all triggers for that table
        bool found_table = false;
        for (auto& [key, trig] : dd->triggers()) {
            std::string ta = trig.table_alias;
            std::string sr = scope_raw;
            for (auto& ch : ta) ch = static_cast<char>(std::tolower((unsigned char)ch));
            for (auto& ch : sr) ch = static_cast<char>(std::tolower((unsigned char)ch));
            if (ta == sr) { trig.enabled = enable; found_table = true; }
        }
        if (found_table) { *prc = ok(); return true; }
        // Check if scope_raw matches a trigger name → disable that single trigger
        for (auto& [key, trig] : dd->triggers()) {
            std::string tn = trig.name;
            std::string sr = scope_raw;
            for (auto& ch : tn) ch = static_cast<char>(std::tolower((unsigned char)ch));
            for (auto& ch : sr) ch = static_cast<char>(std::tolower((unsigned char)ch));
            if (tn == sr) { trig.enabled = enable; *prc = ok(); return true; }
        }
        *prc = fail(openads::AE_INTERNAL_ERROR, "trigger or table not found");
        return true;
    }
    if (uname == "SP_MODIFYDATABASE") {
        if (!dd) { *prc = fail(openads::AE_FUNCTION_NOT_AVAILABLE, "no DD"); return true; }
        std::string upr = arg(0);
        for (auto& ch : upr) ch = static_cast<char>(
            std::toupper(static_cast<unsigned char>(ch)));
        std::string key;
        if      (upr == "ADMIN_PASSWORD")          key = "prop_1101";
        else if (upr == "COMMENT")                key = "prop_1";
        else if (upr == "DEFAULT_TABLE_PATH")     key = "prop_3";
        else if (upr == "LOG_IN_REQUIRED")        key = "prop_5";
        else if (upr == "ENABLE_INTERNET")        key = "prop_6";
        else if (upr == "INTERNET_SECURITY_LEVEL")key = "prop_7";
        else if (upr == "VERIFY_ACCESS_RIGHTS")   key = "prop_8";
        else if (upr == "ENCRYPT_NEW_TABLE")      key = "prop_10";
        else if (upr == "MAX_FAILED_ATTEMPTS")    key = "prop_11";
        else if (upr == "TEMP_TABLE_PATH")        key = "prop_12";
        else if (upr == "ENCRYPT_TABLE_PASSWORD") key = "prop_13";
        else if (upr == "VERSION_MAJOR")          key = "prop_14";
        else if (upr == "VERSION_MINOR")          key = "prop_15";
        else                                       key = arg(0);
        if (auto r = dd->set_db_property(key, arg(1)); !r) { *prc = fail(r.error()); return true; }
        *prc = ok(); return true;
    }
    return false;
}

} // extern "C"  — temporarily closed so proc:: helpers get C++ linkage

// ============================================================
// Procedural-body mini-interpreter for DD stored functions.
// Handles the xHarbour-style body language:
//   DECLARE var TYPE;
//   var = expression;
//   IF cond THEN ... [ELSE ...] END IF;
//   RETURN expression;
// Expressions: literals, variables, arithmetic, CREATETIMESTAMP,
// DATEDIFF, STR, TRIM family, CAST (ignored), and recursive DD
// function calls (executed via SELECT ... FROM system.iota).
// ============================================================
namespace proc {

using Scope = std::unordered_map<std::string, std::string>;

static std::string xtrim(const std::string& s) {
    std::size_t f = s.find_first_not_of(" \t\r\n");
    if (f == std::string::npos) return "";
    std::size_t l = s.find_last_not_of(" \t\r\n");
    return s.substr(f, l - f + 1);
}
static std::string xupper(const std::string& s) {
    std::string r; r.reserve(s.size());
    for (char c : s) r.push_back(static_cast<char>(std::toupper((unsigned char)c)));
    return r;
}

// Split body into semicolon-terminated statements (respects strings + parens).
static std::vector<std::string> split_stmts(const std::string& body) {
    std::vector<std::string> out;
    std::string cur;
    int depth = 0; bool in_str = false;
    for (std::size_t i = 0; i < body.size(); ++i) {
        char c = body[i];
        if (in_str) {
            cur.push_back(c);
            if (c=='\'' && i+1<body.size() && body[i+1]=='\'') cur.push_back(body[++i]);
            else if (c=='\'') in_str=false;
        } else {
            if      (c=='\'') { in_str=true; cur.push_back(c); }
            else if (c=='(')  { ++depth; cur.push_back(c); }
            else if (c==')')  { --depth; cur.push_back(c); }
            else if (c==';' && depth==0) {
                std::string t=xtrim(cur); if (!t.empty()) out.push_back(t); cur.clear();
            } else cur.push_back(c);
        }
    }
    std::string t=xtrim(cur); if (!t.empty()) out.push_back(t);
    return out;
}

// Split a top-level comma-separated argument list.
static std::vector<std::string> split_args(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    int depth=0; bool in_str=false;
    for (std::size_t i=0; i<s.size(); ++i) {
        char c=s[i];
        if (in_str) {
            cur.push_back(c);
            if (c=='\'' && i+1<s.size() && s[i+1]=='\'') cur.push_back(s[++i]);
            else if (c=='\'') in_str=false;
        } else {
            if      (c=='\'') { in_str=true; cur.push_back(c); }
            else if (c=='(')  { ++depth; cur.push_back(c); }
            else if (c==')')  { --depth; cur.push_back(c); }
            else if (c==',' && depth==0) {
                out.push_back(xtrim(cur)); cur.clear();
            } else cur.push_back(c);
        }
    }
    std::string t=xtrim(cur); if (!t.empty()) out.push_back(t);
    return out;
}

// Convert date string (MM/DD/YYYY or YYYY-MM-DD) to Julian Day Number.
static long date_to_jdn(int y, int m, int d) {
    long a=(14-m)/12, y2=y+4800-a, m2=m+12*a-3;
    return d+(153*m2+2)/5+365*y2+y2/4-y2/100+y2/400-32045;
}
static long parse_jdn(const std::string& s) {
    if (s.size()==10 && s[2]=='/' && s[5]=='/') {
        int mo=std::atoi(s.substr(0,2).c_str()), d=std::atoi(s.substr(3,2).c_str()), y=std::atoi(s.substr(6).c_str());
        return date_to_jdn(y,mo,d);
    }
    if (s.size()==10 && s[4]=='-' && s[7]=='-') {
        int y=std::atoi(s.substr(0,4).c_str()), mo=std::atoi(s.substr(5,2).c_str()), d=std::atoi(s.substr(8).c_str());
        return date_to_jdn(y,mo,d);
    }
    return 0;
}

// Forward declaration.
static std::string eval(const std::string& expr, Scope& scope, ADSHANDLE hStmt);

// Evaluate a function call whose arguments have already been evaluated.
static std::string call_builtin(const std::string& fn_up, const std::vector<std::string>& ev,
                                Scope& /*scope*/, ADSHANDLE /*hStmt*/) {
    if (fn_up=="CREATETIMESTAMP" && ev.size()>=3) {
        int y=std::atoi(ev[0].c_str()), mo=std::atoi(ev[1].c_str()), d=std::atoi(ev[2].c_str());
        char buf[16]; std::snprintf(buf,sizeof(buf),"%02d/%02d/%04d",mo,d,y); return buf;
    }
    if (fn_up=="DATEDIFF" && ev.size()>=2) {
        long j1=parse_jdn(ev[0]), j2=parse_jdn(ev[1]);
        char buf[16]; std::snprintf(buf,sizeof(buf),"%ld",j1-j2); return buf;
    }
    if (fn_up=="STR" && !ev.empty()) {
        double n=std::strtod(ev[0].c_str(),nullptr);
        int len=ev.size()>=2?std::atoi(ev[1].c_str()):10;
        int dec=ev.size()>=3?std::atoi(ev[2].c_str()):0;
        char fmt[16]; std::snprintf(fmt,sizeof(fmt),"%%%d.%df",len,dec);
        char buf[64]; std::snprintf(buf,sizeof(buf),fmt,n); return buf;
    }
    if ((fn_up=="LTRIM"||fn_up=="RTRIM"||fn_up=="TRIM"||fn_up=="ALLTRIM") && !ev.empty()) {
        std::string s=ev[0];
        if (fn_up!="RTRIM") { auto p=s.find_first_not_of(' '); s=(p!=std::string::npos)?s.substr(p):""; }
        if (fn_up!="LTRIM") { auto p=s.find_last_not_of(' ');  if (p!=std::string::npos) s.resize(p+1); else s=""; }
        return s;
    }
    if ((fn_up=="LEN"||fn_up=="LENGTH") && !ev.empty()) {
        char buf[16]; std::snprintf(buf,sizeof(buf),"%zu",ev[0].size()); return buf;
    }
    // CAST(x AS type) — ignore type, return value as-is
    if (fn_up=="CAST" && !ev.empty()) return ev[0];
    // IIF(cond, t, f)
    if (fn_up=="IIF" && ev.size()>=3) {
        bool c=(ev[0]!="0" && ev[0]!="" && ev[0]!="false" && ev[0]!=".F.");
        return c ? ev[1] : ev[2];
    }
    if ((fn_up=="INT"||fn_up=="VAL") && !ev.empty()) {
        char buf[32]; std::snprintf(buf,sizeof(buf),"%.0f",std::strtod(ev[0].c_str(),nullptr)); return buf;
    }
    return ""; // not a known builtin
}

static std::string eval(const std::string& expr_in, Scope& scope, ADSHANDLE hStmt) {
    std::string e = xtrim(expr_in);
    if (e.empty()) return "";

    // Numeric literal
    {
        std::size_t i=0;
        if (i<e.size() && (e[i]=='+'||e[i]=='-')) ++i;
        std::size_t s2=i;
        while (i<e.size() && (std::isdigit((unsigned char)e[i])||e[i]=='.')) ++i;
        if (i>s2 && i==e.size()) return e;  // pure number
    }

    // String literal
    if (!e.empty() && e.front()=='\'') {
        std::string r;
        for (std::size_t i=1; i<e.size(); ) {
            if (e[i]=='\'' && i+1<e.size() && e[i+1]=='\'') { r.push_back('\''); i+=2; }
            else if (e[i]=='\'') break;
            else r.push_back(e[i++]);
        }
        return r;
    }

    // Strip balanced outer parens
    if (!e.empty() && e.front()=='(') {
        int pd=0; bool all=true;
        for (std::size_t i=0; i<e.size(); ++i) {
            if (e[i]=='(') ++pd; else if (e[i]==')') { --pd; if (pd==0 && i+1<e.size()) { all=false; break; } }
        }
        if (all && pd==0) return eval(e.substr(1,e.size()-2), scope, hStmt);
    }

    // Find rightmost top-level + or - (handles arithmetic and string concat)
    {
        int dp=0; bool in_s=false;
        std::size_t op_pos=std::string::npos; char op_c=0;
        for (std::size_t i=0; i<e.size(); ++i) {
            char c=e[i];
            if (in_s) { if (c=='\'' && i+1<e.size() && e[i+1]=='\'') ++i; else if (c=='\'') in_s=false; continue; }
            if (c=='\'') { in_s=true; continue; }
            if (c=='(') ++dp; else if (c==')') --dp;
            if (dp==0 && (c=='+'||c=='-') && i>0) {
                char p=e[i-1];
                if (p!='('&&p!='+'&&p!='-'&&p!='*'&&p!='/'&&p!=',') { op_pos=i; op_c=c; }
            }
        }
        if (op_pos!=std::string::npos) {
            std::string lv=eval(e.substr(0,op_pos),scope,hStmt);
            std::string rv=eval(e.substr(op_pos+1),scope,hStmt);
            // Both numeric → arithmetic
            char *ep1,*ep2;
            double a=std::strtod(lv.c_str(),&ep1), b=std::strtod(rv.c_str(),&ep2);
            if (ep1!=lv.c_str()&&*ep1=='\0' && ep2!=rv.c_str()&&*ep2=='\0') {
                double r=(op_c=='+')?a+b:a-b;
                char buf[32]; if(r==std::floor(r)) std::snprintf(buf,sizeof(buf),"%.0f",r); else std::snprintf(buf,sizeof(buf),"%g",r); return buf;
            }
            if (op_c=='+') return lv+rv; // string concat
            return lv;
        }
    }

    // Function call: identifier (possibly @-prefixed) followed by '('
    {
        std::size_t fe=0;
        if (fe<e.size() && e[fe]=='@') ++fe;  // allow @func() syntax (rare but possible)
        while (fe<e.size() && (std::isalnum((unsigned char)e[fe])||e[fe]=='_')) ++fe;
        std::size_t lp=fe;
        while (lp<e.size() && std::isspace((unsigned char)e[lp])) ++lp;
        if (fe>0 && lp<e.size() && e[lp]=='(') {
            std::string fname=e.substr(0,fe);
            std::string fu=xupper(fname);
            // extract balanced arg list
            std::string arglist; int dp=1; std::size_t k=lp+1;
            while (k<e.size() && dp>0) {
                char c=e[k++];
                if      (c=='(') ++dp;
                else if (c==')') { --dp; if (dp==0) break; }
                arglist.push_back(c);
            }
            // evaluate each arg
            std::vector<std::string> raw_args=split_args(arglist);
            std::vector<std::string> ev_args;
            ev_args.reserve(raw_args.size());
            for (auto& a : raw_args) ev_args.push_back(eval(a,scope,hStmt));

            // Try built-ins first
            std::string bres=call_builtin(fu,ev_args,scope,hStmt);
            if (!bres.empty()) return bres;

            // Try as DD function: SELECT fname(quoted_args) FROM system.iota
            {
                std::string sel_args;
                for (std::size_t i=0; i<ev_args.size(); ++i) {
                    if (i) sel_args+=", ";
                    char *ep; std::strtod(ev_args[i].c_str(),&ep);
                    bool is_num=(*ep=='\0' && !ev_args[i].empty());
                    if (is_num) sel_args+=ev_args[i];
                    else {
                        sel_args+='\'';
                        for (char c : ev_args[i]) { if (c=='\'') sel_args+='\''; sel_args+=c; }
                        sel_args+='\'';
                    }
                }
                std::string sel="SELECT "+fname+"("+sel_args+") FROM system.iota";
                std::vector<UNSIGNED8> sbuf(sel.size()+1);
                std::memcpy(sbuf.data(),sel.c_str(),sel.size()+1);
                ADSHANDLE tc=0;
                if (AdsExecuteSQLDirect(hStmt,sbuf.data(),&tc)==openads::AE_SUCCESS && tc!=0) {
                    AdsGotoTop(tc);
                    UNSIGNED16 nf=0; AdsGetNumFields(tc,&nf);
                    std::string r;
                    if (nf>0) {
                        UNSIGNED8 fn[256]={}; UNSIGNED16 fl=sizeof(fn)-1;
                        AdsGetFieldName(tc,1,fn,&fl); fn[fl]=0;
                        UNSIGNED32 vl=256; std::vector<char> vb(vl+1,'\0');
                        AdsGetString(tc,fn,(UNSIGNED8*)vb.data(),&vl,0);
                        r=std::string(vb.data(),(std::size_t)vl);
                    }
                    AdsCloseTable(tc);
                    return r;
                }
            }
            return "";
        }
    }

    // Plain identifier / keyword / variable reference
    std::string eu=xupper(e);
    if (eu==".T."||eu=="TRUE")  return "1";
    if (eu==".F."||eu=="FALSE") return "0";
    // scope lookup (case-insensitive, @-prefix preserved)
    auto it=scope.find(eu);
    if (it!=scope.end()) return it->second;
    return "";
}

// Execute a procedural body with `scope` pre-populated from param substitution.
// Returns the value of the RETURN statement, or "" if none reached.
static std::string exec_body(const std::string& body, Scope& scope, ADSHANDLE hStmt) {
    auto stmts=split_stmts(body);
    for (auto& s : stmts) {
        std::string su=xupper(s);
        // DECLARE varname TYPE
        if (su.rfind("DECLARE ",0)==0) {
            std::string rest=xtrim(s.substr(8));
            std::size_t ve=0;
            while (ve<rest.size() && (std::isalnum((unsigned char)rest[ve])||rest[ve]=='_'||rest[ve]=='@')) ++ve;
            std::string vn=rest.substr(0,ve);
            if (!vn.empty()) scope[xupper(vn)]="";
            continue;
        }
        // RETURN expression
        if (su.rfind("RETURN",0)==0 && (su.size()==6||!std::isalnum((unsigned char)su[6]))) {
            return eval(xtrim(s.substr(6)),scope,hStmt);
        }
        // IF cond THEN stmts [ELSE stmts] END IF
        // (Implemented as a single-pass block search within the statement list.
        //  For now, skip IF blocks — they are handled by re-parsing the body
        //  with IF as a sub-body delimiter.)
        if (su.rfind("IF ",0)==0) {
            // Minimal IF: find THEN and ELSE/END within subsequent statements.
            // Build true/false sub-bodies from the statement stream.
            // This is complex; skip for first pass — most DD functions
            // don't need it when expressions use IIF() instead.
            continue;
        }
        // Assignment: find top-level '='
        std::size_t eq=std::string::npos;
        { bool in_s=false; int dp=0;
          for (std::size_t i=0; i<s.size(); ++i) {
              char c=s[i];
              if (in_s) { if (c=='\'' && i+1<s.size() && s[i+1]=='\'') ++i; else if (c=='\'') in_s=false; continue; }
              if (c=='\'') { in_s=true; continue; }
              if (c=='(') ++dp; else if (c==')') --dp;
              if (dp==0&&c=='='&&i>0) { char p=s[i-1],n=(i+1<s.size()?s[i+1]:'\0');
                if (p!='<'&&p!='>'&&p!='!'&&p!='='&&n!='=') { eq=i; break; } }
          }
        }
        if (eq!=std::string::npos) {
            std::string lhs=xupper(xtrim(s.substr(0,eq)));
            std::string rhs=xtrim(s.substr(eq+1));
            scope[lhs]=eval(rhs,scope,hStmt);
        }
    }
    return "";
}

} // namespace proc

extern "C" {  // reopen for the ACE API exports

UNSIGNED32 AdsExecuteSQLDirect(ADSHANDLE hStatement, UNSIGNED8* pucSQL,
                               ADSHANDLE* phCursor) {
    if (phCursor == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    SqlStatement* st_ptr = stmt_lookup(hStatement);
    if (st_ptr == nullptr) return fail(openads::AE_INTERNAL_ERROR, "unknown stmt");
    // Alias so the long body below keeps its `it->second->...` accesses
    // unchanged; the lookup is now serialised and the pointer is used off
    // the lock (only the owning thread executes this handle).
    std::pair<const ADSHANDLE, SqlStatement*> it_kv{hStatement, st_ptr};
    auto* it = &it_kv;
    // M12.7 — remote SQL exec. The statement was created against a
    // RemoteConnection; ship the SQL over the wire, allocate a
    // RemoteTable handle around the returned cursor table-id, and
    // hand the resulting ADSHANDLE back to the caller. From here on
    // every Ads* read on the returned handle (GetField, Skip, etc.)
    // routes through the same RemoteTable plumbing M12.4 / M12.5
    // already wired up.
    if (it->second->remote != nullptr) {
        auto sqlstr = openads::abi::to_internal(pucSQL, 0);
        auto r = it->second->remote->execute_sql(sqlstr);
        if (!r) return fail(r.error());
        std::uint32_t cur_id = r.value();
        if (cur_id == 0) {
            *phCursor = 0;
            return ok();
        }
        auto& s = state();
        std::lock_guard<std::recursive_mutex> lk(s.mu);
        auto rt = std::make_unique<openads::network::RemoteTable>();
        rt->conn = it->second->remote;
        rt->id   = cur_id;
        Handle h = s.registry.register_object(
            HandleKind::RemoteTable, rt.get());
        remote_sql_cursors_map()[h] = std::move(rt);
        *phCursor = h;
        return ok();
    }
#if defined(OPENADS_WITH_SQLITE)
    if (it->second->sqlite != nullptr) {
        auto sqlstr = openads::abi::to_internal(pucSQL, 0);
        // Let SQLite classify the statement (it knows the column count): run_sql
        // returns a navigable cursor for a result-producing statement, or a null
        // pointer for an executed INSERT/UPDATE/DELETE/DDL — no SQL parsing here.
        auto r = it->second->sqlite->run_sql(sqlstr);
        if (!r) return fail(r.error());
        auto cursor = std::move(r).value();
        if (!cursor) { *phCursor = 0; return ok(); }
        auto& s = state();
        std::lock_guard<std::recursive_mutex> lk(s.mu);
        openads::sql_backend::SqliteTable* raw = cursor.get();
        Handle h = s.registry.register_object(HandleKind::SqliteTable, raw);
        sqlite_tables_map().emplace(h, std::move(cursor));
        *phCursor = h;
        return ok();
    }
#endif
#if defined(OPENADS_WITH_MSSQL)
    if (it->second->mssql_conn != nullptr) {
        auto sqlstr = openads::abi::to_internal(pucSQL, 0);
        auto qr = it->second->mssql_conn->query(sqlstr);
        if (!qr) return fail(qr.error());
        openads::sql_backend::tds::QueryResult result = std::move(qr).value();
        if (!result.ok) {
            return fail(static_cast<int>(result.error_number),
                        result.message.c_str());
        }
        auto st = openads::sql_backend::MssqlTable::from_result(
            std::move(result));
        auto& s = state();
        std::lock_guard<std::recursive_mutex> lk(s.mu);
        Handle h = s.registry.register_object(
            HandleKind::MssqlTable, st.get());
        mssql_tables_map().emplace(h, std::move(st));
        *phCursor = h;
        return ok();
    }
#endif
    Connection* c = it->second->conn;
    if (!c) return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
    auto sql = openads::abi::to_internal(pucSQL, 0);

    // Open a table by name, transparently resolving "system.*" virtual tables
    // to temp DBF files materialized from DD state.
    auto open_or_sys = [c](const std::string& tname,
                            openads::engine::TableType  ttype,
                            openads::engine::OpenMode   omode,
                            openads::engine::LockingMode lmode)
        -> openads::util::Result<Handle> {
        std::string resolved = tname;
        if (tname.size() > 7) {
            std::string px = tname.substr(0, 7);
            for (auto& ch : px) ch = static_cast<char>(
                std::tolower(static_cast<unsigned char>(ch)));
            if (px == "system.") {
                resolved = build_system_dbf(c, tname.substr(7));
                if (resolved.empty())
                    return openads::util::Error{
                        openads::AE_NO_FILE_FOUND, 0, tname, ""};
                ttype = openads::engine::TableType::Adt;
                omode = openads::engine::OpenMode::Read;
                lmode = openads::engine::LockingMode::Compatible;
            }
        }
        return c->open_table(resolved, ttype, omode, lmode);
    };

    // Per-operation ACL check for SQL statements when check_rights is set.
    if (it->second->check_rights != 0 && c->has_dd() && !c->username().empty()) {
        std::string obj_name;
        enum class SqlOp { None, Select, Insert, Update, Delete, Execute } op =
            SqlOp::None;

        if (openads::sql::sql_is_insert(sql)) {
            if (auto p = openads::sql::parse_insert(sql))
                { obj_name = p.value().table; op = SqlOp::Insert; }
        } else if (openads::sql::sql_is_update(sql)) {
            if (auto p = openads::sql::parse_update(sql))
                { obj_name = p.value().table; op = SqlOp::Update; }
        } else if (openads::sql::sql_is_delete(sql)) {
            if (auto p = openads::sql::parse_delete(sql))
                { obj_name = p.value().table; op = SqlOp::Delete; }
        } else {
            // SELECT or EXECUTE PROCEDURE
            if (auto p = openads::sql::parse_select(sql)) {
                obj_name = p.value().table;
                // Detect "EXECUTE PROCEDURE sp_*" by checking table starts with "sp_"
                // or whether the FROM target is a StoredProc/Function in the DD.
                auto* dd = c->dd();
                if (dd && dd->has_proc(obj_name))
                    op = SqlOp::Execute;
                else
                    op = SqlOp::Select;
            }
        }

        if (!obj_name.empty() && op != SqlOp::None) {
            auto ops = eff_ops(c, obj_name);
            bool denied = false;
            switch (op) {
                case SqlOp::Select:  denied = !ops.select_;  break;
                case SqlOp::Insert:  denied = !ops.insert_;  break;
                case SqlOp::Update:  denied = !ops.update_;  break;
                case SqlOp::Delete:  denied = !ops.delete_;  break;
                case SqlOp::Execute: denied = !ops.execute_; break;
                default: break;
            }
            if (denied)
                return fail(openads::AE_ACCESS_DENIED, obj_name.c_str());
        }
    }

    // M10.5/M10.7/M10.9: dispatch on the leading keyword. INSERT /
    // UPDATE / DELETE / CREATE TABLE / CREATE INDEX write through
    // the engine and return no cursor (phCursor → 0); SELECT keeps
    // the M9.21 path.
    if (openads::sql::sql_is_create_table(sql)) {
        auto& s = state();
        auto ct = openads::sql::parse_create_table(sql);
        if (!ct) return fail(ct.error());

        // M10.42 — CREATE TABLE t AS SELECT ...: recursively run the
        // inner SELECT, build the new table's schema from the result
        // cursor's projected fields, then walk + insert each row.
        if (!ct.value().select_sql.empty()) {
            std::vector<UNSIGNED8> selbuf(ct.value().select_sql.size() + 1);
            std::memcpy(selbuf.data(),
                        ct.value().select_sql.c_str(),
                        ct.value().select_sql.size() + 1);
            ADSHANDLE srcCur = 0;
            UNSIGNED32 rrc =
                AdsExecuteSQLDirect(hStatement, selbuf.data(), &srcCur);
            if (rrc != 0) return rrc;
            std::lock_guard<std::recursive_mutex> lk2(s.mu);
            openads::engine::Table* src =
                s.registry.lookup<openads::engine::Table>(
                    srcCur, HandleKind::Table);
            if (!src) {
                return fail(openads::AE_INTERNAL_ERROR,
                            "CTAS inner cursor lookup");
            }
            // Build schema from inner cursor (projection-aware).
            const auto* proj = projection_for(srcCur);
            std::vector<openads::drivers::DbfField> schema;
            if (proj) {
                for (auto idx : *proj) schema.push_back(src->field_descriptor(idx));
            } else {
                std::uint16_t nf = src->field_count();
                for (std::uint16_t k = 0; k < nf; ++k) {
                    schema.push_back(src->field_descriptor(k));
                }
            }
            // Determine target table type. If the caller explicitly set a
            // type via AdsStmtSetTableType(), honour it; otherwise mirror
            // the source so that an ADT source produces an ADT target and a
            // DBF source produces a CDX target.  Hardcoding ADS_CDX here
            // caused silent schema corruption: ADT field types (Money,
            // Timestamp, ShortInt, …) have no DBF equivalent, so they were
            // silently coerced to Character when the target was forced to
            // .dbf regardless of the source format.
            UNSIGNED16 ctas_tgt_type = it->second->table_type;
            if (ctas_tgt_type == 0 || ctas_tgt_type == ADS_DEFAULT) {
                UNSIGNED16 src_type = ADS_CDX;
                AdsGetTableType(srcCur, &src_type);
                ctas_tgt_type = (src_type == ADS_ADT) ? ADS_ADT : ADS_CDX;
            }
            // Build NAME,Type,Len,Dec;… from schema.
            // Two switch blocks: the first covers printable-letter DBF type
            // codes ('C', 'N', …); the second covers ADT numeric type codes
            // (1–20) stored as raw bytes by adt_driver.cpp.  The ranges are
            // disjoint (DBF letters are all ≥ 0x42; ADT codes top out at 20)
            // so there is no ambiguity.
            auto type_name = [](char raw) -> const char* {
                switch (raw) {
                    case 'C': return "Character";
                    case 'N': return "Numeric";
                    case 'D': return "Date";
                    case 'L': return "Logical";
                    case 'M': return "Memo";
                    case 'F': return "Float";
                    case 'I': return "Integer";
                    case 'Y': return "Currency";
                    case 'B': return "Double";
                    case 'V': return "Varchar";
                    case 'Q': return "Varbinary";
                }
                switch (static_cast<std::uint8_t>(raw)) {
                    case  1: return "Logical";
                    case  3: return "Date";
                    case  4: return "Character";
                    case  5: return "Memo";
                    case  6: return "Binary";
                    case  7: return "Image";
                    case 10: return "Double";
                    case 11: return "Integer";
                    case 12: return "ShortInt";
                    case 13: return "Time";
                    case 14: return "Timestamp";
                    case 15: return "AutoInc";
                    case 18: return "Money";
                    case 20: return "CiCharacter";
                }
                return "Character";
            };
            std::string defs;
            for (auto& fd : schema) {
                if (!defs.empty()) defs.push_back(';');
                defs += fd.name;
                defs.push_back(',');
                defs += type_name(static_cast<char>(fd.raw_type));
                if (fd.length > 0) {
                    defs.push_back(',');
                    defs += std::to_string(fd.length);
                }
                if (fd.decimals > 0) {
                    defs.push_back(',');
                    defs += std::to_string(fd.decimals);
                }
            }
            std::vector<UNSIGNED8> name_buf(ct.value().table.size() + 1, 0);
            std::memcpy(name_buf.data(), ct.value().table.data(),
                        ct.value().table.size());
            std::vector<UNSIGNED8> def_buf(defs.size() + 1, 0);
            std::memcpy(def_buf.data(), defs.data(), defs.size());
            ADSHANDLE conn_h = 0;
            s.registry.for_each_handle([&](Handle h, HandleKind k, void* p) {
                if (k != HandleKind::Connection) return;
                if (static_cast<Connection*>(p) == c) conn_h = h;
            });
            if (conn_h == 0) {
                AdsCloseTable(srcCur);
                return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
            }
            ADSHANDLE hTable = 0;
            UNSIGNED32 rc = AdsCreateTable(conn_h, name_buf.data(), nullptr,
                                           ctas_tgt_type, 0, 0, 0, 0,
                                           def_buf.data(), &hTable);
            if (rc != openads::AE_SUCCESS) {
                AdsCloseTable(srcCur);
                return rc;
            }
            openads::engine::Table* tgt =
                s.registry.lookup<openads::engine::Table>(
                    hTable, HandleKind::Table);
            if (!tgt) {
                AdsCloseTable(srcCur);
                AdsCloseTable(hTable);
                return fail(openads::AE_INTERNAL_ERROR, "CTAS post-create");
            }
            // Walk source rows.
            std::vector<std::uint32_t> recnos;
            if (src->has_recno_sequence()) {
                recnos = src->recno_sequence();
            } else {
                std::uint32_t rcount = src->record_count();
                for (std::uint32_t r = 1; r <= rcount; ++r) {
                    if (auto g = src->goto_record(r); !g) continue;
                    if (src->is_deleted()) continue;
                    if (!src->passes_filter()) continue;
                    recnos.push_back(r);
                }
            }
            // Pre-resolve src column → tgt column by name match.
            std::vector<std::uint16_t> src_cols(schema.size());
            std::vector<std::uint16_t> tgt_cols(schema.size());
            for (std::size_t i = 0; i < schema.size(); ++i) {
                src_cols[i] = proj ? (*proj)[i] : static_cast<std::uint16_t>(i);
                std::int32_t fi = tgt->field_index(schema[i].name);
                if (fi < 0) {
                    AdsCloseTable(srcCur);
                    AdsCloseTable(hTable);
                    return fail(openads::AE_INTERNAL_ERROR,
                                "CTAS target field missing");
                }
                tgt_cols[i] = static_cast<std::uint16_t>(fi);
            }
            for (std::uint32_t r : recnos) {
                if (auto g = src->goto_record(r); !g) continue;
                if (auto ar = tgt->append_record(); !ar) {
                    AdsCloseTable(srcCur);
                    AdsCloseTable(hTable);
                    return fail(ar.error());
                }
                for (std::size_t i = 0; i < schema.size(); ++i) {
                    auto v = src->read_field(src_cols[i]);
                    std::string sv = v ? v.value().as_string : std::string();
                    auto wr = tgt->set_field(tgt_cols[i], sv);
                    if (!wr) {
                        AdsCloseTable(srcCur);
                        AdsCloseTable(hTable);
                        return fail(wr.error());
                    }
                }
            }
            (void)tgt->flush();
            AdsCloseTable(srcCur);
            AdsCloseTable(hTable);
            *phCursor = 0;
            return ok();
        }

        // Use the caller-supplied table type (AdsStmtSetTableType), falling
        // back to CDX.  There is no source table to infer from here, so the
        // default is always CDX; callers that want ADT must set it explicitly.
        UNSIGNED16 ct_tgt_type = it->second->table_type;
        if (ct_tgt_type == 0 || ct_tgt_type == ADS_DEFAULT) ct_tgt_type = ADS_CDX;

        // Build the rddads `NAME,Type,Len,Dec;…` field-def string and
        // route through AdsCreateTable so M9.5's parser owns the
        // schema-write logic.
        std::string defs;
        for (const auto& col : ct.value().columns) {
            if (!defs.empty()) defs.push_back(';');
            defs += col.name;
            defs.push_back(',');
            defs += col.type;
            if (col.length > 0) {
                defs.push_back(',');
                defs += std::to_string(col.length);
            }
            if (col.decimals > 0) {
                defs.push_back(',');
                defs += std::to_string(col.decimals);
            }
        }
        std::vector<UNSIGNED8> name_buf(ct.value().table.size() + 1, 0);
        std::memcpy(name_buf.data(), ct.value().table.data(),
                    ct.value().table.size());
        std::vector<UNSIGNED8> def_buf(defs.size() + 1, 0);
        std::memcpy(def_buf.data(), defs.data(), defs.size());
        ADSHANDLE hTable = 0;
        // Resolve the connection's registry handle so AdsCreateTable
        // can look it up the same way external callers do.
        ADSHANDLE conn_h = 0;
        s.registry.for_each_handle([&](Handle h, HandleKind k, void* p) {
            if (k != HandleKind::Connection) return;
            if (static_cast<Connection*>(p) == c) conn_h = h;
        });
        if (conn_h == 0) return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
        UNSIGNED32 rc = AdsCreateTable(conn_h, name_buf.data(), nullptr,
                                       ct_tgt_type, 0, 0, 0, 0,
                                       def_buf.data(), &hTable);
        if (rc != openads::AE_SUCCESS) return rc;
        // Close the table immediately; CREATE TABLE returns no cursor.
        AdsCloseTable(hTable);
        *phCursor = 0;
        return ok();
    }

    if (openads::sql::sql_is_create_index(sql)) {
        auto& s = state();
        auto ci = openads::sql::parse_create_index(sql);
        if (!ci) return fail(ci.error());
        // Resolve the connection handle and open the table to obtain
        // an ADSHANDLE for AdsCreateIndex61.
        ADSHANDLE conn_h = 0;
        s.registry.for_each_handle([&](Handle h, HandleKind k, void* p) {
            if (k != HandleKind::Connection) return;
            if (static_cast<Connection*>(p) == c) conn_h = h;
        });
        if (conn_h == 0) return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
        std::vector<UNSIGNED8> name_buf(ci.value().table.size() + 1, 0);
        std::memcpy(name_buf.data(), ci.value().table.data(),
                    ci.value().table.size());
        ADSHANDLE hTable = 0;
        if (auto rc = AdsOpenTable(conn_h, name_buf.data(), name_buf.data(),
                                   ADS_CDX, 1, 1, 0, 1, &hTable);
            rc != openads::AE_SUCCESS) {
            return rc;
        }

        // CREATE INDEX writes a structural .adi sidecar named after
        // the table's stem so AdsOpenTable auto-attaches it next open.
        namespace fs = std::filesystem;
        fs::path tbl_path(c->data_dir());
        tbl_path /= ci.value().table;
        if (!tbl_path.has_extension()) tbl_path.replace_extension(".dbf");
        fs::path bag = tbl_path;
        bag.replace_extension(".cdx");

        std::vector<UNSIGNED8> bag_buf(bag.string().size() + 1, 0);
        std::memcpy(bag_buf.data(), bag.string().data(),
                    bag.string().size());
        std::vector<UNSIGNED8> tag_buf(ci.value().tag.size() + 1, 0);
        std::memcpy(tag_buf.data(), ci.value().tag.data(),
                    ci.value().tag.size());
        std::vector<UNSIGNED8> expr_buf(ci.value().expression.size() + 1, 0);
        std::memcpy(expr_buf.data(), ci.value().expression.data(),
                    ci.value().expression.size());
        UNSIGNED32 opts = 0;
        // Re-encode for AdsCreateIndex61's decode. That decode treats a
        // .cdx (compound) tag as descending only when BOTH the compound and
        // descending bits are set (the two RDD clients disagree on which bit
        // is which — see the decode comment there), so a descending index
        // must carry ADS_COMPOUND | ADS_DESCENDING (0x0A), not 0x08 alone.
        if (ci.value().unique)     opts |= ADS_UNIQUE;
        if (ci.value().descending) opts |= (ADS_COMPOUND | ADS_DESCENDING);
        ADSHANDLE hIdx = 0;
        UNSIGNED32 rc = AdsCreateIndex61(
            hTable, bag_buf.data(), tag_buf.data(),
            expr_buf.data(), nullptr, nullptr,
            opts, 512, &hIdx);
        AdsCloseTable(hTable);
        if (rc != openads::AE_SUCCESS) return rc;
        *phCursor = 0;
        return ok();
    }

    // `CREATE DATABASE "path" [PASSWORD ... DESCRIPTION ... ENCRYPT ...]`
    if (openads::sql::sql_is_create_database(sql)) {
        auto cd = openads::sql::parse_create_database(sql);
        if (!cd) return fail(cd.error());
        namespace fs = std::filesystem;
        std::string path = cd.value().path;
        if (fs::path(path).is_relative())
            path = (fs::path(c->data_dir()) / path).string();
        auto r = openads::engine::DataDict::create(path);
        if (!r) return fail(r.error());
        if (!cd.value().password.empty())
            r.value().set_db_property("prop_1101", cd.value().password);
        if (!cd.value().description.empty())
            r.value().set_db_property("prop_1", cd.value().description);
        *phCursor = 0;
        return ok();
    }

    // `GRANT right [("col")] ON object TO principal`
    if (openads::sql::sql_is_grant(sql)) {
        auto gs = openads::sql::parse_grant(sql);
        if (!gs) return fail(gs.error());
        if (c->has_dd()) {
            auto* dd = c->dd();
            const auto& g = gs.value();
            // Map right name → bitmask.  Grants accumulate (OR into existing).
            using DD = openads::engine::DataDict;
            uint32_t new_bits = 0;
            std::string r = g.right;
            for (auto& ch : r)
                ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            if      (r == "ALL")       new_bits = DD::DD_PERM_FULL;
            else if (r == "SELECT")    new_bits = DD::DD_PERM_SELECT;
            else if (r == "INSERT")    new_bits = DD::DD_PERM_INSERT;
            else if (r == "UPDATE")    new_bits = DD::DD_PERM_UPDATE;
            else if (r == "DELETE")    new_bits = DD::DD_PERM_DELETE;
            else if (r == "EXECUTE")   new_bits = DD::DD_PERM_EXECUTE;
            else if (r == "REFERENCE" || r == "REFERENCES")
                                       new_bits = DD::DD_PERM_REFERENCE;
            else                       new_bits = DD::DD_PERM_FULL;  // unknown → full
            // Determine object type from the DD.
            std::string obj_type = "Table";
            if (dd->has_proc(g.object))     obj_type = "StoredProc";
            else if (dd->has_function(g.object)) obj_type = "Function";
            else if (dd->has_view(g.object))     obj_type = "View";
            // Accumulate with existing grant for same grantee+object.
            uint32_t cur_bits = 0;
            for (const auto& pe : dd->permissions())
                if (pe.object_name == g.object && pe.grantee == g.principal)
                    cur_bits |= pe.bitmask;
            dd->grant_permission(obj_type, g.object, g.principal, cur_bits | new_bits);
        }
        *phCursor = 0;
        return ok();
    }

    // `REVOKE right [("col")] ON object FROM principal`
    if (openads::sql::sql_is_revoke(sql)) {
        auto gs = openads::sql::parse_revoke(sql);
        if (!gs) return fail(gs.error());
        if (c->has_dd()) {
            auto* dd = c->dd();
            const auto& g = gs.value();
            using DD = openads::engine::DataDict;
            uint32_t revoke_bits = DD::DD_PERM_FULL;
            std::string r = g.right;
            for (auto& ch : r)
                ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            if      (r == "SELECT")    revoke_bits = DD::DD_PERM_SELECT;
            else if (r == "INSERT")    revoke_bits = DD::DD_PERM_INSERT;
            else if (r == "UPDATE")    revoke_bits = DD::DD_PERM_UPDATE;
            else if (r == "DELETE")    revoke_bits = DD::DD_PERM_DELETE;
            else if (r == "EXECUTE")   revoke_bits = DD::DD_PERM_EXECUTE;
            // Compute new bitmask = current & ~revoke_bits.
            uint32_t cur_bits = 0;
            std::string obj_type = "Table";
            for (const auto& pe : dd->permissions()) {
                if (pe.object_name == g.object && pe.grantee == g.principal) {
                    cur_bits |= pe.bitmask;
                    obj_type  = pe.object_type;
                }
            }
            dd->grant_permission(obj_type, g.object, g.principal,
                                  cur_bits & ~revoke_bits);
        }
        *phCursor = 0;
        return ok();
    }

    // M11.4 — `CREATE PROCEDURE <name> AS '<dll_path>::<symbol>'`.
    // Loads the DLL, resolves the symbol, registers the proc on the
    // connection. Returns no cursor.
    if (openads::sql::sql_is_create_procedure(sql)) {
        auto& s = state();
        std::lock_guard<std::recursive_mutex> lk(s.mu);
        auto cp = openads::sql::parse_create_procedure(sql);
        if (!cp) return fail(cp.error());
        auto rr = c->register_procedure(cp.value().name,
                                        cp.value().dll_path,
                                        cp.value().symbol);
        if (!rr) return fail(rr.error());
        *phCursor = 0;
        return ok();
    }

    // M11.4 — `EXECUTE PROCEDURE <name>(<arg>, ...)`. Built-in sp_* names
    // are dispatched directly to DataDict operations; others call the
    // DLL entry point registered via CREATE PROCEDURE.
    if (openads::sql::sql_is_execute_procedure(sql)) {
        auto& s = state();
        std::lock_guard<std::recursive_mutex> lk(s.mu);
        auto ep = openads::sql::parse_execute_procedure(sql);
        if (!ep) return fail(ep.error());
        // Check for sp_* built-in first.
        std::string uname = ep.value().name;
        for (auto& ch : uname) ch = static_cast<char>(
            std::toupper(static_cast<unsigned char>(ch)));
        if (uname.size() > 3 && uname[0]=='S' && uname[1]=='P' && uname[2]=='_') {
            UNSIGNED32 brc = ok();
            if (dispatch_sp_builtin(c, uname, ep.value().args, &brc)) {
                *phCursor = 0;
                return brc;
            }
        }
        std::string packed;
        for (std::size_t i = 0; i < ep.value().args.size(); ++i) {
            if (i != 0) packed.push_back('\x1f');
            packed.append(ep.value().args[i].text);
        }
        auto out = c->execute_procedure(ep.value().name, packed);
        if (!out) return fail(out.error());
        std::string& s_out = out.value();

        // Build a 1-row temp DBF with one C(255) column = "RESULT".
        namespace fs = std::filesystem;
        char nb[64];
        std::snprintf(nb, sizeof(nb), "_call_%llx.dbf",
                      static_cast<unsigned long long>(
                          openads::platform::monotonic_nanos()));
        fs::path dbf = fs::path(c->data_dir()) / nb;
        std::vector<std::uint8_t> file;
        std::array<std::uint8_t, 32> hdr{};
        hdr[0] = 0x03;
        stamp_dbf_header_today(hdr.data());
        hdr[4] = 1;
        std::uint16_t hl = 32 + 32 + 1;
        std::uint16_t rl = 1 + 255;
        hdr[8]  = static_cast<std::uint8_t>( hl       & 0xFFu);
        hdr[9]  = static_cast<std::uint8_t>((hl >> 8) & 0xFFu);
        hdr[10] = static_cast<std::uint8_t>( rl       & 0xFFu);
        hdr[11] = static_cast<std::uint8_t>((rl >> 8) & 0xFFu);
        file.insert(file.end(), hdr.begin(), hdr.end());
        std::array<std::uint8_t, 32> fd{};
        std::memcpy(fd.data(), "RESULT", 6);
        fd[11] = 'C'; fd[16] = 255;
        file.insert(file.end(), fd.begin(), fd.end());
        file.push_back(0x0D);
        file.push_back(' ');
        for (std::size_t i = 0; i < 255; ++i) {
            file.push_back(i < s_out.size()
                ? static_cast<std::uint8_t>(s_out[i]) : ' ');
        }
        file.push_back(0x1A);
        {
            std::ofstream f(dbf, std::ios::binary);
            if (!f) return fail(openads::AE_INTERNAL_ERROR,
                "EXECUTE PROCEDURE temp DBF open failed");
            f.write(reinterpret_cast<const char*>(file.data()),
                    static_cast<std::streamsize>(file.size()));
        }
        std::string rel = dbf.filename().string();
        auto th = c->open_table(rel, openads::engine::TableType::Cdx,
                                openads::engine::OpenMode::Read);
        if (!th) return fail(th.error());
        openads::engine::Table* tbl = c->lookup_table(th.value());
        if (!tbl) return fail(openads::AE_INTERNAL_ERROR, "post-open");
        ADSHANDLE gh = s.registry.register_object(HandleKind::Table, tbl);
        *phCursor = gh;
        return ok();
    }

    if (openads::sql::sql_is_update(sql)) {
        auto upd = openads::sql::parse_update(sql);
        if (!upd) return fail(upd.error());
        auto th = c->open_table(upd.value().table,
                                stmt_table_type(*it->second),
                                stmt_open_mode(*it->second, true),
                                stmt_locking_mode(*it->second));
        if (!th) return fail(th.error());
        openads::engine::Table* tbl = c->lookup_table(th.value());
        if (!tbl) return fail(openads::AE_INTERNAL_ERROR, "post-open");
        // Pre-resolve assignments so a typo surfaces before any write.
        struct Assn {
            std::uint16_t                   field_index;
            openads::sql::InsertLiteral     value;
        };
        std::vector<Assn> assns;
        assns.reserve(upd.value().assignments.size());
        for (const auto& a : upd.value().assignments) {
            std::int32_t fidx = tbl->field_index(a.column);
            if (fidx < 0) {
                return fail(openads::AE_COLUMN_NOT_FOUND, a.column.c_str());
            }
            assns.push_back({static_cast<std::uint16_t>(fidx), a.value});
        }
        // Walk every live record, run optional WHERE via the same
        // engine filter machinery, and apply the assignments inline.
        if (upd.value().where) {
            // Leverage the same compile path as SELECT — but inline
            // a smaller version that walks the AST recursively. Reuse
            // is fine: same structure, no SQL features missing.
            // (Helper extraction is deferred until UPDATE picks up
            // CONTAINS or AND/OR — for now the closures below fully
            // cover the tree.)
            using Pred = std::function<bool(openads::engine::Table&)>;
            std::function<openads::util::Result<Pred>(
                const openads::sql::WhereExpr&)> compile;
            compile = [&](const openads::sql::WhereExpr& node)
                      -> openads::util::Result<Pred> {
                using Kind = openads::sql::WhereExpr::Kind;
                if (node.kind == Kind::And) {
                    std::vector<Pred> ks;
                    for (auto& cn : node.children) {
                        auto r = compile(*cn);
                        if (!r) return r.error();
                        ks.push_back(std::move(r).value());
                    }
                    return Pred{[ks = std::move(ks)](openads::engine::Table& t) {
                        for (auto& k : ks) if (!k(t)) return false;
                        return true;
                    }};
                }
                if (node.kind == Kind::Or) {
                    std::vector<Pred> ks;
                    for (auto& cn : node.children) {
                        auto r = compile(*cn);
                        if (!r) return r.error();
                        ks.push_back(std::move(r).value());
                    }
                    return Pred{[ks = std::move(ks)](openads::engine::Table& t) {
                        for (auto& k : ks) if (k(t)) return true;
                        return false;
                    }};
                }
                if (node.kind == Kind::Not) {
                    auto inner = compile(*node.child);
                    if (!inner) return inner.error();
                    return Pred{[p = std::move(inner).value()]
                                (openads::engine::Table& t){return !p(t);}};
                }
                const auto& w = node.cmp;
                std::int32_t fidx = tbl->field_index(w.column);
                if (fidx < 0) {
                    return openads::util::Error{
                        openads::AE_COLUMN_NOT_FOUND, 0,
                        w.column.c_str(), ""};
                }
                std::uint16_t fi = static_cast<std::uint16_t>(fidx);
                openads::sql::WhereOp op = w.op;
                std::string lit = w.literal;
                bool is_num     = w.is_numeric;
                double num      = w.number;
                openads::sql::WhereFn lhs_fn = w.lhs_fn;
                return Pred{[fi, op, lit, is_num, num, lhs_fn]
                            (openads::engine::Table& t) {
                    auto v = t.read_field(fi);
                    if (!v) return false;
                    int cmp = 0;
                    if (is_num) {
                        double d = v.value().as_double;
                        if      (d < num) cmp = -1;
                        else if (d > num) cmp =  1;
                    } else {
                        cmp = apply_where_fn(v.value().as_string, lhs_fn)
                                  .compare(lit);
                    }
                    switch (op) {
                        case openads::sql::WhereOp::Eq: return cmp == 0;
                        case openads::sql::WhereOp::Ne: return cmp != 0;
                        case openads::sql::WhereOp::Lt: return cmp <  0;
                        case openads::sql::WhereOp::Gt: return cmp >  0;
                        case openads::sql::WhereOp::Le: return cmp <= 0;
                        case openads::sql::WhereOp::Ge: return cmp >= 0;
                        case openads::sql::WhereOp::Contains: return false;
                        default: return false;
                    }
                    return false;
                }};
            };
            auto compiled = compile(*upd.value().where);
            if (!compiled) return fail(compiled.error());
            tbl->set_filter(std::move(compiled).value());
        }
        // Trigger: INSTEAD OF UPDATE supersedes the actual write; BEFORE fires first.
        std::string upd_alias = ri_alias_for_path(c, tbl->path());
        Handle upd_hConn = !upd_alias.empty() ? handle_for_conn(c) : Handle{0};
        bool upd_instead_of = false;
        if (upd_hConn) {
            upd_instead_of = fire_triggers_(upd_hConn, c, upd_alias, 2u, 2u /*INSTEAD_OF*/);
            if (!upd_instead_of)
                fire_triggers_(upd_hConn, c, upd_alias, 2u, 1u /*BEFORE*/);
        }
        if (!upd_instead_of) {
            std::uint32_t rcount = tbl->record_count();
            for (std::uint32_t r = 1; r <= rcount; ++r) {
                if (auto g = tbl->goto_record(r); !g) continue;
                if (tbl->is_deleted()) continue;
                if (!tbl->passes_filter()) continue;
                for (const auto& a : assns) {
                    if (a.value.is_numeric) {
                        auto wr = tbl->set_field(a.field_index, a.value.number);
                        if (!wr) return fail(wr.error());
                    } else {
                        auto wr = tbl->set_field(a.field_index, a.value.text);
                        if (!wr) return fail(wr.error());
                    }
                }
            }
            if (auto fl = tbl->flush(); !fl) return fail(fl.error());
        }
        // Fire AFTER UPDATE triggers (only when no INSTEAD OF ran).
        if (!upd_instead_of && upd_hConn)
            fire_triggers_(upd_hConn, c, upd_alias, 2u, 4u /*AFTER*/);
        tbl->clear_filter();
        c->close_table(th.value());
        *phCursor = 0;
        return ok();
    }

    if (openads::sql::sql_is_delete(sql)) {
        auto del = openads::sql::parse_delete(sql);
        if (!del) return fail(del.error());
        auto th = c->open_table(del.value().table,
                                stmt_table_type(*it->second),
                                stmt_open_mode(*it->second, true),
                                stmt_locking_mode(*it->second));
        if (!th) return fail(th.error());
        openads::engine::Table* tbl = c->lookup_table(th.value());
        if (!tbl) return fail(openads::AE_INTERNAL_ERROR, "post-open");
        // Reuse the WHERE filter machinery for SELECT: it's already
        // wired and the predicate semantics match exactly.
        if (del.value().where) {
            // The compile lambda from the SELECT branch lives below;
            // copy a minimal bool-tree walker inline so DELETE isn't
            // forced to call into SELECT's path.
            using Pred = std::function<bool(openads::engine::Table&)>;
            std::function<openads::util::Result<Pred>(
                const openads::sql::WhereExpr&)> compile;
            compile = [&](const openads::sql::WhereExpr& node)
                      -> openads::util::Result<Pred> {
                using Kind = openads::sql::WhereExpr::Kind;
                if (node.kind == Kind::And) {
                    std::vector<Pred> ks;
                    for (auto& cn : node.children) {
                        auto r = compile(*cn);
                        if (!r) return r.error();
                        ks.push_back(std::move(r).value());
                    }
                    return Pred{[ks = std::move(ks)](openads::engine::Table& t) {
                        for (auto& k : ks) if (!k(t)) return false;
                        return true;
                    }};
                }
                if (node.kind == Kind::Or) {
                    std::vector<Pred> ks;
                    for (auto& cn : node.children) {
                        auto r = compile(*cn);
                        if (!r) return r.error();
                        ks.push_back(std::move(r).value());
                    }
                    return Pred{[ks = std::move(ks)](openads::engine::Table& t) {
                        for (auto& k : ks) if (k(t)) return true;
                        return false;
                    }};
                }
                if (node.kind == Kind::Not) {
                    auto inner = compile(*node.child);
                    if (!inner) return inner.error();
                    return Pred{[p = std::move(inner).value()]
                                (openads::engine::Table& t){return !p(t);}};
                }
                const auto& w = node.cmp;
                std::int32_t fidx = tbl->field_index(w.column);
                if (fidx < 0) {
                    return openads::util::Error{
                        openads::AE_COLUMN_NOT_FOUND, 0,
                        w.column.c_str(), ""};
                }
                std::uint16_t fi = static_cast<std::uint16_t>(fidx);
                openads::sql::WhereOp op = w.op;
                std::string lit = w.literal;
                bool is_num     = w.is_numeric;
                double num      = w.number;
                openads::sql::WhereFn lhs_fn = w.lhs_fn;
                return Pred{[fi, op, lit, is_num, num, lhs_fn]
                            (openads::engine::Table& t) {
                    auto v = t.read_field(fi);
                    if (!v) return false;
                    int cmp = 0;
                    if (is_num) {
                        double d = v.value().as_double;
                        if      (d < num) cmp = -1;
                        else if (d > num) cmp =  1;
                    } else {
                        cmp = apply_where_fn(v.value().as_string, lhs_fn)
                                  .compare(lit);
                    }
                    switch (op) {
                        case openads::sql::WhereOp::Eq: return cmp == 0;
                        case openads::sql::WhereOp::Ne: return cmp != 0;
                        case openads::sql::WhereOp::Lt: return cmp <  0;
                        case openads::sql::WhereOp::Gt: return cmp >  0;
                        case openads::sql::WhereOp::Le: return cmp <= 0;
                        case openads::sql::WhereOp::Ge: return cmp >= 0;
                        case openads::sql::WhereOp::Contains: return false;
                        default: return false;
                    }
                    return false;
                }};
            };
            auto compiled = compile(*del.value().where);
            if (!compiled) return fail(compiled.error());
            tbl->set_filter(std::move(compiled).value());
        }
        // Trigger: INSTEAD OF DELETE supersedes the actual delete; BEFORE fires first.
        std::string del_alias = ri_alias_for_path(c, tbl->path());
        Handle del_hConn = !del_alias.empty() ? handle_for_conn(c) : Handle{0};
        bool del_instead_of = false;
        if (del_hConn) {
            del_instead_of = fire_triggers_(del_hConn, c, del_alias, 3u, 2u /*INSTEAD_OF*/);
            if (!del_instead_of)
                fire_triggers_(del_hConn, c, del_alias, 3u, 1u /*BEFORE*/);
        }
        if (!del_instead_of) {
            std::uint32_t rcount = tbl->record_count();
            for (std::uint32_t r = 1; r <= rcount; ++r) {
                if (auto g = tbl->goto_record(r); !g) continue;
                if (tbl->is_deleted()) continue;
                if (!tbl->passes_filter()) continue;
                (void)tbl->mark_deleted();
            }
            if (auto fl = tbl->flush(); !fl) return fail(fl.error());
        }
        // Fire AFTER DELETE triggers (only when no INSTEAD OF ran).
        if (!del_instead_of && del_hConn)
            fire_triggers_(del_hConn, c, del_alias, 3u, 4u /*AFTER*/);
        tbl->clear_filter();
        c->close_table(th.value());
        *phCursor = 0;
        return ok();
    }

    if (openads::sql::sql_is_insert(sql)) {
        auto ins = openads::sql::parse_insert(sql);
        if (!ins) return fail(ins.error());
        auto th = c->open_table(ins.value().table,
                                stmt_table_type(*it->second),
                                stmt_open_mode(*it->second, true),
                                stmt_locking_mode(*it->second));
        if (!th) return fail(th.error());
        openads::engine::Table* tbl = c->lookup_table(th.value());
        if (!tbl) return fail(openads::AE_INTERNAL_ERROR, "post-open");

        // M10.41 — INSERT INTO t (cols) SELECT ...: recursively
        // execute the inner SELECT, walk its cursor, append one
        // target row per source row mapping the inner cursor's
        // projected columns to `ins.columns` positionally.
        if (!ins.value().select_sql.empty()) {
            std::vector<UNSIGNED8> selbuf(ins.value().select_sql.size() + 1);
            std::memcpy(selbuf.data(),
                        ins.value().select_sql.c_str(),
                        ins.value().select_sql.size() + 1);
            ADSHANDLE srcCur = 0;
            UNSIGNED32 rrc =
                AdsExecuteSQLDirect(hStatement, selbuf.data(), &srcCur);
            if (rrc != 0) {
                c->close_table(th.value());
                return rrc;
            }
            auto& s2 = state();
            std::lock_guard<std::recursive_mutex> lk2(s2.mu);
            openads::engine::Table* src =
                s2.registry.lookup<openads::engine::Table>(
                    srcCur, HandleKind::Table);
            if (!src) {
                c->close_table(th.value());
                return fail(openads::AE_INTERNAL_ERROR,
                            "INSERT...SELECT inner cursor lookup");
            }
            // Resolve inner cursor's projected column indices.
            const auto* proj = projection_for(srcCur);
            std::vector<std::uint16_t> src_cols;
            if (proj) {
                src_cols = *proj;
            } else {
                std::uint16_t nf = src->field_count();
                src_cols.reserve(nf);
                for (std::uint16_t k = 0; k < nf; ++k) src_cols.push_back(k);
            }
            if (src_cols.size() != ins.value().columns.size()) {
                AdsCloseTable(srcCur);
                c->close_table(th.value());
                return fail(openads::AE_PARSE_ERROR,
                    "INSERT INTO ... SELECT: column count mismatch");
            }
            // Pre-resolve target column indices.
            std::vector<std::uint16_t> tgt_cols;
            tgt_cols.reserve(ins.value().columns.size());
            for (const auto& cn : ins.value().columns) {
                std::int32_t fi = tbl->field_index(cn);
                if (fi < 0) {
                    AdsCloseTable(srcCur);
                    c->close_table(th.value());
                    return fail(openads::AE_COLUMN_NOT_FOUND, cn.c_str());
                }
                tgt_cols.push_back(static_cast<std::uint16_t>(fi));
            }
            // Walk source rows.
            std::vector<std::uint32_t> recnos;
            if (src->has_recno_sequence()) {
                recnos = src->recno_sequence();
            } else {
                std::uint32_t rcount = src->record_count();
                for (std::uint32_t r = 1; r <= rcount; ++r) {
                    if (auto g = src->goto_record(r); !g) continue;
                    if (src->is_deleted()) continue;
                    if (!src->passes_filter()) continue;
                    recnos.push_back(r);
                }
            }
            for (std::uint32_t r : recnos) {
                if (auto g = src->goto_record(r); !g) continue;
                if (auto ar = tbl->append_record(); !ar) {
                    AdsCloseTable(srcCur);
                    c->close_table(th.value());
                    return fail(ar.error());
                }
                for (std::size_t i = 0; i < src_cols.size(); ++i) {
                    auto v = src->read_field(src_cols[i]);
                    std::string sv = v ? v.value().as_string : std::string();
                    auto wr = tbl->set_field(tgt_cols[i], sv);
                    if (!wr) {
                        AdsCloseTable(srcCur);
                        c->close_table(th.value());
                        return fail(wr.error());
                    }
                }
            }
            if (auto fl = tbl->flush(); !fl) {
                AdsCloseTable(srcCur);
                c->close_table(th.value());
                return fail(fl.error());
            }
            // Fire AFTER INSERT triggers (event_mask=1) for INSERT...SELECT.
            {
                std::string alias = ri_alias_for_path(c, tbl->path());
                if (!alias.empty()) {
                    Handle hConn = handle_for_conn(c);
                    if (hConn) fire_triggers_(hConn, c, alias, 1u, 4u /*AFTER*/);
                }
            }
            AdsCloseTable(srcCur);
            c->close_table(th.value());
            *phCursor = 0;
            return ok();
        }

        // M10.52 — multi-row VALUES path. When `rows` is non-empty,
        // append + populate one record per tuple; otherwise fall
        // back to the single-row `values` path.
        auto write_one = [&](const std::vector<openads::sql::InsertLiteral>&
                             vals) -> openads::util::Result<std::monostate>
        {
            if (auto r = tbl->append_record(); !r) return r.error();
            for (std::size_t i = 0; i < ins.value().columns.size(); ++i) {
                std::int32_t fidx =
                    tbl->field_index(ins.value().columns[i]);
                if (fidx < 0) {
                    return openads::util::Error{
                        openads::AE_COLUMN_NOT_FOUND, 0,
                        ins.value().columns[i].c_str(), ""};
                }
                const auto& v = vals[i];
                if (v.is_numeric) {
                    auto wr = tbl->set_field(
                        static_cast<std::uint16_t>(fidx), v.number);
                    if (!wr) return wr.error();
                } else {
                    auto wr = tbl->set_field(
                        static_cast<std::uint16_t>(fidx), v.text);
                    if (!wr) return wr.error();
                }
            }
            return std::monostate{};
        };
        // Trigger: INSTEAD OF INSERT supersedes the actual insert; BEFORE fires first.
        std::string ins_alias = ri_alias_for_path(c, tbl->path());
        Handle ins_hConn = !ins_alias.empty() ? handle_for_conn(c) : Handle{0};
        bool ins_instead_of = false;
        if (ins_hConn) {
            ins_instead_of = fire_triggers_(ins_hConn, c, ins_alias, 1u, 2u /*INSTEAD_OF*/);
            if (!ins_instead_of)
                fire_triggers_(ins_hConn, c, ins_alias, 1u, 1u /*BEFORE*/);
        }
        if (!ins_instead_of) {
            if (!ins.value().rows.empty()) {
                for (auto& row : ins.value().rows) {
                    auto r = write_one(row);
                    if (!r) return fail(r.error());
                }
            } else {
                auto r = write_one(ins.value().values);
                if (!r) return fail(r.error());
            }
            if (auto fl = tbl->flush(); !fl) return fail(fl.error());
        }
        // Fire AFTER INSERT triggers (only when no INSTEAD OF ran).
        // Pass tbl so __new fields are available for the body.
        if (!ins_instead_of && ins_hConn)
            fire_triggers_(ins_hConn, c, ins_alias, 1u, 4u /*AFTER*/, tbl);
        c->close_table(th.value());
        *phCursor = 0;
        return ok();
    }

    // M10.26 — top-level `UNION [ALL]` between SELECTs. Each member
    // must currently be a `SELECT * FROM <t> [WHERE ...]` form (no
    // joins, aggregates, projection lists, GROUP BY, or ORDER BY
    // inside members — those compose with UNION in a follow-up).
    // First member's schema is reused for the merged cursor.
    {
        struct UnionPart { std::string sql_text; bool all = false; };
        std::vector<UnionPart> uparts;
        {
            std::size_t start = 0;
            int depth = 0;
            bool in_q = false;
            bool prev_all = false;
            auto kw_at = [&](std::size_t i, const char* kw) {
                std::size_t L = std::strlen(kw);
                if (i + L > sql.size()) return false;
                for (std::size_t k = 0; k < L; ++k)
                    if (std::toupper(static_cast<unsigned char>(sql[i+k])) !=
                        kw[k]) return false;
                bool lb = (i == 0) ||
                    (!std::isalnum(static_cast<unsigned char>(sql[i-1])) &&
                     sql[i-1] != '_');
                bool rb = (i + L == sql.size()) ||
                    (!std::isalnum(static_cast<unsigned char>(sql[i+L])) &&
                     sql[i+L] != '_');
                return lb && rb;
            };
            for (std::size_t i = 0; i < sql.size(); ) {
                char ch = sql[i];
                if (in_q) {
                    if (ch == '\'') in_q = false;
                    ++i; continue;
                }
                if (ch == '\'') { in_q = true; ++i; continue; }
                if (ch == '(')  { ++depth; ++i; continue; }
                if (ch == ')')  { --depth; ++i; continue; }
                if (depth == 0 && kw_at(i, "UNION")) {
                    uparts.push_back({sql.substr(start, i - start), prev_all});
                    i += 5;
                    while (i < sql.size() &&
                           std::isspace(static_cast<unsigned char>(sql[i]))) ++i;
                    prev_all = false;
                    if (kw_at(i, "ALL")) {
                        prev_all = true;
                        i += 3;
                        while (i < sql.size() &&
                               std::isspace(static_cast<unsigned char>(sql[i]))) ++i;
                    }
                    start = i;
                    continue;
                }
                ++i;
            }
            if (start < sql.size()) {
                uparts.push_back({sql.substr(start), prev_all});
            }
        }

        if (uparts.size() > 1) {
            auto& s = state();
            std::lock_guard<std::recursive_mutex> lk(s.mu);

            // M10.36 — every UNION member runs through the full
            // SELECT-execute pipeline as a recursive call to
            // AdsExecuteSQLDirect (allowed by the recursive_mutex on
            // s.mu). Members may now carry JOIN, GROUP BY, aggregates,
            // CASE WHEN, DISTINCT, LIMIT — anything a plain SELECT
            // accepts. The first member's cursor schema (whatever the
            // pipeline produces — temp DBF for joins/aggregates,
            // source schema for SELECT *) drives the merged schema;
            // later members align by column name against it.
            //
            // Last member's ORDER BY still becomes the merged sort
            // (M10.28 semantics). We capture it from a parse, then
            // let the recursive call run as-is — its sort is
            // overwritten by the final post-merge stable_sort below.
            std::optional<openads::sql::OrderBy> final_order;
            {
                auto p = openads::sql::parse_select(uparts.back().sql_text);
                if (p && p.value().order_by) {
                    final_order = *p.value().order_by;
                }
            }

            // The first member's `all` flag is meaningless (it has no
            // UNION keyword preceding it); only the join-flags between
            // members decide whether dedup applies.
            bool any_distinct = false;
            for (std::size_t i = 1; i < uparts.size(); ++i)
                if (!uparts[i].all) any_distinct = true;
            std::unordered_set<std::string> seen;

            std::vector<openads::drivers::DbfField> schema;
            std::uint32_t rec_len = 0;
            std::vector<std::vector<std::uint8_t>> rows;

            for (std::size_t mi = 0; mi < uparts.size(); ++mi) {
                // Recurse into the full SELECT executor for this
                // member. The member SQL goes through every dispatch
                // (UNION, JOIN, GROUP BY, aggregate, CASE, ...) and
                // lands as a registered cursor handle we can walk.
                std::vector<UNSIGNED8> mbuf(uparts[mi].sql_text.size() + 1);
                std::memcpy(mbuf.data(), uparts[mi].sql_text.c_str(),
                            uparts[mi].sql_text.size() + 1);
                ADSHANDLE memberCur = 0;
                UNSIGNED32 rrc =
                    AdsExecuteSQLDirect(hStatement, mbuf.data(), &memberCur);
                if (rrc != 0) return rrc;
                openads::engine::Table* mt =
                    s.registry.lookup<openads::engine::Table>(
                        memberCur, HandleKind::Table);
                if (!mt) {
                    return fail(openads::AE_INTERNAL_ERROR,
                                "UNION member cursor lookup failed");
                }

                if (mi == 0) {
                    auto* proj = projection_for(memberCur);
                    if (proj) {
                        schema.reserve(proj->size());
                        for (auto idx : *proj) {
                            schema.push_back(mt->field_descriptor(idx));
                        }
                    } else {
                        std::uint16_t nf = mt->field_count();
                        schema.reserve(nf);
                        for (std::uint16_t k = 0; k < nf; ++k) {
                            schema.push_back(mt->field_descriptor(k));
                        }
                    }
                    rec_len = 1;
                    for (auto& fd : schema) rec_len += fd.length;
                }

                std::vector<std::int32_t> col_src(schema.size(), -1);
                auto* proj_m = projection_for(memberCur);
                if (proj_m) {
                    if (proj_m->size() != schema.size()) {
                        AdsCloseTable(memberCur);
                        return fail(openads::AE_PARSE_ERROR,
                            "UNION member projection column count differs");
                    }
                    for (std::size_t i = 0; i < schema.size(); ++i) {
                        col_src[i] = static_cast<std::int32_t>((*proj_m)[i]);
                    }
                } else {
                    for (std::size_t i = 0; i < schema.size(); ++i) {
                        col_src[i] = mt->field_index(schema[i].name);
                    }
                }

                std::vector<std::uint32_t> recnos;
                if (mt->has_recno_sequence()) {
                    recnos = mt->recno_sequence();
                } else {
                    std::uint32_t rc = mt->record_count();
                    for (std::uint32_t r = 1; r <= rc; ++r) {
                        if (auto g = mt->goto_record(r); !g) continue;
                        if (mt->is_deleted()) continue;
                        if (!mt->passes_filter()) continue;
                        recnos.push_back(r);
                    }
                }
                for (std::uint32_t r : recnos) {
                    if (auto g = mt->goto_record(r); !g) continue;
                    std::vector<std::uint8_t> rec(rec_len);
                    rec[0] = ' ';
                    std::size_t off = 1;
                    for (std::size_t i = 0; i < schema.size(); ++i) {
                        std::string sval;
                        if (col_src[i] >= 0) {
                            auto v = mt->read_field(
                                static_cast<std::uint16_t>(col_src[i]));
                            if (v) sval = v.value().as_string;
                        }
                        std::uint8_t L = static_cast<std::uint8_t>(schema[i].length);
                        for (std::uint8_t k = 0; k < L; ++k) {
                            rec[off + k] = k < sval.size()
                                ? static_cast<std::uint8_t>(sval[k]) : ' ';
                        }
                        off += L;
                    }
                    if (any_distinct) {
                        std::string key(
                            reinterpret_cast<const char*>(rec.data()),
                            rec.size());
                        if (!seen.insert(std::move(key)).second) continue;
                    }
                    rows.push_back(std::move(rec));
                }
                AdsCloseTable(memberCur);
            }

            std::uint16_t nfields = static_cast<std::uint16_t>(schema.size());

            // Pre-build merged DBF header.
            std::vector<std::uint8_t> file;
            std::array<std::uint8_t, 32> hdr{};
            hdr[0] = 0x03;
            stamp_dbf_header_today(hdr.data());
            std::uint16_t header_len = static_cast<std::uint16_t>(
                32 + 32 * nfields + 1);
            hdr[8]  = static_cast<std::uint8_t>( header_len       & 0xFFu);
            hdr[9]  = static_cast<std::uint8_t>((header_len >> 8) & 0xFFu);
            hdr[10] = static_cast<std::uint8_t>( rec_len          & 0xFFu);
            hdr[11] = static_cast<std::uint8_t>((rec_len    >> 8) & 0xFFu);
            file.insert(file.end(), hdr.begin(), hdr.end());
            for (auto& fd : schema) {
                std::array<std::uint8_t, 32> bytes{};
                std::memcpy(bytes.data(), fd.name.data(),
                            std::min(fd.name.size(), std::size_t{11}));
                bytes[11] = static_cast<std::uint8_t>(fd.raw_type);
                bytes[16] = static_cast<std::uint8_t>(fd.length);
                bytes[17] = fd.decimals;
                file.insert(file.end(), bytes.begin(), bytes.end());
            }
            file.push_back(0x0D);

            // M10.28 — apply ORDER BY (from last member) to merged rows.
            if (final_order) {
                std::int32_t fi = -1;
                std::uint16_t off = 1;
                std::uint16_t flen = 0;
                bool numeric = false;
                for (std::size_t i = 0; i < schema.size(); ++i) {
                    if (schema[i].name == final_order->column) {
                        fi   = static_cast<std::int32_t>(i);
                        flen = schema[i].length;
                        numeric =
                            schema[i].type == openads::drivers::DbfFieldType::Numeric ||
                            schema[i].type == openads::drivers::DbfFieldType::Float   ||
                            schema[i].type == openads::drivers::DbfFieldType::Integer ||
                            schema[i].type == openads::drivers::DbfFieldType::Currency||
                            schema[i].type == openads::drivers::DbfFieldType::Double;
                        break;
                    }
                    off += schema[i].length;
                }
                if (fi < 0) {
                    return fail(openads::AE_COLUMN_NOT_FOUND,
                                final_order->column.c_str());
                }
                bool desc = final_order->descending;
                std::stable_sort(rows.begin(), rows.end(),
                    [&](const std::vector<std::uint8_t>& a,
                        const std::vector<std::uint8_t>& b) {
                        std::string ka(
                            reinterpret_cast<const char*>(a.data() + off), flen);
                        std::string kb(
                            reinterpret_cast<const char*>(b.data() + off), flen);
                        bool less;
                        if (numeric) {
                            double da = std::strtod(ka.c_str(), nullptr);
                            double db = std::strtod(kb.c_str(), nullptr);
                            if (da == db) return false;
                            less = da < db;
                        } else {
                            if (ka == kb) return false;
                            less = ka < kb;
                        }
                        return desc ? !less : less;
                    });
            }

            // Materialise rows into the file buffer.
            for (auto& rec : rows) {
                file.insert(file.end(), rec.begin(), rec.end());
            }

            file.push_back(0x1A);
            std::uint32_t emitted = static_cast<std::uint32_t>(rows.size());
            file[4] = static_cast<std::uint8_t>( emitted        & 0xFFu);
            file[5] = static_cast<std::uint8_t>((emitted >>  8) & 0xFFu);
            file[6] = static_cast<std::uint8_t>((emitted >> 16) & 0xFFu);
            file[7] = static_cast<std::uint8_t>((emitted >> 24) & 0xFFu);

            namespace fs = std::filesystem;
            char nb[64];
            std::snprintf(nb, sizeof(nb), "_uni_%llx.dbf",
                          static_cast<unsigned long long>(
                              openads::platform::monotonic_nanos()));
            fs::path uni_dbf = fs::path(c->data_dir()) / nb;
            {
                std::ofstream out(uni_dbf, std::ios::binary);
                if (!out) return fail(openads::AE_INTERNAL_ERROR,
                    "union temp DBF: open for write failed");
                out.write(reinterpret_cast<const char*>(file.data()),
                          static_cast<std::streamsize>(file.size()));
            }
            std::string rel = uni_dbf.filename().string();
            auto uth = c->open_table(rel, openads::engine::TableType::Cdx,
                                     openads::engine::OpenMode::Read);
            if (!uth) return fail(uth.error());
            openads::engine::Table* utbl = c->lookup_table(uth.value());
            if (!utbl) return fail(openads::AE_INTERNAL_ERROR, "post-open");
            ADSHANDLE gh = s.registry.register_object(HandleKind::Table, utbl);
            *phCursor = gh;
            return ok();
        }
    }

    auto parsed = openads::sql::parse_select(sql);
    if (!parsed) return fail(parsed.error());

    // ====================================================================
    // ADS dialect — N-way comma join (3+ tables) with composite keys and
    // `<alias>.*` projection. The single-JoinClause path below handles only
    // two tables; this path generalises to the multi-table inventory
    // aggregation the application issues (a header/detail join with
    // dimension tables). It runs a left-deep equi-join in FROM order and writes the
    // projected columns — named by their UNQUALIFIED column name, the ADS
    // result semantics — into a temp DBF cursor.
    //
    // NOTE: filters are applied after the join is fully bound (same as the
    // two-table path). Pushing single-table predicates (e.g. the date range)
    // down per level is a future optimisation; correctness first.
    // ====================================================================
    if (parsed.value().from_tables.size() >= 3) {
        auto& st = parsed.value();
        if (st.projection_complex) {
            return fail(openads::AE_INTERNAL_ERROR,
                "multi-table join: only column and <alias>.* projections "
                "are supported");
        }
        auto& s = state();
        std::lock_guard<std::recursive_mutex> lk(s.mu);

        const std::size_t N = st.from_tables.size();
        auto lc = [](std::string v) {
            for (auto& ch : v)
                ch = static_cast<char>(
                    std::tolower(static_cast<unsigned char>(ch)));
            return v;
        };
        auto trim_trailing = [](std::string v) {
            while (!v.empty() && v.back() == ' ') v.pop_back();
            return v;
        };

        // --- 1. Open every table; map alias (and bare name) -> index. ---
        std::vector<Handle> handles(N, 0);
        std::vector<openads::engine::Table*> tbls(N, nullptr);
        std::unordered_map<std::string, std::size_t> alias_idx;
        std::size_t opened = 0;
        bool open_ok = true;
        for (std::size_t i = 0; i < N; ++i) {
            auto h = open_or_sys(st.from_tables[i].name,
                                 openads::engine::TableType::Cdx,
                                 openads::engine::OpenMode::Read,
                                 openads::engine::LockingMode::Compatible);
            if (!h) { open_ok = false; break; }
            handles[i] = h.value();
            ++opened;
            tbls[i] = c->lookup_table(h.value());
            if (!tbls[i]) { open_ok = false; break; }
            const std::string& al = st.from_tables[i].alias;
            if (!al.empty()) alias_idx[lc(al)] = i;
            // Also index by the table's base name (strip dir + extension) so
            // a column may qualify by table name as well as by alias.
            std::string base = st.from_tables[i].name;
            std::size_t slash = base.find_last_of("\\/");
            if (slash != std::string::npos) base = base.substr(slash + 1);
            std::size_t dot = base.find_last_of('.');
            if (dot != std::string::npos) base = base.substr(0, dot);
            if (!base.empty()) alias_idx.emplace(lc(base), i);
        }
        auto close_all = [&]() {
            for (std::size_t k = 0; k < opened; ++k)
                if (handles[k]) c->close_table(handles[k]);
        };
        if (!open_ok) {
            close_all();
            return fail(openads::AE_NO_FILE_FOUND,
                        "multi-table join: table open failed");
        }

        // --- 2. Resolve a qualified column -> (table idx, field idx). ---
        auto resolve = [&](const std::string& alias, const std::string& col,
                           std::size_t& ti, std::int32_t& fi) -> bool {
            if (!alias.empty()) {
                auto a = alias_idx.find(lc(alias));
                if (a == alias_idx.end()) return false;
                ti = a->second;
                fi = tbls[ti]->field_index(col);
                return fi >= 0;
            }
            for (std::size_t i = 0; i < N; ++i) {
                std::int32_t f = tbls[i]->field_index(col);
                if (f >= 0) { ti = i; fi = f; return true; }
            }
            return false;
        };

        // --- 3. Walk the top-level AND spine: separate equi-join predicates
        //        (col = col, enforced by the hash join) from residual filter
        //        conjuncts. Each residual conjunct is bucketed by the DEEPEST
        //        table it references, so the join walk can evaluate it the
        //        moment that table is bound and prune early (filter pushdown).
        //        Only the AND spine is descended for join keys, so a `col=col`
        //        nested under OR/NOT is treated as a residual filter, not a
        //        join key.
        struct JEq { std::size_t lti; std::int32_t lfi;
                     std::size_t rti; std::int32_t rfi; };
        std::vector<JEq> jeqs;
        std::vector<std::vector<const openads::sql::WhereExpr*>> buckets(N);
        // Residual conjuncts that reference ONLY one joined table (index >= 1)
        // are applied while BUILDING that table's hash, so the hash holds just
        // the matching rows (e.g. only the month's detail rows) instead of the
        // whole history. Indexed by table.
        std::vector<std::vector<const openads::sql::WhereExpr*>> pure(N);
        bool spine_ok = true;
        // Accumulate the min/max table index a (sub)expression references.
        // mx < 0 means it references no column (a folded constant).
        std::function<void(const openads::sql::WhereExpr*, int&, int&)>
            conj_range = [&](const openads::sql::WhereExpr* n,
                             int& mn, int& mx) {
                if (n == nullptr) return;
                std::size_t ti; std::int32_t fi;
                if (n->kind == openads::sql::WhereExpr::Kind::Cmp) {
                    const auto& w = n->cmp;
                    if (resolve(w.column_alias, w.column, ti, fi)) {
                        mn = std::min(mn, static_cast<int>(ti));
                        mx = std::max(mx, static_cast<int>(ti));
                    }
                    if (w.is_outer_ref &&
                        resolve(w.outer_column_alias, w.outer_column, ti, fi)) {
                        mn = std::min(mn, static_cast<int>(ti));
                        mx = std::max(mx, static_cast<int>(ti));
                    }
                    return;
                }
                if (n->kind == openads::sql::WhereExpr::Kind::In) {
                    if (resolve(std::string(), n->in_clause.column, ti, fi)) {
                        mn = std::min(mn, static_cast<int>(ti));
                        mx = std::max(mx, static_cast<int>(ti));
                    }
                    return;
                }
                for (auto& ch : n->children) conj_range(ch.get(), mn, mx);
                conj_range(n->child.get(), mn, mx);
            };
        std::function<void(const openads::sql::WhereExpr*)> spine =
            [&](const openads::sql::WhereExpr* n) {
                if (n == nullptr || !spine_ok) return;
                if (n->kind == openads::sql::WhereExpr::Kind::And) {
                    for (auto& ch : n->children) spine(ch.get());
                    return;
                }
                if (n->kind == openads::sql::WhereExpr::Kind::Cmp &&
                    n->cmp.op == openads::sql::WhereOp::Eq &&
                    n->cmp.is_outer_ref) {
                    JEq e{};
                    if (!resolve(n->cmp.column_alias, n->cmp.column,
                                 e.lti, e.lfi) ||
                        !resolve(n->cmp.outer_column_alias, n->cmp.outer_column,
                                 e.rti, e.rfi)) {
                        spine_ok = false;
                        return;
                    }
                    jeqs.push_back(e);
                    return;
                }
                int mn = static_cast<int>(N), mx = -1;
                conj_range(n, mn, mx);
                if (mx < 0) {
                    buckets[0].push_back(n);                  // constant -> level 0
                } else if (mn == mx && mn >= 1) {
                    pure[static_cast<std::size_t>(mn)].push_back(n);  // hash-build
                } else {
                    buckets[static_cast<std::size_t>(mx)].push_back(n);  // walk
                }
            };
        spine(st.where.get());
        if (!spine_ok) {
            close_all();
            return fail(openads::AE_COLUMN_NOT_FOUND,
                        "multi-table join: unresolved join column");
        }

        // --- 4. Build a left-deep probe plan in FROM order. ---
        struct Probe {
            std::size_t partner = 0;
            std::vector<std::int32_t> myCols;
            std::vector<std::int32_t> partnerCols;
            std::unordered_map<std::string,
                               std::vector<std::uint32_t>> hash;
        };
        std::vector<Probe> probes(N);
        for (std::size_t i = 1; i < N; ++i) {
            std::size_t partner = N;   // sentinel = unset
            for (const auto& e : jeqs) {
                std::int32_t myc; std::size_t other; std::int32_t otc;
                if (e.lti == i && e.rti < i) {
                    myc = e.lfi; other = e.rti; otc = e.rfi;
                } else if (e.rti == i && e.lti < i) {
                    myc = e.rfi; other = e.lti; otc = e.lfi;
                } else {
                    continue;
                }
                if (partner == N) partner = other;
                if (other != partner) {
                    close_all();
                    return fail(openads::AE_INTERNAL_ERROR,
                        "multi-table join: a table joins to more than one "
                        "prior table (only left-deep trees are supported)");
                }
                probes[i].myCols.push_back(myc);
                probes[i].partnerCols.push_back(otc);
            }
            if (partner == N) {
                close_all();
                return fail(openads::AE_INTERNAL_ERROR,
                    "multi-table join: a table has no equi-join to a prior "
                    "table (cartesian products are not supported)");
            }
            probes[i].partner = partner;
        }

        // --- 5. (Table hashes are built in step 7b below, after the WHERE
        //         evaluator is defined, so the single-table residual filters
        //         in pure[i] can be applied during the build.) ---

        // --- 6. Resolve the output schema from select_items (ADS names). ---
        struct OutCol { std::size_t ti; openads::drivers::DbfField fld; };
        std::vector<OutCol> outcols;
        std::unordered_set<std::string> seen;
        auto add_out = [&](std::size_t ti, std::int32_t fi) {
            const auto& f =
                tbls[ti]->driver()->fields()[static_cast<std::size_t>(fi)];
            std::string nm = f.name;
            if (nm.size() > 10) nm.resize(10);
            if (!seen.insert(lc(nm)).second) return;   // first-wins de-dupe
            outcols.push_back(OutCol{ti, f});
        };
        if (st.select_items.empty()) {
            // bare `SELECT *` across all tables, in FROM order.
            for (std::size_t i = 0; i < N; ++i) {
                const auto& flds = tbls[i]->driver()->fields();
                for (std::int32_t k = 0;
                     k < static_cast<std::int32_t>(flds.size()); ++k)
                    add_out(i, k);
            }
        } else {
            for (const auto& si : st.select_items) {
                if (si.wildcard) {
                    auto a = alias_idx.find(lc(si.alias));
                    if (a == alias_idx.end()) {
                        close_all();
                        return fail(openads::AE_COLUMN_NOT_FOUND,
                                    si.alias.c_str());
                    }
                    const auto& flds =
                        tbls[a->second]->driver()->fields();
                    for (std::int32_t k = 0;
                         k < static_cast<std::int32_t>(flds.size()); ++k)
                        add_out(a->second, k);
                } else {
                    std::size_t ti; std::int32_t fi;
                    if (!resolve(si.alias, si.column, ti, fi)) {
                        close_all();
                        return fail(openads::AE_COLUMN_NOT_FOUND,
                                    si.column.c_str());
                    }
                    add_out(ti, fi);
                }
            }
        }
        if (outcols.empty()) {
            close_all();
            return fail(openads::AE_INTERNAL_ERROR,
                        "multi-table join: empty projection");
        }

        // --- 7. Residual WHERE evaluator over the bound raw records. ---
        std::vector<std::vector<std::uint8_t>> raws(N);
        auto slice = [&](std::size_t ti, std::int32_t fi) -> std::string {
            const auto& f =
                tbls[ti]->driver()->fields()[static_cast<std::size_t>(fi)];
            const auto& buf = raws[ti];
            if (static_cast<std::size_t>(f.record_offset) + f.length
                    > buf.size())
                return std::string();
            return std::string(reinterpret_cast<const char*>(
                buf.data() + f.record_offset), f.length);
        };
        auto is_num_type = [](openads::drivers::DbfFieldType t) {
            return t == openads::drivers::DbfFieldType::Numeric  ||
                   t == openads::drivers::DbfFieldType::Float    ||
                   t == openads::drivers::DbfFieldType::Integer  ||
                   t == openads::drivers::DbfFieldType::Currency ||
                   t == openads::drivers::DbfFieldType::Double;
        };
        auto fold = [&](std::string v, openads::sql::WhereFn fn) {
            if (fn == openads::sql::WhereFn::Upper)
                for (auto& ch : v)
                    ch = static_cast<char>(
                        std::toupper(static_cast<unsigned char>(ch)));
            else if (fn == openads::sql::WhereFn::Lower)
                for (auto& ch : v)
                    ch = static_cast<char>(
                        std::tolower(static_cast<unsigned char>(ch)));
            return v;
        };
        std::function<bool(const char*, const char*)> like_m =
            [&](const char* p, const char* sv) -> bool {
            if (*p == '\0') return *sv == '\0';
            if (*p == '%') {
                if (like_m(p + 1, sv)) return true;
                return *sv != '\0' && like_m(p, sv + 1);
            }
            if (*sv == '\0') return false;
            if (*p == '_' || *p == *sv) return like_m(p + 1, sv + 1);
            return false;
        };
        std::function<bool(const openads::sql::WhereExpr*)> eval =
            [&](const openads::sql::WhereExpr* n) -> bool {
            if (n == nullptr) return true;
            using K  = openads::sql::WhereExpr::Kind;
            using Op = openads::sql::WhereOp;
            switch (n->kind) {
                case K::And: {
                    for (auto& ch : n->children)
                        if (!eval(ch.get())) return false;
                    return true;                 // empty AND -> true
                }
                case K::Or: {
                    if (n->children.empty()) return false;  // empty OR -> false
                    for (auto& ch : n->children)
                        if (eval(ch.get())) return true;
                    return false;
                }
                case K::Not:
                    return !eval(n->child.get());
                case K::In: {
                    std::size_t lti; std::int32_t lfi;
                    if (!resolve(std::string(), n->in_clause.column, lti, lfi))
                        return false;
                    std::string lhs = trim_trailing(slice(lti, lfi));
                    for (const auto& v : n->in_clause.literals)
                        if (lhs == v) return true;
                    return false;
                }
                case K::Cmp: {
                    const auto& w = n->cmp;
                    std::size_t lti; std::int32_t lfi;
                    if (!resolve(w.column_alias, w.column, lti, lfi))
                        return false;
                    const auto& lf =
                        tbls[lti]->driver()->fields()[
                            static_cast<std::size_t>(lfi)];
                    std::string lhs = fold(trim_trailing(slice(lti, lfi)),
                                           w.lhs_fn);
                    if (w.is_outer_ref) {
                        std::size_t rti; std::int32_t rfi;
                        if (!resolve(w.outer_column_alias, w.outer_column,
                                     rti, rfi))
                            return false;
                        int cc = lhs.compare(trim_trailing(slice(rti, rfi)));
                        switch (w.op) {
                            case Op::Ne: return cc != 0;
                            case Op::Lt: return cc <  0;
                            case Op::Gt: return cc >  0;
                            case Op::Le: return cc <= 0;
                            case Op::Ge: return cc >= 0;
                            default:     return cc == 0;   // Eq
                        }
                    }
                    if (w.op == Op::IsNull)    return lhs.empty();
                    if (w.op == Op::IsNotNull) return !lhs.empty();
                    bool numeric = is_num_type(lf.type);
                    if (w.op == Op::Like)
                        return like_m(w.literal.c_str(), lhs.c_str());
                    if (w.op == Op::Between) {
                        if (numeric) {
                            double a  = std::strtod(lhs.c_str(), nullptr);
                            double b1 = std::strtod(w.literal.c_str(), nullptr);
                            double b2 = std::strtod(w.literal2.c_str(), nullptr);
                            return a >= b1 && a <= b2;
                        }
                        return lhs.compare(w.literal)  >= 0 &&
                               lhs.compare(w.literal2) <= 0;
                    }
                    int cc;
                    if (numeric) {
                        double a = std::strtod(lhs.c_str(), nullptr);
                        double b = w.is_numeric
                                       ? w.number
                                       : std::strtod(w.literal.c_str(), nullptr);
                        cc = (a < b) ? -1 : (a > b ? 1 : 0);
                    } else {
                        cc = lhs.compare(w.literal);
                    }
                    switch (w.op) {
                        case Op::Eq: return cc == 0;
                        case Op::Ne: return cc != 0;
                        case Op::Lt: return cc <  0;
                        case Op::Gt: return cc >  0;
                        case Op::Le: return cc <= 0;
                        case Op::Ge: return cc >= 0;
                        default:     return false;
                    }
                }
                default:
                    return true;   // Exists / unsupported -> non-filtering
            }
        };

        // --- 7b. Hash each joined table on its key columns, applying the
        //         single-table residual filters (pure[i]) up front so the hash
        //         holds only matching rows (e.g. just the month's detail rows)
        //         instead of the whole history. raws[i] is scratch here; the
        //         walk re-binds it per emitted combination.
        for (std::size_t i = 1; i < N; ++i) {
            auto& pr = probes[i];
            const auto& flds = tbls[i]->driver()->fields();
            std::uint32_t rc = tbls[i]->record_count();
            for (std::uint32_t r = 1; r <= rc; ++r) {
                auto raw = tbls[i]->driver()->read_record_raw(r);
                if (!raw) continue;
                raws[i] = std::move(raw).value();
                const auto& buf = raws[i];
                if (buf.empty() || buf[0] == '*') continue;   // skip deleted
                bool keep = true;
                for (const auto* cj : pure[i])
                    if (!eval(cj)) { keep = false; break; }   // single-table filter
                if (!keep) continue;
                std::string key;
                bool kok = true;
                for (std::int32_t fc : pr.myCols) {
                    const auto& f = flds[static_cast<std::size_t>(fc)];
                    if (static_cast<std::size_t>(f.record_offset) + f.length
                            > buf.size()) { kok = false; break; }
                    key += trim_trailing(std::string(
                        reinterpret_cast<const char*>(
                            buf.data() + f.record_offset), f.length));
                    key.push_back('\x01');
                }
                if (!kok) continue;
                pr.hash[key].push_back(r);
            }
        }

        // --- 8. Output DBF header + field descriptors. ---
        std::uint16_t out_hlen = static_cast<std::uint16_t>(
            32 + 32 * outcols.size() + 1);
        std::uint32_t out_rlen = 1;
        for (const auto& oc : outcols) out_rlen += oc.fld.length;
        if (out_rlen > 0xFFFF) {
            close_all();
            return fail(openads::AE_INTERNAL_ERROR,
                        "multi-table join: record exceeds 64 KiB");
        }
        std::vector<std::uint8_t> file;
        {
            std::array<std::uint8_t, 32> hdr{};
            hdr[0] = 0x03;
            stamp_dbf_header_today(hdr.data());
            hdr[8]  = static_cast<std::uint8_t>( out_hlen       & 0xFFu);
            hdr[9]  = static_cast<std::uint8_t>((out_hlen >> 8) & 0xFFu);
            hdr[10] = static_cast<std::uint8_t>( out_rlen       & 0xFFu);
            hdr[11] = static_cast<std::uint8_t>((out_rlen >> 8) & 0xFFu);
            file.insert(file.end(), hdr.begin(), hdr.end());
            for (const auto& oc : outcols) {
                std::array<std::uint8_t, 32> fd{};
                std::size_t nn = std::min<std::size_t>(oc.fld.name.size(), 10);
                std::memcpy(fd.data(), oc.fld.name.data(), nn);
                fd[11] = static_cast<std::uint8_t>(oc.fld.raw_type);
                fd[16] = static_cast<std::uint8_t>(oc.fld.length);
                fd[17] = oc.fld.decimals;
                file.insert(file.end(), fd.begin(), fd.end());
            }
            file.push_back(0x0D);
        }

        // --- 9. Left-deep nested-loop join walk; emit projected rows. ---
        std::uint32_t emitted = 0;
        auto emit_row = [&]() {
            std::vector<std::uint8_t> rec(out_rlen, ' ');
            rec[0] = ' ';
            std::size_t off = 1;
            for (const auto& oc : outcols) {
                const auto& buf = raws[oc.ti];
                std::size_t so = oc.fld.record_offset;
                std::size_t L  = oc.fld.length;
                if (so + L <= buf.size())
                    std::memcpy(rec.data() + off, buf.data() + so, L);
                off += L;
            }
            file.insert(file.end(), rec.begin(), rec.end());
            ++emitted;
        };
        // Evaluate the residual conjuncts that become bound at `level`;
        // returning false prunes the branch before descending further.
        auto pass_bucket = [&](std::size_t level) -> bool {
            for (const auto* cj : buckets[level])
                if (!eval(cj)) return false;
            return true;
        };
        std::function<void(std::size_t)> walk = [&](std::size_t level) {
            if (level == N) { emit_row(); return; }   // all conjuncts checked
            if (level == 0) {
                std::uint32_t rc = tbls[0]->record_count();
                for (std::uint32_t r = 1; r <= rc; ++r) {
                    auto raw = tbls[0]->driver()->read_record_raw(r);
                    if (!raw) continue;
                    if (raw.value().empty() || raw.value()[0] == '*') continue;
                    raws[0] = std::move(raw).value();
                    if (pass_bucket(0)) walk(1);   // prune table-0 filters early
                }
                return;
            }
            const auto& pr = probes[level];
            const auto& pfld = tbls[pr.partner]->driver()->fields();
            std::string key;
            for (std::int32_t pc : pr.partnerCols) {
                const auto& f = pfld[static_cast<std::size_t>(pc)];
                const auto& pbuf = raws[pr.partner];
                std::string v;
                if (static_cast<std::size_t>(f.record_offset) + f.length
                        <= pbuf.size())
                    v.assign(reinterpret_cast<const char*>(
                        pbuf.data() + f.record_offset), f.length);
                key += trim_trailing(v);
                key.push_back('\x01');
            }
            auto it2 = pr.hash.find(key);
            if (it2 == pr.hash.end()) return;
            for (std::uint32_t r : it2->second) {
                auto raw = tbls[level]->driver()->read_record_raw(r);
                if (!raw) continue;
                raws[level] = std::move(raw).value();
                if (pass_bucket(level)) walk(level + 1);  // push filters down
            }
        };
        walk(0);

        file.push_back(0x1A);
        file[4] = static_cast<std::uint8_t>( emitted        & 0xFFu);
        file[5] = static_cast<std::uint8_t>((emitted >>  8) & 0xFFu);
        file[6] = static_cast<std::uint8_t>((emitted >> 16) & 0xFFu);
        file[7] = static_cast<std::uint8_t>((emitted >> 24) & 0xFFu);

        close_all();

        // --- 10. Write temp DBF, reopen as cursor, register, return. ---
        namespace fs = std::filesystem;
        char nb[64];
        std::snprintf(nb, sizeof(nb), "_mjoin_%llx.dbf",
                      static_cast<unsigned long long>(
                          openads::platform::monotonic_nanos()));
        fs::path mj = fs::path(c->data_dir()) / nb;
        {
            std::ofstream out(mj, std::ios::binary);
            if (!out)
                return fail(openads::AE_INTERNAL_ERROR,
                            "multi-table join temp DBF: open for write failed");
            out.write(reinterpret_cast<const char*>(file.data()),
                      static_cast<std::streamsize>(file.size()));
        }
        std::string rel = mj.filename().string();
        auto cth = c->open_table(rel, openads::engine::TableType::Cdx,
                                 openads::engine::OpenMode::Read);
        if (!cth) return fail(cth.error());
        openads::engine::Table* ctbl = c->lookup_table(cth.value());
        if (!ctbl) return fail(openads::AE_INTERNAL_ERROR, "post-open");
        ADSHANDLE gh = s.registry.register_object(HandleKind::Table, ctbl);
        *phCursor = gh;
        return ok();
    }

    if (parsed.value().inner_join) {
        // M10.14 materialises the join into a temp DBF cursor; M10.20
        // additionally compiles the outer WHERE / ORDER BY against
        // that cursor's merged schema; M10.23 runs aggregates over
        // that merged cursor when the projection is `agg(...)` instead
        // of a column list.
        const auto& j = *parsed.value().inner_join;
        auto& s = state();
        std::lock_guard<std::recursive_mutex> lk(s.mu);

        auto lh = open_or_sys(parsed.value().table,
                              openads::engine::TableType::Cdx,
                              openads::engine::OpenMode::Read,
                              openads::engine::LockingMode::Compatible);
        if (!lh) return fail(lh.error());
        auto rh = open_or_sys(j.table,
                              openads::engine::TableType::Cdx,
                              openads::engine::OpenMode::Read,
                              openads::engine::LockingMode::Compatible);
        if (!rh) {
            c->close_table(lh.value());
            return fail(rh.error());
        }
        openads::engine::Table* ltbl = c->lookup_table(lh.value());
        openads::engine::Table* rtbl = c->lookup_table(rh.value());
        if (ltbl == nullptr || rtbl == nullptr) {
            return fail(openads::AE_INTERNAL_ERROR, "join post-open");
        }

        std::int32_t lcol = ltbl->field_index(j.left_column);
        std::int32_t rcol = rtbl->field_index(j.right_column);
        // Orientation fallback: a comma-join (`FROM a, b WHERE a.x = b.y`)
        // lowers the join key from the WHERE, where the alias qualifiers are
        // dropped — so `left_column`/`right_column` may name the columns in
        // the opposite order to base/joined. If the straight assignment does
        // not resolve but the swapped one does, use the swap. Purely
        // additive: it only fires on inputs the strict path already rejects,
        // and also makes explicit `JOIN ON b.y = a.x` lenient like ADS.
        if ((lcol < 0 || rcol < 0)) {
            std::int32_t lcol_sw = ltbl->field_index(j.right_column);
            std::int32_t rcol_sw = rtbl->field_index(j.left_column);
            if (lcol_sw >= 0 && rcol_sw >= 0) {
                lcol = lcol_sw;
                rcol = rcol_sw;
            }
        }
        if (lcol < 0) {
            c->close_table(lh.value()); c->close_table(rh.value());
            return fail(openads::AE_COLUMN_NOT_FOUND,
                        j.left_column.c_str());
        }
        if (rcol < 0) {
            c->close_table(lh.value()); c->close_table(rh.value());
            return fail(openads::AE_COLUMN_NOT_FOUND,
                        j.right_column.c_str());
        }

        auto trim_trailing = [](std::string sl) {
            while (!sl.empty() && sl.back() == ' ') sl.pop_back();
            return sl;
        };

        // INNER / LEFT walk left + lookup right. RIGHT swaps that —
        // walk right + lookup left. FULL walks left first (emitting
        // matched + LEFT-style fillers) and then walks right to emit
        // only the unmatched right rows with a blank left filler.
        // The merged schema's column order (left fields first, then
        // right with `R_` prefix) stays identical regardless of join
        // direction so the cursor exposes the same shape to apps.
        bool walk_right = j.is_right;

        std::unordered_map<std::string, std::vector<std::uint32_t>> probe_map;
        if (walk_right) {
            // Hash the LEFT column (we'll walk the right side and
            // look up each right row's join value in this map).
            std::uint32_t lrc_for_hash = ltbl->record_count();
            for (std::uint32_t r = 1; r <= lrc_for_hash; ++r) {
                if (auto g = ltbl->goto_record(r); !g) continue;
                if (ltbl->is_deleted()) continue;
                auto v = ltbl->read_field(
                    static_cast<std::uint16_t>(lcol));
                if (!v) continue;
                probe_map[trim_trailing(v.value().as_string)].push_back(r);
            }
        } else {
            // Default: hash the RIGHT column (M10.14 + M10.16 path).
            std::uint32_t rrc = rtbl->record_count();
            for (std::uint32_t r = 1; r <= rrc; ++r) {
                if (auto g = rtbl->goto_record(r); !g) continue;
                if (rtbl->is_deleted()) continue;
                auto v = rtbl->read_field(
                    static_cast<std::uint16_t>(rcol));
                if (!v) continue;
                probe_map[trim_trailing(v.value().as_string)].push_back(r);
            }
        }
        // Keep the legacy name `rmap` working — the executor below
        // walks one side and probes the other through this map.
        auto& rmap = probe_map;

        // Build merged schema.
        std::vector<openads::drivers::DbfField> merged;
        const auto& lfields = ltbl->driver()->fields();
        const auto& rfields = rtbl->driver()->fields();
        for (const auto& f : lfields) merged.push_back(f);
        for (auto f : rfields) {
            std::string nm = "R_" + f.name;
            if (nm.size() > 10) nm.resize(10);
            f.name = std::move(nm);
            merged.push_back(f);
        }

        std::uint16_t header_len = static_cast<std::uint16_t>(
            32 + 32 * merged.size() + 1);
        std::uint32_t lrec = ltbl->driver()->record_length();
        std::uint32_t rrec = rtbl->driver()->record_length();
        std::uint32_t merged_rec = 1 + (lrec - 1) + (rrec - 1);
        if (merged_rec > 0xFFFF) {
            c->close_table(lh.value()); c->close_table(rh.value());
            return fail(openads::AE_INTERNAL_ERROR,
                        "joined record exceeds 64 KiB");
        }

        // Lay out file bytes: header + field-desc + records + EOF.
        std::vector<std::uint8_t> file;
        std::array<std::uint8_t, 32> hdr{};
        hdr[0] = 0x03;
        stamp_dbf_header_today(hdr.data());
        hdr[8]  = static_cast<std::uint8_t>( header_len       & 0xFFu);
        hdr[9]  = static_cast<std::uint8_t>((header_len >> 8) & 0xFFu);
        hdr[10] = static_cast<std::uint8_t>( merged_rec       & 0xFFu);
        hdr[11] = static_cast<std::uint8_t>((merged_rec >> 8) & 0xFFu);
        file.insert(file.end(), hdr.begin(), hdr.end());
        for (const auto& f : merged) {
            std::array<std::uint8_t, 32> fd{};
            std::size_t n = std::min<std::size_t>(f.name.size(), 10);
            std::memcpy(fd.data(), f.name.data(), n);
            fd[11] = static_cast<std::uint8_t>(f.raw_type);
            fd[16] = static_cast<std::uint8_t>(f.length);
            fd[17] = f.decimals;
            file.insert(file.end(), fd.begin(), fd.end());
        }
        file.push_back(0x0D);

        // Helper: emit one merged record with explicit left/right
        // byte slices. Either side may be null — outer-join fillers
        // pass nullptr for the side that has no match.
        std::uint32_t emitted = 0;
        auto emit_merged = [&](const std::uint8_t* lbytes, std::size_t lsize,
                               const std::uint8_t* rbytes, std::size_t rsize) {
            std::vector<std::uint8_t> mrec(merged_rec, ' ');
            mrec[0] = (lbytes != nullptr && lsize > 0) ? lbytes[0] : ' ';
            if (lbytes != nullptr && lrec > 1 && lsize >= lrec) {
                std::memcpy(mrec.data() + 1, lbytes + 1, lrec - 1);
            }
            if (rbytes != nullptr && rrec > 1 && rsize >= rrec) {
                std::memcpy(mrec.data() + lrec, rbytes + 1, rrec - 1);
            }
            file.insert(file.end(), mrec.begin(), mrec.end());
            ++emitted;
        };

        if (walk_right) {
            // RIGHT OUTER — walk right rows, look up the LEFT hash.
            // Unmatched right rows surface with blank left fields.
            std::uint32_t rrc = rtbl->record_count();
            for (std::uint32_t r = 1; r <= rrc; ++r) {
                if (auto g = rtbl->goto_record(r); !g) continue;
                if (rtbl->is_deleted()) continue;
                auto rv = rtbl->read_field(
                    static_cast<std::uint16_t>(rcol));
                if (!rv) continue;
                auto lit = rmap.find(trim_trailing(rv.value().as_string));
                auto rraw = rtbl->driver()->read_record_raw(r);
                if (!rraw) continue;
                const auto& rbuf = rraw.value();
                if (lit == rmap.end()) {
                    emit_merged(nullptr, 0, rbuf.data(), rbuf.size());
                    continue;
                }
                for (std::uint32_t ll : lit->second) {
                    auto lraw = ltbl->driver()->read_record_raw(ll);
                    if (!lraw) continue;
                    const auto& lbuf = lraw.value();
                    emit_merged(lbuf.data(), lbuf.size(),
                                rbuf.data(), rbuf.size());
                }
            }
        } else {
            // INNER / LEFT / FULL — walk left rows, look up the RIGHT
            // hash. Unmatched left rows surface with blank right
            // fields when is_left or is_full; dropped otherwise.
            std::unordered_set<std::uint32_t> matched_right;
            std::uint32_t lrc = ltbl->record_count();
            for (std::uint32_t l = 1; l <= lrc; ++l) {
                if (auto g = ltbl->goto_record(l); !g) continue;
                if (ltbl->is_deleted()) continue;
                auto lv = ltbl->read_field(
                    static_cast<std::uint16_t>(lcol));
                if (!lv) continue;
                auto rit = rmap.find(trim_trailing(lv.value().as_string));
                auto lraw = ltbl->driver()->read_record_raw(l);
                if (!lraw) continue;
                const auto& lbuf = lraw.value();
                if (rit == rmap.end()) {
                    if (j.is_left || j.is_full) {
                        emit_merged(lbuf.data(), lbuf.size(), nullptr, 0);
                    }
                    continue;
                }
                for (std::uint32_t rr : rit->second) {
                    auto rraw = rtbl->driver()->read_record_raw(rr);
                    if (!rraw) continue;
                    const auto& rbuf = rraw.value();
                    emit_merged(lbuf.data(), lbuf.size(),
                                rbuf.data(), rbuf.size());
                    if (j.is_full) matched_right.insert(rr);
                }
            }
            // FULL OUTER: emit unmatched right rows with blank left.
            if (j.is_full) {
                std::uint32_t rrc = rtbl->record_count();
                for (std::uint32_t r = 1; r <= rrc; ++r) {
                    if (matched_right.find(r) != matched_right.end()) continue;
                    if (auto g = rtbl->goto_record(r); !g) continue;
                    if (rtbl->is_deleted()) continue;
                    auto rraw = rtbl->driver()->read_record_raw(r);
                    if (!rraw) continue;
                    const auto& rbuf = rraw.value();
                    emit_merged(nullptr, 0, rbuf.data(), rbuf.size());
                }
            }
        }
        file.push_back(0x1A);
        // Patch record count (header bytes 4-7).
        file[4] = static_cast<std::uint8_t>( emitted        & 0xFFu);
        file[5] = static_cast<std::uint8_t>((emitted >>  8) & 0xFFu);
        file[6] = static_cast<std::uint8_t>((emitted >> 16) & 0xFFu);
        file[7] = static_cast<std::uint8_t>((emitted >> 24) & 0xFFu);

        c->close_table(lh.value());
        c->close_table(rh.value());

        // Write temp DBF.
        namespace fs = std::filesystem;
        char namebuf[64];
        std::snprintf(namebuf, sizeof(namebuf), "_join_%llx.dbf",
                      static_cast<unsigned long long>(
                          openads::platform::monotonic_nanos()));
        fs::path tmp_dbf = fs::path(c->data_dir()) / namebuf;
        {
            std::ofstream out(tmp_dbf, std::ios::binary);
            if (!out) return fail(openads::AE_INTERNAL_ERROR,
                                  "join temp DBF open failed");
            out.write(reinterpret_cast<const char*>(file.data()),
                      static_cast<std::streamsize>(file.size()));
        }

        std::string rel = tmp_dbf.filename().string();
        auto cth = c->open_table(rel,
                                 openads::engine::TableType::Cdx,
                                 openads::engine::OpenMode::Read);
        if (!cth) return fail(cth.error());
        openads::engine::Table* ctbl = c->lookup_table(cth.value());
        if (!ctbl) return fail(openads::AE_INTERNAL_ERROR, "post-open");

        // M10.20: apply outer WHERE / ORDER BY against the merged
        // cursor's schema (left names verbatim; right names as
        // `R_<orig>`).
        if (parsed.value().where) {
            using Pred = std::function<bool(openads::engine::Table&)>;
            std::function<openads::util::Result<Pred>(
                const openads::sql::WhereExpr&)> compile;
            compile = [&](const openads::sql::WhereExpr& node)
                      -> openads::util::Result<Pred> {
                using Kind = openads::sql::WhereExpr::Kind;
                if (node.kind == Kind::And || node.kind == Kind::Or) {
                    std::vector<Pred> ks;
                    for (auto& cn : node.children) {
                        auto r = compile(*cn);
                        if (!r) return r.error();
                        ks.push_back(std::move(r).value());
                    }
                    bool is_and = (node.kind == Kind::And);
                    return Pred{[ks = std::move(ks), is_and]
                                (openads::engine::Table& t) {
                        if (is_and) {
                            for (auto& k : ks) if (!k(t)) return false;
                            return true;
                        }
                        for (auto& k : ks) if (k(t)) return true;
                        return false;
                    }};
                }
                if (node.kind == Kind::Not) {
                    auto inner = compile(*node.child);
                    if (!inner) return inner.error();
                    return Pred{[p = std::move(inner).value()]
                                (openads::engine::Table& t){return !p(t);}};
                }
                if (node.kind == Kind::Cmp) {
                    const auto& w = node.cmp;
                    std::int32_t fi = ctbl->field_index(w.column);
                    if (fi < 0) {
                        return openads::util::Error{
                            openads::AE_COLUMN_NOT_FOUND, 0,
                            w.column.c_str(), ""};
                    }
                    std::uint16_t f = static_cast<std::uint16_t>(fi);
                    openads::sql::WhereOp op = w.op;
                    std::string lit = w.literal;
                    bool is_num     = w.is_numeric;
                    double num      = w.number;
                    std::shared_ptr<std::unordered_set<std::uint32_t>> contains_hits;
                    if (op == openads::sql::WhereOp::Contains) {
                        namespace fs = std::filesystem;
                        fs::path fts_path =
                            fs::path(ctbl->path()).replace_extension(".fts");
                        auto loaded = openads::engine::Fts::load(fts_path.string());
                        if (loaded) {
                            openads::engine::FtsOptions opts;
                            auto hits = openads::engine::Fts::search(
                                loaded.value(), lit, opts);
                            contains_hits =
                                std::make_shared<std::unordered_set<std::uint32_t>>(
                                    hits.begin(), hits.end());
                        }
                    }
                    std::string lit2 = w.literal2;
                    double num2 = w.number2;
                    openads::sql::WhereFn lhs_fn = w.lhs_fn;
                    return Pred{[f, op, lit, lit2, is_num, num, num2,
                                 contains_hits, lhs_fn]
                                (openads::engine::Table& t) {
                        if (op == openads::sql::WhereOp::Contains) {
                            if (!contains_hits) return false;
                            return contains_hits->find(t.recno()) !=
                                   contains_hits->end();
                        }
                        auto v = t.read_field(f);
                        if (!v) return false;
                        if (op == openads::sql::WhereOp::IsNull ||
                            op == openads::sql::WhereOp::IsNotNull) {
                            bool null_ish = t.is_field_null(f);
                            if (!null_ish) {
                                auto sv = v.value().as_string;
                                while (!sv.empty() && sv.back() == ' ') sv.pop_back();
                                null_ish = sv.empty();
                            }
                            return op == openads::sql::WhereOp::IsNull
                                ? null_ish : !null_ish;
                        }
                        if (op == openads::sql::WhereOp::Between) {
                            if (is_num) {
                                double d = v.value().as_double;
                                return d >= num && d <= num2;
                            }
                            auto sv = apply_where_fn(v.value().as_string, lhs_fn);
                            return sv.compare(lit) >= 0 && sv.compare(lit2) <= 0;
                        }
                        if (op == openads::sql::WhereOp::Like) {
                            auto sv = apply_where_fn(v.value().as_string, lhs_fn);
                            while (!sv.empty() && sv.back() == ' ') sv.pop_back();
                            return sql_like_match(sv, lit);
                        }
                        int cmp = 0;
                        if (is_num) {
                            double d = v.value().as_double;
                            if      (d < num) cmp = -1;
                            else if (d > num) cmp =  1;
                        } else {
                            cmp = apply_where_fn(v.value().as_string, lhs_fn)
                                      .compare(lit);
                        }
                        switch (op) {
                            case openads::sql::WhereOp::Eq: return cmp == 0;
                            case openads::sql::WhereOp::Ne: return cmp != 0;
                            case openads::sql::WhereOp::Lt: return cmp <  0;
                            case openads::sql::WhereOp::Gt: return cmp >  0;
                            case openads::sql::WhereOp::Le: return cmp <= 0;
                            case openads::sql::WhereOp::Ge: return cmp >= 0;
                        default: return false;
                        }
                    }};
                }
                if (node.kind == Kind::In) {
                    std::int32_t fidx = ctbl->field_index(node.in_clause.column);
                    if (fidx < 0) return openads::util::Error{
                        openads::AE_COLUMN_NOT_FOUND, 0,
                        node.in_clause.column.c_str(), ""};
                    std::uint16_t fi = static_cast<std::uint16_t>(fidx);
                    auto set = std::make_shared<std::unordered_set<std::string>>(
                        node.in_clause.literals.begin(),
                        node.in_clause.literals.end());
                    return Pred{[fi, set](openads::engine::Table& t) {
                        auto v = t.read_field(fi);
                        if (!v) return false;
                        auto sv = v.value().as_string;
                        while (!sv.empty() && sv.back() == ' ') sv.pop_back();
                        return set->count(sv) > 0;
                    }};
                }
                return openads::util::Error{
                    openads::AE_FUNCTION_NOT_AVAILABLE, 0,
                    "join cursor WHERE supports Cmp/AND/OR/NOT/IN only", ""};
            };
            auto compiled = compile(*parsed.value().where);
            if (!compiled) return fail(compiled.error());
            ctbl->set_filter(std::move(compiled).value());
        }
        if (parsed.value().order_by) {
            // M10.37 — multi-column ORDER BY against the joined cursor.
            struct SortKey {
                std::uint16_t field_index;
                bool          descending;
                bool          numeric;
            };
            std::vector<SortKey> sks;
            auto add_sk = [&](const openads::sql::OrderBy& ob)
                -> openads::util::Result<std::monostate>
            {
                std::int32_t fi = ctbl->field_index(ob.column);
                if (fi < 0) return openads::util::Error{
                    openads::AE_COLUMN_NOT_FOUND, 0,
                    ob.column.c_str(), ""};
                const auto& fd = ctbl->field_descriptor(
                    static_cast<std::uint16_t>(fi));
                SortKey k;
                k.field_index = static_cast<std::uint16_t>(fi);
                k.descending  = ob.descending;
                k.numeric =
                    fd.type == openads::drivers::DbfFieldType::Numeric ||
                    fd.type == openads::drivers::DbfFieldType::Float   ||
                    fd.type == openads::drivers::DbfFieldType::Integer ||
                    fd.type == openads::drivers::DbfFieldType::Currency||
                    fd.type == openads::drivers::DbfFieldType::Double;
                sks.push_back(k);
                return std::monostate{};
            };
            if (auto r = add_sk(*parsed.value().order_by); !r)
                return fail(r.error());
            for (auto& obx : parsed.value().order_by_extra) {
                if (auto r = add_sk(obx); !r) return fail(r.error());
            }
            struct Row {
                std::uint32_t            recno;
                std::vector<std::string> s;
                std::vector<double>      d;
            };
            std::vector<Row> rows;
            std::uint32_t crc = ctbl->record_count();
            for (std::uint32_t r = 1; r <= crc; ++r) {
                if (auto g = ctbl->goto_record(r); !g) continue;
                if (ctbl->is_deleted()) continue;
                if (!ctbl->passes_filter()) continue;
                Row row;
                row.recno = r;
                row.s.resize(sks.size());
                row.d.resize(sks.size());
                for (std::size_t i = 0; i < sks.size(); ++i) {
                    auto v = ctbl->read_field(sks[i].field_index);
                    if (v) {
                        row.s[i] = v.value().as_string;
                        row.d[i] = v.value().as_double;
                    }
                }
                rows.push_back(std::move(row));
            }
            std::stable_sort(rows.begin(), rows.end(),
                [&](const Row& a, const Row& b) {
                    for (std::size_t i = 0; i < sks.size(); ++i) {
                        bool less, equal;
                        if (sks[i].numeric) {
                            less  = a.d[i] <  b.d[i];
                            equal = a.d[i] == b.d[i];
                        } else {
                            less  = a.s[i] <  b.s[i];
                            equal = a.s[i] == b.s[i];
                        }
                        if (equal) continue;
                        return sks[i].descending ? !less : less;
                    }
                    return false;
                });
            std::vector<std::uint32_t> seq;
            seq.reserve(rows.size());
            for (auto& row : rows) seq.push_back(row.recno);
            ctbl->clear_filter();
            ctbl->set_recno_sequence(std::move(seq));
        }

        // M10.34 — GROUP BY across JOIN. Same shape as the plain-table
        // grouped path (M10.25) but reads from the merged cursor.
        if (!parsed.value().group_by.empty() &&
            !parsed.value().aggregates.empty()) {
            struct AggSlot {
                openads::sql::Aggregate def;
                std::int32_t            field_index = -1;
            };
            std::vector<AggSlot> slots;
            slots.reserve(parsed.value().aggregates.size());
            for (auto& a : parsed.value().aggregates) {
                AggSlot slot;
                slot.def = a;
                if (a.kind != openads::sql::AggregateKind::CountStar) {
                    slot.field_index = ctbl->field_index(a.column);
                    if (slot.field_index < 0) {
                        c->close_table(cth.value());
                        return fail(openads::AE_COLUMN_NOT_FOUND,
                                    a.column.c_str());
                    }
                }
                slots.push_back(std::move(slot));
            }

            struct GBCol {
                std::uint16_t field_index;
                std::uint8_t  length;
                std::string   name;
                std::uint8_t  raw_type;
            };
            std::vector<GBCol> gbs;
            gbs.reserve(parsed.value().group_by.size());
            for (auto& gname : parsed.value().group_by) {
                std::int32_t fi = ctbl->field_index(gname);
                if (fi < 0) {
                    c->close_table(cth.value());
                    return fail(openads::AE_COLUMN_NOT_FOUND, gname.c_str());
                }
                const auto& fd = ctbl->field_descriptor(
                    static_cast<std::uint16_t>(fi));
                GBCol gc;
                gc.field_index = static_cast<std::uint16_t>(fi);
                gc.length      = static_cast<std::uint8_t>(fd.length);
                gc.name        = gname;
                gc.raw_type    = static_cast<std::uint8_t>(fd.raw_type);
                gbs.push_back(std::move(gc));
            }

            auto resolve_slot = [&](const openads::sql::HavingCmp& ha)
                                  -> std::int32_t {
                for (std::size_t i = 0; i < slots.size(); ++i) {
                    if (slots[i].def.kind == ha.agg.kind &&
                        slots[i].def.column == ha.agg.column) {
                        return static_cast<std::int32_t>(i);
                    }
                }
                if (ha.agg.kind == openads::sql::AggregateKind::CountStar) {
                    for (std::size_t i = 0; i < slots.size(); ++i) {
                        if (slots[i].def.kind ==
                            openads::sql::AggregateKind::CountStar) {
                            return static_cast<std::int32_t>(i);
                        }
                    }
                }
                return -1;
            };
            if (parsed.value().having) {
                std::function<openads::util::Result<std::monostate>(
                    const openads::sql::HavingExpr&)> validate;
                validate = [&](const openads::sql::HavingExpr& n)
                            -> openads::util::Result<std::monostate> {
                    using K = openads::sql::HavingExpr::Kind;
                    if (n.kind == K::And || n.kind == K::Or) {
                        for (auto& cn : n.children) {
                            auto r = validate(*cn);
                            if (!r) return r.error();
                        }
                        return std::monostate{};
                    }
                    if (n.kind == K::Not) return validate(*n.child);
                    if (resolve_slot(n.cmp) < 0) {
                        return openads::util::Error{
                            openads::AE_PARSE_ERROR, 0,
                            "HAVING aggregate must match one in projection",
                            ""};
                    }
                    return std::monostate{};
                };
                auto vr = validate(*parsed.value().having);
                if (!vr) {
                    c->close_table(cth.value());
                    return fail(vr.error());
                }
            }

            struct GroupAcc {
                std::vector<std::string>   key_parts;
                std::vector<double>        sum;
                std::vector<double>        minv;
                std::vector<double>        maxv;
                std::vector<std::uint64_t> count;
                std::uint64_t              row_count = 0;
            };
            std::unordered_map<std::string, GroupAcc> groups;
            std::vector<std::string> insertion_order;
            std::uint32_t crc3 = ctbl->record_count();
            for (std::uint32_t r = 1; r <= crc3; ++r) {
                if (auto g = ctbl->goto_record(r); !g) continue;
                if (ctbl->is_deleted()) continue;
                if (!ctbl->passes_filter()) continue;
                std::string key;
                std::vector<std::string> parts;
                parts.reserve(gbs.size());
                for (auto& g : gbs) {
                    auto v = ctbl->read_field(g.field_index);
                    std::string raw = v ? v.value().as_string : std::string();
                    if (raw.size() < g.length)
                        raw.append(g.length - raw.size(), ' ');
                    else if (raw.size() > g.length) raw.resize(g.length);
                    parts.push_back(raw);
                    key.append(raw);
                    key.push_back('\x1f');
                }
                auto git = groups.find(key);
                if (git == groups.end()) {
                    GroupAcc acc;
                    acc.key_parts = std::move(parts);
                    acc.sum.assign(slots.size(), 0.0);
                    acc.minv.assign(slots.size(),
                        std::numeric_limits<double>::infinity());
                    acc.maxv.assign(slots.size(),
                        -std::numeric_limits<double>::infinity());
                    acc.count.assign(slots.size(), 0);
                    git = groups.emplace(key, std::move(acc)).first;
                    insertion_order.push_back(key);
                }
                auto& acc = git->second;
                ++acc.row_count;
                for (std::size_t i = 0; i < slots.size(); ++i) {
                    if (slots[i].def.kind ==
                        openads::sql::AggregateKind::CountStar) {
                        ++acc.count[i]; continue;
                    }
                    auto v = ctbl->read_field(
                        static_cast<std::uint16_t>(slots[i].field_index));
                    if (!v) continue;
                    double d = v.value().as_double;
                    ++acc.count[i];
                    acc.sum[i] += d;
                    if (d < acc.minv[i]) acc.minv[i] = d;
                    if (d > acc.maxv[i]) acc.maxv[i] = d;
                }
            }
            c->close_table(cth.value());

            auto agg_at = [&](const GroupAcc& acc, std::size_t si) -> double {
                using K = openads::sql::AggregateKind;
                switch (slots[si].def.kind) {
                    case K::CountStar: return static_cast<double>(acc.row_count);
                    case K::Count:     return static_cast<double>(acc.count[si]);
                    case K::Sum:       return acc.sum[si];
                    case K::Avg:
                        return acc.count[si]
                            ? acc.sum[si] / static_cast<double>(acc.count[si])
                            : 0.0;
                    case K::Min:
                        return acc.count[si] ? acc.minv[si] : 0.0;
                    case K::Max:
                        return acc.count[si] ? acc.maxv[si] : 0.0;
                }
                return 0.0;
            };
            std::function<bool(const openads::sql::HavingExpr&,
                               const GroupAcc&)> eval_having;
            eval_having = [&](const openads::sql::HavingExpr& n,
                              const GroupAcc& acc) -> bool {
                using K = openads::sql::HavingExpr::Kind;
                if (n.kind == K::And) {
                    for (auto& cn : n.children)
                        if (!eval_having(*cn, acc)) return false;
                    return true;
                }
                if (n.kind == K::Or) {
                    for (auto& cn : n.children)
                        if (eval_having(*cn, acc)) return true;
                    return false;
                }
                if (n.kind == K::Not) return !eval_having(*n.child, acc);
                std::int32_t si = resolve_slot(n.cmp);
                if (si < 0) return false;
                double v   = agg_at(acc, static_cast<std::size_t>(si));
                double rhs = n.cmp.num;
                switch (n.cmp.op) {
                    case openads::sql::WhereOp::Eq: return v == rhs;
                    case openads::sql::WhereOp::Ne: return v != rhs;
                    case openads::sql::WhereOp::Lt: return v <  rhs;
                    case openads::sql::WhereOp::Gt: return v >  rhs;
                    case openads::sql::WhereOp::Le: return v <= rhs;
                    case openads::sql::WhereOp::Ge: return v >= rhs;
                    default: return false;
                }
            };

            namespace fs = std::filesystem;
            char namebuf4[64];
            std::snprintf(namebuf4, sizeof(namebuf4), "_jgrp_%llx.dbf",
                          static_cast<unsigned long long>(
                              openads::platform::monotonic_nanos()));
            fs::path grp_dbf = fs::path(c->data_dir()) / namebuf4;
            std::vector<std::uint8_t> jg_file;
            std::array<std::uint8_t, 32> jg_hdr{};
            jg_hdr[0] = 0x03;
            stamp_dbf_header_today(jg_hdr.data());
            std::uint16_t jg_hlen = static_cast<std::uint16_t>(
                32 + 32 * (gbs.size() + slots.size()) + 1);
            std::uint32_t jg_rlen = 1;
            for (auto& g : gbs) jg_rlen += g.length;
            jg_rlen += 30u * static_cast<std::uint32_t>(slots.size());
            jg_hdr[8]  = static_cast<std::uint8_t>( jg_hlen       & 0xFFu);
            jg_hdr[9]  = static_cast<std::uint8_t>((jg_hlen >> 8) & 0xFFu);
            jg_hdr[10] = static_cast<std::uint8_t>( jg_rlen       & 0xFFu);
            jg_hdr[11] = static_cast<std::uint8_t>((jg_rlen >> 8) & 0xFFu);
            jg_file.insert(jg_file.end(), jg_hdr.begin(), jg_hdr.end());
            for (auto& g : gbs) {
                std::array<std::uint8_t, 32> fd{};
                std::memcpy(fd.data(), g.name.data(),
                            std::min(g.name.size(), std::size_t{11}));
                fd[11] = g.raw_type ? g.raw_type : 'C';
                fd[16] = g.length;
                jg_file.insert(jg_file.end(), fd.begin(), fd.end());
            }
            for (std::size_t i = 0; i < slots.size(); ++i) {
                std::array<std::uint8_t, 32> fd{};
                char fn[16];
                std::snprintf(fn, sizeof(fn), "COL%u",
                              static_cast<unsigned>((i + 1) & 0xFFFFFu));
                std::size_t fn_len = std::strlen(fn);
                std::memcpy(fd.data(), fn,
                            fn_len > 11 ? 11 : fn_len);
                fd[11] = 'C'; fd[16] = 30;
                jg_file.insert(jg_file.end(), fd.begin(), fd.end());
            }
            jg_file.push_back(0x0D);

            std::uint32_t jg_emitted = 0;
            for (auto& key : insertion_order) {
                auto& acc = groups[key];
                if (parsed.value().having) {
                    if (!eval_having(*parsed.value().having, acc)) continue;
                }
                jg_file.push_back(' ');
                for (std::size_t i = 0; i < gbs.size(); ++i) {
                    const std::string& kp = acc.key_parts[i];
                    for (std::uint8_t b = 0; b < gbs[i].length; ++b) {
                        jg_file.push_back(b < kp.size()
                            ? static_cast<std::uint8_t>(kp[b]) : ' ');
                    }
                }
                for (std::size_t i = 0; i < slots.size(); ++i) {
                    char buf[32] = {0};
                    using K = openads::sql::AggregateKind;
                    switch (slots[i].def.kind) {
                        case K::CountStar:
                            std::snprintf(buf, sizeof(buf), "%llu",
                                static_cast<unsigned long long>(acc.row_count));
                            break;
                        case K::Count:
                            std::snprintf(buf, sizeof(buf), "%llu",
                                static_cast<unsigned long long>(acc.count[i]));
                            break;
                        case K::Sum:
                            std::snprintf(buf, sizeof(buf), "%.6f", acc.sum[i]);
                            break;
                        case K::Avg:
                            std::snprintf(buf, sizeof(buf), "%.6f",
                                acc.count[i]
                                    ? acc.sum[i] /
                                        static_cast<double>(acc.count[i])
                                    : 0.0);
                            break;
                        case K::Min:
                            if (acc.count[i] == 0) std::memcpy(buf, "0", 2);
                            else std::snprintf(buf, sizeof(buf), "%.6f",
                                               acc.minv[i]);
                            break;
                        case K::Max:
                            if (acc.count[i] == 0) std::memcpy(buf, "0", 2);
                            else std::snprintf(buf, sizeof(buf), "%.6f",
                                               acc.maxv[i]);
                            break;
                    }
                    std::array<std::uint8_t, 30> cell{};
                    std::memset(cell.data(), ' ', cell.size());
                    std::size_t n = std::min<std::size_t>(std::strlen(buf), 30);
                    std::memcpy(cell.data(), buf, n);
                    jg_file.insert(jg_file.end(), cell.begin(), cell.end());
                }
                ++jg_emitted;
            }
            jg_file.push_back(0x1A);
            jg_file[4] = static_cast<std::uint8_t>( jg_emitted        & 0xFFu);
            jg_file[5] = static_cast<std::uint8_t>((jg_emitted >>  8) & 0xFFu);
            jg_file[6] = static_cast<std::uint8_t>((jg_emitted >> 16) & 0xFFu);
            jg_file[7] = static_cast<std::uint8_t>((jg_emitted >> 24) & 0xFFu);
            {
                std::ofstream out(grp_dbf, std::ios::binary);
                if (!out) return fail(openads::AE_INTERNAL_ERROR,
                    "join+group temp DBF: open for write failed");
                out.write(reinterpret_cast<const char*>(jg_file.data()),
                          static_cast<std::streamsize>(jg_file.size()));
            }
            std::string rel4 = grp_dbf.filename().string();
            auto gth = c->open_table(rel4, openads::engine::TableType::Cdx,
                                     openads::engine::OpenMode::Read);
            if (!gth) return fail(gth.error());
            openads::engine::Table* gtbl = c->lookup_table(gth.value());
            if (!gtbl) return fail(openads::AE_INTERNAL_ERROR, "post-open");
            ADSHANDLE gh = s.registry.register_object(HandleKind::Table, gtbl);
            *phCursor = gh;
            return ok();
        }

        // M10.23 — JOIN + aggregate. Walk the merged cursor (already
        // filtered by the outer WHERE) and replace it with a 1-row
        // aggregate temp DBF before registering the user-visible
        // handle.
        if (!parsed.value().aggregates.empty()) {
            struct AggSlot {
                openads::sql::Aggregate def;
                std::int32_t            field_index = -1;
            };
            std::vector<AggSlot> slots;
            slots.reserve(parsed.value().aggregates.size());
            for (auto& a : parsed.value().aggregates) {
                AggSlot slot;
                slot.def = a;
                if (a.kind != openads::sql::AggregateKind::CountStar) {
                    slot.field_index = ctbl->field_index(a.column);
                    if (slot.field_index < 0) {
                        c->close_table(cth.value());
                        return fail(openads::AE_COLUMN_NOT_FOUND, a.column.c_str());
                    }
                }
                slots.push_back(std::move(slot));
            }

            std::vector<double> sum(slots.size(), 0.0);
            std::vector<double> minv(slots.size(),
                std::numeric_limits<double>::infinity());
            std::vector<double> maxv(slots.size(),
                -std::numeric_limits<double>::infinity());
            std::vector<std::uint64_t> count(slots.size(), 0);
            std::uint64_t row_count = 0;
            std::uint32_t crc2 = ctbl->record_count();
            for (std::uint32_t r = 1; r <= crc2; ++r) {
                if (auto g = ctbl->goto_record(r); !g) continue;
                if (ctbl->is_deleted()) continue;
                if (!ctbl->passes_filter()) continue;
                ++row_count;
                for (std::size_t i = 0; i < slots.size(); ++i) {
                    if (slots[i].def.kind == openads::sql::AggregateKind::CountStar) {
                        ++count[i]; continue;
                    }
                    auto v = ctbl->read_field(
                        static_cast<std::uint16_t>(slots[i].field_index));
                    if (!v) continue;
                    double d = v.value().as_double;
                    ++count[i];
                    sum[i] += d;
                    if (d < minv[i]) minv[i] = d;
                    if (d > maxv[i]) maxv[i] = d;
                }
            }
            c->close_table(cth.value());

            namespace fs = std::filesystem;
            char namebuf2[64];
            std::snprintf(namebuf2, sizeof(namebuf2), "_jagg_%llx.dbf",
                          static_cast<unsigned long long>(
                              openads::platform::monotonic_nanos()));
            fs::path agg_dbf = fs::path(c->data_dir()) / namebuf2;
            std::vector<std::uint8_t> agg_file;
            std::array<std::uint8_t, 32> agg_hdr{};
            agg_hdr[0] = 0x03;
            stamp_dbf_header_today(agg_hdr.data());
            agg_hdr[4] = 1;
            std::uint16_t agg_hlen = static_cast<std::uint16_t>(
                32 + 32 * slots.size() + 1);
            std::uint16_t agg_rlen = static_cast<std::uint16_t>(
                1 + 30 * slots.size());
            agg_hdr[8]  = static_cast<std::uint8_t>( agg_hlen       & 0xFFu);
            agg_hdr[9]  = static_cast<std::uint8_t>((agg_hlen >> 8) & 0xFFu);
            agg_hdr[10] = static_cast<std::uint8_t>( agg_rlen       & 0xFFu);
            agg_hdr[11] = static_cast<std::uint8_t>((agg_rlen >> 8) & 0xFFu);
            agg_file.insert(agg_file.end(), agg_hdr.begin(), agg_hdr.end());
            for (std::size_t i = 0; i < slots.size(); ++i) {
                std::array<std::uint8_t, 32> fd{};
                char fn[16];
                std::snprintf(fn, sizeof(fn), "COL%u",
                              static_cast<unsigned>((i + 1) & 0xFFFFFu));
                std::size_t fn_len = std::strlen(fn);
                std::memcpy(fd.data(), fn, fn_len > 11 ? 11 : fn_len);
                fd[11] = 'C'; fd[16] = 30;
                agg_file.insert(agg_file.end(), fd.begin(), fd.end());
            }
            agg_file.push_back(0x0D);
            agg_file.push_back(' ');
            for (std::size_t i = 0; i < slots.size(); ++i) {
                char buf[32] = {0};
                switch (slots[i].def.kind) {
                    case openads::sql::AggregateKind::CountStar:
                    case openads::sql::AggregateKind::Count:
                        std::snprintf(buf, sizeof(buf), "%llu",
                            static_cast<unsigned long long>(
                                slots[i].def.kind ==
                                openads::sql::AggregateKind::CountStar
                                    ? row_count : count[i]));
                        break;
                    case openads::sql::AggregateKind::Sum:
                        std::snprintf(buf, sizeof(buf), "%.6f", sum[i]);
                        break;
                    case openads::sql::AggregateKind::Avg:
                        std::snprintf(buf, sizeof(buf), "%.6f",
                            count[i] ? sum[i] / static_cast<double>(count[i])
                                     : 0.0);
                        break;
                    case openads::sql::AggregateKind::Min:
                        if (count[i] == 0) std::memcpy(buf, "0", 2);
                        else std::snprintf(buf, sizeof(buf), "%.6f", minv[i]);
                        break;
                    case openads::sql::AggregateKind::Max:
                        if (count[i] == 0) std::memcpy(buf, "0", 2);
                        else std::snprintf(buf, sizeof(buf), "%.6f", maxv[i]);
                        break;
                }
                std::array<std::uint8_t, 30> cell{};
                std::memset(cell.data(), ' ', cell.size());
                std::size_t n = std::min<std::size_t>(std::strlen(buf), 30);
                std::memcpy(cell.data(), buf, n);
                agg_file.insert(agg_file.end(), cell.begin(), cell.end());
            }
            agg_file.push_back(0x1A);
            {
                std::ofstream out(agg_dbf, std::ios::binary);
                if (!out) return fail(openads::AE_INTERNAL_ERROR,
                                      "join+agg temp DBF: open for write failed");
                out.write(reinterpret_cast<const char*>(agg_file.data()),
                          static_cast<std::streamsize>(agg_file.size()));
            }
            std::string rel2 = agg_dbf.filename().string();
            auto ath = c->open_table(rel2, openads::engine::TableType::Cdx,
                                     openads::engine::OpenMode::Read);
            if (!ath) return fail(ath.error());
            openads::engine::Table* atbl = c->lookup_table(ath.value());
            if (!atbl) return fail(openads::AE_INTERNAL_ERROR, "post-open");
            ADSHANDLE gh = s.registry.register_object(HandleKind::Table, atbl);
            *phCursor = gh;
            return ok();
        }

        ADSHANDLE gh = s.registry.register_object(HandleKind::Table, ctbl);
        *phCursor = gh;
        return ok();
    }

    // M10.46 — derived table: `FROM (SELECT ...)`. Recursively run
    // the inner SELECT first; the resulting cursor's underlying
    // engine::Table becomes the source for the outer clauses.
    auto& s = state();
    openads::engine::Table* tbl = nullptr;
    ADSHANDLE derived_cur = 0;
    if (!parsed.value().derived_sql.empty()) {
        std::vector<UNSIGNED8> selbuf(parsed.value().derived_sql.size() + 1);
        std::memcpy(selbuf.data(),
                    parsed.value().derived_sql.c_str(),
                    parsed.value().derived_sql.size() + 1);
        UNSIGNED32 rrc =
            AdsExecuteSQLDirect(hStatement, selbuf.data(), &derived_cur);
        if (rrc != 0) return rrc;
        std::lock_guard<std::recursive_mutex> lk(s.mu);
        tbl = s.registry.lookup<openads::engine::Table>(
            derived_cur, HandleKind::Table);
        if (!tbl) return fail(openads::AE_INTERNAL_ERROR,
                              "derived table cursor lookup");
        // continue, lock_guard scoped to whole function below by
        // dropping out — we want to hold lock through registration.
        // Since `lk` would die at end of this `if` block, re-take.
    }
    std::lock_guard<std::recursive_mutex> lk(s.mu);
    Handle table_handle = 0;
    if (!tbl) {
        auto th = open_or_sys(parsed.value().table,
                              stmt_table_type(*it->second),
                              stmt_open_mode(*it->second, false),
                              stmt_locking_mode(*it->second));
        if (!th) return fail(th.error());
        table_handle = th.value();
        tbl = c->lookup_table(table_handle);
        if (!tbl) return fail(openads::AE_INTERNAL_ERROR, "post-open");
    }
    (void)table_handle;

    // M10.10: aggregate query — walk matching rows, compute the
    // aggregate accumulators, materialise a 1-row temp DBF with one
    // numeric column per aggregate, and return a cursor on it.
    if (!parsed.value().aggregates.empty()) {
        // Resolve each aggregate's column index up front.
        struct AggSlot {
            openads::sql::Aggregate def;
            std::int32_t            field_index = -1;   // -1 for COUNT(*)
        };
        std::vector<AggSlot> slots;
        slots.reserve(parsed.value().aggregates.size());
        for (auto& a : parsed.value().aggregates) {
            AggSlot slot;
            slot.def = a;
            if (a.kind != openads::sql::AggregateKind::CountStar) {
                slot.field_index = tbl->field_index(a.column);
                if (slot.field_index < 0) {
                    if (table_handle != 0) c->close_table(table_handle);
                    return fail(openads::AE_COLUMN_NOT_FOUND, a.column.c_str());
                }
            }
            slots.push_back(std::move(slot));
        }

        // Build the WHERE filter (same shape as the SELECT branch
        // below — but the predicate compiles independently here so
        // the aggregate path doesn't depend on that block's lambdas).
        std::function<bool(openads::engine::Table&)> filter;
        if (parsed.value().where) {
            using Pred = std::function<bool(openads::engine::Table&)>;
            std::function<openads::util::Result<Pred>(
                const openads::sql::WhereExpr&)> compile;
            compile = [&](const openads::sql::WhereExpr& node)
                      -> openads::util::Result<Pred> {
                using Kind = openads::sql::WhereExpr::Kind;
                if (node.kind == Kind::And || node.kind == Kind::Or) {
                    std::vector<Pred> ks;
                    for (auto& cn : node.children) {
                        auto r = compile(*cn);
                        if (!r) return r.error();
                        ks.push_back(std::move(r).value());
                    }
                    bool is_and = (node.kind == Kind::And);
                    return Pred{[ks = std::move(ks), is_and]
                                (openads::engine::Table& t) {
                        if (is_and) {
                            for (auto& k : ks) if (!k(t)) return false;
                            return true;
                        }
                        for (auto& k : ks) if (k(t)) return true;
                        return false;
                    }};
                }
                if (node.kind == Kind::Not) {
                    auto inner = compile(*node.child);
                    if (!inner) return inner.error();
                    return Pred{[p = std::move(inner).value()]
                                (openads::engine::Table& t){return !p(t);}};
                }
                const auto& w = node.cmp;
                std::int32_t fidx = tbl->field_index(w.column);
                if (fidx < 0) {
                    return openads::util::Error{
                        openads::AE_COLUMN_NOT_FOUND, 0,
                        w.column.c_str(), ""};
                }
                std::uint16_t fi = static_cast<std::uint16_t>(fidx);
                openads::sql::WhereOp op = w.op;
                std::string lit = w.literal;
                bool is_num     = w.is_numeric;
                double num      = w.number;
                openads::sql::WhereFn lhs_fn = w.lhs_fn;
                return Pred{[fi, op, lit, is_num, num, lhs_fn]
                            (openads::engine::Table& t) {
                    auto v = t.read_field(fi);
                    if (!v) return false;
                    int cmp = 0;
                    if (is_num) {
                        double d = v.value().as_double;
                        if      (d < num) cmp = -1;
                        else if (d > num) cmp =  1;
                    } else {
                        cmp = apply_where_fn(v.value().as_string, lhs_fn)
                                  .compare(lit);
                    }
                    switch (op) {
                        case openads::sql::WhereOp::Eq: return cmp == 0;
                        case openads::sql::WhereOp::Ne: return cmp != 0;
                        case openads::sql::WhereOp::Lt: return cmp <  0;
                        case openads::sql::WhereOp::Gt: return cmp >  0;
                        case openads::sql::WhereOp::Le: return cmp <= 0;
                        case openads::sql::WhereOp::Ge: return cmp >= 0;
                        case openads::sql::WhereOp::Contains: return false;
                        default: return false;
                    }
                    return false;
                }};
            };
            auto compiled = compile(*parsed.value().where);
            if (!compiled) {
                if (table_handle != 0) c->close_table(table_handle);
                return fail(compiled.error());
            }
            filter = std::move(compiled).value();
        }

        // M10.25 — `GROUP BY <col>[, <col>...] [HAVING <agg> op num]`.
        // Walk matching rows, hash by group-key tuple, accumulate per
        // group, then emit one row per group (passing HAVING) into a
        // multi-row temp DBF cursor. Schema: original group-by
        // columns (preserving type + length) followed by COL1..COLn
        // C(30) cells for each aggregate.
        if (!parsed.value().group_by.empty()) {
            struct GBCol {
                std::uint16_t field_index;
                std::uint8_t  length;
                std::string   name;
                std::uint8_t  raw_type;
            };
            std::vector<GBCol> gbs;
            gbs.reserve(parsed.value().group_by.size());
            for (auto& gname : parsed.value().group_by) {
                std::int32_t fi = tbl->field_index(gname);
                if (fi < 0) {
                    if (table_handle != 0) c->close_table(table_handle);
                    return fail(openads::AE_COLUMN_NOT_FOUND, gname.c_str());
                }
                const auto& fd = tbl->field_descriptor(
                    static_cast<std::uint16_t>(fi));
                GBCol gc;
                gc.field_index = static_cast<std::uint16_t>(fi);
                gc.length      = static_cast<std::uint8_t>(fd.length);
                gc.name        = gname;
                gc.raw_type    = static_cast<std::uint8_t>(fd.raw_type);
                gbs.push_back(std::move(gc));
            }

            // M10.30 — HAVING is now a boolean tree of HavingCmp leaves.
            // Resolve each leaf to a slot at compile time (validation
            // only); per-group evaluation re-walks the tree.
            auto resolve_slot = [&](const openads::sql::HavingCmp& ha)
                                  -> std::int32_t {
                for (std::size_t i = 0; i < slots.size(); ++i) {
                    if (slots[i].def.kind == ha.agg.kind &&
                        slots[i].def.column == ha.agg.column) {
                        return static_cast<std::int32_t>(i);
                    }
                }
                if (ha.agg.kind == openads::sql::AggregateKind::CountStar) {
                    for (std::size_t i = 0; i < slots.size(); ++i) {
                        if (slots[i].def.kind ==
                            openads::sql::AggregateKind::CountStar) {
                            return static_cast<std::int32_t>(i);
                        }
                    }
                }
                return -1;
            };
            if (parsed.value().having) {
                std::function<openads::util::Result<std::monostate>(
                    const openads::sql::HavingExpr&)> validate;
                validate = [&](const openads::sql::HavingExpr& n)
                            -> openads::util::Result<std::monostate> {
                    using K = openads::sql::HavingExpr::Kind;
                    if (n.kind == K::And || n.kind == K::Or) {
                        for (auto& cn : n.children) {
                            auto r = validate(*cn);
                            if (!r) return r.error();
                        }
                        return std::monostate{};
                    }
                    if (n.kind == K::Not) return validate(*n.child);
                    if (resolve_slot(n.cmp) < 0) {
                        return openads::util::Error{
                            openads::AE_PARSE_ERROR, 0,
                            "HAVING aggregate must match one in projection",
                            ""};
                    }
                    return std::monostate{};
                };
                auto vr = validate(*parsed.value().having);
                if (!vr) {
                    if (table_handle != 0) c->close_table(table_handle);
                    return fail(vr.error());
                }
            }

            struct GroupAcc {
                std::vector<std::string>   key_parts;
                std::vector<double>        sum;
                std::vector<double>        minv;
                std::vector<double>        maxv;
                std::vector<std::uint64_t> count;
                std::uint64_t              row_count = 0;
            };
            std::unordered_map<std::string, GroupAcc> groups;
            std::vector<std::string> insertion_order;
            std::uint32_t rcount = tbl->record_count();
            for (std::uint32_t r = 1; r <= rcount; ++r) {
                if (auto g = tbl->goto_record(r); !g) continue;
                if (tbl->is_deleted()) continue;
                if (filter && !filter(*tbl)) continue;
                std::string key;
                std::vector<std::string> parts;
                parts.reserve(gbs.size());
                for (auto& g : gbs) {
                    auto v = tbl->read_field(g.field_index);
                    std::string raw = v ? v.value().as_string : std::string();
                    if (raw.size() < g.length) raw.append(g.length - raw.size(), ' ');
                    else if (raw.size() > g.length) raw.resize(g.length);
                    parts.push_back(raw);
                    key.append(raw);
                    key.push_back('\x1f');
                }
                auto git = groups.find(key);
                if (git == groups.end()) {
                    GroupAcc acc;
                    acc.key_parts = std::move(parts);
                    acc.sum.assign(slots.size(), 0.0);
                    acc.minv.assign(slots.size(),
                        std::numeric_limits<double>::infinity());
                    acc.maxv.assign(slots.size(),
                        -std::numeric_limits<double>::infinity());
                    acc.count.assign(slots.size(), 0);
                    git = groups.emplace(key, std::move(acc)).first;
                    insertion_order.push_back(key);
                }
                auto& acc = git->second;
                ++acc.row_count;
                for (std::size_t i = 0; i < slots.size(); ++i) {
                    if (slots[i].def.kind ==
                        openads::sql::AggregateKind::CountStar) {
                        ++acc.count[i];
                        continue;
                    }
                    auto v = tbl->read_field(
                        static_cast<std::uint16_t>(slots[i].field_index));
                    if (!v) continue;
                    double d = v.value().as_double;
                    ++acc.count[i];
                    acc.sum[i] += d;
                    if (d < acc.minv[i]) acc.minv[i] = d;
                    if (d > acc.maxv[i]) acc.maxv[i] = d;
                }
            }
            if (table_handle != 0) c->close_table(table_handle);

            auto agg_at = [&](const GroupAcc& acc, std::size_t si) -> double {
                using K = openads::sql::AggregateKind;
                switch (slots[si].def.kind) {
                    case K::CountStar: return static_cast<double>(acc.row_count);
                    case K::Count:     return static_cast<double>(acc.count[si]);
                    case K::Sum:       return acc.sum[si];
                    case K::Avg:
                        return acc.count[si]
                            ? acc.sum[si] / static_cast<double>(acc.count[si])
                            : 0.0;
                    case K::Min:
                        return acc.count[si] ? acc.minv[si] : 0.0;
                    case K::Max:
                        return acc.count[si] ? acc.maxv[si] : 0.0;
                }
                return 0.0;
            };

            namespace fs = std::filesystem;
            char namebuf3[64];
            std::snprintf(namebuf3, sizeof(namebuf3), "_grp_%llx.dbf",
                          static_cast<unsigned long long>(
                              openads::platform::monotonic_nanos()));
            fs::path grp_dbf = fs::path(c->data_dir()) / namebuf3;
            std::vector<std::uint8_t> file;
            std::array<std::uint8_t, 32> hdr{};
            hdr[0] = 0x03;
            stamp_dbf_header_today(hdr.data());
            std::uint16_t header_len = static_cast<std::uint16_t>(
                32 + 32 * (gbs.size() + slots.size()) + 1);
            std::uint32_t rec_len = 1;
            for (auto& g : gbs) rec_len += g.length;
            rec_len += 30u * static_cast<std::uint32_t>(slots.size());
            hdr[8]  = static_cast<std::uint8_t>( header_len       & 0xFFu);
            hdr[9]  = static_cast<std::uint8_t>((header_len >> 8) & 0xFFu);
            hdr[10] = static_cast<std::uint8_t>( rec_len          & 0xFFu);
            hdr[11] = static_cast<std::uint8_t>((rec_len    >> 8) & 0xFFu);
            file.insert(file.end(), hdr.begin(), hdr.end());

            for (auto& g : gbs) {
                std::array<std::uint8_t, 32> fd{};
                std::memcpy(fd.data(), g.name.data(),
                            std::min(g.name.size(), std::size_t{11}));
                fd[11] = g.raw_type ? g.raw_type : 'C';
                fd[16] = g.length;
                file.insert(file.end(), fd.begin(), fd.end());
            }
            for (std::size_t i = 0; i < slots.size(); ++i) {
                std::array<std::uint8_t, 32> fd{};
                char fn[16];
                std::snprintf(fn, sizeof(fn), "COL%u",
                              static_cast<unsigned>((i + 1) & 0xFFFFFu));
                std::size_t fn_len = std::strlen(fn);
                std::memcpy(fd.data(), fn, fn_len > 11 ? 11 : fn_len);
                fd[11] = 'C'; fd[16] = 30;
                file.insert(file.end(), fd.begin(), fd.end());
            }
            file.push_back(0x0D);

            std::function<bool(const openads::sql::HavingExpr&,
                               const GroupAcc&)> eval_having;
            eval_having = [&](const openads::sql::HavingExpr& n,
                              const GroupAcc& acc) -> bool {
                using K = openads::sql::HavingExpr::Kind;
                if (n.kind == K::And) {
                    for (auto& cn : n.children)
                        if (!eval_having(*cn, acc)) return false;
                    return true;
                }
                if (n.kind == K::Or) {
                    for (auto& cn : n.children)
                        if (eval_having(*cn, acc)) return true;
                    return false;
                }
                if (n.kind == K::Not) return !eval_having(*n.child, acc);
                std::int32_t si = resolve_slot(n.cmp);
                if (si < 0) return false;
                double v   = agg_at(acc, static_cast<std::size_t>(si));
                double rhs = n.cmp.num;
                switch (n.cmp.op) {
                    case openads::sql::WhereOp::Eq: return v == rhs;
                    case openads::sql::WhereOp::Ne: return v != rhs;
                    case openads::sql::WhereOp::Lt: return v <  rhs;
                    case openads::sql::WhereOp::Gt: return v >  rhs;
                    case openads::sql::WhereOp::Le: return v <= rhs;
                    case openads::sql::WhereOp::Ge: return v >= rhs;
                    default: return false;
                }
            };

            std::uint32_t emitted = 0;
            for (auto& key : insertion_order) {
                auto& acc = groups[key];
                if (parsed.value().having) {
                    if (!eval_having(*parsed.value().having, acc)) continue;
                }
                file.push_back(' ');
                for (std::size_t i = 0; i < gbs.size(); ++i) {
                    const std::string& kp = acc.key_parts[i];
                    for (std::uint8_t b = 0; b < gbs[i].length; ++b) {
                        file.push_back(b < kp.size()
                            ? static_cast<std::uint8_t>(kp[b]) : ' ');
                    }
                }
                for (std::size_t i = 0; i < slots.size(); ++i) {
                    char buf[32] = {0};
                    using K = openads::sql::AggregateKind;
                    switch (slots[i].def.kind) {
                        case K::CountStar:
                            std::snprintf(buf, sizeof(buf), "%llu",
                                static_cast<unsigned long long>(acc.row_count));
                            break;
                        case K::Count:
                            std::snprintf(buf, sizeof(buf), "%llu",
                                static_cast<unsigned long long>(acc.count[i]));
                            break;
                        case K::Sum:
                            std::snprintf(buf, sizeof(buf), "%.6f", acc.sum[i]);
                            break;
                        case K::Avg:
                            std::snprintf(buf, sizeof(buf), "%.6f",
                                acc.count[i]
                                    ? acc.sum[i] /
                                        static_cast<double>(acc.count[i])
                                    : 0.0);
                            break;
                        case K::Min:
                            if (acc.count[i] == 0) std::memcpy(buf, "0", 2);
                            else std::snprintf(buf, sizeof(buf), "%.6f",
                                               acc.minv[i]);
                            break;
                        case K::Max:
                            if (acc.count[i] == 0) std::memcpy(buf, "0", 2);
                            else std::snprintf(buf, sizeof(buf), "%.6f",
                                               acc.maxv[i]);
                            break;
                    }
                    std::array<std::uint8_t, 30> cell{};
                    std::memset(cell.data(), ' ', cell.size());
                    std::size_t n = std::min<std::size_t>(std::strlen(buf), 30);
                    std::memcpy(cell.data(), buf, n);
                    file.insert(file.end(), cell.begin(), cell.end());
                }
                ++emitted;
            }
            file.push_back(0x1A);
            file[4] = static_cast<std::uint8_t>( emitted        & 0xFFu);
            file[5] = static_cast<std::uint8_t>((emitted >>  8) & 0xFFu);
            file[6] = static_cast<std::uint8_t>((emitted >> 16) & 0xFFu);
            file[7] = static_cast<std::uint8_t>((emitted >> 24) & 0xFFu);
            {
                std::ofstream out(grp_dbf, std::ios::binary);
                if (!out) return fail(openads::AE_INTERNAL_ERROR,
                                      "group-by temp DBF: open for write failed");
                out.write(reinterpret_cast<const char*>(file.data()),
                          static_cast<std::streamsize>(file.size()));
            }
            std::string rel3 = grp_dbf.filename().string();
            auto gth = c->open_table(rel3, openads::engine::TableType::Cdx,
                                     openads::engine::OpenMode::Read);
            if (!gth) return fail(gth.error());
            openads::engine::Table* gtbl = c->lookup_table(gth.value());
            if (!gtbl) return fail(openads::AE_INTERNAL_ERROR, "post-open");
            ADSHANDLE gh = s.registry.register_object(HandleKind::Table, gtbl);
            *phCursor = gh;
            return ok();
        }

        // M10.54 — compile each slot's optional FILTER. Subset:
        // Cmp / AND / OR / NOT (full WHERE support left to follow-up).
        using AggPred = std::function<bool(openads::engine::Table&)>;
        std::vector<AggPred> slot_preds(slots.size());
        for (std::size_t i = 0; i < slots.size(); ++i) {
            if (!parsed.value().aggregates[i].filter) continue;
            std::function<openads::util::Result<AggPred>(
                const openads::sql::WhereExpr&)> cf;
            cf = [&](const openads::sql::WhereExpr& n)
                  -> openads::util::Result<AggPred> {
                using K = openads::sql::WhereExpr::Kind;
                if (n.kind == K::And || n.kind == K::Or) {
                    std::vector<AggPred> ks;
                    for (auto& cn : n.children) {
                        auto r = cf(*cn);
                        if (!r) return r.error();
                        ks.push_back(std::move(r).value());
                    }
                    bool is_and = (n.kind == K::And);
                    return AggPred{[ks = std::move(ks), is_and]
                                   (openads::engine::Table& t) {
                        if (is_and) {
                            for (auto& k : ks) if (!k(t)) return false;
                            return true;
                        }
                        for (auto& k : ks) if (k(t)) return true;
                        return false;
                    }};
                }
                if (n.kind == K::Not) {
                    auto inner = cf(*n.child);
                    if (!inner) return inner.error();
                    return AggPred{[p = std::move(inner).value()]
                                   (openads::engine::Table& t)
                                   { return !p(t); }};
                }
                if (n.kind == K::In) {
                    std::int32_t fidx = tbl->field_index(n.in_clause.column);
                    if (fidx < 0) return openads::util::Error{
                        openads::AE_COLUMN_NOT_FOUND, 0,
                        n.in_clause.column.c_str(), ""};
                    std::uint16_t fi2 = static_cast<std::uint16_t>(fidx);
                    auto set = std::make_shared<std::unordered_set<std::string>>(
                        n.in_clause.literals.begin(),
                        n.in_clause.literals.end());
                    return AggPred{[fi2, set](openads::engine::Table& t) {
                        auto v = t.read_field(fi2);
                        if (!v) return false;
                        auto sv = v.value().as_string;
                        while (!sv.empty() && sv.back() == ' ') sv.pop_back();
                        return set->count(sv) > 0;
                    }};
                }
                if (n.kind != K::Cmp) {
                    return openads::util::Error{
                        openads::AE_FUNCTION_NOT_AVAILABLE, 0,
                        "aggregate FILTER supports Cmp/AND/OR/NOT/IN only", ""};
                }
                const auto& w = n.cmp;
                std::int32_t fi = tbl->field_index(w.column);
                if (fi < 0) return openads::util::Error{
                    openads::AE_COLUMN_NOT_FOUND, 0, w.column.c_str(), ""};
                std::uint16_t f = static_cast<std::uint16_t>(fi);
                openads::sql::WhereOp op = w.op;
                std::string lit = w.literal;
                std::string lit2 = w.literal2;
                bool is_num = w.is_numeric;
                double num = w.number;
                double num2 = w.number2;
                std::shared_ptr<std::unordered_set<std::uint32_t>> contains_hits;
                if (op == openads::sql::WhereOp::Contains) {
                    namespace fs = std::filesystem;
                    fs::path fts_path =
                        fs::path(tbl->path()).replace_extension(".fts");
                    auto loaded = openads::engine::Fts::load(fts_path.string());
                    if (loaded) {
                        openads::engine::FtsOptions opts;
                        auto hits = openads::engine::Fts::search(
                            loaded.value(), lit, opts);
                        contains_hits =
                            std::make_shared<std::unordered_set<std::uint32_t>>(
                                hits.begin(), hits.end());
                    }
                }
                openads::sql::WhereFn lhs_fn = w.lhs_fn;
                return AggPred{[f, op, lit, lit2, is_num, num, num2,
                                contains_hits, lhs_fn]
                               (openads::engine::Table& t) {
                    if (op == openads::sql::WhereOp::Contains) {
                        if (!contains_hits) return false;
                        return contains_hits->find(t.recno()) !=
                               contains_hits->end();
                    }
                    auto v = t.read_field(f);
                    if (!v) return false;
                    if (op == openads::sql::WhereOp::IsNull ||
                        op == openads::sql::WhereOp::IsNotNull) {
                        bool null_ish = t.is_field_null(f);
                        if (!null_ish) {
                            auto sv = v.value().as_string;
                            while (!sv.empty() && sv.back() == ' ') sv.pop_back();
                            null_ish = sv.empty();
                        }
                        return op == openads::sql::WhereOp::IsNull
                            ? null_ish : !null_ish;
                    }
                    if (op == openads::sql::WhereOp::Between) {
                        if (is_num) {
                            double d = v.value().as_double;
                            return d >= num && d <= num2;
                        }
                        auto sv = apply_where_fn(v.value().as_string, lhs_fn);
                        return sv.compare(lit) >= 0 && sv.compare(lit2) <= 0;
                    }
                    if (op == openads::sql::WhereOp::Like) {
                        auto sv = apply_where_fn(v.value().as_string, lhs_fn);
                        while (!sv.empty() && sv.back() == ' ') sv.pop_back();
                        return sql_like_match(sv, lit);
                    }
                    int cmp = 0;
                    if (is_num) {
                        double d = v.value().as_double;
                        if      (d < num) cmp = -1;
                        else if (d > num) cmp =  1;
                    } else {
                        cmp = apply_where_fn(v.value().as_string, lhs_fn)
                                  .compare(lit);
                    }
                    switch (op) {
                        case openads::sql::WhereOp::Eq: return cmp == 0;
                        case openads::sql::WhereOp::Ne: return cmp != 0;
                        case openads::sql::WhereOp::Lt: return cmp <  0;
                        case openads::sql::WhereOp::Gt: return cmp >  0;
                        case openads::sql::WhereOp::Le: return cmp <= 0;
                        case openads::sql::WhereOp::Ge: return cmp >= 0;
                    default: return false;
                    }
                }};
            };
            auto p = cf(*parsed.value().aggregates[i].filter);
            if (!p) return fail(p.error());
            slot_preds[i] = std::move(p).value();
        }

        // Walk matching rows, accumulate per slot.
        std::vector<double> sum(slots.size(), 0.0);
        std::vector<double> minv(slots.size(),
            std::numeric_limits<double>::infinity());
        std::vector<double> maxv(slots.size(),
            -std::numeric_limits<double>::infinity());
        std::vector<std::uint64_t> count(slots.size(), 0);
        std::uint64_t row_count = 0;
        std::uint32_t rcount = tbl->record_count();
        for (std::uint32_t r = 1; r <= rcount; ++r) {
            if (auto g = tbl->goto_record(r); !g) continue;
            if (tbl->is_deleted()) continue;
            if (filter && !filter(*tbl)) continue;
            ++row_count;
            for (std::size_t i = 0; i < slots.size(); ++i) {
                if (slot_preds[i] && !slot_preds[i](*tbl)) continue;
                if (slots[i].def.kind == openads::sql::AggregateKind::CountStar) {
                    ++count[i];
                    continue;
                }
                auto v = tbl->read_field(
                    static_cast<std::uint16_t>(slots[i].field_index));
                if (!v) continue;
                double d = v.value().as_double;
                ++count[i];
                sum[i] += d;
                if (d < minv[i]) minv[i] = d;
                if (d > maxv[i]) maxv[i] = d;
            }
        }
        if (table_handle != 0) c->close_table(table_handle);

        // Write a 1-row temp DBF with one C(30) field per aggregate
        // and the formatted result in each slot.
        namespace fs = std::filesystem;
        fs::path tmp_dbf = fs::path(c->data_dir());
        char namebuf[64];
        std::snprintf(namebuf, sizeof(namebuf), "_agg_%llx.dbf",
                      static_cast<unsigned long long>(
                          openads::platform::monotonic_nanos()));
        tmp_dbf /= namebuf;
        std::vector<std::uint8_t> file;
        std::array<std::uint8_t, 32> hdr{};
        hdr[0] = 0x03;
        stamp_dbf_header_today(hdr.data());
        hdr[4] = 1;
        std::uint16_t header_len = static_cast<std::uint16_t>(
            32 + 32 * slots.size() + 1);
        std::uint16_t rec_len = static_cast<std::uint16_t>(
            1 + 30 * slots.size());
        hdr[8]  = static_cast<std::uint8_t>( header_len       & 0xFFu);
        hdr[9]  = static_cast<std::uint8_t>((header_len >> 8) & 0xFFu);
        hdr[10] = static_cast<std::uint8_t>( rec_len          & 0xFFu);
        hdr[11] = static_cast<std::uint8_t>((rec_len    >> 8) & 0xFFu);
        file.insert(file.end(), hdr.begin(), hdr.end());
        for (std::size_t i = 0; i < slots.size(); ++i) {
            std::array<std::uint8_t, 32> fd{};
            char fn[16];
            if (!slots[i].def.alias.empty()) {
                std::snprintf(fn, sizeof(fn), "%s", slots[i].def.alias.c_str());
            } else {
                std::snprintf(fn, sizeof(fn), "COL%u",
                              static_cast<unsigned>((i + 1) & 0xFFFFFu));
            }
            std::size_t fn_len = std::strlen(fn);
            std::memcpy(fd.data(), fn, fn_len > 11 ? 11 : fn_len);
            fd[11] = 'C'; fd[16] = 30;
            file.insert(file.end(), fd.begin(), fd.end());
        }
        file.push_back(0x0D);
        file.push_back(' ');
        for (std::size_t i = 0; i < slots.size(); ++i) {
            char buf[32] = {0};
            switch (slots[i].def.kind) {
                case openads::sql::AggregateKind::CountStar:
                case openads::sql::AggregateKind::Count:
                    // M10.54 — when this slot has a FILTER, count[i]
                    // already excludes filter-failing rows; use it
                    // even for CountStar.
                    std::snprintf(buf, sizeof(buf), "%llu",
                        static_cast<unsigned long long>(
                            slots[i].def.kind ==
                                openads::sql::AggregateKind::CountStar
                                ? (slot_preds[i] ? count[i] : row_count)
                                : count[i]));
                    break;
                case openads::sql::AggregateKind::Sum:
                    std::snprintf(buf, sizeof(buf), "%.6f", sum[i]);
                    break;
                case openads::sql::AggregateKind::Avg:
                    std::snprintf(buf, sizeof(buf), "%.6f",
                        count[i] ? sum[i] / static_cast<double>(count[i])
                                 : 0.0);
                    break;
                case openads::sql::AggregateKind::Min:
                    if (count[i] == 0) std::memcpy(buf, "0", 2);
                    else std::snprintf(buf, sizeof(buf), "%.6f", minv[i]);
                    break;
                case openads::sql::AggregateKind::Max:
                    if (count[i] == 0) std::memcpy(buf, "0", 2);
                    else std::snprintf(buf, sizeof(buf), "%.6f", maxv[i]);
                    break;
            }
            std::array<std::uint8_t, 30> cell{};
            std::memset(cell.data(), ' ', cell.size());
            std::size_t n = std::min<std::size_t>(std::strlen(buf), 30);
            std::memcpy(cell.data(), buf, n);
            file.insert(file.end(), cell.begin(), cell.end());
        }
        file.push_back(0x1A);
        {
            std::ofstream out(tmp_dbf, std::ios::binary);
            if (!out) return fail(openads::AE_INTERNAL_ERROR,
                                  "aggregate temp DBF: open for write failed");
            out.write(reinterpret_cast<const char*>(file.data()),
                      static_cast<std::streamsize>(file.size()));
            // Explicit close before re-opening for read.
        }

        // Open the temp DBF as the cursor.
        std::string rel = tmp_dbf.filename().string();
        auto cth = c->open_table(rel, openads::engine::TableType::Cdx,
                                 openads::engine::OpenMode::Read);
        if (!cth) return fail(cth.error());
        openads::engine::Table* ctbl = c->lookup_table(cth.value());
        if (!ctbl) return fail(openads::AE_INTERNAL_ERROR, "post-open");
        ADSHANDLE gh = s.registry.register_object(HandleKind::Table, ctbl);
        *phCursor = gh;
        return ok();
    }

    // Compile the WHERE expression tree into a row-predicate closure
    // (M10.3). CONTAINS captures a precomputed recno set at compile
    // time; AND / OR / NOT short-circuit during evaluation.
    if (parsed.value().where) {
        // Compiled term per Cmp leaf.
        struct CmpTerm {
            std::uint16_t                       field_index = 0;
            openads::sql::WhereOp               op = openads::sql::WhereOp::Eq;
            std::string                         literal;
            bool                                is_numeric = false;
            double                              number = 0.0;
            std::shared_ptr<std::unordered_set<std::uint32_t>> contains_hits;
            // M10.33 — BETWEEN upper bound.
            std::string                         literal2;
            double                              number2 = 0.0;
            // M11.7 — case-insensitive ASCII compare when set.
            bool                                nocase = false;
            // ADS dialect — UPPER()/LOWER() wrapping the LHS column.
            openads::sql::WhereFn               lhs_fn =
                openads::sql::WhereFn::None;
        };
        bool conn_nocase =
            (c->collation() == Connection::Collation::NoCase);
        auto to_lower_ascii = [](std::string sl) {
            for (auto& ch : sl) {
                if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch + 32);
            }
            return sl;
        };

        // Compile the AST into a Predicate functor.
        using Pred = std::function<bool(openads::engine::Table&)>;
        std::function<openads::util::Result<Pred>(
            const openads::sql::WhereExpr&)> compile;
        compile = [&](const openads::sql::WhereExpr& node)
                  -> openads::util::Result<Pred> {
            using Kind = openads::sql::WhereExpr::Kind;
            if (node.kind == Kind::And) {
                std::vector<Pred> kids;
                for (auto& cn : node.children) {
                    auto r = compile(*cn);
                    if (!r) return r.error();
                    kids.push_back(std::move(r).value());
                }
                return Pred{[kids = std::move(kids)](openads::engine::Table& t) {
                    for (auto& k : kids) if (!k(t)) return false;
                    return true;
                }};
            }
            if (node.kind == Kind::Or) {
                std::vector<Pred> kids;
                for (auto& cn : node.children) {
                    auto r = compile(*cn);
                    if (!r) return r.error();
                    kids.push_back(std::move(r).value());
                }
                return Pred{[kids = std::move(kids)](openads::engine::Table& t) {
                    for (auto& k : kids) if (k(t)) return true;
                    return false;
                }};
            }
            if (node.kind == Kind::Not) {
                auto inner = compile(*node.child);
                if (!inner) return inner.error();
                return Pred{[p = std::move(inner).value()]
                            (openads::engine::Table& t) { return !p(t); }};
            }
            if (node.kind == Kind::Exists) {
                // M10.17 / M10.24 — EXISTS / NOT EXISTS. Honors
                // subquery's WHERE (M10.24); when that WHERE has
                // outer-column references (e.g.
                // `EXISTS (SELECT * FROM b WHERE b.x = a.y)`), the
                // predicate re-evaluates per outer row, binding outer
                // values from the live `tbl` cursor.
                if (!node.exists_subquery) {
                    return openads::util::Error{
                        openads::AE_PARSE_ERROR, 0,
                        "EXISTS subquery missing", ""};
                }
                // Move ownership of the subquery into a shared_ptr so
                // the captured WhereExpr* outlives the parsed
                // SelectStmt (which is local to AdsExecuteSQLDirect).
                auto sub = std::shared_ptr<openads::sql::SelectStmt>(
                    const_cast<openads::sql::WhereExpr&>(node)
                        .exists_subquery.release());
                openads::engine::Table* outer_tbl = tbl;
                std::string sub_table = sub->table;
                return Pred{[c, sub, outer_tbl, sub_table]
                            (openads::engine::Table&) -> bool {
                    auto sh = c->open_table(sub_table,
                                            openads::engine::TableType::Cdx,
                                            openads::engine::OpenMode::Read);
                    if (!sh) return false;
                    openads::engine::Table* stbl = c->lookup_table(sh.value());
                    if (!stbl) { c->close_table(sh.value()); return false; }
                    auto trim = [](std::string sl) {
                        while (!sl.empty() && sl.back() == ' ') sl.pop_back();
                        return sl;
                    };
                    std::function<bool(const openads::sql::WhereExpr&)> evalw;
                    evalw = [&](const openads::sql::WhereExpr& n) -> bool {
                        using K = openads::sql::WhereExpr::Kind;
                        if (n.kind == K::And) {
                            for (auto& cn : n.children)
                                if (!evalw(*cn)) return false;
                            return true;
                        }
                        if (n.kind == K::Or) {
                            for (auto& cn : n.children)
                                if (evalw(*cn)) return true;
                            return false;
                        }
                        if (n.kind == K::Not) return !evalw(*n.child);
                        if (n.kind != K::Cmp) return false;
                        const auto& w = n.cmp;
                        std::int32_t fi = stbl->field_index(w.column);
                        if (fi < 0) return false;
                        auto v = stbl->read_field(
                            static_cast<std::uint16_t>(fi));
                        if (!v) return false;
                        int cmp = 0;
                        if (w.is_outer_ref) {
                            std::int32_t ofi =
                                outer_tbl->field_index(w.outer_column);
                            if (ofi < 0) return false;
                            auto ov = outer_tbl->read_field(
                                static_cast<std::uint16_t>(ofi));
                            if (!ov) return false;
                            cmp = trim(v.value().as_string)
                                      .compare(trim(ov.value().as_string));
                        } else if (w.is_numeric) {
                            double d = v.value().as_double;
                            if      (d < w.number) cmp = -1;
                            else if (d > w.number) cmp =  1;
                        } else {
                            cmp = trim(v.value().as_string).compare(w.literal);
                        }
                        switch (w.op) {
                            case openads::sql::WhereOp::Eq: return cmp == 0;
                            case openads::sql::WhereOp::Ne: return cmp != 0;
                            case openads::sql::WhereOp::Lt: return cmp <  0;
                            case openads::sql::WhereOp::Gt: return cmp >  0;
                            case openads::sql::WhereOp::Le: return cmp <= 0;
                            case openads::sql::WhereOp::Ge: return cmp >= 0;
                            default: return false;
                        }
                    };
                    bool any = false;
                    std::uint32_t srcount = stbl->record_count();
                    for (std::uint32_t r = 1; r <= srcount; ++r) {
                        if (auto g = stbl->goto_record(r); !g) continue;
                        if (stbl->is_deleted()) continue;
                        if (!sub->where) { any = true; break; }
                        if (evalw(*sub->where)) { any = true; break; }
                    }
                    c->close_table(sh.value());
                    return any;
                }};
            }
            if (node.kind == Kind::In) {
                // M10.15: materialise the IN set at compile time. For
                // a literal list, just lift the strings in. For a
                // subquery, walk its source table inline (no nested
                // ABI dispatch — keeps the lock_guard intact).
                std::int32_t fidx = tbl->field_index(node.in_clause.column);
                if (fidx < 0) {
                    return openads::util::Error{
                        openads::AE_COLUMN_NOT_FOUND, 0,
                        node.in_clause.column.c_str(), ""};
                }
                std::uint16_t fi = static_cast<std::uint16_t>(fidx);
                auto trim_trailing = [](std::string sl) {
                    while (!sl.empty() && sl.back() == ' ') sl.pop_back();
                    return sl;
                };
                auto set = std::make_shared<std::unordered_set<std::string>>();
                for (auto& lit : node.in_clause.literals) set->insert(lit);
                if (node.in_clause.subquery) {
                    // M10.35 — detect correlation in subquery's WHERE.
                    bool correlated = false;
                    if (node.in_clause.subquery->where) {
                        std::function<void(const openads::sql::WhereExpr&)>
                            scan;
                        scan = [&](const openads::sql::WhereExpr& n) {
                            using K = openads::sql::WhereExpr::Kind;
                            if (correlated) return;
                            if (n.kind == K::And || n.kind == K::Or) {
                                for (auto& cn : n.children) scan(*cn);
                                return;
                            }
                            if (n.kind == K::Not) { scan(*n.child); return; }
                            if (n.kind == K::Cmp && n.cmp.is_outer_ref)
                                correlated = true;
                        };
                        scan(*node.in_clause.subquery->where);
                    }
                    if (correlated) {
                        auto sub = std::shared_ptr<openads::sql::SelectStmt>(
                            const_cast<openads::sql::InClause&>(
                                node.in_clause).subquery.release());
                        openads::engine::Table* outer_tbl = tbl;
                        std::string sub_table = sub->table;
                        return Pred{[c, sub, outer_tbl, fi, sub_table,
                                     trim_trailing]
                                    (openads::engine::Table&) -> bool {
                            auto sh = c->open_table(
                                sub_table,
                                openads::engine::TableType::Cdx,
                                openads::engine::OpenMode::Read);
                            if (!sh) return false;
                            openads::engine::Table* stbl =
                                c->lookup_table(sh.value());
                            if (!stbl) {
                                c->close_table(sh.value()); return false;
                            }
                            if (sub->projection.size() != 1 ||
                                !sub->aggregates.empty()) {
                                c->close_table(sh.value()); return false;
                            }
                            std::int32_t scol =
                                stbl->field_index(sub->projection[0]);
                            if (scol < 0) {
                                c->close_table(sh.value()); return false;
                            }
                            auto trim = trim_trailing;
                            std::function<bool(const openads::sql::WhereExpr&)>
                                evalw;
                            evalw = [&](const openads::sql::WhereExpr& n)
                                    -> bool {
                                using K = openads::sql::WhereExpr::Kind;
                                if (n.kind == K::And) {
                                    for (auto& cn : n.children)
                                        if (!evalw(*cn)) return false;
                                    return true;
                                }
                                if (n.kind == K::Or) {
                                    for (auto& cn : n.children)
                                        if (evalw(*cn)) return true;
                                    return false;
                                }
                                if (n.kind == K::Not) return !evalw(*n.child);
                                if (n.kind != K::Cmp) return false;
                                const auto& wn = n.cmp;
                                std::int32_t sfi =
                                    stbl->field_index(wn.column);
                                if (sfi < 0) return false;
                                auto v = stbl->read_field(
                                    static_cast<std::uint16_t>(sfi));
                                if (!v) return false;
                                int cmp = 0;
                                if (wn.is_outer_ref) {
                                    std::int32_t ofi =
                                        outer_tbl->field_index(wn.outer_column);
                                    if (ofi < 0) return false;
                                    auto ov = outer_tbl->read_field(
                                        static_cast<std::uint16_t>(ofi));
                                    if (!ov) return false;
                                    cmp = trim(v.value().as_string)
                                              .compare(trim(ov.value().as_string));
                                } else if (wn.is_numeric) {
                                    double d = v.value().as_double;
                                    if      (d < wn.number) cmp = -1;
                                    else if (d > wn.number) cmp =  1;
                                } else {
                                    cmp = trim(v.value().as_string)
                                              .compare(wn.literal);
                                }
                                switch (wn.op) {
                                    case openads::sql::WhereOp::Eq: return cmp == 0;
                                    case openads::sql::WhereOp::Ne: return cmp != 0;
                                    case openads::sql::WhereOp::Lt: return cmp <  0;
                                    case openads::sql::WhereOp::Gt: return cmp >  0;
                                    case openads::sql::WhereOp::Le: return cmp <= 0;
                                    case openads::sql::WhereOp::Ge: return cmp >= 0;
                                    default: return false;
                                }
                            };
                            auto ov = outer_tbl->read_field(fi);
                            if (!ov) {
                                c->close_table(sh.value()); return false;
                            }
                            std::string outer_v =
                                trim(ov.value().as_string);
                            bool any = false;
                            std::uint32_t srcount = stbl->record_count();
                            for (std::uint32_t r = 1; r <= srcount; ++r) {
                                if (auto g = stbl->goto_record(r); !g)
                                    continue;
                                if (stbl->is_deleted()) continue;
                                if (sub->where && !evalw(*sub->where))
                                    continue;
                                auto sv = stbl->read_field(
                                    static_cast<std::uint16_t>(scol));
                                if (!sv) continue;
                                if (trim(sv.value().as_string) == outer_v) {
                                    any = true; break;
                                }
                            }
                            c->close_table(sh.value());
                            return any;
                        }};
                    }
                    const auto& sq = *node.in_clause.subquery;
                    auto sh = c->open_table(sq.table,
                                            openads::engine::TableType::Cdx,
                                            openads::engine::OpenMode::Read);
                    if (!sh) return sh.error();
                    openads::engine::Table* stbl = c->lookup_table(sh.value());
                    if (stbl == nullptr) {
                        return openads::util::Error{
                            openads::AE_INTERNAL_ERROR, 0,
                            "subquery post-open", ""};
                    }
                    if (sq.projection.empty() && sq.aggregates.empty()) {
                        return openads::util::Error{
                            openads::AE_PARSE_ERROR, 0,
                            "IN subquery must project a single column", ""};
                    }
                    if (!sq.aggregates.empty() ||
                        sq.projection.size() != 1) {
                        c->close_table(sh.value());
                        return openads::util::Error{
                            openads::AE_PARSE_ERROR, 0,
                            "IN subquery must project exactly one column", ""};
                    }
                    std::int32_t scol = stbl->field_index(sq.projection[0]);
                    if (scol < 0) {
                        c->close_table(sh.value());
                        return openads::util::Error{
                            openads::AE_COLUMN_NOT_FOUND, 0,
                            sq.projection[0].c_str(), ""};
                    }
                    std::uint32_t srcount = stbl->record_count();
                    for (std::uint32_t r = 1; r <= srcount; ++r) {
                        if (auto g = stbl->goto_record(r); !g) continue;
                        if (stbl->is_deleted()) continue;
                        auto v = stbl->read_field(
                            static_cast<std::uint16_t>(scol));
                        if (!v) continue;
                        set->insert(trim_trailing(v.value().as_string));
                    }
                    c->close_table(sh.value());
                }
                return Pred{[fi, set, trim_trailing]
                            (openads::engine::Table& t) {
                    auto v = t.read_field(fi);
                    if (!v) return false;
                    return set->find(trim_trailing(v.value().as_string)) !=
                           set->end();
                }};
            }
            // Cmp leaf.
            const auto& w = node.cmp;
            std::int32_t fidx = tbl->field_index(w.column);
            if (fidx < 0) {
                return openads::util::Error{
                    openads::AE_COLUMN_NOT_FOUND, 0,
                    w.column.c_str(), ""};
            }
            CmpTerm term;
            term.field_index = static_cast<std::uint16_t>(fidx);
            term.op          = w.op;
            term.literal     = w.literal;
            term.is_numeric  = w.is_numeric;
            term.number      = w.number;
            term.literal2    = w.literal2;
            term.number2     = w.number2;
            term.lhs_fn      = w.lhs_fn;
            // M11.7 — stamp collation onto the term when the
            // connection is in nocase mode and the cmp involves
            // string operands.
            if (conn_nocase && !w.is_numeric) {
                term.nocase   = true;
                term.literal  = to_lower_ascii(term.literal);
                term.literal2 = to_lower_ascii(term.literal2);
            }
            if (w.subquery) {
                // M10.29 — correlated scalar subquery. If the
                // subquery's WHERE references an outer column, we
                // re-evaluate the subquery per outer row instead of
                // materialising a single value at compile time.
                bool correlated = false;
                if (w.subquery->where) {
                    std::function<void(const openads::sql::WhereExpr&)> scan;
                    scan = [&](const openads::sql::WhereExpr& n) {
                        using K = openads::sql::WhereExpr::Kind;
                        if (correlated) return;
                        if (n.kind == K::And || n.kind == K::Or) {
                            for (auto& cn : n.children) scan(*cn);
                            return;
                        }
                        if (n.kind == K::Not) { scan(*n.child); return; }
                        if (n.kind == K::Cmp && n.cmp.is_outer_ref)
                            correlated = true;
                    };
                    scan(*w.subquery->where);
                }
                if (correlated) {
                    auto sub = std::shared_ptr<openads::sql::SelectStmt>(
                        const_cast<openads::sql::WhereCmp&>(w)
                            .subquery.release());
                    openads::engine::Table* outer_tbl = tbl;
                    std::uint16_t outer_field =
                        static_cast<std::uint16_t>(fidx);
                    bool outer_is_numeric_local =
                        tbl->field_descriptor(outer_field).type !=
                            openads::drivers::DbfFieldType::Character;
                    openads::sql::WhereOp op_local = w.op;
                    std::string sub_table = sub->table;
                    return Pred{[c, sub, outer_tbl, outer_field,
                                 outer_is_numeric_local, op_local, sub_table]
                                (openads::engine::Table&) -> bool {
                        auto sh = c->open_table(
                            sub_table,
                            openads::engine::TableType::Cdx,
                            openads::engine::OpenMode::Read);
                        if (!sh) return false;
                        openads::engine::Table* stbl =
                            c->lookup_table(sh.value());
                        if (!stbl) {
                            c->close_table(sh.value()); return false;
                        }
                        auto trim = [](std::string sl) {
                            while (!sl.empty() && sl.back() == ' ')
                                sl.pop_back();
                            return sl;
                        };
                        std::function<bool(const openads::sql::WhereExpr&)>
                            evalw;
                        evalw = [&](const openads::sql::WhereExpr& n) -> bool {
                            using K = openads::sql::WhereExpr::Kind;
                            if (n.kind == K::And) {
                                for (auto& cn : n.children)
                                    if (!evalw(*cn)) return false;
                                return true;
                            }
                            if (n.kind == K::Or) {
                                for (auto& cn : n.children)
                                    if (evalw(*cn)) return true;
                                return false;
                            }
                            if (n.kind == K::Not) return !evalw(*n.child);
                            if (n.kind != K::Cmp) return false;
                            const auto& wn = n.cmp;
                            std::int32_t fi = stbl->field_index(wn.column);
                            if (fi < 0) return false;
                            auto v = stbl->read_field(
                                static_cast<std::uint16_t>(fi));
                            if (!v) return false;
                            int cmp = 0;
                            if (wn.is_outer_ref) {
                                std::int32_t ofi =
                                    outer_tbl->field_index(wn.outer_column);
                                if (ofi < 0) return false;
                                auto ov = outer_tbl->read_field(
                                    static_cast<std::uint16_t>(ofi));
                                if (!ov) return false;
                                cmp = trim(v.value().as_string)
                                          .compare(trim(ov.value().as_string));
                            } else if (wn.is_numeric) {
                                double d = v.value().as_double;
                                if      (d < wn.number) cmp = -1;
                                else if (d > wn.number) cmp =  1;
                            } else {
                                cmp = trim(v.value().as_string)
                                          .compare(wn.literal);
                            }
                            switch (wn.op) {
                                case openads::sql::WhereOp::Eq: return cmp == 0;
                                case openads::sql::WhereOp::Ne: return cmp != 0;
                                case openads::sql::WhereOp::Lt: return cmp <  0;
                                case openads::sql::WhereOp::Gt: return cmp >  0;
                                case openads::sql::WhereOp::Le: return cmp <= 0;
                                case openads::sql::WhereOp::Ge: return cmp >= 0;
                                default: return false;
                            }
                        };
                        double scalar_num = 0.0;
                        std::string scalar_str;
                        bool found = false;
                        std::uint32_t srcount = stbl->record_count();
                        if (sub->aggregates.size() == 1 &&
                            sub->projection.empty()) {
                            const auto& a = sub->aggregates[0];
                            std::int32_t scol = -1;
                            if (a.kind !=
                                openads::sql::AggregateKind::CountStar) {
                                scol = stbl->field_index(a.column);
                                if (scol < 0) {
                                    c->close_table(sh.value()); return false;
                                }
                            }
                            std::uint64_t cnt = 0;
                            double sum  = 0.0;
                            double minv =  std::numeric_limits<double>::infinity();
                            double maxv = -std::numeric_limits<double>::infinity();
                            for (std::uint32_t r = 1; r <= srcount; ++r) {
                                if (auto g = stbl->goto_record(r); !g) continue;
                                if (stbl->is_deleted()) continue;
                                if (sub->where && !evalw(*sub->where)) continue;
                                if (a.kind ==
                                    openads::sql::AggregateKind::CountStar) {
                                    ++cnt; continue;
                                }
                                auto v = stbl->read_field(
                                    static_cast<std::uint16_t>(scol));
                                if (!v) continue;
                                ++cnt;
                                double d = v.value().as_double;
                                sum += d;
                                if (d < minv) minv = d;
                                if (d > maxv) maxv = d;
                            }
                            switch (a.kind) {
                                case openads::sql::AggregateKind::CountStar:
                                case openads::sql::AggregateKind::Count:
                                    scalar_num = static_cast<double>(cnt); break;
                                case openads::sql::AggregateKind::Sum:
                                    scalar_num = sum; break;
                                case openads::sql::AggregateKind::Avg:
                                    scalar_num = cnt
                                        ? sum / static_cast<double>(cnt)
                                        : 0.0; break;
                                case openads::sql::AggregateKind::Min:
                                    scalar_num = cnt ? minv : 0.0; break;
                                case openads::sql::AggregateKind::Max:
                                    scalar_num = cnt ? maxv : 0.0; break;
                            }
                            found = true;
                            char tmp[64];
                            std::snprintf(tmp, sizeof(tmp),
                                          "%.17g", scalar_num);
                            scalar_str = tmp;
                        } else if (sub->projection.size() == 1 &&
                                   sub->aggregates.empty()) {
                            std::int32_t scol =
                                stbl->field_index(sub->projection[0]);
                            if (scol < 0) {
                                c->close_table(sh.value()); return false;
                            }
                            for (std::uint32_t r = 1; r <= srcount; ++r) {
                                if (auto g = stbl->goto_record(r); !g) continue;
                                if (stbl->is_deleted()) continue;
                                if (sub->where && !evalw(*sub->where)) continue;
                                auto v = stbl->read_field(
                                    static_cast<std::uint16_t>(scol));
                                if (!v) continue;
                                scalar_str = v.value().as_string;
                                scalar_num = v.value().as_double;
                                while (!scalar_str.empty() &&
                                       scalar_str.back() == ' ')
                                    scalar_str.pop_back();
                                found = true;
                                break;
                            }
                        } else {
                            c->close_table(sh.value());
                            return false;
                        }
                        c->close_table(sh.value());
                        if (!found) return false;
                        auto ov = outer_tbl->read_field(outer_field);
                        if (!ov) return false;
                        int cmp = 0;
                        if (outer_is_numeric_local) {
                            double d = ov.value().as_double;
                            if      (d < scalar_num) cmp = -1;
                            else if (d > scalar_num) cmp =  1;
                        } else {
                            std::string os = ov.value().as_string;
                            while (!os.empty() && os.back() == ' ')
                                os.pop_back();
                            cmp = os.compare(scalar_str);
                        }
                        switch (op_local) {
                            case openads::sql::WhereOp::Eq: return cmp == 0;
                            case openads::sql::WhereOp::Ne: return cmp != 0;
                            case openads::sql::WhereOp::Lt: return cmp <  0;
                            case openads::sql::WhereOp::Gt: return cmp >  0;
                            case openads::sql::WhereOp::Le: return cmp <= 0;
                            case openads::sql::WhereOp::Ge: return cmp >= 0;
                            default: return false;
                        }
                    }};
                }
                // M10.18: scalar subquery — materialise once at
                // compile time. Open the subquery's table, walk for
                // the first non-deleted record, read the projection's
                // first column, and use that as the cmp literal.
                const auto& sq = *w.subquery;
                auto sh = c->open_table(sq.table,
                                        openads::engine::TableType::Cdx,
                                        openads::engine::OpenMode::Read);
                if (!sh) return sh.error();
                openads::engine::Table* stbl = c->lookup_table(sh.value());
                if (stbl == nullptr) {
                    return openads::util::Error{
                        openads::AE_INTERNAL_ERROR, 0,
                        "scalar subquery post-open", ""};
                }
                bool outer_is_numeric =
                    tbl->field_descriptor(static_cast<std::uint16_t>(fidx))
                        .type != openads::drivers::DbfFieldType::Character;

                // M10.19 — aggregate scalar subquery
                // (`= (SELECT MAX(x) FROM t)`). Single aggregate slot
                // computes against the inner table; numeric result
                // lands directly in the cmp's number/literal.
                if (sq.aggregates.size() == 1 && sq.projection.empty()) {
                    const auto& a = sq.aggregates[0];
                    std::int32_t scol = -1;
                    if (a.kind != openads::sql::AggregateKind::CountStar) {
                        scol = stbl->field_index(a.column);
                        if (scol < 0) {
                            c->close_table(sh.value());
                            return openads::util::Error{
                                openads::AE_COLUMN_NOT_FOUND, 0,
                                a.column.c_str(), ""};
                        }
                    }
                    std::uint64_t cnt = 0;
                    double sum = 0.0;
                    double minv =  std::numeric_limits<double>::infinity();
                    double maxv = -std::numeric_limits<double>::infinity();
                    std::uint32_t srcount = stbl->record_count();
                    for (std::uint32_t r = 1; r <= srcount; ++r) {
                        if (auto g = stbl->goto_record(r); !g) continue;
                        if (stbl->is_deleted()) continue;
                        if (a.kind == openads::sql::AggregateKind::CountStar) {
                            ++cnt;
                            continue;
                        }
                        auto v = stbl->read_field(
                            static_cast<std::uint16_t>(scol));
                        if (!v) continue;
                        ++cnt;
                        double d = v.value().as_double;
                        sum += d;
                        if (d < minv) minv = d;
                        if (d > maxv) maxv = d;
                    }
                    c->close_table(sh.value());
                    double result = 0.0;
                    switch (a.kind) {
                        case openads::sql::AggregateKind::CountStar:
                        case openads::sql::AggregateKind::Count:
                            result = static_cast<double>(cnt);
                            break;
                        case openads::sql::AggregateKind::Sum: result = sum; break;
                        case openads::sql::AggregateKind::Avg:
                            result = cnt ? sum / static_cast<double>(cnt) : 0.0;
                            break;
                        case openads::sql::AggregateKind::Min:
                            result = cnt ? minv : 0.0; break;
                        case openads::sql::AggregateKind::Max:
                            result = cnt ? maxv : 0.0; break;
                    }
                    term.is_numeric = outer_is_numeric;
                    term.number     = result;
                    char tmp[64];
                    std::snprintf(tmp, sizeof(tmp), "%.17g", result);
                    term.literal = tmp;
                    if (!outer_is_numeric) {
                        // String comparison: drop trailing zeros so
                        // "42.000" compares clean against the column.
                        std::snprintf(tmp, sizeof(tmp), "%g", result);
                        term.literal = tmp;
                    }
                } else if (!sq.projection.empty() && sq.aggregates.empty()) {
                    if (sq.projection.size() != 1) {
                        c->close_table(sh.value());
                        return openads::util::Error{
                            openads::AE_PARSE_ERROR, 0,
                            "scalar subquery must project a single column", ""};
                    }
                    std::int32_t scol = stbl->field_index(sq.projection[0]);
                    if (scol < 0) {
                        c->close_table(sh.value());
                        return openads::util::Error{
                            openads::AE_COLUMN_NOT_FOUND, 0,
                            sq.projection[0].c_str(), ""};
                    }
                    bool found = false;
                    std::uint32_t srcount = stbl->record_count();
                    for (std::uint32_t r = 1; r <= srcount; ++r) {
                        if (auto g = stbl->goto_record(r); !g) continue;
                        if (stbl->is_deleted()) continue;
                        auto v = stbl->read_field(
                            static_cast<std::uint16_t>(scol));
                        if (!v) continue;
                        term.literal    = v.value().as_string;
                        term.is_numeric = outer_is_numeric;
                        term.number     = v.value().as_double;
                        while (!term.literal.empty() &&
                               term.literal.back() == ' ') {
                            term.literal.pop_back();
                        }
                        found = true;
                        break;
                    }
                    c->close_table(sh.value());
                    if (!found) {
                        return Pred{[](openads::engine::Table&) { return false; }};
                    }
                } else {
                    c->close_table(sh.value());
                    return openads::util::Error{
                        openads::AE_PARSE_ERROR, 0,
                        "scalar subquery must project exactly one "
                        "column or one aggregate", ""};
                }
            }
            if (w.op == openads::sql::WhereOp::Contains) {
                namespace fs = std::filesystem;
                fs::path fts_path =
                    fs::path(tbl->path()).replace_extension(".fts");
                auto loaded = openads::engine::Fts::load(fts_path.string());
                if (!loaded) return loaded.error();
                openads::engine::FtsOptions opts;
                auto hits = openads::engine::Fts::search(
                    loaded.value(), w.literal, opts);
                term.contains_hits =
                    std::make_shared<std::unordered_set<std::uint32_t>>(
                        hits.begin(), hits.end());
            }
            return Pred{[term](openads::engine::Table& t) {
                if (term.op == openads::sql::WhereOp::Contains) {
                    if (!term.contains_hits) return false;
                    return term.contains_hits->find(t.recno()) !=
                           term.contains_hits->end();
                }
                auto v = t.read_field(term.field_index);
                if (!v) return false;
                auto maybe_lower = [&](std::string sl) {
                    // ADS UPPER()/LOWER() folds the cell first; literal is
                    // verbatim. nocase then lowercases both sides.
                    sl = apply_where_fn(std::move(sl), term.lhs_fn);
                    if (!term.nocase) return sl;
                    for (auto& ch : sl) {
                        if (ch >= 'A' && ch <= 'Z')
                            ch = static_cast<char>(ch + 32);
                    }
                    return sl;
                };
                if (term.op == openads::sql::WhereOp::Between) {
                    if (term.is_numeric) {
                        double d = v.value().as_double;
                        return d >= term.number && d <= term.number2;
                    }
                    auto sv = maybe_lower(v.value().as_string);
                    return sv.compare(term.literal)  >= 0 &&
                           sv.compare(term.literal2) <= 0;
                }
                if (term.op == openads::sql::WhereOp::Like) {
                    auto sv = maybe_lower(v.value().as_string);
                    while (!sv.empty() && sv.back() == ' ') sv.pop_back();
                    return sql_like_match(sv, term.literal);
                }
                if (term.op == openads::sql::WhereOp::IsNull ||
                    term.op == openads::sql::WhereOp::IsNotNull) {
                    // M10.44 / M11.6 — prefer the VFP NULL bitmap
                    // when the field is nullable; otherwise treat
                    // an all-blanks character cell as NULL.
                    bool null_ish = t.is_field_null(term.field_index);
                    if (!null_ish) {
                        auto sv = v.value().as_string;
                        while (!sv.empty() && sv.back() == ' ') sv.pop_back();
                        null_ish = sv.empty();
                    }
                    return term.op == openads::sql::WhereOp::IsNull
                        ? null_ish : !null_ish;
                }
                int cmp = 0;
                if (term.is_numeric) {
                    double d = v.value().as_double;
                    if      (d < term.number) cmp = -1;
                    else if (d > term.number) cmp =  1;
                } else {
                    cmp = maybe_lower(v.value().as_string).compare(term.literal);
                }
                switch (term.op) {
                    case openads::sql::WhereOp::Eq: return cmp == 0;
                    case openads::sql::WhereOp::Ne: return cmp != 0;
                    case openads::sql::WhereOp::Lt: return cmp <  0;
                    case openads::sql::WhereOp::Gt: return cmp >  0;
                    case openads::sql::WhereOp::Le: return cmp <= 0;
                    case openads::sql::WhereOp::Ge: return cmp >= 0;
                    case openads::sql::WhereOp::Contains: return true;
                    default: return false;
                }
            }};
        };

        auto compiled = compile(*parsed.value().where);
        if (!compiled) return fail(compiled.error());
        tbl->set_filter(std::move(compiled).value());
    }

    // M10.6: ORDER BY <col> [ASC|DESC]. Materialize matching recnos
    // through the WHERE filter (or every live row when none), sort
    // them by the column's value, and install the sequence as the
    // cursor's traversal order.
    if (parsed.value().order_by) {
        // M10.6 / M10.37 — ORDER BY one column, with cascading
        // additional columns for ties (M10.37).
        struct SortKey {
            std::uint16_t field_index;
            bool          descending;
            bool          numeric;
        };
        std::vector<SortKey> sks;
        auto add_sort_key = [&](const openads::sql::OrderBy& ob)
            -> openads::util::Result<std::monostate>
        {
            std::int32_t fidx = tbl->field_index(ob.column);
            if (fidx < 0) {
                return openads::util::Error{
                    openads::AE_COLUMN_NOT_FOUND, 0,
                    ob.column.c_str(), ""};
            }
            const auto& fd = tbl->field_descriptor(
                static_cast<std::uint16_t>(fidx));
            SortKey k;
            k.field_index = static_cast<std::uint16_t>(fidx);
            k.descending  = ob.descending;
            k.numeric =
                fd.type == openads::drivers::DbfFieldType::Numeric ||
                fd.type == openads::drivers::DbfFieldType::Float   ||
                fd.type == openads::drivers::DbfFieldType::Integer ||
                fd.type == openads::drivers::DbfFieldType::Currency||
                fd.type == openads::drivers::DbfFieldType::Double;
            sks.push_back(k);
            return std::monostate{};
        };
        if (auto r = add_sort_key(*parsed.value().order_by); !r)
            return fail(r.error());
        for (auto& ob : parsed.value().order_by_extra) {
            if (auto r = add_sort_key(ob); !r) return fail(r.error());
        }

        std::vector<std::uint32_t> matched;
        std::uint32_t rcount = tbl->record_count();
        for (std::uint32_t r = 1; r <= rcount; ++r) {
            if (auto g = tbl->goto_record(r); !g) continue;
            if (tbl->is_deleted()) continue;
            if (!tbl->passes_filter()) continue;
            matched.push_back(r);
        }

        struct Row {
            std::uint32_t              recno;
            std::vector<std::string>   s;
            std::vector<double>        d;
        };
        std::vector<Row> rows;
        rows.reserve(matched.size());
        for (auto r : matched) {
            (void)tbl->goto_record(r);
            Row row;
            row.recno = r;
            row.s.resize(sks.size());
            row.d.resize(sks.size());
            for (std::size_t i = 0; i < sks.size(); ++i) {
                auto v = tbl->read_field(sks[i].field_index);
                if (v) {
                    row.s[i] = v.value().as_string;
                    row.d[i] = v.value().as_double;
                }
            }
            rows.push_back(std::move(row));
        }
        std::stable_sort(rows.begin(), rows.end(),
            [&](const Row& a, const Row& b) {
                for (std::size_t i = 0; i < sks.size(); ++i) {
                    bool less, equal;
                    if (sks[i].numeric) {
                        less  = a.d[i] <  b.d[i];
                        equal = a.d[i] == b.d[i];
                    } else {
                        less  = a.s[i] <  b.s[i];
                        equal = a.s[i] == b.s[i];
                    }
                    if (equal) continue;
                    return sks[i].descending ? !less : less;
                }
                return false;
            });
        std::vector<std::uint32_t> seq;
        seq.reserve(rows.size());
        for (auto& row : rows) seq.push_back(row.recno);
        tbl->clear_filter();
        tbl->set_recno_sequence(std::move(seq));
    }

    // M10.31 — DISTINCT. M10.32 — LIMIT [OFFSET]. Both operate on the
    // post-WHERE / post-ORDER-BY traversal sequence; if neither
    // ORDER BY nor a recno_sequence is present yet, walk the
    // filtered cursor to materialise one first.
    bool need_seq_post = parsed.value().distinct ||
                         parsed.value().limit  >= 0 ||
                         parsed.value().offset > 0;
    if (need_seq_post) {
        std::vector<std::uint32_t> seq;
        if (tbl->has_recno_sequence()) {
            seq = tbl->recno_sequence();
        } else {
            std::uint32_t rcount = tbl->record_count();
            seq.reserve(rcount);
            for (std::uint32_t r = 1; r <= rcount; ++r) {
                if (auto g = tbl->goto_record(r); !g) continue;
                if (tbl->is_deleted()) continue;
                if (!tbl->passes_filter()) continue;
                seq.push_back(r);
            }
        }
        if (parsed.value().distinct) {
            std::vector<std::uint16_t> proj_indices;
            if (parsed.value().projection.empty()) {
                std::uint16_t nf = tbl->field_count();
                proj_indices.reserve(nf);
                for (std::uint16_t i = 0; i < nf; ++i) proj_indices.push_back(i);
            } else {
                for (auto& cn : parsed.value().projection) {
                    std::int32_t fi = tbl->field_index(cn);
                    if (fi < 0) return fail(openads::AE_COLUMN_NOT_FOUND,
                                            cn.c_str());
                    proj_indices.push_back(static_cast<std::uint16_t>(fi));
                }
            }
            std::unordered_set<std::string> seen;
            std::vector<std::uint32_t> dedup;
            dedup.reserve(seq.size());
            for (auto r : seq) {
                if (auto g = tbl->goto_record(r); !g) continue;
                std::string key;
                for (auto fi : proj_indices) {
                    auto v = tbl->read_field(fi);
                    if (v) key.append(v.value().as_string);
                    key.push_back('\x1f');
                }
                if (seen.insert(std::move(key)).second) dedup.push_back(r);
            }
            seq = std::move(dedup);
        }
        std::int64_t off = parsed.value().offset > 0 ? parsed.value().offset : 0;
        if (off > static_cast<std::int64_t>(seq.size())) {
            seq.clear();
        } else if (off > 0) {
            seq.erase(seq.begin(), seq.begin() +
                static_cast<std::vector<std::uint32_t>::difference_type>(off));
        }
        if (parsed.value().limit >= 0 &&
            static_cast<std::size_t>(parsed.value().limit) < seq.size()) {
            seq.resize(static_cast<std::size_t>(parsed.value().limit));
        }
        tbl->clear_filter();
        tbl->set_recno_sequence(std::move(seq));
    }

    // M10.38 — projection contains a CASE expression. Materialise the
    // post-WHERE / post-ORDER-BY / post-DISTINCT / post-LIMIT row set
    // into a temp DBF whose schema mirrors the projection list (CASE
    // items become C(30); regular columns preserve source type +
    // length), evaluating each row's CASE branches inline.
    auto starts_with = [](const std::string& sl, const char* pre) {
        std::size_t L = std::strlen(pre);
        return sl.size() >= L && std::memcmp(sl.data(), pre, L) == 0;
    };
    bool has_synth = false;
    for (auto& p : parsed.value().projection) {
        if (starts_with(p, "$CASE_") || starts_with(p, "$FN_") ||
            starts_with(p, "$ARITH_") || starts_with(p, "$WIN_")) {
            has_synth = true; break;
        }
    }
    if (has_synth) {
        struct OutCol {
            std::string  name;
            char         raw_type = 'C';
            std::uint8_t length   = 0;
            std::int32_t src_field = -1;
            std::int32_t case_idx  = -1;
            std::int32_t fn_idx    = -1;
            std::int32_t arith_idx = -1;
            std::int32_t arith_lhs_field = -1;
            std::int32_t arith_rhs_field = -1;
            std::int32_t win_idx   = -1;
        };
        std::vector<OutCol> outs;
        outs.reserve(parsed.value().projection.size());
        for (std::size_t i = 0; i < parsed.value().projection.size(); ++i) {
            const auto& p = parsed.value().projection[i];
            OutCol o;
            if (starts_with(p, "$CASE_")) {
                std::size_t idx = std::stoul(p.substr(6));
                o.case_idx = static_cast<std::int32_t>(idx);
                const auto& ce = parsed.value().case_items[idx];
                if (!ce.alias.empty()) o.name = ce.alias;
                else {
                    char nm[16];
                    std::snprintf(nm, sizeof(nm), "CASE%zu", idx + 1);
                    o.name = nm;
                }
                o.raw_type = 'C';
                o.length   = 30;
            } else if (starts_with(p, "$FN_")) {
                std::size_t idx = std::stoul(p.substr(4));
                o.fn_idx = static_cast<std::int32_t>(idx);
                const auto& fc = parsed.value().fn_items[idx];
                using K = openads::sql::ScalarFnKind;
                bool zero_arg   = (fc.kind == K::Now  || fc.kind == K::Today ||
                                   fc.kind == K::Date || fc.kind == K::Time);
                bool single_col = !zero_arg &&
                                  (fc.kind == K::Upper ||
                                   fc.kind == K::Lower ||
                                   fc.kind == K::Len   ||
                                   fc.kind == K::Trim  ||
                                   fc.kind == K::Ltrim ||
                                   fc.kind == K::Rtrim);
                if (zero_arg) {
                    if (!fc.alias.empty()) o.name = fc.alias;
                    else o.name = (fc.kind == K::Now)   ? "NOW"
                                : (fc.kind == K::Today) ? "TODAY"
                                : (fc.kind == K::Date)  ? "DATE"
                                :                         "TIME";
                    o.raw_type = 'C';
                    o.length   = (fc.kind == K::Time) ? 8 : 19;
                    // src_field stays -1; value produced at row-eval time
                } else if (single_col) {
                    std::int32_t fi = tbl->field_index(fc.column);
                    if (fi < 0) return fail(openads::AE_COLUMN_NOT_FOUND,
                                            fc.column.c_str());
                    o.src_field = fi;
                    if (!fc.alias.empty()) o.name = fc.alias;
                    else o.name = fc.column;
                    o.raw_type = 'C';
                    if (fc.kind == K::Len) {
                        o.length = 10;
                    } else {
                        const auto& fd = tbl->field_descriptor(
                            static_cast<std::uint16_t>(fi));
                        o.length = static_cast<std::uint8_t>(fd.length ? fd.length : 30);
                    }
                } else if (fc.kind == K::Udf) {
                    o.name     = fc.alias.empty() ? fc.fn_name : fc.alias;
                    o.raw_type = 'C';
                    o.length   = 50;
                } else {
                    // M10.43 / M10.45 — multi-arg fns. Width = generous
                    // default; alias drives the column name; no
                    // pre-resolved src_field (per-arg lookups happen
                    // at row-eval time).
                    if (!fc.alias.empty()) o.name = fc.alias;
                    else {
                        char nm[16];
                        std::snprintf(nm, sizeof(nm), "EXPR%zu", idx + 1);
                        o.name = nm;
                    }
                    o.raw_type = 'C';
                    o.length   = (fc.kind == K::DateAdd) ? 8
                               : (fc.kind == K::DateDiff) ? 12
                               : 64;
                }
            } else if (starts_with(p, "$WIN_")) {
                std::size_t idx = std::stoul(p.substr(5));
                o.win_idx = static_cast<std::int32_t>(idx);
                const auto& wf = parsed.value().window_items[idx];
                if (!wf.alias.empty()) o.name = wf.alias;
                else {
                    char nm[16];
                    std::snprintf(nm, sizeof(nm), "RN%zu", idx + 1);
                    o.name = nm;
                }
                o.raw_type = 'C';
                o.length   = 10;
            } else if (starts_with(p, "$ARITH_")) {
                std::size_t idx = std::stoul(p.substr(7));
                o.arith_idx = static_cast<std::int32_t>(idx);
                const auto& ae = parsed.value().arith_items[idx];
                std::int32_t lhs = tbl->field_index(ae.lhs_column);
                if (lhs < 0) return fail(openads::AE_COLUMN_NOT_FOUND,
                                         ae.lhs_column.c_str());
                o.arith_lhs_field = lhs;
                if (!ae.rhs_is_literal) {
                    std::int32_t rhs = tbl->field_index(ae.rhs_column);
                    if (rhs < 0) return fail(openads::AE_COLUMN_NOT_FOUND,
                                             ae.rhs_column.c_str());
                    o.arith_rhs_field = rhs;
                }
                if (!ae.alias.empty()) o.name = ae.alias;
                else {
                    char nm[16];
                    std::snprintf(nm, sizeof(nm), "EXPR%zu", idx + 1);
                    o.name = nm;
                }
                o.raw_type = 'C';
                o.length   = 30;
            } else {
                std::int32_t fi = tbl->field_index(p);
                if (fi < 0) return fail(openads::AE_COLUMN_NOT_FOUND,
                                        p.c_str());
                const auto& fd = tbl->field_descriptor(
                    static_cast<std::uint16_t>(fi));
                o.name      = fd.name;
                o.raw_type  = static_cast<char>(fd.raw_type);
                o.length    = static_cast<std::uint8_t>(fd.length);
                o.src_field = fi;
            }
            outs.push_back(std::move(o));
        }

        // Compile each CASE branch's condition against tbl.
        using CondPred = std::function<bool(openads::engine::Table&)>;
        std::function<openads::util::Result<CondPred>(
            const openads::sql::WhereExpr&)> compile_cond;
        compile_cond = [&](const openads::sql::WhereExpr& node)
            -> openads::util::Result<CondPred>
        {
            using K = openads::sql::WhereExpr::Kind;
            if (node.kind == K::And || node.kind == K::Or) {
                std::vector<CondPred> ks;
                for (auto& cn : node.children) {
                    auto r = compile_cond(*cn);
                    if (!r) return r.error();
                    ks.push_back(std::move(r).value());
                }
                bool is_and = (node.kind == K::And);
                return CondPred{[ks = std::move(ks), is_and]
                                (openads::engine::Table& t) {
                    if (is_and) {
                        for (auto& k : ks) if (!k(t)) return false;
                        return true;
                    }
                    for (auto& k : ks) if (k(t)) return true;
                    return false;
                }};
            }
            if (node.kind == K::Not) {
                auto inner = compile_cond(*node.child);
                if (!inner) return inner.error();
                return CondPred{[p = std::move(inner).value()]
                                (openads::engine::Table& t)
                                { return !p(t); }};
            }
            if (node.kind == K::In) {
                std::int32_t fidx = tbl->field_index(node.in_clause.column);
                if (fidx < 0) return openads::util::Error{
                    openads::AE_COLUMN_NOT_FOUND, 0,
                    node.in_clause.column.c_str(), ""};
                std::uint16_t fi2 = static_cast<std::uint16_t>(fidx);
                auto set = std::make_shared<std::unordered_set<std::string>>(
                    node.in_clause.literals.begin(),
                    node.in_clause.literals.end());
                return CondPred{[fi2, set](openads::engine::Table& t) {
                    auto v = t.read_field(fi2);
                    if (!v) return false;
                    auto sv = v.value().as_string;
                    while (!sv.empty() && sv.back() == ' ') sv.pop_back();
                    return set->count(sv) > 0;
                }};
            }
            if (node.kind != K::Cmp) {
                return openads::util::Error{
                    openads::AE_FUNCTION_NOT_AVAILABLE, 0,
                    "CASE WHEN supports Cmp / AND / OR / NOT / IN only", ""};
            }
            const auto& w = node.cmp;
            std::int32_t fi = tbl->field_index(w.column);
            if (fi < 0) return openads::util::Error{
                openads::AE_COLUMN_NOT_FOUND, 0,
                w.column.c_str(), ""};
            std::uint16_t f = static_cast<std::uint16_t>(fi);
            openads::sql::WhereOp op = w.op;
            std::string lit = w.literal;
            std::string lit2 = w.literal2;
            bool is_num = w.is_numeric;
            double num  = w.number;
            double num2 = w.number2;
            std::shared_ptr<std::unordered_set<std::uint32_t>> contains_hits;
            if (op == openads::sql::WhereOp::Contains) {
                namespace fs = std::filesystem;
                fs::path fts_path =
                    fs::path(tbl->path()).replace_extension(".fts");
                auto loaded = openads::engine::Fts::load(fts_path.string());
                if (loaded) {
                    openads::engine::FtsOptions opts;
                    auto hits = openads::engine::Fts::search(
                        loaded.value(), lit, opts);
                    contains_hits =
                        std::make_shared<std::unordered_set<std::uint32_t>>(
                            hits.begin(), hits.end());
                }
            }
            openads::sql::WhereFn lhs_fn = w.lhs_fn;
            return CondPred{[f, op, lit, lit2, is_num, num, num2,
                             contains_hits, lhs_fn]
                            (openads::engine::Table& t) {
                if (op == openads::sql::WhereOp::Contains) {
                    if (!contains_hits) return false;
                    return contains_hits->find(t.recno()) != contains_hits->end();
                }
                auto v = t.read_field(f);
                if (!v) return false;
                if (op == openads::sql::WhereOp::IsNull ||
                    op == openads::sql::WhereOp::IsNotNull) {
                    bool null_ish = t.is_field_null(f);
                    if (!null_ish) {
                        auto sv = v.value().as_string;
                        while (!sv.empty() && sv.back() == ' ') sv.pop_back();
                        null_ish = sv.empty();
                    }
                    return op == openads::sql::WhereOp::IsNull
                        ? null_ish : !null_ish;
                }
                if (op == openads::sql::WhereOp::Between) {
                    if (is_num) {
                        double d = v.value().as_double;
                        return d >= num && d <= num2;
                    }
                    auto sv = apply_where_fn(v.value().as_string, lhs_fn);
                    return sv.compare(lit)  >= 0 &&
                           sv.compare(lit2) <= 0;
                }
                if (op == openads::sql::WhereOp::Like) {
                    auto sv = apply_where_fn(v.value().as_string, lhs_fn);
                    while (!sv.empty() && sv.back() == ' ') sv.pop_back();
                    return sql_like_match(sv, lit);
                }
                int cmp = 0;
                if (is_num) {
                    double d = v.value().as_double;
                    if      (d < num) cmp = -1;
                    else if (d > num) cmp =  1;
                } else {
                    cmp = apply_where_fn(v.value().as_string, lhs_fn)
                              .compare(lit);
                }
                switch (op) {
                    case openads::sql::WhereOp::Eq: return cmp == 0;
                    case openads::sql::WhereOp::Ne: return cmp != 0;
                    case openads::sql::WhereOp::Lt: return cmp <  0;
                    case openads::sql::WhereOp::Gt: return cmp >  0;
                    case openads::sql::WhereOp::Le: return cmp <= 0;
                    case openads::sql::WhereOp::Ge: return cmp >= 0;
                default: return false;
                }
            }};
        };
        struct CompiledCase {
            std::vector<CondPred>           branch_preds;
            std::vector<std::string>        branch_values;
            bool                            has_else = false;
            std::string                     else_value;
        };
        std::vector<CompiledCase> ccases;
        ccases.reserve(parsed.value().case_items.size());
        for (auto& ce : parsed.value().case_items) {
            CompiledCase cc;
            for (auto& br : ce.branches) {
                auto p = compile_cond(*br.cond);
                if (!p) return fail(p.error());
                cc.branch_preds.push_back(std::move(p).value());
                cc.branch_values.push_back(br.then_value.text);
            }
            cc.has_else  = ce.has_else;
            cc.else_value = ce.has_else ? ce.else_value.text : std::string();
            ccases.push_back(std::move(cc));
        }

        // Build the row list — honor any installed recno_sequence,
        // else walk the filtered cursor.
        std::vector<std::uint32_t> walk_seq;
        if (tbl->has_recno_sequence()) {
            walk_seq = tbl->recno_sequence();
        } else {
            std::uint32_t rcount = tbl->record_count();
            for (std::uint32_t r = 1; r <= rcount; ++r) {
                if (auto g = tbl->goto_record(r); !g) continue;
                if (tbl->is_deleted()) continue;
                if (!tbl->passes_filter()) continue;
                walk_seq.push_back(r);
            }
        }

        // M10.49 / M10.50 — pre-compute window values per row when
        // any window items appear in the projection. For each
        // window slot, group rows by PARTITION BY key, sort within
        // each group by ORDER BY (if any), then assign values per
        // kind (ROW_NUMBER / RANK / DENSE_RANK).
        std::unordered_map<std::uint32_t, std::vector<std::string>>
            window_vals;
        if (!parsed.value().window_items.empty()) {
            window_vals.reserve(walk_seq.size());
            for (std::size_t wi = 0;
                 wi < parsed.value().window_items.size(); ++wi) {
                const auto& wf = parsed.value().window_items[wi];
                struct Entry {
                    std::uint32_t recno;
                    std::string   pkey;
                    std::string   okey;
                };
                std::vector<Entry> ents;
                ents.reserve(walk_seq.size());
                for (auto r : walk_seq) {
                    if (auto g = tbl->goto_record(r); !g) continue;
                    Entry e;
                    e.recno = r;
                    for (auto& pc : wf.partition_by) {
                        std::int32_t fi = tbl->field_index(pc);
                        if (fi >= 0) {
                            auto v = tbl->read_field(
                                static_cast<std::uint16_t>(fi));
                            if (v) e.pkey += v.value().as_string;
                        }
                        e.pkey.push_back('\x1f');
                    }
                    if (wf.order_by) {
                        std::int32_t fi =
                            tbl->field_index(wf.order_by->column);
                        if (fi >= 0) {
                            auto v = tbl->read_field(
                                static_cast<std::uint16_t>(fi));
                            if (v) e.okey = v.value().as_string;
                        }
                    }
                    ents.push_back(std::move(e));
                }
                std::stable_sort(ents.begin(), ents.end(),
                    [&](const Entry& a, const Entry& b) {
                        if (a.pkey != b.pkey) return a.pkey < b.pkey;
                        if (wf.order_by) {
                            return wf.order_by->descending
                                ? a.okey > b.okey
                                : a.okey < b.okey;
                        }
                        return false;
                    });
                std::string prev_pk;
                std::string prev_ok;
                bool prev_ok_set = false;
                std::uint32_t pos       = 0;
                std::uint32_t rank_now  = 0;
                std::uint32_t dense_now = 0;
                for (auto& e : ents) {
                    if (e.pkey != prev_pk) {
                        pos = 0; rank_now = 0; dense_now = 0;
                        prev_ok_set = false;
                        prev_pk = e.pkey;
                    }
                    ++pos;
                    bool tied = wf.order_by && prev_ok_set &&
                                e.okey == prev_ok;
                    if (!tied) {
                        rank_now = pos;
                        ++dense_now;
                    }
                    prev_ok = e.okey;
                    prev_ok_set = true;
                    std::string val;
                    switch (wf.kind) {
                        case openads::sql::WindowFnKind::RowNumber:
                            val = std::to_string(pos); break;
                        case openads::sql::WindowFnKind::Rank:
                            val = std::to_string(rank_now); break;
                        case openads::sql::WindowFnKind::DenseRank:
                            val = std::to_string(dense_now); break;
                    }
                    auto& slot = window_vals[e.recno];
                    if (slot.size() <
                        parsed.value().window_items.size()) {
                        slot.resize(
                            parsed.value().window_items.size());
                    }
                    slot[wi] = std::move(val);
                }
            }
        }

        // Build temp DBF.
        namespace fs = std::filesystem;
        char nb[64];
        std::snprintf(nb, sizeof(nb), "_case_%llx.dbf",
                      static_cast<unsigned long long>(
                          openads::platform::monotonic_nanos()));
        fs::path dbf = fs::path(c->data_dir()) / nb;
        std::vector<std::uint8_t> file;
        std::array<std::uint8_t, 32> hdr{};
        hdr[0] = 0x03;
        stamp_dbf_header_today(hdr.data());
        std::uint16_t hl = static_cast<std::uint16_t>(
            32 + 32 * outs.size() + 1);
        std::uint32_t rl = 1;
        for (auto& o : outs) rl += o.length;
        hdr[8]  = static_cast<std::uint8_t>( hl       & 0xFFu);
        hdr[9]  = static_cast<std::uint8_t>((hl >> 8) & 0xFFu);
        hdr[10] = static_cast<std::uint8_t>( rl       & 0xFFu);
        hdr[11] = static_cast<std::uint8_t>((rl >> 8) & 0xFFu);
        file.insert(file.end(), hdr.begin(), hdr.end());
        for (auto& o : outs) {
            std::array<std::uint8_t, 32> fd{};
            std::memcpy(fd.data(), o.name.data(),
                        std::min(o.name.size(), std::size_t{11}));
            fd[11] = static_cast<std::uint8_t>(o.raw_type);
            fd[16] = o.length;
            file.insert(file.end(), fd.begin(), fd.end());
        }
        file.push_back(0x0D);

        std::uint32_t emitted = 0;
        auto trim_left = [](std::string sl) {
            std::size_t i = 0;
            while (i < sl.size() && sl[i] == ' ') ++i;
            return sl.substr(i);
        };
        auto trim_right = [](std::string sl) {
            while (!sl.empty() && sl.back() == ' ') sl.pop_back();
            return sl;
        };
        auto trim_both = [&](std::string sl) {
            return trim_left(trim_right(std::move(sl)));
        };
        for (std::uint32_t r : walk_seq) {
            if (auto g = tbl->goto_record(r); !g) continue;
            file.push_back(' ');
            for (auto& o : outs) {
                std::string val;
                bool from_synth = false;
                if (o.case_idx >= 0) {
                    from_synth = true;
                    const auto& cc =
                        ccases[static_cast<std::size_t>(o.case_idx)];
                    val = cc.has_else ? cc.else_value : "";
                    for (std::size_t bi = 0; bi < cc.branch_preds.size(); ++bi) {
                        if (cc.branch_preds[bi](*tbl)) {
                            val = cc.branch_values[bi];
                            break;
                        }
                    }
                } else if (o.fn_idx >= 0) {
                    from_synth = true;
                    const auto& fc = parsed.value().fn_items[
                        static_cast<std::size_t>(o.fn_idx)];
                    std::string raw;
                    if (o.src_field >= 0) {
                        auto v = tbl->read_field(
                            static_cast<std::uint16_t>(o.src_field));
                        if (v) raw = v.value().as_string;
                    }
                    using K = openads::sql::ScalarFnKind;
                    switch (fc.kind) {
                        case K::Now:
                        case K::Today:
                        case K::Date:
                        case K::Time: {
                            std::time_t t = std::time(nullptr);
                            std::tm tm_local{};
#ifdef _WIN32
                            localtime_s(&tm_local, &t);
#else
                            localtime_r(&t, &tm_local);
#endif
                            char buf[24];
                            if (fc.kind == K::Time)
                                std::strftime(buf, sizeof(buf), "%H:%M:%S", &tm_local);
                            else if (fc.kind == K::Date || fc.kind == K::Today)
                                std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm_local);
                            else
                                std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_local);
                            val = buf;
                            break;
                        }
                        case K::Upper:
                            for (auto& ch : raw)
                                ch = static_cast<char>(std::toupper(
                                    static_cast<unsigned char>(ch)));
                            val = std::move(raw);
                            break;
                        case K::Lower:
                            for (auto& ch : raw)
                                ch = static_cast<char>(std::tolower(
                                    static_cast<unsigned char>(ch)));
                            val = std::move(raw);
                            break;
                        case K::Len: {
                            std::string trimmed = trim_right(std::move(raw));
                            char buf[16];
                            std::snprintf(buf, sizeof(buf), "%zu",
                                          trimmed.size());
                            val = buf;
                            break;
                        }
                        case K::Trim:  val = trim_both(std::move(raw));  break;
                        case K::Ltrim: val = trim_left(std::move(raw));  break;
                        case K::Rtrim: val = trim_right(std::move(raw)); break;

                        case K::Udf: {
                            // Look up function in the DD (case-insensitive).
                            if (!c->has_dd()) break;
                            auto* dd2 = c->dd();
                            std::string udf_impl, udf_params;
                            {
                                std::string fn_up = fc.fn_name;
                                for (auto& ch2 : fn_up) ch2 = static_cast<char>(std::toupper((unsigned char)ch2));
                                for (const auto& kv : dd2->functions()) {
                                    std::string kv_up = kv.first;
                                    for (auto& ch2 : kv_up) ch2 = static_cast<char>(std::toupper((unsigned char)ch2));
                                    if (kv_up == fn_up) {
                                        udf_impl   = kv.second.implementation;
                                        udf_params = kv.second.input_params;
                                        break;
                                    }
                                }
                            }
                            if (udf_impl.empty()) break;

                            // Build scope from named parameters, then run the
                            // procedural body through the mini-interpreter.
                            {
                                proc::Scope scope;
                                // Parse "name TYPE, name TYPE, ..." → parameter names
                                std::vector<std::string> pnames;
                                {
                                    std::size_t ix = 0;
                                    while (ix < udf_params.size()) {
                                        while (ix < udf_params.size() && std::isspace((unsigned char)udf_params[ix])) ++ix;
                                        std::string pn;
                                        while (ix < udf_params.size() &&
                                               !std::isspace((unsigned char)udf_params[ix]) &&
                                               udf_params[ix] != ',')
                                            pn.push_back(udf_params[ix++]);
                                        if (!pn.empty()) pnames.push_back(pn);
                                        while (ix < udf_params.size() && udf_params[ix] != ',') ++ix;
                                        if (ix < udf_params.size()) ++ix;
                                    }
                                }
                                // Populate scope with evaluated argument values.
                                for (std::size_t pi = 0; pi < pnames.size() && pi < fc.args.size(); ++pi) {
                                    const auto& arg = fc.args[pi];
                                    std::string aval;
                                    if (!arg.is_column) {
                                        if (arg.is_numeric) {
                                            char buf[32];
                                            if (arg.number == std::floor(arg.number) && std::abs(arg.number) < 1e15)
                                                std::snprintf(buf, sizeof(buf), "%.0f", arg.number);
                                            else
                                                std::snprintf(buf, sizeof(buf), "%g", arg.number);
                                            aval = buf;
                                        } else if (arg.is_call) {
                                            aval = proc::eval(arg.text, scope, hStatement);
                                        } else {
                                            aval = arg.text; // string literal stored unquoted
                                        }
                                    }
                                    scope[proc::xupper(pnames[pi])] = aval;
                                }
                                val = proc::exec_body(udf_impl, scope, hStatement);
                            }
                            break;
                        }
                        case K::Substr:
                        case K::Concat:
                        case K::Replace:
                        case K::DateDiff:
                        case K::DateAdd:
                        case K::NullIf:
                        case K::Coalesce:
                        case K::IfNull: {
                            // M10.43 / M10.45 — multi-arg fns. Resolve
                            // each arg as either a column read (with
                            // trailing-space trim for Char-typed slots)
                            // or the parsed literal.
                            auto arg_str = [&](const openads::sql::ScalarFnArg& a)
                                -> std::string {
                                if (!a.is_column) return a.text;
                                std::int32_t fi = tbl->field_index(a.column);
                                if (fi < 0) return std::string();
                                auto fv = tbl->read_field(
                                    static_cast<std::uint16_t>(fi));
                                if (!fv) return std::string();
                                std::string sl = fv.value().as_string;
                                while (!sl.empty() && sl.back() == ' ')
                                    sl.pop_back();
                                return sl;
                            };
                            auto arg_num = [&](const openads::sql::ScalarFnArg& a)
                                -> double {
                                if (!a.is_column) return a.number;
                                std::int32_t fi = tbl->field_index(a.column);
                                if (fi < 0) return 0.0;
                                auto fv = tbl->read_field(
                                    static_cast<std::uint16_t>(fi));
                                return fv ? fv.value().as_double : 0.0;
                            };
                            if (fc.kind == K::Substr && fc.args.size() >= 2) {
                                std::string src = arg_str(fc.args[0]);
                                long start = static_cast<long>(
                                    arg_num(fc.args[1]));      // 1-based
                                long len = (fc.args.size() >= 3)
                                    ? static_cast<long>(arg_num(fc.args[2]))
                                    : static_cast<long>(src.size());
                                if (start < 1) start = 1;
                                std::size_t s0 = static_cast<std::size_t>(start - 1);
                                if (s0 >= src.size()) val.clear();
                                else {
                                    std::size_t take =
                                        std::min<std::size_t>(
                                            len < 0 ? 0 : (std::size_t)len,
                                            src.size() - s0);
                                    val = src.substr(s0, take);
                                }
                            } else if (fc.kind == K::Concat) {
                                for (auto& a : fc.args) val += arg_str(a);
                            } else if (fc.kind == K::Replace &&
                                       fc.args.size() == 3) {
                                std::string src = arg_str(fc.args[0]);
                                std::string oldp = arg_str(fc.args[1]);
                                std::string newp = arg_str(fc.args[2]);
                                if (!oldp.empty()) {
                                    std::size_t i = 0;
                                    while ((i = src.find(oldp, i)) !=
                                           std::string::npos) {
                                        src.replace(i, oldp.size(), newp);
                                        i += newp.size();
                                    }
                                }
                                val = std::move(src);
                            } else if (fc.kind == K::DateDiff &&
                                       fc.args.size() == 2) {
                                // M10.45 — DATEDIFF on YYYYMMDD strings:
                                // returns days_a - days_b via Julian day.
                                auto julian = [](const std::string& sl) -> long {
                                    if (sl.size() < 8) return 0;
                                    int y = std::atoi(sl.substr(0, 4).c_str());
                                    int mo = std::atoi(sl.substr(4, 2).c_str());
                                    int d = std::atoi(sl.substr(6, 2).c_str());
                                    long a = (14 - mo) / 12;
                                    long y2 = y + 4800 - a;
                                    long m2 = mo + 12 * a - 3;
                                    return d + (153 * m2 + 2) / 5 + 365 * y2 +
                                           y2 / 4 - y2 / 100 + y2 / 400 - 32045;
                                };
                                long ja = julian(arg_str(fc.args[0]));
                                long jb = julian(arg_str(fc.args[1]));
                                char buf[32];
                                std::snprintf(buf, sizeof(buf), "%ld",
                                              ja - jb);
                                val = buf;
                            } else if (fc.kind == K::NullIf &&
                                       fc.args.size() == 2) {
                                // M10.53 — NULLIF(a, b): NULL if
                                // equal, else a. Empty string =
                                // NULL by convention.
                                std::string a = arg_str(fc.args[0]);
                                std::string b = arg_str(fc.args[1]);
                                val = (a == b) ? std::string() : a;
                            } else if (fc.kind == K::Coalesce) {
                                // M10.53 — first non-empty arg wins.
                                for (auto& a : fc.args) {
                                    auto cs = arg_str(a);
                                    if (!cs.empty()) {
                                        val = std::move(cs); break;
                                    }
                                }
                            } else if (fc.kind == K::IfNull &&
                                       fc.args.size() == 2) {
                                // M10.53 — IFNULL(expr, default).
                                std::string a = arg_str(fc.args[0]);
                                val = a.empty() ? arg_str(fc.args[1]) : a;
                            } else if (fc.kind == K::DateAdd &&
                                       fc.args.size() == 2) {
                                // M10.45 — add N days to YYYYMMDD.
                                std::string ds = arg_str(fc.args[0]);
                                if (ds.size() < 8) { val = ds; break; }
                                int y = std::atoi(ds.substr(0, 4).c_str());
                                int mo_in = std::atoi(ds.substr(4, 2).c_str());
                                int d = std::atoi(ds.substr(6, 2).c_str());
                                long n = static_cast<long>(arg_num(fc.args[1]));
                                long aa = (14 - mo_in) / 12;
                                long y2 = y + 4800 - aa;
                                long m2 = mo_in + 12 * aa - 3;
                                long jdn = d + (153 * m2 + 2) / 5 + 365 * y2 +
                                           y2 / 4 - y2 / 100 + y2 / 400 - 32045;
                                jdn += n;
                                long la = jdn + 32044;
                                long b  = (4 * la + 3) / 146097;
                                long c2 = la - (146097 * b) / 4;
                                long d2 = (4 * c2 + 3) / 1461;
                                long e  = c2 - (1461 * d2) / 4;
                                long mn = (5 * e + 2) / 153;
                                int  day = static_cast<int>(
                                    e - (153 * mn + 2) / 5 + 1);
                                int  mo  = static_cast<int>(
                                    mn + 3 - 12 * (mn / 10));
                                int  yr  = static_cast<int>(
                                    100 * b + d2 - 4800 + mn / 10);
                                char buf[16];
                                std::snprintf(buf, sizeof(buf),
                                              "%04d%02d%02d", yr, mo, day);
                                val = buf;
                            } else {
                                val.clear();
                            }
                            break;
                        }
                    }
                } else if (o.win_idx >= 0) {
                    from_synth = true;
                    auto wit = window_vals.find(r);
                    if (wit != window_vals.end() &&
                        static_cast<std::size_t>(o.win_idx) <
                            wit->second.size() &&
                        !wit->second[
                            static_cast<std::size_t>(o.win_idx)].empty()) {
                        val = wit->second[
                            static_cast<std::size_t>(o.win_idx)];
                    } else {
                        char buf[16];
                        std::snprintf(buf, sizeof(buf), "%u",
                                      static_cast<unsigned>(emitted + 1));
                        val = buf;
                    }
                } else if (o.arith_idx >= 0) {
                    from_synth = true;
                    const auto& ae = parsed.value().arith_items[
                        static_cast<std::size_t>(o.arith_idx)];
                    auto lv = tbl->read_field(
                        static_cast<std::uint16_t>(o.arith_lhs_field));
                    double a = lv ? lv.value().as_double : 0.0;
                    double b = 0.0;
                    if (ae.rhs_is_literal) b = ae.rhs_number;
                    else {
                        auto rv = tbl->read_field(
                            static_cast<std::uint16_t>(o.arith_rhs_field));
                        b = rv ? rv.value().as_double : 0.0;
                    }
                    double res = 0.0;
                    using AO = openads::sql::ArithOp;
                    switch (ae.op) {
                        case AO::Add: res = a + b; break;
                        case AO::Sub: res = a - b; break;
                        case AO::Mul: res = a * b; break;
                        case AO::Div: res = (b != 0.0) ? a / b : 0.0; break;
                    }
                    char buf[64];
                    std::snprintf(buf, sizeof(buf), "%g", res);
                    val = buf;
                }
                if (from_synth) {
                    if (val.size() > o.length) val.resize(o.length);
                    for (std::uint8_t k = 0; k < o.length; ++k) {
                        file.push_back(k < val.size()
                            ? static_cast<std::uint8_t>(val[k]) : ' ');
                    }
                } else {
                    auto v = tbl->read_field(
                        static_cast<std::uint16_t>(o.src_field));
                    std::string raw = v ? v.value().as_string : std::string();
                    for (std::uint8_t k = 0; k < o.length; ++k) {
                        file.push_back(k < raw.size()
                            ? static_cast<std::uint8_t>(raw[k]) : ' ');
                    }
                }
            }
            ++emitted;
        }
        file.push_back(0x1A);
        file[4] = static_cast<std::uint8_t>( emitted        & 0xFFu);
        file[5] = static_cast<std::uint8_t>((emitted >>  8) & 0xFFu);
        file[6] = static_cast<std::uint8_t>((emitted >> 16) & 0xFFu);
        file[7] = static_cast<std::uint8_t>((emitted >> 24) & 0xFFu);
        {
            std::ofstream out(dbf, std::ios::binary);
            if (!out) return fail(openads::AE_INTERNAL_ERROR,
                "case temp DBF open for write failed");
            out.write(reinterpret_cast<const char*>(file.data()),
                      static_cast<std::streamsize>(file.size()));
        }
        std::string rel = dbf.filename().string();
        auto cth = c->open_table(rel, openads::engine::TableType::Cdx,
                                 openads::engine::OpenMode::Read);
        if (!cth) return fail(cth.error());
        openads::engine::Table* ctbl = c->lookup_table(cth.value());
        if (!ctbl) return fail(openads::AE_INTERNAL_ERROR, "post-open");
        ADSHANDLE gh_case = s.registry.register_object(HandleKind::Table, ctbl);
        *phCursor = gh_case;
        return ok();
    }

    // M10.46 — when this query was a derived-table outer SELECT,
    // reuse the inner cursor's existing handle so the user-visible
    // cursor isn't a stale alias of an already-registered Table*.
    ADSHANDLE gh = (derived_cur != 0)
        ? derived_cur
        : s.registry.register_object(HandleKind::Table, tbl);

    if (!parsed.value().projection.empty()) {
        std::vector<std::uint16_t> proj;
        proj.reserve(parsed.value().projection.size());
        for (const auto& col : parsed.value().projection) {
            std::int32_t fidx = tbl->field_index(col);
            if (fidx < 0) {
                return fail(openads::AE_COLUMN_NOT_FOUND, col.c_str());
            }
            proj.push_back(static_cast<std::uint16_t>(fidx));
        }
        cursor_projections()[gh] = std::move(proj);
    }

    *phCursor = gh;
    return ok();
}

// ---- Date display format (AdsSetDateFormat / AdsGetDateFormat) -------------
//
// ACE keeps one process-wide date display string. The historical
// default here is "yyyy-mm-dd"; AdsSetDateFormat overrides it (ADS
// itself uses "MM/DD/CCYY" out of the box, but changing our default
// would shift every existing caller, so the override is opt-in).
//
// This file is largely one big `extern "C"` block; these helpers
// return std::string / a small struct, so they need C++ linkage.
extern "C++" {
namespace {

std::string g_date_format = "yyyy-mm-dd";

// Render (y, m, d) through an ACE-style format string. Recognised,
// case-insensitively: CCYY / YYYY → 4-digit year, YY → 2-digit year,
// MM → 2-digit month, DD → 2-digit day. Every other character is
// copied verbatim, so separators ("/", "-", ".") pass straight through.
std::string format_ace_date(const std::string& fmt, int y, int m, int d) {
    char two_y[4], two[4], four[8];
    std::snprintf(two_y, sizeof(two_y), "%02d", ((y % 100) + 100) % 100);
    std::snprintf(four,  sizeof(four),  "%04d", y);
    std::string up;
    up.reserve(fmt.size());
    for (char c : fmt)
        up.push_back(static_cast<char>(std::toupper(
            static_cast<unsigned char>(c))));
    std::string out;
    out.reserve(fmt.size() + 4);
    for (std::size_t i = 0; i < up.size(); ) {
        auto at = [&](const char* tok) {
            std::size_t L = std::strlen(tok);
            return up.compare(i, L, tok) == 0;
        };
        if (at("CCYY") || at("YYYY")) { out += four;  i += 4; }
        else if (at("YY"))            { out += two_y; i += 2; }
        else if (at("MM")) { std::snprintf(two, sizeof(two), "%02d", m);
                             out += two; i += 2; }
        else if (at("DD")) { std::snprintf(two, sizeof(two), "%02d", d);
                             out += two; i += 2; }
        else { out.push_back(up[i]); ++i; }
    }
    return out;
}

// Read the DBF header's "last updated" stamp (header bytes 1..3 are
// YY MM DD, the year byte being an offset from 1900) straight off the
// table file. Returns {0,0,0} when the table has no driver or the
// read fails. Matches the convention in drivers/dbf_common.cpp.
struct HeaderDate { int y = 0, m = 0, d = 0; };
HeaderDate read_header_date(openads::engine::Table* t) {
    HeaderDate r;
    if (t == nullptr || t->driver() == nullptr) return r;
    std::uint8_t b[3] = {0, 0, 0};
    auto got = t->driver()->file().read_at(1, b, sizeof(b));
    if (!got || got.value() < sizeof(b)) return r;
    r.y = 1900 + static_cast<int>(b[0]);
    r.m = static_cast<int>(b[1]);
    r.d = static_cast<int>(b[2]);
    return r;
}

// Copy `s` (plus NUL) into a caller buffer using the ACE in/out length
// convention: *pusLen in = capacity, out = the string's true length;
// the copy is truncated to fit. No-op when pusLen is null.
void copy_ace_string(const std::string& s, UNSIGNED8* buf, UNSIGNED16* pusLen) {
    if (pusLen == nullptr) return;
    UNSIGNED16 cap = *pusLen;
    if (buf != nullptr && cap > 0) {
        std::size_t n = std::min<std::size_t>(s.size(),
                                              static_cast<std::size_t>(cap - 1));
        std::memcpy(buf, s.data(), n);
        buf[n] = 0;
    }
    *pusLen = static_cast<UNSIGNED16>(s.size());
}

} // namespace
} // extern "C++"

// ---- M(rddads-compat): stubs for Harbour contrib/rddads -------------------
//
// rddads.hbp pulls in symbols across the full SAP ace.h surface even if
// only a handful are actually exercised by `dbUseArea( .T., "ADS", ... )`.
// To make rddads link clean against ace64.lib, every referenced symbol
// must resolve. The stubs below return AE_FUNCTION_NOT_AVAILABLE for
// features the engine doesn't implement yet (advisory-only management
// API, AOF, scoped relations, callbacks); apps that don't touch those
// features still link and run.

#define ADS_STUB(rc) return (rc)

extern "C" {

UNSIGNED32 AdsConnect(UNSIGNED8* pucServer, ADSHANDLE* phConnect) {
    return AdsConnect60(pucServer, ADS_LOCAL_SERVER,
                        nullptr, nullptr, 0, phConnect);
}
UNSIGNED32 AdsApplicationExit(void) { ADS_STUB(openads::AE_SUCCESS); }
UNSIGNED32 AdsClearFilter(ADSHANDLE) { ADS_STUB(openads::AE_SUCCESS); }
UNSIGNED32 AdsClearRelation(ADSHANDLE) { ADS_STUB(openads::AE_SUCCESS); }
UNSIGNED32 AdsClearCallbackFunction(void) { ADS_STUB(openads::AE_SUCCESS); }
UNSIGNED32 AdsClearProgressCallback(void) { ADS_STUB(openads::AE_SUCCESS); }
UNSIGNED32 AdsCacheOpenCursors(UNSIGNED16) { ADS_STUB(openads::AE_SUCCESS); }
UNSIGNED32 AdsCacheOpenTables(UNSIGNED16) { ADS_STUB(openads::AE_SUCCESS); }
UNSIGNED32 AdsCacheRecords(ADSHANDLE, UNSIGNED16) { ADS_STUB(openads::AE_SUCCESS); }
UNSIGNED32 AdsCloseCachedTables(ADSHANDLE) { ADS_STUB(openads::AE_SUCCESS); }
UNSIGNED32 AdsCopyTableContent(ADSHANDLE hSrc, ADSHANDLE hDst) {
    Table* src = get_table(hSrc);
    Table* dst = get_table(hDst);
    if (!src || !dst) return fail(openads::AE_INTERNAL_ERROR, "unknown table");

    // Build a field-name mapping: for each source field find the
    // matching destination field (by name). Fields that exist only in
    // one table are silently skipped, which is the documented ADS
    // behaviour — no schema match required.
    struct FieldPair { std::uint16_t si; std::uint16_t di; };
    std::vector<FieldPair> pairs;
    std::uint16_t nc = src->field_count();
    for (std::uint16_t si = 0; si < nc; ++si) {
        std::int32_t di = dst->field_index(src->field_descriptor(si).name);
        if (di >= 0) pairs.push_back({si, static_cast<std::uint16_t>(di)});
    }

    std::uint32_t rcount = src->record_count();
    for (std::uint32_t r = 1; r <= rcount; ++r) {
        if (auto g = src->goto_record(r); !g) continue;
        if (src->is_deleted()) continue;
        if (auto ar = dst->append_record(); !ar) return fail(ar.error());
        for (auto& fp : pairs) {
            auto v = src->read_field(fp.si);
            if (!v) continue;
            (void)dst->set_field(fp.di, v.value().as_string);
        }
    }
    if (auto fl = dst->flush(); !fl) return fail(fl.error());
    return ok();
}
UNSIGNED32 AdsCustomizeAOF(ADSHANDLE, UNSIGNED32, UNSIGNED32*, UNSIGNED16)
    { ADS_STUB(openads::AE_FUNCTION_NOT_AVAILABLE); }
UNSIGNED32 AdsData(UNSIGNED16, void*) { ADS_STUB(openads::AE_SUCCESS); }
// SAP / rddads signature: AdsEvalAOF(hTable, pucExpr, *pusOptLevel).
// Returns the optimisation level (ADS_OPTIMIZED_NONE / PART / FULL)
// the engine would use to evaluate the filter. Currently a stub —
// caller's *pusOptLevel is zeroed (= ADS_OPTIMIZED_NONE).
UNSIGNED32 AdsEvalAOF(ADSHANDLE, UNSIGNED8*, UNSIGNED16* pusOptLevel)
    { if (pusOptLevel) *pusOptLevel = 0;
      return openads::AE_SUCCESS; }
UNSIGNED32 AdsFilterOption(ADSHANDLE, UNSIGNED16, UNSIGNED16* p)
    { if (p) *p = 0; return openads::AE_SUCCESS; }
// SAP / rddads signature: AdsGetAOF(hTable, pucFilter, *pusLen).
// pucFilter is a caller-allocated buffer; pusLen is in/out (capacity
// in, actual filter length out). We don't track per-table AOF source
// strings yet (only the evaluated bitmap), so return an empty filter
// — Harbour's ADSGETAOF treats that as "no AOF" and returns "".
UNSIGNED32 AdsGetAOF(ADSHANDLE, UNSIGNED8* pucFilter, UNSIGNED16* pusLen)
    {
        if (pusLen != nullptr) {
            if (pucFilter != nullptr && *pusLen > 0) pucFilter[0] = '\0';
            *pusLen = 0;
        }
        return openads::AE_SUCCESS;
    }
UNSIGNED32 AdsGetConnectionType(ADSHANDLE hConnect, UNSIGNED16* p) {
    if (p == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    *p = ADS_LOCAL_SERVER;
    // If the handle resolves to a remote connection, report REMOTE.
    if (get_remote_table(hConnect) != nullptr) {
        *p = ADS_REMOTE_SERVER;
        return ok();
    }
    Connection* c = lookup_connection(hConnect);
    if (c != nullptr) {
        *p = ADS_LOCAL_SERVER;
    }
    return ok();
}
UNSIGNED32 AdsGetDateFormat(UNSIGNED8* pucBuf, UNSIGNED16* pusLen) {
    copy_ace_string(g_date_format, pucBuf, pusLen);
    return openads::AE_SUCCESS;
}
UNSIGNED32 AdsGetDefault(UNSIGNED8* p, UNSIGNED16* l)
    { if (l) { if (p && *l > 0) p[0] = 0; *l = 0; } return openads::AE_SUCCESS; }
UNSIGNED32 AdsGetDeleted(UNSIGNED16* p)
    { if (p) *p = 0; return openads::AE_SUCCESS; }
// AdsGetDouble already defined elsewhere in this file.
UNSIGNED32 AdsGetEpoch(UNSIGNED16* p)
    { if (p) *p = 1900; return openads::AE_SUCCESS; }
UNSIGNED32 AdsGetErrorString(UNSIGNED32 ulErrCode, UNSIGNED8* pucBuf, UNSIGNED16* pusLen) {
    const char* text;
    switch (ulErrCode) {
        case openads::AE_PARSE_ERROR:               text = "SQL parsing error";               break;
        case openads::AE_INVALID_SQL_TOKEN:         text = "Invalid SQL token";               break;
        case openads::AE_COLUMN_NOT_FOUND:          text = "Column not found";                break;
        case openads::AE_TABLE_NOT_FOUND:           text = "Table not found";                 break;
        case openads::AE_TYPE_MISMATCH:             text = "Type mismatch";                   break;
        case openads::AE_DIVISION_BY_ZERO:          text = "Division by zero";                break;
        case openads::AE_LOGIN_FAILED:              text = "Login failed";                    break;
        case openads::AE_ACCESS_DENIED:             text = "Access denied";                   break;
        case openads::AE_INTERNAL_ERROR:            text = "Internal error";                  break;
        case openads::AE_FUNCTION_NOT_AVAILABLE:    text = "Function not available";          break;
        case openads::AE_NO_FILE_FOUND:             text = "File not found";                  break;
        case openads::AE_NO_CONNECTION:             text = "No connection";                   break;
        case openads::AE_INVALID_CONNECTION_HANDLE: text = "Invalid connection handle";       break;
        case openads::AE_LOCKED:                    text = "Record or table is locked";       break;
        case openads::AE_LOCK_FAILED:               text = "Lock failed";                     break;
        case openads::AE_TABLE_CORRUPTED:           text = "Table corrupted";                 break;
        case openads::AE_RI_VIOLATION:              text = "Referential integrity violation";  break;
        case openads::AE_UNIQUE_INDEX_VIOLATION:    text = "Duplicate key value in unique index"; break;
        case openads::AE_REMOTE_ERROR:              text = "Remote server error";              break;
        default:                                     text = "";                               break;
    }
    openads::abi::copy_to_caller(pucBuf, pusLen, std::string(text));
    return openads::AE_SUCCESS;
}
UNSIGNED32 AdsGetExact(UNSIGNED16* p)
    { if (p) *p = 0; return openads::AE_SUCCESS; }
UNSIGNED32 AdsGetFieldRaw(ADSHANDLE hTable, UNSIGNED8* pucField,
                          UNSIGNED8* pucBuf, UNSIGNED32* pulLen) {
    return AdsGetField(hTable, pucField, pucBuf, pulLen, 0);
}
UNSIGNED32 AdsGetFilter(ADSHANDLE, UNSIGNED8* p, UNSIGNED16* l)
    { if (l) { if (p && *l > 0) p[0] = 0; *l = 0; } return openads::AE_SUCCESS; }
UNSIGNED32 AdsGetHandleType(ADSHANDLE h, UNSIGNED16* p) {
    if (p == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    *p = ADS_NONE;
    auto& s = state();
    auto kind = s.registry.kind_of(h);
    switch (kind) {
        case HandleKind::Connection:
        case HandleKind::RemoteConnection:
            *p = ADS_DATABASE_CONNECTION;  break;
        case HandleKind::Table:
        case HandleKind::RemoteTable:
        case HandleKind::SqliteTable:
        case HandleKind::MssqlTable:
        case HandleKind::MariadbTable:
        case HandleKind::PostgresqlTable:
        case HandleKind::OdbcTable:
        case HandleKind::FirebirdTable:
            *p = ADS_TABLE;  break;
        case HandleKind::Statement:
            *p = ADS_STATEMENT;  break;
        default:
            *p = ADS_NONE;  break;
    }
    return ok();
}
UNSIGNED32 AdsGetIndexCondition(ADSHANDLE hIndex, UNSIGNED8* p,
                               UNSIGNED16* l) {
    if (l == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    auto& m = index_bindings();
    auto it = m.find(hIndex);
    if (it == m.end()) {
        if (p && *l > 0) p[0] = 0;
        *l = 0;
        return ok();
    }
    std::string cond;
    if (it->second.parked) {
        cond = it->second.parked->condition();
    } else if (it->second.table && it->second.table->order()
            && it->second.table->order()->index()) {
        cond = it->second.table->order()->index()->condition();
    }
    openads::abi::copy_to_caller(p, l, cond);
    return ok();
}
UNSIGNED32 AdsGetIndexFilename(ADSHANDLE hIndex, UNSIGNED16 /*usOrder*/,
                               UNSIGNED8* p, UNSIGNED16* l) {
    if (l == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    auto& m = index_bindings();
    auto it = m.find(hIndex);
    if (it == m.end()) {
        if (p && *l > 0) p[0] = 0;
        *l = 0;
        return ok();
    }
    openads::abi::copy_to_caller(p, l, it->second.path);
    return ok();
}
// 1-based position of the order `hIndex` within its table's ordinal
// sequence — the exact inverse of AdsGetIndexHandleByOrder. Harbour
// rddads' OrdNumber() / DBOI_NUMBER calls this after resolving a tag
// name to a handle (contrib/rddads/ads1.c, adsOrderInfo); a stubbed 0
// made OrdNumber() report 0 for every tag. Returns 0 (natural order)
// for an unknown handle.
UNSIGNED32 AdsGetIndexOrderByHandle(ADSHANDLE hIndex, UNSIGNED16* p) {
    if (p == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    *p = 0;
    auto& m = index_bindings();
    Table* t = nullptr;
    std::string tag;
    {
        auto it = m.find(hIndex);
        if (it == m.end()) return ok();   // unknown handle → natural order
        t   = it->second.table;
        tag = it->second.tag_name;        // copy before the helper rehashes m
    }
    if (t == nullptr) return ok();
    // Match by tag name (not handle identity) so the result is stable even
    // if more than one binding transiently exists for the same tag.
    std::vector<ADSHANDLE> ordered = ordered_index_handles_for(t);
    for (std::size_t i = 0; i < ordered.size(); ++i) {
        auto bit = m.find(ordered[i]);
        if (bit != m.end() && bit->second.tag_name == tag) {
            *p = static_cast<UNSIGNED16>(i + 1);
            break;
        }
    }
    return ok();
}
// AdsGetJulian already defined elsewhere in this file.
UNSIGNED32 AdsGetKeyLength(ADSHANDLE hIndex, UNSIGNED16* p) {
    if (p == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    *p = 0;
    auto* idx = iindex_for_handle(hIndex);
    if (idx == nullptr) return fail(openads::AE_INTERNAL_ERROR, "no index");
    *p = idx->key_length();
    return ok();
}
// 1-based position of the current record within the active order's key
// sequence (== the record number when no order is active). FWH's
// xBrowse uses this (via Harbour rddads' AdsKeyNo()) as its scrollbar
// position; a stubbed 0 left the browse unable to paint any row.
UNSIGNED32 AdsGetKeyNum(ADSHANDLE hObj, UNSIGNED16 /*usFilterOption*/,
                        UNSIGNED32* pulKeyNum) {
    if (pulKeyNum == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    *pulKeyNum = 0;
    if (auto* rt = get_remote_table(hObj)) {
        if (rt->row_valid) { *pulKeyNum = rt->current_recno; return ok(); }
        auto r = rt->conn->get_record_num(rt->id);
        if (!r) return fail(r.error());
        *pulKeyNum = r.value();
        return ok();
    }
    Table* t = get_table(hObj);
    if (t == nullptr) return fail(openads::AE_INTERNAL_ERROR, "no table");
    auto* ord = t->order();
    if (ord == nullptr || ord->index() == nullptr) {
        // natural order: key number == record number
        *pulKeyNum = t->recno();
        return ok();
    }
    // Active order: logical key number = position in key order + 1.
    std::uint32_t rn  = t->recno();
    auto*         idx = ord->index();
    // CDX: O(1) via the cached ordered-recno walk.
    if (auto* cdx = dynamic_cast<openads::drivers::cdx::CdxIndex*>(idx)) {
        std::uint32_t pos = cdx->pos_of_recno_cached(rn);
        *pulKeyNum = (pos == 0xFFFFFFFFu) ? 0u : (pos + 1u);
        return ok();
    }
    // Non-CDX: legacy O(n) walk (cursor restored).
    idx->invalidate_cursor();
    auto first = idx->seek_first();
    if (!first) { (void)t->goto_record(rn); return fail(first.error()); }
    std::uint32_t pos = 0, found = 0;
    auto cur = first.value();
    while (cur.positioned) {
        ++pos;
        if (cur.recno == rn) { found = pos; break; }
        auto nx = idx->next();
        if (!nx) { idx->invalidate_cursor(); (void)t->goto_record(rn);
                   return fail(nx.error()); }
        cur = nx.value();
    }
    idx->invalidate_cursor();
    (void)t->goto_record(rn);
    *pulKeyNum = found;            // 0 when rn isn't in the index walk
    return ok();
}
UNSIGNED32 AdsGetKeyType(ADSHANDLE hIndex, UNSIGNED16* p) {
    if (p == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    *p = ADS_STRINGKEY;
    auto* idx = iindex_for_handle(hIndex);
    if (idx == nullptr) return ok();
    switch (idx->key_encoding()) {
        case openads::drivers::KeyEncoding::Text:
            *p = ADS_STRINGKEY;  break;
        case openads::drivers::KeyEncoding::FoxNumeric:
        case openads::drivers::KeyEncoding::NtxNumeric:
            *p = ADS_DOUBLEKEY;  break;
    }
    return ok();
}
UNSIGNED32 AdsGetLastTableUpdate(ADSHANDLE hTable, UNSIGNED8* pucDate,
                                 UNSIGNED16* pusLen) {
    int y = 0, m = 0, d = 0;
    if (auto* rt = get_remote_table(hTable)) {
        auto r = rt->conn->get_last_table_update(rt->id);
        if (!r) return fail(r.error());
        std::uint32_t v = r.value();
        y = static_cast<int>((v >> 16) & 0xFFFFu);
        m = static_cast<int>((v >>  8) & 0xFFu);
        d = static_cast<int>( v        & 0xFFu);
    } else {
        Table* tbl = get_table(hTable);
        if (tbl == nullptr) return fail(openads::AE_INTERNAL_ERROR, "no table");
        auto hd = read_header_date(tbl);
        y = hd.y; m = hd.m; d = hd.d;
    }
    copy_ace_string(format_ace_date(g_date_format, y, m, d), pucDate, pusLen);
    return ok();
}
UNSIGNED32 AdsGetLogical(ADSHANDLE hTable, UNSIGNED8* pucField, UNSIGNED16* pb) {
    if (pb == nullptr) return openads::AE_INTERNAL_ERROR;
    UNSIGNED8 buf[8] = {0};
    UNSIGNED32 cap = sizeof(buf);
    UNSIGNED32 rc = AdsGetField(hTable, pucField, buf, &cap, 0);
    if (rc != openads::AE_SUCCESS) return rc;
    *pb = (cap > 0 && (buf[0] == 'T' || buf[0] == 't' ||
                        buf[0] == 'Y' || buf[0] == 'y' ||
                        buf[0] == '1')) ? 1 : 0;
    return openads::AE_SUCCESS;
}
UNSIGNED32 AdsGetMilliseconds(ADSHANDLE, UNSIGNED8*, SIGNED32* p)
    { if (p) *p = 0; return openads::AE_SUCCESS; }
UNSIGNED32 AdsGetNumActiveLinks(ADSHANDLE, UNSIGNED16* p)
    { if (p) *p = 0; return openads::AE_SUCCESS; }
UNSIGNED32 AdsGetNumLocks(ADSHANDLE, UNSIGNED16* p)
    { if (p) *p = 0; return openads::AE_SUCCESS; }
UNSIGNED32 AdsGetNumOpenTables(UNSIGNED16* p) {
    if (p == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    *p = 0;
    auto& s = state();
    std::uint16_t count = 0;
    s.registry.for_each_handle([&](Handle, HandleKind k, void*) {
        if (k == HandleKind::Table || k == HandleKind::RemoteTable
            || k == HandleKind::SqliteTable || k == HandleKind::MssqlTable
            || k == HandleKind::MariadbTable || k == HandleKind::PostgresqlTable
            || k == HandleKind::OdbcTable || k == HandleKind::FirebirdTable) {
            ++count;
        }
    });
    *p = count;
    return ok();
}
UNSIGNED32 AdsGetRecord(ADSHANDLE, UNSIGNED8*, UNSIGNED32* p)
    { if (p) *p = 0; return openads::AE_FUNCTION_NOT_AVAILABLE; }
UNSIGNED32 AdsGetRelKeyPos(ADSHANDLE h, double* p) {
    if (p == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    *p = 0.0;
#if defined(OPENADS_WITH_ODBC)
    if (auto* st = get_odbc_table(h)) {
        if (st->conn == nullptr) {
            return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
        }
        std::uint32_t rc = 0;
        if (st->rec_count_cached) {
            rc = st->cached_rec_count;
        } else {
            auto rcr = st->conn->record_count(st);
            if (!rcr) return fail(rcr.error());
            rc = rcr.value();
            st->cached_rec_count = rc;
            st->rec_count_cached = true;
        }
        std::uint32_t rn = 0;
        if (st->row_valid && st->positioned) {
            rn = st->current_recno;
        }
        if (rc <= 1 || rn == 0) { *p = 0.0; return ok(); }
        if (rn > rc) rn = rc;
        *p = static_cast<double>(rn - 1) / static_cast<double>(rc - 1);
        return ok();
    }
#endif
#if defined(OPENADS_WITH_FIREBIRD)
    if (auto* st = get_firebird_table(h)) {
        if (st->conn == nullptr) {
            return fail(openads::AE_INVALID_CONNECTION_HANDLE, "");
        }
        std::uint32_t rc = 0;
        if (st->rec_count_cached) {
            rc = st->cached_rec_count;
        } else {
            auto rcr = st->conn->record_count(st);
            if (!rcr) return fail(rcr.error());
            rc = rcr.value();
            st->cached_rec_count = rc;
            st->rec_count_cached = true;
        }
        std::uint32_t rn = 0;
        if (st->row_valid && st->positioned) {
            rn = st->current_recno;
        }
        if (rc <= 1 || rn == 0) { *p = 0.0; return ok(); }
        if (rn > rc) rn = rc;
        *p = static_cast<double>(rn - 1) / static_cast<double>(rc - 1);
        return ok();
    }
#endif
    if (auto* rt = get_remote_table(h)) {
        // M12.19 — scrollbar callers (xbrowse) hit this every paint.
        // current_recno arrives with the row trailer (M12.18) and
        // cached_rec_count survives every nav, so the hot path is
        // 0 RTT. Fall back to a wire RTT only if either piece of
        // state is cold (e.g. just-opened table before first nav).
        std::uint32_t rc = 0;
        if (rt->rec_count_cached) {
            rc = rt->cached_rec_count;
        } else {
            auto rcr = rt->conn->record_count(rt->id);
            if (!rcr) return fail(rcr.error());
            rc = static_cast<std::uint32_t>(rcr.value());
            rt->cached_rec_count = rc;
            rt->rec_count_cached = true;
        }
        std::uint32_t rn = 0;
        if (rt->row_valid) {
            rn = rt->current_recno;
        } else {
            auto rnr = rt->conn->get_record_num(rt->id);
            if (!rnr) return fail(rnr.error());
            rn = rnr.value();
        }
        if (rc <= 1 || rn == 0) { *p = 0.0; return ok(); }
        if (rn > rc) rn = rc;
        *p = static_cast<double>(rn - 1) / static_cast<double>(rc - 1);
        return ok();
    }
    Table* t = get_table(h);
    if (t == nullptr) return fail(openads::AE_INTERNAL_ERROR, "no table");
    std::uint32_t rc = t->record_count();
    std::uint32_t rn = t->recno();
    if (rc <= 1) { *p = 0.0; return ok(); }
    if (rn == 0) {
        // Phantom position (recno 0): EOF -> bottom (1.0), BOF -> top (0.0).
        // Returning 0.0 for EOF made the scrollbar snap to the TOP when a
        // held PageDown overshot the end, so the browse jumped back up and
        // would not scroll down again. Reporting EOF as bottom keeps it put.
        *p = t->eof() ? 1.0 : 0.0;
        return ok();
    }

    // Active-order path: ADS reports the cursor's relative position
    // in the *index walk*, not in raw recno space. Walk the index
    // once collecting recnos in order; the cursor's recno's index
    // in that list, divided by (total - 1), is the logical Get
    // value. Cursor position is restored afterwards so a Get probe
    // doesn't move the user-visible cursor.
    auto* ord = t->order();
    if (ord != nullptr && ord->index() != nullptr) {
        auto* idx = ord->index();
        // CDX: O(1) via the cached ordered-recno walk (built once, reused
        // across paints). The previous per-call O(n) walk made TXBrowse
        // freeze on large/filtered orders (the scrollbar hits this every
        // paint).
        if (auto* cdx =
                dynamic_cast<openads::drivers::cdx::CdxIndex*>(idx)) {
            const auto& walk = cdx->ordered_recnos_cached();
            if (walk.size() <= 1) { *p = 0.0; return ok(); }
            std::uint32_t pos = cdx->pos_of_recno_cached(rn);
            if (pos == 0xFFFFFFFFu) {
                if (rn > rc) rn = rc;
                *p = static_cast<double>(rn - 1) /
                     static_cast<double>(rc - 1);
                return ok();
            }
            *p = static_cast<double>(pos) /
                 static_cast<double>(walk.size() - 1);
            return ok();
        }
        // Non-CDX (NTX/ADI): legacy per-call O(n) walk.
        idx->invalidate_cursor();
        auto first = idx->seek_first();
        if (!first) return fail(first.error());
        std::vector<std::uint32_t> walk;
        walk.reserve(128);
        auto cur = first.value();
        while (cur.positioned) {
            walk.push_back(cur.recno);
            auto nx = idx->next();
            if (!nx) return fail(nx.error());
            cur = nx.value();
        }
        idx->invalidate_cursor();
        (void)t->goto_record(rn);
        if (walk.size() <= 1) { *p = 0.0; return ok(); }
        std::size_t pos = walk.size();
        for (std::size_t i = 0; i < walk.size(); ++i) {
            if (walk[i] == rn) { pos = i; break; }
        }
        if (pos >= walk.size()) {
            // Cursor recno not present in the index — fall back to
            // recno-based fraction so the result stays monotonic.
            if (rn > rc) rn = rc;
            *p = static_cast<double>(rn - 1) /
                 static_cast<double>(rc - 1);
            return ok();
        }
        *p = static_cast<double>(pos) /
             static_cast<double>(walk.size() - 1);
        return ok();
    }

    // No active order: relative position in raw recno space.
    if (rn > rc) rn = rc;
    *p = static_cast<double>(rn - 1) / static_cast<double>(rc - 1);
    return ok();
}
UNSIGNED32 AdsGetSearchPath(UNSIGNED8* p, UNSIGNED16* l)
    { if (l) { if (p && *l > 0) p[0] = 0; *l = 0; } return openads::AE_SUCCESS; }
UNSIGNED32 AdsGetTableAlias(ADSHANDLE, UNSIGNED8* p, UNSIGNED16* l)
    { if (l) { if (p && *l > 0) p[0] = 0; *l = 0; } return openads::AE_SUCCESS; }
UNSIGNED32 AdsGetTableCharType(ADSHANDLE, UNSIGNED16* p)
    { if (p) *p = ADS_ANSI; return openads::AE_SUCCESS; }
UNSIGNED32 AdsGetTableConType(ADSHANDLE hTable, UNSIGNED16* p) {
    if (p == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    // Delegate to AdsGetTableType which already maps file extensions
    // to ACE table-type constants (CDX/NTX/ADT).
    return AdsGetTableType(hTable, p);
}
UNSIGNED32 AdsGetTableConnection(ADSHANDLE, ADSHANDLE* p)
    { if (p) *p = 0; return openads::AE_SUCCESS; }
UNSIGNED32 AdsIsConnectionAlive(ADSHANDLE, UNSIGNED16* p)
    { if (p) *p = 1; return openads::AE_SUCCESS; }
UNSIGNED32 AdsIsEmpty(ADSHANDLE, UNSIGNED8*, UNSIGNED16* p)
    { if (p) *p = 0; return openads::AE_SUCCESS; }
UNSIGNED32 AdsIsExprValid(ADSHANDLE, UNSIGNED8*, UNSIGNED16* p)
    { if (p) *p = 1; return openads::AE_SUCCESS; }
// AdsIsFound already defined elsewhere in this file.
UNSIGNED32 AdsIsIndexCustom(ADSHANDLE, UNSIGNED16* p)
    { if (p) *p = 0; return openads::AE_SUCCESS; }
UNSIGNED32 AdsIsIndexDescending(ADSHANDLE hIndex, UNSIGNED16* p) {
    if (p == nullptr) return openads::AE_INTERNAL_ERROR;
    *p = 0;
    auto* idx = iindex_for_handle(hIndex);
    if (idx == nullptr) return openads::AE_INTERNAL_ERROR;
    *p = idx->descending() ? 1 : 0;
    return openads::AE_SUCCESS;
}
UNSIGNED32 AdsIsIndexUnique(ADSHANDLE hIndex, UNSIGNED16* p) {
    if (p == nullptr) return openads::AE_INTERNAL_ERROR;
    *p = 0;
    auto* idx = iindex_for_handle(hIndex);
    if (idx == nullptr) return openads::AE_INTERNAL_ERROR;
    *p = idx->unique() ? 1 : 0;
    return openads::AE_SUCCESS;
}
UNSIGNED32 AdsIsNull(ADSHANDLE hTable, UNSIGNED8* pucField,
                     UNSIGNED16* pbNull) {
    if (pbNull == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    *pbNull = 0;
    if (auto* rt = get_remote_table(hTable)) {
        // Remote tables: nullability isn't exposed over the wire yet;
        // conservatively report "not null" (same as legacy ACE).
        return ok();
    }
    Table* t = get_table(hTable);
    if (t == nullptr) return fail(openads::AE_INTERNAL_ERROR, "no table");
    std::uint16_t idx = 0;
    if (!resolve_field_index_h(hTable, t, pucField, &idx)) {
        return fail(openads::AE_COLUMN_NOT_FOUND, "");
    }
    *pbNull = t->is_field_null(idx) ? 1 : 0;
    return ok();
}
UNSIGNED32 AdsIsRecordInAOF(ADSHANDLE, UNSIGNED32, UNSIGNED16* p)
    { if (p) *p = 1; return openads::AE_SUCCESS; }
// ulRecord == 0 means "the current record" (ACE convention). Reports
// whether *this* connection holds an exclusive lock on it. Remote
// handles aren't introspected yet — they report 0.
UNSIGNED32 AdsIsRecordLocked(ADSHANDLE hTable, UNSIGNED32 ulRecord,
                             UNSIGNED16* pbLocked) {
    if (pbLocked == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    *pbLocked = 0;
    if (get_remote_table(hTable) != nullptr) return ok();
    Table* t = get_table(hTable);
    if (t == nullptr) return fail(openads::AE_INTERNAL_ERROR, "no table");
    std::uint32_t rec = (ulRecord == 0) ? t->recno() : ulRecord;
    for (std::uint32_t held : t->held_record_locks()) {
        if (held == rec) { *pbLocked = 1; break; }
    }
    return ok();
}
UNSIGNED32 AdsIsServerLoaded(UNSIGNED8*, UNSIGNED16* p)
    { if (p) *p = 1; return openads::AE_SUCCESS; }
UNSIGNED32 AdsIsTableLocked(ADSHANDLE, UNSIGNED16* p)
    { if (p) *p = 0; return openads::AE_SUCCESS; }
UNSIGNED32 AdsRefreshAOF(ADSHANDLE) { ADS_STUB(openads::AE_SUCCESS); }
UNSIGNED32 AdsRegisterCallbackFunction(void*) { ADS_STUB(openads::AE_SUCCESS); }
UNSIGNED32 AdsRegisterProgressCallback(void*) { ADS_STUB(openads::AE_SUCCESS); }
UNSIGNED32 AdsSetDateFormat(UNSIGNED8* pucFormat) {
    if (pucFormat != nullptr && pucFormat[0] != 0)
        g_date_format.assign(reinterpret_cast<const char*>(pucFormat));
    return openads::AE_SUCCESS;
}
UNSIGNED32 AdsSetDecimals(UNSIGNED16) { ADS_STUB(openads::AE_SUCCESS); }
UNSIGNED32 AdsSetDefault(UNSIGNED8*) { ADS_STUB(openads::AE_SUCCESS); }
UNSIGNED32 AdsSetEpoch(UNSIGNED16) { ADS_STUB(openads::AE_SUCCESS); }
UNSIGNED32 AdsSetExact(UNSIGNED16) { ADS_STUB(openads::AE_SUCCESS); }
UNSIGNED32 AdsSetFilter(ADSHANDLE, UNSIGNED8*) { ADS_STUB(openads::AE_SUCCESS); }
// AdsSetJulian, AdsSetLongLong already defined elsewhere in this file.
UNSIGNED32 AdsSetMilliseconds(ADSHANDLE, UNSIGNED8*, SIGNED32) { ADS_STUB(openads::AE_FUNCTION_NOT_AVAILABLE); }
UNSIGNED32 AdsSetRecord(ADSHANDLE, UNSIGNED8*, UNSIGNED32) { ADS_STUB(openads::AE_FUNCTION_NOT_AVAILABLE); }
UNSIGNED32 AdsSetRelKeyPos(ADSHANDLE h, double pos) {
    Table* t = get_table(h);
    if (t == nullptr) return fail(openads::AE_INTERNAL_ERROR, "no table");
    std::uint32_t rc = t->record_count();
    if (rc == 0) return ok();
    if (pos < 0.0) pos = 0.0;
    if (pos > 1.0) pos = 1.0;

    // Active-order path: ADS positions the cursor at fraction `pos`
    // through the *index walk*, not through raw recno space. Walk
    // the index once to collect every recno in order, then jump to
    // walk[round(pos * (total - 1))]. Mirrors the Get path above so
    // a round-trip Get-then-Set lands on the same key.
    auto* ord = t->order();
    if (ord != nullptr && ord->index() != nullptr) {
        auto* idx = ord->index();
        const std::vector<std::uint32_t>* walkp = nullptr;
        std::vector<std::uint32_t> walk_local;
        if (auto* cdx =
                dynamic_cast<openads::drivers::cdx::CdxIndex*>(idx)) {
            walkp = &cdx->ordered_recnos_cached();   // O(1) after first build
        } else {
            idx->invalidate_cursor();
            auto first = idx->seek_first();
            if (!first) return fail(first.error());
            auto cur = first.value();
            while (cur.positioned) {
                walk_local.push_back(cur.recno);
                auto nx = idx->next();
                if (!nx) return fail(nx.error());
                cur = nx.value();
            }
            idx->invalidate_cursor();
            walkp = &walk_local;
        }
        const auto& walk = *walkp;
        if (walk.empty()) return ok();
        std::size_t target = static_cast<std::size_t>(
            pos * static_cast<double>(walk.size() - 1) + 0.5);
        if (target >= walk.size()) target = walk.size() - 1;
        auto r = t->goto_record(walk[target]);
        if (!r) return fail(r.error());
        return ok();
    }

    // No active order: position by raw recno fraction.
    std::uint32_t rn = static_cast<std::uint32_t>(
        pos * static_cast<double>(rc - 1) + 0.5) + 1;
    if (rn < 1) rn = 1;
    if (rn > rc) rn = rc;
    auto r = t->goto_record(rn);
    if (!r) return fail(r.error());
    return ok();
}
// Not yet implemented — return AE_FUNCTION_NOT_AVAILABLE so callers know to
// use a workaround rather than silently getting no relation following.
UNSIGNED32 AdsSetRelation(ADSHANDLE, ADSHANDLE, UNSIGNED8*) { ADS_STUB(openads::AE_FUNCTION_NOT_AVAILABLE); }
UNSIGNED32 AdsSetScopedRelation(ADSHANDLE, ADSHANDLE, UNSIGNED8*) { ADS_STUB(openads::AE_SUCCESS); }
UNSIGNED32 AdsSetSearchPath(UNSIGNED8*) { ADS_STUB(openads::AE_SUCCESS); }
UNSIGNED32 AdsSetServerType(UNSIGNED16) { ADS_STUB(openads::AE_SUCCESS); }
UNSIGNED32 AdsShowDeleted(UNSIGNED16 us) {
    openads::engine::set_show_deleted(us != 0);
    return openads::AE_SUCCESS;
}
UNSIGNED32 AdsShowError(UNSIGNED8*) { ADS_STUB(openads::AE_SUCCESS); }
UNSIGNED32 AdsStmtSetTableLockType(ADSHANDLE h, UNSIGNED16 us) {
    SqlStatement* st = stmt_lookup(h);
    if (st == nullptr) return fail(openads::AE_INTERNAL_ERROR, "unknown stmt");
    st->lock_type = us; return ok();
}
UNSIGNED32 AdsStmtSetTablePassword(ADSHANDLE h, UNSIGNED8* pTable, UNSIGNED8* pPwd) {
    SqlStatement* st = stmt_lookup(h);
    if (st == nullptr) return fail(openads::AE_INTERNAL_ERROR, "unknown stmt");
    st->passwords.emplace_back(openads::abi::to_internal(pTable, 0),
                               openads::abi::to_internal(pPwd, 0));
    return ok();
}
UNSIGNED32 AdsStmtSetTableReadOnly(ADSHANDLE h, UNSIGNED16 us) {
    SqlStatement* st = stmt_lookup(h);
    if (st == nullptr) return fail(openads::AE_INTERNAL_ERROR, "unknown stmt");
    st->read_only = us; return ok();
}
UNSIGNED32 AdsStmtSetTableType(ADSHANDLE h, UNSIGNED16 us) {
    SqlStatement* st = stmt_lookup(h);
    if (st == nullptr) return fail(openads::AE_INTERNAL_ERROR, "unknown stmt");
    st->table_type = us; return ok();
}
UNSIGNED32 AdsTestLogin(UNSIGNED8*, UNSIGNED16, UNSIGNED8*, UNSIGNED8*, UNSIGNED32)
    { ADS_STUB(openads::AE_SUCCESS); }
UNSIGNED32 AdsTestRecLocks(ADSHANDLE) { ADS_STUB(openads::AE_SUCCESS); }
UNSIGNED32 AdsWriteAllRecords(void) { return openads::AE_SUCCESS; }

// This file is largely one big `extern "C"` block; the mgmt helpers
// below return std::string / Result<> / a small struct, so they need
// C++ linkage.
extern "C++" {
namespace {

// A management handle resolves to one of these. Local collects an
// in-process snapshot; Remote ships a MgRequest to the server.
struct MgBackend {
    bool          remote = false;
    std::string   host;       // remote only
    std::uint16_t port = 0;   // remote only
};

// Registry of open mgmt handles. ADSHANDLE values for mgmt start at a
// high base so they never collide with table / connection handles.
std::mutex                               g_mg_mu;
std::unordered_map<ADSHANDLE, MgBackend> g_mg_handles;
ADSHANDLE                                g_mg_next = 0x4D670001;  // 'Mg'

// Builds a MgSnapshot for whichever backend the handle names.
openads::util::Result<openads::mgmt::MgSnapshot>
fetch_mg_snapshot(const MgBackend& be) {
    using openads::util::Error;
    if (!be.remote) {
        // Local mode: report this process by enumerating the ABI
        // handle registry for real open-connection / open-table
        // counts. The per-table list stays empty in local mode —
        // counts are what the management surface needs; resolving
        // each handle to a table name would require engine::Table
        // introspection that is out of scope here.
        openads::mgmt::MgSnapshot snap;
        snap.server_type = 0;   // 0 = local
        std::uint32_t conns = 0, tbls = 0;
        state().registry.for_each_handle(
            [&](Handle, HandleKind k, void*) {
                if (k == HandleKind::Connection ||
                    k == HandleKind::RemoteConnection)
                    ++conns;
                else if (k == HandleKind::Table ||
                         k == HandleKind::RemoteTable)
                    ++tbls;
            });
        snap.connections = conns;
        snap.tables      = tbls;
        snap.workareas   = tbls;
        // The calling process always counts as one "user".
        snap.users = 1;
        openads::mgmt::MgUser u;
        u.name    = "(local)";
        u.conn_no = 1;
        snap.user_list.push_back(u);
        snap.rss_bytes = openads::platform::process_rss_bytes();
        openads::mgmt::capture_mg_stats(
            snap, openads::mgmt::process_mg_stats());
        return snap;
    }
    // Remote mode: open a socket, ship one MgRequest, read the reply.
    openads::network::network_init();
    auto sock = openads::network::connect_tcp(be.host, be.port);
    if (!sock.has_value())
        return Error{1, 0, "mg connect failed", ""};
    openads::network::Socket s = sock.value();

    openads::network::Frame req;
    req.opcode = openads::network::Opcode::MgRequest;
    std::string body = openads::network::encode_mg_request(
        openads::network::MgRequestKind::Snapshot, 0);
    req.payload.assign(body.begin(), body.end());

    auto wr = openads::network::write_frame(s, req);
    if (!wr.has_value()) {
        openads::network::sock_close(s);
        return Error{1, 0, "mg request send failed", ""};
    }
    auto reply = openads::network::read_frame(s);
    openads::network::sock_close(s);
    if (!reply.has_value())
        return Error{1, 0, "mg reply read failed", ""};
    if (reply.value().opcode != openads::network::Opcode::MgReplyAck)
        return Error{1, 0, "mg request rejected", ""};
    std::string payload(reply.value().payload.begin(),
                         reply.value().payload.end());
    return openads::network::decode_mg_snapshot(payload);
}

}  // namespace
}  // extern "C++"

// Mgmt accessor helpers. These return C++ types (Result<>, MgCollector,
// templates), so they live in their own C++-linkage block.
extern "C++" {
namespace {

// Resolve a mgmt handle to its backend; nullptr if unknown.
const MgBackend* lookup_mg(ADSHANDLE h) {
    std::lock_guard<std::mutex> g(g_mg_mu);
    auto it = g_mg_handles.find(h);
    return it == g_mg_handles.end() ? nullptr : &it->second;
}

// Build a MgCollector for a handle, or return an error.
openads::util::Result<openads::mgmt::MgCollector>
mg_collector_for(ADSHANDLE h) {
    const MgBackend* be = lookup_mg(h);
    if (be == nullptr)
        return openads::util::Error{
            static_cast<std::int32_t>(
                openads::AE_INVALID_CONNECTION_HANDLE),
            0, "invalid mgmt handle", ""};
    auto snap = fetch_mg_snapshot(*be);
    if (!snap.has_value())
        return openads::util::Error{
            static_cast<std::int32_t>(openads::AE_NO_CONNECTION),
            0, "mg telemetry fetch failed", ""};
    return openads::mgmt::MgCollector(snap.value());
}

// Copy a POD struct into the caller's buffer, clamped to *pusLen, and
// write back the real struct size.
// Raw struct memcpy is safe across THIS boundary — unlike the wire,
// where a 32-bit client and 64-bit server may disagree on layout.
// The caller (rddads / Harbour) and this DLL both consume the same
// include/openads/ace.h ADS_MGMT_* definitions, so the layouts are
// identical by construction. mg_wire handles the cross-ABI case.
template <typename T>
UNSIGNED32 emit_mg_struct(const T& src, void* pBuf, UNSIGNED16* pusLen) {
    if (pusLen == nullptr) return openads::AE_INTERNAL_ERROR;
    UNSIGNED16 cap = *pusLen;
    UNSIGNED16 n = static_cast<UNSIGNED16>(
        sizeof(T) < cap ? sizeof(T) : cap);
    if (pBuf != nullptr && n > 0) std::memcpy(pBuf, &src, n);
    *pusLen = static_cast<UNSIGNED16>(sizeof(T));
    return openads::AE_SUCCESS;
}

// Copy a vector of POD structs into the caller's array buffer.
// *pusCount in: capacity (#elements); out: actual element count.
template <typename T>
UNSIGNED32 emit_mg_array(const std::vector<T>& src, void* pBuf,
                         UNSIGNED16* pusCount, UNSIGNED16* pusSize) {
    if (pusCount == nullptr) return openads::AE_INTERNAL_ERROR;
    UNSIGNED16 cap = *pusCount;
    UNSIGNED16 n = static_cast<UNSIGNED16>(
        src.size() < cap ? src.size() : cap);
    if (pBuf != nullptr && n > 0)
        std::memcpy(pBuf, src.data(),
                    static_cast<std::size_t>(n) * sizeof(T));
    *pusCount = static_cast<UNSIGNED16>(src.size());
    if (pusSize != nullptr) *pusSize = static_cast<UNSIGNED16>(sizeof(T));
    return openads::AE_SUCCESS;
}

// Ship a fire-and-forget MgRequest mutator to a remote server.
bool send_mg_mutator(const MgBackend& be,
                     openads::network::MgRequestKind kind,
                     std::uint16_t arg) {
    openads::network::network_init();
    auto sock = openads::network::connect_tcp(be.host, be.port);
    if (!sock.has_value()) return false;
    openads::network::Socket s = sock.value();
    openads::network::Frame req;
    req.opcode = openads::network::Opcode::MgRequest;
    std::string body = openads::network::encode_mg_request(kind, arg);
    req.payload.assign(body.begin(), body.end());
    bool ok = openads::network::write_frame(s, req).has_value();
    if (ok) (void)openads::network::read_frame(s);  // drain the ack
    openads::network::sock_close(s);
    return ok;
}

}  // namespace
}  // extern "C++"

// AdsMg* stubs are implemented elsewhere. Kept tests/unit/abi_mgmt_test.cpp's
// existing pattern: zero-fill caller's buffer, return AE_SUCCESS so apps
// proceed without special-casing local-mode mgmt absence.

// AdsMgConnect — pucServer selects local vs. remote. An empty or
// "local" server string yields a local-process backend; anything of
// the form "host" or "host:port" yields a remote backend (default
// port 16262, the OpenADS server port).
UNSIGNED32 AdsMgConnect(UNSIGNED8* pucServer, UNSIGNED8* /*pucUser*/,
                        UNSIGNED8* /*pucPwd*/, ADSHANDLE* phMgmt) {
    if (phMgmt == nullptr) return openads::AE_INTERNAL_ERROR;

    MgBackend be;
    std::string srv = pucServer
        ? reinterpret_cast<const char*>(pucServer) : "";
    // Strip leading / trailing UNC slashes ("\\\\host\\").
    while (!srv.empty() && (srv.front() == '\\' || srv.front() == '/'))
        srv.erase(srv.begin());
    while (!srv.empty() && (srv.back() == '\\' || srv.back() == '/'))
        srv.pop_back();

    // Decide local vs. remote. A string is a REMOTE target only when
    // it is "host:port" with a non-empty, all-digit port. Everything
    // else — empty, "local", a drive path like "C:" or "C:\data",
    // a bare name — is a local-mode backend. (rddads' manage.prg
    // passes "C:" for local management; that must not be mistaken
    // for a host named "C".)
    be.remote = false;
    auto colon = srv.rfind(':');
    if (colon != std::string::npos && colon > 0 &&
        colon + 1 < srv.size()) {
        std::string portstr = srv.substr(colon + 1);
        bool all_digits = !portstr.empty();
        for (char ch : portstr)
            if (ch < '0' || ch > '9') { all_digits = false; break; }
        if (all_digits) {
            be.remote = true;
            be.host   = srv.substr(0, colon);
            be.port   = static_cast<std::uint16_t>(
                std::strtoul(portstr.c_str(), nullptr, 10));
        }
    }

    if (be.remote) {
        // Validate the server is reachable up front with a MgConnect
        // handshake, so a down server fails here (AE_NO_CONNECTION)
        // rather than later with a misleading handle error.
        openads::network::network_init();
        auto probe = openads::network::connect_tcp(be.host, be.port);
        if (!probe.has_value()) return openads::AE_NO_CONNECTION;
        openads::network::Socket ps = probe.value();
        openads::network::Frame hello;
        hello.opcode = openads::network::Opcode::MgConnect;
        if (!openads::network::write_frame(ps, hello).has_value()) {
            openads::network::sock_close(ps);
            return openads::AE_NO_CONNECTION;
        }
        auto hack = openads::network::read_frame(ps);
        openads::network::sock_close(ps);
        if (!hack.has_value() ||
            hack.value().opcode != openads::network::Opcode::MgConnectAck)
            return openads::AE_NO_CONNECTION;
    }

    std::lock_guard<std::mutex> g(g_mg_mu);
    ADSHANDLE h = g_mg_next++;
    g_mg_handles.emplace(h, std::move(be));
    *phMgmt = h;
    return openads::AE_SUCCESS;
}

UNSIGNED32 AdsMgDisconnect(ADSHANDLE hMgmt) {
    std::lock_guard<std::mutex> g(g_mg_mu);
    g_mg_handles.erase(hMgmt);
    return openads::AE_SUCCESS;
}
UNSIGNED32 AdsMgGetActivityInfo(ADSHANDLE h, void* p, UNSIGNED16* l) {
    auto c = mg_collector_for(h);
    if (!c.has_value()) return static_cast<UNSIGNED32>(c.error().code);
    return emit_mg_struct(c.value().activity_info(), p, l);
}
UNSIGNED32 AdsMgGetCommStats(ADSHANDLE h, void* p, UNSIGNED16* l) {
    auto c = mg_collector_for(h);
    if (!c.has_value()) return static_cast<UNSIGNED32>(c.error().code);
    return emit_mg_struct(c.value().comm_stats(), p, l);
}
UNSIGNED32 AdsMgGetConfigInfo(ADSHANDLE h, void* pv, UNSIGNED16* lv,
                               void* pm, UNSIGNED16* lm) {
    auto c = mg_collector_for(h);
    if (!c.has_value()) return static_cast<UNSIGNED32>(c.error().code);
    UNSIGNED32 rc = emit_mg_struct(c.value().config_params(), pv, lv);
    if (rc != openads::AE_SUCCESS) return rc;
    return emit_mg_struct(c.value().config_memory(), pm, lm);
}
UNSIGNED32 AdsMgGetInstallInfo(ADSHANDLE h, void* p, UNSIGNED16* l) {
    auto c = mg_collector_for(h);
    if (!c.has_value()) return static_cast<UNSIGNED32>(c.error().code);
    return emit_mg_struct(c.value().install_info(), p, l);
}
UNSIGNED32 AdsMgGetLockOwner(ADSHANDLE h, UNSIGNED8* /*t*/, UNSIGNED32 ulRec,
                              void* p, UNSIGNED16* l, UNSIGNED16* lt) {
    auto col = mg_collector_for(h);
    if (!col.has_value()) return static_cast<UNSIGNED32>(col.error().code);
    if (lt) *lt = ADS_MGMT_RECORD_LOCK;
    return emit_mg_struct(col.value().lock_owner(ulRec), p, l);
}
UNSIGNED32 AdsMgGetLocks(ADSHANDLE h, UNSIGNED8* /*f*/, UNSIGNED8* /*t*/,
                          UNSIGNED16 /*o*/, void* p, UNSIGNED16* c,
                          UNSIGNED16* sz) {
    auto col = mg_collector_for(h);
    if (!col.has_value()) return static_cast<UNSIGNED32>(col.error().code);
    return emit_mg_array(col.value().locks(), p, c, sz);
}
UNSIGNED32 AdsMgGetOpenIndexes(ADSHANDLE h, UNSIGNED8* /*f*/, UNSIGNED8* /*t*/,
                                UNSIGNED16 /*o*/, void* p, UNSIGNED16* c,
                                UNSIGNED16* sz) {
    auto col = mg_collector_for(h);
    if (!col.has_value()) return static_cast<UNSIGNED32>(col.error().code);
    return emit_mg_array(col.value().open_indexes(), p, c, sz);
}
UNSIGNED32 AdsMgGetOpenTables(ADSHANDLE h, UNSIGNED8* /*f*/, UNSIGNED16 /*o*/,
                               void* p, UNSIGNED16* c, UNSIGNED16* sz) {
    auto col = mg_collector_for(h);
    if (!col.has_value()) return static_cast<UNSIGNED32>(col.error().code);
    return emit_mg_array(col.value().open_tables(), p, c, sz);
}
UNSIGNED32 AdsMgGetOpenTables2(ADSHANDLE h, UNSIGNED8* /*f*/, UNSIGNED16 /*o*/,
                                void* p, UNSIGNED16* c, UNSIGNED16* sz) {
    auto col = mg_collector_for(h);
    if (!col.has_value()) return static_cast<UNSIGNED32>(col.error().code);
    return emit_mg_array(col.value().open_tables(), p, c, sz);
}
UNSIGNED32 AdsMgGetServerType(ADSHANDLE h, UNSIGNED16* p) {
    auto c = mg_collector_for(h);
    if (!c.has_value()) return static_cast<UNSIGNED32>(c.error().code);
    if (p) *p = c.value().server_type();
    return openads::AE_SUCCESS;
}
UNSIGNED32 AdsMgGetUserNames(ADSHANDLE h, UNSIGNED8* /*pucFile*/, void* p,
                              UNSIGNED16* c, UNSIGNED16* sz) {
    auto col = mg_collector_for(h);
    if (!col.has_value()) return static_cast<UNSIGNED32>(col.error().code);
    return emit_mg_array(col.value().user_names(), p, c, sz);
}
UNSIGNED32 AdsMgGetWorkerThreadActivity(ADSHANDLE h, void* p, UNSIGNED16* c,
                                         UNSIGNED16* sz) {
    auto col = mg_collector_for(h);
    if (!col.has_value()) return static_cast<UNSIGNED32>(col.error().code);
    return emit_mg_array(col.value().worker_thread_activity(), p, c, sz);
}
UNSIGNED32 AdsMgKillUser(ADSHANDLE h, UNSIGNED8* /*pucUser*/,
                         UNSIGNED16 usConnNo) {
    const MgBackend* be = lookup_mg(h);
    if (be == nullptr) return openads::AE_INVALID_CONNECTION_HANDLE;
    if (!be->remote) return openads::AE_SUCCESS;   // no-op local
    return send_mg_mutator(*be,
               openads::network::MgRequestKind::KillUser, usConnNo)
        ? openads::AE_SUCCESS : openads::AE_NO_CONNECTION;
}
UNSIGNED32 AdsMgResetCommStats(ADSHANDLE h) {
    const MgBackend* be = lookup_mg(h);
    if (be == nullptr) return openads::AE_INVALID_CONNECTION_HANDLE;
    if (!be->remote) {
        openads::mgmt::process_mg_stats().reset_comm();
        return openads::AE_SUCCESS;
    }
    return send_mg_mutator(*be,
               openads::network::MgRequestKind::ResetCommStats, 0)
        ? openads::AE_SUCCESS : openads::AE_NO_CONNECTION;
}

// All AdsDD* helpers (AddIndexFile, AddUserToGroup, CreateLink,
// CreateRefIntegrity, CreateUser, DeleteUser, DropLink,
// Get/SetDatabaseProperty, GetUserProperty, ModifyLink,
// RemoveIndexFile, RemoveRefIntegrity, RemoveUserFromGroup) are
// already defined earlier in this file.

// ---------------------------------------------------------------------------
// M12.22 — versioned ACE overloads the X# RDD (xsharp.eu) binds by name.
// Most are thin forwards to the base signature already implemented above,
// dropping the parameters newer ACE builds added (charset/collation tags,
// page sizes, RI error strings). The handful with no OpenADS base get a
// minimal implementation. Parameter lists mirror XSharp.Rdd/ACE/ACE.prg.
// ---------------------------------------------------------------------------

UNSIGNED32 AdsConnect26(UNSIGNED8* pucServer, UNSIGNED16 usServerType,
                        ADSHANDLE* phConnect) {
    return AdsConnect60(pucServer, usServerType, nullptr, nullptr, 0,
                        phConnect);
}

UNSIGNED32 AdsCreateTable71(ADSHANDLE hConnect, UNSIGNED8* pucName,
                            UNSIGNED8* pucAlias, UNSIGNED16 usTableType,
                            UNSIGNED16 usCharType, UNSIGNED16 usLockType,
                            UNSIGNED16 usCheckRights, UNSIGNED16 usMemoSize,
                            UNSIGNED8* pucFields, UNSIGNED32 /*ulOptions*/,
                            ADSHANDLE* phTable) {
    return AdsCreateTable(hConnect, pucName, pucAlias, usTableType, usCharType,
                          usLockType, usCheckRights, usMemoSize, pucFields,
                          phTable);
}

UNSIGNED32 AdsCreateTable90(ADSHANDLE hConnect, UNSIGNED8* pucName,
                            UNSIGNED8* pucAlias, UNSIGNED16 usTableType,
                            UNSIGNED16 usCharType, UNSIGNED16 usLockType,
                            UNSIGNED16 usCheckRights, UNSIGNED16 usMemoSize,
                            UNSIGNED8* pucFields, UNSIGNED32 /*ulOptions*/,
                            UNSIGNED8* /*pucCollation*/, ADSHANDLE* phTable) {
    return AdsCreateTable(hConnect, pucName, pucAlias, usTableType, usCharType,
                          usLockType, usCheckRights, usMemoSize, pucFields,
                          phTable);
}

UNSIGNED32 AdsOpenTable90(ADSHANDLE hConnect, UNSIGNED8* pucName,
                          UNSIGNED8* pucAlias, UNSIGNED16 usTableType,
                          UNSIGNED16 usCharType, UNSIGNED16 usLockType,
                          UNSIGNED16 usCheckRights, UNSIGNED32 ulOptions,
                          UNSIGNED8* /*pucCollation*/, ADSHANDLE* phTable) {
    return AdsOpenTable(hConnect, pucName, pucAlias, usTableType, usCharType,
                        usLockType, usCheckRights,
                        static_cast<UNSIGNED16>(ulOptions), phTable);
}

UNSIGNED32 AdsCreateIndex90(ADSHANDLE hObj, UNSIGNED8* pucFileName,
                            UNSIGNED8* pucTag, UNSIGNED8* pucExpr,
                            UNSIGNED8* pucCondition, UNSIGNED8* pucWhile,
                            UNSIGNED32 ulOptions, UNSIGNED32 ulPageSize,
                            UNSIGNED8* /*pucCollation*/, ADSHANDLE* phIndex) {
    return AdsCreateIndex61(hObj, pucFileName, pucTag, pucExpr, pucCondition,
                            pucWhile, ulOptions,
                            static_cast<UNSIGNED16>(ulPageSize), phIndex);
}

UNSIGNED32 AdsDDAddTable90(ADSHANDLE hConnect, UNSIGNED8* pucAlias,
                           UNSIGNED8* pucTablePath, UNSIGNED16 usTableType,
                           UNSIGNED16 usCharType, UNSIGNED8* pucIndexPath,
                           UNSIGNED8* pucComment, UNSIGNED8* /*pucCollation*/) {
    return AdsDDAddTable(hConnect, pucAlias, pucTablePath, usTableType,
                         usCharType, pucIndexPath, pucComment);
}

UNSIGNED32 AdsDDCreateRefIntegrity62(ADSHANDLE hConnect, UNSIGNED8* pucName,
                                     UNSIGNED8* pucFail, UNSIGNED8* pucParent,
                                     UNSIGNED8* pucParentTag, UNSIGNED8* pucChild,
                                     UNSIGNED8* pucChildTag, UNSIGNED16 usUpdate,
                                     UNSIGNED16 usDelete,
                                     UNSIGNED8* /*pucNoPrimaryError*/,
                                     UNSIGNED8* /*pucCascadeError*/) {
    return AdsDDCreateRefIntegrity(hConnect, pucName, pucFail, pucParent,
                                   pucParentTag, pucChild, pucChildTag,
                                   usUpdate, usDelete);
}

UNSIGNED32 AdsFindFirstTable62(ADSHANDLE hConnect, UNSIGNED8* pucFileMask,
                               UNSIGNED8* pucFirstDD, UNSIGNED16* pusDDLen,
                               UNSIGNED8* pucFirstFile, UNSIGNED16* pusFileLen,
                               ADSHANDLE* phFind) {
    // OpenADS doesn't track a data-dictionary name alongside the file
    // name, so report an empty DD name and forward to the base API.
    if (pusDDLen) {
        if (pucFirstDD && *pusDDLen > 0) pucFirstDD[0] = 0;
        *pusDDLen = 0;
    }
    return AdsFindFirstTable(hConnect, pucFileMask, pucFirstFile, pusFileLen,
                             phFind);
}

UNSIGNED32 AdsFindNextTable62(ADSHANDLE hConnect, ADSHANDLE hFind,
                              UNSIGNED8* pucDDName, UNSIGNED16* pusDDLen,
                              UNSIGNED8* pucFileName, UNSIGNED16* pusFileLen) {
    if (pusDDLen) {
        if (pucDDName && *pusDDLen > 0) pucDDName[0] = 0;
        *pusDDLen = 0;
    }
    return AdsFindNextTable(hConnect, hFind, pucFileName, pusFileLen);
}

UNSIGNED32 AdsGetDateFormat60(ADSHANDLE /*hConnect*/, UNSIGNED8* pucBuf,
                              UNSIGNED16* pusLen) {
    return AdsGetDateFormat(pucBuf, pusLen);
}

UNSIGNED32 AdsGetExact22(ADSHANDLE /*hObj*/, UNSIGNED16* pbExact) {
    return AdsGetExact(pbExact);
}

UNSIGNED32 AdsReindex61(ADSHANDLE hObject, UNSIGNED32 /*ulPageSize*/) {
    return AdsReindex(hObject);
}

UNSIGNED32 AdsRestructureTable90(ADSHANDLE hConnect, UNSIGNED8* pucTableName,
                                 UNSIGNED8* /*pucPassword*/,
                                 UNSIGNED16 usTableType, UNSIGNED16 usCharType,
                                 UNSIGNED16 usLockType, UNSIGNED16 usCheckRights,
                                 UNSIGNED8* pucAddFields,
                                 UNSIGNED8* pucDeleteFields,
                                 UNSIGNED8* pucChangeFields,
                                 UNSIGNED8* /*pucCollation*/) {
    return AdsRestructureTable(hConnect, pucTableName, nullptr, usTableType,
                               usCharType, usLockType, usCheckRights,
                               pucAddFields, pucDeleteFields, pucChangeFields);
}

// --- no OpenADS base function: minimal implementations ---

// X# calls these on row-edit cancel / connection-property tuning.
// OpenADS has no deferred-write row buffer and treats ACE properties
// as no-ops, so acknowledge success rather than break the caller flow.
UNSIGNED32 AdsCancelUpdate90(ADSHANDLE /*hTable*/, UNSIGNED32 /*ulOptions*/) {
    return ok();
}
UNSIGNED32 AdsSetProperty90(ADSHANDLE /*hObj*/, UNSIGNED32 /*ulOperation*/,
                            UNSIGNED64* /*puqValue*/) {
    return ok();
}

// OpenADS keys connections/tables by handle, not by path/name, so
// report "not found" — X# then opens a fresh connection/table.
UNSIGNED32 AdsFindConnection25(UNSIGNED8* /*pucFullPath*/,
                               ADSHANDLE* phConnect) {
    if (phConnect) *phConnect = 0;
    return fail(openads::AE_NO_CONNECTION, "no connection for path");
}
UNSIGNED32 AdsGetTableHandle25(ADSHANDLE /*hConnect*/, UNSIGNED8* /*pucName*/,
                               ADSHANDLE* phTable) {
    if (phTable) *phTable = 0;
    return fail(openads::AE_TABLE_NOT_FOUND, "no open table for name");
}

// The SAP "60" bookmark API hands back an opaque blob the app later
// replays. OpenADS encodes it as the 4-byte little-endian recno —
// stable for the table's lifetime, enough for navigate-and-return.
UNSIGNED32 AdsGetBookmark60(ADSHANDLE hObj, UNSIGNED8* pucBookmark,
                            UNSIGNED32* pulLength) {
    if (pulLength == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    if (pucBookmark == nullptr || *pulLength < 4) {
        *pulLength = 4;
        return fail(openads::AE_INTERNAL_ERROR, "bookmark buffer too small");
    }
    UNSIGNED32 recno = 0;
    UNSIGNED32 rc = AdsGetRecordNum(hObj, 0, &recno);
    if (rc != openads::AE_SUCCESS) return rc;
    pucBookmark[0] = static_cast<UNSIGNED8>(recno & 0xFF);
    pucBookmark[1] = static_cast<UNSIGNED8>((recno >> 8) & 0xFF);
    pucBookmark[2] = static_cast<UNSIGNED8>((recno >> 16) & 0xFF);
    pucBookmark[3] = static_cast<UNSIGNED8>((recno >> 24) & 0xFF);
    *pulLength = 4;
    return ok();
}
// SAP / rddads signature: 3 args. AdsGetBookmark60 returns
// (pucBookmark + *pulLength); the caller hands that exact length
// back into AdsGotoBookmark60 to replay the bookmark — needed
// because real ACE supports variable-length bookmarks (the size
// depends on the index/order). OpenADS encodes everything as a
// 4-byte recno today, so any ulLength < 4 is a malformed call.
UNSIGNED32 AdsGotoBookmark60(ADSHANDLE hObj, UNSIGNED8* pucBookmark,
                             UNSIGNED32 ulLength) {
    if (pucBookmark == nullptr || ulLength < 4) {
        return fail(openads::AE_INTERNAL_ERROR, "");
    }
    UNSIGNED32 recno = static_cast<UNSIGNED32>(pucBookmark[0])
                     | (static_cast<UNSIGNED32>(pucBookmark[1]) << 8)
                     | (static_cast<UNSIGNED32>(pucBookmark[2]) << 16)
                     | (static_cast<UNSIGNED32>(pucBookmark[3]) << 24);
    return AdsGotoRecord(hObj, recno);
}

// X#'s ADSRDD calls this during table OPEN to size its memo buffers.
// Report the attached memo store's block size; for a table with no
// memo (or a remote handle — no memo introspection over the wire yet)
// hand back the xBase FPT default so the RDD has a usable value.
UNSIGNED32 AdsGetMemoBlockSize(ADSHANDLE hObj, UNSIGNED16* pusBlockSize) {
    if (pusBlockSize == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    *pusBlockSize = 64;
    if (get_remote_table(hObj) != nullptr) return ok();
    Table* t = get_table(hObj);
    if (t == nullptr) return fail(openads::AE_INTERNAL_ERROR, "no table");
    if (auto* m = t->memo(); m != nullptr && m->block_size() != 0) {
        *pusBlockSize = static_cast<UNSIGNED16>(m->block_size());
    }
    return ok();
}

// ---------------------------------------------------------------------------
// M12.23 — close the export gap the X# Advantage RDD (XSharp.Rdd) relies on.
// ADSRDD.prg references ~45 entry points OpenADS didn't yet export. Most are
// accept-and-ignore (session toggles, statement helpers) or thin forwards;
// the field-setter family uses the ACE "field NAME or 1-based ordinal cast to
// a pointer" idiom. Genuinely-unimplemented ones return
// AE_FUNCTION_NOT_AVAILABLE so the X# runtime falls back to its own
// client-side path. Signatures mirror XSharp.Rdd/ACE/ACE32.prg.
// ---------------------------------------------------------------------------

// ACE field-identifier idiom: a small integer in the "field name" pointer
// slot is a 1-based field ordinal; a real pointer is the name string.
static const char* resolve_field_id(ADSHANDLE hTable, UNSIGNED8* pId,
                                    UNSIGNED8* scratch, UNSIGNED16 scratchLen) {
    if (reinterpret_cast<uintptr_t>(pId) >= 0x10000u) {
        return reinterpret_cast<const char*>(pId);
    }
    if (scratch == nullptr || scratchLen == 0) return "";
    scratch[0] = 0;
    UNSIGNED16 ord = static_cast<UNSIGNED16>(reinterpret_cast<uintptr_t>(pId));
    UNSIGNED16 len = scratchLen;
    if (AdsGetFieldName(hTable, ord, scratch, &len) != openads::AE_SUCCESS) {
        return "";
    }
    return reinterpret_cast<const char*>(scratch);
}
static UNSIGNED8* as_field(const char* s) {
    return const_cast<UNSIGNED8*>(reinterpret_cast<const UNSIGNED8*>(s));
}

UNSIGNED32 AdsGetTableOpenOptions(ADSHANDLE /*hTable*/, UNSIGNED32* pulOptions) {
    if (pulOptions) *pulOptions = 0;
    return ok();
}
UNSIGNED32 AdsGetBookmark(ADSHANDLE hTable, ADSHANDLE* phBookmark) {
    if (phBookmark == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    UNSIGNED32 rec = 0;
    UNSIGNED32 rc = AdsGetRecordNum(hTable, 0, &rec);
    if (rc != openads::AE_SUCCESS) return rc;
    *phBookmark = rec;            // recno-as-token; pairs with AdsGotoRecord
    return ok();
}
UNSIGNED32 AdsCancelUpdate(ADSHANDLE /*hTable*/) { return ok(); }
UNSIGNED32 AdsClearAllScopes(ADSHANDLE /*hTable*/) { return ok(); }
UNSIGNED32 AdsClearDefault(void) { return ok(); }
UNSIGNED32 AdsResetConnection(ADSHANDLE /*hConnect*/) { return ok(); }
UNSIGNED32 AdsThreadExit(void) { return ok(); }
UNSIGNED32 AdsDisableLocalConnections(void) { return ok(); }
UNSIGNED32 AdsEnableRI(ADSHANDLE /*hConn*/) { return ok(); }
UNSIGNED32 AdsDisableRI(ADSHANDLE /*hConn*/) { return ok(); }
UNSIGNED32 AdsEnableUniqueEnforcement(ADSHANDLE /*hConn*/) { return ok(); }
UNSIGNED32 AdsDisableUniqueEnforcement(ADSHANDLE /*hConn*/) { return ok(); }
UNSIGNED32 AdsEnableAutoIncEnforcement(ADSHANDLE /*hConn*/) { return ok(); }
UNSIGNED32 AdsDisableAutoIncEnforcement(ADSHANDLE /*hConn*/) { return ok(); }
UNSIGNED32 AdsRecallAllRecords(ADSHANDLE /*hTable*/) { return ok(); }
UNSIGNED32 AdsIsRecordVisible(ADSHANDLE /*hObj*/, UNSIGNED16* pbVisible) {
    if (pbVisible) *pbVisible = 1;
    return ok();
}
UNSIGNED32 AdsGetKeyCount(ADSHANDLE hIndex, UNSIGNED16 /*usFilter*/,
                          UNSIGNED32* pulCount) {
    if (pulCount == nullptr) return fail(openads::AE_INTERNAL_ERROR, "");
    *pulCount = 0;
    Table* t = get_table(hIndex);
    if (t == nullptr) return ok();
    // With an active order, the KEY count can be far fewer than the table's
    // record_count() (a conditional/FOR index indexes only matching rows).
    // Returning record_count() here made it inconsistent with OrdKeyNo /
    // GetRelKeyPos (which count the index walk) -> TXBrowse position math
    // broke on conditional orders. Use the cached index walk (O(1)).
    auto* ord = t->order();
    if (ord != nullptr && ord->index() != nullptr) {
        if (auto* cdx =
                dynamic_cast<openads::drivers::cdx::CdxIndex*>(ord->index())) {
            *pulCount = static_cast<UNSIGNED32>(
                cdx->ordered_recnos_cached().size());
            return ok();
        }
    }
    *pulCount = t->record_count();
    return ok();
}
UNSIGNED32 AdsContinue(ADSHANDLE hTable, UNSIGNED16* pbFound) {
    if (pbFound) *pbFound = 0;
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    // Skip one record forward — Table::skip() is filter-aware: it walks
    // past non-matching records until it finds one that passes the current
    // filter (AOF or SetFilter) or reaches EOF.
    auto r = t->skip(1);
    if (!r) return fail(r.error());
    if (pbFound) *pbFound = t->eof() ? 0 : 1;
    return ok();
}
UNSIGNED32 AdsEvalTestExpr(ADSHANDLE /*hTable*/, UNSIGNED8* /*pucExpr*/,
                           UNSIGNED16* pusType) {
    if (pusType) *pusType = 0;
    return ok();                                 // treat any expr as syntactically valid
}
UNSIGNED32 AdsEvalLogicalExpr(ADSHANDLE hTable, UNSIGNED8* pucExpr,
                              UNSIGNED16* pbResult) {
    if (pbResult) *pbResult = 0;
    if (!pucExpr) return fail(openads::AE_INTERNAL_ERROR, "null expr");
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    std::string expr = reinterpret_cast<const char*>(pucExpr);
    auto ast = openads::engine::aof::parse(expr);
    if (!ast) return fail(ast.error());
    if (pbResult)
        *pbResult = openads::engine::aof::evaluate_record(*ast.value(), *t) ? 1 : 0;
    return ok();
}
UNSIGNED32 AdsEvalNumericExpr(ADSHANDLE hTable, UNSIGNED8* pucExpr, double* pdResult) {
    if (pdResult) *pdResult = 0.0;
    if (!pucExpr) return fail(openads::AE_INTERNAL_ERROR, "null expr");
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    std::string expr = reinterpret_cast<const char*>(pucExpr);
    std::int32_t fi = t->field_index(expr);
    if (fi >= 0) {
        auto v = t->read_field(static_cast<std::uint16_t>(fi));
        if (v && pdResult) *pdResult = v.value().as_double;
        return ok();
    }
    try {
        std::size_t pos = 0;
        double d = std::stod(expr, &pos);
        if (pos == expr.size() && pdResult) *pdResult = d;
        return ok();
    } catch (...) {}
    return fail(openads::AE_FUNCTION_NOT_AVAILABLE, "cannot evaluate numeric expr");
}
UNSIGNED32 AdsEvalStringExpr(ADSHANDLE hTable, UNSIGNED8* pucExpr,
                             UNSIGNED8* pucResult, UNSIGNED16* pusLen) {
    if (!pucExpr) return fail(openads::AE_INTERNAL_ERROR, "null expr");
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    std::string expr = reinterpret_cast<const char*>(pucExpr);
    std::string result;
    std::int32_t fi = t->field_index(expr);
    if (fi >= 0) {
        auto v = t->read_field(static_cast<std::uint16_t>(fi));
        if (v) result = v.value().as_string;
    } else {
        result = expr;
    }
    if (pusLen) {
        std::uint16_t cap = *pusLen;
        std::uint16_t rlen = static_cast<std::uint16_t>(result.size());
        if (pucResult && cap > 0) {
            auto cap1 = static_cast<std::uint16_t>(cap - 1u);
            std::uint16_t copy = rlen < cap1 ? rlen : cap1;
            std::memcpy(pucResult, result.data(), copy);
            pucResult[copy] = 0;
        }
        *pusLen = rlen;
    }
    return ok();
}
UNSIGNED32 AdsFindConnection(UNSIGNED8* /*pucServer*/, ADSHANDLE* phConnect) {
    if (phConnect) *phConnect = 0;
    return fail(openads::AE_NO_CONNECTION, "no connection for path");
}
UNSIGNED32 AdsGetAllIndexes(ADSHANDLE hTable, ADSHANDLE* ahIndex,
                            UNSIGNED16* pusArrayLen) {
    if (!pusArrayLen) return fail(openads::AE_INTERNAL_ERROR, "null out");
    if (get_remote_table(hTable)) { *pusArrayLen = 0; return ok(); }
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    std::vector<ADSHANDLE> found;
    for (auto& [h, b] : index_bindings()) {
        if (b.table == t) found.push_back(h);
    }
    UNSIGNED16 cap   = *pusArrayLen;
    auto       total = static_cast<UNSIGNED16>(found.size());
    UNSIGNED16 n     = cap < total ? cap : total;
    *pusArrayLen     = n;
    if (ahIndex) {
        for (UNSIGNED16 i = 0; i < n; ++i) ahIndex[i] = found[i];
    }
    return ok();
}
UNSIGNED32 AdsGetFTSIndexes(ADSHANDLE /*hTable*/, ADSHANDLE* /*ahIndex*/,
                             UNSIGNED16* pusArrayLen) {
    // FTS indexes are loaded ad-hoc at query time with no persistent
    // handle, so there is nothing to enumerate here.
    if (pusArrayLen) *pusArrayLen = 0;
    return ok();
}
UNSIGNED32 AdsGetAllTables(ADSHANDLE hConnect, ADSHANDLE* ahTable,
                           UNSIGNED16* pusArrayLen) {
    if (!pusArrayLen) return fail(openads::AE_INTERNAL_ERROR, "null out");
    auto& s = state();
    std::lock_guard<std::recursive_mutex> lk(s.mu);
    Connection* conn =
        s.registry.lookup<Connection>(hConnect, HandleKind::Connection);
    if (!conn) { *pusArrayLen = 0; return ok(); }  // remote or unknown
    std::vector<ADSHANDLE> found;
    s.registry.for_each_handle([&](Handle h, HandleKind k, void* p) {
        if (k != HandleKind::Table) return;
        if (conn->owns_table_ptr(static_cast<Table*>(p))) found.push_back(h);
    });
    UNSIGNED16 cap   = *pusArrayLen;
    auto       total = static_cast<UNSIGNED16>(found.size());
    UNSIGNED16 n     = cap < total ? cap : total;
    *pusArrayLen     = n;
    if (ahTable) {
        for (UNSIGNED16 i = 0; i < n; ++i) ahTable[i] = found[i];
    }
    return ok();
}
UNSIGNED32 AdsCloneTable(ADSHANDLE hTable, ADSHANDLE* phClone) {
    if (!phClone) return fail(openads::AE_INTERNAL_ERROR, "null out param");
    *phClone = 0;
    auto& s = state();
    std::lock_guard<std::recursive_mutex> lk(s.mu);
    Table* t = s.registry.lookup<Table>(hTable, HandleKind::Table);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    if (!t->driver()) return fail(openads::AE_INTERNAL_ERROR, "no driver");

    // Locate the connection that owns this table.
    Connection* owning = nullptr;
    s.registry.for_each_handle([&](Handle, HandleKind k, void* p) {
        if (k != HandleKind::Connection || owning) return;
        auto* cc = static_cast<Connection*>(p);
        if (cc->owns_table_ptr(t)) owning = cc;
    });
    if (!owning) return fail(openads::AE_INVALID_CONNECTION_HANDLE,
                             "table not owned by any connection");

    // Unique temp filename inside the data directory.
    namespace fs = std::filesystem;
    char tmp_name[64] = {};
    std::snprintf(tmp_name, sizeof(tmp_name), "_clone_%llx.dbf",
                  static_cast<unsigned long long>(
                      openads::platform::monotonic_nanos()));

    // Serialize structure + all records (including deleted, for exact
    // fidelity) to the temp file.
    const auto& src_fields = t->driver()->fields();
    if (src_fields.empty())
        return fail(openads::AE_INTERNAL_ERROR, "AdsCloneTable: no fields");
    std::uint16_t header_len = static_cast<std::uint16_t>(
        32 + 32 * src_fields.size() + 1);
    std::uint16_t rec_len = t->driver()->record_length();

    std::vector<std::uint8_t> file;
    std::vector<std::uint8_t> hdr(32, 0);
    hdr[0] = 0x03;
    stamp_dbf_header_today(hdr.data());
    hdr[8]  = static_cast<std::uint8_t>(header_len & 0xFFu);
    hdr[9]  = static_cast<std::uint8_t>((header_len >> 8) & 0xFFu);
    hdr[10] = static_cast<std::uint8_t>(rec_len & 0xFFu);
    hdr[11] = static_cast<std::uint8_t>((rec_len >> 8) & 0xFFu);
    file = hdr;
    for (const auto& f : src_fields) {
        std::vector<std::uint8_t> fd(32, 0);
        std::size_t n = std::min<std::size_t>(f.name.size(), 10);
        std::memcpy(fd.data(), f.name.data(), n);
        fd[11] = static_cast<std::uint8_t>(f.raw_type ? f.raw_type : 'C');
        fd[16] = static_cast<std::uint8_t>(f.length);
        fd[17] = f.decimals;
        file.insert(file.end(), fd.begin(), fd.end());
    }
    file.push_back(0x0D);

    std::uint32_t rcount = t->driver()->record_count();
    for (std::uint32_t r = 1; r <= rcount; ++r) {
        auto rec = t->driver()->read_record_raw(r);
        if (!rec) return fail(rec.error());
        file.insert(file.end(), rec.value().begin(), rec.value().end());
    }
    file.push_back(0x1A);
    file[4] = static_cast<std::uint8_t>( rcount        & 0xFFu);
    file[5] = static_cast<std::uint8_t>((rcount >>  8) & 0xFFu);
    file[6] = static_cast<std::uint8_t>((rcount >> 16) & 0xFFu);
    file[7] = static_cast<std::uint8_t>((rcount >> 24) & 0xFFu);

    fs::path tmp_path = fs::path(owning->data_dir()) / tmp_name;
    {
        std::ofstream out(tmp_path, std::ios::binary);
        if (!out) return fail(openads::AE_INTERNAL_ERROR,
                              "AdsCloneTable: write failed");
        out.write(reinterpret_cast<const char*>(file.data()),
                  static_cast<std::streamsize>(file.size()));
        if (!out) return fail(openads::AE_INTERNAL_ERROR,
                              "AdsCloneTable: write error");
    }

    // Open the clone through the owning connection.
    auto th = owning->open_table(std::string(tmp_name),
                                 openads::engine::TableType::Cdx,
                                 openads::engine::OpenMode::Shared);
    if (!th) {
        std::error_code ec;
        fs::remove(tmp_path, ec);
        return fail(th.error());
    }
    Table* clone = owning->lookup_table(th.value());
    if (!clone) return fail(openads::AE_INTERNAL_ERROR,
                            "AdsCloneTable: post-open");
    *phClone = s.registry.register_object(HandleKind::Table, clone);
    return ok();
}

UNSIGNED32 AdsCopyTableStructure(ADSHANDLE hTable, UNSIGNED8* pucFile) {
    if (!pucFile) return fail(openads::AE_INTERNAL_ERROR, "null path");
    Table* t = get_table(hTable);
    if (!t) return fail(openads::AE_INTERNAL_ERROR, "unknown table");
    if (!t->driver()) return fail(openads::AE_INTERNAL_ERROR, "no driver");

    namespace fs = std::filesystem;
    auto raw = openads::abi::to_internal(pucFile, 0);
    fs::path dst(raw);
    if (!dst.is_absolute())
        dst = fs::path(t->path()).parent_path() / dst;
    if (!dst.has_extension()) dst.replace_extension(".dbf");

    const auto& src_fields = t->driver()->fields();
    if (src_fields.empty())
        return fail(openads::AE_INTERNAL_ERROR, "AdsCopyTableStructure: no fields");
    std::uint16_t header_len = static_cast<std::uint16_t>(
        32 + 32 * src_fields.size() + 1);
    std::uint16_t rec_len = t->driver()->record_length();

    std::vector<std::uint8_t> file;
    std::vector<std::uint8_t> hdr(32, 0);
    hdr[0] = 0x03;
    stamp_dbf_header_today(hdr.data());
    // record count = 0 (bytes 4-7 stay zero)
    hdr[8]  = static_cast<std::uint8_t>(header_len & 0xFFu);
    hdr[9]  = static_cast<std::uint8_t>((header_len >> 8) & 0xFFu);
    hdr[10] = static_cast<std::uint8_t>(rec_len & 0xFFu);
    hdr[11] = static_cast<std::uint8_t>((rec_len >> 8) & 0xFFu);
    file = hdr;
    for (const auto& f : src_fields) {
        std::vector<std::uint8_t> fd(32, 0);
        std::size_t n = std::min<std::size_t>(f.name.size(), 10);
        std::memcpy(fd.data(), f.name.data(), n);
        fd[11] = static_cast<std::uint8_t>(f.raw_type ? f.raw_type : 'C');
        fd[16] = static_cast<std::uint8_t>(f.length);
        fd[17] = f.decimals;
        file.insert(file.end(), fd.begin(), fd.end());
    }
    file.push_back(0x0D);
    file.push_back(0x1A);   // EOF, no records

    {
        std::error_code ec;
        fs::remove(dst, ec);
    }
    {
        std::ofstream out(dst, std::ios::binary);
        if (!out) return fail(openads::AE_INTERNAL_ERROR,
                              "AdsCopyTableStructure: open failed");
        out.write(reinterpret_cast<const char*>(file.data()),
                  static_cast<std::streamsize>(file.size()));
        if (!out) return fail(openads::AE_INTERNAL_ERROR,
                              "AdsCopyTableStructure: write failed");
    }
    return ok();
}
UNSIGNED32 AdsGetRecordCRC(ADSHANDLE, UNSIGNED32* p, UNSIGNED32)
    { if (p) *p = 0; return openads::AE_FUNCTION_NOT_AVAILABLE; }
UNSIGNED32 AdsInitRawKey(ADSHANDLE) { return ok(); }
UNSIGNED32 AdsMgDumpInternalTables(ADSHANDLE h) {
    return lookup_mg(h) ? openads::AE_SUCCESS
                        : openads::AE_INVALID_CONNECTION_HANDLE;
}
UNSIGNED32 AdsClearSQLAbortFunc(void) { return ok(); }
UNSIGNED32 AdsClearSQLParams(ADSHANDLE) { return ok(); }
UNSIGNED32 AdsStmtClearTablePasswords(ADSHANDLE h) {
    if (auto* st = stmt_lookup(h)) st->passwords.clear();
    return ok();
}
UNSIGNED32 AdsStmtDisableEncryption(ADSHANDLE h) {
    if (auto* st = stmt_lookup(h)) st->disable_enc = true;
    return ok();
}
UNSIGNED32 AdsStmtSetTableCharType(ADSHANDLE h, UNSIGNED16 us) {
    if (auto* st = stmt_lookup(h)) st->char_type = us;
    return ok();
}
UNSIGNED32 AdsStmtSetTableCollation(ADSHANDLE h, UNSIGNED8* puc) {
    if (auto* st = stmt_lookup(h)) st->collation = openads::abi::to_internal(puc, 0);
    return ok();
}
UNSIGNED32 AdsStmtSetTableRights(ADSHANDLE h, UNSIGNED16 us) {
    if (auto* st = stmt_lookup(h)) st->check_rights = us;
    return ok();
}

// --- field-identifier-aware setters/getters (name OR ordinal-as-pointer) ---
UNSIGNED32 AdsSetField(ADSHANDLE hObj, UNSIGNED8* pId, UNSIGNED8* pucBuf,
                       UNSIGNED32 ulLen) {
    UNSIGNED8 nm[64];
    return AdsSetString(hObj, as_field(resolve_field_id(hObj, pId, nm, sizeof(nm))),
                        pucBuf, ulLen);
}
UNSIGNED32 AdsSetEmpty(ADSHANDLE hObj, UNSIGNED8* pId) {
    UNSIGNED8 nm[64];
    UNSIGNED8 blank = 0;
    return AdsSetString(hObj, as_field(resolve_field_id(hObj, pId, nm, sizeof(nm))),
                        &blank, 0);
}
UNSIGNED32 AdsSetNull(ADSHANDLE hObj, UNSIGNED8* pId) {
    return AdsSetEmpty(hObj, pId);     // DBF has no SQL NULL — store empty
}
UNSIGNED32 AdsSetShort(ADSHANDLE hObj, UNSIGNED8* pId, SIGNED32 sValue) {
    UNSIGNED8 nm[64];
    return AdsSetDouble(hObj, as_field(resolve_field_id(hObj, pId, nm, sizeof(nm))),
                        static_cast<double>(sValue));
}
UNSIGNED32 AdsSetMoney(ADSHANDLE hObj, UNSIGNED8* pId, SIGNED64 qValue) {
    UNSIGNED8 nm[64];
    return AdsSetDouble(hObj, as_field(resolve_field_id(hObj, pId, nm, sizeof(nm))),
                        static_cast<double>(qValue) / 10000.0);   // ACE money scale
}
UNSIGNED32 AdsSetTime(ADSHANDLE hObj, UNSIGNED8* pId, UNSIGNED8* pucValue,
                      UNSIGNED16 usLen) {
    UNSIGNED8 nm[64];
    return AdsSetString(hObj, as_field(resolve_field_id(hObj, pId, nm, sizeof(nm))),
                        pucValue, usLen);
}
UNSIGNED32 AdsSetTimeStamp(ADSHANDLE hObj, UNSIGNED8* pId, UNSIGNED8* pucBuf,
                           UNSIGNED32 ulLen) {
    UNSIGNED8 nm[64];
    return AdsSetString(hObj, as_field(resolve_field_id(hObj, pId, nm, sizeof(nm))),
                        pucBuf, ulLen);
}
UNSIGNED32 AdsGetDate(ADSHANDLE hObj, UNSIGNED8* pId, UNSIGNED8* pucBuf,
                      UNSIGNED16* pusLen) {
    UNSIGNED8 nm[64];
    UNSIGNED32 cap = pusLen ? *pusLen : 0;
    UNSIGNED32 rc = AdsGetField(hObj,
                                as_field(resolve_field_id(hObj, pId, nm, sizeof(nm))),
                                pucBuf, &cap, 0);
    if (pusLen) *pusLen = static_cast<UNSIGNED16>(cap);
    return rc;
}

// ---------------------------------------------------------------------------
// SAP ACE API name aliases — binaries compiled against ace64.dll use these
// names.  OpenADS uses Create/Drop internally; SAP ACE uses Add/Remove.
// ---------------------------------------------------------------------------

UNSIGNED32 AdsDDAddProcedure(ADSHANDLE hConn, UNSIGNED8* pucName,
                              UNSIGNED8* pucContainer, UNSIGNED8* pucProcName,
                              UNSIGNED32 ulInvokeOption,
                              UNSIGNED8* pucInParams, UNSIGNED8* pucOutParams,
                              UNSIGNED8* pucComments) {
    return AdsDDCreateProcedure(hConn, pucName, pucContainer, pucProcName,
                                ulInvokeOption, pucInParams, pucOutParams,
                                pucComments);
}
UNSIGNED32 AdsDDRemoveProcedure(ADSHANDLE hConn, UNSIGNED8* pucName) {
    return AdsDDDropProcedure(hConn, pucName);
}
UNSIGNED32 AdsDDGetProcedureProperty(ADSHANDLE hConn, UNSIGNED8* pucName,
                                      UNSIGNED16 usProp, void* pBuf,
                                      UNSIGNED16* pusLen) {
    return AdsDDGetProcProperty(hConn, pucName, usProp, pBuf, pusLen);
}
UNSIGNED32 AdsDDSetProcedureProperty(ADSHANDLE hConn, UNSIGNED8* pucName,
                                      UNSIGNED16 usProp, void* pBuf,
                                      UNSIGNED16 usLen) {
    return AdsDDSetProcProperty(hConn, pucName, usProp, pBuf, usLen);
}
UNSIGNED32 AdsDDRemoveTrigger(ADSHANDLE hConn, UNSIGNED8* pucName) {
    return AdsDDDropTrigger(hConn, pucName);
}

// AdsDDFindFirstObject / FindNextObject / FindClose — stubs
// OpenADS does not implement the find-handle enumeration pattern; callers
// that need object lists should query the system.* virtual tables instead.
UNSIGNED32 AdsDDFindFirstObject(ADSHANDLE /*hObject*/,
                                 UNSIGNED16 /*usFindObjectType*/,
                                 UNSIGNED8*  /*pucParentName*/,
                                 UNSIGNED8*  /*pucObjectName*/,
                                 UNSIGNED16* pusObjectNameLen,
                                 ADSHANDLE*  phFindHandle) {
    if (pusObjectNameLen) *pusObjectNameLen = 0;
    if (phFindHandle)     *phFindHandle     = 0;
    return fail(openads::AE_FUNCTION_NOT_AVAILABLE, "AdsDDFindFirstObject");
}
UNSIGNED32 AdsDDFindNextObject(ADSHANDLE /*hObject*/,
                                ADSHANDLE  /*hFindHandle*/,
                                UNSIGNED8*  /*pucObjectName*/,
                                UNSIGNED16* pusObjectNameLen) {
    if (pusObjectNameLen) *pusObjectNameLen = 0;
    return fail(openads::AE_FUNCTION_NOT_AVAILABLE, "AdsDDFindNextObject");
}
UNSIGNED32 AdsDDFindClose(ADSHANDLE /*hObject*/, ADSHANDLE /*hFindHandle*/) {
    return ok();
}

} // extern "C"

} // extern "C"
