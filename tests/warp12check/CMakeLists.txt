nt_add_console_exe(warp12check main.cpp)
target_link_libraries(warp12check PRIVATE d3d12.lib dxgi.lib dwmapi.lib d3dcompiler.lib dxguid)
