// openads_sql_c.cpp — thin SQL C API (see include/openads/openads_sql.h).
//
// A small, original C surface over the project's own public ACE functions
// (include/openads/ace.h). It owns no engine state of its own — every call
// delegates to an Ads* entry point. Opaque handles wrap the ACE handles so
// callers never touch them directly.
#include "openads/openads_sql.h"
#include "openads/ace.h"

#include <cstring>
#include <string>
#include <vector>

struct openads_conn {
    ADSHANDLE h;
};

struct openads_stmt {
    ADSHANDLE conn;     // owning connection (not closed here)
    ADSHANDLE stmt;     // SQL statement handle
    ADSHANDLE cursor;   // result-set cursor (0 = none, e.g. DML/DDL)
    long      pos;      // 0 = before first row; N = positioned on row N
};

namespace {

// NUL-terminated mutable byte buffer from a C string (ACE takes UNSIGNED8*).
std::vector<UNSIGNED8> cbuf(const char* s) {
    std::vector<UNSIGNED8> v;
    std::size_t n = s ? std::strlen(s) : 0;
    v.assign(s, s + n);
    v.push_back(0);
    return v;
}

// Resolve a 1-based column ordinal to its field name on a cursor.
bool col_field_name(ADSHANDLE cur, int col, UNSIGNED8* out, UNSIGNED16 cap) {
    if (col < 1) return false;
    UNSIGNED16 len = cap;
    return AdsGetFieldName(cur, static_cast<UNSIGNED16>(col), out, &len) == 0;
}

} // namespace

