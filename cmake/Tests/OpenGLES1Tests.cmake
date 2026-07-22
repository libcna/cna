if(CNA_BUILD_TESTS AND NOT EMSCRIPTEN AND NOT WIN32
   AND CNA_GRAPHICS_BACKEND STREQUAL "OPENGLES1")
    enable_testing()

    macro(cna_opengles1_test target src)
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

    # plan_opengles1.md OPENGLES1-baseline: Clear()/Present(), SpriteBatch, and
    # DrawUserPrimitives(VertexPositionColor) through the real fixed-function pipeline. Needs a
    # real EGL/GLESv1_CM driver capable of creating an actual ES1 context -- see
    # docs/opengles1-backend.md.
    cna_opengles1_test(cna_test_opengles1_clear_readback examples/opengles1_clear_readback_test.cpp)
    cna_register_backend_test(NAME OpenGLES1_Clear_Readback COMMAND cna_test_opengles1_clear_readback
        TIMEOUT 30 LABELS "GraphicsSmoke;OpenGLES1" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_opengles1.md OPENGLES1-79: blend/depth/rasterizer-cull/sampler state through the real
    # fixed-function pipeline (OPENGLES1-25/26/27/28).
    cna_opengles1_test(cna_test_opengles1_render_state examples/opengles1_render_state_test.cpp)
    cna_register_backend_test(NAME OpenGLES1_RenderState_Readback COMMAND cna_test_opengles1_render_state
        TIMEOUT 30 LABELS "GraphicsSmoke;OpenGLES1" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_opengles1.md OPENGLES1-79: fixed-function shading -- directional lighting, linear fog
    # and the alpha test (OPENGLES1-22/23/24).
    cna_opengles1_test(cna_test_opengles1_lighting_fog_alphatest examples/opengles1_lighting_fog_alphatest_test.cpp)
    cna_register_backend_test(NAME OpenGLES1_LightingFogAlphaTest_Readback COMMAND cna_test_opengles1_lighting_fog_alphatest
        TIMEOUT 30 LABELS "GraphicsSmoke;OpenGLES1" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_opengles1.md OPENGLES1-79: real GPU vertex/index buffers, RenderTarget2D and wireframe
    # emulation (OPENGLES1-15/16/72/73/76).
    cna_opengles1_test(cna_test_opengles1_buffers_rendertarget examples/opengles1_buffers_rendertarget_test.cpp)
    cna_register_backend_test(NAME OpenGLES1_BuffersRenderTarget_Readback COMMAND cna_test_opengles1_buffers_rendertarget
        TIMEOUT 30 LABELS "GraphicsSmoke;OpenGLES1" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_opengles1.md OPENGLES1-79: dual texture, environment map and context loss/restore
    # (OPENGLES1-71/74/11).
    cna_opengles1_test(cna_test_opengles1_multitexture_contextloss examples/opengles1_multitexture_contextloss_test.cpp)
    cna_register_backend_test(NAME OpenGLES1_MultitextureContextLoss_Readback COMMAND cna_test_opengles1_multitexture_contextloss
        TIMEOUT 30 LABELS "GraphicsSmoke;OpenGLES1" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_opengles1.md OPENGLES1-79: viewport and scissor clipping (OPENGLES1-29).
    cna_opengles1_test(cna_test_opengles1_viewport_scissor examples/opengles1_viewport_scissor_test.cpp)
    cna_register_backend_test(NAME OpenGLES1_ViewportScissor_Readback COMMAND cna_test_opengles1_viewport_scissor
        TIMEOUT 30 LABELS "GraphicsSmoke;OpenGLES1" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
endif()
