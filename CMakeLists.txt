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
set(QUMIR_DATA_DIR "/var/lib/qumir-data" CACHE STRING "Writable data directory, created by the administrator")

# Build number 0 means a working copy; anything else comes from CI and names a package.
string(TIMESTAMP QUMIR_BUILD_DATE "%Y-%m-%d" UTC)
if(QUMIR_BUILD_NUMBER STREQUAL "0")
    set(QUMIR_VERSION_STRING "dev,${QUMIR_BUILD_DATE}")
else()
    execute_process(
        COMMAND git rev-parse --short=7 HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE QUMIR_GIT_SHA
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    if(QUMIR_GIT_SHA)
        set(QUMIR_VERSION_STRING "${PROJECT_VERSION}-${QUMIR_BUILD_NUMBER},${QUMIR_GIT_SHA},${QUMIR_BUILD_DATE}")
    else()
        set(QUMIR_VERSION_STRING "${PROJECT_VERSION}-${QUMIR_BUILD_NUMBER},${QUMIR_BUILD_DATE}")
    endif()
endif()
message(STATUS "Qumir version string: ${QUMIR_VERSION_STRING}")
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

# Only the standalone build packages itself; embedded in another project its CPack
# configuration would overwrite the outer one.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND PROJECT_IS_TOP_LEVEL)
    include(cmake/QumirPackaging.cmake)
endif()
