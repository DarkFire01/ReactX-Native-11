nt_add_usermode_dll(d3d10warp
    DEF d3d10warp.def
    SOURCES
        adapter.cpp
        adapter12.cpp
        bc.cpp
        clear.cpp
        compute.cpp
        device.cpp
        descriptor12.cpp
        device12.cpp
        draw.cpp
        exec.cpp
        format.cpp
        list12.cpp
        mips.cpp
        query.cpp
        queue12.cpp
        resource.cpp
        resource12.cpp
        sample.cpp
        shader.cpp
        state.cpp
        tess.cpp
        view.cpp
        warpmain.cpp)

set_target_properties(d3d10warp PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/reactx")
