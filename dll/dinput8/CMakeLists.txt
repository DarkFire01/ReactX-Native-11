nt_add_usermode_dll(dinput8
    DEF dinput8.def
    SOURCES
        apps.cpp
        device.cpp
        dinput.cpp
        formats.cpp
        keyboard.cpp
        keytables.cpp
        main.cpp
        mouse.cpp
        dinput8.rc)

target_link_libraries(dinput8 PRIVATE dxguid advapi32 user32)

set_target_properties(dinput8 PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/reactx")
