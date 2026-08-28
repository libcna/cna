# webgpu renderer example/test registration (module-local; this file is the
# former cmake/Tests/WebGpuTests.cmake, moved when examples moved into their owning
# physical modules).

# Example/test executables historically live at the build root (scripts, docs and
# the golden-image working-directory conventions rely on it); keep them there.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
# Shared example-support headers ("common/PixelTestGame.hpp", ...) are owned by
# the graphics module's examples directory.
include_directories(${CNA_GRAPHICS_EXAMPLES_DIR})

if(CNA_BUILD_TESTS AND NOT EMSCRIPTEN AND NOT WIN32
   AND CNA_GRAPHICS_RENDERER STREQUAL "WEBGPU")
    enable_testing()

    # WEBGPU-150: route every display/GPU-backed WebGPU test (those whose ENVIRONMENT sets a DISPLAY)
    # through a preflight/retry launcher so a headless / no-adapter host SKIPs (CTest code 77) instead
    # of producing dozens of abort failures, while real failures and expected aborts propagate. Set
    # before any cna_register_renderer_test call so the shared macro (cmake/TestHelpers.cmake) sees it.
    set(CNA_RENDERER_GPU_TEST_LAUNCHER "${CMAKE_SOURCE_DIR}/modules/renderers/webgpu/webgpu_test_launcher.sh")
    add_test(NAME WebGPU_LauncherSelfTest COMMAND sh
        "${CMAKE_SOURCE_DIR}/modules/renderers/webgpu/webgpu_launcher_selftest.sh"
        "${CNA_RENDERER_GPU_TEST_LAUNCHER}")
    set_tests_properties(WebGPU_LauncherSelfTest PROPERTIES TIMEOUT 30 LABELS "WebGPU")
    if(TARGET cna_demo_2d)
        cna_register_renderer_test(NAME WebGPU_Native2D_Smoke COMMAND ${CMAKE_COMMAND} -DCNA_WEBGPU_DEMO=$<TARGET_FILE:cna_demo_2d> -P ${CMAKE_SOURCE_DIR}/cmake/WebGPUNativeSmokeTest.cmake
            TIMEOUT 70 LABELS "GraphicsSmoke;WebGPU" SKIP_REGULAR_EXPRESSION "\\[SKIP\\] CNA WebGPU native smoke")
    else()
        message(WARNING "CNA WebGPU: native smoke test requires CNA_BUILD_EXAMPLES=ON")
    endif()

    # WEBGPU-1: SHA-256 integrity verification for the auto-downloaded wgpu-native packages. A pure
    # CMake test (no GPU/display): it drives cmake/WebGPUChecksum.cmake with a good archive, a corrupt
    # archive (must fail closed and remove the archive + stamp) and an unknown/empty hash.
    cna_register_renderer_test(NAME WebGPU_ChecksumVerification COMMAND ${CMAKE_COMMAND}
        -DMODULE=${CMAKE_SOURCE_DIR}/cmake/WebGPUChecksum.cmake
        -DCHILD=${CMAKE_SOURCE_DIR}/cmake/tests/webgpu_checksum_verify_once.cmake
        -DWORKDIR=${CMAKE_CURRENT_BINARY_DIR}/webgpu_checksum_tmp
        -P ${CMAKE_SOURCE_DIR}/cmake/tests/webgpu_checksum_test.cmake
        TIMEOUT 30 LABELS "WebGPU")

    # WEBGPU-151: integration test of the real ThirdPartyWebGPU cache logic (cna_webgpu_prepare_cache
    # + the versioned/hashed stamp + purge helpers): old/legacy stamp migration, correct new stamp,
    # wrong hash (purge), changed version/asset, interrupted extraction, no archive. Pure CMake.
    cna_register_renderer_test(NAME WebGPU_CacheMigration COMMAND ${CMAKE_COMMAND}
        -DMODULE=${CMAKE_SOURCE_DIR}/cmake/WebGPUChecksum.cmake
        -DWORKDIR=${CMAKE_CURRENT_BINARY_DIR}/webgpu_cache_tmp
        -P ${CMAKE_SOURCE_DIR}/cmake/tests/webgpu_cache_test.cmake
        TIMEOUT 30 LABELS "WebGPU")

    # WEBGPU-91/92: pixel-asserted readback coverage -- registered the same way as
    # WebGPU_Native2D_Smoke (SKIP_REGULAR_EXPRESSION for a headless/no-display environment),
    # since ReadBackbuffer() needs a real GPU-backed surface exactly like the smoke test does.
    macro(cna_webgpu_test target src)
        add_executable(${target} ${src})
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(${target} PRIVATE
                CNA
                SHARP_RUNTIME)
        else()
            target_link_libraries(${target} PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(${target} PRIVATE SDL3::SDL3main)
        endif()
        if(CNA_WEBGPU_RUNTIME_LIBRARY)
            add_custom_command(TARGET ${target} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CNA_WEBGPU_RUNTIME_LIBRARY}" "$<TARGET_FILE_DIR:${target}>"
                    COMMENT "Copying wgpu-native runtime next to ${target}")
            if(UNIX AND NOT APPLE)
                set_property(TARGET ${target} APPEND PROPERTY BUILD_RPATH "$ORIGIN")
            endif()
        endif()
    endmacro()

    cna_webgpu_test(cna_test_webgpu_clear_readback webgpu_clear_readback_test.cpp)
    cna_register_renderer_test(NAME WebGPU_Clear_Readback COMMAND cna_test_webgpu_clear_readback
        TIMEOUT 30 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plans/plan_webgpu.md WEBGPU-123: the shared cross-renderer diagnostic scene built for WEBGPU,
    # exactly as EASYGL's cna_diag_easygl and SOFTWARE's cna_diag_software. Deliberately NOT a CTest
    # (mirrors those two): it writes a 64x64 RGBA8 dump for cna_diag_compare to diff against another
    # backend's dump, which is a two-build comparison, not a single-run pass/fail.
    cna_webgpu_test(cna_diag_webgpu ${CNA_GRAPHICS_EXAMPLES_DIR}/cross_renderer_diagnostic_scene.cpp)

    # Phase 57/63 vertical slice: DrawColoredPrimitives/DrawIndexedColoredPrimitives.
    cna_webgpu_test(cna_test_webgpu_colored3d webgpu_colored3d_test.cpp)
    cna_register_renderer_test(NAME WebGPU_Colored3D COMMAND cna_test_webgpu_colored3d
        TIMEOUT 30 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Phase 63: DrawPrimitivesEx/DrawIndexedPrimitivesEx real GpuDrawParams dispatch (stride 16).
    cna_webgpu_test(cna_test_webgpu_drawprimitivesex webgpu_drawprimitivesex_test.cpp)
    cna_register_renderer_test(NAME WebGPU_DrawPrimitivesEx COMMAND cna_test_webgpu_drawprimitivesex
        TIMEOUT 30 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Phase 58/59/63: textured3d.wgsl / GetOrCreatePipelineTextured3D / stride-20 dispatch.
    cna_webgpu_test(cna_test_webgpu_textured3d webgpu_textured3d_test.cpp)
    cna_register_renderer_test(NAME WebGPU_Textured3D COMMAND cna_test_webgpu_textured3d
        TIMEOUT 30 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Phase 58/59/63: colored_textured3d.wgsl / GetOrCreatePipelineColoredTextured3D / stride-24.
    cna_webgpu_test(cna_test_webgpu_coloredtextured3d webgpu_coloredtextured3d_test.cpp)
    cna_register_renderer_test(NAME WebGPU_ColoredTextured3D COMMAND cna_test_webgpu_coloredtextured3d
        TIMEOUT 30 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # WEBGPU-22: lit_textured3d.wgsl / GetOrCreatePipelineLitTextured3D / stride-32 dispatch,
    # real Blinn-Phong lighting.
    cna_webgpu_test(cna_test_webgpu_littextured3d webgpu_littextured3d_test.cpp)
    cna_register_renderer_test(NAME WebGPU_LitTextured3D COMMAND cna_test_webgpu_littextured3d
        TIMEOUT 30 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # WEBGPU-76: custom-WGSL ShaderEffect on the 3D DrawPrimitives route -- CreateEffectRenderer /
    # WebGPUEffectRenderer / DeclareUniformBlockEXT, and World reaching the WGSL vertex stage.
    cna_webgpu_test(cna_test_webgpu_shadereffect_3d webgpu_shadereffect_3d_test.cpp)
    cna_register_renderer_test(NAME WebGPU_ShaderEffect3D COMMAND cna_test_webgpu_shadereffect_3d
        TIMEOUT 30 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # WEBGPU-85/86/87: multiple simultaneous render targets -- binds 2 then 4 targets and proves a
    # custom-WGSL @location(0..N-1) fragment writes each slot's own content (not slot 0's in all).
    cna_webgpu_test(cna_test_webgpu_mrt webgpu_mrt_test.cpp)
    cna_register_renderer_test(NAME WebGPU_MRT COMMAND cna_test_webgpu_mrt
        TIMEOUT 30 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # WEBGPU-142: custom-WGSL ShaderEffect on the SpriteBatch route -- SetCustomEffect /
    # IssueSpriteWithCustomEffect (a tint effect proves the sprite path runs the custom shader).
    cna_webgpu_test(cna_test_webgpu_spritebatch_shadereffect webgpu_spritebatch_shadereffect_test.cpp)
    cna_register_renderer_test(NAME WebGPU_SpriteBatch_ShaderEffect COMMAND cna_test_webgpu_spritebatch_shadereffect
        TIMEOUT 30 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # WEBGPU-144: GPU-native block-compressed textures -- a DXT1/BC1 block uploads to a BC texture
    # and the GPU decodes it at sample time (no CPU decompression), plus an exact block round-trip.
    cna_webgpu_test(cna_test_webgpu_compressed_texture webgpu_compressed_texture_test.cpp)
    cna_register_renderer_test(NAME WebGPU_CompressedTexture COMMAND cna_test_webgpu_compressed_texture
        TIMEOUT 30 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # WEBGPU-144 Phase 2 (XNB-24): the content loaders reach the GPU-native BC path -- FromStream
    # (DDS) and the .xnb Texture2DReader keep DXT compressed (gated on LoadsCompressedContentNativelyEXT)
    # instead of force-decoding to Color.
    cna_webgpu_test(cna_test_webgpu_compressed_content webgpu_compressed_content_test.cpp)
    cna_register_renderer_test(NAME WebGPU_CompressedContent COMMAND cna_test_webgpu_compressed_content
        TIMEOUT 30 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # WEBGPU-83 (Phase 2): stencil ops bake into a NON-colored3d family's pipeline -- a stamp then
    # a gate on the Textured3D route, where the gate must be rejected outside the stamped region.
    cna_webgpu_test(cna_test_webgpu_stencil_family webgpu_stencil_family_test.cpp)
    cna_register_renderer_test(NAME WebGPU_StencilFamily COMMAND cna_test_webgpu_stencil_family
        TIMEOUT 30 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # WEBGPU-83 (Phase 3): DepthStencilState.TwoSidedStencilMode -- a back-facing triangle must pick
    # up the CounterClockwise* stencil ops (differential vs the two-sided=false control), the
    # cross-renderer parity contract from easygl_depthstencilstate_stencil_twosided_test.cpp.
    cna_webgpu_test(cna_test_webgpu_stencil_twosided webgpu_stencil_twosided_test.cpp)
    cna_register_renderer_test(NAME WebGPU_StencilTwoSided COMMAND cna_test_webgpu_stencil_twosided
        TIMEOUT 30 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # WEBGPU-39: RenderTarget2D honours its DepthFormat exactly instead of always allocating
    # Depth24Stencil8. None -> no depth test (far fragment overwrites), Depth16/Depth24 -> depth
    # test works with NO stencil op named on the pass, Depth24Stencil8 -> depth test + a working
    # stamp/gate stencil round trip on the target's own attachment.
    cna_webgpu_test(cna_test_webgpu_depthformat webgpu_depthformat_test.cpp)
    cna_register_renderer_test(NAME WebGPU_DepthFormat COMMAND cna_test_webgpu_depthformat
        TIMEOUT 30 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # WEBGPU-28: every WGSL source is extracted into webgpu_shaders.hpp and compilable through one
    # entry point (ValidateAllShadersEXT); this drives it on a real device and asserts the whole set
    # -- including shaders a scene never draws (mipBlit, the Pbr/SkinnedPbr variants) -- compiles.
    cna_webgpu_test(cna_test_webgpu_shader_validation webgpu_shader_validation_test.cpp)
    # ValidateAllShadersEXT lives on the concrete WebGPURenderer, so this test includes
    # WebGPURenderer.hpp and needs webgpu.h on its include path (via the WebGPU::WebGPU target).
    target_link_libraries(cna_test_webgpu_shader_validation PRIVATE WebGPU::WebGPU)
    cna_register_renderer_test(NAME WebGPU_ShaderValidation COMMAND cna_test_webgpu_shader_validation
        TIMEOUT 30 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # WEBGPU-12/59: transient per-draw buffer pool + 3-slot sprite vertex ring. Renders a fixed
    # scene for many frames and asserts the pool's create count plateaus while its reuse count keeps
    # climbing (bounded + reused), with zero uncaptured GPU errors. Reads pool stats off the
    # concrete WebGPURenderer, so it also links WebGPU::WebGPU.
    cna_webgpu_test(cna_test_webgpu_buffer_pool_stress webgpu_buffer_pool_stress_test.cpp)
    target_link_libraries(cna_test_webgpu_buffer_pool_stress PRIVATE WebGPU::WebGPU)
    cna_register_renderer_test(NAME WebGPU_BufferPoolStress COMMAND cna_test_webgpu_buffer_pool_stress
        TIMEOUT 60 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 1105 (plans/plan_graphics.md Phase 80): GetOrCreatePipelineLitTextured3DVertexLit /
    # BasicEffect.PreferPerPixelLighting real dispatch.
    cna_webgpu_test(cna_test_webgpu_basiceffect_preferperpixellighting webgpu_basiceffect_preferperpixellighting_test.cpp)
    cna_register_renderer_test(NAME WebGPU_BasicEffect_PreferPerPixelLighting COMMAND cna_test_webgpu_basiceffect_preferperpixellighting
        TIMEOUT 30 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # WEBGPU-23/34/72: alpha_test3d.wgsl / GetOrCreatePipelineAlphaTest3D / AlphaTestEffect
    # per-pixel discard, strides 20/24/32.
    cna_webgpu_test(cna_test_webgpu_alphatest3d webgpu_alphatest3d_test.cpp)
    cna_register_renderer_test(NAME WebGPU_AlphaTest3D COMMAND cna_test_webgpu_alphatest3d
        TIMEOUT 30 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # WEBGPU-24: dual_texture3d.wgsl / GetOrCreatePipelineDualTexture3D / DualTextureEffect,
    # strides 20/24, second texture bind group.
    cna_webgpu_test(cna_test_webgpu_dualtexture3d webgpu_dualtexture3d_test.cpp)
    cna_register_renderer_test(NAME WebGPU_DualTexture3D COMMAND cna_test_webgpu_dualtexture3d
        TIMEOUT 30 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # WEBGPU-25/36/74: env_map3d.wgsl / GetOrCreatePipelineEnvMap3D / EnvironmentMapEffect (stride
    # 32, this renderer's first cube-map shader) -- also exercises WebGPUTextureCubeRenderer
    # (CreateTextureCube()/SetData()), this renderer's first cube-map texture support.
    cna_webgpu_test(cna_test_webgpu_envmap3d webgpu_envmap3d_test.cpp)
    cna_register_renderer_test(NAME WebGPU_EnvMap3D COMMAND cna_test_webgpu_envmap3d
        TIMEOUT 30 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-007: EnvironmentMapEffect must add EmissiveColor UNSCALED (FNA Lighting.fxh),
    # not (Emissive+lightSum)*Diffuse. Discriminating non-white-Diffuse / non-zero-Emissive /
    # non-default-Alpha cases; transfer-function-agnostic via env-map full-replace calibration.
    cna_webgpu_test(cna_test_webgpu_envmap_emissive webgpu_environmentmapeffect_emissive_test.cpp)
    cna_register_renderer_test(NAME WebGPU_EnvMapEmissive COMMAND cna_test_webgpu_envmap_emissive
        TIMEOUT 30 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-100: EnvironmentMapEffect fog must consume the CPU-prepared World*View vector.
    # This public-state discriminator covers World/View translation, projection exclusion,
    # enabled/disabled A->B->A, transform A->B->A/deferred capture, range boundaries, zero range,
    # complete environment/material composition, and alpha preservation on an sRGB-safe RT path.
    cna_webgpu_test(cna_test_webgpu_environmentmapeffect_fog webgpu_environmentmapeffect_fog_test.cpp)
    cna_register_renderer_test(NAME WebGPU_EnvironmentMapEffect_Fog COMMAND cna_test_webgpu_environmentmapeffect_fog
        TIMEOUT 90 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # WEBGPU-145: BasicEffect fog (FNA EffectHelpers.SetFogVector) on the colored3d / textured3d /
    # colored_textured3d / lit_textured3d (per-pixel + per-vertex) WGSL families. Public BasicEffect
    # API only: FogStart==FogEnd collapses every family to FogColor (the drop-the-fog discriminator),
    # half fog lies between base and FogColor, boundaries, and FogColor genuinely drives the blend.
    cna_webgpu_test(cna_test_webgpu_basiceffect_fog webgpu_basiceffect_fog_test.cpp)
    cna_register_renderer_test(NAME WebGPU_BasicEffect_Fog COMMAND cna_test_webgpu_basiceffect_fog
        TIMEOUT 90 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # WEBGPU-146: AlphaTestEffect fog on both alpha_test3d WGSL modules (uncolored stride-20 and
    # vertex-colour stride-24). Fog applies to the pixel that survives the alpha-test discard.
    cna_webgpu_test(cna_test_webgpu_alphatesteffect_fog webgpu_alphatesteffect_fog_test.cpp)
    cna_register_renderer_test(NAME WebGPU_AlphaTestEffect_Fog COMMAND cna_test_webgpu_alphatesteffect_fog
        TIMEOUT 90 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # WEBGPU-147: DualTextureEffect fog on both dual_texture3d WGSL modules (uncolored stride-20 and
    # vertex-colour stride-24). Fog blends the composed sample0*2 x sample1 x tint result.
    cna_webgpu_test(cna_test_webgpu_dualtextureeffect_fog webgpu_dualtextureeffect_fog_test.cpp)
    cna_register_renderer_test(NAME WebGPU_DualTextureEffect_Fog COMMAND cna_test_webgpu_dualtextureeffect_fog
        TIMEOUT 90 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # WEBGPU-148: SkinnedEffect fog across all four skinned WGSL modules ({per-pixel,per-vertex}
    # lighting x {stride-52, stride-56 vertex colour}). Fog uses the SKINNED view-space position.
    cna_webgpu_test(cna_test_webgpu_skinnedeffect_fog webgpu_skinnedeffect_fog_test.cpp)
    cna_register_renderer_test(NAME WebGPU_SkinnedEffect_Fog COMMAND cna_test_webgpu_skinnedeffect_fog
        TIMEOUT 90 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # WEBGPU-70: the reachable draw routes honour vertexStart (DrawPrimitives) / baseVertex+startIndex
    # (DrawIndexedPrimitives) -- two identically-placed red/blue triangles, offset selects which.
    cna_webgpu_test(cna_test_webgpu_vertexstart webgpu_vertexstart_test.cpp)
    cna_register_renderer_test(NAME WebGPU_VertexStart COMMAND cna_test_webgpu_vertexstart
        TIMEOUT 60 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # WEBGPU-108: PresentInterval -> present-mode policy (FIFO/Immediate/Mailbox + unsupported-mode
    # fallback). A pure unit test of the WGPU-free SelectPresentModeChoiceEXT seam -- no GPU/display.
    cna_webgpu_test(cna_test_webgpu_present_mode webgpu_present_mode_test.cpp)
    cna_register_renderer_test(NAME WebGPU_PresentModeMapping COMMAND cna_test_webgpu_present_mode
        TIMEOUT 30 LABELS "WebGPU")

    # WEBGPU-27/38/68: instanced3d.wgsl / GetOrCreatePipelineInstanced3D / DrawInstancedPrimitivesEx
    # -- a genuine second (WGPUVertexStepMode_Instance) vertex buffer binding carrying a
    # per-instance mat4 world transform.
    cna_webgpu_test(cna_test_webgpu_instanced3d webgpu_instanced3d_test.cpp)
    cna_register_renderer_test(NAME WebGPU_Instanced3D COMMAND cna_test_webgpu_instanced3d
        TIMEOUT 30 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-211/213 structural half: honouring both bindings' VertexOffset and every legal
    # InstanceFrequency must cost no extra render pass, queue submit, pipeline variant or GPU
    # buffer, and must not allocate per logical instance. Counts come from the renderer's own EXT
    # counters and from wgpu-native's wgpuGenerateReport() live-object registry.
    cna_webgpu_test(cna_test_webgpu_instanced_offset_frequency_cardinality
                    webgpu_instanced_offset_frequency_cardinality_test.cpp)
    target_link_libraries(cna_test_webgpu_instanced_offset_frequency_cardinality
                          PRIVATE WebGPU::WebGPU)
    cna_register_renderer_test(NAME WebGPU_InstancedOffsetFrequency_Cardinality
        COMMAND cna_test_webgpu_instanced_offset_frequency_cardinality
        TIMEOUT 60 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-212 structural half: honouring BasicEffect.VertexColorEnabled on the instanced
    # route must build no render-pipeline variant per setting, must keep a position-only and a
    # position+colour declaration in separate variants, and must add no render pass or queue
    # submit. Counts come from the renderer's own EXT counters and from wgpu-native's
    # wgpuGenerateReport() live-object registry.
    cna_webgpu_test(cna_test_webgpu_instanced_vertex_color_cardinality
                    webgpu_instanced_vertex_color_cardinality_test.cpp)
    target_link_libraries(cna_test_webgpu_instanced_vertex_color_cardinality
                          PRIVATE WebGPU::WebGPU)
    cna_register_renderer_test(NAME WebGPU_InstancedVertexColor_Cardinality
        COMMAND cna_test_webgpu_instanced_vertex_color_cardinality
        TIMEOUT 60 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plans/plan_cnj.md CNB-58 WebGPU counterpart: pbr3d.wgsl / GetOrCreatePipelinePbr3D / PbrEffect
    # (unskinned only, stride 48), the WebGPU renderer's real glTF 2.0 metallic-roughness BRDF.
    cna_webgpu_test(cna_test_webgpu_pbr3d webgpu_pbr3d_test.cpp)
    cna_register_renderer_test(NAME WebGPU_Pbr3D COMMAND cna_test_webgpu_pbr3d
        TIMEOUT 30 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # GLTF-213: exact mid-grey sRGB transfer and linear base+emissive composition, repeated on
    # both rigid and identity-skinned WebGPU PBR pipelines.
    cna_webgpu_test(cna_test_webgpu_pbr_srgb_transfer
                    ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_pbr_srgb_transfer_test.cpp)
    target_link_libraries(cna_test_webgpu_pbr_srgb_transfer PRIVATE SDL3::SDL3)
    cna_register_renderer_test(NAME WebGPU_Pbr_SrgbTransfer COMMAND cna_test_webgpu_pbr_srgb_transfer
        TIMEOUT 60 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # GLTF-343/344: IOR-derived dielectric F0 and factor-only KHR_materials_specular F0/F90,
    # repeated on both rigid and identity-skinned WebGPU PBR pipelines.
    cna_webgpu_test(cna_test_webgpu_pbr_fresnel_factors
                    ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_pbr_fresnel_factors_test.cpp)
    target_link_libraries(cna_test_webgpu_pbr_fresnel_factors PRIVATE SDL3::SDL3)
    cna_register_renderer_test(NAME WebGPU_Pbr_FresnelFactors COMMAND cna_test_webgpu_pbr_fresnel_factors
        TIMEOUT 60 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plans/plan_cnj.md Phase 14J WebGPU counterpart: skinned3d.wgsl family / GetOrCreatePipelineSkinned3D
    # / SkinnedEffect (strides 52/56, both PreferPerPixelLighting variants, VertexColorEnabled) --
    # closes this renderer's pre-existing "no skinning shader at all" gap.
    cna_webgpu_test(cna_test_webgpu_skinned3d webgpu_skinned3d_test.cpp)
    cna_register_renderer_test(NAME WebGPU_Skinned3D COMMAND cna_test_webgpu_skinned3d
        TIMEOUT 30 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plans/plan_cnj.md Phase 14J WebGPU counterpart: skinned_pbr3d.wgsl / GetOrCreatePipelineSkinnedPbr3D
    # / SkinnedPbrEffect (stride 68, PBR + skinning combo).
    cna_webgpu_test(cna_test_webgpu_skinnedpbr3d webgpu_skinnedpbr3d_test.cpp)
    cna_register_renderer_test(NAME WebGPU_SkinnedPbr3D COMMAND cna_test_webgpu_skinnedpbr3d
        TIMEOUT 30 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-006: SkinnedEffect world-space normal transform under non-identity World (rotation,
    # non-uniform scale, and both) for both PreferPerPixelLighting variants -- the cases every
    # existing skinned test, all of which use World = Identity, structurally cannot detect.
    cna_webgpu_test(cna_test_webgpu_skinnedeffect_world_normal webgpu_skinnedeffect_world_normal_test.cpp)
    cna_register_renderer_test(NAME WebGPU_SkinnedEffect_WorldNormal COMMAND cna_test_webgpu_skinnedeffect_world_normal
        TIMEOUT 60 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-090: all four SkinnedEffect WGSL modules must add the CPU-pre-folded
    # EmissiveShader after directionalLightSum*DiffuseShader, without re-multiplying it by
    # DiffuseColor/Alpha. Covers ambient-only, pure emissive, combined, directional, texture,
    # Alpha, vertex colour, A->B->A, and post-draw effect mutation through an encoding-calibrated
    # RenderTarget2D readback.
    cna_webgpu_test(cna_test_webgpu_skinnedeffect_material webgpu_skinnedeffect_material_test.cpp)
    cna_register_renderer_test(NAME WebGPU_SkinnedEffect_Material COMMAND cna_test_webgpu_skinnedeffect_material
        TIMEOUT 90 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # WEBGPU-41/77/78/79/80/81/82/83: real BlendState/RasterizerState (cull mode, wireframe)/
    # scissor/viewport wiring -- previously ApplyBlendState/ApplyRasterizerState/SetScissorRect/
    # SetViewport had no override at all on this renderer, silently falling back to
    # IGraphicsRenderer's no-op defaults for every 3D draw.
    cna_webgpu_test(cna_test_webgpu_graphicsstate webgpu_graphicsstate_test.cpp)
    cna_register_renderer_test(NAME WebGPU_GraphicsState COMMAND cna_test_webgpu_graphicsstate
        TIMEOUT 30 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # WEBGPU-53/54: WebGPURenderTargetRenderer / CreateRenderTarget2D() / SetRenderTarget2D() --
    # this renderer's first real RenderTarget2D support (Clear-only, real BasicEffect 3D draw, and
    # depth+stencil-tested round trips via GetData(), sampling a RenderTarget2D back through
    # SpriteBatch, and the critical "an intervening RT-targeted Clear() must not leak into the
    # backbuffer's own render pass" architecture check).
    cna_webgpu_test(cna_test_webgpu_rendertarget2d webgpu_rendertarget2d_test.cpp)
    cna_register_renderer_test(NAME WebGPU_RenderTarget2D COMMAND cna_test_webgpu_rendertarget2d
        TIMEOUT 30 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # WEBGPU-58: MSAA infrastructure for both the swapchain backbuffer and RenderTarget2D --
    # backbuffer MultiSampleCount=0 vs. PreferMultiSampling+ApplyChanges() (the real runtime
    # ApplyMultiSampleCount() reconfigure path, mirroring vulkan_msaa_test.cpp's own Task 902
    # verification), ApplyMultiSampleCount()'s clamped-return-value contract (8 -> 0 or 4, never
    # the raw unsupported request), and a RenderTarget2D created after backbuffer MSAA is engaged
    # unconditionally mirroring that same real sample count. KNOWN OPEN ISSUE, intentionally left
    # registered and failing rather than hidden: 3 of 6 checks in this test currently FAIL because
    # genuine multisample-resolved rendering does not yet work end-to-end through the real
    # GraphicsDevice/BasicEffect draw path, despite the infrastructure above being real and
    # individually verified -- see this test's own top-of-file comment and plans/plan_webgpu.md's
    # WEBGPU-58 row for the full investigation.
    cna_webgpu_test(cna_test_webgpu_msaa webgpu_msaa_test.cpp)
    cna_register_renderer_test(NAME WebGPU_Msaa COMMAND cna_test_webgpu_msaa
        TIMEOUT 30 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # WEBGPU-51: WebGPUTextureRenderer::GetData() -- real staged-copy CPU readback for an arbitrary
    # Texture2D renderer (previously the ITextureRenderer::GetData() no-op default, since only the
    # swapchain (ReadBackbuffer, WEBGPU-91) and RenderTarget2D (WEBGPU-53/54) had real readback).
    cna_webgpu_test(cna_test_webgpu_texture2d_getdata webgpu_texture2d_getdata_test.cpp)
    cna_register_renderer_test(NAME WebGPU_Texture2D_GetData COMMAND cna_test_webgpu_texture2d_getdata
        TIMEOUT 30 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # WEBGPU-113: WebGPUTextureCubeRenderer::GetData() -- real per-face staged-copy CPU readback,
    # closing the no-op default WEBGPU-56/74 deliberately left in place. Also exercises a real
    # TextureCube mip level 1 SetData()/GetData() round trip (untested since WEBGPU-56/74).
    cna_webgpu_test(cna_test_webgpu_texturecube_getdata webgpu_texturecube_getdata_test.cpp)
    cna_register_renderer_test(NAME WebGPU_TextureCube_GetData COMMAND cna_test_webgpu_texturecube_getdata
        TIMEOUT 30 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # WEBGPU-57/112: WebGPUTexture3DRenderer / CreateTexture3D() -- this renderer's first Texture3D
    # (volume texture) support (previously IGraphicsRenderer's own nullptr-returning default).
    cna_webgpu_test(cna_test_webgpu_texture3d webgpu_texture3d_test.cpp)
    cna_register_renderer_test(NAME WebGPU_Texture3D COMMAND cna_test_webgpu_texture3d
        TIMEOUT 30 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # WEBGPU-47: a disposed VertexBuffer/IndexBuffer must throw System::ObjectDisposedException from
    # SetData on the WebGPU device (the guard is the XNA GraphicsResource layer's; this proves the
    # WebGPU path leaves it intact and that a live upload on the same device still works).
    cna_webgpu_test(cna_test_webgpu_disposed_guard webgpu_disposed_guard_test.cpp)
    cna_register_renderer_test(NAME WebGPU_DisposedGuard COMMAND cna_test_webgpu_disposed_guard
        TIMEOUT 30 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # WEBGPU-44/99: SetDataWithOptions (None/Discard/NoOverwrite) uploads correctly for every option,
    # and a Discard re-upload between two already-queued draws does not corrupt the first (this
    # renderer snapshots each draw's vertices at queue time, so the immediate-mode Discard hazard
    # cannot occur).
    cna_webgpu_test(cna_test_webgpu_setdataoptions webgpu_setdataoptions_test.cpp)
    cna_register_renderer_test(NAME WebGPU_SetDataOptions COMMAND cna_test_webgpu_setdataoptions
        TIMEOUT 30 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # WEBGPU-63: every SpriteBatch sort mode's ordering (Immediate/Deferred/Texture/FrontToBack/
    # BackToFront) reaches the WebGPU submission path -- two overlapping solid sprites, which colour
    # ends up on top, with each depth-sorted mode set opposite to submission order.
    cna_webgpu_test(cna_test_webgpu_spritebatch_sortmode webgpu_spritebatch_sortmode_test.cpp)
    cna_register_renderer_test(NAME WebGPU_SpriteBatch_SortMode COMMAND cna_test_webgpu_spritebatch_sortmode
        TIMEOUT 30 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # WEBGPU-84: real GPU occlusion queries -- the same quad reads back 0 samples behind an occluder
    # and a full target of samples in front, with two independent per-slot query results.
    cna_webgpu_test(cna_test_webgpu_occlusion_query webgpu_occlusion_query_test.cpp)
    cna_register_renderer_test(NAME WebGPU_OcclusionQuery COMMAND cna_test_webgpu_occlusion_query
        TIMEOUT 30 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # WEBGPU-114: WebGPURenderTargetCubeRenderer / CreateRenderTargetCube() -- this renderer's first
    # real render-into-a-cube-face support (previously IGraphicsRenderer's own nullptr-returning
    # default). Direct face-to-face switching (no intervening backbuffer bind), a real BasicEffect
    # 3D draw into a face, an EnvironmentMapEffect sampling round trip proving IWebGPUCubeSamplable
    # resolves a RenderTargetCube (not just a plain TextureCube), and the "an intervening cube-
    # face-targeted Clear() must not leak into the backbuffer's own render pass" architecture check.
    cna_webgpu_test(cna_test_webgpu_rendertargetcube webgpu_rendertargetcube_test.cpp)
    cna_register_renderer_test(NAME WebGPU_RenderTargetCube COMMAND cna_test_webgpu_rendertargetcube
        TIMEOUT 30 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-096: renderer-neutral singular/plural cube-face binding parity.
    cna_webgpu_test(cna_test_webgpu_rendertargetcube_plural_binding
                    ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertargetcube_plural_binding_test.cpp)
    # REMED-GFX-131: CNA_GFX096_EXPECT_SRGB_ENCODED is gone. It sRGB-encoded this shared fixture's
    # expectations for WebGPU alone, because WebGPU gave its render targets the swapchain's
    # *UnormSrgb format; SurfaceFormat::Color is a plain UNORM byte format, so the define was
    # encoding a defect into the oracle rather than describing a platform difference.
    target_compile_definitions(cna_test_webgpu_rendertargetcube_plural_binding
        PRIVATE CNA_GFX096_CUBE_GETDATA=1)
    cna_register_renderer_test(NAME WebGPU_RenderTargetCube_PluralBinding
        COMMAND cna_test_webgpu_rendertargetcube_plural_binding
        TIMEOUT 60 LABELS "WebGPU"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-019: SpriteBatch drawn INTO an off-screen RenderTarget2D of a different size than
    # the backbuffer must map its destination rectangle in the render target's OWN pixel space, not
    # the backbuffer's (QueueSprite() previously divided clip-space by physicalWidth_/physicalHeight_
    # + ComputeLogicalViewport() unconditionally). Asymmetric 96x72 backbuffer vs 48x32/64x40 render
    # targets, transformMatrix, different-sized targets, transition isolation, orientation control.
    cna_webgpu_test(cna_test_webgpu_spritebatch_rendertarget webgpu_spritebatch_rendertarget_test.cpp)
    cna_register_renderer_test(NAME WebGPU_SpriteBatch_RenderTarget COMMAND cna_test_webgpu_spritebatch_rendertarget
        TIMEOUT 30 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # WEBGPU-52: real, render-pass-based, genuinely-linear-filtered mip generation for a plain
    # Texture2D/TextureCube from level 0 -- previously mipMap=true only pre-allocated empty
    # levels. A hard-edged red/blue stripe proves genuine LINEAR blending at the boundary (not a
    # nearest-neighbor copy), for both Texture2D and one TextureCube face; also documents the
    # deliberate divergence this introduces from FNA/every sibling CNA renderer (which only
    # auto-regenerate mips for a RENDER TARGET being unbound, never a plain texture).
    cna_webgpu_test(cna_test_webgpu_mipgen webgpu_mipgen_test.cpp)
    cna_register_renderer_test(NAME WebGPU_MipGen COMMAND cna_test_webgpu_mipgen
        TIMEOUT 30 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-072: SpriteBatch clip space must be built from the active GraphicsDevice.Viewport
    # (Viewport.Width/Height), not the full target. GFX-019 fixed the RT-size selection but the
    # QueueSprite NDC divide still used the full target dims, while the render pass applied the
    # custom Viewport as the rasterizer viewport -- squishing the sprite. QueueSprite must bake
    # viewport-relative NDC for a custom sub-Viewport. Check D is REMED-GFX-116's addition: the
    # converse case, a full-target batch followed by a sub-Viewport, which GFX-072's capture-only-
    # if-custom rule got wrong. The former "two viewports in one backbuffer frame are per-pass
    # last-wins" note is gone: REMED-GFX-116 made the viewport per-draw state on this renderer, so
    # modules/graphics/examples/spritebatch_viewport_switch_test.cpp is registered here too (below).
    cna_webgpu_test(cna_test_webgpu_spritebatch_custom_viewport
                    ${CNA_GRAPHICS_EXAMPLES_DIR}/spritebatch_custom_viewport_test.cpp)
    cna_register_renderer_test(NAME WebGPU_SpriteBatch_CustomViewport COMMAND cna_test_webgpu_spritebatch_custom_viewport
        TIMEOUT 30 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-116: two SpriteBatch cycles under two different Viewports inside ONE frame. This
    # file was deliberately NOT registered here while WebGPU applied one viewport per render pass.
    # The viewport is per-draw state now, so the per-batch requirement is a real gate on this
    # renderer too.
    cna_webgpu_test(cna_test_webgpu_spritebatch_viewport_switch
                    ${CNA_GRAPHICS_EXAMPLES_DIR}/spritebatch_viewport_switch_test.cpp)
    cna_register_renderer_test(NAME WebGPU_SpriteBatch_ViewportSwitch COMMAND cna_test_webgpu_spritebatch_viewport_switch
        TIMEOUT 30 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-102: every SpriteBatch.Begin() BlendState must be captured by value and select a
    # compatible static sprite pipeline; BlendFactor remains per-batch dynamic state. The public
    # sRGB-aware regression distinguishes presets, custom colour/alpha equations, constants,
    # static/dynamic A->B->A timing, post-Begin mutation, independent channel masks, ordinary 1x
    # backbuffer/RT2D transitions, target/texture identity reuse, 64 dynamic RGBA values with
    # stable cache cardinality, and real 4x MultiSampleMask coverage. It also records the native
    # validation callback count after the existing WebGPU MSAA validation error-scope probe.
    cna_webgpu_test(cna_test_webgpu_spritebatch_blendstate
                    webgpu_spritebatch_blendstate_test.cpp)
    target_link_libraries(cna_test_webgpu_spritebatch_blendstate PRIVATE WebGPU::WebGPU)
    cna_register_renderer_test(NAME WebGPU_SpriteBatch_BlendState
        COMMAND cna_test_webgpu_spritebatch_blendstate
        TIMEOUT 60 LABELS "WebGPU"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-077: BlendState.ColorWriteChannels (RT0) via WGPUColorTargetState.writeMask +
    # BlendState.MultiSampleMask via WGPUMultisampleState.mask (both in Make3DPipelineKey). This
    # remains the generic 3D control; REMED-GFX-102 now independently covers the keyed SpriteBatch
    # path for both fields. MultiSampleMask IS compiled in (wgpu-native mask is real).
    cna_webgpu_test(cna_test_webgpu_colorwritechannels ${CNA_GRAPHICS_EXAMPLES_DIR}/gfx077_colorwritechannels_3d_test.cpp)
    target_compile_definitions(cna_test_webgpu_colorwritechannels PRIVATE GFX077_MULTISAMPLEMASK_SUPPORTED)
    cna_register_renderer_test(NAME WebGPU_ColorWriteChannels COMMAND cna_test_webgpu_colorwritechannels
        TIMEOUT 60 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-127: every public Texture2D::GetData call must return the resource's real content or
    # reject the request deterministically -- never fabricate one. Pre-fix the shared render-target
    # fallback zero-initialized its own scratch buffer, handed it to ITextureRenderer::GetData (whose
    # interface default did nothing) and converted it for the caller regardless, so a renderer with no
    # readback returned a complete, uniformly transparent-black frame that satisfied both "the
    # destination was overwritten" and any transparent-black content expectation.
    cna_webgpu_test(cna_test_webgpu_texture2d_getdata_contract
                    ${CNA_GRAPHICS_EXAMPLES_DIR}/texture2d_getdata_contract_test.cpp)
    cna_register_renderer_test(NAME WebGPU_Texture2D_GetDataContract COMMAND cna_test_webgpu_texture2d_getdata_contract
        TIMEOUT 60 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-149 (and REMED-GFX-128, the same expression): the exact XNA-compatible
    # startIndex/elementCount contract of every public Texture2D::GetData overload. startIndex is a
    # DESTINATION element offset and elementCount is the destination capacity available from it;
    # the whole-level overload applied startIndex to the SOURCE, rejected any non-zero offset on a
    # render target, rejected legal excess capacity, and silently returned a partial frame for an
    # undersized one.
    cna_webgpu_test(cna_test_webgpu_texture2d_getdata_transfer_range
                    ${CNA_GRAPHICS_EXAMPLES_DIR}/texture2d_getdata_transfer_range_test.cpp)
    cna_register_renderer_test(NAME WebGPU_Texture2D_GetDataTransferRange COMMAND cna_test_webgpu_texture2d_getdata_transfer_range
        TIMEOUT 60 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-130: the TextureCube/Texture3D half of REMED-GFX-127's finding. This renderer's own
    # cube/volume readbacks additionally `memset` the caller's destination to zero and returned as if
    # successful whenever the readback buffer could not be mapped -- the same fabrication in renderer
    # clothing, closed here.
    cna_webgpu_test(cna_test_webgpu_cube_volume_getdata_contract
                    ${CNA_GRAPHICS_EXAMPLES_DIR}/texturecube_texture3d_getdata_contract_test.cpp)
    cna_register_renderer_test(NAME WebGPU_CubeVolume_GetDataContract COMMAND cna_test_webgpu_cube_volume_getdata_contract
        TIMEOUT 60 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-134: `RenderTargetCube::GetData` must return the face that was really rendered or
    # reject the request deterministically. REMED-GFX-130 made the reporting honest but left the
    # readback itself implemented on only two renderers, so a rendered cube face had no byte-exact
    # public oracle anywhere else. Drawn geometry (never Clear -- see REMED-GFX-129) paints an
    # asymmetric five-region pattern whose colours rotate per face, so a flip, a rotation, a wrong
    # array layer/subresource, a stale face or an unresolved multisample surface all fail.
    cna_webgpu_test(cna_test_webgpu_rendertargetcube_getdata_contract
                    ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertargetcube_getdata_contract_test.cpp)
    cna_register_renderer_test(NAME WebGPU_RenderTargetCube_GetDataContract COMMAND cna_test_webgpu_rendertargetcube_getdata_contract
        TIMEOUT 60 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-136: IGraphicsRenderer::CreateRenderTargetCube had no `preserveContents` parameter,
    # unlike CreateRenderTarget2D, so a RenderTargetCube's real RenderTargetUsage never reached the
    # renderer and Vulkan/WebGPU discarded a PreserveContents cube face on every bind cycle. Reuses
    # REMED-GFX-134's asymmetric face producer, then rebinds and draws only a small marker: "the
    # marker landed" is what a discarding renderer also achieves, so it can never pass for
    # preservation.
    cna_webgpu_test(cna_test_webgpu_rendertargetcube_usage
                    ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertargetcube_usage_test.cpp)
    cna_register_renderer_test(NAME WebGPU_RenderTargetCube_Usage COMMAND cna_test_webgpu_rendertargetcube_usage
        TIMEOUT 60 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-141: a MULTISAMPLED RenderTargetCube shared ONE multisample colour attachment
    # across all six faces on EasyGL, Vulkan and SdlGpu, so a PreserveContents face reloaded whichever
    # face was rendered last instead of its own samples. Renders face A, then face B, then rebinds A for
    # a small partial update -- with no readback, Present or flush in between -- and reads the whole of
    # A: the resolved single-sample layer still held the correct A, which is why a full redraw and an
    # immediate read both passed before this.
    cna_webgpu_test(cna_test_webgpu_rendertargetcube_msaa_face
                    ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertargetcube_msaa_face_test.cpp)
    cna_register_renderer_test(NAME WebGPU_RenderTargetCube_MsaaFace COMMAND cna_test_webgpu_rendertargetcube_msaa_face
        TIMEOUT 60 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-142: RenderTargetUsage's DEPTH and STENCIL half. FNA3D's own header documents
    # `preserveTargetContents` as storing the "color/depth/stencil" contents, and FNA's DiscardContents
    # bind clears all three (Target|DepthBuffer|Stencil, MaxDepth, 0) -- so the enum governs depth and
    # stencil, not colour alone. Renders an occluder, unbinds, rebinds WITHOUT clearing and draws behind
    # it: three bands separate preserved depth from cleared depth, lost colour and a second pass that
    # never drew. A parallel stencil stamp/gate sequence does the same for stencil.
    cna_webgpu_test(cna_test_webgpu_rendertarget_depthstencil_usage
                    ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_depthstencil_usage_test.cpp)
    cna_register_renderer_test(NAME WebGPU_RenderTarget_DepthStencilUsage COMMAND cna_test_webgpu_rendertarget_depthstencil_usage
        TIMEOUT 60 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-140: every public render-target bind/unbind cycle must be its own logical pass.
    # `VulkanRenderer::RecordCommandBuffer` collected ONE render pass per unique render-target
    # source per flush and replayed every queued batch for it inside that pass, so two bind cycles of
    # one target within a single flush window shared one load action. The decisive checks all use
    # DiscardContents -- collapsing is invisible on a preserving target, which is why REMED-GFX-136
    # needed an artificial readback barrier to see it -- and nothing between two cycles ever forces a
    # flush.
    cna_webgpu_test(cna_test_webgpu_rendertarget_pass_boundary
                    ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_pass_boundary_test.cpp)
    cna_register_renderer_test(NAME WebGPU_RenderTarget_PassBoundary COMMAND cna_test_webgpu_rendertarget_pass_boundary
        TIMEOUT 90 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-129: cross-renderer control for Vulkan's ordered-Clear correction.
    cna_webgpu_test(cna_test_webgpu_ordered_clear
                    ${CNA_GRAPHICS_EXAMPLES_DIR}/graphicsdevice_ordered_clear_test.cpp)
    cna_register_renderer_test(NAME WebGPU_GraphicsDevice_OrderedClear COMMAND cna_test_webgpu_ordered_clear
        TIMEOUT 120 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-143: backbuffer work and render-target work must replay in ONE ordered stream.
    # REMED-GFX-140 (Vulkan) and REMED-GFX-145 (SdlGpu) gave every render-target bind cycle its own
    # native pass in public order, but both kept the BACKBUFFER as one trailing pass, so every
    # backbuffer draw of a frame was replayed after every target pass regardless of when it was
    # issued. A frame that samples a target onto the backbuffer and then renders into that target
    # again therefore saw the FINAL content. Every check queues its whole public sequence with no
    # Present, GetData, flush or extra frame between the cycles, and only then reads once.
    cna_webgpu_test(cna_test_webgpu_backbuffer_pass_order
                    ${CNA_GRAPHICS_EXAMPLES_DIR}/backbuffer_pass_order_test.cpp)
    cna_register_renderer_test(NAME WebGPU_Backbuffer_PassOrder COMMAND cna_test_webgpu_backbuffer_pass_order
        TIMEOUT 120 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-135: the WRITE half of the same finding. `TextureCube::SetData`/`Texture3D::SetData`
    # kept the pre-REMED-GFX-127 shape -- a `void` renderer method behind `if (renderer_)` -- so an
    # upload that stored nothing, or only part of the requested region, still returned normally.
    cna_webgpu_test(cna_test_webgpu_cube_volume_setdata_contract
                    ${CNA_GRAPHICS_EXAMPLES_DIR}/texturecube_texture3d_setdata_contract_test.cpp)
    cna_register_renderer_test(NAME WebGPU_CubeVolume_SetDataContract COMMAND cna_test_webgpu_cube_volume_setdata_contract
        TIMEOUT 60 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-116: every deferred draw must execute under the GraphicsDevice.Viewport active at
    # its own public call. All three of this renderer's render-pass recorders applied the viewport
    # once, at the top of the pass, from the live viewport*_ members -- so two viewports in one bind
    # cycle collapsed last-wins and an ordinary restore before the flush promoted every queued draw
    # to the whole target. Every sequence here is queued whole, with no GetData/Present/flush/target
    # switch in between, and observed exactly once at its end.
    cna_webgpu_test(cna_test_webgpu_deferred_viewport
                    ${CNA_GRAPHICS_EXAMPLES_DIR}/deferred_viewport_capture_test.cpp)
    cna_register_renderer_test(NAME WebGPU_Deferred_Viewport COMMAND cna_test_webgpu_deferred_viewport
        TIMEOUT 120 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-116's structural half: the captured viewport must be dynamic pass state. It may not
    # create a pipeline variant, split a render pass, add a submit, or issue one native
    # setViewport per draw when consecutive draws share a viewport.
    cna_webgpu_test(cna_test_webgpu_viewport_cardinality
                    webgpu_viewport_cardinality_test.cpp)
    target_link_libraries(cna_test_webgpu_viewport_cardinality PRIVATE WebGPU::WebGPU)
    cna_register_renderer_test(NAME WebGPU_Viewport_Cardinality COMMAND cna_test_webgpu_viewport_cardinality
        TIMEOUT 90 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-146: the scissor counterpart of REMED-GFX-116. All three render-pass recorders
    # issued one wgpuRenderPassEncoderSetScissorRect at the top of the pass from the live
    # scissorEnabled_/scissorX_/scissorY_/scissorW_/scissorH_ members, so several rectangles in one
    # bind cycle collapsed last-wins and the full-target rectangle SetRenderTarget installs on every
    # bind unclipped every already-queued draw. Every sequence here is queued whole, with no
    # GetData/Present/flush/target switch in between, and observed exactly once at its end.
    cna_webgpu_test(cna_test_webgpu_deferred_scissor
                    ${CNA_GRAPHICS_EXAMPLES_DIR}/deferred_scissor_capture_test.cpp)
    cna_register_renderer_test(NAME WebGPU_Deferred_Scissor COMMAND cna_test_webgpu_deferred_scissor
        TIMEOUT 120 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-146's structural half: the captured scissor state must be dynamic pass state. It may
    # not create a pipeline variant (for the rectangle OR for ScissorTestEnable), split a render
    # pass, add a submit, or issue one native setScissorRect per draw when consecutive draws share a
    # rectangle. It also re-reads the setViewport counter, so REMED-GFX-116 stays measurably intact.
    cna_webgpu_test(cna_test_webgpu_scissor_cardinality
                    webgpu_scissor_cardinality_test.cpp)
    target_link_libraries(cna_test_webgpu_scissor_cardinality PRIVATE WebGPU::WebGPU)
    cna_register_renderer_test(NAME WebGPU_Scissor_Cardinality COMMAND cna_test_webgpu_scissor_cardinality
        TIMEOUT 90 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-159's structural half: replaying the two draw families in public order must not cost
    # a render-pass split, a submit or wait per family transition, or a pipeline variant keyed on a
    # draw's position. The pixel oracle in modules/graphics/examples/spritebatch_3d_order_test.cpp cannot see any of
    # those -- each renders the correct picture -- so the cost contract is measured separately, on
    # both the backbuffer and a render target, against this renderer's own native counters.
    cna_webgpu_test(cna_test_webgpu_draw_order_cardinality
                    webgpu_draw_order_cardinality_test.cpp)
    target_link_libraries(cna_test_webgpu_draw_order_cardinality PRIVATE WebGPU::WebGPU)
    cna_register_renderer_test(NAME WebGPU_DrawOrder_Cardinality COMMAND cna_test_webgpu_draw_order_cardinality
        TIMEOUT 90 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-146 false-positive audit: REMED-GFX-081's shared SpriteBatch.Begin(rasterizerState)
    # fixture was registered for SdlGpu and Vulkan but never for WebGPU, on no recorded grounds, so
    # the one renderer whose scissor state was resolved late was the one renderer this never ran on.
    # Registered here; it renders into a RenderTarget2D, which on this renderer is its own flush, so
    # it complements rather than duplicates the deferral fixture above.
    cna_webgpu_test(cna_test_webgpu_spritebatch_begin_rasterizerstate_scissor
                    ${CNA_GRAPHICS_EXAMPLES_DIR}/spritebatch_begin_rasterizerstate_scissor_test.cpp)
    cna_register_renderer_test(NAME WebGPU_SpriteBatch_Begin_RasterizerState_Scissor
        COMMAND cna_test_webgpu_spritebatch_begin_rasterizerstate_scissor
        TIMEOUT 90 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-131: SurfaceFormat::Color is a plain 8-bit UNORM byte format, so a mid-tone channel
    # must survive Clear/draw/sample/readback unchanged. Every pre-existing WebGPU colour fixture
    # used an RGB palette drawn only from {0, 255} -- both exact fixed points of sRGB encoding --
    # so the sRGB render-target format was invisible to all of them.
    cna_webgpu_test(cna_test_webgpu_colorspace_midtone
                    ${CNA_GRAPHICS_EXAMPLES_DIR}/colorspace_midtone_contract_test.cpp)
    # For GetUncapturedErrorCountEXT(): the fix changes a resource format and introduces a
    # view-format reinterpretation, so the suite asserts wgpu-native reported no validation error.
    target_link_libraries(cna_test_webgpu_colorspace_midtone PRIVATE WebGPU::WebGPU)
    cna_register_renderer_test(NAME WebGPU_ColorSpace_MidTone COMMAND cna_test_webgpu_colorspace_midtone
        TIMEOUT 120 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_webgpu_test(cna_test_webgpu_additive_blend_contract
                    ${CNA_GRAPHICS_EXAMPLES_DIR}/additive_blend_contract_test.cpp)
    cna_register_renderer_test(NAME WebGPU_AdditiveBlendContract
        COMMAND cna_test_webgpu_additive_blend_contract
        TIMEOUT 120 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-147: a RenderTarget2D used as a texture must sample in the same logical orientation
    # as an ordinary Texture2D holding identical bytes. Registered here as a cross-renderer control:
    # the defect was EasyGL-local (an OpenGL framebuffer's origin is bottom-left, so a target's
    # colour texture stores the image bottom-up and sampling did not compensate even though GetData
    # already did), and these runs are what establish that render-target and ordinary-texture
    # sampling already agree everywhere else rather than being made to agree by the fix.
    cna_webgpu_test(cna_test_webgpu_rt_sampling_orientation
                    ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_sampling_orientation_test.cpp)
    cna_register_renderer_test(NAME WebGPU_RenderTarget_SamplingOrientation COMMAND cna_test_webgpu_rt_sampling_orientation
        TIMEOUT 120 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-153 cross-renderer source-selection and destination-orientation control.
    cna_webgpu_test(cna_test_webgpu_gfx153_source_rectangle
                    ${CNA_GRAPHICS_EXAMPLES_DIR}/source_rectangle_orientation_test.cpp)
    cna_register_renderer_test(NAME WebGPU_GFX153_SourceRectangleOrientation
        COMMAND cna_test_webgpu_gfx153_source_rectangle
        TIMEOUT 180 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")



    # REMED-GFX-169: every stock 3D effect must sample its textures through the public
    # GraphicsDevice.SamplerStates[slot], exactly like SpriteBatch already does. Vulkan's
    # ApplySamplerState is correct and SpriteBatch reads slotSamplers_[0], but six stock 3D
    # descriptor builders take NO sampler parameter at all, so all 15 of their combined-image-
    # sampler bindings are hardcoded to defaultSampler_ (LINEAR + CLAMP_TO_EDGE) and the sampler is
    # absent from their cache keys too. Vulkan is where the defect lives; the other renderers run the
    # same public fixture as controls.
    cna_webgpu_test(cna_test_webgpu_stock_effect_sampler
        ${CNA_GRAPHICS_EXAMPLES_DIR}/stock_effect_sampler_contract_test.cpp)
    cna_register_renderer_test(NAME WebGPU_StockEffectSamplerContract COMMAND cna_test_webgpu_stock_effect_sampler
        TIMEOUT 300 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

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
    cna_webgpu_test(cna_test_webgpu_texture_filter_ordinal
        ${CNA_GRAPHICS_EXAMPLES_DIR}/texture_filter_ordinal_contract_test.cpp)
    cna_register_renderer_test(NAME WebGPU_TextureFilterOrdinalContract COMMAND cna_test_webgpu_texture_filter_ordinal
        TIMEOUT 300 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-173 cross-renderer control: EnvironmentMapEffect samples TWO independent
    # resources -- the ordinary 2D texture through GraphicsDevice.SamplerStates[0] and the
    # reflection cube through SamplerStates[1]. SDL_GPU bound a literal LinearClamp for the cube
    # and captured only slot 0, so slot 1 could not reach replay at all. This fixture makes the
    # two slots independently observable (EnvironmentMapAmount 1 leaves the cube the only
    # contributor, 0 leaves the 2D texture the only one) and measures what every other renderer
    # does with the same public state.
    # REMED-GFX-172 fixed WebGPU's half of this: its EnvironmentMap3D bind group had ONE sampler
    # entry for two texture views, so SamplerStates[1] was inexpressible for the cube and slot 1
    # silently inherited slot 0's sampler. Both the WGSL and the layout now carry a second sampler.
    cna_webgpu_test(cna_test_webgpu_envmap_cube_sampler
        ${CNA_GRAPHICS_EXAMPLES_DIR}/envmap_cube_sampler_contract_test.cpp)
    cna_register_renderer_test(NAME WebGPU_EnvMapCubeSamplerContract COMMAND cna_test_webgpu_envmap_cube_sampler
        TIMEOUT 600 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-172: DualTextureEffect's own half of the same defect. FNA's DualTextureEffect.fx
    # declares DECLARE_TEXTURE(Texture, 0) and DECLARE_TEXTURE(Texture2, 1), so the two layers have
    # INDEPENDENT public sampler slots. WebGPU's dual_texture3d.wgsl declared one `texSampler` and
    # sampled both layers through it, its bind-group layout had one sampler entry for two views, and
    # DualTextureDrawCommand carried one sampler-state group filled from slotSamplers_[0]. This
    # fixture observes BOTH layers in the same image -- slot 0's texture varies along X only, slot
    # 1's along Y only, and the shader's product is separable -- so "changing one sampler affects
    # only its own resource" is measured rather than inferred. The other renderers run it as controls.
    cna_webgpu_test(cna_test_webgpu_dualtexture_slot_sampler
        ${CNA_GRAPHICS_EXAMPLES_DIR}/dualtexture_slot_sampler_contract_test.cpp)
    cna_register_renderer_test(NAME WebGPU_DualTextureSlotSamplerContract COMMAND cna_test_webgpu_dualtexture_slot_sampler
        TIMEOUT 600 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-175 cross-renderer control: the MIPMAP component of a TextureFilter ordinal.
    # EasyGL mapped ordinals 0 and 1 onto a GL filter with no mipmap term and Software had no
    # mip pipeline at all; this fixture measures what every other renderer does with a chain
    # whose levels name themselves, so a divergence is classified rather than assumed.
    cna_webgpu_test(cna_test_webgpu_texture_filter_mip_contract
        ${CNA_GRAPHICS_EXAMPLES_DIR}/texture_filter_mip_contract_test.cpp)
    cna_register_renderer_test(NAME WebGPU_TextureFilterMipContract COMMAND cna_test_webgpu_texture_filter_mip_contract
        TIMEOUT 300 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-177 cross-renderer control: descriptor/binding bookkeeping must be a function of what
    # is LIVE, never of what has ever existed. D3D12 owned four fixed-capacity heaps with a monotonic
    # bump cursor and no free list, so the 65th sampleable resource EVER CREATED threw even when six
    # were alive. Every draw here is checked against a self-identifying oracle that decodes back to an
    # integer, so a recycled or grown pool that aliases one resource onto another's binding names the
    # wrong resource rather than merely producing an odd colour.
    cna_webgpu_test(cna_test_webgpu_descriptor_capacity
        ${CNA_GRAPHICS_EXAMPLES_DIR}/descriptor_capacity_contract_test.cpp)
    cna_register_renderer_test(NAME WebGPU_DescriptorCapacityContract COMMAND cna_test_webgpu_descriptor_capacity
        TIMEOUT 900 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_webgpu_test(cna_test_webgpu_point_sampling
        ${CNA_GRAPHICS_EXAMPLES_DIR}/point_sampling_contract_test.cpp)
    cna_register_renderer_test(NAME WebGPU_PointSamplingContract COMMAND cna_test_webgpu_point_sampling
        TIMEOUT 300 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-151 cross-renderer control: the canonical XNA render-to-texture sequence -- render
    # into a target, unbind it, sample it -- must complete in ONE public frame with no intervening
    # GetData, Present, extra frame, manual flush or wait. The defect was Vulkan-local (its deferred
    # recorder's readback flush filtered the frame's segment list down to the target being READ, so a
    # producer's pass was never recorded before the consumer that sampled it); these runs are what
    # establish that every other renderer already honoured the contract rather than being made to.
    cna_webgpu_test(cna_test_webgpu_rt_producer_consumer
                    ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_producer_consumer_test.cpp)
    cna_register_renderer_test(NAME WebGPU_RenderTarget_ProducerConsumer COMMAND cna_test_webgpu_rt_producer_consumer
        TIMEOUT 120 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-152 cross-renderer control: a RenderTarget2D handed to a stock or custom 3D effect as
    # its texture must be sampled, not reinterpreted. The defect was SDL_GPU-local and fatal (its
    # stock-effect paths static_cast an ITextureRenderer* to the unrelated sibling
    # SdlGpuTextureRenderer, fabricating an SDL_GPUTexture* out of a render target's own fields); this
    # run is what establishes that WEBGPU already honoured the contract rather than being made to.
    cna_webgpu_test(cna_test_webgpu_rt_effect_source
                    ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_effect_source_test.cpp)
    cna_register_renderer_test(NAME WebGPU_RenderTarget_EffectSource COMMAND cna_test_webgpu_rt_effect_source
        TIMEOUT 300 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-167 deferred-source lifetime: a resource sampled by a draw that has only been
    # QUEUED must stay bindable until that draw actually renders, and destroying the public wrapper
    # first must never terminate the process. The defect was WebGPU-local -- every deferred command
    # stored a raw pointer to the resource's RENDERER OBJECT and called a VIRTUAL method on it at
    # replay, so a RenderTarget2D produced, sampled onto the BACKBUFFER and dropped inside one
    # Draw() was a heap-use-after-free at Present(). These runs establish which renderers already
    # honoured the contract rather than being made to; each leg runs in its own process so a
    # SIGSEGV is an attributable result instead of a lost shard.
    cna_webgpu_test(cna_test_webgpu_deferred_source_lifetime
                    ${CNA_GRAPHICS_EXAMPLES_DIR}/deferred_source_lifetime_test.cpp)
    cna_register_renderer_test(NAME WebGPU_DeferredSourceLifetime COMMAND cna_test_webgpu_deferred_source_lifetime
        TIMEOUT 300 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-168 cross-renderer control: destroying a RenderTarget2D that is STILL the bound
    # render target must never make the next SetRenderTarget transition unsafe, and must leave the
    # next target and the backbuffer exactly correct. The defect was EasyGL-local -- it remembered
    # the bound destination as a raw IRenderTargetRenderer* and the next transition called
    # UnbindAsRenderTarget() on it, so a scoped target leaving scope while bound made that
    # transition a virtual call through freed storage. This run is what establishes that WEBGPU
    # already honoured the contract rather than being made to; each leg runs in its own process so
    # a SIGSEGV is an attributable result instead of a lost shard.
    cna_webgpu_test(cna_test_webgpu_bound_target_lifetime
        ${CNA_GRAPHICS_EXAMPLES_DIR}/bound_target_lifetime_test.cpp)
    cna_register_renderer_test(NAME WebGPU_BoundTargetLifetime COMMAND cna_test_webgpu_bound_target_lifetime
        TIMEOUT 600 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-180: the public render-target -> Present lifecycle contract. `SdlGpu_RenderState`
    # aborted with `Cannot present while render targets are bound`, which is the SECOND event in a
    # two-stage chain: an over-range DrawPrimitives (two triangles from a three-vertex buffer) was
    # correctly rejected by REMED-GFX-113's range guard, that rejection unwound past the fixture's own
    # SetRenderTarget(nullptr), and EndDraw then met a still-bound target. FNA's contract is REJECT,
    # never auto-unbind, so both guards are right and the fixture was wrong. Every leg runs in its own
    # process; leg C2 reproduces the abort end to end and its correct outcome is SIGABRT.
    cna_webgpu_test(cna_test_webgpu_present_lifecycle
        ${CNA_GRAPHICS_EXAMPLES_DIR}/present_lifecycle_contract_test.cpp)
    cna_register_renderer_test(NAME WebGPU_PresentLifecycle COMMAND cna_test_webgpu_present_lifecycle
        TIMEOUT 900 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-163 cross-renderer control: the same public depth-backed MULTISAMPLED
    # RenderTarget2D construction and lifecycle that aborted the process on Bgfx. Only Bgfx
    # production changed; this renderer is registered to show the construction was always legal
    # and to keep it that way.
    cna_webgpu_test(cna_test_webgpu_msaa_depth_contract
        ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_msaa_depth_contract_test.cpp)
    cna_register_renderer_test(NAME WebGPU_MsaaDepthContract COMMAND cna_test_webgpu_msaa_depth_contract
        TIMEOUT 900 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-154 cross-renderer control: the same PUBLIC first-readback contract on this
    # renderer. The defect was Bgfx-only and only Bgfx production changed; running the identical
    # fixture here is what makes that claim falsifiable rather than asserted.
    cna_webgpu_test(cna_test_webgpu_msaa_first_readback
        ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_msaa_first_readback_test.cpp)
    cna_register_renderer_test(NAME WebGPU_MsaaFirstReadback COMMAND cna_test_webgpu_msaa_first_readback
        TIMEOUT 900 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-186 cross-renderer control: the same PUBLIC generated-mip readback contract on this
    # renderer. The SIGSEGV was SDL_GPU-only and only SDL_GPU production changed; running the
    # identical fixture here is what makes that claim falsifiable rather than asserted.
    cna_webgpu_test(cna_test_webgpu_msaa_mip_readback
        ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_msaa_mip_readback_test.cpp)
    cna_register_renderer_test(NAME WebGPU_MsaaMipReadback COMMAND cna_test_webgpu_msaa_mip_readback
        TIMEOUT 1200 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
    # REMED-GFX-189 cross-renderer control: the same PUBLIC invalid-mip-level contract on this
    # renderer. The fabrication was Vulkan-only and only Vulkan production changed; running the
    # identical fixture here is what makes that claim falsifiable rather than asserted.
    cna_webgpu_test(cna_test_webgpu_invalid_mip_level
        ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_invalid_mip_level_test.cpp)
    cna_register_renderer_test(NAME WebGPU_InvalidMipLevel COMMAND cna_test_webgpu_invalid_mip_level
        TIMEOUT 1200 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
    # REMED-GFX-155 cross-renderer control: a render target produced and unbound earlier in a public
    # frame must be visible to a consumer that draws on the BACKBUFFER later in that same frame. The
    # defect was bgfx-local (it radix-sorts a frame's draws by their view's sort position, which
    # defaults to the numeric view id, and its backbuffer owns the lowest id -- so a backbuffer
    # consumer executed before its own producer); these runs are what establish that every other
    # renderer already honoured the contract rather than being made to.
    cna_webgpu_test(cna_test_webgpu_rt_backbuffer_consumer
                    ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_backbuffer_consumer_test.cpp)
    cna_register_renderer_test(NAME WebGPU_RenderTarget_BackbufferConsumer COMMAND cna_test_webgpu_rt_backbuffer_consumer
        TIMEOUT 180 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-158's control: a RenderTarget2D constructed during a public frame must be usable in
    # that same frame -- bound, cleared and/or drawn into, unbound and observed -- with no warm-up
    # frame, Present, GetData-before-first-render, dummy draw, empty bind cycle, manual frame advance
    # or wait. The defect was bgfx-local (`bgfx::reset()` discards every view's framebuffer binding,
    # including the bound target's), so this run is what establishes that WEBGPU already honoured the
    # contract rather than being made to.
    cna_webgpu_test(cna_test_webgpu_rt_first_use
                    ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_first_use_test.cpp)
    cna_register_renderer_test(NAME WebGPU_RenderTarget_FirstUse COMMAND cna_test_webgpu_rt_first_use
        TIMEOUT 180 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-157: a stock 3D draw issued AFTER a SpriteBatch inside ONE render-target bind cycle
    # must execute after it rather than be dropped. REMED-GFX-155's leg I0 measured the region it
    # drew into still holding the cycle's clear colour on BGFX, VULKAN, SOFTWARE and EASYGL, with
    # WEBGPU clean, using ordinary Texture2D sources on both destinations. The oracle is a staircase
    # -- step i covers stripes [0, N-i), so one readback proves every draw happened AND that they
    # happened in the issued order.
    cna_webgpu_test(cna_test_webgpu_spritebatch_3d_order
                    ${CNA_GRAPHICS_EXAMPLES_DIR}/spritebatch_3d_order_test.cpp)
    cna_register_renderer_test(NAME WebGPU_SpriteBatch3DOrder COMMAND cna_test_webgpu_spritebatch_3d_order
        TIMEOUT 300 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-160: the XNA/FNA front-face winding contract. FNA's SpriteBatch emits
    # CLOCKWISE-as-displayed triangles and they must survive RasterizerState.CullCounterClockwise --
    # the state SpriteBatch.Begin itself defaults to -- so clockwise-as-displayed is XNA's FRONT face
    # and each enum names the face it CULLS. Two triangles in disjoint quadrants, one wound each way,
    # make every cull mode's expected answer complementary in one readback.
    cna_webgpu_test(cna_test_webgpu_frontface_winding
                    ${CNA_GRAPHICS_EXAMPLES_DIR}/frontface_winding_test.cpp)
    cna_register_renderer_test(NAME WebGPU_FrontFaceWinding COMMAND cna_test_webgpu_frontface_winding
        TIMEOUT 300 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-183: dedicated TriangleStrip parity/culling matrix. Covers native and DrawUser,
    # indexed/non-indexed, Uint16/Uint32, odd/even primitives, both source windings, all cull modes,
    # nonzero ranges, list interleaving, deferred state snapshots, disposal, and validation.
    cna_webgpu_test(cna_test_webgpu_triangle_strip_winding
                    ${CNA_GRAPHICS_EXAMPLES_DIR}/triangle_strip_winding_test.cpp)
    target_link_libraries(cna_test_webgpu_triangle_strip_winding PRIVATE WebGPU::WebGPU)
    cna_register_renderer_test(NAME WebGPU_TriangleStripWinding
        COMMAND cna_test_webgpu_triangle_strip_winding
        TIMEOUT 300 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-165: a rectangle-less GetBackBufferData must address the whole logical backbuffer
    # (PresentationParameters W x H) from the first observable frame, with no SetRenderTarget round
    # trip, no extra Present and no viewport reset -- sized from the backbuffer description, never from
    # the renderer's live viewport. The defect was WebGPU/SDL_GPU-local (the rectangle-less path asked
    # renderer_->GetViewportSize, which under FixedHeightDynamicWidth is aspect-scaled and, before the
    # window is realised, stale). Each leg runs in its own process so its first frame is genuinely first.
    cna_webgpu_test(cna_test_webgpu_backbuffer_readback_dimension
                    ${CNA_GRAPHICS_EXAMPLES_DIR}/backbuffer_readback_dimension_test.cpp)
    cna_register_renderer_test(NAME WebGPU_BackbufferReadbackDimension COMMAND cna_test_webgpu_backbuffer_readback_dimension
        TIMEOUT 300 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-161: the FIRST GetBackBufferData of a process must fully and synchronously write the
    # requested destination range. Pre-fix, WebGPU's first backbuffer readback returned successfully
    # while leaving part of the caller's poison prefill in place; every later read was correct. Each
    # leg runs in its own process (fork+exec) so its first read is genuinely first.
    cna_webgpu_test(cna_test_webgpu_backbuffer_first_read
                    ${CNA_GRAPHICS_EXAMPLES_DIR}/backbuffer_first_read_test.cpp)
    cna_register_renderer_test(NAME WebGPU_BackbufferFirstRead COMMAND cna_test_webgpu_backbuffer_first_read
        TIMEOUT 300 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-162 cross-renderer control: a rasterizing renderer's GetBackBufferData SUCCEEDS and
    # fully writes the requested range (GFX-161/165 preserved), so the Headless rejection is specific.
    cna_webgpu_test(cna_test_webgpu_backbuffer_reject
                    ${CNA_GRAPHICS_EXAMPLES_DIR}/backbuffer_headless_reject_test.cpp)
    cna_register_renderer_test(NAME WebGPU_BackbufferReject COMMAND cna_test_webgpu_backbuffer_reject
        TIMEOUT 300 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
endif()

# plans/plan_webgpu.md WEBGPU-121: browser shader coverage. The native effect suites above prove
# every WGSL pair compiles under wgpu-native; this block builds a curated set of the same Game
# programs for the browser so their WGSL is compiled by emdawnwebgpu's Tint path via
# createShaderModule too. The set is the shaders with features BasicEffect (already browser-verified
# by cna_house3d_demo) does not exercise: PbrEffect's metallic-roughness BRDF, EnvironmentMapEffect's
# cube-map sampling, and SkinnedEffect's bone palette. dual-texture and alpha-test are narrow
# variants of the already-verified textured3d path. Each is a self-contained procedural Game (no
# preloaded assets); drive them with scripts/run-webgpu-browser-test.sh (CNA_WEBGPU_DEMO=<target>).
if(EMSCRIPTEN AND CNA_BUILD_EXAMPLES AND CNA_GRAPHICS_RENDERER STREQUAL "WEBGPU")
    macro(cna_webgpu_effect_page target src)
        add_executable(${target} ${src})
        set_target_properties(${target} PROPERTIES SUFFIX ".html")
        target_link_libraries(${target} PRIVATE
            CNA ${RENDERER_TARGET} SDL3::SDL3-static SHARP_RUNTIME)
        target_link_options(${target} PRIVATE
            -sALLOW_MEMORY_GROWTH=1
            -sSTACK_SIZE=1048576
            -sINITIAL_MEMORY=67108864
            -sFORCE_FILESYSTEM=1)
    endmacro()

    cna_webgpu_effect_page(cna_webgpu_pbr3d_page       webgpu_pbr3d_test.cpp)
    cna_webgpu_effect_page(cna_webgpu_envmap3d_page    webgpu_envmap3d_test.cpp)
    cna_webgpu_effect_page(cna_webgpu_skinned3d_page   webgpu_skinned3d_test.cpp)
    # The last two shader pairs: narrow variants of the already-verified textured3d path, built to
    # close the "all 9" WEBGPU-121 question with real in-browser evidence rather than by inference.
    cna_webgpu_effect_page(cna_webgpu_dualtexture3d_page webgpu_dualtexture3d_test.cpp)
    cna_webgpu_effect_page(cna_webgpu_alphatest3d_page   webgpu_alphatest3d_test.cpp)

    # plans/plan_webgpu.md WEBGPU-123: the shared, renderer-agnostic cross-renderer diagnostic scene
    # (one unlit vertex-colour triangle -> 64x64 RGBA8 dump), built for WEBGPU exactly as SOFTWARE
    # and EASYGL build it, so the WebGPU output can be pixel-compared against another backend's dump
    # with cna_diag_compare. Under Emscripten it writes into MEMFS; scripts/run-webgpu-parity-test.sh
    # extracts the dump and runs the comparison.
    cna_webgpu_effect_page(cna_diag_webgpu ${CNA_GRAPHICS_EXAMPLES_DIR}/cross_renderer_diagnostic_scene.cpp)
    # The scene writes its dump into MEMFS; export FS so the driver page (run-webgpu-parity-test.sh)
    # can read the bytes back out of the browser without modifying the renderer-agnostic scene.
    target_link_options(cna_diag_webgpu PRIVATE "SHELL:-sEXPORTED_RUNTIME_METHODS=FS,callMain")
endif()

# Module-local tests get the same headless-safe skip convention the root one-shot
# gives root-registered tests (see cmake/TestHelpers.cmake).
cna_apply_skip_convention()
