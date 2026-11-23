nt_add_usermode_dll(dxilconv
    DEF dxilconv.def
    SOURCES
        container.cpp
        dxilconvmain.cpp)

# The bridge includes LLVM headers, so it builds with the vendored code's flags
add_library(dxilconv_upstream STATIC upstream.cpp)
target_link_libraries(dxilconv_upstream PRIVATE dxc_dxbcconverter dxc_flags)
set_target_properties(dxilconv_upstream PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

target_link_libraries(dxilconv PRIVATE dxilconv_upstream ole32 uuid)
set_target_properties(dxilconv PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/reactx")
