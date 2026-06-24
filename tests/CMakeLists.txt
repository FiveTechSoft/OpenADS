# Test fixtures synthesise binary DBF / index / DD blobs with integer
# literals and `buf[int_index]` accesses. Under clang/gcc -Werror those
# trip -Wconversion / -Wsign-conversion even though the narrowing is
# intentional (masking bytes). Relax just those two for test code — the
# library in src/ keeps the full strict warning set. MSVC is unaffected.
if(NOT MSVC)
    add_compile_options(-Wno-conversion -Wno-sign-conversion)
    # __COUNTER__ is a widely-supported GCC/Clang extension used by doctest.
    # Clang 19+ pedantically classifies it as -Wc2y-extensions; suppress it.
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
        include(CheckCXXCompilerFlag)
        check_cxx_compiler_flag(-Wno-c2y-extensions HAS_NO_C2Y)
        if(HAS_NO_C2Y)
            add_compile_options(-Wno-c2y-extensions)
        endif()
    endif()
endif()

add_executable(openads_unit_tests
    unit/doctest_main.cpp
    unit/util_result_test.cpp
    unit/util_span_test.cpp
    unit/util_log_test.cpp
    unit/platform_file_test.cpp
    unit/platform_lock_test.cpp
    unit/platform_mmap_test.cpp
    unit/platform_path_test.cpp
    unit/platform_time_test.cpp
    unit/platform_thread_test.cpp
    unit/dbf_header_test.cpp
    unit/dbf_field_test.cpp
    unit/dbf_record_test.cpp
    unit/dbf_write_test.cpp
    unit/ntx_driver_test.cpp
    unit/lock_mgr_test.cpp
    unit/engine_table_test.cpp
    unit/engine_table_write_test.cpp
    unit/engine_cursor_test.cpp
    unit/session_handle_registry_test.cpp
    unit/handle_registry_kind_of_test.cpp
    unit/backend_registry_test.cpp
    unit/session_connection_test.cpp
    unit/abi_smoke_test.cpp
    unit/abi_write_smoke_test.cpp
    unit/ntx_index_test.cpp
    unit/cdx_index_test.cpp
    unit/engine_order_test.cpp
    unit/engine_scope_test.cpp
    unit/abi_index_smoke_test.cpp
    unit/aof_expr_test.cpp
    unit/aof_eval_test.cpp
    unit/abi_aof_test.cpp
    unit/aes_test.cpp
    unit/dbt_memo_test.cpp
    unit/fpt_memo_test.cpp
    unit/engine_memo_test.cpp
    unit/abi_m4_smoke_test.cpp
    unit/abi_m5_smoke_test.cpp
    unit/tx_log_test.cpp
    unit/abi_m5x_recovery_test.cpp
    unit/abi_savepoint_test.cpp
    unit/abi_tx_rollback_append_test.cpp
    unit/data_dict_test.cpp
    unit/abi_dd_test.cpp
    unit/sql_parser_test.cpp
    unit/abi_sql_smoke_test.cpp
    unit/abi_plus_sqlite_read_test.cpp
    unit/abi_plus_sqlite_seek_test.cpp
    unit/abi_plus_sqlite_passthrough_test.cpp
    unit/abi_plus_sqlcipher_read_test.cpp
    unit/lsn_map_test.cpp
    unit/index_expr_test.cpp
    unit/abi_m92_real_test.cpp
    unit/abi_create_table_test.cpp
    unit/abi_refresh_extract_test.cpp
    unit/abi_create_index61_test.cpp
    unit/abi_index_expr_validation_test.cpp
    unit/abi_zap_pack_test.cpp
    unit/abi_pack_reindex_softseek_test.cpp
    unit/abi_record_count_filter_test.cpp
    unit/abi_reindex_test.cpp
    unit/ntx_multilevel_test.cpp
    unit/cdx_multilevel_test.cpp
    unit/abi_memo_large_test.cpp
    unit/abi_ordsetfocus_test.cpp
    unit/abi_seek_empty_test.cpp
    unit/abi_open_concurrent_header_test.cpp
    unit/cdx_peer_append_5000_test.cpp
    unit/abi_seek_numeric_alias_test.cpp
    unit/abi_open_index_order_test.cpp
    unit/abi_ordlistadd_path_test.cpp
    unit/abi_gobottom_filtered_test.cpp
    unit/abi_rddtst_repro_test.cpp
    unit/engine_navigation_empty_test.cpp
    unit/abi_deleted_records_test.cpp
    unit/abi_index_key_count_test.cpp
    unit/abi_copy_table_test.cpp
    unit/abi_find_table_test.cpp
    unit/abi_binary_memo_test.cpp
    unit/abi_ntx_multitag_test.cpp
    unit/abi_ordnumber_test.cpp
    unit/abi_server_info_test.cpp
    unit/abi_unicode_test.cpp
    unit/abi_lock_retry_test.cpp
    unit/abi_fts_test.cpp
    unit/abi_custom_key_test.cpp
    unit/abi_fts_search_test.cpp
    unit/abi_sql_contains_test.cpp
    unit/abi_sql_operators_test.cpp
    unit/index_expr_utf8_test.cpp
    unit/abi_misc_impls_test.cpp
    unit/abi_mgmt_test.cpp
    unit/mg_collector_test.cpp
    unit/mg_wire_test.cpp
    unit/abi_mgmt_remote_test.cpp
    unit/abi_dd_crud_test.cpp
    unit/abi_restructure_test.cpp
    unit/abi_versioned_overloads_test.cpp
    unit/abi_remote_overloads_test.cpp
    unit/abi_remote_prefetch_test.cpp
    unit/abi_append_autolock_test.cpp
    unit/abi_dd_persistence_test.cpp
    unit/dbf_vfp_test.cpp
    unit/abi_sql_or_test.cpp
    unit/abi_index_direction_test.cpp
    unit/abi_sql_insert_test.cpp
    unit/abi_sql_named_param_test.cpp
    unit/abi_sql_orderby_test.cpp
    unit/abi_sql_dml_test.cpp
    unit/abi_sql_projection_test.cpp
    unit/abi_sql_ddl_test.cpp
    unit/abi_sql_agg_test.cpp
    unit/dbf_vfp_autoinc_test.cpp
    unit/abi_sql_join_test.cpp
    unit/abi_sql_in_test.cpp
    unit/abi_sql_exists_test.cpp
    unit/abi_sql_scalar_test.cpp
    unit/abi_sql_union_test.cpp
    unit/abi_sql_distinct_limit_test.cpp
    unit/abi_sql_case_test.cpp
    unit/abi_aep_test.cpp
    unit/abi_encryption_test.cpp
    unit/abi_sql_scalar_fn_test.cpp
    unit/abi_sql_insert_select_test.cpp
    unit/abi_sql_derived_test.cpp
    unit/abi_sql_null_window_test.cpp
    unit/network_wire_test.cpp
    unit/abi_collation_codepage_test.cpp
    unit/network_socket_test.cpp
    unit/network_server_test.cpp
    unit/abi_adt_smoke_test.cpp
    unit/abi_adt_create_test.cpp
    unit/abi_adi_create_test.cpp
    unit/abi_adi_types_test.cpp
    unit/abi_adi_multilevel_build_test.cpp
    unit/abi_adt_scope_validation_test.cpp
    unit/abi_adi_smoke_test.cpp
    unit/abi_adt_sql_test.cpp
    unit/abi_dd_table_prop_test.cpp
    unit/abi_dd_user_prop_test.cpp
    unit/abi_enum_test.cpp
    unit/abi_dd_ri_test.cpp
    unit/abi_dd_auth_test.cpp
    unit/abi_dd_perms_test.cpp
    unit/abi_dd_field_prop_test.cpp
    unit/abi_dd_trigger_test.cpp
    unit/abi_dd_trigger_fire_test.cpp
    unit/abi_dd_proc_view_test.cpp
    unit/abi_sql_system_tables_test.cpp
    unit/abi_sql_dd_sql_test.cpp
    unit/abi_no_current_record_test.cpp
)

