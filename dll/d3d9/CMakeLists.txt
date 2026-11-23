nt_add_usermode_dll(d3d9
    DEF d3d9.def
    SOURCES
        adapter.cpp
        enum.cpp
        identity.cpp
        main.cpp
        perf.cpp
        psgp.cpp)

target_link_libraries(d3d9 PRIVATE rxcommon user32 gdi32 advapi32 version)

# A plug-in error unwinds through the caller as a C++ exception
set_source_files_properties(psgp.cpp PROPERTIES COMPILE_OPTIONS "/EHs")

# Kept out of bin/ itself: anything there that links d3d9 would otherwise load this one
set_target_properties(d3d9 PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/reactx")
