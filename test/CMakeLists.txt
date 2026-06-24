find_package(PkgConfig REQUIRED)

pkg_check_modules(GTEST gtest REQUIRED)

macro(ut name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} qumir qumir_runtime qumir_codegen_llvm ${GTEST_LIBRARIES})
    target_include_directories(${name} PRIVATE ${GTEST_INCLUDE_DIRS})
    target_link_directories(${name} PRIVATE ${GTEST_LIBRARY_DIRS})
    add_test(NAME ${name} COMMAND ${name} ${ARGN} --gtest_output=xml:${name}.xml)
endmacro()

ut(test_lexer test_lexer.cpp)
ut(test_core_lexer test_core_lexer.cpp)
ut(test_name_resolver test_name_resolver.cpp)
ut(test_cfg test_cfg.cpp)
ut(test_reg test_reg.cpp ${CMAKE_SOURCE_DIR}/test/regtest)
ut(test_field_access test_field_access.cpp)
ut(test_bit_ops test_bit_ops.cpp)
ut(test_if_expr test_if_expr.cpp)
ut(test_future test_future.cpp)
ut(test_lifetime_traits test_lifetime_traits.cpp)
ut(test_lifetime_validator test_lifetime_validator.cpp)
ut(test_lifetime_lowering test_lifetime_lowering.cpp)
ut(test_lifetime_pass test_lifetime_pass.cpp)
ut(test_printer test_printer.cpp)
ut(test_semantic_pipeline test_semantic_pipeline.cpp)
ut(test_source_module_loader test_source_module_loader.cpp)

# JavaScript execution tests (Node.js required)
find_program(NODE_EXECUTABLE node)
if(NODE_EXECUTABLE)
    add_test(
        NAME test_exec_js
        COMMAND ${NODE_EXECUTABLE} ${CMAKE_SOURCE_DIR}/test/test_exec.js
            --root ${CMAKE_SOURCE_DIR}/test/regtest/
            --runtime ${CMAKE_SOURCE_DIR}/service/static/runtime/
            --wasm-dir ${CMAKE_BINARY_DIR}/wasm-output
            --xml ${CMAKE_BINARY_DIR}/test/test_js.xml
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    )
    add_test(
        NAME test_exec_js_core_lifetime
        COMMAND ${NODE_EXECUTABLE} ${CMAKE_SOURCE_DIR}/test/test_exec.js
            --root ${CMAKE_SOURCE_DIR}/test/regtest/
            --runtime ${CMAKE_SOURCE_DIR}/service/static/runtime/
            --wasm-dir ${CMAKE_BINARY_DIR}/wasm-output-core
            --core
            --filter corelang/lifetime*
            --xml ${CMAKE_BINARY_DIR}/test/test_js_core_lifetime.xml
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    )
endif()
