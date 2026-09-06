# easygl renderer example/test registration (module-local; this file is the
# former cmake/Tests/EasyGLTests.cmake, moved when examples moved into their owning
# physical modules).
#
# From the root CMakeLists.txt include site:
# EasyGL integration tests (Task 85, 86, 87). Self-contained: bundles the CNA_BUILD_EXAMPLES
# condition this block has always (historically) required -- see EasyGLTests.cmake's own comment.

# Example/test executables historically live at the build root (scripts, docs and
# the golden-image working-directory conventions rely on it); keep them there.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
# Shared example-support headers ("common/PixelTestGame.hpp", ...) are owned by
# the graphics module's examples directory.
include_directories(${CNA_GRAPHICS_EXAMPLES_DIR})

# Historically nested inside the house3d_demo/net-demos block below (both only build for the
# EASYGL/VULKAN renderers), so EasyGL's test suite has always additionally required
# CNA_BUILD_EXAMPLES=ON, unlike every other renderer's test block. Preserved verbatim here as its
# own self-contained condition rather than silently dropped during the file split.
# plans/plan_glbackends.md GLB-6: widened to the non-Emscripten public GL profiles
# (OPENGLES2/OPENGLES3/OPENGL33) -- WEBGL1/WEBGL2 are already excluded by the NOT EMSCRIPTEN
# guard below, matching their Emscripten-only gate in RendererSelection.cmake.
if(CNA_BUILD_EXAMPLES AND CNA_BUILD_TESTS AND NOT EMSCRIPTEN AND NOT WIN32
   AND (CNA_GRAPHICS_RENDERER STREQUAL "OPENGLES2" OR CNA_GRAPHICS_RENDERER STREQUAL "OPENGLES3"
        OR CNA_GRAPHICS_RENDERER STREQUAL "OPENGL33"))

        # --- helper macro: build a headless EasyGL test exe --------------------
        macro(cna_easygl_test target src)
            add_executable(${target} ${src})
            # SDL3::SDL3 is linked explicitly (same as cna_sokol_test/cna_diligent_test):
            # examples include <SDL3/SDL.h> directly, and transitive SDL3 include/link
            # propagation through CNA cannot be relied on (REMED-BUILD-005's finding).
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

        # Ad hoc (not a permanent registered test): reuses the same SOFTWARE-renderer
        # screenshot demo scene to prove a real EasyGL screenshot under Xvfb.
        # The source deliberately stays a repository-level cross-renderer diagnostic,
        # so it is spelled with an explicit ${CMAKE_SOURCE_DIR} path like the other
        # non-module-owned sources this file builds.
        cna_easygl_test(cna_xvfb_screenshot_demo_easygl ${CMAKE_SOURCE_DIR}/examples/xvfb_screenshot_demo.cpp)

        # Task 85: 3-frame smoke test
        cna_register_renderer_test(NAME EasyGL_House3D_SmokeTest COMMAND cna_house3d_demo --smoke 3
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}" LABELS "GraphicsSmoke")

        # Task 86: textured quad pixel readback
        cna_easygl_test(cna_test_easygl_textured_quad
                        easygl_textured_quad_test.cpp)
        cna_register_renderer_test(NAME EasyGL_TexturedQuad_Readback COMMAND cna_test_easygl_textured_quad
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # plans/plan_software.md SOFTWARE-61/84: this renderer's half of the cross-renderer diagnostic
        # dump -- not registered as a ctest, just a plain executable a developer/script runs by
        # hand alongside SOFTWARE's own cna_diag_software and cna_diag_compare.
        # plan_vulkan.md VULKAN-137: the control for Vulkan's zero-length buffer run. Vulkan pads
        # the allocation and EasyGL does not, so agreement between the two is the whole claim.
        cna_easygl_test(cna_test_easygl_zero_length_buffers
                        ${CNA_GRAPHICS_EXAMPLES_DIR}/zero_length_buffer_test.cpp)
        cna_register_renderer_test(NAME EasyGL_ZeroLengthBuffers
            COMMAND cna_test_easygl_zero_length_buffers
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # plan_vulkan.md VULKAN-050: registered here as the CONTROL for the Vulkan run -- the
        # whole question is whether two renderers with different submission models agree about
        # SpriteSortMode::Immediate, and a one-renderer measurement cannot answer it.
        cna_easygl_test(cna_test_easygl_spritebatch_sort_mode_semantics
                        ${CNA_GRAPHICS_EXAMPLES_DIR}/spritebatch_sort_mode_semantics_test.cpp)
        cna_register_renderer_test(NAME EasyGL_SpriteBatch_SortModeSemantics
            COMMAND cna_test_easygl_spritebatch_sort_mode_semantics
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_diag_easygl ${CNA_GRAPHICS_EXAMPLES_DIR}/cross_renderer_diagnostic_scene.cpp)

        # plans/plan_direct2d.md D2D-129: the 2D counterpart of the dump above. The 3D scene next to it
        # cannot serve that purpose -- Direct2D is a permanently 2D-only renderer and rejects its
        # VertexBuffer/BasicEffect calls -- so the 2D corpus is a separate, equally
        # renderer-agnostic source built once per renderer. Same "comparison tool, no pass/fail
        # assertion of its own" precedent: not registered as a ctest.
        cna_easygl_test(cna_corpus2d_easygl ${CNA_GRAPHICS_EXAMPLES_DIR}/cross_renderer_2d_corpus.cpp)

        # plans/plan_dx9.md D9-A6: builds the SAME ${CMAKE_SOURCE_DIR}/tools/xna-oracle/CnaOracleRender.cpp (D9-A3's own
        # scene-driven renderer, already renderer-agnostic -- real public Game/GraphicsDeviceManager/
        # GraphicsDevice/BasicEffect/SpriteBatch API throughout, no EasyGL- or D3D9-specific code)
        # natively under the EasyGL renderer, so the checked-in 31-scene XNA-oracle corpus can be
        # measured against EasyGL too -- no Wine/cross-compilation needed here, unlike the
        # D3D9-branch's own cna_oracle_render registration (this file's D3D9 CTest section) which
        # this target deliberately does not touch or duplicate. Not registered as a ctest, same
        # "it's a comparison tool with no pass/fail assertion of its own" precedent as
        # cna_diag_easygl just above and D9-A3's own cna_oracle_render -- xna-diff.py (invoked by
        # a script) is what judges this binary's PNG output against the checked-in reference
        # images.
        cna_easygl_test(cna_oracle_render_easygl ${CMAKE_SOURCE_DIR}/tools/xna-oracle/CnaOracleRender.cpp)

        # Task 461: canary proving the new shared PixelTestGame helper (modules/graphics/examples/common/) works
        cna_easygl_test(cna_test_easygl_pixeltestgame_smoke
                        easygl_pixeltestgame_smoke_test.cpp)
        cna_register_renderer_test(NAME EasyGL_PixelTestGame_Smoke COMMAND cna_test_easygl_pixeltestgame_smoke
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 463: canary proving PixelTestGame::CompareGoldenImage() (modules/graphics/examples/common/) works.
        # WORKING_DIRECTORY is set to the repo root so the test's relative
        # "examples/golden/*.png" path resolves the same way regardless of which
        # cmake-build-* directory this is run from (CMake's default test working directory is
        # CMAKE_CURRENT_BINARY_DIR, not the source tree).
        cna_easygl_test(cna_test_easygl_goldenimage_smoke
                        easygl_goldenimage_smoke_test.cpp)
        cna_register_renderer_test(NAME EasyGL_GoldenImage_Smoke COMMAND cna_test_easygl_goldenimage_smoke
            TIMEOUT 30 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 464: first real (non-canary) golden-image case, reusing Phase 42's already-
        # verified BasicEffect capstone scene (Task 370).
        cna_easygl_test(cna_test_easygl_basiceffect_golden
                        easygl_basiceffect_golden_test.cpp)
        cna_register_renderer_test(NAME EasyGL_BasicEffect_Golden COMMAND cna_test_easygl_basiceffect_golden
            TIMEOUT 30 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # SAMPLE-002: application-defined Position+Normal vertices have the same 24-byte stride
        # as VertexPositionColorTexture, so this real draw proves EasyGL selects by declaration.
        cna_easygl_test(cna_test_easygl_basiceffect_position_normal
                        easygl_basiceffect_position_normal_test.cpp)
        cna_register_renderer_test(NAME EasyGL_BasicEffect_PositionNormal
            COMMAND cna_test_easygl_basiceffect_position_normal
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 465: golden-image case reusing Phase 47's already-verified SpriteBatch
        # rotation-around-origin scene (Task 417).
        cna_easygl_test(cna_test_easygl_spritebatch_rotation_golden
                        easygl_spritebatch_rotation_golden_test.cpp)
        cna_register_renderer_test(NAME EasyGL_SpriteBatch_Rotation_Golden COMMAND cna_test_easygl_spritebatch_rotation_golden
            TIMEOUT 30 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 466: golden-image case reusing Phase 35's already-verified texture-filter
        # magnification/Linear scene (Task 297).
        cna_easygl_test(cna_test_easygl_texture_filter_linear_golden
                        easygl_texture_filter_linear_golden_test.cpp)
        cna_register_renderer_test(NAME EasyGL_TextureFilter_Linear_Golden COMMAND cna_test_easygl_texture_filter_linear_golden
            TIMEOUT 30 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 467: golden-image case reusing Phase 36's already-verified BlendState::Additive
        # scene (Task 306).
        cna_easygl_test(cna_test_easygl_blendstate_additive_golden
                        easygl_blendstate_additive_golden_test.cpp)
        cna_register_renderer_test(NAME EasyGL_BlendState_Additive_Golden COMMAND cna_test_easygl_blendstate_additive_golden
            TIMEOUT 30 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 468: golden-image cases reusing Phase 37's DepthStencilState write-enable scene
        # (Task 313) and Phase 38's RasterizerState.CullMode scene (Task 323) -- two separate
        # files, one per phase, since no existing test combines both areas.
        cna_easygl_test(cna_test_easygl_depthstencilstate_write_enable_golden
                        easygl_depthstencilstate_write_enable_golden_test.cpp)
        cna_register_renderer_test(NAME EasyGL_DepthStencilState_WriteEnable_Golden COMMAND cna_test_easygl_depthstencilstate_write_enable_golden
            TIMEOUT 30 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_rasterizerstate_cullmode_golden
                        easygl_rasterizerstate_cullmode_golden_test.cpp)
        cna_register_renderer_test(NAME EasyGL_RasterizerState_CullMode_Golden COMMAND cna_test_easygl_rasterizerstate_cullmode_golden
            TIMEOUT 30 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 469: golden-image cases reusing Phases 43-46's already-verified stock-effect
        # scenes (AlphaTestEffect/Task 373, DualTextureEffect/Task 389, EnvironmentMapEffect/
        # Task 399, SkinnedEffect/Task 409) -- 4 separate files, one per phase.
        cna_easygl_test(cna_test_easygl_alphatesteffect_golden
                        easygl_alphatesteffect_golden_test.cpp)
        cna_register_renderer_test(NAME EasyGL_AlphaTestEffect_Golden COMMAND cna_test_easygl_alphatesteffect_golden
            TIMEOUT 30 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_dualtextureeffect_golden
                        easygl_dualtextureeffect_golden_test.cpp)
        cna_register_renderer_test(NAME EasyGL_DualTextureEffect_Golden COMMAND cna_test_easygl_dualtextureeffect_golden
            TIMEOUT 30 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_environmentmapeffect_golden
                        easygl_environmentmapeffect_golden_test.cpp)
        cna_register_renderer_test(NAME EasyGL_EnvironmentMapEffect_Golden COMMAND cna_test_easygl_environmentmapeffect_golden
            TIMEOUT 30 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_skinnedeffect_golden
                        easygl_skinnedeffect_golden_test.cpp)
        cna_register_renderer_test(NAME EasyGL_SkinnedEffect_Golden COMMAND cna_test_easygl_skinnedeffect_golden
            TIMEOUT 30 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # CNB-67 (Phase 13C): SkinnedEffect's new CNAEXT VertexColorEnabled property.
        cna_easygl_test(cna_test_easygl_skinnedeffect_vertexcolor
                        easygl_skinnedeffect_vertexcolor_test.cpp)
        cna_register_renderer_test(NAME EasyGL_SkinnedEffect_VertexColor COMMAND cna_test_easygl_skinnedeffect_vertexcolor
            TIMEOUT 30 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # CNB-58/60 (Phase 13A): PbrEffect's real glTF metallic-roughness BRDF shader.
        cna_easygl_test(cna_test_easygl_pbreffect_golden
                        easygl_pbreffect_golden_test.cpp)
        cna_register_renderer_test(NAME EasyGL_PbrEffect_Golden COMMAND cna_test_easygl_pbreffect_golden
            TIMEOUT 30 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # PBR + skinning combo: SkinnedPbrEffect's shader (EnsurePbrSkinnedProgram()).
        cna_easygl_test(cna_test_easygl_skinnedpbreffect_golden
                        easygl_skinnedpbreffect_golden_test.cpp)
        cna_register_renderer_test(NAME EasyGL_SkinnedPbrEffect_Golden COMMAND cna_test_easygl_skinnedpbreffect_golden
            TIMEOUT 30 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # plans/plan_gltf.md GLTF-343/344: IOR/specular Fresnel endpoints reach both real PBR programs.
        cna_easygl_test(cna_test_easygl_pbr_fresnel_factors
                        easygl_pbr_fresnel_factors_test.cpp)
        cna_register_renderer_test(NAME EasyGL_Pbr_FresnelFactors COMMAND cna_test_easygl_pbr_fresnel_factors
            TIMEOUT 30 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # plans/plan_gltf.md GLTF-210/212/223: mid-grey texture decode and final sRGB encode on both
        # rigid and skinned PBR programs. Endpoint textures cannot prove the decode path.
        cna_easygl_test(cna_test_easygl_pbr_srgb_transfer
                        easygl_pbr_srgb_transfer_test.cpp)
        cna_register_renderer_test(NAME EasyGL_Pbr_SrgbTransfer COMMAND cna_test_easygl_pbr_srgb_transfer
            TIMEOUT 30 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # plans/plan_gltf.md GLTF-182/183/224..227: independent rigid/skinned UV0/UV1 material slots,
        # channel-discriminating normal/occlusion semantics, XY-only normal scale and the
        # specification's occlusion-strength formula.
        cna_easygl_test(cna_test_easygl_pbr_material_maps
                        easygl_pbr_material_maps_test.cpp)
        cna_register_renderer_test(NAME EasyGL_Pbr_MaterialMaps COMMAND cna_test_easygl_pbr_material_maps
            TIMEOUT 30 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # plans/plan_gltf.md GLTF-373: a narrower renderer-shared test whose five sentinel textures
        # prove base/normal/MR/emissive/occlusion binding without relying on EasyGL-only scalar or
        # output-transfer extensions.
        cna_easygl_test(cna_test_easygl_pbr_texture_slots
                        ${CMAKE_SOURCE_DIR}/modules/graphics/examples/pbr_texture_slot_mapping_test.cpp)
        cna_register_renderer_test(NAME EasyGL_Pbr_TextureSlots COMMAND cna_test_easygl_pbr_texture_slots
            TIMEOUT 30 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # plans/plan_gltf.md GLTF-268: the generated stride-68 skinned PBR corpus fixture through the
        # real ContentManager.Load<Model> -> bind-pose palette -> Model.Draw -> EasyGL pixel path.
        cna_easygl_test(cna_test_easygl_gltf_skinned_pbr_nonuniform_joint
                        easygl_gltf_skinned_pbr_nonuniform_joint_test.cpp)
        cna_register_renderer_test(NAME EasyGL_Gltf_SkinnedPbrNonUniformJoint
            COMMAND cna_test_easygl_gltf_skinned_pbr_nonuniform_joint
            TIMEOUT 30 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # plans/plan_gltf.md GLTF-218: generated textured material through ContentManager.Load<Model>,
        # retaining both operands and multiplying them in linear space in the real PBR program.
        cna_easygl_test(cna_test_easygl_gltf_basecolor_factor_texture
                        easygl_gltf_basecolor_factor_texture_test.cpp)
        cna_register_renderer_test(NAME EasyGL_Gltf_BaseColorFactorTexture
            COMMAND cna_test_easygl_gltf_basecolor_factor_texture
            TIMEOUT 30 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # plans/plan_gltf.md GLTF-184/336: one imported UV samples the base-colour and normal maps
        # through two distinct KHR_texture_transform affine matrices at the real PBR samplers.
        cna_easygl_test(cna_test_easygl_gltf_texture_transform_per_map
                        easygl_gltf_texture_transform_per_map_test.cpp)
        cna_register_renderer_test(NAME EasyGL_Gltf_TextureTransformPerMap
            COMMAND cna_test_easygl_gltf_texture_transform_per_map
            TIMEOUT 30 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # plans/plan_gltf.md GLTF-439: retain one loaded glTF model across EasyGL's real desktop GL
        # context destroy/recreate cycle, then draw the same imported buffers and texture again.
        cna_easygl_test(cna_test_easygl_gltf_context_loss
                        easygl_gltf_context_loss_test.cpp)
        cna_register_renderer_test(NAME EasyGL_Gltf_ContextLoss
            COMMAND cna_test_easygl_gltf_context_loss
            TIMEOUT 30 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            # Unlike screenshot tests that exercise the X11 presenter, this case exercises GL
            # resource lifetime only. SDL's offscreen driver gives it a real Mesa context while
            # keeping the regression runnable on Linux builders that do not expose an X server.
            # Do not force a software device: EGL rejects that override when SDL explicitly
            # selected another device, which crashes before the test reaches context recovery.
            ENVIRONMENT "SDL_VIDEODRIVER=offscreen")

        # plans/plan_gltf.md GLTF-189: CLAMP/REPEAT/MIRRORED_REPEAT from three generated assets,
        # applied through ModelMeshPart's public per-slot sampler contract before Model::Draw.
        cna_easygl_test(cna_test_easygl_gltf_sampler_wrap
                        easygl_gltf_sampler_wrap_test.cpp)
        cna_register_renderer_test(NAME EasyGL_Gltf_SamplerWrap
            COMMAND cna_test_easygl_gltf_sampler_wrap
            TIMEOUT 30 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # plans/plan_gltf.md GLTF-175: the two corpus primitives' opposite tangent.w signs must
        # reconstruct opposite bitangents under the same imported normal map.
        cna_easygl_test(cna_test_easygl_gltf_tangent_handedness
                        easygl_gltf_tangent_handedness_test.cpp)
        cna_register_renderer_test(NAME EasyGL_Gltf_TangentHandedness
            COMMAND cna_test_easygl_gltf_tangent_handedness
            TIMEOUT 30 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # plans/plan_gltf.md GLTF-176: one shared tangent buffer placed normally and through a
        # negative-determinant node transform; both draws must reconstruct the same world B.
        cna_easygl_test(cna_test_easygl_gltf_mirrored_tangent
                        easygl_gltf_mirrored_tangent_test.cpp)
        cna_register_renderer_test(NAME EasyGL_Gltf_MirroredTangent
            COMMAND cna_test_easygl_gltf_mirrored_tangent
            TIMEOUT 30 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # plans/plan_gltf.md GLTF-230/231: a real imported BLEND + doubleSided material remains
        # straight-alpha output; the application selects blend/cull state and draw order around
        # Model::Draw, which preserves caller-owned device state.
        cna_easygl_test(cna_test_easygl_gltf_alpha_blend
                        easygl_gltf_alpha_blend_test.cpp)
        cna_register_renderer_test(NAME EasyGL_Gltf_AlphaBlend
            COMMAND cna_test_easygl_gltf_alpha_blend
            TIMEOUT 30 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # plans/plan_gltf.md GLTF-340: an opaque dial must be drawn before the nearer transmission
        # approximation; the inverse order writes glass depth and hides the dial behind it.
        cna_easygl_test(cna_test_easygl_gltf_transmission_ordering
                        easygl_gltf_transmission_ordering_test.cpp)
        cna_register_renderer_test(NAME EasyGL_Gltf_TransmissionOrdering
            COMMAND cna_test_easygl_gltf_transmission_ordering
            TIMEOUT 30 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 87: RenderTarget2D → texture → readback
        cna_easygl_test(cna_test_easygl_render_target
                        easygl_render_target_test.cpp)
        cna_register_renderer_test(NAME EasyGL_RenderTarget2D_Readback COMMAND cna_test_easygl_render_target
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_rendertarget_golden ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget2d_golden_test.cpp)
        cna_register_renderer_test(NAME EasyGL_RenderTarget2D_Golden COMMAND cna_test_easygl_rendertarget_golden
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 335: RenderTarget2D depth buffer is real and functional (not just a stored property)
        cna_easygl_test(cna_test_easygl_rendertarget2d_depth
                        ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget2d_depth_test.cpp)
        cna_register_renderer_test(NAME EasyGL_RenderTarget2D_DepthBuffer COMMAND cna_test_easygl_rendertarget2d_depth
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 336: RenderTarget2D mip chain is genuinely GL-complete after auto-generation on unbind
        cna_easygl_test(cna_test_easygl_rendertarget2d_mip
                        easygl_rendertarget2d_mip_test.cpp)
        cna_register_renderer_test(NAME EasyGL_RenderTarget2D_MipComplete COMMAND cna_test_easygl_rendertarget2d_mip
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 337: RenderTarget2D MSAA resolve genuinely anti-aliases (not just solid-fill safe)
        # OPENGLES2 (docs/opengles2-renderer.md): this test exists to exercise an ES 3.0-level
        # feature the profile truthfully refuses (SupportsCapability answers false), so it is not
        # registered there -- the refusal contract itself is covered by the shared capability and
        # lifecycle tests.
        if(NOT CNA_GRAPHICS_RENDERER STREQUAL "OPENGLES2")
            cna_easygl_test(cna_test_easygl_rendertarget2d_msaa
                            easygl_rendertarget2d_msaa_test.cpp)
            cna_register_renderer_test(NAME EasyGL_RenderTarget2D_MsaaResolve COMMAND cna_test_easygl_rendertarget2d_msaa
                TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
        endif()

        # Task 338: SetRenderTarget(nullptr) resets Viewport/ScissorRectangle to the backbuffer
        cna_easygl_test(cna_test_easygl_rendertarget_viewport_scissor_reset
                        ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_viewport_scissor_reset_test.cpp)
        cna_register_renderer_test(NAME EasyGL_RenderTarget_ViewportScissorReset COMMAND cna_test_easygl_rendertarget_viewport_scissor_reset
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 22: BasicEffect property tests
        cna_easygl_test(cna_test_basic_effect
                        ${CNA_GRAPHICS_EXAMPLES_DIR}/basic_effect_test.cpp)
        cna_register_renderer_test(NAME EasyGL_BasicEffect_Properties COMMAND cna_test_basic_effect
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 23: AlphaTestEffect property tests
        cna_easygl_test(cna_test_alpha_test_effect
                        ${CNA_GRAPHICS_EXAMPLES_DIR}/alpha_test_effect_test.cpp)
        cna_register_renderer_test(NAME EasyGL_AlphaTestEffect_Properties COMMAND cna_test_alpha_test_effect
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 24: SkinnedEffect property tests
        cna_easygl_test(cna_test_skinned_effect
                        ${CNA_GRAPHICS_EXAMPLES_DIR}/skinned_effect_test.cpp)
        cna_register_renderer_test(NAME EasyGL_SkinnedEffect_Properties COMMAND cna_test_skinned_effect
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 27: SpriteFont property and MeasureString tests
        cna_easygl_test(cna_test_sprite_font
                        ${CNA_GRAPHICS_EXAMPLES_DIR}/sprite_font_test.cpp)
        cna_register_renderer_test(NAME EasyGL_SpriteFont_Properties COMMAND cna_test_sprite_font
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 35: OcclusionQuery begin/end/IsComplete integration test
        cna_easygl_test(cna_test_occlusion_query
                        ${CNA_GRAPHICS_EXAMPLES_DIR}/occlusion_query_test.cpp)
        cna_register_renderer_test(NAME EasyGL_OcclusionQuery_Cycle COMMAND cna_test_occlusion_query
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 445: Pixel/query test -- fully visible quad returns a positive occlusion-query pixel count.
        # OPENGLES2 (docs/opengles2-renderer.md): this test exists to exercise an ES 3.0-level
        # feature the profile truthfully refuses (SupportsCapability answers false), so it is not
        # registered there -- the refusal contract itself is covered by the shared capability and
        # lifecycle tests.
        if(NOT CNA_GRAPHICS_RENDERER STREQUAL "OPENGLES2")
            cna_easygl_test(cna_test_easygl_occlusion_query_visible_quad
                            easygl_occlusion_query_visible_quad_test.cpp)
            cna_register_renderer_test(NAME EasyGL_OcclusionQuery_VisibleQuad COMMAND cna_test_easygl_occlusion_query_visible_quad
                TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
        endif()

        # Task 446: Pixel/query test -- fully occluded quad returns a zero occlusion-query pixel count.
        # OPENGLES2 (docs/opengles2-renderer.md): this test exists to exercise an ES 3.0-level
        # feature the profile truthfully refuses (SupportsCapability answers false), so it is not
        # registered there -- the refusal contract itself is covered by the shared capability and
        # lifecycle tests.
        if(NOT CNA_GRAPHICS_RENDERER STREQUAL "OPENGLES2")
            cna_easygl_test(cna_test_easygl_occlusion_query_occluded_quad
                            easygl_occlusion_query_occluded_quad_test.cpp)
            cna_register_renderer_test(NAME EasyGL_OcclusionQuery_OccludedQuad COMMAND cna_test_easygl_occlusion_query_occluded_quad
                TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
        endif()

        # Task 122: AlphaTestEffect GPU alpha cutout + pixel readback
        cna_easygl_test(cna_test_alpha_test_integration
                        ${CNA_GRAPHICS_EXAMPLES_DIR}/alpha_test_integration_test.cpp)
        cna_register_renderer_test(NAME EasyGL_AlphaTestEffect_AlphaCutout COMMAND cna_test_alpha_test_integration
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 123: SkinnedEffect 2-bone GPU transform + mesh deformation
        cna_easygl_test(cna_test_skinned_integration
                        ${CNA_GRAPHICS_EXAMPLES_DIR}/skinned_effect_integration_test.cpp)
        cna_register_renderer_test(NAME EasyGL_SkinnedEffect_BoneDeformation COMMAND cna_test_skinned_integration
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 10.14: AvatarRenderer::EnableRealRenderingEXT/DrawRealEXT real GPU-skinned
        # rendering (Phase 10 — Avatar real-rendering CNAEXT/EXT extension). Needs
        # CNA_GamerServices (AvatarRenderer lives there), so it's only built when networking
        # (and thus GamerServices) is enabled.
        if(CNA_ENABLE_NET)
            cna_easygl_test(cna_test_avatar_real_render
                            ${CMAKE_SOURCE_DIR}/modules/gamer-services/examples/avatar_real_render_integration_test.cpp)
            target_link_libraries(cna_test_avatar_real_render PRIVATE CNA_GamerServices)
            cna_register_renderer_test(NAME EasyGL_AvatarRenderer_RealRender COMMAND cna_test_avatar_real_render
                TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

            # Task 11.21: SkinnedModelEXT::AttachPartEXT runtime wardrobe-attach real render.
            cna_easygl_test(cna_test_avatar_attach_part
                            ${CMAKE_SOURCE_DIR}/modules/gamer-services/examples/avatar_attach_part_integration_test.cpp)
            target_link_libraries(cna_test_avatar_attach_part PRIVATE CNA_GamerServices)
            cna_register_renderer_test(NAME EasyGL_AvatarRenderer_AttachPart COMMAND cna_test_avatar_attach_part
                TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

            # Task 11.24: AvatarRenderer::PartTintEXT per-part tint routing regression test.
            cna_easygl_test(cna_test_avatar_tint_routing
                            ${CMAKE_SOURCE_DIR}/modules/gamer-services/examples/avatar_tint_routing_integration_test.cpp)
            target_link_libraries(cna_test_avatar_tint_routing PRIVATE CNA_GamerServices)
            cna_register_renderer_test(NAME EasyGL_AvatarRenderer_TintRouting COMMAND cna_test_avatar_tint_routing
                TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
        endif()

        # Task 125: DXT1 texture loaded via FromStream, rendered, pixel readback
        cna_easygl_test(cna_test_dxt1_texture
                        ${CNA_GRAPHICS_EXAMPLES_DIR}/dxt1_texture_test.cpp)
        cna_register_renderer_test(NAME EasyGL_DXT1_FromStream_Readback COMMAND cna_test_dxt1_texture
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 132: ShaderEffect (GLSL) with SpriteBatch — red-tint over green background
        cna_easygl_test(cna_test_easygl_shader_effect
                        easygl_shader_effect_test.cpp)
        cna_register_renderer_test(NAME EasyGL_ShaderEffect_GLSL COMMAND cna_test_easygl_shader_effect
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 1077: ShaderEffect::SetUniformXxx() must reach the real draw via SpriteBatch
        cna_easygl_test(cna_test_easygl_shader_effect_spritebatch_uniform
                        easygl_shader_effect_spritebatch_uniform_test.cpp)
        cna_register_renderer_test(NAME EasyGL_ShaderEffect_SpriteBatch_Uniform COMMAND cna_test_easygl_shader_effect_spritebatch_uniform
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 946: BloomExtract.fx HLSL->GLSL conversion proof + .shader.json ContentManager round-trip
        cna_easygl_test(cna_test_easygl_bloom_extract
                        easygl_bloom_extract_test.cpp)
        cna_register_renderer_test(NAME EasyGL_Bloom_Extract COMMAND cna_test_easygl_bloom_extract
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 946: GaussianBlur.fx HLSL->GLSL conversion proof (array uniforms)
        cna_easygl_test(cna_test_easygl_bloom_gaussianblur
                        easygl_bloom_gaussianblur_test.cpp)
        cna_register_renderer_test(NAME EasyGL_Bloom_GaussianBlur COMMAND cna_test_easygl_bloom_gaussianblur
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 946: BloomCombine.fx HLSL->GLSL conversion proof (2nd texture unit)
        cna_easygl_test(cna_test_easygl_bloom_combine
                        easygl_bloom_combine_test.cpp)
        cna_register_renderer_test(NAME EasyGL_Bloom_Combine COMMAND cna_test_easygl_bloom_combine
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 946: full 4-pass BloomSample pipeline end-to-end proof (extract->blurH->blurV->combine)
        cna_easygl_test(cna_test_easygl_bloom_pipeline
                        easygl_bloom_pipeline_test.cpp)
        cna_register_renderer_test(NAME EasyGL_Bloom_Pipeline COMMAND cna_test_easygl_bloom_pipeline
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 947 (Phase 78 rollout): Clouds.fx HLSL->GLSL conversion proof -- clears NetRumble's
        # shader blocker (the bloom trio above already proved the other 3 of its 4 shaders).
        cna_easygl_test(cna_test_easygl_clouds_shader
                        easygl_clouds_shader_test.cpp)
        cna_register_renderer_test(NAME EasyGL_Clouds_Shader COMMAND cna_test_easygl_clouds_shader
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 1079: ShaderEffect can now drive a real 3D DrawIndexedPrimitives() call, not just
        # SpriteBatch -- verifies World/View/Projection + attribute wiring for stride-32 vertices.
        cna_easygl_test(cna_test_easygl_shadereffect_3d
                        easygl_shadereffect_3d_test.cpp)
        cna_register_renderer_test(NAME EasyGL_ShaderEffect_3D COMMAND cna_test_easygl_shadereffect_3d
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 947 (Phase 78 rollout): PerPixelLighting.fx's PerPixelDiffuseAndPhong technique --
        # first real 3D shader conversion, using Task 1079's new 3D ShaderEffect capability.
        cna_easygl_test(cna_test_easygl_perpixellighting_shader
                        easygl_perpixellighting_shader_test.cpp)
        cna_register_renderer_test(NAME EasyGL_PerPixelLighting_Shader COMMAND cna_test_easygl_perpixellighting_shader
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 947: PerPixelLighting.fx's PerPixelDiffuse technique (diffuse-only, no specular).
        cna_easygl_test(cna_test_easygl_perpixellighting_diffuseonly_shader
                        easygl_perpixellighting_diffuseonly_shader_test.cpp)
        cna_register_renderer_test(NAME EasyGL_PerPixelLighting_DiffuseOnly_Shader COMMAND cna_test_easygl_perpixellighting_diffuseonly_shader
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 947: PerPixelLighting.fx's PerVertexDiffuseAndPerPixelPhong technique (diffuse per
        # vertex, specular per pixel).
        cna_easygl_test(cna_test_easygl_perpixellighting_vertexdiffuse_pixelphong_shader
                        easygl_perpixellighting_vertexdiffuse_pixelphong_shader_test.cpp)
        cna_register_renderer_test(NAME EasyGL_PerPixelLighting_VertexDiffusePixelPhong_Shader COMMAND cna_test_easygl_perpixellighting_vertexdiffuse_pixelphong_shader
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 947: VertexLighting.fx's PerVertexDiffuse technique (diffuse-only, fully per vertex).
        cna_easygl_test(cna_test_easygl_vertexlighting_diffuse_shader
                        easygl_vertexlighting_diffuse_shader_test.cpp)
        cna_register_renderer_test(NAME EasyGL_VertexLighting_Diffuse_Shader COMMAND cna_test_easygl_vertexlighting_diffuse_shader
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 947: VertexLighting.fx's PerVertexDiffuseAndPhong technique (diffuse+specular, fully
        # per vertex) -- with this one, all 5 of PerPixelLighting sample's effect/technique
        # combinations are ported.
        cna_easygl_test(cna_test_easygl_vertexlighting_diffusephong_shader
                        easygl_vertexlighting_diffusephong_shader_test.cpp)
        cna_register_renderer_test(NAME EasyGL_VertexLighting_DiffusePhong_Shader COMMAND cna_test_easygl_vertexlighting_diffusephong_shader
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 947: VertexLightingSample's OWN VertexLighting.fx (directional light -- distinct
        # from the same-named file already ported for the PerPixelLighting sample).
        cna_easygl_test(cna_test_easygl_vertexlighting_directional_shader
                        easygl_vertexlighting_directional_shader_test.cpp)
        cna_register_renderer_test(NAME EasyGL_VertexLighting_Directional_Shader COMMAND cna_test_easygl_vertexlighting_directional_shader
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 947: VertexLightingSample's FlatShaded.fx -- with this and the row above,
        # VertexLighting sample's shader blocker is fully cleared.
        cna_easygl_test(cna_test_easygl_flatshaded_shader
                        easygl_flatshaded_shader_test.cpp)
        cna_register_renderer_test(NAME EasyGL_FlatShaded_Shader COMMAND cna_test_easygl_flatshaded_shader
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 947: Distort.fx's Distort technique (no-blur variant).
        cna_easygl_test(cna_test_easygl_distort_shader
                        easygl_distort_shader_test.cpp)
        cna_register_renderer_test(NAME EasyGL_Distort_Shader COMMAND cna_test_easygl_distort_shader
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 947: Distorters.fx's DisplacementMapped technique.
        cna_easygl_test(cna_test_easygl_distorters_displacementmapped_shader
                        easygl_distorters_displacementmapped_shader_test.cpp)
        cna_register_renderer_test(NAME EasyGL_Distorters_DisplacementMapped_Shader COMMAND cna_test_easygl_distorters_displacementmapped_shader
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 947: Distorters.fx's HeatHaze technique.
        cna_easygl_test(cna_test_easygl_distorters_heathaze_shader
                        easygl_distorters_heathaze_shader_test.cpp)
        cna_register_renderer_test(NAME EasyGL_Distorters_HeatHaze_Shader COMMAND cna_test_easygl_distorters_heathaze_shader
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 947: Distorters.fx's PullIn technique -- with this, Distorters.fx's own 3
        # actually-used techniques (DisplacementMapped/HeatHaze/PullIn) are all ported.
        cna_easygl_test(cna_test_easygl_distorters_pullin_shader
                        easygl_distorters_pullin_shader_test.cpp)
        cna_register_renderer_test(NAME EasyGL_Distorters_PullIn_Shader COMMAND cna_test_easygl_distorters_pullin_shader
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 947: Distort.fx's DistortBlur technique -- with this, DistortionSample's shader
        # blocker is fully cleared.
        cna_easygl_test(cna_test_easygl_distortblur_shader
                        easygl_distortblur_shader_test.cpp)
        cna_register_renderer_test(NAME EasyGL_DistortBlur_Shader COMMAND cna_test_easygl_distortblur_shader
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 947: CartoonEffect.Fx's Lambert technique.
        cna_easygl_test(cna_test_easygl_cartooneffect_lambert_shader
                        easygl_cartooneffect_lambert_shader_test.cpp)
        cna_register_renderer_test(NAME EasyGL_CartoonEffect_Lambert_Shader COMMAND cna_test_easygl_cartooneffect_lambert_shader
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 947: CartoonEffect.Fx's Toon technique.
        cna_easygl_test(cna_test_easygl_cartooneffect_toon_shader
                        easygl_cartooneffect_toon_shader_test.cpp)
        cna_register_renderer_test(NAME EasyGL_CartoonEffect_Toon_Shader COMMAND cna_test_easygl_cartooneffect_toon_shader
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 947: CartoonEffect.Fx's NormalDepth technique -- with this, all 3 of
        # CartoonEffect.Fx's techniques are ported.
        cna_easygl_test(cna_test_easygl_cartooneffect_normaldepth_shader
                        easygl_cartooneffect_normaldepth_shader_test.cpp)
        cna_register_renderer_test(NAME EasyGL_CartoonEffect_NormalDepth_Shader COMMAND cna_test_easygl_cartooneffect_normaldepth_shader
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 947: PostprocessEffect.Fx's 5 techniques -- with this, NonPhotoRealistic sample's
        # shader blocker is fully cleared.
        cna_easygl_test(cna_test_easygl_postprocesseffect_shader
                        easygl_postprocesseffect_shader_test.cpp)
        cna_register_renderer_test(NAME EasyGL_PostprocessEffect_Shader COMMAND cna_test_easygl_postprocesseffect_shader
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 947: DrawModel.fx's CreateShadowMap technique (ShadowMappingSample_4_0).
        cna_easygl_test(cna_test_easygl_shadowmapping_createshadowmap_shader
                        easygl_shadowmapping_createshadowmap_shader_test.cpp)
        cna_register_renderer_test(NAME EasyGL_ShadowMapping_CreateShadowMap_Shader COMMAND cna_test_easygl_shadowmapping_createshadowmap_shader
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 947: DrawModel.fx's DrawWithShadowMap technique -- with this, ShadowMapping
        # sample's shader blocker is fully cleared.
        cna_easygl_test(cna_test_easygl_shadowmapping_drawwithshadowmap_shader
                        easygl_shadowmapping_drawwithshadowmap_shader_test.cpp)
        cna_register_renderer_test(NAME EasyGL_ShadowMapping_DrawWithShadowMap_Shader COMMAND cna_test_easygl_shadowmapping_drawwithshadowmap_shader
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 1080: genuinely custom vertex-layout support for the 3D ShaderEffect draw path
        # (a 48-byte stride/5-element declaration matching none of the 5 built-in strides).
        cna_easygl_test(cna_test_easygl_shadereffect_custom_vertex_layout
                        easygl_shadereffect_custom_vertex_layout_test.cpp)
        cna_register_renderer_test(NAME EasyGL_ShaderEffect_CustomVertexLayout COMMAND cna_test_easygl_shadereffect_custom_vertex_layout
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 947: NormalMapping.fx (tangent-space normal mapping + Phong specular) -- with
        # this, NormalMapping sample's shader blocker is fully cleared. Uses Task 1080's custom
        # vertex layout (Position+TexCoord+Normal+Binormal+Tangent, stride 56).
        cna_easygl_test(cna_test_easygl_normalmapping_shader
                        easygl_normalmapping_shader_test.cpp)
        cna_register_renderer_test(NAME EasyGL_NormalMapping_Shader COMMAND cna_test_easygl_normalmapping_shader
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 947: Billboard.fx (view-facing billboard expansion + wind sway + alpha-tested
        # diffuse lighting) -- with this, BillboardSample's shader blocker is fully cleared.
        # Uses Task 1080's custom vertex layout (Position+Normal+TexCoord+Random, stride 36).
        cna_easygl_test(cna_test_easygl_billboard_shader
                        easygl_billboard_shader_test.cpp)
        cna_register_renderer_test(NAME EasyGL_Billboard_Shader COMMAND cna_test_easygl_billboard_shader
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 947: ShatterEffect.fx (per-triangle rotation-and-fall shatter animation + Phong
        # lighting) -- with this, ShatterEffect sample's shader blocker is fully cleared. Uses
        # Task 1080's custom vertex layout (Position+Normal+TexCoords+TriangleCenter+
        # RotationalVelocity, stride 56).
        cna_easygl_test(cna_test_easygl_shattereffect_shader
                        easygl_shattereffect_shader_test.cpp)
        cna_register_renderer_test(NAME EasyGL_ShatterEffect_Shader COMMAND cna_test_easygl_shattereffect_shader
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 947: ParticleEffect.fx (GPU-animated billboarded particles) -- shared verbatim by
        # Particles3DSample and XmlParticles, so this clears both samples' shader blockers at
        # once. Uses Task 1080's custom vertex layout (Corner+Position+Velocity+Random+Time,
        # stride 52).
        cna_easygl_test(cna_test_easygl_particleeffect_shader
                        easygl_particleeffect_shader_test.cpp)
        cna_register_renderer_test(NAME EasyGL_ParticleEffect_Shader COMMAND cna_test_easygl_particleeffect_shader
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 947: ShipGame's AnimSprite.fx (sprite-sheet frame cross-fade), 1 of ShipGame's 4
        # distinct custom shaders. Uses VertexPositionTexture (stride 20, already supported).
        cna_easygl_test(cna_test_easygl_animsprite_shader
                        easygl_animsprite_shader_test.cpp)
        cna_register_renderer_test(NAME EasyGL_AnimSprite_Shader COMMAND cna_test_easygl_animsprite_shader
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 947: ShipGame's Blur.fx (5-technique box blur / colour / colour-texture), 2 of
        # ShipGame's 4 distinct custom shaders now ported.
        cna_easygl_test(cna_test_easygl_blur_shader
                        easygl_blur_shader_test.cpp)
        cna_register_renderer_test(NAME EasyGL_ShipGame_Blur_Shader COMMAND cna_test_easygl_blur_shader
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 1081: TextureCube sampling support for custom ShaderEffect shaders (needed for
        # ShipGame's own NormalMapping.fx reflection map).
        cna_easygl_test(cna_test_easygl_shadereffect_texturecube
                        easygl_shadereffect_texturecube_test.cpp)
        cna_register_renderer_test(NAME EasyGL_ShaderEffect_TextureCube COMMAND cna_test_easygl_shadereffect_texturecube
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # plans/plan_graphics.md Task 863: Texture3D now inherits Texture (matching FNA), and
        # ShaderEffect::SetTexture(int, Texture3D&)/IEffectRenderer::BindTexture3D() let a custom
        # shader sample a real sampler3D uniform -- same shape as Task 1081's TextureCube proof
        # above, adapted to a volume texture.
        # OPENGLES2 (docs/opengles2-renderer.md): this test exists to exercise an ES 3.0-level
        # feature the profile truthfully refuses (SupportsCapability answers false), so it is not
        # registered there -- the refusal contract itself is covered by the shared capability and
        # lifecycle tests.
        if(NOT CNA_GRAPHICS_RENDERER STREQUAL "OPENGLES2")
            cna_easygl_test(cna_test_easygl_shadereffect_texture3d
                            easygl_shadereffect_texture3d_test.cpp)
            cna_register_renderer_test(NAME EasyGL_ShaderEffect_Texture3D COMMAND cna_test_easygl_shadereffect_texture3d
                TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
        endif()

        # Task 947: ShipGame's own NormalMapping.fx (3 techniques: PlainMapping/NormalMapping/
        # ViewMapping) -- 3 of ShipGame's 4 distinct custom shaders now ported. Uses Task 1081's
        # TextureCube capability.
        cna_easygl_test(cna_test_easygl_shipgame_normalmapping_shader
                        easygl_shipgame_normalmapping_shader_test.cpp)
        cna_register_renderer_test(NAME EasyGL_ShipGame_NormalMapping_Shader COMMAND cna_test_easygl_shipgame_normalmapping_shader
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 947: ShipGame's own Particle.fx (real GPU point sprites via PSIZE/gl_PointSize) --
        # the 4th and final ShipGame custom shader, fully clearing ShipGame's shader blocker.
        cna_easygl_test(cna_test_easygl_shipgame_particle_shader
                        easygl_shipgame_particle_shader_test.cpp)
        cna_register_renderer_test(NAME EasyGL_ShipGame_Particle_Shader COMMAND cna_test_easygl_shipgame_particle_shader
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 947 (final sample) + Task 1082: InstancedModel.fx's HardwareInstancing technique --
        # proves real GPU hardware instancing (glVertexAttribDivisor-based per-instance vertex
        # stream through a custom ShaderEffect), completing Task 947 at 13/13.
        # OPENGLES2 (docs/opengles2-renderer.md): this test exists to exercise an ES 3.0-level
        # feature the profile truthfully refuses (SupportsCapability answers false), so it is not
        # registered there -- the refusal contract itself is covered by the shared capability and
        # lifecycle tests.
        if(NOT CNA_GRAPHICS_RENDERER STREQUAL "OPENGLES2")
            cna_easygl_test(cna_test_easygl_instancedmodel_shader
                            easygl_instancedmodel_shader_test.cpp)
            cna_register_renderer_test(NAME EasyGL_InstancedModel_Shader COMMAND cna_test_easygl_instancedmodel_shader
                TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
        endif()

        # Task 133: DualTextureEffect — magenta×yellow→red pixel readback
        cna_easygl_test(cna_test_easygl_dual_texture
                        easygl_dual_texture_test.cpp)
        cna_register_renderer_test(NAME EasyGL_DualTextureEffect_Blend COMMAND cna_test_easygl_dual_texture
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 134: EnvironmentMapEffect — emissive red, envAmount=0, pixel readback
        cna_easygl_test(cna_test_easygl_env_map
                        easygl_env_map_test.cpp)
        cna_register_renderer_test(NAME EasyGL_EnvironmentMapEffect_Readback COMMAND cna_test_easygl_env_map
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # audit_net.md remediation (2026-07-18, sixth round): discriminating regression test for
        # EmissiveColor composition (light*diffuse + emissive, NOT (emissive+light)*diffuse) across
        # all three EasyGL shader paths that had the bug. Uses a DiffuseColor strictly between 0
        # and 1 so a squared term is detectable - every pre-existing test used 0/1 components,
        # where x*x == x, which is why the bug went unnoticed.
        cna_easygl_test(cna_test_easygl_emissive_ambient_composition
                        easygl_emissive_ambient_composition_test.cpp)
        cna_register_renderer_test(NAME EasyGL_EmissiveAmbientComposition COMMAND cna_test_easygl_emissive_ambient_composition
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 393: EnvironmentMapEffect EnvironmentMapAmount=0 should ignore the cube map
        cna_easygl_test(cna_test_easygl_environmentmapeffect_amount_zero
                        easygl_environmentmapeffect_amount_zero_test.cpp)
        cna_register_renderer_test(NAME EasyGL_EnvironmentMapEffect_AmountZero COMMAND cna_test_easygl_environmentmapeffect_amount_zero
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 900: EnvironmentMapEffect fog fix verification
        cna_easygl_test(cna_test_easygl_environmentmapeffect_fog
                        easygl_environmentmapeffect_fog_test.cpp)
        cna_register_renderer_test(NAME EasyGL_EnvironmentMapEffect_Fog COMMAND cna_test_easygl_environmentmapeffect_fog
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 394: EnvironmentMapEffect EnvironmentMapAmount=1 should fully replace lit color
        cna_easygl_test(cna_test_easygl_environmentmapeffect_amount_one
                        easygl_environmentmapeffect_amount_one_test.cpp)
        cna_register_renderer_test(NAME EasyGL_EnvironmentMapEffect_AmountOne COMMAND cna_test_easygl_environmentmapeffect_amount_one
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 395: EnvironmentMapEffect EnvironmentMapSpecular should scale by cubemap alpha
        cna_easygl_test(cna_test_easygl_environmentmapeffect_specular
                        easygl_environmentmapeffect_specular_test.cpp)
        cna_register_renderer_test(NAME EasyGL_EnvironmentMapEffect_Specular COMMAND cna_test_easygl_environmentmapeffect_specular
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 891: EnvironmentMapEffect's base cube-map lerp target must be scaled by the
        # combined texture x diffuse alpha -- verbatim reuse of the shared renderer-agnostic
        # source, registered on all 3 renderers.
        cna_easygl_test(cna_test_easygl_environmentmapeffect_alphascaledlerp
                        ${CNA_GRAPHICS_EXAMPLES_DIR}/environmentmapeffect_alphascaledlerp_test.cpp)
        cna_register_renderer_test(NAME EasyGL_EnvironmentMapEffect_AlphaScaledLerp COMMAND cna_test_easygl_environmentmapeffect_alphascaledlerp
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 396: EnvironmentMapEffect Fresnel edge-weighting should suppress head-on reflections
        cna_easygl_test(cna_test_easygl_environmentmapeffect_fresnel
                        easygl_environmentmapeffect_fresnel_test.cpp)
        cna_register_renderer_test(NAME EasyGL_EnvironmentMapEffect_Fresnel COMMAND cna_test_easygl_environmentmapeffect_fresnel
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 1112: EnvironmentMapEffect's Fresnel term must be computed per-vertex and Gouraud-
        # interpolated, not recomputed per-fragment from a renormalized interpolated normal.
        cna_easygl_test(cna_test_easygl_environmentmapeffect_fresnel_gradient
                        easygl_environmentmapeffect_fresnel_gradient_test.cpp)
        cna_register_renderer_test(NAME EasyGL_EnvironmentMapEffect_Fresnel_Gradient COMMAND cna_test_easygl_environmentmapeffect_fresnel_gradient
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 397: EnvironmentMapEffect reflection vector should respond to EyePosition
        cna_easygl_test(cna_test_easygl_environmentmapeffect_eyeposition
                        easygl_environmentmapeffect_eyeposition_test.cpp)
        cna_register_renderer_test(NAME EasyGL_EnvironmentMapEffect_EyePosition COMMAND cna_test_easygl_environmentmapeffect_eyeposition
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 398: EnvironmentMapEffect normal transform should use inverse-transpose of World
        cna_easygl_test(cna_test_easygl_environmentmapeffect_worldtransform
                        easygl_environmentmapeffect_worldtransform_test.cpp)
        cna_register_renderer_test(NAME EasyGL_EnvironmentMapEffect_WorldTransform COMMAND cna_test_easygl_environmentmapeffect_worldtransform
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 399: cross-renderer EnvironmentMapEffect capstone (Tasks 393-398 combined)
        cna_easygl_test(cna_test_easygl_environmentmapeffect_combined
                        easygl_environmentmapeffect_combined_test.cpp)
        cna_register_renderer_test(NAME EasyGL_EnvironmentMapEffect_Combined COMMAND cna_test_easygl_environmentmapeffect_combined
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 890: EnvironmentMapEffect DirectionalLight1/DirectionalLight2 forwarding
        cna_easygl_test(cna_test_easygl_environmentmapeffect_multilight
                        easygl_environmentmapeffect_multilight_test.cpp)
        cna_register_renderer_test(NAME EasyGL_EnvironmentMapEffect_MultiLight COMMAND cna_test_easygl_environmentmapeffect_multilight
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 900: SkinnedEffect fog fix verification
        cna_easygl_test(cna_test_easygl_skinnedeffect_fog
                        easygl_skinnedeffect_fog_test.cpp)
        cna_register_renderer_test(NAME EasyGL_SkinnedEffect_Fog COMMAND cna_test_easygl_skinnedeffect_fog
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-010: transformed-camera view-space fog conformance (fails pre-fix).
        cna_easygl_test(cna_test_easygl_viewspace_fog
                        easygl_viewspace_fog_test.cpp)
        cna_register_renderer_test(NAME EasyGL_ViewSpace_Fog COMMAND cna_test_easygl_viewspace_fog
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 893: SkinnedEffect DirectionalLight1/DirectionalLight2 forwarding
        cna_easygl_test(cna_test_easygl_skinnedeffect_multilight
                        easygl_skinnedeffect_multilight_test.cpp)
        cna_register_renderer_test(NAME EasyGL_SkinnedEffect_MultiLight COMMAND cna_test_easygl_skinnedeffect_multilight
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-008: analytic SkinnedEffect ambient/emissive lighting conformance
        cna_easygl_test(cna_test_easygl_skinnedeffect_lighting_conformance
                        ${CNA_GRAPHICS_EXAMPLES_DIR}/skinnedeffect_lighting_conformance_test.cpp)
        cna_register_renderer_test(NAME EasyGL_SkinnedEffect_LightingConformance COMMAND cna_test_easygl_skinnedeffect_lighting_conformance
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 894: SkinnedEffect real specular highlights
        cna_easygl_test(cna_test_easygl_skinnedeffect_specular
                        easygl_skinnedeffect_specular_test.cpp)
        cna_register_renderer_test(NAME EasyGL_SkinnedEffect_Specular COMMAND cna_test_easygl_skinnedeffect_specular
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 1102b: SkinnedEffect pixel test — PreferPerPixelLighting genuinely selects between
        # a real per-vertex-lit shader variant and the existing per-pixel-lit one (mirrors Task
        # 1102's BasicEffect test exactly).
        cna_easygl_test(cna_test_easygl_skinnedeffect_preferperpixellighting
                        easygl_skinnedeffect_preferperpixellighting_test.cpp)
        cna_register_renderer_test(NAME EasyGL_SkinnedEffect_PreferPerPixelLighting COMMAND cna_test_easygl_skinnedeffect_preferperpixellighting
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 895: SkinnedEffect WeightsPerVertex real GPU enforcement
        cna_easygl_test(cna_test_easygl_skinnedeffect_weightspervertex
                        easygl_skinnedeffect_weightspervertex_test.cpp)
        cna_register_renderer_test(NAME EasyGL_SkinnedEffect_WeightsPerVertex COMMAND cna_test_easygl_skinnedeffect_weightspervertex
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 406: SkinnedEffect identity bone palette should produce zero deformation
        cna_easygl_test(cna_test_easygl_skinnedeffect_identity_bones
                        easygl_skinnedeffect_identity_bones_test.cpp)
        cna_register_renderer_test(NAME EasyGL_SkinnedEffect_IdentityBones COMMAND cna_test_easygl_skinnedeffect_identity_bones
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 407: SkinnedEffect single translation bone should shift the mesh
        cna_easygl_test(cna_test_easygl_skinnedeffect_translation_bone
                        easygl_skinnedeffect_translation_bone_test.cpp)
        cna_register_renderer_test(NAME EasyGL_SkinnedEffect_TranslationBone COMMAND cna_test_easygl_skinnedeffect_translation_bone
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 408: SkinnedEffect two-bone weighted blend should produce midpoint deformation
        cna_easygl_test(cna_test_easygl_skinnedeffect_twobone_blend
                        easygl_skinnedeffect_twobone_blend_test.cpp)
        cna_register_renderer_test(NAME EasyGL_SkinnedEffect_TwoBoneBlend COMMAND cna_test_easygl_skinnedeffect_twobone_blend
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 409: cross-renderer SkinnedEffect capstone (identity + single-bone + two-bone blend)
        cna_easygl_test(cna_test_easygl_skinnedeffect_combined
                        easygl_skinnedeffect_combined_test.cpp)
        cna_register_renderer_test(NAME EasyGL_SkinnedEffect_Combined COMMAND cna_test_easygl_skinnedeffect_combined
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 334: RenderTargetCube sampled as TextureCube via EnvironmentMapEffect after unbinding
        cna_easygl_test(cna_test_easygl_rendertargetcube_sample
                        easygl_rendertargetcube_sample_test.cpp)
        cna_register_renderer_test(NAME EasyGL_RenderTargetCube_SampleAfterUnbind COMMAND cna_test_easygl_rendertargetcube_sample
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-096: renderer-neutral singular/plural cube-face binding parity.
        cna_easygl_test(cna_test_easygl_rendertargetcube_plural_binding
                        ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertargetcube_plural_binding_test.cpp)
        cna_register_renderer_test(NAME EasyGL_RenderTargetCube_PluralBinding
            COMMAND cna_test_easygl_rendertargetcube_plural_binding
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 877: RenderTargetCube honors its exact requested DepthFormat (None vs Depth24Stencil8)
        cna_easygl_test(cna_test_easygl_rendertargetcube_depthformat
                        easygl_rendertargetcube_depthformat_test.cpp)
        cna_register_renderer_test(NAME EasyGL_RenderTargetCube_DepthFormat COMMAND cna_test_easygl_rendertargetcube_depthformat
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 144: Model.Draw — 2-bone model, BasicEffect VertexColor, red quad, pixel readback
        cna_easygl_test(cna_test_easygl_model_draw
                        easygl_model_draw_test.cpp)
        cna_register_renderer_test(NAME EasyGL_ModelDraw_RedQuad COMMAND cna_test_easygl_model_draw
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # SAMPLE-061: ContentManager may construct model buffers on XNA's background loading
        # thread. The renderer must move one native GL context between complete frame/content
        # operations instead of leaving the main thread current or issuing context-free GL calls.
        cna_easygl_test(cna_test_easygl_background_content_context
                        easygl_background_content_context_test.cpp)
        cna_register_renderer_test(NAME EasyGL_BackgroundContent_ContextOwnership
            COMMAND cna_test_easygl_background_content_context
            TIMEOUT 30 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 927: Content.Load<Model>() via a real .model.json + binary vertex/index file
        # fixture -- exercises ModelTypeReader::Read() itself (the vertex-stride/vtable-size
        # corruption bug found while porting ../cna-samples), unlike Task 144's hand-built-in-C++
        # model above.
        cna_easygl_test(cna_test_easygl_model_json_reader
                        easygl_model_json_reader_test.cpp)
        cna_register_renderer_test(NAME EasyGL_ModelJsonReader_Quad COMMAND cna_test_easygl_model_json_reader
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 931: ModelTypeReader::Read() auto-selecting 32-bit indices once a mesh exceeds
        # 65535 vertices, mirroring real XNA's stock ModelProcessor.
        cna_easygl_test(cna_test_easygl_model_json_reader_32bit_indices
                        easygl_model_json_reader_32bit_indices_test.cpp)
        cna_register_renderer_test(NAME EasyGL_ModelJsonReader_32BitIndices COMMAND cna_test_easygl_model_json_reader_32bit_indices
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 932: ModelTypeReader::Read() binding a per-mesh "texture" field to BasicEffect.
        cna_easygl_test(cna_test_easygl_model_json_reader_texture
                        easygl_model_json_reader_texture_test.cpp)
        cna_register_renderer_test(NAME EasyGL_ModelJsonReader_Texture COMMAND cna_test_easygl_model_json_reader_texture
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 934: Content.Load<TextureCube>() via a real .dds cubemap file fixture --
        # exercises the new TextureCubeTypeReader (ContentManager.cpp).
        cna_easygl_test(cna_test_easygl_texturecube_content_load
                        easygl_texturecube_content_load_test.cpp)
        cna_register_renderer_test(NAME EasyGL_TextureCube_ContentLoad COMMAND cna_test_easygl_texturecube_content_load
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 937: ModelTypeReader::Read() gives each mesh its own real ModelBone (a child of
        # Root, named after the mesh) instead of leaving ParentBone null. Pure structural test,
        # no rendering.
        cna_easygl_test(cna_test_easygl_model_json_reader_bone_hierarchy
                        easygl_model_json_reader_bone_hierarchy_test.cpp)
        cna_register_renderer_test(NAME EasyGL_ModelJsonReader_BoneHierarchy COMMAND cna_test_easygl_model_json_reader_bone_hierarchy
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 941: ModelTypeReader::Read() parses .model.json's new "skeleton"/"animations"
        # fields into a SkinningData attached to Model.Tag, and honors a mesh's "effect":
        # "SkinnedEffect" + "texture" field. Pure structural test, no rendering.
        cna_easygl_test(cna_test_easygl_model_json_reader_skeleton
                        easygl_model_json_reader_skeleton_test.cpp)
        cna_register_renderer_test(NAME EasyGL_ModelJsonReader_Skeleton COMMAND cna_test_easygl_model_json_reader_skeleton
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 942: Pixel test -- AnimationPlayer::GetSkinTransforms() fed into a real, Content.
        # Load<Model>()-loaded mesh's SkinnedEffect::SetBoneTransforms() before Model.Draw()
        # actually deforms the rendered mesh as the clip advances.
        cna_easygl_test(cna_test_easygl_model_skinned_animation_playback
                        easygl_model_skinned_animation_playback_test.cpp)
        cna_register_renderer_test(NAME EasyGL_Model_SkinnedAnimationPlayback COMMAND cna_test_easygl_model_skinned_animation_playback
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 438: Pixel test -- Model with two meshes, each using its own distinct Effect.
        cna_easygl_test(cna_test_easygl_model_two_meshes_effects
                        easygl_model_two_meshes_effects_test.cpp)
        cna_register_renderer_test(NAME EasyGL_Model_TwoMeshesEffects COMMAND cna_test_easygl_model_two_meshes_effects
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 439: Pixel test -- a child mesh's ParentBone's absolute transform actually moves it.
        cna_easygl_test(cna_test_easygl_model_hierarchy_child_mesh
                        easygl_model_hierarchy_child_mesh_test.cpp)
        cna_register_renderer_test(NAME EasyGL_Model_HierarchyChildMesh COMMAND cna_test_easygl_model_hierarchy_child_mesh
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 145: MRT — SetRenderTargets({rt0,rt1}), draw green to attachment0, verify left=green right=blue
        # OPENGLES2 (docs/opengles2-renderer.md): this test exists to exercise an ES 3.0-level
        # feature the profile truthfully refuses (SupportsCapability answers false), so it is not
        # registered there -- the refusal contract itself is covered by the shared capability and
        # lifecycle tests.
        if(NOT CNA_GRAPHICS_RENDERER STREQUAL "OPENGLES2")
            cna_easygl_test(cna_test_easygl_mrt
                            easygl_mrt_test.cpp)
            cna_register_renderer_test(NAME EasyGL_MRT_TwoAttachments COMMAND cna_test_easygl_mrt
                TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
        endif()

        # Task 146: MSAA 4× — multisampled renderbuffer + resolve on Present
        cna_easygl_test(cna_test_easygl_msaa
                        easygl_msaa_test.cpp)
        cna_register_renderer_test(NAME EasyGL_MSAA_4x_Readback COMMAND cna_test_easygl_msaa
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 167: SpriteEffects — FlipHorizontally, FlipVertically, FlipHV pixel readback
        cna_easygl_test(cna_test_easygl_sprite_effects
                        easygl_sprite_effects_test.cpp)
        cna_register_renderer_test(NAME EasyGL_SpriteEffects_Flip COMMAND cna_test_easygl_sprite_effects
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 417: SpriteBatch Draw rotation must pivot around the specified origin
        cna_easygl_test(cna_test_easygl_spritebatch_rotation
                        easygl_spritebatch_rotation_test.cpp)
        cna_register_renderer_test(NAME EasyGL_SpriteBatch_RotationAroundOrigin COMMAND cna_test_easygl_spritebatch_rotation
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 418: SpriteBatch Draw scalar/Vector2 scale overloads produce expected size
        cna_easygl_test(cna_test_easygl_spritebatch_scale
                        easygl_spritebatch_scale_test.cpp)
        cna_register_renderer_test(NAME EasyGL_SpriteBatch_ScaleOverloads COMMAND cna_test_easygl_spritebatch_scale
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-019 cross-renderer parity: SpriteBatch INTO a RenderTarget2D whose size differs
        # from the backbuffer must place its destination rectangle in the render target's own pixel
        # space. Same geometry/probes as modules/renderers/webgpu/examples/webgpu_spritebatch_rendertarget_test.cpp, so EasyGL
        # (already render-target-aware) is an independent oracle for the WebGPU fix's expected bounds.
        cna_easygl_test(cna_test_easygl_spritebatch_rendertarget_size
                        easygl_spritebatch_rendertarget_size_test.cpp)
        cna_register_renderer_test(NAME EasyGL_SpriteBatch_RenderTargetSize COMMAND cna_test_easygl_spritebatch_rendertarget_size
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 419: SpriteBatch Draw sourceRectangle must crop, not stretch the whole texture
        cna_easygl_test(cna_test_easygl_spritebatch_sourcerect
                        easygl_spritebatch_sourcerect_test.cpp)
        cna_register_renderer_test(NAME EasyGL_SpriteBatch_SourceRectangleCropping COMMAND cna_test_easygl_spritebatch_sourcerect
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 420: layerDepth must affect on-screen draw order under FrontToBack
        cna_easygl_test(cna_test_easygl_spritebatch_layerdepth
                        easygl_spritebatch_layerdepth_test.cpp)
        cna_register_renderer_test(NAME EasyGL_SpriteBatch_LayerDepthOrder COMMAND cna_test_easygl_spritebatch_layerdepth
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 885: BasicEffect pixel test — DirectionalLight1/2 + EmissiveColor on the lit path
        cna_easygl_test(cna_test_easygl_basiceffect_multilight_emissive
                        easygl_basiceffect_multilight_emissive_test.cpp)
        cna_register_renderer_test(NAME EasyGL_BasicEffect_MultiLightEmissive COMMAND cna_test_easygl_basiceffect_multilight_emissive
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 886: BasicEffect pixel test — real specular highlights
        cna_easygl_test(cna_test_easygl_basiceffect_specular
                        easygl_basiceffect_specular_test.cpp)
        cna_register_renderer_test(NAME EasyGL_BasicEffect_Specular COMMAND cna_test_easygl_basiceffect_specular
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 1102: BasicEffect pixel test — PreferPerPixelLighting genuinely selects between
        # a real per-vertex-lit shader variant and the existing per-pixel-lit one.
        cna_easygl_test(cna_test_easygl_basiceffect_preferperpixellighting
                        easygl_basiceffect_preferperpixellighting_test.cpp)
        cna_register_renderer_test(NAME EasyGL_BasicEffect_PreferPerPixelLighting COMMAND cna_test_easygl_basiceffect_preferperpixellighting
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 168: transformMatrix in SpriteBatch::Begin — translation pixel readback
        cna_easygl_test(cna_test_easygl_transform_matrix
                        easygl_transform_matrix_test.cpp)
        cna_register_renderer_test(NAME EasyGL_TransformMatrix_Translation COMMAND cna_test_easygl_transform_matrix
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 169: Texture2D SetData/GetData partial rectangle round-trip
        cna_easygl_test(cna_test_easygl_texture2d_partial_rect
                        easygl_texture2d_partial_rect_test.cpp)
        cna_register_renderer_test(NAME EasyGL_Texture2D_PartialRect_RoundTrip COMMAND cna_test_easygl_texture2d_partial_rect
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 171: Texture2D mip-level SetData/GetData round-trip
        cna_easygl_test(cna_test_easygl_texture2d_mip
                        easygl_texture2d_mip_test.cpp)
        cna_register_renderer_test(NAME EasyGL_Texture2D_Mip_RoundTrip COMMAND cna_test_easygl_texture2d_mip
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 172: TextureCube per-face SetData/GetData round-trip
        cna_easygl_test(cna_test_easygl_texturecube_faces
                        easygl_texturecube_faces_test.cpp)
        cna_register_renderer_test(NAME EasyGL_TextureCube_Faces_RoundTrip COMMAND cna_test_easygl_texturecube_faces
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 276: TextureCube mip-level SetData/GetData round-trip, all six faces
        cna_easygl_test(cna_test_easygl_texturecube_mip
                        easygl_texturecube_mip_test.cpp)
        cna_register_renderer_test(NAME EasyGL_TextureCube_Mip_RoundTrip COMMAND cna_test_easygl_texturecube_mip
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 275: TextureCube partial rect + startIndex, all six faces
        cna_easygl_test(cna_test_easygl_texturecube_partial_rect
                        easygl_texturecube_partial_rect_test.cpp)
        cna_register_renderer_test(NAME EasyGL_TextureCube_PartialRect_RoundTrip COMMAND cna_test_easygl_texturecube_partial_rect
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 173: Texture3D z-slice SetData/GetData round-trip
        # OPENGLES2 (docs/opengles2-renderer.md): this test exists to exercise an ES 3.0-level
        # feature the profile truthfully refuses (SupportsCapability answers false), so it is not
        # registered there -- the refusal contract itself is covered by the shared capability and
        # lifecycle tests.
        if(NOT CNA_GRAPHICS_RENDERER STREQUAL "OPENGLES2")
            cna_easygl_test(cna_test_easygl_texture3d_slices
                            easygl_texture3d_slices_test.cpp)
            cna_register_renderer_test(NAME EasyGL_Texture3D_Slices_RoundTrip COMMAND cna_test_easygl_texture3d_slices
                TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
        endif()

        # Task 862: Texture3D mip-level SetData/GetData round-trip
        # OPENGLES2 (docs/opengles2-renderer.md): this test exists to exercise an ES 3.0-level
        # feature the profile truthfully refuses (SupportsCapability answers false), so it is not
        # registered there -- the refusal contract itself is covered by the shared capability and
        # lifecycle tests.
        if(NOT CNA_GRAPHICS_RENDERER STREQUAL "OPENGLES2")
            cna_easygl_test(cna_test_easygl_texture3d_mip
                            easygl_texture3d_mip_test.cpp)
            cna_register_renderer_test(NAME EasyGL_Texture3D_Mip_RoundTrip COMMAND cna_test_easygl_texture3d_mip
                TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
        endif()

        # Task 273: Texture3D partial box (x/y/z region) SetData/GetData round-trip
        # OPENGLES2 (docs/opengles2-renderer.md): this test exists to exercise an ES 3.0-level
        # feature the profile truthfully refuses (SupportsCapability answers false), so it is not
        # registered there -- the refusal contract itself is covered by the shared capability and
        # lifecycle tests.
        if(NOT CNA_GRAPHICS_RENDERER STREQUAL "OPENGLES2")
            cna_easygl_test(cna_test_easygl_texture3d_partial_box
                            easygl_texture3d_partial_box_test.cpp)
            cna_register_renderer_test(NAME EasyGL_Texture3D_PartialBox_RoundTrip COMMAND cna_test_easygl_texture3d_partial_box
                TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
        endif()

        # Task 274: Texture3D partial box (x/y/z region) GetData readback
        # OPENGLES2 (docs/opengles2-renderer.md): this test exists to exercise an ES 3.0-level
        # feature the profile truthfully refuses (SupportsCapability answers false), so it is not
        # registered there -- the refusal contract itself is covered by the shared capability and
        # lifecycle tests.
        if(NOT CNA_GRAPHICS_RENDERER STREQUAL "OPENGLES2")
            cna_easygl_test(cna_test_easygl_texture3d_partial_box_readback
                            easygl_texture3d_partial_box_readback_test.cpp)
            cna_register_renderer_test(NAME EasyGL_Texture3D_PartialBox_Readback COMMAND cna_test_easygl_texture3d_partial_box_readback
                TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
        endif()

        # Task 176: SurfaceFormat validation — unsupported formats throw
        cna_easygl_test(cna_test_easygl_surface_format_throws
                        easygl_surface_format_throws_test.cpp)
        cna_register_renderer_test(NAME EasyGL_SurfaceFormat_Throws COMMAND cna_test_easygl_surface_format_throws
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 177: RenderTargetUsage DiscardContents vs PreserveContents
        cna_easygl_test(cna_test_easygl_render_target_usage
                        easygl_render_target_usage_test.cpp)
        cna_register_renderer_test(NAME EasyGL_RenderTargetUsage COMMAND cna_test_easygl_render_target_usage
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 180: backbuffer → RT → backbuffer → RT → backbuffer round-trip
        cna_easygl_test(cna_test_easygl_rt_roundtrip
                        easygl_rt_roundtrip_test.cpp)
        cna_register_renderer_test(NAME EasyGL_RT_Roundtrip COMMAND cna_test_easygl_rt_roundtrip
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 182: PresentationParameters round-trip via GraphicsDeviceManager
        cna_easygl_test(cna_test_easygl_presentation_parameters
                        easygl_presentation_parameters_test.cpp)
        cna_register_renderer_test(NAME EasyGL_PresentationParameters COMMAND cna_test_easygl_presentation_parameters
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_device_reset_events
                        easygl_device_reset_events_test.cpp)
        cna_register_renderer_test(NAME EasyGL_DeviceResetEvents COMMAND cna_test_easygl_device_reset_events
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_device_validation
                        easygl_device_validation_test.cpp)
        cna_register_renderer_test(NAME EasyGL_DeviceValidation COMMAND cna_test_easygl_device_validation
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_draw_novertexbuffer
                        easygl_draw_novertexbuffer_test.cpp)
        cna_register_renderer_test(NAME EasyGL_DrawNoVertexBuffer COMMAND cna_test_easygl_draw_novertexbuffer
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_draw_noindexbuffer
                        easygl_draw_noindexbuffer_test.cpp)
        cna_register_renderer_test(NAME EasyGL_DrawNoIndexBuffer COMMAND cna_test_easygl_draw_noindexbuffer
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_draw_range_validation
                        easygl_draw_range_validation_test.cpp)
        cna_register_renderer_test(NAME EasyGL_DrawRangeValidation COMMAND cna_test_easygl_draw_range_validation
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_primitivetype_validation
                        easygl_primitivetype_validation_test.cpp)
        cna_register_renderer_test(NAME EasyGL_PrimitiveTypeValidation COMMAND cna_test_easygl_primitivetype_validation
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_clear_overloads
                        easygl_clear_overloads_test.cpp)
        cna_register_renderer_test(NAME EasyGL_ClearOverloads COMMAND cna_test_easygl_clear_overloads
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_viewport_state
                        easygl_viewport_state_test.cpp)
        cna_register_renderer_test(NAME EasyGL_ViewportState COMMAND cna_test_easygl_viewport_state
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_scissor
                        easygl_scissor_test.cpp)
        cna_register_renderer_test(NAME EasyGL_Scissor COMMAND cna_test_easygl_scissor
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 880: Viewport sub-region (split-screen) GPU wiring
        cna_easygl_test(cna_test_easygl_viewport_subregion
                        easygl_viewport_subregion_test.cpp)
        cna_register_renderer_test(NAME EasyGL_Viewport_Subregion COMMAND cna_test_easygl_viewport_subregion
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_disposed_resource
                        easygl_disposed_resource_test.cpp)
        cna_register_renderer_test(NAME EasyGL_DisposedResource COMMAND cna_test_easygl_disposed_resource
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_double_dispose
                        easygl_double_dispose_test.cpp)
        cna_register_renderer_test(NAME EasyGL_DoubleDispose COMMAND cna_test_easygl_double_dispose
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_bound_resource_dispose
                        easygl_bound_resource_dispose_test.cpp)
        cna_register_renderer_test(NAME EasyGL_BoundResourceDispose COMMAND cna_test_easygl_bound_resource_dispose
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_handle_release
                        easygl_handle_release_test.cpp)
        cna_register_renderer_test(NAME EasyGL_HandleRelease COMMAND cna_test_easygl_handle_release
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_move_semantics
                        easygl_move_semantics_test.cpp)
        cna_register_renderer_test(NAME EasyGL_MoveSemantics COMMAND cna_test_easygl_move_semantics
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_resource_events
                        easygl_resource_events_test.cpp)
        cna_register_renderer_test(NAME EasyGL_ResourceEvents COMMAND cna_test_easygl_resource_events
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_device_dispose_order
                        easygl_device_dispose_order_test.cpp)
        cna_register_renderer_test(NAME EasyGL_DeviceDisposeOrder COMMAND cna_test_easygl_device_dispose_order
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_resource_leak
                        easygl_resource_leak_test.cpp)
        cna_register_renderer_test(NAME EasyGL_ResourceLeak COMMAND cna_test_easygl_resource_leak
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_present_interval
                        easygl_present_interval_test.cpp)
        cna_register_renderer_test(NAME EasyGL_PresentInterval COMMAND cna_test_easygl_present_interval
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_fullscreen_field
                        easygl_fullscreen_field_test.cpp)
        cna_register_renderer_test(NAME EasyGL_FullScreenField COMMAND cna_test_easygl_fullscreen_field
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_backbuffer_resize
                        easygl_backbuffer_resize_test.cpp)
        cna_register_renderer_test(NAME EasyGL_BackbufferResize COMMAND cna_test_easygl_backbuffer_resize
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_real_window_resize
                        easygl_real_window_resize_test.cpp)
        cna_register_renderer_test(NAME EasyGL_RealWindowResize COMMAND cna_test_easygl_real_window_resize
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 349: custom Viewport survives Present() with no resize, resets after a real one.
        cna_easygl_test(cna_test_easygl_viewport_reset_after_resize
                        ${CNA_GRAPHICS_EXAMPLES_DIR}/viewport_reset_after_resize_test.cpp)
        cna_register_renderer_test(NAME EasyGL_ViewportResetAfterResize COMMAND cna_test_easygl_viewport_reset_after_resize
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_depth_format
                        easygl_depth_format_test.cpp)
        cna_register_renderer_test(NAME EasyGL_DepthFormat COMMAND cna_test_easygl_depth_format
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_msaa_change
                        easygl_msaa_change_test.cpp)
        cna_register_renderer_test(NAME EasyGL_MsaaChange COMMAND cna_test_easygl_msaa_change
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_vertexbuffer_setdata
                        easygl_vertexbuffer_setdata_test.cpp)
        cna_register_renderer_test(NAME EasyGL_VbSetData COMMAND cna_test_easygl_vertexbuffer_setdata
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_dynamic_buffer_stress
                        easygl_dynamic_buffer_stress_test.cpp)
        cna_register_renderer_test(NAME EasyGL_DynamicBufferStress COMMAND cna_test_easygl_dynamic_buffer_stress
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_buffer_usage
                        easygl_buffer_usage_test.cpp)
        cna_register_renderer_test(NAME EasyGL_BufferUsage COMMAND cna_test_easygl_buffer_usage
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_disposed_buffer
                        easygl_disposed_buffer_test.cpp)
        cna_register_renderer_test(NAME EasyGL_DisposedBuffer COMMAND cna_test_easygl_disposed_buffer
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_vertexbuffer_indexbuffer_getdata
                        easygl_vertexbuffer_indexbuffer_getdata_test.cpp)
        cna_register_renderer_test(NAME EasyGL_VertexBufferIndexBufferGetData COMMAND cna_test_easygl_vertexbuffer_indexbuffer_getdata
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_effect_clone
                        easygl_effect_clone_test.cpp)
        cna_register_renderer_test(NAME EasyGL_EffectClone COMMAND cna_test_easygl_effect_clone
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_effect_current_technique
                        easygl_effect_current_technique_test.cpp)
        cna_register_renderer_test(NAME EasyGL_EffectCurrentTechnique COMMAND cna_test_easygl_effect_current_technique
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_basiceffect_combinations
                        easygl_basiceffect_combinations_test.cpp)
        cna_register_renderer_test(NAME EasyGL_BasicEffectCombinations COMMAND cna_test_easygl_basiceffect_combinations
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 364: BasicEffect pixel test — VertexColorEnabled=false, no texture, diffuse color only
        cna_easygl_test(cna_test_easygl_basiceffect_vertexcolor_disabled
                        easygl_basiceffect_vertexcolor_disabled_test.cpp)
        cna_register_renderer_test(NAME EasyGL_BasicEffect_VertexColorDisabled COMMAND cna_test_easygl_basiceffect_vertexcolor_disabled
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 365: BasicEffect pixel test — VertexColorEnabled=true, no texture, vertex color multiplication
        cna_easygl_test(cna_test_easygl_basiceffect_vertexcolor_enabled
                        easygl_basiceffect_vertexcolor_enabled_test.cpp)
        cna_register_renderer_test(NAME EasyGL_BasicEffect_VertexColorEnabled COMMAND cna_test_easygl_basiceffect_vertexcolor_enabled
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 366: BasicEffect pixel test — TextureEnabled=true, no vertex color
        cna_easygl_test(cna_test_easygl_basiceffect_texture_enabled
                        easygl_basiceffect_texture_enabled_test.cpp)
        cna_register_renderer_test(NAME EasyGL_BasicEffect_TextureEnabled COMMAND cna_test_easygl_basiceffect_texture_enabled
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 367: BasicEffect pixel test — TextureEnabled=true AND VertexColorEnabled=true
        cna_easygl_test(cna_test_easygl_basiceffect_texture_vertexcolor_enabled
                        easygl_basiceffect_texture_vertexcolor_enabled_test.cpp)
        cna_register_renderer_test(NAME EasyGL_BasicEffect_TextureVertexColorEnabled COMMAND cna_test_easygl_basiceffect_texture_vertexcolor_enabled
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 368: BasicEffect pixel test — LightingEnabled=true, one DirectionalLight
        cna_easygl_test(cna_test_easygl_basiceffect_one_light
                        easygl_basiceffect_one_light_test.cpp)
        cna_register_renderer_test(NAME EasyGL_BasicEffect_OneLight COMMAND cna_test_easygl_basiceffect_one_light
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 369: BasicEffect pixel test — DiffuseColor+EmissiveColor, LightingEnabled=false
        cna_easygl_test(cna_test_easygl_basiceffect_emissive
                        easygl_basiceffect_emissive_test.cpp)
        cna_register_renderer_test(NAME EasyGL_BasicEffect_Emissive COMMAND cna_test_easygl_basiceffect_emissive
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 370: BasicEffect cross-renderer image comparison suite — closes Phase 42
        cna_easygl_test(cna_test_easygl_basiceffect_combined
                        easygl_basiceffect_combined_test.cpp)
        cna_register_renderer_test(NAME EasyGL_BasicEffect_Combined COMMAND cna_test_easygl_basiceffect_combined
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_alphatest_modes
                        easygl_alphatest_modes_test.cpp)
        cna_register_renderer_test(NAME EasyGL_AlphaTestModes COMMAND cna_test_easygl_alphatest_modes
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 373: AlphaTestEffect all CompareFunction modes — threshold sweep (below/at/above)
        cna_easygl_test(cna_test_easygl_alphatest_comparefunction_sweep
                        easygl_alphatest_comparefunction_sweep_test.cpp)
        cna_register_renderer_test(NAME EasyGL_AlphaTest_CompareFunctionSweep COMMAND cna_test_easygl_alphatest_comparefunction_sweep
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 377: AlphaTestEffect vertex color x diffuse color interaction
        cna_easygl_test(cna_test_easygl_alphatest_vertexcolor_diffuse
                        easygl_alphatest_vertexcolor_diffuse_test.cpp)
        cna_register_renderer_test(NAME EasyGL_AlphaTest_VertexColorDiffuse COMMAND cna_test_easygl_alphatest_vertexcolor_diffuse
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 378: AlphaTestEffect fog behavior
        cna_easygl_test(cna_test_easygl_alphatest_fog
                        easygl_alphatest_fog_test.cpp)
        cna_register_renderer_test(NAME EasyGL_AlphaTest_Fog COMMAND cna_test_easygl_alphatest_fog
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 379: AlphaTestEffect null/no-texture behavior
        cna_easygl_test(cna_test_easygl_alphatest_null_texture
                        easygl_alphatest_null_texture_test.cpp)
        cna_register_renderer_test(NAME EasyGL_AlphaTest_NullTexture COMMAND cna_test_easygl_alphatest_null_texture
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 383: DualTextureEffect two-texture blend formula, including FNA's color.rgb*=2
        cna_easygl_test(cna_test_easygl_dualtextureeffect_doubling
                        easygl_dualtextureeffect_doubling_test.cpp)
        cna_register_renderer_test(NAME EasyGL_DualTextureEffect_Doubling COMMAND cna_test_easygl_dualtextureeffect_doubling
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 385: DualTextureEffect Alpha premultiplication, verified via real AlphaBlend
        cna_easygl_test(cna_test_easygl_dualtextureeffect_alpha
                        easygl_dualtextureeffect_alpha_test.cpp)
        cna_register_renderer_test(NAME EasyGL_DualTextureEffect_Alpha COMMAND cna_test_easygl_dualtextureeffect_alpha
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 386: DualTextureEffect first texture (Texture, slot 0) null behavior
        cna_easygl_test(cna_test_easygl_dualtextureeffect_null_texture0
                        easygl_dualtextureeffect_null_texture0_test.cpp)
        cna_register_renderer_test(NAME EasyGL_DualTextureEffect_NullTexture0 COMMAND cna_test_easygl_dualtextureeffect_null_texture0
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 387: DualTextureEffect second texture (Texture2, slot 1) null behavior
        cna_easygl_test(cna_test_easygl_dualtextureeffect_null_texture2
                        easygl_dualtextureeffect_null_texture2_test.cpp)
        cna_register_renderer_test(NAME EasyGL_DualTextureEffect_NullTexture2 COMMAND cna_test_easygl_dualtextureeffect_null_texture2
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 388: DualTextureEffect fog behavior
        cna_easygl_test(cna_test_easygl_dualtextureeffect_fog
                        easygl_dualtextureeffect_fog_test.cpp)
        cna_register_renderer_test(NAME EasyGL_DualTextureEffect_Fog COMMAND cna_test_easygl_dualtextureeffect_fog
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 389: DualTextureEffect cross-renderer image comparison suite (closes Phase 44 pixel work)
        cna_easygl_test(cna_test_easygl_dualtextureeffect_combined
                        easygl_dualtextureeffect_combined_test.cpp)
        cna_register_renderer_test(NAME EasyGL_DualTextureEffect_Combined COMMAND cna_test_easygl_dualtextureeffect_combined
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_dualtexture
                        easygl_dualtexture_test.cpp)
        cna_register_renderer_test(NAME EasyGL_DualTexture COMMAND cna_test_easygl_dualtexture
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # SAMPLE-073: FNA's DualTextureEffect consumes TEXCOORD0 and TEXCOORD1 independently.
        cna_easygl_test(cna_test_easygl_dualtextureeffect_independent_uv
                        easygl_dualtextureeffect_independent_uv_test.cpp)
        cna_register_renderer_test(NAME EasyGL_DualTextureEffect_IndependentUV
            COMMAND cna_test_easygl_dualtextureeffect_independent_uv
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_skinned_effect_bones
                        easygl_skinned_effect_bones_test.cpp)
        cna_register_renderer_test(NAME EasyGL_SkinnedBones COMMAND cna_test_easygl_skinned_effect_bones
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_basiceffect_default_lighting
                        easygl_basiceffect_default_lighting_test.cpp)
        cna_register_renderer_test(NAME EasyGL_BasicEffectDefaultLighting COMMAND cna_test_easygl_basiceffect_default_lighting
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_basiceffect_fog
                        easygl_basiceffect_fog_test.cpp)
        cna_register_renderer_test(NAME EasyGL_BasicEffectFog COMMAND cna_test_easygl_basiceffect_fog
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # plans/plan_fx.md FX-123: D3D9 saturates a vertex shader's colour output registers before
        # interpolating them; the per-vertex-lit program's varyings must do the same.
        cna_easygl_test(cna_test_easygl_basiceffect_vertex_color_clamp
                        easygl_basiceffect_vertex_color_clamp_test.cpp)
        cna_register_renderer_test(NAME EasyGL_BasicEffectVertexColorClamp
            COMMAND cna_test_easygl_basiceffect_vertex_color_clamp
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # plans/plan_fx.md FX-124: the per-pixel-lit fragment stage normalizes a world-space view
        # vector, so mediump's fp16 range destroys specular on a large scene. Scale invariance.
        cna_easygl_test(cna_test_easygl_basiceffect_world_scale_precision
                        easygl_basiceffect_world_scale_precision_test.cpp)
        cna_register_renderer_test(NAME EasyGL_BasicEffectWorldScalePrecision
            COMMAND cna_test_easygl_basiceffect_world_scale_precision
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # plans/plan_fx.md FX-125: a lit, vertex-coloured 36-byte vertex must be BOTH lit and
        # coloured; it used to fall through to the unlit vertex-colour program.
        cna_easygl_test(cna_test_easygl_basiceffect_lit_vertex_color
                        easygl_basiceffect_lit_vertex_color_test.cpp)
        cna_register_renderer_test(NAME EasyGL_BasicEffectLitVertexColor
            COMMAND cna_test_easygl_basiceffect_lit_vertex_color
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # plans/plan_fx.md FX-127: a BLENDINDICES element declared Vector4 rather than Byte4 is
        # equally legal XNA -- the format describes the bytes, the shader register is a float4
        # either way -- and EasyGL refused it outright.
        cna_easygl_test(cna_test_easygl_skinnedeffect_vector4_bone_indices
                        easygl_skinnedeffect_vector4_bone_indices_test.cpp)
        cna_register_renderer_test(NAME EasyGL_SkinnedEffectVector4BoneIndices
            COMMAND cna_test_easygl_skinnedeffect_vector4_bone_indices
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 247: draw quad with each supported VertexElementFormat via VertexBuffer
        cna_easygl_test(cna_test_easygl_vertex_formats
                        easygl_vertex_formats_test.cpp)
        cna_register_renderer_test(NAME EasyGL_VertexFormats_AllStrides COMMAND cna_test_easygl_vertex_formats
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # plans/plan_gltf.md GLTF-157: a raw upload whose stride has no fixed layout and no explicit
        # VertexDeclaration must fail by name instead of inheriting stale VAO attributes.
        cna_easygl_test(cna_test_easygl_unknown_stride_rejection
                        easygl_unknown_stride_rejection_test.cpp)
        cna_register_renderer_test(NAME EasyGL_UnknownStride_Rejected COMMAND cna_test_easygl_unknown_stride_rejection
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 255: DrawUserPrimitives<VertexPositionColor> pixel-readback
        cna_easygl_test(cna_test_easygl_draw_user_primitives_vpc
                        easygl_draw_user_primitives_vpc_test.cpp)
        cna_register_renderer_test(NAME EasyGL_DrawUserPrimitives_VPC COMMAND cna_test_easygl_draw_user_primitives_vpc
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 256: DrawUserPrimitives with explicit VertexDeclaration pixel-readback
        cna_easygl_test(cna_test_easygl_draw_user_primitives_custom
                        easygl_draw_user_primitives_custom_test.cpp)
        cna_register_renderer_test(NAME EasyGL_DrawUserPrimitives_CustomVD COMMAND cna_test_easygl_draw_user_primitives_custom
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 257: DrawUserIndexedPrimitives<VertexPositionColor> (16-bit indices) pixel-readback
        cna_easygl_test(cna_test_easygl_draw_user_indexed_primitives_vpc
                        easygl_draw_user_indexed_primitives_vpc_test.cpp)
        cna_register_renderer_test(NAME EasyGL_DrawUserIndexedPrimitives_VPC COMMAND cna_test_easygl_draw_user_indexed_primitives_vpc
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 258: DrawUserIndexedPrimitives<VertexPositionColor> (32-bit indices) pixel-readback
        cna_easygl_test(cna_test_easygl_draw_user_indexed_primitives_32
                        easygl_draw_user_indexed_primitives_32_test.cpp)
        cna_register_renderer_test(NAME EasyGL_DrawUserIndexedPrimitives_32 COMMAND cna_test_easygl_draw_user_indexed_primitives_32
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 268: non-power-of-two (3x5) texture upload + GPU-sampled pixel-readback
        cna_easygl_test(cna_test_easygl_npot_texture
                        easygl_npot_texture_test.cpp)
        cna_register_renderer_test(NAME EasyGL_NpotTexture COMMAND cna_test_easygl_npot_texture
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 269: SpriteBatch TextureAddressMode (Wrap vs Clamp) edge-sampling pixel-readback
        cna_easygl_test(cna_test_easygl_texture_address_mode
                        easygl_texture_address_mode_test.cpp)
        cna_register_renderer_test(NAME EasyGL_TextureAddressMode COMMAND cna_test_easygl_texture_address_mode
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 737: SpriteBatch TextureAddressMode::Mirror edge-sampling pixel-readback -- Task 269
        # only independently verified Wrap/Clamp; Mirror is fixed by the same code path but was
        # never verified on its own until now.
        cna_easygl_test(cna_test_easygl_texture_address_mode_mirror
                        easygl_texture_address_mode_mirror_test.cpp)
        cna_register_renderer_test(NAME EasyGL_TextureAddressMode_Mirror COMMAND cna_test_easygl_texture_address_mode_mirror
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 424: Pixel test -- draw a single SpriteFont glyph at a known position on EasyGL.
        cna_easygl_test(cna_test_easygl_spritefont_single_glyph
                        easygl_spritefont_single_glyph_test.cpp)
        cna_register_renderer_test(NAME EasyGL_SpriteFont_SingleGlyph COMMAND cna_test_easygl_spritefont_single_glyph
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 425: Pixel test -- draw multiple SpriteFont glyphs with spacing on EasyGL.
        cna_easygl_test(cna_test_easygl_spritefont_multiglyph_spacing
                        easygl_spritefont_multiglyph_spacing_test.cpp)
        cna_register_renderer_test(NAME EasyGL_SpriteFont_MultiGlyphSpacing COMMAND cna_test_easygl_spritefont_multiglyph_spacing
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 426: Pixel test -- SpriteFont newline advances by line spacing on EasyGL.
        cna_easygl_test(cna_test_easygl_spritefont_newline
                        easygl_spritefont_newline_test.cpp)
        cna_register_renderer_test(NAME EasyGL_SpriteFont_Newline COMMAND cna_test_easygl_spritefont_newline
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 427: Pixel test -- SpriteFont default character fallback renders the expected
        # glyph on EasyGL.
        cna_easygl_test(cna_test_easygl_spritefont_default_char
                        easygl_spritefont_default_char_test.cpp)
        cna_register_renderer_test(NAME EasyGL_SpriteFont_DefaultChar COMMAND cna_test_easygl_spritefont_default_char
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 428: Verify SpriteEffects (flip) with DrawString on EasyGL.
        cna_easygl_test(cna_test_easygl_spritefont_effects_flip
                        easygl_spritefont_effects_flip_test.cpp)
        cna_register_renderer_test(NAME EasyGL_SpriteFont_EffectsFlip COMMAND cna_test_easygl_spritefont_effects_flip
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 429: Verify rotation/origin/scale with DrawString on EasyGL.
        cna_easygl_test(cna_test_easygl_spritefont_effects_rotation_scale
                        easygl_spritefont_effects_rotation_scale_test.cpp)
        cna_register_renderer_test(NAME EasyGL_SpriteFont_EffectsRotationScale COMMAND cna_test_easygl_spritefont_effects_rotation_scale
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 293: DualTextureEffect (3D stock effect) honors GraphicsDevice.SamplerStates[0]
        cna_easygl_test(cna_test_easygl_sampler_state_effect
                        easygl_sampler_state_effect_test.cpp)
        cna_register_renderer_test(NAME EasyGL_SamplerState_DualTextureEffect COMMAND cna_test_easygl_sampler_state_effect
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 294: TextureAddressMode::Clamp on a 3D stock effect (DualTextureEffect)
        cna_easygl_test(cna_test_easygl_texture_address_mode_clamp_effect
                        easygl_texture_address_mode_clamp_effect_test.cpp)
        cna_register_renderer_test(NAME EasyGL_TextureAddressMode_Clamp_DualTextureEffect COMMAND cna_test_easygl_texture_address_mode_clamp_effect
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 296: TextureAddressMode::Mirror on a 3D stock effect (DualTextureEffect)
        cna_easygl_test(cna_test_easygl_texture_address_mode_mirror_effect
                        easygl_texture_address_mode_mirror_effect_test.cpp)
        cna_register_renderer_test(NAME EasyGL_TextureAddressMode_Mirror_DualTextureEffect COMMAND cna_test_easygl_texture_address_mode_mirror_effect
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 297: TextureFilter::Point vs Linear, magnification and minification
        cna_easygl_test(cna_test_easygl_texture_filter_point_vs_linear
                        easygl_texture_filter_point_vs_linear_test.cpp)
        cna_register_renderer_test(NAME EasyGL_TextureFilter_PointVsLinear COMMAND cna_test_easygl_texture_filter_point_vs_linear
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 298: mipmap filter behavior (LinearMipPoint selects the correct mip; Point does not)
        cna_easygl_test(cna_test_easygl_texture_mip_filter_effect
                        easygl_texture_mip_filter_effect_test.cpp)
        cna_register_renderer_test(NAME EasyGL_TextureMipFilter_DualTextureEffect COMMAND cna_test_easygl_texture_mip_filter_effect
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 299: anisotropic filtering caps and fallback (extreme MaxAnisotropy must not crash)
        cna_easygl_test(cna_test_easygl_texture_anisotropic_effect
                        easygl_texture_anisotropic_effect_test.cpp)
        cna_register_renderer_test(NAME EasyGL_TextureAnisotropic_DualTextureEffect COMMAND cna_test_easygl_texture_anisotropic_effect
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 924 (split from Task 867): a single-level Texture2D sampled with a mipmap-requiring
        # TextureFilter (Anisotropic) must not render solid black (GL_TEXTURE_MAX_LEVEL now
        # clamped to the real level count instead of GL's own default of 1000).
        cna_easygl_test(cna_test_easygl_texture2d_anisotropic_singlelevel
                        easygl_texture2d_anisotropic_singlelevel_test.cpp)
        cna_register_renderer_test(NAME EasyGL_Texture2D_AnisotropicSingleLevel COMMAND cna_test_easygl_texture2d_anisotropic_singlelevel
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 918: real GL_EXT_texture_filter_anisotropic wiring, verified directly against
        # easy-gl's own Sampler API rather than trusting the capability-dump log alone.
        cna_easygl_test(cna_test_easygl_anisotropic_gl_state
                        easygl_anisotropic_gl_state_test.cpp)
        target_link_libraries(cna_test_easygl_anisotropic_gl_state PRIVATE easy-gl)
        cna_register_renderer_test(NAME EasyGL_Anisotropic_GlState COMMAND cna_test_easygl_anisotropic_gl_state
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 303: BlendState::Opaque discards destination + source alpha entirely
        cna_easygl_test(cna_test_easygl_blendstate_opaque
                        easygl_blendstate_opaque_test.cpp)
        cna_register_renderer_test(NAME EasyGL_BlendState_Opaque COMMAND cna_test_easygl_blendstate_opaque
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-077: BlendState.ColorWriteChannels via glColorMask (RT0).
        cna_easygl_test(cna_test_easygl_colorwritechannels
                        easygl_colorwritechannels_test.cpp)
        cna_register_renderer_test(NAME EasyGL_ColorWriteChannels COMMAND cna_test_easygl_colorwritechannels
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 304: BlendState::AlphaBlend expects premultiplied source colour
        cna_easygl_test(cna_test_easygl_blendstate_alphablend
                        easygl_blendstate_alphablend_test.cpp)
        cna_register_renderer_test(NAME EasyGL_BlendState_AlphaBlend COMMAND cna_test_easygl_blendstate_alphablend
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 305: BlendState::NonPremultiplied expects a raw (non-premultiplied) source colour
        cna_easygl_test(cna_test_easygl_blendstate_nonpremultiplied
                        easygl_blendstate_nonpremultiplied_test.cpp)
        cna_register_renderer_test(NAME EasyGL_BlendState_NonPremultiplied COMMAND cna_test_easygl_blendstate_nonpremultiplied
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 306: BlendState::Additive saturation + full-destination-add behaviour
        cna_easygl_test(cna_test_easygl_blendstate_additive
                        easygl_blendstate_additive_test.cpp)
        cna_register_renderer_test(NAME EasyGL_BlendState_Additive COMMAND cna_test_easygl_blendstate_additive
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 307: ColorBlendFunction/AlphaBlendFunction operate independently
        cna_easygl_test(cna_test_easygl_blendstate_separate_functions
                        easygl_blendstate_separate_functions_test.cpp)
        cna_register_renderer_test(NAME EasyGL_BlendState_SeparateFunctions COMMAND cna_test_easygl_blendstate_separate_functions
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 308: ColorSourceBlend/ColorDestinationBlend operate independently from Alpha*Blend
        cna_easygl_test(cna_test_easygl_blendstate_separate_factors
                        easygl_blendstate_separate_factors_test.cpp)
        cna_register_renderer_test(NAME EasyGL_BlendState_SeparateFactors COMMAND cna_test_easygl_blendstate_separate_factors
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 309: BlendState.BlendFactor propagates via GraphicsDevice.setBlendStateProperty alone
        cna_easygl_test(cna_test_easygl_blendstate_blendfactor
                        easygl_blendstate_blendfactor_test.cpp)
        cna_register_renderer_test(NAME EasyGL_BlendState_BlendFactor COMMAND cna_test_easygl_blendstate_blendfactor
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 313: DepthStencilState.DepthBufferWriteEnable actually gates depth-buffer writes
        cna_easygl_test(cna_test_easygl_depthstencilstate_write_enable
                        easygl_depthstencilstate_write_enable_test.cpp)
        cna_register_renderer_test(NAME EasyGL_DepthStencilState_WriteEnable COMMAND cna_test_easygl_depthstencilstate_write_enable
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 314: DepthStencilState.DepthBufferFunction (Always/Never/Less/LessEqual/Greater)
        cna_easygl_test(cna_test_easygl_depthstencilstate_compare_function
                        easygl_depthstencilstate_compare_function_test.cpp)
        cna_register_renderer_test(NAME EasyGL_DepthStencilState_CompareFunction COMMAND cna_test_easygl_depthstencilstate_compare_function
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 315: DepthStencilState.StencilEnable actually gates the stencil test
        cna_easygl_test(cna_test_easygl_depthstencilstate_stencil_enable
                        easygl_depthstencilstate_stencil_enable_test.cpp)
        cna_register_renderer_test(NAME EasyGL_DepthStencilState_StencilEnable COMMAND cna_test_easygl_depthstencilstate_stencil_enable
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 316: DepthStencilState.StencilMask / StencilWriteMask
        cna_easygl_test(cna_test_easygl_depthstencilstate_stencil_mask
                        easygl_depthstencilstate_stencil_mask_test.cpp)
        cna_register_renderer_test(NAME EasyGL_DepthStencilState_StencilMask COMMAND cna_test_easygl_depthstencilstate_stencil_mask
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 317: DepthStencilState front-face stencil operations (StencilFail/
        # StencilDepthBufferFail/StencilPass)
        cna_easygl_test(cna_test_easygl_depthstencilstate_stencil_ops
                        easygl_depthstencilstate_stencil_ops_test.cpp)
        cna_register_renderer_test(NAME EasyGL_DepthStencilState_StencilOps COMMAND cna_test_easygl_depthstencilstate_stencil_ops
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 318: DepthStencilState.TwoSidedStencilMode / back-face (CCW) stencil ops
        cna_easygl_test(cna_test_easygl_depthstencilstate_stencil_twosided
                        easygl_depthstencilstate_stencil_twosided_test.cpp)
        cna_register_renderer_test(NAME EasyGL_DepthStencilState_StencilTwoSided COMMAND cna_test_easygl_depthstencilstate_stencil_twosided
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 319: GraphicsDevice.ReferenceStencil independent-override behavior (Task 872 -
        # confirmed a universal, not-Vulkan-specific gap; registered as a documented known failure)
        cna_easygl_test(cna_test_easygl_graphicsdevice_reference_stencil
                        easygl_graphicsdevice_reference_stencil_test.cpp)
        cna_register_renderer_test(NAME EasyGL_GraphicsDevice_ReferenceStencil COMMAND cna_test_easygl_graphicsdevice_reference_stencil
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 871: GraphicsDevice::Clear() now actually clears the stencil buffer to the
        # requested value on ClearOptions combinations that include ClearOptions::Stencil.
        cna_easygl_test(cna_test_easygl_graphicsdevice_clear_stencil
                        easygl_graphicsdevice_clear_stencil_test.cpp)
        cna_register_renderer_test(NAME EasyGL_GraphicsDevice_ClearStencil COMMAND cna_test_easygl_graphicsdevice_clear_stencil
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 950: GraphicsDevice::Clear()'s depth parameter -- EasyGL already forwarded it
        # correctly (unlike Vulkan/Bgfx); registered here too as a regression guard.
        cna_easygl_test(cna_test_easygl_graphicsdevice_clear_depth
                        ${CNA_GRAPHICS_EXAMPLES_DIR}/graphicsdevice_clear_depth_test.cpp)
        cna_register_renderer_test(NAME EasyGL_GraphicsDevice_ClearDepth COMMAND cna_test_easygl_graphicsdevice_clear_depth
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 955: a freshly-constructed GraphicsDevice's own default state (no game code ever
        # explicitly setting BlendState/DepthStencilState/RasterizerState, exactly like
        # ../cna-samples SimpleAnimation's Tank.hpp) must produce correct opaque depth occlusion.
        cna_easygl_test(cna_test_easygl_graphicsdevice_default_state_occlusion
                        ${CNA_GRAPHICS_EXAMPLES_DIR}/graphicsdevice_default_state_occlusion_test.cpp)
        cna_register_renderer_test(NAME EasyGL_GraphicsDevice_DefaultStateOcclusion COMMAND cna_test_easygl_graphicsdevice_default_state_occlusion
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 889: DualTextureEffect.VertexColorEnabled -- verbatim reuse of the shared
        # renderer-agnostic source, registered on all 3 renderers.
        cna_easygl_test(cna_test_easygl_dualtextureeffect_vertexcolor
                        ${CNA_GRAPHICS_EXAMPLES_DIR}/dualtextureeffect_vertexcolor_test.cpp)
        cna_register_renderer_test(NAME EasyGL_DualTextureEffect_VertexColor COMMAND cna_test_easygl_dualtextureeffect_vertexcolor
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 323 (+ 324/325 coverage): RasterizerState.CullMode — None/CullClockwiseFace/
        # CullCounterClockwiseFace, contrast-checked with front- and back-winding quads
        cna_easygl_test(cna_test_easygl_rasterizerstate_cullmode
                        easygl_rasterizerstate_cullmode_test.cpp)
        cna_register_renderer_test(NAME EasyGL_RasterizerState_CullMode COMMAND cna_test_easygl_rasterizerstate_cullmode
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-244: the three block-compressed formats GraphicsProfile.Reach permits, uploaded
        # as raw 4x4 blocks and sampled. Two blocks side by side, so a mis-sized block or a stream
        # walked in the wrong order shows as a wrong half rather than passing.
        cna_easygl_test(cna_test_easygl_dxt_format
                        easygl_dxt_format_test.cpp)
        cna_register_renderer_test(NAME EasyGL_DxtFormat COMMAND cna_test_easygl_dxt_format
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-244: the three packed 16-bit formats GraphicsProfile.Reach permits, uploaded
        # through their typed SetData overloads and sampled in a real draw -- the channel order
        # differs between XNA and GL for the two with alpha, so this is the case that proves the
        # rotation goes the right way rather than merely that the upload was accepted.
        cna_easygl_test(cna_test_easygl_packed16_format
                        easygl_packed16_format_test.cpp)
        cna_register_renderer_test(NAME EasyGL_Packed16Format COMMAND cna_test_easygl_packed16_format
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # SAMPLE-001: XNA/D3D9 considers integer screen coordinates pixel centers. The original
        # Primitives sample relies on that convention for every one-pixel star. REMED-GFX-239 moved
        # the fixture to the shared contract directory: the convention is XNA's, not EasyGL's, so
        # every renderer owes it and a renderer that does not implement it must say so by failing
        # rather than by never being asked.
        cna_easygl_test(cna_test_easygl_xna_pixel_center
                        ${CNA_GRAPHICS_EXAMPLES_DIR}/xna_pixel_center_contract_test.cpp)
        cna_register_renderer_test(NAME EasyGL_XnaPixelCenter COMMAND cna_test_easygl_xna_pixel_center
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # XNA culling-compatibility audit (docs/xna_culling_compatibility_audit.md): extends the
        # identity-only CullMode test above with a real Matrix::CreateLookAt camera and
        # perspective/orthographic projection -- verbatim reuse of the shared renderer-agnostic
        # source, registered on all 3 renderers.
        cna_easygl_test(cna_test_easygl_rasterizerstate_cullmode_camera
                        ${CNA_GRAPHICS_EXAMPLES_DIR}/rasterizerstate_cullmode_camera_test.cpp)
        cna_register_renderer_test(NAME EasyGL_RasterizerState_CullMode_Camera COMMAND cna_test_easygl_rasterizerstate_cullmode_camera
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # XNA culling-compatibility audit: same real-camera reproducer, but through
        # GraphicsDevice::DrawIndexedPrimitives with a real VertexBuffer/IndexBuffer/BasicEffect
        # -- the exact code path Model/ModelMesh::Draw() uses -- instead of DrawUserPrimitives'
        # separate DrawColoredPrimitives dispatch. Verbatim reuse of the shared source.
        cna_easygl_test(cna_test_easygl_rasterizerstate_cullmode_indexed_basiceffect
                        ${CNA_GRAPHICS_EXAMPLES_DIR}/rasterizerstate_cullmode_indexed_basiceffect_test.cpp)
        cna_register_renderer_test(NAME EasyGL_RasterizerState_CullMode_IndexedBasicEffect COMMAND cna_test_easygl_rasterizerstate_cullmode_indexed_basiceffect
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 326: FillMode::Solid baseline on EasyGL (reuses Task 327's renderer-agnostic Vulkan
        # source — its Solid sub-tests 1/3 previously only ran on Vulkan).
        cna_easygl_test(cna_test_easygl_fill_mode
                        ${CMAKE_SOURCE_DIR}/modules/renderers/vulkan/examples/vulkan_fill_mode_test.cpp)
        cna_register_renderer_test(NAME EasyGL_FillMode_Solid COMMAND cna_test_easygl_fill_mode
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 767: depth bias / slope-scale depth bias via real glPolygonOffset.
        cna_easygl_test(cna_test_easygl_depth_bias
                        easygl_depth_bias_test.cpp)
        cna_register_renderer_test(NAME EasyGL_DepthBias COMMAND cna_test_easygl_depth_bias
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 331: RenderTarget2D constructor/property audit against FNA.
        cna_easygl_test(cna_test_easygl_rendertarget2d_properties
                        easygl_rendertarget2d_properties_test.cpp)
        cna_register_renderer_test(NAME EasyGL_RenderTarget2D_Properties COMMAND cna_test_easygl_rendertarget2d_properties
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 332: RenderTargetCube constructor/property audit against FNA.
        cna_easygl_test(cna_test_easygl_rendertargetcube_properties
                        easygl_rendertargetcube_properties_test.cpp)
        cna_register_renderer_test(NAME EasyGL_RenderTargetCube_Properties COMMAND cna_test_easygl_rendertargetcube_properties
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 498: build and run at least 5 small FNA/XNA sample ports on CNA as real
        # compatibility proof -- EasyGL is the primary/most-mature renderer, so most of this
        # task's own new samples target it, with 3 genuinely 3D (Task 730 already proved 2D
        # compatibility via SDL_Renderer's own 5 samples).
        # Sample 1/5: reuse cna_demo_2d, closing the exact same kind of gap Task 730 closed for
        # SDL_Renderer (already smoke-tested on Vulkan/Bgfx via Tasks 88/89, never on EasyGL).
        cna_register_renderer_test(NAME EasyGL_Demo2D_SmokeTest COMMAND cna_demo_2d --smoke 3
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}" LABELS "GraphicsSmoke")

        # Sample 2/5: moving 3D object -- Update()-driven World-space translation, BasicEffect.
        cna_easygl_test(cna_test_easygl_sample_moving_quad3d
                        easygl_sample_moving_quad3d_test.cpp)
        cna_register_renderer_test(NAME EasyGL_Sample_MovingQuad3D COMMAND cna_test_easygl_sample_moving_quad3d
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Sample 3/5: reactive material swap -- Update()-driven DualTextureEffect.Texture2 swap.
        cna_easygl_test(cna_test_easygl_sample_dualtexture_swap
                        easygl_sample_dualtexture_swap_test.cpp)
        cna_register_renderer_test(NAME EasyGL_Sample_DualTextureSwap COMMAND cna_test_easygl_sample_dualtexture_swap
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Sample 4/5: keyboard-driven 3D object -- the 3D counterpart to Task 730's keyboard
        # sprite sample, using the platform module's shared canned-keyboard test decorator.
        cna_easygl_test(cna_test_easygl_sample_keyboard_cube3d
                        easygl_sample_keyboard_cube3d_test.cpp)
        target_include_directories(cna_test_easygl_sample_keyboard_cube3d PRIVATE
                                   ${CMAKE_SOURCE_DIR}/modules/platform/tests)
        cna_register_renderer_test(NAME EasyGL_Sample_KeyboardCube3D COMMAND cna_test_easygl_sample_keyboard_cube3d
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Sample 5/5: layered UI fade -- Update()-driven SpriteBatch alpha-blend compositing.
        cna_easygl_test(cna_test_easygl_sample_layered_blend
                        easygl_sample_layered_blend_test.cpp)
        cna_register_renderer_test(NAME EasyGL_Sample_LayeredBlend COMMAND cna_test_easygl_sample_layered_blend
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 956: SpriteBatch's blend state must not leak a hardcoded value into a following 3D draw.
        cna_easygl_test(cna_test_easygl_spritebatch_blendstate_leak
                        easygl_spritebatch_blendstate_leak_test.cpp)
        cna_register_renderer_test(NAME EasyGL_SpriteBatch_BlendStateLeak COMMAND cna_test_easygl_spritebatch_blendstate_leak
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # plan_vulkan.md VULKAN-400: the control run. The finding this checks is documented as
        # EasyGL's (migration-guide known issues, Task 933), so a Vulkan-only registration could
        # not tell "absent on Vulkan" from "the test cannot detect it anywhere".
        cna_easygl_test(cna_test_easygl_fullscreen_sprite_then_3d
            ${CNA_GRAPHICS_EXAMPLES_DIR}/fullscreen_spritebatch_then_3d_test.cpp)
        cna_register_renderer_test(NAME EasyGL_FullscreenSpriteThen3D COMMAND cna_test_easygl_fullscreen_sprite_then_3d
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # GraphicsDeviceManager.SynchronizeWithVerticalRetrace/ApplyChanges() must actually reach
        # IGraphicsRenderer::SetSwapInterval() -- renderer-independent fix in GraphicsDevice::Reset().
        cna_easygl_test(cna_test_easygl_graphicsdevicemanager_vsync
                        easygl_graphicsdevicemanager_vsync_test.cpp)
        cna_register_renderer_test(NAME EasyGL_GraphicsDeviceManager_Vsync COMMAND cna_test_easygl_graphicsdevicemanager_vsync
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-006: SkinnedEffect world-space normal transform under non-identity World
        # (rotation, non-uniform scale, and both) for BOTH skinned programs (per-pixel-lit
        # EnsureSkinnedProgram and per-vertex-lit EnsureSkinnedVertexLitProgram) -- the cases every
        # existing skinned test, all of which use World = Identity, structurally cannot detect.
        cna_easygl_test(cna_test_easygl_skinnedeffect_world_normal
                        easygl_skinnedeffect_world_normal_test.cpp)
        cna_register_renderer_test(NAME EasyGL_SkinnedEffect_WorldNormal COMMAND cna_test_easygl_skinnedeffect_world_normal
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-072: SpriteBatch clip space must be built from the active GraphicsDevice.Viewport
        # (Viewport.Width/Height), not the full target. EasyGL's FlushBatch built the ortho from the
        # RT/logical dims AND reset the GL viewport to the full target, so a custom sub-Viewport was
        # ignored for sprites; it must instead project by Viewport.W/H and raster into Viewport.X/Y/W/H.
        cna_easygl_test(cna_test_easygl_spritebatch_custom_viewport
                        ${CNA_GRAPHICS_EXAMPLES_DIR}/spritebatch_custom_viewport_test.cpp)
        cna_register_renderer_test(NAME EasyGL_SpriteBatch_CustomViewport COMMAND cna_test_easygl_spritebatch_custom_viewport
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-072 (Phase 9): two SpriteBatch batches with DIFFERENT viewports in one frame --
        # EasyGL flushes per End() (immediate), so each batch gets its own glViewport + ortho.
        cna_easygl_test(cna_test_easygl_spritebatch_viewport_switch
                        ${CNA_GRAPHICS_EXAMPLES_DIR}/spritebatch_viewport_switch_test.cpp)
        cna_register_renderer_test(NAME EasyGL_SpriteBatch_ViewportSwitch COMMAND cna_test_easygl_spritebatch_viewport_switch
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
        cna_easygl_test(cna_test_easygl_colorspace_midtone
            ${CNA_GRAPHICS_EXAMPLES_DIR}/colorspace_midtone_contract_test.cpp)
        cna_register_renderer_test(NAME EasyGL_ColorSpace_MidTone COMMAND cna_test_easygl_colorspace_midtone
            TIMEOUT 120 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_additive_blend_contract
            ${CNA_GRAPHICS_EXAMPLES_DIR}/additive_blend_contract_test.cpp)
        cna_register_renderer_test(NAME EasyGL_AdditiveBlendContract
            COMMAND cna_test_easygl_additive_blend_contract
            TIMEOUT 120 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-147: a RenderTarget2D used as a texture must sample in the same logical
        # orientation as an ordinary Texture2D holding identical bytes. An OpenGL framebuffer's
        # origin is bottom-left, so a target's colour texture stores the image bottom-up; GetData
        # already compensated (which is why public readback was always right) but sampling did not,
        # so a rendered texture arrived upside down while an uploaded one did not. This is the
        # EasyGL counterpart of REMED-GFX-067's bgfx finding.
        cna_easygl_test(cna_test_easygl_rt_sampling_orientation
            ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_sampling_orientation_test.cpp)
        cna_register_renderer_test(NAME EasyGL_RenderTarget_SamplingOrientation
            COMMAND cna_test_easygl_rt_sampling_orientation
            TIMEOUT 120 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-153 cross-renderer source-selection and destination-orientation control.
        cna_easygl_test(cna_test_easygl_gfx153_source_rectangle
            ${CNA_GRAPHICS_EXAMPLES_DIR}/source_rectangle_orientation_test.cpp)
        cna_register_renderer_test(NAME EasyGL_GFX153_SourceRectangleOrientation
            COMMAND cna_test_easygl_gfx153_source_rectangle
            TIMEOUT 180 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")


        # REMED-GFX-169: every stock 3D effect must sample its textures through the public
        # GraphicsDevice.SamplerStates[slot], exactly like SpriteBatch already does. Vulkan's
        # ApplySamplerState is correct and SpriteBatch reads slotSamplers_[0], but six stock 3D
        # descriptor builders take NO sampler parameter at all, so all 15 of their combined-image-
        # sampler bindings are hardcoded to defaultSampler_ (LINEAR + CLAMP_TO_EDGE) and the sampler is
        # absent from their cache keys too. Vulkan is where the defect lives; the other renderers run the
        # same public fixture as controls.
        cna_easygl_test(cna_test_easygl_stock_effect_sampler
            ${CNA_GRAPHICS_EXAMPLES_DIR}/stock_effect_sampler_contract_test.cpp)
        cna_register_renderer_test(NAME EasyGL_StockEffectSamplerContract COMMAND cna_test_easygl_stock_effect_sampler
            TIMEOUT 300 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-150 principal positive control: TextureFilter::Point must select exactly ONE
        # texel and TextureAddressMode must decide which one. The defect was Software-local, and
        # EasyGL is the conforming renderer the finding was measured against (128/128 exact where
        # Software managed 4/128) -- so this run is what establishes that the fixture's expectations
        # are the XNA contract rather than a description of the Software fix.
        # REMED-GFX-170: every public TextureFilter ordinal names a SEPARATE magnification, a separate
        # minification and a separate mipmap filter, so a renderer may not reduce the ordinal to one
        # boolean. WebGPU's SpriteBatch sampler and SDL_GPU's ONE shared sampler helper both resolved
        # `textureFilter == 0 ? LINEAR : NEAREST`, and both keyed their sampler cache on
        # `filter == 0 ? 0 : 1`, so Anisotropic, LinearMipPoint, MinPointMagLinearMipLinear and
        # MinPointMagLinearMipPoint all magnified with POINT. This fixture measures the two DIFFERENT
        # partitions of the nine ordinals that magnification and minification induce, on SpriteBatch and
        # on every textured stock family; the other renderers run it as controls.
        cna_easygl_test(cna_test_easygl_texture_filter_ordinal
            ${CNA_GRAPHICS_EXAMPLES_DIR}/texture_filter_ordinal_contract_test.cpp)
        cna_register_renderer_test(NAME EasyGL_TextureFilterOrdinalContract
            COMMAND cna_test_easygl_texture_filter_ordinal
            TIMEOUT 300 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-173 cross-renderer control: EnvironmentMapEffect samples TWO independent
    # resources -- the ordinary 2D texture through GraphicsDevice.SamplerStates[0] and the
    # reflection cube through SamplerStates[1]. SDL_GPU bound a literal LinearClamp for the cube
    # and captured only slot 0, so slot 1 could not reach replay at all. This fixture makes the
    # two slots independently observable (EnvironmentMapAmount 1 leaves the cube the only
    # contributor, 0 leaves the 2D texture the only one) and measures what every other renderer
    # does with the same public state.
        cna_easygl_test(cna_test_easygl_envmap_cube_sampler
            ${CNA_GRAPHICS_EXAMPLES_DIR}/envmap_cube_sampler_contract_test.cpp)
        cna_register_renderer_test(NAME EasyGL_EnvMapCubeSamplerContract
            COMMAND cna_test_easygl_envmap_cube_sampler
            TIMEOUT 600 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-172 cross-renderer control: DualTextureEffect's two layers have INDEPENDENT public
    # sampler slots -- FNA's DualTextureEffect.fx declares DECLARE_TEXTURE(Texture, 0) and
    # DECLARE_TEXTURE(Texture2, 1). WebGPU declared ONE WGSL sampler for both texture views, so
    # SamplerStates[1] was inexpressible and slot 1 inherited slot 0's. This fixture observes both
    # layers in the SAME image -- slot 0's texture varies along X only, slot 1's along Y only, and
    # the shader's product is separable -- so independence is measured, not inferred.
        cna_easygl_test(cna_test_easygl_dualtexture_slot_sampler
            ${CNA_GRAPHICS_EXAMPLES_DIR}/dualtexture_slot_sampler_contract_test.cpp)
        cna_register_renderer_test(NAME EasyGL_DualTextureSlotSamplerContract
            COMMAND cna_test_easygl_dualtexture_slot_sampler
            TIMEOUT 600 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-174: a TextureFilter ordinal names four INDEPENDENT components, and EasyGL wrote
        # GL_TEXTURE_MAX_ANISOTROPY only when the ordinal was Anisotropic. Its per-slot GL sampler
        # object is mutated in place and reused, so that write could only ever raise the value and it
        # then stayed raised: every later Point or Linear draw on that slot kept sampling
        # anisotropically. This fixture is ORDER-SENSITIVE by construction -- it draws Anisotropic
        # first and then asserts the next ordinal -- which is exactly what the existing per-ordinal
        # fixtures could not see. It also asserts single-level completeness for both sampleable
        # texture kinds and mip-level selection where the renderer really allocates a chain.
        cna_easygl_test(cna_test_easygl_sampler_component_isolation
            ${CNA_GRAPHICS_EXAMPLES_DIR}/sampler_component_isolation_contract_test.cpp)
        cna_register_renderer_test(NAME EasyGL_SamplerComponentIsolation
            COMMAND cna_test_easygl_sampler_component_isolation
            TIMEOUT 300 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-175: a TextureFilter ordinal's MIPMAP component is a third, independent decision,
        # and ordinals 0 (Linear) and 1 (Point) were losing it -- both mapped onto a GL filter with no
        # mipmap term, so a texture that really owns a chain never mip-filtered under either, the
        # DEFAULT filter included. This fixture supplies the dimension every existing mip test lacked:
        # a chain whose levels name themselves, drawn at destination sizes that put the LOD on an
        # exact integer, so the selected level is read straight out of the framebuffer.
        cna_easygl_test(cna_test_easygl_texture_filter_mip_contract
            ${CNA_GRAPHICS_EXAMPLES_DIR}/texture_filter_mip_contract_test.cpp)
        cna_register_renderer_test(NAME EasyGL_TextureFilterMipContract
            COMMAND cna_test_easygl_texture_filter_mip_contract
            TIMEOUT 300 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-177 cross-renderer control: descriptor/binding bookkeeping must be a function of what
        # is LIVE, never of what has ever existed. D3D12 owned four fixed-capacity heaps with a monotonic
        # bump cursor and no free list, so the 65th sampleable resource EVER CREATED threw even when six
        # were alive. Every draw here is checked against a self-identifying oracle that decodes back to an
        # integer, so a recycled or grown pool that aliases one resource onto another's binding names the
        # wrong resource rather than merely producing an odd colour.
        cna_easygl_test(cna_test_easygl_descriptor_capacity
            ${CNA_GRAPHICS_EXAMPLES_DIR}/descriptor_capacity_contract_test.cpp)
        cna_register_renderer_test(NAME EasyGL_DescriptorCapacityContract
            COMMAND cna_test_easygl_descriptor_capacity
            TIMEOUT 900 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_point_sampling
            ${CNA_GRAPHICS_EXAMPLES_DIR}/point_sampling_contract_test.cpp)
        cna_register_renderer_test(NAME EasyGL_PointSamplingContract
            COMMAND cna_test_easygl_point_sampling
            TIMEOUT 300 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-151 cross-renderer control: the canonical XNA render-to-texture sequence -- render
        # into a target, unbind it, sample it -- must complete in ONE public frame with no intervening
        # GetData, Present, extra frame, manual flush or wait. The defect was Vulkan-local (its deferred
        # recorder's readback flush filtered the frame's segment list down to the target being READ, so a
        # producer's pass was never recorded before the consumer that sampled it); these runs are what
        # establish that every other renderer already honoured the contract rather than being made to.
        cna_easygl_test(cna_test_easygl_rt_producer_consumer
            ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_producer_consumer_test.cpp)
        cna_register_renderer_test(NAME EasyGL_RenderTarget_ProducerConsumer
            COMMAND cna_test_easygl_rt_producer_consumer
            TIMEOUT 120 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-152 cross-renderer control: a RenderTarget2D handed to a stock or custom 3D effect as
    # its texture must be sampled, not reinterpreted. The defect was SDL_GPU-local and fatal (its
    # stock-effect paths static_cast an ITextureRenderer* to the unrelated sibling
    # SdlGpuTextureRenderer, fabricating an SDL_GPUTexture* out of a render target's own fields); this
    # run is what establishes that EASYGL already honoured the contract rather than being made to.
        cna_easygl_test(cna_test_easygl_rt_effect_source
            ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_effect_source_test.cpp)
        cna_register_renderer_test(NAME EasyGL_RenderTarget_EffectSource
            COMMAND cna_test_easygl_rt_effect_source
            TIMEOUT 300 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-167 deferred-source lifetime: a resource sampled by a draw that has only been
        # QUEUED must stay bindable until that draw actually renders, and destroying the public wrapper
        # first must never terminate the process. The defect was WebGPU-local -- every deferred command
        # stored a raw pointer to the resource's RENDERER OBJECT and called a VIRTUAL method on it at
        # replay, so a RenderTarget2D produced, sampled onto the BACKBUFFER and dropped inside one
        # Draw() was a heap-use-after-free at Present(). These runs establish which renderers already
        # honoured the contract rather than being made to; each leg runs in its own process so a
        # SIGSEGV is an attributable result instead of a lost shard.
        cna_easygl_test(cna_test_easygl_deferred_source_lifetime
            ${CNA_GRAPHICS_EXAMPLES_DIR}/deferred_source_lifetime_test.cpp)
        cna_register_renderer_test(NAME EasyGL_DeferredSourceLifetime COMMAND cna_test_easygl_deferred_source_lifetime
            TIMEOUT 300 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-168 bound-target lifetime: destroying a RenderTarget2D that is STILL the bound
        # render target must never make the next SetRenderTarget transition unsafe, and must leave the
        # next target and the backbuffer exactly correct. The defect was EasyGL-local -- the renderer
        # remembered the bound destination as a raw IRenderTargetRenderer* and the next transition
        # called UnbindAsRenderTarget() on it to run that target's pending MSAA resolve and mip
        # regeneration, so a scoped target that went out of scope while bound made the next
        # SetRenderTarget() a virtual call through freed storage. Each leg runs in its own process so a
        # SIGSEGV is an attributable result instead of a lost shard.
        cna_easygl_test(cna_test_easygl_bound_target_lifetime
            ${CNA_GRAPHICS_EXAMPLES_DIR}/bound_target_lifetime_test.cpp)
        cna_register_renderer_test(NAME EasyGL_BoundTargetLifetime COMMAND cna_test_easygl_bound_target_lifetime
            TIMEOUT 600 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-180: the public render-target -> Present lifecycle contract. `SdlGpu_RenderState`
        # aborted with `Cannot present while render targets are bound`, which is the SECOND event in a
        # two-stage chain: an over-range DrawPrimitives (two triangles from a three-vertex buffer) was
        # correctly rejected by REMED-GFX-113's range guard, that rejection unwound past the fixture's own
        # SetRenderTarget(nullptr), and EndDraw then met a still-bound target. FNA's contract is REJECT,
        # never auto-unbind, so both guards are right and the fixture was wrong. Every leg runs in its own
        # process; leg C2 reproduces the abort end to end and its correct outcome is SIGABRT.
        cna_easygl_test(cna_test_easygl_present_lifecycle
            ${CNA_GRAPHICS_EXAMPLES_DIR}/present_lifecycle_contract_test.cpp)
        cna_register_renderer_test(NAME EasyGL_PresentLifecycle COMMAND cna_test_easygl_present_lifecycle
            TIMEOUT 900 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-163 cross-renderer control: the same public depth-backed MULTISAMPLED
        # RenderTarget2D construction and lifecycle that aborted the process on Bgfx. Only Bgfx
        # production changed; this renderer is registered to show the construction was always legal
        # and to keep it that way.
        cna_easygl_test(cna_test_easygl_msaa_depth_contract
            ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_msaa_depth_contract_test.cpp)
        cna_register_renderer_test(NAME EasyGL_MsaaDepthContract COMMAND cna_test_easygl_msaa_depth_contract
            TIMEOUT 900 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-154 cross-renderer control. The Bgfx half of the original REMED-GFX-164 report
        # was reconciled into GFX-154 and fixed there. This fixture's unbound sequence was already
        # byte-exact on EasyGL and remains fully asserted; REMED-GFX-164's distinct active-target
        # sequence is covered immediately below.
        cna_easygl_test(cna_test_easygl_msaa_first_readback
            ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_msaa_first_readback_test.cpp)
        cna_register_renderer_test(NAME EasyGL_MsaaFirstReadback COMMAND cna_test_easygl_msaa_first_readback
            TIMEOUT 900 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-164: active-target MSAA resolve and exact public alpha delivery.  This is the
        # ticket's dedicated fixture; it keeps the bound-vs-unbound stage discriminating and also
        # covers raw RGBA transfer, target sampling, guarded rectangles, repetition and teardown.
        # OPENGLES2 (docs/opengles2-renderer.md): this test exists to exercise an ES 3.0-level
        # feature the profile truthfully refuses (SupportsCapability answers false), so it is not
        # registered there -- the refusal contract itself is covered by the shared capability and
        # lifecycle tests.
        if(NOT CNA_GRAPHICS_RENDERER STREQUAL "OPENGLES2")
            cna_easygl_test(cna_test_easygl_gfx164_bound_msaa_alpha
                easygl_gfx164_bound_msaa_alpha_test.cpp)
            cna_register_renderer_test(NAME EasyGL_GFX164_BoundMsaaAlpha
                COMMAND cna_test_easygl_gfx164_bound_msaa_alpha
                TIMEOUT 900 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
        endif()

        # REMED-GFX-186 cross-renderer control: the same PUBLIC generated-mip readback contract on this
        # renderer. The SIGSEGV was SDL_GPU-only and only SDL_GPU production changed; running the
        # identical fixture here is what makes that claim falsifiable rather than asserted.
        # OPENGLES2 (docs/opengles2-renderer.md): this test exists to exercise an ES 3.0-level
        # feature the profile truthfully refuses (SupportsCapability answers false), so it is not
        # registered there -- the refusal contract itself is covered by the shared capability and
        # lifecycle tests.
        if(NOT CNA_GRAPHICS_RENDERER STREQUAL "OPENGLES2")
            cna_easygl_test(cna_test_easygl_msaa_mip_readback
                ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_msaa_mip_readback_test.cpp)
            cna_register_renderer_test(NAME EasyGL_MsaaMipReadback COMMAND cna_test_easygl_msaa_mip_readback
                TIMEOUT 1200 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
        endif()

        # REMED-GFX-189 cross-renderer control: the same PUBLIC invalid-mip-level contract on this
        # renderer. The fabrication was Vulkan-only and only Vulkan production changed; running the
        # identical fixture here is what makes that claim falsifiable rather than asserted.
        cna_easygl_test(cna_test_easygl_invalid_mip_level
            ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_invalid_mip_level_test.cpp)
        cna_register_renderer_test(NAME EasyGL_InvalidMipLevel COMMAND cna_test_easygl_invalid_mip_level
            TIMEOUT 1200 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-165 cross-renderer control: EasyGL already honoured the authoritative-dimension
        # GetBackBufferData contract; this run establishes that the shared fix left it byte-unchanged.
        cna_easygl_test(cna_test_easygl_backbuffer_readback_dimension
            ${CNA_GRAPHICS_EXAMPLES_DIR}/backbuffer_readback_dimension_test.cpp)
        cna_register_renderer_test(NAME EasyGL_BackbufferReadbackDimension COMMAND cna_test_easygl_backbuffer_readback_dimension
            TIMEOUT 300 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-161: cross-renderer control for the first-read completion contract.
        cna_easygl_test(cna_test_easygl_backbuffer_first_read
            ${CNA_GRAPHICS_EXAMPLES_DIR}/backbuffer_first_read_test.cpp)
        cna_register_renderer_test(NAME EasyGL_BackbufferFirstRead COMMAND cna_test_easygl_backbuffer_first_read
            TIMEOUT 300 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-162 cross-renderer control: a rasterizing renderer's GetBackBufferData SUCCEEDS and
        # fully writes the requested range, so the Headless rejection is specific, not universal.
        cna_easygl_test(cna_test_easygl_backbuffer_reject
            ${CNA_GRAPHICS_EXAMPLES_DIR}/backbuffer_headless_reject_test.cpp)
        cna_register_renderer_test(NAME EasyGL_BackbufferReject COMMAND cna_test_easygl_backbuffer_reject
            TIMEOUT 300 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-155 cross-renderer control: a render target produced and unbound earlier in a public
        # frame must be visible to a consumer that draws on the BACKBUFFER later in that same frame. The
        # defect was bgfx-local (it radix-sorts a frame's draws by their view's sort position, which
        # defaults to the numeric view id, and its backbuffer owns the lowest id -- so a backbuffer
        # consumer executed before its own producer); these runs are what establish that every other
        # renderer already honoured the contract rather than being made to.
        cna_easygl_test(cna_test_easygl_rt_backbuffer_consumer
            ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_backbuffer_consumer_test.cpp)
        cna_register_renderer_test(NAME EasyGL_RenderTarget_BackbufferConsumer
            COMMAND cna_test_easygl_rt_backbuffer_consumer
            TIMEOUT 180 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-158's control: a RenderTarget2D constructed during a public frame must be usable in
        # that same frame -- bound, cleared and/or drawn into, unbound and observed -- with no warm-up
        # frame, Present, GetData-before-first-render, dummy draw, empty bind cycle, manual frame advance
        # or wait. The defect was bgfx-local (`bgfx::reset()` discards every view's framebuffer binding,
        # including the bound target's), so this run is what establishes that EASYGL already honoured the
        # contract rather than being made to.
        cna_easygl_test(cna_test_easygl_rt_first_use
            ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_first_use_test.cpp)
        cna_register_renderer_test(NAME EasyGL_RenderTarget_FirstUse
            COMMAND cna_test_easygl_rt_first_use
            TIMEOUT 180 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-157: a stock 3D draw issued AFTER a SpriteBatch inside ONE render-target bind cycle
        # must execute after it rather than be dropped. REMED-GFX-155's leg I0 measured the region it
        # drew into still holding the cycle's clear colour on BGFX, VULKAN, SOFTWARE and EASYGL, with
        # WEBGPU clean, using ordinary Texture2D sources on both destinations. The oracle is a staircase
        # -- step i covers stripes [0, N-i), so one readback proves every draw happened AND that they
        # happened in the issued order.
        cna_easygl_test(cna_test_easygl_spritebatch_3d_order
            ${CNA_GRAPHICS_EXAMPLES_DIR}/spritebatch_3d_order_test.cpp)
        cna_register_renderer_test(NAME EasyGL_SpriteBatch3DOrder
            COMMAND cna_test_easygl_spritebatch_3d_order
            TIMEOUT 300 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-160: the XNA/FNA front-face winding contract. FNA's SpriteBatch emits
        # CLOCKWISE-as-displayed triangles and they must survive RasterizerState.CullCounterClockwise
        # -- the state SpriteBatch.Begin itself defaults to -- so clockwise-as-displayed is XNA's
        # FRONT face and each enum names the face it CULLS. Two triangles in disjoint quadrants, one
        # wound each way, make every cull mode's expected answer complementary in one readback.
        cna_easygl_test(cna_test_easygl_frontface_winding
            ${CNA_GRAPHICS_EXAMPLES_DIR}/frontface_winding_test.cpp)
        cna_register_renderer_test(NAME EasyGL_FrontFaceWinding
            COMMAND cna_test_easygl_frontface_winding
            TIMEOUT 300 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-183 cross-renderer control: the complete TriangleStrip parity/culling matrix.
        cna_easygl_test(cna_test_easygl_triangle_strip_winding
            ${CNA_GRAPHICS_EXAMPLES_DIR}/triangle_strip_winding_test.cpp)
        cna_register_renderer_test(NAME EasyGL_TriangleStripWinding
            COMMAND cna_test_easygl_triangle_strip_winding
            TIMEOUT 300 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_texture2d_getdata_contract
                        ${CNA_GRAPHICS_EXAMPLES_DIR}/texture2d_getdata_contract_test.cpp)
        cna_register_renderer_test(NAME EasyGL_Texture2D_GetDataContract COMMAND cna_test_easygl_texture2d_getdata_contract
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-149 (and REMED-GFX-128, the same expression): the exact XNA-compatible
    # startIndex/elementCount contract of every public Texture2D::GetData overload. startIndex is a
    # DESTINATION element offset and elementCount is the destination capacity available from it;
    # the whole-level overload applied startIndex to the SOURCE, rejected any non-zero offset on a
    # render target, rejected legal excess capacity, and silently returned a partial frame for an
    # undersized one.
        cna_easygl_test(cna_test_easygl_texture2d_getdata_transfer_range
                        ${CNA_GRAPHICS_EXAMPLES_DIR}/texture2d_getdata_transfer_range_test.cpp)
        cna_register_renderer_test(NAME EasyGL_Texture2D_GetDataTransferRange COMMAND cna_test_easygl_texture2d_getdata_transfer_range
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-130: the TextureCube/Texture3D half of REMED-GFX-127's finding. Both public cube
        # and volume GetData paths converted their own zero-initialized scratch buffer for the caller
        # regardless of whether the renderer read anything back, so an unimplemented readback returned
        # a complete, uniformly transparent-black face/volume instead of rejecting the request.
        # OPENGLES2 (docs/opengles2-renderer.md): this test exists to exercise an ES 3.0-level
        # feature the profile truthfully refuses (SupportsCapability answers false), so it is not
        # registered there -- the refusal contract itself is covered by the shared capability and
        # lifecycle tests.
        if(NOT CNA_GRAPHICS_RENDERER STREQUAL "OPENGLES2")
            cna_easygl_test(cna_test_easygl_cube_volume_getdata_contract
                            ${CNA_GRAPHICS_EXAMPLES_DIR}/texturecube_texture3d_getdata_contract_test.cpp)
            cna_register_renderer_test(NAME EasyGL_CubeVolume_GetDataContract COMMAND cna_test_easygl_cube_volume_getdata_contract
                TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
        endif()

        # REMED-GFX-134: `RenderTargetCube::GetData` must return the face that was really rendered or
        # reject the request deterministically. REMED-GFX-130 made the reporting honest but left the
        # readback itself implemented on only two renderers, so a rendered cube face had no byte-exact
        # public oracle anywhere else. Drawn geometry (never Clear -- see REMED-GFX-129) paints an
        # asymmetric five-region pattern whose colours rotate per face, so a flip, a rotation, a wrong
        # array layer/subresource, a stale face or an unresolved multisample surface all fail.
        cna_easygl_test(cna_test_easygl_rendertargetcube_getdata_contract
                        ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertargetcube_getdata_contract_test.cpp)
        cna_register_renderer_test(NAME EasyGL_RenderTargetCube_GetDataContract COMMAND cna_test_easygl_rendertargetcube_getdata_contract
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-136: IGraphicsRenderer::CreateRenderTargetCube had no `preserveContents` parameter,
        # unlike CreateRenderTarget2D, so a RenderTargetCube's real RenderTargetUsage never reached the
        # renderer and Vulkan/WebGPU discarded a PreserveContents cube face on every bind cycle. Reuses
        # REMED-GFX-134's asymmetric face producer, then rebinds and draws only a small marker: "the
        # marker landed" is what a discarding renderer also achieves, so it can never pass for
        # preservation.
        cna_easygl_test(cna_test_easygl_rendertargetcube_usage
                        ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertargetcube_usage_test.cpp)
        cna_register_renderer_test(NAME EasyGL_RenderTargetCube_Usage COMMAND cna_test_easygl_rendertargetcube_usage
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-141: a MULTISAMPLED RenderTargetCube shared ONE multisample colour attachment
        # across all six faces on EasyGL, Vulkan and SdlGpu, so a PreserveContents face reloaded whichever
        # face was rendered last instead of its own samples. Renders face A, then face B, then rebinds A for
        # a small partial update -- with no readback, Present or flush in between -- and reads the whole of
        # A: the resolved single-sample layer still held the correct A, which is why a full redraw and an
        # immediate read both passed before this.
        # OPENGLES2 (docs/opengles2-renderer.md): this test exists to exercise an ES 3.0-level
        # feature the profile truthfully refuses (SupportsCapability answers false), so it is not
        # registered there -- the refusal contract itself is covered by the shared capability and
        # lifecycle tests.
        if(NOT CNA_GRAPHICS_RENDERER STREQUAL "OPENGLES2")
            cna_easygl_test(cna_test_easygl_rendertargetcube_msaa_face
                            ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertargetcube_msaa_face_test.cpp)
            cna_register_renderer_test(NAME EasyGL_RenderTargetCube_MsaaFace COMMAND cna_test_easygl_rendertargetcube_msaa_face
                TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
        endif()

        # REMED-GFX-142: RenderTargetUsage's DEPTH and STENCIL half. FNA3D's own header documents
        # `preserveTargetContents` as storing the "color/depth/stencil" contents, and FNA's DiscardContents
        # bind clears all three (Target|DepthBuffer|Stencil, MaxDepth, 0) -- so the enum governs depth and
        # stencil, not colour alone. Renders an occluder, unbinds, rebinds WITHOUT clearing and draws behind
        # it: three bands separate preserved depth from cleared depth, lost colour and a second pass that
        # never drew. A parallel stencil stamp/gate sequence does the same for stencil.
        cna_easygl_test(cna_test_easygl_rendertarget_depthstencil_usage
                        ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_depthstencil_usage_test.cpp)
        cna_register_renderer_test(NAME EasyGL_RenderTarget_DepthStencilUsage COMMAND cna_test_easygl_rendertarget_depthstencil_usage
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-140: every public render-target bind/unbind cycle must be its own logical pass.
        # `VulkanRenderer::RecordCommandBuffer` collected ONE render pass per unique render-target
        # source per flush and replayed every queued batch for it inside that pass, so two bind cycles of
        # one target within a single flush window shared one load action. The decisive checks all use
        # DiscardContents -- collapsing is invisible on a preserving target, which is why REMED-GFX-136
        # needed an artificial readback barrier to see it -- and nothing between two cycles ever forces a
        # flush.
        cna_easygl_test(cna_test_easygl_rendertarget_pass_boundary
                        ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_pass_boundary_test.cpp)
        cna_register_renderer_test(NAME EasyGL_RenderTarget_PassBoundary COMMAND cna_test_easygl_rendertarget_pass_boundary
            TIMEOUT 90 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-129: cross-renderer control for Vulkan's ordered-Clear correction. Clear() is an
        # ordered command at its exact public call position, not a hint for the next render-pass
        # load action; every check queues its whole public sequence and reads once at the end.
        cna_easygl_test(cna_test_easygl_ordered_clear
                        ${CNA_GRAPHICS_EXAMPLES_DIR}/graphicsdevice_ordered_clear_test.cpp)
        cna_register_renderer_test(NAME EasyGL_GraphicsDevice_OrderedClear COMMAND cna_test_easygl_ordered_clear
            TIMEOUT 120 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-143: backbuffer work and render-target work must replay in ONE ordered stream.
        # REMED-GFX-140 (Vulkan) and REMED-GFX-145 (SdlGpu) gave every render-target bind cycle its own
        # native pass in public order, but both kept the BACKBUFFER as one trailing pass, so every
        # backbuffer draw of a frame was replayed after every target pass regardless of when it was
        # issued. A frame that samples a target onto the backbuffer and then renders into that target
        # again therefore saw the FINAL content. Every check queues its whole public sequence with no
        # Present, GetData, flush or extra frame between the cycles, and only then reads once.
        cna_easygl_test(cna_test_easygl_backbuffer_pass_order
                        ${CNA_GRAPHICS_EXAMPLES_DIR}/backbuffer_pass_order_test.cpp)
        cna_register_renderer_test(NAME EasyGL_Backbuffer_PassOrder COMMAND cna_test_easygl_backbuffer_pass_order
            TIMEOUT 120 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-116 cross-renderer control: every deferred draw must execute under the
        # GraphicsDevice.Viewport active at its own public call. WebGPU resolved it live when it
        # recorded the pass; this file is the same public fixture, so a renderer that regresses the
        # contract is caught here rather than assumed correct.
        cna_easygl_test(cna_test_easygl_deferred_viewport
                        ${CNA_GRAPHICS_EXAMPLES_DIR}/deferred_viewport_capture_test.cpp)
        cna_register_renderer_test(NAME EasyGL_Deferred_Viewport COMMAND cna_test_easygl_deferred_viewport
            TIMEOUT 120 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-146 cross-renderer control: every deferred draw must execute under the
        # GraphicsDevice.ScissorRectangle and RasterizerState.ScissorTestEnable active at its own
        # public call. WebGPU resolved both live when it recorded the pass; this file is the same
        # public fixture, so a renderer that regresses the contract is caught here rather than
        # assumed correct.
        cna_easygl_test(cna_test_easygl_deferred_scissor
                        ${CNA_GRAPHICS_EXAMPLES_DIR}/deferred_scissor_capture_test.cpp)
        cna_register_renderer_test(NAME EasyGL_Deferred_Scissor COMMAND cna_test_easygl_deferred_scissor
            TIMEOUT 120 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-135: the WRITE half of the same finding. `TextureCube::SetData`/`Texture3D::SetData`
        # kept the pre-REMED-GFX-127 shape -- a `void` renderer method behind `if (renderer_)` -- so an
        # upload that stored nothing, or only part of the requested region, still returned normally.
        # OPENGLES2 (docs/opengles2-renderer.md): this test exists to exercise an ES 3.0-level
        # feature the profile truthfully refuses (SupportsCapability answers false), so it is not
        # registered there -- the refusal contract itself is covered by the shared capability and
        # lifecycle tests.
        if(NOT CNA_GRAPHICS_RENDERER STREQUAL "OPENGLES2")
            cna_easygl_test(cna_test_easygl_cube_volume_setdata_contract
                            ${CNA_GRAPHICS_EXAMPLES_DIR}/texturecube_texture3d_setdata_contract_test.cpp)
            cna_register_renderer_test(NAME EasyGL_CubeVolume_SetDataContract COMMAND cna_test_easygl_cube_volume_setdata_contract
                TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
        endif()

    endif()

# Module-local tests get the same headless-safe skip convention the root one-shot
# gives root-registered tests (see cmake/TestHelpers.cmake).
cna_apply_skip_convention()
