# opengl1 renderer example/test registration (module-local; this file is the
# former cmake/Tests/OpenGL1Tests.cmake, moved when examples moved into their owning
# physical modules).
#
# From the root CMakeLists.txt include site:
# plans/plan_opengl1.md: OPENGL1 (native legacy fixed-function OpenGL) CTest registration --
# tests/opengl1/README.md's 8 priority smoke-coverage scenarios.

# Example/test executables historically live at the build root (scripts, docs and
# the golden-image working-directory conventions rely on it); keep them there.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
# Shared example-support headers ("common/PixelTestGame.hpp", ...) are owned by
# the graphics module's examples directory.
include_directories(${CNA_GRAPHICS_EXAMPLES_DIR})

# tests/opengl1/README.md's "priority runtime smoke coverage" list, wired to real CTest
# registrations. Reuses already-verified, renderer-agnostic example sources (real public
# Game/GraphicsDevice/BasicEffect/SpriteBatch/AlphaTestEffect API only, no EasyGL-specific
# code -- confirmed by inspection before reuse here) wherever OPENGL1's documented fixed-
# function capability (plans/plan_opengl1.md "Implemented foundation"/"Intentional OpenGL 1.x
# limitations") genuinely matches what the shared source exercises. Only scenario 7 (fog +
# alpha-test) gets an OPENGL1-specific file: the shared EasyGL fog/alpha-test tests pin exact
# blended RGB values tuned for a different (shader-exact) formula, which OpenGL1's real
# fixed-function GL_FOG/glAlphaFunc "coarse approximation" is not expected to reproduce
# bit-for-bit -- see opengl1_fog_alphatest_test.cpp's own header for the full
# rationale.
#
# Gated the same way as SdlRendererTests.cmake (NOT EMSCRIPTEN AND NOT WIN32): OPENGL1 is a
# real desktop-only renderer (no Emscripten/WebGL target), and native Windows ctest execution
# for this project's Linux CI/dev-sandbox is out of scope here, same as every other
# non-Windows-only renderer's own test file.
if(CNA_BUILD_TESTS AND NOT EMSCRIPTEN AND NOT WIN32
   AND CNA_GRAPHICS_RENDERER STREQUAL "OPENGL1")

    enable_testing()

    # --- helper macro: build a headless OPENGL1 test exe --------------------
    macro(cna_opengl1_test target src)
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

    # Scenario 1: clear + present, plus PixelTestGame's tolerance path.
    cna_opengl1_test(cna_test_opengl1_pixeltestgame_smoke
                      ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_pixeltestgame_smoke_test.cpp)
    cna_register_renderer_test(NAME OpenGL1_PixelTestGame_Smoke COMMAND cna_test_opengl1_pixeltestgame_smoke
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Scenario 2: SpriteBatch textured quad.
    cna_opengl1_test(cna_test_opengl1_textured_quad
                      ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_textured_quad_test.cpp)
    cna_register_renderer_test(NAME OpenGL1_TexturedQuad_Readback COMMAND cna_test_opengl1_textured_quad
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Scenario 3: colored 3D triangles with depth -- also directly exercises
    # ApplyRasterizerState's CullMode->glCullFace mapping (the bug fixed alongside this test
    # suite: every SpriteBatch quad was being back-face culled before the fix).
    cna_opengl1_test(cna_test_opengl1_rasterizerstate_cullmode
                      ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_rasterizerstate_cullmode_test.cpp)
    cna_register_renderer_test(NAME OpenGL1_RasterizerState_CullMode COMMAND cna_test_opengl1_rasterizerstate_cullmode
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Scenario 4: textured 3D triangle via BasicEffect.TextureEnabled (VertexPositionTexture,
    # stride 20) -- also directly exercises the DrawInternal stride==20 diffuseColor fix
    # (previously hardcoded glColor4f(1,1,1,1), silently ignoring BasicEffect.DiffuseColor).
    cna_opengl1_test(cna_test_opengl1_basiceffect_texture_enabled
                      ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_basiceffect_texture_enabled_test.cpp)
    cna_register_renderer_test(NAME OpenGL1_BasicEffect_TextureEnabled COMMAND cna_test_opengl1_basiceffect_texture_enabled
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Scenarios 5+6: indexed VertexBuffer/IndexBuffer draw (the real Model/ModelMesh::Draw()
    # code path) with a real perspective camera, depth, CullMode contrast, and
    # BasicEffect.EnableDefaultLighting() on VertexPositionNormalTexture (fixed-function
    # directional lighting).
    cna_opengl1_test(cna_test_opengl1_rasterizerstate_cullmode_indexed_basiceffect
                      ${CNA_GRAPHICS_EXAMPLES_DIR}/rasterizerstate_cullmode_indexed_basiceffect_test.cpp)
    cna_register_renderer_test(NAME OpenGL1_RasterizerState_CullMode_IndexedBasicEffect COMMAND cna_test_opengl1_rasterizerstate_cullmode_indexed_basiceffect
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Scenario 7: fog + alpha-test approximations (OPENGL1-specific -- see file header).
    cna_opengl1_test(cna_test_opengl1_fog_alphatest
                      opengl1_fog_alphatest_test.cpp)
    cna_register_renderer_test(NAME OpenGL1_Fog_AlphaTest COMMAND cna_test_opengl1_fog_alphatest
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Scenario 8: scissor + stencil state.
    cna_opengl1_test(cna_test_opengl1_scissor
                      ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_scissor_test.cpp)
    cna_register_renderer_test(NAME OpenGL1_Scissor COMMAND cna_test_opengl1_scissor
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_opengl1_test(cna_test_opengl1_depthstencilstate_stencil_ops
                      ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_depthstencilstate_stencil_ops_test.cpp)
    cna_register_renderer_test(NAME OpenGL1_DepthStencilState_StencilOps COMMAND cna_test_opengl1_depthstencilstate_stencil_ops
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plans/plan_opengl1.md phase 1: runtime GL version/extension discovery -- verifies the real
    # GL_EXT_texture_filter_anisotropic wiring in ApplySamplerState() reaches the driver.
    cna_opengl1_test(cna_test_opengl1_anisotropic_gl_state
                      opengl1_anisotropic_gl_state_test.cpp)
    cna_register_renderer_test(NAME OpenGL1_Anisotropic_GlState COMMAND cna_test_opengl1_anisotropic_gl_state
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plans/plan_opengl1.md phase 2: RenderTarget2D via ARB_framebuffer_object/core FBO. Reused verbatim
    # from the SDL_Renderer renderer's own Task 705 test (renderer-agnostic: real public
    # RenderTarget2D/GraphicsDevice/SpriteBatch API only) -- its corner-marker check is exactly
    # the round-trip-orientation proof this phase needs (a render target sampled later as a plain
    # Texture2D must not come out vertically flipped).
    cna_opengl1_test(cna_test_opengl1_rendertarget2d_sample
                      ${CMAKE_SOURCE_DIR}/modules/renderers/sdl-renderer/examples/sdlrenderer_rendertarget2d_sample_test.cpp)
    cna_register_renderer_test(NAME OpenGL1_RenderTarget2D_Sample COMMAND cna_test_opengl1_rendertarget2d_sample
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plans/plan_opengl1.md phase 3: ARB_multitexture/core-1.3 DualTextureEffect fixed-function path.
    # Reused verbatim from EasyGL's own Task 383 test (renderer-agnostic) -- isolates FNA's real
    # "texture0.rgb *= 2" doubling factor from a naive texture0*texture1*diffuse multiply, which
    # the GL_COMBINE/GL_RGB_SCALE=2 texture-environment chain in DrawInternal must reproduce.
    cna_opengl1_test(cna_test_opengl1_dualtextureeffect_doubling
                      ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_dualtextureeffect_doubling_test.cpp)
    cna_register_renderer_test(NAME OpenGL1_DualTextureEffect_Doubling COMMAND cna_test_opengl1_dualtextureeffect_doubling
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plans/plan_opengl1.md phase 11: explicit GraphicsCapability reporting, backed by real evidence
    # (not just the flag) for every checked value.
    cna_opengl1_test(cna_test_opengl1_graphics_capability
                      opengl1_graphics_capability_test.cpp)
    cna_register_renderer_test(NAME OpenGL1_GraphicsCapability COMMAND cna_test_opengl1_graphics_capability
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plans/plan_opengl1.md phase 4: fixed-function texture environment/combine mappings, closing the
    # gap DrawInternal's stride==16/24 vertex-color paths had (vertex color used alone, ignoring
    # BasicEffect's material DiffuseColor/EmissiveColor entirely). Reused verbatim from EasyGL's
    # own Task 370 capstone test (renderer-agnostic) -- asserts the exact multiplicative formula
    # TextureColor x VertexColor x (DiffuseColor+EmissiveColor) across 4 independent texels.
    cna_opengl1_test(cna_test_opengl1_basiceffect_combined
                      ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_basiceffect_combined_test.cpp)
    cna_register_renderer_test(NAME OpenGL1_BasicEffect_Combined COMMAND cna_test_opengl1_basiceffect_combined
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plans/plan_opengl1.md phase 5: ARB_texture_cube_map cube maps + EnvironmentMapEffect's fixed-
    # function reflection-mapping subset (OPENGL1-specific -- see the test file's own header for
    # why Fresnel/specular are deliberately out of scope for a fixed-function-only renderer).
    cna_opengl1_test(cna_test_opengl1_environmentmapeffect
                      opengl1_environmentmapeffect_test.cpp)
    cna_register_renderer_test(NAME OpenGL1_EnvironmentMapEffect COMMAND cna_test_opengl1_environmentmapeffect
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plans/plan_opengl1.md phase 10: visual golden-image tests, reusing the EXACT same checked-in
    # reference PNGs EasyGL's own golden-image suite uses (examples/golden/*.png) -- no new
    # images. Same precedent as Vulkan's own golden-image reuse (VulkanTests.cmake): the shared
    # PixelTestGame::CompareGoldenImage() helper and every one of these .cpp files is renderer-
    # agnostic (real public Game/GraphicsDevice/Effect/SpriteBatch API only). Deliberately
    # excluded: PbrEffect/SkinnedEffect/SkinnedPbrEffect (GLSL-shader-only, no fixed-function
    # equivalent -- plans/plan_opengl1.md's own design rule) and EnvironmentMapEffect (its golden scene
    # exercises Fresnel/specular, which plans/plan_opengl1.md phase 5 documents as an intentional,
    # unimplemented fixed-function limitation -- a guaranteed mismatch, not worth a test that can
    # never pass). WORKING_DIRECTORY is the repo root so each test's relative
    # "examples/golden/*.png" path resolves correctly regardless of which cmake-build-* directory
    # this runs from (CMake's default test CWD is the build directory, not the source tree).
    cna_opengl1_test(cna_test_opengl1_goldenimage_smoke
                      ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_goldenimage_smoke_test.cpp)
    cna_register_renderer_test(NAME OpenGL1_GoldenImage_Smoke COMMAND cna_test_opengl1_goldenimage_smoke
        TIMEOUT 30 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_opengl1_test(cna_test_opengl1_basiceffect_golden
                      ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_basiceffect_golden_test.cpp)
    cna_register_renderer_test(NAME OpenGL1_BasicEffect_Golden COMMAND cna_test_opengl1_basiceffect_golden
        TIMEOUT 30 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_opengl1_test(cna_test_opengl1_spritebatch_rotation_golden
                      ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_spritebatch_rotation_golden_test.cpp)
    cna_register_renderer_test(NAME OpenGL1_SpriteBatch_Rotation_Golden COMMAND cna_test_opengl1_spritebatch_rotation_golden
        TIMEOUT 30 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_opengl1_test(cna_test_opengl1_texture_filter_linear_golden
                      ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_texture_filter_linear_golden_test.cpp)
    cna_register_renderer_test(NAME OpenGL1_TextureFilter_Linear_Golden COMMAND cna_test_opengl1_texture_filter_linear_golden
        TIMEOUT 30 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_opengl1_test(cna_test_opengl1_blendstate_additive_golden
                      ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_blendstate_additive_golden_test.cpp)
    cna_register_renderer_test(NAME OpenGL1_BlendState_Additive_Golden COMMAND cna_test_opengl1_blendstate_additive_golden
        TIMEOUT 30 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_opengl1_test(cna_test_opengl1_dualtextureeffect_golden
                      ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_dualtextureeffect_golden_test.cpp)
    cna_register_renderer_test(NAME OpenGL1_DualTextureEffect_Golden COMMAND cna_test_opengl1_dualtextureeffect_golden
        TIMEOUT 30 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_opengl1_test(cna_test_opengl1_rasterizerstate_cullmode_golden
                      ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_rasterizerstate_cullmode_golden_test.cpp)
    cna_register_renderer_test(NAME OpenGL1_RasterizerState_CullMode_Golden COMMAND cna_test_opengl1_rasterizerstate_cullmode_golden
        TIMEOUT 30 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_opengl1_test(cna_test_opengl1_depthstencilstate_write_enable_golden
                      ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_depthstencilstate_write_enable_golden_test.cpp)
    cna_register_renderer_test(NAME OpenGL1_DepthStencilState_WriteEnable_Golden COMMAND cna_test_opengl1_depthstencilstate_write_enable_golden
        TIMEOUT 30 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # AlphaTestEffect's golden scene uses CompareFunction::Greater with alpha=192 clearly above
    # ReferenceAlpha=128 (not at the boundary) -- within the "clearly separated from the
    # reference" honest subset OPENGL1's glAlphaFunc(GL_GEQUAL,...) coarse approximation can
    # actually reproduce (see opengl1_fog_alphatest_test.cpp's own header for the full
    # rationale on why an exact-boundary/other-CompareFunction case would not be honest to reuse).
    cna_opengl1_test(cna_test_opengl1_alphatesteffect_golden
                      ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_alphatesteffect_golden_test.cpp)
    cna_register_renderer_test(NAME OpenGL1_AlphaTestEffect_Golden COMMAND cna_test_opengl1_alphatesteffect_golden
        TIMEOUT 30 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plans/plan_opengl1.md phase 8: context-loss resource recreation registry (OpenGL1ResourceRegistry/
    # IOpenGL1Recoverable, independent of EasyGL's own ::easygl::ResourceRegistry).
    cna_opengl1_test(cna_test_opengl1_context_loss
                      opengl1_context_loss_test.cpp)
    cna_register_renderer_test(NAME OpenGL1_ContextLoss COMMAND cna_test_opengl1_context_loss
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plans/plan_opengl1.md phase 6: automatic mipmap generation (glGenerateMipmap/GL_GENERATE_MIPMAP/
    # CPU box-filter fallback) plus the full 9-value TextureFilter -> GL min/mag mapping needed to
    # actually sample the generated levels.
    cna_opengl1_test(cna_test_opengl1_mipmap_generation
                      opengl1_mipmap_generation_test.cpp)
    cna_register_renderer_test(NAME OpenGL1_MipmapGeneration COMMAND cna_test_opengl1_mipmap_generation
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # SamplerState ordering (ApplySamplerState fired before BindGL) + per-slot (slot param
    # ignored) bugs found and fixed while implementing phase 6's mip-aware filtering.
    cna_opengl1_test(cna_test_opengl1_samplerstate_bind_order
                      opengl1_samplerstate_bind_order_test.cpp)
    cna_register_renderer_test(NAME OpenGL1_SamplerState_BindOrder COMMAND cna_test_opengl1_samplerstate_bind_order
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plans/plan_opengl1.md item 20 (EasyGL parity): runtime SetSwapInterval() override.
    cna_opengl1_test(cna_test_opengl1_swapinterval
                      opengl1_swapinterval_test.cpp)
    cna_register_renderer_test(NAME OpenGL1_SwapInterval COMMAND cna_test_opengl1_swapinterval
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plans/plan_opengl1.md item 21 (EasyGL parity): RenderTarget2D mip-chain generation on unbind.
    cna_opengl1_test(cna_test_opengl1_rendertarget2d_mip
                      opengl1_rendertarget2d_mip_test.cpp)
    cna_register_renderer_test(NAME OpenGL1_RenderTarget2D_Mip COMMAND cna_test_opengl1_rendertarget2d_mip
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plans/plan_opengl1.md item 22 (EasyGL parity): backbuffer MSAA.
    cna_opengl1_test(cna_test_opengl1_msaa
                      opengl1_msaa_test.cpp)
    cna_register_renderer_test(NAME OpenGL1_MSAA COMMAND cna_test_opengl1_msaa
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plans/plan_opengl1.md item 14 (EasyGL parity): BasicEffect.DirectionalLight1/DirectionalLight2.
    cna_opengl1_test(cna_test_opengl1_directionallight12
                      opengl1_directionallight12_test.cpp)
    cna_register_renderer_test(NAME OpenGL1_DirectionalLight12 COMMAND cna_test_opengl1_directionallight12
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plans/plan_opengl1.md item 15 (EasyGL parity): BasicEffect specular highlights.
    cna_opengl1_test(cna_test_opengl1_specular
                      opengl1_specular_test.cpp)
    cna_register_renderer_test(NAME OpenGL1_Specular COMMAND cna_test_opengl1_specular
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plans/plan_opengl1.md item 16 (EasyGL parity): TextureAddressMode.Mirror.
    cna_opengl1_test(cna_test_opengl1_textureaddressmode_mirror
                      opengl1_textureaddressmode_mirror_test.cpp)
    cna_register_renderer_test(NAME OpenGL1_TextureAddressMode_Mirror COMMAND cna_test_opengl1_textureaddressmode_mirror
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plans/plan_opengl1.md item 17 (EasyGL parity): constant blend color.
    cna_opengl1_test(cna_test_opengl1_blendfactor
                      opengl1_blendfactor_test.cpp)
    cna_register_renderer_test(NAME OpenGL1_BlendFactor COMMAND cna_test_opengl1_blendfactor
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plans/plan_opengl1.md item 18 (EasyGL parity): blend equation beyond additive.
    cna_opengl1_test(cna_test_opengl1_blendequation
                      opengl1_blendequation_test.cpp)
    cna_register_renderer_test(NAME OpenGL1_BlendEquation COMMAND cna_test_opengl1_blendequation
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plans/plan_opengl1.md item 19 (EasyGL parity): separate alpha blend factors.
    cna_opengl1_test(cna_test_opengl1_blendalphafactor
                      opengl1_blendalphafactor_test.cpp)
    cna_register_renderer_test(NAME OpenGL1_BlendAlphaFactor COMMAND cna_test_opengl1_blendalphafactor
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plans/plan_opengl1.md item 13 (EasyGL parity): virtual-resolution/presentation-mode scaling.
    cna_opengl1_test(cna_test_opengl1_presentationmode
                      opengl1_presentationmode_test.cpp)
    cna_register_renderer_test(NAME OpenGL1_PresentationMode COMMAND cna_test_opengl1_presentationmode
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plans/plan_opengl1.md item 24 (EasyGL parity): RenderTargetCube support.
    cna_opengl1_test(cna_test_opengl1_rendertargetcube
                      opengl1_rendertargetcube_test.cpp)
    cna_register_renderer_test(NAME OpenGL1_RenderTargetCube COMMAND cna_test_opengl1_rendertargetcube
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plans/plan_opengl1.md item 25: RenderTarget2D MSAA.
    cna_opengl1_test(cna_test_opengl1_rendertarget2d_msaa
                      opengl1_rendertarget2d_msaa_test.cpp)
    cna_register_renderer_test(NAME OpenGL1_RenderTarget2D_MSAA COMMAND cna_test_opengl1_rendertarget2d_msaa
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

endif()

# Module-local tests get the same headless-safe skip convention the root one-shot
# gives root-registered tests (see cmake/TestHelpers.cmake).
cna_apply_skip_convention()
