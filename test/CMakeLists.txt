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
ut(test_name_resolver test_name_resolver.cpp)
ut(test_cfg test_cfg.cpp)
ut(test_reg test_reg.cpp ${CMAKE_SOURCE_DIR}/test/regtest)
