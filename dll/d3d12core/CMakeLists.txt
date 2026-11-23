nt_add_usermode_dll(d3d12core
    DEF d3d12core.def
    SOURCES
        adapter.cpp
        allocator.cpp
        allocator_stubs.cpp
        commandlist.cpp
        commandlist_stubs.cpp
        coremain.cpp
        ddicallbacks.cpp
        descriptorheap.cpp
        descriptorheap_stubs.cpp
        device.cpp
        device_stubs.cpp
        devicecreate.cpp
        features.cpp
        formatsupport12.cpp
        formattable12.cpp
        fence.cpp
        fence_stubs.cpp
        queue.cpp
        queue_stubs.cpp
        resource.cpp
        resource_stubs.cpp
        rootsignature.cpp
        schedulinggroup.cpp)
target_link_libraries(d3d12core PRIVATE rxcommon ole32 advapi32 dxguid)
set_target_properties(d3d12core PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/reactx")
