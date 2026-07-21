# Historically nested inside the house3d_demo/net-demos block below (both only build for the
# EASYGL/VULKAN backends), so EasyGL's test suite has always additionally required
# CNA_BUILD_EXAMPLES=ON, unlike every other backend's test block. Preserved verbatim here as its
# own self-contained condition rather than silently dropped during the file split.
if(CNA_BUILD_EXAMPLES AND CNA_BUILD_TESTS AND NOT EMSCRIPTEN AND NOT WIN32
   AND CNA_GRAPHICS_BACKEND STREQUAL "EASYGL")

        # --- helper macro: build a headless EasyGL test exe --------------------
        macro(cna_easygl_test target src)
            add_executable(${target} ${src})
            if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
                target_link_libraries(${target} PRIVATE
                    -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                    SHARP_RUNTIME)
            else()
                target_link_libraries(${target} PRIVATE CNA SHARP_RUNTIME)
            endif()
            if(TARGET SDL3::SDL3main)
                target_link_libraries(${target} PRIVATE SDL3::SDL3main)
            endif()
        endmacro()

        enable_testing()

        # Task 85: 3-frame smoke test
        cna_register_backend_test(NAME EasyGL_House3D_SmokeTest COMMAND cna_house3d_demo --smoke 3
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}" LABELS "GraphicsSmoke")

        # Task 86: textured quad pixel readback
        cna_easygl_test(cna_test_easygl_textured_quad
                        examples/easygl_textured_quad_test.cpp)
        cna_register_backend_test(NAME EasyGL_TexturedQuad_Readback COMMAND cna_test_easygl_textured_quad
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # plan_software.md SOFTWARE-61/84: this backend's half of the cross-backend diagnostic
        # dump -- not registered as a ctest, just a plain executable a developer/script runs by
        # hand alongside SOFTWARE's own cna_diag_software and cna_diag_compare.
        cna_easygl_test(cna_diag_easygl examples/cross_backend_diagnostic_scene.cpp)

        # plan_dx9.md D9-A6: builds the SAME tools/xna-oracle/CnaOracleRender.cpp (D9-A3's own
        # scene-driven renderer, already backend-agnostic -- real public Game/GraphicsDeviceManager/
        # GraphicsDevice/BasicEffect/SpriteBatch API throughout, no EasyGL- or D3D9-specific code)
        # natively under the EasyGL backend, so the checked-in 31-scene XNA-oracle corpus can be
        # measured against EasyGL too -- no Wine/cross-compilation needed here, unlike the
        # D3D9-branch's own cna_oracle_render registration (this file's D3D9 CTest section) which
        # this target deliberately does not touch or duplicate. Not registered as a ctest, same
        # "it's a comparison tool with no pass/fail assertion of its own" precedent as
        # cna_diag_easygl just above and D9-A3's own cna_oracle_render -- xna-diff.py (invoked by
        # a script) is what judges this binary's PNG output against the checked-in reference
        # images.
        cna_easygl_test(cna_oracle_render_easygl tools/xna-oracle/CnaOracleRender.cpp)

        # Task 461: canary proving the new shared PixelTestGame helper (examples/common/) works
        cna_easygl_test(cna_test_easygl_pixeltestgame_smoke
                        examples/easygl_pixeltestgame_smoke_test.cpp)
        cna_register_backend_test(NAME EasyGL_PixelTestGame_Smoke COMMAND cna_test_easygl_pixeltestgame_smoke
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 463: canary proving PixelTestGame::CompareGoldenImage() (examples/common/) works.
        # WORKING_DIRECTORY is set to the repo root so the test's relative
        # "examples/golden/*.png" path resolves the same way regardless of which
        # cmake-build-* directory this is run from (CMake's default test working directory is
        # CMAKE_CURRENT_BINARY_DIR, not the source tree).
        cna_easygl_test(cna_test_easygl_goldenimage_smoke
                        examples/easygl_goldenimage_smoke_test.cpp)
        cna_register_backend_test(NAME EasyGL_GoldenImage_Smoke COMMAND cna_test_easygl_goldenimage_smoke
            TIMEOUT 30 WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 464: first real (non-canary) golden-image case, reusing Phase 42's already-
        # verified BasicEffect capstone scene (Task 370).
        cna_easygl_test(cna_test_easygl_basiceffect_golden
                        examples/easygl_basiceffect_golden_test.cpp)
        cna_register_backend_test(NAME EasyGL_BasicEffect_Golden COMMAND cna_test_easygl_basiceffect_golden
            TIMEOUT 30 WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 465: golden-image case reusing Phase 47's already-verified SpriteBatch
        # rotation-around-origin scene (Task 417).
        cna_easygl_test(cna_test_easygl_spritebatch_rotation_golden
                        examples/easygl_spritebatch_rotation_golden_test.cpp)
        cna_register_backend_test(NAME EasyGL_SpriteBatch_Rotation_Golden COMMAND cna_test_easygl_spritebatch_rotation_golden
            TIMEOUT 30 WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 466: golden-image case reusing Phase 35's already-verified texture-filter
        # magnification/Linear scene (Task 297).
        cna_easygl_test(cna_test_easygl_texture_filter_linear_golden
                        examples/easygl_texture_filter_linear_golden_test.cpp)
        cna_register_backend_test(NAME EasyGL_TextureFilter_Linear_Golden COMMAND cna_test_easygl_texture_filter_linear_golden
            TIMEOUT 30 WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 467: golden-image case reusing Phase 36's already-verified BlendState::Additive
        # scene (Task 306).
        cna_easygl_test(cna_test_easygl_blendstate_additive_golden
                        examples/easygl_blendstate_additive_golden_test.cpp)
        cna_register_backend_test(NAME EasyGL_BlendState_Additive_Golden COMMAND cna_test_easygl_blendstate_additive_golden
            TIMEOUT 30 WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 468: golden-image cases reusing Phase 37's DepthStencilState write-enable scene
        # (Task 313) and Phase 38's RasterizerState.CullMode scene (Task 323) -- two separate
        # files, one per phase, since no existing test combines both areas.
        cna_easygl_test(cna_test_easygl_depthstencilstate_write_enable_golden
                        examples/easygl_depthstencilstate_write_enable_golden_test.cpp)
        cna_register_backend_test(NAME EasyGL_DepthStencilState_WriteEnable_Golden COMMAND cna_test_easygl_depthstencilstate_write_enable_golden
            TIMEOUT 30 WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_rasterizerstate_cullmode_golden
                        examples/easygl_rasterizerstate_cullmode_golden_test.cpp)
        cna_register_backend_test(NAME EasyGL_RasterizerState_CullMode_Golden COMMAND cna_test_easygl_rasterizerstate_cullmode_golden
            TIMEOUT 30 WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 469: golden-image cases reusing Phases 43-46's already-verified stock-effect
        # scenes (AlphaTestEffect/Task 373, DualTextureEffect/Task 389, EnvironmentMapEffect/
        # Task 399, SkinnedEffect/Task 409) -- 4 separate files, one per phase.
        cna_easygl_test(cna_test_easygl_alphatesteffect_golden
                        examples/easygl_alphatesteffect_golden_test.cpp)
        cna_register_backend_test(NAME EasyGL_AlphaTestEffect_Golden COMMAND cna_test_easygl_alphatesteffect_golden
            TIMEOUT 30 WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_dualtextureeffect_golden
                        examples/easygl_dualtextureeffect_golden_test.cpp)
        cna_register_backend_test(NAME EasyGL_DualTextureEffect_Golden COMMAND cna_test_easygl_dualtextureeffect_golden
            TIMEOUT 30 WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_environmentmapeffect_golden
                        examples/easygl_environmentmapeffect_golden_test.cpp)
        cna_register_backend_test(NAME EasyGL_EnvironmentMapEffect_Golden COMMAND cna_test_easygl_environmentmapeffect_golden
            TIMEOUT 30 WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_skinnedeffect_golden
                        examples/easygl_skinnedeffect_golden_test.cpp)
        cna_register_backend_test(NAME EasyGL_SkinnedEffect_Golden COMMAND cna_test_easygl_skinnedeffect_golden
            TIMEOUT 30 WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # CNB-67 (Phase 13C): SkinnedEffect's new NOXNA VertexColorEnabled property.
        cna_easygl_test(cna_test_easygl_skinnedeffect_vertexcolor
                        examples/easygl_skinnedeffect_vertexcolor_test.cpp)
        cna_register_backend_test(NAME EasyGL_SkinnedEffect_VertexColor COMMAND cna_test_easygl_skinnedeffect_vertexcolor
            TIMEOUT 30 WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # CNB-58/60 (Phase 13A): PbrEffect's real glTF metallic-roughness BRDF shader.
        cna_easygl_test(cna_test_easygl_pbreffect_golden
                        examples/easygl_pbreffect_golden_test.cpp)
        cna_register_backend_test(NAME EasyGL_PbrEffect_Golden COMMAND cna_test_easygl_pbreffect_golden
            TIMEOUT 30 WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # PBR + skinning combo: SkinnedPbrEffect's shader (EnsurePbrSkinnedProgram()).
        cna_easygl_test(cna_test_easygl_skinnedpbreffect_golden
                        examples/easygl_skinnedpbreffect_golden_test.cpp)
        cna_register_backend_test(NAME EasyGL_SkinnedPbrEffect_Golden COMMAND cna_test_easygl_skinnedpbreffect_golden
            TIMEOUT 30 WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 87: RenderTarget2D → texture → readback
        cna_easygl_test(cna_test_easygl_render_target
                        examples/easygl_render_target_test.cpp)
        cna_register_backend_test(NAME EasyGL_RenderTarget2D_Readback COMMAND cna_test_easygl_render_target
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 335: RenderTarget2D depth buffer is real and functional (not just a stored property)
        cna_easygl_test(cna_test_easygl_rendertarget2d_depth
                        examples/rendertarget2d_depth_test.cpp)
        cna_register_backend_test(NAME EasyGL_RenderTarget2D_DepthBuffer COMMAND cna_test_easygl_rendertarget2d_depth
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 336: RenderTarget2D mip chain is genuinely GL-complete after auto-generation on unbind
        cna_easygl_test(cna_test_easygl_rendertarget2d_mip
                        examples/easygl_rendertarget2d_mip_test.cpp)
        cna_register_backend_test(NAME EasyGL_RenderTarget2D_MipComplete COMMAND cna_test_easygl_rendertarget2d_mip
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 337: RenderTarget2D MSAA resolve genuinely anti-aliases (not just solid-fill safe)
        cna_easygl_test(cna_test_easygl_rendertarget2d_msaa
                        examples/easygl_rendertarget2d_msaa_test.cpp)
        cna_register_backend_test(NAME EasyGL_RenderTarget2D_MsaaResolve COMMAND cna_test_easygl_rendertarget2d_msaa
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 338: SetRenderTarget(nullptr) resets Viewport/ScissorRectangle to the backbuffer
        cna_easygl_test(cna_test_easygl_rendertarget_viewport_scissor_reset
                        examples/rendertarget_viewport_scissor_reset_test.cpp)
        cna_register_backend_test(NAME EasyGL_RenderTarget_ViewportScissorReset COMMAND cna_test_easygl_rendertarget_viewport_scissor_reset
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 22: BasicEffect property tests
        cna_easygl_test(cna_test_basic_effect
                        examples/basic_effect_test.cpp)
        cna_register_backend_test(NAME EasyGL_BasicEffect_Properties COMMAND cna_test_basic_effect
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 23: AlphaTestEffect property tests
        cna_easygl_test(cna_test_alpha_test_effect
                        examples/alpha_test_effect_test.cpp)
        cna_register_backend_test(NAME EasyGL_AlphaTestEffect_Properties COMMAND cna_test_alpha_test_effect
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 24: SkinnedEffect property tests
        cna_easygl_test(cna_test_skinned_effect
                        examples/skinned_effect_test.cpp)
        cna_register_backend_test(NAME EasyGL_SkinnedEffect_Properties COMMAND cna_test_skinned_effect
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 27: SpriteFont property and MeasureString tests
        cna_easygl_test(cna_test_sprite_font
                        examples/sprite_font_test.cpp)
        cna_register_backend_test(NAME EasyGL_SpriteFont_Properties COMMAND cna_test_sprite_font
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 35: OcclusionQuery begin/end/IsComplete integration test
        cna_easygl_test(cna_test_occlusion_query
                        examples/occlusion_query_test.cpp)
        cna_register_backend_test(NAME EasyGL_OcclusionQuery_Cycle COMMAND cna_test_occlusion_query
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 445: Pixel/query test -- fully visible quad returns a positive occlusion-query pixel count.
        cna_easygl_test(cna_test_easygl_occlusion_query_visible_quad
                        examples/easygl_occlusion_query_visible_quad_test.cpp)
        cna_register_backend_test(NAME EasyGL_OcclusionQuery_VisibleQuad COMMAND cna_test_easygl_occlusion_query_visible_quad
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 446: Pixel/query test -- fully occluded quad returns a zero occlusion-query pixel count.
        cna_easygl_test(cna_test_easygl_occlusion_query_occluded_quad
                        examples/easygl_occlusion_query_occluded_quad_test.cpp)
        cna_register_backend_test(NAME EasyGL_OcclusionQuery_OccludedQuad COMMAND cna_test_easygl_occlusion_query_occluded_quad
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 122: AlphaTestEffect GPU alpha cutout + pixel readback
        cna_easygl_test(cna_test_alpha_test_integration
                        examples/alpha_test_integration_test.cpp)
        cna_register_backend_test(NAME EasyGL_AlphaTestEffect_AlphaCutout COMMAND cna_test_alpha_test_integration
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 123: SkinnedEffect 2-bone GPU transform + mesh deformation
        cna_easygl_test(cna_test_skinned_integration
                        examples/skinned_effect_integration_test.cpp)
        cna_register_backend_test(NAME EasyGL_SkinnedEffect_BoneDeformation COMMAND cna_test_skinned_integration
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 10.14: AvatarRenderer::EnableRealRenderingEXT/DrawRealEXT real GPU-skinned
        # rendering (Phase 10 — Avatar real-rendering NOXNA/EXT extension). Needs
        # CNA_GamerServices (AvatarRenderer lives there), so it's only built when networking
        # (and thus GamerServices) is enabled.
        if(CNA_ENABLE_NET)
            cna_easygl_test(cna_test_avatar_real_render
                            examples/avatar_real_render_integration_test.cpp)
            target_link_libraries(cna_test_avatar_real_render PRIVATE CNA_GamerServices)
            cna_register_backend_test(NAME EasyGL_AvatarRenderer_RealRender COMMAND cna_test_avatar_real_render
                TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

            # Task 11.21: SkinnedModelEXT::AttachPartEXT runtime wardrobe-attach real render.
            cna_easygl_test(cna_test_avatar_attach_part
                            examples/avatar_attach_part_integration_test.cpp)
            target_link_libraries(cna_test_avatar_attach_part PRIVATE CNA_GamerServices)
            cna_register_backend_test(NAME EasyGL_AvatarRenderer_AttachPart COMMAND cna_test_avatar_attach_part
                TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

            # Task 11.24: AvatarRenderer::PartTintEXT per-part tint routing regression test.
            cna_easygl_test(cna_test_avatar_tint_routing
                            examples/avatar_tint_routing_integration_test.cpp)
            target_link_libraries(cna_test_avatar_tint_routing PRIVATE CNA_GamerServices)
            cna_register_backend_test(NAME EasyGL_AvatarRenderer_TintRouting COMMAND cna_test_avatar_tint_routing
                TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
        endif()

        # Task 125: DXT1 texture loaded via FromStream, rendered, pixel readback
        cna_easygl_test(cna_test_dxt1_texture
                        examples/dxt1_texture_test.cpp)
        cna_register_backend_test(NAME EasyGL_DXT1_FromStream_Readback COMMAND cna_test_dxt1_texture
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 132: ShaderEffect (GLSL) with SpriteBatch — red-tint over green background
        cna_easygl_test(cna_test_easygl_shader_effect
                        examples/easygl_shader_effect_test.cpp)
        cna_register_backend_test(NAME EasyGL_ShaderEffect_GLSL COMMAND cna_test_easygl_shader_effect
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 1077: ShaderEffect::SetUniformXxx() must reach the real draw via SpriteBatch
        cna_easygl_test(cna_test_easygl_shader_effect_spritebatch_uniform
                        examples/easygl_shader_effect_spritebatch_uniform_test.cpp)
        cna_register_backend_test(NAME EasyGL_ShaderEffect_SpriteBatch_Uniform COMMAND cna_test_easygl_shader_effect_spritebatch_uniform
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 946: BloomExtract.fx HLSL->GLSL conversion proof + .shader.json ContentManager round-trip
        cna_easygl_test(cna_test_easygl_bloom_extract
                        examples/easygl_bloom_extract_test.cpp)
        cna_register_backend_test(NAME EasyGL_Bloom_Extract COMMAND cna_test_easygl_bloom_extract
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 946: GaussianBlur.fx HLSL->GLSL conversion proof (array uniforms)
        cna_easygl_test(cna_test_easygl_bloom_gaussianblur
                        examples/easygl_bloom_gaussianblur_test.cpp)
        cna_register_backend_test(NAME EasyGL_Bloom_GaussianBlur COMMAND cna_test_easygl_bloom_gaussianblur
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 946: BloomCombine.fx HLSL->GLSL conversion proof (2nd texture unit)
        cna_easygl_test(cna_test_easygl_bloom_combine
                        examples/easygl_bloom_combine_test.cpp)
        cna_register_backend_test(NAME EasyGL_Bloom_Combine COMMAND cna_test_easygl_bloom_combine
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 946: full 4-pass BloomSample pipeline end-to-end proof (extract->blurH->blurV->combine)
        cna_easygl_test(cna_test_easygl_bloom_pipeline
                        examples/easygl_bloom_pipeline_test.cpp)
        cna_register_backend_test(NAME EasyGL_Bloom_Pipeline COMMAND cna_test_easygl_bloom_pipeline
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 947 (Phase 78 rollout): Clouds.fx HLSL->GLSL conversion proof -- clears NetRumble's
        # shader blocker (the bloom trio above already proved the other 3 of its 4 shaders).
        cna_easygl_test(cna_test_easygl_clouds_shader
                        examples/easygl_clouds_shader_test.cpp)
        cna_register_backend_test(NAME EasyGL_Clouds_Shader COMMAND cna_test_easygl_clouds_shader
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 1079: ShaderEffect can now drive a real 3D DrawIndexedPrimitives() call, not just
        # SpriteBatch -- verifies World/View/Projection + attribute wiring for stride-32 vertices.
        cna_easygl_test(cna_test_easygl_shadereffect_3d
                        examples/easygl_shadereffect_3d_test.cpp)
        cna_register_backend_test(NAME EasyGL_ShaderEffect_3D COMMAND cna_test_easygl_shadereffect_3d
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 947 (Phase 78 rollout): PerPixelLighting.fx's PerPixelDiffuseAndPhong technique --
        # first real 3D shader conversion, using Task 1079's new 3D ShaderEffect capability.
        cna_easygl_test(cna_test_easygl_perpixellighting_shader
                        examples/easygl_perpixellighting_shader_test.cpp)
        cna_register_backend_test(NAME EasyGL_PerPixelLighting_Shader COMMAND cna_test_easygl_perpixellighting_shader
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 947: PerPixelLighting.fx's PerPixelDiffuse technique (diffuse-only, no specular).
        cna_easygl_test(cna_test_easygl_perpixellighting_diffuseonly_shader
                        examples/easygl_perpixellighting_diffuseonly_shader_test.cpp)
        cna_register_backend_test(NAME EasyGL_PerPixelLighting_DiffuseOnly_Shader COMMAND cna_test_easygl_perpixellighting_diffuseonly_shader
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 947: PerPixelLighting.fx's PerVertexDiffuseAndPerPixelPhong technique (diffuse per
        # vertex, specular per pixel).
        cna_easygl_test(cna_test_easygl_perpixellighting_vertexdiffuse_pixelphong_shader
                        examples/easygl_perpixellighting_vertexdiffuse_pixelphong_shader_test.cpp)
        cna_register_backend_test(NAME EasyGL_PerPixelLighting_VertexDiffusePixelPhong_Shader COMMAND cna_test_easygl_perpixellighting_vertexdiffuse_pixelphong_shader
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 947: VertexLighting.fx's PerVertexDiffuse technique (diffuse-only, fully per vertex).
        cna_easygl_test(cna_test_easygl_vertexlighting_diffuse_shader
                        examples/easygl_vertexlighting_diffuse_shader_test.cpp)
        cna_register_backend_test(NAME EasyGL_VertexLighting_Diffuse_Shader COMMAND cna_test_easygl_vertexlighting_diffuse_shader
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 947: VertexLighting.fx's PerVertexDiffuseAndPhong technique (diffuse+specular, fully
        # per vertex) -- with this one, all 5 of PerPixelLighting sample's effect/technique
        # combinations are ported.
        cna_easygl_test(cna_test_easygl_vertexlighting_diffusephong_shader
                        examples/easygl_vertexlighting_diffusephong_shader_test.cpp)
        cna_register_backend_test(NAME EasyGL_VertexLighting_DiffusePhong_Shader COMMAND cna_test_easygl_vertexlighting_diffusephong_shader
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 947: VertexLightingSample's OWN VertexLighting.fx (directional light -- distinct
        # from the same-named file already ported for the PerPixelLighting sample).
        cna_easygl_test(cna_test_easygl_vertexlighting_directional_shader
                        examples/easygl_vertexlighting_directional_shader_test.cpp)
        cna_register_backend_test(NAME EasyGL_VertexLighting_Directional_Shader COMMAND cna_test_easygl_vertexlighting_directional_shader
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 947: VertexLightingSample's FlatShaded.fx -- with this and the row above,
        # VertexLighting sample's shader blocker is fully cleared.
        cna_easygl_test(cna_test_easygl_flatshaded_shader
                        examples/easygl_flatshaded_shader_test.cpp)
        cna_register_backend_test(NAME EasyGL_FlatShaded_Shader COMMAND cna_test_easygl_flatshaded_shader
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 947: Distort.fx's Distort technique (no-blur variant).
        cna_easygl_test(cna_test_easygl_distort_shader
                        examples/easygl_distort_shader_test.cpp)
        cna_register_backend_test(NAME EasyGL_Distort_Shader COMMAND cna_test_easygl_distort_shader
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 947: Distorters.fx's DisplacementMapped technique.
        cna_easygl_test(cna_test_easygl_distorters_displacementmapped_shader
                        examples/easygl_distorters_displacementmapped_shader_test.cpp)
        cna_register_backend_test(NAME EasyGL_Distorters_DisplacementMapped_Shader COMMAND cna_test_easygl_distorters_displacementmapped_shader
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 947: Distorters.fx's HeatHaze technique.
        cna_easygl_test(cna_test_easygl_distorters_heathaze_shader
                        examples/easygl_distorters_heathaze_shader_test.cpp)
        cna_register_backend_test(NAME EasyGL_Distorters_HeatHaze_Shader COMMAND cna_test_easygl_distorters_heathaze_shader
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 947: Distorters.fx's PullIn technique -- with this, Distorters.fx's own 3
        # actually-used techniques (DisplacementMapped/HeatHaze/PullIn) are all ported.
        cna_easygl_test(cna_test_easygl_distorters_pullin_shader
                        examples/easygl_distorters_pullin_shader_test.cpp)
        cna_register_backend_test(NAME EasyGL_Distorters_PullIn_Shader COMMAND cna_test_easygl_distorters_pullin_shader
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 947: Distort.fx's DistortBlur technique -- with this, DistortionSample's shader
        # blocker is fully cleared.
        cna_easygl_test(cna_test_easygl_distortblur_shader
                        examples/easygl_distortblur_shader_test.cpp)
        cna_register_backend_test(NAME EasyGL_DistortBlur_Shader COMMAND cna_test_easygl_distortblur_shader
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 947: CartoonEffect.Fx's Lambert technique.
        cna_easygl_test(cna_test_easygl_cartooneffect_lambert_shader
                        examples/easygl_cartooneffect_lambert_shader_test.cpp)
        cna_register_backend_test(NAME EasyGL_CartoonEffect_Lambert_Shader COMMAND cna_test_easygl_cartooneffect_lambert_shader
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 947: CartoonEffect.Fx's Toon technique.
        cna_easygl_test(cna_test_easygl_cartooneffect_toon_shader
                        examples/easygl_cartooneffect_toon_shader_test.cpp)
        cna_register_backend_test(NAME EasyGL_CartoonEffect_Toon_Shader COMMAND cna_test_easygl_cartooneffect_toon_shader
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 947: CartoonEffect.Fx's NormalDepth technique -- with this, all 3 of
        # CartoonEffect.Fx's techniques are ported.
        cna_easygl_test(cna_test_easygl_cartooneffect_normaldepth_shader
                        examples/easygl_cartooneffect_normaldepth_shader_test.cpp)
        cna_register_backend_test(NAME EasyGL_CartoonEffect_NormalDepth_Shader COMMAND cna_test_easygl_cartooneffect_normaldepth_shader
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 947: PostprocessEffect.Fx's 5 techniques -- with this, NonPhotoRealistic sample's
        # shader blocker is fully cleared.
        cna_easygl_test(cna_test_easygl_postprocesseffect_shader
                        examples/easygl_postprocesseffect_shader_test.cpp)
        cna_register_backend_test(NAME EasyGL_PostprocessEffect_Shader COMMAND cna_test_easygl_postprocesseffect_shader
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 947: DrawModel.fx's CreateShadowMap technique (ShadowMappingSample_4_0).
        cna_easygl_test(cna_test_easygl_shadowmapping_createshadowmap_shader
                        examples/easygl_shadowmapping_createshadowmap_shader_test.cpp)
        cna_register_backend_test(NAME EasyGL_ShadowMapping_CreateShadowMap_Shader COMMAND cna_test_easygl_shadowmapping_createshadowmap_shader
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 947: DrawModel.fx's DrawWithShadowMap technique -- with this, ShadowMapping
        # sample's shader blocker is fully cleared.
        cna_easygl_test(cna_test_easygl_shadowmapping_drawwithshadowmap_shader
                        examples/easygl_shadowmapping_drawwithshadowmap_shader_test.cpp)
        cna_register_backend_test(NAME EasyGL_ShadowMapping_DrawWithShadowMap_Shader COMMAND cna_test_easygl_shadowmapping_drawwithshadowmap_shader
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 1080: genuinely custom vertex-layout support for the 3D ShaderEffect draw path
        # (a 48-byte stride/5-element declaration matching none of the 5 built-in strides).
        cna_easygl_test(cna_test_easygl_shadereffect_custom_vertex_layout
                        examples/easygl_shadereffect_custom_vertex_layout_test.cpp)
        cna_register_backend_test(NAME EasyGL_ShaderEffect_CustomVertexLayout COMMAND cna_test_easygl_shadereffect_custom_vertex_layout
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 947: NormalMapping.fx (tangent-space normal mapping + Phong specular) -- with
        # this, NormalMapping sample's shader blocker is fully cleared. Uses Task 1080's custom
        # vertex layout (Position+TexCoord+Normal+Binormal+Tangent, stride 56).
        cna_easygl_test(cna_test_easygl_normalmapping_shader
                        examples/easygl_normalmapping_shader_test.cpp)
        cna_register_backend_test(NAME EasyGL_NormalMapping_Shader COMMAND cna_test_easygl_normalmapping_shader
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 947: Billboard.fx (view-facing billboard expansion + wind sway + alpha-tested
        # diffuse lighting) -- with this, BillboardSample's shader blocker is fully cleared.
        # Uses Task 1080's custom vertex layout (Position+Normal+TexCoord+Random, stride 36).
        cna_easygl_test(cna_test_easygl_billboard_shader
                        examples/easygl_billboard_shader_test.cpp)
        cna_register_backend_test(NAME EasyGL_Billboard_Shader COMMAND cna_test_easygl_billboard_shader
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 947: ShatterEffect.fx (per-triangle rotation-and-fall shatter animation + Phong
        # lighting) -- with this, ShatterEffect sample's shader blocker is fully cleared. Uses
        # Task 1080's custom vertex layout (Position+Normal+TexCoords+TriangleCenter+
        # RotationalVelocity, stride 56).
        cna_easygl_test(cna_test_easygl_shattereffect_shader
                        examples/easygl_shattereffect_shader_test.cpp)
        cna_register_backend_test(NAME EasyGL_ShatterEffect_Shader COMMAND cna_test_easygl_shattereffect_shader
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 947: ParticleEffect.fx (GPU-animated billboarded particles) -- shared verbatim by
        # Particles3DSample and XmlParticles, so this clears both samples' shader blockers at
        # once. Uses Task 1080's custom vertex layout (Corner+Position+Velocity+Random+Time,
        # stride 52).
        cna_easygl_test(cna_test_easygl_particleeffect_shader
                        examples/easygl_particleeffect_shader_test.cpp)
        cna_register_backend_test(NAME EasyGL_ParticleEffect_Shader COMMAND cna_test_easygl_particleeffect_shader
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 947: ShipGame's AnimSprite.fx (sprite-sheet frame cross-fade), 1 of ShipGame's 4
        # distinct custom shaders. Uses VertexPositionTexture (stride 20, already supported).
        cna_easygl_test(cna_test_easygl_animsprite_shader
                        examples/easygl_animsprite_shader_test.cpp)
        cna_register_backend_test(NAME EasyGL_AnimSprite_Shader COMMAND cna_test_easygl_animsprite_shader
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 947: ShipGame's Blur.fx (5-technique box blur / colour / colour-texture), 2 of
        # ShipGame's 4 distinct custom shaders now ported.
        cna_easygl_test(cna_test_easygl_blur_shader
                        examples/easygl_blur_shader_test.cpp)
        cna_register_backend_test(NAME EasyGL_ShipGame_Blur_Shader COMMAND cna_test_easygl_blur_shader
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 1081: TextureCube sampling support for custom ShaderEffect shaders (needed for
        # ShipGame's own NormalMapping.fx reflection map).
        cna_easygl_test(cna_test_easygl_shadereffect_texturecube
                        examples/easygl_shadereffect_texturecube_test.cpp)
        cna_register_backend_test(NAME EasyGL_ShaderEffect_TextureCube COMMAND cna_test_easygl_shadereffect_texturecube
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # plan_graphics.md Task 863: Texture3D now inherits Texture (matching FNA), and
        # ShaderEffect::SetTexture(int, Texture3D&)/IEffectBackend::BindTexture3D() let a custom
        # shader sample a real sampler3D uniform -- same shape as Task 1081's TextureCube proof
        # above, adapted to a volume texture.
        cna_easygl_test(cna_test_easygl_shadereffect_texture3d
                        examples/easygl_shadereffect_texture3d_test.cpp)
        cna_register_backend_test(NAME EasyGL_ShaderEffect_Texture3D COMMAND cna_test_easygl_shadereffect_texture3d
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 947: ShipGame's own NormalMapping.fx (3 techniques: PlainMapping/NormalMapping/
        # ViewMapping) -- 3 of ShipGame's 4 distinct custom shaders now ported. Uses Task 1081's
        # TextureCube capability.
        cna_easygl_test(cna_test_easygl_shipgame_normalmapping_shader
                        examples/easygl_shipgame_normalmapping_shader_test.cpp)
        cna_register_backend_test(NAME EasyGL_ShipGame_NormalMapping_Shader COMMAND cna_test_easygl_shipgame_normalmapping_shader
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 947: ShipGame's own Particle.fx (real GPU point sprites via PSIZE/gl_PointSize) --
        # the 4th and final ShipGame custom shader, fully clearing ShipGame's shader blocker.
        cna_easygl_test(cna_test_easygl_shipgame_particle_shader
                        examples/easygl_shipgame_particle_shader_test.cpp)
        cna_register_backend_test(NAME EasyGL_ShipGame_Particle_Shader COMMAND cna_test_easygl_shipgame_particle_shader
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 947 (final sample) + Task 1082: InstancedModel.fx's HardwareInstancing technique --
        # proves real GPU hardware instancing (glVertexAttribDivisor-based per-instance vertex
        # stream through a custom ShaderEffect), completing Task 947 at 13/13.
        cna_easygl_test(cna_test_easygl_instancedmodel_shader
                        examples/easygl_instancedmodel_shader_test.cpp)
        cna_register_backend_test(NAME EasyGL_InstancedModel_Shader COMMAND cna_test_easygl_instancedmodel_shader
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 133: DualTextureEffect — magenta×yellow→red pixel readback
        cna_easygl_test(cna_test_easygl_dual_texture
                        examples/easygl_dual_texture_test.cpp)
        cna_register_backend_test(NAME EasyGL_DualTextureEffect_Blend COMMAND cna_test_easygl_dual_texture
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 134: EnvironmentMapEffect — emissive red, envAmount=0, pixel readback
        cna_easygl_test(cna_test_easygl_env_map
                        examples/easygl_env_map_test.cpp)
        cna_register_backend_test(NAME EasyGL_EnvironmentMapEffect_Readback COMMAND cna_test_easygl_env_map
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # audit_net.md remediation (2026-07-18, sixth round): discriminating regression test for
        # EmissiveColor composition (light*diffuse + emissive, NOT (emissive+light)*diffuse) across
        # all three EasyGL shader paths that had the bug. Uses a DiffuseColor strictly between 0
        # and 1 so a squared term is detectable - every pre-existing test used 0/1 components,
        # where x*x == x, which is why the bug went unnoticed.
        cna_easygl_test(cna_test_easygl_emissive_ambient_composition
                        examples/easygl_emissive_ambient_composition_test.cpp)
        cna_register_backend_test(NAME EasyGL_EmissiveAmbientComposition COMMAND cna_test_easygl_emissive_ambient_composition
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 393: EnvironmentMapEffect EnvironmentMapAmount=0 should ignore the cube map
        cna_easygl_test(cna_test_easygl_environmentmapeffect_amount_zero
                        examples/easygl_environmentmapeffect_amount_zero_test.cpp)
        cna_register_backend_test(NAME EasyGL_EnvironmentMapEffect_AmountZero COMMAND cna_test_easygl_environmentmapeffect_amount_zero
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 900: EnvironmentMapEffect fog fix verification
        cna_easygl_test(cna_test_easygl_environmentmapeffect_fog
                        examples/easygl_environmentmapeffect_fog_test.cpp)
        cna_register_backend_test(NAME EasyGL_EnvironmentMapEffect_Fog COMMAND cna_test_easygl_environmentmapeffect_fog
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 394: EnvironmentMapEffect EnvironmentMapAmount=1 should fully replace lit color
        cna_easygl_test(cna_test_easygl_environmentmapeffect_amount_one
                        examples/easygl_environmentmapeffect_amount_one_test.cpp)
        cna_register_backend_test(NAME EasyGL_EnvironmentMapEffect_AmountOne COMMAND cna_test_easygl_environmentmapeffect_amount_one
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 395: EnvironmentMapEffect EnvironmentMapSpecular should scale by cubemap alpha
        cna_easygl_test(cna_test_easygl_environmentmapeffect_specular
                        examples/easygl_environmentmapeffect_specular_test.cpp)
        cna_register_backend_test(NAME EasyGL_EnvironmentMapEffect_Specular COMMAND cna_test_easygl_environmentmapeffect_specular
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 891: EnvironmentMapEffect's base cube-map lerp target must be scaled by the
        # combined texture x diffuse alpha -- verbatim reuse of the shared backend-agnostic
        # source, registered on all 3 backends.
        cna_easygl_test(cna_test_easygl_environmentmapeffect_alphascaledlerp
                        examples/environmentmapeffect_alphascaledlerp_test.cpp)
        cna_register_backend_test(NAME EasyGL_EnvironmentMapEffect_AlphaScaledLerp COMMAND cna_test_easygl_environmentmapeffect_alphascaledlerp
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 396: EnvironmentMapEffect Fresnel edge-weighting should suppress head-on reflections
        cna_easygl_test(cna_test_easygl_environmentmapeffect_fresnel
                        examples/easygl_environmentmapeffect_fresnel_test.cpp)
        cna_register_backend_test(NAME EasyGL_EnvironmentMapEffect_Fresnel COMMAND cna_test_easygl_environmentmapeffect_fresnel
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 1112: EnvironmentMapEffect's Fresnel term must be computed per-vertex and Gouraud-
        # interpolated, not recomputed per-fragment from a renormalized interpolated normal.
        cna_easygl_test(cna_test_easygl_environmentmapeffect_fresnel_gradient
                        examples/easygl_environmentmapeffect_fresnel_gradient_test.cpp)
        cna_register_backend_test(NAME EasyGL_EnvironmentMapEffect_Fresnel_Gradient COMMAND cna_test_easygl_environmentmapeffect_fresnel_gradient
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 397: EnvironmentMapEffect reflection vector should respond to EyePosition
        cna_easygl_test(cna_test_easygl_environmentmapeffect_eyeposition
                        examples/easygl_environmentmapeffect_eyeposition_test.cpp)
        cna_register_backend_test(NAME EasyGL_EnvironmentMapEffect_EyePosition COMMAND cna_test_easygl_environmentmapeffect_eyeposition
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 398: EnvironmentMapEffect normal transform should use inverse-transpose of World
        cna_easygl_test(cna_test_easygl_environmentmapeffect_worldtransform
                        examples/easygl_environmentmapeffect_worldtransform_test.cpp)
        cna_register_backend_test(NAME EasyGL_EnvironmentMapEffect_WorldTransform COMMAND cna_test_easygl_environmentmapeffect_worldtransform
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 399: cross-backend EnvironmentMapEffect capstone (Tasks 393-398 combined)
        cna_easygl_test(cna_test_easygl_environmentmapeffect_combined
                        examples/easygl_environmentmapeffect_combined_test.cpp)
        cna_register_backend_test(NAME EasyGL_EnvironmentMapEffect_Combined COMMAND cna_test_easygl_environmentmapeffect_combined
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 890: EnvironmentMapEffect DirectionalLight1/DirectionalLight2 forwarding
        cna_easygl_test(cna_test_easygl_environmentmapeffect_multilight
                        examples/easygl_environmentmapeffect_multilight_test.cpp)
        cna_register_backend_test(NAME EasyGL_EnvironmentMapEffect_MultiLight COMMAND cna_test_easygl_environmentmapeffect_multilight
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 900: SkinnedEffect fog fix verification
        cna_easygl_test(cna_test_easygl_skinnedeffect_fog
                        examples/easygl_skinnedeffect_fog_test.cpp)
        cna_register_backend_test(NAME EasyGL_SkinnedEffect_Fog COMMAND cna_test_easygl_skinnedeffect_fog
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-010: transformed-camera view-space fog conformance (fails pre-fix).
        cna_easygl_test(cna_test_easygl_viewspace_fog
                        examples/easygl_viewspace_fog_test.cpp)
        cna_register_backend_test(NAME EasyGL_ViewSpace_Fog COMMAND cna_test_easygl_viewspace_fog
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 893: SkinnedEffect DirectionalLight1/DirectionalLight2 forwarding
        cna_easygl_test(cna_test_easygl_skinnedeffect_multilight
                        examples/easygl_skinnedeffect_multilight_test.cpp)
        cna_register_backend_test(NAME EasyGL_SkinnedEffect_MultiLight COMMAND cna_test_easygl_skinnedeffect_multilight
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-008: analytic SkinnedEffect ambient/emissive lighting conformance
        cna_easygl_test(cna_test_easygl_skinnedeffect_lighting_conformance
                        examples/skinnedeffect_lighting_conformance_test.cpp)
        cna_register_backend_test(NAME EasyGL_SkinnedEffect_LightingConformance COMMAND cna_test_easygl_skinnedeffect_lighting_conformance
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 894: SkinnedEffect real specular highlights
        cna_easygl_test(cna_test_easygl_skinnedeffect_specular
                        examples/easygl_skinnedeffect_specular_test.cpp)
        cna_register_backend_test(NAME EasyGL_SkinnedEffect_Specular COMMAND cna_test_easygl_skinnedeffect_specular
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 1102b: SkinnedEffect pixel test — PreferPerPixelLighting genuinely selects between
        # a real per-vertex-lit shader variant and the existing per-pixel-lit one (mirrors Task
        # 1102's BasicEffect test exactly).
        cna_easygl_test(cna_test_easygl_skinnedeffect_preferperpixellighting
                        examples/easygl_skinnedeffect_preferperpixellighting_test.cpp)
        cna_register_backend_test(NAME EasyGL_SkinnedEffect_PreferPerPixelLighting COMMAND cna_test_easygl_skinnedeffect_preferperpixellighting
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 895: SkinnedEffect WeightsPerVertex real GPU enforcement
        cna_easygl_test(cna_test_easygl_skinnedeffect_weightspervertex
                        examples/easygl_skinnedeffect_weightspervertex_test.cpp)
        cna_register_backend_test(NAME EasyGL_SkinnedEffect_WeightsPerVertex COMMAND cna_test_easygl_skinnedeffect_weightspervertex
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 406: SkinnedEffect identity bone palette should produce zero deformation
        cna_easygl_test(cna_test_easygl_skinnedeffect_identity_bones
                        examples/easygl_skinnedeffect_identity_bones_test.cpp)
        cna_register_backend_test(NAME EasyGL_SkinnedEffect_IdentityBones COMMAND cna_test_easygl_skinnedeffect_identity_bones
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 407: SkinnedEffect single translation bone should shift the mesh
        cna_easygl_test(cna_test_easygl_skinnedeffect_translation_bone
                        examples/easygl_skinnedeffect_translation_bone_test.cpp)
        cna_register_backend_test(NAME EasyGL_SkinnedEffect_TranslationBone COMMAND cna_test_easygl_skinnedeffect_translation_bone
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 408: SkinnedEffect two-bone weighted blend should produce midpoint deformation
        cna_easygl_test(cna_test_easygl_skinnedeffect_twobone_blend
                        examples/easygl_skinnedeffect_twobone_blend_test.cpp)
        cna_register_backend_test(NAME EasyGL_SkinnedEffect_TwoBoneBlend COMMAND cna_test_easygl_skinnedeffect_twobone_blend
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 409: cross-backend SkinnedEffect capstone (identity + single-bone + two-bone blend)
        cna_easygl_test(cna_test_easygl_skinnedeffect_combined
                        examples/easygl_skinnedeffect_combined_test.cpp)
        cna_register_backend_test(NAME EasyGL_SkinnedEffect_Combined COMMAND cna_test_easygl_skinnedeffect_combined
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 334: RenderTargetCube sampled as TextureCube via EnvironmentMapEffect after unbinding
        cna_easygl_test(cna_test_easygl_rendertargetcube_sample
                        examples/easygl_rendertargetcube_sample_test.cpp)
        cna_register_backend_test(NAME EasyGL_RenderTargetCube_SampleAfterUnbind COMMAND cna_test_easygl_rendertargetcube_sample
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 877: RenderTargetCube honors its exact requested DepthFormat (None vs Depth24Stencil8)
        cna_easygl_test(cna_test_easygl_rendertargetcube_depthformat
                        examples/easygl_rendertargetcube_depthformat_test.cpp)
        cna_register_backend_test(NAME EasyGL_RenderTargetCube_DepthFormat COMMAND cna_test_easygl_rendertargetcube_depthformat
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 144: Model.Draw — 2-bone model, BasicEffect VertexColor, red quad, pixel readback
        cna_easygl_test(cna_test_easygl_model_draw
                        examples/easygl_model_draw_test.cpp)
        cna_register_backend_test(NAME EasyGL_ModelDraw_RedQuad COMMAND cna_test_easygl_model_draw
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 927: Content.Load<Model>() via a real .model.json + binary vertex/index file
        # fixture -- exercises ModelTypeReader::Read() itself (the vertex-stride/vtable-size
        # corruption bug found while porting ../cna-samples), unlike Task 144's hand-built-in-C++
        # model above.
        cna_easygl_test(cna_test_easygl_model_json_reader
                        examples/easygl_model_json_reader_test.cpp)
        cna_register_backend_test(NAME EasyGL_ModelJsonReader_Quad COMMAND cna_test_easygl_model_json_reader
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 931: ModelTypeReader::Read() auto-selecting 32-bit indices once a mesh exceeds
        # 65535 vertices, mirroring real XNA's stock ModelProcessor.
        cna_easygl_test(cna_test_easygl_model_json_reader_32bit_indices
                        examples/easygl_model_json_reader_32bit_indices_test.cpp)
        cna_register_backend_test(NAME EasyGL_ModelJsonReader_32BitIndices COMMAND cna_test_easygl_model_json_reader_32bit_indices
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 932: ModelTypeReader::Read() binding a per-mesh "texture" field to BasicEffect.
        cna_easygl_test(cna_test_easygl_model_json_reader_texture
                        examples/easygl_model_json_reader_texture_test.cpp)
        cna_register_backend_test(NAME EasyGL_ModelJsonReader_Texture COMMAND cna_test_easygl_model_json_reader_texture
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 934: Content.Load<TextureCube>() via a real .dds cubemap file fixture --
        # exercises the new TextureCubeTypeReader (ContentManager.cpp).
        cna_easygl_test(cna_test_easygl_texturecube_content_load
                        examples/easygl_texturecube_content_load_test.cpp)
        cna_register_backend_test(NAME EasyGL_TextureCube_ContentLoad COMMAND cna_test_easygl_texturecube_content_load
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 937: ModelTypeReader::Read() gives each mesh its own real ModelBone (a child of
        # Root, named after the mesh) instead of leaving ParentBone null. Pure structural test,
        # no rendering.
        cna_easygl_test(cna_test_easygl_model_json_reader_bone_hierarchy
                        examples/easygl_model_json_reader_bone_hierarchy_test.cpp)
        cna_register_backend_test(NAME EasyGL_ModelJsonReader_BoneHierarchy COMMAND cna_test_easygl_model_json_reader_bone_hierarchy
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 941: ModelTypeReader::Read() parses .model.json's new "skeleton"/"animations"
        # fields into a SkinningData attached to Model.Tag, and honors a mesh's "effect":
        # "SkinnedEffect" + "texture" field. Pure structural test, no rendering.
        cna_easygl_test(cna_test_easygl_model_json_reader_skeleton
                        examples/easygl_model_json_reader_skeleton_test.cpp)
        cna_register_backend_test(NAME EasyGL_ModelJsonReader_Skeleton COMMAND cna_test_easygl_model_json_reader_skeleton
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 942: Pixel test -- AnimationPlayer::GetSkinTransforms() fed into a real, Content.
        # Load<Model>()-loaded mesh's SkinnedEffect::SetBoneTransforms() before Model.Draw()
        # actually deforms the rendered mesh as the clip advances.
        cna_easygl_test(cna_test_easygl_model_skinned_animation_playback
                        examples/easygl_model_skinned_animation_playback_test.cpp)
        cna_register_backend_test(NAME EasyGL_Model_SkinnedAnimationPlayback COMMAND cna_test_easygl_model_skinned_animation_playback
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 438: Pixel test -- Model with two meshes, each using its own distinct Effect.
        cna_easygl_test(cna_test_easygl_model_two_meshes_effects
                        examples/easygl_model_two_meshes_effects_test.cpp)
        cna_register_backend_test(NAME EasyGL_Model_TwoMeshesEffects COMMAND cna_test_easygl_model_two_meshes_effects
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 439: Pixel test -- a child mesh's ParentBone's absolute transform actually moves it.
        cna_easygl_test(cna_test_easygl_model_hierarchy_child_mesh
                        examples/easygl_model_hierarchy_child_mesh_test.cpp)
        cna_register_backend_test(NAME EasyGL_Model_HierarchyChildMesh COMMAND cna_test_easygl_model_hierarchy_child_mesh
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 145: MRT — SetRenderTargets({rt0,rt1}), draw green to attachment0, verify left=green right=blue
        cna_easygl_test(cna_test_easygl_mrt
                        examples/easygl_mrt_test.cpp)
        cna_register_backend_test(NAME EasyGL_MRT_TwoAttachments COMMAND cna_test_easygl_mrt
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 146: MSAA 4× — multisampled renderbuffer + resolve on Present
        cna_easygl_test(cna_test_easygl_msaa
                        examples/easygl_msaa_test.cpp)
        cna_register_backend_test(NAME EasyGL_MSAA_4x_Readback COMMAND cna_test_easygl_msaa
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 167: SpriteEffects — FlipHorizontally, FlipVertically, FlipHV pixel readback
        cna_easygl_test(cna_test_easygl_sprite_effects
                        examples/easygl_sprite_effects_test.cpp)
        cna_register_backend_test(NAME EasyGL_SpriteEffects_Flip COMMAND cna_test_easygl_sprite_effects
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 417: SpriteBatch Draw rotation must pivot around the specified origin
        cna_easygl_test(cna_test_easygl_spritebatch_rotation
                        examples/easygl_spritebatch_rotation_test.cpp)
        cna_register_backend_test(NAME EasyGL_SpriteBatch_RotationAroundOrigin COMMAND cna_test_easygl_spritebatch_rotation
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 418: SpriteBatch Draw scalar/Vector2 scale overloads produce expected size
        cna_easygl_test(cna_test_easygl_spritebatch_scale
                        examples/easygl_spritebatch_scale_test.cpp)
        cna_register_backend_test(NAME EasyGL_SpriteBatch_ScaleOverloads COMMAND cna_test_easygl_spritebatch_scale
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 419: SpriteBatch Draw sourceRectangle must crop, not stretch the whole texture
        cna_easygl_test(cna_test_easygl_spritebatch_sourcerect
                        examples/easygl_spritebatch_sourcerect_test.cpp)
        cna_register_backend_test(NAME EasyGL_SpriteBatch_SourceRectangleCropping COMMAND cna_test_easygl_spritebatch_sourcerect
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 420: layerDepth must affect on-screen draw order under FrontToBack
        cna_easygl_test(cna_test_easygl_spritebatch_layerdepth
                        examples/easygl_spritebatch_layerdepth_test.cpp)
        cna_register_backend_test(NAME EasyGL_SpriteBatch_LayerDepthOrder COMMAND cna_test_easygl_spritebatch_layerdepth
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 885: BasicEffect pixel test — DirectionalLight1/2 + EmissiveColor on the lit path
        cna_easygl_test(cna_test_easygl_basiceffect_multilight_emissive
                        examples/easygl_basiceffect_multilight_emissive_test.cpp)
        cna_register_backend_test(NAME EasyGL_BasicEffect_MultiLightEmissive COMMAND cna_test_easygl_basiceffect_multilight_emissive
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 886: BasicEffect pixel test — real specular highlights
        cna_easygl_test(cna_test_easygl_basiceffect_specular
                        examples/easygl_basiceffect_specular_test.cpp)
        cna_register_backend_test(NAME EasyGL_BasicEffect_Specular COMMAND cna_test_easygl_basiceffect_specular
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 1102: BasicEffect pixel test — PreferPerPixelLighting genuinely selects between
        # a real per-vertex-lit shader variant and the existing per-pixel-lit one.
        cna_easygl_test(cna_test_easygl_basiceffect_preferperpixellighting
                        examples/easygl_basiceffect_preferperpixellighting_test.cpp)
        cna_register_backend_test(NAME EasyGL_BasicEffect_PreferPerPixelLighting COMMAND cna_test_easygl_basiceffect_preferperpixellighting
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 168: transformMatrix in SpriteBatch::Begin — translation pixel readback
        cna_easygl_test(cna_test_easygl_transform_matrix
                        examples/easygl_transform_matrix_test.cpp)
        cna_register_backend_test(NAME EasyGL_TransformMatrix_Translation COMMAND cna_test_easygl_transform_matrix
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 169: Texture2D SetData/GetData partial rectangle round-trip
        cna_easygl_test(cna_test_easygl_texture2d_partial_rect
                        examples/easygl_texture2d_partial_rect_test.cpp)
        cna_register_backend_test(NAME EasyGL_Texture2D_PartialRect_RoundTrip COMMAND cna_test_easygl_texture2d_partial_rect
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 171: Texture2D mip-level SetData/GetData round-trip
        cna_easygl_test(cna_test_easygl_texture2d_mip
                        examples/easygl_texture2d_mip_test.cpp)
        cna_register_backend_test(NAME EasyGL_Texture2D_Mip_RoundTrip COMMAND cna_test_easygl_texture2d_mip
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 172: TextureCube per-face SetData/GetData round-trip
        cna_easygl_test(cna_test_easygl_texturecube_faces
                        examples/easygl_texturecube_faces_test.cpp)
        cna_register_backend_test(NAME EasyGL_TextureCube_Faces_RoundTrip COMMAND cna_test_easygl_texturecube_faces
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 276: TextureCube mip-level SetData/GetData round-trip, all six faces
        cna_easygl_test(cna_test_easygl_texturecube_mip
                        examples/easygl_texturecube_mip_test.cpp)
        cna_register_backend_test(NAME EasyGL_TextureCube_Mip_RoundTrip COMMAND cna_test_easygl_texturecube_mip
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 275: TextureCube partial rect + startIndex, all six faces
        cna_easygl_test(cna_test_easygl_texturecube_partial_rect
                        examples/easygl_texturecube_partial_rect_test.cpp)
        cna_register_backend_test(NAME EasyGL_TextureCube_PartialRect_RoundTrip COMMAND cna_test_easygl_texturecube_partial_rect
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 173: Texture3D z-slice SetData/GetData round-trip
        cna_easygl_test(cna_test_easygl_texture3d_slices
                        examples/easygl_texture3d_slices_test.cpp)
        cna_register_backend_test(NAME EasyGL_Texture3D_Slices_RoundTrip COMMAND cna_test_easygl_texture3d_slices
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 862: Texture3D mip-level SetData/GetData round-trip
        cna_easygl_test(cna_test_easygl_texture3d_mip
                        examples/easygl_texture3d_mip_test.cpp)
        cna_register_backend_test(NAME EasyGL_Texture3D_Mip_RoundTrip COMMAND cna_test_easygl_texture3d_mip
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 273: Texture3D partial box (x/y/z region) SetData/GetData round-trip
        cna_easygl_test(cna_test_easygl_texture3d_partial_box
                        examples/easygl_texture3d_partial_box_test.cpp)
        cna_register_backend_test(NAME EasyGL_Texture3D_PartialBox_RoundTrip COMMAND cna_test_easygl_texture3d_partial_box
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 274: Texture3D partial box (x/y/z region) GetData readback
        cna_easygl_test(cna_test_easygl_texture3d_partial_box_readback
                        examples/easygl_texture3d_partial_box_readback_test.cpp)
        cna_register_backend_test(NAME EasyGL_Texture3D_PartialBox_Readback COMMAND cna_test_easygl_texture3d_partial_box_readback
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 176: SurfaceFormat validation — unsupported formats throw
        cna_easygl_test(cna_test_easygl_surface_format_throws
                        examples/easygl_surface_format_throws_test.cpp)
        cna_register_backend_test(NAME EasyGL_SurfaceFormat_Throws COMMAND cna_test_easygl_surface_format_throws
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 177: RenderTargetUsage DiscardContents vs PreserveContents
        cna_easygl_test(cna_test_easygl_render_target_usage
                        examples/easygl_render_target_usage_test.cpp)
        cna_register_backend_test(NAME EasyGL_RenderTargetUsage COMMAND cna_test_easygl_render_target_usage
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 180: backbuffer → RT → backbuffer → RT → backbuffer round-trip
        cna_easygl_test(cna_test_easygl_rt_roundtrip
                        examples/easygl_rt_roundtrip_test.cpp)
        cna_register_backend_test(NAME EasyGL_RT_Roundtrip COMMAND cna_test_easygl_rt_roundtrip
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 182: PresentationParameters round-trip via GraphicsDeviceManager
        cna_easygl_test(cna_test_easygl_presentation_parameters
                        examples/easygl_presentation_parameters_test.cpp)
        cna_register_backend_test(NAME EasyGL_PresentationParameters COMMAND cna_test_easygl_presentation_parameters
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_device_reset_events
                        examples/easygl_device_reset_events_test.cpp)
        cna_register_backend_test(NAME EasyGL_DeviceResetEvents COMMAND cna_test_easygl_device_reset_events
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_device_validation
                        examples/easygl_device_validation_test.cpp)
        cna_register_backend_test(NAME EasyGL_DeviceValidation COMMAND cna_test_easygl_device_validation
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_draw_novertexbuffer
                        examples/easygl_draw_novertexbuffer_test.cpp)
        cna_register_backend_test(NAME EasyGL_DrawNoVertexBuffer COMMAND cna_test_easygl_draw_novertexbuffer
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_draw_noindexbuffer
                        examples/easygl_draw_noindexbuffer_test.cpp)
        cna_register_backend_test(NAME EasyGL_DrawNoIndexBuffer COMMAND cna_test_easygl_draw_noindexbuffer
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_draw_range_validation
                        examples/easygl_draw_range_validation_test.cpp)
        cna_register_backend_test(NAME EasyGL_DrawRangeValidation COMMAND cna_test_easygl_draw_range_validation
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_primitivetype_validation
                        examples/easygl_primitivetype_validation_test.cpp)
        cna_register_backend_test(NAME EasyGL_PrimitiveTypeValidation COMMAND cna_test_easygl_primitivetype_validation
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_clear_overloads
                        examples/easygl_clear_overloads_test.cpp)
        cna_register_backend_test(NAME EasyGL_ClearOverloads COMMAND cna_test_easygl_clear_overloads
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_viewport_state
                        examples/easygl_viewport_state_test.cpp)
        cna_register_backend_test(NAME EasyGL_ViewportState COMMAND cna_test_easygl_viewport_state
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_scissor
                        examples/easygl_scissor_test.cpp)
        cna_register_backend_test(NAME EasyGL_Scissor COMMAND cna_test_easygl_scissor
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 880: Viewport sub-region (split-screen) GPU wiring
        cna_easygl_test(cna_test_easygl_viewport_subregion
                        examples/easygl_viewport_subregion_test.cpp)
        cna_register_backend_test(NAME EasyGL_Viewport_Subregion COMMAND cna_test_easygl_viewport_subregion
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_disposed_resource
                        examples/easygl_disposed_resource_test.cpp)
        cna_register_backend_test(NAME EasyGL_DisposedResource COMMAND cna_test_easygl_disposed_resource
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_double_dispose
                        examples/easygl_double_dispose_test.cpp)
        cna_register_backend_test(NAME EasyGL_DoubleDispose COMMAND cna_test_easygl_double_dispose
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_bound_resource_dispose
                        examples/easygl_bound_resource_dispose_test.cpp)
        cna_register_backend_test(NAME EasyGL_BoundResourceDispose COMMAND cna_test_easygl_bound_resource_dispose
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_handle_release
                        examples/easygl_handle_release_test.cpp)
        cna_register_backend_test(NAME EasyGL_HandleRelease COMMAND cna_test_easygl_handle_release
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_move_semantics
                        examples/easygl_move_semantics_test.cpp)
        cna_register_backend_test(NAME EasyGL_MoveSemantics COMMAND cna_test_easygl_move_semantics
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_resource_events
                        examples/easygl_resource_events_test.cpp)
        cna_register_backend_test(NAME EasyGL_ResourceEvents COMMAND cna_test_easygl_resource_events
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_device_dispose_order
                        examples/easygl_device_dispose_order_test.cpp)
        cna_register_backend_test(NAME EasyGL_DeviceDisposeOrder COMMAND cna_test_easygl_device_dispose_order
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_resource_leak
                        examples/easygl_resource_leak_test.cpp)
        cna_register_backend_test(NAME EasyGL_ResourceLeak COMMAND cna_test_easygl_resource_leak
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_present_interval
                        examples/easygl_present_interval_test.cpp)
        cna_register_backend_test(NAME EasyGL_PresentInterval COMMAND cna_test_easygl_present_interval
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_fullscreen_field
                        examples/easygl_fullscreen_field_test.cpp)
        cna_register_backend_test(NAME EasyGL_FullScreenField COMMAND cna_test_easygl_fullscreen_field
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_backbuffer_resize
                        examples/easygl_backbuffer_resize_test.cpp)
        cna_register_backend_test(NAME EasyGL_BackbufferResize COMMAND cna_test_easygl_backbuffer_resize
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_real_window_resize
                        examples/easygl_real_window_resize_test.cpp)
        cna_register_backend_test(NAME EasyGL_RealWindowResize COMMAND cna_test_easygl_real_window_resize
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 349: custom Viewport survives Present() with no resize, resets after a real one.
        cna_easygl_test(cna_test_easygl_viewport_reset_after_resize
                        examples/viewport_reset_after_resize_test.cpp)
        cna_register_backend_test(NAME EasyGL_ViewportResetAfterResize COMMAND cna_test_easygl_viewport_reset_after_resize
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_depth_format
                        examples/easygl_depth_format_test.cpp)
        cna_register_backend_test(NAME EasyGL_DepthFormat COMMAND cna_test_easygl_depth_format
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_msaa_change
                        examples/easygl_msaa_change_test.cpp)
        cna_register_backend_test(NAME EasyGL_MsaaChange COMMAND cna_test_easygl_msaa_change
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_vertexbuffer_setdata
                        examples/easygl_vertexbuffer_setdata_test.cpp)
        cna_register_backend_test(NAME EasyGL_VbSetData COMMAND cna_test_easygl_vertexbuffer_setdata
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_dynamic_buffer_stress
                        examples/easygl_dynamic_buffer_stress_test.cpp)
        cna_register_backend_test(NAME EasyGL_DynamicBufferStress COMMAND cna_test_easygl_dynamic_buffer_stress
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_buffer_usage
                        examples/easygl_buffer_usage_test.cpp)
        cna_register_backend_test(NAME EasyGL_BufferUsage COMMAND cna_test_easygl_buffer_usage
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_disposed_buffer
                        examples/easygl_disposed_buffer_test.cpp)
        cna_register_backend_test(NAME EasyGL_DisposedBuffer COMMAND cna_test_easygl_disposed_buffer
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_vertexbuffer_indexbuffer_getdata
                        examples/easygl_vertexbuffer_indexbuffer_getdata_test.cpp)
        cna_register_backend_test(NAME EasyGL_VertexBufferIndexBufferGetData COMMAND cna_test_easygl_vertexbuffer_indexbuffer_getdata
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_effect_clone
                        examples/easygl_effect_clone_test.cpp)
        cna_register_backend_test(NAME EasyGL_EffectClone COMMAND cna_test_easygl_effect_clone
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_effect_current_technique
                        examples/easygl_effect_current_technique_test.cpp)
        cna_register_backend_test(NAME EasyGL_EffectCurrentTechnique COMMAND cna_test_easygl_effect_current_technique
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_basiceffect_combinations
                        examples/easygl_basiceffect_combinations_test.cpp)
        cna_register_backend_test(NAME EasyGL_BasicEffectCombinations COMMAND cna_test_easygl_basiceffect_combinations
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 364: BasicEffect pixel test — VertexColorEnabled=false, no texture, diffuse color only
        cna_easygl_test(cna_test_easygl_basiceffect_vertexcolor_disabled
                        examples/easygl_basiceffect_vertexcolor_disabled_test.cpp)
        cna_register_backend_test(NAME EasyGL_BasicEffect_VertexColorDisabled COMMAND cna_test_easygl_basiceffect_vertexcolor_disabled
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 365: BasicEffect pixel test — VertexColorEnabled=true, no texture, vertex color multiplication
        cna_easygl_test(cna_test_easygl_basiceffect_vertexcolor_enabled
                        examples/easygl_basiceffect_vertexcolor_enabled_test.cpp)
        cna_register_backend_test(NAME EasyGL_BasicEffect_VertexColorEnabled COMMAND cna_test_easygl_basiceffect_vertexcolor_enabled
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 366: BasicEffect pixel test — TextureEnabled=true, no vertex color
        cna_easygl_test(cna_test_easygl_basiceffect_texture_enabled
                        examples/easygl_basiceffect_texture_enabled_test.cpp)
        cna_register_backend_test(NAME EasyGL_BasicEffect_TextureEnabled COMMAND cna_test_easygl_basiceffect_texture_enabled
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 367: BasicEffect pixel test — TextureEnabled=true AND VertexColorEnabled=true
        cna_easygl_test(cna_test_easygl_basiceffect_texture_vertexcolor_enabled
                        examples/easygl_basiceffect_texture_vertexcolor_enabled_test.cpp)
        cna_register_backend_test(NAME EasyGL_BasicEffect_TextureVertexColorEnabled COMMAND cna_test_easygl_basiceffect_texture_vertexcolor_enabled
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 368: BasicEffect pixel test — LightingEnabled=true, one DirectionalLight
        cna_easygl_test(cna_test_easygl_basiceffect_one_light
                        examples/easygl_basiceffect_one_light_test.cpp)
        cna_register_backend_test(NAME EasyGL_BasicEffect_OneLight COMMAND cna_test_easygl_basiceffect_one_light
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 369: BasicEffect pixel test — DiffuseColor+EmissiveColor, LightingEnabled=false
        cna_easygl_test(cna_test_easygl_basiceffect_emissive
                        examples/easygl_basiceffect_emissive_test.cpp)
        cna_register_backend_test(NAME EasyGL_BasicEffect_Emissive COMMAND cna_test_easygl_basiceffect_emissive
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 370: BasicEffect cross-backend image comparison suite — closes Phase 42
        cna_easygl_test(cna_test_easygl_basiceffect_combined
                        examples/easygl_basiceffect_combined_test.cpp)
        cna_register_backend_test(NAME EasyGL_BasicEffect_Combined COMMAND cna_test_easygl_basiceffect_combined
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_alphatest_modes
                        examples/easygl_alphatest_modes_test.cpp)
        cna_register_backend_test(NAME EasyGL_AlphaTestModes COMMAND cna_test_easygl_alphatest_modes
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 373: AlphaTestEffect all CompareFunction modes — threshold sweep (below/at/above)
        cna_easygl_test(cna_test_easygl_alphatest_comparefunction_sweep
                        examples/easygl_alphatest_comparefunction_sweep_test.cpp)
        cna_register_backend_test(NAME EasyGL_AlphaTest_CompareFunctionSweep COMMAND cna_test_easygl_alphatest_comparefunction_sweep
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 377: AlphaTestEffect vertex color x diffuse color interaction
        cna_easygl_test(cna_test_easygl_alphatest_vertexcolor_diffuse
                        examples/easygl_alphatest_vertexcolor_diffuse_test.cpp)
        cna_register_backend_test(NAME EasyGL_AlphaTest_VertexColorDiffuse COMMAND cna_test_easygl_alphatest_vertexcolor_diffuse
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 378: AlphaTestEffect fog behavior
        cna_easygl_test(cna_test_easygl_alphatest_fog
                        examples/easygl_alphatest_fog_test.cpp)
        cna_register_backend_test(NAME EasyGL_AlphaTest_Fog COMMAND cna_test_easygl_alphatest_fog
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 379: AlphaTestEffect null/no-texture behavior
        cna_easygl_test(cna_test_easygl_alphatest_null_texture
                        examples/easygl_alphatest_null_texture_test.cpp)
        cna_register_backend_test(NAME EasyGL_AlphaTest_NullTexture COMMAND cna_test_easygl_alphatest_null_texture
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 383: DualTextureEffect two-texture blend formula, including FNA's color.rgb*=2
        cna_easygl_test(cna_test_easygl_dualtextureeffect_doubling
                        examples/easygl_dualtextureeffect_doubling_test.cpp)
        cna_register_backend_test(NAME EasyGL_DualTextureEffect_Doubling COMMAND cna_test_easygl_dualtextureeffect_doubling
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 385: DualTextureEffect Alpha premultiplication, verified via real AlphaBlend
        cna_easygl_test(cna_test_easygl_dualtextureeffect_alpha
                        examples/easygl_dualtextureeffect_alpha_test.cpp)
        cna_register_backend_test(NAME EasyGL_DualTextureEffect_Alpha COMMAND cna_test_easygl_dualtextureeffect_alpha
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 386: DualTextureEffect first texture (Texture, slot 0) null behavior
        cna_easygl_test(cna_test_easygl_dualtextureeffect_null_texture0
                        examples/easygl_dualtextureeffect_null_texture0_test.cpp)
        cna_register_backend_test(NAME EasyGL_DualTextureEffect_NullTexture0 COMMAND cna_test_easygl_dualtextureeffect_null_texture0
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 387: DualTextureEffect second texture (Texture2, slot 1) null behavior
        cna_easygl_test(cna_test_easygl_dualtextureeffect_null_texture2
                        examples/easygl_dualtextureeffect_null_texture2_test.cpp)
        cna_register_backend_test(NAME EasyGL_DualTextureEffect_NullTexture2 COMMAND cna_test_easygl_dualtextureeffect_null_texture2
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 388: DualTextureEffect fog behavior
        cna_easygl_test(cna_test_easygl_dualtextureeffect_fog
                        examples/easygl_dualtextureeffect_fog_test.cpp)
        cna_register_backend_test(NAME EasyGL_DualTextureEffect_Fog COMMAND cna_test_easygl_dualtextureeffect_fog
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 389: DualTextureEffect cross-backend image comparison suite (closes Phase 44 pixel work)
        cna_easygl_test(cna_test_easygl_dualtextureeffect_combined
                        examples/easygl_dualtextureeffect_combined_test.cpp)
        cna_register_backend_test(NAME EasyGL_DualTextureEffect_Combined COMMAND cna_test_easygl_dualtextureeffect_combined
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_dualtexture
                        examples/easygl_dualtexture_test.cpp)
        cna_register_backend_test(NAME EasyGL_DualTexture COMMAND cna_test_easygl_dualtexture
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_skinned_effect_bones
                        examples/easygl_skinned_effect_bones_test.cpp)
        cna_register_backend_test(NAME EasyGL_SkinnedBones COMMAND cna_test_easygl_skinned_effect_bones
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_basiceffect_default_lighting
                        examples/easygl_basiceffect_default_lighting_test.cpp)
        cna_register_backend_test(NAME EasyGL_BasicEffectDefaultLighting COMMAND cna_test_easygl_basiceffect_default_lighting
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_basiceffect_fog
                        examples/easygl_basiceffect_fog_test.cpp)
        cna_register_backend_test(NAME EasyGL_BasicEffectFog COMMAND cna_test_easygl_basiceffect_fog
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 247: draw quad with each supported VertexElementFormat via VertexBuffer
        cna_easygl_test(cna_test_easygl_vertex_formats
                        examples/easygl_vertex_formats_test.cpp)
        cna_register_backend_test(NAME EasyGL_VertexFormats_AllStrides COMMAND cna_test_easygl_vertex_formats
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 255: DrawUserPrimitives<VertexPositionColor> pixel-readback
        cna_easygl_test(cna_test_easygl_draw_user_primitives_vpc
                        examples/easygl_draw_user_primitives_vpc_test.cpp)
        cna_register_backend_test(NAME EasyGL_DrawUserPrimitives_VPC COMMAND cna_test_easygl_draw_user_primitives_vpc
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 256: DrawUserPrimitives with explicit VertexDeclaration pixel-readback
        cna_easygl_test(cna_test_easygl_draw_user_primitives_custom
                        examples/easygl_draw_user_primitives_custom_test.cpp)
        cna_register_backend_test(NAME EasyGL_DrawUserPrimitives_CustomVD COMMAND cna_test_easygl_draw_user_primitives_custom
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 257: DrawUserIndexedPrimitives<VertexPositionColor> (16-bit indices) pixel-readback
        cna_easygl_test(cna_test_easygl_draw_user_indexed_primitives_vpc
                        examples/easygl_draw_user_indexed_primitives_vpc_test.cpp)
        cna_register_backend_test(NAME EasyGL_DrawUserIndexedPrimitives_VPC COMMAND cna_test_easygl_draw_user_indexed_primitives_vpc
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 258: DrawUserIndexedPrimitives<VertexPositionColor> (32-bit indices) pixel-readback
        cna_easygl_test(cna_test_easygl_draw_user_indexed_primitives_32
                        examples/easygl_draw_user_indexed_primitives_32_test.cpp)
        cna_register_backend_test(NAME EasyGL_DrawUserIndexedPrimitives_32 COMMAND cna_test_easygl_draw_user_indexed_primitives_32
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 268: non-power-of-two (3x5) texture upload + GPU-sampled pixel-readback
        cna_easygl_test(cna_test_easygl_npot_texture
                        examples/easygl_npot_texture_test.cpp)
        cna_register_backend_test(NAME EasyGL_NpotTexture COMMAND cna_test_easygl_npot_texture
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 269: SpriteBatch TextureAddressMode (Wrap vs Clamp) edge-sampling pixel-readback
        cna_easygl_test(cna_test_easygl_texture_address_mode
                        examples/easygl_texture_address_mode_test.cpp)
        cna_register_backend_test(NAME EasyGL_TextureAddressMode COMMAND cna_test_easygl_texture_address_mode
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 737: SpriteBatch TextureAddressMode::Mirror edge-sampling pixel-readback -- Task 269
        # only independently verified Wrap/Clamp; Mirror is fixed by the same code path but was
        # never verified on its own until now.
        cna_easygl_test(cna_test_easygl_texture_address_mode_mirror
                        examples/easygl_texture_address_mode_mirror_test.cpp)
        cna_register_backend_test(NAME EasyGL_TextureAddressMode_Mirror COMMAND cna_test_easygl_texture_address_mode_mirror
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 424: Pixel test -- draw a single SpriteFont glyph at a known position on EasyGL.
        cna_easygl_test(cna_test_easygl_spritefont_single_glyph
                        examples/easygl_spritefont_single_glyph_test.cpp)
        cna_register_backend_test(NAME EasyGL_SpriteFont_SingleGlyph COMMAND cna_test_easygl_spritefont_single_glyph
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 425: Pixel test -- draw multiple SpriteFont glyphs with spacing on EasyGL.
        cna_easygl_test(cna_test_easygl_spritefont_multiglyph_spacing
                        examples/easygl_spritefont_multiglyph_spacing_test.cpp)
        cna_register_backend_test(NAME EasyGL_SpriteFont_MultiGlyphSpacing COMMAND cna_test_easygl_spritefont_multiglyph_spacing
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 426: Pixel test -- SpriteFont newline advances by line spacing on EasyGL.
        cna_easygl_test(cna_test_easygl_spritefont_newline
                        examples/easygl_spritefont_newline_test.cpp)
        cna_register_backend_test(NAME EasyGL_SpriteFont_Newline COMMAND cna_test_easygl_spritefont_newline
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 427: Pixel test -- SpriteFont default character fallback renders the expected
        # glyph on EasyGL.
        cna_easygl_test(cna_test_easygl_spritefont_default_char
                        examples/easygl_spritefont_default_char_test.cpp)
        cna_register_backend_test(NAME EasyGL_SpriteFont_DefaultChar COMMAND cna_test_easygl_spritefont_default_char
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 428: Verify SpriteEffects (flip) with DrawString on EasyGL.
        cna_easygl_test(cna_test_easygl_spritefont_effects_flip
                        examples/easygl_spritefont_effects_flip_test.cpp)
        cna_register_backend_test(NAME EasyGL_SpriteFont_EffectsFlip COMMAND cna_test_easygl_spritefont_effects_flip
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 429: Verify rotation/origin/scale with DrawString on EasyGL.
        cna_easygl_test(cna_test_easygl_spritefont_effects_rotation_scale
                        examples/easygl_spritefont_effects_rotation_scale_test.cpp)
        cna_register_backend_test(NAME EasyGL_SpriteFont_EffectsRotationScale COMMAND cna_test_easygl_spritefont_effects_rotation_scale
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 293: DualTextureEffect (3D stock effect) honors GraphicsDevice.SamplerStates[0]
        cna_easygl_test(cna_test_easygl_sampler_state_effect
                        examples/easygl_sampler_state_effect_test.cpp)
        cna_register_backend_test(NAME EasyGL_SamplerState_DualTextureEffect COMMAND cna_test_easygl_sampler_state_effect
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 294: TextureAddressMode::Clamp on a 3D stock effect (DualTextureEffect)
        cna_easygl_test(cna_test_easygl_texture_address_mode_clamp_effect
                        examples/easygl_texture_address_mode_clamp_effect_test.cpp)
        cna_register_backend_test(NAME EasyGL_TextureAddressMode_Clamp_DualTextureEffect COMMAND cna_test_easygl_texture_address_mode_clamp_effect
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 296: TextureAddressMode::Mirror on a 3D stock effect (DualTextureEffect)
        cna_easygl_test(cna_test_easygl_texture_address_mode_mirror_effect
                        examples/easygl_texture_address_mode_mirror_effect_test.cpp)
        cna_register_backend_test(NAME EasyGL_TextureAddressMode_Mirror_DualTextureEffect COMMAND cna_test_easygl_texture_address_mode_mirror_effect
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 297: TextureFilter::Point vs Linear, magnification and minification
        cna_easygl_test(cna_test_easygl_texture_filter_point_vs_linear
                        examples/easygl_texture_filter_point_vs_linear_test.cpp)
        cna_register_backend_test(NAME EasyGL_TextureFilter_PointVsLinear COMMAND cna_test_easygl_texture_filter_point_vs_linear
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 298: mipmap filter behavior (LinearMipPoint selects the correct mip; Point does not)
        cna_easygl_test(cna_test_easygl_texture_mip_filter_effect
                        examples/easygl_texture_mip_filter_effect_test.cpp)
        cna_register_backend_test(NAME EasyGL_TextureMipFilter_DualTextureEffect COMMAND cna_test_easygl_texture_mip_filter_effect
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 299: anisotropic filtering caps and fallback (extreme MaxAnisotropy must not crash)
        cna_easygl_test(cna_test_easygl_texture_anisotropic_effect
                        examples/easygl_texture_anisotropic_effect_test.cpp)
        cna_register_backend_test(NAME EasyGL_TextureAnisotropic_DualTextureEffect COMMAND cna_test_easygl_texture_anisotropic_effect
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 924 (split from Task 867): a single-level Texture2D sampled with a mipmap-requiring
        # TextureFilter (Anisotropic) must not render solid black (GL_TEXTURE_MAX_LEVEL now
        # clamped to the real level count instead of GL's own default of 1000).
        cna_easygl_test(cna_test_easygl_texture2d_anisotropic_singlelevel
                        examples/easygl_texture2d_anisotropic_singlelevel_test.cpp)
        cna_register_backend_test(NAME EasyGL_Texture2D_AnisotropicSingleLevel COMMAND cna_test_easygl_texture2d_anisotropic_singlelevel
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 918: real GL_EXT_texture_filter_anisotropic wiring, verified directly against
        # easy-gl's own Sampler API rather than trusting the capability-dump log alone.
        cna_easygl_test(cna_test_easygl_anisotropic_gl_state
                        examples/easygl_anisotropic_gl_state_test.cpp)
        target_link_libraries(cna_test_easygl_anisotropic_gl_state PRIVATE easy-gl)
        cna_register_backend_test(NAME EasyGL_Anisotropic_GlState COMMAND cna_test_easygl_anisotropic_gl_state
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 303: BlendState::Opaque discards destination + source alpha entirely
        cna_easygl_test(cna_test_easygl_blendstate_opaque
                        examples/easygl_blendstate_opaque_test.cpp)
        cna_register_backend_test(NAME EasyGL_BlendState_Opaque COMMAND cna_test_easygl_blendstate_opaque
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 304: BlendState::AlphaBlend expects premultiplied source colour
        cna_easygl_test(cna_test_easygl_blendstate_alphablend
                        examples/easygl_blendstate_alphablend_test.cpp)
        cna_register_backend_test(NAME EasyGL_BlendState_AlphaBlend COMMAND cna_test_easygl_blendstate_alphablend
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 305: BlendState::NonPremultiplied expects a raw (non-premultiplied) source colour
        cna_easygl_test(cna_test_easygl_blendstate_nonpremultiplied
                        examples/easygl_blendstate_nonpremultiplied_test.cpp)
        cna_register_backend_test(NAME EasyGL_BlendState_NonPremultiplied COMMAND cna_test_easygl_blendstate_nonpremultiplied
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 306: BlendState::Additive saturation + full-destination-add behaviour
        cna_easygl_test(cna_test_easygl_blendstate_additive
                        examples/easygl_blendstate_additive_test.cpp)
        cna_register_backend_test(NAME EasyGL_BlendState_Additive COMMAND cna_test_easygl_blendstate_additive
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 307: ColorBlendFunction/AlphaBlendFunction operate independently
        cna_easygl_test(cna_test_easygl_blendstate_separate_functions
                        examples/easygl_blendstate_separate_functions_test.cpp)
        cna_register_backend_test(NAME EasyGL_BlendState_SeparateFunctions COMMAND cna_test_easygl_blendstate_separate_functions
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 308: ColorSourceBlend/ColorDestinationBlend operate independently from Alpha*Blend
        cna_easygl_test(cna_test_easygl_blendstate_separate_factors
                        examples/easygl_blendstate_separate_factors_test.cpp)
        cna_register_backend_test(NAME EasyGL_BlendState_SeparateFactors COMMAND cna_test_easygl_blendstate_separate_factors
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 309: BlendState.BlendFactor propagates via GraphicsDevice.setBlendStateProperty alone
        cna_easygl_test(cna_test_easygl_blendstate_blendfactor
                        examples/easygl_blendstate_blendfactor_test.cpp)
        cna_register_backend_test(NAME EasyGL_BlendState_BlendFactor COMMAND cna_test_easygl_blendstate_blendfactor
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 313: DepthStencilState.DepthBufferWriteEnable actually gates depth-buffer writes
        cna_easygl_test(cna_test_easygl_depthstencilstate_write_enable
                        examples/easygl_depthstencilstate_write_enable_test.cpp)
        cna_register_backend_test(NAME EasyGL_DepthStencilState_WriteEnable COMMAND cna_test_easygl_depthstencilstate_write_enable
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 314: DepthStencilState.DepthBufferFunction (Always/Never/Less/LessEqual/Greater)
        cna_easygl_test(cna_test_easygl_depthstencilstate_compare_function
                        examples/easygl_depthstencilstate_compare_function_test.cpp)
        cna_register_backend_test(NAME EasyGL_DepthStencilState_CompareFunction COMMAND cna_test_easygl_depthstencilstate_compare_function
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 315: DepthStencilState.StencilEnable actually gates the stencil test
        cna_easygl_test(cna_test_easygl_depthstencilstate_stencil_enable
                        examples/easygl_depthstencilstate_stencil_enable_test.cpp)
        cna_register_backend_test(NAME EasyGL_DepthStencilState_StencilEnable COMMAND cna_test_easygl_depthstencilstate_stencil_enable
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 316: DepthStencilState.StencilMask / StencilWriteMask
        cna_easygl_test(cna_test_easygl_depthstencilstate_stencil_mask
                        examples/easygl_depthstencilstate_stencil_mask_test.cpp)
        cna_register_backend_test(NAME EasyGL_DepthStencilState_StencilMask COMMAND cna_test_easygl_depthstencilstate_stencil_mask
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 317: DepthStencilState front-face stencil operations (StencilFail/
        # StencilDepthBufferFail/StencilPass)
        cna_easygl_test(cna_test_easygl_depthstencilstate_stencil_ops
                        examples/easygl_depthstencilstate_stencil_ops_test.cpp)
        cna_register_backend_test(NAME EasyGL_DepthStencilState_StencilOps COMMAND cna_test_easygl_depthstencilstate_stencil_ops
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 318: DepthStencilState.TwoSidedStencilMode / back-face (CCW) stencil ops
        cna_easygl_test(cna_test_easygl_depthstencilstate_stencil_twosided
                        examples/easygl_depthstencilstate_stencil_twosided_test.cpp)
        cna_register_backend_test(NAME EasyGL_DepthStencilState_StencilTwoSided COMMAND cna_test_easygl_depthstencilstate_stencil_twosided
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 319: GraphicsDevice.ReferenceStencil independent-override behavior (Task 872 -
        # confirmed a universal, not-Vulkan-specific gap; registered as a documented known failure)
        cna_easygl_test(cna_test_easygl_graphicsdevice_reference_stencil
                        examples/easygl_graphicsdevice_reference_stencil_test.cpp)
        cna_register_backend_test(NAME EasyGL_GraphicsDevice_ReferenceStencil COMMAND cna_test_easygl_graphicsdevice_reference_stencil
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 871: GraphicsDevice::Clear() now actually clears the stencil buffer to the
        # requested value on ClearOptions combinations that include ClearOptions::Stencil.
        cna_easygl_test(cna_test_easygl_graphicsdevice_clear_stencil
                        examples/easygl_graphicsdevice_clear_stencil_test.cpp)
        cna_register_backend_test(NAME EasyGL_GraphicsDevice_ClearStencil COMMAND cna_test_easygl_graphicsdevice_clear_stencil
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 950: GraphicsDevice::Clear()'s depth parameter -- EasyGL already forwarded it
        # correctly (unlike Vulkan/Bgfx); registered here too as a regression guard.
        cna_easygl_test(cna_test_easygl_graphicsdevice_clear_depth
                        examples/graphicsdevice_clear_depth_test.cpp)
        cna_register_backend_test(NAME EasyGL_GraphicsDevice_ClearDepth COMMAND cna_test_easygl_graphicsdevice_clear_depth
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 955: a freshly-constructed GraphicsDevice's own default state (no game code ever
        # explicitly setting BlendState/DepthStencilState/RasterizerState, exactly like
        # ../cna-samples SimpleAnimation's Tank.hpp) must produce correct opaque depth occlusion.
        cna_easygl_test(cna_test_easygl_graphicsdevice_default_state_occlusion
                        examples/graphicsdevice_default_state_occlusion_test.cpp)
        cna_register_backend_test(NAME EasyGL_GraphicsDevice_DefaultStateOcclusion COMMAND cna_test_easygl_graphicsdevice_default_state_occlusion
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 889: DualTextureEffect.VertexColorEnabled -- verbatim reuse of the shared
        # backend-agnostic source, registered on all 3 backends.
        cna_easygl_test(cna_test_easygl_dualtextureeffect_vertexcolor
                        examples/dualtextureeffect_vertexcolor_test.cpp)
        cna_register_backend_test(NAME EasyGL_DualTextureEffect_VertexColor COMMAND cna_test_easygl_dualtextureeffect_vertexcolor
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 323 (+ 324/325 coverage): RasterizerState.CullMode — None/CullClockwiseFace/
        # CullCounterClockwiseFace, contrast-checked with front- and back-winding quads
        cna_easygl_test(cna_test_easygl_rasterizerstate_cullmode
                        examples/easygl_rasterizerstate_cullmode_test.cpp)
        cna_register_backend_test(NAME EasyGL_RasterizerState_CullMode COMMAND cna_test_easygl_rasterizerstate_cullmode
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # XNA culling-compatibility audit (docs/xna_culling_compatibility_audit.md): extends the
        # identity-only CullMode test above with a real Matrix::CreateLookAt camera and
        # perspective/orthographic projection -- verbatim reuse of the shared backend-agnostic
        # source, registered on all 3 backends.
        cna_easygl_test(cna_test_easygl_rasterizerstate_cullmode_camera
                        examples/rasterizerstate_cullmode_camera_test.cpp)
        cna_register_backend_test(NAME EasyGL_RasterizerState_CullMode_Camera COMMAND cna_test_easygl_rasterizerstate_cullmode_camera
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # XNA culling-compatibility audit: same real-camera reproducer, but through
        # GraphicsDevice::DrawIndexedPrimitives with a real VertexBuffer/IndexBuffer/BasicEffect
        # -- the exact code path Model/ModelMesh::Draw() uses -- instead of DrawUserPrimitives'
        # separate DrawColoredPrimitives dispatch. Verbatim reuse of the shared source.
        cna_easygl_test(cna_test_easygl_rasterizerstate_cullmode_indexed_basiceffect
                        examples/rasterizerstate_cullmode_indexed_basiceffect_test.cpp)
        cna_register_backend_test(NAME EasyGL_RasterizerState_CullMode_IndexedBasicEffect COMMAND cna_test_easygl_rasterizerstate_cullmode_indexed_basiceffect
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 326: FillMode::Solid baseline on EasyGL (reuses Task 327's backend-agnostic Vulkan
        # source — its Solid sub-tests 1/3 previously only ran on Vulkan).
        cna_easygl_test(cna_test_easygl_fill_mode
                        examples/vulkan_fill_mode_test.cpp)
        cna_register_backend_test(NAME EasyGL_FillMode_Solid COMMAND cna_test_easygl_fill_mode
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 767: depth bias / slope-scale depth bias via real glPolygonOffset.
        cna_easygl_test(cna_test_easygl_depth_bias
                        examples/easygl_depth_bias_test.cpp)
        cna_register_backend_test(NAME EasyGL_DepthBias COMMAND cna_test_easygl_depth_bias
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 331: RenderTarget2D constructor/property audit against FNA.
        cna_easygl_test(cna_test_easygl_rendertarget2d_properties
                        examples/easygl_rendertarget2d_properties_test.cpp)
        cna_register_backend_test(NAME EasyGL_RenderTarget2D_Properties COMMAND cna_test_easygl_rendertarget2d_properties
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 332: RenderTargetCube constructor/property audit against FNA.
        cna_easygl_test(cna_test_easygl_rendertargetcube_properties
                        examples/easygl_rendertargetcube_properties_test.cpp)
        cna_register_backend_test(NAME EasyGL_RenderTargetCube_Properties COMMAND cna_test_easygl_rendertargetcube_properties
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 498: build and run at least 5 small FNA/XNA sample ports on CNA as real
        # compatibility proof -- EasyGL is the primary/most-mature backend, so most of this
        # task's own new samples target it, with 3 genuinely 3D (Task 730 already proved 2D
        # compatibility via SDL_Renderer's own 5 samples).
        # Sample 1/5: reuse cna_demo_2d, closing the exact same kind of gap Task 730 closed for
        # SDL_Renderer (already smoke-tested on Vulkan/Bgfx via Tasks 88/89, never on EasyGL).
        cna_register_backend_test(NAME EasyGL_Demo2D_SmokeTest COMMAND cna_demo_2d --smoke 3
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}" LABELS "GraphicsSmoke")

        # Sample 2/5: moving 3D object -- Update()-driven World-space translation, BasicEffect.
        cna_easygl_test(cna_test_easygl_sample_moving_quad3d
                        examples/easygl_sample_moving_quad3d_test.cpp)
        cna_register_backend_test(NAME EasyGL_Sample_MovingQuad3D COMMAND cna_test_easygl_sample_moving_quad3d
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Sample 3/5: reactive material swap -- Update()-driven DualTextureEffect.Texture2 swap.
        cna_easygl_test(cna_test_easygl_sample_dualtexture_swap
                        examples/easygl_sample_dualtexture_swap_test.cpp)
        cna_register_backend_test(NAME EasyGL_Sample_DualTextureSwap COMMAND cna_test_easygl_sample_dualtexture_swap
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Sample 4/5: keyboard-driven 3D object -- the 3D counterpart to Task 730's keyboard
        # sprite sample, using the same InputManager::SetKeyState injection seam.
        cna_easygl_test(cna_test_easygl_sample_keyboard_cube3d
                        examples/easygl_sample_keyboard_cube3d_test.cpp)
        cna_register_backend_test(NAME EasyGL_Sample_KeyboardCube3D COMMAND cna_test_easygl_sample_keyboard_cube3d
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Sample 5/5: layered UI fade -- Update()-driven SpriteBatch alpha-blend compositing.
        cna_easygl_test(cna_test_easygl_sample_layered_blend
                        examples/easygl_sample_layered_blend_test.cpp)
        cna_register_backend_test(NAME EasyGL_Sample_LayeredBlend COMMAND cna_test_easygl_sample_layered_blend
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 956: SpriteBatch's blend state must not leak a hardcoded value into a following 3D draw.
        cna_easygl_test(cna_test_easygl_spritebatch_blendstate_leak
                        examples/easygl_spritebatch_blendstate_leak_test.cpp)
        cna_register_backend_test(NAME EasyGL_SpriteBatch_BlendStateLeak COMMAND cna_test_easygl_spritebatch_blendstate_leak
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # GraphicsDeviceManager.SynchronizeWithVerticalRetrace/ApplyChanges() must actually reach
        # IGraphicsBackend::SetSwapInterval() -- backend-independent fix in GraphicsDevice::Reset().
        cna_easygl_test(cna_test_easygl_graphicsdevicemanager_vsync
                        examples/easygl_graphicsdevicemanager_vsync_test.cpp)
        cna_register_backend_test(NAME EasyGL_GraphicsDeviceManager_Vsync COMMAND cna_test_easygl_graphicsdevicemanager_vsync
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-006: SkinnedEffect world-space normal transform under non-identity World
        # (rotation, non-uniform scale, and both) for BOTH skinned programs (per-pixel-lit
        # EnsureSkinnedProgram and per-vertex-lit EnsureSkinnedVertexLitProgram) -- the cases every
        # existing skinned test, all of which use World = Identity, structurally cannot detect.
        cna_easygl_test(cna_test_easygl_skinnedeffect_world_normal
                        examples/easygl_skinnedeffect_world_normal_test.cpp)
        cna_register_backend_test(NAME EasyGL_SkinnedEffect_WorldNormal COMMAND cna_test_easygl_skinnedeffect_world_normal
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    endif()
