nt_add_console_exe(dinput8check main.cpp device.cpp formats.cpp)
target_link_libraries(dinput8check PRIVATE dxguid user32)
add_dependencies(dinput8check dinput8)
