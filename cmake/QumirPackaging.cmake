set(CPACK_GENERATOR "DEB")
set(CPACK_PACKAGE_NAME "qumir")
set(CPACK_PACKAGE_VERSION "${QUMIR_VERSION}")
set(CPACK_PACKAGE_CONTACT "Alexey Ozeritskiy <aozeritsky@gmail.com>")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://qumir.dev")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Educational programming language with Russian keywords")
set(CPACK_STRIP_FILES ON)

set(CPACK_DEB_COMPONENT_INSTALL ON)
set(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)

set(CPACK_COMPONENTS_ALL compiler)

set(CPACK_DEBIAN_COMPILER_PACKAGE_NAME "qumir")
set(CPACK_DEBIAN_COMPILER_PACKAGE_SECTION "devel")
# qumirc links the produced object file by invoking c++, and needs wasm-ld for --target=wasm.
set(CPACK_DEBIAN_COMPILER_PACKAGE_DEPENDS "g++")
set(CPACK_DEBIAN_COMPILER_PACKAGE_RECOMMENDS "lld")
set(CPACK_COMPONENT_COMPILER_DESCRIPTION
    "Qumir compiler and interpreter.\n Compiles the KUMIR-style educational language to native code,\n WebAssembly or runs it through the IR interpreter and the LLVM JIT.")

if(QUMIR_BUILD_SERVICE)
    list(APPEND CPACK_COMPONENTS_ALL service)
    set(CPACK_DEBIAN_SERVICE_PACKAGE_NAME "qumir-service")
    set(CPACK_DEBIAN_SERVICE_PACKAGE_SECTION "web")
    # The server spawns qumirc from --binary-dir, so the compiler must match exactly.
    set(CPACK_DEBIAN_SERVICE_PACKAGE_DEPENDS "qumir (= ${QUMIR_VERSION})")
    set(CPACK_COMPONENT_SERVICE_DESCRIPTION
        "Qumir playground web service.\n HTTP server backing the browser playground: editor, examples\n and the robot/turtle executors.")
endif()

include(CPack)
