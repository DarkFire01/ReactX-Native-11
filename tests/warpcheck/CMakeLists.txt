nt_add_console_exe(warpcheck main.cpp)
target_link_libraries(warpcheck PRIVATE d3d11 d3dcompiler dxguid)
