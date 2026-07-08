add_executable(openads_stress main.cpp)
target_link_libraries(openads_stress PRIVATE openads_core)
target_include_directories(openads_stress PRIVATE
    ${CMAKE_SOURCE_DIR}/src)

add_executable(openads_memo_stress memo_main.cpp)
target_link_libraries(openads_memo_stress PRIVATE openads_core)
target_include_directories(openads_memo_stress PRIVATE
    ${CMAKE_SOURCE_DIR}/src)

add_executable(openads_memo_zip memo_zip_main.cpp)
target_link_libraries(openads_memo_zip PRIVATE openads_core)
target_include_directories(openads_memo_zip PRIVATE
    ${CMAKE_SOURCE_DIR}/src)

add_executable(openads_concurrency_stress concurrency_main.cpp)
target_link_libraries(openads_concurrency_stress PRIVATE openads_core)
target_include_directories(openads_concurrency_stress PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
if(NOT WIN32)
    find_package(Threads REQUIRED)
    target_link_libraries(openads_concurrency_stress
        PRIVATE Threads::Threads)
endif()

# Local in-process "real business day" mixed-workload soak: scan / seek /
# filter (AOF) / order-swap / scope / temp index / reindex / append, with a
# built-in integrity oracle (errors=0, miscount=0 => exit 0).
add_executable(openads_daily_ops daily_ops_main.cpp)
target_link_libraries(openads_daily_ops PRIVATE openads_core)
target_include_directories(openads_daily_ops PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
if(NOT WIN32)
    find_package(Threads REQUIRED)
    target_link_libraries(openads_daily_ops PRIVATE Threads::Threads)
endif()

# Concurrent multi-client load harness for the TCP wire server.
add_executable(openads_remote_stress remote_concurrency_main.cpp)
target_link_libraries(openads_remote_stress PRIVATE openads_core)
target_include_directories(openads_remote_stress PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
if(NOT WIN32)
    find_package(Threads REQUIRED)
    target_link_libraries(openads_remote_stress PRIVATE Threads::Threads)
endif()

# Randomized soak harness: per-client random mix of scan / SELECT / AOF /
# batch / reconnect, for OFF-vs-ON correctness parity under churn.
add_executable(openads_remote_random remote_random_main.cpp)
target_link_libraries(openads_remote_random PRIVATE openads_core)
target_include_directories(openads_remote_random PRIVATE
    ${CMAKE_SOURCE_DIR}/src)

add_executable(openads_remote_scope_probe
    ${CMAKE_SOURCE_DIR}/tools/remote_scope_probe.cpp)
target_link_libraries(openads_remote_scope_probe PRIVATE openads_core)
target_include_directories(openads_remote_scope_probe PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
if(NOT WIN32)
    find_package(Threads REQUIRED)
    target_link_libraries(openads_remote_random PRIVATE Threads::Threads)
endif()
