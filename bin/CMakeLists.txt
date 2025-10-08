add_executable(qumiri interpreter.cpp)
target_link_libraries(qumiri PUBLIC qumir qumir_runtime qumir_codegen_llvm)
add_executable(qumirc driver.cpp)
target_link_libraries(qumirc PUBLIC qumir qumir_runtime qumir_codegen_llvm)
