# library linked to targets that need runtime support
add_library(qumir_runtime
    array.cpp
    array.h
    colors.cpp
    colors.h
    math.cpp
    math.h
    io.cpp
    io.h
    runtime.cpp
    runtime.h
    future.cpp
    future.h
    string.cpp
    string.h
    robot.cpp
    robot.h
    turtle.cpp
    turtle.h
    drawer.cpp
    drawer.h
    painter.cpp
    painter.h
    complex.cpp
    complex.h
    )
target_compile_features(qumir_runtime PUBLIC cxx_std_23)
target_include_directories(qumir_runtime PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../..)

add_library(qumir_runtime_future
    future.cpp
    future.h
)
target_compile_features(qumir_runtime_future PUBLIC cxx_std_23)
target_include_directories(qumir_runtime_future PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../..)

target_link_libraries(qumir_runtime PUBLIC qumir_runtime_future)
