add_library(openads_aes_impl OBJECT
    ${CMAKE_SOURCE_DIR}/third_party/tinyaes/aes128_impl.c
    ${CMAKE_SOURCE_DIR}/third_party/tinyaes/aes256_impl.c
)

target_include_directories(openads_aes_impl PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party/tinyaes
)

if(MSVC)
    target_compile_options(openads_aes_impl PRIVATE /wd4244 /wd4267)
else()
    target_compile_options(openads_aes_impl PRIVATE
        -Wno-shadow -Wno-conversion -Wno-pedantic)
endif()

add_library(openads_core STATIC
    util/log.cpp
    platform/path.cpp
    platform/time.cpp
    platform/thread.cpp
    platform/proc.cpp
    abi/ace_exports.cpp
    abi/ace_stubs.cpp
    abi/openads_sql_c.cpp
    abi/last_error.cpp
    abi/charset.cpp
    abi/backend_registry.cpp
    session/handle_registry.cpp
    session/connection.cpp
    engine/table.cpp
    engine/cursor.cpp
    engine/lock_mgr.cpp
    engine/order.cpp
    engine/index_expr.cpp
    engine/index_expr_sql.cpp
    engine/aggregate.cpp
    engine/aof_expr.cpp
    engine/aof_eval.cpp
    engine/aes.cpp
    engine/tx.cpp
    engine/tx_log.cpp
    engine/lsn_map.cpp
    engine/data_dict.cpp
    engine/fts.cpp
    engine/codepage.cpp
    sql/parser.cpp
    drivers/dbf_common.cpp
    drivers/cdx/cdx_driver.cpp
    drivers/cdx/cdx_index.cpp
    drivers/ntx/ntx_driver.cpp
    drivers/ntx/ntx_index.cpp
    drivers/adt/adt_driver.cpp
    drivers/adm/adm_memo.cpp
    drivers/adi/adi_index.cpp
    drivers/dbt/dbt_memo.cpp
    drivers/fpt/fpt_memo.cpp
    network/wire.cpp
    network/frame_reader.cpp
    network/server.cpp
    network/session.cpp
    network/worker_pool.cpp
    network/client.cpp
    mgmt/mg_collector.cpp
    network/mg_wire.cpp
    sql_backend/uri.cpp
    sql_backend/sql_common.cpp
    sql_backend/enterprise_config.cpp
)

if(OPENADS_WITH_POSTGRESQL)
    target_sources(openads_core PRIVATE
        sql_backend/postgres_uri.cpp
        sql_backend/postgres_backend.cpp
        sql_backend/postgres_connection.cpp
    )
endif()

if(OPENADS_WITH_MARIADB)
    target_sources(openads_core PRIVATE
        sql_backend/maria_uri.cpp
        sql_backend/maria_backend.cpp
        sql_backend/maria_connection.cpp
    )
endif()

if(OPENADS_WITH_ODBC)
    target_sources(openads_core PRIVATE
        sql_backend/odbc_uri.cpp
        sql_backend/odbc_backend.cpp
        sql_backend/odbc_connection.cpp
    )
endif()

if(OPENADS_WITH_MSSQL)
    # The native MSSQL backend tunnels the TLS handshake inside TDS packets,
    # so it links mbedtls symbols directly — require OPENADS_WITH_TLS.
    if(NOT OPENADS_WITH_TLS)
        message(FATAL_ERROR
            "OPENADS_WITH_MSSQL=ON requires OPENADS_WITH_TLS=ON "
            "(the native TDS backend tunnels the TLS handshake through "
            "mbedtls). Re-run cmake with -DOPENADS_WITH_TLS=ON.")
    endif()
    # sql_common.cpp is shared with the ODBC backend; add it only once.
    if(NOT OPENADS_WITH_ODBC)
        target_sources(openads_core PRIVATE sql_backend/sql_common.cpp)
    endif()
    target_sources(openads_core PRIVATE
        sql_backend/tds_protocol.cpp
        sql_backend/mssql_uri.cpp
        sql_backend/tds_tls_channel.cpp
        sql_backend/mssql_connection.cpp
        sql_backend/mssql_table.cpp
    )
    target_compile_definitions(openads_core PUBLIC OPENADS_WITH_MSSQL=1)
