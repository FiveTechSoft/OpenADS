cmake_minimum_required(VERSION 3.20)

project(OpenADS
    VERSION 1.2.0
    LANGUAGES C CXX
    DESCRIPTION "Open-source ADS-compatible engine"
)

# Resolve the public version string the binary reports through
# `--version`. Prefer `git describe --tags` so a build straight off
# tag v1.0.0-rc13 reports "1.0.0-rc13", a build off main (between
# tags) reports "1.0.0-rc13-7-g<sha>[-dirty]", and a tarball without
# a .git tree falls back to ${PROJECT_VERSION} from project() above.
# The previous hard-coded "1.0.0-rc1" string drifted six releases
# behind reality and surprised users running rc12.
set(OPENADS_VERSION_STR "${PROJECT_VERSION}")
find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --always --dirty
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE _git_describe
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE _git_rc)
    if(_git_rc EQUAL 0 AND NOT _git_describe STREQUAL "")
        # Strip a leading "v" so tags v1.0.0-rc13 surface as 1.0.0-rc13.
        string(REGEX REPLACE "^v" "" OPENADS_VERSION_STR "${_git_describe}")
    endif()
endif()
message(STATUS "OpenADS version: ${OPENADS_VERSION_STR}")
add_compile_definitions(OPENADS_VERSION_STR="${OPENADS_VERSION_STR}")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# openads_core is a STATIC library that gets linked into both an
# executable (openads_serverd, tests) and a SHARED library
# (openads_ace -> ace64.dll / libace64.so). On Linux / macOS the
# shared-library link path requires every translation unit pulled
# in to be position-independent, otherwise we hit
# `R_X86_64_TPOFF32 ... can not be used when making a shared
# object; recompile with -fPIC`. Force PIC globally — Windows
# silently ignores the flag, so this is a no-op there.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(OPENADS_BUILD_TESTS "Build OpenADS unit tests" ON)
option(OPENADS_WARNINGS_AS_ERRORS "Treat warnings as errors" ON)

# M12.12 — real TLS transport via vendored mbedtls (Apache-2.0).
# Off by default so existing builds don't pull a network dependency
# at configure time. When ON, FetchContent downloads mbedtls 3.6 LTS
# at configure time and links it into openads_core; AdsConnect60
# with a `tls://` URI then opens a TlsTransport instead of returning
# AE_FUNCTION_NOT_AVAILABLE.
option(OPENADS_WITH_TLS "Enable TLS transport (vendors mbedtls 3.6 LTS via FetchContent)" OFF)

# studio.web — embedded HTTP web console inside openads_serverd.
# When ON, serverd gains a `--http-port N` flag that exposes a
# single-page admin UI + REST API on top of the running engine,
# so the database can be administered from any browser on the LAN
# without installing a desktop client. Vendors cpp-httplib (MIT)
# and nlohmann/json (MIT) at configure time via FetchContent.
option(OPENADS_WITH_HTTP "Enable embedded HTTP web console (Studio) in openads_serverd / ace64.dll" ON)

# OpenADS Plus — optional SQLite-backed table driver (static amalgamation).
# On by default; set OFF for dev builds that skip the FetchContent download.
option(OPENADS_WITH_SQLITE "Enable the SQLite-backed table driver (vendors the SQLite amalgamation via FetchContent)" ON)
# Optional PostgreSQL table driver (libpq). OFF by default.
option(OPENADS_WITH_POSTGRESQL "Enable PostgreSQL-backed table driver (libpq)" OFF)
# Optional MariaDB/MySQL table driver (libmariadb). OFF by default.
option(OPENADS_WITH_MARIADB "Enable MariaDB-backed table driver (libmariadb)" OFF)
# Optional generic ODBC table driver (system ODBC driver manager). OFF by default.
option(OPENADS_WITH_ODBC "Enable ODBC-backed table driver (system ODBC driver manager)" OFF)
# Native MS SQL Server / TDS 7.4 backend. Requires OPENADS_WITH_TLS=ON
# (uses mbedtls for the TLS layer that SQL Server mandates on port 1433
# since MSSQL 2022 / Azure SQL). No external library is downloaded beyond
# mbedtls which OPENADS_WITH_TLS already fetches. OFF by default.
option(OPENADS_WITH_MSSQL "Enable native MS SQL Server / TDS backend" OFF)
# Opt-in: build the SQLite backend from the SQLite3 Multiple Ciphers
# amalgamation (SQLCipher-compatible encryption at rest, self-contained — no
# external OpenSSL) instead of plain SQLite. Vendored under
# third_party/sqlcipher/ (not committed — see .gitignore).
option(OPENADS_WITH_SQLCIPHER "Build the SQL backend with SQLCipher-compatible encryption (SQLite3 Multiple Ciphers)" OFF)
if(OPENADS_WITH_SQLCIPHER)
    set(OPENADS_WITH_SQLITE ON CACHE BOOL "" FORCE)
