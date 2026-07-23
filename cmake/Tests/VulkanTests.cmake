# Historically nested inside the house3d_demo/net-demos block below (both only build for the
# EASYGL/VULKAN backends), so Vulkan's test suite has always additionally required
# CNA_BUILD_EXAMPLES=ON, unlike every other backend's test block. Preserved verbatim here as its
# own self-contained condition rather than silently dropped during the file split.
if(CNA_BUILD_EXAMPLES AND CNA_BUILD_TESTS AND NOT EMSCRIPTEN AND NOT WIN32
   AND CNA_GRAPHICS_BACKEND STREQUAL "VULKAN")

        # --- helper macro: build a headless Vulkan test exe ----------------------
        macro(cna_vulkan_test target src)
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

        # Task 88: 3-frame smoke test for the Vulkan backend
        cna_register_backend_test(NAME Vulkan_Demo2D_SmokeTest COMMAND cna_demo_2d --smoke 3
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}" LABELS "GraphicsSmoke")

        # REMED-GFX-011: orientation oracle. Pins "XNA +Y is up" and "readback row 0 is top"
        # against colored3d, a family whose Y-flip is already correct, so that the per-family
        # orientation tests below assert a calibrated convention rather than an assumed one.
        cna_vulkan_test(cna_test_vulkan_orientation_calibration
                        examples/vulkan_orientation_calibration_test.cpp)
        cna_register_backend_test(NAME Vulkan_Orientation_Calibration COMMAND cna_test_vulkan_orientation_calibration
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-011: per-family orientation regression for env_map3d / pbr3d /
        # pbr3d_skinned / instanced3d — asymmetric quadrant geometry, four-corner sampling.
        cna_vulkan_test(cna_test_vulkan_orientation_effects
                        examples/vulkan_orientation_effects_test.cpp)
        cna_register_backend_test(NAME Vulkan_Orientation_Effects COMMAND cna_test_vulkan_orientation_effects
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-011: same four families rendered into a RenderTarget2D, calibrated against
        # colored3d — proves the RT path is not double-flipped relative to the backbuffer path.
        cna_vulkan_test(cna_test_vulkan_orientation_rendertarget
                        examples/vulkan_orientation_rendertarget_test.cpp)
        cna_register_backend_test(NAME Vulkan_Orientation_RenderTarget COMMAND cna_test_vulkan_orientation_rendertarget
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-006: SkinnedEffect world-space normal transform under non-identity World
        # (rotation, non-uniform scale, and both) — the cases every existing skinned test,
        # all of which use World = Identity, structurally cannot detect.
        cna_vulkan_test(cna_test_vulkan_skinnedeffect_world_normal
                        examples/vulkan_skinnedeffect_world_normal_test.cpp)
        cna_register_backend_test(NAME Vulkan_SkinnedEffect_WorldNormal COMMAND cna_test_vulkan_skinnedeffect_world_normal
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 124: DrawInstancedPrimitives — 3 instances at different X positions
        cna_vulkan_test(cna_test_vulkan_instanced
                        examples/vulkan_instanced_test.cpp)
        cna_register_backend_test(NAME Vulkan_DrawInstanced_3Instances COMMAND cna_test_vulkan_instanced
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 119: ShaderEffect with SPIR-V — SpriteBatch tint via custom push constants
        cna_vulkan_test(cna_test_vulkan_shader_effect
                        examples/vulkan_shader_effect_test.cpp)
        cna_register_backend_test(NAME Vulkan_ShaderEffect_SpirV COMMAND cna_test_vulkan_shader_effect
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 284: Texture2D Color format mapping — linear vs. sRGB decode on Vulkan
        cna_vulkan_test(cna_test_vulkan_texture_srgb
                        examples/vulkan_texture_srgb_test.cpp)
        cna_register_backend_test(NAME Vulkan_Texture2D_ColorFormat_Linear COMMAND cna_test_vulkan_texture_srgb
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 135: DualTextureEffect — magenta×yellow→red pixel readback on Vulkan
        cna_vulkan_test(cna_test_vulkan_dual_texture
                        examples/vulkan_dual_texture_test.cpp)
        cna_register_backend_test(NAME Vulkan_DualTextureEffect_Blend COMMAND cna_test_vulkan_dual_texture
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 293: DualTextureEffect (3D stock effect) honors GraphicsDevice.SamplerStates[0] on
        # Vulkan too — reuses the backend-agnostic EasyGL test source (public XNA API only).
        cna_vulkan_test(cna_test_vulkan_sampler_state_effect
                        examples/easygl_sampler_state_effect_test.cpp)
        cna_register_backend_test(NAME Vulkan_SamplerState_DualTextureEffect COMMAND cna_test_vulkan_sampler_state_effect
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 294: TextureAddressMode::Clamp on a 3D stock effect on Vulkan too (reuses the
        # backend-agnostic EasyGL test source).
        cna_vulkan_test(cna_test_vulkan_texture_address_mode_clamp_effect
                        examples/easygl_texture_address_mode_clamp_effect_test.cpp)
        cna_register_backend_test(NAME Vulkan_TextureAddressMode_Clamp_DualTextureEffect COMMAND cna_test_vulkan_texture_address_mode_clamp_effect
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 296: TextureAddressMode::Mirror on a 3D stock effect on Vulkan too (reuses the
        # backend-agnostic EasyGL test source).
        cna_vulkan_test(cna_test_vulkan_texture_address_mode_mirror_effect
                        examples/easygl_texture_address_mode_mirror_effect_test.cpp)
        cna_register_backend_test(NAME Vulkan_TextureAddressMode_Mirror_DualTextureEffect COMMAND cna_test_vulkan_texture_address_mode_mirror_effect
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 297: TextureFilter::Point vs Linear on Vulkan too (reuses the backend-agnostic
        # EasyGL test source).
        cna_vulkan_test(cna_test_vulkan_texture_filter_point_vs_linear
                        examples/easygl_texture_filter_point_vs_linear_test.cpp)
        cna_register_backend_test(NAME Vulkan_TextureFilter_PointVsLinear COMMAND cna_test_vulkan_texture_filter_point_vs_linear
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 298: now registered on Vulkan too -- a Vulkan-specific adaptation of the EasyGL
        # source (can't reuse it verbatim: Vulkan's TextureFilter::Point mapping has always been
        # mip-aware, unlike EasyGL's still-flat mapping, so the expected result for that one check
        # differs -- see the test's own file-header comment). Task 925 fixed the confound noted
        # here previously (Texture2D::SetData(level>0) was a silent no-op on Vulkan,
        # UpdatePixelsLevel never overridden; mipLevels/levelCount were also both hardcoded to 1).
        cna_vulkan_test(cna_test_vulkan_texture_mip_filter_effect
                        examples/vulkan_texture_mip_filter_effect_test.cpp)
        cna_register_backend_test(NAME Vulkan_TextureMipFilter_DualTextureEffect COMMAND cna_test_vulkan_texture_mip_filter_effect
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 299: anisotropic filtering caps and fallback on Vulkan too (reuses the
        # backend-agnostic EasyGL test source). This is the one backend where MaxAnisotropy is
        # actually respected (queried/clamped to the real device cap).
        cna_vulkan_test(cna_test_vulkan_texture_anisotropic_effect
                        examples/easygl_texture_anisotropic_effect_test.cpp)
        cna_register_backend_test(NAME Vulkan_TextureAnisotropic_DualTextureEffect COMMAND cna_test_vulkan_texture_anisotropic_effect
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 303: BlendState::Opaque on Vulkan too (reuses the backend-agnostic EasyGL source)
        cna_vulkan_test(cna_test_vulkan_blendstate_opaque
                        examples/easygl_blendstate_opaque_test.cpp)
        cna_register_backend_test(NAME Vulkan_BlendState_Opaque COMMAND cna_test_vulkan_blendstate_opaque
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 304: BlendState::AlphaBlend on Vulkan too (reuses the backend-agnostic EasyGL source)
        cna_vulkan_test(cna_test_vulkan_blendstate_alphablend
                        examples/easygl_blendstate_alphablend_test.cpp)
        cna_register_backend_test(NAME Vulkan_BlendState_AlphaBlend COMMAND cna_test_vulkan_blendstate_alphablend
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 305: BlendState::NonPremultiplied on Vulkan too (reuses the backend-agnostic
        # EasyGL source). NOTE: a passing result here does NOT mean Task 868 is fixed — see the
        # test source's file-header comment for why.
        cna_vulkan_test(cna_test_vulkan_blendstate_nonpremultiplied
                        examples/easygl_blendstate_nonpremultiplied_test.cpp)
        cna_register_backend_test(NAME Vulkan_BlendState_NonPremultiplied COMMAND cna_test_vulkan_blendstate_nonpremultiplied
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 306: BlendState::Additive on Vulkan too (reuses the backend-agnostic EasyGL source).
        # NOTE: expected to genuinely re-expose Task 868 here, unlike Task 305's coincidental pass.
        cna_vulkan_test(cna_test_vulkan_blendstate_additive
                        examples/easygl_blendstate_additive_test.cpp)
        cna_register_backend_test(NAME Vulkan_BlendState_Additive COMMAND cna_test_vulkan_blendstate_additive
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 307: ColorBlendFunction/AlphaBlendFunction independence on Vulkan too (reuses the
        # backend-agnostic EasyGL source). NOTE: expect Check A (Subtract) to fail per Task 868 -
        # Vulkan always hardcodes VK_BLEND_OP_ADD.
        cna_vulkan_test(cna_test_vulkan_blendstate_separate_functions
                        examples/easygl_blendstate_separate_functions_test.cpp)
        cna_register_backend_test(NAME Vulkan_BlendState_SeparateFunctions COMMAND cna_test_vulkan_blendstate_separate_functions
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 308: ColorSourceBlend/ColorDestinationBlend independence on Vulkan too (reuses the
        # backend-agnostic EasyGL source). NOTE: expect Check A to coincidentally pass and Check B
        # to fail per Task 868 - Vulkan always hardcodes SourceAlpha/InverseSourceAlpha for colour.
        cna_vulkan_test(cna_test_vulkan_blendstate_separate_factors
                        examples/easygl_blendstate_separate_factors_test.cpp)
        cna_register_backend_test(NAME Vulkan_BlendState_SeparateFactors COMMAND cna_test_vulkan_blendstate_separate_factors
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 309: BlendState.BlendFactor propagation (reuses the backend-agnostic EasyGL source).
        # This DrawUserPrimitives (3D-path) test PASSES: Task 868's FillBlendAttachmentState +
        # per-draw draw.blendParams made the 3D pipeline honor Blend::BlendFactor
        # (VK_BLEND_FACTOR_CONSTANT_COLOR), and REMED-GFX-070 made the constant per-draw-correct.
        # (A prior note here claiming this was "expected to FAIL per Task 868" was stale — the 3D
        # path was fixed. The remaining limitation is SpriteBatch-only: the 2D sprite pipeline still
        # hardcodes alpha-blend and ignores the BlendState — tracked as REMED-GFX-071.)
        cna_vulkan_test(cna_test_vulkan_blendstate_blendfactor
                        examples/easygl_blendstate_blendfactor_test.cpp)
        cna_register_backend_test(NAME Vulkan_BlendState_BlendFactor COMMAND cna_test_vulkan_blendstate_blendfactor
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 313: DepthStencilState.DepthBufferWriteEnable on Vulkan (reuses the backend-agnostic
        # EasyGL source).
        cna_vulkan_test(cna_test_vulkan_depthstencilstate_write_enable
                        examples/easygl_depthstencilstate_write_enable_test.cpp)
        cna_register_backend_test(NAME Vulkan_DepthStencilState_WriteEnable COMMAND cna_test_vulkan_depthstencilstate_write_enable
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 314: DepthStencilState.DepthBufferFunction on Vulkan (reuses the backend-agnostic
        # EasyGL source). NOTE: expected to FAIL some/all checks per Task 870 - Vulkan hardcodes
        # depthCompareOp per pipeline, ignoring DepthBufferFunction entirely.
        cna_vulkan_test(cna_test_vulkan_depthstencilstate_compare_function
                        examples/easygl_depthstencilstate_compare_function_test.cpp)
        cna_register_backend_test(NAME Vulkan_DepthStencilState_CompareFunction COMMAND cna_test_vulkan_depthstencilstate_compare_function
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 315: DepthStencilState.StencilEnable on Vulkan (reuses the backend-agnostic EasyGL
        # source). NOTE: expected to FAIL Check A per Task 870 - stencil testing is completely
        # non-functional on Vulkan (stencilTestEnable never set), so no gating ever happens.
        cna_vulkan_test(cna_test_vulkan_depthstencilstate_stencil_enable
                        examples/easygl_depthstencilstate_stencil_enable_test.cpp)
        cna_register_backend_test(NAME Vulkan_DepthStencilState_StencilEnable COMMAND cna_test_vulkan_depthstencilstate_stencil_enable
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 316: DepthStencilState.StencilMask/StencilWriteMask on Vulkan (reuses the
        # backend-agnostic EasyGL source). NOTE: expected to partially fail per Task 870 - stencil
        # testing never gates on Vulkan, so 2 of 4 checks pass only by coincidence.
        cna_vulkan_test(cna_test_vulkan_depthstencilstate_stencil_mask
                        examples/easygl_depthstencilstate_stencil_mask_test.cpp)
        cna_register_backend_test(NAME Vulkan_DepthStencilState_StencilMask COMMAND cna_test_vulkan_depthstencilstate_stencil_mask
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 317: DepthStencilState front-face stencil operations on Vulkan (reuses the
        # backend-agnostic EasyGL source). NOTE: expected to FAIL all 3 checks per Task 870 -
        # stencil testing never gates on Vulkan, so no operation ever fires.
        cna_vulkan_test(cna_test_vulkan_depthstencilstate_stencil_ops
                        examples/easygl_depthstencilstate_stencil_ops_test.cpp)
        cna_register_backend_test(NAME Vulkan_DepthStencilState_StencilOps COMMAND cna_test_vulkan_depthstencilstate_stencil_ops
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 318: DepthStencilState.TwoSidedStencilMode on Vulkan (reuses the backend-agnostic
        # EasyGL source). NOTE: expected to FAIL the contrast check per Task 870 - stencil testing
        # never gates on Vulkan.
        cna_vulkan_test(cna_test_vulkan_depthstencilstate_stencil_twosided
                        examples/easygl_depthstencilstate_stencil_twosided_test.cpp)
        cna_register_backend_test(NAME Vulkan_DepthStencilState_StencilTwoSided COMMAND cna_test_vulkan_depthstencilstate_stencil_twosided
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 319: GraphicsDevice.ReferenceStencil independent-override behavior on Vulkan (Task
        # 872, universal gap, reuses the backend-agnostic EasyGL source).
        cna_vulkan_test(cna_test_vulkan_graphicsdevice_reference_stencil
                        examples/easygl_graphicsdevice_reference_stencil_test.cpp)
        cna_register_backend_test(NAME Vulkan_GraphicsDevice_ReferenceStencil COMMAND cna_test_vulkan_graphicsdevice_reference_stencil
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 871: GraphicsDevice::Clear() stencil-clearing fix is in SHARED code
        # (GraphicsDevice.cpp) -- verbatim reuse of the EasyGL source.
        cna_vulkan_test(cna_test_vulkan_graphicsdevice_clear_stencil
                        examples/easygl_graphicsdevice_clear_stencil_test.cpp)
        cna_register_backend_test(NAME Vulkan_GraphicsDevice_ClearStencil COMMAND cna_test_vulkan_graphicsdevice_clear_stencil
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 950: VulkanGraphicsBackend::ClearColorAndDepth/ClearColorDepthAndStencil now
        # actually thread their `depth` parameter into VkClearValue.depthStencil.depth instead of
        # hardcoding 1.0f -- verbatim reuse of the shared backend-agnostic source.
        cna_vulkan_test(cna_test_vulkan_graphicsdevice_clear_depth
                        examples/graphicsdevice_clear_depth_test.cpp)
        cna_register_backend_test(NAME Vulkan_GraphicsDevice_ClearDepth COMMAND cna_test_vulkan_graphicsdevice_clear_depth
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-089: backend-neutral public GraphicsDevice/DepthStencilState
        # A(Default)->B(DepthRead)->C(None)->A near/far contract, shared with D3D9/D3D11/Software.
        cna_vulkan_test(cna_test_vulkan_depth_contract
                        examples/graphicsdevice_depth_contract_test.cpp)
        cna_register_backend_test(NAME Vulkan_GraphicsDevice_DepthContract
            COMMAND cna_test_vulkan_depth_contract
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 955: a freshly-constructed GraphicsDevice's own default state (no game code ever
        # explicitly setting BlendState/DepthStencilState/RasterizerState, exactly like
        # ../cna-samples SimpleAnimation's Tank.hpp) must produce correct opaque depth occlusion.
        cna_vulkan_test(cna_test_vulkan_graphicsdevice_default_state_occlusion
                        examples/graphicsdevice_default_state_occlusion_test.cpp)
        cna_register_backend_test(NAME Vulkan_GraphicsDevice_DefaultStateOcclusion COMMAND cna_test_vulkan_graphicsdevice_default_state_occlusion
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 889: DualTextureEffect.VertexColorEnabled -- verbatim reuse of the shared
        # backend-agnostic source, registered on all 3 backends.
        cna_vulkan_test(cna_test_vulkan_dualtextureeffect_vertexcolor
                        examples/dualtextureeffect_vertexcolor_test.cpp)
        cna_register_backend_test(NAME Vulkan_DualTextureEffect_VertexColor COMMAND cna_test_vulkan_dualtextureeffect_vertexcolor
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 323 (+ 324/325 coverage): RasterizerState.CullMode on Vulkan (reuses the
        # backend-agnostic EasyGL source).
        cna_vulkan_test(cna_test_vulkan_rasterizerstate_cullmode
                        examples/easygl_rasterizerstate_cullmode_test.cpp)
        cna_register_backend_test(NAME Vulkan_RasterizerState_CullMode COMMAND cna_test_vulkan_rasterizerstate_cullmode
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # XNA culling-compatibility audit: real-camera CullMode reproducer, verbatim reuse of the
        # shared backend-agnostic source.
        cna_vulkan_test(cna_test_vulkan_rasterizerstate_cullmode_camera
                        examples/rasterizerstate_cullmode_camera_test.cpp)
        cna_register_backend_test(NAME Vulkan_RasterizerState_CullMode_Camera COMMAND cna_test_vulkan_rasterizerstate_cullmode_camera
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_vulkan_test(cna_test_vulkan_rasterizerstate_cullmode_indexed_basiceffect
                        examples/rasterizerstate_cullmode_indexed_basiceffect_test.cpp)
        cna_register_backend_test(NAME Vulkan_RasterizerState_CullMode_IndexedBasicEffect COMMAND cna_test_vulkan_rasterizerstate_cullmode_indexed_basiceffect
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 136: EnvironmentMapEffect — emissive=(1,0,0), envAmount=0 → red pixel on Vulkan
        cna_vulkan_test(cna_test_vulkan_env_map
                        examples/vulkan_env_map_test.cpp)
        cna_register_backend_test(NAME Vulkan_EnvironmentMapEffect_Readback COMMAND cna_test_vulkan_env_map
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 393: EnvironmentMapEffect EnvironmentMapAmount=0 should ignore the cube map
        cna_vulkan_test(cna_test_vulkan_environmentmapeffect_amount_zero
                        examples/vulkan_environmentmapeffect_amount_zero_test.cpp)
        cna_register_backend_test(NAME Vulkan_EnvironmentMapEffect_AmountZero COMMAND cna_test_vulkan_environmentmapeffect_amount_zero
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 890: EnvironmentMapEffect DirectionalLight1/DirectionalLight2 forwarding
        cna_vulkan_test(cna_test_vulkan_environmentmapeffect_multilight
                        examples/vulkan_environmentmapeffect_multilight_test.cpp)
        cna_register_backend_test(NAME Vulkan_EnvironmentMapEffect_MultiLight COMMAND cna_test_vulkan_environmentmapeffect_multilight
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 394: EnvironmentMapEffect EnvironmentMapAmount=1 should fully replace lit color
        cna_vulkan_test(cna_test_vulkan_environmentmapeffect_amount_one
                        examples/vulkan_environmentmapeffect_amount_one_test.cpp)
        cna_register_backend_test(NAME Vulkan_EnvironmentMapEffect_AmountOne COMMAND cna_test_vulkan_environmentmapeffect_amount_one
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 395: EnvironmentMapEffect EnvironmentMapSpecular should scale by cubemap alpha
        cna_vulkan_test(cna_test_vulkan_environmentmapeffect_specular
                        examples/vulkan_environmentmapeffect_specular_test.cpp)
        cna_register_backend_test(NAME Vulkan_EnvironmentMapEffect_Specular COMMAND cna_test_vulkan_environmentmapeffect_specular
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 891: EnvironmentMapEffect's base cube-map lerp target must be scaled by the
        # combined texture x diffuse alpha -- verbatim reuse of the shared backend-agnostic
        # source, registered on all 3 backends.
        cna_vulkan_test(cna_test_vulkan_environmentmapeffect_alphascaledlerp
                        examples/environmentmapeffect_alphascaledlerp_test.cpp)
        cna_register_backend_test(NAME Vulkan_EnvironmentMapEffect_AlphaScaledLerp COMMAND cna_test_vulkan_environmentmapeffect_alphascaledlerp
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 396: EnvironmentMapEffect Fresnel edge-weighting should suppress head-on reflections
        cna_vulkan_test(cna_test_vulkan_environmentmapeffect_fresnel
                        examples/vulkan_environmentmapeffect_fresnel_test.cpp)
        cna_register_backend_test(NAME Vulkan_EnvironmentMapEffect_Fresnel COMMAND cna_test_vulkan_environmentmapeffect_fresnel
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 397: EnvironmentMapEffect reflection vector should respond to EyePosition
        cna_vulkan_test(cna_test_vulkan_environmentmapeffect_eyeposition
                        examples/vulkan_environmentmapeffect_eyeposition_test.cpp)
        cna_register_backend_test(NAME Vulkan_EnvironmentMapEffect_EyePosition COMMAND cna_test_vulkan_environmentmapeffect_eyeposition
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 398: EnvironmentMapEffect normal transform should use inverse-transpose of World
        cna_vulkan_test(cna_test_vulkan_environmentmapeffect_worldtransform
                        examples/vulkan_environmentmapeffect_worldtransform_test.cpp)
        cna_register_backend_test(NAME Vulkan_EnvironmentMapEffect_WorldTransform COMMAND cna_test_vulkan_environmentmapeffect_worldtransform
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 399: cross-backend EnvironmentMapEffect capstone (Tasks 393-398 combined)
        cna_vulkan_test(cna_test_vulkan_environmentmapeffect_combined
                        examples/vulkan_environmentmapeffect_combined_test.cpp)
        cna_register_backend_test(NAME Vulkan_EnvironmentMapEffect_Combined COMMAND cna_test_vulkan_environmentmapeffect_combined
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 406: SkinnedEffect identity bone palette should produce zero deformation
        cna_vulkan_test(cna_test_vulkan_skinnedeffect_identity_bones
                        examples/vulkan_skinnedeffect_identity_bones_test.cpp)
        cna_register_backend_test(NAME Vulkan_SkinnedEffect_IdentityBones COMMAND cna_test_vulkan_skinnedeffect_identity_bones
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 407: SkinnedEffect single translation bone should shift the mesh
        cna_vulkan_test(cna_test_vulkan_skinnedeffect_translation_bone
                        examples/vulkan_skinnedeffect_translation_bone_test.cpp)
        cna_register_backend_test(NAME Vulkan_SkinnedEffect_TranslationBone COMMAND cna_test_vulkan_skinnedeffect_translation_bone
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 408: SkinnedEffect two-bone weighted blend should produce midpoint deformation
        cna_vulkan_test(cna_test_vulkan_skinnedeffect_twobone_blend
                        examples/vulkan_skinnedeffect_twobone_blend_test.cpp)
        cna_register_backend_test(NAME Vulkan_SkinnedEffect_TwoBoneBlend COMMAND cna_test_vulkan_skinnedeffect_twobone_blend
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 409: cross-backend SkinnedEffect capstone (identity + single-bone + two-bone blend)
        cna_vulkan_test(cna_test_vulkan_skinnedeffect_combined
                        examples/vulkan_skinnedeffect_combined_test.cpp)
        cna_register_backend_test(NAME Vulkan_SkinnedEffect_Combined COMMAND cna_test_vulkan_skinnedeffect_combined
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 334: RenderTargetCube sampled as TextureCube via EnvironmentMapEffect after unbinding
        cna_vulkan_test(cna_test_vulkan_rendertargetcube_sample
                        examples/vulkan_rendertargetcube_sample_test.cpp)
        cna_register_backend_test(NAME Vulkan_RenderTargetCube_SampleAfterUnbind COMMAND cna_test_vulkan_rendertargetcube_sample
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 907: RenderTargetCube mip chains are genuinely generated per-face
        # (vkCmdBlitImage cascade), split out of Task 878's original combined scope.
        cna_vulkan_test(cna_test_vulkan_rendertargetcube_mip
                        examples/vulkan_rendertargetcube_mip_test.cpp)
        cna_register_backend_test(NAME Vulkan_RenderTargetCube_MipChain COMMAND cna_test_vulkan_rendertargetcube_mip
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 903: RenderTargetCube MSAA is genuinely generated per-face (shared MSAA colour
        # image + promoted MSAA depth + 3-attachment render pass), split out of Task 878/879's
        # original RenderTarget2D-only scope.
        cna_vulkan_test(cna_test_vulkan_rendertargetcube_msaa
                        examples/vulkan_rendertargetcube_msaa_test.cpp)
        cna_register_backend_test(NAME Vulkan_RenderTargetCube_MsaaResolve COMMAND cna_test_vulkan_rendertargetcube_msaa
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 148: RenderTarget2D full cycle — red quad into RT, blit to backbuffer
        cna_vulkan_test(cna_test_vulkan_rt2d
                        examples/vulkan_rt2d_test.cpp)
        cna_register_backend_test(NAME Vulkan_RenderTarget2D_FullCycle COMMAND cna_test_vulkan_rt2d
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 875: Clear()-only render target (no draw call) actually records a render pass
        cna_vulkan_test(cna_test_vulkan_rt_roundtrip
                        examples/vulkan_rt_roundtrip_test.cpp)
        cna_register_backend_test(NAME Vulkan_RenderTarget2D_ClearOnlyRoundtrip COMMAND cna_test_vulkan_rt_roundtrip
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 335: RenderTarget2D depth buffer is real and functional (not just a stored property)
        cna_vulkan_test(cna_test_vulkan_rendertarget2d_depth
                        examples/rendertarget2d_depth_test.cpp)
        cna_register_backend_test(NAME Vulkan_RenderTarget2D_DepthBuffer COMMAND cna_test_vulkan_rendertarget2d_depth
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 865: Texture3D::GetData is now real on Vulkan -- shared with EasyGL's registration
        # of the same backend-agnostic file (Task 173).
        cna_vulkan_test(cna_test_vulkan_texture3d_slices
                        examples/easygl_texture3d_slices_test.cpp)
        cna_register_backend_test(NAME Vulkan_Texture3D_Slices_RoundTrip COMMAND cna_test_vulkan_texture3d_slices
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 865: TextureCube::GetData is now real on Vulkan -- shared with EasyGL's
        # registration of the same backend-agnostic file (Task 275).
        cna_vulkan_test(cna_test_vulkan_texturecube_partial_rect
                        examples/easygl_texturecube_partial_rect_test.cpp)
        cna_register_backend_test(NAME Vulkan_TextureCube_PartialRect_RoundTrip COMMAND cna_test_vulkan_texturecube_partial_rect
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 864: Texture3D mip-level allocation is now real on Vulkan -- shared with EasyGL's
        # registration of the same backend-agnostic file (Task 862).
        cna_vulkan_test(cna_test_vulkan_texture3d_mip
                        examples/easygl_texture3d_mip_test.cpp)
        cna_register_backend_test(NAME Vulkan_Texture3D_Mip_RoundTrip COMMAND cna_test_vulkan_texture3d_mip
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-093: level-scoped SetData/GetData state cycles.  Pixel/data checks alone pass
        # on the defective implementation, so make the exact copy-layout validation failure fatal.
        cna_vulkan_test(cna_test_vulkan_texture3d_mip_layout
                        examples/vulkan_texture3d_mip_layout_test.cpp)
        cna_register_backend_test(NAME Vulkan_Texture3D_Mip_Layout COMMAND cna_test_vulkan_texture3d_mip_layout
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
        set_tests_properties(Vulkan_Texture3D_Mip_Layout PROPERTIES
            FAIL_REGULAR_EXPRESSION "VUID-vkCmdDraw-None-09600")

        # Task 864: TextureCube mip-level allocation is now real on Vulkan -- shared with EasyGL's
        # registration of the same backend-agnostic file (Task 276).
        cna_vulkan_test(cna_test_vulkan_texturecube_mip
                        examples/easygl_texturecube_mip_test.cpp)
        cna_register_backend_test(NAME Vulkan_TextureCube_Mip_RoundTrip COMMAND cna_test_vulkan_texturecube_mip
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 178: RenderTargetUsage DiscardContents vs PreserveContents
        cna_vulkan_test(cna_test_vulkan_render_target_usage
                        examples/vulkan_render_target_usage_test.cpp)
        cna_register_backend_test(NAME Vulkan_RenderTargetUsage COMMAND cna_test_vulkan_render_target_usage
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 142: RenderTargetCube — 6-face per-face render passes
        cna_vulkan_test(cna_test_vulkan_rtcube
                        examples/vulkan_rtcube_test.cpp)
        cna_register_backend_test(NAME Vulkan_RenderTargetCube_PerFace COMMAND cna_test_vulkan_rtcube
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 147: MSAA 4× — multisampled swapchain + subpass resolve
        cna_vulkan_test(cna_test_vulkan_msaa
                        examples/vulkan_msaa_test.cpp)
        cna_register_backend_test(NAME Vulkan_MSAA_4x_Readback COMMAND cna_test_vulkan_msaa
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_vulkan_test(cna_test_vulkan_vertex_format
                        examples/vulkan_vertex_format_test.cpp)
        cna_register_backend_test(NAME Vulkan_VertexFormat_AllStrides COMMAND cna_test_vulkan_vertex_format
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_vulkan_test(cna_test_vulkan_fill_mode
                        examples/vulkan_fill_mode_test.cpp)
        cna_register_backend_test(NAME Vulkan_FillMode_WireFrame COMMAND cna_test_vulkan_fill_mode
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_vulkan_test(cna_test_vulkan_depth_bias
                        examples/vulkan_depth_bias_test.cpp)
        cna_register_backend_test(NAME Vulkan_DepthBias COMMAND cna_test_vulkan_depth_bias
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 329: ScissorTestEnable + ScissorRectangle pixel-readback test
        cna_vulkan_test(cna_test_vulkan_scissor
                        examples/vulkan_scissor_test.cpp)
        cna_register_backend_test(NAME Vulkan_ScissorTest COMMAND cna_test_vulkan_scissor
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-081: SpriteBatch.Begin must apply its RasterizerState argument (shared fix in
        # SpriteBatch.cpp). Deferred-backend control: the scissor state must reach the BatchSnapshot
        # through Begin, not only via a direct GraphicsDevice.RasterizerState assignment.
        cna_vulkan_test(cna_test_vulkan_spritebatch_begin_rasterizerstate
                        examples/spritebatch_begin_rasterizerstate_scissor_test.cpp)
        cna_register_backend_test(NAME Vulkan_SpriteBatch_BeginRasterizerState COMMAND cna_test_vulkan_spritebatch_begin_rasterizerstate
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-013: ScissorRectangle must clip draws issued while a RenderTarget2D is bound
        cna_vulkan_test(cna_test_vulkan_rendertarget_scissor
                        examples/vulkan_rendertarget_scissor_test.cpp)
        cna_register_backend_test(NAME Vulkan_RenderTarget_Scissor COMMAND cna_test_vulkan_rendertarget_scissor
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-062: Viewport must be honored for draws issued while a RenderTarget2D is bound
        cna_vulkan_test(cna_test_vulkan_rendertarget_viewport
                        examples/vulkan_rendertarget_viewport_test.cpp)
        cna_register_backend_test(NAME Vulkan_RenderTarget_Viewport COMMAND cna_test_vulkan_rendertarget_viewport
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-070: GraphicsDevice.BlendFactor (the constant blend color) must be applied per
        # draw/batch, not once per frame — RT passes must set it, and multiple values in one frame
        # must not collapse to last-wins.
        cna_vulkan_test(cna_test_vulkan_rendertarget_blendfactor
                        examples/vulkan_rendertarget_blendfactor_test.cpp)
        cna_register_backend_test(NAME Vulkan_RenderTarget_BlendFactor COMMAND cna_test_vulkan_rendertarget_blendfactor
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-071: the Vulkan 2D SpriteBatch pipeline must honor the SpriteBatch.Begin()
        # BlendState (colour/alpha factors + function + BlendFactor), not one hardcoded alpha-blend
        # equation. Opaque/AlphaBlend(premultiplied)/NonPremultiplied/Additive/custom factors/
        # custom function/separate alpha/Blend::BlendFactor into a RenderTarget2D, read back with
        # GetData (GFX-074). Pre-fix all colored scenes read the hardcoded (130,80,110,128); the fix
        # plumbs BlendKeyParams into the 2D pipeline cache key + FillBlendAttachmentState (the 3D
        # path's proven translation). The GFX-070 per-batch blend-constant capture becomes live here.
        cna_vulkan_test(cna_test_vulkan_spritebatch_blendstate
                        examples/vulkan_spritebatch_blendstate_test.cpp)
        cna_register_backend_test(NAME Vulkan_SpriteBatch_BlendState COMMAND cna_test_vulkan_spritebatch_blendstate
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-077: BlendState.ColorWriteChannels (per-attachment write mask) + MultiSampleMask
        # (pSampleMask) via the extended pipeline cache key.
        cna_vulkan_test(cna_test_vulkan_colorwritechannels
                        examples/vulkan_colorwritechannels_test.cpp)
        cna_register_backend_test(NAME Vulkan_ColorWriteChannels COMMAND cna_test_vulkan_colorwritechannels
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 664: SpriteBatch multiple Begin()/End() cycles per frame must all render
        cna_vulkan_test(cna_test_vulkan_spritebatch_multi_begin_end
                        examples/vulkan_spritebatch_multi_begin_end_test.cpp)
        cna_register_backend_test(NAME Vulkan_SpriteBatch_MultiBeginEnd COMMAND cna_test_vulkan_spritebatch_multi_begin_end
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-012: SpriteBatch Begin()'s transformMatrix must be applied (was a silent no-op)
        cna_vulkan_test(cna_test_vulkan_transform_matrix
                        examples/vulkan_transform_matrix_test.cpp)
        cna_register_backend_test(NAME Vulkan_SpriteBatch_TransformMatrix COMMAND cna_test_vulkan_transform_matrix
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 665: SpriteBatch Begin()'s SamplerState (TextureAddressMode) must take effect
        cna_vulkan_test(cna_test_vulkan_texture_address_mode
                        examples/vulkan_texture_address_mode_test.cpp)
        cna_register_backend_test(NAME Vulkan_TextureAddressMode COMMAND cna_test_vulkan_texture_address_mode
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 338: SetRenderTarget(nullptr) resets Viewport/ScissorRectangle to the backbuffer
        cna_vulkan_test(cna_test_vulkan_rendertarget_viewport_scissor_reset
                        examples/rendertarget_viewport_scissor_reset_test.cpp)
        cna_register_backend_test(NAME Vulkan_RenderTarget_ViewportScissorReset COMMAND cna_test_vulkan_rendertarget_viewport_scissor_reset
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 349: custom Viewport survives Present() with no resize, resets after a real one.
        cna_vulkan_test(cna_test_vulkan_viewport_reset_after_resize
                        examples/viewport_reset_after_resize_test.cpp)
        cna_register_backend_test(NAME Vulkan_ViewportResetAfterResize COMMAND cna_test_vulkan_viewport_reset_after_resize
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 331: RenderTarget2D constructor/property audit against FNA.
        cna_vulkan_test(cna_test_vulkan_rendertarget2d_properties
                        examples/easygl_rendertarget2d_properties_test.cpp)
        cna_register_backend_test(NAME Vulkan_RenderTarget2D_Properties COMMAND cna_test_vulkan_rendertarget2d_properties
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 332: RenderTargetCube constructor/property audit against FNA.
        cna_vulkan_test(cna_test_vulkan_rendertargetcube_properties
                        examples/easygl_rendertargetcube_properties_test.cpp)
        cna_register_backend_test(NAME Vulkan_RenderTargetCube_Properties COMMAND cna_test_vulkan_rendertargetcube_properties
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 364: BasicEffect pixel test — VertexColorEnabled=false, no texture, diffuse color only
        cna_vulkan_test(cna_test_vulkan_basiceffect_vertexcolor_disabled
                        examples/vulkan_basiceffect_vertexcolor_disabled_test.cpp)
        cna_register_backend_test(NAME Vulkan_BasicEffect_VertexColorDisabled COMMAND cna_test_vulkan_basiceffect_vertexcolor_disabled
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 365: BasicEffect pixel test — VertexColorEnabled=true, no texture, vertex color multiplication
        cna_vulkan_test(cna_test_vulkan_basiceffect_vertexcolor_enabled
                        examples/vulkan_basiceffect_vertexcolor_enabled_test.cpp)
        cna_register_backend_test(NAME Vulkan_BasicEffect_VertexColorEnabled COMMAND cna_test_vulkan_basiceffect_vertexcolor_enabled
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 366: BasicEffect pixel test — TextureEnabled=true, no vertex color
        cna_vulkan_test(cna_test_vulkan_basiceffect_texture_enabled
                        examples/vulkan_basiceffect_texture_enabled_test.cpp)
        cna_register_backend_test(NAME Vulkan_BasicEffect_TextureEnabled COMMAND cna_test_vulkan_basiceffect_texture_enabled
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 904: GetOrCreatePipelineFogTex3D's missing msaa-aware render-pass check.
        cna_vulkan_test(cna_test_vulkan_basiceffect_textured_msaa
                        examples/vulkan_basiceffect_textured_msaa_test.cpp)
        cna_register_backend_test(NAME Vulkan_BasicEffect_TexturedMsaa COMMAND cna_test_vulkan_basiceffect_textured_msaa
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 367: BasicEffect pixel test — TextureEnabled=true AND VertexColorEnabled=true
        cna_vulkan_test(cna_test_vulkan_basiceffect_texture_vertexcolor_enabled
                        examples/vulkan_basiceffect_texture_vertexcolor_enabled_test.cpp)
        cna_register_backend_test(NAME Vulkan_BasicEffect_TextureVertexColorEnabled COMMAND cna_test_vulkan_basiceffect_texture_vertexcolor_enabled
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 368: BasicEffect pixel test — LightingEnabled=true, one DirectionalLight
        cna_vulkan_test(cna_test_vulkan_basiceffect_one_light
                        examples/vulkan_basiceffect_one_light_test.cpp)
        cna_register_backend_test(NAME Vulkan_BasicEffect_OneLight COMMAND cna_test_vulkan_basiceffect_one_light
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 897: BasicEffect pixel test — DirectionalLight1/2 + EmissiveColor on the lit path
        cna_vulkan_test(cna_test_vulkan_basiceffect_multilight_emissive
                        examples/vulkan_basiceffect_multilight_emissive_test.cpp)
        cna_register_backend_test(NAME Vulkan_BasicEffect_MultiLightEmissive COMMAND cna_test_vulkan_basiceffect_multilight_emissive
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 886: BasicEffect pixel test — real specular highlights
        cna_vulkan_test(cna_test_vulkan_basiceffect_specular
                        examples/vulkan_basiceffect_specular_test.cpp)
        cna_register_backend_test(NAME Vulkan_BasicEffect_Specular COMMAND cna_test_vulkan_basiceffect_specular
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 1103: BasicEffect pixel test — PreferPerPixelLighting genuinely selects between a
        # real per-vertex-lit shader variant and the existing per-pixel-lit one.
        cna_vulkan_test(cna_test_vulkan_basiceffect_preferperpixellighting
                        examples/vulkan_basiceffect_preferperpixellighting_test.cpp)
        cna_register_backend_test(NAME Vulkan_BasicEffect_PreferPerPixelLighting COMMAND cna_test_vulkan_basiceffect_preferperpixellighting
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 887: AlphaTestEffect.VertexColorEnabled fix verification
        cna_vulkan_test(cna_test_vulkan_alphatest_vertexcolor
                        examples/vulkan_alphatest_vertexcolor_test.cpp)
        cna_register_backend_test(NAME Vulkan_AlphaTest_VertexColor COMMAND cna_test_vulkan_alphatest_vertexcolor
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 888: BasicEffect fog on the lit_textured3d (stride-32) pipeline
        cna_vulkan_test(cna_test_vulkan_basiceffect_fog
                        examples/vulkan_basiceffect_fog_test.cpp)
        cna_register_backend_test(NAME Vulkan_BasicEffect_Fog COMMAND cna_test_vulkan_basiceffect_fog
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 888: AlphaTestEffect fog on the alpha_test3d pipeline
        cna_vulkan_test(cna_test_vulkan_alphatest_fog
                        examples/vulkan_alphatest_fog_test.cpp)
        cna_register_backend_test(NAME Vulkan_AlphaTest_Fog COMMAND cna_test_vulkan_alphatest_fog
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 369: BasicEffect pixel test — DiffuseColor+EmissiveColor, LightingEnabled=false
        cna_vulkan_test(cna_test_vulkan_basiceffect_emissive
                        examples/vulkan_basiceffect_emissive_test.cpp)
        cna_register_backend_test(NAME Vulkan_BasicEffect_Emissive COMMAND cna_test_vulkan_basiceffect_emissive
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 370: BasicEffect cross-backend image comparison suite — closes Phase 42
        cna_vulkan_test(cna_test_vulkan_basiceffect_combined
                        examples/vulkan_basiceffect_combined_test.cpp)
        cna_register_backend_test(NAME Vulkan_BasicEffect_Combined COMMAND cna_test_vulkan_basiceffect_combined
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 374: AlphaTestEffect all CompareFunction modes — threshold sweep
        cna_vulkan_test(cna_test_vulkan_alphatest_comparefunction_sweep
                        examples/vulkan_alphatest_comparefunction_sweep_test.cpp)
        cna_register_backend_test(NAME Vulkan_AlphaTest_CompareFunctionSweep COMMAND cna_test_vulkan_alphatest_comparefunction_sweep
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 379: AlphaTestEffect null/no-texture behavior
        cna_vulkan_test(cna_test_vulkan_alphatest_null_texture
                        examples/vulkan_alphatest_null_texture_test.cpp)
        cna_register_backend_test(NAME Vulkan_AlphaTest_NullTexture COMMAND cna_test_vulkan_alphatest_null_texture
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 383: DualTextureEffect two-texture blend formula, including FNA's color.rgb*=2
        cna_vulkan_test(cna_test_vulkan_dualtextureeffect_doubling
                        examples/vulkan_dualtextureeffect_doubling_test.cpp)
        cna_register_backend_test(NAME Vulkan_DualTextureEffect_Doubling COMMAND cna_test_vulkan_dualtextureeffect_doubling
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 385: DualTextureEffect Alpha forwarding, alpha-channel-only (see file header for
        # why the full RGB-premultiplication check is deliberately not done on Vulkan)
        cna_vulkan_test(cna_test_vulkan_dualtextureeffect_alpha
                        examples/vulkan_dualtextureeffect_alpha_test.cpp)
        cna_register_backend_test(NAME Vulkan_DualTextureEffect_Alpha COMMAND cna_test_vulkan_dualtextureeffect_alpha
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 386: DualTextureEffect first texture (Texture, slot 0) null behavior
        cna_vulkan_test(cna_test_vulkan_dualtextureeffect_null_texture0
                        examples/vulkan_dualtextureeffect_null_texture0_test.cpp)
        cna_register_backend_test(NAME Vulkan_DualTextureEffect_NullTexture0 COMMAND cna_test_vulkan_dualtextureeffect_null_texture0
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 387: DualTextureEffect second texture (Texture2, slot 1) null behavior
        cna_vulkan_test(cna_test_vulkan_dualtextureeffect_null_texture2
                        examples/vulkan_dualtextureeffect_null_texture2_test.cpp)
        cna_register_backend_test(NAME Vulkan_DualTextureEffect_NullTexture2 COMMAND cna_test_vulkan_dualtextureeffect_null_texture2
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 389: DualTextureEffect cross-backend image comparison suite (closes Phase 44 pixel work)
        cna_vulkan_test(cna_test_vulkan_dualtextureeffect_combined
                        examples/vulkan_dualtextureeffect_combined_test.cpp)
        cna_register_backend_test(NAME Vulkan_DualTextureEffect_Combined COMMAND cna_test_vulkan_dualtextureeffect_combined
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 880: GraphicsDevice.Viewport GPU wiring — sub-region viewport pixel-readback test
        cna_vulkan_test(cna_test_vulkan_viewport_subregion
                        examples/vulkan_viewport_subregion_test.cpp)
        cna_register_backend_test(NAME Vulkan_Viewport_Subregion COMMAND cna_test_vulkan_viewport_subregion
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 899: fog on colored3d/textured3d/colored_textured3d/dual_texture3d/skinned3d
        cna_vulkan_test(cna_test_vulkan_basiceffect_colored3d_fog
                        examples/vulkan_basiceffect_colored3d_fog_test.cpp)
        cna_register_backend_test(NAME Vulkan_BasicEffect_Colored3D_Fog COMMAND cna_test_vulkan_basiceffect_colored3d_fog
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_vulkan_test(cna_test_vulkan_basiceffect_textured3d_fog
                        examples/vulkan_basiceffect_textured3d_fog_test.cpp)
        cna_register_backend_test(NAME Vulkan_BasicEffect_Textured3D_Fog COMMAND cna_test_vulkan_basiceffect_textured3d_fog
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_vulkan_test(cna_test_vulkan_basiceffect_coloredtextured3d_fog
                        examples/vulkan_basiceffect_coloredtextured3d_fog_test.cpp)
        cna_register_backend_test(NAME Vulkan_BasicEffect_ColoredTextured3D_Fog COMMAND cna_test_vulkan_basiceffect_coloredtextured3d_fog
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_vulkan_test(cna_test_vulkan_dualtextureeffect_fog
                        examples/vulkan_dualtextureeffect_fog_test.cpp)
        cna_register_backend_test(NAME Vulkan_DualTextureEffect_Fog COMMAND cna_test_vulkan_dualtextureeffect_fog
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_vulkan_test(cna_test_vulkan_skinnedeffect_fog
                        examples/vulkan_skinnedeffect_fog_test.cpp)
        cna_register_backend_test(NAME Vulkan_SkinnedEffect_Fog COMMAND cna_test_vulkan_skinnedeffect_fog
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-010: transformed-camera view-space fog conformance (fails pre-fix).
        cna_vulkan_test(cna_test_vulkan_viewspace_fog
                        examples/vulkan_viewspace_fog_test.cpp)
        cna_register_backend_test(NAME Vulkan_ViewSpace_Fog COMMAND cna_test_vulkan_viewspace_fog
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 893: SkinnedEffect DirectionalLight1/DirectionalLight2 forwarding
        cna_vulkan_test(cna_test_vulkan_skinnedeffect_multilight
                        examples/vulkan_skinnedeffect_multilight_test.cpp)
        cna_register_backend_test(NAME Vulkan_SkinnedEffect_MultiLight COMMAND cna_test_vulkan_skinnedeffect_multilight
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 894: SkinnedEffect real specular highlights
        cna_vulkan_test(cna_test_vulkan_skinnedeffect_specular
                        examples/vulkan_skinnedeffect_specular_test.cpp)
        cna_register_backend_test(NAME Vulkan_SkinnedEffect_Specular COMMAND cna_test_vulkan_skinnedeffect_specular
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-008: analytic SkinnedEffect ambient/emissive lighting conformance
        cna_vulkan_test(cna_test_vulkan_skinnedeffect_lighting_conformance
                        examples/skinnedeffect_lighting_conformance_test.cpp)
        cna_register_backend_test(NAME Vulkan_SkinnedEffect_LightingConformance COMMAND cna_test_vulkan_skinnedeffect_lighting_conformance
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 1103: SkinnedEffect pixel test — PreferPerPixelLighting genuinely selects between a
        # real per-vertex-lit shader variant and the existing per-pixel-lit one.
        cna_vulkan_test(cna_test_vulkan_skinnedeffect_preferperpixellighting
                        examples/vulkan_skinnedeffect_preferperpixellighting_test.cpp)
        cna_register_backend_test(NAME Vulkan_SkinnedEffect_PreferPerPixelLighting COMMAND cna_test_vulkan_skinnedeffect_preferperpixellighting
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 895: SkinnedEffect WeightsPerVertex real GPU enforcement
        cna_vulkan_test(cna_test_vulkan_skinnedeffect_weightspervertex
                        examples/vulkan_skinnedeffect_weightspervertex_test.cpp)
        cna_register_backend_test(NAME Vulkan_SkinnedEffect_WeightsPerVertex COMMAND cna_test_vulkan_skinnedeffect_weightspervertex
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 899's own noted cheap leftover: env_map3d fog packed into EnvMapParams' spare bytes
        cna_vulkan_test(cna_test_vulkan_environmentmapeffect_fog
                        examples/vulkan_environmentmapeffect_fog_test.cpp)
        cna_register_backend_test(NAME Vulkan_EnvironmentMapEffect_Fog COMMAND cna_test_vulkan_environmentmapeffect_fog
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 878/879: RenderTarget2D MSAA resolve genuinely anti-aliases on Vulkan (piggybacks
        # on the backend's own backbuffer sampleCount_ -- requires PreferMultiSampling=true).
        cna_vulkan_test(cna_test_vulkan_rendertarget2d_msaa
                        examples/vulkan_rendertarget2d_msaa_test.cpp)
        cna_register_backend_test(NAME Vulkan_RenderTarget2D_MsaaResolve COMMAND cna_test_vulkan_rendertarget2d_msaa
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 878: RenderTarget2D mip chains are genuinely generated (vkCmdBlitImage cascade),
        # not just present-but-undefined storage.
        cna_vulkan_test(cna_test_vulkan_rendertarget2d_mip
                        examples/vulkan_rendertarget2d_mip_test.cpp)
        cna_register_backend_test(NAME Vulkan_RenderTarget2D_MipChain COMMAND cna_test_vulkan_rendertarget2d_mip
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 911: RenderTarget2D per-instance DepthStencilFormat fidelity -- DepthFormat::None
        # genuinely has no depth buffer, Depth24Stencil8/Depth16 genuinely do (each with its own
        # render pass), all three coexisting correctly in a single frame.
        cna_vulkan_test(cna_test_vulkan_rendertarget_depthformat_fidelity
                        examples/vulkan_rendertarget_depthformat_fidelity_test.cpp)
        cna_register_backend_test(NAME Vulkan_RenderTarget2D_DepthFormatFidelity COMMAND cna_test_vulkan_rendertarget_depthformat_fidelity
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 13.6: the same 3 avatar-rendering GPU integration tests already registered for
        # EasyGL above, reusing the exact same backend-agnostic source files (public
        # GraphicsDevice/SkinnedModelEXT/AvatarRenderer API only) - Vulkan's own dedicated
        # GetOrCreatePipelineSkinned3D pipeline (Task 11.10) had never been run against real (or
        # even synthetic) avatar content before now.
        if(CNA_ENABLE_NET)
            cna_vulkan_test(cna_test_vulkan_avatar_real_render
                            examples/avatar_real_render_integration_test.cpp)
            target_link_libraries(cna_test_vulkan_avatar_real_render PRIVATE CNA_GamerServices)
            cna_register_backend_test(NAME Vulkan_AvatarRenderer_RealRender COMMAND cna_test_vulkan_avatar_real_render
                TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

            cna_vulkan_test(cna_test_vulkan_avatar_attach_part
                            examples/avatar_attach_part_integration_test.cpp)
            target_link_libraries(cna_test_vulkan_avatar_attach_part PRIVATE CNA_GamerServices)
            cna_register_backend_test(NAME Vulkan_AvatarRenderer_AttachPart COMMAND cna_test_vulkan_avatar_attach_part
                TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

            cna_vulkan_test(cna_test_vulkan_avatar_tint_routing
                            examples/avatar_tint_routing_integration_test.cpp)
            target_link_libraries(cna_test_vulkan_avatar_tint_routing PRIVATE CNA_GamerServices)
            cna_register_backend_test(NAME Vulkan_AvatarRenderer_TintRouting COMMAND cna_test_vulkan_avatar_tint_routing
                TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
        endif()

        # Task 842 (Phase 73 Vulkan gap closure): verify MRT with mixed formats is rejected or
        # handled per XNA constraints on Vulkan -- verbatim reuse of the backend-agnostic test
        # (Task 774's own file, no Bgfx-specific code at all: pure GraphicsDevice/RenderTarget2D
        # API calls, no rendering). Confirms Task 774's shared-code Texture::ValidateFormat fix
        # (RenderTarget2D/RenderTargetCube's own delegating constructors previously skipped it)
        # also holds on Vulkan.
        cna_vulkan_test(cna_test_vulkan_mrt_mixed_formats
                        examples/bgfx_mrt_mixed_formats_test.cpp)
        cna_register_backend_test(NAME Vulkan_MRT_MixedFormats COMMAND cna_test_vulkan_mrt_mixed_formats
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 842 (verbatim reuse): Task 176's own SurfaceFormat-validation contract, confirmed
        # backend-agnostic -- reused unmodified from EasyGL to directly confirm the same
        # Color-only constraint holds on Vulkan too.
        cna_vulkan_test(cna_test_vulkan_surface_format_throws
                        examples/easygl_surface_format_throws_test.cpp)
        cna_register_backend_test(NAME Vulkan_SurfaceFormat_Throws COMMAND cna_test_vulkan_surface_format_throws
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 850 (Phase 73 Vulkan gap closure): SpriteSortMode ordering pixel test on Vulkan --
        # verbatim reuse of the shared EasyGL source (Task 420); no Vulkan-specific restructuring
        # needed (Vulkan has no Bgfx-style "first GetBackBufferData read per frame" quirk).
        cna_vulkan_test(cna_test_vulkan_spritebatch_layerdepth
                        examples/easygl_spritebatch_layerdepth_test.cpp)
        cna_register_backend_test(NAME Vulkan_SpriteBatch_LayerDepthOrder COMMAND cna_test_vulkan_spritebatch_layerdepth
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 851 (Phase 73 Vulkan gap closure): SpriteBatch rotation/scale/source-rectangle-
        # cropping/SpriteEffects flip on Vulkan -- verbatim reuse of the shared EasyGL sources
        # (Tasks 417/418/419/167).
        cna_vulkan_test(cna_test_vulkan_spritebatch_rotation
                        examples/easygl_spritebatch_rotation_test.cpp)
        cna_register_backend_test(NAME Vulkan_SpriteBatch_Rotation COMMAND cna_test_vulkan_spritebatch_rotation
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_vulkan_test(cna_test_vulkan_spritebatch_scale
                        examples/easygl_spritebatch_scale_test.cpp)
        cna_register_backend_test(NAME Vulkan_SpriteBatch_Scale COMMAND cna_test_vulkan_spritebatch_scale
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_vulkan_test(cna_test_vulkan_spritebatch_sourcerect
                        examples/easygl_spritebatch_sourcerect_test.cpp)
        cna_register_backend_test(NAME Vulkan_SpriteBatch_SourceRectangleCropping COMMAND cna_test_vulkan_spritebatch_sourcerect
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_vulkan_test(cna_test_vulkan_sprite_effects
                        examples/easygl_sprite_effects_test.cpp)
        cna_register_backend_test(NAME Vulkan_SpriteEffects_Flip COMMAND cna_test_vulkan_sprite_effects
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 852 (Phase 73 Vulkan gap closure): SpriteFont single-glyph placement, multi-glyph
        # spacing + newline, and default-character fallback on Vulkan -- verbatim reuse of the
        # shared EasyGL sources (Tasks 424/425/426/427).
        cna_vulkan_test(cna_test_vulkan_spritefont_single_glyph
                        examples/easygl_spritefont_single_glyph_test.cpp)
        cna_register_backend_test(NAME Vulkan_SpriteFont_SingleGlyph COMMAND cna_test_vulkan_spritefont_single_glyph
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_vulkan_test(cna_test_vulkan_spritefont_multiglyph_spacing
                        examples/easygl_spritefont_multiglyph_spacing_test.cpp)
        cna_register_backend_test(NAME Vulkan_SpriteFont_MultiGlyphSpacing COMMAND cna_test_vulkan_spritefont_multiglyph_spacing
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_vulkan_test(cna_test_vulkan_spritefont_newline
                        examples/easygl_spritefont_newline_test.cpp)
        cna_register_backend_test(NAME Vulkan_SpriteFont_Newline COMMAND cna_test_vulkan_spritefont_newline
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_vulkan_test(cna_test_vulkan_spritefont_default_char
                        examples/easygl_spritefont_default_char_test.cpp)
        cna_register_backend_test(NAME Vulkan_SpriteFont_DefaultCharacterFallback COMMAND cna_test_vulkan_spritefont_default_char
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 853 (Phase 73 Vulkan gap closure): Model with two meshes and distinct per-mesh
        # Effects, and Model hierarchy transform propagation, on Vulkan -- verbatim reuse of the
        # shared EasyGL sources (Tasks 438/439); Vulkan's own SetDepthTestEnabled is a real,
        # working method (unlike Bgfx's throwing stub), so no substitution is needed at all.
        cna_vulkan_test(cna_test_vulkan_model_two_meshes_effects
                        examples/easygl_model_two_meshes_effects_test.cpp)
        cna_register_backend_test(NAME Vulkan_Model_TwoMeshesEffects COMMAND cna_test_vulkan_model_two_meshes_effects
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_vulkan_test(cna_test_vulkan_model_hierarchy_child_mesh
                        examples/easygl_model_hierarchy_child_mesh_test.cpp)
        cna_register_backend_test(NAME Vulkan_Model_HierarchyChildMesh COMMAND cna_test_vulkan_model_hierarchy_child_mesh
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 927: ModelTypeReader::Read()'s vertex-stride/vtable-size corruption fix is in
        # SHARED code (ContentManager.cpp) -- verbatim reuse of the EasyGL source.
        cna_vulkan_test(cna_test_vulkan_model_json_reader
                        examples/easygl_model_json_reader_test.cpp)
        cna_register_backend_test(NAME Vulkan_ModelJsonReader_Quad COMMAND cna_test_vulkan_model_json_reader
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 931: ModelTypeReader::Read()'s 32-bit index auto-selection fix is in SHARED code
        # (ContentManager.cpp) -- verbatim reuse of the EasyGL source.
        cna_vulkan_test(cna_test_vulkan_model_json_reader_32bit_indices
                        examples/easygl_model_json_reader_32bit_indices_test.cpp)
        cna_register_backend_test(NAME Vulkan_ModelJsonReader_32BitIndices COMMAND cna_test_vulkan_model_json_reader_32bit_indices
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 932: ModelTypeReader::Read()'s per-mesh texture-binding fix is in SHARED code
        # (ContentManager.cpp) -- verbatim reuse of the EasyGL source.
        cna_vulkan_test(cna_test_vulkan_model_json_reader_texture
                        examples/easygl_model_json_reader_texture_test.cpp)
        cna_register_backend_test(NAME Vulkan_ModelJsonReader_Texture COMMAND cna_test_vulkan_model_json_reader_texture
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 934: TextureCubeTypeReader / Content.Load<TextureCube>() fix is in SHARED code
        # (ContentManager.cpp/.hpp) -- verbatim reuse of the EasyGL source.
        cna_vulkan_test(cna_test_vulkan_texturecube_content_load
                        examples/easygl_texturecube_content_load_test.cpp)
        cna_register_backend_test(NAME Vulkan_TextureCube_ContentLoad COMMAND cna_test_vulkan_texturecube_content_load
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 937: ModelTypeReader::Read()'s per-mesh-bone fix is in SHARED code
        # (ContentManager.cpp) -- verbatim reuse of the EasyGL source (pure structural test).
        cna_vulkan_test(cna_test_vulkan_model_json_reader_bone_hierarchy
                        examples/easygl_model_json_reader_bone_hierarchy_test.cpp)
        cna_register_backend_test(NAME Vulkan_ModelJsonReader_BoneHierarchy COMMAND cna_test_vulkan_model_json_reader_bone_hierarchy
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 941: ModelTypeReader::Read()'s "skeleton"/"animations"/SkinnedEffect support is in
        # SHARED code (ContentManager.cpp) -- verbatim reuse of the EasyGL source (pure
        # structural test).
        cna_vulkan_test(cna_test_vulkan_model_json_reader_skeleton
                        examples/easygl_model_json_reader_skeleton_test.cpp)
        cna_register_backend_test(NAME Vulkan_ModelJsonReader_Skeleton COMMAND cna_test_vulkan_model_json_reader_skeleton
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 942: AnimationPlayer -> SkinnedEffect -> Model.Draw() wiring is in SHARED code
        # (ContentManager.cpp/AnimationPlayer.cpp/SkinnedEffect) -- verbatim reuse of the EasyGL
        # source (pixel test).
        cna_vulkan_test(cna_test_vulkan_model_skinned_animation_playback
                        examples/easygl_model_skinned_animation_playback_test.cpp)
        cna_register_backend_test(NAME Vulkan_Model_SkinnedAnimationPlayback COMMAND cna_test_vulkan_model_skinned_animation_playback
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 930: VertexBuffer/IndexBuffer::GetData() is a CPU-side shadow buffer in SHARED
        # code (VertexBuffer.cpp/IndexBuffer.cpp) -- verbatim reuse of the EasyGL source.
        cna_vulkan_test(cna_test_vulkan_vertexbuffer_indexbuffer_getdata
                        examples/easygl_vertexbuffer_indexbuffer_getdata_test.cpp)
        cna_register_backend_test(NAME Vulkan_VertexBufferIndexBufferGetData COMMAND cna_test_vulkan_vertexbuffer_indexbuffer_getdata
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 855 (Phase 73 Vulkan gap closure): Texture2D SetData/GetData partial-rectangle +
        # startIndex/elementCount + mip-level on Vulkan -- verbatim reuse of the shared EasyGL
        # sources (Tasks 169-171); both operate entirely on a backend-agnostic CPU-side shadow
        # buffer (Texture2D.cpp), no GPU readback involved at all.
        cna_vulkan_test(cna_test_vulkan_texture2d_partial_rect
                        examples/easygl_texture2d_partial_rect_test.cpp)
        cna_register_backend_test(NAME Vulkan_Texture2D_PartialRect_RoundTrip COMMAND cna_test_vulkan_texture2d_partial_rect
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_vulkan_test(cna_test_vulkan_texture2d_mip
                        examples/easygl_texture2d_mip_test.cpp)
        cna_register_backend_test(NAME Vulkan_Texture2D_Mip_RoundTrip COMMAND cna_test_vulkan_texture2d_mip
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 857 (Phase 73 Vulkan gap closure): NPOT texture upload+sample on Vulkan --
        # verbatim reuse of the shared EasyGL source (Task 268).
        cna_vulkan_test(cna_test_vulkan_npot_texture
                        examples/easygl_npot_texture_test.cpp)
        cna_register_backend_test(NAME Vulkan_NpotTexture COMMAND cna_test_vulkan_npot_texture
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 858 (Phase 73 Vulkan gap closure): Texture3D box-region SetData/GetData pixel
        # correctness on Vulkan -- verbatim reuse of the shared EasyGL source (Task 274). Vulkan's
        # Texture3D::GetData already has a real per-backend GPU round-trip (Task 865).
        cna_vulkan_test(cna_test_vulkan_texture3d_partial_box_readback
                        examples/easygl_texture3d_partial_box_readback_test.cpp)
        cna_register_backend_test(NAME Vulkan_Texture3D_PartialBox_Readback COMMAND cna_test_vulkan_texture3d_partial_box_readback
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Tasks 447/854 (Phase 73 Vulkan gap closure): real per-draw-call OcclusionQuery
        # correlation -- previously entirely-stubbed Begin()/End() (always returned PixelCount=0).
        # Draws are now tagged with the active query (VulkanGraphicsBackend::PushPending3DDraw)
        # and RecordCommandBuffer() wraps the tagged run in a real vkCmdBeginQuery/vkCmdEndQuery
        # pair with correct per-frame vkCmdResetQueryPool sequencing.
        cna_vulkan_test(cna_test_vulkan_occlusionquery_pixelcount
                        examples/vulkan_occlusionquery_pixelcount_test.cpp)
        cna_register_backend_test(NAME Vulkan_OcclusionQuery_PixelCount COMMAND cna_test_vulkan_occlusionquery_pixelcount
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # plan_cnj.md CNB-58/91 Vulkan port: PbrEffect/SkinnedPbrEffect real glTF metallic-
        # roughness BRDF shader + CNB-67 SkinnedEffect.VertexColorEnabled on the stride-56 vertex
        # layout -- verbatim reuse of the shared, backend-agnostic EasyGL sources (public XNA API
        # only, same PixelTestGame/ExpectPixel/CompareGoldenImage harness already used by
        # Task 293/294/296/297's own EasyGL-source reuse for Vulkan), cross-checked against the
        # same captured golden PNGs EasyGL's own tests use.
        # WORKING_DIRECTORY is set to the repo root (mirrors EasyGLTests.cmake's own identical
        # requirement for every CompareGoldenImage-using test) since these 3 reused sources
        # reference their golden PNGs by a path relative to the repo root
        # (examples/golden/...), not to ctest's default CWD (the build directory).
        cna_vulkan_test(cna_test_vulkan_pbreffect_golden
                        examples/easygl_pbreffect_golden_test.cpp)
        cna_register_backend_test(NAME Vulkan_PbrEffect_Golden COMMAND cna_test_vulkan_pbreffect_golden
            TIMEOUT 30 WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_vulkan_test(cna_test_vulkan_skinnedpbreffect_golden
                        examples/easygl_skinnedpbreffect_golden_test.cpp)
        cna_register_backend_test(NAME Vulkan_SkinnedPbrEffect_Golden COMMAND cna_test_vulkan_skinnedpbreffect_golden
            TIMEOUT 30 WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_vulkan_test(cna_test_vulkan_skinnedeffect_vertexcolor_reused
                        examples/easygl_skinnedeffect_vertexcolor_test.cpp)
        cna_register_backend_test(NAME Vulkan_SkinnedEffect_VertexColor_Reused COMMAND cna_test_vulkan_skinnedeffect_vertexcolor_reused
            TIMEOUT 30 WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
        # REMED-GFX-091: this two-draw reused-pipeline case is the minimal validation reproducer.
        # Pixel output alone stays correct, so make the exact Vulkan validity failure test-fatal.
        set_tests_properties(Vulkan_SkinnedEffect_VertexColor_Reused PROPERTIES
            FAIL_REGULAR_EXPRESSION "VUID-vkCmdDraw-None-08608")

        # Vulkan-native counterparts (this backend's own established Game-subclass/check()
        # pattern, e.g. vulkan_basiceffect_specular_test.cpp/vulkan_skinnedeffect_specular_test.cpp)
        # with expected pixel values independently hand-derived from the exact glTF metallic-
        # roughness BRDF formula (see each test's own header comment for the Python re-derivation),
        # not just captured-and-pasted from a single run.
        cna_vulkan_test(cna_test_vulkan_pbreffect_handderived
                        examples/vulkan_pbreffect_handderived_test.cpp)
        cna_register_backend_test(NAME Vulkan_PbrEffect_HandDerived COMMAND cna_test_vulkan_pbreffect_handderived
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_vulkan_test(cna_test_vulkan_skinnedeffect_vertexcolor
                        examples/vulkan_skinnedeffect_vertexcolor_test.cpp)
        cna_register_backend_test(NAME Vulkan_SkinnedEffect_VertexColor COMMAND cna_test_vulkan_skinnedeffect_vertexcolor
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-072: SpriteBatch clip space must be built from the active GraphicsDevice.Viewport
        # (Viewport.Width/Height), not the full target -- a custom sub-Viewport made sprites
        # viewport-relative in XNA/FNA (D3D11-correct) but the Vulkan sprite push-constant
        # viewportSize was fed the full target/virtual dims, squishing the sprite into the sub-region.
        cna_vulkan_test(cna_test_vulkan_spritebatch_custom_viewport
                        examples/spritebatch_custom_viewport_test.cpp)
        cna_register_backend_test(NAME Vulkan_SpriteBatch_CustomViewport COMMAND cna_test_vulkan_spritebatch_custom_viewport
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-072 (Phase 9): two SpriteBatch batches with DIFFERENT viewports in one frame --
        # each batch must use the viewport active for its own Begin/End (per-batch snapshot).
        cna_vulkan_test(cna_test_vulkan_spritebatch_viewport_switch
                        examples/spritebatch_viewport_switch_test.cpp)
        cna_register_backend_test(NAME Vulkan_SpriteBatch_ViewportSwitch COMMAND cna_test_vulkan_spritebatch_viewport_switch
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-074: RenderTarget2D::GetData() now flushes deferred sprite work into the target
        # and reads it back, so the RT-readback variant of the GFX-072 conformance test (previously
        # routed through the backbuffer path on Vulkan) runs directly here as an additional oracle.
        cna_vulkan_test(cna_test_vulkan_spritebatch_custom_viewport_rt
                        examples/spritebatch_custom_viewport_rt_test.cpp)
        cna_register_backend_test(NAME Vulkan_SpriteBatch_CustomViewport_RT COMMAND cna_test_vulkan_spritebatch_custom_viewport_rt
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-074: GetData-before-Present visibility + destroy-before-Present lifetime safety
        # for deferred RenderTarget2D work (sprite + 3D), multiple targets/batches, and no double
        # replay. The destroy-before-Present scene is the UAF regression (run under ASan).
        cna_vulkan_test(cna_test_vulkan_rendertarget_getdata_lifetime
                        examples/vulkan_rendertarget_getdata_lifetime_test.cpp)
        cna_register_backend_test(NAME Vulkan_RenderTarget_GetDataLifetime COMMAND cna_test_vulkan_rendertarget_getdata_lifetime
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-075: deferred SOURCE-resource lifetime safety -- a Texture2D / TextureCube /
        # RenderTarget2D-as-sampler / custom Effect / OcclusionQuery destroyed after queuing a
        # deferred draw but before the Present/GetData record consumes it must keep its borrowed
        # VkImageView / VkPipeline / VkQueryPool alive (retirement queue) without dropping or
        # altering the already-issued draw. The destroy-before-flush scenes are the UAF regression
        # (run under ASan; validation catches destroyed-object-in-use VUIDs).
        cna_vulkan_test(cna_test_vulkan_deferred_resource_lifetime
                        examples/vulkan_deferred_resource_lifetime_test.cpp)
        cna_register_backend_test(NAME Vulkan_DeferredResourceLifetime COMMAND cna_test_vulkan_deferred_resource_lifetime
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # REMED-GFX-076: effect descriptor-set caches must key resource IDENTITY, not a recyclable
        # VkImageView handle value. A cached set is evicted the instant its sampled source is
        # destroyed, so a later texture reusing the freed handle value cannot be handed a stale set
        # sampling the destroyed image. Deterministic (reads backend cache introspection) + a
        # best-effort real-handle-reuse scene + a memory-bounded stress loop.
        cna_vulkan_test(cna_test_vulkan_effect_descriptor_cache_identity
                        examples/vulkan_effect_descriptor_cache_identity_test.cpp)
        cna_register_backend_test(NAME Vulkan_EffectDescriptorCacheIdentity COMMAND cna_test_vulkan_effect_descriptor_cache_identity
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")



    endif()
