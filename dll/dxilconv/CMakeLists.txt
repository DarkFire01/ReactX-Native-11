nt_add_usermode_dll(dxilconv
    DEF dxilconv.def
    SOURCES
        bitcode.cpp
        container.cpp
        converter.cpp
        dxilconvmain.cpp
        module.cpp
        translate.cpp)
target_link_libraries(dxilconv PRIVATE ole32 uuid)
set_target_properties(dxilconv PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/reactx")