endif()

if(OPENADS_WITH_FIREBIRD)
    target_sources(openads_core PRIVATE
        sql_backend/firebird_uri.cpp
        sql_backend/firebird_connection.cpp
    )
    # sql_common.cpp is already compiled unconditionally above (shared by the
    # SQLite/ODBC/PostgreSQL/MariaDB backends) — do NOT add it again here.
endif()

if(WIN32)
    target_sources(openads_core PRIVATE
        platform/file_win32.cpp
        platform/lock_win32.cpp
        platform/mmap_win32.cpp
        platform/dll_win32.cpp
        network/socket_win32.cpp
    )
    target_link_libraries(openads_core PUBLIC ws2_32 psapi)
else()
    target_sources(openads_core PRIVATE
        platform/file_posix.cpp
        platform/lock_posix.cpp
        platform/mmap_posix.cpp
        platform/dll_posix.cpp
        network/socket_posix.cpp
    )
endif()

target_include_directories(openads_core
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/third_party/tinyaes
)

target_link_libraries(openads_core PRIVATE openads_aes_impl)

if(UNIX)
    target_link_libraries(openads_core PUBLIC pthread)
endif()

if(OPENADS_WITH_TLS)
    target_sources(openads_core PRIVATE network/tls_transport.cpp)
    target_compile_definitions(openads_core PUBLIC OPENADS_WITH_TLS=1)
    target_link_libraries(openads_core PRIVATE
        MbedTLS::mbedtls MbedTLS::mbedx509 MbedTLS::mbedcrypto)
endif()

if(OPENADS_WITH_SQLITE)
    # Gate the SQLite backend sources too: with OPENADS_WITH_SQLITE=OFF the
    # FetchContent that provides sqlite3.h is skipped, so compiling these
    # unconditionally fails with a missing-header error (reported by a
    # downstream OFF build). Only build them when the backend is enabled.
    target_sources(openads_core PRIVATE
        sql_backend/sqlite_connection.cpp
        sql_backend/sqlite_backend.cpp
    )
    target_compile_definitions(openads_core PUBLIC OPENADS_WITH_SQLITE=1)
    if(OPENADS_WITH_SQLCIPHER)
        target_compile_definitions(openads_core PUBLIC OPENADS_WITH_SQLCIPHER=1)
    endif()
    target_link_libraries(openads_core PRIVATE openads_sqlite3)
endif()

if(OPENADS_WITH_POSTGRESQL)
    target_compile_definitions(openads_core PUBLIC OPENADS_WITH_POSTGRESQL=1)
    target_link_libraries(openads_core PUBLIC openads_libpq)
endif()

if(OPENADS_WITH_MARIADB)
    target_compile_definitions(openads_core PUBLIC OPENADS_WITH_MARIADB=1)
    target_link_libraries(openads_core PUBLIC openads_libmariadb)
endif()

if(OPENADS_WITH_ODBC)
    target_compile_definitions(openads_core PUBLIC OPENADS_WITH_ODBC=1)
    if(WIN32)
        target_link_libraries(openads_core PUBLIC odbc32)
    else()
        target_link_libraries(openads_core PUBLIC odbc)
    endif()
endif()

