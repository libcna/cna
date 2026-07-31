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
endif()
