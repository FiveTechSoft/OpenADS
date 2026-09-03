add_executable(openads_serverd main.cpp config_ini.cpp setup_wizard.cpp)
target_link_libraries(openads_serverd PRIVATE openads_core)
target_include_directories(openads_serverd PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src)

if(OPENADS_WITH_HTTP)
    target_sources(openads_serverd PRIVATE http_server.cpp)
    target_compile_definitions(openads_serverd PRIVATE OPENADS_WITH_HTTP=1)
    target_link_libraries(openads_serverd PRIVATE
        httplib::httplib
        nlohmann_json::nlohmann_json)
endif()

# 32-bit builds: opt into the 4 GiB user address space (LARGEADDRESSAWARE).
# Without it a Windows x86 process is capped at 2 GiB VA; the storm test with
# hundreds of concurrent sessions (~1 MB thread stacks + buffers each) exhausts
# it, allocations start failing and an uncaught std::bad_alloc aborts the whole
# server. 64-bit targets are unaffected by this flag.
if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    if(MSVC)
        target_link_options(openads_serverd PRIVATE "/LARGEADDRESSAWARE")
    else()
        target_link_options(openads_serverd PRIVATE "-Wl,--large-address-aware")
    endif()
endif()
