include_guard(GLOBAL)

# DX-242: one authoritative inventory for every CTest fixture participating in
# DirectX parity. Renderer-local smoke/gate programs stay in their owning
# CMakeLists; everything below is either shared or an explicit, reasoned
# renderer exception.
function(cna_register_d3d_parity_tests)
    if(NOT CNA_GRAPHICS_RENDERER MATCHES "^DIRECTX(11|12)$")
        message(FATAL_ERROR
            "cna_register_d3d_parity_tests is only valid for DIRECTX11 or DIRECTX12")
    endif()

    set(_cna_d3d_fixture_order 0)
    set(_cna_d3d_fixture_keys "")

    macro(cna_d3d_parity_fixture)
        cmake_parse_arguments(F
            "DIRECTX11_ONLY;DIRECTX12_ONLY;DIRECTX12_NO_HEADLESS"
            "NAME;TARGET;SOURCE;DIRECTX11_TIMEOUT;DIRECTX12_TIMEOUT;DIRECTX12_ORDER;REASON;DIRECTX11_ENVIRONMENT;WORKING_DIRECTORY"
            ""
            ${ARGN})

        if(NOT F_NAME OR NOT F_TARGET OR NOT F_SOURCE)
            message(FATAL_ERROR "Every DirectX parity fixture needs NAME, TARGET, and SOURCE")
        endif()
        if(F_DIRECTX11_ONLY AND F_DIRECTX12_ONLY)
            message(FATAL_ERROR "${F_NAME} cannot be both DIRECTX11_ONLY and DIRECTX12_ONLY")
        endif()
        if((F_DIRECTX11_ONLY OR F_DIRECTX12_ONLY) AND NOT F_REASON)
            message(FATAL_ERROR "The renderer exception for ${F_NAME} requires REASON")
        endif()
        if(DEFINED _cna_d3d_fixture_${F_NAME}_SOURCE)
            message(FATAL_ERROR "DirectX parity fixture ${F_NAME} is listed more than once")
        endif()

        math(EXPR _cna_d3d_fixture_order "${_cna_d3d_fixture_order} + 10")
        set(_cna_d3d_fixture_${F_NAME}_SOURCE "${F_SOURCE}")
        set(_cna_d3d_fixture_${F_NAME}_TARGET "${F_TARGET}")
        set(_cna_d3d_fixture_${F_NAME}_DIRECTX11_TIMEOUT "${F_DIRECTX11_TIMEOUT}")
        set(_cna_d3d_fixture_${F_NAME}_DIRECTX12_TIMEOUT "${F_DIRECTX12_TIMEOUT}")
        set(_cna_d3d_fixture_${F_NAME}_DIRECTX11_ONLY "${F_DIRECTX11_ONLY}")
        set(_cna_d3d_fixture_${F_NAME}_DIRECTX12_ONLY "${F_DIRECTX12_ONLY}")
        set(_cna_d3d_fixture_${F_NAME}_DIRECTX12_NO_HEADLESS "${F_DIRECTX12_NO_HEADLESS}")
        set(_cna_d3d_fixture_${F_NAME}_DIRECTX11_ENVIRONMENT "${F_DIRECTX11_ENVIRONMENT}")
        set(_cna_d3d_fixture_${F_NAME}_WORKING_DIRECTORY "${F_WORKING_DIRECTORY}")
        set(_cna_d3d_fixture_${F_NAME}_REASON "${F_REASON}")

        if(CNA_GRAPHICS_RENDERER STREQUAL "DIRECTX11")
            set(_cna_d3d_selected_order "${_cna_d3d_fixture_order}")
        elseif(F_DIRECTX12_ORDER)
            set(_cna_d3d_selected_order "${F_DIRECTX12_ORDER}")
        else()
            set(_cna_d3d_selected_order "${_cna_d3d_fixture_order}")
        endif()
        math(EXPR _cna_d3d_sort_key "100000 + ${_cna_d3d_selected_order}")
        list(APPEND _cna_d3d_fixture_keys "${_cna_d3d_sort_key}:${F_NAME}")
    endmacro()

    # D3D11's existing registrations define the declaration order. DIRECTX12_ORDER
    # preserves D3D12's independently-established CTest order during this refactor.
    cna_d3d_parity_fixture(
        NAME GraphicsDevice_DepthContract TARGET depth_contract
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/graphicsdevice_depth_contract_test.cpp"
        DIRECTX11_ONLY REASON "D3D12 adoption is tracked by DX-227 and DX-236")
    cna_d3d_parity_fixture(
        NAME Common TARGET common SOURCE directx11_common_test.cpp
        DIRECTX11_TIMEOUT 30 DIRECTX11_ENVIRONMENT "CNA_D3D11_SKIP_DXVK_GATE=1"
        DIRECTX11_ONLY REASON "This pure mapping-table executable tests D3D11-native enums")
    cna_d3d_parity_fixture(
        NAME BlendState_Opaque TARGET blendstate_opaque DIRECTX12_ORDER 310
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_blendstate_opaque_test.cpp")
    cna_d3d_parity_fixture(
        NAME BlendState_AlphaBlend TARGET blendstate_alphablend DIRECTX12_ORDER 320
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_blendstate_alphablend_test.cpp")
    cna_d3d_parity_fixture(
        NAME BlendState_Additive TARGET blendstate_additive DIRECTX12_ORDER 330
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_blendstate_additive_test.cpp")
    cna_d3d_parity_fixture(
        NAME BlendState_NonPremultiplied TARGET blendstate_nonpremultiplied DIRECTX12_ORDER 340
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_blendstate_nonpremultiplied_test.cpp")
    cna_d3d_parity_fixture(
        NAME BlendState_SeparateFactors TARGET blendstate_separate_factors DIRECTX12_ORDER 350
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_blendstate_separate_factors_test.cpp")
    cna_d3d_parity_fixture(
        NAME BlendState_SeparateFunctions TARGET blendstate_separate_functions DIRECTX12_ORDER 360
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_blendstate_separate_functions_test.cpp")
    cna_d3d_parity_fixture(
        NAME ColorWriteChannels TARGET colorwritechannels DIRECTX12_ORDER 370
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_colorwritechannels_test.cpp")
    cna_d3d_parity_fixture(
        NAME AdditiveBlendContract TARGET additive_blend_contract DIRECTX12_ORDER 380
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/additive_blend_contract_test.cpp"
        DIRECTX11_TIMEOUT 300 DIRECTX12_TIMEOUT 600)
    cna_d3d_parity_fixture(
        NAME ColorWriteChannels3D TARGET colorwritechannels_3d DIRECTX12_ORDER 390
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/gfx077_colorwritechannels_3d_test.cpp"
        DIRECTX11_TIMEOUT 300 DIRECTX12_TIMEOUT 600)
    cna_d3d_parity_fixture(
        NAME DepthStencilState_StencilEnable TARGET depthstencilstate_stencil_enable DIRECTX12_ORDER 400
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_depthstencilstate_stencil_enable_test.cpp")
    cna_d3d_parity_fixture(
        NAME DepthStencilState_CompareFunction TARGET depthstencilstate_compare_function DIRECTX12_ORDER 410
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_depthstencilstate_compare_function_test.cpp")
    cna_d3d_parity_fixture(
        NAME DepthStencilState_StencilMask TARGET depthstencilstate_stencil_mask DIRECTX12_ORDER 420
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_depthstencilstate_stencil_mask_test.cpp")
    cna_d3d_parity_fixture(
        NAME DepthStencilState_StencilOps TARGET depthstencilstate_stencil_ops DIRECTX12_ORDER 430
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_depthstencilstate_stencil_ops_test.cpp")
    cna_d3d_parity_fixture(
        NAME DepthStencilState_StencilTwoSided TARGET depthstencilstate_stencil_twosided DIRECTX12_ORDER 440
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_depthstencilstate_stencil_twosided_test.cpp")
    cna_d3d_parity_fixture(
        NAME DepthStencilState_WriteEnable TARGET depthstencilstate_write_enable DIRECTX12_ORDER 450
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_depthstencilstate_write_enable_test.cpp")
    cna_d3d_parity_fixture(
        NAME GraphicsDevice_ClearStencil TARGET graphicsdevice_clear_stencil DIRECTX12_ORDER 460
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_graphicsdevice_clear_stencil_test.cpp")
    cna_d3d_parity_fixture(
        NAME GraphicsDevice_ReferenceStencil TARGET graphicsdevice_reference_stencil DIRECTX12_ORDER 470
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_graphicsdevice_reference_stencil_test.cpp")
    cna_d3d_parity_fixture(
        NAME GraphicsDevice_ClearDepth TARGET graphicsdevice_clear_depth DIRECTX12_ORDER 480
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/graphicsdevice_clear_depth_test.cpp")
    cna_d3d_parity_fixture(
        NAME RasterizerState_CullMode TARGET rasterizerstate_cullmode DIRECTX12_ORDER 490
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_rasterizerstate_cullmode_test.cpp")
    cna_d3d_parity_fixture(
        NAME RasterizerState_CullModeCamera TARGET rasterizerstate_cullmode_camera DIRECTX12_ORDER 500
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/rasterizerstate_cullmode_camera_test.cpp")
    cna_d3d_parity_fixture(
        NAME RasterizerState_CullModeIndexedBasicEffect
        TARGET rasterizerstate_cullmode_indexed_basiceffect DIRECTX12_ORDER 510
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/rasterizerstate_cullmode_indexed_basiceffect_test.cpp")
    cna_d3d_parity_fixture(
        NAME FrontFaceWinding TARGET frontface_winding DIRECTX12_ORDER 520
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/frontface_winding_test.cpp"
        DIRECTX11_TIMEOUT 600 DIRECTX12_TIMEOUT 900)
    cna_d3d_parity_fixture(
        NAME TriangleStripWinding TARGET triangle_strip_winding DIRECTX12_ORDER 530
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/triangle_strip_winding_test.cpp"
        DIRECTX11_TIMEOUT 600 DIRECTX12_TIMEOUT 900)
    cna_d3d_parity_fixture(
        NAME BasicEffect_Properties TARGET basiceffect_properties DIRECTX12_ORDER 540
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/basic_effect_test.cpp")
    cna_d3d_parity_fixture(
        NAME BasicEffect_Golden TARGET basiceffect_golden DIRECTX12_ORDER 550
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_basiceffect_golden_test.cpp"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    cna_d3d_parity_fixture(
        NAME BasicEffect_PositionNormal TARGET basiceffect_position_normal DIRECTX12_ORDER 560
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_basiceffect_position_normal_test.cpp")
    cna_d3d_parity_fixture(
        NAME BasicEffect_MultiLightEmissive TARGET basiceffect_multilight_emissive DIRECTX12_ORDER 570
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_basiceffect_multilight_emissive_test.cpp")
    cna_d3d_parity_fixture(
        NAME BasicEffect_Specular TARGET basiceffect_specular DIRECTX12_ORDER 580
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_basiceffect_specular_test.cpp")
    cna_d3d_parity_fixture(
        NAME BasicEffect_PreferPerPixelLighting TARGET basiceffect_preferperpixellighting DIRECTX12_ORDER 590
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_basiceffect_preferperpixellighting_test.cpp")
    cna_d3d_parity_fixture(
        NAME BasicEffect_Combinations TARGET basiceffect_combinations DIRECTX12_ORDER 600
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_basiceffect_combinations_test.cpp")
    cna_d3d_parity_fixture(
        NAME BasicEffect_VertexColorDisabled TARGET basiceffect_vertexcolor_disabled DIRECTX12_ORDER 610
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_basiceffect_vertexcolor_disabled_test.cpp")
    cna_d3d_parity_fixture(
        NAME BasicEffect_VertexColorEnabled TARGET basiceffect_vertexcolor_enabled DIRECTX12_ORDER 620
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_basiceffect_vertexcolor_enabled_test.cpp")
    cna_d3d_parity_fixture(
        NAME BasicEffect_TextureEnabled TARGET basiceffect_texture_enabled DIRECTX12_ORDER 630
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_basiceffect_texture_enabled_test.cpp")
    cna_d3d_parity_fixture(
        NAME BasicEffect_TextureVertexColorEnabled TARGET basiceffect_texture_vertexcolor_enabled DIRECTX12_ORDER 640
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_basiceffect_texture_vertexcolor_enabled_test.cpp")
    cna_d3d_parity_fixture(
        NAME BasicEffect_OneLight TARGET basiceffect_one_light DIRECTX12_ORDER 650
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_basiceffect_one_light_test.cpp")
    cna_d3d_parity_fixture(
        NAME BasicEffect_Emissive TARGET basiceffect_emissive DIRECTX12_ORDER 660
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_basiceffect_emissive_test.cpp")
    cna_d3d_parity_fixture(
        NAME BasicEffect_Combined TARGET basiceffect_combined DIRECTX12_ORDER 670
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_basiceffect_combined_test.cpp")
    cna_d3d_parity_fixture(
        NAME BasicEffect_DefaultLighting TARGET basiceffect_default_lighting DIRECTX12_ORDER 680
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_basiceffect_default_lighting_test.cpp")
    cna_d3d_parity_fixture(
        NAME BasicEffect_Fog TARGET basiceffect_fog DIRECTX12_ORDER 690
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_basiceffect_fog_test.cpp")
    cna_d3d_parity_fixture(
        NAME BasicEffect_VertexColorClamp TARGET basiceffect_vertex_color_clamp DIRECTX12_ORDER 700
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_basiceffect_vertex_color_clamp_test.cpp")
    cna_d3d_parity_fixture(
        NAME BasicEffect_WorldScalePrecision TARGET basiceffect_world_scale_precision DIRECTX12_ORDER 710
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_basiceffect_world_scale_precision_test.cpp")
    cna_d3d_parity_fixture(
        NAME BasicEffect_LitVertexColor TARGET basiceffect_lit_vertex_color DIRECTX12_ORDER 720
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_basiceffect_lit_vertex_color_test.cpp")
    cna_d3d_parity_fixture(
        NAME AlphaTestEffect_Properties TARGET alphatesteffect_properties DIRECTX12_ORDER 730
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/alpha_test_effect_test.cpp")
    cna_d3d_parity_fixture(
        NAME AlphaTestEffect_AlphaCutout TARGET alphatesteffect_alpha_cutout DIRECTX12_ORDER 740
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/alpha_test_integration_test.cpp")
    cna_d3d_parity_fixture(
        NAME AlphaTestEffect_Golden TARGET alphatesteffect_golden DIRECTX12_ORDER 750
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_alphatesteffect_golden_test.cpp"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    cna_d3d_parity_fixture(
        NAME AlphaTestEffect_Modes TARGET alphatest_modes DIRECTX12_ORDER 760
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_alphatest_modes_test.cpp")
    cna_d3d_parity_fixture(
        NAME AlphaTestEffect_CompareFunctionSweep TARGET alphatest_comparefunction_sweep
        DIRECTX12_ORDER 770
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_alphatest_comparefunction_sweep_test.cpp")
    cna_d3d_parity_fixture(
        NAME AlphaTestEffect_VertexColorDiffuse TARGET alphatest_vertexcolor_diffuse
        DIRECTX12_ORDER 780
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_alphatest_vertexcolor_diffuse_test.cpp")
    cna_d3d_parity_fixture(
        NAME AlphaTestEffect_NullTexture TARGET alphatest_null_texture DIRECTX12_ORDER 790
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_alphatest_null_texture_test.cpp")
    cna_d3d_parity_fixture(
        NAME Pbr_VertexColor TARGET pbr_vertexcolor SOURCE directx11_pbr_vertexcolor_test.cpp
        DIRECTX11_ONLY REASON "The fixture directly exercises the D3D11 PBR frontend")
    cna_d3d_parity_fixture(
        NAME Pbr_SrgbTransfer TARGET pbr_srgb_transfer DIRECTX12_ORDER 10
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_pbr_srgb_transfer_test.cpp")
    cna_d3d_parity_fixture(
        NAME Pbr_FresnelFactors TARGET pbr_fresnel_factors DIRECTX12_ORDER 20
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_pbr_fresnel_factors_test.cpp")
    cna_d3d_parity_fixture(
        NAME EnvironmentMapAmountZero TARGET environmentmapeffect_amount_zero
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/vulkan/examples/vulkan_environmentmapeffect_amount_zero_test.cpp"
        DIRECTX11_ONLY REASON "D3D12 stock-effect corpus adoption is tracked by DX-230 and DX-236")
    cna_d3d_parity_fixture(
        NAME SkinnedEffect_WorldNormal TARGET skinnedeffect_world_normal
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/vulkan/examples/vulkan_skinnedeffect_world_normal_test.cpp"
        DIRECTX11_ONLY REASON "D3D12 stock-effect corpus adoption is tracked by DX-230 and DX-236")
    cna_d3d_parity_fixture(
        NAME SkinnedEffect_LightingConformance TARGET skinnedeffect_lighting_conformance
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/skinnedeffect_lighting_conformance_test.cpp"
        DIRECTX11_ONLY REASON "D3D12 stock-effect corpus adoption is tracked by DX-230 and DX-236")
    cna_d3d_parity_fixture(
        NAME ViewSpaceFog TARGET viewspace_fog
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/vulkan/examples/vulkan_viewspace_fog_test.cpp"
        DIRECTX11_ONLY REASON "D3D12 stock-effect corpus adoption is tracked by DX-230 and DX-236")
    cna_d3d_parity_fixture(
        NAME AlphaTest_Fog TARGET alphatest_fog DIRECTX12_ORDER 800
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_alphatest_fog_test.cpp")
    cna_d3d_parity_fixture(
        NAME DualTextureEffect_Fog TARGET dualtextureeffect_fog DIRECTX12_ORDER 810
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_dualtextureeffect_fog_test.cpp")
    cna_d3d_parity_fixture(
        NAME DualTextureEffect_Golden TARGET dualtextureeffect_golden DIRECTX12_ORDER 820
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_dualtextureeffect_golden_test.cpp"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    cna_d3d_parity_fixture(
        NAME DualTextureEffect_Blend TARGET dual_texture DIRECTX12_ORDER 830
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_dual_texture_test.cpp")
    cna_d3d_parity_fixture(
        NAME DualTextureEffect_Doubling TARGET dualtextureeffect_doubling DIRECTX12_ORDER 840
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_dualtextureeffect_doubling_test.cpp")
    cna_d3d_parity_fixture(
        NAME DualTextureEffect_Alpha TARGET dualtextureeffect_alpha DIRECTX12_ORDER 850
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_dualtextureeffect_alpha_test.cpp")
    cna_d3d_parity_fixture(
        NAME DualTextureEffect_NullTexture0 TARGET dualtextureeffect_null_texture0 DIRECTX12_ORDER 860
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_dualtextureeffect_null_texture0_test.cpp")
    cna_d3d_parity_fixture(
        NAME DualTextureEffect_NullTexture2 TARGET dualtextureeffect_null_texture2 DIRECTX12_ORDER 870
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_dualtextureeffect_null_texture2_test.cpp")
    cna_d3d_parity_fixture(
        NAME DualTextureEffect_Combined TARGET dualtextureeffect_combined DIRECTX12_ORDER 880
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_dualtextureeffect_combined_test.cpp")
    cna_d3d_parity_fixture(
        NAME DualTextureEffect_Integration TARGET dualtexture DIRECTX12_ORDER 890
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_dualtexture_test.cpp")
    cna_d3d_parity_fixture(
        NAME DualTextureEffect_IndependentUV TARGET dualtextureeffect_independent_uv DIRECTX12_ORDER 900
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_dualtextureeffect_independent_uv_test.cpp")
    cna_d3d_parity_fixture(
        NAME DualTextureEffect_VertexColor TARGET dualtextureeffect_vertexcolor DIRECTX12_ORDER 910
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/dualtextureeffect_vertexcolor_test.cpp")
    cna_d3d_parity_fixture(
        NAME EnvironmentMapEffect_Fog TARGET environmentmap_fog
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/vulkan/examples/vulkan_environmentmapeffect_fog_test.cpp"
        DIRECTX11_ONLY REASON "D3D12 stock-effect corpus adoption is tracked by DX-230 and DX-236")
    cna_d3d_parity_fixture(
        NAME SkinnedEffect_Fog TARGET skinnedeffect_fog
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/vulkan/examples/vulkan_skinnedeffect_fog_test.cpp"
        DIRECTX11_ONLY REASON "D3D12 stock-effect corpus adoption is tracked by DX-230 and DX-236")
    cna_d3d_parity_fixture(
        NAME SpriteBatch_CustomViewport TARGET spritebatch_custom_viewport
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/spritebatch_custom_viewport_test.cpp"
        DIRECTX11_ONLY REASON "D3D12 presentation-corpus adoption is tracked by DX-234 and DX-236")
    cna_d3d_parity_fixture(
        NAME Texture2D_GetDataContract TARGET texture2d_getdata_contract DIRECTX12_ORDER 30
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/texture2d_getdata_contract_test.cpp")
    cna_d3d_parity_fixture(
        NAME Texture2D_GetDataTransferRange TARGET texture2d_getdata_transfer_range DIRECTX12_ORDER 40
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/texture2d_getdata_transfer_range_test.cpp")
    cna_d3d_parity_fixture(
        NAME RenderTarget_SamplingOrientation TARGET rt_sampling_orientation
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_sampling_orientation_test.cpp"
        DIRECTX11_TIMEOUT 120 DIRECTX11_ONLY
        REASON "D3D12 render-target corpus adoption is tracked by DX-232 and DX-236")
    cna_d3d_parity_fixture(
        NAME StockEffectSamplerContract TARGET stock_effect_sampler DIRECTX12_ORDER 120
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/stock_effect_sampler_contract_test.cpp"
        DIRECTX11_TIMEOUT 300)
    cna_d3d_parity_fixture(
        NAME TextureFilterOrdinalContract TARGET texture_filter_ordinal DIRECTX12_ORDER 50
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/texture_filter_ordinal_contract_test.cpp"
        DIRECTX11_TIMEOUT 300 DIRECTX12_TIMEOUT 600)
    cna_d3d_parity_fixture(
        NAME EnvMapCubeSamplerContract TARGET envmap_cube_sampler DIRECTX12_ORDER 60
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/envmap_cube_sampler_contract_test.cpp"
        DIRECTX11_TIMEOUT 600)
    cna_d3d_parity_fixture(
        NAME DualTextureSlotSamplerContract TARGET dualtexture_slot_sampler DIRECTX12_ORDER 70
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/dualtexture_slot_sampler_contract_test.cpp"
        DIRECTX11_TIMEOUT 600)
    cna_d3d_parity_fixture(
        NAME TextureFilterMipContract TARGET texture_filter_mip_contract DIRECTX12_ORDER 80
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/texture_filter_mip_contract_test.cpp"
        DIRECTX11_TIMEOUT 300)
    cna_d3d_parity_fixture(
        NAME SamplerLodAddressWContract TARGET sampler_lod_addressw DIRECTX12_ORDER 85
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/sampler_lod_addressw_contract_test.cpp"
        DIRECTX11_TIMEOUT 300 DIRECTX12_TIMEOUT 600)
    cna_d3d_parity_fixture(
        NAME DescriptorCapacityContract TARGET descriptor_capacity DIRECTX12_ORDER 90
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/descriptor_capacity_contract_test.cpp"
        DIRECTX11_TIMEOUT 900 DIRECTX12_TIMEOUT 600)
    cna_d3d_parity_fixture(
        NAME DescriptorAllocator TARGET descriptor_allocator SOURCE directx12_descriptor_allocator_test.cpp
        DIRECTX12_ORDER 100 DIRECTX12_ONLY DIRECTX12_NO_HEADLESS
        REASON "This executable probes D3D12-native descriptor allocator internals")
    cna_d3d_parity_fixture(
        NAME PointSamplingContract TARGET point_sampling DIRECTX12_ORDER 110
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/point_sampling_contract_test.cpp"
        DIRECTX11_TIMEOUT 300 DIRECTX12_TIMEOUT 600)
    cna_d3d_parity_fixture(
        NAME XnaPixelCenter TARGET xna_pixel_center DIRECTX12_ORDER 115
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/xna_pixel_center_contract_test.cpp")
    cna_d3d_parity_fixture(
        NAME RenderTarget_ProducerConsumer TARGET rt_producer_consumer
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_producer_consumer_test.cpp"
        DIRECTX11_TIMEOUT 120 DIRECTX11_ONLY
        REASON "D3D12 render-target corpus adoption is tracked by DX-232 and DX-236")
    cna_d3d_parity_fixture(
        NAME RenderTarget_EffectSource TARGET rt_effect_source DIRECTX12_ORDER 140
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_effect_source_test.cpp"
        DIRECTX11_TIMEOUT 300)
    cna_d3d_parity_fixture(
        NAME CubeVolume_GetDataContract TARGET cube_volume_getdata_contract DIRECTX12_ORDER 130
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/texturecube_texture3d_getdata_contract_test.cpp")
    cna_d3d_parity_fixture(
        NAME RenderTargetCube_GetDataContract TARGET rendertargetcube_getdata_contract DIRECTX12_ORDER 150
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/rendertargetcube_getdata_contract_test.cpp")
    cna_d3d_parity_fixture(
        NAME RenderTargetCube_Usage TARGET rendertargetcube_usage DIRECTX12_ORDER 160
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/rendertargetcube_usage_test.cpp")
    cna_d3d_parity_fixture(
        NAME RenderTargetCube_MsaaFace TARGET rendertargetcube_msaa_face DIRECTX12_ORDER 170
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/rendertargetcube_msaa_face_test.cpp")
    cna_d3d_parity_fixture(
        NAME RenderTarget_DepthStencilUsage TARGET rendertarget_depthstencil_usage DIRECTX12_ORDER 180
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_depthstencil_usage_test.cpp")
    cna_d3d_parity_fixture(
        NAME RenderTarget_PassBoundary TARGET rendertarget_pass_boundary DIRECTX12_ORDER 190
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_pass_boundary_test.cpp"
        DIRECTX11_TIMEOUT 90)
    cna_d3d_parity_fixture(
        NAME GraphicsDevice_OrderedClear TARGET ordered_clear
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/graphicsdevice_ordered_clear_test.cpp"
        DIRECTX11_TIMEOUT 120 DIRECTX11_ONLY
        REASON "D3D12 render-target corpus adoption is tracked by DX-232 and DX-236")
    cna_d3d_parity_fixture(
        NAME Backbuffer_PassOrder TARGET backbuffer_pass_order DIRECTX12_ORDER 200
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/backbuffer_pass_order_test.cpp"
        DIRECTX11_TIMEOUT 120)
    cna_d3d_parity_fixture(
        NAME Deferred_Viewport TARGET deferred_viewport
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/deferred_viewport_capture_test.cpp"
        DIRECTX11_TIMEOUT 120 DIRECTX11_ONLY
        REASON "D3D12 presentation-corpus adoption is tracked by DX-234 and DX-236")
    cna_d3d_parity_fixture(
        NAME Deferred_Scissor TARGET deferred_scissor DIRECTX12_ORDER 270
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/deferred_scissor_capture_test.cpp"
        DIRECTX11_TIMEOUT 120)
    cna_d3d_parity_fixture(
        NAME CubeVolume_SetDataContract TARGET cube_volume_setdata_contract DIRECTX12_ORDER 300
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/texturecube_texture3d_setdata_contract_test.cpp")
    cna_d3d_parity_fixture(
        NAME SpriteBatch_3DOrder TARGET spritebatch_3d_order DIRECTX12_ORDER 210
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/spritebatch_3d_order_test.cpp")
    cna_d3d_parity_fixture(
        NAME BlendState_BlendFactor TARGET blendstate_blendfactor DIRECTX12_ORDER 220
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_blendstate_blendfactor_test.cpp")
    cna_d3d_parity_fixture(
        NAME PrimitiveTypeValidation TARGET primitivetype_validation DIRECTX12_ORDER 230
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_primitivetype_validation_test.cpp")
    cna_d3d_parity_fixture(
        NAME SamplerConformance TARGET sampler_conformance DIRECTX12_ORDER 240
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/fna3d/examples/fna3d_sampler_test.cpp")
    cna_d3d_parity_fixture(
        NAME RenderTarget2D_Msaa TARGET rendertarget2d_msaa DIRECTX12_ORDER 250
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_rendertarget2d_msaa_test.cpp")
    cna_d3d_parity_fixture(
        NAME DepthBias TARGET depth_bias DIRECTX12_ORDER 260
        SOURCE "${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_depth_bias_test.cpp")
    cna_d3d_parity_fixture(
        NAME SpriteBatch_Scissor TARGET spritebatch_scissor DIRECTX12_ORDER 280
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/spritebatch_begin_rasterizerstate_scissor_test.cpp"
        DIRECTX12_ONLY REASON "D3D11 state-corpus adoption is tracked by DX-227")
    cna_d3d_parity_fixture(
        NAME RenderTarget_ViewportScissorReset TARGET rt_viewport_scissor_reset DIRECTX12_ORDER 290
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_viewport_scissor_reset_test.cpp"
        DIRECTX12_ONLY REASON "D3D11 render-target corpus adoption is tracked by DX-232")
    cna_d3d_parity_fixture(
        NAME RenderTarget_FirstUse TARGET rt_first_use
        SOURCE "${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_first_use_test.cpp"
        DIRECTX11_TIMEOUT 180 DIRECTX11_ONLY
        REASON "D3D12 render-target corpus adoption is tracked by DX-232 and DX-236")

    list(SORT _cna_d3d_fixture_keys)
    foreach(_cna_d3d_fixture_key IN LISTS _cna_d3d_fixture_keys)
        string(REGEX REPLACE "^[0-9]+:" "" _cna_d3d_fixture "${_cna_d3d_fixture_key}")

        if(CNA_GRAPHICS_RENDERER STREQUAL "DIRECTX11")
            if(_cna_d3d_fixture_${_cna_d3d_fixture}_DIRECTX12_ONLY)
                continue()
            endif()
            set(_cna_d3d_target "cna_test_directx11_${_cna_d3d_fixture_${_cna_d3d_fixture}_TARGET}")
            cna_directx11_test(${_cna_d3d_target} "${_cna_d3d_fixture_${_cna_d3d_fixture}_SOURCE}")
            cna_directx11_ctest_command(_cna_d3d_command ${_cna_d3d_target})
            set(_cna_d3d_timeout "${_cna_d3d_fixture_${_cna_d3d_fixture}_DIRECTX11_TIMEOUT}")
            if(NOT _cna_d3d_timeout)
                set(_cna_d3d_timeout 60)
            endif()
            set(_cna_d3d_registration
                NAME "DirectX11_${_cna_d3d_fixture}"
                COMMAND ${_cna_d3d_command}
                TIMEOUT ${_cna_d3d_timeout}
                LABELS "DIRECTX11")
            if(_cna_d3d_fixture_${_cna_d3d_fixture}_DIRECTX11_ENVIRONMENT)
                list(APPEND _cna_d3d_registration ENVIRONMENT
                    "${_cna_d3d_fixture_${_cna_d3d_fixture}_DIRECTX11_ENVIRONMENT}")
            endif()
        else()
            if(_cna_d3d_fixture_${_cna_d3d_fixture}_DIRECTX11_ONLY)
                continue()
            endif()
            set(_cna_d3d_target "cna_test_directx12_${_cna_d3d_fixture_${_cna_d3d_fixture}_TARGET}")
            cna_directx12_test(${_cna_d3d_target} "${_cna_d3d_fixture_${_cna_d3d_fixture}_SOURCE}")
            cna_directx12_ctest_command(_cna_d3d_command ${_cna_d3d_target})
            set(_cna_d3d_timeout "${_cna_d3d_fixture_${_cna_d3d_fixture}_DIRECTX12_TIMEOUT}")
            if(NOT _cna_d3d_timeout)
                set(_cna_d3d_timeout 300)
            endif()
            set(_cna_d3d_registration
                NAME "DirectX12_${_cna_d3d_fixture}"
                COMMAND ${_cna_d3d_command}
                TIMEOUT ${_cna_d3d_timeout}
                LABELS "DIRECTX12")
            if(NOT _cna_d3d_fixture_${_cna_d3d_fixture}_DIRECTX12_NO_HEADLESS)
                list(APPEND _cna_d3d_registration
                    ENVIRONMENT "CNA_FORCE_HEADLESS_DEVICE_EXT=DIRECTX12")
            endif()
        endif()

        if(_cna_d3d_fixture_${_cna_d3d_fixture}_WORKING_DIRECTORY)
            list(APPEND _cna_d3d_registration WORKING_DIRECTORY
                "${_cna_d3d_fixture_${_cna_d3d_fixture}_WORKING_DIRECTORY}")
        endif()
        cna_register_renderer_test(${_cna_d3d_registration})
    endforeach()
endfunction()
