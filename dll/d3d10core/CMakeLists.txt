foreach(_core d3d10core d3d10_1core)
    nt_add_usermode_dll(${_core}
        DEF ${_core}.def
        SOURCES d3d10coremain.cpp)
    target_link_libraries(${_core} PRIVATE rxcommon)
    set_target_properties(${_core} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/reactx")
endforeach()

target_compile_definitions(d3d10_1core PRIVATE RX_D3D10_1)
