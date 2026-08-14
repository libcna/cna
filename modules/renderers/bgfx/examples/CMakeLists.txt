# bgfx renderer example/test registration (module-local; this file is the
# former cmake/Tests/BgfxTests.cmake, moved when examples moved into their owning
# physical modules).

# Example/test executables historically live at the build root (scripts, docs and
# the golden-image working-directory conventions rely on it); keep them there.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
# Shared example-support headers ("common/PixelTestGame.hpp", ...) are owned by
# the graphics module's examples directory.
include_directories(${CNA_GRAPHICS_EXAMPLES_DIR})

if(CNA_BUILD_TESTS AND NOT EMSCRIPTEN AND NOT WIN32
   AND CNA_GRAPHICS_RENDERER STREQUAL "BGFX")

    enable_testing()

    macro(cna_bgfx_test target src)
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

    # Task 13.6: investigated adding avatar-rendering smoke tests here too (mirroring the Vulkan
    # registration below). Confirmed a real, concrete blocker independent of readback reliability:
    # BgfxRenderer::SetDepthTestEnabled/SetBlendEnabled/SetDepthWriteEnabled are all
    # unconditionally unimplemented stubs (ThrowNo3DState()) - since the avatar test harnesses
    # call SetDepthTestEnabled/setBlendStateProperty to set up their scene before drawing (not
    # AvatarRenderer's own code, which never touches these), all 3 avatar tests throw immediately
    # on Bgfx today, before ever reaching DrawRealEXT. This is a pre-existing, deep gap in the
    # Bgfx renderer's 3D device-state wiring, not a tooling/build limitation - genuinely fixing it
    # (implementing depth-test/blend state via bgfx::setState flags) is a full renderer feature,
    # well beyond this test-coverage task's scope. Deferred; not attempted here.

    # Task 89: 3-frame smoke test for the Bgfx renderer
    cna_register_renderer_test(NAME Bgfx_Demo2D_SmokeTest COMMAND cna_demo_2d --smoke 3
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}" LABELS "GraphicsSmoke")

    # Task 817 (Phase 72 Bgfx gap closure): Texture2D SetData/GetData partial-rectangle +
    # startIndex/elementCount on Bgfx -- verbatim reuse of the shared EasyGL source (Tasks
    # 169-170), same as several Vulkan registrations above. Texture2D::SetData/GetData operate
    # entirely on a renderer-agnostic CPU-side cpuPixels_ shadow buffer (Texture2D.cpp) -- confirmed
    # via source that ITextureRenderer doesn't even declare a GetData() method (only
    # UpdatePixels/UpdatePixelsLevel, one-way CPU-to-GPU), so this test exercises zero
    # Bgfx-specific code and needs no restructuring, unlike the GetBackBufferData-based tests above.
    cna_bgfx_test(cna_test_bgfx_texture2d_partial_rect
                  ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_texture2d_partial_rect_test.cpp)
    cna_register_renderer_test(NAME Bgfx_Texture2D_PartialRect_RoundTrip COMMAND cna_test_bgfx_texture2d_partial_rect
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 819 (Phase 72 Bgfx gap closure): Texture3D partial-box GetData bounds/offset
    # verification on Bgfx -- verbatim reuse of the shared EasyGL source (Task 274). Unlike
    # Texture2D, Texture3D::GetData genuinely delegates to the per-renderer
    # ITexture3DRenderer::GetData -- BgfxTexture3DRenderer already has a real GPU readback
    # implementation (Task 914: blit into a temporary BGFX_TEXTURE_BLIT_DST|READ_BACK texture,
    # then bgfx::readTexture()), so this is a real per-renderer verification, not a no-op reuse of
    # shared CPU-only code like Task 817 above. No GetBackBufferData/rendered-frame involved at
    # all (direct texture-to-CPU readback), so no Bgfx-specific restructuring is needed either.
    cna_bgfx_test(cna_test_bgfx_texture3d_partial_box_readback
                  ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_texture3d_partial_box_readback_test.cpp)
    cna_register_renderer_test(NAME Bgfx_Texture3D_PartialBox_Readback COMMAND cna_test_bgfx_texture3d_partial_box_readback
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 821 (Phase 72 Bgfx gap closure): NPOT texture upload + GPU sampling on Bgfx -- a
    # Bgfx-specific adaptation of the EasyGL source (Task 268; that file uses
    # SetDepthTestEnabled(false), which throws on Bgfx, and reads back 5 rows from one rendered
    # frame, incompatible with Bgfx's GetBackBufferData quirk, Task 406 -- see the test's own
    # file-header comment). Closes the entire Phase 72 Texture2D/3D/Cube depth row group (817-821).
    cna_bgfx_test(cna_test_bgfx_npot_texture
                  bgfx_npot_texture_test.cpp)
    cna_register_renderer_test(NAME Bgfx_NpotTexture COMMAND cna_test_bgfx_npot_texture
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 744 (Phase 72 Bgfx gap closure): TextureAddressMode::Clamp -- already covered by
    # Task 750's own Bgfx_TextureAddressMode test (PointWrap-vs-PointClamp comparison, both
    # asserted); no new test needed, see plan_graphics.md Task 744 for the closure note.

    # Task 746 (Phase 72 Bgfx gap closure): TextureAddressMode::Mirror on Bgfx -- a Bgfx-specific
    # adaptation of the EasyGL source (Task 737; that file uses SetDepthTestEnabled(false), which
    # throws on Bgfx, and takes 3 samples within one Draw() tick, incompatible with Bgfx's
    # GetBackBufferData quirk, Task 406 -- see the test's own file-header comment).
    cna_bgfx_test(cna_test_bgfx_texture_address_mode_mirror
                  bgfx_texture_address_mode_mirror_test.cpp)
    cna_register_renderer_test(NAME Bgfx_TextureAddressMode_Mirror COMMAND cna_test_bgfx_texture_address_mode_mirror
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 747 (Phase 72 Bgfx gap closure): TextureFilter::Point vs Linear (magnification and
    # minification) on Bgfx -- a Bgfx-specific adaptation of the EasyGL source (Task 297; that
    # file uses SetDepthTestEnabled(false), which throws on Bgfx, and draws 4 columns in one
    # rendered frame before 4 separate reads, incompatible with Bgfx's GetBackBufferData quirk,
    # Task 406 -- see the test's own file-header comment).
    cna_bgfx_test(cna_test_bgfx_texture_filter_point_vs_linear
                  bgfx_texture_filter_point_vs_linear_test.cpp)
    cna_register_renderer_test(NAME Bgfx_TextureFilter_PointVsLinear COMMAND cna_test_bgfx_texture_filter_point_vs_linear
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 749 (Phase 72 Bgfx gap closure): anisotropic filtering cap query + fallback on Bgfx --
    # a Bgfx-specific adaptation of the EasyGL source (Task 299; that file uses
    # SetDepthTestEnabled(false), which throws on Bgfx -- see the test's own file-header comment).
    cna_bgfx_test(cna_test_bgfx_texture_anisotropic_effect
                  bgfx_texture_anisotropic_effect_test.cpp)
    cna_register_renderer_test(NAME Bgfx_TextureAnisotropicEffect COMMAND cna_test_bgfx_texture_anisotropic_effect
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 743 (Phase 72 Bgfx gap closure): audit SamplerState -> BGFX_SAMPLER_* flag mapping
    # completeness -- found and fixed a real, previously-undiscovered bug: BgfxRenderer::
    # ApplySamplerState's filter switch only handled Point/Anisotropic, silently collapsing all 6
    # split Min/Mag/Mip TextureFilter values (LinearMipPoint, PointMipLinear, etc.) to plain Linear.
    # Closes the entire Phase 72 SamplerState/TextureAddressMode row group (743-749).
    cna_bgfx_test(cna_test_bgfx_texturefilter_split_minmag
                  bgfx_texturefilter_split_minmag_test.cpp)
    cna_register_renderer_test(NAME Bgfx_TextureFilter_SplitMinMag COMMAND cna_test_bgfx_texturefilter_split_minmag
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 179: RenderTargetUsage DiscardContents/PreserveContents smoke test
    cna_bgfx_test(cna_test_bgfx_render_target_usage
                  bgfx_render_target_usage_test.cpp)
    cna_register_renderer_test(NAME Bgfx_RenderTargetUsage COMMAND cna_test_bgfx_render_target_usage
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 773 (Phase 72 Bgfx gap closure): verify SetRenderTarget(nullptr) genuinely restores the
    # real backbuffer with real pixel verification, superseding Task 179's own stale "pixel-level
    # verification is not available" comment (predates Task 406's readback convention and Task
    # 901's EnsureViewState() fix) -- see the test's own file-header comment.
    cna_bgfx_test(cna_test_bgfx_setrendertarget_null_restore
                  bgfx_setrendertarget_null_restore_test.cpp)
    cna_register_renderer_test(NAME Bgfx_SetRenderTarget_NullRestore COMMAND cna_test_bgfx_setrendertarget_null_restore
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 774 (Phase 72 Bgfx gap closure): verify MRT with mixed formats is rejected or handled
    # per XNA constraints on Bgfx -- Texture::ValidateFormat (shared code, all 4 renderers) already
    # throws for every SurfaceFormat except Color, so mixed-format MRT is unreachable, not merely
    # rejected at bind time -- see the test's own file-header comment for the full research finding.
    cna_bgfx_test(cna_test_bgfx_mrt_mixed_formats
                  bgfx_mrt_mixed_formats_test.cpp)
    cna_register_renderer_test(NAME Bgfx_MRT_MixedFormats COMMAND cna_test_bgfx_mrt_mixed_formats
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 774 (verbatim reuse): Task 176's own SurfaceFormat-validation contract, confirmed
    # renderer-agnostic (pure CPU-side construction, no rendering) -- reused unmodified from EasyGL
    # to directly confirm the same Color-only constraint holds on Bgfx too.
    cna_bgfx_test(cna_test_bgfx_surface_format_throws
                  ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_surface_format_throws_test.cpp)
    cna_register_renderer_test(NAME Bgfx_SurfaceFormat_Throws COMMAND cna_test_bgfx_surface_format_throws
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 757 (Phase 72 Bgfx gap closure): BlendFactor constant-color blending on Bgfx -- a
    # Bgfx-specific adaptation of the EasyGL source (Task 309; that file uses
    # SetDepthTestEnabled(false), which throws on Bgfx -- see the test's own file-header comment).
    cna_bgfx_test(cna_test_bgfx_blendstate_blendfactor
                  bgfx_blendstate_blendfactor_test.cpp)
    cna_register_renderer_test(NAME Bgfx_BlendState_BlendFactor COMMAND cna_test_bgfx_blendstate_blendfactor
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 777 (Phase 72 Bgfx gap closure): verify Viewport reset after backbuffer resize on Bgfx
    # -- verbatim reuse of the shared, 100% renderer-agnostic source (Task 349, already reused
    # verbatim on Vulkan). Exercises only GraphicsDevice/GraphicsDeviceManager C++ state
    # (UpdateViewportFromWindow()/Present()), no pixel readback, no renderer-specific code at all.
    cna_bgfx_test(cna_test_bgfx_viewport_reset_after_resize
                  ${CNA_GRAPHICS_EXAMPLES_DIR}/viewport_reset_after_resize_test.cpp)
    cna_register_renderer_test(NAME Bgfx_ViewportResetAfterResize COMMAND cna_test_bgfx_viewport_reset_after_resize
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 249: Bgfx vertex layout mapping tests — all 12 VertexElementFormat values
    cna_bgfx_test(cna_test_bgfx_vertex_format
                  bgfx_vertex_format_test.cpp)
    cna_register_renderer_test(NAME Bgfx_VertexFormatMapping COMMAND cna_test_bgfx_vertex_format
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 448: real Bgfx occlusion-query pixel/query test — visible quad (PixelCount > 0) and
    # occluded quad (PixelCount <= 0), proving BgfxOcclusionQueryRenderer's Begin()/End() are now
    # actually wired to bgfx's own submit(id, program, occlusionQuery, ...) overload.
    cna_bgfx_test(cna_test_bgfx_occlusion_query
                  bgfx_occlusion_query_test.cpp)
    cna_register_renderer_test(NAME Bgfx_OcclusionQuery COMMAND cna_test_bgfx_occlusion_query
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 278: EnvironmentMapEffect smoke test (no GPU readback available on Bgfx)
    cna_bgfx_test(cna_test_bgfx_env_map
                  bgfx_env_map_test.cpp)
    cna_register_renderer_test(NAME Bgfx_EnvironmentMapEffect_Smoke COMMAND cna_test_bgfx_env_map
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 393: EnvironmentMapEffect EnvironmentMapAmount=0 should ignore the cube map
    cna_bgfx_test(cna_test_bgfx_environmentmapeffect_amount_zero
                  bgfx_environmentmapeffect_amount_zero_test.cpp)
    cna_register_renderer_test(NAME Bgfx_EnvironmentMapEffect_AmountZero COMMAND cna_test_bgfx_environmentmapeffect_amount_zero
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 890: EnvironmentMapEffect DirectionalLight1/DirectionalLight2 forwarding
    cna_bgfx_test(cna_test_bgfx_environmentmapeffect_multilight
                  bgfx_environmentmapeffect_multilight_test.cpp)
    cna_register_renderer_test(NAME Bgfx_EnvironmentMapEffect_MultiLight COMMAND cna_test_bgfx_environmentmapeffect_multilight
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 394: EnvironmentMapEffect EnvironmentMapAmount=1 should fully replace lit color
    cna_bgfx_test(cna_test_bgfx_environmentmapeffect_amount_one
                  bgfx_environmentmapeffect_amount_one_test.cpp)
    cna_register_renderer_test(NAME Bgfx_EnvironmentMapEffect_AmountOne COMMAND cna_test_bgfx_environmentmapeffect_amount_one
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 395: EnvironmentMapEffect EnvironmentMapSpecular should scale by cubemap alpha
    cna_bgfx_test(cna_test_bgfx_environmentmapeffect_specular
                  bgfx_environmentmapeffect_specular_test.cpp)
    cna_register_renderer_test(NAME Bgfx_EnvironmentMapEffect_Specular COMMAND cna_test_bgfx_environmentmapeffect_specular
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 891: EnvironmentMapEffect's base cube-map lerp target must be scaled by the combined
    # texture x diffuse alpha -- verbatim reuse of the shared renderer-agnostic source, registered
    # on all 3 renderers.
    cna_bgfx_test(cna_test_bgfx_environmentmapeffect_alphascaledlerp
                  ${CNA_GRAPHICS_EXAMPLES_DIR}/environmentmapeffect_alphascaledlerp_test.cpp)
    cna_register_renderer_test(NAME Bgfx_EnvironmentMapEffect_AlphaScaledLerp COMMAND cna_test_bgfx_environmentmapeffect_alphascaledlerp
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 396: EnvironmentMapEffect Fresnel edge-weighting should suppress head-on reflections
    cna_bgfx_test(cna_test_bgfx_environmentmapeffect_fresnel
                  bgfx_environmentmapeffect_fresnel_test.cpp)
    cna_register_renderer_test(NAME Bgfx_EnvironmentMapEffect_Fresnel COMMAND cna_test_bgfx_environmentmapeffect_fresnel
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 397: EnvironmentMapEffect reflection vector should respond to EyePosition
    cna_bgfx_test(cna_test_bgfx_environmentmapeffect_eyeposition
                  bgfx_environmentmapeffect_eyeposition_test.cpp)
    cna_register_renderer_test(NAME Bgfx_EnvironmentMapEffect_EyePosition COMMAND cna_test_bgfx_environmentmapeffect_eyeposition
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 398: EnvironmentMapEffect normal transform should use inverse-transpose of World
    cna_bgfx_test(cna_test_bgfx_environmentmapeffect_worldtransform
                  bgfx_environmentmapeffect_worldtransform_test.cpp)
    cna_register_renderer_test(NAME Bgfx_EnvironmentMapEffect_WorldTransform COMMAND cna_test_bgfx_environmentmapeffect_worldtransform
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 399: cross-renderer EnvironmentMapEffect capstone (Tasks 393-398 combined)
    cna_bgfx_test(cna_test_bgfx_environmentmapeffect_combined
                  bgfx_environmentmapeffect_combined_test.cpp)
    cna_register_renderer_test(NAME Bgfx_EnvironmentMapEffect_Combined COMMAND cna_test_bgfx_environmentmapeffect_combined
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 406: SkinnedEffect identity bone palette should produce zero deformation
    cna_bgfx_test(cna_test_bgfx_skinnedeffect_identity_bones
                  bgfx_skinnedeffect_identity_bones_test.cpp)
    cna_register_renderer_test(NAME Bgfx_SkinnedEffect_IdentityBones COMMAND cna_test_bgfx_skinnedeffect_identity_bones
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-006: SkinnedEffect world-space normal transform (inverse-transpose after the bone
    # skin) on Bgfx -- the non-identity-World harness the existing identity-World skinned tests
    # structurally cannot cover. Fails pre-fix (Variant A/B), passes post-fix. Covers both the
    # per-pixel (vs_skinned3d) and per-vertex (vs_skinned3d_vertexlit) skinned programs.
    cna_bgfx_test(cna_test_bgfx_skinnedeffect_world_normal
                  bgfx_skinnedeffect_world_normal_test.cpp)
    cna_register_renderer_test(NAME Bgfx_SkinnedEffect_WorldNormal COMMAND cna_test_bgfx_skinnedeffect_world_normal
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 407: SkinnedEffect single translation bone should shift the mesh
    cna_bgfx_test(cna_test_bgfx_skinnedeffect_translation_bone
                  bgfx_skinnedeffect_translation_bone_test.cpp)
    cna_register_renderer_test(NAME Bgfx_SkinnedEffect_TranslationBone COMMAND cna_test_bgfx_skinnedeffect_translation_bone
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 408: SkinnedEffect two-bone weighted blend should produce midpoint deformation
    cna_bgfx_test(cna_test_bgfx_skinnedeffect_twobone_blend
                  bgfx_skinnedeffect_twobone_blend_test.cpp)
    cna_register_renderer_test(NAME Bgfx_SkinnedEffect_TwoBoneBlend COMMAND cna_test_bgfx_skinnedeffect_twobone_blend
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 409: cross-renderer SkinnedEffect capstone (identity + single-bone + two-bone blend)
    cna_bgfx_test(cna_test_bgfx_skinnedeffect_combined
                  bgfx_skinnedeffect_combined_test.cpp)
    cna_register_renderer_test(NAME Bgfx_SkinnedEffect_Combined COMMAND cna_test_bgfx_skinnedeffect_combined
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 333: RenderTarget2D sampled as Texture2D via SpriteBatch after unbinding (Bgfx)
    cna_bgfx_test(cna_test_bgfx_render_target_sample
                  bgfx_render_target_sample_test.cpp)
    cna_register_renderer_test(NAME Bgfx_RenderTarget2D_SampleAfterUnbind COMMAND cna_test_bgfx_render_target_sample
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 335/912: RenderTarget2D depth buffer is real and functional (Bgfx) -- shared with
    # EasyGL/Vulkan's registrations of the same file; needed the retry-until-non-black convention
    # added to the shared test first (see that file's header comment).
    cna_bgfx_test(cna_test_bgfx_rendertarget2d_depth
                  ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget2d_depth_test.cpp)
    cna_register_renderer_test(NAME Bgfx_RenderTarget2D_DepthBuffer COMMAND cna_test_bgfx_rendertarget2d_depth
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 334: RenderTargetCube sampled as TextureCube via EnvironmentMapEffect after unbinding (Bgfx)
    cna_bgfx_test(cna_test_bgfx_render_target_cube_sample
                  bgfx_render_target_cube_sample_test.cpp)
    cna_register_renderer_test(NAME Bgfx_RenderTargetCube_SampleAfterUnbind COMMAND cna_test_bgfx_render_target_cube_sample
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-096: renderer-neutral singular/plural cube-face binding parity.
    cna_bgfx_test(cna_test_bgfx_rendertargetcube_plural_binding
                  ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertargetcube_plural_binding_test.cpp)
    # REMED-GFX-134: the CNA_GFX096_STAGED_SINGLE_FACE reduction this used to carry is gone. It
    # existed because binding several cube faces within one un-advanced bgfx frame silently dropped
    # the first face's draws -- BgfxRenderTargetCubeRenderer destroyed and rebuilt one shared
    # framebuffer handle per bind and re-pointed the base view at it, so the earlier face's already
    # recorded draws ended up aimed at a destroyed framebuffer. With per-face framebuffers and the
    # base view left alone once it has recorded work, this renderer runs the full battery every
    # other renderer runs: all six faces, A->B->A, and two independent cubes.
    cna_register_renderer_test(NAME Bgfx_RenderTargetCube_PluralBinding
        COMMAND cna_test_bgfx_rendertargetcube_plural_binding
        TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 364: BasicEffect pixel test — VertexColorEnabled=false, no texture, diffuse color only
    cna_bgfx_test(cna_test_bgfx_basiceffect_vertexcolor_disabled
                  bgfx_basiceffect_vertexcolor_disabled_test.cpp)
    cna_register_renderer_test(NAME Bgfx_BasicEffect_VertexColorDisabled COMMAND cna_test_bgfx_basiceffect_vertexcolor_disabled
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 365: BasicEffect pixel test — VertexColorEnabled=true, no texture, vertex color multiplication
    cna_bgfx_test(cna_test_bgfx_basiceffect_vertexcolor_enabled
                  bgfx_basiceffect_vertexcolor_enabled_test.cpp)
    cna_register_renderer_test(NAME Bgfx_BasicEffect_VertexColorEnabled COMMAND cna_test_bgfx_basiceffect_vertexcolor_enabled
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 366: BasicEffect pixel test — TextureEnabled=true, no vertex color
    cna_bgfx_test(cna_test_bgfx_basiceffect_texture_enabled
                  bgfx_basiceffect_texture_enabled_test.cpp)
    cna_register_renderer_test(NAME Bgfx_BasicEffect_TextureEnabled COMMAND cna_test_bgfx_basiceffect_texture_enabled
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 367: BasicEffect pixel test — TextureEnabled=true AND VertexColorEnabled=true
    cna_bgfx_test(cna_test_bgfx_basiceffect_texture_vertexcolor_enabled
                  bgfx_basiceffect_texture_vertexcolor_enabled_test.cpp)
    cna_register_renderer_test(NAME Bgfx_BasicEffect_TextureVertexColorEnabled COMMAND cna_test_bgfx_basiceffect_texture_vertexcolor_enabled
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 368: BasicEffect pixel test — LightingEnabled=true, one DirectionalLight
    cna_bgfx_test(cna_test_bgfx_basiceffect_one_light
                  bgfx_basiceffect_one_light_test.cpp)
    cna_register_renderer_test(NAME Bgfx_BasicEffect_OneLight COMMAND cna_test_bgfx_basiceffect_one_light
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 885: BasicEffect pixel test — DirectionalLight1/2 + EmissiveColor on the lit path
    cna_bgfx_test(cna_test_bgfx_basiceffect_multilight_emissive
                  bgfx_basiceffect_multilight_emissive_test.cpp)
    cna_register_renderer_test(NAME Bgfx_BasicEffect_MultiLightEmissive COMMAND cna_test_bgfx_basiceffect_multilight_emissive
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 886: BasicEffect pixel test — real specular highlights
    cna_bgfx_test(cna_test_bgfx_basiceffect_specular
                  bgfx_basiceffect_specular_test.cpp)
    cna_register_renderer_test(NAME Bgfx_BasicEffect_Specular COMMAND cna_test_bgfx_basiceffect_specular
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 1104: BasicEffect PreferPerPixelLighting real dispatch selector
    cna_bgfx_test(cna_test_bgfx_basiceffect_preferperpixellighting
                  bgfx_basiceffect_preferperpixellighting_test.cpp)
    cna_register_renderer_test(NAME Bgfx_BasicEffect_PreferPerPixelLighting COMMAND cna_test_bgfx_basiceffect_preferperpixellighting
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 887: AlphaTestEffect.VertexColorEnabled fix verification
    cna_bgfx_test(cna_test_bgfx_alphatest_vertexcolor
                  bgfx_alphatest_vertexcolor_test.cpp)
    cna_register_renderer_test(NAME Bgfx_AlphaTest_VertexColor COMMAND cna_test_bgfx_alphatest_vertexcolor
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 888: BasicEffect fog on the colored3d (stride-16) pipeline
    cna_bgfx_test(cna_test_bgfx_basiceffect_fog
                  bgfx_basiceffect_fog_test.cpp)
    cna_register_renderer_test(NAME Bgfx_BasicEffect_Fog COMMAND cna_test_bgfx_basiceffect_fog
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 888: BasicEffect fog on the lit_textured3d (stride-32) pipeline
    cna_bgfx_test(cna_test_bgfx_basiceffect_lit_fog
                  bgfx_basiceffect_lit_fog_test.cpp)
    cna_register_renderer_test(NAME Bgfx_BasicEffect_LitFog COMMAND cna_test_bgfx_basiceffect_lit_fog
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 888: AlphaTestEffect fog on the alpha_test3d pipeline
    cna_bgfx_test(cna_test_bgfx_alphatest_fog
                  bgfx_alphatest_fog_test.cpp)
    cna_register_renderer_test(NAME Bgfx_AlphaTest_Fog COMMAND cna_test_bgfx_alphatest_fog
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 888: DualTextureEffect fog on the dual_texture3d pipeline
    cna_bgfx_test(cna_test_bgfx_dualtextureeffect_fog
                  bgfx_dualtextureeffect_fog_test.cpp)
    cna_register_renderer_test(NAME Bgfx_DualTextureEffect_Fog COMMAND cna_test_bgfx_dualtextureeffect_fog
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 892: BasicEffect lit-textured normal transform under a real camera (Bgfx)
    cna_bgfx_test(cna_test_bgfx_basiceffect_normaltransform
                  bgfx_basiceffect_normaltransform_test.cpp)
    cna_register_renderer_test(NAME Bgfx_BasicEffect_NormalTransform COMMAND cna_test_bgfx_basiceffect_normaltransform
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 369: BasicEffect pixel test — DiffuseColor+EmissiveColor, LightingEnabled=false
    cna_bgfx_test(cna_test_bgfx_basiceffect_emissive
                  bgfx_basiceffect_emissive_test.cpp)
    cna_register_renderer_test(NAME Bgfx_BasicEffect_Emissive COMMAND cna_test_bgfx_basiceffect_emissive
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 370: BasicEffect cross-renderer image comparison suite — closes Phase 42
    cna_bgfx_test(cna_test_bgfx_basiceffect_combined
                  bgfx_basiceffect_combined_test.cpp)
    cna_register_renderer_test(NAME Bgfx_BasicEffect_Combined COMMAND cna_test_bgfx_basiceffect_combined
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 375: AlphaTestEffect all CompareFunction modes — threshold sweep
    cna_bgfx_test(cna_test_bgfx_alphatest_comparefunction_sweep
                  bgfx_alphatest_comparefunction_sweep_test.cpp)
    cna_register_renderer_test(NAME Bgfx_AlphaTest_CompareFunctionSweep COMMAND cna_test_bgfx_alphatest_comparefunction_sweep
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 379: AlphaTestEffect null/no-texture behavior
    cna_bgfx_test(cna_test_bgfx_alphatest_null_texture
                  bgfx_alphatest_null_texture_test.cpp)
    cna_register_renderer_test(NAME Bgfx_AlphaTest_NullTexture COMMAND cna_test_bgfx_alphatest_null_texture
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 383: DualTextureEffect two-texture blend formula, including FNA's color.rgb*=2.
    # Also the first-ever Bgfx pixel-integration test for DualTextureEffect.
    cna_bgfx_test(cna_test_bgfx_dualtextureeffect_doubling
                  bgfx_dualtextureeffect_doubling_test.cpp)
    cna_register_renderer_test(NAME Bgfx_DualTextureEffect_Doubling COMMAND cna_test_bgfx_dualtextureeffect_doubling
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 384: DualTextureEffect magenta × yellow = red (mirrors Task 133/135 EasyGL/Vulkan)
    cna_bgfx_test(cna_test_bgfx_dual_texture
                  bgfx_dual_texture_test.cpp)
    cna_register_renderer_test(NAME Bgfx_DualTextureEffect_Blend COMMAND cna_test_bgfx_dual_texture
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 385: DualTextureEffect Alpha premultiplication, verified via real AlphaBlend
    cna_bgfx_test(cna_test_bgfx_dualtextureeffect_alpha
                  bgfx_dualtextureeffect_alpha_test.cpp)
    cna_register_renderer_test(NAME Bgfx_DualTextureEffect_Alpha COMMAND cna_test_bgfx_dualtextureeffect_alpha
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 386: DualTextureEffect first texture (Texture, slot 0) null behavior
    cna_bgfx_test(cna_test_bgfx_dualtextureeffect_null_texture0
                  bgfx_dualtextureeffect_null_texture0_test.cpp)
    cna_register_renderer_test(NAME Bgfx_DualTextureEffect_NullTexture0 COMMAND cna_test_bgfx_dualtextureeffect_null_texture0
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 387: DualTextureEffect second texture (Texture2, slot 1) null behavior
    cna_bgfx_test(cna_test_bgfx_dualtextureeffect_null_texture2
                  bgfx_dualtextureeffect_null_texture2_test.cpp)
    cna_register_renderer_test(NAME Bgfx_DualTextureEffect_NullTexture2 COMMAND cna_test_bgfx_dualtextureeffect_null_texture2
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 389: DualTextureEffect cross-renderer image comparison suite (closes Phase 44 pixel work)
    cna_bgfx_test(cna_test_bgfx_dualtextureeffect_combined
                  bgfx_dualtextureeffect_combined_test.cpp)
    cna_register_renderer_test(NAME Bgfx_DualTextureEffect_Combined COMMAND cna_test_bgfx_dualtextureeffect_combined
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 880: GraphicsDevice.Viewport GPU wiring — sub-region viewport pixel-readback test
    cna_bgfx_test(cna_test_bgfx_viewport_subregion
                  bgfx_viewport_subregion_test.cpp)
    cna_register_renderer_test(NAME Bgfx_Viewport_Subregion COMMAND cna_test_bgfx_viewport_subregion
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-063: custom Viewport must be honored for draws issued while a RenderTarget2D is bound
    cna_bgfx_test(cna_test_bgfx_rendertarget_viewport
                  bgfx_rendertarget_viewport_test.cpp)
    cna_register_renderer_test(NAME Bgfx_RenderTarget_Viewport COMMAND cna_test_bgfx_rendertarget_viewport
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-065: two DIFFERENT viewports on one target in one frame must each be honored (bgfx
    # per-view setViewRect/setViewTransform is last-wins on a shared view id -> ordered view
    # segmentation). Covers 3D + SpriteBatch + A->B->A ordering + RenderTarget2D + same-viewport reuse.
    cna_bgfx_test(cna_test_bgfx_multi_viewport
                  bgfx_multi_viewport_test.cpp)
    cna_register_renderer_test(NAME Bgfx_MultiViewport COMMAND cna_test_bgfx_multi_viewport
        TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-084: two SpriteBatch batches sharing the SAME GraphicsDevice.Viewport but a DIFFERENT
    # Begin(transformMatrix) in one frame must each render under their own transform. bgfx
    # setViewTransform is per-view/last-wins and GFX-065's segmentation keys only the viewport, so a
    # transform-only change collapsed onto one view id -> last transform won for both. Covers translation
    # + scale + A->B->A ordering + same-transform reuse + RenderTarget2D + 32-transform stress +
    # viewport/transform composition + default/identity/custom.
    cna_bgfx_test(cna_test_bgfx_spritebatch_transform
                  bgfx_spritebatch_transform_test.cpp)
    cna_register_renderer_test(NAME Bgfx_SpriteBatch_Transform COMMAND cna_test_bgfx_spritebatch_transform
        TIMEOUT 90 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-066: per-draw ScissorRectangle must clip on FBO-bound (render-target) views too
    cna_bgfx_test(cna_test_bgfx_rendertarget_scissor
                  bgfx_rendertarget_scissor_test.cpp)
    cna_register_renderer_test(NAME Bgfx_RenderTarget_Scissor COMMAND cna_test_bgfx_rendertarget_scissor
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-081: SpriteBatch.Begin must apply its RasterizerState argument (shared fix in
    # SpriteBatch.cpp). Bgfx control: GFX-066 proved bgfx::setScissor works when ScissorTestEnable is
    # truly active, so this is a clean discriminator that the state reaches Bgfx through Begin. Uses
    # Bgfx's own backbuffer + 20-frame settle loop (its single-frame RT GetData returns black, so the
    # shared RT-readback test is Vulkan/SdlGpu only).
    cna_bgfx_test(cna_test_bgfx_spritebatch_begin_rasterizerstate
                  bgfx_spritebatch_begin_rasterizerstate_test.cpp)
    cna_register_renderer_test(NAME Bgfx_SpriteBatch_BeginRasterizerState COMMAND cna_test_bgfx_spritebatch_begin_rasterizerstate
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-067: a RenderTarget2D sampled back through SpriteBatch must read UPRIGHT (not
    # vertically mirrored) at EVERY size — FBO color memory is bottom-up on originBottomLeft renderers
    cna_bgfx_test(cna_test_bgfx_rendertarget_orientation
                  bgfx_rendertarget_orientation_test.cpp)
    cna_register_renderer_test(NAME Bgfx_RenderTarget_Orientation COMMAND cna_test_bgfx_rendertarget_orientation
        TIMEOUT 40 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-078: a RenderTarget2D used as a generic 3D-effect texture (effect.Texture = rt) must
    # bind safely (no UB static_cast to BgfxTextureRenderer) and sample UPRIGHT on both bgfx renderers,
    # per sampler slot (BasicEffect single texture + DualTextureEffect two-slot discriminator).
    cna_bgfx_test(cna_test_bgfx_rendertarget_effect_texture
                  bgfx_rendertarget_effect_texture_test.cpp)
    cna_register_renderer_test(NAME Bgfx_RenderTarget_EffectTexture COMMAND cna_test_bgfx_rendertarget_effect_texture
        TIMEOUT 40 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 750: SpriteBatch Begin()'s SamplerState (TextureAddressMode) must take effect
    cna_bgfx_test(cna_test_bgfx_texture_address_mode
                  bgfx_texture_address_mode_test.cpp)
    cna_register_renderer_test(NAME Bgfx_TextureAddressMode COMMAND cna_test_bgfx_texture_address_mode
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 899: EnvironmentMapEffect/SkinnedEffect fog on Bgfx (opportunistic bonus scope)
    cna_bgfx_test(cna_test_bgfx_environmentmapeffect_fog
                  bgfx_environmentmapeffect_fog_test.cpp)
    cna_register_renderer_test(NAME Bgfx_EnvironmentMapEffect_Fog COMMAND cna_test_bgfx_environmentmapeffect_fog
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_bgfx_test(cna_test_bgfx_skinnedeffect_fog
                  bgfx_skinnedeffect_fog_test.cpp)
    cna_register_renderer_test(NAME Bgfx_SkinnedEffect_Fog COMMAND cna_test_bgfx_skinnedeffect_fog
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-010: transformed-camera view-space fog conformance (fails pre-fix).
    cna_bgfx_test(cna_test_bgfx_viewspace_fog
                  bgfx_viewspace_fog_test.cpp)
    cna_register_renderer_test(NAME Bgfx_ViewSpace_Fog COMMAND cna_test_bgfx_viewspace_fog
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 893: SkinnedEffect DirectionalLight1/DirectionalLight2 forwarding
    cna_bgfx_test(cna_test_bgfx_skinnedeffect_multilight
                  bgfx_skinnedeffect_multilight_test.cpp)
    cna_register_renderer_test(NAME Bgfx_SkinnedEffect_MultiLight COMMAND cna_test_bgfx_skinnedeffect_multilight
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-008: analytic SkinnedEffect ambient/emissive lighting conformance
    cna_bgfx_test(cna_test_bgfx_skinnedeffect_lighting_conformance
                  ${CNA_GRAPHICS_EXAMPLES_DIR}/skinnedeffect_lighting_conformance_test.cpp)
    cna_register_renderer_test(NAME Bgfx_SkinnedEffect_LightingConformance COMMAND cna_test_bgfx_skinnedeffect_lighting_conformance
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 894: SkinnedEffect real specular highlights
    cna_bgfx_test(cna_test_bgfx_skinnedeffect_specular
                  bgfx_skinnedeffect_specular_test.cpp)
    cna_register_renderer_test(NAME Bgfx_SkinnedEffect_Specular COMMAND cna_test_bgfx_skinnedeffect_specular
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 1104: SkinnedEffect PreferPerPixelLighting real dispatch selector
    cna_bgfx_test(cna_test_bgfx_skinnedeffect_preferperpixellighting
                  bgfx_skinnedeffect_preferperpixellighting_test.cpp)
    cna_register_renderer_test(NAME Bgfx_SkinnedEffect_PreferPerPixelLighting COMMAND cna_test_bgfx_skinnedeffect_preferperpixellighting
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 895: SkinnedEffect WeightsPerVertex real GPU enforcement
    cna_bgfx_test(cna_test_bgfx_skinnedeffect_weightspervertex
                  bgfx_skinnedeffect_weightspervertex_test.cpp)
    cna_register_renderer_test(NAME Bgfx_SkinnedEffect_WeightsPerVertex COMMAND cna_test_bgfx_skinnedeffect_weightspervertex
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 878/879: RenderTarget2D MSAA resolve genuinely anti-aliases on Bgfx
    # (BGFX_TEXTURE_RT_MSAA_Xn -- also closes Task 873's SpriteBatch RT-sampling cast bug and a
    # second, separate EnsureViewState() RT-viewport-clobbering bug, both hard prerequisites for
    # this test's RT-then-backbuffer-sample methodology to be meaningful at all -- see the test
    # file's own header comment for full detail). CNA_BGFX_RENDERER=VULKAN routes bgfx through its
    # Vulkan renderer instead of its default OpenGL one: this dev environment's bgfx OpenGL path
    # negotiates only a legacy GL 2.1 context (confirmed via glxinfo that the underlying driver
    # actually supports OpenGL 4.6) under which MSAA-flagged framebuffer textures do not really
    # resolve with sub-pixel blending, despite bgfx::getCaps() reporting format support -- a
    # bgfx/legacy-GL-context environment limitation, not a defect in this task's (100% renderer-
    # agnostic) C++ wiring, confirmed by this exact test passing cleanly under bgfx's Vulkan
    # renderer with zero code changes.
    cna_bgfx_test(cna_test_bgfx_rendertarget2d_msaa
                  bgfx_rendertarget2d_msaa_test.cpp)
    cna_register_renderer_test(NAME Bgfx_RenderTarget2D_MsaaResolve COMMAND cna_test_bgfx_rendertarget2d_msaa
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY};CNA_BGFX_RENDERER=VULKAN")

    # REMED-GFX-185: exact active-renderer MSAA capability clamp and public applied-count report.
    # OpenGL exercises a real limited renderer; Vulkan exercises renderer selection/fallback with
    # the active caps remaining authoritative. The fixture also drives the selection helper with
    # synthetic 0/2/4/8/16 ceilings so device differences remain deterministic on one machine.
    cna_bgfx_test(cna_test_bgfx_gfx185_msaa_capability
                  bgfx_gfx185_msaa_capability_test.cpp)
    cna_register_renderer_test(NAME Bgfx_GFX185_MsaaCapability
        COMMAND cna_test_bgfx_gfx185_msaa_capability
        TIMEOUT 120 ENVIRONMENT
            "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY};CNA_BGFX_RENDERER=OPENGL")
    cna_register_renderer_test(NAME Bgfx_GFX185_MsaaCapability_Vulkan
        COMMAND cna_test_bgfx_gfx185_msaa_capability
        TIMEOUT 120 ENVIRONMENT
            "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY};CNA_BGFX_RENDERER=VULKAN")

    # REMED-GFX-196: Noop is an explicitly selectable, active zero-capability renderer. It has no
    # native backbuffer and advertises no BACKBUFFER-capable texture format, so initialization,
    # public reset and resize must not route through bgfx::reset's fatal format assertion. The
    # process-isolated fixture classifies a catchable rejection, native fatal and renderer fallback
    # separately, covers default/explicit Color negotiation and repeated 0/minimal/ordinary reset
    # cycles, verifies teardown, and runs an ordinary active-OpenGL control.
    cna_bgfx_test(cna_test_bgfx_gfx196_noop_reset
                  bgfx_gfx196_noop_reset_test.cpp)
    cna_register_renderer_test(NAME Bgfx_GFX196_NoopReset
        COMMAND cna_test_bgfx_gfx196_noop_reset
        TIMEOUT 180 ENVIRONMENT
            "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
    # Task 906: RenderTarget2D mip chains are genuinely generated (bgfx's own built-in
    # hasMips+BGFX_RESOLVE_AUTO_GEN_MIPS mechanism), not just present-but-undefined storage.
    cna_bgfx_test(cna_test_bgfx_rendertarget2d_mip
                  bgfx_rendertarget2d_mip_test.cpp)
    cna_register_renderer_test(NAME Bgfx_RenderTarget2D_MipChain COMMAND cna_test_bgfx_rendertarget2d_mip
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 907: RenderTargetCube mip chains (closes Task 874 as a hard prerequisite --
    # this is Bgfx's first-ever RenderTargetCube-via-EnvironmentMapEffect test).
    cna_bgfx_test(cna_test_bgfx_rendertargetcube_mip
                  bgfx_rendertargetcube_mip_test.cpp)
    cna_register_renderer_test(NAME Bgfx_RenderTargetCube_MipChain COMMAND cna_test_bgfx_rendertargetcube_mip
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 903: RenderTargetCube MSAA (property fidelity + no-corruption sanity check -- see this
    # test's own header comment for why a genuine sub-pixel AA differential isn't attempted).
    cna_bgfx_test(cna_test_bgfx_rendertargetcube_msaa
                  bgfx_rendertargetcube_msaa_test.cpp)
    cna_register_renderer_test(NAME Bgfx_RenderTargetCube_MsaaResolve COMMAND cna_test_bgfx_rendertargetcube_msaa
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-138: resolved/generated cube content must reach the public sampleable image in
    # producer-consumer order, with no public frame/read/present workaround between them.
    cna_bgfx_test(cna_test_bgfx_gfx138_rendertargetcube_finalize
                  bgfx_rendertargetcube_finalize_test.cpp)
    cna_register_renderer_test(NAME Bgfx_GFX138_RenderTargetCube_Finalize
        COMMAND cna_test_bgfx_gfx138_rendertargetcube_finalize
        TIMEOUT 1200 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-197 / historical Task 952: direct functional cube depth/stencil matrix. Covers all
    # public depth formats, single-sample/supported-MSAA, all faces, ordered aspect clears, stencil
    # gates, A/B/A preservation, same-frame sampling, GetData, handle reuse and device teardown.
    cna_bgfx_test(cna_test_bgfx_rendertargetcube_depthformat
                  bgfx_rendertargetcube_depthformat_test.cpp)
    cna_register_renderer_test(NAME Bgfx_RenderTargetCube_DepthFormat COMMAND cna_test_bgfx_rendertargetcube_depthformat
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 910: binding + filling more than one RenderTarget2D within a single un-advanced bgfx
    # frame no longer clobbers all but the last one bound -- each render target now gets its own
    # distinct bgfx view id instead of sharing one hardcoded id.
    cna_bgfx_test(cna_test_bgfx_concurrent_rendertargets
                  bgfx_concurrent_rendertargets_test.cpp)
    cna_register_renderer_test(NAME Bgfx_ConcurrentRenderTargets COMMAND cna_test_bgfx_concurrent_rendertargets
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 914: Texture3D/TextureCube::GetData is now real on Bgfx (blit into a temporary
    # BGFX_TEXTURE_BLIT_DST|BGFX_TEXTURE_READ_BACK texture + bgfx::readTexture()) -- shared with
    # EasyGL/Vulkan's registration of the same renderer-agnostic files (Tasks 173/275/865).
    cna_bgfx_test(cna_test_bgfx_texture3d_slices
                  ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_texture3d_slices_test.cpp)
    cna_register_renderer_test(NAME Bgfx_Texture3D_Slices_RoundTrip COMMAND cna_test_bgfx_texture3d_slices
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_bgfx_test(cna_test_bgfx_texturecube_partial_rect
                  ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_texturecube_partial_rect_test.cpp)
    cna_register_renderer_test(NAME Bgfx_TextureCube_PartialRect_RoundTrip COMMAND cna_test_bgfx_texturecube_partial_rect
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 914: Texture3D/TextureCube mip-level allocation is now real on Bgfx (mipMap genuinely
    # threaded through createTexture3D/createTextureCube's _hasMips, previously hardcoded false) --
    # shared with EasyGL/Vulkan's registration of the same renderer-agnostic files (Tasks 862/864).
    cna_bgfx_test(cna_test_bgfx_texture3d_mip
                  ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_texture3d_mip_test.cpp)
    cna_register_renderer_test(NAME Bgfx_Texture3D_Mip_RoundTrip COMMAND cna_test_bgfx_texture3d_mip
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_bgfx_test(cna_test_bgfx_texturecube_mip
                  ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_texturecube_mip_test.cpp)
    cna_register_renderer_test(NAME Bgfx_TextureCube_Mip_RoundTrip COMMAND cna_test_bgfx_texturecube_mip
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 923: BlendState ColorBlendFunction/AlphaBlendFunction independence on Bgfx -- a Bgfx-
    # specific adaptation of the EasyGL source (Task 307; can't reuse it verbatim like Vulkan's
    # Task 868 registration does, since it calls GraphicsDevice.SetDepthTestEnabled(false), an
    # unconditional throw on Bgfx -- see the test's own file-header comment). Discriminates Bgfx's
    # colorBlendFunc-always-Add gap (Check A expects Subtract; failed before this task's fix).
    cna_bgfx_test(cna_test_bgfx_blendstate_separate_functions
                  bgfx_blendstate_separate_functions_test.cpp)
    cna_register_renderer_test(NAME Bgfx_BlendState_SeparateFunctions COMMAND cna_test_bgfx_blendstate_separate_functions
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 923: BlendState AlphaSourceBlend/AlphaDestinationBlend independence from the colour
    # factors -- fixed in ApplyBlendState (real BGFX_STATE_BLEND_FUNC_SEPARATE, confirmed correct
    # against bgfx's own renderer_gl.cpp bit-decode logic), but NOT independently pixel-verified:
    # a two-draw indirect-alpha-observation test was built and empirically found to report the
    # SAME (wrong) result whether or not the fix is applied -- this sandbox's bgfx GL2.1/Mesa-
    # llvmpipe software renderer does not appear to honor separately-requested alpha blend factors
    # when they differ from the colour channel's own (confirmed via 3 independent probes; not
    # investigated further, out of this task's scope -- see plan_graphics.md Task 923 for detail).
    # Matches this project's established precedent for a sandbox limitation, not a CNA defect
    # (e.g. Task 448's occlusion query, Task 879's Bgfx_RenderTarget2D_MsaaResolve).

    # Task 926 (split from Task 867, the last of the 3 renderers -- closes the split in full):
    # real Texture2D mip-level upload on Bgfx -- a Bgfx-specific adaptation of the EasyGL test
    # source (Task 298; can't reuse it verbatim, same reasoning as Task 925's Vulkan copy: Bgfx's
    # TextureFilter::Point has always mapped to BGFX_SAMPLER_MIP_POINT, genuinely mip-aware,
    # unlike EasyGL's still-flat mapping -- see the test's own file-header comment).
    cna_bgfx_test(cna_test_bgfx_texture_mip_filter_effect
                  bgfx_texture_mip_filter_effect_test.cpp)
    cna_register_renderer_test(NAME Bgfx_TextureMipFilter_DualTextureEffect COMMAND cna_test_bgfx_texture_mip_filter_effect
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 759 (Phase 72 Bgfx gap closure): DepthStencilState.DepthBufferWriteEnable pixel test
    # on Bgfx -- a Bgfx-specific adaptation of the EasyGL source (Task 313; already reused
    # verbatim on Vulkan, but Bgfx's own GetBackBufferData only reliably reflects the first read
    # per rendered frame, Task 406, so the EasyGL source's 2-reads-per-frame structure needed
    # restructuring into 2 separately-read passes -- see the test's own file-header comment).
    cna_bgfx_test(cna_test_bgfx_depthstencilstate_write_enable
                  bgfx_depthstencilstate_write_enable_test.cpp)
    cna_register_renderer_test(NAME Bgfx_DepthStencilState_WriteEnable COMMAND cna_test_bgfx_depthstencilstate_write_enable
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 760 (Phase 72 Bgfx gap closure): all 8 depth CompareFunction values on Bgfx -- a
    # Bgfx-specific adaptation of the EasyGL source (Task 314; that file only covers 5 of 8 values
    # and reads back 5 columns in one frame, which Bgfx's own GetBackBufferData quirk, Task 406,
    # doesn't support reliably -- see the test's own file-header comment).
    cna_bgfx_test(cna_test_bgfx_depthstencilstate_compare_function
                  bgfx_depthstencilstate_compare_function_test.cpp)
    cna_register_renderer_test(NAME Bgfx_DepthStencilState_CompareFunction COMMAND cna_test_bgfx_depthstencilstate_compare_function
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 761 (Phase 72 Bgfx gap closure): StencilEnable + StencilMask (read mask) +
    # StencilWriteMask on Bgfx -- a Bgfx-specific adaptation of the EasyGL sources (Tasks 315/316;
    # those files sample multiple regions in one frame, incompatible with Bgfx's GetBackBufferData
    # quirk, Task 406 -- see the test's own file-header comment). StencilWriteMask is verified
    # informationally only: bgfx's own state API has no per-draw stencil write-mask flag at all
    # (confirmed against bgfx/defines.h), a permanent renderer limitation, not a bug in this codebase.
    cna_bgfx_test(cna_test_bgfx_depthstencilstate_stencil_mask
                  bgfx_depthstencilstate_stencil_mask_test.cpp)
    cna_register_renderer_test(NAME Bgfx_DepthStencilState_StencilMask COMMAND cna_test_bgfx_depthstencilstate_stencil_mask
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 762 (Phase 72 Bgfx gap closure): front-facing StencilFail/StencilDepthBufferFail/
    # StencilPass operation slots on Bgfx -- a Bgfx-specific adaptation of the EasyGL source (Task
    # 317; that file reads back 4 columns in one frame, incompatible with Bgfx's GetBackBufferData
    # quirk, Task 406 -- see the test's own file-header comment).
    cna_bgfx_test(cna_test_bgfx_depthstencilstate_stencil_ops
                  bgfx_depthstencilstate_stencil_ops_test.cpp)
    cna_register_renderer_test(NAME Bgfx_DepthStencilState_StencilOps COMMAND cna_test_bgfx_depthstencilstate_stencil_ops
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 763 (Phase 72 Bgfx gap closure): TwoSidedStencilMode (separate CCW stencil func/ops for
    # back-facing triangles) on Bgfx -- a Bgfx-specific adaptation of the EasyGL source (Task 318;
    # that file reads back 2 columns in one frame, incompatible with Bgfx's GetBackBufferData
    # quirk, Task 406 -- see the test's own file-header comment).
    cna_bgfx_test(cna_test_bgfx_depthstencilstate_stencil_twosided
                  bgfx_depthstencilstate_stencil_twosided_test.cpp)
    cna_register_renderer_test(NAME Bgfx_DepthStencilState_StencilTwoSided COMMAND cna_test_bgfx_depthstencilstate_stencil_twosided
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 764 (Phase 72 Bgfx gap closure): GraphicsDevice.ReferenceStencil standalone override
    # reaching Bgfx draw calls -- a Bgfx-specific adaptation of the EasyGL source (Task 319; that
    # file reads back 2 regions in one frame, incompatible with Bgfx's GetBackBufferData quirk,
    # Task 406 -- see the test's own file-header comment).
    cna_bgfx_test(cna_test_bgfx_graphicsdevice_reference_stencil
                  bgfx_graphicsdevice_reference_stencil_test.cpp)
    cna_register_renderer_test(NAME Bgfx_GraphicsDevice_ReferenceStencil COMMAND cna_test_bgfx_graphicsdevice_reference_stencil
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 871: GraphicsDevice::Clear() now actually clears the stencil buffer to the requested
    # value on Bgfx too -- a Bgfx-specific adaptation of the EasyGL source (1 step per real frame,
    # since Bgfx's own per-frame view clear cannot be interleaved mid-frame with draws either).
    cna_bgfx_test(cna_test_bgfx_graphicsdevice_clear_stencil
                  bgfx_graphicsdevice_clear_stencil_test.cpp)
    cna_register_renderer_test(NAME Bgfx_GraphicsDevice_ClearStencil COMMAND cna_test_bgfx_graphicsdevice_clear_stencil
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 950: BgfxRenderer::ClearColorAndDepth/ClearColorDepthAndStencil now actually
    # thread their `depth` parameter into bgfx::setViewClear() instead of hardcoding 1.0f --
    # verbatim reuse of the shared renderer-agnostic source (no retry loop needed, same as the
    # Task 871 Bgfx stencil test: each check's Clear()+Draw()+GetBackBufferData() is its own real
    # frame).
    cna_bgfx_test(cna_test_bgfx_graphicsdevice_clear_depth
                  ${CNA_GRAPHICS_EXAMPLES_DIR}/graphicsdevice_clear_depth_test.cpp)
    cna_register_renderer_test(NAME Bgfx_GraphicsDevice_ClearDepth COMMAND cna_test_bgfx_graphicsdevice_clear_depth
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-018: complete public ClearOptions mask contract. Establishes distinct pre-existing
    # colour/depth/stencil values, covers every flag combination + public overloads, ordered clears,
    # full-target viewport/scissor independence, backbuffer, Depth24Stencil8/Depth24/None RTs,
    # target isolation, and A->B->A. One whole-framebuffer snapshot per observable result; run
    # through both Bgfx renderer routes. The fixture selects clip-space probes from the actually
    # active renderer, runs two complete initialization/teardown cycles, and classifies requested
    # Vulkan -> active OpenGL as a successful fallback oracle followed by deterministic exit 77:
    # native Vulkan coverage is skipped honestly rather than reported as a Vulkan pass.
    cna_bgfx_test(cna_test_bgfx_graphicsdevice_clearoptions
                  bgfx_graphicsdevice_clearoptions_test.cpp)
    cna_register_renderer_test(NAME Bgfx_GraphicsDevice_ClearOptions COMMAND cna_test_bgfx_graphicsdevice_clearoptions
        TIMEOUT 120 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY};CNA_BGFX_RENDERER=OPENGL")
    cna_register_renderer_test(NAME Bgfx_GraphicsDevice_ClearOptions_Vulkan COMMAND cna_test_bgfx_graphicsdevice_clearoptions
        TIMEOUT 120 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY};CNA_BGFX_RENDERER=VULKAN")

    # Task 955: a freshly-constructed GraphicsDevice's own default state (no game code ever
    # explicitly setting BlendState/DepthStencilState/RasterizerState, exactly like
    # ../cna-samples SimpleAnimation's Tank.hpp) must produce correct opaque depth occlusion.
    cna_bgfx_test(cna_test_bgfx_graphicsdevice_default_state_occlusion
                  ${CNA_GRAPHICS_EXAMPLES_DIR}/graphicsdevice_default_state_occlusion_test.cpp)
    cna_register_renderer_test(NAME Bgfx_GraphicsDevice_DefaultStateOcclusion COMMAND cna_test_bgfx_graphicsdevice_default_state_occlusion
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 889: DualTextureEffect.VertexColorEnabled -- verbatim reuse of the shared
    # renderer-agnostic source, registered on all 3 renderers.
    cna_bgfx_test(cna_test_bgfx_dualtextureeffect_vertexcolor
                  ${CNA_GRAPHICS_EXAMPLES_DIR}/dualtextureeffect_vertexcolor_test.cpp)
    cna_register_renderer_test(NAME Bgfx_DualTextureEffect_VertexColor COMMAND cna_test_bgfx_dualtextureeffect_vertexcolor
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-017: complete public winding contract on Bgfx. The fixture covers None,
    # CullClockwiseFace, CullCounterClockwiseFace, the GraphicsDevice/RasterizerState defaults,
    # opposite-winding colored geometry, backbuffer + RenderTarget2D, A->B->A state capture,
    # target transitions, BasicEffect/direct 3D, and SpriteBatch state isolation. Run the same
    # permanent regression through both Bgfx renderer routes; a host without usable Vulkan may
    # fall back to OpenGL, which the renderer reports explicitly in its native startup output.
    cna_bgfx_test(cna_test_bgfx_rasterizerstate_cullmode
                  bgfx_rasterizerstate_cullmode_test.cpp)
    cna_register_renderer_test(NAME Bgfx_RasterizerState_CullMode COMMAND cna_test_bgfx_rasterizerstate_cullmode
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY};CNA_BGFX_RENDERER=OPENGL")
    cna_register_renderer_test(NAME Bgfx_RasterizerState_CullMode_Vulkan COMMAND cna_test_bgfx_rasterizerstate_cullmode
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY};CNA_BGFX_RENDERER=VULKAN")

    # XNA culling-compatibility audit: real-camera CullMode reproducer, verbatim reuse of the
    # shared renderer-agnostic source (first tried on Bgfx as-is; see the audit doc for whether
    # Bgfx's own multiple-reads-per-frame GetBackBufferData limitation, Task 406, required a
    # restructure here).
    cna_bgfx_test(cna_test_bgfx_rasterizerstate_cullmode_camera
                  ${CNA_GRAPHICS_EXAMPLES_DIR}/rasterizerstate_cullmode_camera_test.cpp)
    cna_register_renderer_test(NAME Bgfx_RasterizerState_CullMode_Camera COMMAND cna_test_bgfx_rasterizerstate_cullmode_camera
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_bgfx_test(cna_test_bgfx_rasterizerstate_cullmode_indexed_basiceffect
                  ${CNA_GRAPHICS_EXAMPLES_DIR}/rasterizerstate_cullmode_indexed_basiceffect_test.cpp)
    cna_register_renderer_test(NAME Bgfx_RasterizerState_CullMode_IndexedBasicEffect COMMAND cna_test_bgfx_rasterizerstate_cullmode_indexed_basiceffect
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 766 (Phase 72 Bgfx gap closure): FillMode::WireFrame on Bgfx -- adapted from
    # vulkan_fill_mode_test.cpp (Task 327; no EasyGL/shared source exists for this property, no
    # dedicated pixel test existed anywhere in this project before this task -- see the test's own
    # file-header comment). Exercises the new ExpandWireframeIndices triangle-to-line-list
    # emulation (bgfx has no native polygon-fill-mode toggle).
    cna_bgfx_test(cna_test_bgfx_rasterizerstate_fillmode_wireframe
                  bgfx_rasterizerstate_fillmode_wireframe_test.cpp)
    cna_register_renderer_test(NAME Bgfx_RasterizerState_FillMode_WireFrame COMMAND cna_test_bgfx_rasterizerstate_fillmode_wireframe
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 767 (Phase 72 Bgfx gap closure, project-owner decision 2026-07-10): RasterizerState.
    # DepthBias on Bgfx -- bgfx has no native polygon-offset mechanism, emulated via a per-draw
    # vertex-shader Z-offset (see BgfxRenderer::SetDepthBiasUniform, u_depthBias in every
    # 3D vertex shader). SlopeScaleDepthBias deliberately NOT emulated (documented gap, checked
    # informationally only -- see the test's own file-header comment).
    cna_bgfx_test(cna_test_bgfx_rasterizerstate_depthbias
                  bgfx_rasterizerstate_depthbias_test.cpp)
    cna_register_renderer_test(NAME Bgfx_RasterizerState_DepthBias COMMAND cna_test_bgfx_rasterizerstate_depthbias
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 768 (Phase 72 Bgfx gap closure): ScissorTestEnable/ScissorRectangle interaction on Bgfx
    # -- a Bgfx-specific adaptation of the EasyGL source (Task 209; that file reads 2 columns per
    # frame across 3 draws, incompatible with Bgfx's GetBackBufferData quirk, Task 406 -- see the
    # test's own file-header comment). Found and fixed a genuine bug: none of Bgfx's 4 3D
    # draw-dispatch functions ever called bgfx::setScissor (only the 2D SpriteBatch path did).
    cna_bgfx_test(cna_test_bgfx_scissor
                  bgfx_scissor_test.cpp)
    cna_register_renderer_test(NAME Bgfx_Scissor COMMAND cna_test_bgfx_scissor
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Tasks 752-755 (Phase 72 Bgfx gap closure): BlendState::Opaque/AlphaBlend/NonPremultiplied/
    # Additive on Bgfx -- each is a Bgfx-specific copy of the renderer-agnostic EasyGL source (also
    # reused verbatim on Vulkan above), differing only in replacing the legacy
    # GraphicsDevice::SetDepthTestEnabled(false) call (throws on Bgfx -- a pre-existing, deliberate
    # stub) with the equivalent DepthStencilState-based call. Each still does exactly 1 Draw + 1
    # GetBackBufferData read per frame, so Bgfx's own "first read per rendered frame" quirk (Task
    # 406) does not apply -- no other restructuring needed.
    cna_bgfx_test(cna_test_bgfx_blendstate_opaque
                  bgfx_blendstate_opaque_test.cpp)
    cna_register_renderer_test(NAME Bgfx_BlendState_Opaque COMMAND cna_test_bgfx_blendstate_opaque
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-077: BlendState.ColorWriteChannels via the per-draw BGFX_STATE_WRITE_* bits (RT0).
    cna_bgfx_test(cna_test_bgfx_colorwritechannels
                  bgfx_colorwritechannels_test.cpp)
    cna_register_renderer_test(NAME Bgfx_ColorWriteChannels COMMAND cna_test_bgfx_colorwritechannels
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_bgfx_test(cna_test_bgfx_blendstate_alphablend
                  bgfx_blendstate_alphablend_test.cpp)
    cna_register_renderer_test(NAME Bgfx_BlendState_AlphaBlend COMMAND cna_test_bgfx_blendstate_alphablend
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_bgfx_test(cna_test_bgfx_blendstate_nonpremultiplied
                  bgfx_blendstate_nonpremultiplied_test.cpp)
    cna_register_renderer_test(NAME Bgfx_BlendState_NonPremultiplied COMMAND cna_test_bgfx_blendstate_nonpremultiplied
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_bgfx_test(cna_test_bgfx_blendstate_additive
                  bgfx_blendstate_additive_test.cpp)
    cna_register_renderer_test(NAME Bgfx_BlendState_Additive COMMAND cna_test_bgfx_blendstate_additive
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 803 (Phase 72 Bgfx gap closure): SpriteSortMode ordering affects on-screen draw order
    # on Bgfx -- a Bgfx-specific adaptation of the EasyGL source (Task 420; that file reads back 3
    # regions from one rendered frame, incompatible with Bgfx's GetBackBufferData quirk, Task 406
    # -- see the test's own file-header comment for full scope notes).
    cna_bgfx_test(cna_test_bgfx_spritebatch_layerdepth
                  bgfx_spritebatch_layerdepth_test.cpp)
    cna_register_renderer_test(NAME Bgfx_SpriteBatch_LayerDepthOrder COMMAND cna_test_bgfx_spritebatch_layerdepth
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 804 (Phase 72 Bgfx gap closure): SpriteBatch::Draw rotation-around-origin on Bgfx -- a
    # Bgfx-specific adaptation of the EasyGL source (Task 417; that file reads back 3 regions from
    # one rendered frame, incompatible with Bgfx's GetBackBufferData quirk, Task 406 -- see the
    # test's own file-header comment).
    cna_bgfx_test(cna_test_bgfx_spritebatch_rotation
                  bgfx_spritebatch_rotation_test.cpp)
    cna_register_renderer_test(NAME Bgfx_SpriteBatch_Rotation COMMAND cna_test_bgfx_spritebatch_rotation
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 805 (Phase 72 Bgfx gap closure): SpriteBatch::Draw scalar/Vector2 scale overloads on
    # Bgfx -- a Bgfx-specific adaptation of the EasyGL source (Task 418; that file reads back 5
    # regions from one rendered frame, incompatible with Bgfx's GetBackBufferData quirk, Task 406
    # -- see the test's own file-header comment).
    cna_bgfx_test(cna_test_bgfx_spritebatch_scale
                  bgfx_spritebatch_scale_test.cpp)
    cna_register_renderer_test(NAME Bgfx_SpriteBatch_Scale COMMAND cna_test_bgfx_spritebatch_scale
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 806 (Phase 72 Bgfx gap closure): SpriteBatch::Draw sourceRectangle cropping on Bgfx --
    # a Bgfx-specific adaptation of the EasyGL source (Task 419; that file reads back 3 regions
    # from one rendered frame, incompatible with Bgfx's GetBackBufferData quirk, Task 406 -- see
    # the test's own file-header comment).
    cna_bgfx_test(cna_test_bgfx_spritebatch_sourcerect
                  bgfx_spritebatch_sourcerect_test.cpp)
    cna_register_renderer_test(NAME Bgfx_SpriteBatch_SourceRectangleCropping COMMAND cna_test_bgfx_spritebatch_sourcerect
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 807 (Phase 72 Bgfx gap closure): SpriteEffects::FlipHorizontally/FlipVertically on
    # Bgfx (mirrors Task 167) -- a Bgfx-specific adaptation of the EasyGL source (that file reads
    # back 8 points from 4 draws in one rendered frame, incompatible with Bgfx's
    # GetBackBufferData quirk, Task 406 -- see the test's own file-header comment).
    cna_bgfx_test(cna_test_bgfx_sprite_effects
                  bgfx_sprite_effects_test.cpp)
    cna_register_renderer_test(NAME Bgfx_SpriteEffects_Flip COMMAND cna_test_bgfx_sprite_effects
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 808 (Phase 72 Bgfx gap closure): SpriteBatch::Begin's transformMatrix parameter on
    # Bgfx (mirrors Task 168) -- a Bgfx-specific adaptation of the EasyGL source (that file reads
    # back 2 regions from one rendered frame, incompatible with Bgfx's GetBackBufferData quirk,
    # Task 406 -- see the test's own file-header comment).
    cna_bgfx_test(cna_test_bgfx_transform_matrix
                  bgfx_transform_matrix_test.cpp)
    cna_register_renderer_test(NAME Bgfx_SpriteBatch_TransformMatrix COMMAND cna_test_bgfx_transform_matrix
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 809 (Phase 72 Bgfx gap closure): SpriteFont single-glyph placement on Bgfx -- a
    # Bgfx-specific adaptation of the EasyGL source (Task 424; that file reads back 5 regions from
    # one rendered frame, incompatible with Bgfx's GetBackBufferData quirk, Task 406 -- see the
    # test's own file-header comment).
    cna_bgfx_test(cna_test_bgfx_spritefont_single_glyph
                  bgfx_spritefont_single_glyph_test.cpp)
    cna_register_renderer_test(NAME Bgfx_SpriteFont_SingleGlyph COMMAND cna_test_bgfx_spritefont_single_glyph
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 810 (Phase 72 Bgfx gap closure): SpriteFont multi-glyph spacing + newline advance on
    # Bgfx -- a Bgfx-specific adaptation combining the EasyGL sources (Tasks 425/426; those files
    # read back several regions from one rendered frame, incompatible with Bgfx's
    # GetBackBufferData quirk, Task 406 -- see the test's own file-header comment).
    cna_bgfx_test(cna_test_bgfx_spritefont_multiglyph_newline
                  bgfx_spritefont_multiglyph_newline_test.cpp)
    cna_register_renderer_test(NAME Bgfx_SpriteFont_MultiGlyphSpacingNewline COMMAND cna_test_bgfx_spritefont_multiglyph_newline
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 811 (Phase 72 Bgfx gap closure): SpriteFont default-character fallback on Bgfx -- a
    # Bgfx-specific adaptation of the EasyGL source (Task 427; that file reads back 3 regions from
    # one rendered frame, incompatible with Bgfx's GetBackBufferData quirk, Task 406 -- see the
    # test's own file-header comment). Closes the entire Phase 72 SpriteFont row group (809-811).
    cna_bgfx_test(cna_test_bgfx_spritefont_default_char
                  bgfx_spritefont_default_char_test.cpp)
    cna_register_renderer_test(NAME Bgfx_SpriteFont_DefaultCharacterFallback COMMAND cna_test_bgfx_spritefont_default_char
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 812 (Phase 72 Bgfx gap closure): Model with two meshes, each using its own distinct
    # Effect, on Bgfx -- a Bgfx-specific adaptation of the EasyGL source (Task 438; that file uses
    # SetDepthTestEnabled(false), which throws on Bgfx, and reads back 2 regions from one rendered
    # frame, incompatible with Bgfx's GetBackBufferData quirk, Task 406 -- see the test's own
    # file-header comment).
    cna_bgfx_test(cna_test_bgfx_model_two_meshes_effects
                  bgfx_model_two_meshes_effects_test.cpp)
    cna_register_renderer_test(NAME Bgfx_Model_TwoMeshesEffects COMMAND cna_test_bgfx_model_two_meshes_effects
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 927: ModelTypeReader::Read()'s vertex-stride/vtable-size corruption fix is in SHARED
    # code (ContentManager.cpp) -- a Bgfx-specific adaptation of the EasyGL source, restructured
    # into per-check RunCheck() passes for Bgfx's own GetBackBufferData "first read per frame"
    # quirk (Task 406).
    cna_bgfx_test(cna_test_bgfx_model_json_reader
                  bgfx_model_json_reader_test.cpp)
    cna_register_renderer_test(NAME Bgfx_ModelJsonReader_Quad COMMAND cna_test_bgfx_model_json_reader
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 813 (Phase 72 Bgfx gap closure): Model hierarchy transform propagation (child mesh's
    # own ParentBone) on Bgfx -- a Bgfx-specific adaptation of the EasyGL source (Task 439; same
    # SetDepthTestEnabled/GetBackBufferData substitutions as Task 812 above). Closes the entire
    # Phase 72 Model row group (812-813).
    cna_bgfx_test(cna_test_bgfx_model_hierarchy_child_mesh
                  bgfx_model_hierarchy_child_mesh_test.cpp)
    cna_register_renderer_test(NAME Bgfx_Model_HierarchyChildMesh COMMAND cna_test_bgfx_model_hierarchy_child_mesh
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Tasks 814/815 (Phase 72 Bgfx gap closure): OcclusionQuery visible/occluded pixel counts on
    # Bgfx -- Bgfx-specific counterpart of the EasyGL sources (Tasks 445/446), combined into one
    # file since both scenarios share the same fixture (see the test's own file-header comment).
    # Real, pixel-verified assertions cover the underlying rendering/depth-occlusion behavior;
    # OcclusionQuery.IsComplete()/PixelCount() are reported informational-only -- a scratch probe
    # confirmed PixelCount() does not discriminate visible from occluded geometry in this sandbox's
    # software GL renderer (same class of limitation as Task 448/923).
    cna_bgfx_test(cna_test_bgfx_occlusionquery_pixelcount
                  bgfx_occlusionquery_pixelcount_test.cpp)
    cna_register_renderer_test(NAME Bgfx_OcclusionQuery_PixelCount COMMAND cna_test_bgfx_occlusionquery_pixelcount
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 816 (Phase 72 Bgfx gap closure): explicit Dispose() on an ACTIVE OcclusionQuery is safe
    # on Bgfx -- distinct from Task 449's own EasyGL-only occlusion_query_test.cpp, which covers
    # C++ destruction, not the explicit .Dispose() API call (see the test's own file-header
    # comment). Closes the entire Phase 72 OcclusionQuery row group (814-816).
    cna_bgfx_test(cna_test_bgfx_occlusionquery_dispose_active
                  bgfx_occlusionquery_dispose_active_test.cpp)
    cna_register_renderer_test(NAME Bgfx_OcclusionQuery_DisposeActive COMMAND cna_test_bgfx_occlusionquery_dispose_active
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_cnj.md CNB-58/60 (Phase 13A), Bgfx port: PbrEffect real glTF metallic-roughness BRDF
    # (vs_pbr3d.sc/fs_pbr3d.sc, stride-48 VertexPositionNormalTangentTexture) -- closes the
    # EasyGL-only PBR gap for the Bgfx renderer (see bgfx_pbreffect_test.cpp's own
    # file-header comment for the fully analytic camera/light derivation).
    cna_bgfx_test(cna_test_bgfx_pbreffect
                  bgfx_pbreffect_test.cpp)
    cna_register_renderer_test(NAME Bgfx_PbrEffect COMMAND cna_test_bgfx_pbreffect
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_cnj.md CNB-58/60 (Phase 13A), Bgfx port: SkinnedPbrEffect (PBR + skinning combo,
    # vs_pbr_skinned3d.sc sharing fs_pbr3d.sc, stride-68
    # VertexPositionNormalTangentTextureSkinned) -- reuses bgfx_pbreffect_test.cpp's own
    # expected values via an identity bind pose (see the test's own file-header comment).
    cna_bgfx_test(cna_test_bgfx_skinnedpbreffect
                  bgfx_skinnedpbreffect_test.cpp)
    cna_register_renderer_test(NAME Bgfx_SkinnedPbrEffect COMMAND cna_test_bgfx_skinnedpbreffect
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # GLTF-213: exact mid-grey transfer witnesses for both Bgfx PBR programs.
    cna_bgfx_test(cna_test_bgfx_pbr_srgb_transfer
                  bgfx_pbr_srgb_transfer_test.cpp)
    target_link_libraries(cna_test_bgfx_pbr_srgb_transfer PRIVATE SDL3::SDL3)
    cna_register_renderer_test(NAME Bgfx_Pbr_SrgbTransfer COMMAND cna_test_bgfx_pbr_srgb_transfer
        TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # GLTF-343/344: renderer-specific async-readback adaptation of the shared Fresnel witness.
    cna_bgfx_test(cna_test_bgfx_pbr_fresnel_factors
                  bgfx_pbr_fresnel_factors_test.cpp)
    target_link_libraries(cna_test_bgfx_pbr_fresnel_factors PRIVATE SDL3::SDL3)
    cna_register_renderer_test(NAME Bgfx_Pbr_FresnelFactors COMMAND cna_test_bgfx_pbr_fresnel_factors
        TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # CNB-67 (Phase 13C), Bgfx port: SkinnedEffect.VertexColorEnabled on the stride-56 skinned+
    # Color vertex layout (vs_skinned3d.sc/fs_skinned3d.sc and vs_skinned3d_vertexlit.sc/
    # fs_skinned3d_vertexlit.sc's new a_color0/v_vertexColor0/u_vertexColorEnabled3D wiring) --
    # closes the EasyGL-only vertex-color-on-skinned-mesh gap for the Bgfx renderer.
    cna_bgfx_test(cna_test_bgfx_skinnedeffect_vertexcolor
                  bgfx_skinnedeffect_vertexcolor_test.cpp)
    cna_register_renderer_test(NAME Bgfx_SkinnedEffect_VertexColor COMMAND cna_test_bgfx_skinnedeffect_vertexcolor
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-072: SpriteBatch clip space must be built from the active GraphicsDevice.Viewport
    # (Viewport.Width/Height), not the full target. Bgfx's sprite view built its ortho from the
    # RT/window dims and set the sprite view rect to the full target (ApplyViewportOverride was wired
    # only into the 3D path), so a custom sub-Viewport was ignored for sprites. The sprite view must
    # project by Viewport.W/H and setViewRect the sub-region. (Multiple viewports on one view in one
    # frame remain last-wins -- REMED-GFX-065 -- so only the single-custom-viewport case is asserted.)
    cna_bgfx_test(cna_test_bgfx_spritebatch_custom_viewport
                  ${CNA_GRAPHICS_EXAMPLES_DIR}/spritebatch_custom_viewport_test.cpp)
    cna_register_renderer_test(NAME Bgfx_SpriteBatch_CustomViewport COMMAND cna_test_bgfx_spritebatch_custom_viewport
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-127: every public Texture2D::GetData call must return the resource's real content or
    # reject the request deterministically -- never fabricate one. Pre-fix the shared render-target
    # fallback zero-initialized its own scratch buffer, handed it to ITextureRenderer::GetData (whose
    # interface default did nothing) and converted it for the caller regardless, so a renderer with no
    # readback returned a complete, uniformly transparent-black frame that satisfied both "the
    # destination was overwritten" and any transparent-black content expectation.
    # REMED-GFX-131: SurfaceFormat::Color is a plain 8-bit UNORM byte format, so a mid-tone channel
    # must survive Clear/draw/sample/readback unchanged. Registered here as a cross-renderer control:
    # the defect was WebGPU-local (its render targets used the swapchain's *UnormSrgb format), and
    # these runs are what establish that byte-exact identity is CNA's existing behaviour everywhere
    # else rather than a value invented for the fix.
    cna_bgfx_test(cna_test_bgfx_colorspace_midtone
        ${CNA_GRAPHICS_EXAMPLES_DIR}/colorspace_midtone_contract_test.cpp)
    cna_register_renderer_test(NAME Bgfx_ColorSpace_MidTone COMMAND cna_test_bgfx_colorspace_midtone
        TIMEOUT 120 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_bgfx_test(cna_test_bgfx_additive_blend_contract
        ${CNA_GRAPHICS_EXAMPLES_DIR}/additive_blend_contract_test.cpp)
    cna_register_renderer_test(NAME Bgfx_AdditiveBlendContract
        COMMAND cna_test_bgfx_additive_blend_contract
        TIMEOUT 120 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-147: a RenderTarget2D used as a texture must sample in the same logical orientation
    # as an ordinary Texture2D holding identical bytes. Registered here as a cross-renderer control:
    # the defect was EasyGL-local (an OpenGL framebuffer's origin is bottom-left, so a target's
    # colour texture stores the image bottom-up and sampling did not compensate even though GetData
    # already did), and these runs are what establish that render-target and ordinary-texture
    # sampling already agree everywhere else rather than being made to agree by the fix.
    cna_bgfx_test(cna_test_bgfx_rt_sampling_orientation
        ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_sampling_orientation_test.cpp)
    cna_register_renderer_test(NAME Bgfx_RenderTarget_SamplingOrientation COMMAND cna_test_bgfx_rt_sampling_orientation
        TIMEOUT 120 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-153: source rectangles on a RenderTarget2D must select the same logical rows as an
    # ordinary Texture2D, independently of source origin, destination target, scaling, effects, and
    # repeated target/backbuffer transitions.
    cna_bgfx_test(cna_test_bgfx_gfx153_source_rectangle
        ${CNA_GRAPHICS_EXAMPLES_DIR}/source_rectangle_orientation_test.cpp)
    cna_register_renderer_test(NAME Bgfx_GFX153_SourceRectangleOrientation
        COMMAND cna_test_bgfx_gfx153_source_rectangle
        TIMEOUT 180 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")



    # REMED-GFX-169: every stock 3D effect must sample its textures through the public
    # GraphicsDevice.SamplerStates[slot], exactly like SpriteBatch already does. Vulkan's
    # ApplySamplerState is correct and SpriteBatch reads slotSamplers_[0], but six stock 3D
    # descriptor builders take NO sampler parameter at all, so all 15 of their combined-image-
    # sampler bindings are hardcoded to defaultSampler_ (LINEAR + CLAMP_TO_EDGE) and the sampler is
    # absent from their cache keys too. Vulkan is where the defect lives; the other renderers run the
    # same public fixture as controls.
    cna_bgfx_test(cna_test_bgfx_stock_effect_sampler
        ${CNA_GRAPHICS_EXAMPLES_DIR}/stock_effect_sampler_contract_test.cpp)
    cna_register_renderer_test(NAME Bgfx_StockEffectSamplerContract COMMAND cna_test_bgfx_stock_effect_sampler
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
    cna_bgfx_test(cna_test_bgfx_texture_filter_ordinal
        ${CNA_GRAPHICS_EXAMPLES_DIR}/texture_filter_ordinal_contract_test.cpp)
    cna_register_renderer_test(NAME Bgfx_TextureFilterOrdinalContract COMMAND cna_test_bgfx_texture_filter_ordinal
        TIMEOUT 300 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-173 cross-renderer control: EnvironmentMapEffect samples TWO independent
    # resources -- the ordinary 2D texture through GraphicsDevice.SamplerStates[0] and the
    # reflection cube through SamplerStates[1]. SDL_GPU bound a literal LinearClamp for the cube
    # and captured only slot 0, so slot 1 could not reach replay at all. This fixture makes the
    # two slots independently observable (EnvironmentMapAmount 1 leaves the cube the only
    # contributor, 0 leaves the 2D texture the only one) and measures what every other renderer
    # does with the same public state.
    cna_bgfx_test(cna_test_bgfx_envmap_cube_sampler
        ${CNA_GRAPHICS_EXAMPLES_DIR}/envmap_cube_sampler_contract_test.cpp)
    cna_register_renderer_test(NAME Bgfx_EnvMapCubeSamplerContract COMMAND cna_test_bgfx_envmap_cube_sampler
        TIMEOUT 600 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-172 cross-renderer control: DualTextureEffect's two layers have INDEPENDENT public
    # sampler slots -- FNA's DualTextureEffect.fx declares DECLARE_TEXTURE(Texture, 0) and
    # DECLARE_TEXTURE(Texture2, 1). WebGPU declared ONE WGSL sampler for both texture views, so
    # SamplerStates[1] was inexpressible and slot 1 inherited slot 0's. This fixture observes both
    # layers in the SAME image -- slot 0's texture varies along X only, slot 1's along Y only, and
    # the shader's product is separable -- so independence is measured, not inferred.
    cna_bgfx_test(cna_test_bgfx_dualtexture_slot_sampler
        ${CNA_GRAPHICS_EXAMPLES_DIR}/dualtexture_slot_sampler_contract_test.cpp)
    cna_register_renderer_test(NAME Bgfx_DualTextureSlotSamplerContract COMMAND cna_test_bgfx_dualtexture_slot_sampler
        TIMEOUT 600 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-175 cross-renderer control: the MIPMAP component of a TextureFilter ordinal.
    # EasyGL mapped ordinals 0 and 1 onto a GL filter with no mipmap term and Software had no
    # mip pipeline at all; this fixture measures what every other renderer does with a chain
    # whose levels name themselves, so a divergence is classified rather than assumed.
    cna_bgfx_test(cna_test_bgfx_texture_filter_mip_contract
        ${CNA_GRAPHICS_EXAMPLES_DIR}/texture_filter_mip_contract_test.cpp)
    cna_register_renderer_test(NAME Bgfx_TextureFilterMipContract COMMAND cna_test_bgfx_texture_filter_mip_contract
        TIMEOUT 300 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-177 cross-renderer control: descriptor/binding bookkeeping must be a function of what
    # is LIVE, never of what has ever existed. D3D12 owned four fixed-capacity heaps with a monotonic
    # bump cursor and no free list, so the 65th sampleable resource EVER CREATED threw even when six
    # were alive. Every draw here is checked against a self-identifying oracle that decodes back to an
    # integer, so a recycled or grown pool that aliases one resource onto another's binding names the
    # wrong resource rather than merely producing an odd colour.
    cna_bgfx_test(cna_test_bgfx_descriptor_capacity
        ${CNA_GRAPHICS_EXAMPLES_DIR}/descriptor_capacity_contract_test.cpp)
    cna_register_renderer_test(NAME Bgfx_DescriptorCapacityContract COMMAND cna_test_bgfx_descriptor_capacity
        TIMEOUT 900 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-179: bgfx view IDs are per-frame command routing, not render-target descriptors.
    # The former [1,64) persistent-ID partition made the 64th simultaneously live target throw
    # before any target was bound.  This renderer-specific fixture covers that exact 63/64 boundary,
    # farther counts, sequential reclamation, A/B/A ordering, native-handle and per-frame view-ID
    # reuse, both reserved-view readback paths, multiple frames, and teardown with live targets.
    cna_bgfx_test(cna_test_bgfx_gfx179_view_capacity
        bgfx_gfx179_view_capacity_test.cpp)
    cna_register_renderer_test(NAME Bgfx_GFX179_ViewCapacity
        COMMAND cna_test_bgfx_gfx179_view_capacity
        TIMEOUT 300
        ENVIRONMENT
            "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY};CNA_BGFX_TRACE_VIEW_ORDER=1;CNA_BGFX_TRACE_READBACK=1")

    cna_bgfx_test(cna_test_bgfx_point_sampling
        ${CNA_GRAPHICS_EXAMPLES_DIR}/point_sampling_contract_test.cpp)
    cna_register_renderer_test(NAME Bgfx_PointSamplingContract COMMAND cna_test_bgfx_point_sampling
        TIMEOUT 300 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-151 cross-renderer control: the canonical XNA render-to-texture sequence -- render
    # into a target, unbind it, sample it -- must complete in ONE public frame with no intervening
    # GetData, Present, extra frame, manual flush or wait. The defect was Vulkan-local (its deferred
    # recorder's readback flush filtered the frame's segment list down to the target being READ, so a
    # producer's pass was never recorded before the consumer that sampled it); these runs are what
    # establish that every other renderer already honoured the contract rather than being made to.
    cna_bgfx_test(cna_test_bgfx_rt_producer_consumer
        ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_producer_consumer_test.cpp)
    cna_register_renderer_test(NAME Bgfx_RenderTarget_ProducerConsumer COMMAND cna_test_bgfx_rt_producer_consumer
        TIMEOUT 120 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-152 cross-renderer control: a RenderTarget2D handed to a stock or custom 3D effect as
    # its texture must be sampled, not reinterpreted. The defect was SDL_GPU-local and fatal (its
    # stock-effect paths static_cast an ITextureRenderer* to the unrelated sibling
    # SdlGpuTextureRenderer, fabricating an SDL_GPUTexture* out of a render target's own fields); this
    # run is what establishes that BGFX already honoured the contract rather than being made to.
    cna_bgfx_test(cna_test_bgfx_rt_effect_source
        ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_effect_source_test.cpp)
    cna_register_renderer_test(NAME Bgfx_RenderTarget_EffectSource COMMAND cna_test_bgfx_rt_effect_source
        TIMEOUT 300 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-167 deferred-source lifetime: a resource sampled by a draw that has only been
    # QUEUED must stay bindable until that draw actually renders, and destroying the public wrapper
    # first must never terminate the process. The defect was WebGPU-local -- every deferred command
    # stored a raw pointer to the resource's RENDERER OBJECT and called a VIRTUAL method on it at
    # replay, so a RenderTarget2D produced, sampled onto the BACKBUFFER and dropped inside one
    # Draw() was a heap-use-after-free at Present(). These runs establish which renderers already
    # honoured the contract rather than being made to; each leg runs in its own process so a
    # SIGSEGV is an attributable result instead of a lost shard.
    cna_bgfx_test(cna_test_bgfx_deferred_source_lifetime
        ${CNA_GRAPHICS_EXAMPLES_DIR}/deferred_source_lifetime_test.cpp)
    cna_register_renderer_test(NAME Bgfx_DeferredSourceLifetime COMMAND cna_test_bgfx_deferred_source_lifetime
        TIMEOUT 300 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-168 cross-renderer control: destroying a RenderTarget2D that is STILL the bound
    # render target must never make the next SetRenderTarget transition unsafe, and must leave the
    # next target and the backbuffer exactly correct. The defect was EasyGL-local -- it remembered
    # the bound destination as a raw IRenderTargetRenderer* and the next transition called
    # UnbindAsRenderTarget() on it, so a scoped target leaving scope while bound made that
    # transition a virtual call through freed storage. This run is what establishes that BGFX
    # already honoured the contract rather than being made to; each leg runs in its own process so
    # a SIGSEGV is an attributable result instead of a lost shard.
    cna_bgfx_test(cna_test_bgfx_bound_target_lifetime
        ${CNA_GRAPHICS_EXAMPLES_DIR}/bound_target_lifetime_test.cpp)
    cna_register_renderer_test(NAME Bgfx_BoundTargetLifetime COMMAND cna_test_bgfx_bound_target_lifetime
        TIMEOUT 600 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-180: the public render-target -> Present lifecycle contract. `SdlGpu_RenderState`
    # aborted with `Cannot present while render targets are bound`, which is the SECOND event in a
    # two-stage chain: an over-range DrawPrimitives (two triangles from a three-vertex buffer) was
    # correctly rejected by REMED-GFX-113's range guard, that rejection unwound past the fixture's own
    # SetRenderTarget(nullptr), and EndDraw then met a still-bound target. FNA's contract is REJECT,
    # never auto-unbind, so both guards are right and the fixture was wrong. Every leg runs in its own
    # process; leg C2 reproduces the abort end to end and its correct outcome is SIGABRT.
    cna_bgfx_test(cna_test_bgfx_present_lifecycle
        ${CNA_GRAPHICS_EXAMPLES_DIR}/present_lifecycle_contract_test.cpp)
    cna_register_renderer_test(NAME Bgfx_PresentLifecycle COMMAND cna_test_bgfx_present_lifecycle
        TIMEOUT 900 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-163: a depth-backed MULTISAMPLED RenderTarget2D killed the process by SIGTRAP inside
    # bgfx's own framebuffer validation -- the depth attachment was created with the sample-count flag
    # alone, and bgfx requires BGFX_TEXTURE_RT_WRITE_ONLY (or BGFX_TEXTURE_MSAA_SAMPLE) on any depth
    # attachment whose sample count is greater than one, because it cannot resolve multisampled depth.
    # The identical fix already existed on the CUBE path (REMED-GFX-141) and was never applied to its
    # 2D sibling. Every leg runs in its own process so a native ASSERT cannot take the shard with it;
    # the supervisor names SIGTRAP specifically. No leg is permitted to abort.
    cna_bgfx_test(cna_test_bgfx_msaa_depth_contract
        ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_msaa_depth_contract_test.cpp)
    cna_register_renderer_test(NAME Bgfx_MsaaDepthContract COMMAND cna_test_bgfx_msaa_depth_contract
        TIMEOUT 900 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-154, the home renderer of the finding: the FIRST GetData of a multisampled
    # RenderTarget2D returned every texel as (0,0,0,0) while a second read of the same target was
    # exact. bgfx's GL renderer resolves a framebuffer only when the renderer switches AWAY from it,
    # and the readback blit was drained by the tail submitBlit() while the producer's framebuffer was
    # still current and unresolved -- so the copy read the freshly-zeroed resolve texture. Every leg
    # runs in its own process, because "the first readback of the PROCESS" is exactly what is under
    # test. The Bgfx half of REMED-GFX-164 is the same defect; its distinct EasyGL scope was closed
    # separately by resolving an active target before direct readback.
    cna_bgfx_test(cna_test_bgfx_msaa_first_readback
        ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_msaa_first_readback_test.cpp)
    cna_register_renderer_test(NAME Bgfx_MsaaFirstReadback COMMAND cna_test_bgfx_msaa_first_readback
        TIMEOUT 900 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-186 cross-renderer control: the same PUBLIC generated-mip readback contract on this
    # renderer. The SIGSEGV was SDL_GPU-only and only SDL_GPU production changed; running the
    # identical fixture here is what makes that claim falsifiable rather than asserted.
    cna_bgfx_test(cna_test_bgfx_msaa_mip_readback
        ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_msaa_mip_readback_test.cpp)
    cna_register_renderer_test(NAME Bgfx_MsaaMipReadback COMMAND cna_test_bgfx_msaa_mip_readback
        TIMEOUT 1200 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-189 cross-renderer control: the same PUBLIC invalid-mip-level contract on this
    # renderer. The fabrication was Vulkan-only and only Vulkan production changed; running the
    # identical fixture here is what makes that claim falsifiable rather than asserted.
    cna_bgfx_test(cna_test_bgfx_invalid_mip_level
        ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_invalid_mip_level_test.cpp)
    cna_register_renderer_test(NAME Bgfx_InvalidMipLevel COMMAND cna_test_bgfx_invalid_mip_level
        TIMEOUT 1200 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-165 cross-renderer control: Bgfx already honoured the authoritative-dimension
    # GetBackBufferData contract; this run establishes that the shared fix left it byte-unchanged.
    cna_bgfx_test(cna_test_bgfx_backbuffer_readback_dimension
        ${CNA_GRAPHICS_EXAMPLES_DIR}/backbuffer_readback_dimension_test.cpp)
    cna_register_renderer_test(NAME Bgfx_BackbufferReadbackDimension COMMAND cna_test_bgfx_backbuffer_readback_dimension
        TIMEOUT 300 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-161: cross-renderer control for the first-read completion contract. Bgfx's sub-rectangle
    # backbuffer read is a pre-existing gap (declared as a boundary by the test), not this task's subject.
    cna_bgfx_test(cna_test_bgfx_backbuffer_first_read
        ${CNA_GRAPHICS_EXAMPLES_DIR}/backbuffer_first_read_test.cpp)
    cna_register_renderer_test(NAME Bgfx_BackbufferFirstRead COMMAND cna_test_bgfx_backbuffer_first_read
        TIMEOUT 300 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-162 cross-renderer control: a rasterizing renderer's GetBackBufferData SUCCEEDS and
    # fully writes the full-surface range (bgfx sub-rectangle read is a declared boundary), so the
    # Headless rejection is specific, not universal.
    cna_bgfx_test(cna_test_bgfx_backbuffer_reject
        ${CNA_GRAPHICS_EXAMPLES_DIR}/backbuffer_headless_reject_test.cpp)
    cna_register_renderer_test(NAME Bgfx_BackbufferReject COMMAND cna_test_bgfx_backbuffer_reject
        TIMEOUT 300 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-155, the home renderer of the finding: the same contract for the one destination the
    # fixture above could only pin -- the BACKBUFFER. bgfx radix-sorts a frame's draws by their
    # view's sort position, which defaults to the numeric view id, and this renderer's partition puts
    # the backbuffer at id 0 below every render target -- so a backbuffer consumer executed BEFORE
    # its same-frame producer and sampled an empty image (0/32, all texels (0,0,0,0)). Fixed by
    # recording the frame's views in public first-use order and programming that through
    # bgfx::setViewOrder. The MSAA run is separate because a target's applied sample count follows
    # the device's own here.
    cna_bgfx_test(cna_test_bgfx_rt_backbuffer_consumer
        ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_backbuffer_consumer_test.cpp)
    cna_register_renderer_test(NAME Bgfx_RenderTarget_BackbufferConsumer COMMAND cna_test_bgfx_rt_backbuffer_consumer
        TIMEOUT 180 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
    cna_register_renderer_test(NAME Bgfx_RenderTarget_BackbufferConsumer_Msaa
        COMMAND cna_test_bgfx_rt_backbuffer_consumer --msaa
        TIMEOUT 180 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-158, the home renderer of the finding: a RenderTarget2D constructed during a public
    # frame must be usable in that same frame with no warm-up. `bgfx::reset()` -- which this renderer
    # calls the moment the SDL window's size differs from the size bgfx was initialised with -- ends
    # by discarding EVERY view's framebuffer binding, including the one just programmed for the
    # target that is bound right now, and bgfx resolves view state only at frame(). The bind cycle's
    # operations then resolved against the backbuffer and the target was never written: measured
    # (0,0,0,0), not even its own DiscardContents clear. Fixed by mirroring the view->framebuffer
    # bindings this renderer programs and replaying the mirror straight after any reset.
    #
    # The resolution settles exactly once per process, so only a process's FIRST bind cycle can
    # observe it. `--leg=<id>` runs one leg alone, which puts that leg in first position; the four
    # single-leg runs below are the four distinguishable first operations a brand-new RenderTarget2D
    # can have (a draw, a public clear, the bind's own discard clear, and a stock 3D draw), every one
    # of them red pre-fix, plus a RenderTargetCube face -- a second registration path
    # (BindAsRenderTargetFace) through the same reset.
    cna_bgfx_test(cna_test_bgfx_rt_first_use
        ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_first_use_test.cpp)
    cna_register_renderer_test(NAME Bgfx_RenderTarget_FirstUse COMMAND cna_test_bgfx_rt_first_use
        TIMEOUT 180 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
    foreach(_gfx158_leg A1 A3 A4 J N)
        cna_register_renderer_test(NAME Bgfx_RenderTarget_FirstUse_Leg${_gfx158_leg}
            COMMAND cna_test_bgfx_rt_first_use --leg=${_gfx158_leg}
            TIMEOUT 120 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
    endforeach()

    # REMED-GFX-157: a stock 3D draw issued AFTER a SpriteBatch inside ONE render-target bind cycle
    # must execute after it rather than be dropped. REMED-GFX-155's leg I0 measured the region it
    # drew into still holding the cycle's clear colour on BGFX, VULKAN, SOFTWARE and EASYGL, with
    # WEBGPU clean, using ordinary Texture2D sources on both destinations. The oracle is a staircase
    # -- step i covers stripes [0, N-i), so one readback proves every draw happened AND that they
    # happened in the issued order.
    cna_bgfx_test(cna_test_bgfx_spritebatch_3d_order
        ${CNA_GRAPHICS_EXAMPLES_DIR}/spritebatch_3d_order_test.cpp)
    cna_register_renderer_test(NAME Bgfx_SpriteBatch3DOrder COMMAND cna_test_bgfx_spritebatch_3d_order
        TIMEOUT 300 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-160: the XNA/FNA front-face winding contract. FNA's SpriteBatch emits
    # CLOCKWISE-as-displayed triangles and they must survive RasterizerState.CullCounterClockwise --
    # the state SpriteBatch.Begin itself defaults to -- so clockwise-as-displayed is XNA's FRONT face
    # and each enum names the face it CULLS. Two triangles in disjoint quadrants, one wound each way,
    # make every cull mode's expected answer complementary in one readback.
    cna_bgfx_test(cna_test_bgfx_frontface_winding
        ${CNA_GRAPHICS_EXAMPLES_DIR}/frontface_winding_test.cpp)
    cna_register_renderer_test(NAME Bgfx_FrontFaceWinding COMMAND cna_test_bgfx_frontface_winding
        TIMEOUT 300 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-183 cross-renderer control: the complete TriangleStrip parity/culling matrix.
    cna_bgfx_test(cna_test_bgfx_triangle_strip_winding
        ${CNA_GRAPHICS_EXAMPLES_DIR}/triangle_strip_winding_test.cpp)
    cna_register_renderer_test(NAME Bgfx_TriangleStripWinding
        COMMAND cna_test_bgfx_triangle_strip_winding
        TIMEOUT 300 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_bgfx_test(cna_test_bgfx_texture2d_getdata_contract
                  ${CNA_GRAPHICS_EXAMPLES_DIR}/texture2d_getdata_contract_test.cpp)
    cna_register_renderer_test(NAME Bgfx_Texture2D_GetDataContract COMMAND cna_test_bgfx_texture2d_getdata_contract
        TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-149 (and REMED-GFX-128, the same expression): the exact XNA-compatible
    # startIndex/elementCount contract of every public Texture2D::GetData overload. startIndex is a
    # DESTINATION element offset and elementCount is the destination capacity available from it;
    # the whole-level overload applied startIndex to the SOURCE, rejected any non-zero offset on a
    # render target, rejected legal excess capacity, and silently returned a partial frame for an
    # undersized one.
    cna_bgfx_test(cna_test_bgfx_texture2d_getdata_transfer_range
                  ${CNA_GRAPHICS_EXAMPLES_DIR}/texture2d_getdata_transfer_range_test.cpp)
    cna_register_renderer_test(NAME Bgfx_Texture2D_GetDataTransferRange COMMAND cna_test_bgfx_texture2d_getdata_transfer_range
        TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-130: the TextureCube/Texture3D half of REMED-GFX-127's finding. Both public cube and
    # volume GetData paths converted their own zero-initialized scratch buffer for the caller
    # regardless of whether the renderer read anything back, so an unimplemented readback returned a
    # complete, uniformly transparent-black face/volume instead of rejecting the request.
    cna_bgfx_test(cna_test_bgfx_cube_volume_getdata_contract
                  ${CNA_GRAPHICS_EXAMPLES_DIR}/texturecube_texture3d_getdata_contract_test.cpp)
    cna_register_renderer_test(NAME Bgfx_CubeVolume_GetDataContract COMMAND cna_test_bgfx_cube_volume_getdata_contract
        TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-134: `RenderTargetCube::GetData` must return the face that was really rendered or
    # reject the request deterministically. REMED-GFX-130 made the reporting honest but left the
    # readback itself implemented on only two renderers, so a rendered cube face had no byte-exact
    # public oracle anywhere else. Drawn geometry (never Clear -- see REMED-GFX-129) paints an
    # asymmetric five-region pattern whose colours rotate per face, so a flip, a rotation, a wrong
    # array layer/subresource, a stale face or an unresolved multisample surface all fail.
    cna_bgfx_test(cna_test_bgfx_rendertargetcube_getdata_contract
                  ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertargetcube_getdata_contract_test.cpp)
    cna_register_renderer_test(NAME Bgfx_RenderTargetCube_GetDataContract COMMAND cna_test_bgfx_rendertargetcube_getdata_contract
        TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-136: IGraphicsRenderer::CreateRenderTargetCube had no `preserveContents` parameter,
    # unlike CreateRenderTarget2D, so a RenderTargetCube's real RenderTargetUsage never reached the
    # renderer and Vulkan/WebGPU discarded a PreserveContents cube face on every bind cycle. Reuses
    # REMED-GFX-134's asymmetric face producer, then rebinds and draws only a small marker: "the
    # marker landed" is what a discarding renderer also achieves, so it can never pass for
    # preservation.
    cna_bgfx_test(cna_test_bgfx_rendertargetcube_usage
                  ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertargetcube_usage_test.cpp)
    cna_register_renderer_test(NAME Bgfx_RenderTargetCube_Usage COMMAND cna_test_bgfx_rendertargetcube_usage
        TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-141: a MULTISAMPLED RenderTargetCube shared ONE multisample colour attachment
    # across all six faces on EasyGL, Vulkan and SdlGpu, so a PreserveContents face reloaded whichever
    # face was rendered last instead of its own samples. Renders face A, then face B, then rebinds A for
    # a small partial update -- with no readback, Present or flush in between -- and reads the whole of
    # A: the resolved single-sample layer still held the correct A, which is why a full redraw and an
    # immediate read both passed before this.
    cna_bgfx_test(cna_test_bgfx_rendertargetcube_msaa_face
                  ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertargetcube_msaa_face_test.cpp)
    cna_register_renderer_test(NAME Bgfx_RenderTargetCube_MsaaFace COMMAND cna_test_bgfx_rendertargetcube_msaa_face
        TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-195: the GFX-141 oracle is also the focused closure fixture for Bgfx's remaining
    # per-face multisample-colour alias. It directly requires A -> B -> partial-A preservation,
    # all six faces, depth-backed configurations, repeated cycles, disposal and public GetData.
    cna_register_renderer_test(NAME Bgfx_GFX195_RenderTargetCube_Preserve
        COMMAND cna_test_bgfx_rendertargetcube_msaa_face
        TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-142: RenderTargetUsage's DEPTH and STENCIL half. FNA3D's own header documents
    # `preserveTargetContents` as storing the "color/depth/stencil" contents, and FNA's DiscardContents
    # bind clears all three (Target|DepthBuffer|Stencil, MaxDepth, 0) -- so the enum governs depth and
    # stencil, not colour alone. Renders an occluder, unbinds, rebinds WITHOUT clearing and draws behind
    # it: three bands separate preserved depth from cleared depth, lost colour and a second pass that
    # never drew. A parallel stencil stamp/gate sequence does the same for stencil.
    cna_bgfx_test(cna_test_bgfx_rendertarget_depthstencil_usage
                  ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_depthstencil_usage_test.cpp)
    cna_register_renderer_test(NAME Bgfx_RenderTarget_DepthStencilUsage COMMAND cna_test_bgfx_rendertarget_depthstencil_usage
        TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-140: every public render-target bind/unbind cycle must be its own logical pass.
    # `VulkanRenderer::RecordCommandBuffer` collected ONE render pass per unique render-target
    # source per flush and replayed every queued batch for it inside that pass, so two bind cycles of
    # one target within a single flush window shared one load action. The decisive checks all use
    # DiscardContents -- collapsing is invisible on a preserving target, which is why REMED-GFX-136
    # needed an artificial readback barrier to see it -- and nothing between two cycles ever forces a
    # flush.
    cna_bgfx_test(cna_test_bgfx_rendertarget_pass_boundary
                  ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_pass_boundary_test.cpp)
    cna_register_renderer_test(NAME Bgfx_RenderTarget_PassBoundary COMMAND cna_test_bgfx_rendertarget_pass_boundary
        TIMEOUT 90 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-129: cross-renderer control for Vulkan's ordered-Clear correction.
    cna_bgfx_test(cna_test_bgfx_ordered_clear
                  ${CNA_GRAPHICS_EXAMPLES_DIR}/graphicsdevice_ordered_clear_test.cpp)
    cna_register_renderer_test(NAME Bgfx_GraphicsDevice_OrderedClear COMMAND cna_test_bgfx_ordered_clear
        TIMEOUT 120 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-143: backbuffer work and render-target work must replay in ONE ordered stream.
    # REMED-GFX-140 (Vulkan) and REMED-GFX-145 (SdlGpu) gave every render-target bind cycle its own
    # native pass in public order, but both kept the BACKBUFFER as one trailing pass, so every
    # backbuffer draw of a frame was replayed after every target pass regardless of when it was
    # issued. A frame that samples a target onto the backbuffer and then renders into that target
    # again therefore saw the FINAL content. Every check queues its whole public sequence with no
    # Present, GetData, flush or extra frame between the cycles, and only then reads once.
    cna_bgfx_test(cna_test_bgfx_backbuffer_pass_order
                  ${CNA_GRAPHICS_EXAMPLES_DIR}/backbuffer_pass_order_test.cpp)
    cna_register_renderer_test(NAME Bgfx_Backbuffer_PassOrder COMMAND cna_test_bgfx_backbuffer_pass_order
        TIMEOUT 120 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-116 cross-renderer control: every deferred draw must execute under the
    # GraphicsDevice.Viewport active at its own public call. WebGPU resolved it live when it
    # recorded the pass; this file is the same public fixture, so a renderer that regresses the
    # contract is caught here rather than assumed correct.
    cna_bgfx_test(cna_test_bgfx_deferred_viewport
                    ${CNA_GRAPHICS_EXAMPLES_DIR}/deferred_viewport_capture_test.cpp)
    cna_register_renderer_test(NAME Bgfx_Deferred_Viewport COMMAND cna_test_bgfx_deferred_viewport
        TIMEOUT 120 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-146 cross-renderer control: every deferred draw must execute under the
    # GraphicsDevice.ScissorRectangle and RasterizerState.ScissorTestEnable active at its own
    # public call. WebGPU resolved both live when it recorded the pass; this file is the same
    # public fixture, so a renderer that regresses the contract is caught here rather than
    # assumed correct.
    cna_bgfx_test(cna_test_bgfx_deferred_scissor
                    ${CNA_GRAPHICS_EXAMPLES_DIR}/deferred_scissor_capture_test.cpp)
    cna_register_renderer_test(NAME Bgfx_Deferred_Scissor COMMAND cna_test_bgfx_deferred_scissor
        TIMEOUT 120 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-135: the WRITE half of the same finding. `TextureCube::SetData`/`Texture3D::SetData`
    # kept the pre-REMED-GFX-127 shape -- a `void` renderer method behind `if (renderer_)` -- so an
    # upload that stored nothing, or only part of the requested region, still returned normally.
    cna_bgfx_test(cna_test_bgfx_cube_volume_setdata_contract
                  ${CNA_GRAPHICS_EXAMPLES_DIR}/texturecube_texture3d_setdata_contract_test.cpp)
    cna_register_renderer_test(NAME Bgfx_CubeVolume_SetDataContract COMMAND cna_test_bgfx_cube_volume_setdata_contract
        TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

endif()

# Module-local tests get the same headless-safe skip convention the root one-shot
# gives root-registered tests (see cmake/TestHelpers.cmake).
cna_apply_skip_convention()
