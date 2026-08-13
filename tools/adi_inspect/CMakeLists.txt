add_executable(adi_inspect main.cpp)
target_include_directories(adi_inspect PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src)
