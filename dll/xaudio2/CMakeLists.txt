nt_add_usermode_dll(xaudio2_9
    DEF xaudio2_9.def
    SOURCES
        apo.cpp
        device.cpp
        effects.cpp
        engine.cpp
        filter.cpp
        format.cpp
        fxecho.cpp
        fxeq.cpp
        fxlimit.cpp
        fxreverb.cpp
        main.cpp
        matrix.cpp
        meter.cpp
        mixer.cpp
        mixmatrix.cpp
        output.cpp
        resample.cpp
        reverb.cpp
        source.cpp
        voice.cpp
        wmadec.cpp
        wmasource.cpp
        x3daudio.cpp
        xapobase.cpp)

target_link_libraries(xaudio2_9 PRIVATE ole32 uuid mmdevapi)

set_target_properties(xaudio2_9 PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/reactx")
