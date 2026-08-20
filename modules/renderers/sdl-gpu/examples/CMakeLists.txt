# sdl-gpu renderer example/test registration (module-local; this file is the
# former cmake/Tests/SdlGpuTests.cmake, moved when examples moved into their owning
# physical modules).
#
# From the root CMakeLists.txt include site:
# plans/plan_sdlgpu.md SDLGPU-12: first end-to-end proof for the SDL_GPU graphics renderer -- a real
# window, a real SDL_GPUDevice, and a 60-frame Clear()+Present() loop. Needs a real
# display/GPU, same as the Vulkan/Bgfx/D3D11 CTest entries above -- run with a real DISPLAY.

# Example/test executables historically live at the build root (scripts, docs and
# the golden-image working-directory conventions rely on it); keep them there.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
# Shared example-support headers ("common/PixelTestGame.hpp", ...) are owned by
# the graphics module's examples directory.
include_directories(${CNA_GRAPHICS_EXAMPLES_DIR})

if(CNA_BUILD_TESTS AND CNA_GRAPHICS_RENDERER STREQUAL "SDL_GPU")
    enable_testing()

    macro(cna_sdlgpu_test target src)
        add_executable(${target} ${src})
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(${target} PRIVATE
                CNA
                SHARP_RUNTIME SDL3::SDL3)
        else()
            target_link_libraries(${target} PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(${target} PRIVATE SDL3::SDL3main)
        endif()
    endmacro()

    cna_sdlgpu_test(cna_test_sdlgpu_smoke sdlgpu_smoke_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_Smoke COMMAND cna_test_sdlgpu_smoke
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-028: every real constructor acquisition is transactional, failed objects never
    # enter the window registry, and lazy command/pipeline/sampler/fallback failures remain
    # retryable. Resource callbacks make leaks/double-destruction observable independently of
    # whether the selected failure point throws.
    cna_sdlgpu_test(cna_test_sdlgpu_constructor_exception_safety
                    sdlgpu_constructor_exception_safety_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_ConstructorExceptionSafety
        COMMAND cna_test_sdlgpu_constructor_exception_safety
        TIMEOUT 240 LABELS "SdlGpu"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
    set_tests_properties(SdlGpu_ConstructorExceptionSafety PROPERTIES
        FAIL_REGULAR_EXPRESSION "Validation (Error|Warning);VUID-")

    # plans/plan_sdlgpu.md SDLGPU-22..25: 2D vertical slice -- real Texture2D + SpriteBatch.
    cna_sdlgpu_test(cna_test_sdlgpu_2d sdlgpu_2d_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_2D COMMAND cna_test_sdlgpu_2d
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Ad-hoc manual diagnostic (not a CTest) -- see the file's own header comment.
    cna_sdlgpu_test(cna_diag_sdlgpu_single_sprite sdlgpu_diag_single_sprite.cpp)

    # plans/plan_sdlgpu.md SDLGPU-26..30: core 3D vertex formats and BasicEffect.
    cna_sdlgpu_test(cna_test_sdlgpu_3d sdlgpu_3d_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_3D COMMAND cna_test_sdlgpu_3d
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plans/plan_sdlgpu.md SDLGPU-31/32: AlphaTestEffect and DualTextureEffect.
    cna_sdlgpu_test(cna_test_sdlgpu_effects sdlgpu_effects_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_Effects COMMAND cna_test_sdlgpu_effects
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plans/plan_sdlgpu.md SDLGPU-35: RenderTarget2D.
    cna_sdlgpu_test(cna_test_sdlgpu_rendertarget2d sdlgpu_rendertarget2d_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_RenderTarget2D COMMAND cna_test_sdlgpu_rendertarget2d
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plans/plan_sdlgpu.md SDLGPU-36: RenderTargetCube.
    cna_sdlgpu_test(cna_test_sdlgpu_rendertargetcube sdlgpu_rendertargetcube_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_RenderTargetCube COMMAND cna_test_sdlgpu_rendertargetcube
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-188: mipMap and MSAA are independent on RenderTargetCube. The resolved cube owns
    # every public level, each one-level face attachment resolves before that face's chain is
    # generated, and a later pass in the same frame can sample the generated levels. All native
    # validation diagnostics are fatal in addition to the pixel/count/lifecycle matrix.
    cna_sdlgpu_test(cna_test_sdlgpu_rendertargetcube_msaa_mip
                    sdlgpu_rendertargetcube_msaa_mip_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_RenderTargetCube_MsaaMip
        COMMAND cna_test_sdlgpu_rendertargetcube_msaa_mip
        TIMEOUT 300 LABELS "SdlGpu"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
    set_tests_properties(SdlGpu_RenderTargetCube_MsaaMip PROPERTIES
        FAIL_REGULAR_EXPRESSION "Validation (Error|Warning);VUID-")

    # REMED-GFX-096: renderer-neutral singular/plural cube-face binding parity.
    cna_sdlgpu_test(cna_test_sdlgpu_rendertargetcube_plural_binding
                    ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertargetcube_plural_binding_test.cpp)
    target_compile_definitions(cna_test_sdlgpu_rendertargetcube_plural_binding
        PRIVATE CNA_GFX096_CUBE_GETDATA=1)
    cna_register_renderer_test(NAME SdlGpu_RenderTargetCube_PluralBinding
        COMMAND cna_test_sdlgpu_rendertargetcube_plural_binding
        TIMEOUT 60 LABELS "SdlGpu"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-097: a depthless cube-face SpriteBatch draw must use a graphics pipeline whose
    # target info also has no depth/stencil target. Pixel checks prove exact face selection; make
    # the Vulkan compatibility VUID independently fatal so validation-cleanliness cannot be hidden
    # by otherwise-correct pixels.
    cna_sdlgpu_test(cna_test_sdlgpu_depthless_cube_spritebatch_compatibility
                    sdlgpu_depthless_cube_spritebatch_compatibility_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_DepthlessCube_SpriteBatchCompatibility
        COMMAND cna_test_sdlgpu_depthless_cube_spritebatch_compatibility
        TIMEOUT 60 LABELS "SdlGpu"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
    set_tests_properties(SdlGpu_DepthlessCube_SpriteBatchCompatibility PROPERTIES
        FAIL_REGULAR_EXPRESSION "VUID-vkCmdDraw-renderPass-02684")

    # plans/plan_sdlgpu.md SDLGPU-37: Multiple Render Targets (MRT).
    cna_sdlgpu_test(cna_test_sdlgpu_mrt sdlgpu_mrt_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_MRT COMMAND cna_test_sdlgpu_mrt
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
    # REMED-GFX-098: pixels alone were always correct; a color-attachment render-pass
    # compatibility VUID is fatal for this regression.
    set_tests_properties(SdlGpu_MRT PROPERTIES
        FAIL_REGULAR_EXPRESSION "VUID-vkCmdDraw-renderPass-02684")

    # plans/plan_sdlgpu.md SDLGPU-38: RenderTarget2D MSAA.
    cna_sdlgpu_test(cna_test_sdlgpu_rendertarget2d_msaa sdlgpu_rendertarget2d_msaa_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_RenderTarget2DMSAA COMMAND cna_test_sdlgpu_rendertarget2d_msaa
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plans/plan_sdlgpu.md SDLGPU-40/41: Texture3D + mipmaps.
    cna_sdlgpu_test(cna_test_sdlgpu_texture3d sdlgpu_texture3d_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_Texture3D COMMAND cna_test_sdlgpu_texture3d
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-099: the first invalid event occurs while constructing a mipmapped Texture3D,
    # before upload/readback. Keep the create-only discriminator separate from the larger data
    # contract and make both the six exact original VUIDs and any new validation category fatal.
    cna_sdlgpu_test(cna_test_sdlgpu_texture3d_validity
                    sdlgpu_texture3d_validity_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_Texture3D_Validity
        COMMAND cna_test_sdlgpu_texture3d_validity
        TIMEOUT 60 LABELS "SdlGpu"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
    set_tests_properties(SdlGpu_Texture3D SdlGpu_Texture3D_Validity PROPERTIES
        FAIL_REGULAR_EXPRESSION
            "VUID-(VkImageViewCreateInfo-image-02724|VkImageViewCreateInfo-subresourceRange-02725|vkCmdBlitImage-dstOffset-00251|vkCmdBlitImage-pRegions-00216|vkCmdBlitImage-srcOffset-00246|vkCmdBlitImage-pRegions-00215);Validation (Error|Warning)")

    # plans/plan_sdlgpu.md SDLGPU-51: plain, non-render-target TextureCube.
    cna_sdlgpu_test(cna_test_sdlgpu_texturecube sdlgpu_texturecube_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_TextureCube COMMAND cna_test_sdlgpu_texturecube
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plans/plan_sdlgpu.md SDLGPU-33: EnvironmentMapEffect.
    cna_sdlgpu_test(cna_test_sdlgpu_envmap sdlgpu_envmap_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_EnvMap COMMAND cna_test_sdlgpu_envmap
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-007: EnvironmentMapEffect must add EmissiveColor UNSCALED (FNA Lighting.fxh), not
    # (Emissive+lightSum)*Diffuse -- which also squares Alpha (both operands are CPU-prefolded).
    # Discriminating non-white-Diffuse / non-zero-Emissive / Alpha=0.5 cases; linear RT readback.
    cna_sdlgpu_test(cna_test_sdlgpu_envmap_emissive sdlgpu_environmentmapeffect_emissive_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_EnvMapEmissive COMMAND cna_test_sdlgpu_envmap_emissive
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plans/plan_sdlgpu.md SDLGPU-34: SkinnedEffect.
    cna_sdlgpu_test(cna_test_sdlgpu_skinned sdlgpu_skinned_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_Skinned COMMAND cna_test_sdlgpu_skinned
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # PbrEffect/SkinnedPbrEffect (metallic-roughness BRDF, stride 48/68) and SkinnedEffect's
    # stride-56 VertexColorEnabled path -- porting EasyGL's own PBR + skinned-vertex-color shader
    # support to this renderer (see SdlGpuRenderer::CreatePbrResources()/
    # GetOrCreatePipelinePbr3D() and GetOrCreatePipelineSkinned3D's hasVertexColor parameter).
    cna_sdlgpu_test(cna_test_sdlgpu_pbreffect sdlgpu_pbreffect_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_PbrEffect COMMAND cna_test_sdlgpu_pbreffect
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_sdlgpu_test(cna_test_sdlgpu_skinnedpbreffect sdlgpu_skinnedpbreffect_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_SkinnedPbrEffect COMMAND cna_test_sdlgpu_skinnedpbreffect
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # GLTF-213: exact PBR sRGB transfer witnesses on rigid and identity-skinned pipelines.
    cna_sdlgpu_test(cna_test_sdlgpu_pbr_srgb_transfer
                    ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_pbr_srgb_transfer_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_Pbr_SrgbTransfer COMMAND cna_test_sdlgpu_pbr_srgb_transfer
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # GLTF-343/344: KHR_materials_ior and factor-only KHR_materials_specular must alter both
    # dielectric Schlick endpoints on rigid and identity-skinned PBR pipelines.
    cna_sdlgpu_test(cna_test_sdlgpu_pbr_fresnel_factors
                    ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_pbr_fresnel_factors_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_Pbr_FresnelFactors COMMAND cna_test_sdlgpu_pbr_fresnel_factors
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_sdlgpu_test(cna_test_sdlgpu_skinnedeffect_vertexcolor sdlgpu_skinnedeffect_vertexcolor_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_SkinnedEffectVertexColor COMMAND cna_test_sdlgpu_skinnedeffect_vertexcolor
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-006: SkinnedEffect world-space normal transform under non-identity World (rotation,
    # non-uniform scale, and both) -- the cases every existing skinned test, all of which use
    # World = Identity, structurally cannot detect. RenderTarget2D readback (swapchain download
    # segfaults on this renderer, SDLGPU-39).
    cna_sdlgpu_test(cna_test_sdlgpu_skinnedeffect_world_normal sdlgpu_skinnedeffect_world_normal_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_SkinnedEffect_WorldNormal COMMAND cna_test_sdlgpu_skinnedeffect_world_normal
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-009: stock-effect fog (previously a total absence on this renderer). Blue geometry +
    # red fog, FogStart=0/FogEnd=-0.9 -> keep=1/0.5/0 at Z=0/0.45/0.9; linear RT readback. One file
    # per effect group covering all 13 fog-capable shader families.
    cna_sdlgpu_test(cna_test_sdlgpu_basiceffect_fog sdlgpu_basiceffect_fog_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_BasicEffect_Fog COMMAND cna_test_sdlgpu_basiceffect_fog
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_sdlgpu_test(cna_test_sdlgpu_effects_fog sdlgpu_effects_fog_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_Effects_Fog COMMAND cna_test_sdlgpu_effects_fog
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_sdlgpu_test(cna_test_sdlgpu_skinnedeffect_fog sdlgpu_skinnedeffect_fog_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_SkinnedEffect_Fog COMMAND cna_test_sdlgpu_skinnedeffect_fog
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-010: transformed-camera view-space fog conformance (fails pre-fix).
    cna_sdlgpu_test(cna_test_sdlgpu_viewspace_fog sdlgpu_viewspace_fog_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_ViewSpace_Fog COMMAND cna_test_sdlgpu_viewspace_fog
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plans/plan_sdlgpu.md SDLGPU-42/43: custom ShaderEffect (runtime GLSL->SPIR-V via libshaderc).
    cna_sdlgpu_test(cna_test_sdlgpu_shadereffect sdlgpu_shadereffect_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_ShaderEffect COMMAND cna_test_sdlgpu_shadereffect
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plans/plan_sdlgpu.md SDLGPU-18/19/20: dynamic BlendState/DepthStencilState/RasterizerState.
    cna_sdlgpu_test(cna_test_sdlgpu_renderstate sdlgpu_renderstate_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_RenderState COMMAND cna_test_sdlgpu_renderstate
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-051: RasterizerState.DepthBias/SlopeScaleDepthBias are pipeline-static in SDL 3.5.
    # Exact/near-coplanar flat+sloped triangles prove both signs and combined bias through stock
    # effects, indexed/non-indexed draws, SpriteBatch, RT/backbuffer, deferred A->B->A state, and
    # target-independent cache identity. Any Vulkan validation category is fatal.
    cna_sdlgpu_test(cna_test_sdlgpu_depth_bias sdlgpu_depth_bias_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_DepthBias COMMAND cna_test_sdlgpu_depth_bias
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
    set_tests_properties(SdlGpu_DepthBias PROPERTIES
        FAIL_REGULAR_EXPRESSION "Validation (Error|Warning);VUID-")

    # plans/plan_sdlgpu.md SDLGPU-21: dynamic SamplerState for direct 3D draws.
    cna_sdlgpu_test(cna_test_sdlgpu_samplerstate sdlgpu_samplerstate_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_SamplerState COMMAND cna_test_sdlgpu_samplerstate
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plans/plan_sdlgpu.md: real chronological draw-order proof (adversarial-review finding #4).
    cna_sdlgpu_test(cna_test_sdlgpu_draworder sdlgpu_draworder_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_DrawOrder COMMAND cna_test_sdlgpu_draworder
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plans/plan_sdlgpu.md SDLGPU-11: hard swapchain-acquisition-failure recovery proof.
    cna_sdlgpu_test(cna_test_sdlgpu_swapchain_recovery sdlgpu_swapchain_recovery_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_SwapchainRecovery COMMAND cna_test_sdlgpu_swapchain_recovery
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plans/plan_sdlgpu.md: render-target-destroyed-before-flush use-after-free regression proof.
    cna_sdlgpu_test(cna_test_sdlgpu_rt_lifetime sdlgpu_rendertarget_lifetime_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_RenderTargetLifetime COMMAND cna_test_sdlgpu_rt_lifetime
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-064: GraphicsDevice.Viewport honored per-draw in RenderTarget2D passes.
    cna_sdlgpu_test(cna_test_sdlgpu_rt_viewport sdlgpu_rendertarget_viewport_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_RenderTargetViewport COMMAND cna_test_sdlgpu_rt_viewport
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-068: GraphicsDevice.ScissorRectangle honored per-draw in RenderTarget2D passes
    # (deferred-model analog of Vulkan GFX-013; scissor was applied once per pass from the live
    # post-unbind full-backbuffer state, so an RT-bound scissor no longer clipped at replay).
    cna_sdlgpu_test(cna_test_sdlgpu_rt_scissor sdlgpu_rendertarget_scissor_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_RenderTargetScissor COMMAND cna_test_sdlgpu_rt_scissor
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-069: GraphicsDevice.BlendFactor (constant blend color) reaches the GPU per-draw in
    # RenderTarget2D passes (blend-constant analog of GFX-064/068; SdlGpu never overrode the base
    # SetBlendFactor / called SDL_SetGPUBlendConstants, so constant-color blends used SDL's (0,0,0,0)).
    cna_sdlgpu_test(cna_test_sdlgpu_rt_blendfactor sdlgpu_rendertarget_blendfactor_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_RenderTargetBlendFactor COMMAND cna_test_sdlgpu_rt_blendfactor
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-072: SpriteBatch clip space must be built from the active GraphicsDevice.Viewport
    # (Viewport.Width/Height), not the full target -- the sprite2d viewportSize uniform was fed the
    # full target dims while ApplyViewportForRef (GFX-064) mapped NDC into the sub-region, squishing
    # the sprite. The per-QueuedDrawRef viewport must also drive the sprite NDC divide. SdlGpu does
    # not implement GetBackBufferData, so this uses the RenderTarget2D-readback variant (GetData
    # flushes the deferred sprite batch into the RT); it also covers the two-viewports-per-frame
    # (per-QueuedDrawRef) and transformMatrix cases.
    cna_sdlgpu_test(cna_test_sdlgpu_spritebatch_custom_viewport ${CNA_GRAPHICS_EXAMPLES_DIR}/spritebatch_custom_viewport_rt_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_SpriteBatch_CustomViewport COMMAND cna_test_sdlgpu_spritebatch_custom_viewport
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-081: SpriteBatch.Begin must apply its RasterizerState argument (shared fix in
    # SpriteBatch.cpp). Deferred-renderer control: the scissor state must reach the QueuedDrawRef
    # through Begin, not only via a direct GraphicsDevice.RasterizerState assignment.
    cna_sdlgpu_test(cna_test_sdlgpu_spritebatch_begin_rasterizerstate ${CNA_GRAPHICS_EXAMPLES_DIR}/spritebatch_begin_rasterizerstate_scissor_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_SpriteBatch_BeginRasterizerState COMMAND cna_test_sdlgpu_spritebatch_begin_rasterizerstate
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-077: BlendState.ColorWriteChannels (RT0) via SDL_GPUColorTargetBlendState
    # .color_write_mask (pipeline cache key). MultiSampleMask is NOT compiled in: SDL 3.5.0 reserves
    # SDL_GPUMultisampleState::sample_mask as non-functional (REMED-GFX-086). Generic 3D path.
    cna_sdlgpu_test(cna_test_sdlgpu_colorwritechannels ${CNA_GRAPHICS_EXAMPLES_DIR}/gfx077_colorwritechannels_3d_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_ColorWriteChannels COMMAND cna_test_sdlgpu_colorwritechannels
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-127: every public Texture2D::GetData call must return the resource's real content or
    # reject the request deterministically -- never fabricate one. Pre-fix the shared render-target
    # fallback zero-initialized its own scratch buffer, handed it to ITextureRenderer::GetData (whose
    # interface default did nothing) and converted it for the caller regardless, so a renderer with no
    # readback returned a complete, uniformly transparent-black frame that satisfied both "the
    # destination was overwritten" and any transparent-black content expectation.
    # REMED-GFX-131: SurfaceFormat::Color is a plain 8-bit UNORM byte format, so a mid-tone channel
    # must survive Clear/draw/sample/readback unchanged. Registered here as a cross-renderer control:
    # the defect was WebGPU-local (its render targets used the swapchain's *UnormSrgb format), and
    # these runs are what establish that byte-exact identity is CNA's existing behaviour everywhere
    # else rather than a value invented for the fix.
    cna_sdlgpu_test(cna_test_sdlgpu_colorspace_midtone
        ${CNA_GRAPHICS_EXAMPLES_DIR}/colorspace_midtone_contract_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_ColorSpace_MidTone COMMAND cna_test_sdlgpu_colorspace_midtone
        TIMEOUT 120 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_sdlgpu_test(cna_test_sdlgpu_additive_blend_contract
        ${CNA_GRAPHICS_EXAMPLES_DIR}/additive_blend_contract_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_AdditiveBlendContract
        COMMAND cna_test_sdlgpu_additive_blend_contract
        TIMEOUT 120 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-147: a RenderTarget2D used as a texture must sample in the same logical orientation
    # as an ordinary Texture2D holding identical bytes. Registered here as a cross-renderer control:
    # the defect was EasyGL-local (an OpenGL framebuffer's origin is bottom-left, so a target's
    # colour texture stores the image bottom-up and sampling did not compensate even though GetData
    # already did), and these runs are what establish that render-target and ordinary-texture
    # sampling already agree everywhere else rather than being made to agree by the fix.
    cna_sdlgpu_test(cna_test_sdlgpu_rt_sampling_orientation
        ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_sampling_orientation_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_RenderTarget_SamplingOrientation COMMAND cna_test_sdlgpu_rt_sampling_orientation
        TIMEOUT 120 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-153 cross-renderer source-selection and render-target-destination control. SDL_GPU's
    # established missing swapchain-download boundary is asserted separately inside the fixture.
    cna_sdlgpu_test(cna_test_sdlgpu_gfx153_source_rectangle
        ${CNA_GRAPHICS_EXAMPLES_DIR}/source_rectangle_orientation_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_GFX153_SourceRectangleOrientation
        COMMAND cna_test_sdlgpu_gfx153_source_rectangle
        TIMEOUT 180 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")



    # REMED-GFX-169: every stock 3D effect must sample its textures through the public
    # GraphicsDevice.SamplerStates[slot], exactly like SpriteBatch already does. Vulkan's
    # ApplySamplerState is correct and SpriteBatch reads slotSamplers_[0], but six stock 3D
    # descriptor builders take NO sampler parameter at all, so all 15 of their combined-image-
    # sampler bindings are hardcoded to defaultSampler_ (LINEAR + CLAMP_TO_EDGE) and the sampler is
    # absent from their cache keys too. Vulkan is where the defect lives; the other renderers run the
    # same public fixture as controls.
    cna_sdlgpu_test(cna_test_sdlgpu_stock_effect_sampler
        ${CNA_GRAPHICS_EXAMPLES_DIR}/stock_effect_sampler_contract_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_StockEffectSamplerContract COMMAND cna_test_sdlgpu_stock_effect_sampler
        TIMEOUT 300 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-150 cross-renderer control: TextureFilter::Point must select exactly ONE texel and
    # TextureAddressMode must decide which one, on SpriteBatch and on the device SamplerStates[0] 3D
    # path alike. The defect was Software-local (its ApplySamplerState named none of its parameters
    # and one bilinear function served every textured fragment, so every draw was LinearClamp); this
    # run is what establishes that this renderer already honoured the contract rather than being made
    # to.
    # REMED-GFX-170: every public TextureFilter ordinal names a SEPARATE magnification, a separate
    # minification and a separate mipmap filter, so a renderer may not reduce the ordinal to one
    # boolean. WebGPU's SpriteBatch sampler and SDL_GPU's ONE shared sampler helper both resolved
    # `textureFilter == 0 ? LINEAR : NEAREST`, and both keyed their sampler cache on
    # `filter == 0 ? 0 : 1`, so Anisotropic, LinearMipPoint, MinPointMagLinearMipLinear and
    # MinPointMagLinearMipPoint all magnified with POINT. This fixture measures the two DIFFERENT
    # partitions of the nine ordinals that magnification and minification induce, on SpriteBatch and
    # on every textured stock family; the other renderers run it as controls.
    cna_sdlgpu_test(cna_test_sdlgpu_texture_filter_ordinal
        ${CNA_GRAPHICS_EXAMPLES_DIR}/texture_filter_ordinal_contract_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_TextureFilterOrdinalContract COMMAND cna_test_sdlgpu_texture_filter_ordinal
        TIMEOUT 300 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-175 cross-renderer control: the MIPMAP component of a TextureFilter ordinal.
    # EasyGL mapped ordinals 0 and 1 onto a GL filter with no mipmap term and Software had no
    # mip pipeline at all; this fixture measures what every other renderer does with a chain
    # whose levels name themselves, so a divergence is classified rather than assumed.
    cna_sdlgpu_test(cna_test_sdlgpu_texture_filter_mip_contract
        ${CNA_GRAPHICS_EXAMPLES_DIR}/texture_filter_mip_contract_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_TextureFilterMipContract COMMAND cna_test_sdlgpu_texture_filter_mip_contract
        TIMEOUT 300 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-176: the question UNDERNEATH the filter contract above. SdlGpuTextureRenderer
    # hardcoded num_levels = 1 and declared no UpdatePixelsLevel override, so a mipMap=true
    # Texture2D reported a LevelCount its GPU resource did not have and every SetData above level 0
    # was silently discarded. This fixture asserts allocation and upload against the NATIVE texture
    # rather than inferring them from a sampled pixel, so a partial fix cannot look complete.
    cna_sdlgpu_test(cna_test_sdlgpu_texture2d_mip_storage
        sdlgpu_texture2d_mip_storage_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_Texture2DMipStorage COMMAND cna_test_sdlgpu_texture2d_mip_storage
        TIMEOUT 300 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-173: EnvironmentMapEffect samples TWO independent resources -- the ordinary 2D
    # texture through GraphicsDevice.SamplerStates[0] and the reflection cube through
    # SamplerStates[1]. SdlGpuRenderer::IssueEnvMapDraw bound a literal LinearClamp for the
    # cube and QueueEnvMapDraw captured only slot 0, so EnvMapDrawCommand had no field in which
    # slot 1 could reach replay. This fixture makes the two slots independently observable --
    # EnvironmentMapAmount 1 makes the cube the ONLY contributor and 0 makes the 2D texture the
    # only one -- so a hardcoded sampler shows up as interpolated colours that exist in no stored
    # texel. SDL_GPU is where the defect lives; the other renderers run it as controls.
    cna_sdlgpu_test(cna_test_sdlgpu_envmap_cube_sampler
        ${CNA_GRAPHICS_EXAMPLES_DIR}/envmap_cube_sampler_contract_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_EnvMapCubeSamplerContract COMMAND cna_test_sdlgpu_envmap_cube_sampler
        TIMEOUT 600 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-172 cross-renderer control: DualTextureEffect's two layers have INDEPENDENT public
    # sampler slots -- FNA's DualTextureEffect.fx declares DECLARE_TEXTURE(Texture, 0) and
    # DECLARE_TEXTURE(Texture2, 1). WebGPU declared ONE WGSL sampler for both texture views, so
    # SamplerStates[1] was inexpressible and slot 1 inherited slot 0's. This fixture observes both
    # layers in the SAME image -- slot 0's texture varies along X only, slot 1's along Y only, and
    # the shader's product is separable -- so independence is measured, not inferred. SDL_GPU
    # already carries texture0*/texture1* sampler fields per command and runs this as a control.
    cna_sdlgpu_test(cna_test_sdlgpu_dualtexture_slot_sampler
        ${CNA_GRAPHICS_EXAMPLES_DIR}/dualtexture_slot_sampler_contract_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_DualTextureSlotSamplerContract COMMAND cna_test_sdlgpu_dualtexture_slot_sampler
        TIMEOUT 600 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-182 cross-renderer control: the slot-1 sampler questions the fixture above does not
    # ask -- returning to a previous sampler, interleaving with an unrelated effect, sweeping either
    # slot with the other fixed, the magnification partition of the public enum, create/sample/
    # destroy lifetime and a degenerate reflection direction. The defect is Software-local; SDL_GPU
    # is REMED-GFX-173's own corrected renderer and a DEFERRED one, so it is where "a later sampler
    # change must not reach back into a queued draw" has teeth.
    cna_sdlgpu_test(cna_test_sdlgpu_envmap_cube_sampler_state
        ${CNA_GRAPHICS_EXAMPLES_DIR}/envmap_cube_sampler_state_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_EnvMapCubeSamplerState COMMAND cna_test_sdlgpu_envmap_cube_sampler_state
        TIMEOUT 600 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-177 cross-renderer control: descriptor/binding bookkeeping must be a function of what
    # is LIVE, never of what has ever existed. D3D12 owned four fixed-capacity heaps with a monotonic
    # bump cursor and no free list, so the 65th sampleable resource EVER CREATED threw even when six
    # were alive. Every draw here is checked against a self-identifying oracle that decodes back to an
    # integer, so a recycled or grown pool that aliases one resource onto another's binding names the
    # wrong resource rather than merely producing an odd colour.
    cna_sdlgpu_test(cna_test_sdlgpu_descriptor_capacity
        ${CNA_GRAPHICS_EXAMPLES_DIR}/descriptor_capacity_contract_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_DescriptorCapacityContract COMMAND cna_test_sdlgpu_descriptor_capacity
        TIMEOUT 900 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_sdlgpu_test(cna_test_sdlgpu_point_sampling
        ${CNA_GRAPHICS_EXAMPLES_DIR}/point_sampling_contract_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_PointSamplingContract COMMAND cna_test_sdlgpu_point_sampling
        TIMEOUT 300 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-151 cross-renderer control: the canonical XNA render-to-texture sequence -- render
    # into a target, unbind it, sample it -- must complete in ONE public frame with no intervening
    # GetData, Present, extra frame, manual flush or wait. The defect was Vulkan-local (its deferred
    # recorder's readback flush filtered the frame's segment list down to the target being READ, so a
    # producer's pass was never recorded before the consumer that sampled it); these runs are what
    # establish that every other renderer already honoured the contract rather than being made to.
    cna_sdlgpu_test(cna_test_sdlgpu_rt_producer_consumer
        ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_producer_consumer_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_RenderTarget_ProducerConsumer COMMAND cna_test_sdlgpu_rt_producer_consumer
        TIMEOUT 120 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-155 cross-renderer control: a render target produced and unbound earlier in a public
    # frame must be visible to a consumer that draws on the BACKBUFFER later in that same frame. The
    # defect was bgfx-local (it radix-sorts a frame's draws by their view's sort position, which
    # defaults to the numeric view id, and its backbuffer owns the lowest id -- so a backbuffer
    # consumer executed before its own producer); these runs are what establish that every other
    # renderer already honoured the contract rather than being made to.
    cna_sdlgpu_test(cna_test_sdlgpu_rt_backbuffer_consumer
        ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_backbuffer_consumer_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_RenderTarget_BackbufferConsumer COMMAND cna_test_sdlgpu_rt_backbuffer_consumer
        TIMEOUT 180 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-158's control: a RenderTarget2D constructed during a public frame must be usable in
    # that same frame -- bound, cleared and/or drawn into, unbound and observed -- with no warm-up
    # frame, Present, GetData-before-first-render, dummy draw, empty bind cycle, manual frame advance
    # or wait. The defect was bgfx-local (`bgfx::reset()` discards every view's framebuffer binding,
    # including the bound target's), so this run is what establishes that SDL_GPU already honoured the
    # contract rather than being made to.
    cna_sdlgpu_test(cna_test_sdlgpu_rt_first_use
        ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_first_use_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_RenderTarget_FirstUse COMMAND cna_test_sdlgpu_rt_first_use
        TIMEOUT 180 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-152: a RenderTarget2D handed to a stock or custom 3D effect as its Texture must be
    # sampled, not reinterpreted. The defect was SDL_GPU-local and fatal -- thirteen sites
    # static_cast an ITextureRenderer* to the unrelated sibling SdlGpuTextureRenderer, fabricating an
    # SDL_GPUTexture* out of a render target's mipMap_/padding/multiSampleCount_ bytes -- so this
    # fixture supervises each leg in its own process and reports a SIGSEGV as an attributable
    # result instead of losing the shard.
    cna_sdlgpu_test(cna_test_sdlgpu_rt_effect_source
        ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_effect_source_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_RenderTarget_EffectSource COMMAND cna_test_sdlgpu_rt_effect_source
        TIMEOUT 300 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-167 deferred-source lifetime: a resource sampled by a draw that has only been
    # QUEUED must stay bindable until that draw actually renders, and destroying the public wrapper
    # first must never terminate the process. The defect was WebGPU-local -- every deferred command
    # stored a raw pointer to the resource's RENDERER OBJECT and called a VIRTUAL method on it at
    # replay, so a RenderTarget2D produced, sampled onto the BACKBUFFER and dropped inside one
    # Draw() was a heap-use-after-free at Present(). These runs establish which renderers already
    # honoured the contract rather than being made to; each leg runs in its own process so a
    # SIGSEGV is an attributable result instead of a lost shard.
    cna_sdlgpu_test(cna_test_sdlgpu_deferred_source_lifetime
        ${CNA_GRAPHICS_EXAMPLES_DIR}/deferred_source_lifetime_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_DeferredSourceLifetime COMMAND cna_test_sdlgpu_deferred_source_lifetime
        TIMEOUT 300 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-168 cross-renderer control: destroying a RenderTarget2D that is STILL the bound
    # render target must never make the next SetRenderTarget transition unsafe, and must leave the
    # next target and the backbuffer exactly correct. The defect was EasyGL-local -- it remembered
    # the bound destination as a raw IRenderTargetRenderer* and the next transition called
    # UnbindAsRenderTarget() on it, so a scoped target leaving scope while bound made that
    # transition a virtual call through freed storage. This run is what establishes that SDL_GPU
    # already honoured the contract rather than being made to; each leg runs in its own process so
    # a SIGSEGV is an attributable result instead of a lost shard.
    cna_sdlgpu_test(cna_test_sdlgpu_bound_target_lifetime
        ${CNA_GRAPHICS_EXAMPLES_DIR}/bound_target_lifetime_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_BoundTargetLifetime COMMAND cna_test_sdlgpu_bound_target_lifetime
        TIMEOUT 600 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-180: the public render-target -> Present lifecycle contract. `SdlGpu_RenderState`
    # aborted with `Cannot present while render targets are bound`, which is the SECOND event in a
    # two-stage chain: an over-range DrawPrimitives (two triangles from a three-vertex buffer) was
    # correctly rejected by REMED-GFX-113's range guard, that rejection unwound past the fixture's own
    # SetRenderTarget(nullptr), and EndDraw then met a still-bound target. FNA's contract is REJECT,
    # never auto-unbind, so both guards are right and the fixture was wrong. Every leg runs in its own
    # process; leg C2 reproduces the abort end to end and its correct outcome is SIGABRT.
    cna_sdlgpu_test(cna_test_sdlgpu_present_lifecycle
        ${CNA_GRAPHICS_EXAMPLES_DIR}/present_lifecycle_contract_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_PresentLifecycle COMMAND cna_test_sdlgpu_present_lifecycle
        TIMEOUT 900 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-163 cross-renderer control: the same public depth-backed MULTISAMPLED
    # RenderTarget2D construction and lifecycle that aborted the process on Bgfx. Only Bgfx
    # production changed; this renderer is registered to show the construction was always legal
    # and to keep it that way.
    cna_sdlgpu_test(cna_test_sdlgpu_msaa_depth_contract
        ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_msaa_depth_contract_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_MsaaDepthContract COMMAND cna_test_sdlgpu_msaa_depth_contract
        TIMEOUT 900 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-154 cross-renderer control: the same PUBLIC first-readback contract on this
    # renderer. The defect was Bgfx-only and only Bgfx production changed; running the identical
    # fixture here is what makes that claim falsifiable rather than asserted.
    cna_sdlgpu_test(cna_test_sdlgpu_msaa_first_readback
        ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_msaa_first_readback_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_MsaaFirstReadback COMMAND cna_test_sdlgpu_msaa_first_readback
        TIMEOUT 900 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-186: reading a NONZERO MIP LEVEL of a mipmapped MULTISAMPLED RenderTarget2D.
    # Level 0 of the same target read back exactly while GetData(1, ...) died by SIGSEGV, so this
    # is the mip-level route, not the resolve (REMED-GFX-154) and not the transfer window
    # (REMED-GFX-149). Every leg runs in its own process because that signal is uncatchable.
    cna_sdlgpu_test(cna_test_sdlgpu_msaa_mip_readback
        ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_msaa_mip_readback_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_MsaaMipReadback COMMAND cna_test_sdlgpu_msaa_mip_readback
        TIMEOUT 1200 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
    # REMED-GFX-189 cross-renderer control: the same PUBLIC invalid-mip-level contract on this
    # renderer. The fabrication was Vulkan-only and only Vulkan production changed; this renderer
    # adopted the identical guard in REMED-GFX-186, so running the same fixture here is what keeps
    # both claims falsifiable.
    cna_sdlgpu_test(cna_test_sdlgpu_invalid_mip_level
        ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_invalid_mip_level_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_InvalidMipLevel COMMAND cna_test_sdlgpu_invalid_mip_level
        TIMEOUT 1200 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
    # REMED-GFX-157: a stock 3D draw issued AFTER a SpriteBatch inside ONE render-target bind cycle
    # must execute after it rather than be dropped. REMED-GFX-155's leg I0 measured the region it
    # drew into still holding the cycle's clear colour on BGFX, VULKAN, SOFTWARE and EASYGL, with
    # WEBGPU clean, using ordinary Texture2D sources on both destinations. The oracle is a staircase
    # -- step i covers stripes [0, N-i), so one readback proves every draw happened AND that they
    # happened in the issued order.
    cna_sdlgpu_test(cna_test_sdlgpu_spritebatch_3d_order
        ${CNA_GRAPHICS_EXAMPLES_DIR}/spritebatch_3d_order_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_SpriteBatch3DOrder COMMAND cna_test_sdlgpu_spritebatch_3d_order
        TIMEOUT 300 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-160: the XNA/FNA front-face winding contract. FNA's SpriteBatch emits
    # CLOCKWISE-as-displayed triangles and they must survive RasterizerState.CullCounterClockwise --
    # the state SpriteBatch.Begin itself defaults to -- so clockwise-as-displayed is XNA's FRONT face
    # and each enum names the face it CULLS. This renderer has no public backbuffer readback, so the
    # render-target legs carry the contract and the backbuffer ones record their boundary.
    cna_sdlgpu_test(cna_test_sdlgpu_frontface_winding
        ${CNA_GRAPHICS_EXAMPLES_DIR}/frontface_winding_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_FrontFaceWinding COMMAND cna_test_sdlgpu_frontface_winding
        TIMEOUT 300 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-183 cross-renderer control: the complete TriangleStrip parity/culling matrix.
    cna_sdlgpu_test(cna_test_sdlgpu_triangle_strip_winding
        ${CNA_GRAPHICS_EXAMPLES_DIR}/triangle_strip_winding_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_TriangleStripWinding
        COMMAND cna_test_sdlgpu_triangle_strip_winding
        TIMEOUT 300 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_sdlgpu_test(cna_test_sdlgpu_texture2d_getdata_contract
                    ${CNA_GRAPHICS_EXAMPLES_DIR}/texture2d_getdata_contract_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_Texture2D_GetDataContract COMMAND cna_test_sdlgpu_texture2d_getdata_contract
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-149 (and REMED-GFX-128, the same expression): the exact XNA-compatible
    # startIndex/elementCount contract of every public Texture2D::GetData overload. startIndex is a
    # DESTINATION element offset and elementCount is the destination capacity available from it;
    # the whole-level overload applied startIndex to the SOURCE, rejected any non-zero offset on a
    # render target, rejected legal excess capacity, and silently returned a partial frame for an
    # undersized one.
    cna_sdlgpu_test(cna_test_sdlgpu_texture2d_getdata_transfer_range
                    ${CNA_GRAPHICS_EXAMPLES_DIR}/texture2d_getdata_transfer_range_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_Texture2D_GetDataTransferRange COMMAND cna_test_sdlgpu_texture2d_getdata_transfer_range
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-130: the TextureCube/Texture3D half of REMED-GFX-127's finding. Both public cube and
    # volume GetData paths converted their own zero-initialized scratch buffer for the caller
    # regardless of whether the renderer read anything back, so an unimplemented readback returned a
    # complete, uniformly transparent-black face/volume instead of rejecting the request.
    cna_sdlgpu_test(cna_test_sdlgpu_cube_volume_getdata_contract
                    ${CNA_GRAPHICS_EXAMPLES_DIR}/texturecube_texture3d_getdata_contract_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_CubeVolume_GetDataContract COMMAND cna_test_sdlgpu_cube_volume_getdata_contract
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-134: `RenderTargetCube::GetData` must return the face that was really rendered or
    # reject the request deterministically. REMED-GFX-130 made the reporting honest but left the
    # readback itself implemented on only two renderers, so a rendered cube face had no byte-exact
    # public oracle anywhere else. Drawn geometry (never Clear -- see REMED-GFX-129) paints an
    # asymmetric five-region pattern whose colours rotate per face, so a flip, a rotation, a wrong
    # array layer/subresource, a stale face or an unresolved multisample surface all fail.
    cna_sdlgpu_test(cna_test_sdlgpu_rendertargetcube_getdata_contract
                    ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertargetcube_getdata_contract_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_RenderTargetCube_GetDataContract COMMAND cna_test_sdlgpu_rendertargetcube_getdata_contract
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-136: IGraphicsRenderer::CreateRenderTargetCube had no `preserveContents` parameter,
    # unlike CreateRenderTarget2D, so a RenderTargetCube's real RenderTargetUsage never reached the
    # renderer and Vulkan/WebGPU discarded a PreserveContents cube face on every bind cycle. Reuses
    # REMED-GFX-134's asymmetric face producer, then rebinds and draws only a small marker: "the
    # marker landed" is what a discarding renderer also achieves, so it can never pass for
    # preservation.
    cna_sdlgpu_test(cna_test_sdlgpu_rendertargetcube_usage
                    ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertargetcube_usage_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_RenderTargetCube_Usage COMMAND cna_test_sdlgpu_rendertargetcube_usage
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-141: a MULTISAMPLED RenderTargetCube shared ONE multisample colour attachment
    # across all six faces on EasyGL, Vulkan and SdlGpu, so a PreserveContents face reloaded whichever
    # face was rendered last instead of its own samples. Renders face A, then face B, then rebinds A for
    # a small partial update -- with no readback, Present or flush in between -- and reads the whole of
    # A: the resolved single-sample layer still held the correct A, which is why a full redraw and an
    # immediate read both passed before this.
    cna_sdlgpu_test(cna_test_sdlgpu_rendertargetcube_msaa_face
                    ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertargetcube_msaa_face_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_RenderTargetCube_MsaaFace COMMAND cna_test_sdlgpu_rendertargetcube_msaa_face
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-142: RenderTargetUsage's DEPTH and STENCIL half. FNA3D's own header documents
    # `preserveTargetContents` as storing the "color/depth/stencil" contents, and FNA's DiscardContents
    # bind clears all three (Target|DepthBuffer|Stencil, MaxDepth, 0) -- so the enum governs depth and
    # stencil, not colour alone. Renders an occluder, unbinds, rebinds WITHOUT clearing and draws behind
    # it: three bands separate preserved depth from cleared depth, lost colour and a second pass that
    # never drew. A parallel stencil stamp/gate sequence does the same for stencil.
    cna_sdlgpu_test(cna_test_sdlgpu_rendertarget_depthstencil_usage
                    ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_depthstencil_usage_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_RenderTarget_DepthStencilUsage COMMAND cna_test_sdlgpu_rendertarget_depthstencil_usage
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-140: every public render-target bind/unbind cycle must be its own logical pass.
    # `VulkanRenderer::RecordCommandBuffer` collected ONE render pass per unique render-target
    # source per flush and replayed every queued batch for it inside that pass, so two bind cycles of
    # one target within a single flush window shared one load action. The decisive checks all use
    # DiscardContents -- collapsing is invisible on a preserving target, which is why REMED-GFX-136
    # needed an artificial readback barrier to see it -- and nothing between two cycles ever forces a
    # flush.
    cna_sdlgpu_test(cna_test_sdlgpu_rendertarget_pass_boundary
                    ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_pass_boundary_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_RenderTarget_PassBoundary COMMAND cna_test_sdlgpu_rendertarget_pass_boundary
        TIMEOUT 90 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-129: cross-renderer control for Vulkan's ordered-Clear correction.
    cna_sdlgpu_test(cna_test_sdlgpu_ordered_clear
                    ${CNA_GRAPHICS_EXAMPLES_DIR}/graphicsdevice_ordered_clear_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_GraphicsDevice_OrderedClear COMMAND cna_test_sdlgpu_ordered_clear
        TIMEOUT 120 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-143: backbuffer work and render-target work must replay in ONE ordered stream.
    # REMED-GFX-140 (Vulkan) and REMED-GFX-145 (SdlGpu) gave every render-target bind cycle its own
    # native pass in public order, but both kept the BACKBUFFER as one trailing pass, so every
    # backbuffer draw of a frame was replayed after every target pass regardless of when it was
    # issued. A frame that samples a target onto the backbuffer and then renders into that target
    # again therefore saw the FINAL content. Every check queues its whole public sequence with no
    # Present, GetData, flush or extra frame between the cycles, and only then reads once.
    cna_sdlgpu_test(cna_test_sdlgpu_backbuffer_pass_order
                    ${CNA_GRAPHICS_EXAMPLES_DIR}/backbuffer_pass_order_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_Backbuffer_PassOrder COMMAND cna_test_sdlgpu_backbuffer_pass_order
        TIMEOUT 120 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-116 cross-renderer control: every deferred draw must execute under the
    # GraphicsDevice.Viewport active at its own public call. WebGPU resolved it live when it
    # recorded the pass; this file is the same public fixture, so a renderer that regresses the
    # contract is caught here rather than assumed correct.
    cna_sdlgpu_test(cna_test_sdlgpu_deferred_viewport
                    ${CNA_GRAPHICS_EXAMPLES_DIR}/deferred_viewport_capture_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_Deferred_Viewport COMMAND cna_test_sdlgpu_deferred_viewport
        TIMEOUT 120 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-146 cross-renderer control: every deferred draw must execute under the
    # GraphicsDevice.ScissorRectangle and RasterizerState.ScissorTestEnable active at its own
    # public call. WebGPU resolved both live when it recorded the pass; this file is the same
    # public fixture, so a renderer that regresses the contract is caught here rather than
    # assumed correct.
    cna_sdlgpu_test(cna_test_sdlgpu_deferred_scissor
                    ${CNA_GRAPHICS_EXAMPLES_DIR}/deferred_scissor_capture_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_Deferred_Scissor COMMAND cna_test_sdlgpu_deferred_scissor
        TIMEOUT 120 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-145: the SDL_GPU-specific half of the same contract. The shared oracle above drives
    # SpriteBatch only and scopes itself to colour, so it cannot see a segment filter that works for
    # `DrawKind::Sprite` and not for the eight 3D families, a transient upload arena aliasing
    # between passes (its producer draws identical rectangles and varies only the texture), or a
    # depth/stencil load action attached to the wrong bind cycle. This one asks all three, with
    # geometry that differs left-from-right and with REMED-GFX-117's startIndex/baseVertex issued
    # inside a SECOND logical pass.
    cna_sdlgpu_test(cna_test_sdlgpu_pass_boundary_upload
                    sdlgpu_pass_boundary_upload_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_PassBoundary_Upload COMMAND cna_test_sdlgpu_pass_boundary_upload
        TIMEOUT 90 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-135: the WRITE half of the same finding. `TextureCube::SetData`/`Texture3D::SetData`
    # kept the pre-REMED-GFX-127 shape -- a `void` renderer method behind `if (renderer_)` -- so an
    # upload that stored nothing, or only part of the requested region, still returned normally.
    cna_sdlgpu_test(cna_test_sdlgpu_cube_volume_setdata_contract
                    ${CNA_GRAPHICS_EXAMPLES_DIR}/texturecube_texture3d_setdata_contract_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_CubeVolume_SetDataContract COMMAND cna_test_sdlgpu_cube_volume_setdata_contract
        TIMEOUT 60 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-165: a rectangle-less GetBackBufferData must address the whole logical backbuffer
    # (PresentationParameters W x H) from the first observable frame, with no SetRenderTarget round
    # trip and no extra Present. SDL_GPU had no backbuffer readback at all (the swapchain texture is
    # write-only by permanent SDL contract), and its rectangle-less size check tripped on the same
    # aspect-scaled live viewport WebGPU used; both are addressed here.
    cna_sdlgpu_test(cna_test_sdlgpu_backbuffer_readback_dimension
                    ${CNA_GRAPHICS_EXAMPLES_DIR}/backbuffer_readback_dimension_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_BackbufferReadbackDimension COMMAND cna_test_sdlgpu_backbuffer_readback_dimension
        TIMEOUT 300 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-161: cross-renderer control for the first-read completion contract (preserves the
    # GFX-165 lazy-proxy backbuffer read; no SDL_GPU production change here).
    cna_sdlgpu_test(cna_test_sdlgpu_backbuffer_first_read
                    ${CNA_GRAPHICS_EXAMPLES_DIR}/backbuffer_first_read_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_BackbufferFirstRead COMMAND cna_test_sdlgpu_backbuffer_first_read
        TIMEOUT 300 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-162 cross-renderer control: SdlGpu's lazy-proxy GetBackBufferData (GFX-165) SUCCEEDS
    # and fully writes the requested range, so the Headless rejection is specific, not universal.
    cna_sdlgpu_test(cna_test_sdlgpu_backbuffer_reject
                    ${CNA_GRAPHICS_EXAMPLES_DIR}/backbuffer_headless_reject_test.cpp)
    cna_register_renderer_test(NAME SdlGpu_BackbufferReject COMMAND cna_test_sdlgpu_backbuffer_reject
        TIMEOUT 300 LABELS "SdlGpu" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
endif()

# Module-local tests get the same headless-safe skip convention the root one-shot
# gives root-registered tests (see cmake/TestHelpers.cmake).
cna_apply_skip_convention()
