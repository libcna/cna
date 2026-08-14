# llgl renderer example/test registration (module-local; this file is the
# former cmake/Tests/LlglTests.cmake, moved when examples moved into their owning
# physical modules).
#
# From the root CMakeLists.txt include site:
# plan_llgl.md: LLGL renderer CTest registration. Like the Vulkan/Bgfx/SDL_GPU entries above these
# need a real (or virtual) display; unlike them, the native API underneath is decided at runtime by
# LLGL itself, so the same binaries exercise whichever module was selected.

# Example/test executables historically live at the build root (scripts, docs and
# the golden-image working-directory conventions rely on it); keep them there.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
# Shared example-support headers ("common/PixelTestGame.hpp", ...) are owned by
# the graphics module's examples directory.
include_directories(${CNA_GRAPHICS_EXAMPLES_DIR})

# plan_llgl.md LLGL-9/LLGL-16: CTest registration for the LLGL graphics renderer.
#
# Both tests need a real display and a real GPU (or a software rasterizer behind a virtual X
# server), exactly like the Vulkan/Bgfx/SDL_GPU entries: LLGL creates its context or surface from
# the SDL window itself. Each binary probes for a usable display first and exits with the project's
# skip code when there is none, so a headless machine reports SKIPPED rather than FAILED.
#
# Only the X11 SDL video driver is supported (see LlglSdlSurface) -- the environment pins it rather
# than letting SDL pick Wayland on a session that has both.
if(CNA_BUILD_TESTS AND CNA_GRAPHICS_RENDERER STREQUAL "LLGL")
    enable_testing()

    macro(cna_llgl_test target src)
        # LLGL-55: link every LLGL test binary against the shared Vulkan-WSI-unavailable skip guard
        # (common/LlglVulkanWsiSkipGuard.cpp) so a DRI3-less Xvfb reports a clean SKIP
        # instead of a crash, without retrofitting each test's own main().
        add_executable(${target} ${src}
            common/LlglVulkanWsiSkipGuard.cpp)
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(${target} PRIVATE
                CNA
                SHARP_RUNTIME SDL3::SDL3)
        else()
            target_link_libraries(${target} PRIVATE CNA ${RENDERER_TARGET} SHARP_RUNTIME SDL3::SDL3)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(${target} PRIVATE SDL3::SDL3main)
        endif()
    endmacro()

    cna_llgl_test(cna_test_llgl_smoke llgl_smoke_test.cpp)
    cna_register_renderer_test(NAME Llgl_Smoke COMMAND cna_test_llgl_smoke
        TIMEOUT 90 LABELS "GraphicsSmoke;Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_llgl_test(cna_test_llgl_2d llgl_2d_test.cpp)
    cna_register_renderer_test(NAME Llgl_2D COMMAND cna_test_llgl_2d
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # LLGL-19: byte-exact texture upload/readback through the renderer's own path.
    cna_llgl_test(cna_test_llgl_texture_readback llgl_texture_readback_test.cpp)
    cna_register_renderer_test(NAME Llgl_TextureReadback COMMAND cna_test_llgl_texture_readback
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # LLGL-20: the five presentation policies, checked by where pixels land.
    cna_llgl_test(cna_test_llgl_presentation llgl_presentation_test.cpp)
    cna_register_renderer_test(NAME Llgl_Presentation COMMAND cna_test_llgl_presentation
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # LLGL-24: the colour-only 3D path -- vertex/index buffers, depth test, cull mode.
    cna_llgl_test(cna_test_llgl_3d llgl_3d_test.cpp)
    cna_register_renderer_test(NAME Llgl_3D COMMAND cna_test_llgl_3d
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # LLGL-25: textured/tinted/fogged/alpha-tested stock effects.
    cna_llgl_test(cna_test_llgl_basiceffect llgl_basiceffect_test.cpp)
    cna_register_renderer_test(NAME Llgl_BasicEffect COMMAND cna_test_llgl_basiceffect
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # LLGL-25: BasicEffect's lit path.
    cna_llgl_test(cna_test_llgl_lighting llgl_lighting_test.cpp)
    cna_register_renderer_test(NAME Llgl_Lighting COMMAND cna_test_llgl_lighting
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # LLGL-26: RenderTarget2D -- draw into it, unbind, sample it back as a Texture2D, GetData().
    cna_llgl_test(cna_test_llgl_rendertarget llgl_rendertarget_test.cpp)
    cna_register_renderer_test(NAME Llgl_RenderTarget COMMAND cna_test_llgl_rendertarget
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # LLGL-28: real occlusion queries via LLGL::QueryHeap.
    cna_llgl_test(cna_test_llgl_occlusionquery llgl_occlusionquery_test.cpp)
    cna_register_renderer_test(NAME Llgl_OcclusionQuery COMMAND cna_test_llgl_occlusionquery
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # LLGL-27: custom ShaderEffect -- runtime GLSL/SPIR-V compile, tinted through a real uniform.
    cna_llgl_test(cna_test_llgl_shadereffect llgl_shadereffect_test.cpp)
    cna_register_renderer_test(NAME Llgl_ShaderEffect COMMAND cna_test_llgl_shadereffect
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # LLGL-29: a real window resize (GraphicsDeviceManager.ApplyChanges()), read back at the new
    # resolution.
    cna_llgl_test(cna_test_llgl_resize llgl_resize_test.cpp)
    cna_register_renderer_test(NAME Llgl_Resize COMMAND cna_test_llgl_resize
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # LLGL-23: MSAA back buffer -- a real antialiased edge, read back from a multisampled swap chain.
    cna_llgl_test(cna_test_llgl_msaa llgl_msaa_test.cpp)
    cna_register_renderer_test(NAME Llgl_Msaa COMMAND cna_test_llgl_msaa
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # LLGL-25 follow-up: DualTextureEffect.
    cna_llgl_test(cna_test_llgl_dualtexture llgl_dualtexture_test.cpp)
    cna_register_renderer_test(NAME Llgl_DualTexture COMMAND cna_test_llgl_dualtexture
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # LLGL-32: GraphicsDevice::DrawUserPrimitives()'s typed overloads -- verbatim reuse of the
    # shared, renderer-agnostic source already registered on the EasyGL/Vulkan/Bgfx renderers. Only
    # buildable now that ResolveVertexAttributes() infers a vertex layout from the raw upload
    # stride when DrawUserPrimitives' own renderer_->CreateVertexBuffer(int)-based buffer carries
    # no VertexDeclaration at all.
    cna_llgl_test(cna_test_llgl_dualtextureeffect_vertexcolor
                  ${CNA_GRAPHICS_EXAMPLES_DIR}/dualtextureeffect_vertexcolor_test.cpp)
    cna_register_renderer_test(NAME Llgl_DualTextureEffect_VertexColor COMMAND cna_test_llgl_dualtextureeffect_vertexcolor
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # LLGL-32: DrawUserPrimitives() with VertexPositionColor (stride 16) -- the other stride the
    # stride-inference fix covers, exercised through default device state rather than an explicit
    # BlendState/DepthStencilState/RasterizerState (verbatim reuse of the shared source).
    cna_llgl_test(cna_test_llgl_graphicsdevice_default_state_occlusion
                  ${CNA_GRAPHICS_EXAMPLES_DIR}/graphicsdevice_default_state_occlusion_test.cpp)
    cna_register_renderer_test(NAME Llgl_GraphicsDevice_DefaultStateOcclusion COMMAND cna_test_llgl_graphicsdevice_default_state_occlusion
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # LLGL-25 follow-up: EnvironmentMapEffect -- verbatim reuse of the shared, renderer-agnostic
    # source already registered on the EasyGL/Vulkan/Bgfx renderers (Task 891's alpha-scaled base
    # lerp fix), the same one-source-N-renderers pattern as the two DrawUserPrimitives reuses above.
    cna_llgl_test(cna_test_llgl_environmentmapeffect_alphascaledlerp
                  ${CNA_GRAPHICS_EXAMPLES_DIR}/environmentmapeffect_alphascaledlerp_test.cpp)
    cna_register_renderer_test(NAME Llgl_EnvironmentMapEffect_AlphaScaledLerp COMMAND cna_test_llgl_environmentmapeffect_alphascaledlerp
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # LLGL-25 follow-up: SkinnedEffect -- ported (not verbatim-shared, unlike the two tests above)
    # from the Vulkan renderer's own modules/renderers/vulkan/examples/vulkan_skinnedeffect_*_test.cpp, which are themselves
    # fully renderer-agnostic (real public XNA API + VertexBuffer::SetDataRaw, no Vulkan-specific
    # code at all) and so port over with only the class name/comment header changed.
    cna_llgl_test(cna_test_llgl_skinnedeffect_identity_bones
                  llgl_skinnedeffect_identity_bones_test.cpp)
    cna_register_renderer_test(NAME Llgl_SkinnedEffect_IdentityBones COMMAND cna_test_llgl_skinnedeffect_identity_bones
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_llgl_test(cna_test_llgl_skinnedeffect_twobone_blend
                  llgl_skinnedeffect_twobone_blend_test.cpp)
    cna_register_renderer_test(NAME Llgl_SkinnedEffect_TwoBoneBlend COMMAND cna_test_llgl_skinnedeffect_twobone_blend
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # LLGL-37: SkinnedEffect.VertexColorEnabled (CNAEXT extension property) -- a genuinely new
    # skinned3d_color.vert/frag.glsl shader pair, selected instead of skinned3d.vert/frag.glsl only
    # for a stride-56 (colour-carrying) vertex layout. Plain SkinnedEffect works on both modules, so
    # this gets an _OpenGL variant below too.
    cna_llgl_test(cna_test_llgl_skinnedeffect_vertexcolor
                  llgl_skinnedeffect_vertexcolor_test.cpp)
    cna_register_renderer_test(NAME Llgl_SkinnedEffect_VertexColor COMMAND cna_test_llgl_skinnedeffect_vertexcolor
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # LLGL-26 follow-up: RenderTargetCube -- 6 independent LLGL::RenderTargets sharing one
    # TextureCube colour texture (a different arrayLayer each) and one shared depth texture,
    # mirroring CreateRenderTarget2D's own established pattern. No _OpenGL variant, same reason as
    # Llgl_EnvironmentMapEffect_AlphaScaledLerp: this project's own OpenGL module has no cube
    # texture support at all (hasCubeTextures false), and this test samples the cube through
    # EnvironmentMapEffect as its own final check.
    cna_llgl_test(cna_test_llgl_rendertargetcube llgl_rendertargetcube_test.cpp)
    cna_register_renderer_test(NAME Llgl_RenderTargetCube COMMAND cna_test_llgl_rendertargetcube
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # LLGL-34: RenderTargetCube MSAA -- mirrors CreateRenderTarget2D's own LLGL-26 MSAA follow-up
    # architecture (anonymous per-face multisampled colour attachment resolving into the cube's own
    # single-sample colour texture at the relevant arrayLayer) plus a real, shared, explicitly-owned
    # Texture2DMS depth texture across all 6 faces (promoted to MSAA samples, mirroring the Vulkan
    # renderer's own VulkanRenderTargetCubeRenderer depthImage_ precedent) instead of the plain
    # Texture2D used when MSAA is not requested. No _OpenGL variant, same hasCubeTextures reason as
    # Llgl_RenderTargetCube above.
    cna_llgl_test(cna_test_llgl_msaa_rendertargetcube llgl_msaa_rendertargetcube_test.cpp)
    cna_register_renderer_test(NAME Llgl_Msaa_RenderTargetCube COMMAND cna_test_llgl_msaa_rendertargetcube
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # LLGL-35: RenderTargetCube mip-mapping -- mirrors CreateRenderTarget2D's own LLGL-26
    # mip-mapped-render-target follow-up architecture (a real mip chain on the shared cube colour
    # texture, each per-face attachment still binding only level 0, GenerateMips() called after
    # every face's own render pass ends). No _OpenGL variant, same hasCubeTextures reason as
    # Llgl_RenderTargetCube above.
    cna_llgl_test(cna_test_llgl_mip_rendertargetcube llgl_mip_rendertargetcube_test.cpp)
    cna_register_renderer_test(NAME Llgl_Mip_RenderTargetCube COMMAND cna_test_llgl_mip_rendertargetcube
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # LLGL-36: shared, cross-renderer RenderTargetCube oracles (already registered on EasyGL/Vulkan/
    # Bgfx/etc, not previously on this renderer). Each file's own Contract table gained a genuine,
    # independently-verified CNA_RENDERER_LLGL branch -- see each file's own LLGL-specific comment.
    # Notably, this renderer's own "one render pass per distinct target, not per bind" replay
    # architecture (docs/llgl-renderer.md's own "Two implementation choices" paragraph) turns out to
    # deliver genuine RenderTargetUsage.PreserveContents preservation WITHIN one frame as an
    # incidental consequence -- not because preserveContents is honoured explicitly (it is not: the
    # parameter is never read), but because every command naming the same target within one frame is
    # grouped into ONE render pass, so a "second bind" never becomes a literal, freshly-cleared
    # second pass unless an explicit Clear() was queued (which only a DiscardContents bind does, at
    # the shared XNA layer). All three files' own checks confirmed this empirically.
    cna_llgl_test(cna_test_llgl_rendertargetcube_getdata_contract
                  ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertargetcube_getdata_contract_test.cpp)
    cna_register_renderer_test(NAME Llgl_RenderTargetCube_GetDataContract COMMAND cna_test_llgl_rendertargetcube_getdata_contract
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_llgl_test(cna_test_llgl_rendertargetcube_usage
                  ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertargetcube_usage_test.cpp)
    cna_register_renderer_test(NAME Llgl_RenderTargetCube_Usage COMMAND cna_test_llgl_rendertargetcube_usage
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_llgl_test(cna_test_llgl_rendertargetcube_msaa_face
                  ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertargetcube_msaa_face_test.cpp)
    cna_register_renderer_test(NAME Llgl_RenderTargetCube_MsaaFace COMMAND cna_test_llgl_rendertargetcube_msaa_face
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # LLGL-26 MRT follow-up: RenderTarget2D-slots-only, SpriteBatch/custom-ShaderEffect-only first
    # cut -- see LlglMRTBinding's own doc comment in LlglRenderer.hpp for the scope
    # boundary. RenderTarget2D (unlike RenderTargetCube) works on both modules, so this gets an
    # _OpenGL variant below too.
    cna_llgl_test(cna_test_llgl_mrt llgl_mrt_test.cpp)
    cna_register_renderer_test(NAME Llgl_MRT COMMAND cna_test_llgl_mrt
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # LLGL-54: preserve the effective MSAA sample count across a multi-render-target bind, and
    # resolve every attachment independently, not just slot 0.
    cna_llgl_test(cna_test_llgl_mrt_msaa llgl_mrt_msaa_test.cpp)
    cna_register_renderer_test(NAME Llgl_MRT_MSAA COMMAND cna_test_llgl_mrt_msaa
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # LLGL-25 follow-up: PbrEffect + SkinnedPbrEffect -- adapted (Check (d) ported back once
    # SkinnedPbrEffect itself landed) from the Vulkan renderer's own
    # modules/renderers/vulkan/examples/vulkan_pbreffect_handderived_test.cpp (itself fully renderer-agnostic real public XNA
    # API + VertexBuffer::SetDataRaw). Plain VertexPositionNormalTangentTexture (stride 48)/
    # VertexPositionNormalTangentTextureSkinned (stride 68) both work on both modules, so this gets
    # an _OpenGL variant below too.
    cna_llgl_test(cna_test_llgl_pbreffect_handderived llgl_pbreffect_handderived_test.cpp)
    cna_register_renderer_test(NAME Llgl_PbrEffect_HandDerived COMMAND cna_test_llgl_pbreffect_handderived
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # GLTF-213: mid-grey sRGB decode/encode and linear factor/composition witnesses for both PBR
    # programs. The same binary is registered against LLGL's Vulkan and OpenGL modules.
    cna_llgl_test(cna_test_llgl_pbr_srgb_transfer
                  ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_pbr_srgb_transfer_test.cpp)
    cna_register_renderer_test(NAME Llgl_Pbr_SrgbTransfer COMMAND cna_test_llgl_pbr_srgb_transfer
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # LLGL-26 MSAA follow-up: real multisampling into a RenderTarget2D's own colour attachment,
    # resolved automatically by LLGL at the end of each render pass this renderer replays. Mirrors
    # llgl_msaa_test.cpp's own diagonal-hypotenuse antialiasing technique. Plain RenderTarget2D
    # works on both modules, so this gets an _OpenGL variant below too -- whether either module
    # actually applies MultiSampleCount to it is a separate, module-dependent question the test
    # itself gates on (see its own header comment).
    cna_llgl_test(cna_test_llgl_msaa_rendertarget llgl_msaa_rendertarget_test.cpp)
    cna_register_renderer_test(NAME Llgl_Msaa_RenderTarget COMMAND cna_test_llgl_msaa_rendertarget
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # LLGL-26 mip-mapped render target follow-up: a real, correctly downsampled mip chain in a
    # RenderTarget2D's own colour texture, generated via LLGL::CommandBuffer::GenerateMips() right
    # after every render pass a mip-mapped target appears in. Adapted from
    # vulkan_rendertarget2d_mip_test.cpp's own asymmetric 7:1 split technique, reading levels back
    # directly via the now-real GetData(level) instead of forcing GPU LOD selection. Plain
    # RenderTarget2D works on both modules, so this gets an _OpenGL variant below too.
    cna_llgl_test(cna_test_llgl_rendertarget2d_mip llgl_rendertarget2d_mip_test.cpp)
    cna_register_renderer_test(NAME Llgl_RenderTarget2D_Mip COMMAND cna_test_llgl_rendertarget2d_mip
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # LLGL-48: the complete blend state now has an independent primitive-pipeline cache-key
    # element, so per-channel write masks cannot collide with a previously cached Opaque pipeline.
    # This shared oracle also exercises A(Red)->B(Green)->A(Red) within one frame.
    cna_llgl_test(cna_test_llgl_colorwritechannels ${CNA_GRAPHICS_EXAMPLES_DIR}/gfx077_colorwritechannels_3d_test.cpp)
    cna_register_renderer_test(NAME Llgl_ColorWriteChannels_3D
        COMMAND cna_test_llgl_colorwritechannels
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY};CNA_LLGL_RENDERER=opengl")

    # LLGL-33: BlendState.MultiSampleMask -- a real per-sample coverage bitmask on a genuinely
    # multisampled RenderTarget2D. Module-dependent (see the test's own header comment): the
    # Vulkan module applies VkPipelineMultisampleStateCreateInfo::pSampleMask unconditionally; the
    # OpenGL module's own glColorMaski-adjacent SetSampleMask call is `#if 0`'d out entirely in
    # vendored LLGL, a permanent limitation on that module confirmed by reading its source.
    cna_llgl_test(cna_test_llgl_multisamplemask llgl_multisamplemask_test.cpp)
    cna_register_renderer_test(NAME Llgl_MultiSampleMask COMMAND cna_test_llgl_multisamplemask
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # LLGL-17: the same two binaries, pinned to the OpenGL module instead of the default
    # (Vulkan-first) preference. Running only the default preference is what let the OpenGL module
    # clear correctly and draw nothing for as long as it did -- a module nothing exercises is a
    # module nobody notices breaking. No separate executables: forcing CNA_LLGL_RENDERER is exactly
    # how a user selects it, so this tests the real selection path too.
    cna_register_renderer_test(NAME Llgl_Smoke_OpenGL COMMAND cna_test_llgl_smoke
        TIMEOUT 90 LABELS "GraphicsSmoke;Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY};CNA_LLGL_RENDERER=opengl")
    cna_register_renderer_test(NAME Llgl_2D_OpenGL COMMAND cna_test_llgl_2d
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY};CNA_LLGL_RENDERER=opengl")
    cna_register_renderer_test(NAME Llgl_TextureReadback_OpenGL COMMAND cna_test_llgl_texture_readback
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY};CNA_LLGL_RENDERER=opengl")
    cna_register_renderer_test(NAME Llgl_Presentation_OpenGL COMMAND cna_test_llgl_presentation
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY};CNA_LLGL_RENDERER=opengl")
    cna_register_renderer_test(NAME Llgl_3D_OpenGL COMMAND cna_test_llgl_3d
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY};CNA_LLGL_RENDERER=opengl")
    cna_register_renderer_test(NAME Llgl_BasicEffect_OpenGL COMMAND cna_test_llgl_basiceffect
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY};CNA_LLGL_RENDERER=opengl")
    cna_register_renderer_test(NAME Llgl_Lighting_OpenGL COMMAND cna_test_llgl_lighting
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY};CNA_LLGL_RENDERER=opengl")
    cna_register_renderer_test(NAME Llgl_RenderTarget_OpenGL COMMAND cna_test_llgl_rendertarget
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY};CNA_LLGL_RENDERER=opengl")
    cna_register_renderer_test(NAME Llgl_OcclusionQuery_OpenGL COMMAND cna_test_llgl_occlusionquery
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY};CNA_LLGL_RENDERER=opengl")
    cna_register_renderer_test(NAME Llgl_ShaderEffect_OpenGL COMMAND cna_test_llgl_shadereffect
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY};CNA_LLGL_RENDERER=opengl")
    cna_register_renderer_test(NAME Llgl_Resize_OpenGL COMMAND cna_test_llgl_resize
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY};CNA_LLGL_RENDERER=opengl")
    cna_register_renderer_test(NAME Llgl_Msaa_OpenGL COMMAND cna_test_llgl_msaa
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY};CNA_LLGL_RENDERER=opengl")
    cna_register_renderer_test(NAME Llgl_DualTexture_OpenGL COMMAND cna_test_llgl_dualtexture
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY};CNA_LLGL_RENDERER=opengl")
    cna_register_renderer_test(NAME Llgl_DualTextureEffect_VertexColor_OpenGL COMMAND cna_test_llgl_dualtextureeffect_vertexcolor
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY};CNA_LLGL_RENDERER=opengl")
    cna_register_renderer_test(NAME Llgl_GraphicsDevice_DefaultStateOcclusion_OpenGL COMMAND cna_test_llgl_graphicsdevice_default_state_occlusion
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY};CNA_LLGL_RENDERER=opengl")
    # No _OpenGL variant of Llgl_EnvironmentMapEffect_AlphaScaledLerp: this project's own OpenGL
    # module (GLX/llvmpipe software rasterizer) reports LLGL::RenderingFeatures::hasCubeTextures
    # false, so ANY cube-texture creation aborts with "ValidateGLTextureType: ... not supported"
    # on this module -- a genuine, pre-existing environment/driver gap discovered while adding
    # this test (cube textures were previously exercised only through CnaTests' default,
    # Vulkan-preferred TextureCubeTest, never through a CNA_LLGL_RENDERER=opengl-pinned CTest),
    # not a regression in EnvironmentMapEffect or CreateTextureCube itself. See plan_llgl.md
    # LLGL-25/LLGL-26 and docs/llgl-renderer.md.
    cna_register_renderer_test(NAME Llgl_SkinnedEffect_IdentityBones_OpenGL COMMAND cna_test_llgl_skinnedeffect_identity_bones
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY};CNA_LLGL_RENDERER=opengl")
    cna_register_renderer_test(NAME Llgl_SkinnedEffect_TwoBoneBlend_OpenGL COMMAND cna_test_llgl_skinnedeffect_twobone_blend
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY};CNA_LLGL_RENDERER=opengl")
    cna_register_renderer_test(NAME Llgl_SkinnedEffect_VertexColor_OpenGL COMMAND cna_test_llgl_skinnedeffect_vertexcolor
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY};CNA_LLGL_RENDERER=opengl")
    cna_register_renderer_test(NAME Llgl_MRT_OpenGL COMMAND cna_test_llgl_mrt
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY};CNA_LLGL_RENDERER=opengl")
    cna_register_renderer_test(NAME Llgl_PbrEffect_HandDerived_OpenGL COMMAND cna_test_llgl_pbreffect_handderived
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY};CNA_LLGL_RENDERER=opengl")
    cna_register_renderer_test(NAME Llgl_Pbr_SrgbTransfer_OpenGL COMMAND cna_test_llgl_pbr_srgb_transfer
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY};CNA_LLGL_RENDERER=opengl")
    cna_register_renderer_test(NAME Llgl_Msaa_RenderTarget_OpenGL COMMAND cna_test_llgl_msaa_rendertarget
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY};CNA_LLGL_RENDERER=opengl")
    cna_register_renderer_test(NAME Llgl_RenderTarget2D_Mip_OpenGL COMMAND cna_test_llgl_rendertarget2d_mip
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY};CNA_LLGL_RENDERER=opengl")
    cna_register_renderer_test(NAME Llgl_MultiSampleMask_OpenGL COMMAND cna_test_llgl_multisamplemask
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY};CNA_LLGL_RENDERER=opengl")

    # LLGL-39: plain-registration batch of shared, cross-renderer tests (no CNA_RENDERER_ conditional
    # Contract branches -- ordinary Game-subclass oracles, already registered on EasyGL and usually
    # several other renderers, simply never wired up here). See plan_llgl.md's own Phase LLGL-7 for
    # how this list was derived.
    cna_llgl_test(cna_test_llgl_rendertarget2d_depth ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget2d_depth_test.cpp)
    cna_register_renderer_test(NAME Llgl_RenderTarget2D_DepthBuffer COMMAND cna_test_llgl_rendertarget2d_depth
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_llgl_test(cna_test_llgl_rendertarget_viewport_scissor_reset
                  ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_viewport_scissor_reset_test.cpp)
    cna_register_renderer_test(NAME Llgl_RenderTarget_ViewportScissorReset COMMAND cna_test_llgl_rendertarget_viewport_scissor_reset
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_llgl_test(cna_test_llgl_skinnedeffect_lighting_conformance
                  ${CNA_GRAPHICS_EXAMPLES_DIR}/skinnedeffect_lighting_conformance_test.cpp)
    cna_register_renderer_test(NAME Llgl_SkinnedEffect_LightingConformance COMMAND cna_test_llgl_skinnedeffect_lighting_conformance
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # LLGL-52: rasterizerstate_cullmode_indexed_basiceffect_test.cpp's own crash is fixed. The test's
    # BasicEffect actually calls EnableDefaultLighting() (lit=true), not unlit as an earlier summary
    # of this bug assumed -- live instrumentation in AcquirePrimitiveVertexShader() showed the real
    # combination hitting the throw was lit+untextured+no-vertex-colour, which fell through to the
    # UNLIT flat3d branch and then failed to link against the LIT fragment shader the pipeline
    # actually paired it with. A new lit_flat3d shader variant (lit_colored3d minus the colour
    # attribute) closes that gap. Confirmed with a fresh run.
    cna_llgl_test(cna_test_llgl_rasterizerstate_cullmode_indexed_basiceffect
                  ${CNA_GRAPHICS_EXAMPLES_DIR}/rasterizerstate_cullmode_indexed_basiceffect_test.cpp)
    cna_register_renderer_test(NAME Llgl_RasterizerState_CullMode_IndexedBasicEffect
        COMMAND cna_test_llgl_rasterizerstate_cullmode_indexed_basiceffect
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # LLGL-52: the post-audit stream/pipeline adaptation resolves the historical orthographic
    # CreateLookAt failure. Both the camera and indexed BasicEffect controls are now gating CTests.
    cna_llgl_test(cna_test_llgl_rasterizerstate_cullmode_camera
                  ${CNA_GRAPHICS_EXAMPLES_DIR}/rasterizerstate_cullmode_camera_test.cpp)
    cna_register_renderer_test(NAME Llgl_RasterizerState_CullMode_Camera
        COMMAND cna_test_llgl_rasterizerstate_cullmode_camera
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # LLGL-53: the pinned OpenGL module cannot provide a truthful non-zero depth-bias contract.
    # The shared test therefore gates deterministic NotSupported rejection rather than accepting
    # a silent state no-op; ordinary depth testing remains covered by the render-target suite.
    cna_llgl_test(cna_test_llgl_rasterizerstate_depthbias ${CMAKE_SOURCE_DIR}/modules/renderers/software/examples/software_depthbias_test.cpp)
    cna_register_renderer_test(NAME Llgl_RasterizerState_DepthBias
        COMMAND cna_test_llgl_rasterizerstate_depthbias
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # LLGL-53: stencil is deliberately unsupported on the measured OpenGL path. This test gates
    # the capability report and deterministic rejection boundary.
    cna_llgl_test(cna_test_llgl_stencil llgl_stencil_test.cpp)
    cna_register_renderer_test(NAME Llgl_Stencil COMMAND cna_test_llgl_stencil
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
    #
    # rendertargetcube_plural_binding_test.cpp, spritebatch_custom_viewport_test.cpp and
    # spritebatch_viewport_switch_test.cpp previously failed here too, all three tracing to the same
    # root cause (now fixed, see CaptureFrameCommandViewportEXT()): this renderer's frame replay only
    # called LLGL::CommandBuffer::SetViewport() once per render-pass bucket, and sprite geometry was
    # never offset by a custom Viewport's X/Y at all (only clipped to it via scissor), so a game that
    # set a DIFFERENT Viewport for each of several draws into the SAME target within one unflushed
    # frame got either the wrong GPU viewport (3D primitives) or the wrong screen position (sprites).
    # The fix captures a per-command physical viewport (narrowed only for Primitives, matching the
    # scissor precedent) and bakes the Viewport-local sprite offset into QueueSpriteEXT's own CPU-side
    # geometry, matching the FNA SpriteBatch.cs PrepRenderState contract these tests assert against.
    # This same fix also resolved an independent, pre-existing bug found along the way: LlglSpriteBatchRenderer
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
    # glClipControl(GL_UPPER_LEFT) is actually honoured) rather than anything in this renderer's own
    # coordinate math, which is identical for both modules and produces byte-exact results on Vulkan.
    # Plausible cause: this environment's software GLX/llvmpipe driver not exposing ARB_clip_control,
    # forcing LLGL's CPU-side emulation path, which appears inconsistent between the geometry and the
    # scissor/viewport rectangle for a Y-offset sub-region specifically. rendertargetcube_plural_binding
    # additionally hits the separate, already-documented "hasCubeTextures not supported" gap on this
    # same OpenGL module regardless of this finding. See known_bugs.md for the full writeup.
    cna_llgl_test(cna_test_llgl_rendertargetcube_plural_binding
                  ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertargetcube_plural_binding_test.cpp)
    cna_register_renderer_test(NAME Llgl_RenderTargetCube_PluralBinding COMMAND cna_test_llgl_rendertargetcube_plural_binding
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_llgl_test(cna_test_llgl_spritebatch_custom_viewport
                  ${CNA_GRAPHICS_EXAMPLES_DIR}/spritebatch_custom_viewport_test.cpp)
    cna_register_renderer_test(NAME Llgl_SpriteBatch_CustomViewport COMMAND cna_test_llgl_spritebatch_custom_viewport
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
    # LLGL-51 (2026-08-04): this OpenGL-forced lane used to read back ZERO matching pixels for
    # every non-zero-Y checked rectangle -- root-caused and fixed in CaptureBackbuffer() (see the
    # Llgl_Deferred_Viewport/Llgl_Deferred_Scissor comment below), now a full 13/13 pass.
    cna_register_renderer_test(NAME Llgl_SpriteBatch_CustomViewport_OpenGL COMMAND cna_test_llgl_spritebatch_custom_viewport
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY};CNA_LLGL_RENDERER=opengl")

    cna_llgl_test(cna_test_llgl_spritebatch_viewport_switch
                  ${CNA_GRAPHICS_EXAMPLES_DIR}/spritebatch_viewport_switch_test.cpp)
    cna_register_renderer_test(NAME Llgl_SpriteBatch_ViewportSwitch COMMAND cna_test_llgl_spritebatch_viewport_switch
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
    # LLGL-51 (2026-08-04): same fix as above, now a full 6/6 pass.
    cna_register_renderer_test(NAME Llgl_SpriteBatch_ViewportSwitch_OpenGL COMMAND cna_test_llgl_spritebatch_viewport_switch
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY};CNA_LLGL_RENDERER=opengl")

    # LLGL-45 follow-up (2026-08-03): this file's own Contract.orderedBackbufferSegments was
    # corrected from false to true (the pre-LLGL-45 bucket-by-identity behavior it used to declare
    # is fixed) -- dozens of previously-skipped checks now genuinely evaluate and pass, strongly
    # confirming LLGL-45's own fix. Correcting it also exposed a new, real, narrower, still-OPEN
    # finding: V1/V2 (per-cycle viewport/scissor on a REVISITED backbuffer) now fail for real. Left
    # registered unchanged (this predates the current session and targets the default/Vulkan
    # selection, which cannot be re-verified in this sandbox) -- see known_bugs.md's new entry.
    cna_llgl_test(cna_test_llgl_backbuffer_pass_order
                  ${CNA_GRAPHICS_EXAMPLES_DIR}/backbuffer_pass_order_test.cpp)
    cna_register_renderer_test(NAME Llgl_BackBuffer_PassOrder COMMAND cna_test_llgl_backbuffer_pass_order
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_llgl_test(cna_test_llgl_backbuffer_readback_dimension
                  ${CNA_GRAPHICS_EXAMPLES_DIR}/backbuffer_readback_dimension_test.cpp)
    cna_register_renderer_test(NAME Llgl_BackBuffer_ReadbackDimension COMMAND cna_test_llgl_backbuffer_readback_dimension
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_llgl_test(cna_test_llgl_backbuffer_headless_reject
                  ${CNA_GRAPHICS_EXAMPLES_DIR}/backbuffer_headless_reject_test.cpp)
    cna_register_renderer_test(NAME Llgl_BackBuffer_HeadlessReject COMMAND cna_test_llgl_backbuffer_headless_reject
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_llgl_test(cna_test_llgl_rendertarget_pass_boundary
                  ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_pass_boundary_test.cpp)
    cna_register_renderer_test(NAME Llgl_RenderTarget_PassBoundary COMMAND cna_test_llgl_rendertarget_pass_boundary
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # rendertarget_depthstencil_usage_test.cpp is not yet wired up as a build target at all: U2 (two
    # RenderTargetCube faces sharing one depth buffer, replayed out of public order, expecting the
    # shared attachment's real prior content on a same-frame revisit) needs both LLGL-45's ordering
    # fix AND real cube-texture support to verify -- this environment's OpenGL module has none
    # (LLGL::RenderingFeatures::hasCubeTextures is false here) and its Vulkan module cannot present
    # under this sandbox's Xvfb (no DRI3, VK_ERROR_SURFACE_LOST_KHR). D5/F1/G1/I2 below (the same
    # bucket-ordering finding on plain RenderTarget2D/the swap chain, which need no cube support)
    # are now confirmed fixed and registered; U2 itself still needs LLGL-38's real-hardware pass or
    # a DRI3-capable Xvfb (LLGL-55) before it can be verified and registered.

    # rendertarget_effect_source_test.cpp: 19/20 legs pass once EnvironmentMapEffect/SkinnedEffect
    # are declared (like D3D9/D3D11/D3D12/WebGPU already are) as needing a normal/bone-weight-bearing
    # vertex stream this fixture's plain VertexPositionTexture does not provide, and F1 (LLGL-45: "A
    # -> B -> A round trip preserves the pattern exactly", 32/32) is now fixed by true public-order
    # segment replay -- confirmed via a fresh, independent run after the fix, not assumed from the
    # ordering-only reasoning alone. Only C1 remains unregistered: it crashes the Vulkan driver via a
    # custom ShaderEffect using multiple descriptor sets this renderer's custom-effect pipeline layout
    # does not support (see known_bugs.md, a separate, distinct finding, LLGL-47's own scope) -- under
    # this sandbox's OpenGL module the same shader simply fails to compile instead (no Vulkan SPIR-V
    # runtime path), so C1's own crash is not even reachable here to confirm one way or the other.
    # Registered per-leg (not as one supervisor run) specifically so C1's crash cannot take the other
    # legs' own coverage down with it.
    cna_llgl_test(cna_test_llgl_rendertarget_effect_source
                  ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_effect_source_test.cpp)
    foreach(_llgl_res_leg A1 A2 A3 B1 D1 E1 F1 G1 H1 I1 J1 K1 L1 M1 M2 M3 N1 O1 P1)
        cna_register_renderer_test(NAME "Llgl_RenderTarget_EffectSource_${_llgl_res_leg}"
            COMMAND cna_test_llgl_rendertarget_effect_source --leg=${_llgl_res_leg}
            TIMEOUT 90 LABELS "Llgl"
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
    endforeach()

    # LLGL-52: CD4 ("BasicEffect lit + textured", no normal attribute) used to crash the whole
    # process (uncaught std::runtime_error) because AcquirePrimitiveVertexShader() refused any lit
    # draw with no normal attribute; a new lit_textured3d_flatnormal shader variant (fixed (0,0,1)
    # object-space normal) now handles it. Confirmed with a fresh 61/61 run.
    cna_llgl_test(cna_test_llgl_rendertarget_sampling_orientation
                  ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_sampling_orientation_test.cpp)
    cna_register_renderer_test(NAME Llgl_RenderTarget_SamplingOrientation
        COMMAND cna_test_llgl_rendertarget_sampling_orientation
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # LLGL-45: D5 and I2 -- both "a target/the back buffer revisited after depending on another
    # target" -- used to hit the bucket-ordering finding also covered by
    # rendertarget_effect_source_test.cpp's own F1; true public-order segment replay fixes both,
    # confirmed by a fresh 41/41 run (this file's own CNA_RENDERER_LLGL Contract branch needed no
    # changes -- it was already accurate about what SHOULD happen, only the replay engine was wrong).
    cna_llgl_test(cna_test_llgl_rendertarget_producer_consumer
                  ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_producer_consumer_test.cpp)
    cna_register_renderer_test(NAME Llgl_RenderTarget_ProducerConsumer COMMAND cna_test_llgl_rendertarget_producer_consumer
        TIMEOUT 120 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-158 cross-renderer control: a RenderTarget2D constructed during a public frame must be
    # usable in that same frame -- bound, cleared and/or drawn into, unbound and observed -- with no
    # warm-up frame, Present, GetData-before-first-render, dummy draw, empty bind cycle, manual frame
    # advance or wait. The defect was bgfx-local (`bgfx::reset()` discards every view's framebuffer
    # binding, including the bound target's); this run is what establishes that LLGL already honoured
    # the contract rather than being made to.
    cna_llgl_test(cna_test_llgl_rendertarget_first_use
                  ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_first_use_test.cpp)
    cna_register_renderer_test(NAME Llgl_RenderTarget_FirstUse COMMAND cna_test_llgl_rendertarget_first_use
        TIMEOUT 120 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # LLGL-45: G1 -- a BACKBUFFER consumer issued between two bind cycles of the SAME target,
    # expected to see the FIRST cycle -- used to hit the same bucket-ordering finding as
    # rendertarget_producer_consumer_test.cpp's own I2; true public-order segment replay (plus the
    # swap chain's own AttachmentLoadOp::Load reload on a same-frame revisit) fixes it, confirmed by
    # a fresh 86/86 run.
    cna_llgl_test(cna_test_llgl_rendertarget_backbuffer_consumer
                  ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_backbuffer_consumer_test.cpp)
    cna_register_renderer_test(NAME Llgl_RenderTarget_BackbufferConsumer COMMAND cna_test_llgl_rendertarget_backbuffer_consumer
        TIMEOUT 120 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # LLGL-50: 17/18 legs now pass (up from 3/18) now that FixedHeightDynamicWidth's own logical
    # width is a floor, not a hard override -- this file's 72x36 backbuffer request is fully
    # addressable now. Only F1 remains unregistered: it needs a RenderTargetCube, and this
    # sandbox's OpenGL module has no cube-texture support at all (a separate, pre-existing,
    # unrelated `hasCubeTextures` gap -- confirmed identical against the pre-LLGL-50 binary too).
    cna_llgl_test(cna_test_llgl_bound_target_lifetime
                  ${CNA_GRAPHICS_EXAMPLES_DIR}/bound_target_lifetime_test.cpp)
    foreach(_llgl_btl_leg A1 A2 B1 B2 C1 D1 E1 G1 G2 H1 I1 J1 K1 L1 M1 N1 P1)
        cna_register_renderer_test(NAME "Llgl_BoundTargetLifetime_${_llgl_btl_leg}"
            COMMAND cna_test_llgl_bound_target_lifetime --leg=${_llgl_btl_leg}
            TIMEOUT 90 LABELS "Llgl"
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
    endforeach()

    # LLGL-50: the aspect-derived logical width FixedHeightDynamicWidth computes is now a floor,
    # never a hard override -- it can no longer derive something NARROWER than the game's own
    # PreferredBackBufferWidth, which is what silently made D63/D64/D65 (row-pitch matrix, widths
    # 63/64/65 against a fixed height of 17) and E1 (64x32) unreachable before. Confirmed fixed with
    # a fresh run: supervisor 13/13 legs, 0 crashed.
    cna_llgl_test(cna_test_llgl_backbuffer_first_read
                  ${CNA_GRAPHICS_EXAMPLES_DIR}/backbuffer_first_read_test.cpp)
    cna_register_renderer_test(NAME Llgl_BackBuffer_FirstRead COMMAND cna_test_llgl_backbuffer_first_read
        TIMEOUT 120 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_llgl_test(cna_test_llgl_viewport_reset_after_resize
                  ${CNA_GRAPHICS_EXAMPLES_DIR}/viewport_reset_after_resize_test.cpp)
    cna_register_renderer_test(NAME Llgl_ViewportResetAfterResize COMMAND cna_test_llgl_viewport_reset_after_resize
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_llgl_test(cna_test_llgl_graphicsdevice_clear_depth
                  ${CNA_GRAPHICS_EXAMPLES_DIR}/graphicsdevice_clear_depth_test.cpp)
    cna_register_renderer_test(NAME Llgl_GraphicsDevice_ClearDepth COMMAND cna_test_llgl_graphicsdevice_clear_depth
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-135: the WRITE half of the same finding backbuffer_first_read_test.cpp's own
    # REMED-GFX-127 covers the READ side of -- TextureCube::SetData/Texture3D::SetData must report
    # honestly whether the whole requested region was actually stored, not silently accept-and-discard.
    cna_llgl_test(cna_test_llgl_cube_volume_setdata_contract
                  ${CNA_GRAPHICS_EXAMPLES_DIR}/texturecube_texture3d_setdata_contract_test.cpp)
    cna_register_renderer_test(NAME Llgl_CubeVolume_SetDataContract COMMAND cna_test_llgl_cube_volume_setdata_contract
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-130: the READ half of the same finding -- TextureCube/Texture3D GetData must report
    # honestly whether it read anything back, not convert a zero-initialized scratch buffer into an
    # apparently-successful, uniformly transparent-black readback.
    cna_llgl_test(cna_test_llgl_cube_volume_getdata_contract
                  ${CNA_GRAPHICS_EXAMPLES_DIR}/texturecube_texture3d_getdata_contract_test.cpp)
    cna_register_renderer_test(NAME Llgl_CubeVolume_GetDataContract COMMAND cna_test_llgl_cube_volume_getdata_contract
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # startIndex/elementCount contract of every public Texture2D::GetData overload. startIndex is a
    # DESTINATION element offset and elementCount is the destination capacity available from it.
    cna_llgl_test(cna_test_llgl_texture2d_getdata_transfer_range
                  ${CNA_GRAPHICS_EXAMPLES_DIR}/texture2d_getdata_transfer_range_test.cpp)
    cna_register_renderer_test(NAME Llgl_Texture2D_GetDataTransferRange COMMAND cna_test_llgl_texture2d_getdata_transfer_range
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_llgl_test(cna_test_llgl_texture2d_getdata_contract
                  ${CNA_GRAPHICS_EXAMPLES_DIR}/texture2d_getdata_contract_test.cpp)
    cna_register_renderer_test(NAME Llgl_Texture2D_GetDataContract COMMAND cna_test_llgl_texture2d_getdata_contract
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # measures the two DIFFERENT partitions of the nine TextureFilter ordinals that magnification and
    # minification induce, on SpriteBatch and on every textured stock family.
    cna_llgl_test(cna_test_llgl_texture_filter_ordinal
                  ${CNA_GRAPHICS_EXAMPLES_DIR}/texture_filter_ordinal_contract_test.cpp)
    cna_register_renderer_test(NAME Llgl_TextureFilterOrdinalContract COMMAND cna_test_llgl_texture_filter_ordinal
        TIMEOUT 300 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_llgl_test(cna_test_llgl_point_sampling
                  ${CNA_GRAPHICS_EXAMPLES_DIR}/point_sampling_contract_test.cpp)
    cna_register_renderer_test(NAME Llgl_PointSamplingContract COMMAND cna_test_llgl_point_sampling
        TIMEOUT 300 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_llgl_test(cna_test_llgl_colorspace_midtone
                  ${CNA_GRAPHICS_EXAMPLES_DIR}/colorspace_midtone_contract_test.cpp)
    cna_register_renderer_test(NAME Llgl_ColorSpace_MidTone COMMAND cna_test_llgl_colorspace_midtone
        TIMEOUT 120 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-116 cross-renderer control: every deferred draw must execute under the
    # GraphicsDevice.Viewport active at its own public call.
    cna_llgl_test(cna_test_llgl_deferred_viewport
                  ${CNA_GRAPHICS_EXAMPLES_DIR}/deferred_viewport_capture_test.cpp)
    cna_register_renderer_test(NAME Llgl_Deferred_Viewport COMMAND cna_test_llgl_deferred_viewport
        TIMEOUT 120 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
    # LLGL-51 (2026-08-04): a leftover GL_SCISSOR_TEST rectangle from the frame's last
    # sub-viewport draw was silently clipping CaptureBackbuffer()'s own internal glBlitFramebuffer
    # call to that draw's own small rectangle -- fixed by widening the scissor box to the full
    # resolution immediately before the copy. F2/F3 (the only two failures this file's own
    # backbuffer-after-an-off-screen-bind legs had) now pass: 35/39 -> 37/39. The remaining two
    # (E1/E2) are a genuinely separate, pre-existing, DECLARED limitation ("this rasterizer has no
    # viewport depth remap"), not silently masked -- kept registered as a real regression
    # trip-wire, matching this project's own established partial-pass-suite precedent.
    cna_register_renderer_test(NAME Llgl_Deferred_Viewport_OpenGL COMMAND cna_test_llgl_deferred_viewport
        TIMEOUT 120 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY};CNA_LLGL_RENDERER=opengl")

    # REMED-GFX-146 cross-renderer control: every deferred draw must execute under the
    # GraphicsDevice.ScissorRectangle and RasterizerState.ScissorTestEnable active at its own public
    # call.
    cna_llgl_test(cna_test_llgl_deferred_scissor
                  ${CNA_GRAPHICS_EXAMPLES_DIR}/deferred_scissor_capture_test.cpp)
    cna_register_renderer_test(NAME Llgl_Deferred_Scissor COMMAND cna_test_llgl_deferred_scissor
        TIMEOUT 120 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
    # LLGL-51 (2026-08-04): same fix as Llgl_Deferred_Viewport_OpenGL above -- 43/47 -> 47/47, a
    # full pass.
    cna_register_renderer_test(NAME Llgl_Deferred_Scissor_OpenGL COMMAND cna_test_llgl_deferred_scissor
        TIMEOUT 120 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY};CNA_LLGL_RENDERER=opengl")

    # LLGL-50: 15/17 legs now pass (up from 8/17) now that FixedHeightDynamicWidth's own logical
    # width is a floor, not a hard override -- this file's 72x36 backbuffer request is fully
    # addressable now. E1/E2 remain unregistered: both need a RenderTargetCube, and this sandbox's
    # OpenGL module has no cube-texture support at all (the same pre-existing, unrelated
    # `hasCubeTextures` gap `bound_target_lifetime_test.cpp`'s own F1 hits).
    cna_llgl_test(cna_test_llgl_deferred_source_lifetime
                  ${CNA_GRAPHICS_EXAMPLES_DIR}/deferred_source_lifetime_test.cpp)
    foreach(_llgl_dsl_leg A1 A2 A3 B1 B2 C1 D1 F1 G1 H1 I1 J1 K1 L1 N1)
        cna_register_renderer_test(NAME "Llgl_DeferredSourceLifetime_${_llgl_dsl_leg}"
            COMMAND cna_test_llgl_deferred_source_lifetime --leg=${_llgl_dsl_leg}
            TIMEOUT 90 LABELS "Llgl"
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
    endforeach()

    # REMED-GFX-157: a stock 3D draw issued AFTER a SpriteBatch inside ONE render-target bind cycle
    # must execute after it rather than be dropped.
    cna_llgl_test(cna_test_llgl_spritebatch_3d_order
                  ${CNA_GRAPHICS_EXAMPLES_DIR}/spritebatch_3d_order_test.cpp)
    cna_register_renderer_test(NAME Llgl_SpriteBatch3DOrder COMMAND cna_test_llgl_spritebatch_3d_order
        TIMEOUT 300 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-129: Clear() is an ORDERED command at its exact public call position, not a hint
    # for the next render-pass load action.
    cna_llgl_test(cna_test_llgl_ordered_clear
                  ${CNA_GRAPHICS_EXAMPLES_DIR}/graphicsdevice_ordered_clear_test.cpp)
    cna_register_renderer_test(NAME Llgl_GraphicsDevice_OrderedClear COMMAND cna_test_llgl_ordered_clear
        TIMEOUT 120 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # LLGL-46: W3's 12 former failures (Entry::BufferPrimitives/BufferPrimitivesRange/BufferIndexed/
    # BufferIndexedRange, each reusing the SAME persistent VertexBuffer/IndexBuffer object for two
    # draws within one frame) are fixed -- LlglVertexBufferRenderer::SetData()/
    # LlglIndexBufferRenderer::Upload() now flush any pending frame (a no-op when nothing is queued)
    # before writing in place or reallocating, so an earlier queued draw always replays against the
    # content it actually had when queued. Confirmed by a fresh 127/127 run.
    cna_llgl_test(cna_test_llgl_frontface_winding
                  ${CNA_GRAPHICS_EXAMPLES_DIR}/frontface_winding_test.cpp)
    cna_register_renderer_test(NAME Llgl_FrontFaceWinding COMMAND cna_test_llgl_frontface_winding
        TIMEOUT 120 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # LLGL-46 focused regression: growing a persistent VertexBuffer/IndexBuffer mid-frame (a
    # SECOND SetData() exceeding the byte capacity the FIRST one allocated) must not free the old
    # LLGL::Buffer an earlier, still-queued draw holds by raw pointer -- see this file's own header
    # comment and known_bugs.md's now-fixed entry for the reverted fix attempt that first
    # reproduced this exact crash shape elsewhere in this renderer.
    cna_llgl_test(cna_test_llgl_vertexindexbuffer_grow
                  llgl_vertexindexbuffer_grow_test.cpp)
    cna_register_renderer_test(NAME Llgl_VertexIndexBuffer_Grow COMMAND cna_test_llgl_vertexindexbuffer_grow
        TIMEOUT 90 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # LLGL-49: ApplySamplerState() now tracks 5 slots independently (0..4, matching the Vulkan
    # renderer's own slotSamplers_[]/PbrSlotSamplersRawEXT() convention) instead of a single set of
    # member variables slot 0 alone wrote. M3 (slot 1's Linear filter alone breaking block
    # uniformity) now passes, confirmed by a fresh run with zero failures.
    cna_llgl_test(cna_test_llgl_stock_effect_sampler
                  ${CNA_GRAPHICS_EXAMPLES_DIR}/stock_effect_sampler_contract_test.cpp)
    cna_register_renderer_test(NAME Llgl_StockEffectSampler COMMAND cna_test_llgl_stock_effect_sampler
        TIMEOUT 120 LABELS "Llgl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # The validated CNA LLGL runtime is OpenGL, whose pinned LLGL module exposes no usable cube
    # sampling/RenderTargetCube path here. Plain TextureCube keeps exact transfer-only CPU storage
    # (covered by both CubeVolume suites); keep the historical positive rendering binaries
    # build-covered, but do not execute them as supported-path tests.
    set_tests_properties(
        Llgl_EnvironmentMapEffect_AlphaScaledLerp
        Llgl_RenderTargetCube
        Llgl_Msaa_RenderTargetCube
        Llgl_Mip_RenderTargetCube
        Llgl_RenderTargetCube_GetDataContract
        Llgl_RenderTargetCube_Usage
        Llgl_RenderTargetCube_MsaaFace
        Llgl_RenderTargetCube_PluralBinding
        PROPERTIES DISABLED TRUE)

endif()

# Module-local tests get the same headless-safe skip convention the root one-shot
# gives root-registered tests (see cmake/TestHelpers.cmake).
cna_apply_skip_convention()
