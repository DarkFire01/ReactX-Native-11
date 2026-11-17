nt_add_usermode_dll(d3d10warp
    DEF d3d10warp.def
    SOURCES
        adapter.cpp
        clear.cpp
        device.cpp
        draw.cpp
        exec.cpp
        format.cpp
        mips.cpp
        query.cpp
        resource.cpp
        sample.cpp
        shader.cpp
        state.cpp
        view.cpp
        warpmain.cpp)

set_target_properties(d3d10warp PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/reactx")
