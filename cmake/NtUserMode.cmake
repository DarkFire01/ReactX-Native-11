include_guard(GLOBAL)

if(NOT NT_ARCH)
    string(TOLOWER "${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}" _nt_arch)
    if(_nt_arch MATCHES "^(x64|arm64|x86)$")
        set(NT_ARCH "${_nt_arch}")
    elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(NT_ARCH x64)
    else()
        set(NT_ARCH x86)
    endif()
    unset(_nt_arch)
endif()
set(NT_ARCH "${NT_ARCH}" CACHE STRING "Target architecture" FORCE)

function(nt_usermode_init)
    if(TARGET nt_usermode_settings)
        return()
    endif()

    # The UMD DDI headers (d3d10umddi.h and friends) only ship in newer SDKs.
    set(_sdk "$ENV{WindowsSDKVersion}")
    string(REGEX REPLACE "[\\/]+$" "" _sdk "${_sdk}")
    if(NOT _sdk)
        set(_sdk "${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}")
    endif()
    set(NT_SDK_VERSION "${_sdk}" PARENT_SCOPE)

    file(TO_CMAKE_PATH "$ENV{WindowsSdkDir}" _sdk_root)
    if(NOT EXISTS "${_sdk_root}/Include/${_sdk}/um/d3d10umddi.h")
        message(FATAL_ERROR
            "d3d10umddi.h was not found in Windows SDK ${_sdk}. "
            "Install Windows SDK 10.0.28000 or newer.")
    endif()

    foreach(_cfg "" _DEBUG _RELEASE _RELWITHDEBINFO _MINSIZEREL)
        set(_flags "${CMAKE_CXX_FLAGS${_cfg}}")
        string(REGEX REPLACE "[/-]EH[a-z]*" "" _flags "${_flags}")
        string(REGEX REPLACE "[/-]GR( |$)" " " _flags "${_flags}")
        set(CMAKE_CXX_FLAGS${_cfg} "${_flags}" PARENT_SCOPE)
    endforeach()

    add_library(nt_usermode_settings INTERFACE)

    target_compile_features(nt_usermode_settings INTERFACE cxx_std_20)

    target_compile_definitions(nt_usermode_settings INTERFACE
        NTDDI_VERSION=${NT_TARGET_NTDDI}
        _WIN32_WINNT=${NT_TARGET_WIN32_WINNT}
        WINVER=${NT_TARGET_WIN32_WINNT}
        NT_TARGET_ID="${NT_TARGET_ID}"
        NT_TARGET_NAME="${NT_TARGET_DISPLAY_NAME}"
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _HAS_EXCEPTIONS=$<IF:$<BOOL:$<TARGET_PROPERTY:NT_CXX_EXCEPTIONS>>,1,0>
        DBG=$<IF:$<CONFIG:Debug>,1,0>)

    # NT_CXX_EXCEPTIONS is for targets built on vendored code that throws, such as D3D12TranslationLayer
    target_compile_options(nt_usermode_settings INTERFACE
        $<$<COMPILE_LANGUAGE:CXX>:$<IF:$<BOOL:$<TARGET_PROPERTY:NT_CXX_EXCEPTIONS>>,/EHsc,/EHs-c->>
        $<$<COMPILE_LANGUAGE:CXX>:$<IF:$<BOOL:$<TARGET_PROPERTY:NT_CXX_EXCEPTIONS>>,/GR,/GR->>
        $<$<COMPILE_LANGUAGE:CXX>:/Zc:__cplusplus>
        /permissive-
        /utf-8
        /Gy
        /Zi
        /external:W0
        /W4
        /wd4100     # unreferenced formal parameter
        /wd4201     # nameless struct/union (used throughout the DDI headers)
        $<$<BOOL:${NT_WARNINGS_AS_ERRORS}>:/WX>)

    target_link_options(nt_usermode_settings INTERFACE
        /DEBUG
        /OPT:REF
        /OPT:ICF)
endfunction()

#
# nt_add_usermode_dll(<name> DEF <file.def> SOURCES <sources>...)
#
function(nt_add_usermode_dll name)
    cmake_parse_arguments(PARSE_ARGV 1 _arg "" "DEF" "SOURCES")
    add_library(${name} SHARED ${_arg_SOURCES} ${_arg_DEF})
    target_link_libraries(${name} PRIVATE nt_usermode_settings)
    set_target_properties(${name} PROPERTIES
        PREFIX ""
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_link_options(${name} PRIVATE
        /SUBSYSTEM:WINDOWS,${NT_TARGET_SUBSYSTEM_VERSION})
endfunction()

#
# nt_add_usermode_lib(<name> <sources>...)
#
function(nt_add_usermode_lib name)
    add_library(${name} STATIC ${ARGN})
    target_link_libraries(${name} PUBLIC nt_usermode_settings)
    set_target_properties(${name} PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endfunction()

#
# nt_add_console_exe(<name> <sources>...)
#
function(nt_add_console_exe name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE nt_usermode_settings)
    set_target_properties(${name} PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_link_options(${name} PRIVATE
        /SUBSYSTEM:CONSOLE,${NT_TARGET_SUBSYSTEM_VERSION})
endfunction()
