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
endif()
