nt_add_usermode_lib(rxcommon
    adapteridentity.cpp displayconfig.cpp dxbchash.cpp formatrules.cpp formats.cpp formatsupport.cpp kmcallbacks.cpp
    kmt.cpp trace.cpp)

target_include_directories(rxcommon PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
