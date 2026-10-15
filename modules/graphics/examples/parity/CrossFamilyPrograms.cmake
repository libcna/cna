# RRC-046: programs that held an XNA behaviour on exactly one surviving implementation family after
# the 2026-10-13 retirement, each given an independent second family. One list; each family's
# examples CMakeLists calls cna_register_cross_family_programs(FAMILY <its identity> ...) and builds
# exactly the entries naming it, registered as <TEST_PREFIX>_CrossFamily_<NAME>.
#
# Where they came from:
#   * the EasyGL-only block: the shared-corpus fixtures cmake/DirectXParityTests.cmake also ran as
#     DirectX11_<NAME> until the DIRECTX11 retirement (def6f7747), plus the public programs the
#     VULKAN suite registered beside EasyGL. They gain SDL_GPU, a different implementation.
#   * the reverse block: programs moved out of the VULKAN suite (RRC-030) or kept by one GPU family
#     (SDL_GPU, WEBGPU or DIRECTX9 alone). They gain EasyGL, and SDL_GPU where DIRECTX9 -- which
#     cannot run on the Linux hosts these suites run on -- was the only family.
#
# Deliberately absent, each for a reason of its own:
#   * easygl_graphicsdevicemanager_vsync_test.cpp reads the GL swap interval directly, and
#     shader_effect_reflection_contract_test.cpp asserts EasyGL's GLSL reflection -- EasyGL
#     implementation tests, not shared contracts.
#   * easygl_mrt_test.cpp and sampler_lod_addressw_contract_test.cpp drive their contract through
#     EasyGL-dialect ShaderEffect sources (their other branch was the retired DIRECTX11's HLSL).
#     SDL_GPU holds the same behaviour in its own dialect: sdlgpu_mrt_test.cpp and
#     SdlGpu_Conformance_cube_mrt_binding for MRT, SdlGpu_Parity_sampler_lod_bias and
#     _sampler_max_mip_level for the mip fields, shader_effect_per_unit_sampler_test.cpp for W.
#   * shader_effect_per_unit_sampler_test.cpp feeds precompiled SPIR-V, which only SDL_GPU consumes;
#     on EasyGL the effect does not compile and a leg would pass on an undrawn pixel.
#   * spritebatch_presentation_test.cpp (FULLSCREEN-001) reads CNA's internal presentation path and
#     fails most legs on EasyGL (measured 2026-10-14, not investigated); its second family is
#     SDL_GPU, where it passes.
#
# A program that a family cannot execute for a stated capability reason exits 77 itself; an entry
# is never dropped from a family because it fails there -- a failure is a defect, recorded in
# handoff.md §5.1.

set(CNA_CROSS_FAMILY_PROGRAM_DIR "${CMAKE_CURRENT_LIST_DIR}")

