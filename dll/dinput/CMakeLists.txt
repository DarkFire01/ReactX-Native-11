nt_add_usermode_dll(dinput
    DEF dinput.def
    SOURCES
        device.cpp
        devices.cpp
        dinputobj.cpp
        effect.cpp
        emulation.cpp
        hid.cpp
        keytables.cpp
        main.cpp
        dinput.rc)

target_link_libraries(dinput PRIVATE dxguid setupapi hid advapi32 user32 winmm)

set_target_properties(dinput PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/reactx")
