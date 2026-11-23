nt_add_usermode_dll(d3d12
    DEF d3d12.def
    SOURCES d3d12main.cpp sdkconfig.cpp)
target_link_libraries(d3d12 PRIVATE rxcommon dxguid)
set_target_properties(d3d12 PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/reactx")
