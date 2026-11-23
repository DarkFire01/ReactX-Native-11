nt_add_console_exe(d3d11on12check main.cpp trace12.cpp)
target_link_libraries(d3d11on12check PRIVATE dxguid dxgi)
add_dependencies(d3d11on12check d3d11on12)
