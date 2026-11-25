nt_add_usermode_dll(d3d12core
    DEF d3d12core.def
    SOURCES
        adapter.cpp
        allocator.cpp
        allocator_stubs.cpp
        commandlist.cpp
        commandlist_draw.cpp
        commandlist_pass.cpp
        commandlist_state.cpp
        commandlist_stubs.cpp
        coremain.cpp
        ddicallbacks.cpp
        descriptorheap.cpp
        descriptorheap_stubs.cpp
        device.cpp
        device_stubs.cpp
        devicecaps.cpp
        devicecontrol.cpp
        devicecreate.cpp
        devicefactory.cpp
        dxillibrary.cpp
        feedbackview.cpp
        features.cpp
        formatlayout12.cpp
        formatsupport12.cpp
        formattable12.cpp
        fence.cpp
        heap.cpp
        indirect.cpp
        library.cpp
        fence_stubs.cpp
        queue.cpp
        queue_stubs.cpp
        raytracing.cpp
        residency.cpp
        resource.cpp
        resource_stubs.cpp
        resourcevalidate.cpp
        pipeline.cpp
        pipelinestream.cpp
        rootsignature.cpp
        rootsigobject.cpp
        schedulinggroup.cpp
        shadercache.cpp
        session.cpp
        sharing.cpp
        tiles.cpp
        stateobject.cpp
        views.cpp)
target_link_libraries(d3d12core PRIVATE rxcommon ole32 advapi32 dxguid synchronization)
target_include_directories(d3d12core PRIVATE "${RX_INTERNAL_SHADERS_DIR}")
add_dependencies(d3d12core internalshaders_header)
set_target_properties(d3d12core PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/reactx")
