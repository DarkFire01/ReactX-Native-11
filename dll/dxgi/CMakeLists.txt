nt_add_usermode_dll(dxgi
    DEF dxgi.def
    SOURCES
        dxgiadapter.cpp
        dxgiadapter_stubs.cpp
        dxgievents.cpp
        dxgifactory.cpp
        dxgifactory_stubs.cpp
        framedump.cpp
        gdipresent.cpp
        outputcolor.cpp
        dxgimain.cpp
        dwmoutput.cpp
        dwmswapchain.cpp
        dxgioutput.cpp
        dxgioutput_stubs.cpp
        outputdupl.cpp
        swapchain.cpp
        swapchainflip.cpp
        swapchain_stubs.cpp)

target_link_libraries(dxgi PRIVATE rxcommon user32 advapi32 version dxguid)

# Next to the ReactX d3d11.dll, which loads it from there
set_target_properties(dxgi PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/reactx")