endif()
# Optional native Firebird table driver (libfbclient; embedded .fdb in-process
# or TCP server). OFF by default. Requires the Firebird client SDK
# (fbclient + ibase.h) discoverable via FIREBIRD_ROOT.
option(OPENADS_WITH_FIREBIRD "Enable the native Firebird-backed table driver (libfbclient)" OFF)

# Pull in mbedtls FIRST so our strict /WX -Werror flags don't bleed
# into the upstream sources (C4200 zero-sized arrays etc). Each
# OpenADS target adds the strict flags target-locally below via
# `apply_openads_warnings(<target>)`.
if(OPENADS_WITH_TLS)
    include(FetchContent)
    set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
    set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
    # Force a STATIC mbedtls so the OpenADS DLL / server binary
    # has no runtime dependency on a system OpenSSL. Reported by
    # downstream users: a release ZIP without those system DLLs
    # fails to launch on a clean Windows / macOS box. Static
    # linking sidesteps that whole class of "missing libssl/
    # libcrypto" deployment errors.
    set(USE_STATIC_MBEDTLS_LIBRARY ON  CACHE BOOL "" FORCE)
    set(USE_SHARED_MBEDTLS_LIBRARY OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS          OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        mbedtls
        GIT_REPOSITORY https://github.com/Mbed-TLS/mbedtls.git
        GIT_TAG        v3.6.2
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(mbedtls)
    message(STATUS "OpenADS: TLS enabled via vendored mbedtls 3.6.2 (statically linked)")
endif()

if(OPENADS_WITH_HTTP)
    include(FetchContent)
    # cpp-httplib auto-detects OpenSSL / zlib / brotli on the host
    # and pulls them in as link libraries on its INTERFACE target.
    # That collides with the OpenADS no-runtime-DLL deployment
    # promise (the shipped DLL must not depend on libssl /
    # libcrypto). Force-disable the auto-link so Linux/macOS
    # builds match the Windows side: plain HTTP only, no system
    # OpenSSL pulled into ace64.dll / libace64.so.
    set(HTTPLIB_USE_OPENSSL_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
    set(HTTPLIB_USE_ZLIB_IF_AVAILABLE    OFF CACHE BOOL "" FORCE)
    set(HTTPLIB_USE_BROTLI_IF_AVAILABLE  OFF CACHE BOOL "" FORCE)
    set(HTTPLIB_REQUIRE_OPENSSL          OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        cpp_httplib
        GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
        GIT_TAG        v0.18.5
        GIT_SHALLOW    TRUE
    )
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG        v3.11.3
        GIT_SHALLOW    TRUE
    )
    set(JSON_BuildTests OFF CACHE INTERNAL "")
    FetchContent_MakeAvailable(cpp_httplib nlohmann_json)
    message(STATUS "OpenADS: HTTP web console enabled (cpp-httplib + nlohmann/json)")
endif()

if(OPENADS_WITH_SQLITE)
    if(OPENADS_WITH_SQLCIPHER)
        # SQLite3 Multiple Ciphers amalgamation — SQLCipher-compatible AES at
        # rest, self-contained. Vendored under third_party/sqlcipher/.
        set(_openads_sqlite_src "${CMAKE_SOURCE_DIR}/third_party/sqlcipher/sqlite3mc_amalgamation.c")
        set(_openads_sqlite_inc "${CMAKE_SOURCE_DIR}/third_party/sqlcipher")
        if(NOT EXISTS "${_openads_sqlite_src}")
            message(FATAL_ERROR
                "OPENADS_WITH_SQLCIPHER=ON needs the SQLite3 Multiple Ciphers "
                "amalgamation at third_party/sqlcipher/sqlite3mc_amalgamation.c")
        endif()
        message(STATUS "OpenADS: SQL backend with SQLCipher-compatible encryption (SQLite3 Multiple Ciphers)")
    else()
        # Use a locally vendored amalgamation under third_party/sqlite/ when
        # present (offline / air-gapped builds); otherwise fetch it. The
        # amalgamation is not committed — see .gitignore.
        set(_openads_sqlite_src "${CMAKE_SOURCE_DIR}/third_party/sqlite/sqlite3.c")
        set(_openads_sqlite_inc "${CMAKE_SOURCE_DIR}/third_party/sqlite")
        if(NOT EXISTS "${_openads_sqlite_src}")
            include(FetchContent)
            FetchContent_Declare(
                sqlite_amalgamation
                URL https://www.sqlite.org/2024/sqlite-amalgamation-3460100.zip
                DOWNLOAD_EXTRACT_TIMESTAMP TRUE
            )
            FetchContent_MakeAvailable(sqlite_amalgamation)
            set(_openads_sqlite_src "${sqlite_amalgamation_SOURCE_DIR}/sqlite3.c")
            set(_openads_sqlite_inc "${sqlite_amalgamation_SOURCE_DIR}")
        endif()
    endif()
    add_library(openads_sqlite3 STATIC "${_openads_sqlite_src}")
    target_include_directories(openads_sqlite3 PUBLIC "${_openads_sqlite_inc}")
    target_compile_definitions(openads_sqlite3 PRIVATE
        SQLITE_THREADSAFE=1
        SQLITE_DEFAULT_MEMSTATUS=0)
    if(MSVC)
        target_compile_options(openads_sqlite3 PRIVATE /wd4267 /wd4244)
    else()
        target_compile_options(openads_sqlite3 PRIVATE -Wno-conversion)
    endif()
    message(STATUS "OpenADS: SQLite-backed table driver enabled")
endif()

if(OPENADS_WITH_POSTGRESQL)
    set(_openads_pg_linked FALSE)
    set(OPENADS_LIBPQ_INCLUDE "" CACHE PATH "Directory containing libpq-fe.h")
    set(OPENADS_LIBPQ_LIBRARY "" CACHE FILEPATH "libpq import library (.lib/.a)")
    if(OPENADS_LIBPQ_INCLUDE AND OPENADS_LIBPQ_LIBRARY)
        set(_openads_pg_inc "${OPENADS_LIBPQ_INCLUDE}")
        set(_openads_pg_lib "${OPENADS_LIBPQ_LIBRARY}")
    endif()
    if(NOT _openads_pg_lib)
        if(DEFINED ENV{OPENADS_LIBPQ_LIBRARY})
            set(_openads_pg_lib "$ENV{OPENADS_LIBPQ_LIBRARY}")
        endif()
        if(DEFINED ENV{OPENADS_LIBPQ_INCLUDE})
            set(_openads_pg_inc "$ENV{OPENADS_LIBPQ_INCLUDE}")
        endif()
    endif()
    if(NOT _openads_pg_lib OR NOT _openads_pg_inc)
        if(DEFINED ENV{OPENADS_TOOLCHAIN_ROOT})
            set(_openads_toolchain_root "$ENV{OPENADS_TOOLCHAIN_ROOT}")
        elseif(DEFINED OPENADS_TOOLCHAIN_ROOT)
            set(_openads_toolchain_root "${OPENADS_TOOLCHAIN_ROOT}")
        endif()
        if(_openads_toolchain_root)
            if(NOT _openads_pg_inc)
                find_path(_openads_pg_inc libpq-fe.h
                    PATHS "${_openads_toolchain_root}/pgsql/include"
                    NO_DEFAULT_PATH)
            endif()
            if(NOT _openads_pg_lib)
                if(CMAKE_SIZEOF_VOID_P EQUAL 4)
                    find_library(_openads_pg_lib NAMES libpq pq
                        PATHS "${_openads_toolchain_root}/libpq/x86"
                              "${_openads_toolchain_root}/libpq/x86/lib"
                        NO_DEFAULT_PATH)
                else()
                    find_library(_openads_pg_lib NAMES pq libpq
                        PATHS "${_openads_toolchain_root}/pgsql/lib"
                        NO_DEFAULT_PATH)
                endif()
            endif()
        endif()
    endif()
    if(_openads_pg_inc AND _openads_pg_lib)
        add_library(openads_libpq INTERFACE)
        target_include_directories(openads_libpq INTERFACE "${_openads_pg_inc}")
        target_link_libraries(openads_libpq INTERFACE "${_openads_pg_lib}")
        set(_openads_pg_linked TRUE)
        message(STATUS "OpenADS: PostgreSQL backend (libpq: ${_openads_pg_lib})")
    endif()
    if(NOT _openads_pg_linked)
        find_package(PostgreSQL REQUIRED)
        add_library(openads_libpq INTERFACE)
        target_include_directories(openads_libpq INTERFACE "${PostgreSQL_INCLUDE_DIRS}")
        target_link_libraries(openads_libpq INTERFACE PostgreSQL::PostgreSQL)
        message(STATUS "OpenADS: PostgreSQL backend (libpq via find_package)")
    endif()
endif()

if(OPENADS_WITH_MARIADB)
    set(_openads_maria_linked FALSE)
    set(OPENADS_LIBMARIADB_INCLUDE "" CACHE PATH "Directory containing mysql.h")
    set(OPENADS_LIBMARIADB_LIBRARY "" CACHE FILEPATH "libmariadb import library (.lib/.a)")
    if(OPENADS_LIBMARIADB_INCLUDE AND OPENADS_LIBMARIADB_LIBRARY)
        set(_openads_maria_inc "${OPENADS_LIBMARIADB_INCLUDE}")
        set(_openads_maria_lib "${OPENADS_LIBMARIADB_LIBRARY}")
    endif()
    if(NOT _openads_maria_lib)
        if(DEFINED ENV{OPENADS_LIBMARIADB_LIBRARY})
            set(_openads_maria_lib "$ENV{OPENADS_LIBMARIADB_LIBRARY}")
        endif()
        if(DEFINED ENV{OPENADS_LIBMARIADB_INCLUDE})
            set(_openads_maria_inc "$ENV{OPENADS_LIBMARIADB_INCLUDE}")
        endif()
    endif()
    if(NOT _openads_maria_lib OR NOT _openads_maria_inc)
        if(DEFINED ENV{OPENADS_TOOLCHAIN_ROOT})
            set(_openads_toolchain_root "$ENV{OPENADS_TOOLCHAIN_ROOT}")
        elseif(DEFINED OPENADS_TOOLCHAIN_ROOT)
            set(_openads_toolchain_root "${OPENADS_TOOLCHAIN_ROOT}")
        endif()
        if(_openads_toolchain_root)
            if(NOT _openads_maria_inc)
                find_path(_openads_maria_inc mysql.h
                    PATHS "${_openads_toolchain_root}/mariadb/include"
                    NO_DEFAULT_PATH)
            endif()
            if(NOT _openads_maria_lib)
                if(CMAKE_SIZEOF_VOID_P EQUAL 4)
                    find_library(_openads_maria_lib NAMES libmariadb
                        PATHS "${_openads_toolchain_root}/mariadb/lib"
                        NO_DEFAULT_PATH)
                else()
                    find_library(_openads_maria_lib NAMES libmariadb64 libmariadb
                        PATHS "${_openads_toolchain_root}/mariadb/lib"
                        NO_DEFAULT_PATH)
                endif()
            endif()
        endif()
    endif()
    if(_openads_maria_inc AND _openads_maria_lib)
        add_library(openads_libmariadb INTERFACE)
        target_include_directories(openads_libmariadb INTERFACE "${_openads_maria_inc}")
        target_link_libraries(openads_libmariadb INTERFACE "${_openads_maria_lib}")
        set(_openads_maria_linked TRUE)
        message(STATUS "OpenADS: MariaDB backend (libmariadb: ${_openads_maria_lib})")
    endif()
    if(NOT _openads_maria_linked)
        message(FATAL_ERROR "OPENADS_WITH_MARIADB=ON but libmariadb was not found")
    endif()
endif()

if(MSVC)
    add_compile_options(/W4 /permissive-)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
    if(OPENADS_WARNINGS_AS_ERRORS)
        add_compile_options(/WX)
    endif()
elseif(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # MinGW/winlibs: static link toolchain runtime; libpq.dll still loads at runtime.
    add_link_options(-static)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wconversion)
    # _CRT_SECURE_NO_WARNINGS: needed when clang-cl targets MSVC CRT on Windows;
    # harmless on Linux/macOS where this define has no effect.
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
    if(OPENADS_WARNINGS_AS_ERRORS)
        add_compile_options(-Werror)
    endif()
endif()

add_subdirectory(src)
add_subdirectory(tools/serverd)
add_subdirectory(tools/bench)
add_subdirectory(tools/stress)
add_subdirectory(tools/mgprobe)
add_subdirectory(tools/import_dd)

if(OPENADS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
