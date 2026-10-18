find_program(NT_FXC fxc
    HINTS "$ENV{WindowsSdkVerBinPath}/x64" "$ENV{WindowsSdkVerBinPath}/x86"
    REQUIRED)

set(_shader_source "${CMAKE_CURRENT_SOURCE_DIR}/shaders.hlsl")
set(_shader5_source "${CMAKE_CURRENT_SOURCE_DIR}/shaders5.hlsl")
set(_shader_dir "${CMAKE_CURRENT_BINARY_DIR}/shaders")
set(_shader_headers)

# Each entry point becomes <entry>.h holding a byte array named g_<entry>
foreach(_shader SimpleVS:vs_4_0 SimplePS:ps_4_0 SquaresCS:cs_5_0 AppendCS:cs_5_0 PassGS:gs_5_0 PassHS:hs_5_0
                PassDS:ds_5_0)
    string(REPLACE ":" ";" _parts "${_shader}")
    list(GET _parts 0 _entry)
    list(GET _parts 1 _profile)
    set(_header "${_shader_dir}/${_entry}.h")
    set(_source "${_shader_source}")
    if(_profile MATCHES "_5_0$")
        set(_source "${_shader5_source}")
    endif()
    add_custom_command(
        OUTPUT "${_header}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_shader_dir}"
        COMMAND "${NT_FXC}" /nologo /T ${_profile} /E ${_entry} /Vn g_${_entry} /Fh "${_header}" "${_source}"
        DEPENDS "${_source}"
        VERBATIM)
    list(APPEND _shader_headers "${_header}")
endforeach()

nt_add_console_exe(apicheck main.cpp checks_state.cpp checks_compute.cpp checks_formats.cpp checks_interfaces.cpp ${_shader_headers})
target_include_directories(apicheck PRIVATE "${_shader_dir}")
target_link_libraries(apicheck PRIVATE dxguid)
add_dependencies(apicheck d3d11)
