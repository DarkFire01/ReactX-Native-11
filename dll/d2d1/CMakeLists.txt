find_program(NT_FXC fxc
    HINTS "$ENV{WindowsSdkVerBinPath}/x64" "$ENV{WindowsSdkVerBinPath}/x86" "$ENV{WindowsSdkVerBinPath}/arm64"
    REQUIRED)

set(_d2d_shader_dir "${CMAKE_CURRENT_BINARY_DIR}/shaders")
set(_d2d_shader_headers)

# Each entry point becomes <entry>.h holding a byte array named g_<entry>
foreach(_shader PrimitiveVS:vs_4_0_level_9_1 SolidPS:ps_4_0_level_9_1 TexturePS:ps_4_0_level_9_1)
    string(REPLACE ":" ";" _parts "${_shader}")
    list(GET _parts 0 _entry)
    list(GET _parts 1 _profile)
    set(_header "${_d2d_shader_dir}/${_entry}.h")
    add_custom_command(
        OUTPUT "${_header}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_d2d_shader_dir}"
        COMMAND "${NT_FXC}" /nologo /O3 /T ${_profile} /E ${_entry} /Vn g_${_entry} /Fh "${_header}"
                "${CMAKE_CURRENT_SOURCE_DIR}/shaders.hlsl"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders.hlsl"
        VERBATIM)
    list(APPEND _d2d_shader_headers "${_header}")
endforeach()

nt_add_usermode_dll(d2d1
    DEF d2d1.def
    SOURCES
        bitmap_stubs.cpp
        context.cpp
        context_stubs.cpp
        device_stubs.cpp
        factory.cpp
        factory_stubs.cpp
        geometry.cpp
        gradientstops_stubs.cpp
        linearbrush_stubs.cpp
        renderer.cpp
        resources.cpp
        solidbrush_stubs.cpp
        text.cpp
        ${_d2d_shader_headers})

target_include_directories(d2d1 PRIVATE "${_d2d_shader_dir}")
target_link_libraries(d2d1 PRIVATE rxcommon user32 gdi32 dxguid)

set_target_properties(d2d1 PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/reactx")
