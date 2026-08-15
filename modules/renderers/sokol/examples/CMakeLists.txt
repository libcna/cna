# sokol renderer example/test registration (module-local; this file is the
# former cmake/Tests/SokolTests.cmake, moved when examples moved into their owning
# physical modules).
#
# From the root CMakeLists.txt include site:
# plan_sokol.md SOKOL-16/17: sokol_gfx renderer CTest registration. Needs a real display and GL
# context, same as the EasyGL/Vulkan/Bgfx entries above -- run with a real (or Xvfb) DISPLAY.

# Example/test executables historically live at the build root (scripts, docs and
# the golden-image working-directory conventions rely on it); keep them there.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
# Shared example-support headers ("common/PixelTestGame.hpp", ...) are owned by
# the graphics module's examples directory.
include_directories(${CNA_GRAPHICS_EXAMPLES_DIR})

if(CNA_BUILD_TESTS AND CNA_BUILD_EXAMPLES
   AND CNA_GRAPHICS_RENDERER STREQUAL "SOKOL")
    enable_testing()

    # The renderer, graphics and input archives form a genuine cycle (the renderer implements
    # IGraphicsRenderer, while GraphicsDevice routes window state into input). Direct renderer
    # tests need that cycle rescanned rather than relying on incidental archive order.
    macro(cna_sokol_test target src)
        add_executable(${target} ${src})
        target_include_directories(${target} PRIVATE ${CNA_GRAPHICS_EXAMPLES_DIR})
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(${target} PRIVATE
                CNA
                "$<LINK_GROUP:RESCAN,cna_input,cna_graphics_core,${RENDERER_TARGET}>"
                SHARP_RUNTIME SDL3::SDL3)
        else()
            target_link_libraries(${target} PRIVATE CNA ${RENDERER_TARGET} SHARP_RUNTIME SDL3::SDL3)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(${target} PRIVATE SDL3::SDL3main)
        endif()
    endmacro()

    # plan_sokol.md SOKOL-16: real window + real GPU context + real sg_setup, a 60-frame
    # Clear()/Present() loop, and a back-buffer readback of the clear colour.
    cna_sokol_test(cna_test_sokol_smoke sokol_smoke_test.cpp)
    cna_register_renderer_test(NAME Sokol_Smoke COMMAND cna_test_sokol_smoke
        TIMEOUT 120 LABELS "Sokol"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_sokol.md SOKOL-17: Texture2D + SpriteBatch vertical slice, every draw pixel-verified
    # through GetBackBufferData().
    cna_sokol_test(cna_test_sokol_2d sokol_2d_test.cpp)
    cna_register_renderer_test(NAME Sokol_2D COMMAND cna_test_sokol_2d
        TIMEOUT 120 LABELS "Sokol"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_sokol.md SOKOL-20: vertex-coloured 3D geometry through the public VertexBuffer/
    # BasicEffect/DrawPrimitives API, including a real depth-occlusion proof.
    cna_sokol_test(cna_test_sokol_3d sokol_3d_test.cpp)
    cna_register_renderer_test(NAME Sokol_3D COMMAND cna_test_sokol_3d
        TIMEOUT 120 LABELS "Sokol"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_sokol.md SOKOL-21: textured and lit BasicEffect draws (ambient, directional lights,
    # emissive, fog), through the same public API.
    cna_sokol_test(cna_test_sokol_lit3d sokol_lit3d_test.cpp)
    cna_register_renderer_test(NAME Sokol_Lit3D COMMAND cna_test_sokol_lit3d
        TIMEOUT 120 LABELS "Sokol"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_sokol.md SOKOL-25: RenderTarget2D. These are shared, renderer-agnostic oracles (already
    # registered for EasyGL/Vulkan/Bgfx/SdlGpu/etc.) exercising only the public XNA API, reused
    # here rather than duplicated -- Task 338 (viewport/scissor reset on bind/unbind) and Task 335
    # (a render target's own depth buffer genuinely gates its draws).
    cna_sokol_test(cna_test_sokol_rendertarget_viewport_scissor_reset
                    ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_viewport_scissor_reset_test.cpp)
    cna_register_renderer_test(NAME Sokol_RenderTarget_ViewportScissorReset
        COMMAND cna_test_sokol_rendertarget_viewport_scissor_reset
        TIMEOUT 60 LABELS "Sokol"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_sokol_test(cna_test_sokol_rendertarget2d_depth ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget2d_depth_test.cpp)
    cna_register_renderer_test(NAME Sokol_RenderTarget2D_Depth COMMAND cna_test_sokol_rendertarget2d_depth
        TIMEOUT 60 LABELS "Sokol"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_sokol_test(cna_test_sokol_rendertarget_depthstencil_usage
                    ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_depthstencil_usage_test.cpp)
    cna_register_renderer_test(NAME Sokol_RenderTarget_DepthStencilUsage
        COMMAND cna_test_sokol_rendertarget_depthstencil_usage
        TIMEOUT 60 LABELS "Sokol"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_sokol_test(cna_test_sokol_rendertarget_pass_boundary
                    ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_pass_boundary_test.cpp)
    cna_register_renderer_test(NAME Sokol_RenderTarget_PassBoundary
        COMMAND cna_test_sokol_rendertarget_pass_boundary
        TIMEOUT 60 LABELS "Sokol"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_sokol_test(cna_test_sokol_rendertarget_producer_consumer
                    ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_producer_consumer_test.cpp)
    cna_register_renderer_test(NAME Sokol_RenderTarget_ProducerConsumer
        COMMAND cna_test_sokol_rendertarget_producer_consumer
        TIMEOUT 60 LABELS "Sokol"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_sokol_test(cna_test_sokol_rendertarget_backbuffer_consumer
                    ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_backbuffer_consumer_test.cpp)
    cna_register_renderer_test(NAME Sokol_RenderTarget_BackbufferConsumer
        COMMAND cna_test_sokol_rendertarget_backbuffer_consumer
        TIMEOUT 60 LABELS "Sokol"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_sokol_test(cna_test_sokol_rendertarget_first_use
                    ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_first_use_test.cpp)
    cna_register_renderer_test(NAME Sokol_RenderTarget_FirstUse
        COMMAND cna_test_sokol_rendertarget_first_use
        TIMEOUT 60 LABELS "Sokol"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_sokol_test(cna_test_sokol_rendertarget_sampling_orientation
                    ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_sampling_orientation_test.cpp)
    cna_register_renderer_test(NAME Sokol_RenderTarget_SamplingOrientation
        COMMAND cna_test_sokol_rendertarget_sampling_orientation
        TIMEOUT 60 LABELS "Sokol"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_sokol.md SOKOL-29: a real raw-GL GL_SAMPLES_PASSED occlusion query. Shared,
    # renderer-agnostic oracle (already registered for EasyGL/Bgfx) reused here rather than
    # duplicated -- Begin/End/IsComplete/PixelCount plus the invalid-call-sequence and
    # dispose-while-active safety checks.
    cna_sokol_test(cna_test_sokol_occlusion_query ${CNA_GRAPHICS_EXAMPLES_DIR}/occlusion_query_test.cpp)
    cna_register_renderer_test(NAME Sokol_OcclusionQuery_Cycle COMMAND cna_test_sokol_occlusion_query
        TIMEOUT 30 LABELS "Sokol"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 445's own pixel/query correctness proof (real BasicEffect + VertexBuffer/IndexBuffer
    # geometry, no CNA_RENDERER_* conditionals despite its "EasyGL" filename) -- reused here since
    # SOKOL now has both real 3D draws and a real occlusion query.
    cna_sokol_test(cna_test_sokol_occlusion_query_visible_quad
                    ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_occlusion_query_visible_quad_test.cpp)
    cna_register_renderer_test(NAME Sokol_OcclusionQuery_VisibleQuad
        COMMAND cna_test_sokol_occlusion_query_visible_quad
        TIMEOUT 30 LABELS "Sokol"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_sokol_test(cna_test_sokol_occlusion_query_occluded_quad
                    ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_occlusion_query_occluded_quad_test.cpp)
    cna_register_renderer_test(NAME Sokol_OcclusionQuery_OccludedQuad
        COMMAND cna_test_sokol_occlusion_query_occluded_quad
        TIMEOUT 30 LABELS "Sokol"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 767's own EasyGL-authored (but renderer-agnostic) DepthBias/SlopeScaleDepthBias
    # integration test -- reused here since SOKOL-23 now maps both onto sg_depth_state.bias/
    # bias_slope_scale, the same glPolygonOffset values EasyGL applies.
    cna_sokol_test(cna_test_sokol_depth_bias ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_depth_bias_test.cpp)
    cna_register_renderer_test(NAME Sokol_RasterizerState_DepthBias COMMAND cna_test_sokol_depth_bias
        TIMEOUT 30 LABELS "Sokol"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_sokol.md SOKOL-26: RenderTarget2D MSAA + resolve, following sokol_gfx.h's own documented
    # offscreen-MSAA workflow. Task 337's own differential (MultiSampleCount=0 vs 8) real-AA proof
    # -- renderer-agnostic despite its "EasyGL" filename -- reused here rather than duplicated.
    cna_sokol_test(cna_test_sokol_rendertarget2d_msaa ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_rendertarget2d_msaa_test.cpp)
    cna_register_renderer_test(NAME Sokol_RenderTarget2D_Msaa
        COMMAND cna_test_sokol_rendertarget2d_msaa
        TIMEOUT 30 LABELS "Sokol"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-141: a multisampled RenderTargetCube must give each face its own multisample colour
    # state, not one shared attachment all six faces point at -- SokolRenderTargetCubeRenderer
    # allocates a real 6-face CUBE image for the MSAA colour attachment (per-face sg_view via
    # .slice), the same "real six-slice array" shape this oracle credits D3D11/D3D12 with, rather
    # than the single shared scratch attachment three other renderers originally shipped.
    cna_sokol_test(cna_test_sokol_rendertargetcube_msaa_face
                    ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertargetcube_msaa_face_test.cpp)
    cna_register_renderer_test(NAME Sokol_RenderTargetCube_MsaaFace
        COMMAND cna_test_sokol_rendertargetcube_msaa_face
        TIMEOUT 30 LABELS "Sokol"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_sokol.md SOKOL-37: Viewport.MinDepth/MaxDepth. REMED-GFX-079's own renderer-agnostic 3D
    # viewport oracle (despite its "software_" filename) -- checks A-G/I-O already exercise the
    # general viewport transform this renderer already had; check H is the MinDepth/MaxDepth depth
    # remap this task adds.
    cna_sokol_test(cna_test_sokol_3d_viewport ${CMAKE_SOURCE_DIR}/modules/renderers/software/examples/software_3d_viewport_test.cpp)
    cna_register_renderer_test(NAME Sokol_Viewport_MinMaxDepth COMMAND cna_test_sokol_3d_viewport
        TIMEOUT 30 LABELS "Sokol"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_sokol.md SOKOL-33: DualTextureEffect. Task 889's own renderer-agnostic VertexColorEnabled
    # oracle (no CNA_RENDERER_* conditionals) -- also exercises the base doubling+overlay+diffuse+
    # alpha formula both cases share, reused here rather than duplicated.
    cna_sokol_test(cna_test_sokol_dualtextureeffect_vertexcolor
                    ${CNA_GRAPHICS_EXAMPLES_DIR}/dualtextureeffect_vertexcolor_test.cpp)
    cna_register_renderer_test(NAME Sokol_DualTextureEffect_VertexColor
        COMMAND cna_test_sokol_dualtextureeffect_vertexcolor
        TIMEOUT 30 LABELS "Sokol"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_sokol.md SOKOL-39: mipmapped RenderTarget2D. Task 336's own renderer-agnostic mip-chain
    # completeness proof (no CNA_RENDERER_* conditionals despite its "easygl_" filename) -- reused
    # here rather than duplicated.
    cna_sokol_test(cna_test_sokol_rendertarget2d_mip ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_rendertarget2d_mip_test.cpp)
    cna_register_renderer_test(NAME Sokol_RenderTarget2D_Mip COMMAND cna_test_sokol_rendertarget2d_mip
        TIMEOUT 30 LABELS "Sokol"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_sokol.md SOKOL-35: SkinnedEffect. Task 123's own renderer-agnostic 2-bone GPU transform +
    # mesh deformation oracle (no CNA_RENDERER_* conditionals), reused here rather than duplicated.
    cna_sokol_test(cna_test_sokol_skinned_integration ${CNA_GRAPHICS_EXAMPLES_DIR}/skinned_effect_integration_test.cpp)
    cna_register_renderer_test(NAME Sokol_SkinnedEffect_BoneDeformation
        COMMAND cna_test_sokol_skinned_integration
        TIMEOUT 30 LABELS "Sokol"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-008: analytic SkinnedEffect ambient/emissive lighting conformance, also renderer-
    # agnostic (its own header comment: "registered per renderer by the CMake test files").
    cna_sokol_test(cna_test_sokol_skinnedeffect_lighting_conformance
                    ${CNA_GRAPHICS_EXAMPLES_DIR}/skinnedeffect_lighting_conformance_test.cpp)
    cna_register_renderer_test(NAME Sokol_SkinnedEffect_LightingConformance
        COMMAND cna_test_sokol_skinnedeffect_lighting_conformance
        TIMEOUT 30 LABELS "Sokol"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_sokol.md SOKOL-36: instanced draws. WEBGPU-27/38/68's own renderer-agnostic oracle (no
    # CNA_RENDERER_* conditionals, drives IGraphicsRenderer::DrawInstancedPrimitivesEx() directly),
    # reused here rather than duplicated.
    cna_sokol_test(cna_test_sokol_instanced3d ${CMAKE_SOURCE_DIR}/modules/renderers/webgpu/examples/webgpu_instanced3d_test.cpp)
    cna_register_renderer_test(NAME Sokol_Instanced3D COMMAND cna_test_sokol_instanced3d
        TIMEOUT 30 LABELS "Sokol"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_sokol.md SOKOL-34: EnvironmentMapEffect. Task 891's own renderer-agnostic oracle (no
    # CNA_RENDERER_* conditionals, already registered for EasyGL/Vulkan/Bgfx), reused here rather
    # than duplicated.
    cna_sokol_test(cna_test_sokol_environmentmapeffect_alphascaledlerp
                    ${CNA_GRAPHICS_EXAMPLES_DIR}/environmentmapeffect_alphascaledlerp_test.cpp)
    cna_register_renderer_test(NAME Sokol_EnvironmentMapEffect_AlphaScaledLerp
        COMMAND cna_test_sokol_environmentmapeffect_alphascaledlerp
        TIMEOUT 30 LABELS "Sokol"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_sokol.md SOKOL-28: custom ShaderEffect via SpriteBatch.Begin(effect). Task 132's own
    # renderer-agnostic oracle (no CNA_RENDERER_* conditionals, already registered for EasyGL),
    # reused here rather than duplicated -- its fixed location=0/1/2 attribute convention matches
    # SokolSpriteBatchRenderer's own raw-GL custom-effect draw path exactly.
    cna_sokol_test(cna_test_sokol_shader_effect ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_shader_effect_test.cpp)
    cna_register_renderer_test(NAME Sokol_ShaderEffect_SpriteBatch COMMAND cna_test_sokol_shader_effect
        TIMEOUT 30 LABELS "Sokol"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_sokol.md SOKOL-28: custom ShaderEffect via a real 3D GraphicsDevice::DrawIndexedPrimitives
    # call (not just SpriteBatch). Task 1079's own renderer-agnostic oracle (no CNA_RENDERER_*
    # conditionals, already registered for EasyGL), reused here rather than duplicated.
    cna_sokol_test(cna_test_sokol_shader_effect_3d ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_shadereffect_3d_test.cpp)
    cna_register_renderer_test(NAME Sokol_ShaderEffect_3D COMMAND cna_test_sokol_shader_effect_3d
        TIMEOUT 30 LABELS "Sokol"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_sokol.md SOKOL-26: multiple render targets (MRT). REMED-GFX-016's own renderer-agnostic
    # oracle (no CNA_RENDERER_* conditionals, already registered for EasyGL), reused here rather than
    # duplicated -- its four-output ESSL 3.00 ShaderEffect fragment shader exercises the same
    # raw-GL custom-effect draw path SOKOL-28 already established, now against a real
    # multi-attachment sg_pass.
    cna_sokol_test(cna_test_sokol_mrt ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_mrt_test.cpp)
    cna_register_renderer_test(NAME Sokol_MRT COMMAND cna_test_sokol_mrt
        TIMEOUT 30 LABELS "Sokol"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_sokol.md SOKOL-40: GraphicsDevice.BlendFactor must not be cached away by a stale Sokol
    # pipeline -- sg_pipeline_desc.blend_color is baked at creation time with no dynamic
    # blend-constant call, unlike Vulkan/EasyGL, so it must be part of every pipeline cache key.
    cna_sokol_test(cna_test_sokol_blendfactor_pipeline_cache
                    sokol_blendfactor_pipeline_cache_test.cpp)
    cna_register_renderer_test(NAME Sokol_BlendFactor_PipelineCache
        COMMAND cna_test_sokol_blendfactor_pipeline_cache
        TIMEOUT 30 LABELS "Sokol"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_sokol.md SOKOL-45: GL-context creation must share the constructor's transactional
    # cleanup path -- platform CreateContext() succeeding followed by MakeCurrent() failing must
    # not leak the context.
    cna_sokol_test(cna_test_sokol_gl_context_transactional
                    sokol_gl_context_transactional_test.cpp)
    cna_register_renderer_test(NAME Sokol_GLContext_Transactional
        COMMAND cna_test_sokol_gl_context_transactional
        TIMEOUT 30 LABELS "Sokol"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_sokol.md SOKOL-42: OcclusionQuery/ShaderEffect must release their raw-GL renderer objects
    # during GraphicsDevice::Dispose(), before the renderer device/GL context is torn down.
    cna_sokol_test(cna_test_sokol_dispose_order_occlusionquery_shadereffect
                    sokol_dispose_order_occlusionquery_shadereffect_test.cpp)
    cna_register_renderer_test(NAME Sokol_DisposeOrder_OcclusionQueryShaderEffect
        COMMAND cna_test_sokol_dispose_order_occlusionquery_shadereffect
        TIMEOUT 30 LABELS "Sokol"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_sokol.md SOKOL-44: a custom ShaderEffect's deferred texture bindings must survive the
    # bound texture being re-uploaded (SetData()) between SetTexture() and the eventual draw.
    cna_sokol_test(cna_test_sokol_customeffect_texture_reupload
                    sokol_customeffect_texture_reupload_test.cpp)
    cna_register_renderer_test(NAME Sokol_CustomEffect_TextureReupload
        COMMAND cna_test_sokol_customeffect_texture_reupload
        TIMEOUT 30 LABELS "Sokol"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_sokol.md SOKOL-41: SpriteBatch's raw-GL custom-effect draw path must apply the device's
    # real BlendState instead of inheriting whatever GL_BLEND state a previous draw left behind.
    cna_sokol_test(cna_test_sokol_customeffect_blendstate_order
                    sokol_customeffect_blendstate_order_test.cpp)
    cna_register_renderer_test(NAME Sokol_CustomEffect_BlendStateOrder
        COMMAND cna_test_sokol_customeffect_blendstate_order
        TIMEOUT 30 LABELS "Sokol"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_sokol.md SOKOL-47: committed RenderTargetCube mipmap coverage, replacing SOKOL-39's
    # original throwaway/uncommitted verification program.
    cna_sokol_test(cna_test_sokol_rendertargetcube_mip
                    sokol_rendertargetcube_mip_test.cpp)
    cna_register_renderer_test(NAME Sokol_RenderTargetCube_Mip
        COMMAND cna_test_sokol_rendertargetcube_mip
        TIMEOUT 30 LABELS "Sokol"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_sokol.md SOKOL-48: compact state-transition/lifetime regression matrix -- two-object
    # OcclusionQuery interleaving (both orders) and DrawCustomEffect3D cull-mode independence from
    # a preceding stock draw's leftover GL state.
    cna_sokol_test(cna_test_sokol_state_lifetime_regression_matrix
                    sokol_state_lifetime_regression_matrix_test.cpp)
    cna_register_renderer_test(NAME Sokol_StateLifetimeRegressionMatrix
        COMMAND cna_test_sokol_state_lifetime_regression_matrix
        TIMEOUT 30 LABELS "Sokol"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_sokol.md SOKOL-23: RasterizerState.FillMode == WireFrame, implemented via CPU-side
    # triangle-to-GL_LINES re-expansion (the same technique EasyGLRenderer::DrawWireframe()
    # uses) despite sokol_gfx exposing no native polygon-fill-mode API.
    cna_sokol_test(cna_test_sokol_wireframe sokol_wireframe_test.cpp)
    cna_register_renderer_test(NAME Sokol_WireFrame COMMAND cna_test_sokol_wireframe
        TIMEOUT 30 LABELS "Sokol"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_sokol.md SOKOL-24: VertexBuffer/IndexBuffer SetData() reuses its sokol_gfx buffer via
    # sg_update_buffer() for same-shape re-uploads across frames instead of always recreating it,
    # falling back to recreate when the data outgrows what is allocated or a second upload lands
    # within the same frame (sokol_gfx's own one-update-per-buffer-per-frame rule).
    cna_sokol_test(cna_test_sokol_vertexbuffer_reupload
                    sokol_vertexbuffer_reupload_test.cpp)
    cna_register_renderer_test(NAME Sokol_VertexBuffer_Reupload
        COMMAND cna_test_sokol_vertexbuffer_reupload
        TIMEOUT 30 LABELS "Sokol"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
endif()

# Module-local tests get the same headless-safe skip convention the root one-shot
# gives root-registered tests (see cmake/TestHelpers.cmake).
cna_apply_skip_convention()
