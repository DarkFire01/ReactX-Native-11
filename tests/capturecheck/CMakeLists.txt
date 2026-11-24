nt_add_console_exe(capturecheck main.cpp)
set_target_properties(capturecheck PROPERTIES NT_CXX_EXCEPTIONS ON)
target_link_libraries(capturecheck PRIVATE dxguid ole32 runtimeobject)
add_dependencies(capturecheck d3d11 dxgi)
