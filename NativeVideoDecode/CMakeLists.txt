nt_add_console_exe(NativeVideoDecode
    annexbsource.cpp
    av1decoder.cpp
    av1syntax.cpp
    h264decoder.cpp
    h264syntax.cpp
    main.cpp
    mkvsource.cpp
    mp4source.cpp
    player.cpp
    presenter.cpp
    sourceutil.cpp
    stats.cpp
    videodecoder.cpp)

target_link_libraries(NativeVideoDecode PRIVATE
    dxgi dxguid d2d1 dwrite user32 winmm ole32 comdlg32 shell32 windowscodecs)

# Uses the ReactX d3d11.dll next to it only when asked with -runtime reactx
add_dependencies(NativeVideoDecode d3d11)
