nt_add_console_exe(dsoundcheck main.cpp)
target_link_libraries(dsoundcheck PRIVATE ole32 user32)
add_dependencies(dsoundcheck dsound)
