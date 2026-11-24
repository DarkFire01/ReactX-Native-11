nt_add_console_exe(dwriteqt main.cpp)
target_link_libraries(dwriteqt PRIVATE dxguid)
add_dependencies(dwriteqt dwrite)
