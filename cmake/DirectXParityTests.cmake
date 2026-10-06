include_guard(GLOBAL)

# DX-242: one authoritative inventory of the shared-corpus CTest fixtures the DIRECTX11 renderer
# runs. Renderer-local smoke/gate programs stay in modules/renderers/directx11/examples; every
# fixture below is registered as DirectX11_<NAME>, in declaration order.
function(cna_register_d3d_parity_tests)
    if(NOT CNA_GRAPHICS_RENDERER STREQUAL "DIRECTX11")
        message(FATAL_ERROR "cna_register_d3d_parity_tests is only valid for DIRECTX11")
    endif()

    macro(cna_d3d_parity_fixture)
        cmake_parse_arguments(F
            "REQUIRES_SDL"
            "NAME;TARGET;SOURCE;TIMEOUT;ENVIRONMENT;WORKING_DIRECTORY"
            ""
            ${ARGN})

        if(NOT F_NAME OR NOT F_TARGET OR NOT F_SOURCE)
            message(FATAL_ERROR "Every DirectX parity fixture needs NAME, TARGET, and SOURCE")
        endif()
        if(DEFINED _cna_d3d_fixture_${F_NAME}_LISTED)
            message(FATAL_ERROR "DirectX parity fixture ${F_NAME} is listed more than once")
        endif()
        set(_cna_d3d_fixture_${F_NAME}_LISTED TRUE)

        # plans/plan_win32_native_validation.md WINNATIVE-0015: a fixture that reaches into an
        # SDL_Window to cross-check what CNA reported needs SDL to exist. With CNA_ENABLE_SDL=OFF
        # it cannot be built -- which is a boundary to state, not a build to break.
        if(F_REQUIRES_SDL AND NOT TARGET SDL3::SDL3)
            message(STATUS
                "CNA: D3D parity fixture ${F_NAME} needs SDL3 and CNA_ENABLE_SDL is off -- skipped")
        else()
            # plans/plan_win32_native_validation.md WINNATIVE-0009: this inventory names sources
            # that live in other modules, so a commit that MOVES one of them and does not come back
            # here leaves a dangling path. Without this check the generate step fails much later,
            # with "No SOURCES given to target: cna_test_directx11_<something>" and no mention of
            # the file or of this file. A SOURCE is either absolute (the cross-module ones, which
            # are the ones that drift) or relative to the renderer's own examples directory, which
            # is where this function is called from.
            if(IS_ABSOLUTE "${F_SOURCE}")
                set(_cna_d3d_source_path "${F_SOURCE}")
            else()
                set(_cna_d3d_source_path "${CMAKE_CURRENT_SOURCE_DIR}/${F_SOURCE}")
            endif()
            if(NOT EXISTS "${_cna_d3d_source_path}")
                message(FATAL_ERROR
                    "DirectX parity fixture ${F_NAME} names a source that does not exist:\n"
                    "    ${_cna_d3d_source_path}\n"
                    "Either the file moved and this inventory (cmake/DirectXParityTests.cmake) was "
                    "not updated with it, or the fixture should be removed.")
            endif()

            set(_cna_d3d_target "cna_test_directx11_${F_TARGET}")
            cna_directx11_test(${_cna_d3d_target} "${F_SOURCE}")
            cna_directx11_ctest_command(_cna_d3d_command ${_cna_d3d_target})
            set(_cna_d3d_timeout "${F_TIMEOUT}")
            if(NOT _cna_d3d_timeout)
                set(_cna_d3d_timeout 60)
            endif()
            set(_cna_d3d_registration
                NAME "DirectX11_${F_NAME}"
                COMMAND ${_cna_d3d_command}
                TIMEOUT ${_cna_d3d_timeout}
                LABELS "DIRECTX11")
            if(F_ENVIRONMENT)
                list(APPEND _cna_d3d_registration ENVIRONMENT "${F_ENVIRONMENT}")
            endif()
            if(F_WORKING_DIRECTORY)
                list(APPEND _cna_d3d_registration WORKING_DIRECTORY "${F_WORKING_DIRECTORY}")
            endif()
            cna_register_renderer_test(${_cna_d3d_registration})
        endif()
    endmacro()

    cna_d3d_parity_fixture(
        NAME GraphicsDevice_DepthContract TARGET depth_contract
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/graphicsdevice_depth_contract_test.cpp")
    cna_d3d_parity_fixture(
        NAME Common TARGET common SOURCE directx11_common_test.cpp
        TIMEOUT 30 ENVIRONMENT "CNA_D3D11_SKIP_DXVK_GATE=1")
    cna_d3d_parity_fixture(
        NAME BlendState_Opaque TARGET blendstate_opaque
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_blendstate_opaque_test.cpp")
    cna_d3d_parity_fixture(
        NAME BlendState_AlphaBlend TARGET blendstate_alphablend
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_blendstate_alphablend_test.cpp")
    cna_d3d_parity_fixture(
        NAME BlendState_Additive TARGET blendstate_additive
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_blendstate_additive_test.cpp")
    cna_d3d_parity_fixture(
        NAME BlendState_NonPremultiplied TARGET blendstate_nonpremultiplied
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_blendstate_nonpremultiplied_test.cpp")
    cna_d3d_parity_fixture(
        NAME BlendState_SeparateFactors TARGET blendstate_separate_factors
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_blendstate_separate_factors_test.cpp")
    cna_d3d_parity_fixture(
        NAME BlendState_SeparateFunctions TARGET blendstate_separate_functions
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_blendstate_separate_functions_test.cpp")
    cna_d3d_parity_fixture(
        NAME ColorWriteChannels TARGET colorwritechannels
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_colorwritechannels_test.cpp")
    cna_d3d_parity_fixture(
        NAME AdditiveBlendContract TARGET additive_blend_contract
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/additive_blend_contract_test.cpp"
        TIMEOUT 300)
    cna_d3d_parity_fixture(
        NAME ColorWriteChannels3D TARGET colorwritechannels_3d
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/gfx077_colorwritechannels_3d_test.cpp"
        TIMEOUT 300)
    cna_d3d_parity_fixture(
        NAME DepthStencilState_StencilEnable TARGET depthstencilstate_stencil_enable
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_depthstencilstate_stencil_enable_test.cpp")
    cna_d3d_parity_fixture(
        NAME DepthStencilState_CompareFunction TARGET depthstencilstate_compare_function
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_depthstencilstate_compare_function_test.cpp")
    cna_d3d_parity_fixture(
        NAME DepthStencilState_StencilMask TARGET depthstencilstate_stencil_mask
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_depthstencilstate_stencil_mask_test.cpp")
    cna_d3d_parity_fixture(
        NAME DepthStencilState_StencilOps TARGET depthstencilstate_stencil_ops
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_depthstencilstate_stencil_ops_test.cpp")
    cna_d3d_parity_fixture(
        NAME DepthStencilState_StencilTwoSided TARGET depthstencilstate_stencil_twosided
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_depthstencilstate_stencil_twosided_test.cpp")
    cna_d3d_parity_fixture(
        NAME DepthStencilState_WriteEnable TARGET depthstencilstate_write_enable
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_depthstencilstate_write_enable_test.cpp")
    cna_d3d_parity_fixture(
        NAME GraphicsDevice_ClearStencil TARGET graphicsdevice_clear_stencil
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_graphicsdevice_clear_stencil_test.cpp")
    cna_d3d_parity_fixture(
        NAME GraphicsDevice_ReferenceStencil TARGET graphicsdevice_reference_stencil
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_graphicsdevice_reference_stencil_test.cpp")
    cna_d3d_parity_fixture(
        NAME GraphicsDevice_ClearDepth TARGET graphicsdevice_clear_depth
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/graphicsdevice_clear_depth_test.cpp")
    cna_d3d_parity_fixture(
        NAME RasterizerState_CullMode TARGET rasterizerstate_cullmode
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_rasterizerstate_cullmode_test.cpp")
    cna_d3d_parity_fixture(
        NAME RasterizerState_CullModeCamera TARGET rasterizerstate_cullmode_camera
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/rasterizerstate_cullmode_camera_test.cpp")
    cna_d3d_parity_fixture(
        NAME RasterizerState_CullModeIndexedBasicEffect
        TARGET rasterizerstate_cullmode_indexed_basiceffect
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/rasterizerstate_cullmode_indexed_basiceffect_test.cpp")
    cna_d3d_parity_fixture(
        NAME FrontFaceWinding TARGET frontface_winding
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/frontface_winding_test.cpp"
        TIMEOUT 600)
    cna_d3d_parity_fixture(
        NAME TriangleStripWinding TARGET triangle_strip_winding
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/triangle_strip_winding_test.cpp"
        TIMEOUT 600)
    cna_d3d_parity_fixture(
        NAME BasicEffect_Properties TARGET basiceffect_properties
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/basic_effect_test.cpp")
    cna_d3d_parity_fixture(
        NAME BasicEffect_Golden TARGET basiceffect_golden
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_basiceffect_golden_test.cpp"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    cna_d3d_parity_fixture(
        NAME BasicEffect_PositionNormal TARGET basiceffect_position_normal
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_basiceffect_position_normal_test.cpp")
    cna_d3d_parity_fixture(
        NAME BasicEffect_MultiLightEmissive TARGET basiceffect_multilight_emissive
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_basiceffect_multilight_emissive_test.cpp")
    cna_d3d_parity_fixture(
        NAME BasicEffect_Specular TARGET basiceffect_specular
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_basiceffect_specular_test.cpp")
    cna_d3d_parity_fixture(
        NAME BasicEffect_PreferPerPixelLighting TARGET basiceffect_preferperpixellighting
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_basiceffect_preferperpixellighting_test.cpp")
    cna_d3d_parity_fixture(
        NAME BasicEffect_Combinations TARGET basiceffect_combinations
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_basiceffect_combinations_test.cpp")
    cna_d3d_parity_fixture(
        NAME BasicEffect_VertexColorDisabled TARGET basiceffect_vertexcolor_disabled
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_basiceffect_vertexcolor_disabled_test.cpp")
    cna_d3d_parity_fixture(
        NAME BasicEffect_VertexColorEnabled TARGET basiceffect_vertexcolor_enabled
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_basiceffect_vertexcolor_enabled_test.cpp")
    cna_d3d_parity_fixture(
        NAME BasicEffect_TextureEnabled TARGET basiceffect_texture_enabled
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_basiceffect_texture_enabled_test.cpp")
    cna_d3d_parity_fixture(
        NAME BasicEffect_TextureVertexColorEnabled TARGET basiceffect_texture_vertexcolor_enabled
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_basiceffect_texture_vertexcolor_enabled_test.cpp")
    cna_d3d_parity_fixture(
        NAME BasicEffect_OneLight TARGET basiceffect_one_light
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_basiceffect_one_light_test.cpp")
    cna_d3d_parity_fixture(
        NAME BasicEffect_Emissive TARGET basiceffect_emissive
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_basiceffect_emissive_test.cpp")
    cna_d3d_parity_fixture(
        NAME BasicEffect_Combined TARGET basiceffect_combined
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_basiceffect_combined_test.cpp")
    cna_d3d_parity_fixture(
        NAME BasicEffect_DefaultLighting TARGET basiceffect_default_lighting
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_basiceffect_default_lighting_test.cpp")
    cna_d3d_parity_fixture(
        NAME BasicEffect_Fog TARGET basiceffect_fog
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_basiceffect_fog_test.cpp")
    cna_d3d_parity_fixture(
        NAME BasicEffect_VertexColorClamp TARGET basiceffect_vertex_color_clamp
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_basiceffect_vertex_color_clamp_test.cpp")
    cna_d3d_parity_fixture(
        NAME BasicEffect_WorldScalePrecision TARGET basiceffect_world_scale_precision
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_basiceffect_world_scale_precision_test.cpp")
    cna_d3d_parity_fixture(
        NAME BasicEffect_LitVertexColor TARGET basiceffect_lit_vertex_color
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_basiceffect_lit_vertex_color_test.cpp")
    cna_d3d_parity_fixture(
        NAME AlphaTestEffect_Properties TARGET alphatesteffect_properties
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/alpha_test_effect_test.cpp")
    cna_d3d_parity_fixture(
        NAME AlphaTestEffect_AlphaCutout TARGET alphatesteffect_alpha_cutout
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/alpha_test_integration_test.cpp")
    cna_d3d_parity_fixture(
        NAME AlphaTestEffect_Golden TARGET alphatesteffect_golden
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_alphatesteffect_golden_test.cpp"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    cna_d3d_parity_fixture(
        NAME AlphaTestEffect_Modes TARGET alphatest_modes
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_alphatest_modes_test.cpp")
    cna_d3d_parity_fixture(
        NAME AlphaTestEffect_CompareFunctionSweep TARGET alphatest_comparefunction_sweep
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_alphatest_comparefunction_sweep_test.cpp")
    cna_d3d_parity_fixture(
        NAME AlphaTestEffect_VertexColorDiffuse TARGET alphatest_vertexcolor_diffuse
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_alphatest_vertexcolor_diffuse_test.cpp")
    cna_d3d_parity_fixture(
        NAME AlphaTestEffect_NullTexture TARGET alphatest_null_texture
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_alphatest_null_texture_test.cpp")
    cna_d3d_parity_fixture(
        NAME Pbr_VertexColor TARGET pbr_vertexcolor SOURCE directx11_pbr_vertexcolor_test.cpp)
    cna_d3d_parity_fixture(
        NAME Pbr_SrgbTransfer TARGET pbr_srgb_transfer
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_pbr_srgb_transfer_test.cpp")
    cna_d3d_parity_fixture(
        NAME Pbr_FresnelFactors TARGET pbr_fresnel_factors
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_pbr_fresnel_factors_test.cpp")
    cna_d3d_parity_fixture(
        NAME EnvironmentMapAmountZero TARGET environmentmapeffect_amount_zero
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_environmentmapeffect_amount_zero_test.cpp")
    cna_d3d_parity_fixture(
        NAME SkinnedEffect_WorldNormal TARGET skinnedeffect_world_normal
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_skinnedeffect_world_normal_test.cpp")
    cna_d3d_parity_fixture(
        NAME SkinnedEffect_LightingConformance TARGET skinnedeffect_lighting_conformance
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/skinnedeffect_lighting_conformance_test.cpp")
    cna_d3d_parity_fixture(
        NAME ViewSpaceFog TARGET viewspace_fog
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/vulkan/examples/vulkan_viewspace_fog_test.cpp")
    cna_d3d_parity_fixture(
        NAME AlphaTest_Fog TARGET alphatest_fog
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_alphatest_fog_test.cpp")
    cna_d3d_parity_fixture(
        NAME DualTextureEffect_Fog TARGET dualtextureeffect_fog
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_dualtextureeffect_fog_test.cpp")
    cna_d3d_parity_fixture(
        NAME DualTextureEffect_Golden TARGET dualtextureeffect_golden
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_dualtextureeffect_golden_test.cpp"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    cna_d3d_parity_fixture(
        NAME DualTextureEffect_Blend TARGET dual_texture
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_dual_texture_test.cpp")
    cna_d3d_parity_fixture(
        NAME DualTextureEffect_Doubling TARGET dualtextureeffect_doubling
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_dualtextureeffect_doubling_test.cpp")
    cna_d3d_parity_fixture(
        NAME DualTextureEffect_Alpha TARGET dualtextureeffect_alpha
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_dualtextureeffect_alpha_test.cpp")
    cna_d3d_parity_fixture(
        NAME DualTextureEffect_NullTexture0 TARGET dualtextureeffect_null_texture0
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_dualtextureeffect_null_texture0_test.cpp")
    cna_d3d_parity_fixture(
        NAME DualTextureEffect_NullTexture2 TARGET dualtextureeffect_null_texture2
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_dualtextureeffect_null_texture2_test.cpp")
    cna_d3d_parity_fixture(
        NAME DualTextureEffect_Combined TARGET dualtextureeffect_combined
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_dualtextureeffect_combined_test.cpp")
    cna_d3d_parity_fixture(
        NAME DualTextureEffect_Integration TARGET dualtexture
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_dualtexture_test.cpp")
    cna_d3d_parity_fixture(
        NAME DualTextureEffect_IndependentUV TARGET dualtextureeffect_independent_uv
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_dualtextureeffect_independent_uv_test.cpp")
    cna_d3d_parity_fixture(
        NAME DualTextureEffect_VertexColor TARGET dualtextureeffect_vertexcolor
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/dualtextureeffect_vertexcolor_test.cpp")
    cna_d3d_parity_fixture(
        NAME EnvironmentMapEffect_Fog TARGET environmentmapeffect_fog
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_environmentmapeffect_fog_test.cpp")
    cna_d3d_parity_fixture(
        NAME EnvironmentMapEffect_Golden TARGET environmentmapeffect_golden
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_environmentmapeffect_golden_test.cpp"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    cna_d3d_parity_fixture(
        NAME EnvironmentMapEffect_Readback TARGET env_map
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_env_map_test.cpp")
    cna_d3d_parity_fixture(
        NAME EnvironmentMapEffect_AmountOne TARGET environmentmapeffect_amount_one
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_environmentmapeffect_amount_one_test.cpp")
    cna_d3d_parity_fixture(
        NAME EnvironmentMapEffect_Specular TARGET environmentmapeffect_specular
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_environmentmapeffect_specular_test.cpp")
    cna_d3d_parity_fixture(
        NAME EnvironmentMapEffect_AlphaScaledLerp TARGET environmentmapeffect_alphascaledlerp
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/environmentmapeffect_alphascaledlerp_test.cpp")
    cna_d3d_parity_fixture(
        NAME EnvironmentMapEffect_Fresnel TARGET environmentmapeffect_fresnel
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_environmentmapeffect_fresnel_test.cpp")
    cna_d3d_parity_fixture(
        NAME EnvironmentMapEffect_Fresnel_Gradient TARGET environmentmapeffect_fresnel_gradient
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_environmentmapeffect_fresnel_gradient_test.cpp")
    cna_d3d_parity_fixture(
        NAME EnvironmentMapEffect_EyePosition TARGET environmentmapeffect_eyeposition
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_environmentmapeffect_eyeposition_test.cpp")
    cna_d3d_parity_fixture(
        NAME EnvironmentMapEffect_WorldTransform TARGET environmentmapeffect_worldtransform
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_environmentmapeffect_worldtransform_test.cpp")
    cna_d3d_parity_fixture(
        NAME EnvironmentMapEffect_Combined TARGET environmentmapeffect_combined
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_environmentmapeffect_combined_test.cpp")
    cna_d3d_parity_fixture(
        NAME EnvironmentMapEffect_MultiLight TARGET environmentmapeffect_multilight
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_environmentmapeffect_multilight_test.cpp")
    cna_d3d_parity_fixture(
        NAME SkinnedEffect_Fog TARGET skinnedeffect_fog
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_skinnedeffect_fog_test.cpp")
    cna_d3d_parity_fixture(
        NAME SkinnedEffect_Properties TARGET skinned_effect
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/skinned_effect_test.cpp")
    cna_d3d_parity_fixture(
        NAME SkinnedEffect_Golden TARGET skinnedeffect_golden
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_skinnedeffect_golden_test.cpp"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    cna_d3d_parity_fixture(
        NAME SkinnedEffect_VertexColor TARGET skinnedeffect_vertexcolor
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_skinnedeffect_vertexcolor_test.cpp"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    cna_d3d_parity_fixture(
        NAME SkinnedEffect_BoneDeformation TARGET skinned_integration
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/skinned_effect_integration_test.cpp")
    cna_d3d_parity_fixture(
        NAME SkinnedEffect_MultiLight TARGET skinnedeffect_multilight
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_skinnedeffect_multilight_test.cpp")
    cna_d3d_parity_fixture(
        NAME SkinnedEffect_Specular TARGET skinnedeffect_specular
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_skinnedeffect_specular_test.cpp")
    cna_d3d_parity_fixture(
        NAME SkinnedEffect_PreferPerPixelLighting TARGET skinnedeffect_preferperpixellighting
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_skinnedeffect_preferperpixellighting_test.cpp")
    cna_d3d_parity_fixture(
        NAME SkinnedEffect_WeightsPerVertex TARGET skinnedeffect_weightspervertex
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_skinnedeffect_weightspervertex_test.cpp")
    cna_d3d_parity_fixture(
        NAME SkinnedEffect_IdentityBones TARGET skinnedeffect_identity_bones
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_skinnedeffect_identity_bones_test.cpp")
    cna_d3d_parity_fixture(
        NAME SkinnedEffect_TranslationBone TARGET skinnedeffect_translation_bone
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_skinnedeffect_translation_bone_test.cpp")
    cna_d3d_parity_fixture(
        NAME SkinnedEffect_TwoBoneBlend TARGET skinnedeffect_twobone_blend
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_skinnedeffect_twobone_blend_test.cpp")
    cna_d3d_parity_fixture(
        NAME SkinnedEffect_Combined TARGET skinnedeffect_combined
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_skinnedeffect_combined_test.cpp")
    cna_d3d_parity_fixture(
        NAME SkinnedEffect_BoneCount TARGET skinned_effect_bones
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_skinned_effect_bones_test.cpp")
    cna_d3d_parity_fixture(
        NAME SkinnedEffect_Vector4BoneIndices TARGET skinnedeffect_vector4_bone_indices
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_skinnedeffect_vector4_bone_indices_test.cpp")
    cna_d3d_parity_fixture(
        NAME Effect_Clone TARGET effect_clone
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_effect_clone_test.cpp")
    cna_d3d_parity_fixture(
        NAME Effect_CurrentTechnique TARGET effect_current_technique
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_effect_current_technique_test.cpp")
    cna_d3d_parity_fixture(
        NAME Model_Draw TARGET model_draw
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_model_draw_test.cpp")
    cna_d3d_parity_fixture(
        NAME Model_HierarchyChildMesh TARGET model_hierarchy_child_mesh
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_model_hierarchy_child_mesh_test.cpp")
    cna_d3d_parity_fixture(
        NAME Model_JsonReader TARGET model_json_reader
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_model_json_reader_test.cpp")
    cna_d3d_parity_fixture(
        NAME Model_JsonReader_Texture TARGET model_json_reader_texture
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_model_json_reader_texture_test.cpp")
    cna_d3d_parity_fixture(
        NAME Model_JsonReader_Skeleton TARGET model_json_reader_skeleton
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_model_json_reader_skeleton_test.cpp")
    cna_d3d_parity_fixture(
        NAME Model_JsonReader_BoneHierarchy TARGET model_json_reader_bone_hierarchy
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_model_json_reader_bone_hierarchy_test.cpp")
    cna_d3d_parity_fixture(
        NAME Model_JsonReader_32BitIndices TARGET model_json_reader_32bit_indices
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_model_json_reader_32bit_indices_test.cpp")
    cna_d3d_parity_fixture(
        NAME Model_SkinnedAnimationPlayback TARGET model_skinned_animation_playback
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_model_skinned_animation_playback_test.cpp")
    cna_d3d_parity_fixture(
        NAME Model_TwoMeshesEffects TARGET model_two_meshes_effects
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_model_two_meshes_effects_test.cpp")
    cna_d3d_parity_fixture(
        NAME Resource_Disposed TARGET disposed_resource
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_disposed_resource_test.cpp")
    cna_d3d_parity_fixture(
        NAME Resource_DoubleDispose TARGET double_dispose
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_double_dispose_test.cpp")
    cna_d3d_parity_fixture(
        NAME Resource_BoundDispose TARGET bound_resource_dispose
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_bound_resource_dispose_test.cpp")
    cna_d3d_parity_fixture(
        NAME Resource_HandleRelease TARGET handle_release
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_handle_release_test.cpp")
    cna_d3d_parity_fixture(
        NAME Resource_MoveSemantics TARGET move_semantics
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_move_semantics_test.cpp")
    cna_d3d_parity_fixture(
        NAME Resource_Events TARGET resource_events
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_resource_events_test.cpp")
    cna_d3d_parity_fixture(
        NAME Resource_DeviceDisposeOrder TARGET device_dispose_order
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_device_dispose_order_test.cpp")
    cna_d3d_parity_fixture(
        NAME Resource_Leak TARGET resource_leak
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_resource_leak_test.cpp")
    cna_d3d_parity_fixture(
        NAME Resource_DeferredSourceLifetime TARGET deferred_source_lifetime
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/deferred_source_lifetime_test.cpp"
        TIMEOUT 900)
    cna_d3d_parity_fixture(
        NAME Resource_BoundTargetLifetime TARGET bound_target_lifetime
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/bound_target_lifetime_test.cpp"
        TIMEOUT 900)
    cna_d3d_parity_fixture(
        NAME Resource_PresentLifecycle TARGET present_lifecycle
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/present_lifecycle_contract_test.cpp"
        TIMEOUT 1200)
    cna_d3d_parity_fixture(
        NAME Buffer_Disposed TARGET disposed_buffer
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_disposed_buffer_test.cpp")
    cna_d3d_parity_fixture(
        NAME Buffer_Usage TARGET buffer_usage
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_buffer_usage_test.cpp")
    cna_d3d_parity_fixture(
        NAME Buffer_DynamicStress TARGET dynamic_buffer_stress
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_dynamic_buffer_stress_test.cpp")
    cna_d3d_parity_fixture(
        NAME Buffer_SetData TARGET vertexbuffer_setdata
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_vertexbuffer_setdata_test.cpp")
    cna_d3d_parity_fixture(
        NAME Buffer_GetData TARGET vertexbuffer_indexbuffer_getdata
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_vertexbuffer_indexbuffer_getdata_test.cpp")
    cna_d3d_parity_fixture(
        NAME SpriteBatch_LayerDepth TARGET spritebatch_layerdepth
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_spritebatch_layerdepth_test.cpp")
    cna_d3d_parity_fixture(
        NAME SpriteBatch_Rotation TARGET spritebatch_rotation
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_spritebatch_rotation_test.cpp")
    cna_d3d_parity_fixture(
        NAME SpriteBatch_Scale TARGET spritebatch_scale
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_spritebatch_scale_test.cpp")
    cna_d3d_parity_fixture(
        NAME SpriteBatch_SourceRectangle TARGET spritebatch_sourcerect
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_spritebatch_sourcerect_test.cpp")
    cna_d3d_parity_fixture(
        NAME SpriteEffects_Flip TARGET sprite_effects
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_sprite_effects_test.cpp")
    cna_d3d_parity_fixture(
        NAME SpriteBatch_BlendStateLeak TARGET spritebatch_blendstate_leak
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_spritebatch_blendstate_leak_test.cpp")
    cna_d3d_parity_fixture(
        NAME SpriteBatch_RenderTargetSize TARGET spritebatch_rendertarget_size
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_spritebatch_rendertarget_size_test.cpp")
    cna_d3d_parity_fixture(
        NAME SpriteFont_Properties TARGET sprite_font
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/sprite_font_test.cpp")
    cna_d3d_parity_fixture(
        NAME SpriteFont_SingleGlyph TARGET spritefont_single_glyph
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_spritefont_single_glyph_test.cpp")
    cna_d3d_parity_fixture(
        NAME SpriteFont_MultiGlyphSpacing TARGET spritefont_multiglyph_spacing
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_spritefont_multiglyph_spacing_test.cpp")
    cna_d3d_parity_fixture(
        NAME SpriteFont_Newline TARGET spritefont_newline
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_spritefont_newline_test.cpp")
    cna_d3d_parity_fixture(
        NAME SpriteFont_DefaultChar TARGET spritefont_default_char
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_spritefont_default_char_test.cpp")
    cna_d3d_parity_fixture(
        NAME SpriteFont_EffectsFlip TARGET spritefont_effects_flip
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_spritefont_effects_flip_test.cpp")
    cna_d3d_parity_fixture(
        NAME SpriteFont_EffectsRotationScale TARGET spritefont_effects_rotation_scale
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_spritefont_effects_rotation_scale_test.cpp")
    cna_d3d_parity_fixture(
        NAME SourceRectangleOrientation TARGET source_rectangle_orientation
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/source_rectangle_orientation_test.cpp"
        TIMEOUT 300)
    cna_d3d_parity_fixture(
        NAME Texture2D_PartialRect_RoundTrip TARGET texture2d_partial_rect
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_texture2d_partial_rect_test.cpp")
    cna_d3d_parity_fixture(
        NAME Texture2D_Mip_RoundTrip TARGET texture2d_mip
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_texture2d_mip_test.cpp")
    cna_d3d_parity_fixture(
        NAME NpotTexture TARGET npot_texture
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_npot_texture_test.cpp")
    cna_d3d_parity_fixture(
        NAME Texture3D_Slices_RoundTrip TARGET texture3d_slices
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_texture3d_slices_test.cpp")
    cna_d3d_parity_fixture(
        NAME Texture3D_Mip_RoundTrip TARGET texture3d_mip
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_texture3d_mip_test.cpp")
    cna_d3d_parity_fixture(
        NAME Texture3D_PartialBox_RoundTrip TARGET texture3d_partial_box
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_texture3d_partial_box_test.cpp")
    cna_d3d_parity_fixture(
        NAME Texture3D_PartialBox_Readback TARGET texture3d_partial_box_readback
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_texture3d_partial_box_readback_test.cpp")
    cna_d3d_parity_fixture(
        NAME TextureCube_ContentLoad TARGET texturecube_content_load
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_texturecube_content_load_test.cpp")
    cna_d3d_parity_fixture(
        NAME TextureCube_Faces_RoundTrip TARGET texturecube_faces
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_texturecube_faces_test.cpp")
    cna_d3d_parity_fixture(
        NAME TextureCube_Mip_RoundTrip TARGET texturecube_mip
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_texturecube_mip_test.cpp")
    cna_d3d_parity_fixture(
        NAME TextureCube_PartialRect_RoundTrip TARGET texturecube_partial_rect
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_texturecube_partial_rect_test.cpp")
    cna_d3d_parity_fixture(
        NAME TextureAddressMode TARGET texture_address_mode
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_texture_address_mode_test.cpp")
    cna_d3d_parity_fixture(
        NAME TextureAddressMode_Mirror TARGET texture_address_mode_mirror
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_texture_address_mode_mirror_test.cpp")
    cna_d3d_parity_fixture(
        NAME SamplerState_DualTextureEffect TARGET sampler_state_effect
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_sampler_state_effect_test.cpp")
    cna_d3d_parity_fixture(
        NAME TextureAddressMode_Clamp_DualTextureEffect TARGET texture_address_mode_clamp_effect
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_texture_address_mode_clamp_effect_test.cpp")
    cna_d3d_parity_fixture(
        NAME TextureAddressMode_Mirror_DualTextureEffect TARGET texture_address_mode_mirror_effect
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_texture_address_mode_mirror_effect_test.cpp")
    cna_d3d_parity_fixture(
        NAME Texture2D_AnisotropicSingleLevel TARGET texture2d_anisotropic_singlelevel
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_texture2d_anisotropic_singlelevel_test.cpp")
    cna_d3d_parity_fixture(
        NAME TextureAnisotropic_DualTextureEffect TARGET texture_anisotropic_effect
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_texture_anisotropic_effect_test.cpp")
    cna_d3d_parity_fixture(
        NAME TextureFilter_PointVsLinear TARGET texture_filter_point_vs_linear
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_texture_filter_point_vs_linear_test.cpp")
    cna_d3d_parity_fixture(
        NAME TextureFilter_Linear_Golden TARGET texture_filter_linear_golden
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_texture_filter_linear_golden_test.cpp"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    cna_d3d_parity_fixture(
        NAME TexturedQuad_Readback TARGET textured_quad
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_textured_quad_test.cpp")
    cna_d3d_parity_fixture(
        NAME SamplerComponentIsolation TARGET sampler_component_isolation
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/sampler_component_isolation_contract_test.cpp"
        TIMEOUT 300)
    cna_d3d_parity_fixture(
        NAME EnvMapCubeSamplerState TARGET envmap_cube_sampler_state
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/envmap_cube_sampler_state_test.cpp"
        TIMEOUT 600)
    cna_d3d_parity_fixture(
        NAME TextureMipFilter_DualTextureEffect TARGET texture_mip_filter_effect
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_texture_mip_filter_effect_test.cpp"
        TIMEOUT 300)
    cna_d3d_parity_fixture(
        NAME ColorSpace_MidTone TARGET colorspace_midtone
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/colorspace_midtone_contract_test.cpp"
        TIMEOUT 600)
    cna_d3d_parity_fixture(
        NAME SurfaceFormat_StorageContract TARGET surface_format_storage
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/d3d_surface_format_storage_contract_test.cpp")
    cna_d3d_parity_fixture(
        NAME RenderTarget_SurfaceFormat TARGET rendertarget_surface_format
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_surface_format_contract_test.cpp"
        TIMEOUT 300)
    cna_d3d_parity_fixture(
        NAME RenderTarget2D_Readback TARGET rendertarget2d_readback
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_render_target_test.cpp"
        TIMEOUT 300)
    cna_d3d_parity_fixture(
        NAME RenderTarget2D_Golden TARGET rendertarget2d_golden
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget2d_golden_test.cpp"
        TIMEOUT 300)
    cna_d3d_parity_fixture(
        NAME RenderTarget2D_DepthBuffer TARGET rendertarget2d_depth
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget2d_depth_test.cpp"
        TIMEOUT 300)
    cna_d3d_parity_fixture(
        NAME RenderTarget2D_MipComplete TARGET rendertarget2d_mip_complete
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_rendertarget2d_mip_test.cpp"
        TIMEOUT 300)
    cna_d3d_parity_fixture(
        NAME RenderTarget2D_Properties TARGET rendertarget2d_properties
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_rendertarget2d_properties_test.cpp"
        TIMEOUT 300)
    cna_d3d_parity_fixture(
        NAME RenderTargetCube_Properties TARGET rendertargetcube_properties
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_rendertargetcube_properties_test.cpp"
        TIMEOUT 300)
    cna_d3d_parity_fixture(
        NAME RenderTarget_BackbufferConsumer TARGET rendertarget_backbuffer_consumer
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_backbuffer_consumer_test.cpp"
        TIMEOUT 600)
    cna_d3d_parity_fixture(
        NAME RenderTargetCube_PluralBinding TARGET rendertargetcube_plural_binding
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/rendertargetcube_plural_binding_test.cpp"
        TIMEOUT 600)
    cna_d3d_parity_fixture(
        NAME RenderTarget_Usage TARGET rendertarget_usage
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_render_target_usage_test.cpp"
        TIMEOUT 300)
    cna_d3d_parity_fixture(
        NAME RenderTarget_Roundtrip TARGET rendertarget_roundtrip
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_rt_roundtrip_test.cpp"
        TIMEOUT 300)
    cna_d3d_parity_fixture(
        NAME RenderTarget_MsaaDepthContract TARGET rendertarget_msaa_depth
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_msaa_depth_contract_test.cpp"
        TIMEOUT 900)
    cna_d3d_parity_fixture(
        NAME RenderTarget_MsaaFirstReadback TARGET rendertarget_msaa_first_readback
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_msaa_first_readback_test.cpp"
        TIMEOUT 900)
    cna_d3d_parity_fixture(
        NAME RenderTarget_InvalidMipLevel TARGET rendertarget_invalid_mip
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_invalid_mip_level_test.cpp"
        TIMEOUT 1200)
    cna_d3d_parity_fixture(
        NAME RenderTarget_MsaaMipReadback TARGET rendertarget_msaa_mip_readback
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_msaa_mip_readback_test.cpp"
        TIMEOUT 1200)
    cna_d3d_parity_fixture(
        NAME OcclusionQuery_Cycle TARGET occlusion_query
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/occlusion_query_test.cpp"
        TIMEOUT 300)
    cna_d3d_parity_fixture(
        NAME OcclusionQuery_VisibleQuad TARGET occlusion_query_visible
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_occlusion_query_visible_quad_test.cpp"
        TIMEOUT 300)
    cna_d3d_parity_fixture(
        NAME OcclusionQuery_OccludedQuad TARGET occlusion_query_occluded
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_occlusion_query_occluded_quad_test.cpp"
        TIMEOUT 300)
    cna_d3d_parity_fixture(
        NAME GraphicsDevice_DefaultStateOcclusion TARGET graphicsdevice_default_state_occlusion
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/graphicsdevice_default_state_occlusion_test.cpp"
        TIMEOUT 300)
    cna_d3d_parity_fixture(
        NAME RenderTargetCube_SampleAfterUnbind TARGET rendertargetcube_sample
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/rendertargetcube_sample_test.cpp"
        TIMEOUT 300)
    cna_d3d_parity_fixture(
        NAME RenderTargetCube_DepthFormat TARGET rendertargetcube_depthformat
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_rendertargetcube_depthformat_test.cpp"
        TIMEOUT 300)
    cna_d3d_parity_fixture(
        NAME RenderTarget_ActiveMsaaReadback TARGET active_msaa_readback
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_active_msaa_readback_test.cpp"
        TIMEOUT 300)
    cna_d3d_parity_fixture(
        NAME MRT TARGET mrt
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_mrt_test.cpp"
        TIMEOUT 600)
    cna_d3d_parity_fixture(
        NAME CompressedTexture_StorageContract TARGET compressed_texture_storage
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/d3d_compressed_texture_contract_test.cpp"
        TIMEOUT 300)
    cna_d3d_parity_fixture(
        NAME DxtFormat TARGET dxt_format
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_dxt_format_test.cpp"
        TIMEOUT 300)
    cna_d3d_parity_fixture(
        NAME Dxt1_FromStream TARGET dxt1_fromstream
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/dxt1_texture_test.cpp"
        TIMEOUT 300)
    cna_d3d_parity_fixture(
        NAME Packed16Format TARGET packed16_format
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_packed16_format_test.cpp"
        TIMEOUT 300)
    cna_d3d_parity_fixture(
        NAME SurfaceFormat_Throws TARGET surface_format_throws
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_surface_format_throws_test.cpp")
    cna_d3d_parity_fixture(
        NAME SpriteBatch_CustomViewport TARGET spritebatch_custom_viewport
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/spritebatch_custom_viewport_test.cpp")
    cna_d3d_parity_fixture(
        NAME Texture2D_GetDataContract TARGET texture2d_getdata_contract
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/texture2d_getdata_contract_test.cpp")
    cna_d3d_parity_fixture(
        NAME Texture2D_GetDataTransferRange TARGET texture2d_getdata_transfer_range
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/texture2d_getdata_transfer_range_test.cpp")
    cna_d3d_parity_fixture(
        NAME RenderTarget_SamplingOrientation TARGET rt_sampling_orientation
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_sampling_orientation_test.cpp"
        TIMEOUT 120)
    cna_d3d_parity_fixture(
        NAME StockEffectSamplerContract TARGET stock_effect_sampler
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/stock_effect_sampler_contract_test.cpp"
        TIMEOUT 300)
    cna_d3d_parity_fixture(
        NAME TextureFilterOrdinalContract TARGET texture_filter_ordinal
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/texture_filter_ordinal_contract_test.cpp"
        TIMEOUT 300)
    cna_d3d_parity_fixture(
        NAME EnvMapCubeSamplerContract TARGET envmap_cube_sampler
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/envmap_cube_sampler_contract_test.cpp"
        TIMEOUT 600)
    cna_d3d_parity_fixture(
        NAME DualTextureSlotSamplerContract TARGET dualtexture_slot_sampler
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/dualtexture_slot_sampler_contract_test.cpp"
        TIMEOUT 600)
    cna_d3d_parity_fixture(
        NAME TextureFilterMipContract TARGET texture_filter_mip_contract
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/texture_filter_mip_contract_test.cpp"
        TIMEOUT 300)
    cna_d3d_parity_fixture(
        NAME SamplerLodAddressWContract TARGET sampler_lod_addressw
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/sampler_lod_addressw_contract_test.cpp"
        TIMEOUT 300)
    cna_d3d_parity_fixture(
        NAME ShaderEffect_ReflectionContract TARGET shader_effect_reflection
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/shader_effect_reflection_contract_test.cpp"
        TIMEOUT 300)
    cna_d3d_parity_fixture(
        NAME DescriptorCapacityContract TARGET descriptor_capacity
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/descriptor_capacity_contract_test.cpp"
        TIMEOUT 900)
    cna_d3d_parity_fixture(
        NAME PointSamplingContract TARGET point_sampling
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/point_sampling_contract_test.cpp"
        TIMEOUT 300)
    cna_d3d_parity_fixture(
        NAME XnaPixelCenter TARGET xna_pixel_center
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/xna_pixel_center_contract_test.cpp")
    cna_d3d_parity_fixture(
        NAME RenderTarget_ProducerConsumer TARGET rt_producer_consumer
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_producer_consumer_test.cpp"
        TIMEOUT 120)
    cna_d3d_parity_fixture(
        NAME RenderTarget_EffectSource TARGET rt_effect_source
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_effect_source_test.cpp"
        TIMEOUT 300)
    cna_d3d_parity_fixture(
        NAME CubeVolume_GetDataContract TARGET cube_volume_getdata_contract
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/texturecube_texture3d_getdata_contract_test.cpp")
    cna_d3d_parity_fixture(
        NAME RenderTargetCube_GetDataContract TARGET rendertargetcube_getdata_contract
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/rendertargetcube_getdata_contract_test.cpp")
    cna_d3d_parity_fixture(
        NAME RenderTargetCube_Usage TARGET rendertargetcube_usage
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/rendertargetcube_usage_test.cpp")
    cna_d3d_parity_fixture(
        NAME RenderTargetCube_MsaaFace TARGET rendertargetcube_msaa_face
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/rendertargetcube_msaa_face_test.cpp")
    cna_d3d_parity_fixture(
        NAME RenderTarget_DepthStencilUsage TARGET rendertarget_depthstencil_usage
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_depthstencil_usage_test.cpp")
    cna_d3d_parity_fixture(
        NAME RenderTarget_PassBoundary TARGET rendertarget_pass_boundary
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_pass_boundary_test.cpp"
        TIMEOUT 90)
    cna_d3d_parity_fixture(
        NAME GraphicsDevice_OrderedClear TARGET ordered_clear
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/graphicsdevice_ordered_clear_test.cpp"
        TIMEOUT 120)
    cna_d3d_parity_fixture(
        NAME Backbuffer_PassOrder TARGET backbuffer_pass_order
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/backbuffer_pass_order_test.cpp"
        TIMEOUT 120)
    cna_d3d_parity_fixture(
        NAME Deferred_Viewport TARGET deferred_viewport
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/deferred_viewport_capture_test.cpp"
        TIMEOUT 120)
    cna_d3d_parity_fixture(
        NAME Deferred_Scissor TARGET deferred_scissor
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/deferred_scissor_capture_test.cpp"
        TIMEOUT 120)
    cna_d3d_parity_fixture(
        NAME CubeVolume_SetDataContract TARGET cube_volume_setdata_contract
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/texturecube_texture3d_setdata_contract_test.cpp")
    cna_d3d_parity_fixture(
        NAME SpriteBatch_3DOrder TARGET spritebatch_3d_order
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/spritebatch_3d_order_test.cpp")
    cna_d3d_parity_fixture(
        NAME BlendState_BlendFactor TARGET blendstate_blendfactor
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_blendstate_blendfactor_test.cpp")
    cna_d3d_parity_fixture(
        NAME PrimitiveTypeValidation TARGET primitivetype_validation
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_primitivetype_validation_test.cpp")
    cna_d3d_parity_fixture(
        NAME SamplerConformance TARGET sampler_conformance
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/fna3d/examples/fna3d_sampler_test.cpp")
    cna_d3d_parity_fixture(
        NAME RenderTarget2D_Msaa TARGET rendertarget2d_msaa
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_rendertarget2d_msaa_test.cpp")
    cna_d3d_parity_fixture(
        NAME DepthBias TARGET depth_bias
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_depth_bias_test.cpp")
    cna_d3d_parity_fixture(
        NAME SpriteBatch_Scissor TARGET spritebatch_scissor
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/spritebatch_begin_rasterizerstate_scissor_test.cpp")
    cna_d3d_parity_fixture(
        NAME RenderTarget_ViewportScissorReset TARGET rt_viewport_scissor_reset
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_viewport_scissor_reset_test.cpp")
    cna_d3d_parity_fixture(
        NAME RenderTarget_FirstUse TARGET rt_first_use
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_first_use_test.cpp"
        TIMEOUT 180)
    cna_d3d_parity_fixture(
        NAME DeviceValidation TARGET device_validation
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_device_validation_test.cpp")
    cna_d3d_parity_fixture(
        NAME DrawNoVertexBuffer TARGET draw_novertexbuffer
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_draw_novertexbuffer_test.cpp")
    cna_d3d_parity_fixture(
        NAME DrawNoIndexBuffer TARGET draw_noindexbuffer
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_draw_noindexbuffer_test.cpp")
    cna_d3d_parity_fixture(
        NAME DrawRangeValidation TARGET draw_range_validation
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_draw_range_validation_test.cpp")
    cna_d3d_parity_fixture(
        NAME DrawUserPrimitives_VPC TARGET draw_user_primitives_vpc
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_draw_user_primitives_vpc_test.cpp")
    cna_d3d_parity_fixture(
        NAME DrawUserPrimitives_CustomVD TARGET draw_user_primitives_custom
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_draw_user_primitives_custom_test.cpp")
    cna_d3d_parity_fixture(
        NAME DrawUserIndexedPrimitives_VPC TARGET draw_user_indexed_primitives_vpc
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_draw_user_indexed_primitives_vpc_test.cpp")
    cna_d3d_parity_fixture(
        NAME DrawUserIndexedPrimitives_32 TARGET draw_user_indexed_primitives_32
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_draw_user_indexed_primitives_32_test.cpp")
    cna_d3d_parity_fixture(
        NAME ViewportState TARGET viewport_state
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_viewport_state_test.cpp")
    cna_d3d_parity_fixture(
        NAME Viewport_Subregion TARGET viewport_subregion
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_viewport_subregion_test.cpp")
    cna_d3d_parity_fixture(
        NAME Scissor TARGET scissor
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_scissor_test.cpp")
    cna_d3d_parity_fixture(
        NAME ClearOverloads TARGET clear_overloads
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_clear_overloads_test.cpp")
    cna_d3d_parity_fixture(
        NAME DeviceResetEvents TARGET device_reset_events
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_device_reset_events_test.cpp")
    cna_d3d_parity_fixture(
        NAME ContextRecoveryContract TARGET context_recovery_contract
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/d3d_context_recovery_contract_test.cpp"
        TIMEOUT 900)
    cna_d3d_parity_fixture(
        NAME ContextRecovery_Model TARGET context_recovery_model
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_gltf_context_loss_test.cpp"
        TIMEOUT 900
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    cna_d3d_parity_fixture(
        NAME ContentLostProbe TARGET content_lost_probe
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/content_lost_probe.cpp"
        TIMEOUT 300)
    cna_d3d_parity_fixture(
        NAME TransformMatrix_Translation TARGET transform_matrix
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_transform_matrix_test.cpp")
    cna_d3d_parity_fixture(
        NAME BackbufferReadbackDimension TARGET backbuffer_readback_dimension
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/backbuffer_readback_dimension_test.cpp"
        TIMEOUT 900)
    cna_d3d_parity_fixture(
        NAME BackbufferFirstRead TARGET backbuffer_first_read
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/backbuffer_first_read_test.cpp"
        TIMEOUT 900)
    cna_d3d_parity_fixture(
        NAME BackbufferReject TARGET backbuffer_reject
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/backbuffer_headless_reject_test.cpp"
        TIMEOUT 900)
    cna_d3d_parity_fixture(
        NAME PresentationModeContract TARGET presentation_mode_contract
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/presentation_mode_contract_test.cpp"
        TIMEOUT 300)
    cna_d3d_parity_fixture(
        NAME SwapIntervalForwarding TARGET swap_interval_forwarding
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/swap_interval_forwarding_contract_test.cpp")
    cna_d3d_parity_fixture(
        NAME SpriteBatch_CustomViewportRT TARGET spritebatch_custom_viewport_rt
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/spritebatch_custom_viewport_rt_test.cpp")
    cna_d3d_parity_fixture(
        NAME SpriteBatch_ViewportSwitch TARGET spritebatch_viewport_switch
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/spritebatch_viewport_switch_test.cpp")
    cna_d3d_parity_fixture(
        NAME MSAA_4x_Readback TARGET backbuffer_msaa
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_msaa_test.cpp"
        TIMEOUT 300)
    cna_d3d_parity_fixture(
        NAME MsaaChange TARGET backbuffer_msaa_change
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_msaa_change_test.cpp"
        TIMEOUT 600)
    cna_d3d_parity_fixture(
        NAME PresentationParameters TARGET presentation_parameters
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_presentation_parameters_test.cpp")
    cna_d3d_parity_fixture(
        NAME PresentationFormatContract TARGET presentation_format_contract
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/d3d_presentation_format_contract_test.cpp"
        TIMEOUT 300)
    cna_d3d_parity_fixture(
        NAME GraphicsAdapterQueryContract TARGET graphics_adapter_query_contract
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/graphics_adapter_query_contract_test.cpp"
        TIMEOUT 300)
    cna_d3d_parity_fixture(
        NAME RendererCapabilityTruth TARGET renderer_capability_truth
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/renderer_capability_truth_test.cpp"
        TIMEOUT 300)
    cna_d3d_parity_fixture(
        NAME PresentInterval TARGET present_interval
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_present_interval_test.cpp")
    cna_d3d_parity_fixture(
        NAME GraphicsDeviceManager_Vsync TARGET graphicsdevicemanager_vsync
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_graphicsdevicemanager_vsync_test.cpp")
    cna_d3d_parity_fixture(
        NAME BackbufferResize TARGET backbuffer_resize
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_backbuffer_resize_test.cpp"
        TIMEOUT 300)
    cna_d3d_parity_fixture(
        NAME RealWindowResize TARGET real_window_resize
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_real_window_resize_test.cpp"
        REQUIRES_SDL TIMEOUT 300)
    cna_d3d_parity_fixture(
        NAME ViewportResetAfterResize TARGET viewport_reset_after_resize
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/viewport_reset_after_resize_test.cpp" REQUIRES_SDL
        TIMEOUT 300)
endfunction()
