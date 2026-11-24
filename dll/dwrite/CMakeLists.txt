nt_add_usermode_dll(dwrite
    DEF dwrite.def
    SOURCES
        dwmain.cpp
        facetables.cpp
        factory.cpp
        factorymisc.cpp
        family_stubs.cpp
        factory_stubs.cpp
        fontface.cpp
        fontface_stubs.cpp
        font_stubs.cpp
        fontfile.cpp
        fontgdi.cpp
        fontmode.cpp
        gdiinterop.cpp
        glyphraster.cpp
        glyphrun.cpp
        gpos.cpp
        collection.cpp
        collection1.cpp
        collection_stubs.cpp
        hinter.cpp
        layoutengine.cpp
        linebreak.cpp
        names.cpp
        params.cpp
        params_stubs.cpp
        scan.cpp
        sfnt.cpp
        textformat.cpp
        textlayout.cpp)

target_link_libraries(dwrite PRIVATE rxcommon user32 advapi32 dxguid)

set_target_properties(dwrite PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/reactx")
