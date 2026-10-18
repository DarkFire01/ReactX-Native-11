nt_add_console_exe(rxprobe main.cpp)

target_link_libraries(rxprobe PRIVATE dxgi ole32 dxguid)

# Runs against the ReactX d3d11.dll sitting next to it unless told -system
add_dependencies(rxprobe d3d11)
