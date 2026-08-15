cmake_minimum_required(VERSION 3.30)
project(qumir VERSION 1.0.0)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(QUMIR_BUILD_SERVICE "Build Qumir service component with externals" OFF)
option(QUMIR_BUILD_TESTS "Build Qumir tests" OFF)
set(QUMIR_BUILD_NUMBER "0" CACHE STRING "Build number, becomes the Debian revision after the hyphen")

# The upstream version is bumped by hand above; the build number rides along as the
# Debian revision, so the package version reads 1.0.0-42.
set(QUMIR_DEB_VERSION "${PROJECT_VERSION}-${QUMIR_BUILD_NUMBER}")

# Only major.minor names the install tree, so patch releases replace files in place.
set(QUMIR_RELEASE "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}")
set(QUMIR_PRIVATE_DIR "lib/qumir-${QUMIR_RELEASE}")
set(QUMIR_PRIVATE_BINDIR "${QUMIR_PRIVATE_DIR}/bin")
set(QUMIR_PRIVATE_LIBDIR "${QUMIR_PRIVATE_DIR}/lib")

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

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    include(cmake/QumirPackaging.cmake)
endif()
