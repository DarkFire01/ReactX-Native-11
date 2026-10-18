nt_add_console_exe(dxgicheck main.cpp)
target_link_libraries(dxgicheck PRIVATE user32 dxguid)
add_dependencies(dxgicheck d3d11 dxgi)
