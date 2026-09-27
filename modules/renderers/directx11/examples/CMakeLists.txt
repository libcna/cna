# DirectX 11 renderer example/test registration. Renderer-neutral parity
# fixtures are owned by cmake/DirectXParityTests.cmake (DX-242).

# Example/test executables historically live at the build root; scripts, docs,
# and golden-image working-directory conventions rely on this location.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
include_directories(${CNA_GRAPHICS_EXAMPLES_DIR})

if(CNA_BUILD_TESTS AND CNA_GRAPHICS_RENDERER STREQUAL "DIRECTX11")
    enable_testing()

    macro(cna_directx11_test target src)
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

    macro(cna_directx11_ctest_command out_var target_name)
        if(CMAKE_CROSSCOMPILING)
            set(${out_var} ${CMAKE_SOURCE_DIR}/scripts/run-wine-dxvk.sh $<TARGET_FILE:${target_name}>)
        else()
            set(${out_var} $<TARGET_FILE:${target_name}>)
        endif()
    endmacro()

    # GLTF-386: accept both version spellings emitted by genuine DXVK builds.
    # Native Windows has neither bash nor DXVK, so this launcher gate only belongs
    # to the cross-compiled Wine lane.
    if(CMAKE_CROSSCOMPILING)
        cna_register_renderer_test(NAME DirectX11_DxvkGate
            COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/test-run-wine-dxvk-gate.sh
            TIMEOUT 10 LABELS "DIRECTX11")
    endif()

    cna_directx11_test(cna_test_directx11_smoke directx11_smoke_test.cpp)
    cna_directx11_ctest_command(_directx11_smoke_cmd cna_test_directx11_smoke)
    cna_register_renderer_test(NAME DirectX11_Smoke COMMAND ${_directx11_smoke_cmd}
        TIMEOUT 60 LABELS "DIRECTX11")

    if(CNA_PLATFORM STREQUAL "WIN32")
        cna_directx11_test(cna_test_directx11_win32_hardware_smoke
            directx11_win32_hardware_smoke.cpp)
        cna_register_renderer_test(NAME DirectX11_Win32HardwareSmoke
            COMMAND $<TARGET_FILE:cna_test_directx11_win32_hardware_smoke>
            TIMEOUT 60 LABELS "DIRECTX11;Win32")
    endif()

    if(CNA_DIRECTX11_COMPILED_EFFECTS)
        cna_directx11_ctest_command(_directx11_compiled_effects_cmd CnaRendererTests)
        cna_register_renderer_test(NAME DirectX11_CompiledEffects
            COMMAND ${_directx11_compiled_effects_cmd}
                    --gtest_filter=DirectX11CompiledEffect*
            TIMEOUT 300 LABELS "DIRECTX11;CompiledEffects")
    endif()

    include(${CMAKE_SOURCE_DIR}/cmake/DirectXParityTests.cmake)
    # plans/plan_directx12_parity.md DX12-0005: registered in every configuration. The corpus used to
    # be dropped whole when SDL was off (WINNATIVE-0015), because PixelTestGame.hpp probed for a
    # display with SDL; measured, 4 of its 265 sources call SDL at all, 26 reached it only through
    # that probe, and 235 never include it. The probe is now SDL-free where SDL is absent, and the
    # fixtures that genuinely drive an SDL_Window keep REQUIRES_SDL.
    cna_register_d3d_parity_tests()

    # Keep the shared classic parity corpus executable on native D3D11 too. The
    # DirectX-specific list above does not include its multistream/instancing probes.
    include(${CNA_GRAPHICS_EXAMPLES_DIR}/parity/ParityFixtures.cmake)
    cna_register_parity_fixtures(
        BUILDER cna_directx11_test
        TARGET_SUFFIX directx11
        TEST_PREFIX DirectX11
        LABELS "Parity;DIRECTX11"
        TIMEOUT 120)

    cna_directx11_test(cna_test_directx11_instanced_textured
        ${CNA_GRAPHICS_EXAMPLES_DIR}/instanced_textured_draw_test.cpp)
    cna_register_renderer_test(NAME DirectX11_InstancedTexturedDraw
        COMMAND $<TARGET_FILE:cna_test_directx11_instanced_textured>
        TIMEOUT 180 LABELS "Parity;DIRECTX11")

    cna_directx11_test(cna_test_directx11_draw_line_topology
        ${CNA_GRAPHICS_EXAMPLES_DIR}/draw_line_topology_test.cpp)
    cna_register_renderer_test(NAME DirectX11_DrawLineTopology
        COMMAND $<TARGET_FILE:cna_test_directx11_draw_line_topology>
        TIMEOUT 180 LABELS "Parity;DIRECTX11")
endif()

cna_apply_skip_convention()
