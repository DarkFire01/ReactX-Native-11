nt_add_usermode_dll(d3d9on12
    DEF d3d9on12.def
    SOURCES
        adapter.cpp
        caps.cpp
        formats.cpp
        main.cpp)

target_link_libraries(d3d9on12 PRIVATE d3d12.lib dxgi.lib dxguid)

# Kept out of bin/ itself so the system d3d9.dll only finds it when a test hands it over
set_target_properties(d3d9on12 PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/reactx")
