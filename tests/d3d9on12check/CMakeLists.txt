nt_add_console_exe(d3d9on12check hooks.cpp main.cpp)
target_link_libraries(d3d9on12check PRIVATE d3d12.lib)
