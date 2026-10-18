nt_add_console_exe(d3d10check main.cpp)
target_link_libraries(d3d10check PRIVATE dxguid)
add_dependencies(d3d10check d3d10 d3d10_1 d3d10core d3d10_1core d3d11 dxgi)
