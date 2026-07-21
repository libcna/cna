if(CNA_BUILD_TESTS AND NOT EMSCRIPTEN AND NOT WIN32
   AND CNA_GRAPHICS_BACKEND STREQUAL "BGFX")

    enable_testing()

    macro(cna_bgfx_test target src)
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

    # Task 13.6: investigated adding avatar-rendering smoke tests here too (mirroring the Vulkan
    # registration below). Confirmed a real, concrete blocker independent of readback reliability:
    # BgfxGraphicsBackend::SetDepthTestEnabled/SetBlendEnabled/SetDepthWriteEnabled are all
    # unconditionally unimplemented stubs (ThrowNo3DState()) - since the avatar test harnesses
    # call SetDepthTestEnabled/setBlendStateProperty to set up their scene before drawing (not
    # AvatarRenderer's own code, which never touches these), all 3 avatar tests throw immediately
    # on Bgfx today, before ever reaching DrawRealEXT. This is a pre-existing, deep gap in the
    # Bgfx backend's 3D device-state wiring, not a tooling/build limitation - genuinely fixing it
    # (implementing depth-test/blend state via bgfx::setState flags) is a full backend feature,
    # well beyond this test-coverage task's scope. Deferred; not attempted here.

    # Task 89: 3-frame smoke test for the Bgfx backend
    cna_register_backend_test(NAME Bgfx_Demo2D_SmokeTest COMMAND cna_demo_2d --smoke 3
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}" LABELS "GraphicsSmoke")

    # Task 817 (Phase 72 Bgfx gap closure): Texture2D SetData/GetData partial-rectangle +
    # startIndex/elementCount on Bgfx -- verbatim reuse of the shared EasyGL source (Tasks
    # 169-170), same as several Vulkan registrations above. Texture2D::SetData/GetData operate
    # entirely on a backend-agnostic CPU-side cpuPixels_ shadow buffer (Texture2D.cpp) -- confirmed
    # via source that ITextureBackend doesn't even declare a GetData() method (only
    # UpdatePixels/UpdatePixelsLevel, one-way CPU-to-GPU), so this test exercises zero
    # Bgfx-specific code and needs no restructuring, unlike the GetBackBufferData-based tests above.
    cna_bgfx_test(cna_test_bgfx_texture2d_partial_rect
                  examples/easygl_texture2d_partial_rect_test.cpp)
    cna_register_backend_test(NAME Bgfx_Texture2D_PartialRect_RoundTrip COMMAND cna_test_bgfx_texture2d_partial_rect
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 819 (Phase 72 Bgfx gap closure): Texture3D partial-box GetData bounds/offset
    # verification on Bgfx -- verbatim reuse of the shared EasyGL source (Task 274). Unlike
    # Texture2D, Texture3D::GetData genuinely delegates to the per-backend
    # ITexture3DBackend::GetData -- BgfxTexture3DBackend already has a real GPU readback
    # implementation (Task 914: blit into a temporary BGFX_TEXTURE_BLIT_DST|READ_BACK texture,
    # then bgfx::readTexture()), so this is a real per-backend verification, not a no-op reuse of
    # shared CPU-only code like Task 817 above. No GetBackBufferData/rendered-frame involved at
    # all (direct texture-to-CPU readback), so no Bgfx-specific restructuring is needed either.
    cna_bgfx_test(cna_test_bgfx_texture3d_partial_box_readback
                  examples/easygl_texture3d_partial_box_readback_test.cpp)
    cna_register_backend_test(NAME Bgfx_Texture3D_PartialBox_Readback COMMAND cna_test_bgfx_texture3d_partial_box_readback
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 821 (Phase 72 Bgfx gap closure): NPOT texture upload + GPU sampling on Bgfx -- a
    # Bgfx-specific adaptation of the EasyGL source (Task 268; that file uses
    # SetDepthTestEnabled(false), which throws on Bgfx, and reads back 5 rows from one rendered
    # frame, incompatible with Bgfx's GetBackBufferData quirk, Task 406 -- see the test's own
    # file-header comment). Closes the entire Phase 72 Texture2D/3D/Cube depth row group (817-821).
    cna_bgfx_test(cna_test_bgfx_npot_texture
                  examples/bgfx_npot_texture_test.cpp)
    cna_register_backend_test(NAME Bgfx_NpotTexture COMMAND cna_test_bgfx_npot_texture
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 744 (Phase 72 Bgfx gap closure): TextureAddressMode::Clamp -- already covered by
    # Task 750's own Bgfx_TextureAddressMode test (PointWrap-vs-PointClamp comparison, both
    # asserted); no new test needed, see plan_graphics.md Task 744 for the closure note.

    # Task 746 (Phase 72 Bgfx gap closure): TextureAddressMode::Mirror on Bgfx -- a Bgfx-specific
    # adaptation of the EasyGL source (Task 737; that file uses SetDepthTestEnabled(false), which
    # throws on Bgfx, and takes 3 samples within one Draw() tick, incompatible with Bgfx's
    # GetBackBufferData quirk, Task 406 -- see the test's own file-header comment).
    cna_bgfx_test(cna_test_bgfx_texture_address_mode_mirror
                  examples/bgfx_texture_address_mode_mirror_test.cpp)
    cna_register_backend_test(NAME Bgfx_TextureAddressMode_Mirror COMMAND cna_test_bgfx_texture_address_mode_mirror
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 747 (Phase 72 Bgfx gap closure): TextureFilter::Point vs Linear (magnification and
    # minification) on Bgfx -- a Bgfx-specific adaptation of the EasyGL source (Task 297; that
    # file uses SetDepthTestEnabled(false), which throws on Bgfx, and draws 4 columns in one
    # rendered frame before 4 separate reads, incompatible with Bgfx's GetBackBufferData quirk,
    # Task 406 -- see the test's own file-header comment).
    cna_bgfx_test(cna_test_bgfx_texture_filter_point_vs_linear
                  examples/bgfx_texture_filter_point_vs_linear_test.cpp)
    cna_register_backend_test(NAME Bgfx_TextureFilter_PointVsLinear COMMAND cna_test_bgfx_texture_filter_point_vs_linear
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 749 (Phase 72 Bgfx gap closure): anisotropic filtering cap query + fallback on Bgfx --
    # a Bgfx-specific adaptation of the EasyGL source (Task 299; that file uses
    # SetDepthTestEnabled(false), which throws on Bgfx -- see the test's own file-header comment).
    cna_bgfx_test(cna_test_bgfx_texture_anisotropic_effect
                  examples/bgfx_texture_anisotropic_effect_test.cpp)
    cna_register_backend_test(NAME Bgfx_TextureAnisotropicEffect COMMAND cna_test_bgfx_texture_anisotropic_effect
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 743 (Phase 72 Bgfx gap closure): audit SamplerState -> BGFX_SAMPLER_* flag mapping
    # completeness -- found and fixed a real, previously-undiscovered bug: BgfxGraphicsBackend::
    # ApplySamplerState's filter switch only handled Point/Anisotropic, silently collapsing all 6
    # split Min/Mag/Mip TextureFilter values (LinearMipPoint, PointMipLinear, etc.) to plain Linear.
    # Closes the entire Phase 72 SamplerState/TextureAddressMode row group (743-749).
    cna_bgfx_test(cna_test_bgfx_texturefilter_split_minmag
                  examples/bgfx_texturefilter_split_minmag_test.cpp)
    cna_register_backend_test(NAME Bgfx_TextureFilter_SplitMinMag COMMAND cna_test_bgfx_texturefilter_split_minmag
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 179: RenderTargetUsage DiscardContents/PreserveContents smoke test
    cna_bgfx_test(cna_test_bgfx_render_target_usage
                  examples/bgfx_render_target_usage_test.cpp)
    cna_register_backend_test(NAME Bgfx_RenderTargetUsage COMMAND cna_test_bgfx_render_target_usage
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 773 (Phase 72 Bgfx gap closure): verify SetRenderTarget(nullptr) genuinely restores the
    # real backbuffer with real pixel verification, superseding Task 179's own stale "pixel-level
    # verification is not available" comment (predates Task 406's readback convention and Task
    # 901's EnsureViewState() fix) -- see the test's own file-header comment.
    cna_bgfx_test(cna_test_bgfx_setrendertarget_null_restore
                  examples/bgfx_setrendertarget_null_restore_test.cpp)
    cna_register_backend_test(NAME Bgfx_SetRenderTarget_NullRestore COMMAND cna_test_bgfx_setrendertarget_null_restore
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 774 (Phase 72 Bgfx gap closure): verify MRT with mixed formats is rejected or handled
    # per XNA constraints on Bgfx -- Texture::ValidateFormat (shared code, all 4 backends) already
    # throws for every SurfaceFormat except Color, so mixed-format MRT is unreachable, not merely
    # rejected at bind time -- see the test's own file-header comment for the full research finding.
    cna_bgfx_test(cna_test_bgfx_mrt_mixed_formats
                  examples/bgfx_mrt_mixed_formats_test.cpp)
    cna_register_backend_test(NAME Bgfx_MRT_MixedFormats COMMAND cna_test_bgfx_mrt_mixed_formats
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 774 (verbatim reuse): Task 176's own SurfaceFormat-validation contract, confirmed
    # backend-agnostic (pure CPU-side construction, no rendering) -- reused unmodified from EasyGL
    # to directly confirm the same Color-only constraint holds on Bgfx too.
    cna_bgfx_test(cna_test_bgfx_surface_format_throws
                  examples/easygl_surface_format_throws_test.cpp)
    cna_register_backend_test(NAME Bgfx_SurfaceFormat_Throws COMMAND cna_test_bgfx_surface_format_throws
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 757 (Phase 72 Bgfx gap closure): BlendFactor constant-color blending on Bgfx -- a
    # Bgfx-specific adaptation of the EasyGL source (Task 309; that file uses
    # SetDepthTestEnabled(false), which throws on Bgfx -- see the test's own file-header comment).
    cna_bgfx_test(cna_test_bgfx_blendstate_blendfactor
                  examples/bgfx_blendstate_blendfactor_test.cpp)
    cna_register_backend_test(NAME Bgfx_BlendState_BlendFactor COMMAND cna_test_bgfx_blendstate_blendfactor
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 777 (Phase 72 Bgfx gap closure): verify Viewport reset after backbuffer resize on Bgfx
    # -- verbatim reuse of the shared, 100% backend-agnostic source (Task 349, already reused
    # verbatim on Vulkan). Exercises only GraphicsDevice/GraphicsDeviceManager C++ state
    # (UpdateViewportFromWindow()/Present()), no pixel readback, no backend-specific code at all.
    cna_bgfx_test(cna_test_bgfx_viewport_reset_after_resize
                  examples/viewport_reset_after_resize_test.cpp)
    cna_register_backend_test(NAME Bgfx_ViewportResetAfterResize COMMAND cna_test_bgfx_viewport_reset_after_resize
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 249: Bgfx vertex layout mapping tests — all 12 VertexElementFormat values
    cna_bgfx_test(cna_test_bgfx_vertex_format
                  examples/bgfx_vertex_format_test.cpp)
    cna_register_backend_test(NAME Bgfx_VertexFormatMapping COMMAND cna_test_bgfx_vertex_format
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 448: real Bgfx occlusion-query pixel/query test — visible quad (PixelCount > 0) and
    # occluded quad (PixelCount <= 0), proving BgfxOcclusionQueryBackend's Begin()/End() are now
    # actually wired to bgfx's own submit(id, program, occlusionQuery, ...) overload.
    cna_bgfx_test(cna_test_bgfx_occlusion_query
                  examples/bgfx_occlusion_query_test.cpp)
    cna_register_backend_test(NAME Bgfx_OcclusionQuery COMMAND cna_test_bgfx_occlusion_query
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 278: EnvironmentMapEffect smoke test (no GPU readback available on Bgfx)
    cna_bgfx_test(cna_test_bgfx_env_map
                  examples/bgfx_env_map_test.cpp)
    cna_register_backend_test(NAME Bgfx_EnvironmentMapEffect_Smoke COMMAND cna_test_bgfx_env_map
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 393: EnvironmentMapEffect EnvironmentMapAmount=0 should ignore the cube map
    cna_bgfx_test(cna_test_bgfx_environmentmapeffect_amount_zero
                  examples/bgfx_environmentmapeffect_amount_zero_test.cpp)
    cna_register_backend_test(NAME Bgfx_EnvironmentMapEffect_AmountZero COMMAND cna_test_bgfx_environmentmapeffect_amount_zero
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 890: EnvironmentMapEffect DirectionalLight1/DirectionalLight2 forwarding
    cna_bgfx_test(cna_test_bgfx_environmentmapeffect_multilight
                  examples/bgfx_environmentmapeffect_multilight_test.cpp)
    cna_register_backend_test(NAME Bgfx_EnvironmentMapEffect_MultiLight COMMAND cna_test_bgfx_environmentmapeffect_multilight
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 394: EnvironmentMapEffect EnvironmentMapAmount=1 should fully replace lit color
    cna_bgfx_test(cna_test_bgfx_environmentmapeffect_amount_one
                  examples/bgfx_environmentmapeffect_amount_one_test.cpp)
    cna_register_backend_test(NAME Bgfx_EnvironmentMapEffect_AmountOne COMMAND cna_test_bgfx_environmentmapeffect_amount_one
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 395: EnvironmentMapEffect EnvironmentMapSpecular should scale by cubemap alpha
    cna_bgfx_test(cna_test_bgfx_environmentmapeffect_specular
                  examples/bgfx_environmentmapeffect_specular_test.cpp)
    cna_register_backend_test(NAME Bgfx_EnvironmentMapEffect_Specular COMMAND cna_test_bgfx_environmentmapeffect_specular
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 891: EnvironmentMapEffect's base cube-map lerp target must be scaled by the combined
    # texture x diffuse alpha -- verbatim reuse of the shared backend-agnostic source, registered
    # on all 3 backends.
    cna_bgfx_test(cna_test_bgfx_environmentmapeffect_alphascaledlerp
                  examples/environmentmapeffect_alphascaledlerp_test.cpp)
    cna_register_backend_test(NAME Bgfx_EnvironmentMapEffect_AlphaScaledLerp COMMAND cna_test_bgfx_environmentmapeffect_alphascaledlerp
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 396: EnvironmentMapEffect Fresnel edge-weighting should suppress head-on reflections
    cna_bgfx_test(cna_test_bgfx_environmentmapeffect_fresnel
                  examples/bgfx_environmentmapeffect_fresnel_test.cpp)
    cna_register_backend_test(NAME Bgfx_EnvironmentMapEffect_Fresnel COMMAND cna_test_bgfx_environmentmapeffect_fresnel
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 397: EnvironmentMapEffect reflection vector should respond to EyePosition
    cna_bgfx_test(cna_test_bgfx_environmentmapeffect_eyeposition
                  examples/bgfx_environmentmapeffect_eyeposition_test.cpp)
    cna_register_backend_test(NAME Bgfx_EnvironmentMapEffect_EyePosition COMMAND cna_test_bgfx_environmentmapeffect_eyeposition
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 398: EnvironmentMapEffect normal transform should use inverse-transpose of World
    cna_bgfx_test(cna_test_bgfx_environmentmapeffect_worldtransform
                  examples/bgfx_environmentmapeffect_worldtransform_test.cpp)
    cna_register_backend_test(NAME Bgfx_EnvironmentMapEffect_WorldTransform COMMAND cna_test_bgfx_environmentmapeffect_worldtransform
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 399: cross-backend EnvironmentMapEffect capstone (Tasks 393-398 combined)
    cna_bgfx_test(cna_test_bgfx_environmentmapeffect_combined
                  examples/bgfx_environmentmapeffect_combined_test.cpp)
    cna_register_backend_test(NAME Bgfx_EnvironmentMapEffect_Combined COMMAND cna_test_bgfx_environmentmapeffect_combined
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 406: SkinnedEffect identity bone palette should produce zero deformation
    cna_bgfx_test(cna_test_bgfx_skinnedeffect_identity_bones
                  examples/bgfx_skinnedeffect_identity_bones_test.cpp)
    cna_register_backend_test(NAME Bgfx_SkinnedEffect_IdentityBones COMMAND cna_test_bgfx_skinnedeffect_identity_bones
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-006: SkinnedEffect world-space normal transform (inverse-transpose after the bone
    # skin) on Bgfx -- the non-identity-World harness the existing identity-World skinned tests
    # structurally cannot cover. Fails pre-fix (Variant A/B), passes post-fix. Covers both the
    # per-pixel (vs_skinned3d) and per-vertex (vs_skinned3d_vertexlit) skinned programs.
    cna_bgfx_test(cna_test_bgfx_skinnedeffect_world_normal
                  examples/bgfx_skinnedeffect_world_normal_test.cpp)
    cna_register_backend_test(NAME Bgfx_SkinnedEffect_WorldNormal COMMAND cna_test_bgfx_skinnedeffect_world_normal
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 407: SkinnedEffect single translation bone should shift the mesh
    cna_bgfx_test(cna_test_bgfx_skinnedeffect_translation_bone
                  examples/bgfx_skinnedeffect_translation_bone_test.cpp)
    cna_register_backend_test(NAME Bgfx_SkinnedEffect_TranslationBone COMMAND cna_test_bgfx_skinnedeffect_translation_bone
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 408: SkinnedEffect two-bone weighted blend should produce midpoint deformation
    cna_bgfx_test(cna_test_bgfx_skinnedeffect_twobone_blend
                  examples/bgfx_skinnedeffect_twobone_blend_test.cpp)
    cna_register_backend_test(NAME Bgfx_SkinnedEffect_TwoBoneBlend COMMAND cna_test_bgfx_skinnedeffect_twobone_blend
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 409: cross-backend SkinnedEffect capstone (identity + single-bone + two-bone blend)
    cna_bgfx_test(cna_test_bgfx_skinnedeffect_combined
                  examples/bgfx_skinnedeffect_combined_test.cpp)
    cna_register_backend_test(NAME Bgfx_SkinnedEffect_Combined COMMAND cna_test_bgfx_skinnedeffect_combined
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 333: RenderTarget2D sampled as Texture2D via SpriteBatch after unbinding (Bgfx)
    cna_bgfx_test(cna_test_bgfx_render_target_sample
                  examples/bgfx_render_target_sample_test.cpp)
    cna_register_backend_test(NAME Bgfx_RenderTarget2D_SampleAfterUnbind COMMAND cna_test_bgfx_render_target_sample
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 335/912: RenderTarget2D depth buffer is real and functional (Bgfx) -- shared with
    # EasyGL/Vulkan's registrations of the same file; needed the retry-until-non-black convention
    # added to the shared test first (see that file's header comment).
    cna_bgfx_test(cna_test_bgfx_rendertarget2d_depth
                  examples/rendertarget2d_depth_test.cpp)
    cna_register_backend_test(NAME Bgfx_RenderTarget2D_DepthBuffer COMMAND cna_test_bgfx_rendertarget2d_depth
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 334: RenderTargetCube sampled as TextureCube via EnvironmentMapEffect after unbinding (Bgfx)
    cna_bgfx_test(cna_test_bgfx_render_target_cube_sample
                  examples/bgfx_render_target_cube_sample_test.cpp)
    cna_register_backend_test(NAME Bgfx_RenderTargetCube_SampleAfterUnbind COMMAND cna_test_bgfx_render_target_cube_sample
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 364: BasicEffect pixel test — VertexColorEnabled=false, no texture, diffuse color only
    cna_bgfx_test(cna_test_bgfx_basiceffect_vertexcolor_disabled
                  examples/bgfx_basiceffect_vertexcolor_disabled_test.cpp)
    cna_register_backend_test(NAME Bgfx_BasicEffect_VertexColorDisabled COMMAND cna_test_bgfx_basiceffect_vertexcolor_disabled
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 365: BasicEffect pixel test — VertexColorEnabled=true, no texture, vertex color multiplication
    cna_bgfx_test(cna_test_bgfx_basiceffect_vertexcolor_enabled
                  examples/bgfx_basiceffect_vertexcolor_enabled_test.cpp)
    cna_register_backend_test(NAME Bgfx_BasicEffect_VertexColorEnabled COMMAND cna_test_bgfx_basiceffect_vertexcolor_enabled
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 366: BasicEffect pixel test — TextureEnabled=true, no vertex color
    cna_bgfx_test(cna_test_bgfx_basiceffect_texture_enabled
                  examples/bgfx_basiceffect_texture_enabled_test.cpp)
    cna_register_backend_test(NAME Bgfx_BasicEffect_TextureEnabled COMMAND cna_test_bgfx_basiceffect_texture_enabled
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 367: BasicEffect pixel test — TextureEnabled=true AND VertexColorEnabled=true
    cna_bgfx_test(cna_test_bgfx_basiceffect_texture_vertexcolor_enabled
                  examples/bgfx_basiceffect_texture_vertexcolor_enabled_test.cpp)
    cna_register_backend_test(NAME Bgfx_BasicEffect_TextureVertexColorEnabled COMMAND cna_test_bgfx_basiceffect_texture_vertexcolor_enabled
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 368: BasicEffect pixel test — LightingEnabled=true, one DirectionalLight
    cna_bgfx_test(cna_test_bgfx_basiceffect_one_light
                  examples/bgfx_basiceffect_one_light_test.cpp)
    cna_register_backend_test(NAME Bgfx_BasicEffect_OneLight COMMAND cna_test_bgfx_basiceffect_one_light
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 885: BasicEffect pixel test — DirectionalLight1/2 + EmissiveColor on the lit path
    cna_bgfx_test(cna_test_bgfx_basiceffect_multilight_emissive
                  examples/bgfx_basiceffect_multilight_emissive_test.cpp)
    cna_register_backend_test(NAME Bgfx_BasicEffect_MultiLightEmissive COMMAND cna_test_bgfx_basiceffect_multilight_emissive
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 886: BasicEffect pixel test — real specular highlights
    cna_bgfx_test(cna_test_bgfx_basiceffect_specular
                  examples/bgfx_basiceffect_specular_test.cpp)
    cna_register_backend_test(NAME Bgfx_BasicEffect_Specular COMMAND cna_test_bgfx_basiceffect_specular
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 1104: BasicEffect PreferPerPixelLighting real dispatch selector
    cna_bgfx_test(cna_test_bgfx_basiceffect_preferperpixellighting
                  examples/bgfx_basiceffect_preferperpixellighting_test.cpp)
    cna_register_backend_test(NAME Bgfx_BasicEffect_PreferPerPixelLighting COMMAND cna_test_bgfx_basiceffect_preferperpixellighting
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 887: AlphaTestEffect.VertexColorEnabled fix verification
    cna_bgfx_test(cna_test_bgfx_alphatest_vertexcolor
                  examples/bgfx_alphatest_vertexcolor_test.cpp)
    cna_register_backend_test(NAME Bgfx_AlphaTest_VertexColor COMMAND cna_test_bgfx_alphatest_vertexcolor
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 888: BasicEffect fog on the colored3d (stride-16) pipeline
    cna_bgfx_test(cna_test_bgfx_basiceffect_fog
                  examples/bgfx_basiceffect_fog_test.cpp)
    cna_register_backend_test(NAME Bgfx_BasicEffect_Fog COMMAND cna_test_bgfx_basiceffect_fog
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 888: BasicEffect fog on the lit_textured3d (stride-32) pipeline
    cna_bgfx_test(cna_test_bgfx_basiceffect_lit_fog
                  examples/bgfx_basiceffect_lit_fog_test.cpp)
    cna_register_backend_test(NAME Bgfx_BasicEffect_LitFog COMMAND cna_test_bgfx_basiceffect_lit_fog
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 888: AlphaTestEffect fog on the alpha_test3d pipeline
    cna_bgfx_test(cna_test_bgfx_alphatest_fog
                  examples/bgfx_alphatest_fog_test.cpp)
    cna_register_backend_test(NAME Bgfx_AlphaTest_Fog COMMAND cna_test_bgfx_alphatest_fog
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 888: DualTextureEffect fog on the dual_texture3d pipeline
    cna_bgfx_test(cna_test_bgfx_dualtextureeffect_fog
                  examples/bgfx_dualtextureeffect_fog_test.cpp)
    cna_register_backend_test(NAME Bgfx_DualTextureEffect_Fog COMMAND cna_test_bgfx_dualtextureeffect_fog
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 892: BasicEffect lit-textured normal transform under a real camera (Bgfx)
    cna_bgfx_test(cna_test_bgfx_basiceffect_normaltransform
                  examples/bgfx_basiceffect_normaltransform_test.cpp)
    cna_register_backend_test(NAME Bgfx_BasicEffect_NormalTransform COMMAND cna_test_bgfx_basiceffect_normaltransform
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 369: BasicEffect pixel test — DiffuseColor+EmissiveColor, LightingEnabled=false
    cna_bgfx_test(cna_test_bgfx_basiceffect_emissive
                  examples/bgfx_basiceffect_emissive_test.cpp)
    cna_register_backend_test(NAME Bgfx_BasicEffect_Emissive COMMAND cna_test_bgfx_basiceffect_emissive
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 370: BasicEffect cross-backend image comparison suite — closes Phase 42
    cna_bgfx_test(cna_test_bgfx_basiceffect_combined
                  examples/bgfx_basiceffect_combined_test.cpp)
    cna_register_backend_test(NAME Bgfx_BasicEffect_Combined COMMAND cna_test_bgfx_basiceffect_combined
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 375: AlphaTestEffect all CompareFunction modes — threshold sweep
    cna_bgfx_test(cna_test_bgfx_alphatest_comparefunction_sweep
                  examples/bgfx_alphatest_comparefunction_sweep_test.cpp)
    cna_register_backend_test(NAME Bgfx_AlphaTest_CompareFunctionSweep COMMAND cna_test_bgfx_alphatest_comparefunction_sweep
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 379: AlphaTestEffect null/no-texture behavior
    cna_bgfx_test(cna_test_bgfx_alphatest_null_texture
                  examples/bgfx_alphatest_null_texture_test.cpp)
    cna_register_backend_test(NAME Bgfx_AlphaTest_NullTexture COMMAND cna_test_bgfx_alphatest_null_texture
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 383: DualTextureEffect two-texture blend formula, including FNA's color.rgb*=2.
    # Also the first-ever Bgfx pixel-integration test for DualTextureEffect.
    cna_bgfx_test(cna_test_bgfx_dualtextureeffect_doubling
                  examples/bgfx_dualtextureeffect_doubling_test.cpp)
    cna_register_backend_test(NAME Bgfx_DualTextureEffect_Doubling COMMAND cna_test_bgfx_dualtextureeffect_doubling
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 384: DualTextureEffect magenta × yellow = red (mirrors Task 133/135 EasyGL/Vulkan)
    cna_bgfx_test(cna_test_bgfx_dual_texture
                  examples/bgfx_dual_texture_test.cpp)
    cna_register_backend_test(NAME Bgfx_DualTextureEffect_Blend COMMAND cna_test_bgfx_dual_texture
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 385: DualTextureEffect Alpha premultiplication, verified via real AlphaBlend
    cna_bgfx_test(cna_test_bgfx_dualtextureeffect_alpha
                  examples/bgfx_dualtextureeffect_alpha_test.cpp)
    cna_register_backend_test(NAME Bgfx_DualTextureEffect_Alpha COMMAND cna_test_bgfx_dualtextureeffect_alpha
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 386: DualTextureEffect first texture (Texture, slot 0) null behavior
    cna_bgfx_test(cna_test_bgfx_dualtextureeffect_null_texture0
                  examples/bgfx_dualtextureeffect_null_texture0_test.cpp)
    cna_register_backend_test(NAME Bgfx_DualTextureEffect_NullTexture0 COMMAND cna_test_bgfx_dualtextureeffect_null_texture0
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 387: DualTextureEffect second texture (Texture2, slot 1) null behavior
    cna_bgfx_test(cna_test_bgfx_dualtextureeffect_null_texture2
                  examples/bgfx_dualtextureeffect_null_texture2_test.cpp)
    cna_register_backend_test(NAME Bgfx_DualTextureEffect_NullTexture2 COMMAND cna_test_bgfx_dualtextureeffect_null_texture2
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 389: DualTextureEffect cross-backend image comparison suite (closes Phase 44 pixel work)
    cna_bgfx_test(cna_test_bgfx_dualtextureeffect_combined
                  examples/bgfx_dualtextureeffect_combined_test.cpp)
    cna_register_backend_test(NAME Bgfx_DualTextureEffect_Combined COMMAND cna_test_bgfx_dualtextureeffect_combined
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 880: GraphicsDevice.Viewport GPU wiring — sub-region viewport pixel-readback test
    cna_bgfx_test(cna_test_bgfx_viewport_subregion
                  examples/bgfx_viewport_subregion_test.cpp)
    cna_register_backend_test(NAME Bgfx_Viewport_Subregion COMMAND cna_test_bgfx_viewport_subregion
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 750: SpriteBatch Begin()'s SamplerState (TextureAddressMode) must take effect
    cna_bgfx_test(cna_test_bgfx_texture_address_mode
                  examples/bgfx_texture_address_mode_test.cpp)
    cna_register_backend_test(NAME Bgfx_TextureAddressMode COMMAND cna_test_bgfx_texture_address_mode
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 899: EnvironmentMapEffect/SkinnedEffect fog on Bgfx (opportunistic bonus scope)
    cna_bgfx_test(cna_test_bgfx_environmentmapeffect_fog
                  examples/bgfx_environmentmapeffect_fog_test.cpp)
    cna_register_backend_test(NAME Bgfx_EnvironmentMapEffect_Fog COMMAND cna_test_bgfx_environmentmapeffect_fog
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_bgfx_test(cna_test_bgfx_skinnedeffect_fog
                  examples/bgfx_skinnedeffect_fog_test.cpp)
    cna_register_backend_test(NAME Bgfx_SkinnedEffect_Fog COMMAND cna_test_bgfx_skinnedeffect_fog
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 893: SkinnedEffect DirectionalLight1/DirectionalLight2 forwarding
    cna_bgfx_test(cna_test_bgfx_skinnedeffect_multilight
                  examples/bgfx_skinnedeffect_multilight_test.cpp)
    cna_register_backend_test(NAME Bgfx_SkinnedEffect_MultiLight COMMAND cna_test_bgfx_skinnedeffect_multilight
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 894: SkinnedEffect real specular highlights
    cna_bgfx_test(cna_test_bgfx_skinnedeffect_specular
                  examples/bgfx_skinnedeffect_specular_test.cpp)
    cna_register_backend_test(NAME Bgfx_SkinnedEffect_Specular COMMAND cna_test_bgfx_skinnedeffect_specular
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 1104: SkinnedEffect PreferPerPixelLighting real dispatch selector
    cna_bgfx_test(cna_test_bgfx_skinnedeffect_preferperpixellighting
                  examples/bgfx_skinnedeffect_preferperpixellighting_test.cpp)
    cna_register_backend_test(NAME Bgfx_SkinnedEffect_PreferPerPixelLighting COMMAND cna_test_bgfx_skinnedeffect_preferperpixellighting
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 895: SkinnedEffect WeightsPerVertex real GPU enforcement
    cna_bgfx_test(cna_test_bgfx_skinnedeffect_weightspervertex
                  examples/bgfx_skinnedeffect_weightspervertex_test.cpp)
    cna_register_backend_test(NAME Bgfx_SkinnedEffect_WeightsPerVertex COMMAND cna_test_bgfx_skinnedeffect_weightspervertex
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 878/879: RenderTarget2D MSAA resolve genuinely anti-aliases on Bgfx
    # (BGFX_TEXTURE_RT_MSAA_Xn -- also closes Task 873's SpriteBatch RT-sampling cast bug and a
    # second, separate EnsureViewState() RT-viewport-clobbering bug, both hard prerequisites for
    # this test's RT-then-backbuffer-sample methodology to be meaningful at all -- see the test
    # file's own header comment for full detail). CNA_BGFX_RENDERER=VULKAN routes bgfx through its
    # Vulkan backend instead of its default OpenGL one: this dev environment's bgfx OpenGL path
    # negotiates only a legacy GL 2.1 context (confirmed via glxinfo that the underlying driver
    # actually supports OpenGL 4.6) under which MSAA-flagged framebuffer textures do not really
    # resolve with sub-pixel blending, despite bgfx::getCaps() reporting format support -- a
    # bgfx/legacy-GL-context environment limitation, not a defect in this task's (100% backend-
    # agnostic) C++ wiring, confirmed by this exact test passing cleanly under bgfx's Vulkan
    # renderer with zero code changes.
    cna_bgfx_test(cna_test_bgfx_rendertarget2d_msaa
                  examples/bgfx_rendertarget2d_msaa_test.cpp)
    cna_register_backend_test(NAME Bgfx_RenderTarget2D_MsaaResolve COMMAND cna_test_bgfx_rendertarget2d_msaa
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY};CNA_BGFX_RENDERER=VULKAN")

    # Task 906: RenderTarget2D mip chains are genuinely generated (bgfx's own built-in
    # hasMips+BGFX_RESOLVE_AUTO_GEN_MIPS mechanism), not just present-but-undefined storage.
    cna_bgfx_test(cna_test_bgfx_rendertarget2d_mip
                  examples/bgfx_rendertarget2d_mip_test.cpp)
    cna_register_backend_test(NAME Bgfx_RenderTarget2D_MipChain COMMAND cna_test_bgfx_rendertarget2d_mip
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 907: RenderTargetCube mip chains (closes Task 874 as a hard prerequisite --
    # this is Bgfx's first-ever RenderTargetCube-via-EnvironmentMapEffect test).
    cna_bgfx_test(cna_test_bgfx_rendertargetcube_mip
                  examples/bgfx_rendertargetcube_mip_test.cpp)
    cna_register_backend_test(NAME Bgfx_RenderTargetCube_MipChain COMMAND cna_test_bgfx_rendertargetcube_mip
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 903: RenderTargetCube MSAA (property fidelity + no-corruption sanity check -- see this
    # test's own header comment for why a genuine sub-pixel AA differential isn't attempted).
    cna_bgfx_test(cna_test_bgfx_rendertargetcube_msaa
                  examples/bgfx_rendertargetcube_msaa_test.cpp)
    cna_register_backend_test(NAME Bgfx_RenderTargetCube_MsaaResolve COMMAND cna_test_bgfx_rendertargetcube_msaa
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 877: RenderTargetCube honors its exact requested DepthFormat -- Bgfx's first-ever
    # depth-tested RenderTargetCube face (previously no depth attachment support at all).
    cna_bgfx_test(cna_test_bgfx_rendertargetcube_depthformat
                  examples/bgfx_rendertargetcube_depthformat_test.cpp)
    cna_register_backend_test(NAME Bgfx_RenderTargetCube_DepthFormat COMMAND cna_test_bgfx_rendertargetcube_depthformat
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 910: binding + filling more than one RenderTarget2D within a single un-advanced bgfx
    # frame no longer clobbers all but the last one bound -- each render target now gets its own
    # distinct bgfx view id instead of sharing one hardcoded id.
    cna_bgfx_test(cna_test_bgfx_concurrent_rendertargets
                  examples/bgfx_concurrent_rendertargets_test.cpp)
    cna_register_backend_test(NAME Bgfx_ConcurrentRenderTargets COMMAND cna_test_bgfx_concurrent_rendertargets
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 914: Texture3D/TextureCube::GetData is now real on Bgfx (blit into a temporary
    # BGFX_TEXTURE_BLIT_DST|BGFX_TEXTURE_READ_BACK texture + bgfx::readTexture()) -- shared with
    # EasyGL/Vulkan's registration of the same backend-agnostic files (Tasks 173/275/865).
    cna_bgfx_test(cna_test_bgfx_texture3d_slices
                  examples/easygl_texture3d_slices_test.cpp)
    cna_register_backend_test(NAME Bgfx_Texture3D_Slices_RoundTrip COMMAND cna_test_bgfx_texture3d_slices
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_bgfx_test(cna_test_bgfx_texturecube_partial_rect
                  examples/easygl_texturecube_partial_rect_test.cpp)
    cna_register_backend_test(NAME Bgfx_TextureCube_PartialRect_RoundTrip COMMAND cna_test_bgfx_texturecube_partial_rect
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 914: Texture3D/TextureCube mip-level allocation is now real on Bgfx (mipMap genuinely
    # threaded through createTexture3D/createTextureCube's _hasMips, previously hardcoded false) --
    # shared with EasyGL/Vulkan's registration of the same backend-agnostic files (Tasks 862/864).
    cna_bgfx_test(cna_test_bgfx_texture3d_mip
                  examples/easygl_texture3d_mip_test.cpp)
    cna_register_backend_test(NAME Bgfx_Texture3D_Mip_RoundTrip COMMAND cna_test_bgfx_texture3d_mip
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_bgfx_test(cna_test_bgfx_texturecube_mip
                  examples/easygl_texturecube_mip_test.cpp)
    cna_register_backend_test(NAME Bgfx_TextureCube_Mip_RoundTrip COMMAND cna_test_bgfx_texturecube_mip
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 923: BlendState ColorBlendFunction/AlphaBlendFunction independence on Bgfx -- a Bgfx-
    # specific adaptation of the EasyGL source (Task 307; can't reuse it verbatim like Vulkan's
    # Task 868 registration does, since it calls GraphicsDevice.SetDepthTestEnabled(false), an
    # unconditional throw on Bgfx -- see the test's own file-header comment). Discriminates Bgfx's
    # colorBlendFunc-always-Add gap (Check A expects Subtract; failed before this task's fix).
    cna_bgfx_test(cna_test_bgfx_blendstate_separate_functions
                  examples/bgfx_blendstate_separate_functions_test.cpp)
    cna_register_backend_test(NAME Bgfx_BlendState_SeparateFunctions COMMAND cna_test_bgfx_blendstate_separate_functions
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

    # Task 926 (split from Task 867, the last of the 3 backends -- closes the split in full):
    # real Texture2D mip-level upload on Bgfx -- a Bgfx-specific adaptation of the EasyGL test
    # source (Task 298; can't reuse it verbatim, same reasoning as Task 925's Vulkan copy: Bgfx's
    # TextureFilter::Point has always mapped to BGFX_SAMPLER_MIP_POINT, genuinely mip-aware,
    # unlike EasyGL's still-flat mapping -- see the test's own file-header comment).
    cna_bgfx_test(cna_test_bgfx_texture_mip_filter_effect
                  examples/bgfx_texture_mip_filter_effect_test.cpp)
    cna_register_backend_test(NAME Bgfx_TextureMipFilter_DualTextureEffect COMMAND cna_test_bgfx_texture_mip_filter_effect
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 759 (Phase 72 Bgfx gap closure): DepthStencilState.DepthBufferWriteEnable pixel test
    # on Bgfx -- a Bgfx-specific adaptation of the EasyGL source (Task 313; already reused
    # verbatim on Vulkan, but Bgfx's own GetBackBufferData only reliably reflects the first read
    # per rendered frame, Task 406, so the EasyGL source's 2-reads-per-frame structure needed
    # restructuring into 2 separately-read passes -- see the test's own file-header comment).
    cna_bgfx_test(cna_test_bgfx_depthstencilstate_write_enable
                  examples/bgfx_depthstencilstate_write_enable_test.cpp)
    cna_register_backend_test(NAME Bgfx_DepthStencilState_WriteEnable COMMAND cna_test_bgfx_depthstencilstate_write_enable
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 760 (Phase 72 Bgfx gap closure): all 8 depth CompareFunction values on Bgfx -- a
    # Bgfx-specific adaptation of the EasyGL source (Task 314; that file only covers 5 of 8 values
    # and reads back 5 columns in one frame, which Bgfx's own GetBackBufferData quirk, Task 406,
    # doesn't support reliably -- see the test's own file-header comment).
    cna_bgfx_test(cna_test_bgfx_depthstencilstate_compare_function
                  examples/bgfx_depthstencilstate_compare_function_test.cpp)
    cna_register_backend_test(NAME Bgfx_DepthStencilState_CompareFunction COMMAND cna_test_bgfx_depthstencilstate_compare_function
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 761 (Phase 72 Bgfx gap closure): StencilEnable + StencilMask (read mask) +
    # StencilWriteMask on Bgfx -- a Bgfx-specific adaptation of the EasyGL sources (Tasks 315/316;
    # those files sample multiple regions in one frame, incompatible with Bgfx's GetBackBufferData
    # quirk, Task 406 -- see the test's own file-header comment). StencilWriteMask is verified
    # informationally only: bgfx's own state API has no per-draw stencil write-mask flag at all
    # (confirmed against bgfx/defines.h), a permanent backend limitation, not a bug in this codebase.
    cna_bgfx_test(cna_test_bgfx_depthstencilstate_stencil_mask
                  examples/bgfx_depthstencilstate_stencil_mask_test.cpp)
    cna_register_backend_test(NAME Bgfx_DepthStencilState_StencilMask COMMAND cna_test_bgfx_depthstencilstate_stencil_mask
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 762 (Phase 72 Bgfx gap closure): front-facing StencilFail/StencilDepthBufferFail/
    # StencilPass operation slots on Bgfx -- a Bgfx-specific adaptation of the EasyGL source (Task
    # 317; that file reads back 4 columns in one frame, incompatible with Bgfx's GetBackBufferData
    # quirk, Task 406 -- see the test's own file-header comment).
    cna_bgfx_test(cna_test_bgfx_depthstencilstate_stencil_ops
                  examples/bgfx_depthstencilstate_stencil_ops_test.cpp)
    cna_register_backend_test(NAME Bgfx_DepthStencilState_StencilOps COMMAND cna_test_bgfx_depthstencilstate_stencil_ops
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 763 (Phase 72 Bgfx gap closure): TwoSidedStencilMode (separate CCW stencil func/ops for
    # back-facing triangles) on Bgfx -- a Bgfx-specific adaptation of the EasyGL source (Task 318;
    # that file reads back 2 columns in one frame, incompatible with Bgfx's GetBackBufferData
    # quirk, Task 406 -- see the test's own file-header comment).
    cna_bgfx_test(cna_test_bgfx_depthstencilstate_stencil_twosided
                  examples/bgfx_depthstencilstate_stencil_twosided_test.cpp)
    cna_register_backend_test(NAME Bgfx_DepthStencilState_StencilTwoSided COMMAND cna_test_bgfx_depthstencilstate_stencil_twosided
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 764 (Phase 72 Bgfx gap closure): GraphicsDevice.ReferenceStencil standalone override
    # reaching Bgfx draw calls -- a Bgfx-specific adaptation of the EasyGL source (Task 319; that
    # file reads back 2 regions in one frame, incompatible with Bgfx's GetBackBufferData quirk,
    # Task 406 -- see the test's own file-header comment).
    cna_bgfx_test(cna_test_bgfx_graphicsdevice_reference_stencil
                  examples/bgfx_graphicsdevice_reference_stencil_test.cpp)
    cna_register_backend_test(NAME Bgfx_GraphicsDevice_ReferenceStencil COMMAND cna_test_bgfx_graphicsdevice_reference_stencil
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 871: GraphicsDevice::Clear() now actually clears the stencil buffer to the requested
    # value on Bgfx too -- a Bgfx-specific adaptation of the EasyGL source (1 step per real frame,
    # since Bgfx's own per-frame view clear cannot be interleaved mid-frame with draws either).
    cna_bgfx_test(cna_test_bgfx_graphicsdevice_clear_stencil
                  examples/bgfx_graphicsdevice_clear_stencil_test.cpp)
    cna_register_backend_test(NAME Bgfx_GraphicsDevice_ClearStencil COMMAND cna_test_bgfx_graphicsdevice_clear_stencil
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 950: BgfxGraphicsBackend::ClearColorAndDepth/ClearColorDepthAndStencil now actually
    # thread their `depth` parameter into bgfx::setViewClear() instead of hardcoding 1.0f --
    # verbatim reuse of the shared backend-agnostic source (no retry loop needed, same as the
    # Task 871 Bgfx stencil test: each check's Clear()+Draw()+GetBackBufferData() is its own real
    # frame).
    cna_bgfx_test(cna_test_bgfx_graphicsdevice_clear_depth
                  examples/graphicsdevice_clear_depth_test.cpp)
    cna_register_backend_test(NAME Bgfx_GraphicsDevice_ClearDepth COMMAND cna_test_bgfx_graphicsdevice_clear_depth
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 955: a freshly-constructed GraphicsDevice's own default state (no game code ever
    # explicitly setting BlendState/DepthStencilState/RasterizerState, exactly like
    # ../cna-samples SimpleAnimation's Tank.hpp) must produce correct opaque depth occlusion.
    cna_bgfx_test(cna_test_bgfx_graphicsdevice_default_state_occlusion
                  examples/graphicsdevice_default_state_occlusion_test.cpp)
    cna_register_backend_test(NAME Bgfx_GraphicsDevice_DefaultStateOcclusion COMMAND cna_test_bgfx_graphicsdevice_default_state_occlusion
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 889: DualTextureEffect.VertexColorEnabled -- verbatim reuse of the shared
    # backend-agnostic source, registered on all 3 backends.
    cna_bgfx_test(cna_test_bgfx_dualtextureeffect_vertexcolor
                  examples/dualtextureeffect_vertexcolor_test.cpp)
    cna_register_backend_test(NAME Bgfx_DualTextureEffect_VertexColor COMMAND cna_test_bgfx_dualtextureeffect_vertexcolor
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 765 (Phase 72 Bgfx gap closure): CullMode (None/CullClockwiseFace/
    # CullCounterClockwiseFace) on Bgfx -- a Bgfx-specific adaptation of the EasyGL source (Task
    # 323/324/325; that file reads back 2 columns in one frame, incompatible with Bgfx's
    # GetBackBufferData quirk, Task 406 -- see the test's own file-header comment).
    cna_bgfx_test(cna_test_bgfx_rasterizerstate_cullmode
                  examples/bgfx_rasterizerstate_cullmode_test.cpp)
    cna_register_backend_test(NAME Bgfx_RasterizerState_CullMode COMMAND cna_test_bgfx_rasterizerstate_cullmode
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # XNA culling-compatibility audit: real-camera CullMode reproducer, verbatim reuse of the
    # shared backend-agnostic source (first tried on Bgfx as-is; see the audit doc for whether
    # Bgfx's own multiple-reads-per-frame GetBackBufferData limitation, Task 406, required a
    # restructure here).
    cna_bgfx_test(cna_test_bgfx_rasterizerstate_cullmode_camera
                  examples/rasterizerstate_cullmode_camera_test.cpp)
    cna_register_backend_test(NAME Bgfx_RasterizerState_CullMode_Camera COMMAND cna_test_bgfx_rasterizerstate_cullmode_camera
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_bgfx_test(cna_test_bgfx_rasterizerstate_cullmode_indexed_basiceffect
                  examples/rasterizerstate_cullmode_indexed_basiceffect_test.cpp)
    cna_register_backend_test(NAME Bgfx_RasterizerState_CullMode_IndexedBasicEffect COMMAND cna_test_bgfx_rasterizerstate_cullmode_indexed_basiceffect
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 766 (Phase 72 Bgfx gap closure): FillMode::WireFrame on Bgfx -- adapted from
    # vulkan_fill_mode_test.cpp (Task 327; no EasyGL/shared source exists for this property, no
    # dedicated pixel test existed anywhere in this project before this task -- see the test's own
    # file-header comment). Exercises the new ExpandWireframeIndices triangle-to-line-list
    # emulation (bgfx has no native polygon-fill-mode toggle).
    cna_bgfx_test(cna_test_bgfx_rasterizerstate_fillmode_wireframe
                  examples/bgfx_rasterizerstate_fillmode_wireframe_test.cpp)
    cna_register_backend_test(NAME Bgfx_RasterizerState_FillMode_WireFrame COMMAND cna_test_bgfx_rasterizerstate_fillmode_wireframe
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 767 (Phase 72 Bgfx gap closure, project-owner decision 2026-07-10): RasterizerState.
    # DepthBias on Bgfx -- bgfx has no native polygon-offset mechanism, emulated via a per-draw
    # vertex-shader Z-offset (see BgfxGraphicsBackend::SetDepthBiasUniform, u_depthBias in every
    # 3D vertex shader). SlopeScaleDepthBias deliberately NOT emulated (documented gap, checked
    # informationally only -- see the test's own file-header comment).
    cna_bgfx_test(cna_test_bgfx_rasterizerstate_depthbias
                  examples/bgfx_rasterizerstate_depthbias_test.cpp)
    cna_register_backend_test(NAME Bgfx_RasterizerState_DepthBias COMMAND cna_test_bgfx_rasterizerstate_depthbias
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 768 (Phase 72 Bgfx gap closure): ScissorTestEnable/ScissorRectangle interaction on Bgfx
    # -- a Bgfx-specific adaptation of the EasyGL source (Task 209; that file reads 2 columns per
    # frame across 3 draws, incompatible with Bgfx's GetBackBufferData quirk, Task 406 -- see the
    # test's own file-header comment). Found and fixed a genuine bug: none of Bgfx's 4 3D
    # draw-dispatch functions ever called bgfx::setScissor (only the 2D SpriteBatch path did).
    cna_bgfx_test(cna_test_bgfx_scissor
                  examples/bgfx_scissor_test.cpp)
    cna_register_backend_test(NAME Bgfx_Scissor COMMAND cna_test_bgfx_scissor
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Tasks 752-755 (Phase 72 Bgfx gap closure): BlendState::Opaque/AlphaBlend/NonPremultiplied/
    # Additive on Bgfx -- each is a Bgfx-specific copy of the backend-agnostic EasyGL source (also
    # reused verbatim on Vulkan above), differing only in replacing the legacy
    # GraphicsDevice::SetDepthTestEnabled(false) call (throws on Bgfx -- a pre-existing, deliberate
    # stub) with the equivalent DepthStencilState-based call. Each still does exactly 1 Draw + 1
    # GetBackBufferData read per frame, so Bgfx's own "first read per rendered frame" quirk (Task
    # 406) does not apply -- no other restructuring needed.
    cna_bgfx_test(cna_test_bgfx_blendstate_opaque
                  examples/bgfx_blendstate_opaque_test.cpp)
    cna_register_backend_test(NAME Bgfx_BlendState_Opaque COMMAND cna_test_bgfx_blendstate_opaque
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_bgfx_test(cna_test_bgfx_blendstate_alphablend
                  examples/bgfx_blendstate_alphablend_test.cpp)
    cna_register_backend_test(NAME Bgfx_BlendState_AlphaBlend COMMAND cna_test_bgfx_blendstate_alphablend
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_bgfx_test(cna_test_bgfx_blendstate_nonpremultiplied
                  examples/bgfx_blendstate_nonpremultiplied_test.cpp)
    cna_register_backend_test(NAME Bgfx_BlendState_NonPremultiplied COMMAND cna_test_bgfx_blendstate_nonpremultiplied
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_bgfx_test(cna_test_bgfx_blendstate_additive
                  examples/bgfx_blendstate_additive_test.cpp)
    cna_register_backend_test(NAME Bgfx_BlendState_Additive COMMAND cna_test_bgfx_blendstate_additive
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 803 (Phase 72 Bgfx gap closure): SpriteSortMode ordering affects on-screen draw order
    # on Bgfx -- a Bgfx-specific adaptation of the EasyGL source (Task 420; that file reads back 3
    # regions from one rendered frame, incompatible with Bgfx's GetBackBufferData quirk, Task 406
    # -- see the test's own file-header comment for full scope notes).
    cna_bgfx_test(cna_test_bgfx_spritebatch_layerdepth
                  examples/bgfx_spritebatch_layerdepth_test.cpp)
    cna_register_backend_test(NAME Bgfx_SpriteBatch_LayerDepthOrder COMMAND cna_test_bgfx_spritebatch_layerdepth
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 804 (Phase 72 Bgfx gap closure): SpriteBatch::Draw rotation-around-origin on Bgfx -- a
    # Bgfx-specific adaptation of the EasyGL source (Task 417; that file reads back 3 regions from
    # one rendered frame, incompatible with Bgfx's GetBackBufferData quirk, Task 406 -- see the
    # test's own file-header comment).
    cna_bgfx_test(cna_test_bgfx_spritebatch_rotation
                  examples/bgfx_spritebatch_rotation_test.cpp)
    cna_register_backend_test(NAME Bgfx_SpriteBatch_Rotation COMMAND cna_test_bgfx_spritebatch_rotation
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 805 (Phase 72 Bgfx gap closure): SpriteBatch::Draw scalar/Vector2 scale overloads on
    # Bgfx -- a Bgfx-specific adaptation of the EasyGL source (Task 418; that file reads back 5
    # regions from one rendered frame, incompatible with Bgfx's GetBackBufferData quirk, Task 406
    # -- see the test's own file-header comment).
    cna_bgfx_test(cna_test_bgfx_spritebatch_scale
                  examples/bgfx_spritebatch_scale_test.cpp)
    cna_register_backend_test(NAME Bgfx_SpriteBatch_Scale COMMAND cna_test_bgfx_spritebatch_scale
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 806 (Phase 72 Bgfx gap closure): SpriteBatch::Draw sourceRectangle cropping on Bgfx --
    # a Bgfx-specific adaptation of the EasyGL source (Task 419; that file reads back 3 regions
    # from one rendered frame, incompatible with Bgfx's GetBackBufferData quirk, Task 406 -- see
    # the test's own file-header comment).
    cna_bgfx_test(cna_test_bgfx_spritebatch_sourcerect
                  examples/bgfx_spritebatch_sourcerect_test.cpp)
    cna_register_backend_test(NAME Bgfx_SpriteBatch_SourceRectangleCropping COMMAND cna_test_bgfx_spritebatch_sourcerect
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 807 (Phase 72 Bgfx gap closure): SpriteEffects::FlipHorizontally/FlipVertically on
    # Bgfx (mirrors Task 167) -- a Bgfx-specific adaptation of the EasyGL source (that file reads
    # back 8 points from 4 draws in one rendered frame, incompatible with Bgfx's
    # GetBackBufferData quirk, Task 406 -- see the test's own file-header comment).
    cna_bgfx_test(cna_test_bgfx_sprite_effects
                  examples/bgfx_sprite_effects_test.cpp)
    cna_register_backend_test(NAME Bgfx_SpriteEffects_Flip COMMAND cna_test_bgfx_sprite_effects
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 808 (Phase 72 Bgfx gap closure): SpriteBatch::Begin's transformMatrix parameter on
    # Bgfx (mirrors Task 168) -- a Bgfx-specific adaptation of the EasyGL source (that file reads
    # back 2 regions from one rendered frame, incompatible with Bgfx's GetBackBufferData quirk,
    # Task 406 -- see the test's own file-header comment).
    cna_bgfx_test(cna_test_bgfx_transform_matrix
                  examples/bgfx_transform_matrix_test.cpp)
    cna_register_backend_test(NAME Bgfx_SpriteBatch_TransformMatrix COMMAND cna_test_bgfx_transform_matrix
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 809 (Phase 72 Bgfx gap closure): SpriteFont single-glyph placement on Bgfx -- a
    # Bgfx-specific adaptation of the EasyGL source (Task 424; that file reads back 5 regions from
    # one rendered frame, incompatible with Bgfx's GetBackBufferData quirk, Task 406 -- see the
    # test's own file-header comment).
    cna_bgfx_test(cna_test_bgfx_spritefont_single_glyph
                  examples/bgfx_spritefont_single_glyph_test.cpp)
    cna_register_backend_test(NAME Bgfx_SpriteFont_SingleGlyph COMMAND cna_test_bgfx_spritefont_single_glyph
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 810 (Phase 72 Bgfx gap closure): SpriteFont multi-glyph spacing + newline advance on
    # Bgfx -- a Bgfx-specific adaptation combining the EasyGL sources (Tasks 425/426; those files
    # read back several regions from one rendered frame, incompatible with Bgfx's
    # GetBackBufferData quirk, Task 406 -- see the test's own file-header comment).
    cna_bgfx_test(cna_test_bgfx_spritefont_multiglyph_newline
                  examples/bgfx_spritefont_multiglyph_newline_test.cpp)
    cna_register_backend_test(NAME Bgfx_SpriteFont_MultiGlyphSpacingNewline COMMAND cna_test_bgfx_spritefont_multiglyph_newline
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 811 (Phase 72 Bgfx gap closure): SpriteFont default-character fallback on Bgfx -- a
    # Bgfx-specific adaptation of the EasyGL source (Task 427; that file reads back 3 regions from
    # one rendered frame, incompatible with Bgfx's GetBackBufferData quirk, Task 406 -- see the
    # test's own file-header comment). Closes the entire Phase 72 SpriteFont row group (809-811).
    cna_bgfx_test(cna_test_bgfx_spritefont_default_char
                  examples/bgfx_spritefont_default_char_test.cpp)
    cna_register_backend_test(NAME Bgfx_SpriteFont_DefaultCharacterFallback COMMAND cna_test_bgfx_spritefont_default_char
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 812 (Phase 72 Bgfx gap closure): Model with two meshes, each using its own distinct
    # Effect, on Bgfx -- a Bgfx-specific adaptation of the EasyGL source (Task 438; that file uses
    # SetDepthTestEnabled(false), which throws on Bgfx, and reads back 2 regions from one rendered
    # frame, incompatible with Bgfx's GetBackBufferData quirk, Task 406 -- see the test's own
    # file-header comment).
    cna_bgfx_test(cna_test_bgfx_model_two_meshes_effects
                  examples/bgfx_model_two_meshes_effects_test.cpp)
    cna_register_backend_test(NAME Bgfx_Model_TwoMeshesEffects COMMAND cna_test_bgfx_model_two_meshes_effects
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 927: ModelTypeReader::Read()'s vertex-stride/vtable-size corruption fix is in SHARED
    # code (ContentManager.cpp) -- a Bgfx-specific adaptation of the EasyGL source, restructured
    # into per-check RunCheck() passes for Bgfx's own GetBackBufferData "first read per frame"
    # quirk (Task 406).
    cna_bgfx_test(cna_test_bgfx_model_json_reader
                  examples/bgfx_model_json_reader_test.cpp)
    cna_register_backend_test(NAME Bgfx_ModelJsonReader_Quad COMMAND cna_test_bgfx_model_json_reader
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 813 (Phase 72 Bgfx gap closure): Model hierarchy transform propagation (child mesh's
    # own ParentBone) on Bgfx -- a Bgfx-specific adaptation of the EasyGL source (Task 439; same
    # SetDepthTestEnabled/GetBackBufferData substitutions as Task 812 above). Closes the entire
    # Phase 72 Model row group (812-813).
    cna_bgfx_test(cna_test_bgfx_model_hierarchy_child_mesh
                  examples/bgfx_model_hierarchy_child_mesh_test.cpp)
    cna_register_backend_test(NAME Bgfx_Model_HierarchyChildMesh COMMAND cna_test_bgfx_model_hierarchy_child_mesh
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Tasks 814/815 (Phase 72 Bgfx gap closure): OcclusionQuery visible/occluded pixel counts on
    # Bgfx -- Bgfx-specific counterpart of the EasyGL sources (Tasks 445/446), combined into one
    # file since both scenarios share the same fixture (see the test's own file-header comment).
    # Real, pixel-verified assertions cover the underlying rendering/depth-occlusion behavior;
    # OcclusionQuery.IsComplete()/PixelCount() are reported informational-only -- a scratch probe
    # confirmed PixelCount() does not discriminate visible from occluded geometry in this sandbox's
    # software GL renderer (same class of limitation as Task 448/923).
    cna_bgfx_test(cna_test_bgfx_occlusionquery_pixelcount
                  examples/bgfx_occlusionquery_pixelcount_test.cpp)
    cna_register_backend_test(NAME Bgfx_OcclusionQuery_PixelCount COMMAND cna_test_bgfx_occlusionquery_pixelcount
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 816 (Phase 72 Bgfx gap closure): explicit Dispose() on an ACTIVE OcclusionQuery is safe
    # on Bgfx -- distinct from Task 449's own EasyGL-only occlusion_query_test.cpp, which covers
    # C++ destruction, not the explicit .Dispose() API call (see the test's own file-header
    # comment). Closes the entire Phase 72 OcclusionQuery row group (814-816).
    cna_bgfx_test(cna_test_bgfx_occlusionquery_dispose_active
                  examples/bgfx_occlusionquery_dispose_active_test.cpp)
    cna_register_backend_test(NAME Bgfx_OcclusionQuery_DisposeActive COMMAND cna_test_bgfx_occlusionquery_dispose_active
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_cnj.md CNB-58/60 (Phase 13A), Bgfx port: PbrEffect real glTF metallic-roughness BRDF
    # (vs_pbr3d.sc/fs_pbr3d.sc, stride-48 VertexPositionNormalTangentTexture) -- closes the
    # EasyGL-only PBR gap for the Bgfx backend (see examples/bgfx_pbreffect_test.cpp's own
    # file-header comment for the fully analytic camera/light derivation).
    cna_bgfx_test(cna_test_bgfx_pbreffect
                  examples/bgfx_pbreffect_test.cpp)
    cna_register_backend_test(NAME Bgfx_PbrEffect COMMAND cna_test_bgfx_pbreffect
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_cnj.md CNB-58/60 (Phase 13A), Bgfx port: SkinnedPbrEffect (PBR + skinning combo,
    # vs_pbr_skinned3d.sc sharing fs_pbr3d.sc, stride-68
    # VertexPositionNormalTangentTextureSkinned) -- reuses bgfx_pbreffect_test.cpp's own
    # expected values via an identity bind pose (see the test's own file-header comment).
    cna_bgfx_test(cna_test_bgfx_skinnedpbreffect
                  examples/bgfx_skinnedpbreffect_test.cpp)
    cna_register_backend_test(NAME Bgfx_SkinnedPbrEffect COMMAND cna_test_bgfx_skinnedpbreffect
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # CNB-67 (Phase 13C), Bgfx port: SkinnedEffect.VertexColorEnabled on the stride-56 skinned+
    # Color vertex layout (vs_skinned3d.sc/fs_skinned3d.sc and vs_skinned3d_vertexlit.sc/
    # fs_skinned3d_vertexlit.sc's new a_color0/v_vertexColor0/u_vertexColorEnabled3D wiring) --
    # closes the EasyGL-only vertex-color-on-skinned-mesh gap for the Bgfx backend.
    cna_bgfx_test(cna_test_bgfx_skinnedeffect_vertexcolor
                  examples/bgfx_skinnedeffect_vertexcolor_test.cpp)
    cna_register_backend_test(NAME Bgfx_SkinnedEffect_VertexColor COMMAND cna_test_bgfx_skinnedeffect_vertexcolor
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

endif()
