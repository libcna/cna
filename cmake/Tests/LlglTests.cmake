# plan_llgl.md LLGL-9/LLGL-16: CTest registration for the LLGL graphics backend.
#
# Both tests need a real display and a real GPU (or a software rasterizer behind a virtual X
# server), exactly like the Vulkan/Bgfx/SDL_GPU entries: LLGL creates its context or surface from
# the SDL window itself. Each binary probes for a usable display first and exits with the project's
# skip code when there is none, so a headless machine reports SKIPPED rather than FAILED.
#
# Only the X11 SDL video driver is supported (see LlglSdlSurface) -- the environment pins it rather
# than letting SDL pick Wayland on a session that has both.
if(CNA_BUILD_TESTS AND CNA_GRAPHICS_BACKEND STREQUAL "LLGL")
    enable_testing()

    macro(cna_llgl_test target src)
        add_executable(${target} ${src})
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(${target} PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME SDL3::SDL3)
        else()
            target_link_libraries(${target} PRIVATE CNA ${BACKEND_TARGET} SHARP_RUNTIME SDL3::SDL3)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(${target} PRIVATE SDL3::SDL3main)
        endif()
    endmacro()

    cna_llgl_test(cna_test_llgl_smoke examples/llgl_smoke_test.cpp)
    cna_register_backend_test(NAME Llgl_Smoke COMMAND cna_test_llgl_smoke
        TIMEOUT 90 LABELS "GraphicsSmoke;Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_llgl_test(cna_test_llgl_2d examples/llgl_2d_test.cpp)
    cna_register_backend_test(NAME Llgl_2D COMMAND cna_test_llgl_2d
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # LLGL-19: byte-exact texture upload/readback through the backend's own path.
    cna_llgl_test(cna_test_llgl_texture_readback examples/llgl_texture_readback_test.cpp)
    cna_register_backend_test(NAME Llgl_TextureReadback COMMAND cna_test_llgl_texture_readback
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # LLGL-20: the five presentation policies, checked by where pixels land.
    cna_llgl_test(cna_test_llgl_presentation examples/llgl_presentation_test.cpp)
    cna_register_backend_test(NAME Llgl_Presentation COMMAND cna_test_llgl_presentation
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # LLGL-24: the colour-only 3D path -- vertex/index buffers, depth test, cull mode.
    cna_llgl_test(cna_test_llgl_3d examples/llgl_3d_test.cpp)
    cna_register_backend_test(NAME Llgl_3D COMMAND cna_test_llgl_3d
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # LLGL-25: textured/tinted/fogged/alpha-tested stock effects.
    cna_llgl_test(cna_test_llgl_basiceffect examples/llgl_basiceffect_test.cpp)
    cna_register_backend_test(NAME Llgl_BasicEffect COMMAND cna_test_llgl_basiceffect
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # LLGL-25: BasicEffect's lit path.
    cna_llgl_test(cna_test_llgl_lighting examples/llgl_lighting_test.cpp)
    cna_register_backend_test(NAME Llgl_Lighting COMMAND cna_test_llgl_lighting
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # LLGL-26: RenderTarget2D -- draw into it, unbind, sample it back as a Texture2D, GetData().
    cna_llgl_test(cna_test_llgl_rendertarget examples/llgl_rendertarget_test.cpp)
    cna_register_backend_test(NAME Llgl_RenderTarget COMMAND cna_test_llgl_rendertarget
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # LLGL-28: real occlusion queries via LLGL::QueryHeap.
    cna_llgl_test(cna_test_llgl_occlusionquery examples/llgl_occlusionquery_test.cpp)
    cna_register_backend_test(NAME Llgl_OcclusionQuery COMMAND cna_test_llgl_occlusionquery
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # LLGL-27: custom ShaderEffect -- runtime GLSL/SPIR-V compile, tinted through a real uniform.
    cna_llgl_test(cna_test_llgl_shadereffect examples/llgl_shadereffect_test.cpp)
    cna_register_backend_test(NAME Llgl_ShaderEffect COMMAND cna_test_llgl_shadereffect
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # LLGL-29: a real window resize (GraphicsDeviceManager.ApplyChanges()), read back at the new
    # resolution.
    cna_llgl_test(cna_test_llgl_resize examples/llgl_resize_test.cpp)
    cna_register_backend_test(NAME Llgl_Resize COMMAND cna_test_llgl_resize
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # LLGL-23: MSAA back buffer -- a real antialiased edge, read back from a multisampled swap chain.
    cna_llgl_test(cna_test_llgl_msaa examples/llgl_msaa_test.cpp)
    cna_register_backend_test(NAME Llgl_Msaa COMMAND cna_test_llgl_msaa
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # LLGL-25 follow-up: DualTextureEffect.
    cna_llgl_test(cna_test_llgl_dualtexture examples/llgl_dualtexture_test.cpp)
    cna_register_backend_test(NAME Llgl_DualTexture COMMAND cna_test_llgl_dualtexture
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # LLGL-32: GraphicsDevice::DrawUserPrimitives()'s typed overloads -- verbatim reuse of the
    # shared, backend-agnostic source already registered on the EasyGL/Vulkan/Bgfx backends. Only
    # buildable now that ResolveVertexAttributes() infers a vertex layout from the raw upload
    # stride when DrawUserPrimitives' own backend_->CreateVertexBuffer(int)-based buffer carries
    # no VertexDeclaration at all.
    cna_llgl_test(cna_test_llgl_dualtextureeffect_vertexcolor
                  examples/dualtextureeffect_vertexcolor_test.cpp)
    cna_register_backend_test(NAME Llgl_DualTextureEffect_VertexColor COMMAND cna_test_llgl_dualtextureeffect_vertexcolor
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # LLGL-32: DrawUserPrimitives() with VertexPositionColor (stride 16) -- the other stride the
    # stride-inference fix covers, exercised through default device state rather than an explicit
    # BlendState/DepthStencilState/RasterizerState (verbatim reuse of the shared source).
    cna_llgl_test(cna_test_llgl_graphicsdevice_default_state_occlusion
                  examples/graphicsdevice_default_state_occlusion_test.cpp)
    cna_register_backend_test(NAME Llgl_GraphicsDevice_DefaultStateOcclusion COMMAND cna_test_llgl_graphicsdevice_default_state_occlusion
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # LLGL-25 follow-up: EnvironmentMapEffect -- verbatim reuse of the shared, backend-agnostic
    # source already registered on the EasyGL/Vulkan/Bgfx backends (Task 891's alpha-scaled base
    # lerp fix), the same one-source-N-backends pattern as the two DrawUserPrimitives reuses above.
    cna_llgl_test(cna_test_llgl_environmentmapeffect_alphascaledlerp
                  examples/environmentmapeffect_alphascaledlerp_test.cpp)
    cna_register_backend_test(NAME Llgl_EnvironmentMapEffect_AlphaScaledLerp COMMAND cna_test_llgl_environmentmapeffect_alphascaledlerp
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # LLGL-25 follow-up: SkinnedEffect -- ported (not verbatim-shared, unlike the two tests above)
    # from the Vulkan backend's own examples/vulkan_skinnedeffect_*_test.cpp, which are themselves
    # fully backend-agnostic (real public XNA API + VertexBuffer::SetDataRaw, no Vulkan-specific
    # code at all) and so port over with only the class name/comment header changed.
    cna_llgl_test(cna_test_llgl_skinnedeffect_identity_bones
                  examples/llgl_skinnedeffect_identity_bones_test.cpp)
    cna_register_backend_test(NAME Llgl_SkinnedEffect_IdentityBones COMMAND cna_test_llgl_skinnedeffect_identity_bones
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_llgl_test(cna_test_llgl_skinnedeffect_twobone_blend
                  examples/llgl_skinnedeffect_twobone_blend_test.cpp)
    cna_register_backend_test(NAME Llgl_SkinnedEffect_TwoBoneBlend COMMAND cna_test_llgl_skinnedeffect_twobone_blend
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # LLGL-37: SkinnedEffect.VertexColorEnabled (NOXNA extension property) -- a genuinely new
    # skinned3d_color.vert/frag.glsl shader pair, selected instead of skinned3d.vert/frag.glsl only
    # for a stride-56 (colour-carrying) vertex layout. Plain SkinnedEffect works on both modules, so
    # this gets an _OpenGL variant below too.
    cna_llgl_test(cna_test_llgl_skinnedeffect_vertexcolor
                  examples/llgl_skinnedeffect_vertexcolor_test.cpp)
    cna_register_backend_test(NAME Llgl_SkinnedEffect_VertexColor COMMAND cna_test_llgl_skinnedeffect_vertexcolor
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # LLGL-26 follow-up: RenderTargetCube -- 6 independent LLGL::RenderTargets sharing one
    # TextureCube colour texture (a different arrayLayer each) and one shared depth texture,
    # mirroring CreateRenderTarget2D's own established pattern. No _OpenGL variant, same reason as
    # Llgl_EnvironmentMapEffect_AlphaScaledLerp: this project's own OpenGL module has no cube
    # texture support at all (hasCubeTextures false), and this test samples the cube through
    # EnvironmentMapEffect as its own final check.
    cna_llgl_test(cna_test_llgl_rendertargetcube examples/llgl_rendertargetcube_test.cpp)
    cna_register_backend_test(NAME Llgl_RenderTargetCube COMMAND cna_test_llgl_rendertargetcube
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # LLGL-34: RenderTargetCube MSAA -- mirrors CreateRenderTarget2D's own LLGL-26 MSAA follow-up
    # architecture (anonymous per-face multisampled colour attachment resolving into the cube's own
    # single-sample colour texture at the relevant arrayLayer) plus a real, shared, explicitly-owned
    # Texture2DMS depth texture across all 6 faces (promoted to MSAA samples, mirroring the Vulkan
    # backend's own VulkanRenderTargetCubeBackend depthImage_ precedent) instead of the plain
    # Texture2D used when MSAA is not requested. No _OpenGL variant, same hasCubeTextures reason as
    # Llgl_RenderTargetCube above.
    cna_llgl_test(cna_test_llgl_msaa_rendertargetcube examples/llgl_msaa_rendertargetcube_test.cpp)
    cna_register_backend_test(NAME Llgl_Msaa_RenderTargetCube COMMAND cna_test_llgl_msaa_rendertargetcube
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # LLGL-35: RenderTargetCube mip-mapping -- mirrors CreateRenderTarget2D's own LLGL-26
    # mip-mapped-render-target follow-up architecture (a real mip chain on the shared cube colour
    # texture, each per-face attachment still binding only level 0, GenerateMips() called after
    # every face's own render pass ends). No _OpenGL variant, same hasCubeTextures reason as
    # Llgl_RenderTargetCube above.
    cna_llgl_test(cna_test_llgl_mip_rendertargetcube examples/llgl_mip_rendertargetcube_test.cpp)
    cna_register_backend_test(NAME Llgl_Mip_RenderTargetCube COMMAND cna_test_llgl_mip_rendertargetcube
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # LLGL-36: shared, cross-backend RenderTargetCube oracles (already registered on EasyGL/Vulkan/
    # Bgfx/etc, not previously on this backend). Each file's own Contract table gained a genuine,
    # independently-verified CNA_BACKEND_LLGL branch -- see each file's own LLGL-specific comment.
    # Notably, this backend's own "one render pass per distinct target, not per bind" replay
    # architecture (docs/llgl-backend.md's own "Two implementation choices" paragraph) turns out to
    # deliver genuine RenderTargetUsage.PreserveContents preservation WITHIN one frame as an
    # incidental consequence -- not because preserveContents is honoured explicitly (it is not: the
    # parameter is never read), but because every command naming the same target within one frame is
    # grouped into ONE render pass, so a "second bind" never becomes a literal, freshly-cleared
    # second pass unless an explicit Clear() was queued (which only a DiscardContents bind does, at
    # the shared XNA layer). All three files' own checks confirmed this empirically.
    cna_llgl_test(cna_test_llgl_rendertargetcube_getdata_contract
                  examples/rendertargetcube_getdata_contract_test.cpp)
    cna_register_backend_test(NAME Llgl_RenderTargetCube_GetDataContract COMMAND cna_test_llgl_rendertargetcube_getdata_contract
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_llgl_test(cna_test_llgl_rendertargetcube_usage
                  examples/rendertargetcube_usage_test.cpp)
    cna_register_backend_test(NAME Llgl_RenderTargetCube_Usage COMMAND cna_test_llgl_rendertargetcube_usage
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_llgl_test(cna_test_llgl_rendertargetcube_msaa_face
                  examples/rendertargetcube_msaa_face_test.cpp)
    cna_register_backend_test(NAME Llgl_RenderTargetCube_MsaaFace COMMAND cna_test_llgl_rendertargetcube_msaa_face
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # LLGL-26 MRT follow-up: RenderTarget2D-slots-only, SpriteBatch/custom-ShaderEffect-only first
    # cut -- see LlglMRTBinding's own doc comment in LlglGraphicsBackend.hpp for the scope
    # boundary. RenderTarget2D (unlike RenderTargetCube) works on both modules, so this gets an
    # _OpenGL variant below too.
    cna_llgl_test(cna_test_llgl_mrt examples/llgl_mrt_test.cpp)
    cna_register_backend_test(NAME Llgl_MRT COMMAND cna_test_llgl_mrt
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # LLGL-25 follow-up: PbrEffect + SkinnedPbrEffect -- adapted (Check (d) ported back once
    # SkinnedPbrEffect itself landed) from the Vulkan backend's own
    # examples/vulkan_pbreffect_handderived_test.cpp (itself fully backend-agnostic real public XNA
    # API + VertexBuffer::SetDataRaw). Plain VertexPositionNormalTangentTexture (stride 48)/
    # VertexPositionNormalTangentTextureSkinned (stride 68) both work on both modules, so this gets
    # an _OpenGL variant below too.
    cna_llgl_test(cna_test_llgl_pbreffect_handderived examples/llgl_pbreffect_handderived_test.cpp)
    cna_register_backend_test(NAME Llgl_PbrEffect_HandDerived COMMAND cna_test_llgl_pbreffect_handderived
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # LLGL-26 MSAA follow-up: real multisampling into a RenderTarget2D's own colour attachment,
    # resolved automatically by LLGL at the end of each render pass this backend replays. Mirrors
    # llgl_msaa_test.cpp's own diagonal-hypotenuse antialiasing technique. Plain RenderTarget2D
    # works on both modules, so this gets an _OpenGL variant below too -- whether either module
    # actually applies MultiSampleCount to it is a separate, module-dependent question the test
    # itself gates on (see its own header comment).
    cna_llgl_test(cna_test_llgl_msaa_rendertarget examples/llgl_msaa_rendertarget_test.cpp)
    cna_register_backend_test(NAME Llgl_Msaa_RenderTarget COMMAND cna_test_llgl_msaa_rendertarget
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # LLGL-26 mip-mapped render target follow-up: a real, correctly downsampled mip chain in a
    # RenderTarget2D's own colour texture, generated via LLGL::CommandBuffer::GenerateMips() right
    # after every render pass a mip-mapped target appears in. Adapted from
    # vulkan_rendertarget2d_mip_test.cpp's own asymmetric 7:1 split technique, reading levels back
    # directly via the now-real GetData(level) instead of forcing GPU LOD selection. Plain
    # RenderTarget2D works on both modules, so this gets an _OpenGL variant below too.
    cna_llgl_test(cna_test_llgl_rendertarget2d_mip examples/llgl_rendertarget2d_mip_test.cpp)
    cna_register_backend_test(NAME Llgl_RenderTarget2D_Mip COMMAND cna_test_llgl_rendertarget2d_mip
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # LLGL-33 investigation: examples/gfx077_colorwritechannels_3d_test.cpp (shared, cross-backend,
    # already registered on SdlGpu/WebGpu) was tried here too, and its very first check found a
    # real, pre-existing bug -- AcquirePrimitivePipeline's own 3D pipeline cache key truncates
    # MakeBlendPipelineKey's result to its low 16 bits, silently discarding
    # BlendState.ColorWriteChannels (slot 0) and all four blend factors for 3D (BasicEffect +
    # DrawPrimitives) draws. Every attempted fix for that truncation destabilized an unrelated,
    # previously-passing test (Llgl_BasicEffect's own alpha-blend check) in a way not understood
    # (see AcquirePrimitivePipeline's own doc comment in LlglGraphicsBackend.cpp). Deliberately NOT
    # registered as a CTest while unfixed -- see known_bugs.md for the open item -- rather than
    # shipping a known-failing test or an unexplained, empirically-fragile fix.

    # LLGL-33: BlendState.MultiSampleMask -- a real per-sample coverage bitmask on a genuinely
    # multisampled RenderTarget2D. Module-dependent (see the test's own header comment): the
    # Vulkan module applies VkPipelineMultisampleStateCreateInfo::pSampleMask unconditionally; the
    # OpenGL module's own glColorMaski-adjacent SetSampleMask call is `#if 0`'d out entirely in
    # vendored LLGL, a permanent limitation on that module confirmed by reading its source.
    cna_llgl_test(cna_test_llgl_multisamplemask examples/llgl_multisamplemask_test.cpp)
    cna_register_backend_test(NAME Llgl_MultiSampleMask COMMAND cna_test_llgl_multisamplemask
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # LLGL-17: the same two binaries, pinned to the OpenGL module instead of the default
    # (Vulkan-first) preference. Running only the default preference is what let the OpenGL module
    # clear correctly and draw nothing for as long as it did -- a module nothing exercises is a
    # module nobody notices breaking. No separate executables: forcing CNA_LLGL_RENDERER is exactly
    # how a user selects it, so this tests the real selection path too.
    cna_register_backend_test(NAME Llgl_Smoke_OpenGL COMMAND cna_test_llgl_smoke
        TIMEOUT 90 LABELS "GraphicsSmoke;Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY};CNA_LLGL_RENDERER=opengl")
    cna_register_backend_test(NAME Llgl_2D_OpenGL COMMAND cna_test_llgl_2d
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY};CNA_LLGL_RENDERER=opengl")
    cna_register_backend_test(NAME Llgl_TextureReadback_OpenGL COMMAND cna_test_llgl_texture_readback
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY};CNA_LLGL_RENDERER=opengl")
    cna_register_backend_test(NAME Llgl_Presentation_OpenGL COMMAND cna_test_llgl_presentation
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY};CNA_LLGL_RENDERER=opengl")
    cna_register_backend_test(NAME Llgl_3D_OpenGL COMMAND cna_test_llgl_3d
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY};CNA_LLGL_RENDERER=opengl")
    cna_register_backend_test(NAME Llgl_BasicEffect_OpenGL COMMAND cna_test_llgl_basiceffect
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY};CNA_LLGL_RENDERER=opengl")
    cna_register_backend_test(NAME Llgl_Lighting_OpenGL COMMAND cna_test_llgl_lighting
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY};CNA_LLGL_RENDERER=opengl")
    cna_register_backend_test(NAME Llgl_RenderTarget_OpenGL COMMAND cna_test_llgl_rendertarget
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY};CNA_LLGL_RENDERER=opengl")
    cna_register_backend_test(NAME Llgl_OcclusionQuery_OpenGL COMMAND cna_test_llgl_occlusionquery
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY};CNA_LLGL_RENDERER=opengl")
    cna_register_backend_test(NAME Llgl_ShaderEffect_OpenGL COMMAND cna_test_llgl_shadereffect
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY};CNA_LLGL_RENDERER=opengl")
    cna_register_backend_test(NAME Llgl_Resize_OpenGL COMMAND cna_test_llgl_resize
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY};CNA_LLGL_RENDERER=opengl")
    cna_register_backend_test(NAME Llgl_Msaa_OpenGL COMMAND cna_test_llgl_msaa
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY};CNA_LLGL_RENDERER=opengl")
    cna_register_backend_test(NAME Llgl_DualTexture_OpenGL COMMAND cna_test_llgl_dualtexture
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY};CNA_LLGL_RENDERER=opengl")
    cna_register_backend_test(NAME Llgl_DualTextureEffect_VertexColor_OpenGL COMMAND cna_test_llgl_dualtextureeffect_vertexcolor
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY};CNA_LLGL_RENDERER=opengl")
    cna_register_backend_test(NAME Llgl_GraphicsDevice_DefaultStateOcclusion_OpenGL COMMAND cna_test_llgl_graphicsdevice_default_state_occlusion
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY};CNA_LLGL_RENDERER=opengl")
    # No _OpenGL variant of Llgl_EnvironmentMapEffect_AlphaScaledLerp: this project's own OpenGL
    # module (GLX/llvmpipe software rasterizer) reports LLGL::RenderingFeatures::hasCubeTextures
    # false, so ANY cube-texture creation aborts with "ValidateGLTextureType: ... not supported"
    # on this module -- a genuine, pre-existing environment/driver gap discovered while adding
    # this test (cube textures were previously exercised only through CnaTests' default,
    # Vulkan-preferred TextureCubeTest, never through a CNA_LLGL_RENDERER=opengl-pinned CTest),
    # not a regression in EnvironmentMapEffect or CreateTextureCube itself. See plan_llgl.md
    # LLGL-25/LLGL-26 and docs/llgl-backend.md.
    cna_register_backend_test(NAME Llgl_SkinnedEffect_IdentityBones_OpenGL COMMAND cna_test_llgl_skinnedeffect_identity_bones
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY};CNA_LLGL_RENDERER=opengl")
    cna_register_backend_test(NAME Llgl_SkinnedEffect_TwoBoneBlend_OpenGL COMMAND cna_test_llgl_skinnedeffect_twobone_blend
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY};CNA_LLGL_RENDERER=opengl")
    cna_register_backend_test(NAME Llgl_SkinnedEffect_VertexColor_OpenGL COMMAND cna_test_llgl_skinnedeffect_vertexcolor
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY};CNA_LLGL_RENDERER=opengl")
    cna_register_backend_test(NAME Llgl_MRT_OpenGL COMMAND cna_test_llgl_mrt
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY};CNA_LLGL_RENDERER=opengl")
    cna_register_backend_test(NAME Llgl_PbrEffect_HandDerived_OpenGL COMMAND cna_test_llgl_pbreffect_handderived
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY};CNA_LLGL_RENDERER=opengl")
    cna_register_backend_test(NAME Llgl_Msaa_RenderTarget_OpenGL COMMAND cna_test_llgl_msaa_rendertarget
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY};CNA_LLGL_RENDERER=opengl")
    cna_register_backend_test(NAME Llgl_RenderTarget2D_Mip_OpenGL COMMAND cna_test_llgl_rendertarget2d_mip
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY};CNA_LLGL_RENDERER=opengl")
    cna_register_backend_test(NAME Llgl_MultiSampleMask_OpenGL COMMAND cna_test_llgl_multisamplemask
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY};CNA_LLGL_RENDERER=opengl")

    # LLGL-39: plain-registration batch of shared, cross-backend tests (no CNA_BACKEND_ conditional
    # Contract branches -- ordinary Game-subclass oracles, already registered on EasyGL and usually
    # several other backends, simply never wired up here). See plan_llgl.md's own Phase LLGL-7 for
    # how this list was derived.
    cna_llgl_test(cna_test_llgl_rendertarget2d_depth examples/rendertarget2d_depth_test.cpp)
    cna_register_backend_test(NAME Llgl_RenderTarget2D_DepthBuffer COMMAND cna_test_llgl_rendertarget2d_depth
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_llgl_test(cna_test_llgl_rendertarget_viewport_scissor_reset
                  examples/rendertarget_viewport_scissor_reset_test.cpp)
    cna_register_backend_test(NAME Llgl_RenderTarget_ViewportScissorReset COMMAND cna_test_llgl_rendertarget_viewport_scissor_reset
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_llgl_test(cna_test_llgl_skinnedeffect_lighting_conformance
                  examples/skinnedeffect_lighting_conformance_test.cpp)
    cna_register_backend_test(NAME Llgl_SkinnedEffect_LightingConformance COMMAND cna_test_llgl_skinnedeffect_lighting_conformance
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # rasterizerstate_cullmode_indexed_basiceffect_test.cpp and rasterizerstate_cullmode_camera_test.cpp
    # are deliberately NOT registered here -- both fail for real, understood, UNRELATED reasons (see
    # known_bugs.md): rasterizerstate_cullmode_indexed_basiceffect crashes on an untextured+unlit+
    # no-vertex-colour BasicEffect draw (VertexPositionNormalTexture, TextureEnabled=false,
    # VertexColorEnabled=false) -- AcquirePrimitiveVertexShader() has no shader variant for that
    # combination at all and throws by name; rasterizerstate_cullmode_camera hits a scenario-setup
    # failure specific to its own Orthographic+CreateLookAt scenario (every other scenario in the
    # same file, including Perspective+CreateLookAt with the identical camera, passes) -- root cause
    # not yet identified.
    #
    # rendertargetcube_plural_binding_test.cpp, spritebatch_custom_viewport_test.cpp and
    # spritebatch_viewport_switch_test.cpp previously failed here too, all three tracing to the same
    # root cause (now fixed, see CaptureFrameCommandViewportEXT()): this backend's frame replay only
    # called LLGL::CommandBuffer::SetViewport() once per render-pass bucket, and sprite geometry was
    # never offset by a custom Viewport's X/Y at all (only clipped to it via scissor), so a game that
    # set a DIFFERENT Viewport for each of several draws into the SAME target within one unflushed
    # frame got either the wrong GPU viewport (3D primitives) or the wrong screen position (sprites).
    # The fix captures a per-command physical viewport (narrowed only for Primitives, matching the
    # scissor precedent) and bakes the Viewport-local sprite offset into QueueSpriteEXT's own CPU-side
    # geometry, matching the FNA SpriteBatch.cs PrepRenderState contract these tests assert against.
    # This same fix also resolved an independent, pre-existing bug found along the way: LlglSpriteBatchBackend
    # ::Begin() unconditionally reset its own transform_ to identity, discarding whatever SpriteBatch::Begin()
    # had just set via SetTransformMatrix() immediately before calling it.
    #
    # No _OpenGL variant of any of the three: verified failing (all Red/Blue pixels empty) on this
    # project's own OpenGL module (GLX/llvmpipe) specifically whenever the effective scissor/viewport
    # rectangle has a NON-ZERO Y offset into the swap chain's default framebuffer -- a Y-offset-only
    # scissor against the backbuffer renders nothing, while the identical rectangle with Y=0 (or an
    # X-offset-only rectangle, as in Llgl_RenderTarget_ViewportScissorReset's own right-half scissor)
    # renders correctly. This points at LLGL's own upper-left/lower-left screen-origin flip for the
    # GL module's default framebuffer (GLStateManager::AdjustScissor/AdjustViewport, gated on whether
    # glClipControl(GL_UPPER_LEFT) is actually honoured) rather than anything in this backend's own
    # coordinate math, which is identical for both modules and produces byte-exact results on Vulkan.
    # Plausible cause: this environment's software GLX/llvmpipe driver not exposing ARB_clip_control,
    # forcing LLGL's CPU-side emulation path, which appears inconsistent between the geometry and the
    # scissor/viewport rectangle for a Y-offset sub-region specifically. rendertargetcube_plural_binding
    # additionally hits the separate, already-documented "hasCubeTextures not supported" gap on this
    # same OpenGL module regardless of this finding. See known_bugs.md for the full writeup.
    cna_llgl_test(cna_test_llgl_rendertargetcube_plural_binding
                  examples/rendertargetcube_plural_binding_test.cpp)
    cna_register_backend_test(NAME Llgl_RenderTargetCube_PluralBinding COMMAND cna_test_llgl_rendertargetcube_plural_binding
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_llgl_test(cna_test_llgl_spritebatch_custom_viewport
                  examples/spritebatch_custom_viewport_test.cpp)
    cna_register_backend_test(NAME Llgl_SpriteBatch_CustomViewport COMMAND cna_test_llgl_spritebatch_custom_viewport
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_llgl_test(cna_test_llgl_spritebatch_viewport_switch
                  examples/spritebatch_viewport_switch_test.cpp)
    cna_register_backend_test(NAME Llgl_SpriteBatch_ViewportSwitch COMMAND cna_test_llgl_spritebatch_viewport_switch
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_llgl_test(cna_test_llgl_backbuffer_pass_order
                  examples/backbuffer_pass_order_test.cpp)
    cna_register_backend_test(NAME Llgl_BackBuffer_PassOrder COMMAND cna_test_llgl_backbuffer_pass_order
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_llgl_test(cna_test_llgl_backbuffer_readback_dimension
                  examples/backbuffer_readback_dimension_test.cpp)
    cna_register_backend_test(NAME Llgl_BackBuffer_ReadbackDimension COMMAND cna_test_llgl_backbuffer_readback_dimension
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_llgl_test(cna_test_llgl_backbuffer_headless_reject
                  examples/backbuffer_headless_reject_test.cpp)
    cna_register_backend_test(NAME Llgl_BackBuffer_HeadlessReject COMMAND cna_test_llgl_backbuffer_headless_reject
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_llgl_test(cna_test_llgl_rendertarget_pass_boundary
                  examples/rendertarget_pass_boundary_test.cpp)
    cna_register_backend_test(NAME Llgl_RenderTarget_PassBoundary COMMAND cna_test_llgl_rendertarget_pass_boundary
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # rendertarget_depthstencil_usage_test.cpp is deliberately NOT registered here: 28/29 checks
    # pass (its own CNA_BACKEND_LLGL Contract branch is otherwise accurate), but U2 (two
    # RenderTargetCube faces sharing one depth buffer, replayed out of public order) hits a real,
    # separate, OPEN finding -- see known_bugs.md's new entry.

    # rendertarget_effect_source_test.cpp: 18/20 legs pass once EnvironmentMapEffect/SkinnedEffect
    # are declared (like D3D9/D3D11/D3D12/WebGPU already are) as needing a normal/bone-weight-bearing
    # vertex stream this fixture's plain VertexPositionTexture does not provide. The other two legs
    # are registered nowhere: F1 hits the same bucket-ordering finding as
    # rendertarget_depthstencil_usage_test.cpp's own U2 (see known_bugs.md), and C1 crashes the
    # Vulkan driver via a custom ShaderEffect that uses multiple descriptor sets this backend's
    # custom-effect pipeline layout does not support -- also known_bugs.md, a separate, distinct
    # finding. Registered per-leg (not as one supervisor run) specifically so C1's crash cannot take
    # the other 18 legs' own coverage down with it.
    cna_llgl_test(cna_test_llgl_rendertarget_effect_source
                  examples/rendertarget_effect_source_test.cpp)
    foreach(_llgl_res_leg A1 A2 A3 B1 D1 E1 G1 H1 I1 J1 K1 L1 M1 M2 M3 N1 O1 P1)
        cna_register_backend_test(NAME "Llgl_RenderTarget_EffectSource_${_llgl_res_leg}"
            COMMAND cna_test_llgl_rendertarget_effect_source --leg=${_llgl_res_leg}
            TIMEOUT 90 LABELS "Llgl"
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
    endforeach()

    # backbuffer_first_read_test.cpp is deliberately NOT registered here: 9/13 legs pass
    # (A1, A3, A4, A6, B1, B2, B3, B4, C1), but D63/D64/D65 (its own row-pitch matrix, widths
    # 63/64/65 against a fixed height of 17) and E1 (64x32) all fail for a real, identified,
    # UNRELATED-to-row-pitch reason -- see known_bugs.md's new open entry: this backend's default
    # CnaPresentationMode::FixedHeightDynamicWidth derives its own internal logical width from the
    # PHYSICAL window's aspect ratio (`round(physicalWidth * logicalHeight / physicalHeight)`),
    # discarding the game's own requested PreferredBackBufferWidth entirely whenever the requested
    # aspect is wider than the physical window's. In this project's own headless test environment
    # the physical window is consistently ~800x480 regardless of what is requested, so a height-17
    # request derives a logical width of only ~28 -- any column at or past that (including every
    # one of D63/64/65/E1's own probed columns beyond the derived boundary) samples outside this
    # backend's own internal coordinate space instead of the requested 63/64/65/64-wide content.
    # backbuffer_readback_dimension_test.cpp's own odd/small sizes (37x23, 41x29, 50x40, 30x20)
    # all happen to stay under their own derived logical width, which is why that file passes
    # cleanly and did not surface this on its own.

    cna_llgl_test(cna_test_llgl_viewport_reset_after_resize
                  examples/viewport_reset_after_resize_test.cpp)
    cna_register_backend_test(NAME Llgl_ViewportResetAfterResize COMMAND cna_test_llgl_viewport_reset_after_resize
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_llgl_test(cna_test_llgl_graphicsdevice_clear_depth
                  examples/graphicsdevice_clear_depth_test.cpp)
    cna_register_backend_test(NAME Llgl_GraphicsDevice_ClearDepth COMMAND cna_test_llgl_graphicsdevice_clear_depth
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

endif()
