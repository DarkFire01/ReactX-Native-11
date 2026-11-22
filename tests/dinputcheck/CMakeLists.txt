nt_add_console_exe(dinputcheck main.cpp)
# The SDK library for the c_dfDI* data formats, not the dinput8 target this repo also builds
target_link_libraries(dinputcheck PRIVATE dinput8.lib dxguid user32)
add_dependencies(dinputcheck dinput)
