nt_add_usermode_dll(xaudio2_8
    DEF xaudio2_8.def
    SOURCES
        forward.cpp)

# The linker resolves each forwarder against the import library of its target
target_link_libraries(xaudio2_8 PRIVATE xaudio2_9)

set_target_properties(xaudio2_8 PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/reactx")
