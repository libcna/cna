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
endif()