if(OPENADS_WITH_POSTGRESQL)
    target_sources(openads_unit_tests PRIVATE
        unit/abi_plus_postgres_read_test.cpp
    unit/abi_plus_mariadb_read_test.cpp
    unit/abi_plus_odbc_read_test.cpp
    unit/abi_plus_odbc_seek_test.cpp
    unit/odbc_uri_test.cpp
        unit/abi_plus_postgres_seek_test.cpp
    unit/abi_plus_mariadb_seek_test.cpp
    )
endif()

if(OPENADS_WITH_FIREBIRD)
    target_sources(openads_unit_tests PRIVATE
        unit/firebird_uri_test.cpp
        unit/firebird_connection_test.cpp
        unit/abi_plus_firebird_read_test.cpp
    )
endif()

if(OPENADS_WITH_MSSQL)
    target_sources(openads_unit_tests PRIVATE
        unit/abi_plus_mssql_connect_test.cpp
        unit/abi_plus_mssql_read_test.cpp
        unit/tds_protocol_test.cpp
        unit/mssql_uri_test.cpp
    )
endif()

# M11.4 — side DLL with stored procedures used by abi_aep_test.
add_library(openads_test_aep_proc SHARED
    fixtures/test_aep_proc.cpp
)
set_target_properties(openads_test_aep_proc PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY $<TARGET_FILE_DIR:openads_unit_tests>
    RUNTIME_OUTPUT_DIRECTORY $<TARGET_FILE_DIR:openads_unit_tests>
)
add_dependencies(openads_unit_tests openads_test_aep_proc)
target_compile_definitions(openads_unit_tests PRIVATE
    OPENADS_TEST_AEP_DLL="$<TARGET_FILE:openads_test_aep_proc>"
)

target_include_directories(openads_unit_tests SYSTEM PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party/doctest
)
target_include_directories(openads_unit_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(openads_unit_tests PRIVATE openads_core)

if(OPENADS_WITH_SQLITE)
    # The sqlite read/seek tests seed a fixture DB through the SQLite C
    # API directly, so they need the amalgamation header + symbols.
    target_link_libraries(openads_unit_tests PRIVATE openads_sqlite3)
endif()

add_test(NAME openads_unit_tests COMMAND openads_unit_tests)

# Harbour smoke fixture builder (M8.6). Produces a CDX matching the
# M8.5 fixture DBF using OpenADS' own CdxIndex::create + insert path,
# so the Harbour smoke tests dbSeek end-to-end through OpenADS.
add_executable(make_cdx smoke/harbour/make_cdx.cpp)
target_include_directories(make_cdx PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(make_cdx PRIVATE openads_core)

add_executable(probe_cdx smoke/harbour/probe_cdx.cpp)
target_include_directories(probe_cdx PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(probe_cdx PRIVATE openads_core)

add_executable(probe_cdx2 smoke/harbour/probe_cdx2.cpp)
target_include_directories(probe_cdx2 PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(probe_cdx2 PRIVATE openads_core)

# Benchmark: DBF 500K records + CDX index build
add_executable(dbf_cdx_bench bench/dbf_cdx_bench.cpp)
target_include_directories(dbf_cdx_bench PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(dbf_cdx_bench PRIVATE openads_ace)
