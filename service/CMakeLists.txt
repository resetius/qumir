find_package(LLVM REQUIRED CONFIG)

add_executable(server server.cpp)
# Link to our libs; LLVM libs come transitively via qumir_codegen_llvm
target_link_libraries(server PUBLIC qumir qumir_runtime qumir_codegen_llvm coroio)

# Headers and defs for using llvm/Support headers directly in server
target_include_directories(server PRIVATE ${LLVM_INCLUDE_DIRS})
target_compile_definitions(server PRIVATE ${LLVM_DEFINITIONS})