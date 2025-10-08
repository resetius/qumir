# library linked to targets that need runtime support
add_library(qumir_runtime
    math.cpp
    math.h
    runtime.cpp
    runtime.h)
target_compile_features(qumir_runtime PUBLIC cxx_std_23)