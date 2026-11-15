nt_add_usermode_dll(dwrite
    DEF dwrite.def
    SOURCES
        dwmain.cpp
        facetables.cpp
        factory.cpp
        factory_stubs.cpp
        fontface.cpp
        fontface_stubs.cpp
        fontfile.cpp
        glyphraster.cpp
        glyphrun.cpp
        collection.cpp
        hinter.cpp
        names.cpp
        params.cpp
        params_stubs.cpp
        scan.cpp
        sfnt.cpp)

target_link_libraries(dwrite PRIVATE rxcommon user32 advapi32 dxguid)

set_target_properties(dwrite PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/reactx")
