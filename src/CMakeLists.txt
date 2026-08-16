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
    platform/fs_sandbox.cpp
    platform/time.cpp
    platform/thread.cpp
    platform/proc.cpp
    engine/server_fs.cpp
    abi/ace_exports.cpp
    abi/ace_stubs.cpp
    abi/adsfunc.c
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
    engine/sha256.cpp
    engine/pbkdf2.cpp
    engine/tx.cpp
    engine/tx_log.cpp
    engine/lsn_map.cpp
    engine/data_dict.cpp
    engine/dd_native.cpp
    engine/backup.cpp
    engine/fts.cpp
    engine/repl_queue.cpp
    engine/repl_catalog.cpp
    engine/repl_capture.cpp
    engine/repl_apply.cpp
    engine/codepage.cpp
    engine/oem_collation.cpp
    engine/script/value.cpp
    engine/script/lexer.cpp
    engine/script/parser.cpp
    engine/script/exec.cpp
    sql/parser.cpp
    drivers/dbf_common.cpp
    drivers/cache/cached_driver.cpp
    drivers/cdx/cdx_driver.cpp
    drivers/cdx/cdx_index.cpp
    drivers/ntx/ntx_driver.cpp
    drivers/ntx/ntx_index.cpp
    drivers/adt/adt_driver.cpp
    drivers/adm/adm_memo.cpp
    drivers/adi/adi_index.cpp
    drivers/dbt/dbt_memo.cpp
    drivers/fpt/fpt_memo.cpp
    drivers/memory/memory_driver.cpp
    network/wire.cpp
    network/frame_reader.cpp
    network/server.cpp
    network/session.cpp
    network/worker_pool.cpp
    network/client.cpp
    network/remote_index_nav.cpp
    mgmt/mg_collector.cpp
    mgmt/error_log.cpp
    network/mg_wire.cpp
    sql_backend/uri.cpp
    sql_backend/sql_common.cpp
    sql_backend/sql_ddl.cpp
    sql_backend/sql_acl_store.cpp
    sql_backend/sql_system_catalog.cpp
    sql_backend/enterprise_config.cpp
)
# hbnetio Bridge (VFS adapter, distributed lock manager, RPC bridge)
if(OPENADS_WITH_HBNETIO_BRIDGE)
    target_sources(openads_core PRIVATE
        ${CMAKE_SOURCE_DIR}/contrib/hbnetio_bridge/vfs_adapter.cpp
        ${CMAKE_SOURCE_DIR}/contrib/hbnetio_bridge/dist_lock_mgr.cpp
        ${CMAKE_SOURCE_DIR}/contrib/hbnetio_bridge/rpc_bridge.cpp
    )
    target_compile_definitions(openads_core PUBLIC OPENADS_WITH_HBNETIO_BRIDGE=1)
endif()

# Targets linking openads_core get the ACE implementations, not the DLL
# exports, so on x86 they must see the __cdecl declarations (v1.8.48):
# without this their __stdcall-decorated (_AdsXxx@N) references cannot
# resolve against the core's plain _AdsXxx symbols.
if(MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    target_compile_definitions(openads_core PUBLIC OPENADS_ACE_IMPLEMENTATION)
endif()

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
        sql_backend/oracle_uri.cpp
        sql_backend/odbc_backend.cpp
        sql_backend/odbc_connection.cpp
    )
endif()

if(OPENADS_WITH_MSSQL)
    # The native MSSQL backend tunnels the TLS handshake inside TDS packets,
    # so it links mbedtls symbols directly â€” require OPENADS_WITH_TLS.
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
    # SQLite/ODBC/PostgreSQL/MariaDB backends) â€” do NOT add it again here.
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
        ${CMAKE_SOURCE_DIR}/include/openads
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/third_party/tinyaes
)

target_link_libraries(openads_core PRIVATE openads_aes_impl)

# ace_exports.cpp is a very large TU that overflows the Windows COFF object
# section limit. MSVC needs /bigobj (C1128); MinGW's GNU assembler needs the
# equivalent -Wa,-mbig-obj. Neither flag exists on ELF/Mach-O toolchains, so
# guard by compiler â€” never pass /bigobj to GCC/Clang.
if(MSVC)
    set_source_files_properties(abi/ace_exports.cpp PROPERTIES COMPILE_OPTIONS "/bigobj;/utf-8")
elseif(MINGW)
    set_source_files_properties(abi/ace_exports.cpp PROPERTIES COMPILE_OPTIONS "-Wa,-mbig-obj")
endif()

