nt_add_console_exe(dinputcheck main.cpp)
target_link_libraries(dinputcheck PRIVATE dinput8 dxguid user32)
add_dependencies(dinputcheck dinput)
