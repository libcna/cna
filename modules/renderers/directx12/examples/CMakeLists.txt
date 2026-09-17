# DirectX 12 renderer example/test registration. Renderer-neutral parity
# fixtures are owned by cmake/DirectXParityTests.cmake (DX-242).

# Example/test executables historically live at the build root; scripts, docs,
# and golden-image working-directory conventions rely on this location.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
include_directories(${CNA_GRAPHICS_EXAMPLES_DIR})

if(CNA_BUILD_TESTS AND CNA_GRAPHICS_RENDERER STREQUAL "DIRECTX12")
    enable_testing()

    macro(cna_directx12_test target src)
        add_executable(${target} ${src})
        target_link_libraries(${target} PRIVATE CNA)
        # plans/plan_win32_native_validation.md WINNATIVE-0015: SDL3 is linked here because it used
        # to be the only platform CNA had, and these fixtures still use it where it is configured.
        # With CNA_ENABLE_SDL=OFF the target does not exist -- which is the whole point of that
        # option -- so an unconditional link made
        #     CNA_PLATFORM=WIN32 + CNA_ENABLE_SDL=OFF + DIRECTX11 + tests
        # impossible to configure at all. Same idiom as the headless renderer's examples.
        if(TARGET SDL3::SDL3)
            target_link_libraries(${target} PRIVATE SDL3::SDL3)
        else()
            # plans/plan_directx12_parity.md DX12-0005: PixelTestGame.hpp's display probe is the
            # only SDL use the shared fixtures have; this tells it there is no SDL to probe with.
            target_compile_definitions(${target} PRIVATE CNA_EXAMPLES_NO_SDL)
        endif()
        cna_link_sharp_runtime(${target} PRIVATE)
        if(MSVC)
            # plans/plan_directx12_parity.md DX12-0008: this macro builds the whole Direct3D parity
            # corpus -- ~265 executables. RelWithDebInfo's /INCREMENTAL state and full PDB are about
            # 150 MB per executable, ~40 GB per build tree, which filled the validation VM's disk.
            # FASTLINK keeps them debuggable through the object files at a few MB each.
            target_link_options(${target} PRIVATE /INCREMENTAL:NO /DEBUG:FASTLINK)
        endif()
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

    macro(cna_directx12_ctest_command out_var target_name)
        if(CMAKE_CROSSCOMPILING)
            set(${out_var} ${CMAKE_SOURCE_DIR}/scripts/run-wine-vkd3d.sh $<TARGET_FILE:${target_name}>)
        else()
            set(${out_var} $<TARGET_FILE:${target_name}>)
        endif()
    endmacro()

    # The smoke test is renderer-internal and deliberately creates no swap chain.
    cna_directx12_test(cna_test_directx12_smoke directx12_smoke_test.cpp)
    cna_directx12_ctest_command(_directx12_smoke_cmd cna_test_directx12_smoke)
    cna_register_renderer_test(NAME DirectX12_Smoke COMMAND ${_directx12_smoke_cmd}
        TIMEOUT 60 LABELS "DIRECTX12")

    if(CNA_DIRECTX12_COMPILED_EFFECTS)
        cna_directx12_ctest_command(_directx12_compiled_effects_cmd CnaRendererTests)
        cna_register_renderer_test(NAME DirectX12_CompiledEffects
            COMMAND ${_directx12_compiled_effects_cmd}
                    --gtest_filter=DirectX12CompiledEffect*
            TIMEOUT 300 LABELS "DIRECTX12;CompiledEffects"
            ENVIRONMENT "CNA_FORCE_HEADLESS_DEVICE_EXT=DIRECTX12")

        cna_directx12_test(cna_probe_directx12_compiled_effect
            directx12_compiled_effect_probe.cpp)
        target_link_libraries(cna_probe_directx12_compiled_effect PRIVATE cna_mojoshader)
        cna_directx12_ctest_command(_directx12_compiled_effect_probe_cmd
            cna_probe_directx12_compiled_effect)
        cna_register_renderer_test(NAME DirectX12_CompiledEffectProbe
            COMMAND ${_directx12_compiled_effect_probe_cmd}
                    "${CMAKE_SOURCE_DIR}/modules/renderers/fna3d/effects/CnaConformanceEffect.fxb"
            TIMEOUT 60 LABELS "DIRECTX12;FX")
    endif()

    # This window-attached diagnostic remains buildable but intentionally is not
    # a CTest in the routine Wine lane (DX-100/DX-114). It creates its window with SDL, so a
    # CNA_ENABLE_SDL=OFF configuration cannot build it (plans/plan_directx12_parity.md DX12-0005:
    # it was the one failing target of the first SDL-free DirectX 12 build).
    if(TARGET SDL3::SDL3)
        cna_directx12_test(cna_diag_d3d12_swapchain directx12_swapchain_diag.cpp)
    endif()

    # plans/plan_directx12_parity.md DX12-0007: presentation, resize, minimize and resource churn on a
    # real CNA Win32 window. Not a CTest: it drives the desktop's window manager and takes minutes on
    # WARP; tools/platform/win32_run_interactive.ps1 runs it with CNA_D3D12_ADAPTER=warp.
    if(CNA_PLATFORM STREQUAL "WIN32")
        cna_directx12_test(cna_stress_directx12_win32_present directx12_win32_present_stress.cpp)
        target_link_libraries(cna_stress_directx12_win32_present PRIVATE psapi)
    endif()

    include(${CMAKE_SOURCE_DIR}/cmake/DirectXParityTests.cmake)
    # plans/plan_directx12_parity.md DX12-0005: registered in every configuration. The corpus used to
    # be dropped whole when SDL was off (WINNATIVE-0015), because PixelTestGame.hpp probed for a
    # display with SDL; measured, 4 of its 265 sources call SDL at all, 26 reached it only through
    # that probe, and 235 never include it. The probe is now SDL-free where SDL is absent, and the
    # fixtures that genuinely drive an SDL_Window keep REQUIRES_SDL.
    cna_register_d3d_parity_tests()
endif()

cna_apply_skip_convention()
