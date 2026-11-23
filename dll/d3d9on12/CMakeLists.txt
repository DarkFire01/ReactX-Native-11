nt_add_usermode_dll(d3d9on12
    DEF d3d9on12.def
    SOURCES
        adapter.cpp
        caps.cpp
        ddi.cpp
        device.cpp
        formats.cpp
        main.cpp
        settings.cpp)

# The device runs on D3D12TranslationLayer, whose failures arrive as C++ exceptions
set_target_properties(d3d9on12 PROPERTIES NT_CXX_EXCEPTIONS ON)
target_link_libraries(d3d9on12 PRIVATE d3d12translationlayer advapi32)

# Kept out of bin/ itself so the system d3d9.dll only finds it when a test hands it over
set_target_properties(d3d9on12 PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/reactx")