extern "C" {

int openads_connect(const char* path, const char* server_type,
                    const char* user, const char* password,
                    openads_conn** out_conn) {
    if (!out_conn) return OPENADS_ERROR;
    *out_conn = nullptr;

    UNSIGNED16 stype = ADS_LOCAL_SERVER;
    if (server_type && std::strcmp(server_type, "remote") == 0)
        stype = ADS_REMOTE_SERVER;

    std::vector<UNSIGNED8> srv = cbuf(path ? path : "");
    std::vector<UNSIGNED8> u   = cbuf(user);
    std::vector<UNSIGNED8> pw  = cbuf(password);

    ADSHANDLE h = 0;
    if (AdsConnect60(srv.data(), stype,
                     user ? u.data() : nullptr,
                     password ? pw.data() : nullptr,
                     0, &h) != 0)
        return OPENADS_ERROR;

    *out_conn = new openads_conn{h};
    return OPENADS_OK;
}

int openads_disconnect(openads_conn* conn) {
    if (!conn) return OPENADS_ERROR;
    AdsDisconnect(conn->h);
    delete conn;
    return OPENADS_OK;
}

int openads_exec_direct(openads_conn* conn, const char* sql,
                        openads_stmt** out_stmt) {
    if (!conn || !sql || !out_stmt) return OPENADS_ERROR;
    *out_stmt = nullptr;

    ADSHANDLE stmt = 0;
    if (AdsCreateSQLStatement(conn->h, &stmt) != 0) return OPENADS_ERROR;

    std::vector<UNSIGNED8> q = cbuf(sql);
    ADSHANDLE cur = 0;
    if (AdsExecuteSQLDirect(stmt, q.data(), &cur) != 0) {
        AdsCloseSQLStatement(stmt);
        return OPENADS_ERROR;
    }
    *out_stmt = new openads_stmt{conn->h, stmt, cur, 0};
    return OPENADS_OK;
}

int openads_prepare(openads_conn* conn, const char* sql,
                    openads_stmt** out_stmt) {
    if (!conn || !sql || !out_stmt) return OPENADS_ERROR;
    *out_stmt = nullptr;

    ADSHANDLE stmt = 0;
    if (AdsCreateSQLStatement(conn->h, &stmt) != 0) return OPENADS_ERROR;

    std::vector<UNSIGNED8> q = cbuf(sql);
    if (AdsPrepareSQL(stmt, q.data()) != 0) {
        AdsCloseSQLStatement(stmt);
        return OPENADS_ERROR;
    }
    *out_stmt = new openads_stmt{conn->h, stmt, 0, 0};
    return OPENADS_OK;
}

int openads_bind_str(openads_stmt* stmt, const char* name, const char* val) {
    if (!stmt || !name) return OPENADS_ERROR;
    std::vector<UNSIGNED8> n = cbuf(name);
    std::vector<UNSIGNED8> v = cbuf(val ? val : "");
    UNSIGNED32 len = static_cast<UNSIGNED32>(val ? std::strlen(val) : 0);
    return AdsSetString(stmt->stmt, n.data(), v.data(), len) == 0
               ? OPENADS_OK : OPENADS_ERROR;
}

int openads_bind_double(openads_stmt* stmt, const char* name, double val) {
    if (!stmt || !name) return OPENADS_ERROR;
    std::vector<UNSIGNED8> n = cbuf(name);
    return AdsSetDouble(stmt->stmt, n.data(), val) == 0
               ? OPENADS_OK : OPENADS_ERROR;
}

int openads_bind_int64(openads_stmt* stmt, const char* name, long long val) {
    // Routes through the numeric literal path. Exact for |val| < 2^53.
    return openads_bind_double(stmt, name, static_cast<double>(val));
}

int openads_num_params(openads_stmt* stmt, int* out_count) {
    if (!stmt || !out_count) return OPENADS_ERROR;
    UNSIGNED16 n = 0;
    if (AdsGetNumParams(stmt->stmt, &n) != 0) return OPENADS_ERROR;
    *out_count = static_cast<int>(n);
    return OPENADS_OK;
}

int openads_execute(openads_stmt* stmt) {
    if (!stmt) return OPENADS_ERROR;
    if (stmt->cursor) { AdsCloseTable(stmt->cursor); stmt->cursor = 0; }
    ADSHANDLE cur = 0;
    if (AdsExecuteSQL(stmt->stmt, &cur) != 0) return OPENADS_ERROR;
    stmt->cursor = cur;
    stmt->pos = 0;
    return OPENADS_OK;
}

int openads_num_cols(openads_stmt* stmt, int* out_count) {
    if (!stmt || !stmt->cursor || !out_count) return OPENADS_ERROR;
    UNSIGNED16 n = 0;
    if (AdsGetNumFields(stmt->cursor, &n) != 0) return OPENADS_ERROR;
    *out_count = static_cast<int>(n);
    return OPENADS_OK;
}

int openads_col_name(openads_stmt* stmt, int col, char* buf, size_t buflen) {
    if (!stmt || !stmt->cursor || !buf || buflen == 0 || col < 1)
        return OPENADS_ERROR;
    UNSIGNED16 len = static_cast<UNSIGNED16>(
        buflen > 0xFFFF ? 0xFFFF : buflen);
    if (AdsGetFieldName(stmt->cursor, static_cast<UNSIGNED16>(col),
                        reinterpret_cast<UNSIGNED8*>(buf), &len) != 0)
        return OPENADS_ERROR;
    if (len < buflen) buf[len] = '\0';
    return OPENADS_OK;
}

int openads_col_type(openads_stmt* stmt, int col, int* out_type) {
    if (!stmt || !stmt->cursor || !out_type) return OPENADS_ERROR;
    UNSIGNED8 name[256];
    if (!col_field_name(stmt->cursor, col, name, sizeof(name)))
        return OPENADS_ERROR;
    UNSIGNED16 t = 0;
    if (AdsGetFieldType(stmt->cursor, name, &t) != 0) return OPENADS_ERROR;
    *out_type = static_cast<int>(t);
    return OPENADS_OK;
}

int openads_fetch_first(openads_stmt* stmt) {
    if (!stmt || !stmt->cursor) return OPENADS_ERROR;
    if (AdsGotoTop(stmt->cursor) != 0) return OPENADS_ERROR;
    UNSIGNED16 eof = 0;
    if (AdsAtEOF(stmt->cursor, &eof) != 0) return OPENADS_ERROR;
    if (eof) { stmt->pos = 0; return OPENADS_NO_DATA; }
    stmt->pos = 1;
    return OPENADS_OK;
}

int openads_fetch_next(openads_stmt* stmt) {
    if (!stmt || !stmt->cursor) return OPENADS_ERROR;
    if (stmt->pos == 0) {
        if (AdsGotoTop(stmt->cursor) != 0) return OPENADS_ERROR;
    } else {
        if (AdsSkip(stmt->cursor, 1) != 0) return OPENADS_ERROR;
    }
    UNSIGNED16 eof = 0;
    if (AdsAtEOF(stmt->cursor, &eof) != 0) return OPENADS_ERROR;
    if (eof) return OPENADS_NO_DATA;
    stmt->pos += 1;
    return OPENADS_OK;
}

int openads_fetch_absolute(openads_stmt* stmt, long row) {
    if (!stmt || !stmt->cursor || row < 1) return OPENADS_ERROR;
    if (AdsGotoTop(stmt->cursor) != 0) return OPENADS_ERROR;
    if (row > 1) {
        if (AdsSkip(stmt->cursor, static_cast<SIGNED32>(row - 1)) != 0)
            return OPENADS_ERROR;
    }
    UNSIGNED16 eof = 0;
    if (AdsAtEOF(stmt->cursor, &eof) != 0) return OPENADS_ERROR;
    if (eof) { stmt->pos = 0; return OPENADS_NO_DATA; }
    stmt->pos = row;
    return OPENADS_OK;
}

int openads_row_count(openads_stmt* stmt, long* out_count) {
    if (!stmt || !stmt->cursor || !out_count) return OPENADS_ERROR;
    UNSIGNED32 n = 0;
    if (AdsGetRecordCount(stmt->cursor, 0, &n) != 0) return OPENADS_ERROR;
    *out_count = static_cast<long>(n);
    return OPENADS_OK;
}

int openads_get_str(openads_stmt* stmt, int col, char* buf, size_t buflen,
                    size_t* out_len) {
    if (!stmt || !stmt->cursor || !buf || buflen == 0) return OPENADS_ERROR;
    UNSIGNED8 name[256];
    if (!col_field_name(stmt->cursor, col, name, sizeof(name)))
        return OPENADS_ERROR;
    UNSIGNED32 len = static_cast<UNSIGNED32>(buflen);
    if (AdsGetString(stmt->cursor, name,
                     reinterpret_cast<UNSIGNED8*>(buf), &len, 0) != 0)
        return OPENADS_ERROR;
    if (out_len) *out_len = static_cast<size_t>(len);
    if (len < buflen) buf[len] = '\0';
    return OPENADS_OK;
}

int openads_cancel(openads_stmt* stmt) {
    if (!stmt) return OPENADS_ERROR;
    if (stmt->cursor) { AdsCloseTable(stmt->cursor); stmt->cursor = 0; }
    stmt->pos = 0;
    return OPENADS_OK;
}

int openads_finalize(openads_stmt* stmt) {
    if (!stmt) return OPENADS_ERROR;
    if (stmt->cursor) AdsCloseTable(stmt->cursor);
    if (stmt->stmt)   AdsCloseSQLStatement(stmt->stmt);
    delete stmt;
    return OPENADS_OK;
}

int openads_last_error(openads_conn* /*conn*/, int* out_code,
                       char* buf, size_t buflen) {
    UNSIGNED32 code = 0;
    UNSIGNED16 len = static_cast<UNSIGNED16>(buflen > 0xFFFF ? 0xFFFF : buflen);
    AdsGetLastError(&code, reinterpret_cast<UNSIGNED8*>(buf), &len);
    if (out_code) *out_code = static_cast<int>(code);
    if (buf && len < buflen) buf[len] = '\0';
    return OPENADS_OK;
}

const char* openads_libversion(void) {
    return "openads-sql 0.1";
}

} // extern "C"
