find_program(NT_FXC fxc
    HINTS "$ENV{WindowsSdkVerBinPath}/x64" "$ENV{WindowsSdkVerBinPath}/x86"
    REQUIRED)

set(_shader_source "${CMAKE_CURRENT_SOURCE_DIR}/shaders.hlsl")
set(_shader_dir "${CMAKE_CURRENT_BINARY_DIR}/shaders")
set(_shader_headers)

# Each entry point becomes <entry>.h holding a byte array named g_<entry>
foreach(_shader ColorVS:vs ColorPS:ps PlacedVS:vs TexturedVS:vs TexturedPS:ps MeshVS:vs CubeVS:vs CubePS:ps
                BackdropVS:vs BackdropPS:ps InstancedVS:vs)
    string(REPLACE ":" ";" _parts "${_shader}")
    list(GET _parts 0 _entry)
    list(GET _parts 1 _stage)
    set(_header "${_shader_dir}/${_entry}.h")
    add_custom_command(
        OUTPUT "${_header}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_shader_dir}"
        COMMAND "${NT_FXC}" /nologo /T ${_stage}_4_0 /E ${_entry} /Vn g_${_entry} /Fh "${_header}" "${_shader_source}"
        DEPENDS "${_shader_source}"
        VERBATIM)
    list(APPEND _shader_headers "${_header}")
endforeach()

# Shader model 5 stages live in their own file and name their full profile
set(_shader5_source "${CMAKE_CURRENT_SOURCE_DIR}/shaders5.hlsl")
foreach(_shader PatternCS:cs_5_0 FullscreenVS:vs_5_0 WrittenPS:ps_5_0 PointVS:vs_5_0 SpriteGS:gs_5_0
                ControlVS:vs_5_0 TriangleHS:hs_5_0 TriangleDS:ds_5_0 SpinVS:vs_5_0 CapturedVS:vs_5_0
                VertexColorPS:ps_5_0 VolumePS:ps_5_0)
    string(REPLACE ":" ";" _parts "${_shader}")
    list(GET _parts 0 _entry)
    list(GET _parts 1 _profile)
    set(_header "${_shader_dir}/${_entry}.h")
    add_custom_command(
        OUTPUT "${_header}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_shader_dir}"
        COMMAND "${NT_FXC}" /nologo /T ${_profile} /E ${_entry} /Vn g_${_entry} /Fh "${_header}" "${_shader5_source}"
        DEPENDS "${_shader5_source}"
        VERBATIM)
    list(APPEND _shader_headers "${_header}")
endforeach()

nt_add_console_exe(tinyd3d main.cpp scenes.cpp ${_shader_headers})
target_include_directories(tinyd3d PRIVATE "${_shader_dir}")
target_link_libraries(tinyd3d PRIVATE user32 gdi32 dxgi dxguid ole32 dbghelp)
add_dependencies(tinyd3d d3d11)
