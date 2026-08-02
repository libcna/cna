if(CNA_BUILD_TESTS AND CNA_BUILD_EXAMPLES
   AND CNA_GRAPHICS_BACKEND STREQUAL "SOKOL")
    enable_testing()

    # Mirrors the SDL_GPU/Bgfx test macros: the backend static library and CNA form a genuine
    # cycle (the backend implements IGraphicsBackend while calling back into CNA-defined symbols),
    # which GNU ld's single-pass archive scan only resolves inside --start-group/--end-group.
    macro(cna_sokol_test target src)
        add_executable(${target} ${src})
        target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/examples)
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

    # plan_sokol.md SOKOL-16: real window + real GPU context + real sg_setup, a 60-frame
    # Clear()/Present() loop, and a back-buffer readback of the clear colour.
    cna_sokol_test(cna_test_sokol_smoke examples/sokol_smoke_test.cpp)
    cna_register_backend_test(NAME Sokol_Smoke COMMAND cna_test_sokol_smoke
        TIMEOUT 120 LABELS "Sokol"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_sokol.md SOKOL-17: Texture2D + SpriteBatch vertical slice, every draw pixel-verified
    # through GetBackBufferData().
    cna_sokol_test(cna_test_sokol_2d examples/sokol_2d_test.cpp)
    cna_register_backend_test(NAME Sokol_2D COMMAND cna_test_sokol_2d
        TIMEOUT 120 LABELS "Sokol"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_sokol.md SOKOL-20: vertex-coloured 3D geometry through the public VertexBuffer/
    # BasicEffect/DrawPrimitives API, including a real depth-occlusion proof.
    cna_sokol_test(cna_test_sokol_3d examples/sokol_3d_test.cpp)
    cna_register_backend_test(NAME Sokol_3D COMMAND cna_test_sokol_3d
        TIMEOUT 120 LABELS "Sokol"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_sokol.md SOKOL-21: textured and lit BasicEffect draws (ambient, directional lights,
    # emissive, fog), through the same public API.
    cna_sokol_test(cna_test_sokol_lit3d examples/sokol_lit3d_test.cpp)
    cna_register_backend_test(NAME Sokol_Lit3D COMMAND cna_test_sokol_lit3d
        TIMEOUT 120 LABELS "Sokol"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_sokol.md SOKOL-25: RenderTarget2D. These are shared, backend-agnostic oracles (already
    # registered for EasyGL/Vulkan/Bgfx/SdlGpu/etc.) exercising only the public XNA API, reused
    # here rather than duplicated -- Task 338 (viewport/scissor reset on bind/unbind) and Task 335
    # (a render target's own depth buffer genuinely gates its draws).
    cna_sokol_test(cna_test_sokol_rendertarget_viewport_scissor_reset
                    examples/rendertarget_viewport_scissor_reset_test.cpp)
    cna_register_backend_test(NAME Sokol_RenderTarget_ViewportScissorReset
        COMMAND cna_test_sokol_rendertarget_viewport_scissor_reset
        TIMEOUT 60 LABELS "Sokol"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_sokol_test(cna_test_sokol_rendertarget2d_depth examples/rendertarget2d_depth_test.cpp)
    cna_register_backend_test(NAME Sokol_RenderTarget2D_Depth COMMAND cna_test_sokol_rendertarget2d_depth
        TIMEOUT 60 LABELS "Sokol"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_sokol_test(cna_test_sokol_rendertarget_depthstencil_usage
                    examples/rendertarget_depthstencil_usage_test.cpp)
    cna_register_backend_test(NAME Sokol_RenderTarget_DepthStencilUsage
        COMMAND cna_test_sokol_rendertarget_depthstencil_usage
        TIMEOUT 60 LABELS "Sokol"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_sokol_test(cna_test_sokol_rendertarget_pass_boundary
                    examples/rendertarget_pass_boundary_test.cpp)
    cna_register_backend_test(NAME Sokol_RenderTarget_PassBoundary
        COMMAND cna_test_sokol_rendertarget_pass_boundary
        TIMEOUT 60 LABELS "Sokol"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_sokol_test(cna_test_sokol_rendertarget_producer_consumer
                    examples/rendertarget_producer_consumer_test.cpp)
    cna_register_backend_test(NAME Sokol_RenderTarget_ProducerConsumer
        COMMAND cna_test_sokol_rendertarget_producer_consumer
        TIMEOUT 60 LABELS "Sokol"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_sokol_test(cna_test_sokol_rendertarget_backbuffer_consumer
                    examples/rendertarget_backbuffer_consumer_test.cpp)
    cna_register_backend_test(NAME Sokol_RenderTarget_BackbufferConsumer
        COMMAND cna_test_sokol_rendertarget_backbuffer_consumer
        TIMEOUT 60 LABELS "Sokol"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_sokol_test(cna_test_sokol_rendertarget_first_use
                    examples/rendertarget_first_use_test.cpp)
    cna_register_backend_test(NAME Sokol_RenderTarget_FirstUse
        COMMAND cna_test_sokol_rendertarget_first_use
        TIMEOUT 60 LABELS "Sokol"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_sokol_test(cna_test_sokol_rendertarget_sampling_orientation
                    examples/rendertarget_sampling_orientation_test.cpp)
    cna_register_backend_test(NAME Sokol_RenderTarget_SamplingOrientation
        COMMAND cna_test_sokol_rendertarget_sampling_orientation
        TIMEOUT 60 LABELS "Sokol"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_sokol.md SOKOL-29: a real raw-GL GL_SAMPLES_PASSED occlusion query. Shared,
    # backend-agnostic oracle (already registered for EasyGL/Bgfx) reused here rather than
    # duplicated -- Begin/End/IsComplete/PixelCount plus the invalid-call-sequence and
    # dispose-while-active safety checks.
    cna_sokol_test(cna_test_sokol_occlusion_query examples/occlusion_query_test.cpp)
    cna_register_backend_test(NAME Sokol_OcclusionQuery_Cycle COMMAND cna_test_sokol_occlusion_query
        TIMEOUT 30 LABELS "Sokol"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 445's own pixel/query correctness proof (real BasicEffect + VertexBuffer/IndexBuffer
    # geometry, no CNA_BACKEND_* conditionals despite its "EasyGL" filename) -- reused here since
    # SOKOL now has both real 3D draws and a real occlusion query.
    cna_sokol_test(cna_test_sokol_occlusion_query_visible_quad
                    examples/easygl_occlusion_query_visible_quad_test.cpp)
    cna_register_backend_test(NAME Sokol_OcclusionQuery_VisibleQuad
        COMMAND cna_test_sokol_occlusion_query_visible_quad
        TIMEOUT 30 LABELS "Sokol"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_sokol_test(cna_test_sokol_occlusion_query_occluded_quad
                    examples/easygl_occlusion_query_occluded_quad_test.cpp)
    cna_register_backend_test(NAME Sokol_OcclusionQuery_OccludedQuad
        COMMAND cna_test_sokol_occlusion_query_occluded_quad
        TIMEOUT 30 LABELS "Sokol"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 767's own EasyGL-authored (but backend-agnostic) DepthBias/SlopeScaleDepthBias
    # integration test -- reused here since SOKOL-23 now maps both onto sg_depth_state.bias/
    # bias_slope_scale, the same glPolygonOffset values EasyGL applies.
    cna_sokol_test(cna_test_sokol_depth_bias examples/easygl_depth_bias_test.cpp)
    cna_register_backend_test(NAME Sokol_RasterizerState_DepthBias COMMAND cna_test_sokol_depth_bias
        TIMEOUT 30 LABELS "Sokol"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_sokol.md SOKOL-26: RenderTarget2D MSAA + resolve, following sokol_gfx.h's own documented
    # offscreen-MSAA workflow. Task 337's own differential (MultiSampleCount=0 vs 8) real-AA proof
    # -- backend-agnostic despite its "EasyGL" filename -- reused here rather than duplicated.
    cna_sokol_test(cna_test_sokol_rendertarget2d_msaa examples/easygl_rendertarget2d_msaa_test.cpp)
    cna_register_backend_test(NAME Sokol_RenderTarget2D_Msaa
        COMMAND cna_test_sokol_rendertarget2d_msaa
        TIMEOUT 30 LABELS "Sokol"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-141: a multisampled RenderTargetCube must give each face its own multisample colour
    # state, not one shared attachment all six faces point at -- SokolRenderTargetCubeBackend
    # allocates a real 6-face CUBE image for the MSAA colour attachment (per-face sg_view via
    # .slice), the same "real six-slice array" shape this oracle credits D3D11/D3D12 with, rather
    # than the single shared scratch attachment three other backends originally shipped.
    cna_sokol_test(cna_test_sokol_rendertargetcube_msaa_face
                    examples/rendertargetcube_msaa_face_test.cpp)
    cna_register_backend_test(NAME Sokol_RenderTargetCube_MsaaFace
        COMMAND cna_test_sokol_rendertargetcube_msaa_face
        TIMEOUT 30 LABELS "Sokol"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_sokol.md SOKOL-37: Viewport.MinDepth/MaxDepth. REMED-GFX-079's own backend-agnostic 3D
    # viewport oracle (despite its "software_" filename) -- checks A-G/I-O already exercise the
    # general viewport transform this backend already had; check H is the MinDepth/MaxDepth depth
    # remap this task adds.
    cna_sokol_test(cna_test_sokol_3d_viewport examples/software_3d_viewport_test.cpp)
    cna_register_backend_test(NAME Sokol_Viewport_MinMaxDepth COMMAND cna_test_sokol_3d_viewport
        TIMEOUT 30 LABELS "Sokol"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_sokol.md SOKOL-33: DualTextureEffect. Task 889's own backend-agnostic VertexColorEnabled
    # oracle (no CNA_BACKEND_* conditionals) -- also exercises the base doubling+overlay+diffuse+
    # alpha formula both cases share, reused here rather than duplicated.
    cna_sokol_test(cna_test_sokol_dualtextureeffect_vertexcolor
                    examples/dualtextureeffect_vertexcolor_test.cpp)
    cna_register_backend_test(NAME Sokol_DualTextureEffect_VertexColor
        COMMAND cna_test_sokol_dualtextureeffect_vertexcolor
        TIMEOUT 30 LABELS "Sokol"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_sokol.md SOKOL-39: mipmapped RenderTarget2D. Task 336's own backend-agnostic mip-chain
    # completeness proof (no CNA_BACKEND_* conditionals despite its "easygl_" filename) -- reused
    # here rather than duplicated.
    cna_sokol_test(cna_test_sokol_rendertarget2d_mip examples/easygl_rendertarget2d_mip_test.cpp)
    cna_register_backend_test(NAME Sokol_RenderTarget2D_Mip COMMAND cna_test_sokol_rendertarget2d_mip
        TIMEOUT 30 LABELS "Sokol"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_sokol.md SOKOL-35: SkinnedEffect. Task 123's own backend-agnostic 2-bone GPU transform +
    # mesh deformation oracle (no CNA_BACKEND_* conditionals), reused here rather than duplicated.
    cna_sokol_test(cna_test_sokol_skinned_integration examples/skinned_effect_integration_test.cpp)
    cna_register_backend_test(NAME Sokol_SkinnedEffect_BoneDeformation
        COMMAND cna_test_sokol_skinned_integration
        TIMEOUT 30 LABELS "Sokol"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-008: analytic SkinnedEffect ambient/emissive lighting conformance, also backend-
    # agnostic (its own header comment: "registered per backend by the CMake test files").
    cna_sokol_test(cna_test_sokol_skinnedeffect_lighting_conformance
                    examples/skinnedeffect_lighting_conformance_test.cpp)
    cna_register_backend_test(NAME Sokol_SkinnedEffect_LightingConformance
        COMMAND cna_test_sokol_skinnedeffect_lighting_conformance
        TIMEOUT 30 LABELS "Sokol"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_sokol.md SOKOL-36: instanced draws. WEBGPU-27/38/68's own backend-agnostic oracle (no
    # CNA_BACKEND_* conditionals, drives IGraphicsBackend::DrawInstancedPrimitivesEx() directly),
    # reused here rather than duplicated.
    cna_sokol_test(cna_test_sokol_instanced3d examples/webgpu_instanced3d_test.cpp)
    cna_register_backend_test(NAME Sokol_Instanced3D COMMAND cna_test_sokol_instanced3d
        TIMEOUT 30 LABELS "Sokol"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_sokol.md SOKOL-34: EnvironmentMapEffect. Task 891's own backend-agnostic oracle (no
    # CNA_BACKEND_* conditionals, already registered for EasyGL/Vulkan/Bgfx), reused here rather
    # than duplicated.
    cna_sokol_test(cna_test_sokol_environmentmapeffect_alphascaledlerp
                    examples/environmentmapeffect_alphascaledlerp_test.cpp)
    cna_register_backend_test(NAME Sokol_EnvironmentMapEffect_AlphaScaledLerp
        COMMAND cna_test_sokol_environmentmapeffect_alphascaledlerp
        TIMEOUT 30 LABELS "Sokol"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_sokol.md SOKOL-28: custom ShaderEffect via SpriteBatch.Begin(effect). Task 132's own
    # backend-agnostic oracle (no CNA_BACKEND_* conditionals, already registered for EasyGL),
    # reused here rather than duplicated -- its fixed location=0/1/2 attribute convention matches
    # SokolSpriteBatchBackend's own raw-GL custom-effect draw path exactly.
    cna_sokol_test(cna_test_sokol_shader_effect examples/easygl_shader_effect_test.cpp)
    cna_register_backend_test(NAME Sokol_ShaderEffect_SpriteBatch COMMAND cna_test_sokol_shader_effect
        TIMEOUT 30 LABELS "Sokol"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_sokol.md SOKOL-28: custom ShaderEffect via a real 3D GraphicsDevice::DrawIndexedPrimitives
    # call (not just SpriteBatch). Task 1079's own backend-agnostic oracle (no CNA_BACKEND_*
    # conditionals, already registered for EasyGL), reused here rather than duplicated.
    cna_sokol_test(cna_test_sokol_shader_effect_3d examples/easygl_shadereffect_3d_test.cpp)
    cna_register_backend_test(NAME Sokol_ShaderEffect_3D COMMAND cna_test_sokol_shader_effect_3d
        TIMEOUT 30 LABELS "Sokol"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
endif()
