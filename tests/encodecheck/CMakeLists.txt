nt_add_console_exe(encodecheck main.cpp)
target_link_libraries(encodecheck PRIVATE dxguid ole32 mfplat mfuuid strmiids)
add_dependencies(encodecheck d3d11 dxgi)
