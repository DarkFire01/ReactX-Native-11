nt_add_usermode_dll(ddraw
    DEF ddraw.def
    SOURCES
        blit.cpp
        caps.cpp
        clipper.cpp
        device.cpp
        driver.cpp
        globals.cpp
        hardware.cpp
        main.cpp
        object.cpp
        palette.cpp
        surface.cpp
        ddraw.rc)

target_link_libraries(ddraw PRIVATE rxcommon user32 gdi32 advapi32 ole32 dxguid)

# Kept out of bin/ itself: anything there that links ddraw would otherwise load this one
set_target_properties(ddraw PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/reactx")
