nt_add_console_exe(ddrawtrace main.cpp)
target_link_libraries(ddrawtrace PRIVATE user32 gdi32 dwmapi dxguid)
add_dependencies(ddrawtrace ddraw)
