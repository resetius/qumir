add_library(qumir
    parser/ast.h
    parser/ast.cpp
    parser/lexer.h
    parser/lexer.cpp
    parser/operator.h
    parser/operator.cpp
    parser/parser.h
    parser/parser.cpp
    parser/type.h
    parser/type.cpp
    semantics/name_resolution/name_resolver.h
    semantics/name_resolution/name_resolver.cpp
    semantics/type_annotation/type_annotation.h
    semantics/type_annotation/type_annotation.cpp
    error.h
    error.cpp
    location.h
    optional.h
)
target_include_directories(qumir PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(qumir PUBLIC cxx_std_23)