if(UNIX)
    # RCB 2026-07-10 â€” CMAKE_DL_LIBS for dladdr in platform/path.cpp
    # (module_directory(), used to find adslocal.cfg next to the DLL).
    target_link_libraries(openads_core PUBLIC pthread ${CMAKE_DL_LIBS})
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
    if(FIREBIRD_ROOT)
        target_include_directories(openads_core PRIVATE ${FIREBIRD_ROOT}/include)
        target_link_libraries(openads_core PUBLIC ${FIREBIRD_ROOT}/lib/fbclient_ms.lib)
        if(MSVC AND OPENADS_FIREBIRD_DELAYLOAD)
            target_link_options(openads_core INTERFACE /DELAYLOAD:fbclient.dll)
            target_link_libraries(openads_core PUBLIC delayimp.lib)
        endif()
    elseif(MSVC)
        message(FATAL_ERROR
            "OPENADS_WITH_FIREBIRD=ON but FIREBIRD_ROOT was not set "
            "(point it at the Firebird client SDK: include/ibase.h + "
            "lib/fbclient_ms.lib)")
    else()
        find_path(_openads_fb_include ibase.h
            PATHS /usr/include /usr/include/firebird)
        find_library(_openads_fb_client NAMES fbclient)
        if(_openads_fb_include AND _openads_fb_client)
            target_include_directories(openads_core PRIVATE "${_openads_fb_include}")
            target_link_libraries(openads_core PUBLIC "${_openads_fb_client}")
            message(STATUS "OpenADS: Firebird backend (libfbclient: ${_openads_fb_client})")
        else()
            message(FATAL_ERROR
                "OPENADS_WITH_FIREBIRD=ON but libfbclient was not found "
                "(install firebird3.0-dev or set FIREBIRD_ROOT)")
        endif()
    endif()
endif()

# ----------------------------------------------------------------------
# Drop-in replacement DLL: ace32.dll (x86) / ace64.dll (x64).
# Harbour's contrib/rddads links against ace32.lib / ace64.lib import
# libraries; placing this DLL on PATH replaces the SAP-shipped
# ace32.dll / ace64.dll without recompiling consuming applications.
# ----------------------------------------------------------------------
add_library(openads_ace SHARED
    abi/adsfunc.c
    abi/legacy_crt_shims.cpp
)

if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    # x86-only: __stdcall aliases (AdsXxx@N) so 32-bit Harbour rddads.lib /
    # FiveWin apps built for SAP ACE's stdcall ABI can link AND load
    # (ace32.dll must export the decorated names; without them every
    # 32-bit rddads app died with 0xC0000139 at startup).
    # The same file also /export-aliases each entry point under its plain
    # undecorated name (AdsXxx) for cdecl callers (MinGW-built rddads)
    # and GetProcAddress probes (serverd's dll_probe_ace).
    target_sources(openads_ace PRIVATE abi/ace_stdcall_x86.c)
endif()

if(WIN32)
    # Default .def is name-based exports (undecorated, __cdecl). For x86
    # Harbour (rddads) compatibility, openads_ace_x86_stdcall.def exports
    # __stdcall-decorated (@N) names instead. See dist/import-libs/gen.ps1.
    # Apps linked against an
    # ace32.lib / ace64.lib whose imports are by-ordinal need a
    # custom .def whose @N values match the SAP-shipped DLL the
    # user is replacing â€” generate it once per-machine with
    # tools/scripts/sap_ordinals_to_def.py and pass it in via
    # -DOPENADS_ACE_DEF=<path> at configure time.
    if(CMAKE_SIZEOF_VOID_P EQUAL 4)
        # 32-bit: the DLL must export the __stdcall-decorated (_AdsXxx@N)
        # names 32-bit rddads.lib imports - with a plain-name def every
        # 32-bit rddads app dies with 0xC0000139 at startup. The decorated
        # exports come from the dllexport wrappers in abi/ace_stdcall_x86.c;
        # this def adds the oads_* C API and the legacy-CRT shims.
        set(_ace_def_default "${CMAKE_CURRENT_SOURCE_DIR}/openads_ace_x86_stdcall.def")
    else()
        set(_ace_def_default "${CMAKE_CURRENT_SOURCE_DIR}/openads_ace.def")
    endif()
    set(OPENADS_ACE_DEF "${_ace_def_default}"
        CACHE FILEPATH
        "Path to the .def file driving openads_ace exports.")
    if(MINGW)
        # GNU ld does not recognize the MSVC-style .def when it opens with
        # `;` comment lines or a nameless `LIBRARY` line: it fails format
        # detection, treats the file as a linker script, and errors on the
        # first `;`. Regenerate a GNU-clean def (EXPORTS + names only) at
        # configure time and feed that to the linker instead. Read with
        # file(READ) â€” not file(STRINGS) â€” so the `;` in comment lines is
        # not misparsed as a CMake list separator.
        file(READ "${OPENADS_ACE_DEF}" _ace_def_raw)
        string(REGEX REPLACE "(^|\n)[ \t]*;[^\n]*" "\\1" _ace_def_clean "${_ace_def_raw}")
        string(REGEX REPLACE "(^|\n)[ \t]*LIBRARY[ \t]*(\n|$)" "\\1" _ace_def_clean "${_ace_def_clean}")
        string(REGEX REPLACE "^[ \t\n]+" "" _ace_def_clean "${_ace_def_clean}")
        set(_ace_def_gnu "${CMAKE_CURRENT_BINARY_DIR}/openads_ace.gnu.def")
        file(WRITE "${_ace_def_gnu}" "${_ace_def_clean}")
        target_sources(openads_ace PRIVATE "${_ace_def_gnu}")
        message(STATUS "openads_ace exports def (MinGW-clean): ${_ace_def_gnu}")
    else()
        target_sources(openads_ace PRIVATE ${OPENADS_ACE_DEF})
        message(STATUS "openads_ace exports def: ${OPENADS_ACE_DEF}")
    endif()
endif()

target_include_directories(openads_ace
    PUBLIC  ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/include/openads
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_SOURCE_DIR}/third_party/tinyaes
)

target_link_libraries(openads_ace PRIVATE openads_core)

# studio.embed â€” when OPENADS_WITH_HTTP is on, fold the same
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