# Vendored Microsoft projects, built as static libraries with their own C++ settings.
# The upstream CMakeLists.txt files are kept but not used; they fetch dependencies at configure time.

# Exceptions and RTTI stay inside these targets
function(rx_vendored_settings name)
    target_compile_features(${name} PUBLIC cxx_std_17)
    target_compile_options(${name} PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:/EHsc /GR /Zc:__cplusplus>
        /utf-8
        /Gy
        /Zi
        /W3
        /wd4238
        /wd4324)
    target_compile_definitions(${name} PRIVATE
        $<$<CONFIG:Debug>:DBG=1>)
    set_target_properties(${name} PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endfunction()

set(_tl "${CMAKE_CURRENT_SOURCE_DIR}/D3D12TranslationLayer")
set(_on12 "${CMAKE_CURRENT_SOURCE_DIR}/D3D11On12")

add_library(dxbcparser STATIC
    ${_tl}/DxbcParser/src/BlobContainer.cpp
    ${_tl}/DxbcParser/src/DXBCUtils.cpp)
target_include_directories(dxbcparser
    PUBLIC ${_tl}/DxbcParser/include
    PUBLIC ${_tl}/external)
rx_vendored_settings(dxbcparser)

# D3D12TranslationLayer with the WDK dependent parts, for d3d11on12 and d3d9on12
add_library(d3d12translationlayer STATIC
    ${_tl}/src/Allocator.cpp
    ${_tl}/src/BatchedContext.cpp
    ${_tl}/src/BlitHelper.cpp
    ${_tl}/src/ColorConvertHelper.cpp
    ${_tl}/src/CommandListManager.cpp
    ${_tl}/src/DeviceChild.cpp
    ${_tl}/src/Fence.cpp
    ${_tl}/src/FormatDescImpl.cpp
    ${_tl}/src/ImmediateContext.cpp
    ${_tl}/src/Main.cpp
    ${_tl}/src/MaxFrameLatencyHelper.cpp
    ${_tl}/src/PipelineState.cpp
    ${_tl}/src/Query.cpp
    ${_tl}/src/Residency.cpp
    ${_tl}/src/Resource.cpp
    ${_tl}/src/ResourceBinding.cpp
    ${_tl}/src/ResourceCache.cpp
    ${_tl}/src/ResourceState.cpp
    ${_tl}/src/RootSignature.cpp
    ${_tl}/src/Shader.cpp
    ${_tl}/src/SubresourceHelpers.cpp
    ${_tl}/src/SwapChainHelper.cpp
    ${_tl}/src/SwapChainManager.cpp
    ${_tl}/src/Util.cpp
    ${_tl}/src/VideoDecode.cpp
    ${_tl}/src/VideoDecodeStatistics.cpp
    ${_tl}/src/VideoDevice.cpp
    ${_tl}/src/VideoProcess.cpp
    ${_tl}/src/VideoProcessEnum.cpp
    ${_tl}/src/VideoReferenceDataManager.cpp
    ${_tl}/src/View.cpp
    ${_tl}/src/DxbcBuilder.cpp
    ${_tl}/src/ShaderBinary.cpp
    ${_tl}/src/ShaderParser.cpp
    ${_tl}/src/SharedResourceHelpers.cpp)
target_include_directories(d3d12translationlayer
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/DirectX-Headers/include
    PUBLIC ${_tl}/external
    PUBLIC ${_tl}/include
    PRIVATE ${_tl}/src)
target_precompile_headers(d3d12translationlayer PRIVATE ${_tl}/include/pch.h)
target_compile_definitions(d3d12translationlayer
    PUBLIC SUPPORTS_DXBC_PARSE
    PUBLIC USE_PIX
    PUBLIC $<$<CONFIG:Debug>:TRANSLATION_LAYER_DBG=1>
    PUBLIC DYNAMIC_LOAD_DXCORE=1
    PRIVATE NO_IMPLEMENT_RECT_FNS)
target_link_libraries(d3d12translationlayer PUBLIC dxbcparser d3d12 dxgi dxguid)
rx_vendored_settings(d3d12translationlayer)

# D3D11On12 without its DLL wrapper; dll/d3d11on12 links it into d3d11on12.dll
file(GLOB _on12_sources CONFIGURE_DEPENDS ${_on12}/src/*.cpp)
list(REMOVE_ITEM _on12_sources ${_on12}/src/main.cpp)
add_library(d3d11on12core STATIC ${_on12_sources})
target_include_directories(d3d11on12core
    PRIVATE ${_on12}/include
    PRIVATE ${_on12}/external
    PUBLIC ${_on12}/interface
    PRIVATE ${_on12}/src)
target_precompile_headers(d3d11on12core PRIVATE ${_on12}/include/pch.hpp)
target_link_libraries(d3d11on12core PUBLIC d3d12translationlayer)
rx_vendored_settings(d3d11on12core)

# DllMain and the trace provider, linked as objects so the DLL always takes them
add_library(d3d11on12main OBJECT ${_on12}/src/main.cpp)
target_include_directories(d3d11on12main
    PRIVATE ${_on12}/include
    PRIVATE ${_on12}/external
    PRIVATE ${_on12}/src)
target_link_libraries(d3d11on12main PUBLIC d3d11on12core)
rx_vendored_settings(d3d11on12main)
