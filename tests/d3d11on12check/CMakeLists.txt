nt_add_console_exe(d3d11on12check main.cpp pixels.cpp trace12.cpp work.cpp)
target_link_libraries(d3d11on12check PRIVATE dxguid dxgi d3dcompiler)
add_dependencies(d3d11on12check d3d11on12)
