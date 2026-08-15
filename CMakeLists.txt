cmake_minimum_required(VERSION 3.30)
project(qumir)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(QUMIR_BUILD_SERVICE "Build Qumir service component with externals" OFF)
option(QUMIR_BUILD_TESTS "Build Qumir tests" OFF)

add_subdirectory(qumir)
if(QUMIR_BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()
add_subdirectory(bin)

if(QUMIR_BUILD_SERVICE)
    add_subdirectory(externals)
    add_subdirectory(service)
endif()
