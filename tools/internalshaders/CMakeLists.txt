# Builds the shaders the Direct3D 12 core makes its default objects from, then runs at build time
add_executable(internalshaders
    internalshaders.cpp
    ${CMAKE_SOURCE_DIR}/dll/common/dxbchash.cpp)
target_include_directories(internalshaders PRIVATE "${CMAKE_SOURCE_DIR}/dll/common")
target_link_libraries(internalshaders PRIVATE dxc_dxil dxc_flags)
set_target_properties(internalshaders PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools")

set(RX_INTERNAL_SHADERS_DIR "${CMAKE_BINARY_DIR}/generated/internalshaders" CACHE INTERNAL "")
add_custom_command(
    OUTPUT "${RX_INTERNAL_SHADERS_DIR}/internalshaders.h"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${RX_INTERNAL_SHADERS_DIR}"
    COMMAND internalshaders "${RX_INTERNAL_SHADERS_DIR}/internalshaders.h"
    DEPENDS internalshaders
    COMMENT "Building the internal Direct3D 12 shaders"
    VERBATIM)
add_custom_target(internalshaders_header DEPENDS "${RX_INTERNAL_SHADERS_DIR}/internalshaders.h")
