add_executable(adsbackup main.cpp)
target_link_libraries(adsbackup PRIVATE openads_core)
target_include_directories(adsbackup PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src)
