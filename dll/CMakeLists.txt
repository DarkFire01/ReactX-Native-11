add_subdirectory(common)
add_subdirectory(d3d10core)
add_subdirectory(d3d10)
add_subdirectory(d3d11)
add_subdirectory(dxgi)

# Direct3D 12 needs WDDM 2, so only the Windows 10 and later targets get it
if(NT_TARGET MATCHES "^win1[01]")
    add_subdirectory(d3d12core)
    add_subdirectory(d3d12)
endif()
