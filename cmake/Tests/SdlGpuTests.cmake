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

    # REMED-GFX-028: every real constructor acquisition is transactional, failed objects never
    # enter the window registry, and lazy command/pipeline/sampler/fallback failures remain
    # retryable. Resource callbacks make leaks/double-destruction observable independently of
    # whether the selected failure point throws.
    cna_sdlgpu_test(cna_test_sdlgpu_constructor_exception_safety
                    examples/sdlgpu_constructor_exception_safety_test.cpp)
    cna_register_backend_test(NAME SdlGpu_ConstructorExceptionSafety
        COMMAND cna_test_sdlgpu_constructor_exception_safety
        TIMEOUT 240 LABELS "SdlGpu"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
    set_tests_properties(SdlGpu_ConstructorExceptionSafety PROPERTIES
        FAIL_REGULAR_EXPRESSION "Validation (Error|Warning);VUID-")

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

    # REMED-GFX-096: backend-neutral singular/plural cube-face binding parity.
    cna_sdlgpu_test(cna_test_sdlgpu_rendertargetcube_plural_binding
                    examples/rendertargetcube_plural_binding_test.cpp)
    target_compile_definitions(cna_test_sdlgpu_rendertargetcube_plural_binding
        PRIVATE CNA_GFX096_CUBE_GETDATA=1)
    cna_register_backend_test(NAME SdlGpu_RenderTargetCube_PluralBinding
        COMMAND cna_test_sdlgpu_rendertargetcube_plural_binding
        TIMEOUT 60 LABELS "SdlGpu"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-097: a depthless cube-face SpriteBatch draw must use a graphics pipeline whose
    # target info also has no depth/stencil target. Pixel checks prove exact face selection; make
    # the Vulkan compatibility VUID independently fatal so validation-cleanliness cannot be hidden
    # by otherwise-correct pixels.
    cna_sdlgpu_test(cna_test_sdlgpu_depthless_cube_spritebatch_compatibility
                    examples/sdlgpu_depthless_cube_spritebatch_compatibility_test.cpp)
    cna_register_backend_test(NAME SdlGpu_DepthlessCube_SpriteBatchCompatibility
        COMMAND cna_test_sdlgpu_depthless_cube_spritebatch_compatibility
        TIMEOUT 60 LABELS "SdlGpu"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
    set_tests_properties(SdlGpu_DepthlessCube_SpriteBatchCompatibility PROPERTIES
        FAIL_REGULAR_EXPRESSION "VUID-vkCmdDraw-renderPass-02684")

    # plan_sdlgpu.md SDLGPU-37: Multiple Render Targets (MRT).
    cna_sdlgpu_test(cna_test_sdlgpu_mrt examples/sdlgpu_mrt_test.cpp)
    cna_register_backend_test(NAME SdlGpu_MRT COMMAND cna_test_sdlgpu_mrt
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
    # REMED-GFX-098: pixels alone were always correct; a color-attachment render-pass
    # compatibility VUID is fatal for this regression.
    set_tests_properties(SdlGpu_MRT PROPERTIES
        FAIL_REGULAR_EXPRESSION "VUID-vkCmdDraw-renderPass-02684")

    # plan_sdlgpu.md SDLGPU-38: RenderTarget2D MSAA.
    cna_sdlgpu_test(cna_test_sdlgpu_rendertarget2d_msaa examples/sdlgpu_rendertarget2d_msaa_test.cpp)
    cna_register_backend_test(NAME SdlGpu_RenderTarget2DMSAA COMMAND cna_test_sdlgpu_rendertarget2d_msaa
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_sdlgpu.md SDLGPU-40/41: Texture3D + mipmaps.
    cna_sdlgpu_test(cna_test_sdlgpu_texture3d examples/sdlgpu_texture3d_test.cpp)
    cna_register_backend_test(NAME SdlGpu_Texture3D COMMAND cna_test_sdlgpu_texture3d
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-099: the first invalid event occurs while constructing a mipmapped Texture3D,
    # before upload/readback. Keep the create-only discriminator separate from the larger data
    # contract and make both the six exact original VUIDs and any new validation category fatal.
    cna_sdlgpu_test(cna_test_sdlgpu_texture3d_validity
                    examples/sdlgpu_texture3d_validity_test.cpp)
    cna_register_backend_test(NAME SdlGpu_Texture3D_Validity
        COMMAND cna_test_sdlgpu_texture3d_validity
        TIMEOUT 60 LABELS "SdlGpu"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
    set_tests_properties(SdlGpu_Texture3D SdlGpu_Texture3D_Validity PROPERTIES
        FAIL_REGULAR_EXPRESSION
            "VUID-(VkImageViewCreateInfo-image-02724|VkImageViewCreateInfo-subresourceRange-02725|vkCmdBlitImage-dstOffset-00251|vkCmdBlitImage-pRegions-00216|vkCmdBlitImage-srcOffset-00246|vkCmdBlitImage-pRegions-00215);Validation (Error|Warning)")

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

    # REMED-GFX-051: RasterizerState.DepthBias/SlopeScaleDepthBias are pipeline-static in SDL 3.5.
    # Exact/near-coplanar flat+sloped triangles prove both signs and combined bias through stock
    # effects, indexed/non-indexed draws, SpriteBatch, RT/backbuffer, deferred A->B->A state, and
    # target-independent cache identity. Any Vulkan validation category is fatal.
    cna_sdlgpu_test(cna_test_sdlgpu_depth_bias examples/sdlgpu_depth_bias_test.cpp)
    cna_register_backend_test(NAME SdlGpu_DepthBias COMMAND cna_test_sdlgpu_depth_bias
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
    set_tests_properties(SdlGpu_DepthBias PROPERTIES
        FAIL_REGULAR_EXPRESSION "Validation (Error|Warning);VUID-")

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

    # REMED-GFX-127: every public Texture2D::GetData call must return the resource's real content or
    # reject the request deterministically -- never fabricate one. Pre-fix the shared render-target
    # fallback zero-initialized its own scratch buffer, handed it to ITextureBackend::GetData (whose
    # interface default did nothing) and converted it for the caller regardless, so a backend with no
    # readback returned a complete, uniformly transparent-black frame that satisfied both "the
    # destination was overwritten" and any transparent-black content expectation.
    cna_sdlgpu_test(cna_test_sdlgpu_texture2d_getdata_contract
                    examples/texture2d_getdata_contract_test.cpp)
    cna_register_backend_test(NAME SdlGpu_Texture2D_GetDataContract COMMAND cna_test_sdlgpu_texture2d_getdata_contract
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-130: the TextureCube/Texture3D half of REMED-GFX-127's finding. Both public cube and
    # volume GetData paths converted their own zero-initialized scratch buffer for the caller
    # regardless of whether the backend read anything back, so an unimplemented readback returned a
    # complete, uniformly transparent-black face/volume instead of rejecting the request.
    cna_sdlgpu_test(cna_test_sdlgpu_cube_volume_getdata_contract
                    examples/texturecube_texture3d_getdata_contract_test.cpp)
    cna_register_backend_test(NAME SdlGpu_CubeVolume_GetDataContract COMMAND cna_test_sdlgpu_cube_volume_getdata_contract
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-134: `RenderTargetCube::GetData` must return the face that was really rendered or
    # reject the request deterministically. REMED-GFX-130 made the reporting honest but left the
    # readback itself implemented on only two backends, so a rendered cube face had no byte-exact
    # public oracle anywhere else. Drawn geometry (never Clear -- see REMED-GFX-129) paints an
    # asymmetric five-region pattern whose colours rotate per face, so a flip, a rotation, a wrong
    # array layer/subresource, a stale face or an unresolved multisample surface all fail.
    cna_sdlgpu_test(cna_test_sdlgpu_rendertargetcube_getdata_contract
                    examples/rendertargetcube_getdata_contract_test.cpp)
    cna_register_backend_test(NAME SdlGpu_RenderTargetCube_GetDataContract COMMAND cna_test_sdlgpu_rendertargetcube_getdata_contract
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-136: IGraphicsBackend::CreateRenderTargetCube had no `preserveContents` parameter,
    # unlike CreateRenderTarget2D, so a RenderTargetCube's real RenderTargetUsage never reached the
    # backend and Vulkan/WebGPU discarded a PreserveContents cube face on every bind cycle. Reuses
    # REMED-GFX-134's asymmetric face producer, then rebinds and draws only a small marker: "the
    # marker landed" is what a discarding backend also achieves, so it can never pass for
    # preservation.
    cna_sdlgpu_test(cna_test_sdlgpu_rendertargetcube_usage
                    examples/rendertargetcube_usage_test.cpp)
    cna_register_backend_test(NAME SdlGpu_RenderTargetCube_Usage COMMAND cna_test_sdlgpu_rendertargetcube_usage
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-141: a MULTISAMPLED RenderTargetCube shared ONE multisample colour attachment
    # across all six faces on EasyGL, Vulkan and SdlGpu, so a PreserveContents face reloaded whichever
    # face was rendered last instead of its own samples. Renders face A, then face B, then rebinds A for
    # a small partial update -- with no readback, Present or flush in between -- and reads the whole of
    # A: the resolved single-sample layer still held the correct A, which is why a full redraw and an
    # immediate read both passed before this.
    cna_sdlgpu_test(cna_test_sdlgpu_rendertargetcube_msaa_face
                    examples/rendertargetcube_msaa_face_test.cpp)
    cna_register_backend_test(NAME SdlGpu_RenderTargetCube_MsaaFace COMMAND cna_test_sdlgpu_rendertargetcube_msaa_face
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-142: RenderTargetUsage's DEPTH and STENCIL half. FNA3D's own header documents
    # `preserveTargetContents` as storing the "color/depth/stencil" contents, and FNA's DiscardContents
    # bind clears all three (Target|DepthBuffer|Stencil, MaxDepth, 0) -- so the enum governs depth and
    # stencil, not colour alone. Renders an occluder, unbinds, rebinds WITHOUT clearing and draws behind
    # it: three bands separate preserved depth from cleared depth, lost colour and a second pass that
    # never drew. A parallel stencil stamp/gate sequence does the same for stencil.
    cna_sdlgpu_test(cna_test_sdlgpu_rendertarget_depthstencil_usage
                    examples/rendertarget_depthstencil_usage_test.cpp)
    cna_register_backend_test(NAME SdlGpu_RenderTarget_DepthStencilUsage COMMAND cna_test_sdlgpu_rendertarget_depthstencil_usage
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-140: every public render-target bind/unbind cycle must be its own logical pass.
    # `VulkanGraphicsBackend::RecordCommandBuffer` collected ONE render pass per unique render-target
    # source per flush and replayed every queued batch for it inside that pass, so two bind cycles of
    # one target within a single flush window shared one load action. The decisive checks all use
    # DiscardContents -- collapsing is invisible on a preserving target, which is why REMED-GFX-136
    # needed an artificial readback barrier to see it -- and nothing between two cycles ever forces a
    # flush.
    cna_sdlgpu_test(cna_test_sdlgpu_rendertarget_pass_boundary
                    examples/rendertarget_pass_boundary_test.cpp)
    cna_register_backend_test(NAME SdlGpu_RenderTarget_PassBoundary COMMAND cna_test_sdlgpu_rendertarget_pass_boundary
        TIMEOUT 90 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-129: cross-backend control for Vulkan's ordered-Clear correction.
    cna_sdlgpu_test(cna_test_sdlgpu_ordered_clear
                    examples/graphicsdevice_ordered_clear_test.cpp)
    cna_register_backend_test(NAME SdlGpu_GraphicsDevice_OrderedClear COMMAND cna_test_sdlgpu_ordered_clear
        TIMEOUT 120 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-143: backbuffer work and render-target work must replay in ONE ordered stream.
    # REMED-GFX-140 (Vulkan) and REMED-GFX-145 (SdlGpu) gave every render-target bind cycle its own
    # native pass in public order, but both kept the BACKBUFFER as one trailing pass, so every
    # backbuffer draw of a frame was replayed after every target pass regardless of when it was
    # issued. A frame that samples a target onto the backbuffer and then renders into that target
    # again therefore saw the FINAL content. Every check queues its whole public sequence with no
    # Present, GetData, flush or extra frame between the cycles, and only then reads once.
    cna_sdlgpu_test(cna_test_sdlgpu_backbuffer_pass_order
                    examples/backbuffer_pass_order_test.cpp)
    cna_register_backend_test(NAME SdlGpu_Backbuffer_PassOrder COMMAND cna_test_sdlgpu_backbuffer_pass_order
        TIMEOUT 120 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-145: the SDL_GPU-specific half of the same contract. The shared oracle above drives
    # SpriteBatch only and scopes itself to colour, so it cannot see a segment filter that works for
    # `DrawKind::Sprite` and not for the eight 3D families, a transient upload arena aliasing
    # between passes (its producer draws identical rectangles and varies only the texture), or a
    # depth/stencil load action attached to the wrong bind cycle. This one asks all three, with
    # geometry that differs left-from-right and with REMED-GFX-117's startIndex/baseVertex issued
    # inside a SECOND logical pass.
    cna_sdlgpu_test(cna_test_sdlgpu_pass_boundary_upload
                    examples/sdlgpu_pass_boundary_upload_test.cpp)
    cna_register_backend_test(NAME SdlGpu_PassBoundary_Upload COMMAND cna_test_sdlgpu_pass_boundary_upload
        TIMEOUT 90 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-135: the WRITE half of the same finding. `TextureCube::SetData`/`Texture3D::SetData`
    # kept the pre-REMED-GFX-127 shape -- a `void` backend method behind `if (backend_)` -- so an
    # upload that stored nothing, or only part of the requested region, still returned normally.
    cna_sdlgpu_test(cna_test_sdlgpu_cube_volume_setdata_contract
                    examples/texturecube_texture3d_setdata_contract_test.cpp)
    cna_register_backend_test(NAME SdlGpu_CubeVolume_SetDataContract COMMAND cna_test_sdlgpu_cube_volume_setdata_contract
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
endif()
