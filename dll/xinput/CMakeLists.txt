nt_add_usermode_dll(xinput1_4
    DEF xinput1_4.def
    SOURCES
        api.cpp
        audio.cpp
        core.cpp
        device.cpp
        driver.cpp
        keystroke.cpp)

target_link_libraries(xinput1_4 PRIVATE setupapi advapi32)

set_target_properties(xinput1_4 PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/reactx")
