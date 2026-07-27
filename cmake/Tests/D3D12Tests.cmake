if(CNA_BUILD_TESTS AND CNA_GRAPHICS_BACKEND STREQUAL "D3D12")
    enable_testing()

    macro(cna_d3d12_test target src)
        add_executable(${target} ${src})
        target_link_libraries(${target} PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
        if(TARGET SDL3::SDL3main)
            target_link_libraries(${target} PRIVATE SDL3::SDL3main)
        endif()
        if(MINGW)
            target_link_options(${target} PRIVATE -static-libgcc -static-libstdc++)
            target_link_options(${target} PRIVATE -Wl,--allow-multiple-definition)
            cna_copy_mingw_runtime(${target})
            cna_copy_sdl_runtime(${target})
        endif()
    endmacro()

    # DX-102/103/104/105: off-screen device/queue/heap/command-list/fence proof -- deliberately
    # never constructs a window/swap chain (see examples/d3d12_smoke_test.cpp's own header comment
    # for why), so this stays genuinely green on this Wine+vkd3d-proton dev loop.
    cna_d3d12_test(cna_test_d3d12_smoke examples/d3d12_smoke_test.cpp)
    if(CMAKE_CROSSCOMPILING)
        set(_d3d12_smoke_cmd ${CMAKE_SOURCE_DIR}/scripts/run-wine-vkd3d.sh $<TARGET_FILE:cna_test_d3d12_smoke>)
    else()
        set(_d3d12_smoke_cmd cna_test_d3d12_smoke)
    endif()
    cna_register_backend_test(NAME D3D12_Smoke COMMAND ${_d3d12_smoke_cmd}
        TIMEOUT 60 LABELS "D3D12")

    # plan_dx.md DX-102: real (window-attached) swap-chain diagnostic -- deliberately NOT
    # registered as a CTest (mirrors cna_diag_software's own "real executable, not a ctest"
    # precedent above), since DX-100's own spike already found this crashes under vanilla Wine's
    # dxgi.dll; a permanently-registered, always-crashing CTest would just be noise. Built so a
    # developer/script can still run it by hand for a real, up-to-date reading.
    cna_d3d12_test(cna_diag_d3d12_swapchain examples/d3d12_swapchain_diag.cpp)

    # REMED-GFX-127: every public Texture2D::GetData call must return the resource's real content or
    # reject the request deterministically -- never fabricate one. Built but deliberately NOT
    # registered as a CTest, for exactly the reason cna_diag_d3d12_swapchain above is not: this is a
    # Game-harness test, so it constructs a window and swap chain, which DX-100's spike already
    # found crashes under this dev loop's vanilla Wine dxgi.dll. Building it keeps D3D12's readback
    # implementation compile-verified and runnable by hand on real Windows.
    cna_d3d12_test(cna_test_d3d12_texture2d_getdata_contract examples/texture2d_getdata_contract_test.cpp)

    # REMED-GFX-130: the TextureCube/Texture3D half of the same finding. Built, not ctest-registered,
    # for exactly the reason above -- this is a Game-harness test, so it constructs a window and swap
    # chain, which DX-100's spike already found crashes under this dev loop's vanilla Wine dxgi.dll.
    cna_d3d12_test(cna_test_d3d12_cube_volume_getdata_contract
                   examples/texturecube_texture3d_getdata_contract_test.cpp)
endif()
