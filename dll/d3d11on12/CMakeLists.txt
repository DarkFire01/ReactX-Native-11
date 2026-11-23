nt_add_usermode_dll(d3d11on12
    DEF d3d11on12.def
    SOURCES
        $<TARGET_OBJECTS:d3d11on12main>)
target_link_libraries(d3d11on12 PRIVATE d3d11on12core)
set_target_properties(d3d11on12 PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/reactx")
