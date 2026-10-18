# d3d10.dll keeps the helpers; d3d10_1.dll forwards its copies to it, as on Windows
nt_add_usermode_dll(d3d10
    DEF d3d10.def
    SOURCES
        d3d10main.cpp
        d3d10helpers.cpp
        stateblockmask.cpp)
target_link_libraries(d3d10 PRIVATE rxcommon d3d10core dxguid)

nt_add_usermode_dll(d3d10_1
    DEF d3d10_1.def
    SOURCES
        d3d10main.cpp
        d3d10_1forward.cpp)
target_compile_definitions(d3d10_1 PRIVATE RX_D3D10_1)
target_link_libraries(d3d10_1 PRIVATE rxcommon dxguid)

set_target_properties(d3d10 d3d10_1 PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/reactx")
