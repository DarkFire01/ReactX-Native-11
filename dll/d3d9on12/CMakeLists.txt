# The shader converter is a library of its own so tests can drive it beside the system one
nt_add_usermode_lib(d3d9on12sc
    sccontext.cpp
    scops.cpp
    scpixel.cpp
    shaderconv.cpp)
set_target_properties(d3d9on12sc PROPERTIES NT_CXX_EXCEPTIONS ON)
target_link_libraries(d3d9on12sc PUBLIC d3d12translationlayer)

nt_add_usermode_dll(d3d9on12
    DEF d3d9on12.def
    SOURCES
        adapter.cpp
        blit.cpp
        caps.cpp
        ddi.cpp
        device.cpp
        formats.cpp
        interop.cpp
        main.cpp
        output.cpp
        present.cpp
        query.cpp
        resource.cpp
        settings.cpp
        share.cpp
        state.cpp)

# The device runs on D3D12TranslationLayer, whose failures arrive as C++ exceptions
set_target_properties(d3d9on12 PROPERTIES NT_CXX_EXCEPTIONS ON)
target_link_libraries(d3d9on12 PRIVATE d3d9on12sc d3d12translationlayer advapi32)

# Kept out of bin/ itself so the system d3d9.dll only finds it when a test hands it over
set_target_properties(d3d9on12 PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/reactx")
