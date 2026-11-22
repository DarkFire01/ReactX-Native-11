nt_add_usermode_dll(dinput8
    DEF dinput8.def
    SOURCES
        actionmap.cpp
        apps.cpp
        devdata.cpp
        devmap.cpp
        device.cpp
        devprop.cpp
        dinput.cpp
        effectfile.cpp
        formats.cpp
        input.cpp
        keyboard.cpp
        keytables.cpp
        main.cpp
        mouse.cpp
        dinput8.rc)

target_link_libraries(dinput8 PRIVATE dxguid advapi32 user32 winmm)

set_target_properties(dinput8 PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/reactx")
