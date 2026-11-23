nt_add_usermode_dll(dsound
    DEF dsound.def
    SOURCES
        buffer.cpp
        capture.cpp
        device.cpp
        effects.cpp
        focus.cpp
        format.cpp
        main.cpp
        matrix.cpp
        object.cpp
        primary.cpp
        property.cpp
        resample.cpp
        sound.cpp
        sound3d.cpp
        voice.cpp
        dsound.rc)

target_link_libraries(dsound PRIVATE ole32 user32 winmm uuid dxguid)

set_target_properties(dsound PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/reactx")
