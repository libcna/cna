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

    # REMED-GFX-007: EnvironmentMapEffect must add EmissiveColor UNSCALED (FNA Lighting.fxh), not
    # (Emissive+lightSum)*Diffuse -- which also squares Alpha (both operands are CPU-prefolded).
    # Discriminating non-white-Diffuse / non-zero-Emissive / Alpha=0.5 cases; linear RT readback.
    cna_sdlgpu_test(cna_test_sdlgpu_envmap_emissive examples/sdlgpu_environmentmapeffect_emissive_test.cpp)
    cna_register_backend_test(NAME SdlGpu_EnvMapEmissive COMMAND cna_test_sdlgpu_envmap_emissive
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_sdlgpu.md SDLGPU-34: SkinnedEffect.
    cna_sdlgpu_test(cna_test_sdlgpu_skinned examples/sdlgpu_skinned_test.cpp)
    cna_register_backend_test(NAME SdlGpu_Skinned COMMAND cna_test_sdlgpu_skinned
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # PbrEffect/SkinnedPbrEffect (metallic-roughness BRDF, stride 48/68) and SkinnedEffect's
    # stride-56 VertexColorEnabled path -- porting EasyGL's own PBR + skinned-vertex-color shader
    # support to this backend (see SdlGpuGraphicsBackend::CreatePbrResources()/
    # GetOrCreatePipelinePbr3D() and GetOrCreatePipelineSkinned3D's hasVertexColor parameter).
    cna_sdlgpu_test(cna_test_sdlgpu_pbreffect examples/sdlgpu_pbreffect_test.cpp)
    cna_register_backend_test(NAME SdlGpu_PbrEffect COMMAND cna_test_sdlgpu_pbreffect
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_sdlgpu_test(cna_test_sdlgpu_skinnedpbreffect examples/sdlgpu_skinnedpbreffect_test.cpp)
    cna_register_backend_test(NAME SdlGpu_SkinnedPbrEffect COMMAND cna_test_sdlgpu_skinnedpbreffect
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_sdlgpu_test(cna_test_sdlgpu_skinnedeffect_vertexcolor examples/sdlgpu_skinnedeffect_vertexcolor_test.cpp)
    cna_register_backend_test(NAME SdlGpu_SkinnedEffectVertexColor COMMAND cna_test_sdlgpu_skinnedeffect_vertexcolor
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-006: SkinnedEffect world-space normal transform under non-identity World (rotation,
    # non-uniform scale, and both) -- the cases every existing skinned test, all of which use
    # World = Identity, structurally cannot detect. RenderTarget2D readback (swapchain download
    # segfaults on this backend, SDLGPU-39).
    cna_sdlgpu_test(cna_test_sdlgpu_skinnedeffect_world_normal examples/sdlgpu_skinnedeffect_world_normal_test.cpp)
    cna_register_backend_test(NAME SdlGpu_SkinnedEffect_WorldNormal COMMAND cna_test_sdlgpu_skinnedeffect_world_normal
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-009: stock-effect fog (previously a total absence on this backend). Blue geometry +
    # red fog, FogStart=0/FogEnd=-0.9 -> keep=1/0.5/0 at Z=0/0.45/0.9; linear RT readback. One file
    # per effect group covering all 13 fog-capable shader families.
    cna_sdlgpu_test(cna_test_sdlgpu_basiceffect_fog examples/sdlgpu_basiceffect_fog_test.cpp)
    cna_register_backend_test(NAME SdlGpu_BasicEffect_Fog COMMAND cna_test_sdlgpu_basiceffect_fog
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_sdlgpu_test(cna_test_sdlgpu_effects_fog examples/sdlgpu_effects_fog_test.cpp)
    cna_register_backend_test(NAME SdlGpu_Effects_Fog COMMAND cna_test_sdlgpu_effects_fog
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_sdlgpu_test(cna_test_sdlgpu_skinnedeffect_fog examples/sdlgpu_skinnedeffect_fog_test.cpp)
    cna_register_backend_test(NAME SdlGpu_SkinnedEffect_Fog COMMAND cna_test_sdlgpu_skinnedeffect_fog
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-010: transformed-camera view-space fog conformance (fails pre-fix).
    cna_sdlgpu_test(cna_test_sdlgpu_viewspace_fog examples/sdlgpu_viewspace_fog_test.cpp)
    cna_register_backend_test(NAME SdlGpu_ViewSpace_Fog COMMAND cna_test_sdlgpu_viewspace_fog
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

    # REMED-GFX-064: GraphicsDevice.Viewport honored per-draw in RenderTarget2D passes.
    cna_sdlgpu_test(cna_test_sdlgpu_rt_viewport examples/sdlgpu_rendertarget_viewport_test.cpp)
    cna_register_backend_test(NAME SdlGpu_RenderTargetViewport COMMAND cna_test_sdlgpu_rt_viewport
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-068: GraphicsDevice.ScissorRectangle honored per-draw in RenderTarget2D passes
    # (deferred-model analog of Vulkan GFX-013; scissor was applied once per pass from the live
    # post-unbind full-backbuffer state, so an RT-bound scissor no longer clipped at replay).
    cna_sdlgpu_test(cna_test_sdlgpu_rt_scissor examples/sdlgpu_rendertarget_scissor_test.cpp)
    cna_register_backend_test(NAME SdlGpu_RenderTargetScissor COMMAND cna_test_sdlgpu_rt_scissor
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-069: GraphicsDevice.BlendFactor (constant blend color) reaches the GPU per-draw in
    # RenderTarget2D passes (blend-constant analog of GFX-064/068; SdlGpu never overrode the base
    # SetBlendFactor / called SDL_SetGPUBlendConstants, so constant-color blends used SDL's (0,0,0,0)).
    cna_sdlgpu_test(cna_test_sdlgpu_rt_blendfactor examples/sdlgpu_rendertarget_blendfactor_test.cpp)
    cna_register_backend_test(NAME SdlGpu_RenderTargetBlendFactor COMMAND cna_test_sdlgpu_rt_blendfactor
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-072: SpriteBatch clip space must be built from the active GraphicsDevice.Viewport
    # (Viewport.Width/Height), not the full target -- the sprite2d viewportSize uniform was fed the
    # full target dims while ApplyViewportForRef (GFX-064) mapped NDC into the sub-region, squishing
    # the sprite. The per-QueuedDrawRef viewport must also drive the sprite NDC divide. SdlGpu does
    # not implement GetBackBufferData, so this uses the RenderTarget2D-readback variant (GetData
    # flushes the deferred sprite batch into the RT); it also covers the two-viewports-per-frame
    # (per-QueuedDrawRef) and transformMatrix cases.
    cna_sdlgpu_test(cna_test_sdlgpu_spritebatch_custom_viewport examples/spritebatch_custom_viewport_rt_test.cpp)
    cna_register_backend_test(NAME SdlGpu_SpriteBatch_CustomViewport COMMAND cna_test_sdlgpu_spritebatch_custom_viewport
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-081: SpriteBatch.Begin must apply its RasterizerState argument (shared fix in
    # SpriteBatch.cpp). Deferred-backend control: the scissor state must reach the QueuedDrawRef
    # through Begin, not only via a direct GraphicsDevice.RasterizerState assignment.
    cna_sdlgpu_test(cna_test_sdlgpu_spritebatch_begin_rasterizerstate examples/spritebatch_begin_rasterizerstate_scissor_test.cpp)
    cna_register_backend_test(NAME SdlGpu_SpriteBatch_BeginRasterizerState COMMAND cna_test_sdlgpu_spritebatch_begin_rasterizerstate
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-077: BlendState.ColorWriteChannels (RT0) via SDL_GPUColorTargetBlendState
    # .color_write_mask (pipeline cache key). MultiSampleMask is NOT compiled in: SDL 3.5.0 reserves
    # SDL_GPUMultisampleState::sample_mask as non-functional (REMED-GFX-086). Generic 3D path.
    cna_sdlgpu_test(cna_test_sdlgpu_colorwritechannels examples/gfx077_colorwritechannels_3d_test.cpp)
    cna_register_backend_test(NAME SdlGpu_ColorWriteChannels COMMAND cna_test_sdlgpu_colorwritechannels
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
endif()
