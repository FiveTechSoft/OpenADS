add_executable(openads_serverd main.cpp config_ini.cpp setup_wizard.cpp)
target_link_libraries(openads_serverd PRIVATE openads_core)
target_include_directories(openads_serverd PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src)

if(OPENADS_WITH_HARBOUR_UDF)
    # Harbour libs for the UDF backend (registered in main.cpp).
    # PRIVATE: the backend object is pulled from openads_core by the
    # explicit reference below; the ACE DLL never takes this path.
    target_link_libraries(openads_serverd PRIVATE ${HARBOUR_LINK_LIBRARIES})
endif()

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

if(OPENADS_WITH_HARBOUR_UDF)
    add_executable(openads_auth_worker auth_worker_main.cpp)
    target_link_libraries(openads_auth_worker PRIVATE openads_core ${HARBOUR_LINK_LIBRARIES})
    if(OPENADS_WITH_TLS)
        target_link_libraries(openads_auth_worker PRIVATE MbedTLS::mbedcrypto)
    endif()
    if(WIN32)
        target_link_libraries(openads_auth_worker PRIVATE advapi32)
    endif()
    target_include_directories(openads_auth_worker PRIVATE ${CMAKE_SOURCE_DIR}/src ${HARBOUR_INCLUDE_DIR})
endif()
if(OPENADS_WITH_TLS AND OPENADS_WITH_HARBOUR_UDF)
    foreach(probe IN ITEMS worker_probe abi_probe integration_probe transport_probe)
        add_executable(auth_spike_${probe} ${CMAKE_SOURCE_DIR}/tests/hrb/auth-spike/${probe}.cpp)
        target_link_libraries(auth_spike_${probe} PRIVATE openads_core)
        target_include_directories(auth_spike_${probe} PRIVATE ${CMAKE_SOURCE_DIR}/src)
        if(MSVC)
            target_compile_options(auth_spike_${probe} PRIVATE /UNDEBUG)
        else()
            target_compile_options(auth_spike_${probe} PRIVATE -UNDEBUG)
        endif()
    endforeach()
endif()


if(OPENADS_WITH_TLS AND OPENADS_WITH_HARBOUR_UDF)
 add_executable(auth_mutual_probe ${CMAKE_SOURCE_DIR}/tests/hrb/mutual/mutual_probe.cpp)
 target_link_libraries(auth_mutual_probe PRIVATE openads_core MbedTLS::mbedcrypto)
 target_include_directories(auth_mutual_probe PRIVATE ${CMAKE_SOURCE_DIR}/src)
 if(MSVC)
 target_compile_options(auth_mutual_probe PRIVATE /UNDEBUG)
 else()
 target_compile_options(auth_mutual_probe PRIVATE -UNDEBUG)
 endif()
endif()

# Build the public mock only for tests. It is never installed or packaged.
if(OPENADS_BUILD_TESTS AND TARGET auth_mutual_probe)
    set(_mutual_hrb "${CMAKE_CURRENT_BINARY_DIR}/mutual-test-only.hrb")
    add_custom_command(OUTPUT "${_mutual_hrb}"
        COMMAND "${HARBOUR_COMPILER}"
            "${CMAKE_SOURCE_DIR}/tests/hrb/mutual/auth.prg" -gh
            "-o${_mutual_hrb}"
        COMMAND ${CMAKE_COMMAND} "-DHRB=${_mutual_hrb}"
            -P "${CMAKE_SOURCE_DIR}/tests/hrb/mutual/protect_fixture.cmake"
        DEPENDS "${CMAKE_SOURCE_DIR}/tests/hrb/mutual/auth.prg"
        VERBATIM)
    add_custom_target(auth_mutual_test_hrb DEPENDS "${_mutual_hrb}")
    add_dependencies(auth_mutual_probe auth_mutual_test_hrb openads_auth_worker)
endif()
