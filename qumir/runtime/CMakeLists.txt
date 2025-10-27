# library linked to targets that need runtime support
add_library(qumir_runtime
    array.cpp
    array.h
    math.cpp
    math.h
    io.cpp
    io.h
    runtime.cpp
    runtime.h
    string.cpp
    string.h
    )
target_compile_features(qumir_runtime PUBLIC cxx_std_23)