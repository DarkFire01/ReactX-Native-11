nt_add_console_exe(textcheck main.cpp facecheck.cpp factorycheck.cpp gdicheck.cpp)
target_link_libraries(textcheck PRIVATE dxguid)
add_dependencies(textcheck dwrite)
