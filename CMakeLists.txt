#   cmake -B build/win10 -G Ninja -DNT_TARGET=win10
#   cmake --build build/win10
cmake_minimum_required(VERSION 3.24)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

project(ReactX
    VERSION 0.1.0
    DESCRIPTION "Native Direct3D runtimes that drive WDDM user mode drivers"
    LANGUAGES C CXX)

if(NOT MSVC)
    message(FATAL_ERROR
        "ReactX needs the MSVC toolchain from a Visual Studio developer "
        "environment. Run one of the scripts in scripts/.")
endif()

include(NtTargets)
include(NtUserMode)

nt_target_list(_nt_all_targets)
string(REPLACE ";" ", " _nt_all_targets_pretty "${_nt_all_targets}")

set(NT_TARGET "win10" CACHE STRING
    "Windows release to build for. One of: ${_nt_all_targets_pretty}")
set_property(CACHE NT_TARGET PROPERTY STRINGS ${_nt_all_targets})

option(NT_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

nt_target_info(${NT_TARGET})
nt_usermode_init()

message(STATUS "ReactX target : ${NT_TARGET_DISPLAY_NAME} (${NT_TARGET_ID})")
message(STATUS "  NTDDI_VERSION : ${NT_TARGET_NTDDI}")
message(STATUS "  _WIN32_WINNT  : ${NT_TARGET_WIN32_WINNT}")
message(STATUS "  subsystem     : WINDOWS,${NT_TARGET_SUBSYSTEM_VERSION}")
message(STATUS "  architecture  : ${NT_ARCH}")
message(STATUS "  SDK           : ${NT_SDK_VERSION}")

# Keep each target's artifacts apart, so several NT_TARGETs can share a tree.
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

add_subdirectory(third_party/DirectXShaderCompiler)
# Vendored projects the D3D12 based layers build on
if(NT_TARGET MATCHES "^win1[01]")
    add_subdirectory(third_party)
endif()

if(NT_TARGET MATCHES "^win1[01]")
    add_subdirectory(tools/internalshaders)
endif()

add_subdirectory(dll)
add_subdirectory(tests)
add_subdirectory(NativeVideoDecode)
