# sdl-renderer renderer example/test registration (module-local; this file is the
# former cmake/Tests/SdlRendererTests.cmake, moved when examples moved into their owning
# physical modules).
#
# From the root CMakeLists.txt include site:
# SDL_Renderer integration tests (Task 666 audit phase)

# Example/test executables historically live at the build root (scripts, docs and
# the golden-image working-directory conventions rely on it); keep them there.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
# Shared example-support headers ("common/PixelTestGame.hpp", ...) are owned by
# the graphics module's examples directory.
include_directories(${CNA_GRAPHICS_EXAMPLES_DIR})

if(CNA_BUILD_TESTS AND NOT EMSCRIPTEN AND NOT WIN32
   AND CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")

    enable_testing()

    # --- helper macro: build a headless SDL_Renderer test exe --------------------
    macro(cna_sdl_test target src)
        add_executable(${target} ${src})
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(${target} PRIVATE
                CNA
                SHARP_RUNTIME)
        else()
            target_link_libraries(${target} PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(${target} PRIVATE SDL3::SDL3main)
        endif()
    endmacro()

    # Ad hoc (not a permanent registered test): reuses the same SOFTWARE-renderer
    # screenshot demo scene to prove a real SDL_Renderer screenshot under Xvfb.
    # The source deliberately stays a repository-level cross-renderer diagnostic,
    # so it is spelled with an explicit ${CMAKE_SOURCE_DIR} path like the other
    # non-module-owned sources this file builds.
    cna_sdl_test(cna_xvfb_screenshot_demo ${CMAKE_SOURCE_DIR}/examples/xvfb_screenshot_demo.cpp)

    # Task 915: GetBackBufferData readback -- foundational prerequisite for the whole
    # SDL_Renderer pixel-test audit phase (Tasks 666-861).
    cna_sdl_test(cna_test_sdl_readback
                 sdlrenderer_readback_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_Readback COMMAND cna_test_sdl_readback
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 666: audit all SpriteBatch::Draw overloads through SDL_Renderer
    cna_sdl_test(cna_test_sdl_spritebatch_overloads
                 sdlrenderer_spritebatch_overloads_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_SpriteBatch_Overloads COMMAND cna_test_sdl_spritebatch_overloads
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 671: rotation around origin -- found and fixed a real SDL_RenderTextureRotated
    # pivot-offset bug while writing this test.
    cna_sdl_test(cna_test_sdl_spritebatch_rotation
                 sdlrenderer_spritebatch_rotation_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_SpriteBatch_Rotation COMMAND cna_test_sdl_spritebatch_rotation
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 669: SpriteSortMode::FrontToBack layerDepth ordering on SDL_Renderer -- direct port of
    # Task 420's EasyGL test (PresentationMode::NativeBackBuffer required for exact-pixel readback,
    # see Task 915).
    cna_sdl_test(cna_test_sdl_spritebatch_layerdepth
                 sdlrenderer_spritebatch_layerdepth_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_SpriteBatch_LayerDepth COMMAND cna_test_sdl_spritebatch_layerdepth
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 667: SpriteSortMode::Deferred submission-order pixel test on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_spritebatch_deferred_order
                 sdlrenderer_spritebatch_deferred_order_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_SpriteBatch_DeferredOrder COMMAND cna_test_sdl_spritebatch_deferred_order
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 668: SpriteSortMode::Texture grouping pixel test on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_spritebatch_texture_sort
                 sdlrenderer_spritebatch_texture_sort_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_SpriteBatch_TextureSort COMMAND cna_test_sdl_spritebatch_texture_sort
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 670: SpriteSortMode::Immediate per-draw flush pixel test on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_spritebatch_immediate_flush
                 sdlrenderer_spritebatch_immediate_flush_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_SpriteBatch_ImmediateFlush COMMAND cna_test_sdl_spritebatch_immediate_flush
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 672: scalar/Vector2 scale overload pixel test on SDL_Renderer -- direct port of
    # Task 418's EasyGL test.
    cna_sdl_test(cna_test_sdl_spritebatch_scale
                 sdlrenderer_spritebatch_scale_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_SpriteBatch_Scale COMMAND cna_test_sdl_spritebatch_scale
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 673: source rectangle cropping pixel test on SDL_Renderer -- direct port of
    # Task 419's EasyGL test.
    cna_sdl_test(cna_test_sdl_spritebatch_sourcerect
                 sdlrenderer_spritebatch_sourcerect_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_SpriteBatch_SourceRect COMMAND cna_test_sdl_spritebatch_sourcerect
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 674: SpriteEffects::FlipHorizontally/FlipVertically pixel test on SDL_Renderer --
    # direct port of Task 167's EasyGL test.
    cna_sdl_test(cna_test_sdl_sprite_effects
                 sdlrenderer_sprite_effects_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_SpriteEffects COMMAND cna_test_sdl_sprite_effects
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 675: transformMatrix in SpriteBatch::Begin pixel test on SDL_Renderer -- direct port
    # of Task 168's EasyGL test.
    cna_sdl_test(cna_test_sdl_transform_matrix
                 sdlrenderer_transform_matrix_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_TransformMatrix COMMAND cna_test_sdl_transform_matrix
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 676: SpriteBatch::Begin(effect) custom-Effect behavior decision -- must throw
    # (no programmable shader stage exists on SDL_Renderer to apply a custom Effect with).
    cna_sdl_test(cna_test_sdl_custom_effect_throws
                 sdlrenderer_custom_effect_throws_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_CustomEffectThrows COMMAND cna_test_sdl_custom_effect_throws
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 677: SpriteBatch Begin/End sequencing guard end-to-end integration test on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_spritebatch_begin_end_guard
                 sdlrenderer_spritebatch_begin_end_guard_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_SpriteBatch_BeginEndGuard COMMAND cna_test_sdl_spritebatch_begin_end_guard
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 678: Texture2D::SetData/GetData full-array round-trip audit on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_texture2d_setdata_getdata
                 sdlrenderer_texture2d_setdata_getdata_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_Texture2D_SetDataGetData COMMAND cna_test_sdl_texture2d_setdata_getdata
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 679: Texture2D::SetData partial-rectangle region pixel test on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_texture2d_partial_rect
                 sdlrenderer_texture2d_partial_rect_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_Texture2D_PartialRect COMMAND cna_test_sdl_texture2d_partial_rect
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 680: Texture2D::SetData startIndex/elementCount slice pixel test on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_texture2d_startindex
                 sdlrenderer_texture2d_startindex_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_Texture2D_StartIndex COMMAND cna_test_sdl_texture2d_startindex
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 681: Texture2D mip-level SetData/GetData decision test on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_texture2d_miplevel_throws
                 sdlrenderer_texture2d_miplevel_throws_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_Texture2D_MipLevelThrows COMMAND cna_test_sdl_texture2d_miplevel_throws
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 682: Texture2D::FromStream round-trip real-render pixel test on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_texture2d_fromstream
                 sdlrenderer_texture2d_fromstream_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_Texture2D_FromStream COMMAND cna_test_sdl_texture2d_fromstream
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 683: SaveAsPng/SaveAsJpeg round-trip real-render pixel test on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_texture2d_saveas_roundtrip
                 sdlrenderer_texture2d_saveas_roundtrip_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_Texture2D_SaveAsRoundTrip COMMAND cna_test_sdl_texture2d_saveas_roundtrip
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 684: NPOT (3x5, 7x11) texture upload+sample pixel test on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_npot_texture
                 sdlrenderer_npot_texture_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_NpotTexture COMMAND cna_test_sdl_npot_texture
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 685: TextureAddressMode::Clamp via SpriteBatch pixel test on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_texture_address_mode_clamp
                 sdlrenderer_texture_address_mode_clamp_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_TextureAddressModeClamp COMMAND cna_test_sdl_texture_address_mode_clamp
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 688: TextureFilter::Point vs Linear via SDL_ScaleMode pixel test on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_texture_filter_point_vs_linear
                 sdlrenderer_texture_filter_point_vs_linear_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_TextureFilterPointVsLinear COMMAND cna_test_sdl_texture_filter_point_vs_linear
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 689: Texture2D::Dispose double-free/lifetime verification on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_texture2d_dispose
                 sdlrenderer_texture2d_dispose_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_Texture2D_Dispose COMMAND cna_test_sdl_texture2d_dispose
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 690: SpriteFont single-glyph-at-known-position pixel test on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_spritefont_single_glyph
                 sdlrenderer_spritefont_single_glyph_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_SpriteFont_SingleGlyph COMMAND cna_test_sdl_spritefont_single_glyph
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 691: SpriteFont multiple-glyphs-with-spacing pixel test on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_spritefont_multiglyph_spacing
                 sdlrenderer_spritefont_multiglyph_spacing_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_SpriteFont_MultiGlyphSpacing COMMAND cna_test_sdl_spritefont_multiglyph_spacing
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 692: SpriteFont newline-advances-by-lineSpacing pixel test on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_spritefont_newline
                 sdlrenderer_spritefont_newline_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_SpriteFont_Newline COMMAND cna_test_sdl_spritefont_newline
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 693: SpriteFont default-character-fallback pixel test on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_spritefont_default_char
                 sdlrenderer_spritefont_default_char_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_SpriteFont_DefaultChar COMMAND cna_test_sdl_spritefont_default_char
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 694: SpriteFont SpriteEffects flip + rotation/origin/scale pixel test on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_spritefont_effects
                 sdlrenderer_spritefont_effects_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_SpriteFont_Effects COMMAND cna_test_sdl_spritefont_effects
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 695: BlendState -> SDL_BlendMode mapping audit pixel test on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_blendstate_audit
                 sdlrenderer_blendstate_audit_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_BlendState_Audit COMMAND cna_test_sdl_blendstate_audit
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 696: BlendState::Opaque pixel test on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_blendstate_opaque
                 sdlrenderer_blendstate_opaque_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_BlendState_Opaque COMMAND cna_test_sdl_blendstate_opaque
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 697: BlendState::AlphaBlend premultiplied alpha pixel test on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_blendstate_alphablend
                 sdlrenderer_blendstate_alphablend_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_BlendState_AlphaBlend COMMAND cna_test_sdl_blendstate_alphablend
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 698: BlendState::NonPremultiplied pixel test on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_blendstate_nonpremultiplied
                 sdlrenderer_blendstate_nonpremultiplied_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_BlendState_NonPremultiplied COMMAND cna_test_sdl_blendstate_nonpremultiplied
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 699: BlendState::Additive saturation behavior pixel test on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_blendstate_additive
                 sdlrenderer_blendstate_additive_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_BlendState_Additive COMMAND cna_test_sdl_blendstate_additive
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 700: custom (non-preset) BlendState combination decision/verification on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_blendstate_custom
                 sdlrenderer_blendstate_custom_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_BlendState_Custom COMMAND cna_test_sdl_blendstate_custom
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 701: SamplerState -> SDL_ScaleMode + address-mode mapping audit pixel test on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_samplerstate_filter_audit
                 sdlrenderer_samplerstate_filter_audit_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_SamplerState_FilterAudit COMMAND cna_test_sdl_samplerstate_filter_audit
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 702: default SamplerState (LinearClamp) verification pixel test on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_samplerstate_default
                 sdlrenderer_samplerstate_default_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_SamplerState_Default COMMAND cna_test_sdl_samplerstate_default
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 703: per-draw SamplerState changes take effect on the next SpriteBatch::Begin pixel test on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_samplerstate_perdraw_switch
                 sdlrenderer_samplerstate_perdraw_switch_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_SamplerState_PerDrawSwitch COMMAND cna_test_sdl_samplerstate_perdraw_switch
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 704: RenderTarget2D construction audit (property wiring + bind/draw/unbind isolation) on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_rendertarget2d_construction
                 sdlrenderer_rendertarget2d_construction_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_RenderTarget2D_Construction COMMAND cna_test_sdl_rendertarget2d_construction
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 705: RenderTarget2D sampled as Texture2D via SpriteBatch after unbinding on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_rendertarget2d_sample
                 sdlrenderer_rendertarget2d_sample_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_RenderTarget2D_SampleAfterUnbind COMMAND cna_test_sdl_rendertarget2d_sample
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_sdl_test(cna_test_sdl_rendertarget2d_golden ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget2d_golden_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_RenderTarget2D_Golden COMMAND cna_test_sdl_rendertarget2d_golden
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 706: RenderTargetUsage::DiscardContents vs PreserveContents on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_rendertarget_usage
                 sdlrenderer_rendertarget_usage_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_RenderTargetUsage COMMAND cna_test_sdl_rendertarget_usage
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 707: GetBackBufferData reads correct pixels after SetRenderTarget(nullptr) restores the backbuffer on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_getbackbufferdata_after_rt_unbind
                 sdlrenderer_getbackbufferdata_after_rt_unbind_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_GetBackBufferData_AfterRtUnbind COMMAND cna_test_sdl_getbackbufferdata_after_rt_unbind
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 708: render-target depth-buffer behavior decision (silently ignore DepthFormat) on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_rendertarget_depth_decision
                 sdlrenderer_rendertarget_depth_decision_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_RenderTarget_DepthDecision COMMAND cna_test_sdl_rendertarget_depth_decision
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 709: MRT (SetRenderTargets with 2+ bindings) throws clearly on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_rendertargets_mrt_throws
                 sdlrenderer_rendertargets_mrt_throws_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_RenderTargets_MrtThrows COMMAND cna_test_sdl_rendertargets_mrt_throws
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 710: Viewport get/set round-trip and Project/Unproject math (2D orthographic case) on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_viewport_project_unproject
                 sdlrenderer_viewport_project_unproject_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_Viewport_ProjectUnproject COMMAND cna_test_sdl_viewport_project_unproject
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 711: backbuffer resize through PresentationParameters on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_presentationparameters_resize
                 sdlrenderer_presentationparameters_resize_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_PresentationParameters_Resize COMMAND cna_test_sdl_presentationparameters_resize
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 712: fullscreen toggle on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_fullscreen_toggle
                 sdlrenderer_fullscreen_toggle_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_FullscreenToggle COMMAND cna_test_sdl_fullscreen_toggle
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 713: PresentInterval (vsync) mapping to SDL_SetRenderVSync on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_presentinterval
                 sdlrenderer_presentinterval_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_PresentInterval COMMAND cna_test_sdl_presentinterval
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 714: MultiSampleCount decision (accept-and-ignore-with-log) on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_multisamplecount_decision
                 sdlrenderer_multisamplecount_decision_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_MultiSampleCount_Decision COMMAND cna_test_sdl_multisamplecount_decision
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 715: DeviceResetting/DeviceReset events fire correctly on SDL_Renderer backbuffer resize.
    cna_sdl_test(cna_test_sdl_devicereset_events
                 sdlrenderer_devicereset_events_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_DeviceReset_Events COMMAND cna_test_sdl_devicereset_events
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 716: GraphicsDevice::Clear (all ClearOptions combinations) on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_clearoptions_audit
                 sdlrenderer_clearoptions_audit_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_ClearOptions_Audit COMMAND cna_test_sdl_clearoptions_audit
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 717: disposed-resource guards throw ObjectDisposedException consistently on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_disposed_guards
                 sdlrenderer_disposed_guards_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_DisposedGuards COMMAND cna_test_sdl_disposed_guards
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 718: double-Dispose is safe for every SDL_Renderer-backed resource type.
    cna_sdl_test(cna_test_sdl_double_dispose
                 sdlrenderer_double_dispose_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_DoubleDispose COMMAND cna_test_sdl_double_dispose
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 719: leak-check -- create/dispose 80 SDL_Renderer textures/render-targets,
    # verify GetTrackedResourceCount returns to baseline. Mirrors Task 219.
    cna_sdl_test(cna_test_sdl_resource_leak
                 sdlrenderer_resource_leak_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_ResourceLeak COMMAND cna_test_sdl_resource_leak
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 720: DrawPrimitives/DrawIndexedPrimitives/DrawInstancedPrimitives throw the
    # correct exception type+message on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_drawprimitives_throws
                 sdlrenderer_drawprimitives_throws_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_DrawPrimitivesThrows COMMAND cna_test_sdl_drawprimitives_throws
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # CNA::GraphicsCapability: SDL_Renderer is 2D-only -- SupportsCapability() reports which
    # capabilities are genuinely absent (ThreeD and everything that depends on it).
    cna_sdl_test(cna_test_sdl_graphics_capability
                 sdlrenderer_graphics_capability_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_GraphicsCapability COMMAND cna_test_sdl_graphics_capability
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 721: all 5 DrawUserPrimitives typed + VertexDeclaration overloads throw the
    # correct exception type+message on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_drawuserprimitives_throws
                 sdlrenderer_drawuserprimitives_throws_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_DrawUserPrimitivesThrows COMMAND cna_test_sdl_drawuserprimitives_throws
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 722: all 10 DrawUserIndexedPrimitives typed + VertexDeclaration overloads throw the
    # correct exception type+message on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_drawuserindexedprimitives_throws
                 sdlrenderer_drawuserindexedprimitives_throws_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_DrawUserIndexedPrimitivesThrows COMMAND cna_test_sdl_drawuserindexedprimitives_throws
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 723: re-verify CreateVertexBuffer/CreateIndexBuffer/Dynamic variants throw correctly
    # on SDL_Renderer, covering every construction overload plus zero/negative-count edge cases.
    cna_sdl_test(cna_test_sdl_buffer_construction_throws
                 sdlrenderer_buffer_construction_throws_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_BufferConstructionThrows COMMAND cna_test_sdl_buffer_construction_throws
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 724: VertexDeclaration construction does NOT throw on SDL_Renderer -- pure data,
    # no renderer touch; only an actual draw call using it should throw.
    cna_sdl_test(cna_test_sdl_vertexdeclaration_construction
                 sdlrenderer_vertexdeclaration_construction_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_VertexDeclarationConstruction COMMAND cna_test_sdl_vertexdeclaration_construction
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 726: all 5 stock 3D effects' Apply()+property setters do NOT throw on SDL_Renderer,
    # but drawing with them does.
    cna_sdl_test(cna_test_sdl_stock3deffects_apply
                 sdlrenderer_stock3deffects_apply_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_Stock3DEffectsApply COMMAND cna_test_sdl_stock3deffects_apply
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 727: OcclusionQuery::Begin/End throw correctly on SDL_Renderer -- construction now
    # throws (fixed a real silent-no-op gap; the only inconsistent 3D-only entry point).
    cna_sdl_test(cna_test_sdl_occlusionquery_throws
                 sdlrenderer_occlusionquery_throws_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_OcclusionQueryThrows COMMAND cna_test_sdl_occlusionquery_throws
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 728: Model::Draw throws correctly on SDL_Renderer (requires 3D primitives).
    cna_sdl_test(cna_test_sdl_model_draw_throws
                 sdlrenderer_model_draw_throws_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_ModelDrawThrows COMMAND cna_test_sdl_model_draw_throws
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 729: RasterizerState/DepthStencilState construction and assignment do NOT throw on
    # SDL_Renderer -- pure data, mirrors Task 724's VertexDeclaration pattern.
    cna_sdl_test(cna_test_sdl_rasterizer_depthstencil_construction
                 sdlrenderer_rasterizer_depthstencil_construction_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_RasterizerDepthStencilConstruction COMMAND cna_test_sdl_rasterizer_depthstencil_construction
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 730: port and run 5 minimal 2D-only FNA/XNA samples specifically targeting
    # SDL_Renderer -- real compatibility proof for the 2D-only renderer, not EasyGL.
    # Sample 1/5: the existing cross-renderer cna_demo_2d, already smoke-tested on Vulkan/Bgfx
    # (Tasks 88/89) but never on SDL_Renderer -- a genuine gap, closed here.
    cna_register_renderer_test(NAME SDL_Renderer_Demo2D_SmokeTest COMMAND cna_demo_2d --smoke 3
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}" LABELS "GraphicsSmoke")

    # Sample 2/5: bouncing sprite -- real multi-frame Update()+Draw() physics/bounce loop.
    cna_sdl_test(cna_test_sdl_sample_bouncing_sprite
                 sdlrenderer_sample_bouncing_sprite_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_Sample_BouncingSprite COMMAND cna_test_sdl_sample_bouncing_sprite
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Sample 3/5: keyboard-driven sprite -- Keyboard::GetState()-driven Update() logic.
    cna_sdl_test(cna_test_sdl_sample_keyboard_sprite
                 sdlrenderer_sample_keyboard_sprite_test.cpp)
    target_include_directories(cna_test_sdl_sample_keyboard_sprite PRIVATE
                               ${CMAKE_SOURCE_DIR}/modules/platform/tests)
    cna_register_renderer_test(NAME SDL_Renderer_Sample_KeyboardSprite COMMAND cna_test_sdl_sample_keyboard_sprite
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Sample 4/5: SpriteFont text display -- genuine multi-glyph string layout ("HI").
    cna_sdl_test(cna_test_sdl_sample_spritefont_text
                 sdlrenderer_sample_spritefont_text_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_Sample_SpriteFontText COMMAND cna_test_sdl_sample_spritefont_text
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Sample 5/5: animated spritesheet -- Update()-driven sourceRectangle frame selection.
    cna_sdl_test(cna_test_sdl_sample_animated_spritesheet
                 sdlrenderer_sample_animated_spritesheet_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_Sample_AnimatedSpritesheet COMMAND cna_test_sdl_sample_animated_spritesheet
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-127: every public Texture2D::GetData call must return the resource's real content or
    # reject the request deterministically -- never fabricate one. Pre-fix the shared render-target
    # fallback zero-initialized its own scratch buffer, handed it to ITextureRenderer::GetData (whose
    # interface default did nothing) and converted it for the caller regardless, so a renderer with no
    # readback returned a complete, uniformly transparent-black frame that satisfied both "the
    # destination was overwritten" and any transparent-black content expectation.
    cna_sdl_test(cna_test_sdl_texture2d_getdata_contract
                 ${CNA_GRAPHICS_EXAMPLES_DIR}/texture2d_getdata_contract_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_Texture2D_GetDataContract COMMAND cna_test_sdl_texture2d_getdata_contract
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-149 (and REMED-GFX-128, the same expression): the exact XNA-compatible
    # startIndex/elementCount contract of every public Texture2D::GetData overload. startIndex is a
    # DESTINATION element offset and elementCount is the destination capacity available from it;
    # the whole-level overload applied startIndex to the SOURCE, rejected any non-zero offset on a
    # render target, rejected legal excess capacity, and silently returned a partial frame for an
    # undersized one.
    cna_sdl_test(cna_test_sdl_texture2d_getdata_transfer_range
                 ${CNA_GRAPHICS_EXAMPLES_DIR}/texture2d_getdata_transfer_range_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_Texture2D_GetDataTransferRange COMMAND cna_test_sdl_texture2d_getdata_transfer_range
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-130: the TextureCube/Texture3D half of REMED-GFX-127's finding. This renderer creates
    # no cube/volume resource at all, so pre-fix a TextureCube here had no storage AND no way to say
    # so -- GetData converted the shared layer's own zeroed scratch buffer into a complete face.
    cna_sdl_test(cna_test_sdl_cube_volume_getdata_contract
                 ${CNA_GRAPHICS_EXAMPLES_DIR}/texturecube_texture3d_getdata_contract_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_CubeVolume_GetDataContract COMMAND cna_test_sdl_cube_volume_getdata_contract
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-134: `RenderTargetCube::GetData` must return the face that was really rendered or
    # reject the request deterministically. REMED-GFX-130 made the reporting honest but left the
    # readback itself implemented on only two renderers, so a rendered cube face had no byte-exact
    # public oracle anywhere else. Drawn geometry (never Clear -- see REMED-GFX-129) paints an
    # asymmetric five-region pattern whose colours rotate per face, so a flip, a rotation, a wrong
    # array layer/subresource, a stale face or an unresolved multisample surface all fail.
    cna_sdl_test(cna_test_sdl_rendertargetcube_getdata_contract
                 ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertargetcube_getdata_contract_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_RenderTargetCube_GetDataContract COMMAND cna_test_sdl_rendertargetcube_getdata_contract
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-136: IGraphicsRenderer::CreateRenderTargetCube had no `preserveContents` parameter,
    # unlike CreateRenderTarget2D, so a RenderTargetCube's real RenderTargetUsage never reached the
    # renderer and Vulkan/WebGPU discarded a PreserveContents cube face on every bind cycle. Reuses
    # REMED-GFX-134's asymmetric face producer, then rebinds and draws only a small marker: "the
    # marker landed" is what a discarding renderer also achieves, so it can never pass for
    # preservation.
    cna_sdl_test(cna_test_sdl_rendertargetcube_usage
                 ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertargetcube_usage_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_RenderTargetCube_Usage COMMAND cna_test_sdl_rendertargetcube_usage
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-141: a MULTISAMPLED RenderTargetCube shared ONE multisample colour attachment
    # across all six faces on EasyGL, Vulkan and SdlGpu, so a PreserveContents face reloaded whichever
    # face was rendered last instead of its own samples. Renders face A, then face B, then rebinds A for
    # a small partial update -- with no readback, Present or flush in between -- and reads the whole of
    # A: the resolved single-sample layer still held the correct A, which is why a full redraw and an
    # immediate read both passed before this.
    cna_sdl_test(cna_test_sdl_rendertargetcube_msaa_face
                 ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertargetcube_msaa_face_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_RenderTargetCube_MsaaFace COMMAND cna_test_sdl_rendertargetcube_msaa_face
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-142: RenderTargetUsage's DEPTH and STENCIL half. FNA3D's own header documents
    # `preserveTargetContents` as storing the "color/depth/stencil" contents, and FNA's DiscardContents
    # bind clears all three (Target|DepthBuffer|Stencil, MaxDepth, 0) -- so the enum governs depth and
    # stencil, not colour alone. Renders an occluder, unbinds, rebinds WITHOUT clearing and draws behind
    # it: three bands separate preserved depth from cleared depth, lost colour and a second pass that
    # never drew. A parallel stencil stamp/gate sequence does the same for stencil.
    cna_sdl_test(cna_test_sdl_rendertarget_depthstencil_usage
                 ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_depthstencil_usage_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_RenderTarget_DepthStencilUsage COMMAND cna_test_sdl_rendertarget_depthstencil_usage
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-140: every public render-target bind/unbind cycle must be its own logical pass.
    # `VulkanRenderer::RecordCommandBuffer` collected ONE render pass per unique render-target
    # source per flush and replayed every queued batch for it inside that pass, so two bind cycles of
    # one target within a single flush window shared one load action. The decisive checks all use
    # DiscardContents -- collapsing is invisible on a preserving target, which is why REMED-GFX-136
    # needed an artificial readback barrier to see it -- and nothing between two cycles ever forces a
    # flush.
    cna_sdl_test(cna_test_sdl_rendertarget_pass_boundary
                 ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_pass_boundary_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_RenderTarget_PassBoundary COMMAND cna_test_sdl_rendertarget_pass_boundary
        TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-143: backbuffer work and render-target work must replay in ONE ordered stream.
    # REMED-GFX-140 (Vulkan) and REMED-GFX-145 (SdlGpu) gave every render-target bind cycle its own
    # native pass in public order, but both kept the BACKBUFFER as one trailing pass, so every
    # backbuffer draw of a frame was replayed after every target pass regardless of when it was
    # issued. A frame that samples a target onto the backbuffer and then renders into that target
    # again therefore saw the FINAL content. Every check queues its whole public sequence with no
    # Present, GetData, flush or extra frame between the cycles, and only then reads once.
    cna_sdl_test(cna_test_sdl_backbuffer_pass_order
                 ${CNA_GRAPHICS_EXAMPLES_DIR}/backbuffer_pass_order_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_Backbuffer_PassOrder COMMAND cna_test_sdl_backbuffer_pass_order
        TIMEOUT 90 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-135: the WRITE half of the same finding. `TextureCube::SetData`/`Texture3D::SetData`
    # kept the pre-REMED-GFX-127 shape -- a `void` renderer method behind `if (renderer_)` -- so an
    # upload that stored nothing, or only part of the requested region, still returned normally.
    cna_sdl_test(cna_test_sdl_cube_volume_setdata_contract
                 ${CNA_GRAPHICS_EXAMPLES_DIR}/texturecube_texture3d_setdata_contract_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_CubeVolume_SetDataContract COMMAND cna_test_sdl_cube_volume_setdata_contract
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Runtime policy for calls permanently unsupported by this 2D-only renderer.
    cna_sdl_test(cna_test_sdl_unsupported_3d_behavior
                 ${CNA_GRAPHICS_EXAMPLES_DIR}/unsupported_3d_call_behavior_test.cpp)
    cna_register_renderer_test(NAME SDL_Renderer_Unsupported3DBehavior
        COMMAND cna_test_sdl_unsupported_3d_behavior
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

endif()

# Module-local tests get the same headless-safe skip convention the root one-shot
# gives root-registered tests (see cmake/TestHelpers.cmake).
cna_apply_skip_convention()
