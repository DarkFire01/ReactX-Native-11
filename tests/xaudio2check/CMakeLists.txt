nt_add_console_exe(xaudio2check main.cpp)
target_link_libraries(xaudio2check PRIVATE ole32 uuid)
add_dependencies(xaudio2check xaudio2_9 xaudio2_8)
