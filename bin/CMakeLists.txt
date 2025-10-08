add_executable(qumiri interpreter.cpp)
target_link_libraries(qumiri PUBLIC qumir qumir_runtime qumir_codegen_llvm)
