add_library(qumir
    parser/ast.h
    parser/ast.cpp
    parser/lexer.h
    parser/lexer.cpp
    parser/operator.h
    parser/operator.cpp
    parser/type.h
    parser/type.cpp
    location.h
    optional.h
)
target_include_directories(qumir PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(qumir PUBLIC cxx_std_23)
