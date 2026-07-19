# tests/opengl1/README.md's "priority runtime smoke coverage" list, wired to real CTest
# registrations. Reuses already-verified, backend-agnostic example sources (real public
# Game/GraphicsDevice/BasicEffect/SpriteBatch/AlphaTestEffect API only, no EasyGL-specific
# code -- confirmed by inspection before reuse here) wherever OPENGL1's documented fixed-
# function capability (plan_opengl1.md "Implemented foundation"/"Intentional OpenGL 1.x
# limitations") genuinely matches what the shared source exercises. Only scenario 7 (fog +
# alpha-test) gets an OPENGL1-specific file: the shared EasyGL fog/alpha-test tests pin exact
# blended RGB values tuned for a different (shader-exact) formula, which OpenGL1's real
# fixed-function GL_FOG/glAlphaFunc "coarse approximation" is not expected to reproduce
# bit-for-bit -- see examples/opengl1_fog_alphatest_test.cpp's own header for the full
# rationale.
#
# Gated the same way as SdlRendererTests.cmake (NOT EMSCRIPTEN AND NOT WIN32): OPENGL1 is a
# real desktop-only backend (no Emscripten/WebGL target), and native Windows ctest execution
# for this project's Linux CI/dev-sandbox is out of scope here, same as every other
# non-Windows-only backend's own test file.
if(CNA_BUILD_TESTS AND NOT EMSCRIPTEN AND NOT WIN32
   AND CNA_GRAPHICS_BACKEND STREQUAL "OPENGL1")

    enable_testing()

    # --- helper macro: build a headless OPENGL1 test exe --------------------
    macro(cna_opengl1_test target src)
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

    # Scenario 1: clear + present, plus PixelTestGame's tolerance path.
    cna_opengl1_test(cna_test_opengl1_pixeltestgame_smoke
                      examples/easygl_pixeltestgame_smoke_test.cpp)
    cna_register_backend_test(NAME OpenGL1_PixelTestGame_Smoke COMMAND cna_test_opengl1_pixeltestgame_smoke
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Scenario 2: SpriteBatch textured quad.
    cna_opengl1_test(cna_test_opengl1_textured_quad
                      examples/easygl_textured_quad_test.cpp)
    cna_register_backend_test(NAME OpenGL1_TexturedQuad_Readback COMMAND cna_test_opengl1_textured_quad
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Scenario 3: colored 3D triangles with depth -- also directly exercises
    # ApplyRasterizerState's CullMode->glCullFace mapping (the bug fixed alongside this test
    # suite: every SpriteBatch quad was being back-face culled before the fix).
    cna_opengl1_test(cna_test_opengl1_rasterizerstate_cullmode
                      examples/easygl_rasterizerstate_cullmode_test.cpp)
    cna_register_backend_test(NAME OpenGL1_RasterizerState_CullMode COMMAND cna_test_opengl1_rasterizerstate_cullmode
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Scenario 4: textured 3D triangle via BasicEffect.TextureEnabled (VertexPositionTexture,
    # stride 20) -- also directly exercises the DrawInternal stride==20 diffuseColor fix
    # (previously hardcoded glColor4f(1,1,1,1), silently ignoring BasicEffect.DiffuseColor).
    cna_opengl1_test(cna_test_opengl1_basiceffect_texture_enabled
                      examples/easygl_basiceffect_texture_enabled_test.cpp)
    cna_register_backend_test(NAME OpenGL1_BasicEffect_TextureEnabled COMMAND cna_test_opengl1_basiceffect_texture_enabled
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Scenarios 5+6: indexed VertexBuffer/IndexBuffer draw (the real Model/ModelMesh::Draw()
    # code path) with a real perspective camera, depth, CullMode contrast, and
    # BasicEffect.EnableDefaultLighting() on VertexPositionNormalTexture (fixed-function
    # directional lighting).
    cna_opengl1_test(cna_test_opengl1_rasterizerstate_cullmode_indexed_basiceffect
                      examples/rasterizerstate_cullmode_indexed_basiceffect_test.cpp)
    cna_register_backend_test(NAME OpenGL1_RasterizerState_CullMode_IndexedBasicEffect COMMAND cna_test_opengl1_rasterizerstate_cullmode_indexed_basiceffect
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Scenario 7: fog + alpha-test approximations (OPENGL1-specific -- see file header).
    cna_opengl1_test(cna_test_opengl1_fog_alphatest
                      examples/opengl1_fog_alphatest_test.cpp)
    cna_register_backend_test(NAME OpenGL1_Fog_AlphaTest COMMAND cna_test_opengl1_fog_alphatest
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Scenario 8: scissor + stencil state.
    cna_opengl1_test(cna_test_opengl1_scissor
                      examples/easygl_scissor_test.cpp)
    cna_register_backend_test(NAME OpenGL1_Scissor COMMAND cna_test_opengl1_scissor
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_opengl1_test(cna_test_opengl1_depthstencilstate_stencil_ops
                      examples/easygl_depthstencilstate_stencil_ops_test.cpp)
    cna_register_backend_test(NAME OpenGL1_DepthStencilState_StencilOps COMMAND cna_test_opengl1_depthstencilstate_stencil_ops
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_opengl1.md phase 1: runtime GL version/extension discovery -- verifies the real
    # GL_EXT_texture_filter_anisotropic wiring in ApplySamplerState() reaches the driver.
    cna_opengl1_test(cna_test_opengl1_anisotropic_gl_state
                      examples/opengl1_anisotropic_gl_state_test.cpp)
    cna_register_backend_test(NAME OpenGL1_Anisotropic_GlState COMMAND cna_test_opengl1_anisotropic_gl_state
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_opengl1.md phase 2: RenderTarget2D via ARB_framebuffer_object/core FBO. Reused verbatim
    # from the SDL_Renderer backend's own Task 705 test (backend-agnostic: real public
    # RenderTarget2D/GraphicsDevice/SpriteBatch API only) -- its corner-marker check is exactly
    # the round-trip-orientation proof this phase needs (a render target sampled later as a plain
    # Texture2D must not come out vertically flipped).
    cna_opengl1_test(cna_test_opengl1_rendertarget2d_sample
                      examples/sdlrenderer_rendertarget2d_sample_test.cpp)
    cna_register_backend_test(NAME OpenGL1_RenderTarget2D_Sample COMMAND cna_test_opengl1_rendertarget2d_sample
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_opengl1.md phase 3: ARB_multitexture/core-1.3 DualTextureEffect fixed-function path.
    # Reused verbatim from EasyGL's own Task 383 test (backend-agnostic) -- isolates FNA's real
    # "texture0.rgb *= 2" doubling factor from a naive texture0*texture1*diffuse multiply, which
    # the GL_COMBINE/GL_RGB_SCALE=2 texture-environment chain in DrawInternal must reproduce.
    cna_opengl1_test(cna_test_opengl1_dualtextureeffect_doubling
                      examples/easygl_dualtextureeffect_doubling_test.cpp)
    cna_register_backend_test(NAME OpenGL1_DualTextureEffect_Doubling COMMAND cna_test_opengl1_dualtextureeffect_doubling
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_opengl1.md phase 11: explicit GraphicsCapability reporting, backed by real evidence
    # (not just the flag) for every checked value.
    cna_opengl1_test(cna_test_opengl1_graphics_capability
                      examples/opengl1_graphics_capability_test.cpp)
    cna_register_backend_test(NAME OpenGL1_GraphicsCapability COMMAND cna_test_opengl1_graphics_capability
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

endif()
