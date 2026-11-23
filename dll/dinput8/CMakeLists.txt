nt_add_usermode_dll(dinput8
    DEF dinput8.def
    SOURCES
        actionmap.cpp
        apps.cpp
        devdata.cpp
        devff.cpp
        devmap.cpp
        device.cpp
        devprop.cpp
        dinput.cpp
        effectfile.cpp
        formats.cpp
        hidhost.cpp
        input.cpp
        keyboard.cpp
        keytables.cpp
        main.cpp
        mouse.cpp
        dinput8.rc
        ../dinput/effect.cpp
        ../dinput/helpers.cpp
        ../dinput/hid.cpp
        ../dinput/joycfg.cpp
        ../dinput/shepherd.cpp)

# The sources shared with DirectInput 7 build against the DirectInput 8 headers here
target_compile_definitions(dinput8 PRIVATE DIRECTINPUT_VERSION=0x0800)

target_link_libraries(dinput8 PRIVATE dxguid setupapi hid cfgmgr32 advapi32 user32 winmm)

set_target_properties(dinput8 PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/reactx")
