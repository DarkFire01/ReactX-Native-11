nt_add_usermode_dll(dxcore
    DEF dxcore.def
    SOURCES
        adapter.cpp
        enumerate.cpp
        factory.cpp
        forwards.cpp
        kmt.cpp
        legacy.cpp
        list.cpp
        main.cpp
        notify.cpp
        preference.cpp
        state.cpp)

target_link_libraries(dxcore PRIVATE ole32 advapi32 dxguid)

# Kept out of bin/ itself so the tests beside it load the system dxcore.dll unless asked
set_target_properties(dxcore PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/reactx")
