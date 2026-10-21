find_program(NT_FXC fxc
    HINTS "$ENV{WindowsSdkVerBinPath}/x64" "$ENV{WindowsSdkVerBinPath}/x86"
    REQUIRED)

set(_shader_source "${CMAKE_CURRENT_SOURCE_DIR}/shaders.hlsl")
set(_shader_dir "${CMAKE_CURRENT_BINARY_DIR}/shaders")
set(_shader_headers)

# Each entry point becomes <entry>.h holding a byte array named g_<entry>
foreach(_shader FullScreenVS:vs_4_0 CopyPS:ps_4_0)
    string(REPLACE ":" ";" _parts "${_shader}")
    list(GET _parts 0 _entry)
    list(GET _parts 1 _profile)
    set(_header "${_shader_dir}/${_entry}.h")
    add_custom_command(
        OUTPUT "${_header}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_shader_dir}"
        COMMAND "${NT_FXC}" /nologo /T ${_profile} /E ${_entry} /Vn g_${_entry} /Fh "${_header}" "${_shader_source}"
        DEPENDS "${_shader_source}"
        VERBATIM)
    list(APPEND _shader_headers "${_header}")
endforeach()

nt_add_console_exe(sharecheck main.cpp ${_shader_headers})
target_include_directories(sharecheck PRIVATE "${_shader_dir}")
target_link_libraries(sharecheck PRIVATE dxguid)
add_dependencies(sharecheck d3d11 dxgi)