if(OPENADS_WITH_FIREBIRD)
    target_compile_definitions(openads_core PUBLIC OPENADS_WITH_FIREBIRD=1)
    # The Firebird client SDK is located via FIREBIRD_ROOT (no hard-coded
    # paths). The native connection links libfbclient via its import library.
    if(FIREBIRD_ROOT)
        target_include_directories(openads_core PRIVATE ${FIREBIRD_ROOT}/include)
        target_link_libraries(openads_core PUBLIC ${FIREBIRD_ROOT}/lib/fbclient_ms.lib)
        if(MSVC AND OPENADS_FIREBIRD_DELAYLOAD)
            # Delay-load so fbclient.dll is only required when a Firebird
            # connection is actually opened: the unit-test binary links this
            # backend but must still run where the Firebird runtime is absent.
            # delayimp.lib provides the __delayLoadHelper2 thunk.
            target_link_options(openads_core INTERFACE /DELAYLOAD:fbclient.dll)
            target_link_libraries(openads_core PUBLIC delayimp.lib)
        endif()
    else()
        message(FATAL_ERROR
            "OPENADS_WITH_FIREBIRD=ON but FIREBIRD_ROOT was not set "
            "(point it at the Firebird client SDK: include/ibase.h + "
            "lib/fbclient_ms.lib)")
    endif()
endif()

# ----------------------------------------------------------------------
# Drop-in replacement DLL: ace32.dll (x86) / ace64.dll (x64).
# Harbour's contrib/rddads links against ace32.lib / ace64.lib import
# libraries; placing this DLL on PATH replaces the SAP-shipped
# ace32.dll / ace64.dll without recompiling consuming applications.
# ----------------------------------------------------------------------
add_library(openads_ace SHARED
    abi/legacy_crt_shims.cpp
)

if(WIN32)
    # Default .def is name-based exports (undecorated, __cdecl). For x86
    # Harbour (rddads) compatibility, openads_ace_x86.def exports
    # __stdcall-decorated (@N) names instead. See dist/import-libs/gen.ps1.
    # Apps linked against an
    # ace32.lib / ace64.lib whose imports are by-ordinal need a
    # custom .def whose @N values match the SAP-shipped DLL the
    # user is replacing — generate it once per-machine with
    # tools/scripts/sap_ordinals_to_def.py and pass it in via
    # -DOPENADS_ACE_DEF=<path> at configure time.
    set(OPENADS_ACE_DEF "${CMAKE_CURRENT_SOURCE_DIR}/openads_ace.def"
        CACHE FILEPATH
        "Path to the .def file driving openads_ace exports.")
    target_sources(openads_ace PRIVATE ${OPENADS_ACE_DEF})
    message(STATUS "openads_ace exports def: ${OPENADS_ACE_DEF}")
endif()

target_include_directories(openads_ace
    PUBLIC  ${CMAKE_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_SOURCE_DIR}/third_party/tinyaes
)

target_link_libraries(openads_ace PRIVATE openads_core)

# studio.embed — when OPENADS_WITH_HTTP is on, fold the same
# Studio HTTP console that openads_serverd ships into the DLL,
# behind the AdsStudioStart / AdsStudioStop / AdsStudioPort
# extension entry points (plus the OPENADS_STUDIO_PORT env-var
# auto-start hook). Without HTTP support the three entry points
# are still exported but return AE_FUNCTION_NOT_AVAILABLE so
# callers can detect the build flavour at runtime.
target_sources(openads_ace PRIVATE abi/studio_embed.cpp)
if(OPENADS_WITH_HTTP)
    target_sources(openads_ace PRIVATE
        ${CMAKE_SOURCE_DIR}/tools/serverd/http_server.cpp)
    target_compile_definitions(openads_ace PRIVATE OPENADS_WITH_HTTP=1)
    target_include_directories(openads_ace PRIVATE ${CMAKE_SOURCE_DIR})
    target_link_libraries(openads_ace PRIVATE
        httplib::httplib
        nlohmann_json::nlohmann_json)
endif()

# Output as openace64 / openace32 to distinguish from SAP's ace64.dll / ace32.dll.
# This allows both DLLs to coexist in the same PHP installation.
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set_target_properties(openads_ace PROPERTIES OUTPUT_NAME "openace64")
else()
    set_target_properties(openads_ace PROPERTIES OUTPUT_NAME "openace32")
endif()
