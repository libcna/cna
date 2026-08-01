# The initial SKIA implementation is a deterministic CPU-raster surface presented by SDL. Keep
# its CTests split by execution requirement: raster tests never create a window, while presentation
# tests require a real X11/virtual display. The accelerated helper is deliberately distinct so a
# future Ganesh/Graphite path cannot accidentally inherit raster-only registration.
if(CNA_BUILD_TESTS AND NOT EMSCRIPTEN AND NOT WIN32
   AND CNA_GRAPHICS_BACKEND STREQUAL "SKIA")
    enable_testing()

    macro(cna_skia_test target src)
        add_executable(${target} ${src})
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            target_link_libraries(${target} PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME SDL3::SDL3)
        else()
            target_link_libraries(${target} PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(${target} PRIVATE SDL3::SDL3main)
        endif()
    endmacro()

    function(cna_register_skia_raster_test name)
        cna_register_backend_test(NAME ${name} COMMAND ${ARGN} TIMEOUT 30 LABELS "Skia;Raster")
    endfunction()

    function(cna_register_skia_display_test name)
        cna_register_backend_test(NAME ${name} COMMAND ${ARGN} TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}"
            LABELS "Skia;Display")
    endfunction()

    function(cna_register_skia_accelerated_test name)
        cna_register_backend_test(NAME ${name} COMMAND ${ARGN} TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}"
            LABELS "Skia;Accelerated;Display")
    endfunction()

    # SKIA-1: standard-library source audit. Keep it separate from Raster because it neither links
    # Skia nor exercises pixels; it verifies that every current graphics API entry is classified.
    find_program(CNA_SKIA_LEDGER_PYTHON NAMES python3 REQUIRED)
    cna_register_backend_test(
        NAME Skia_ParityLedger_Audit
        COMMAND "${CNA_SKIA_LEDGER_PYTHON}"
                "${CMAKE_SOURCE_DIR}/scripts/validate_skia_parity_ledger.py"
                "${CMAKE_SOURCE_DIR}"
        TIMEOUT 30
        LABELS "Skia;Audit")
    cna_register_backend_test(
        NAME Skia_TestMatrix_Audit
        COMMAND "${CNA_SKIA_LEDGER_PYTHON}"
                "${CMAKE_SOURCE_DIR}/scripts/validate_skia_test_matrix.py"
                "${CMAKE_SOURCE_DIR}"
        TIMEOUT 30
        LABELS "Skia;Audit")

    cna_skia_test(cna_test_skia_surface_raster examples/skia_surface_raster_test.cpp)
    # This test intentionally exercises the internal SkiaSurface boundary directly, so unlike
    # public-API tests it needs the external Skia header root while compiling its own source.
    target_include_directories(cna_test_skia_surface_raster PRIVATE "${CNA_SKIA_ROOT}")
    cna_register_skia_raster_test(Skia_Surface_Raster cna_test_skia_surface_raster)

    cna_skia_test(cna_test_skia_texture_alpha_boundary examples/skia_texture_alpha_boundary_test.cpp)
    target_include_directories(cna_test_skia_texture_alpha_boundary PRIVATE "${CNA_SKIA_ROOT}")
    cna_register_skia_raster_test(Skia_Texture_AlphaBoundary cna_test_skia_texture_alpha_boundary)

    cna_skia_test(cna_test_skia_target_binding_raster examples/skia_target_binding_raster_test.cpp)
    target_include_directories(cna_test_skia_target_binding_raster PRIVATE "${CNA_SKIA_ROOT}")
    cna_register_skia_raster_test(Skia_RenderTargetBinding_Raster cna_test_skia_target_binding_raster)

    cna_skia_test(cna_test_skia_blend_mapping_raster examples/skia_blend_mapping_raster_test.cpp)
    target_include_directories(cna_test_skia_blend_mapping_raster PRIVATE "${CNA_SKIA_ROOT}")
    cna_register_skia_raster_test(Skia_BlendMapping_Raster cna_test_skia_blend_mapping_raster)

    cna_skia_test(cna_test_skia_runtime_blender_raster examples/skia_runtime_blender_raster_test.cpp)
    target_include_directories(cna_test_skia_runtime_blender_raster PRIVATE "${CNA_SKIA_ROOT}")
    cna_register_skia_raster_test(Skia_RuntimeBlender_Raster cna_test_skia_runtime_blender_raster)

    cna_skia_test(cna_test_skia_color_write_mask_raster examples/skia_color_write_mask_raster_test.cpp)
    target_include_directories(cna_test_skia_color_write_mask_raster PRIVATE "${CNA_SKIA_ROOT}")
    cna_register_skia_raster_test(Skia_ColorWriteMask_Raster cna_test_skia_color_write_mask_raster)

    cna_skia_test(cna_test_skia_startup_diagnostic examples/skia_startup_diagnostic_test.cpp)
    cna_register_skia_raster_test(Skia_StartupDiagnostic_Raster cna_test_skia_startup_diagnostic)

    cna_skia_test(cna_test_skia_lifecycle examples/skia_lifecycle_test.cpp)
    target_include_directories(cna_test_skia_lifecycle PRIVATE "${CNA_SKIA_ROOT}")
    cna_register_skia_display_test(Skia_Lifecycle cna_test_skia_lifecycle)

    cna_skia_test(cna_test_skia_ownership examples/skia_ownership_test.cpp)
    target_include_directories(cna_test_skia_ownership PRIVATE "${CNA_SKIA_ROOT}")
    cna_register_skia_display_test(Skia_Ownership cna_test_skia_ownership)

    cna_skia_test(cna_test_skia_present_interval examples/skia_present_interval_test.cpp)
    target_include_directories(cna_test_skia_present_interval PRIVATE "${CNA_SKIA_ROOT}")
    cna_register_skia_display_test(Skia_PresentInterval cna_test_skia_present_interval)

    cna_skia_test(cna_test_skia_graphics_capability examples/skia_graphics_capability_test.cpp)
    cna_register_skia_display_test(Skia_GraphicsCapability cna_test_skia_graphics_capability)

    # SKIA-89: keep untagged backend-specific ShaderEffect strings outside the SkSL path and prove
    # that rejection cannot poison the following built-in SpriteBatch session.
    cna_skia_test(cna_test_skia_effect_boundary examples/skia_effect_boundary_test.cpp)
    cna_register_skia_display_test(Skia_Effect_Boundary cna_test_skia_effect_boundary)

    # SKIA-90: the exact stock SpriteEffect is a semantic alias for the already-proven built-in
    # paint path. Derived/custom effects remain rejected rather than being silently ignored.
    cna_skia_test(cna_test_skia_spriteeffect_alias examples/skia_spriteeffect_alias_test.cpp)
    cna_register_skia_display_test(Skia_SpriteEffect_Alias cna_test_skia_spriteeffect_alias)

    # SKIA-91: only the exact CNA_SKIA_SKSL_V1 marker opts a ShaderEffect into the bounded
    # fragment-only runtime-shader ABI. Pixel checks prove its reserved texture child, while bad
    # source/ABI/size cases retain actionable diagnostics and cannot poison the stock path.
    cna_skia_test(cna_test_skia_sksl_effect_prototype
                  examples/skia_sksl_effect_prototype_test.cpp)
    cna_register_skia_display_test(Skia_SkSL_Effect_Prototype
                                   cna_test_skia_sksl_effect_prototype)

    # SKIA-92: reflected scalar/vector/matrix/array writes and one weak live additional 2D child.
    # The fixture also exercises every deterministic validation boundary and clone isolation.
    cna_skia_test(cna_test_skia_sksl_uniform_texture
                  examples/skia_sksl_uniform_texture_test.cpp)
    cna_register_skia_display_test(Skia_SkSL_UniformTexture
                                   cna_test_skia_sksl_uniform_texture)

    # SKIA-80--SKIA-84: cube/volume resources are bounded CPU transfer storage, not shader-
    # sampleable 3D resources. The direct raster test proves the allocation/validation policy;
    # shared public tests prove every face, mip, rectangle, slice, box, and failure contract.
    cna_skia_test(cna_test_skia_texture_storage_policy examples/skia_texture_storage_policy_test.cpp)
    cna_register_skia_raster_test(Skia_TextureStorage_Policy cna_test_skia_texture_storage_policy)

    cna_skia_test(cna_test_skia_rendertargetcube_policy
                  examples/skia_rendertargetcube_policy_test.cpp)
    target_include_directories(cna_test_skia_rendertargetcube_policy PRIVATE "${CNA_SKIA_ROOT}")
    cna_register_skia_raster_test(Skia_RenderTargetCube_Policy
                                  cna_test_skia_rendertargetcube_policy)

    cna_skia_test(cna_test_skia_texturecube_faces examples/easygl_texturecube_faces_test.cpp)
    cna_register_skia_display_test(Skia_TextureCube_Faces_RoundTrip cna_test_skia_texturecube_faces)

    cna_skia_test(cna_test_skia_texturecube_partial_rect examples/easygl_texturecube_partial_rect_test.cpp)
    cna_register_skia_display_test(Skia_TextureCube_PartialRect_RoundTrip cna_test_skia_texturecube_partial_rect)

    cna_skia_test(cna_test_skia_texturecube_mip examples/easygl_texturecube_mip_test.cpp)
    cna_register_skia_display_test(Skia_TextureCube_Mip_RoundTrip cna_test_skia_texturecube_mip)

    cna_skia_test(cna_test_skia_texturecube_content_load
                  examples/easygl_texturecube_content_load_test.cpp)
    cna_register_skia_display_test(Skia_TextureCube_ContentLoad cna_test_skia_texturecube_content_load)

    cna_skia_test(cna_test_skia_texture3d_slices examples/easygl_texture3d_slices_test.cpp)
    cna_register_skia_display_test(Skia_Texture3D_Slices_RoundTrip cna_test_skia_texture3d_slices)

    cna_skia_test(cna_test_skia_texture3d_partial_box examples/easygl_texture3d_partial_box_test.cpp)
    cna_register_skia_display_test(Skia_Texture3D_PartialBox_RoundTrip cna_test_skia_texture3d_partial_box)

    cna_skia_test(cna_test_skia_texture3d_partial_box_readback examples/easygl_texture3d_partial_box_readback_test.cpp)
    cna_register_skia_display_test(Skia_Texture3D_PartialBox_Readback cna_test_skia_texture3d_partial_box_readback)

    cna_skia_test(cna_test_skia_texture3d_mip examples/easygl_texture3d_mip_test.cpp)
    cna_register_skia_display_test(Skia_Texture3D_Mip_RoundTrip cna_test_skia_texture3d_mip)

    cna_skia_test(cna_test_skia_texture_storage_getdata_contract
                  examples/texturecube_texture3d_getdata_contract_test.cpp)
    cna_register_skia_display_test(Skia_TextureStorage_GetDataContract
                                   cna_test_skia_texture_storage_getdata_contract)

    cna_skia_test(cna_test_skia_texture_storage_setdata_contract
                  examples/texturecube_texture3d_setdata_contract_test.cpp)
    cna_register_skia_display_test(Skia_TextureStorage_SetDataContract
                                   cna_test_skia_texture_storage_setdata_contract)

    cna_skia_test(cna_test_skia_texture2d_getdata_contract examples/texture2d_getdata_contract_test.cpp)
    cna_register_skia_display_test(Skia_Texture2D_GetDataContract cna_test_skia_texture2d_getdata_contract)

    cna_skia_test(cna_test_skia_texture2d_getdata_transfer_range examples/texture2d_getdata_transfer_range_test.cpp)
    cna_register_skia_display_test(Skia_Texture2D_GetDataTransferRange cna_test_skia_texture2d_getdata_transfer_range)

    cna_skia_test(cna_test_skia_texture_constraints examples/skia_texture_constraints_test.cpp)
    cna_register_skia_display_test(Skia_Texture2D_Constraints cna_test_skia_texture_constraints)

    cna_skia_test(cna_test_skia_texture2d_dispose examples/sdlrenderer_texture2d_dispose_test.cpp)
    cna_register_skia_display_test(Skia_Texture2D_Dispose cna_test_skia_texture2d_dispose)

    cna_skia_test(cna_test_skia_mipmap_policy examples/skia_mipmap_policy_test.cpp)
    cna_register_skia_display_test(Skia_Texture2D_MipmapPolicy cna_test_skia_mipmap_policy)

    cna_skia_test(cna_test_skia_sampler_mipmap_filter examples/skia_sampler_mipmap_filter_test.cpp)
    cna_register_skia_display_test(Skia_Sampler_MipmapFilterPolicy cna_test_skia_sampler_mipmap_filter)

    # Pixel-level sampling companion to the CPU readback cases above: two odd NPOT dimensions,
    # source-row selection, PointClamp, and backbuffer readback all pass through the public API.
    cna_skia_test(cna_test_skia_npot_texture examples/sdlrenderer_npot_texture_test.cpp)
    cna_register_skia_display_test(Skia_Texture2D_NpotSampling cna_test_skia_npot_texture)

    cna_skia_test(cna_test_skia_spritebatch_begin_end examples/sdlrenderer_spritebatch_begin_end_guard_test.cpp)
    cna_register_skia_display_test(Skia_SpriteBatch_BeginEnd cna_test_skia_spritebatch_begin_end)

    cna_skia_test(cna_test_skia_disposed_guards examples/sdlrenderer_disposed_guards_test.cpp)
    cna_register_skia_display_test(Skia_DisposedGuards cna_test_skia_disposed_guards)

    cna_skia_test(cna_test_skia_double_dispose examples/sdlrenderer_double_dispose_test.cpp)
    cna_register_skia_display_test(Skia_DoubleDispose cna_test_skia_double_dispose)

    cna_skia_test(cna_test_skia_spritebatch_sourcerect examples/sdlrenderer_spritebatch_sourcerect_test.cpp)
    cna_register_skia_display_test(Skia_SpriteBatch_SourceRect cna_test_skia_spritebatch_sourcerect)

    cna_skia_test(cna_test_skia_spritebatch_overloads examples/sdlrenderer_spritebatch_overloads_test.cpp)
    cna_register_skia_display_test(Skia_SpriteBatch_Overloads cna_test_skia_spritebatch_overloads)

    cna_skia_test(cna_test_skia_spritebatch_remaining_overloads examples/skia_spritebatch_remaining_overloads_test.cpp)
    cna_register_skia_display_test(Skia_SpriteBatch_RemainingOverloads cna_test_skia_spritebatch_remaining_overloads)

    cna_skia_test(cna_test_skia_spritebatch_stress examples/skia_spritebatch_stress_test.cpp)
    cna_register_skia_display_test(Skia_SpriteBatch_Stress cna_test_skia_spritebatch_stress)

    # These three presets establish the alpha convention used by the raster backend:
    # opaque source replacement, premultiplied-alpha source-over, and straight-alpha source-over.
    cna_skia_test(cna_test_skia_blend_opaque examples/sdlrenderer_blendstate_opaque_test.cpp)
    cna_register_skia_display_test(Skia_BlendState_Opaque cna_test_skia_blend_opaque)

    cna_skia_test(cna_test_skia_blend_alphablend examples/sdlrenderer_blendstate_alphablend_test.cpp)
    cna_register_skia_display_test(Skia_BlendState_AlphaBlend cna_test_skia_blend_alphablend)

    cna_skia_test(cna_test_skia_blend_nonpremultiplied examples/sdlrenderer_blendstate_nonpremultiplied_test.cpp)
    cna_register_skia_display_test(Skia_BlendState_NonPremultiplied cna_test_skia_blend_nonpremultiplied)

    cna_skia_test(cna_test_skia_blend_additive examples/sdlrenderer_blendstate_additive_test.cpp)
    cna_register_skia_display_test(Skia_BlendState_Additive cna_test_skia_blend_additive)

    cna_skia_test(cna_test_skia_blend_enabled_state examples/skia_blend_enabled_state_test.cpp)
    cna_register_skia_display_test(Skia_BlendEnabled_State cna_test_skia_blend_enabled_state)

    cna_skia_test(cna_test_skia_blend_mapping_policy examples/skia_blend_mapping_policy_test.cpp)
    cna_register_skia_display_test(Skia_BlendMapping_Policy cna_test_skia_blend_mapping_policy)

    cna_skia_test(cna_test_skia_runtime_blender_policy examples/skia_runtime_blender_policy_test.cpp)
    cna_register_skia_display_test(Skia_RuntimeBlender_Policy cna_test_skia_runtime_blender_policy)

    cna_skia_test(cna_test_skia_color_write_policy examples/skia_color_write_policy_test.cpp)
    cna_register_skia_display_test(Skia_ColorWrite_Policy cna_test_skia_color_write_policy)

    cna_skia_test(cna_test_skia_spritebatch_tint_alpha examples/skia_spritebatch_tint_alpha_test.cpp)
    cna_register_skia_display_test(Skia_SpriteBatch_TintAlpha cna_test_skia_spritebatch_tint_alpha)

    cna_skia_test(cna_test_skia_spritebatch_rotation examples/sdlrenderer_spritebatch_rotation_test.cpp)
    cna_register_skia_display_test(Skia_SpriteBatch_Rotation cna_test_skia_spritebatch_rotation)

    cna_skia_test(cna_test_skia_spritebatch_scale examples/sdlrenderer_spritebatch_scale_test.cpp)
    cna_register_skia_display_test(Skia_SpriteBatch_Scale cna_test_skia_spritebatch_scale)

    cna_skia_test(cna_test_skia_spritebatch_negative_scale examples/skia_spritebatch_negative_scale_test.cpp)
    cna_register_skia_display_test(Skia_SpriteBatch_NegativeScale cna_test_skia_spritebatch_negative_scale)

    # A non-identity transform must compose after the sprite's own geometry. This is a shared
    # public-API probe: it uses scale and translation, so a translation-only implementation fails.
    cna_skia_test(cna_test_skia_transform_matrix examples/vulkan_transform_matrix_test.cpp)
    cna_register_skia_display_test(Skia_SpriteBatch_TransformMatrix cna_test_skia_transform_matrix)

    cna_skia_test(cna_test_skia_spritebatch_source_rect_linear examples/skia_spritebatch_source_rect_linear_test.cpp)
    cna_register_skia_display_test(Skia_SpriteBatch_SourceRectLinear cna_test_skia_spritebatch_source_rect_linear)

    # Shared public SpriteBatch test: its Begin-supplied RasterizerState must enable/disable the
    # ScissorRectangle for deferred, immediate, and front-to-back submissions.
    cna_skia_test(cna_test_skia_spritebatch_rasterizerstate examples/software_spritebatch_rasterizerstate_test.cpp)
    cna_register_skia_display_test(Skia_SpriteBatch_RasterizerState cna_test_skia_spritebatch_rasterizerstate)

    cna_skia_test(cna_test_skia_rasterizer_state_policy examples/skia_rasterizer_state_policy_test.cpp)
    cna_register_skia_display_test(Skia_RasterizerState_Policy cna_test_skia_rasterizer_state_policy)

    cna_skia_test(cna_test_skia_state_transition examples/skia_state_transition_test.cpp)
    cna_register_skia_display_test(Skia_StateTransition cna_test_skia_state_transition)

    cna_skia_test(cna_test_skia_spritebatch_viewport examples/skia_spritebatch_viewport_test.cpp)
    cna_register_skia_display_test(Skia_SpriteBatch_Viewport cna_test_skia_spritebatch_viewport)

    cna_skia_test(cna_test_skia_viewport_project_unproject examples/sdlrenderer_viewport_project_unproject_test.cpp)
    cna_register_skia_display_test(Skia_Viewport_ProjectUnproject cna_test_skia_viewport_project_unproject)

    cna_skia_test(cna_test_skia_sprite_effects examples/sdlrenderer_sprite_effects_test.cpp)
    cna_register_skia_display_test(Skia_SpriteBatch_Effects cna_test_skia_sprite_effects)

    cna_skia_test(cna_test_skia_spritebatch_deferred_order examples/sdlrenderer_spritebatch_deferred_order_test.cpp)
    cna_register_skia_display_test(Skia_SpriteBatch_DeferredOrder cna_test_skia_spritebatch_deferred_order)

    cna_skia_test(cna_test_skia_spritebatch_immediate_flush examples/sdlrenderer_spritebatch_immediate_flush_test.cpp)
    cna_register_skia_display_test(Skia_SpriteBatch_ImmediateFlush cna_test_skia_spritebatch_immediate_flush)

    cna_skia_test(cna_test_skia_spritebatch_layerdepth examples/sdlrenderer_spritebatch_layerdepth_test.cpp)
    cna_register_skia_display_test(Skia_SpriteBatch_LayerDepth cna_test_skia_spritebatch_layerdepth)

    cna_skia_test(cna_test_skia_spritebatch_texture_sort examples/sdlrenderer_spritebatch_texture_sort_test.cpp)
    cna_register_skia_display_test(Skia_SpriteBatch_TextureSort cna_test_skia_spritebatch_texture_sort)

    cna_skia_test(cna_test_skia_texture_filter_point_linear examples/sdlrenderer_texture_filter_point_vs_linear_test.cpp)
    cna_register_skia_display_test(Skia_TextureFilter_PointVsLinear cna_test_skia_texture_filter_point_linear)

    cna_skia_test(cna_test_skia_texture_filter_minification examples/skia_texture_filter_minification_test.cpp)
    cna_register_skia_display_test(Skia_TextureFilter_Minification cna_test_skia_texture_filter_minification)

    cna_skia_test(cna_test_skia_spritebatch_sampler_transition examples/skia_spritebatch_sampler_transition_test.cpp)
    cna_register_skia_display_test(Skia_SpriteBatch_SamplerTransition cna_test_skia_spritebatch_sampler_transition)

    cna_skia_test(cna_test_skia_texture_address_axes examples/skia_texture_address_axes_test.cpp)
    cna_register_skia_display_test(Skia_TextureAddressAxes cna_test_skia_texture_address_axes)

    cna_skia_test(cna_test_skia_spritefont_single_glyph examples/sdlrenderer_spritefont_single_glyph_test.cpp)
    cna_register_skia_display_test(Skia_SpriteFont_SingleGlyph cna_test_skia_spritefont_single_glyph)

    cna_skia_test(cna_test_skia_spritefont_multiglyph_spacing examples/sdlrenderer_spritefont_multiglyph_spacing_test.cpp)
    cna_register_skia_display_test(Skia_SpriteFont_MultiGlyphSpacing cna_test_skia_spritefont_multiglyph_spacing)

    cna_skia_test(cna_test_skia_spritefont_newline examples/sdlrenderer_spritefont_newline_test.cpp)
    cna_register_skia_display_test(Skia_SpriteFont_Newline cna_test_skia_spritefont_newline)

    cna_skia_test(cna_test_skia_spritefont_default_char examples/sdlrenderer_spritefont_default_char_test.cpp)
    cna_register_skia_display_test(Skia_SpriteFont_DefaultChar cna_test_skia_spritefont_default_char)

    cna_skia_test(cna_test_skia_spritefont_effects examples/sdlrenderer_spritefont_effects_test.cpp)
    cna_register_skia_display_test(Skia_SpriteFont_Effects cna_test_skia_spritefont_effects)

    cna_skia_test(cna_test_skia_rendertarget_sample examples/sdlrenderer_rendertarget2d_sample_test.cpp)
    cna_register_skia_display_test(Skia_RenderTarget2D_SampleAfterUnbind cna_test_skia_rendertarget_sample)

    cna_skia_test(cna_test_skia_rendertarget_golden examples/rendertarget2d_golden_test.cpp)
    cna_register_skia_display_test(Skia_RenderTarget2D_Golden cna_test_skia_rendertarget_golden)

    cna_skia_test(cna_test_skia_rendertarget_usage examples/sdlrenderer_rendertarget_usage_test.cpp)
    cna_register_skia_display_test(Skia_RenderTarget2D_Usage cna_test_skia_rendertarget_usage)

    cna_skia_test(cna_test_skia_rendertarget_setdata examples/skia_rendertarget_setdata_test.cpp)
    cna_register_skia_display_test(Skia_RenderTarget2D_SetData cna_test_skia_rendertarget_setdata)

    cna_skia_test(cna_test_skia_getbackbuffer_after_rt_unbind examples/sdlrenderer_getbackbufferdata_after_rt_unbind_test.cpp)
    cna_register_skia_display_test(Skia_GetBackBufferData_AfterRtUnbind cna_test_skia_getbackbuffer_after_rt_unbind)

    cna_skia_test(cna_test_skia_active_target_readback examples/skia_active_target_readback_test.cpp)
    cna_register_skia_display_test(Skia_GetBackBufferData_ActiveTarget cna_test_skia_active_target_readback)

    cna_skia_test(cna_test_skia_rendertarget_readback examples/skia_rendertarget_readback_test.cpp)
    cna_register_skia_display_test(Skia_RenderTarget2D_Readback cna_test_skia_rendertarget_readback)

    cna_skia_test(cna_test_skia_rendertarget_depth_policy examples/skia_rendertarget_depth_policy_test.cpp)
    cna_register_skia_display_test(Skia_RenderTarget2D_DepthPolicy cna_test_skia_rendertarget_depth_policy)

    cna_skia_test(cna_test_skia_rendertarget_msaa_policy examples/skia_rendertarget_msaa_policy_test.cpp)
    cna_register_skia_display_test(Skia_RenderTarget2D_MsaaPolicy cna_test_skia_rendertarget_msaa_policy)

    cna_skia_test(cna_test_skia_rendertarget_switch examples/skia_rendertarget_switch_test.cpp)
    cna_register_skia_display_test(Skia_RenderTarget2D_Switch cna_test_skia_rendertarget_switch)

    cna_skia_test(cna_test_skia_rendertarget_scissor examples/skia_rendertarget_scissor_test.cpp)
    cna_register_skia_display_test(Skia_RenderTarget2D_Scissor cna_test_skia_rendertarget_scissor)

    cna_skia_test(cna_test_skia_rendertarget_lifetime examples/skia_rendertarget_lifetime_test.cpp)
    cna_register_skia_display_test(Skia_RenderTarget2D_Lifetime cna_test_skia_rendertarget_lifetime)

    # SKIA-85--SKIA-86: RenderTargetCube is a six-surface CPU-raster emulation. These shared
    # contracts cover every face, rendered/uploaded readback, generated mips, Preserve/Discard,
    # truthful zero-sample MSAA clamping, public properties, rebinding, and disposal.
    cna_skia_test(cna_test_skia_rendertargetcube_getdata
                  examples/rendertargetcube_getdata_contract_test.cpp)
    cna_register_skia_display_test(Skia_RenderTargetCube_GetDataContract
                                   cna_test_skia_rendertargetcube_getdata)

    cna_skia_test(cna_test_skia_rendertargetcube_usage
                  examples/rendertargetcube_usage_test.cpp)
    cna_register_skia_display_test(Skia_RenderTargetCube_Usage
                                   cna_test_skia_rendertargetcube_usage)

    cna_skia_test(cna_test_skia_rendertargetcube_properties
                  examples/easygl_rendertargetcube_properties_test.cpp)
    cna_register_skia_display_test(Skia_RenderTargetCube_Properties
                                   cna_test_skia_rendertargetcube_properties)

    cna_skia_test(cna_test_skia_rendertargetcube_plural_binding
                  examples/rendertargetcube_plural_binding_test.cpp)
    target_compile_definitions(cna_test_skia_rendertargetcube_plural_binding PRIVATE
                               CNA_GFX096_CUBE_GETDATA=1)
    cna_register_skia_display_test(Skia_RenderTargetCube_PluralBinding
                                   cna_test_skia_rendertargetcube_plural_binding)

    # SKIA-87--SKIA-88: SkCanvas cannot represent distinct fragment outputs for MRT slots 0--3.
    # Verify that every valid 2--4 target set rejects before mutating target/state/pixels and that
    # shared validation errors retain their deterministic precedence.
    cna_skia_test(cna_test_skia_mrt_rejection examples/skia_mrt_rejection_test.cpp)
    cna_register_skia_display_test(Skia_MRT_Rejection cna_test_skia_mrt_rejection)

    cna_skia_test(cna_test_skia_resize_presentation examples/skia_resize_presentation_test.cpp)
    cna_register_skia_display_test(Skia_Resize_Presentation cna_test_skia_resize_presentation)

    cna_skia_test(cna_test_skia_context_recovery examples/skia_context_recovery_test.cpp)
    target_include_directories(cna_test_skia_context_recovery PRIVATE "${CNA_SKIA_ROOT}")
    cna_register_skia_display_test(Skia_ContextRecovery cna_test_skia_context_recovery)

    cna_skia_test(cna_test_skia_display_scale examples/skia_display_scale_test.cpp)
    target_include_directories(cna_test_skia_display_scale PRIVATE "${CNA_SKIA_ROOT}")
    cna_register_skia_display_test(Skia_DisplayScale cna_test_skia_display_scale)

    cna_skia_test(cna_test_skia_presentation_modes examples/skia_presentation_modes_test.cpp)
    target_include_directories(cna_test_skia_presentation_modes PRIVATE "${CNA_SKIA_ROOT}")
    cna_register_skia_display_test(Skia_PresentationModes cna_test_skia_presentation_modes)

    cna_skia_test(cna_test_skia_window_lifecycle examples/skia_window_lifecycle_test.cpp)
    target_include_directories(cna_test_skia_window_lifecycle PRIVATE "${CNA_SKIA_ROOT}")
    cna_register_skia_display_test(Skia_WindowLifecycle cna_test_skia_window_lifecycle)

    cna_skia_test(cna_test_skia_resource_budget examples/skia_resource_budget_test.cpp)
    target_include_directories(cna_test_skia_resource_budget PRIVATE "${CNA_SKIA_ROOT}")
    cna_register_skia_display_test(Skia_ResourceBudget cna_test_skia_resource_budget)

    cna_skia_test(cna_test_skia_presentation_edge examples/skia_presentation_edge_test.cpp)
    cna_register_skia_display_test(Skia_Presentation_Edge cna_test_skia_presentation_edge)

    if(TARGET cna_demo_2d)
        cna_register_skia_display_test(Skia_Demo2D_Smoke cna_demo_2d --smoke 3)
    endif()
endif()
