/* openads_sql.h — OpenADS thin SQL C API
 *
 * A small, modern, C-linkage surface over the OpenADS engine, meant as a
 * single foundation for language bindings and drivers (ODBC, PDO, Python,
 * Node, ...) instead of having each binding wrap the full navigational ACE
 * API. The whole implementation is a thin shim over the project's own public
 * ACE functions (include/openads/ace.h); it adds no engine behaviour.
 *
 * Design notes:
 *   - Opaque handles (openads_conn / openads_stmt); callers never see ACE
 *     handles.
 *   - 1-based column and row ordinals (matches typical SQL CLI conventions).
 *   - Named parameters use the ":name" form already understood by the engine.
 *
 * This is an original OpenADS interface. Not yet covered (follow-ups):
 *   - typed NULL parameter binding (engine lacks a public NULL-literal setter);
 *   - rows-affected for DML (engine exposes no count today);
 *   - typed getters (get_int64/get_double) — only string form for now.
 */
#ifndef OPENADS_SQL_H
#define OPENADS_SQL_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Result codes. */
#define OPENADS_OK       0
#define OPENADS_ERROR    1
#define OPENADS_NO_DATA  100   /* fetch moved past the last row */

/* Opaque handles. */
typedef struct openads_conn openads_conn;
typedef struct openads_stmt openads_stmt;

/* --- connection ----------------------------------------------------------
 * server_type: "local" (default if NULL/empty) or "remote".
 * For local, `path` is the data directory; for remote, the server address. */
int openads_connect(const char* path, const char* server_type,
                    const char* user, const char* password,
                    openads_conn** out_conn);
int openads_disconnect(openads_conn* conn);

/* --- immediate execution --------------------------------------------------
 * Runs `sql` directly. For a result-set statement the resulting cursor is
 * attached to *out_stmt and is positioned before the first row (use
 * openads_fetch_next). For DML/DDL no cursor is attached. */
int openads_exec_direct(openads_conn* conn, const char* sql,
                        openads_stmt** out_stmt);

/* --- prepare / bind / execute --------------------------------------------- */
int openads_prepare(openads_conn* conn, const char* sql,
                    openads_stmt** out_stmt);
int openads_bind_str(openads_stmt* stmt, const char* name, const char* val);
int openads_bind_double(openads_stmt* stmt, const char* name, double val);
int openads_bind_int64(openads_stmt* stmt, const char* name, long long val);
int openads_num_params(openads_stmt* stmt, int* out_count);
int openads_execute(openads_stmt* stmt);

/* --- result-set metadata (describe) --------------------------------------- */
int openads_num_cols(openads_stmt* stmt, int* out_count);
int openads_col_name(openads_stmt* stmt, int col /*1-based*/,
                     char* buf, size_t buflen);
int openads_col_type(openads_stmt* stmt, int col /*1-based*/, int* out_type);

/* --- navigation (scrollable) ----------------------------------------------
 * Each returns OPENADS_OK when positioned on a row, OPENADS_NO_DATA when the
 * move lands past the result set, or OPENADS_ERROR. */
int openads_fetch_first(openads_stmt* stmt);
int openads_fetch_next(openads_stmt* stmt);
int openads_fetch_absolute(openads_stmt* stmt, long row /*1-based*/);
int openads_row_count(openads_stmt* stmt, long* out_count);

/* --- column value of the current row (string form) ------------------------ */
int openads_get_str(openads_stmt* stmt, int col /*1-based*/,
                    char* buf, size_t buflen, size_t* out_len);

/* --- lifecycle / diagnostics ---------------------------------------------- */
int openads_cancel(openads_stmt* stmt);    /* abort the active result set */
int openads_finalize(openads_stmt* stmt);  /* close cursor+statement, free */
int openads_last_error(openads_conn* conn, int* out_code,
                       char* buf, size_t buflen);
const char* openads_libversion(void);

#ifdef __cplusplus
}
#endif

#endif /* OPENADS_SQL_H */
