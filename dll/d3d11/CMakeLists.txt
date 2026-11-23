nt_add_usermode_dll(d3d11
    DEF d3d11.def
    SOURCES
        context.cpp
        context1.cpp
        contextstate.cpp
        context_stubs.cpp
        d3d10device.cpp
        d3d10faces.cpp
        d3d11main.cpp
        ddicallbacks.cpp
        device.cpp
        device1.cpp
        device_stubs.cpp
        devicecreate.cpp
        on12create.cpp
        on12device.cpp
        on12face.cpp
        buffer_stubs.cpp
        dxbc.cpp
        features.cpp
        formatsupport.cpp
        dxgisurface.cpp
        dxgidevice.cpp
        dxgidevice_stubs.cpp
        partner.cpp
        pipeline.cpp
        pipelineget.cpp
        presentbridge.cpp
        queries.cpp
        compute.cpp
        resource.cpp
        resourcecheck.cpp
        shaders.cpp
        stagebinding.cpp
        states.cpp
        textures.cpp
        umdadapter.cpp
        videocontext.cpp
        videocontext_stubs.cpp
        videodecoder_stubs.cpp
        videodecoderoutputview_stubs.cpp
        videodevice.cpp
        videodevice_stubs.cpp
        videoobjects.cpp
        videoprocessor.cpp
        videoprocessor_stubs.cpp
        videoprocessorcontext.cpp
        videoprocessorenumerator_stubs.cpp
        videoprocessorinputview_stubs.cpp
        videoprocessoroutputview_stubs.cpp
        views.cpp)

target_link_libraries(d3d11 PRIVATE rxcommon user32 gdi32 ole32 dxguid version d3d11on12 delayimp)

# The D3D11 on 12 layer loads by name the first time a device needs it, as in Windows
target_link_options(d3d11 PRIVATE /DELAYLOAD:d3d11on12.dll)

# Kept out of bin/ itself: anything there that links d3d11 would otherwise load this one
set_target_properties(d3d11 PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/reactx")
