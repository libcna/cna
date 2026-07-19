if(CNA_BUILD_TESTS AND CNA_GRAPHICS_BACKEND STREQUAL "OPENGL2")
    enable_testing()

    macro(cna_opengl2_test target src)
        add_executable(${target} ${src})
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(${target} PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME SDL3::SDL3)
        else()
            target_link_libraries(${target} PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(${target} PRIVATE SDL3::SDL3main)
        endif()
    endmacro()

    # plan_opengl2.md: device/window/GL-context lifecycle + color/depth/stencil clear/present.
    cna_opengl2_test(cna_test_opengl2_smoke examples/opengl2_smoke_test.cpp)
    cna_register_backend_test(NAME OpenGL2_Smoke COMMAND cna_test_opengl2_smoke
        TIMEOUT 60 LABELS "OpenGL2" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_opengl2.md: 2D vertical slice -- real Texture2D + SpriteBatch, verified via
    # ReadBackbuffer pixel readback (UV mapping, opaque draw, BlendState::AlphaBlend factors).
    cna_opengl2_test(cna_test_opengl2_2d examples/opengl2_2d_test.cpp)
    cna_register_backend_test(NAME OpenGL2_2D COMMAND cna_test_opengl2_2d
        TIMEOUT 60 LABELS "OpenGL2" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_opengl2.md: pixel-exact 3D vertex-format proof -- colored3d, textured3d,
    # colored_textured3d, and a real depth-test occlusion proof.
    cna_opengl2_test(cna_test_opengl2_3d examples/opengl2_3d_test.cpp)
    cna_register_backend_test(NAME OpenGL2_3D COMMAND cna_test_opengl2_3d
        TIMEOUT 60 LABELS "OpenGL2" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_opengl2.md: RenderTarget2D/FBO proof -- off-screen render, direct readback,
    # sampled-as-Texture2D via SpriteBatch, and depth-test occlusion inside the FBO.
    cna_opengl2_test(cna_test_opengl2_rendertarget2d examples/opengl2_rendertarget2d_test.cpp)
    cna_register_backend_test(NAME OpenGL2_RenderTarget2D COMMAND cna_test_opengl2_rendertarget2d
        TIMEOUT 60 LABELS "OpenGL2" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_opengl2.md: AlphaTestEffect, DualTextureEffect, BasicEffect lighting and fog.
    cna_opengl2_test(cna_test_opengl2_effects examples/opengl2_effects_test.cpp)
    cna_register_backend_test(NAME OpenGL2_Effects COMMAND cna_test_opengl2_effects
        TIMEOUT 60 LABELS "OpenGL2" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_opengl2.md: FixedHeightDynamicWidth presentation-mode resize proof.
    cna_opengl2_test(cna_test_opengl2_presentation examples/opengl2_presentation_test.cpp)
    cna_register_backend_test(NAME OpenGL2_Presentation COMMAND cna_test_opengl2_presentation
        TIMEOUT 60 LABELS "OpenGL2" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_opengl2.md: OcclusionQuery (GL_SAMPLES_PASSED) proof.
    cna_opengl2_test(cna_test_opengl2_occlusionquery examples/opengl2_occlusionquery_test.cpp)
    cna_register_backend_test(NAME OpenGL2_OcclusionQuery COMMAND cna_test_opengl2_occlusionquery
        TIMEOUT 60 LABELS "OpenGL2" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
endif()
