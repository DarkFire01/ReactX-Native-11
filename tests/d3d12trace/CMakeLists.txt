nt_add_console_exe(d3d12trace main.cpp)
target_include_directories(d3d12trace PRIVATE "${RX_INTERNAL_SHADERS_DIR}")
add_dependencies(d3d12trace internalshaders_header)
