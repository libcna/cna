if(CNA_BUILD_TESTS AND NOT EMSCRIPTEN AND CNA_GRAPHICS_BACKEND STREQUAL "DILIGENT")
    enable_testing()

    # plan_diligent.md DILIGENT-16. Same link shape as the other backends' GPU test binaries; the
    # extra cna_link_diligent() call is needed because cna_link_diligent() keeps DiligentCore
    # PRIVATE to the backend target (mirroring how the WebGPU backend keeps wgpu-native private).
    macro(cna_diligent_test target src)
        add_executable(${target} ${src})
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(${target} PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME SDL3::SDL3)
        else()
            target_link_libraries(${target} PRIVATE CNA ${BACKEND_TARGET} SHARP_RUNTIME SDL3::SDL3)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(${target} PRIVATE SDL3::SDL3main)
        endif()
    endmacro()

    # Diligent's Linux native window is X11/XCB only (design decision 8), so these run under X11
    # even on a Wayland host. Exit code 77 / the "[SKIP]" line means "no usable device here",
    # which is a skip rather than a failure -- a headless build machine has no GPU to prove
    # anything on, and pretending otherwise is exactly what plan_diligent.md's "Verification
    # status" section forbids.
    cna_diligent_test(cna_test_diligent_clear_readback examples/diligent_clear_readback_test.cpp)
    cna_register_backend_test(NAME Diligent_2D COMMAND cna_test_diligent_clear_readback
        TIMEOUT 90 LABELS "GraphicsSmoke;Diligent"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}"
        SKIP_REGULAR_EXPRESSION "\\[SKIP\\] CNA Diligent smoke")

    cna_diligent_test(cna_test_diligent_colored3d examples/diligent_colored3d_test.cpp)
    cna_register_backend_test(NAME Diligent_3D COMMAND cna_test_diligent_colored3d
        TIMEOUT 90 LABELS "GraphicsSmoke;Diligent"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}"
        SKIP_REGULAR_EXPRESSION "\\[SKIP\\] CNA Diligent smoke")

    # plan_diligent.md DILIGENT-20/DILIGENT-21: off-screen rendering, target readback, sampling the
    # unbound target, and the target's own depth buffer.
    # plan_diligent.md DILIGENT-31/DILIGENT-32: per-pixel alpha-test discard and BasicEffect fog.
    cna_diligent_test(cna_test_diligent_alphatest_fog examples/diligent_alphatest_fog_test.cpp)
    cna_register_backend_test(NAME Diligent_AlphaTestFog COMMAND cna_test_diligent_alphatest_fog
        TIMEOUT 90 LABELS "GraphicsSmoke;Diligent"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}"
        SKIP_REGULAR_EXPRESSION "\\[SKIP\\] CNA Diligent smoke")

    # plan_diligent.md DILIGENT-33/DILIGENT-34: two-layer modulate and cube-map reflection.
    cna_diligent_test(cna_test_diligent_dualtexture_envmap
                      examples/diligent_dualtexture_envmap_test.cpp)
    cna_register_backend_test(NAME Diligent_DualTextureEnvMap
        COMMAND cna_test_diligent_dualtexture_envmap
        TIMEOUT 90 LABELS "GraphicsSmoke;Diligent"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}"
        SKIP_REGULAR_EXPRESSION "\\[SKIP\\] CNA Diligent smoke")

    # plan_diligent.md DILIGENT-35: the 72-bone palette and FNA's WeightsPerVertex semantics.
    # plan_diligent.md DILIGENT-24: several simultaneous render targets and per-slot write masks.
    # plan_diligent.md DILIGENT-22: per-face render-into and readback, plus EnvironmentMapEffect
    # sampling the render target back (the real reason to make a cube render target).
    cna_diligent_test(cna_test_diligent_rendertargetcube examples/diligent_rendertargetcube_test.cpp)
    cna_register_backend_test(NAME Diligent_RenderTargetCube COMMAND cna_test_diligent_rendertargetcube
        TIMEOUT 90 LABELS "GraphicsSmoke;Diligent"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}"
        SKIP_REGULAR_EXPRESSION "\\[SKIP\\] CNA Diligent smoke")

    cna_diligent_test(cna_test_diligent_mrt examples/diligent_mrt_test.cpp)
    cna_register_backend_test(NAME Diligent_MRT COMMAND cna_test_diligent_mrt
        TIMEOUT 90 LABELS "GraphicsSmoke;Diligent"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}"
        SKIP_REGULAR_EXPRESSION "\\[SKIP\\] CNA Diligent smoke")

    cna_diligent_test(cna_test_diligent_skinned examples/diligent_skinned_test.cpp)
    cna_register_backend_test(NAME Diligent_Skinned COMMAND cna_test_diligent_skinned
        TIMEOUT 90 LABELS "GraphicsSmoke;Diligent"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}"
        SKIP_REGULAR_EXPRESSION "\\[SKIP\\] CNA Diligent smoke")

    cna_diligent_test(cna_test_diligent_rendertarget examples/diligent_rendertarget_test.cpp)
    cna_register_backend_test(NAME Diligent_RenderTarget COMMAND cna_test_diligent_rendertarget
        TIMEOUT 90 LABELS "GraphicsSmoke;Diligent"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}"
        SKIP_REGULAR_EXPRESSION "\\[SKIP\\] CNA Diligent smoke")
endif()
