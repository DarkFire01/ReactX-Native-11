nt_add_console_exe(d3d12check main.cpp)
target_link_libraries(d3d12check PRIVATE dxguid)
add_dependencies(d3d12check d3d12 d3d12core dxgi)
