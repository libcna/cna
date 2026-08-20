# opengles1 renderer example/test registration (module-local; this file is the
# former cmake/Tests/OpenGLES1Tests.cmake, moved when examples moved into their owning
# physical modules).
#
# From the root CMakeLists.txt include site:
# plans/plan_opengles1.md: OPENGLES1 (fixed-function OpenGL ES 1.1) CTest registration. Needs a real
# EGL/GLESv1_CM driver capable of creating an actual ES1 context -- not every EGL implementation
# does (see docs/opengles1-renderer.md's own empirical finding on this project's Mesa/llvmpipe
# development host), same "run with a real DISPLAY and a real driver" caveat as the Vulkan/Bgfx/
# D3D11/SDL_GPU entries above.

# Example/test executables historically live at the build root (scripts, docs and
# the golden-image working-directory conventions rely on it); keep them there.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
# Shared example-support headers ("common/PixelTestGame.hpp", ...) are owned by
# the graphics module's examples directory.
include_directories(${CNA_GRAPHICS_EXAMPLES_DIR})

if(CNA_BUILD_TESTS AND NOT EMSCRIPTEN AND NOT WIN32
   AND CNA_GRAPHICS_RENDERER STREQUAL "OPENGLES1")
    enable_testing()

    macro(cna_opengles1_test target src)
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

    # plans/plan_opengles1.md OPENGLES1-baseline: Clear()/Present(), SpriteBatch, and
    # DrawUserPrimitives(VertexPositionColor) through the real fixed-function pipeline. Needs a
    # real EGL/GLESv1_CM driver capable of creating an actual ES1 context -- see
    # docs/opengles1-renderer.md.
    cna_opengles1_test(cna_test_opengles1_clear_readback opengles1_clear_readback_test.cpp)
    cna_register_renderer_test(NAME OpenGLES1_Clear_Readback COMMAND cna_test_opengles1_clear_readback
        TIMEOUT 30 LABELS "GraphicsSmoke;OpenGLES1" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plans/plan_opengles1.md OPENGLES1-79: blend/depth/rasterizer-cull/sampler state through the real
    # fixed-function pipeline (OPENGLES1-25/26/27/28).
    cna_opengles1_test(cna_test_opengles1_render_state opengles1_render_state_test.cpp)
    cna_register_renderer_test(NAME OpenGLES1_RenderState_Readback COMMAND cna_test_opengles1_render_state
        TIMEOUT 30 LABELS "GraphicsSmoke;OpenGLES1" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plans/plan_opengles1.md OPENGLES1-79: fixed-function shading -- directional lighting, linear fog
    # and the alpha test (OPENGLES1-22/23/24).
    cna_opengles1_test(cna_test_opengles1_lighting_fog_alphatest opengles1_lighting_fog_alphatest_test.cpp)
    cna_register_renderer_test(NAME OpenGLES1_LightingFogAlphaTest_Readback COMMAND cna_test_opengles1_lighting_fog_alphatest
        TIMEOUT 30 LABELS "GraphicsSmoke;OpenGLES1" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plans/plan_opengles1.md OPENGLES1-79: real GPU vertex/index buffers, RenderTarget2D and wireframe
    # emulation (OPENGLES1-15/16/72/73/76).
    cna_opengles1_test(cna_test_opengles1_buffers_rendertarget opengles1_buffers_rendertarget_test.cpp)
    cna_register_renderer_test(NAME OpenGLES1_BuffersRenderTarget_Readback COMMAND cna_test_opengles1_buffers_rendertarget
        TIMEOUT 30 LABELS "GraphicsSmoke;OpenGLES1" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plans/plan_opengles1.md OPENGLES1-79: dual texture, environment map and context loss/restore
    # (OPENGLES1-71/74/11).
    cna_opengles1_test(cna_test_opengles1_multitexture_contextloss opengles1_multitexture_contextloss_test.cpp)
    cna_register_renderer_test(NAME OpenGLES1_MultitextureContextLoss_Readback COMMAND cna_test_opengles1_multitexture_contextloss
        TIMEOUT 30 LABELS "GraphicsSmoke;OpenGLES1" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plans/plan_opengles1.md OPENGLES1-79: virtual resolution, coordinate transforms, the direct
    # depth/blend toggles and the SpriteBatch transform (OPENGLES1-9/10/13/18/20).
    cna_opengles1_test(cna_test_opengles1_resolution_statetoggles opengles1_resolution_statetoggles_test.cpp)
    cna_register_renderer_test(NAME OpenGLES1_ResolutionStateToggles_Readback COMMAND cna_test_opengles1_resolution_statetoggles
        TIMEOUT 30 LABELS "GraphicsSmoke;OpenGLES1" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plans/plan_opengles1.md OPENGLES1-78: the same ${CMAKE_SOURCE_DIR}/tools/xna-oracle/CnaOracleRender.cpp the D3D9 and
    # EasyGL branches already build, linked against this renderer, so the checked-in scene corpus can
    # be rendered through real ES 1.1 and diffed against the real XNA 4.0 reference images by
    # scripts/run-oracle-corpus-diff-opengles1.sh. Not registered as a CTest for the same reason
    # cna_oracle_render_easygl is not: it is a comparison tool with no pass/fail assertion of its
    # own -- xna-diff.py is what judges its output.
    cna_opengles1_test(cna_oracle_render_opengles1 ${CMAKE_SOURCE_DIR}/tools/xna-oracle/CnaOracleRender.cpp)

    # plans/plan_opengles1.md OPENGLES1-79: viewport and scissor clipping (OPENGLES1-29).
    cna_opengles1_test(cna_test_opengles1_viewport_scissor opengles1_viewport_scissor_test.cpp)
    cna_register_renderer_test(NAME OpenGLES1_ViewportScissor_Readback COMMAND cna_test_opengles1_viewport_scissor
        TIMEOUT 30 LABELS "GraphicsSmoke;OpenGLES1" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
endif()

# Module-local tests get the same headless-safe skip convention the root one-shot
# gives root-registered tests (see cmake/TestHelpers.cmake).
cna_apply_skip_convention()
