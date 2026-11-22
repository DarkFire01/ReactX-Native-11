nt_add_usermode_dll(xinput9_1_0
    DEF xinput9_1_0.def
    SOURCES
        xinput9main.cpp)

set_target_properties(xinput9_1_0 PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/reactx")
