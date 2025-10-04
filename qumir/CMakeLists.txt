add_library(qumir
    paser/lexer.h
    paser/lexer.cpp
    location.h
    optional.h
)
target_include_directories(qumir PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(qumir PUBLIC cxx_std_23)