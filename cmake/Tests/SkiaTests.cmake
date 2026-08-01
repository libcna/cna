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

    cna_skia_test(cna_test_skia_surface_raster examples/skia_surface_raster_test.cpp)
    # This test intentionally exercises the internal SkiaSurface boundary directly, so unlike
    # public-API tests it needs the external Skia header root while compiling its own source.
    target_include_directories(cna_test_skia_surface_raster PRIVATE "${CNA_SKIA_ROOT}")
    cna_register_skia_raster_test(Skia_Surface_Raster cna_test_skia_surface_raster)

    cna_skia_test(cna_test_skia_texture_alpha_boundary examples/skia_texture_alpha_boundary_test.cpp)
    target_include_directories(cna_test_skia_texture_alpha_boundary PRIVATE "${CNA_SKIA_ROOT}")
    cna_register_skia_raster_test(Skia_Texture_AlphaBoundary cna_test_skia_texture_alpha_boundary)

    cna_skia_test(cna_test_skia_graphics_capability examples/skia_graphics_capability_test.cpp)
    cna_register_skia_display_test(Skia_GraphicsCapability cna_test_skia_graphics_capability)

    cna_skia_test(cna_test_skia_texture2d_getdata_contract examples/texture2d_getdata_contract_test.cpp)
    cna_register_skia_display_test(Skia_Texture2D_GetDataContract cna_test_skia_texture2d_getdata_contract)

    cna_skia_test(cna_test_skia_texture2d_getdata_transfer_range examples/texture2d_getdata_transfer_range_test.cpp)
    cna_register_skia_display_test(Skia_Texture2D_GetDataTransferRange cna_test_skia_texture2d_getdata_transfer_range)

    cna_skia_test(cna_test_skia_texture_constraints examples/skia_texture_constraints_test.cpp)
    cna_register_skia_display_test(Skia_Texture2D_Constraints cna_test_skia_texture_constraints)

    # Pixel-level sampling companion to the CPU readback cases above: two odd NPOT dimensions,
    # source-row selection, PointClamp, and backbuffer readback all pass through the public API.
    cna_skia_test(cna_test_skia_npot_texture examples/sdlrenderer_npot_texture_test.cpp)
    cna_register_skia_display_test(Skia_Texture2D_NpotSampling cna_test_skia_npot_texture)

    cna_skia_test(cna_test_skia_spritebatch_begin_end examples/sdlrenderer_spritebatch_begin_end_guard_test.cpp)
    cna_register_skia_display_test(Skia_SpriteBatch_BeginEnd cna_test_skia_spritebatch_begin_end)

    cna_skia_test(cna_test_skia_spritebatch_sourcerect examples/sdlrenderer_spritebatch_sourcerect_test.cpp)
    cna_register_skia_display_test(Skia_SpriteBatch_SourceRect cna_test_skia_spritebatch_sourcerect)

    cna_skia_test(cna_test_skia_spritebatch_overloads examples/sdlrenderer_spritebatch_overloads_test.cpp)
    cna_register_skia_display_test(Skia_SpriteBatch_Overloads cna_test_skia_spritebatch_overloads)

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

    cna_skia_test(cna_test_skia_spritebatch_tint_alpha examples/skia_spritebatch_tint_alpha_test.cpp)
    cna_register_skia_display_test(Skia_SpriteBatch_TintAlpha cna_test_skia_spritebatch_tint_alpha)

    cna_skia_test(cna_test_skia_spritebatch_rotation examples/sdlrenderer_spritebatch_rotation_test.cpp)
    cna_register_skia_display_test(Skia_SpriteBatch_Rotation cna_test_skia_spritebatch_rotation)

    cna_skia_test(cna_test_skia_spritebatch_scale examples/sdlrenderer_spritebatch_scale_test.cpp)
    cna_register_skia_display_test(Skia_SpriteBatch_Scale cna_test_skia_spritebatch_scale)

    cna_skia_test(cna_test_skia_spritebatch_negative_scale examples/skia_spritebatch_negative_scale_test.cpp)
    cna_register_skia_display_test(Skia_SpriteBatch_NegativeScale cna_test_skia_spritebatch_negative_scale)

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

    cna_skia_test(cna_test_skia_rendertarget_usage examples/sdlrenderer_rendertarget_usage_test.cpp)
    cna_register_skia_display_test(Skia_RenderTarget2D_Usage cna_test_skia_rendertarget_usage)

    cna_skia_test(cna_test_skia_getbackbuffer_after_rt_unbind examples/sdlrenderer_getbackbufferdata_after_rt_unbind_test.cpp)
    cna_register_skia_display_test(Skia_GetBackBufferData_AfterRtUnbind cna_test_skia_getbackbuffer_after_rt_unbind)

    cna_skia_test(cna_test_skia_rendertarget_readback examples/skia_rendertarget_readback_test.cpp)
    cna_register_skia_display_test(Skia_RenderTarget2D_Readback cna_test_skia_rendertarget_readback)

    cna_skia_test(cna_test_skia_rendertarget_switch examples/skia_rendertarget_switch_test.cpp)
    cna_register_skia_display_test(Skia_RenderTarget2D_Switch cna_test_skia_rendertarget_switch)

    if(TARGET cna_demo_2d)
        cna_register_skia_display_test(Skia_Demo2D_Smoke cna_demo_2d --smoke 3)
    endif()
endif()