#   BUILDER      the family's "add an example executable" macro, called as <BUILDER>(<target> <src>).
#   FAMILY       the identity this call registers for: EASYGL or SDL_GPU.
#   TEST_PREFIX  prefix of the CTest name, <TEST_PREFIX>_CrossFamily_<NAME>.
#   LABELS, ENVIRONMENT, FAIL_REGULAR_EXPRESSION as for cna_register_parity_fixtures().
function(cna_register_cross_family_programs)
    cmake_parse_arguments(P "" "BUILDER;FAMILY;TEST_PREFIX"
        "LABELS;ENVIRONMENT;FAIL_REGULAR_EXPRESSION" ${ARGN})
    if(P_UNPARSED_ARGUMENTS OR NOT P_BUILDER OR NOT P_FAMILY OR NOT P_TEST_PREFIX)
        message(FATAL_ERROR
            "cna_register_cross_family_programs: BUILDER, FAMILY and TEST_PREFIX are required "
            "(unparsed: ${P_UNPARSED_ARGUMENTS})")
    endif()
    string(TOLOWER "${P_TEST_PREFIX}" _cna_cf_suffix)

    macro(cna_cross_family_program)
        cmake_parse_arguments(F "SOURCE_ROOT_WORKING_DIRECTORY" "NAME;SOURCE;TIMEOUT" "FAMILIES"
            ${ARGN})
        if(P_FAMILY IN_LIST F_FAMILIES)
            set(_cna_cf_source "${CNA_SOURCE_DIR}/modules/${F_SOURCE}")
            if(NOT EXISTS "${_cna_cf_source}")
                message(FATAL_ERROR
                    "cna_register_cross_family_programs: ${F_NAME} names a missing source "
                    "${_cna_cf_source} (modules/graphics/examples/parity/CrossFamilyPrograms.cmake)")
            endif()
            string(TOLOWER "${F_NAME}" _cna_cf_lower)
            set(_cna_cf_target "cna_crossfamily_${_cna_cf_lower}_${_cna_cf_suffix}")
            cmake_language(CALL ${P_BUILDER} ${_cna_cf_target} "${_cna_cf_source}")
            target_include_directories(${_cna_cf_target} PRIVATE
                "${CNA_CROSS_FAMILY_PROGRAM_DIR}/.." "${CNA_SOURCE_DIR}/tests/support")
            set(_cna_cf_timeout "${F_TIMEOUT}")
            if(NOT _cna_cf_timeout)
                set(_cna_cf_timeout 60)
            endif()
            set(_cna_cf_test "${P_TEST_PREFIX}_CrossFamily_${F_NAME}")
            set(_cna_cf_args NAME ${_cna_cf_test} COMMAND ${_cna_cf_target}
                TIMEOUT ${_cna_cf_timeout} LABELS ${P_LABELS} CrossFamily
                ENVIRONMENT ${P_ENVIRONMENT})
            if(F_SOURCE_ROOT_WORKING_DIRECTORY)
                # The golden-image programs read examples/golden/*.png relative to the checkout.
                list(APPEND _cna_cf_args WORKING_DIRECTORY "${CNA_SOURCE_DIR}")
            endif()
            cna_register_renderer_test(${_cna_cf_args})
            if(P_FAIL_REGULAR_EXPRESSION)
                set_tests_properties(${_cna_cf_test} PROPERTIES
                    FAIL_REGULAR_EXPRESSION "${P_FAIL_REGULAR_EXPRESSION}")
            endif()
        endif()
    endmacro()

    # ---- EasyGL-only after the retirement: a second family, SDL_GPU ---------------------------
    cna_cross_family_program(NAME BlendState_Additive FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_blendstate_additive_test.cpp")
    cna_cross_family_program(NAME BlendState_NonPremultiplied FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_blendstate_nonpremultiplied_test.cpp")
    cna_cross_family_program(NAME BlendState_SeparateFactors FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_blendstate_separate_factors_test.cpp")
    cna_cross_family_program(NAME BlendState_SeparateFunctions FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_blendstate_separate_functions_test.cpp")
    cna_cross_family_program(NAME ColorWriteChannels FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_colorwritechannels_test.cpp")
    cna_cross_family_program(NAME DepthStencilState_CompareFunction FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_depthstencilstate_compare_function_test.cpp")
    cna_cross_family_program(NAME DepthStencilState_WriteEnable FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_depthstencilstate_write_enable_test.cpp")
    cna_cross_family_program(NAME GraphicsDevice_ClearStencil FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_graphicsdevice_clear_stencil_test.cpp")
    cna_cross_family_program(NAME GraphicsDevice_ClearDepth FAMILIES SDL_GPU
        SOURCE "graphics/examples/graphicsdevice_clear_depth_test.cpp")
    cna_cross_family_program(NAME RasterizerState_CullModeCamera FAMILIES SDL_GPU
        SOURCE "graphics/examples/rasterizerstate_cullmode_camera_test.cpp")
    cna_cross_family_program(NAME RasterizerState_CullModeIndexedBasicEffect FAMILIES SDL_GPU
        SOURCE "graphics/examples/rasterizerstate_cullmode_indexed_basiceffect_test.cpp")
    cna_cross_family_program(NAME BasicEffect_Properties FAMILIES SDL_GPU
        SOURCE "graphics/examples/basic_effect_test.cpp")
    cna_cross_family_program(NAME BasicEffect_Golden FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_basiceffect_golden_test.cpp" SOURCE_ROOT_WORKING_DIRECTORY)
    cna_cross_family_program(NAME BasicEffect_PositionNormal FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_basiceffect_position_normal_test.cpp")
    cna_cross_family_program(NAME BasicEffect_MultiLightEmissive FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_basiceffect_multilight_emissive_test.cpp")
    cna_cross_family_program(NAME BasicEffect_Specular FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_basiceffect_specular_test.cpp")
    cna_cross_family_program(NAME BasicEffect_PreferPerPixelLighting FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_basiceffect_preferperpixellighting_test.cpp")
    cna_cross_family_program(NAME BasicEffect_VertexColorDisabled FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_basiceffect_vertexcolor_disabled_test.cpp")
    cna_cross_family_program(NAME BasicEffect_VertexColorEnabled FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_basiceffect_vertexcolor_enabled_test.cpp")
    cna_cross_family_program(NAME BasicEffect_TextureEnabled FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_basiceffect_texture_enabled_test.cpp")
    cna_cross_family_program(NAME BasicEffect_TextureVertexColorEnabled FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_basiceffect_texture_vertexcolor_enabled_test.cpp")
    cna_cross_family_program(NAME BasicEffect_OneLight FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_basiceffect_one_light_test.cpp")
    cna_cross_family_program(NAME BasicEffect_Emissive FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_basiceffect_emissive_test.cpp")
    cna_cross_family_program(NAME BasicEffect_Combined FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_basiceffect_combined_test.cpp")
    cna_cross_family_program(NAME BasicEffect_DefaultLighting FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_basiceffect_default_lighting_test.cpp")
    cna_cross_family_program(NAME BasicEffect_Fog FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_basiceffect_fog_test.cpp")
    cna_cross_family_program(NAME BasicEffect_WorldScalePrecision FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_basiceffect_world_scale_precision_test.cpp")
    cna_cross_family_program(NAME BasicEffect_LitVertexColor FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_basiceffect_lit_vertex_color_test.cpp")
    cna_cross_family_program(NAME AlphaTestEffect_Properties FAMILIES SDL_GPU
        SOURCE "graphics/examples/alpha_test_effect_test.cpp")
    cna_cross_family_program(NAME AlphaTestEffect_AlphaCutout FAMILIES SDL_GPU
        SOURCE "graphics/examples/alpha_test_integration_test.cpp")
    cna_cross_family_program(NAME AlphaTestEffect_Golden FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_alphatesteffect_golden_test.cpp" SOURCE_ROOT_WORKING_DIRECTORY)
    cna_cross_family_program(NAME AlphaTestEffect_Modes FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_alphatest_modes_test.cpp")
    cna_cross_family_program(NAME AlphaTestEffect_CompareFunctionSweep FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_alphatest_comparefunction_sweep_test.cpp")
    cna_cross_family_program(NAME AlphaTestEffect_VertexColorDiffuse FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_alphatest_vertexcolor_diffuse_test.cpp")
    cna_cross_family_program(NAME AlphaTestEffect_NullTexture FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_alphatest_null_texture_test.cpp")
    cna_cross_family_program(NAME EnvironmentMapAmountZero FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_environmentmapeffect_amount_zero_test.cpp")
    cna_cross_family_program(NAME SkinnedEffect_WorldNormal FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_skinnedeffect_world_normal_test.cpp")
    cna_cross_family_program(NAME AlphaTest_Fog FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_alphatest_fog_test.cpp")
    cna_cross_family_program(NAME DualTextureEffect_Fog FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_dualtextureeffect_fog_test.cpp")
    cna_cross_family_program(NAME DualTextureEffect_Golden FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_dualtextureeffect_golden_test.cpp" SOURCE_ROOT_WORKING_DIRECTORY)
    cna_cross_family_program(NAME DualTextureEffect_Blend FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_dual_texture_test.cpp")
    cna_cross_family_program(NAME DualTextureEffect_Doubling FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_dualtextureeffect_doubling_test.cpp")
    cna_cross_family_program(NAME DualTextureEffect_Alpha FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_dualtextureeffect_alpha_test.cpp")
    cna_cross_family_program(NAME DualTextureEffect_NullTexture0 FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_dualtextureeffect_null_texture0_test.cpp")
    cna_cross_family_program(NAME DualTextureEffect_NullTexture2 FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_dualtextureeffect_null_texture2_test.cpp")
    cna_cross_family_program(NAME DualTextureEffect_Combined FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_dualtextureeffect_combined_test.cpp")
    cna_cross_family_program(NAME DualTextureEffect_Integration FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_dualtexture_test.cpp")
    cna_cross_family_program(NAME DualTextureEffect_IndependentUV FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_dualtextureeffect_independent_uv_test.cpp")
    cna_cross_family_program(NAME DualTextureEffect_VertexColor FAMILIES SDL_GPU
        SOURCE "graphics/examples/dualtextureeffect_vertexcolor_test.cpp")
    cna_cross_family_program(NAME EnvironmentMapEffect_Fog FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_environmentmapeffect_fog_test.cpp")
    cna_cross_family_program(NAME EnvironmentMapEffect_Golden FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_environmentmapeffect_golden_test.cpp" SOURCE_ROOT_WORKING_DIRECTORY)
    cna_cross_family_program(NAME EnvironmentMapEffect_Readback FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_env_map_test.cpp")
    cna_cross_family_program(NAME EnvironmentMapEffect_AmountOne FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_environmentmapeffect_amount_one_test.cpp")
    cna_cross_family_program(NAME EnvironmentMapEffect_Specular FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_environmentmapeffect_specular_test.cpp")
    cna_cross_family_program(NAME EnvironmentMapEffect_AlphaScaledLerp FAMILIES SDL_GPU
        SOURCE "graphics/examples/environmentmapeffect_alphascaledlerp_test.cpp")
    cna_cross_family_program(NAME EnvironmentMapEffect_Fresnel FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_environmentmapeffect_fresnel_test.cpp")
    cna_cross_family_program(NAME EnvironmentMapEffect_Fresnel_Gradient FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_environmentmapeffect_fresnel_gradient_test.cpp")
    cna_cross_family_program(NAME EnvironmentMapEffect_EyePosition FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_environmentmapeffect_eyeposition_test.cpp")
    cna_cross_family_program(NAME EnvironmentMapEffect_WorldTransform FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_environmentmapeffect_worldtransform_test.cpp")
    cna_cross_family_program(NAME EnvironmentMapEffect_Combined FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_environmentmapeffect_combined_test.cpp")
    cna_cross_family_program(NAME EnvironmentMapEffect_MultiLight FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_environmentmapeffect_multilight_test.cpp")
    cna_cross_family_program(NAME SkinnedEffect_Fog FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_skinnedeffect_fog_test.cpp")
    cna_cross_family_program(NAME SkinnedEffect_Properties FAMILIES SDL_GPU
        SOURCE "graphics/examples/skinned_effect_test.cpp")
    cna_cross_family_program(NAME SkinnedEffect_Golden FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_skinnedeffect_golden_test.cpp" SOURCE_ROOT_WORKING_DIRECTORY)
    cna_cross_family_program(NAME SkinnedEffect_VertexColor FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_skinnedeffect_vertexcolor_test.cpp" SOURCE_ROOT_WORKING_DIRECTORY)
    cna_cross_family_program(NAME SkinnedEffect_BoneDeformation FAMILIES SDL_GPU
        SOURCE "graphics/examples/skinned_effect_integration_test.cpp")
    cna_cross_family_program(NAME SkinnedEffect_MultiLight FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_skinnedeffect_multilight_test.cpp")
    cna_cross_family_program(NAME SkinnedEffect_Specular FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_skinnedeffect_specular_test.cpp")
    cna_cross_family_program(NAME SkinnedEffect_PreferPerPixelLighting FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_skinnedeffect_preferperpixellighting_test.cpp")
    cna_cross_family_program(NAME SkinnedEffect_WeightsPerVertex FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_skinnedeffect_weightspervertex_test.cpp")
    cna_cross_family_program(NAME SkinnedEffect_IdentityBones FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_skinnedeffect_identity_bones_test.cpp")
    cna_cross_family_program(NAME SkinnedEffect_TranslationBone FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_skinnedeffect_translation_bone_test.cpp")
    cna_cross_family_program(NAME SkinnedEffect_TwoBoneBlend FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_skinnedeffect_twobone_blend_test.cpp")
    cna_cross_family_program(NAME SkinnedEffect_Combined FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_skinnedeffect_combined_test.cpp")
    cna_cross_family_program(NAME SkinnedEffect_BoneCount FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_skinned_effect_bones_test.cpp")
    cna_cross_family_program(NAME SkinnedEffect_Vector4BoneIndices FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_skinnedeffect_vector4_bone_indices_test.cpp")
    cna_cross_family_program(NAME Effect_Clone FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_effect_clone_test.cpp")
    cna_cross_family_program(NAME Effect_CurrentTechnique FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_effect_current_technique_test.cpp")
    cna_cross_family_program(NAME SpriteFont_Properties FAMILIES SDL_GPU
        SOURCE "graphics/examples/sprite_font_test.cpp")
    cna_cross_family_program(NAME Texture2D_PartialRect_RoundTrip FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_texture2d_partial_rect_test.cpp")
    cna_cross_family_program(NAME Texture2D_Mip_RoundTrip FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_texture2d_mip_test.cpp")
    cna_cross_family_program(NAME Texture3D_Slices_RoundTrip FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_texture3d_slices_test.cpp")
    cna_cross_family_program(NAME Texture3D_Mip_RoundTrip FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_texture3d_mip_test.cpp")
    cna_cross_family_program(NAME Texture3D_PartialBox_RoundTrip FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_texture3d_partial_box_test.cpp")
    cna_cross_family_program(NAME Texture3D_PartialBox_Readback FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_texture3d_partial_box_readback_test.cpp")
    cna_cross_family_program(NAME TextureCube_ContentLoad FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_texturecube_content_load_test.cpp")
    cna_cross_family_program(NAME TextureCube_Faces_RoundTrip FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_texturecube_faces_test.cpp")
    cna_cross_family_program(NAME TextureCube_Mip_RoundTrip FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_texturecube_mip_test.cpp")
    cna_cross_family_program(NAME TextureCube_PartialRect_RoundTrip FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_texturecube_partial_rect_test.cpp")
    cna_cross_family_program(NAME TextureAddressMode FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_texture_address_mode_test.cpp")
    cna_cross_family_program(NAME TextureAddressMode_Mirror FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_texture_address_mode_mirror_test.cpp")
    cna_cross_family_program(NAME SamplerState_DualTextureEffect FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_sampler_state_effect_test.cpp")
    cna_cross_family_program(NAME TextureAddressMode_Clamp_DualTextureEffect FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_texture_address_mode_clamp_effect_test.cpp")
    cna_cross_family_program(NAME TextureAddressMode_Mirror_DualTextureEffect FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_texture_address_mode_mirror_effect_test.cpp")
    cna_cross_family_program(NAME Texture2D_AnisotropicSingleLevel FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_texture2d_anisotropic_singlelevel_test.cpp")
    cna_cross_family_program(NAME TextureAnisotropic_DualTextureEffect FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_texture_anisotropic_effect_test.cpp")
    cna_cross_family_program(NAME TextureFilter_PointVsLinear FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_texture_filter_point_vs_linear_test.cpp")
    cna_cross_family_program(NAME TextureFilter_Linear_Golden FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_texture_filter_linear_golden_test.cpp" SOURCE_ROOT_WORKING_DIRECTORY)
    cna_cross_family_program(NAME TexturedQuad_Readback FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_textured_quad_test.cpp")
    cna_cross_family_program(NAME TextureMipFilter_DualTextureEffect FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_texture_mip_filter_effect_test.cpp" TIMEOUT 300)
    cna_cross_family_program(NAME RenderTarget_SurfaceFormat FAMILIES SDL_GPU
        SOURCE "graphics/examples/rendertarget_surface_format_contract_test.cpp" TIMEOUT 300)
    cna_cross_family_program(NAME RenderTarget2D_Readback FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_render_target_test.cpp" TIMEOUT 300)
    cna_cross_family_program(NAME RenderTarget2D_Golden FAMILIES SDL_GPU
        SOURCE "graphics/examples/rendertarget2d_golden_test.cpp" TIMEOUT 300)
    cna_cross_family_program(NAME RenderTarget2D_DepthBuffer FAMILIES SDL_GPU
        SOURCE "graphics/examples/rendertarget2d_depth_test.cpp" TIMEOUT 300)
    cna_cross_family_program(NAME RenderTarget2D_MipComplete FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_rendertarget2d_mip_test.cpp" TIMEOUT 300)
    cna_cross_family_program(NAME RenderTarget_Usage FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_render_target_usage_test.cpp" TIMEOUT 300)
    cna_cross_family_program(NAME RenderTarget_Roundtrip FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_rt_roundtrip_test.cpp" TIMEOUT 300)
    cna_cross_family_program(NAME GraphicsDevice_DefaultStateOcclusion FAMILIES SDL_GPU
        SOURCE "graphics/examples/graphicsdevice_default_state_occlusion_test.cpp" TIMEOUT 300)
    cna_cross_family_program(NAME RenderTarget_ActiveMsaaReadback FAMILIES SDL_GPU
        SOURCE "graphics/examples/rendertarget_active_msaa_readback_test.cpp" TIMEOUT 300)
    cna_cross_family_program(NAME Dxt1_FromStream FAMILIES SDL_GPU
        SOURCE "graphics/examples/dxt1_texture_test.cpp" TIMEOUT 300)
    cna_cross_family_program(NAME SurfaceFormat_Throws FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_surface_format_throws_test.cpp")
    cna_cross_family_program(NAME BlendState_BlendFactor FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_blendstate_blendfactor_test.cpp")
    cna_cross_family_program(NAME RenderTarget2D_Msaa FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_rendertarget2d_msaa_test.cpp")
    cna_cross_family_program(NAME DepthBias FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_depth_bias_test.cpp")
    cna_cross_family_program(NAME DrawUserPrimitives_VPC FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_draw_user_primitives_vpc_test.cpp")
    cna_cross_family_program(NAME DrawUserPrimitives_CustomVD FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_draw_user_primitives_custom_test.cpp")
    cna_cross_family_program(NAME DrawUserIndexedPrimitives_VPC FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_draw_user_indexed_primitives_vpc_test.cpp")
    cna_cross_family_program(NAME DrawUserIndexedPrimitives_32 FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_draw_user_indexed_primitives_32_test.cpp")
    cna_cross_family_program(NAME ViewportState FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_viewport_state_test.cpp")
    cna_cross_family_program(NAME Viewport_Subregion FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_viewport_subregion_test.cpp")
    cna_cross_family_program(NAME Scissor FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_scissor_test.cpp")
    cna_cross_family_program(NAME ClearOverloads FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_clear_overloads_test.cpp")
    cna_cross_family_program(NAME ContextRecovery_Model FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_gltf_context_loss_test.cpp" TIMEOUT 900 SOURCE_ROOT_WORKING_DIRECTORY)
    cna_cross_family_program(NAME PresentationModeContract FAMILIES SDL_GPU
        SOURCE "graphics/examples/presentation_mode_contract_test.cpp" TIMEOUT 300)
    cna_cross_family_program(NAME SwapIntervalForwarding FAMILIES SDL_GPU
        SOURCE "graphics/examples/swap_interval_forwarding_contract_test.cpp")
    cna_cross_family_program(NAME MSAA_4x_Readback FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_msaa_test.cpp" TIMEOUT 300)
    cna_cross_family_program(NAME MsaaChange FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_msaa_change_test.cpp" TIMEOUT 600)
    cna_cross_family_program(NAME RealWindowResize FAMILIES SDL_GPU
        SOURCE "renderers/easygl/examples/easygl_real_window_resize_test.cpp" TIMEOUT 300)
    cna_cross_family_program(NAME ViewportResetAfterResize FAMILIES SDL_GPU
        SOURCE "graphics/examples/viewport_reset_after_resize_test.cpp" TIMEOUT 300)
    cna_cross_family_program(NAME DepthBiasMagnitudeContract FAMILIES SDL_GPU
        SOURCE "graphics/examples/depth_bias_magnitude_contract_test.cpp" TIMEOUT 30)
    cna_cross_family_program(NAME EnvironmentMapEffect_RenderTargetCubeOrientation FAMILIES SDL_GPU
        SOURCE "graphics/examples/environmentmapeffect_rendertargetcube_orientation_test.cpp")
    cna_cross_family_program(NAME FullscreenSpriteThen3D FAMILIES SDL_GPU
        SOURCE "graphics/examples/fullscreen_spritebatch_then_3d_test.cpp")
    cna_cross_family_program(NAME InstancedTexturedDraw FAMILIES SDL_GPU
        SOURCE "graphics/examples/instanced_textured_draw_test.cpp")
    cna_cross_family_program(NAME RenderTargetPreserveAcrossResize FAMILIES SDL_GPU
        SOURCE "graphics/examples/rendertarget_preserve_across_resize_test.cpp")
    cna_cross_family_program(NAME SpriteBatch_DepthStencilState FAMILIES SDL_GPU
        SOURCE "graphics/examples/spritebatch_depthstencil_state_test.cpp")
    cna_cross_family_program(NAME SpriteBatch_SortModeSemantics FAMILIES SDL_GPU
        SOURCE "graphics/examples/spritebatch_sort_mode_semantics_test.cpp")
    cna_cross_family_program(NAME Texture2D_SaveAsRenderTarget FAMILIES SDL_GPU
        SOURCE "graphics/examples/texture2d_saveas_rendertarget_test.cpp")
    cna_cross_family_program(NAME ZeroLengthBuffer FAMILIES SDL_GPU
        SOURCE "graphics/examples/zero_length_buffer_test.cpp")
    cna_cross_family_program(NAME SkinnedModelAttachPartIntegration FAMILIES SDL_GPU
        SOURCE "graphics/examples/skinned_model_attach_part_integration_test.cpp" TIMEOUT 30)
    cna_cross_family_program(NAME FillMode FAMILIES SDL_GPU
        SOURCE "graphics/examples/fill_mode_test.cpp" TIMEOUT 30)
    cna_cross_family_program(NAME PbrTextureSlotMapping FAMILIES SDL_GPU
        SOURCE "graphics/examples/pbr_texture_slot_mapping_test.cpp" TIMEOUT 30 SOURCE_ROOT_WORKING_DIRECTORY)

    # ---- one other GPU family only: EasyGL, and SDL_GPU where DIRECTX9 was alone -------------
    cna_cross_family_program(NAME AlphaTestEffect_FogContract FAMILIES EASYGL SDL_GPU
        SOURCE "graphics/examples/alphatest_fog_test.cpp")
    cna_cross_family_program(NAME DualTextureEffect_FogContract FAMILIES EASYGL SDL_GPU
        SOURCE "graphics/examples/dualtextureeffect_fog_test.cpp")
    cna_cross_family_program(NAME EnvironmentMapEffect_FogContract FAMILIES EASYGL SDL_GPU
        SOURCE "graphics/examples/environmentmapeffect_fog_test.cpp")
    cna_cross_family_program(NAME SkinnedEffect_FogContract FAMILIES EASYGL SDL_GPU
        SOURCE "graphics/examples/skinnedeffect_fog_test.cpp")
    cna_cross_family_program(NAME ViewSpaceFogContract FAMILIES EASYGL SDL_GPU
        SOURCE "graphics/examples/viewspace_fog_test.cpp")
    cna_cross_family_program(NAME SpriteBatch_Presentation FAMILIES SDL_GPU
        SOURCE "graphics/examples/spritebatch_presentation_test.cpp" TIMEOUT 30)
    cna_cross_family_program(NAME BlendFactor_DeferredDraw FAMILIES EASYGL SDL_GPU
        SOURCE "graphics/examples/blendfactor_deferred_draw_test.cpp" TIMEOUT 30)
    cna_cross_family_program(NAME BasicEffect_DiffuseColorClamp FAMILIES EASYGL
        SOURCE "graphics/examples/basiceffect_diffuse_color_clamp_test.cpp")
    cna_cross_family_program(NAME EnvironmentMapEffect_AmountClamp FAMILIES EASYGL
        SOURCE "graphics/examples/environmentmapeffect_amount_clamp_test.cpp")
endfunction()
