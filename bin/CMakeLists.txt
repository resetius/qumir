add_executable(qumiri interpreter.cpp)
add_executable(qumirc driver.cpp)
if(UNIX AND NOT APPLE)
    target_link_libraries(qumiri PUBLIC
        "$<LINK_GROUP:RESCAN,qumir,qumir_runtime,qumir_codegen_llvm>")
    target_link_libraries(qumirc PUBLIC
        "$<LINK_GROUP:RESCAN,qumir,qumir_runtime,qumir_codegen_llvm>")
else()
    target_link_libraries(qumiri PUBLIC qumir qumir_runtime qumir_codegen_llvm)
    target_link_libraries(qumirc PUBLIC qumir qumir_runtime qumir_codegen_llvm)
endif()
