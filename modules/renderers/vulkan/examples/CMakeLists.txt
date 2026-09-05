# vulkan renderer example/test registration (module-local; this file is the
# former cmake/Tests/VulkanTests.cmake, moved when examples moved into their owning
# physical modules).
#
# From the root CMakeLists.txt include site:
# Vulkan integration tests. Self-contained: bundles the CNA_BUILD_EXAMPLES condition this block
# has always (historically) required -- see VulkanTests.cmake's own comment.

# Example/test executables historically live at the build root (scripts, docs and
# the golden-image working-directory conventions rely on it); keep them there.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
# Shared example-support headers ("common/PixelTestGame.hpp", ...) are owned by
# the graphics module's examples directory.
include_directories(${CNA_GRAPHICS_EXAMPLES_DIR})

# Historically nested inside the house3d_demo/net-demos block below (both only build for the
# EASYGL/VULKAN renderers), so Vulkan's test suite has always additionally required
# CNA_BUILD_EXAMPLES=ON, unlike every other renderer's test block. Preserved verbatim here as its
# own self-contained condition rather than silently dropped during the file split.
if(CNA_BUILD_EXAMPLES AND CNA_BUILD_TESTS AND NOT EMSCRIPTEN AND NOT WIN32
   AND CNA_GRAPHICS_RENDERER STREQUAL "VULKAN")

        # --- helper macro: build a headless Vulkan test exe ----------------------
        macro(cna_vulkan_test target src)
            add_executable(${target} ${src})
            # SDL3::SDL3 is linked explicitly like every other renderer's test macro already
            # does: several of these example TUs include <SDL3/SDL.h> directly, and nothing on
            # the CNA surface propagates SDL's include directories (SDL has always been a
            # PRIVATE implementation dependency). This was a latent pre-existing gap unique to
            # the Vulkan macro -- the same TU fails to compile in the pristine tree too.
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

        enable_testing()

        # plans/plan_webgpu.md WEBGPU-123: the shared cross-renderer diagnostic scene built for
        # VULKAN, exactly as EASYGL's cna_diag_easygl / SOFTWARE's cna_diag_software / WEBGPU's
        # cna_diag_webgpu. Deliberately NOT a CTest (a two-build pixel comparison, not a single-run
        # pass/fail): it writes a 64x64 RGBA8 dump for cna_diag_compare.
        cna_vulkan_test(cna_diag_vulkan ${CNA_GRAPHICS_EXAMPLES_DIR}/cross_renderer_diagnostic_scene.cpp)

        # Task 88: 3-frame smoke test for the Vulkan renderer
        cna_register_renderer_test(NAME Vulkan_Demo2D_SmokeTest COMMAND cna_demo_2d --smoke 3
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}" LABELS "GraphicsSmoke")

        # REMED-GFX-011: orientation oracle. Pins "XNA +Y is up" and "readback row 0 is top"
        # against colored3d, a family whose Y-flip is already correct, so that the per-family
        # orientation tests below assert a calibrated convention rather than an assumed one.
        cna_vulkan_test(cna_test_vulkan_orientation_calibration
                        vulkan_orientation_calibration_test.cpp)
        cna_register_renderer_test(NAME Vulkan_Orientation_Calibration COMMAND cna_test_vulkan_orientation_calibration
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-011: per-family orientation regression for env_map3d / pbr3d /
        # pbr3d_skinned / instanced3d — asymmetric quadrant geometry, four-corner sampling.
        cna_vulkan_test(cna_test_vulkan_orientation_effects
                        vulkan_orientation_effects_test.cpp)
        cna_register_renderer_test(NAME Vulkan_Orientation_Effects COMMAND cna_test_vulkan_orientation_effects
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-212 structural half: honouring BasicEffect.VertexColorEnabled on the instanced
        # route must build no pipeline variant per setting, must keep a position-only and a
        # position+colour declaration in separate variants, and must add no submit, wait or
        # present. Counts come from the renderer's own cumulative EXT counters.
        cna_vulkan_test(cna_test_vulkan_instanced_vertex_color_cardinality
                        vulkan_instanced_vertex_color_cardinality_test.cpp)
        cna_register_renderer_test(NAME Vulkan_InstancedVertexColor_Cardinality
            COMMAND cna_test_vulkan_instanced_vertex_color_cardinality
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-011: same four families rendered into a RenderTarget2D, calibrated against
        # colored3d — proves the RT path is not double-flipped relative to the backbuffer path.
        cna_vulkan_test(cna_test_vulkan_orientation_rendertarget
                        vulkan_orientation_rendertarget_test.cpp)
        cna_register_renderer_test(NAME Vulkan_Orientation_RenderTarget COMMAND cna_test_vulkan_orientation_rendertarget
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-006: SkinnedEffect world-space normal transform under non-identity World
        # (rotation, non-uniform scale, and both) — the cases every existing skinned test,
        # all of which use World = Identity, structurally cannot detect.
        cna_vulkan_test(cna_test_vulkan_skinnedeffect_world_normal
                        vulkan_skinnedeffect_world_normal_test.cpp)
        cna_register_renderer_test(NAME Vulkan_SkinnedEffect_WorldNormal COMMAND cna_test_vulkan_skinnedeffect_world_normal
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 124: DrawInstancedPrimitives — 3 instances at different X positions
        cna_vulkan_test(cna_test_vulkan_instanced
                        vulkan_instanced_test.cpp)
        cna_register_renderer_test(NAME Vulkan_DrawInstanced_3Instances COMMAND cna_test_vulkan_instanced
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # plan_vulkan.md VULKAN-396: what the per-one-time-command vkQueueWaitIdle costs, measured
        cna_vulkan_test(cna_test_vulkan_one_time_command_cost
                        vulkan_one_time_command_cost_test.cpp)
        cna_register_renderer_test(NAME Vulkan_OneTimeCommandCost COMMAND cna_test_vulkan_one_time_command_cost
            TIMEOUT 120 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # plan_vulkan.md VULKAN-400: is the EasyGL-documented "full-backbuffer SpriteBatch draw before
        # a 3D draw breaks the frame" finding (migration-guide known issues, Task 933) present here?
        # Renderer-neutral source so the same file can answer it for any family.
        cna_vulkan_test(cna_test_vulkan_fullscreen_sprite_then_3d
            ${CNA_GRAPHICS_EXAMPLES_DIR}/fullscreen_spritebatch_then_3d_test.cpp)
        cna_register_renderer_test(NAME Vulkan_FullscreenSpriteThen3D COMMAND cna_test_vulkan_fullscreen_sprite_then_3d
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # plan_vulkan.md VULKAN-055: a 3D draw after a SpriteBatch Begin()/End() pair inherits
        # whatever GraphicsDevice.BlendState currently is -- FNA's own lasting side effect, asserted
        # by pixel arithmetic (Additive: red(200,0,0) + gray(100,100,100) clamps to (255,100,100),
        # versus (200,0,0) if the batch's blend did not survive End()). Renderer-agnostic source.
        cna_vulkan_test(cna_test_vulkan_spritebatch_blendstate_leak
                        ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_spritebatch_blendstate_leak_test.cpp)
        cna_register_renderer_test(NAME Vulkan_SpriteBatch_BlendStateLeak COMMAND cna_test_vulkan_spritebatch_blendstate_leak
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # plan_vulkan.md VULKAN-392: disposing a buffer must not stall the whole device
        cna_vulkan_test(cna_test_vulkan_buffer_dispose_stall
                        vulkan_buffer_dispose_stall_test.cpp)
        cna_register_renderer_test(NAME Vulkan_BufferDisposeStall COMMAND cna_test_vulkan_buffer_dispose_stall
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # plan_vulkan.md VULKAN-134: DrawUserPrimitives / DrawUserIndexedPrimitives on Vulkan.
        # All four sources are renderer-agnostic (public XNA API only, zero renderer includes), so
        # they run here unchanged -- the same reuse pattern as the other EasyGL sources above.
        # These routes allocate a throwaway VertexBuffer per call (GraphicsDevice.cpp:1753+), which
        # on Vulkan is a vkCreateBuffer/vkAllocateMemory per draw, so they are also VULKAN-392's
        # exercising scene.
        cna_vulkan_test(cna_test_vulkan_draw_user_primitives_vpc
                        ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_draw_user_primitives_vpc_test.cpp)
        cna_register_renderer_test(NAME Vulkan_DrawUserPrimitives_VPC COMMAND cna_test_vulkan_draw_user_primitives_vpc
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_vulkan_test(cna_test_vulkan_draw_user_primitives_custom
                        ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_draw_user_primitives_custom_test.cpp)
        cna_register_renderer_test(NAME Vulkan_DrawUserPrimitives_CustomVertex COMMAND cna_test_vulkan_draw_user_primitives_custom
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_vulkan_test(cna_test_vulkan_draw_user_indexed_primitives_vpc
                        ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_draw_user_indexed_primitives_vpc_test.cpp)
        cna_register_renderer_test(NAME Vulkan_DrawUserIndexedPrimitives_VPC COMMAND cna_test_vulkan_draw_user_indexed_primitives_vpc
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_vulkan_test(cna_test_vulkan_draw_user_indexed_primitives_32
                        ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_draw_user_indexed_primitives_32_test.cpp)
        cna_register_renderer_test(NAME Vulkan_DrawUserIndexedPrimitives_32 COMMAND cna_test_vulkan_draw_user_indexed_primitives_32
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # plan_vulkan.md VULKAN-347: the echoed multisample count must be the applied one
        cna_vulkan_test(cna_test_vulkan_applied_multisample
                        vulkan_applied_multisample_test.cpp)
        cna_register_renderer_test(NAME Vulkan_AppliedMultiSampleCount COMMAND cna_test_vulkan_applied_multisample
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # plan_vulkan.md VULKAN-025: what AcquireThreadContextLeaseEXT() means here, and what it does not
        cna_vulkan_test(cna_test_vulkan_thread_context_lease
                        vulkan_thread_context_lease_test.cpp)
        cna_register_renderer_test(NAME Vulkan_ThreadContextLease COMMAND cna_test_vulkan_thread_context_lease
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # plan_vulkan.md VULKAN-026: what CanBeginDrawEXT() answers when the swapchain is out of date
        cna_vulkan_test(cna_test_vulkan_swapchain_outofdate
                        vulkan_swapchain_outofdate_test.cpp)
        cna_register_renderer_test(NAME Vulkan_SwapchainOutOfDate COMMAND cna_test_vulkan_swapchain_outofdate
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 119: ShaderEffect with SPIR-V — SpriteBatch tint via custom push constants
        cna_vulkan_test(cna_test_vulkan_shader_effect
                        vulkan_shader_effect_test.cpp)
        cna_register_renderer_test(NAME Vulkan_ShaderEffect_SpirV COMMAND cna_test_vulkan_shader_effect
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 284: Texture2D Color format mapping — linear vs. sRGB decode on Vulkan
        cna_vulkan_test(cna_test_vulkan_texture_srgb
                        vulkan_texture_srgb_test.cpp)
        cna_register_renderer_test(NAME Vulkan_Texture2D_ColorFormat_Linear COMMAND cna_test_vulkan_texture_srgb
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 135: DualTextureEffect — magenta×yellow→red pixel readback on Vulkan
        cna_vulkan_test(cna_test_vulkan_dual_texture
                        vulkan_dual_texture_test.cpp)
        cna_register_renderer_test(NAME Vulkan_DualTextureEffect_Blend COMMAND cna_test_vulkan_dual_texture
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 293: DualTextureEffect (3D stock effect) honors GraphicsDevice.SamplerStates[0] on
        # Vulkan too — reuses the renderer-agnostic EasyGL test source (public XNA API only).
        cna_vulkan_test(cna_test_vulkan_sampler_state_effect
                        ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_sampler_state_effect_test.cpp)
        cna_register_renderer_test(NAME Vulkan_SamplerState_DualTextureEffect COMMAND cna_test_vulkan_sampler_state_effect
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 294: TextureAddressMode::Clamp on a 3D stock effect on Vulkan too (reuses the
        # renderer-agnostic EasyGL test source).
        cna_vulkan_test(cna_test_vulkan_texture_address_mode_clamp_effect
                        ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_texture_address_mode_clamp_effect_test.cpp)
        cna_register_renderer_test(NAME Vulkan_TextureAddressMode_Clamp_DualTextureEffect COMMAND cna_test_vulkan_texture_address_mode_clamp_effect
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 296: TextureAddressMode::Mirror on a 3D stock effect on Vulkan too (reuses the
        # renderer-agnostic EasyGL test source).
        cna_vulkan_test(cna_test_vulkan_texture_address_mode_mirror_effect
                        ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_texture_address_mode_mirror_effect_test.cpp)
        cna_register_renderer_test(NAME Vulkan_TextureAddressMode_Mirror_DualTextureEffect COMMAND cna_test_vulkan_texture_address_mode_mirror_effect
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 297: TextureFilter::Point vs Linear on Vulkan too (reuses the renderer-agnostic
        # EasyGL test source).
        cna_vulkan_test(cna_test_vulkan_texture_filter_point_vs_linear
                        ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_texture_filter_point_vs_linear_test.cpp)
        cna_register_renderer_test(NAME Vulkan_TextureFilter_PointVsLinear COMMAND cna_test_vulkan_texture_filter_point_vs_linear
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 298: now registered on Vulkan too -- a Vulkan-specific adaptation of the EasyGL
        # source (can't reuse it verbatim: Vulkan's TextureFilter::Point mapping has always been
        # mip-aware, unlike EasyGL's still-flat mapping, so the expected result for that one check
        # differs -- see the test's own file-header comment). Task 925 fixed the confound noted
        # here previously (Texture2D::SetData(level>0) was a silent no-op on Vulkan,
        # UpdatePixelsLevel never overridden; mipLevels/levelCount were also both hardcoded to 1).
        cna_vulkan_test(cna_test_vulkan_texture_mip_filter_effect
                        vulkan_texture_mip_filter_effect_test.cpp)
        cna_register_renderer_test(NAME Vulkan_TextureMipFilter_DualTextureEffect COMMAND cna_test_vulkan_texture_mip_filter_effect
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 299: anisotropic filtering caps and fallback on Vulkan too (reuses the
        # renderer-agnostic EasyGL test source). This is the one renderer where MaxAnisotropy is
        # actually respected (queried/clamped to the real device cap).
        cna_vulkan_test(cna_test_vulkan_texture_anisotropic_effect
                        ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_texture_anisotropic_effect_test.cpp)
        cna_register_renderer_test(NAME Vulkan_TextureAnisotropic_DualTextureEffect COMMAND cna_test_vulkan_texture_anisotropic_effect
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 303: BlendState::Opaque on Vulkan too (reuses the renderer-agnostic EasyGL source)
        cna_vulkan_test(cna_test_vulkan_blendstate_opaque
                        ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_blendstate_opaque_test.cpp)
        cna_register_renderer_test(NAME Vulkan_BlendState_Opaque COMMAND cna_test_vulkan_blendstate_opaque
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 304: BlendState::AlphaBlend on Vulkan too (reuses the renderer-agnostic EasyGL source)
        cna_vulkan_test(cna_test_vulkan_blendstate_alphablend
                        ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_blendstate_alphablend_test.cpp)
        cna_register_renderer_test(NAME Vulkan_BlendState_AlphaBlend COMMAND cna_test_vulkan_blendstate_alphablend
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 305: BlendState::NonPremultiplied on Vulkan too (reuses the renderer-agnostic
        # EasyGL source). NOTE: a passing result here does NOT mean Task 868 is fixed — see the
        # test source's file-header comment for why.
        cna_vulkan_test(cna_test_vulkan_blendstate_nonpremultiplied
                        ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_blendstate_nonpremultiplied_test.cpp)
        cna_register_renderer_test(NAME Vulkan_BlendState_NonPremultiplied COMMAND cna_test_vulkan_blendstate_nonpremultiplied
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 306: BlendState::Additive on Vulkan too (reuses the renderer-agnostic EasyGL source).
        # NOTE: expected to genuinely re-expose Task 868 here, unlike Task 305's coincidental pass.
        cna_vulkan_test(cna_test_vulkan_blendstate_additive
                        ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_blendstate_additive_test.cpp)
        cna_register_renderer_test(NAME Vulkan_BlendState_Additive COMMAND cna_test_vulkan_blendstate_additive
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 307: ColorBlendFunction/AlphaBlendFunction independence on Vulkan too (reuses the
        # renderer-agnostic EasyGL source). NOTE: expect Check A (Subtract) to fail per Task 868 -
        # Vulkan always hardcodes VK_BLEND_OP_ADD.
        cna_vulkan_test(cna_test_vulkan_blendstate_separate_functions
                        ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_blendstate_separate_functions_test.cpp)
        cna_register_renderer_test(NAME Vulkan_BlendState_SeparateFunctions COMMAND cna_test_vulkan_blendstate_separate_functions
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 308: ColorSourceBlend/ColorDestinationBlend independence on Vulkan too (reuses the
        # renderer-agnostic EasyGL source). NOTE: expect Check A to coincidentally pass and Check B
        # to fail per Task 868 - Vulkan always hardcodes SourceAlpha/InverseSourceAlpha for colour.
        cna_vulkan_test(cna_test_vulkan_blendstate_separate_factors
                        ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_blendstate_separate_factors_test.cpp)
        cna_register_renderer_test(NAME Vulkan_BlendState_SeparateFactors COMMAND cna_test_vulkan_blendstate_separate_factors
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 309: BlendState.BlendFactor propagation (reuses the renderer-agnostic EasyGL source).
        # This DrawUserPrimitives (3D-path) test PASSES: Task 868's FillBlendAttachmentState +
        # per-draw draw.blendParams made the 3D pipeline honor Blend::BlendFactor
        # (VK_BLEND_FACTOR_CONSTANT_COLOR), and REMED-GFX-070 made the constant per-draw-correct.
        # (A prior note here claiming this was "expected to FAIL per Task 868" was stale — the 3D
        # path was fixed. The remaining limitation is SpriteBatch-only: the 2D sprite pipeline still
        # hardcodes alpha-blend and ignores the BlendState — tracked as REMED-GFX-071.)
        cna_vulkan_test(cna_test_vulkan_blendstate_blendfactor
                        ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_blendstate_blendfactor_test.cpp)
        cna_register_renderer_test(NAME Vulkan_BlendState_BlendFactor COMMAND cna_test_vulkan_blendstate_blendfactor
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 313: DepthStencilState.DepthBufferWriteEnable on Vulkan (reuses the renderer-agnostic
        # EasyGL source).
        cna_vulkan_test(cna_test_vulkan_depthstencilstate_write_enable
                        ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_depthstencilstate_write_enable_test.cpp)
        cna_register_renderer_test(NAME Vulkan_DepthStencilState_WriteEnable COMMAND cna_test_vulkan_depthstencilstate_write_enable
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 314: DepthStencilState.DepthBufferFunction on Vulkan (reuses the renderer-agnostic
        # EasyGL source). NOTE: expected to FAIL some/all checks per Task 870 - Vulkan hardcodes
        # depthCompareOp per pipeline, ignoring DepthBufferFunction entirely.
        cna_vulkan_test(cna_test_vulkan_depthstencilstate_compare_function
                        ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_depthstencilstate_compare_function_test.cpp)
        cna_register_renderer_test(NAME Vulkan_DepthStencilState_CompareFunction COMMAND cna_test_vulkan_depthstencilstate_compare_function
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 315: DepthStencilState.StencilEnable on Vulkan (reuses the renderer-agnostic EasyGL
        # source). NOTE: expected to FAIL Check A per Task 870 - stencil testing is completely
        # non-functional on Vulkan (stencilTestEnable never set), so no gating ever happens.
        cna_vulkan_test(cna_test_vulkan_depthstencilstate_stencil_enable
                        ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_depthstencilstate_stencil_enable_test.cpp)
        cna_register_renderer_test(NAME Vulkan_DepthStencilState_StencilEnable COMMAND cna_test_vulkan_depthstencilstate_stencil_enable
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 316: DepthStencilState.StencilMask/StencilWriteMask on Vulkan (reuses the
        # renderer-agnostic EasyGL source). NOTE: expected to partially fail per Task 870 - stencil
        # testing never gates on Vulkan, so 2 of 4 checks pass only by coincidence.
        cna_vulkan_test(cna_test_vulkan_depthstencilstate_stencil_mask
                        ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_depthstencilstate_stencil_mask_test.cpp)
        cna_register_renderer_test(NAME Vulkan_DepthStencilState_StencilMask COMMAND cna_test_vulkan_depthstencilstate_stencil_mask
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 317: DepthStencilState front-face stencil operations on Vulkan (reuses the
        # renderer-agnostic EasyGL source). NOTE: expected to FAIL all 3 checks per Task 870 -
        # stencil testing never gates on Vulkan, so no operation ever fires.
        cna_vulkan_test(cna_test_vulkan_depthstencilstate_stencil_ops
                        ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_depthstencilstate_stencil_ops_test.cpp)
        cna_register_renderer_test(NAME Vulkan_DepthStencilState_StencilOps COMMAND cna_test_vulkan_depthstencilstate_stencil_ops
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 318: DepthStencilState.TwoSidedStencilMode on Vulkan (reuses the renderer-agnostic
        # EasyGL source). NOTE: expected to FAIL the contrast check per Task 870 - stencil testing
        # never gates on Vulkan.
        cna_vulkan_test(cna_test_vulkan_depthstencilstate_stencil_twosided
                        ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_depthstencilstate_stencil_twosided_test.cpp)
        cna_register_renderer_test(NAME Vulkan_DepthStencilState_StencilTwoSided COMMAND cna_test_vulkan_depthstencilstate_stencil_twosided
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 319: GraphicsDevice.ReferenceStencil independent-override behavior on Vulkan (Task
        # 872, universal gap, reuses the renderer-agnostic EasyGL source).
        cna_vulkan_test(cna_test_vulkan_graphicsdevice_reference_stencil
                        ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_graphicsdevice_reference_stencil_test.cpp)
        cna_register_renderer_test(NAME Vulkan_GraphicsDevice_ReferenceStencil COMMAND cna_test_vulkan_graphicsdevice_reference_stencil
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 871: GraphicsDevice::Clear() stencil-clearing fix is in SHARED code
        # (GraphicsDevice.cpp) -- verbatim reuse of the EasyGL source.
        cna_vulkan_test(cna_test_vulkan_graphicsdevice_clear_stencil
                        ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_graphicsdevice_clear_stencil_test.cpp)
        cna_register_renderer_test(NAME Vulkan_GraphicsDevice_ClearStencil COMMAND cna_test_vulkan_graphicsdevice_clear_stencil
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 950: VulkanRenderer::ClearColorAndDepth/ClearColorDepthAndStencil now
        # actually thread their `depth` parameter into VkClearValue.depthStencil.depth instead of
        # hardcoding 1.0f -- verbatim reuse of the shared renderer-agnostic source.
        cna_vulkan_test(cna_test_vulkan_graphicsdevice_clear_depth
                        ${CNA_GRAPHICS_EXAMPLES_DIR}/graphicsdevice_clear_depth_test.cpp)
        cna_register_renderer_test(NAME Vulkan_GraphicsDevice_ClearDepth COMMAND cna_test_vulkan_graphicsdevice_clear_depth
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-129: the full public GraphicsDevice.Clear/ClearOptions contract, reusing
        # REMED-GFX-018's renderer-agnostic source verbatim. Vulkan delivered a clear ONLY through
        # the render pass load op, so every one of the eight masks collapsed to "whatever the pass
        # load action happens to be" -- a depth-only or stencil-only clear did nothing at all, a
        # clear on a PreserveContents target was dropped entirely, and a clear issued after a draw
        # ran before it. This suite establishes distinct pre-existing colour/depth/stencil values
        # first and encodes the survivors into colour probes, so it can tell the three aspects
        # apart instead of asserting one pixel.
        cna_vulkan_test(cna_test_vulkan_graphicsdevice_clearoptions
                        ${CMAKE_SOURCE_DIR}/modules/renderers/bgfx/examples/bgfx_graphicsdevice_clearoptions_test.cpp)
        cna_register_renderer_test(NAME Vulkan_GraphicsDevice_ClearOptions COMMAND cna_test_vulkan_graphicsdevice_clearoptions
            TIMEOUT 90 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-129: Clear() is an ORDERED command at its exact public call position, not a
        # hint for the next render-pass load action. Every check queues its whole public sequence
        # and reads once at the end, with no GetData, Present, flush or extra frame in between, so
        # a renderer cannot pass by being forced to settle between two commands.
        cna_vulkan_test(cna_test_vulkan_ordered_clear
                        ${CNA_GRAPHICS_EXAMPLES_DIR}/graphicsdevice_ordered_clear_test.cpp)
        cna_register_renderer_test(NAME Vulkan_GraphicsDevice_OrderedClear COMMAND cna_test_vulkan_ordered_clear
            TIMEOUT 120 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-089: renderer-neutral public GraphicsDevice/DepthStencilState
        # A(Default)->B(DepthRead)->C(None)->A near/far contract, shared with D3D9/D3D11/Software.
        cna_vulkan_test(cna_test_vulkan_depth_contract
                        ${CNA_GRAPHICS_EXAMPLES_DIR}/graphicsdevice_depth_contract_test.cpp)
        cna_register_renderer_test(NAME Vulkan_GraphicsDevice_DepthContract
            COMMAND cna_test_vulkan_depth_contract
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 955: a freshly-constructed GraphicsDevice's own default state (no game code ever
        # explicitly setting BlendState/DepthStencilState/RasterizerState, exactly like
        # ../cna-samples SimpleAnimation's Tank.hpp) must produce correct opaque depth occlusion.
        cna_vulkan_test(cna_test_vulkan_graphicsdevice_default_state_occlusion
                        ${CNA_GRAPHICS_EXAMPLES_DIR}/graphicsdevice_default_state_occlusion_test.cpp)
        cna_register_renderer_test(NAME Vulkan_GraphicsDevice_DefaultStateOcclusion COMMAND cna_test_vulkan_graphicsdevice_default_state_occlusion
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 889: DualTextureEffect.VertexColorEnabled -- verbatim reuse of the shared
        # renderer-agnostic source, registered on all 3 renderers.
        cna_vulkan_test(cna_test_vulkan_dualtextureeffect_vertexcolor
                        ${CNA_GRAPHICS_EXAMPLES_DIR}/dualtextureeffect_vertexcolor_test.cpp)
        cna_register_renderer_test(NAME Vulkan_DualTextureEffect_VertexColor COMMAND cna_test_vulkan_dualtextureeffect_vertexcolor
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 323 (+ 324/325 coverage): RasterizerState.CullMode on Vulkan (reuses the
        # renderer-agnostic EasyGL source).
        cna_vulkan_test(cna_test_vulkan_rasterizerstate_cullmode
                        ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_rasterizerstate_cullmode_test.cpp)
        cna_register_renderer_test(NAME Vulkan_RasterizerState_CullMode COMMAND cna_test_vulkan_rasterizerstate_cullmode
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # XNA culling-compatibility audit: real-camera CullMode reproducer, verbatim reuse of the
        # shared renderer-agnostic source.
        cna_vulkan_test(cna_test_vulkan_rasterizerstate_cullmode_camera
                        ${CNA_GRAPHICS_EXAMPLES_DIR}/rasterizerstate_cullmode_camera_test.cpp)
        cna_register_renderer_test(NAME Vulkan_RasterizerState_CullMode_Camera COMMAND cna_test_vulkan_rasterizerstate_cullmode_camera
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_vulkan_test(cna_test_vulkan_rasterizerstate_cullmode_indexed_basiceffect
                        ${CNA_GRAPHICS_EXAMPLES_DIR}/rasterizerstate_cullmode_indexed_basiceffect_test.cpp)
        cna_register_renderer_test(NAME Vulkan_RasterizerState_CullMode_IndexedBasicEffect COMMAND cna_test_vulkan_rasterizerstate_cullmode_indexed_basiceffect
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 136: EnvironmentMapEffect — emissive=(1,0,0), envAmount=0 → red pixel on Vulkan
        cna_vulkan_test(cna_test_vulkan_env_map
                        vulkan_env_map_test.cpp)
        cna_register_renderer_test(NAME Vulkan_EnvironmentMapEffect_Readback COMMAND cna_test_vulkan_env_map
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 393: EnvironmentMapEffect EnvironmentMapAmount=0 should ignore the cube map
        cna_vulkan_test(cna_test_vulkan_environmentmapeffect_amount_zero
                        vulkan_environmentmapeffect_amount_zero_test.cpp)
        cna_register_renderer_test(NAME Vulkan_EnvironmentMapEffect_AmountZero COMMAND cna_test_vulkan_environmentmapeffect_amount_zero
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 890: EnvironmentMapEffect DirectionalLight1/DirectionalLight2 forwarding
        cna_vulkan_test(cna_test_vulkan_environmentmapeffect_multilight
                        vulkan_environmentmapeffect_multilight_test.cpp)
        cna_register_renderer_test(NAME Vulkan_EnvironmentMapEffect_MultiLight COMMAND cna_test_vulkan_environmentmapeffect_multilight
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 394: EnvironmentMapEffect EnvironmentMapAmount=1 should fully replace lit color
        cna_vulkan_test(cna_test_vulkan_environmentmapeffect_amount_one
                        vulkan_environmentmapeffect_amount_one_test.cpp)
        cna_register_renderer_test(NAME Vulkan_EnvironmentMapEffect_AmountOne COMMAND cna_test_vulkan_environmentmapeffect_amount_one
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 395: EnvironmentMapEffect EnvironmentMapSpecular should scale by cubemap alpha
        cna_vulkan_test(cna_test_vulkan_environmentmapeffect_specular
                        vulkan_environmentmapeffect_specular_test.cpp)
        cna_register_renderer_test(NAME Vulkan_EnvironmentMapEffect_Specular COMMAND cna_test_vulkan_environmentmapeffect_specular
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 891: EnvironmentMapEffect's base cube-map lerp target must be scaled by the
        # combined texture x diffuse alpha -- verbatim reuse of the shared renderer-agnostic
        # source, registered on all 3 renderers.
        cna_vulkan_test(cna_test_vulkan_environmentmapeffect_alphascaledlerp
                        ${CNA_GRAPHICS_EXAMPLES_DIR}/environmentmapeffect_alphascaledlerp_test.cpp)
        cna_register_renderer_test(NAME Vulkan_EnvironmentMapEffect_AlphaScaledLerp COMMAND cna_test_vulkan_environmentmapeffect_alphascaledlerp
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 396: EnvironmentMapEffect Fresnel edge-weighting should suppress head-on reflections
        cna_vulkan_test(cna_test_vulkan_environmentmapeffect_fresnel
                        vulkan_environmentmapeffect_fresnel_test.cpp)
        cna_register_renderer_test(NAME Vulkan_EnvironmentMapEffect_Fresnel COMMAND cna_test_vulkan_environmentmapeffect_fresnel
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 397: EnvironmentMapEffect reflection vector should respond to EyePosition
        cna_vulkan_test(cna_test_vulkan_environmentmapeffect_eyeposition
                        vulkan_environmentmapeffect_eyeposition_test.cpp)
        cna_register_renderer_test(NAME Vulkan_EnvironmentMapEffect_EyePosition COMMAND cna_test_vulkan_environmentmapeffect_eyeposition
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 398: EnvironmentMapEffect normal transform should use inverse-transpose of World
        cna_vulkan_test(cna_test_vulkan_environmentmapeffect_worldtransform
                        vulkan_environmentmapeffect_worldtransform_test.cpp)
        cna_register_renderer_test(NAME Vulkan_EnvironmentMapEffect_WorldTransform COMMAND cna_test_vulkan_environmentmapeffect_worldtransform
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 399: cross-renderer EnvironmentMapEffect capstone (Tasks 393-398 combined)
        cna_vulkan_test(cna_test_vulkan_environmentmapeffect_combined
                        vulkan_environmentmapeffect_combined_test.cpp)
        cna_register_renderer_test(NAME Vulkan_EnvironmentMapEffect_Combined COMMAND cna_test_vulkan_environmentmapeffect_combined
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 406: SkinnedEffect identity bone palette should produce zero deformation
        cna_vulkan_test(cna_test_vulkan_skinnedeffect_identity_bones
                        vulkan_skinnedeffect_identity_bones_test.cpp)
        cna_register_renderer_test(NAME Vulkan_SkinnedEffect_IdentityBones COMMAND cna_test_vulkan_skinnedeffect_identity_bones
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 407: SkinnedEffect single translation bone should shift the mesh
        cna_vulkan_test(cna_test_vulkan_skinnedeffect_translation_bone
                        vulkan_skinnedeffect_translation_bone_test.cpp)
        cna_register_renderer_test(NAME Vulkan_SkinnedEffect_TranslationBone COMMAND cna_test_vulkan_skinnedeffect_translation_bone
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 408: SkinnedEffect two-bone weighted blend should produce midpoint deformation
        cna_vulkan_test(cna_test_vulkan_skinnedeffect_twobone_blend
                        vulkan_skinnedeffect_twobone_blend_test.cpp)
        cna_register_renderer_test(NAME Vulkan_SkinnedEffect_TwoBoneBlend COMMAND cna_test_vulkan_skinnedeffect_twobone_blend
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 409: cross-renderer SkinnedEffect capstone (identity + single-bone + two-bone blend)
        cna_vulkan_test(cna_test_vulkan_skinnedeffect_combined
                        vulkan_skinnedeffect_combined_test.cpp)
        cna_register_renderer_test(NAME Vulkan_SkinnedEffect_Combined COMMAND cna_test_vulkan_skinnedeffect_combined
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 334: RenderTargetCube sampled as TextureCube via EnvironmentMapEffect after unbinding
        cna_vulkan_test(cna_test_vulkan_rendertargetcube_sample
                        vulkan_rendertargetcube_sample_test.cpp)
        cna_register_renderer_test(NAME Vulkan_RenderTargetCube_SampleAfterUnbind COMMAND cna_test_vulkan_rendertargetcube_sample
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 907: RenderTargetCube mip chains are genuinely generated per-face
        # (vkCmdBlitImage cascade), split out of Task 878's original combined scope.
        cna_vulkan_test(cna_test_vulkan_rendertargetcube_mip
                        vulkan_rendertargetcube_mip_test.cpp)
        cna_register_renderer_test(NAME Vulkan_RenderTargetCube_MipChain COMMAND cna_test_vulkan_rendertargetcube_mip
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 903: RenderTargetCube MSAA is genuinely generated per-face (shared MSAA colour
        # image + promoted MSAA depth + 3-attachment render pass), split out of Task 878/879's
        # original RenderTarget2D-only scope.
        cna_vulkan_test(cna_test_vulkan_rendertargetcube_msaa
                        vulkan_rendertargetcube_msaa_test.cpp)
        cna_register_renderer_test(NAME Vulkan_RenderTargetCube_MsaaResolve COMMAND cna_test_vulkan_rendertargetcube_msaa
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-096: public singular/plural cube-face binding parity.
        cna_vulkan_test(cna_test_vulkan_rendertargetcube_plural_binding
                        ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertargetcube_plural_binding_test.cpp)
        cna_register_renderer_test(NAME Vulkan_RenderTargetCube_PluralBinding
            COMMAND cna_test_vulkan_rendertargetcube_plural_binding
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-096: genuine cube/2D and cube/cube MRT, including selected-face
        # framebuffer identity and per-face MSAA resolves.
        cna_vulkan_test(cna_test_vulkan_cube_mrt_binding
                        vulkan_cube_mrt_binding_test.cpp)
        cna_register_renderer_test(NAME Vulkan_RenderTargetCube_PluralMRT
            COMMAND cna_test_vulkan_cube_mrt_binding
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 148: RenderTarget2D full cycle — red quad into RT, blit to backbuffer
        cna_vulkan_test(cna_test_vulkan_rt2d
                        vulkan_rt2d_test.cpp)
        cna_register_renderer_test(NAME Vulkan_RenderTarget2D_FullCycle COMMAND cna_test_vulkan_rt2d
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 875: Clear()-only render target (no draw call) actually records a render pass
        cna_vulkan_test(cna_test_vulkan_rt_roundtrip
                        vulkan_rt_roundtrip_test.cpp)
        cna_register_renderer_test(NAME Vulkan_RenderTarget2D_ClearOnlyRoundtrip COMMAND cna_test_vulkan_rt_roundtrip
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 335: RenderTarget2D depth buffer is real and functional (not just a stored property)
        cna_vulkan_test(cna_test_vulkan_rendertarget2d_depth
                        ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget2d_depth_test.cpp)
        cna_register_renderer_test(NAME Vulkan_RenderTarget2D_DepthBuffer COMMAND cna_test_vulkan_rendertarget2d_depth
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 865: Texture3D::GetData is now real on Vulkan -- shared with EasyGL's registration
        # of the same renderer-agnostic file (Task 173).
        cna_vulkan_test(cna_test_vulkan_texture3d_slices
                        ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_texture3d_slices_test.cpp)
        cna_register_renderer_test(NAME Vulkan_Texture3D_Slices_RoundTrip COMMAND cna_test_vulkan_texture3d_slices
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 865: TextureCube::GetData is now real on Vulkan -- shared with EasyGL's
        # registration of the same renderer-agnostic file (Task 275).
        cna_vulkan_test(cna_test_vulkan_texturecube_partial_rect
                        ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_texturecube_partial_rect_test.cpp)
        cna_register_renderer_test(NAME Vulkan_TextureCube_PartialRect_RoundTrip COMMAND cna_test_vulkan_texturecube_partial_rect
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 864: Texture3D mip-level allocation is now real on Vulkan -- shared with EasyGL's
        # registration of the same renderer-agnostic file (Task 862).
        cna_vulkan_test(cna_test_vulkan_texture3d_mip
                        ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_texture3d_mip_test.cpp)
        cna_register_renderer_test(NAME Vulkan_Texture3D_Mip_RoundTrip COMMAND cna_test_vulkan_texture3d_mip
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-093: level-scoped SetData/GetData state cycles.  Pixel/data checks alone pass
        # on the defective implementation, so make the exact copy-layout validation failure fatal.
        cna_vulkan_test(cna_test_vulkan_texture3d_mip_layout
                        vulkan_texture3d_mip_layout_test.cpp)
        cna_register_renderer_test(NAME Vulkan_Texture3D_Mip_Layout COMMAND cna_test_vulkan_texture3d_mip_layout
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
        set_tests_properties(Vulkan_Texture3D_Mip_Layout PROPERTIES
            FAIL_REGULAR_EXPRESSION "VUID-vkCmdDraw-None-09600")

        # Task 864: TextureCube mip-level allocation is now real on Vulkan -- shared with EasyGL's
        # registration of the same renderer-agnostic file (Task 276).
        cna_vulkan_test(cna_test_vulkan_texturecube_mip
                        ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_texturecube_mip_test.cpp)
        cna_register_renderer_test(NAME Vulkan_TextureCube_Mip_RoundTrip COMMAND cna_test_vulkan_texturecube_mip
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 178: RenderTargetUsage DiscardContents vs PreserveContents
        cna_vulkan_test(cna_test_vulkan_render_target_usage
                        vulkan_render_target_usage_test.cpp)
        cna_register_renderer_test(NAME Vulkan_RenderTargetUsage COMMAND cna_test_vulkan_render_target_usage
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 142: RenderTargetCube — 6-face per-face render passes
        cna_vulkan_test(cna_test_vulkan_rtcube
                        vulkan_rtcube_test.cpp)
        cna_register_renderer_test(NAME Vulkan_RenderTargetCube_PerFace COMMAND cna_test_vulkan_rtcube
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 147: MSAA 4× — multisampled swapchain + subpass resolve
        cna_vulkan_test(cna_test_vulkan_msaa
                        vulkan_msaa_test.cpp)
        cna_register_renderer_test(NAME Vulkan_MSAA_4x_Readback COMMAND cna_test_vulkan_msaa
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_vulkan_test(cna_test_vulkan_vertex_format
                        vulkan_vertex_format_test.cpp)
        cna_register_renderer_test(NAME Vulkan_VertexFormat_AllStrides COMMAND cna_test_vulkan_vertex_format
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_vulkan_test(cna_test_vulkan_fill_mode
                        vulkan_fill_mode_test.cpp)
        cna_register_renderer_test(NAME Vulkan_FillMode_WireFrame COMMAND cna_test_vulkan_fill_mode
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_vulkan_test(cna_test_vulkan_depth_bias
                        vulkan_depth_bias_test.cpp)
        cna_register_renderer_test(NAME Vulkan_DepthBias COMMAND cna_test_vulkan_depth_bias
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 329: ScissorTestEnable + ScissorRectangle pixel-readback test
        cna_vulkan_test(cna_test_vulkan_scissor
                        vulkan_scissor_test.cpp)
        cna_register_renderer_test(NAME Vulkan_ScissorTest COMMAND cna_test_vulkan_scissor
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-081: SpriteBatch.Begin must apply its RasterizerState argument (shared fix in
        # SpriteBatch.cpp). Deferred-renderer control: the scissor state must reach the BatchSnapshot
        # through Begin, not only via a direct GraphicsDevice.RasterizerState assignment.
        cna_vulkan_test(cna_test_vulkan_spritebatch_begin_rasterizerstate
                        ${CNA_GRAPHICS_EXAMPLES_DIR}/spritebatch_begin_rasterizerstate_scissor_test.cpp)
        cna_register_renderer_test(NAME Vulkan_SpriteBatch_BeginRasterizerState COMMAND cna_test_vulkan_spritebatch_begin_rasterizerstate
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-013: ScissorRectangle must clip draws issued while a RenderTarget2D is bound
        cna_vulkan_test(cna_test_vulkan_rendertarget_scissor
                        vulkan_rendertarget_scissor_test.cpp)
        cna_register_renderer_test(NAME Vulkan_RenderTarget_Scissor COMMAND cna_test_vulkan_rendertarget_scissor
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-062: Viewport must be honored for draws issued while a RenderTarget2D is bound
        cna_vulkan_test(cna_test_vulkan_rendertarget_viewport
                        vulkan_rendertarget_viewport_test.cpp)
        cna_register_renderer_test(NAME Vulkan_RenderTarget_Viewport COMMAND cna_test_vulkan_rendertarget_viewport
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-070: GraphicsDevice.BlendFactor (the constant blend color) must be applied per
        # draw/batch, not once per frame — RT passes must set it, and multiple values in one frame
        # must not collapse to last-wins.
        cna_vulkan_test(cna_test_vulkan_rendertarget_blendfactor
                        vulkan_rendertarget_blendfactor_test.cpp)
        cna_register_renderer_test(NAME Vulkan_RenderTarget_BlendFactor COMMAND cna_test_vulkan_rendertarget_blendfactor
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-071: the Vulkan 2D SpriteBatch pipeline must honor the SpriteBatch.Begin()
        # BlendState (colour/alpha factors + function + BlendFactor), not one hardcoded alpha-blend
        # equation. Opaque/AlphaBlend(premultiplied)/NonPremultiplied/Additive/custom factors/
        # custom function/separate alpha/Blend::BlendFactor into a RenderTarget2D, read back with
        # GetData (GFX-074). Pre-fix all colored scenes read the hardcoded (130,80,110,128); the fix
        # plumbs BlendKeyParams into the 2D pipeline cache key + FillBlendAttachmentState (the 3D
        # path's proven translation). The GFX-070 per-batch blend-constant capture becomes live here.
        cna_vulkan_test(cna_test_vulkan_spritebatch_blendstate
                        vulkan_spritebatch_blendstate_test.cpp)
        cna_register_renderer_test(NAME Vulkan_SpriteBatch_BlendState COMMAND cna_test_vulkan_spritebatch_blendstate
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-077: BlendState.ColorWriteChannels (per-attachment write mask) + MultiSampleMask
        # (pSampleMask) via the extended pipeline cache key.
        cna_vulkan_test(cna_test_vulkan_colorwritechannels
                        vulkan_colorwritechannels_test.cpp)
        cna_register_renderer_test(NAME Vulkan_ColorWriteChannels COMMAND cna_test_vulkan_colorwritechannels
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-095: real multi-output MRT + MSAA render-pass/framebuffer/pipeline/resolve
        # correctness, including direct sample-count diagnostics and partial-sample-mask evidence.
        cna_vulkan_test(cna_test_vulkan_mrt_msaa
                        vulkan_mrt_msaa_test.cpp)
        cna_register_renderer_test(NAME Vulkan_MRT_MsaaResolve COMMAND cna_test_vulkan_mrt_msaa
            TIMEOUT 120 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 664: SpriteBatch multiple Begin()/End() cycles per frame must all render
        cna_vulkan_test(cna_test_vulkan_spritebatch_multi_begin_end
                        vulkan_spritebatch_multi_begin_end_test.cpp)
        cna_register_renderer_test(NAME Vulkan_SpriteBatch_MultiBeginEnd COMMAND cna_test_vulkan_spritebatch_multi_begin_end
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-012: SpriteBatch Begin()'s transformMatrix must be applied (was a silent no-op)
        cna_vulkan_test(cna_test_vulkan_transform_matrix
                        vulkan_transform_matrix_test.cpp)
        cna_register_renderer_test(NAME Vulkan_SpriteBatch_TransformMatrix COMMAND cna_test_vulkan_transform_matrix
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 665: SpriteBatch Begin()'s SamplerState (TextureAddressMode) must take effect
        cna_vulkan_test(cna_test_vulkan_texture_address_mode
                        vulkan_texture_address_mode_test.cpp)
        cna_register_renderer_test(NAME Vulkan_TextureAddressMode COMMAND cna_test_vulkan_texture_address_mode
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 338: SetRenderTarget(nullptr) resets Viewport/ScissorRectangle to the backbuffer
        cna_vulkan_test(cna_test_vulkan_rendertarget_viewport_scissor_reset
                        ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_viewport_scissor_reset_test.cpp)
        cna_register_renderer_test(NAME Vulkan_RenderTarget_ViewportScissorReset COMMAND cna_test_vulkan_rendertarget_viewport_scissor_reset
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 349: custom Viewport survives Present() with no resize, resets after a real one.
        cna_vulkan_test(cna_test_vulkan_viewport_reset_after_resize
                        ${CNA_GRAPHICS_EXAMPLES_DIR}/viewport_reset_after_resize_test.cpp)
        cna_register_renderer_test(NAME Vulkan_ViewportResetAfterResize COMMAND cna_test_vulkan_viewport_reset_after_resize
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 331: RenderTarget2D constructor/property audit against FNA.
        cna_vulkan_test(cna_test_vulkan_rendertarget2d_properties
                        ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_rendertarget2d_properties_test.cpp)
        cna_register_renderer_test(NAME Vulkan_RenderTarget2D_Properties COMMAND cna_test_vulkan_rendertarget2d_properties
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 332: RenderTargetCube constructor/property audit against FNA.
        cna_vulkan_test(cna_test_vulkan_rendertargetcube_properties
                        ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_rendertargetcube_properties_test.cpp)
        cna_register_renderer_test(NAME Vulkan_RenderTargetCube_Properties COMMAND cna_test_vulkan_rendertargetcube_properties
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 364: BasicEffect pixel test — VertexColorEnabled=false, no texture, diffuse color only
        cna_vulkan_test(cna_test_vulkan_basiceffect_vertexcolor_disabled
                        vulkan_basiceffect_vertexcolor_disabled_test.cpp)
        cna_register_renderer_test(NAME Vulkan_BasicEffect_VertexColorDisabled COMMAND cna_test_vulkan_basiceffect_vertexcolor_disabled
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 365: BasicEffect pixel test — VertexColorEnabled=true, no texture, vertex color multiplication
        cna_vulkan_test(cna_test_vulkan_basiceffect_vertexcolor_enabled
                        vulkan_basiceffect_vertexcolor_enabled_test.cpp)
        cna_register_renderer_test(NAME Vulkan_BasicEffect_VertexColorEnabled COMMAND cna_test_vulkan_basiceffect_vertexcolor_enabled
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 366: BasicEffect pixel test — TextureEnabled=true, no vertex color
        cna_vulkan_test(cna_test_vulkan_basiceffect_texture_enabled
                        vulkan_basiceffect_texture_enabled_test.cpp)
        cna_register_renderer_test(NAME Vulkan_BasicEffect_TextureEnabled COMMAND cna_test_vulkan_basiceffect_texture_enabled
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 904: GetOrCreatePipelineFogTex3D's missing msaa-aware render-pass check.
        cna_vulkan_test(cna_test_vulkan_basiceffect_textured_msaa
                        vulkan_basiceffect_textured_msaa_test.cpp)
        cna_register_renderer_test(NAME Vulkan_BasicEffect_TexturedMsaa COMMAND cna_test_vulkan_basiceffect_textured_msaa
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 367: BasicEffect pixel test — TextureEnabled=true AND VertexColorEnabled=true
        cna_vulkan_test(cna_test_vulkan_basiceffect_texture_vertexcolor_enabled
                        vulkan_basiceffect_texture_vertexcolor_enabled_test.cpp)
        cna_register_renderer_test(NAME Vulkan_BasicEffect_TextureVertexColorEnabled COMMAND cna_test_vulkan_basiceffect_texture_vertexcolor_enabled
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 368: BasicEffect pixel test — LightingEnabled=true, one DirectionalLight
        cna_vulkan_test(cna_test_vulkan_basiceffect_one_light
                        vulkan_basiceffect_one_light_test.cpp)
        cna_register_renderer_test(NAME Vulkan_BasicEffect_OneLight COMMAND cna_test_vulkan_basiceffect_one_light
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 897: BasicEffect pixel test — DirectionalLight1/2 + EmissiveColor on the lit path
        cna_vulkan_test(cna_test_vulkan_basiceffect_multilight_emissive
                        vulkan_basiceffect_multilight_emissive_test.cpp)
        cna_register_renderer_test(NAME Vulkan_BasicEffect_MultiLightEmissive COMMAND cna_test_vulkan_basiceffect_multilight_emissive
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 886: BasicEffect pixel test — real specular highlights
        cna_vulkan_test(cna_test_vulkan_basiceffect_specular
                        vulkan_basiceffect_specular_test.cpp)
        cna_register_renderer_test(NAME Vulkan_BasicEffect_Specular COMMAND cna_test_vulkan_basiceffect_specular
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 1103: BasicEffect pixel test — PreferPerPixelLighting genuinely selects between a
        # real per-vertex-lit shader variant and the existing per-pixel-lit one.
        cna_vulkan_test(cna_test_vulkan_basiceffect_preferperpixellighting
                        vulkan_basiceffect_preferperpixellighting_test.cpp)
        cna_register_renderer_test(NAME Vulkan_BasicEffect_PreferPerPixelLighting COMMAND cna_test_vulkan_basiceffect_preferperpixellighting
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 887: AlphaTestEffect.VertexColorEnabled fix verification
        cna_vulkan_test(cna_test_vulkan_alphatest_vertexcolor
                        vulkan_alphatest_vertexcolor_test.cpp)
        cna_register_renderer_test(NAME Vulkan_AlphaTest_VertexColor COMMAND cna_test_vulkan_alphatest_vertexcolor
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 888: BasicEffect fog on the lit_textured3d (stride-32) pipeline
        cna_vulkan_test(cna_test_vulkan_basiceffect_fog
                        vulkan_basiceffect_fog_test.cpp)
        cna_register_renderer_test(NAME Vulkan_BasicEffect_Fog COMMAND cna_test_vulkan_basiceffect_fog
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 888: AlphaTestEffect fog on the alpha_test3d pipeline
        cna_vulkan_test(cna_test_vulkan_alphatest_fog
                        vulkan_alphatest_fog_test.cpp)
        cna_register_renderer_test(NAME Vulkan_AlphaTest_Fog COMMAND cna_test_vulkan_alphatest_fog
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 369: BasicEffect pixel test — DiffuseColor+EmissiveColor, LightingEnabled=false
        cna_vulkan_test(cna_test_vulkan_basiceffect_emissive
                        vulkan_basiceffect_emissive_test.cpp)
        cna_register_renderer_test(NAME Vulkan_BasicEffect_Emissive COMMAND cna_test_vulkan_basiceffect_emissive
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 370: BasicEffect cross-renderer image comparison suite — closes Phase 42
        cna_vulkan_test(cna_test_vulkan_basiceffect_combined
                        vulkan_basiceffect_combined_test.cpp)
        cna_register_renderer_test(NAME Vulkan_BasicEffect_Combined COMMAND cna_test_vulkan_basiceffect_combined
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 374: AlphaTestEffect all CompareFunction modes — threshold sweep
        cna_vulkan_test(cna_test_vulkan_alphatest_comparefunction_sweep
                        vulkan_alphatest_comparefunction_sweep_test.cpp)
        cna_register_renderer_test(NAME Vulkan_AlphaTest_CompareFunctionSweep COMMAND cna_test_vulkan_alphatest_comparefunction_sweep
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 379: AlphaTestEffect null/no-texture behavior
        cna_vulkan_test(cna_test_vulkan_alphatest_null_texture
                        vulkan_alphatest_null_texture_test.cpp)
        cna_register_renderer_test(NAME Vulkan_AlphaTest_NullTexture COMMAND cna_test_vulkan_alphatest_null_texture
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 383: DualTextureEffect two-texture blend formula, including FNA's color.rgb*=2
        cna_vulkan_test(cna_test_vulkan_dualtextureeffect_doubling
                        vulkan_dualtextureeffect_doubling_test.cpp)
        cna_register_renderer_test(NAME Vulkan_DualTextureEffect_Doubling COMMAND cna_test_vulkan_dualtextureeffect_doubling
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 385: DualTextureEffect Alpha forwarding, alpha-channel-only (see file header for
        # why the full RGB-premultiplication check is deliberately not done on Vulkan)
        cna_vulkan_test(cna_test_vulkan_dualtextureeffect_alpha
                        vulkan_dualtextureeffect_alpha_test.cpp)
        cna_register_renderer_test(NAME Vulkan_DualTextureEffect_Alpha COMMAND cna_test_vulkan_dualtextureeffect_alpha
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 386: DualTextureEffect first texture (Texture, slot 0) null behavior
        cna_vulkan_test(cna_test_vulkan_dualtextureeffect_null_texture0
                        vulkan_dualtextureeffect_null_texture0_test.cpp)
        cna_register_renderer_test(NAME Vulkan_DualTextureEffect_NullTexture0 COMMAND cna_test_vulkan_dualtextureeffect_null_texture0
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 387: DualTextureEffect second texture (Texture2, slot 1) null behavior
        cna_vulkan_test(cna_test_vulkan_dualtextureeffect_null_texture2
                        vulkan_dualtextureeffect_null_texture2_test.cpp)
        cna_register_renderer_test(NAME Vulkan_DualTextureEffect_NullTexture2 COMMAND cna_test_vulkan_dualtextureeffect_null_texture2
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 389: DualTextureEffect cross-renderer image comparison suite (closes Phase 44 pixel work)
        cna_vulkan_test(cna_test_vulkan_dualtextureeffect_combined
                        vulkan_dualtextureeffect_combined_test.cpp)
        cna_register_renderer_test(NAME Vulkan_DualTextureEffect_Combined COMMAND cna_test_vulkan_dualtextureeffect_combined
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 880: GraphicsDevice.Viewport GPU wiring — sub-region viewport pixel-readback test
        cna_vulkan_test(cna_test_vulkan_viewport_subregion
                        vulkan_viewport_subregion_test.cpp)
        cna_register_renderer_test(NAME Vulkan_Viewport_Subregion COMMAND cna_test_vulkan_viewport_subregion
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 899: fog on colored3d/textured3d/colored_textured3d/dual_texture3d/skinned3d
        cna_vulkan_test(cna_test_vulkan_basiceffect_colored3d_fog
                        vulkan_basiceffect_colored3d_fog_test.cpp)
        cna_register_renderer_test(NAME Vulkan_BasicEffect_Colored3D_Fog COMMAND cna_test_vulkan_basiceffect_colored3d_fog
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_vulkan_test(cna_test_vulkan_basiceffect_textured3d_fog
                        vulkan_basiceffect_textured3d_fog_test.cpp)
        cna_register_renderer_test(NAME Vulkan_BasicEffect_Textured3D_Fog COMMAND cna_test_vulkan_basiceffect_textured3d_fog
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_vulkan_test(cna_test_vulkan_basiceffect_coloredtextured3d_fog
                        vulkan_basiceffect_coloredtextured3d_fog_test.cpp)
        cna_register_renderer_test(NAME Vulkan_BasicEffect_ColoredTextured3D_Fog COMMAND cna_test_vulkan_basiceffect_coloredtextured3d_fog
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_vulkan_test(cna_test_vulkan_dualtextureeffect_fog
                        vulkan_dualtextureeffect_fog_test.cpp)
        cna_register_renderer_test(NAME Vulkan_DualTextureEffect_Fog COMMAND cna_test_vulkan_dualtextureeffect_fog
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_vulkan_test(cna_test_vulkan_skinnedeffect_fog
                        vulkan_skinnedeffect_fog_test.cpp)
        cna_register_renderer_test(NAME Vulkan_SkinnedEffect_Fog COMMAND cna_test_vulkan_skinnedeffect_fog
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-010: transformed-camera view-space fog conformance (fails pre-fix).
        cna_vulkan_test(cna_test_vulkan_viewspace_fog
                        vulkan_viewspace_fog_test.cpp)
        cna_register_renderer_test(NAME Vulkan_ViewSpace_Fog COMMAND cna_test_vulkan_viewspace_fog
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 893: SkinnedEffect DirectionalLight1/DirectionalLight2 forwarding
        cna_vulkan_test(cna_test_vulkan_skinnedeffect_multilight
                        vulkan_skinnedeffect_multilight_test.cpp)
        cna_register_renderer_test(NAME Vulkan_SkinnedEffect_MultiLight COMMAND cna_test_vulkan_skinnedeffect_multilight
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 894: SkinnedEffect real specular highlights
        cna_vulkan_test(cna_test_vulkan_skinnedeffect_specular
                        vulkan_skinnedeffect_specular_test.cpp)
        cna_register_renderer_test(NAME Vulkan_SkinnedEffect_Specular COMMAND cna_test_vulkan_skinnedeffect_specular
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-008: analytic SkinnedEffect ambient/emissive lighting conformance
        cna_vulkan_test(cna_test_vulkan_skinnedeffect_lighting_conformance
                        ${CNA_GRAPHICS_EXAMPLES_DIR}/skinnedeffect_lighting_conformance_test.cpp)
        cna_register_renderer_test(NAME Vulkan_SkinnedEffect_LightingConformance COMMAND cna_test_vulkan_skinnedeffect_lighting_conformance
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 1103: SkinnedEffect pixel test — PreferPerPixelLighting genuinely selects between a
        # real per-vertex-lit shader variant and the existing per-pixel-lit one.
        cna_vulkan_test(cna_test_vulkan_skinnedeffect_preferperpixellighting
                        vulkan_skinnedeffect_preferperpixellighting_test.cpp)
        cna_register_renderer_test(NAME Vulkan_SkinnedEffect_PreferPerPixelLighting COMMAND cna_test_vulkan_skinnedeffect_preferperpixellighting
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 895: SkinnedEffect WeightsPerVertex real GPU enforcement
        cna_vulkan_test(cna_test_vulkan_skinnedeffect_weightspervertex
                        vulkan_skinnedeffect_weightspervertex_test.cpp)
        cna_register_renderer_test(NAME Vulkan_SkinnedEffect_WeightsPerVertex COMMAND cna_test_vulkan_skinnedeffect_weightspervertex
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 899's own noted cheap leftover: env_map3d fog packed into EnvMapParams' spare bytes
        cna_vulkan_test(cna_test_vulkan_environmentmapeffect_fog
                        vulkan_environmentmapeffect_fog_test.cpp)
        cna_register_renderer_test(NAME Vulkan_EnvironmentMapEffect_Fog COMMAND cna_test_vulkan_environmentmapeffect_fog
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 878/879: RenderTarget2D MSAA resolve genuinely anti-aliases on Vulkan (piggybacks
        # on the renderer's own backbuffer sampleCount_ -- requires PreferMultiSampling=true).
        cna_vulkan_test(cna_test_vulkan_rendertarget2d_msaa
                        vulkan_rendertarget2d_msaa_test.cpp)
        cna_register_renderer_test(NAME Vulkan_RenderTarget2D_MsaaResolve COMMAND cna_test_vulkan_rendertarget2d_msaa
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 878: RenderTarget2D mip chains are genuinely generated (vkCmdBlitImage cascade),
        # not just present-but-undefined storage.
        cna_vulkan_test(cna_test_vulkan_rendertarget2d_mip
                        vulkan_rendertarget2d_mip_test.cpp)
        cna_register_renderer_test(NAME Vulkan_RenderTarget2D_MipChain COMMAND cna_test_vulkan_rendertarget2d_mip
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 911: RenderTarget2D per-instance DepthStencilFormat fidelity -- DepthFormat::None
        # genuinely has no depth buffer, Depth24Stencil8/Depth16 genuinely do (each with its own
        # render pass), all three coexisting correctly in a single frame.
        cna_vulkan_test(cna_test_vulkan_rendertarget_depthformat_fidelity
                        vulkan_rendertarget_depthformat_fidelity_test.cpp)
        cna_register_renderer_test(NAME Vulkan_RenderTarget2D_DepthFormatFidelity COMMAND cna_test_vulkan_rendertarget_depthformat_fidelity
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 13.6: the same 3 avatar-rendering GPU integration tests already registered for
        # EasyGL above, reusing the exact same renderer-agnostic source files (public
        # GraphicsDevice/SkinnedModelEXT/AvatarRenderer API only) - Vulkan's own dedicated
        # GetOrCreatePipelineSkinned3D pipeline (Task 11.10) had never been run against real (or
        # even synthetic) avatar content before now.
        if(CNA_ENABLE_NET)
            cna_vulkan_test(cna_test_vulkan_avatar_real_render
                            ${CMAKE_SOURCE_DIR}/modules/gamer-services/examples/avatar_real_render_integration_test.cpp)
            target_link_libraries(cna_test_vulkan_avatar_real_render PRIVATE CNA_GamerServices)
            cna_register_renderer_test(NAME Vulkan_AvatarRenderer_RealRender COMMAND cna_test_vulkan_avatar_real_render
                TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

            cna_vulkan_test(cna_test_vulkan_avatar_attach_part
                            ${CMAKE_SOURCE_DIR}/modules/gamer-services/examples/avatar_attach_part_integration_test.cpp)
            target_link_libraries(cna_test_vulkan_avatar_attach_part PRIVATE CNA_GamerServices)
            cna_register_renderer_test(NAME Vulkan_AvatarRenderer_AttachPart COMMAND cna_test_vulkan_avatar_attach_part
                TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

            cna_vulkan_test(cna_test_vulkan_avatar_tint_routing
                            ${CMAKE_SOURCE_DIR}/modules/gamer-services/examples/avatar_tint_routing_integration_test.cpp)
            target_link_libraries(cna_test_vulkan_avatar_tint_routing PRIVATE CNA_GamerServices)
            cna_register_renderer_test(NAME Vulkan_AvatarRenderer_TintRouting COMMAND cna_test_vulkan_avatar_tint_routing
                TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
        endif()

        # Task 842 (Phase 73 Vulkan gap closure): verify MRT with mixed formats is rejected or
        # handled per XNA constraints on Vulkan -- verbatim reuse of the renderer-agnostic test
        # (Task 774's own file, no Bgfx-specific code at all: pure GraphicsDevice/RenderTarget2D
        # API calls, no rendering). Confirms Task 774's shared-code Texture::ValidateFormat fix
        # (RenderTarget2D/RenderTargetCube's own delegating constructors previously skipped it)
        # also holds on Vulkan.
        cna_vulkan_test(cna_test_vulkan_mrt_mixed_formats
                        ${CMAKE_SOURCE_DIR}/modules/renderers/bgfx/examples/bgfx_mrt_mixed_formats_test.cpp)
        cna_register_renderer_test(NAME Vulkan_MRT_MixedFormats COMMAND cna_test_vulkan_mrt_mixed_formats
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 842 (verbatim reuse): Task 176's own SurfaceFormat-validation contract, confirmed
        # renderer-agnostic -- reused unmodified from EasyGL to directly confirm the same
        # Color-only constraint holds on Vulkan too.
        cna_vulkan_test(cna_test_vulkan_surface_format_throws
                        ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_surface_format_throws_test.cpp)
        cna_register_renderer_test(NAME Vulkan_SurfaceFormat_Throws COMMAND cna_test_vulkan_surface_format_throws
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 850 (Phase 73 Vulkan gap closure): SpriteSortMode ordering pixel test on Vulkan --
        # verbatim reuse of the shared EasyGL source (Task 420); no Vulkan-specific restructuring
        # needed (Vulkan has no Bgfx-style "first GetBackBufferData read per frame" quirk).
        cna_vulkan_test(cna_test_vulkan_spritebatch_layerdepth
                        ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_spritebatch_layerdepth_test.cpp)
        cna_register_renderer_test(NAME Vulkan_SpriteBatch_LayerDepthOrder COMMAND cna_test_vulkan_spritebatch_layerdepth
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 851 (Phase 73 Vulkan gap closure): SpriteBatch rotation/scale/source-rectangle-
        # cropping/SpriteEffects flip on Vulkan -- verbatim reuse of the shared EasyGL sources
        # (Tasks 417/418/419/167).
        cna_vulkan_test(cna_test_vulkan_spritebatch_rotation
                        ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_spritebatch_rotation_test.cpp)
        cna_register_renderer_test(NAME Vulkan_SpriteBatch_Rotation COMMAND cna_test_vulkan_spritebatch_rotation
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_vulkan_test(cna_test_vulkan_spritebatch_scale
                        ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_spritebatch_scale_test.cpp)
        cna_register_renderer_test(NAME Vulkan_SpriteBatch_Scale COMMAND cna_test_vulkan_spritebatch_scale
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_vulkan_test(cna_test_vulkan_spritebatch_sourcerect
                        ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_spritebatch_sourcerect_test.cpp)
        cna_register_renderer_test(NAME Vulkan_SpriteBatch_SourceRectangleCropping COMMAND cna_test_vulkan_spritebatch_sourcerect
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_vulkan_test(cna_test_vulkan_sprite_effects
                        ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_sprite_effects_test.cpp)
        cna_register_renderer_test(NAME Vulkan_SpriteEffects_Flip COMMAND cna_test_vulkan_sprite_effects
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 852 (Phase 73 Vulkan gap closure): SpriteFont single-glyph placement, multi-glyph
        # spacing + newline, and default-character fallback on Vulkan -- verbatim reuse of the
        # shared EasyGL sources (Tasks 424/425/426/427).
        cna_vulkan_test(cna_test_vulkan_spritefont_single_glyph
                        ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_spritefont_single_glyph_test.cpp)
        cna_register_renderer_test(NAME Vulkan_SpriteFont_SingleGlyph COMMAND cna_test_vulkan_spritefont_single_glyph
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_vulkan_test(cna_test_vulkan_spritefont_multiglyph_spacing
                        ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_spritefont_multiglyph_spacing_test.cpp)
        cna_register_renderer_test(NAME Vulkan_SpriteFont_MultiGlyphSpacing COMMAND cna_test_vulkan_spritefont_multiglyph_spacing
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_vulkan_test(cna_test_vulkan_spritefont_newline
                        ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_spritefont_newline_test.cpp)
        cna_register_renderer_test(NAME Vulkan_SpriteFont_Newline COMMAND cna_test_vulkan_spritefont_newline
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_vulkan_test(cna_test_vulkan_spritefont_default_char
                        ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_spritefont_default_char_test.cpp)
        cna_register_renderer_test(NAME Vulkan_SpriteFont_DefaultCharacterFallback COMMAND cna_test_vulkan_spritefont_default_char
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 853 (Phase 73 Vulkan gap closure): Model with two meshes and distinct per-mesh
        # Effects, and Model hierarchy transform propagation, on Vulkan -- verbatim reuse of the
        # shared EasyGL sources (Tasks 438/439); Vulkan's own SetDepthTestEnabled is a real,
        # working method (unlike Bgfx's throwing stub), so no substitution is needed at all.
        cna_vulkan_test(cna_test_vulkan_model_two_meshes_effects
                        ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_model_two_meshes_effects_test.cpp)
        cna_register_renderer_test(NAME Vulkan_Model_TwoMeshesEffects COMMAND cna_test_vulkan_model_two_meshes_effects
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_vulkan_test(cna_test_vulkan_model_hierarchy_child_mesh
                        ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_model_hierarchy_child_mesh_test.cpp)
        cna_register_renderer_test(NAME Vulkan_Model_HierarchyChildMesh COMMAND cna_test_vulkan_model_hierarchy_child_mesh
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 927: ModelTypeReader::Read()'s vertex-stride/vtable-size corruption fix is in
        # SHARED code (ContentManager.cpp) -- verbatim reuse of the EasyGL source.
        cna_vulkan_test(cna_test_vulkan_model_json_reader
                        ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_model_json_reader_test.cpp)
        cna_register_renderer_test(NAME Vulkan_ModelJsonReader_Quad COMMAND cna_test_vulkan_model_json_reader
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 931: ModelTypeReader::Read()'s 32-bit index auto-selection fix is in SHARED code
        # (ContentManager.cpp) -- verbatim reuse of the EasyGL source.
        cna_vulkan_test(cna_test_vulkan_model_json_reader_32bit_indices
                        ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_model_json_reader_32bit_indices_test.cpp)
        cna_register_renderer_test(NAME Vulkan_ModelJsonReader_32BitIndices COMMAND cna_test_vulkan_model_json_reader_32bit_indices
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 932: ModelTypeReader::Read()'s per-mesh texture-binding fix is in SHARED code
        # (ContentManager.cpp) -- verbatim reuse of the EasyGL source.
        cna_vulkan_test(cna_test_vulkan_model_json_reader_texture
                        ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_model_json_reader_texture_test.cpp)
        cna_register_renderer_test(NAME Vulkan_ModelJsonReader_Texture COMMAND cna_test_vulkan_model_json_reader_texture
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 934: TextureCubeTypeReader / Content.Load<TextureCube>() fix is in SHARED code
        # (ContentManager.cpp/.hpp) -- verbatim reuse of the EasyGL source.
        cna_vulkan_test(cna_test_vulkan_texturecube_content_load
                        ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_texturecube_content_load_test.cpp)
        cna_register_renderer_test(NAME Vulkan_TextureCube_ContentLoad COMMAND cna_test_vulkan_texturecube_content_load
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 937: ModelTypeReader::Read()'s per-mesh-bone fix is in SHARED code
        # (ContentManager.cpp) -- verbatim reuse of the EasyGL source (pure structural test).
        cna_vulkan_test(cna_test_vulkan_model_json_reader_bone_hierarchy
                        ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_model_json_reader_bone_hierarchy_test.cpp)
        cna_register_renderer_test(NAME Vulkan_ModelJsonReader_BoneHierarchy COMMAND cna_test_vulkan_model_json_reader_bone_hierarchy
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 941: ModelTypeReader::Read()'s "skeleton"/"animations"/SkinnedEffect support is in
        # SHARED code (ContentManager.cpp) -- verbatim reuse of the EasyGL source (pure
        # structural test).
        cna_vulkan_test(cna_test_vulkan_model_json_reader_skeleton
                        ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_model_json_reader_skeleton_test.cpp)
        cna_register_renderer_test(NAME Vulkan_ModelJsonReader_Skeleton COMMAND cna_test_vulkan_model_json_reader_skeleton
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 942: AnimationPlayer -> SkinnedEffect -> Model.Draw() wiring is in SHARED code
        # (ContentManager.cpp/AnimationPlayer.cpp/SkinnedEffect) -- verbatim reuse of the EasyGL
        # source (pixel test).
        cna_vulkan_test(cna_test_vulkan_model_skinned_animation_playback
                        ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_model_skinned_animation_playback_test.cpp)
        cna_register_renderer_test(NAME Vulkan_Model_SkinnedAnimationPlayback COMMAND cna_test_vulkan_model_skinned_animation_playback
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 930: VertexBuffer/IndexBuffer::GetData() is a CPU-side shadow buffer in SHARED
        # code (VertexBuffer.cpp/IndexBuffer.cpp) -- verbatim reuse of the EasyGL source.
        cna_vulkan_test(cna_test_vulkan_vertexbuffer_indexbuffer_getdata
                        ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_vertexbuffer_indexbuffer_getdata_test.cpp)
        cna_register_renderer_test(NAME Vulkan_VertexBufferIndexBufferGetData COMMAND cna_test_vulkan_vertexbuffer_indexbuffer_getdata
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # plans/plan_vulkan.md VULKAN-147 (F-15): the same conversion for the stock EFFECT
        # families. Alpha test had the offset guess in its most explicit form -- one VS for strides
        # 20 and 32 with `uvOffset = (stride == 32) ? 24 : 12`. Each leg draws the same geometry at
        # the same stride with the UV element moved, over a two-texel texture, so a stride-derived
        # offset gives both draws the same pixel and a declaration-derived one gives two.
        cna_vulkan_test(cna_test_vulkan_declared_effect_layout
                        vulkan_declared_effect_layout_test.cpp)
        cna_register_renderer_test(NAME Vulkan_DeclaredEffectLayout
            COMMAND cna_test_vulkan_declared_effect_layout
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # plans/plan_vulkan.md VULKAN-094: SamplerState.AddressW is plumbed and inert here, because
        # Vulkan has no Texture3D sampling path -- and the public API that would make it observable
        # accepts a binding and silently drops it (F-31).
        cna_vulkan_test(cna_test_vulkan_sampler_addressw
                        vulkan_sampler_addressw_test.cpp)
        cna_register_renderer_test(NAME Vulkan_SamplerAddressW
            COMMAND cna_test_vulkan_sampler_addressw
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # plans/plan_vulkan.md VULKAN-141: baseVertex/startIndex/minVertexIndex/numVertices mean what
        # XNA says. Four quads, one per quadrant, so the parameters are separable and every leg
        # asserts all four quadrants rather than only the one it expects to light.
        cna_vulkan_test(cna_test_vulkan_indexed_draw_range
                        vulkan_indexed_draw_range_test.cpp)
        cna_register_renderer_test(NAME Vulkan_IndexedDrawRange
            COMMAND cna_test_vulkan_indexed_draw_range
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # plans/plan_vulkan.md VULKAN-394: the sibling question for the ~20 PipelineKey-keyed caches.
        cna_vulkan_test(cna_test_vulkan_pipeline_cache_bound
                        vulkan_pipeline_cache_bound_test.cpp)
        cna_register_renderer_test(NAME Vulkan_PipelineCacheBound
            COMMAND cna_test_vulkan_pipeline_cache_bound
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # plans/plan_vulkan.md VULKAN-395: how large the VkSampler cache can get. Two of its seven key
        # fields are caller-supplied numbers rather than enumerations, so the key space is not the
        # few hundred the row assumed.
        cna_vulkan_test(cna_test_vulkan_sampler_cache_bound
                        vulkan_sampler_cache_bound_test.cpp)
        cna_register_renderer_test(NAME Vulkan_SamplerCacheBound
            COMMAND cna_test_vulkan_sampler_cache_bound
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # plans/plan_vulkan.md VULKAN-008: capability reporting captured as a checked-in snapshot,
        # read through the GraphicsDevice seam because six of the nineteen members are answered
        # above the renderer. The oracle VULKAN-470 re-verifies against.
        cna_vulkan_test(cna_test_vulkan_capability_snapshot
                        vulkan_capability_snapshot_test.cpp)
        cna_register_renderer_test(NAME Vulkan_CapabilitySnapshot
            COMMAND cna_test_vulkan_capability_snapshot
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # plans/plan_vulkan.md VULKAN-098 (F-19): this renderer's clip-space depth range is XNA's
        # [0,1], and EasyGL's is OpenGL's [-1,1]. Measured positively across the range -- a 5x4
        # truth table plus both planes -- rather than inferred from a depth-bias experiment, which
        # can only ever pin one point.
        cna_vulkan_test(cna_test_vulkan_depth_range_contract
                        vulkan_depth_range_contract_test.cpp)
        cna_register_renderer_test(NAME Vulkan_DepthRangeContract
            COMMAND cna_test_vulkan_depth_range_contract
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # plans/plan_vulkan.md VULKAN-391 (F-06's tail): every descriptor-set allocation answers the
        # same way when the device refuses it -- by name, at the draw, never with a null handle a
        # caller can only bind. One injected failure per family, with a warm-up draw first so the
        # failure lands on that family's own pool.
        cna_vulkan_test(cna_test_vulkan_descriptor_contract_uniformity
                        vulkan_descriptor_contract_uniformity_test.cpp)
        cna_register_renderer_test(NAME Vulkan_DescriptorContractUniformity
            COMMAND cna_test_vulkan_descriptor_contract_uniformity
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # plans/plan_vulkan.md VULKAN-158 (F-26): the declaration says where elements sit inside a
        # record; the stride says where the next record starts. Three factories baked the second,
        # so a padded record produced right offsets into a record the fetch never reached. Every leg
        # probes two widely separated pixels, because one vertex being right is the symptom.
        cna_vulkan_test(cna_test_vulkan_record_stride_binding
                        vulkan_record_stride_binding_test.cpp)
        cna_register_renderer_test(NAME Vulkan_RecordStrideBinding
            COMMAND cna_test_vulkan_record_stride_binding
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # plans/plan_vulkan.md VULKAN-150 (F-20): DualTextureEffect samples Texture2 with TEXCOORD1.
        # EasyGL's own SAMPLE-073 fixture, run here -- it was unrunnable before, because this
        # renderer's shader had one UV input and used it for both samplers.
        cna_vulkan_test(cna_test_vulkan_dualtextureeffect_independent_uv
                        ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_dualtextureeffect_independent_uv_test.cpp)
        cna_register_renderer_test(NAME Vulkan_DualTextureEffect_IndependentUV
            COMMAND cna_test_vulkan_dualtextureeffect_independent_uv
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # plans/plan_vulkan.md VULKAN-155 (F-24): the legacy DrawColoredPrimitives pair reaches the one
        # pipeline whose binding stride is hard-coded to 16, while both entry points copy the buffer
        # at its real stride. Of bind it / refuse it / mis-read it, this pins which one happens.
        cna_vulkan_test(cna_test_vulkan_legacy_colored_stride
                        vulkan_legacy_colored_stride_test.cpp)
        cna_register_renderer_test(NAME Vulkan_LegacyColoredStride
            COMMAND cna_test_vulkan_legacy_colored_stride
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # plans/plan_vulkan.md VULKAN-156: what a stock family does when the stream does not supply
        # one of its shader's inputs. Rendering the texel and refusing by name are both acceptable;
        # rasterizing NOTHING and reporting success is not, and was invisible to the conformance
        # suite that found it (it compares a family against itself, and nothing agrees with itself
        # more perfectly than an empty picture).
        cna_vulkan_test(cna_test_vulkan_unsupplied_input_contract
                        vulkan_unsupplied_input_contract_test.cpp)
        cna_register_renderer_test(NAME Vulkan_UnsuppliedInputContract
            COMMAND cna_test_vulkan_unsupplied_input_contract
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # plans/plan_vulkan.md VULKAN-149 (F-15): the INSTANCED route's per-vertex layout, and the
        # choice between its two vertex shaders, come from the declaration rather than from
        # REMED-GFX-212's stride table. Every record carries a decoy colour at the offset that
        # table would have used, so a stride-driven implementation cannot agree by accident.
        cna_vulkan_test(cna_test_vulkan_declared_instanced_layout
                        vulkan_declared_instanced_layout_test.cpp)
        cna_register_renderer_test(NAME Vulkan_DeclaredInstancedLayout
            COMMAND cna_test_vulkan_declared_instanced_layout
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # plans/plan_vulkan.md VULKAN-146 (F-15): the BasicEffect pipelines take their attribute
        # offsets from the caller's VertexDeclaration. Two 24-byte declarations the stride table
        # cannot tell apart -- colour at 12 and colour at 20 -- must draw their own colours and get
        # their own pipelines, and a buffer with no declaration must keep the stride path.
        cna_vulkan_test(cna_test_vulkan_declared_vertex_layout
                        vulkan_declared_vertex_layout_test.cpp)
        cna_register_renderer_test(NAME Vulkan_DeclaredVertexLayout
            COMMAND cna_test_vulkan_declared_vertex_layout
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # plans/plan_vulkan.md VULKAN-145 (F-15's shared half): the declaration-driven vertex
        # input layout builder. No pipeline factory uses it yet -- this row is the contract and its
        # proof, so the conversion rows have something to rest on. Pure logic, no device needed.
        cna_vulkan_test(cna_test_vulkan_vertex_input_layout
                        vulkan_vertex_input_layout_test.cpp)
        cna_register_renderer_test(NAME Vulkan_VertexInputLayout
            COMMAND cna_test_vulkan_vertex_input_layout
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # plans/plan_vulkan.md VULKAN-332 (F-05): swapInterval_ was read once, at swapchain
        # creation, so GraphicsDeviceManager.SynchronizeWithVerticalRetrace + ApplyChanges()
        # reached nothing. Asserts the swapchain was really rebuilt and the live VkPresentModeKHR
        # followed -- not merely that the request was recorded, which the defect would satisfy.
        cna_vulkan_test(cna_test_vulkan_swap_interval
                        vulkan_swap_interval_test.cpp)
        cna_register_renderer_test(NAME Vulkan_SwapInterval
            COMMAND cna_test_vulkan_swap_interval
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # plans/plan_vulkan.md VULKAN-332: the renderer-agnostic PresentationParameters round-trip
        # half, reused verbatim from EasyGL's own registration.
        cna_vulkan_test(cna_test_vulkan_present_interval
            ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_present_interval_test.cpp)
        cna_register_renderer_test(NAME Vulkan_PresentInterval
            COMMAND cna_test_vulkan_present_interval
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # plans/plan_vulkan.md VULKAN-370 (F-14): a Vulkan occlusion query only counts exactly
        # when occlusionQueryPrecise is enabled AND vkCmdBeginQuery is given
        # VK_QUERY_CONTROL_PRECISE_BIT. This renderer did neither while inheriting the shared
        # "precise" default. Asserts an exactly-known visible-fragment count, not "> 0".
        cna_vulkan_test(cna_test_vulkan_occlusionquery_precision
                        vulkan_occlusionquery_precision_test.cpp)
        cna_register_renderer_test(NAME Vulkan_OcclusionQuery_Precision
            COMMAND cna_test_vulkan_occlusionquery_precision
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # plans/plan_vulkan.md VULKAN-250 (F-07/F-08): GetShaderDialectEXT() was unoverridden, so
        # the one query that exists to stop an application guessing the dialect answered Unknown on
        # a renderer whose ShaderEffect accepts nothing but SPIR-V words. Asserts the declaration,
        # that GLSL source is refused rather than silently accepted, and pins the enumerator
        # ambiguity VULKAN-264 owns.
        cna_vulkan_test(cna_test_vulkan_shader_dialect_contract
                        vulkan_shader_dialect_contract_test.cpp)
        cna_register_renderer_test(NAME Vulkan_ShaderDialectContract
            COMMAND cna_test_vulkan_shader_dialect_contract
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # plans/plan_vulkan.md VULKAN-390 (F-06): GetOrCreateTexSamplerDescSet used to return the
        # default WHITE descriptor set when the 512-set pool was full, so a game with more live
        # texture/sampler pairs than that drew white sprites silently. Walks off that cliff on
        # purpose with 640 live textures and asserts the pool chained, every sprite drew its own
        # texture, and the layer stayed silent.
        cna_vulkan_test(cna_test_vulkan_descriptor_pool_overflow
                        vulkan_descriptor_pool_overflow_test.cpp)
        cna_register_renderer_test(NAME Vulkan_DescriptorPoolOverflow
            COMMAND cna_test_vulkan_descriptor_pool_overflow
            TIMEOUT 90 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # plans/plan_vulkan.md VULKAN-020 (F-07): SupportsCapability used to end in
        # `default: return true`, so the renderer answered true for six members GraphicsDevice
        # answers false, and for values that are not enumerators at all. Asks both seams the same
        # 19 questions, requires the same 19 answers, and drives every false answer into the public
        # API to prove it is a refusal rather than a working path.
        cna_vulkan_test(cna_test_vulkan_capability_contract
                        vulkan_capability_contract_test.cpp)
        cna_register_renderer_test(NAME Vulkan_CapabilityContract
            COMMAND cna_test_vulkan_capability_contract
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # plans/plan_vulkan.md VULKAN-130 (F-01): the host-visible mapping a VertexBuffer uploads
        # into is opened at 64 bytes per vertex, because CreateVertexBuffer is handed a count and
        # not a stride -- while this renderer's own pipeline-key table recognises strides 68, 76
        # and 80. Proves the allocation widens (its own EXT byte counter), that every uploaded byte
        # survives, and that a draw from the far end of a wide buffer reads the right bytes.
        cna_vulkan_test(cna_test_vulkan_vertexbuffer_wide_stride_bounds
                        vulkan_vertexbuffer_wide_stride_bounds_test.cpp)
        cna_register_renderer_test(NAME Vulkan_VertexBuffer_WideStrideBounds
            COMMAND cna_test_vulkan_vertexbuffer_wide_stride_bounds
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # plans/plan_vulkan.md VULKAN-131 (F-02): SetData16/SetData32 wrote index_count * element
        # width into a mapping of capacity * element width with no comparison. The allocation is
        # exact here (unlike the vertex buffer's stride guess), so this proves the exactness, the
        # named refusal of an upload that would exceed it, and that a full-capacity 32-bit buffer
        # still draws from its very last index.
        cna_vulkan_test(cna_test_vulkan_indexbuffer_upload_bounds
                        vulkan_indexbuffer_upload_bounds_test.cpp)
        cna_register_renderer_test(NAME Vulkan_IndexBuffer_UploadBounds
            COMMAND cna_test_vulkan_indexbuffer_upload_bounds
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 855 (Phase 73 Vulkan gap closure): Texture2D SetData/GetData partial-rectangle +
        # startIndex/elementCount + mip-level on Vulkan -- verbatim reuse of the shared EasyGL
        # sources (Tasks 169-171); both operate entirely on a renderer-agnostic CPU-side shadow
        # buffer (Texture2D.cpp), no GPU readback involved at all.
        cna_vulkan_test(cna_test_vulkan_texture2d_partial_rect
                        ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_texture2d_partial_rect_test.cpp)
        cna_register_renderer_test(NAME Vulkan_Texture2D_PartialRect_RoundTrip COMMAND cna_test_vulkan_texture2d_partial_rect
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_vulkan_test(cna_test_vulkan_texture2d_mip
                        ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_texture2d_mip_test.cpp)
        cna_register_renderer_test(NAME Vulkan_Texture2D_Mip_RoundTrip COMMAND cna_test_vulkan_texture2d_mip
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 857 (Phase 73 Vulkan gap closure): NPOT texture upload+sample on Vulkan --
        # verbatim reuse of the shared EasyGL source (Task 268).
        cna_vulkan_test(cna_test_vulkan_npot_texture
                        ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_npot_texture_test.cpp)
        cna_register_renderer_test(NAME Vulkan_NpotTexture COMMAND cna_test_vulkan_npot_texture
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 858 (Phase 73 Vulkan gap closure): Texture3D box-region SetData/GetData pixel
        # correctness on Vulkan -- verbatim reuse of the shared EasyGL source (Task 274). Vulkan's
        # Texture3D::GetData already has a real per-renderer GPU round-trip (Task 865).
        cna_vulkan_test(cna_test_vulkan_texture3d_partial_box_readback
                        ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_texture3d_partial_box_readback_test.cpp)
        cna_register_renderer_test(NAME Vulkan_Texture3D_PartialBox_Readback COMMAND cna_test_vulkan_texture3d_partial_box_readback
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Tasks 447/854 (Phase 73 Vulkan gap closure): real per-draw-call OcclusionQuery
        # correlation -- previously entirely-stubbed Begin()/End() (always returned PixelCount=0).
        # Draws are now tagged with the active query (VulkanRenderer::PushPending3DDraw)
        # and RecordCommandBuffer() wraps the tagged run in a real vkCmdBeginQuery/vkCmdEndQuery
        # pair with correct per-frame vkCmdResetQueryPool sequencing.
        cna_vulkan_test(cna_test_vulkan_occlusionquery_pixelcount
                        vulkan_occlusionquery_pixelcount_test.cpp)
        cna_register_renderer_test(NAME Vulkan_OcclusionQuery_PixelCount COMMAND cna_test_vulkan_occlusionquery_pixelcount
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # plans/plan_cnj.md CNB-58/91 Vulkan port: PbrEffect/SkinnedPbrEffect real glTF metallic-
        # roughness BRDF shader + CNB-67 SkinnedEffect.VertexColorEnabled on the stride-56 vertex
        # layout -- verbatim reuse of the shared, renderer-agnostic EasyGL sources (public XNA API
        # only, same PixelTestGame/ExpectPixel/CompareGoldenImage harness already used by
        # Task 293/294/296/297's own EasyGL-source reuse for Vulkan), cross-checked against the
        # same captured golden PNGs EasyGL's own tests use.
        # WORKING_DIRECTORY is set to the repo root (mirrors EasyGLTests.cmake's own identical
        # requirement for every CompareGoldenImage-using test) since these 3 reused sources
        # reference their golden PNGs by a path relative to the repo root
        # (examples/golden/...), not to ctest's default CWD (the build directory).
        cna_vulkan_test(cna_test_vulkan_pbreffect_golden
                        ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_pbreffect_golden_test.cpp)
        cna_register_renderer_test(NAME Vulkan_PbrEffect_Golden COMMAND cna_test_vulkan_pbreffect_golden
            TIMEOUT 30 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_vulkan_test(cna_test_vulkan_skinnedpbreffect_golden
                        ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_skinnedpbreffect_golden_test.cpp)
        cna_register_renderer_test(NAME Vulkan_SkinnedPbrEffect_Golden COMMAND cna_test_vulkan_skinnedpbreffect_golden
            TIMEOUT 30 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # plans/plan_gltf.md GLTF-344/373: all seven PBR texture bindings, with deliberately distinct
        # base/MR/emissive/occlusion/normal/specular-strength/specular-colour texels and both rigid
        # and skinned draw paths. The shared source uses only public renderer-agnostic XNA/CNA APIs.
        cna_vulkan_test(cna_test_vulkan_pbr_texture_slots
                        ${CMAKE_SOURCE_DIR}/modules/graphics/examples/pbr_texture_slot_mapping_test.cpp)
        cna_register_renderer_test(NAME Vulkan_Pbr_TextureSlots COMMAND cna_test_vulkan_pbr_texture_slots
            TIMEOUT 30 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # GLTF-343/344: shader-ready KHR_materials_ior/specular Fresnel endpoints on the rigid
        # and identity-skinned Vulkan PBR pipelines, including a grazing F90-only discriminator.
        cna_vulkan_test(cna_test_vulkan_pbr_fresnel_factors
                        ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_pbr_fresnel_factors_test.cpp)
        cna_register_renderer_test(NAME Vulkan_Pbr_FresnelFactors COMMAND cna_test_vulkan_pbr_fresnel_factors
            TIMEOUT 30 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # GLTF-213: exact mid-grey sRGB decode, linear factors/base+emissive addition, and final
        # output encode on both the rigid and identity-skinned Vulkan PBR pipelines.
        cna_vulkan_test(cna_test_vulkan_pbr_srgb_transfer
                        ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_pbr_srgb_transfer_test.cpp)
        cna_register_renderer_test(NAME Vulkan_Pbr_SrgbTransfer COMMAND cna_test_vulkan_pbr_srgb_transfer
            TIMEOUT 30 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_vulkan_test(cna_test_vulkan_skinnedeffect_vertexcolor_reused
                        ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_skinnedeffect_vertexcolor_test.cpp)
        cna_register_renderer_test(NAME Vulkan_SkinnedEffect_VertexColor_Reused COMMAND cna_test_vulkan_skinnedeffect_vertexcolor_reused
            TIMEOUT 30 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
        # REMED-GFX-091: this two-draw reused-pipeline case is the minimal validation reproducer.
        # Pixel output alone stays correct, so make the exact Vulkan validity failure test-fatal.
        set_tests_properties(Vulkan_SkinnedEffect_VertexColor_Reused PROPERTIES
            FAIL_REGULAR_EXPRESSION "VUID-vkCmdDraw-None-08608")

        # Vulkan-native counterparts (this renderer's own established Game-subclass/check()
        # pattern, e.g. vulkan_basiceffect_specular_test.cpp/vulkan_skinnedeffect_specular_test.cpp)
        # with expected pixel values independently hand-derived from the exact glTF metallic-
        # roughness BRDF formula (see each test's own header comment for the Python re-derivation),
        # not just captured-and-pasted from a single run.
        cna_vulkan_test(cna_test_vulkan_pbreffect_handderived
                        vulkan_pbreffect_handderived_test.cpp)
        cna_register_renderer_test(NAME Vulkan_PbrEffect_HandDerived COMMAND cna_test_vulkan_pbreffect_handderived
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_vulkan_test(cna_test_vulkan_skinnedeffect_vertexcolor
                        vulkan_skinnedeffect_vertexcolor_test.cpp)
        cna_register_renderer_test(NAME Vulkan_SkinnedEffect_VertexColor COMMAND cna_test_vulkan_skinnedeffect_vertexcolor
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-072: SpriteBatch clip space must be built from the active GraphicsDevice.Viewport
        # (Viewport.Width/Height), not the full target -- a custom sub-Viewport made sprites
        # viewport-relative in XNA/FNA (D3D11-correct) but the Vulkan sprite push-constant
        # viewportSize was fed the full target/virtual dims, squishing the sprite into the sub-region.
        cna_vulkan_test(cna_test_vulkan_spritebatch_custom_viewport
                        ${CNA_GRAPHICS_EXAMPLES_DIR}/spritebatch_custom_viewport_test.cpp)
        cna_register_renderer_test(NAME Vulkan_SpriteBatch_CustomViewport COMMAND cna_test_vulkan_spritebatch_custom_viewport
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-072 (Phase 9): two SpriteBatch batches with DIFFERENT viewports in one frame --
        # each batch must use the viewport active for its own Begin/End (per-batch snapshot).
        cna_vulkan_test(cna_test_vulkan_spritebatch_viewport_switch
                        ${CNA_GRAPHICS_EXAMPLES_DIR}/spritebatch_viewport_switch_test.cpp)
        cna_register_renderer_test(NAME Vulkan_SpriteBatch_ViewportSwitch COMMAND cna_test_vulkan_spritebatch_viewport_switch
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-074: RenderTarget2D::GetData() now flushes deferred sprite work into the target
        # and reads it back, so the RT-readback variant of the GFX-072 conformance test (previously
        # routed through the backbuffer path on Vulkan) runs directly here as an additional oracle.
        cna_vulkan_test(cna_test_vulkan_spritebatch_custom_viewport_rt
                        ${CNA_GRAPHICS_EXAMPLES_DIR}/spritebatch_custom_viewport_rt_test.cpp)
        cna_register_renderer_test(NAME Vulkan_SpriteBatch_CustomViewport_RT COMMAND cna_test_vulkan_spritebatch_custom_viewport_rt
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-074: GetData-before-Present visibility + destroy-before-Present lifetime safety
        # for deferred RenderTarget2D work (sprite + 3D), multiple targets/batches, and no double
        # replay. The destroy-before-Present scene is the UAF regression (run under ASan).
        cna_vulkan_test(cna_test_vulkan_rendertarget_getdata_lifetime
                        vulkan_rendertarget_getdata_lifetime_test.cpp)
        cna_register_renderer_test(NAME Vulkan_RenderTarget_GetDataLifetime COMMAND cna_test_vulkan_rendertarget_getdata_lifetime
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-075: deferred SOURCE-resource lifetime safety -- a Texture2D / TextureCube /
        # RenderTarget2D-as-sampler / custom Effect / OcclusionQuery destroyed after queuing a
        # deferred draw but before the Present/GetData record consumes it must keep its borrowed
        # VkImageView / VkPipeline / VkQueryPool alive (retirement queue) without dropping or
        # altering the already-issued draw. The destroy-before-flush scenes are the UAF regression
        # (run under ASan; validation catches destroyed-object-in-use VUIDs).
        cna_vulkan_test(cna_test_vulkan_deferred_resource_lifetime
                        vulkan_deferred_resource_lifetime_test.cpp)
        cna_register_renderer_test(NAME Vulkan_DeferredResourceLifetime COMMAND cna_test_vulkan_deferred_resource_lifetime
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-076: effect descriptor-set caches must key resource IDENTITY, not a recyclable
        # VkImageView handle value. A cached set is evicted the instant its sampled source is
        # destroyed, so a later texture reusing the freed handle value cannot be handed a stale set
        # sampling the destroyed image. Deterministic (reads renderer cache introspection) + a
        # best-effort real-handle-reuse scene + a memory-bounded stress loop.
        cna_vulkan_test(cna_test_vulkan_effect_descriptor_cache_identity
                        vulkan_effect_descriptor_cache_identity_test.cpp)
        cna_register_renderer_test(NAME Vulkan_EffectDescriptorCacheIdentity COMMAND cna_test_vulkan_effect_descriptor_cache_identity
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-127: every public Texture2D::GetData call must return the resource's real content
        # or reject the request deterministically -- never fabricate one. Pre-fix the shared
        # render-target fallback zero-initialized its own scratch buffer, handed it to
        # ITextureRenderer::GetData (whose interface default did nothing) and converted it for the
        # caller regardless, so a renderer with no readback returned a complete, uniformly
        # transparent-black frame that satisfied both "the destination was overwritten" and any
        # transparent-black content expectation.
        # REMED-GFX-131: SurfaceFormat::Color is a plain 8-bit UNORM byte format, so a mid-tone channel
        # must survive Clear/draw/sample/readback unchanged. Registered here as a cross-renderer control:
        # the defect was WebGPU-local (its render targets used the swapchain's *UnormSrgb format), and
        # these runs are what establish that byte-exact identity is CNA's existing behaviour everywhere
        # else rather than a value invented for the fix.
        cna_vulkan_test(cna_test_vulkan_colorspace_midtone
            ${CNA_GRAPHICS_EXAMPLES_DIR}/colorspace_midtone_contract_test.cpp)
        cna_register_renderer_test(NAME Vulkan_ColorSpace_MidTone COMMAND cna_test_vulkan_colorspace_midtone
            TIMEOUT 120 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_vulkan_test(cna_test_vulkan_additive_blend_contract
            ${CNA_GRAPHICS_EXAMPLES_DIR}/additive_blend_contract_test.cpp)
        cna_register_renderer_test(NAME Vulkan_AdditiveBlendContract
            COMMAND cna_test_vulkan_additive_blend_contract
            TIMEOUT 120 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-147: a RenderTarget2D used as a texture must sample in the same logical orientation
        # as an ordinary Texture2D holding identical bytes. Registered here as a cross-renderer control:
        # the defect was EasyGL-local (an OpenGL framebuffer's origin is bottom-left, so a target's
        # colour texture stores the image bottom-up and sampling did not compensate even though GetData
        # already did), and these runs are what establish that render-target and ordinary-texture
        # sampling already agree everywhere else rather than being made to agree by the fix.
        cna_vulkan_test(cna_test_vulkan_rt_sampling_orientation
            ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_sampling_orientation_test.cpp)
        cna_register_renderer_test(NAME Vulkan_RenderTarget_SamplingOrientation COMMAND cna_test_vulkan_rt_sampling_orientation
            TIMEOUT 120 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-153 cross-renderer source-selection and destination-orientation control.
        cna_vulkan_test(cna_test_vulkan_gfx153_source_rectangle
            ${CNA_GRAPHICS_EXAMPLES_DIR}/source_rectangle_orientation_test.cpp)
        cna_register_renderer_test(NAME Vulkan_GFX153_SourceRectangleOrientation
            COMMAND cna_test_vulkan_gfx153_source_rectangle
            TIMEOUT 180 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")



        # REMED-GFX-169: every stock 3D effect must sample its textures through the public
        # GraphicsDevice.SamplerStates[slot], exactly like SpriteBatch already does.
        #
        # WHAT THIS SAID WHEN IT WAS WRITTEN, and why it is worth keeping: six stock 3D descriptor
        # builders took no sampler parameter at all, so all 15 of their combined-image-sampler
        # bindings were hardcoded to defaultSampler_ (LINEAR + CLAMP_TO_EDGE), and the sampler was
        # absent from their cache keys too. Vulkan was where that defect lived.
        #
        # WHAT IS TRUE NOW (measured by plan_vulkan.md VULKAN-055, 2026-09-06): the builders take
        # samplers. Exactly TWO descriptor sites still name defaultSampler_ -- the texture's and the
        # render target's own pre-baked sets (VulkanRenderer.cpp:465, :904) -- and every stock-effect
        # path builds from slotSamplers_[] (:12734, :12780, :12815, :12824, :12858, :12861). The
        # fixture stays registered as the regression guard for that; the other renderers run the same
        # public fixture as controls. A comment describing a fixed defect as current is how the next
        # audit reaches the wrong conclusion, which is why this one now says both.
        cna_vulkan_test(cna_test_vulkan_stock_effect_sampler
            ${CNA_GRAPHICS_EXAMPLES_DIR}/stock_effect_sampler_contract_test.cpp)
        cna_register_renderer_test(NAME Vulkan_StockEffectSamplerContract COMMAND cna_test_vulkan_stock_effect_sampler
            TIMEOUT 300 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

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
        cna_vulkan_test(cna_test_vulkan_texture_filter_ordinal
            ${CNA_GRAPHICS_EXAMPLES_DIR}/texture_filter_ordinal_contract_test.cpp)
        cna_register_renderer_test(NAME Vulkan_TextureFilterOrdinalContract COMMAND cna_test_vulkan_texture_filter_ordinal
            TIMEOUT 300 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-173 cross-renderer control: EnvironmentMapEffect samples TWO independent
    # resources -- the ordinary 2D texture through GraphicsDevice.SamplerStates[0] and the
    # reflection cube through SamplerStates[1]. SDL_GPU bound a literal LinearClamp for the cube
    # and captured only slot 0, so slot 1 could not reach replay at all. This fixture makes the
    # two slots independently observable (EnvironmentMapAmount 1 leaves the cube the only
    # contributor, 0 leaves the 2D texture the only one) and measures what every other renderer
    # does with the same public state.
        cna_vulkan_test(cna_test_vulkan_envmap_cube_sampler
            ${CNA_GRAPHICS_EXAMPLES_DIR}/envmap_cube_sampler_contract_test.cpp)
        cna_register_renderer_test(NAME Vulkan_EnvMapCubeSamplerContract COMMAND cna_test_vulkan_envmap_cube_sampler
            TIMEOUT 600 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-172 cross-renderer control: DualTextureEffect's two layers have INDEPENDENT public
    # sampler slots -- FNA's DualTextureEffect.fx declares DECLARE_TEXTURE(Texture, 0) and
    # DECLARE_TEXTURE(Texture2, 1). WebGPU declared ONE WGSL sampler for both texture views, so
    # SamplerStates[1] was inexpressible and slot 1 inherited slot 0's. This fixture observes both
    # layers in the SAME image -- slot 0's texture varies along X only, slot 1's along Y only, and
    # the shader's product is separable -- so independence is measured, not inferred. Vulkan's own
    # GetOrCreateDualTexDescSet has taken sampler0/sampler1 since REMED-GFX-169; it runs as control.
        cna_vulkan_test(cna_test_vulkan_dualtexture_slot_sampler
            ${CNA_GRAPHICS_EXAMPLES_DIR}/dualtexture_slot_sampler_contract_test.cpp)
        cna_register_renderer_test(NAME Vulkan_DualTextureSlotSamplerContract COMMAND cna_test_vulkan_dualtexture_slot_sampler
            TIMEOUT 600 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-182 cross-renderer control: the slot-1 sampler questions the fixture above does
        # not ask -- returning to a previous sampler, interleaving with an unrelated effect,
        # sweeping either slot with the other fixed, the magnification partition of the public enum,
        # create/sample/destroy lifetime and a degenerate reflection direction. The defect is
        # Software-local; Vulkan is REMED-GFX-169's corrected reference and runs it as a control, so
        # a divergence names the renderer rather than the fixture.
        cna_vulkan_test(cna_test_vulkan_envmap_cube_sampler_state
            ${CNA_GRAPHICS_EXAMPLES_DIR}/envmap_cube_sampler_state_test.cpp)
        cna_register_renderer_test(NAME Vulkan_EnvMapCubeSamplerState COMMAND cna_test_vulkan_envmap_cube_sampler_state
            TIMEOUT 600 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-175 cross-renderer control: the MIPMAP component of a TextureFilter ordinal.
        # EasyGL mapped ordinals 0 and 1 onto a GL filter with no mipmap term and Software had no
        # mip pipeline at all; this fixture measures what every other renderer does with a chain
        # whose levels name themselves, so a divergence is classified rather than assumed.
        cna_vulkan_test(cna_test_vulkan_texture_filter_mip_contract
            ${CNA_GRAPHICS_EXAMPLES_DIR}/texture_filter_mip_contract_test.cpp)
        cna_register_renderer_test(NAME Vulkan_TextureFilterMipContract COMMAND cna_test_vulkan_texture_filter_mip_contract
            TIMEOUT 300 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-177 cross-renderer control: descriptor/binding bookkeeping must be a function of what
        # is LIVE, never of what has ever existed. D3D12 owned four fixed-capacity heaps with a monotonic
        # bump cursor and no free list, so the 65th sampleable resource EVER CREATED threw even when six
        # were alive. Every draw here is checked against a self-identifying oracle that decodes back to an
        # integer, so a recycled or grown pool that aliases one resource onto another's binding names the
        # wrong resource rather than merely producing an odd colour.
        cna_vulkan_test(cna_test_vulkan_descriptor_capacity
            ${CNA_GRAPHICS_EXAMPLES_DIR}/descriptor_capacity_contract_test.cpp)
        cna_register_renderer_test(NAME Vulkan_DescriptorCapacityContract COMMAND cna_test_vulkan_descriptor_capacity
            TIMEOUT 900 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-239: XNA addresses pixel centres at integer window coordinates and covers a
        # 1x1 screen-space triangle; measured on the real XNA 4.0 runtime in
        # spikes/xna-pixel-center-spike/. Vulkan does not implement the convention today, so this
        # case is expected to fail here -- that visibility is the point of registering it.
        cna_vulkan_test(cna_test_vulkan_xna_pixel_center
            ${CNA_GRAPHICS_EXAMPLES_DIR}/xna_pixel_center_contract_test.cpp)
        cna_register_renderer_test(NAME Vulkan_XnaPixelCenter COMMAND cna_test_vulkan_xna_pixel_center
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_vulkan_test(cna_test_vulkan_point_sampling
            ${CNA_GRAPHICS_EXAMPLES_DIR}/point_sampling_contract_test.cpp)
        cna_register_renderer_test(NAME Vulkan_PointSamplingContract COMMAND cna_test_vulkan_point_sampling
            TIMEOUT 300 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-151: the canonical XNA render-to-texture sequence -- render into a target,
        # unbind it, sample it -- must work in ONE public frame with no intervening GetData,
        # Present, extra frame, manual flush or wait. The defect is Vulkan-local: the deferred
        # recorder's early readback flush filtered the frame's segment list down to the target being
        # READ, so a producer target's pass was never recorded before the consumer that sampled it.
        # This is the home renderer of the finding; the same fixture is registered on every other
        # renderer as a control.
        cna_vulkan_test(cna_test_vulkan_rt_producer_consumer
            ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_producer_consumer_test.cpp)
        cna_register_renderer_test(NAME Vulkan_RenderTarget_ProducerConsumer COMMAND cna_test_vulkan_rt_producer_consumer
            TIMEOUT 120 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-152 cross-renderer control: a RenderTarget2D handed to a stock or custom 3D effect as
    # its texture must be sampled, not reinterpreted. The defect was SDL_GPU-local and fatal (its
    # stock-effect paths static_cast an ITextureRenderer* to the unrelated sibling
    # SdlGpuTextureRenderer, fabricating an SDL_GPUTexture* out of a render target's own fields); this
    # run is what establishes that VULKAN already honoured the contract rather than being made to.
        cna_vulkan_test(cna_test_vulkan_rt_effect_source
            ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_effect_source_test.cpp)
        cna_register_renderer_test(NAME Vulkan_RenderTarget_EffectSource COMMAND cna_test_vulkan_rt_effect_source
            TIMEOUT 300 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-167 deferred-source lifetime: a resource sampled by a draw that has only been
        # QUEUED must stay bindable until that draw actually renders, and destroying the public wrapper
        # first must never terminate the process. The defect was WebGPU-local -- every deferred command
        # stored a raw pointer to the resource's RENDERER OBJECT and called a VIRTUAL method on it at
        # replay, so a RenderTarget2D produced, sampled onto the BACKBUFFER and dropped inside one
        # Draw() was a heap-use-after-free at Present(). These runs establish which renderers already
        # honoured the contract rather than being made to; each leg runs in its own process so a
        # SIGSEGV is an attributable result instead of a lost shard.
        cna_vulkan_test(cna_test_vulkan_deferred_source_lifetime
            ${CNA_GRAPHICS_EXAMPLES_DIR}/deferred_source_lifetime_test.cpp)
        cna_register_renderer_test(NAME Vulkan_DeferredSourceLifetime COMMAND cna_test_vulkan_deferred_source_lifetime
            TIMEOUT 300 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-168 cross-renderer control: destroying a RenderTarget2D that is STILL the bound
        # render target must never make the next SetRenderTarget transition unsafe, and must leave the
        # next target and the backbuffer exactly correct. The defect was EasyGL-local -- it remembered
        # the bound destination as a raw IRenderTargetRenderer* and the next transition called
        # UnbindAsRenderTarget() on it, so a scoped target leaving scope while bound made that
        # transition a virtual call through freed storage. This run is what establishes that VULKAN
        # already honoured the contract rather than being made to; each leg runs in its own process so
        # a SIGSEGV is an attributable result instead of a lost shard.
        cna_vulkan_test(cna_test_vulkan_bound_target_lifetime
            ${CNA_GRAPHICS_EXAMPLES_DIR}/bound_target_lifetime_test.cpp)
        cna_register_renderer_test(NAME Vulkan_BoundTargetLifetime COMMAND cna_test_vulkan_bound_target_lifetime
            TIMEOUT 600 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-180: the public render-target -> Present lifecycle contract. `SdlGpu_RenderState`
        # aborted with `Cannot present while render targets are bound`, which is the SECOND event in a
        # two-stage chain: an over-range DrawPrimitives (two triangles from a three-vertex buffer) was
        # correctly rejected by REMED-GFX-113's range guard, that rejection unwound past the fixture's own
        # SetRenderTarget(nullptr), and EndDraw then met a still-bound target. FNA's contract is REJECT,
        # never auto-unbind, so both guards are right and the fixture was wrong. Every leg runs in its own
        # process; leg C2 reproduces the abort end to end and its correct outcome is SIGABRT.
        cna_vulkan_test(cna_test_vulkan_present_lifecycle
            ${CNA_GRAPHICS_EXAMPLES_DIR}/present_lifecycle_contract_test.cpp)
        cna_register_renderer_test(NAME Vulkan_PresentLifecycle COMMAND cna_test_vulkan_present_lifecycle
            TIMEOUT 900 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-163 cross-renderer control: the same public depth-backed MULTISAMPLED
        # RenderTarget2D construction and lifecycle that aborted the process on Bgfx. Only Bgfx
        # production changed; this renderer is registered to show the construction was always legal
        # and to keep it that way.
        cna_vulkan_test(cna_test_vulkan_msaa_depth_contract
            ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_msaa_depth_contract_test.cpp)
        cna_register_renderer_test(NAME Vulkan_MsaaDepthContract COMMAND cna_test_vulkan_msaa_depth_contract
            TIMEOUT 900 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-154 cross-renderer control: the same PUBLIC first-readback contract on this
        # renderer. The defect was Bgfx-only and only Bgfx production changed; running the identical
        # fixture here is what makes that claim falsifiable rather than asserted.
        cna_vulkan_test(cna_test_vulkan_msaa_first_readback
            ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_msaa_first_readback_test.cpp)
        cna_register_renderer_test(NAME Vulkan_MsaaFirstReadback COMMAND cna_test_vulkan_msaa_first_readback
            TIMEOUT 900 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-186 cross-renderer control: the same PUBLIC generated-mip readback contract on this
        # renderer. The SIGSEGV was SDL_GPU-only and only SDL_GPU production changed; running the
        # identical fixture here is what makes that claim falsifiable rather than asserted.
        cna_vulkan_test(cna_test_vulkan_msaa_mip_readback
            ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_msaa_mip_readback_test.cpp)
        cna_register_renderer_test(NAME Vulkan_MsaaMipReadback COMMAND cna_test_vulkan_msaa_mip_readback
            TIMEOUT 1200 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-190: Vulkan MRT pass finalization must regenerate every mipmapped colour
        # attachment after the pass' MSAA resolves, in public binding order. This Vulkan-only
        # matrix independently covers both slots, mixed mip counts, reordered/repeated binds,
        # every mip level, guarded full/rectangular readback, invalid-then-valid reads, cube-face
        # attachments, disposal and device teardown. Validation is asserted live in-process; the
        # output guard also catches a message emitted during final device/resource destruction.
        cna_vulkan_test(cna_test_vulkan_mrt_mip_finalization
            vulkan_mrt_mip_finalization_test.cpp)
        cna_register_renderer_test(NAME Vulkan_MrtMipFinalization
            COMMAND cna_test_vulkan_mrt_mip_finalization
            TIMEOUT 1200 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
        set_tests_properties(Vulkan_MrtMipFinalization PROPERTIES
            FAIL_REGULAR_EXPRESSION "VUID-|Validation Error|Validation Warning|SYNC-HAZARD")

        # REMED-GFX-194: a direct cube-face GetData must select the exact pending producer by
        # resource, face pass, requested mip chain, public MRT slot and binding cycle. The matrix
        # covers all faces, direct/single-target and both MRT slots, reordered/two-cube/A-B-A
        # cycles, guarded full/rectangle reads, disposal and explicit device teardown.
        cna_vulkan_test(cna_test_vulkan_cube_face_readback_dependency
            vulkan_cube_face_readback_dependency_test.cpp)
        cna_register_renderer_test(NAME Vulkan_CubeFaceReadbackDependency
            COMMAND cna_test_vulkan_cube_face_readback_dependency
            TIMEOUT 1200 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
        set_tests_properties(Vulkan_CubeFaceReadbackDependency PROPERTIES
            FAIL_REGULAR_EXPRESSION "VUID-|Validation Error|Validation Warning|SYNC-HAZARD")

        # REMED-GFX-189/191: invalid levels must be refused before native work, and every valid level
        # must be copied from the exact subresource after that subresource alone has transitioned to
        # TRANSFER_SRC_OPTIMAL. Pixel checks passed on GFX-191's defective implementation, so make
        # its exact copy-layout validation failure fatal as part of this fixture's regression gate.
        cna_vulkan_test(cna_test_vulkan_invalid_mip_level
            ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_invalid_mip_level_test.cpp)
        cna_register_renderer_test(NAME Vulkan_InvalidMipLevel COMMAND cna_test_vulkan_invalid_mip_level
            TIMEOUT 1200 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
        set_tests_properties(Vulkan_InvalidMipLevel PROPERTIES
            FAIL_REGULAR_EXPRESSION "VUID-vkCmdDraw-None-09600")
        # REMED-GFX-165 cross-renderer control: Vulkan already honoured the authoritative-dimension
        # GetBackBufferData contract (its swapchain extent equals the backbuffer); this run establishes
        # that the shared fix left it byte-unchanged and that only WebGPU/SDL_GPU production changed.
        cna_vulkan_test(cna_test_vulkan_backbuffer_readback_dimension
            ${CNA_GRAPHICS_EXAMPLES_DIR}/backbuffer_readback_dimension_test.cpp)
        cna_register_renderer_test(NAME Vulkan_BackbufferReadbackDimension COMMAND cna_test_vulkan_backbuffer_readback_dimension
            TIMEOUT 300 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-161: cross-renderer control for the first-read completion contract.
        cna_vulkan_test(cna_test_vulkan_backbuffer_first_read
            ${CNA_GRAPHICS_EXAMPLES_DIR}/backbuffer_first_read_test.cpp)
        cna_register_renderer_test(NAME Vulkan_BackbufferFirstRead COMMAND cna_test_vulkan_backbuffer_first_read
            TIMEOUT 300 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-162 cross-renderer control: a rasterizing renderer's GetBackBufferData SUCCEEDS and
        # fully writes the requested range, so the Headless rejection is specific, not universal.
        cna_vulkan_test(cna_test_vulkan_backbuffer_reject
            ${CNA_GRAPHICS_EXAMPLES_DIR}/backbuffer_headless_reject_test.cpp)
        cna_register_renderer_test(NAME Vulkan_BackbufferReject COMMAND cna_test_vulkan_backbuffer_reject
            TIMEOUT 300 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-155 cross-renderer control: a render target produced and unbound earlier in a public
        # frame must be visible to a consumer that draws on the BACKBUFFER later in that same frame. The
        # defect was bgfx-local (it radix-sorts a frame's draws by their view's sort position, which
        # defaults to the numeric view id, and its backbuffer owns the lowest id -- so a backbuffer
        # consumer executed before its own producer); these runs are what establish that every other
        # renderer already honoured the contract rather than being made to.
        cna_vulkan_test(cna_test_vulkan_rt_backbuffer_consumer
            ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_backbuffer_consumer_test.cpp)
        cna_register_renderer_test(NAME Vulkan_RenderTarget_BackbufferConsumer COMMAND cna_test_vulkan_rt_backbuffer_consumer
            TIMEOUT 180 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-158's control: a RenderTarget2D constructed during a public frame must be usable in
        # that same frame -- bound, cleared and/or drawn into, unbound and observed -- with no warm-up
        # frame, Present, GetData-before-first-render, dummy draw, empty bind cycle, manual frame advance
        # or wait. The defect was bgfx-local (`bgfx::reset()` discards every view's framebuffer binding,
        # including the bound target's), so this run is what establishes that VULKAN already honoured the
        # contract rather than being made to.
        cna_vulkan_test(cna_test_vulkan_rt_first_use
            ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_first_use_test.cpp)
        cna_register_renderer_test(NAME Vulkan_RenderTarget_FirstUse COMMAND cna_test_vulkan_rt_first_use
            TIMEOUT 180 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-157: a stock 3D draw issued AFTER a SpriteBatch inside ONE render-target bind cycle
        # must execute after it rather than be dropped. REMED-GFX-155's leg I0 measured the region it
        # drew into still holding the cycle's clear colour on BGFX, VULKAN, SOFTWARE and EASYGL, with
        # WEBGPU clean, using ordinary Texture2D sources on both destinations. The oracle is a staircase
        # -- step i covers stripes [0, N-i), so one readback proves every draw happened AND that they
        # happened in the issued order.
        cna_vulkan_test(cna_test_vulkan_spritebatch_3d_order
            ${CNA_GRAPHICS_EXAMPLES_DIR}/spritebatch_3d_order_test.cpp)
        cna_register_renderer_test(NAME Vulkan_SpriteBatch3DOrder COMMAND cna_test_vulkan_spritebatch_3d_order
            TIMEOUT 300 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
        # REMED-GFX-160: the XNA/FNA front-face winding contract. FNA's SpriteBatch emits
        # CLOCKWISE-as-displayed triangles and they must survive RasterizerState.CullCounterClockwise
        # -- the state SpriteBatch.Begin itself defaults to -- so clockwise-as-displayed is XNA's
        # FRONT face and each enum names the face it CULLS. Two triangles in disjoint quadrants, one
        # wound each way, make every cull mode's expected answer complementary in one readback.
        cna_vulkan_test(cna_test_vulkan_frontface_winding
            ${CNA_GRAPHICS_EXAMPLES_DIR}/frontface_winding_test.cpp)
        cna_register_renderer_test(NAME Vulkan_FrontFaceWinding COMMAND cna_test_vulkan_frontface_winding
            TIMEOUT 300 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
        # REMED-GFX-183 cross-renderer control: the complete TriangleStrip parity/culling matrix.
        cna_vulkan_test(cna_test_vulkan_triangle_strip_winding
            ${CNA_GRAPHICS_EXAMPLES_DIR}/triangle_strip_winding_test.cpp)
        cna_register_renderer_test(NAME Vulkan_TriangleStripWinding
            COMMAND cna_test_vulkan_triangle_strip_winding
            TIMEOUT 300 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
        # The same fixture on a multisampled device. A Vulkan render target derives its sample count
        # from the device's own, so only this run makes leg E a genuinely multisampled producer --
        # its resolve has to complete before the consumer samples it, with no readback to trigger it.
        cna_register_renderer_test(NAME Vulkan_RenderTarget_ProducerConsumer_Msaa
            COMMAND cna_test_vulkan_rt_producer_consumer --msaa
            TIMEOUT 120 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
        # The same matrix under Khronos SYNCHRONIZATION validation, which is not part of the default
        # validation set. Recording a producer's render pass in the same command buffer as the
        # consumer that samples it is exactly the change that can introduce a read-after-write hazard
        # while every pixel still comes out right on a permissive driver, so the hazard count is
        # asserted, together with the layer's own liveness.
        cna_register_renderer_test(NAME Vulkan_RenderTarget_ProducerConsumer_SyncVal
            COMMAND cna_test_vulkan_rt_producer_consumer --syncval
            TIMEOUT 180 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_vulkan_test(cna_test_vulkan_texture2d_getdata_contract
                        ${CNA_GRAPHICS_EXAMPLES_DIR}/texture2d_getdata_contract_test.cpp)
        cna_register_renderer_test(NAME Vulkan_Texture2D_GetDataContract COMMAND cna_test_vulkan_texture2d_getdata_contract
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-149 (and REMED-GFX-128, the same expression): the exact XNA-compatible
    # startIndex/elementCount contract of every public Texture2D::GetData overload. startIndex is a
    # DESTINATION element offset and elementCount is the destination capacity available from it;
    # the whole-level overload applied startIndex to the SOURCE, rejected any non-zero offset on a
    # render target, rejected legal excess capacity, and silently returned a partial frame for an
    # undersized one.
        cna_vulkan_test(cna_test_vulkan_texture2d_getdata_transfer_range
                        ${CNA_GRAPHICS_EXAMPLES_DIR}/texture2d_getdata_transfer_range_test.cpp)
        cna_register_renderer_test(NAME Vulkan_Texture2D_GetDataTransferRange COMMAND cna_test_vulkan_texture2d_getdata_transfer_range
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-130: the TextureCube/Texture3D half of REMED-GFX-127's finding. Both public cube
        # and volume GetData paths converted their own zero-initialized scratch buffer for the caller
        # regardless of whether the renderer read anything back, so an unimplemented readback returned
        # a complete, uniformly transparent-black face/volume instead of rejecting the request.
        cna_vulkan_test(cna_test_vulkan_cube_volume_getdata_contract
                        ${CNA_GRAPHICS_EXAMPLES_DIR}/texturecube_texture3d_getdata_contract_test.cpp)
        cna_register_renderer_test(NAME Vulkan_CubeVolume_GetDataContract COMMAND cna_test_vulkan_cube_volume_getdata_contract
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-134: `RenderTargetCube::GetData` must return the face that was really rendered or
        # reject the request deterministically. REMED-GFX-130 made the reporting honest but left the
        # readback itself implemented on only two renderers, so a rendered cube face had no byte-exact
        # public oracle anywhere else. Drawn geometry (never Clear -- see REMED-GFX-129) paints an
        # asymmetric five-region pattern whose colours rotate per face, so a flip, a rotation, a wrong
        # array layer/subresource, a stale face or an unresolved multisample surface all fail.
        cna_vulkan_test(cna_test_vulkan_rendertargetcube_getdata_contract
                        ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertargetcube_getdata_contract_test.cpp)
        cna_register_renderer_test(NAME Vulkan_RenderTargetCube_GetDataContract COMMAND cna_test_vulkan_rendertargetcube_getdata_contract
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-136: IGraphicsRenderer::CreateRenderTargetCube had no `preserveContents` parameter,
        # unlike CreateRenderTarget2D, so a RenderTargetCube's real RenderTargetUsage never reached the
        # renderer and Vulkan/WebGPU discarded a PreserveContents cube face on every bind cycle. Reuses
        # REMED-GFX-134's asymmetric face producer, then rebinds and draws only a small marker: "the
        # marker landed" is what a discarding renderer also achieves, so it can never pass for
        # preservation.
        cna_vulkan_test(cna_test_vulkan_rendertargetcube_usage
                        ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertargetcube_usage_test.cpp)
        cna_register_renderer_test(NAME Vulkan_RenderTargetCube_Usage COMMAND cna_test_vulkan_rendertargetcube_usage
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-141: a MULTISAMPLED RenderTargetCube shared ONE multisample colour attachment
        # across all six faces on EasyGL, Vulkan and SdlGpu, so a PreserveContents face reloaded whichever
        # face was rendered last instead of its own samples. Renders face A, then face B, then rebinds A for
        # a small partial update -- with no readback, Present or flush in between -- and reads the whole of
        # A: the resolved single-sample layer still held the correct A, which is why a full redraw and an
        # immediate read both passed before this.
        cna_vulkan_test(cna_test_vulkan_rendertargetcube_msaa_face
                        ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertargetcube_msaa_face_test.cpp)
        cna_register_renderer_test(NAME Vulkan_RenderTargetCube_MsaaFace COMMAND cna_test_vulkan_rendertargetcube_msaa_face
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-142: RenderTargetUsage's DEPTH and STENCIL half. FNA3D's own header documents
        # `preserveTargetContents` as storing the "color/depth/stencil" contents, and FNA's DiscardContents
        # bind clears all three (Target|DepthBuffer|Stencil, MaxDepth, 0) -- so the enum governs depth and
        # stencil, not colour alone. Renders an occluder, unbinds, rebinds WITHOUT clearing and draws behind
        # it: three bands separate preserved depth from cleared depth, lost colour and a second pass that
        # never drew. A parallel stencil stamp/gate sequence does the same for stencil.
        cna_vulkan_test(cna_test_vulkan_rendertarget_depthstencil_usage
                        ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_depthstencil_usage_test.cpp)
        cna_register_renderer_test(NAME Vulkan_RenderTarget_DepthStencilUsage COMMAND cna_test_vulkan_rendertarget_depthstencil_usage
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-140: every public render-target bind/unbind cycle must be its own logical pass.
        # `VulkanRenderer::RecordCommandBuffer` collected ONE render pass per unique render-target
        # source per flush and replayed every queued batch for it inside that pass, so two bind cycles of
        # one target within a single flush window shared one load action. The decisive checks all use
        # DiscardContents -- collapsing is invisible on a preserving target, which is why REMED-GFX-136
        # needed an artificial readback barrier to see it -- and nothing between two cycles ever forces a
        # flush.
        cna_vulkan_test(cna_test_vulkan_rendertarget_pass_boundary
                        ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_pass_boundary_test.cpp)
        cna_register_renderer_test(NAME Vulkan_RenderTarget_PassBoundary COMMAND cna_test_vulkan_rendertarget_pass_boundary
            TIMEOUT 90 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-143: backbuffer work and render-target work must replay in ONE ordered stream.
        # REMED-GFX-140 (Vulkan) and REMED-GFX-145 (SdlGpu) gave every render-target bind cycle its own
        # native pass in public order, but both kept the BACKBUFFER as one trailing pass, so every
        # backbuffer draw of a frame was replayed after every target pass regardless of when it was
        # issued. A frame that samples a target onto the backbuffer and then renders into that target
        # again therefore saw the FINAL content. Every check queues its whole public sequence with no
        # Present, GetData, flush or extra frame between the cycles, and only then reads once.
        cna_vulkan_test(cna_test_vulkan_backbuffer_pass_order
                        ${CNA_GRAPHICS_EXAMPLES_DIR}/backbuffer_pass_order_test.cpp)
        cna_register_renderer_test(NAME Vulkan_Backbuffer_PassOrder COMMAND cna_test_vulkan_backbuffer_pass_order
            TIMEOUT 120 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-116 cross-renderer control: every deferred draw must execute under the
        # GraphicsDevice.Viewport active at its own public call. WebGPU resolved it live when it
        # recorded the pass; this file is the same public fixture, so a renderer that regresses the
        # contract is caught here rather than assumed correct.
        cna_vulkan_test(cna_test_vulkan_deferred_viewport
                        ${CNA_GRAPHICS_EXAMPLES_DIR}/deferred_viewport_capture_test.cpp)
        cna_register_renderer_test(NAME Vulkan_Deferred_Viewport COMMAND cna_test_vulkan_deferred_viewport
            TIMEOUT 120 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-146 cross-renderer control: every deferred draw must execute under the
        # GraphicsDevice.ScissorRectangle and RasterizerState.ScissorTestEnable active at its own
        # public call. WebGPU resolved both live when it recorded the pass; this file is the same
        # public fixture, so a renderer that regresses the contract is caught here rather than
        # assumed correct.
        cna_vulkan_test(cna_test_vulkan_deferred_scissor
                        ${CNA_GRAPHICS_EXAMPLES_DIR}/deferred_scissor_capture_test.cpp)
        cna_register_renderer_test(NAME Vulkan_Deferred_Scissor COMMAND cna_test_vulkan_deferred_scissor
            TIMEOUT 120 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-135: the WRITE half of the same finding. `TextureCube::SetData`/`Texture3D::SetData`
        # kept the pre-REMED-GFX-127 shape -- a `void` renderer method behind `if (renderer_)` -- so an
        # upload that stored nothing, or only part of the requested region, still returned normally.
        cna_vulkan_test(cna_test_vulkan_cube_volume_setdata_contract
                        ${CNA_GRAPHICS_EXAMPLES_DIR}/texturecube_texture3d_setdata_contract_test.cpp)
        cna_register_renderer_test(NAME Vulkan_CubeVolume_SetDataContract COMMAND cna_test_vulkan_cube_volume_setdata_contract
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-144: the acquired swapchain image must not be written before
        # vkAcquireNextImageKHR has handed it over. The acquire semaphore is waited at
        # COLOR_ATTACHMENT_OUTPUT, but the swapchain render pass's automatic initialLayout
        # transition -- a WRITE -- was ordered only by a subpass dependency whose first
        # synchronization scope named nothing but stages EARLIER than that, so the transition could
        # run before the semaphore signalled. This test enables Khronos synchronization validation
        # IN PROCESS (VkValidationFeaturesEXT), proves the layer is loaded rather than assuming it,
        # runs enough consecutive frames to wrap every frame slot and re-enter several swapchain
        # images, includes REMED-GFX-143's backbuffer/render-target interleaving, drives a real
        # swapchain recreation, and asserts the frame model with bounded counters as well as exact
        # pixels -- so it cannot go green by losing its measurement.
        cna_vulkan_test(cna_test_vulkan_swapchain_sync
                        vulkan_swapchain_sync_test.cpp)
        cna_register_renderer_test(NAME Vulkan_Swapchain_Sync COMMAND cna_test_vulkan_swapchain_sync
            TIMEOUT 120 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    endif()

# Module-local tests get the same headless-safe skip convention the root one-shot
# gives root-registered tests (see cmake/TestHelpers.cmake).
cna_apply_skip_convention()
