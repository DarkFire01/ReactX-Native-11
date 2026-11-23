set(_on12 "${CMAKE_SOURCE_DIR}/third_party/D3D11On12")

# The Windows side of the layer, over the vendored D3D11On12 classes
add_library(d3d11on12internal OBJECT
    adapter.cpp
    device.cpp
    present.cpp)
target_include_directories(d3d11on12internal
    PRIVATE ${_on12}/include
    PRIVATE ${_on12}/external
    PRIVATE ${_on12}/src)
target_link_libraries(d3d11on12internal PUBLIC d3d11on12core)
rx_vendored_settings(d3d11on12internal)

nt_add_usermode_dll(d3d11on12
    DEF d3d11on12.def
    SOURCES
        $<TARGET_OBJECTS:d3d11on12main>
        $<TARGET_OBJECTS:d3d11on12internal>)
target_link_libraries(d3d11on12 PRIVATE d3d11on12core)
set_target_properties(d3d11on12 PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/reactx")
