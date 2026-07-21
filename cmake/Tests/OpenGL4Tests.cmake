# plan_opengl4.md GL4-1..GL4-12: real desktop OpenGL 4.x core-profile graphics backend CTest
# registration. Needs a real display/GPU (or a virtual one, e.g. Xvfb) -- same
# SDL_VIDEODRIVER=x11/CNA_TEST_DISPLAY convention as the SDL_GPU/Vulkan/Bgfx/D3D11 registrations
# above; a genuine OpenGL 4.x core-profile context cannot be created without one.
if(CNA_BUILD_TESTS AND CNA_GRAPHICS_BACKEND STREQUAL "OPENGL4")
    enable_testing()

    macro(cna_opengl4_test target src)
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

    # plan_opengl4.md GL4-1..GL4-9: device/window/context lifecycle, VertexBuffer/IndexBuffer
    # round-trip, and a 60-frame Clear()+Present() loop.
    cna_opengl4_test(cna_test_opengl4_smoke examples/opengl4_smoke_test.cpp)
    cna_register_backend_test(NAME OpenGL4_Smoke COMMAND cna_test_opengl4_smoke
        TIMEOUT 60 LABELS "OpenGL4" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_opengl4.md GL4-10: real pixel-readback proof (Clear/SpriteBatch/blend/sampler
    # address-mode), adapted from webgpu_clear_readback_test.cpp's own check list.
    cna_opengl4_test(cna_test_opengl4_readback examples/opengl4_readback_test.cpp)
    cna_register_backend_test(NAME OpenGL4_Readback COMMAND cna_test_opengl4_readback
        TIMEOUT 60 LABELS "OpenGL4" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_opengl4.md GL4-11/GL4-12: DrawColoredPrimitives/DrawIndexedColoredPrimitives, with a
    # real pixel-readback depth-test occlusion proof (not just "didn't throw").
    cna_opengl4_test(cna_test_opengl4_3d examples/opengl4_3d_test.cpp)
    cna_register_backend_test(NAME OpenGL4_3D COMMAND cna_test_opengl4_3d
        TIMEOUT 60 LABELS "OpenGL4" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_opengl4.md GL4-13: DrawPrimitivesEx/DrawIndexedPrimitivesEx stride dispatch --
    # textured3d/colored_textured3d/lit_textured3d (BasicEffect.TextureEnabled/VertexColorEnabled/
    # EnableDefaultLighting), with real pixel-readback proofs.
    cna_opengl4_test(cna_test_opengl4_textured3d examples/opengl4_textured3d_test.cpp)
    cna_register_backend_test(NAME OpenGL4_Textured3D COMMAND cna_test_opengl4_textured3d
        TIMEOUT 60 LABELS "OpenGL4" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
endif()
