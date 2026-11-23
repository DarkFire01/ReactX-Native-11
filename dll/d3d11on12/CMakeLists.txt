nt_add_usermode_dll(d3d11on12
    DEF d3d11on12.def
    SOURCES
        adapter.cpp
        context.cpp
        ddistubs.cpp
        descriptors.cpp
        device.cpp
        main.cpp
        states.cpp)
target_link_libraries(d3d11on12 PRIVATE rxcommon dxguid)
set_target_properties(d3d11on12 PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/reactx")
