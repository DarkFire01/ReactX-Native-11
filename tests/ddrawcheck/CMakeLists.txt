nt_add_console_exe(ddrawcheck main.cpp)
target_link_libraries(ddrawcheck PRIVATE user32 gdi32 dwmapi)
add_dependencies(ddrawcheck ddraw)
