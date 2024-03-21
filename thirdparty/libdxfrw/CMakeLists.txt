cmake_minimum_required(VERSION 3.6)

set(lib_name libdxfrw)
set(target_name ${lib_name})

project(${lib_name})

file(GLOB_RECURSE ${target_name}_src
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
)

add_library(${target_name} STATIC
    ${${target_name}_src}
)

target_include_directories(${target_name} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
