set(_nvd "${CMAKE_SOURCE_DIR}/NativeVideoDecode")

# The AV1 syntax reader comes from the player, for building the WebM CodecPrivate
nt_add_console_exe(maketestclips
    main.cpp
    mftencoder.cpp
    pattern.cpp
    webmwriter.cpp
    ${_nvd}/av1syntax.cpp)

target_link_libraries(maketestclips PRIVATE d3d11 mfplat mfreadwrite mfuuid ole32 oleaut32)
