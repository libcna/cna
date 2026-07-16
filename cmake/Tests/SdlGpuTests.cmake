if(CNA_BUILD_TESTS AND CNA_GRAPHICS_BACKEND STREQUAL "SDL_GPU")
    enable_testing()

    macro(cna_sdlgpu_test target src)
        add_executable(${target} ${src})
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(${target} PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME SDL3::SDL3)
        else()
            target_link_libraries(${target} PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(${target} PRIVATE SDL3::SDL3main)
        endif()
    endmacro()

    cna_sdlgpu_test(cna_test_sdlgpu_smoke examples/sdlgpu_smoke_test.cpp)
    cna_register_backend_test(NAME SdlGpu_Smoke COMMAND cna_test_sdlgpu_smoke
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_sdlgpu.md SDLGPU-22..25: 2D vertical slice -- real Texture2D + SpriteBatch.
    cna_sdlgpu_test(cna_test_sdlgpu_2d examples/sdlgpu_2d_test.cpp)
    cna_register_backend_test(NAME SdlGpu_2D COMMAND cna_test_sdlgpu_2d
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Ad-hoc manual diagnostic (not a CTest) -- see the file's own header comment.
    cna_sdlgpu_test(cna_diag_sdlgpu_single_sprite examples/sdlgpu_diag_single_sprite.cpp)

    # plan_sdlgpu.md SDLGPU-26..30: core 3D vertex formats and BasicEffect.
    cna_sdlgpu_test(cna_test_sdlgpu_3d examples/sdlgpu_3d_test.cpp)
    cna_register_backend_test(NAME SdlGpu_3D COMMAND cna_test_sdlgpu_3d
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_sdlgpu.md SDLGPU-31/32: AlphaTestEffect and DualTextureEffect.
    cna_sdlgpu_test(cna_test_sdlgpu_effects examples/sdlgpu_effects_test.cpp)
    cna_register_backend_test(NAME SdlGpu_Effects COMMAND cna_test_sdlgpu_effects
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_sdlgpu.md SDLGPU-35: RenderTarget2D.
    cna_sdlgpu_test(cna_test_sdlgpu_rendertarget2d examples/sdlgpu_rendertarget2d_test.cpp)
    cna_register_backend_test(NAME SdlGpu_RenderTarget2D COMMAND cna_test_sdlgpu_rendertarget2d
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_sdlgpu.md SDLGPU-36: RenderTargetCube.
    cna_sdlgpu_test(cna_test_sdlgpu_rendertargetcube examples/sdlgpu_rendertargetcube_test.cpp)
    cna_register_backend_test(NAME SdlGpu_RenderTargetCube COMMAND cna_test_sdlgpu_rendertargetcube
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_sdlgpu.md SDLGPU-37: Multiple Render Targets (MRT).
    cna_sdlgpu_test(cna_test_sdlgpu_mrt examples/sdlgpu_mrt_test.cpp)
    cna_register_backend_test(NAME SdlGpu_MRT COMMAND cna_test_sdlgpu_mrt
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_sdlgpu.md SDLGPU-38: RenderTarget2D MSAA.
    cna_sdlgpu_test(cna_test_sdlgpu_rendertarget2d_msaa examples/sdlgpu_rendertarget2d_msaa_test.cpp)
    cna_register_backend_test(NAME SdlGpu_RenderTarget2DMSAA COMMAND cna_test_sdlgpu_rendertarget2d_msaa
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_sdlgpu.md SDLGPU-40/41: Texture3D + mipmaps.
    cna_sdlgpu_test(cna_test_sdlgpu_texture3d examples/sdlgpu_texture3d_test.cpp)
    cna_register_backend_test(NAME SdlGpu_Texture3D COMMAND cna_test_sdlgpu_texture3d
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_sdlgpu.md SDLGPU-51: plain, non-render-target TextureCube.
    cna_sdlgpu_test(cna_test_sdlgpu_texturecube examples/sdlgpu_texturecube_test.cpp)
    cna_register_backend_test(NAME SdlGpu_TextureCube COMMAND cna_test_sdlgpu_texturecube
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_sdlgpu.md SDLGPU-33: EnvironmentMapEffect.
    cna_sdlgpu_test(cna_test_sdlgpu_envmap examples/sdlgpu_envmap_test.cpp)
    cna_register_backend_test(NAME SdlGpu_EnvMap COMMAND cna_test_sdlgpu_envmap
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_sdlgpu.md SDLGPU-34: SkinnedEffect.
    cna_sdlgpu_test(cna_test_sdlgpu_skinned examples/sdlgpu_skinned_test.cpp)
    cna_register_backend_test(NAME SdlGpu_Skinned COMMAND cna_test_sdlgpu_skinned
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_sdlgpu.md SDLGPU-42/43: custom ShaderEffect (runtime GLSL->SPIR-V via libshaderc).
    cna_sdlgpu_test(cna_test_sdlgpu_shadereffect examples/sdlgpu_shadereffect_test.cpp)
    cna_register_backend_test(NAME SdlGpu_ShaderEffect COMMAND cna_test_sdlgpu_shadereffect
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_sdlgpu.md SDLGPU-18/19/20: dynamic BlendState/DepthStencilState/RasterizerState.
    cna_sdlgpu_test(cna_test_sdlgpu_renderstate examples/sdlgpu_renderstate_test.cpp)
    cna_register_backend_test(NAME SdlGpu_RenderState COMMAND cna_test_sdlgpu_renderstate
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_sdlgpu.md SDLGPU-21: dynamic SamplerState for direct 3D draws.
    cna_sdlgpu_test(cna_test_sdlgpu_samplerstate examples/sdlgpu_samplerstate_test.cpp)
    cna_register_backend_test(NAME SdlGpu_SamplerState COMMAND cna_test_sdlgpu_samplerstate
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_sdlgpu.md: real chronological draw-order proof (adversarial-review finding #4).
    cna_sdlgpu_test(cna_test_sdlgpu_draworder examples/sdlgpu_draworder_test.cpp)
    cna_register_backend_test(NAME SdlGpu_DrawOrder COMMAND cna_test_sdlgpu_draworder
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_sdlgpu.md SDLGPU-11: hard swapchain-acquisition-failure recovery proof.
    cna_sdlgpu_test(cna_test_sdlgpu_swapchain_recovery examples/sdlgpu_swapchain_recovery_test.cpp)
    cna_register_backend_test(NAME SdlGpu_SwapchainRecovery COMMAND cna_test_sdlgpu_swapchain_recovery
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_sdlgpu.md: render-target-destroyed-before-flush use-after-free regression proof.
    cna_sdlgpu_test(cna_test_sdlgpu_rt_lifetime examples/sdlgpu_rendertarget_lifetime_test.cpp)
    cna_register_backend_test(NAME SdlGpu_RenderTargetLifetime COMMAND cna_test_sdlgpu_rt_lifetime
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
endif()
