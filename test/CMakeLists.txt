find_package(PkgConfig REQUIRED)

pkg_check_modules(GTEST gtest REQUIRED)

macro(ut name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} qumir ${GTEST_LIBRARIES})
    target_include_directories(${name} PRIVATE ${GTEST_INCLUDE_DIRS})
    target_link_directories(${name} PRIVATE ${GTEST_LIBRARY_DIRS})
    add_test(NAME ${name} COMMAND ${name} ${ARGN})
endmacro()

ut(test_lexer test_lexer.cpp)