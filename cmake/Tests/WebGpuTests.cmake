if(CNA_BUILD_TESTS AND NOT EMSCRIPTEN AND NOT WIN32
   AND CNA_GRAPHICS_BACKEND STREQUAL "WEBGPU")
    enable_testing()
    if(TARGET cna_demo_2d)
        cna_register_backend_test(NAME WebGPU_Native2D_Smoke COMMAND ${CMAKE_COMMAND} -DCNA_WEBGPU_DEMO=$<TARGET_FILE:cna_demo_2d> -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/WebGPUNativeSmokeTest.cmake
            TIMEOUT 70 LABELS "GraphicsSmoke;WebGPU" SKIP_REGULAR_EXPRESSION "\\[SKIP\\] CNA WebGPU native smoke")
    else()
        message(WARNING "CNA WebGPU: native smoke test requires CNA_BUILD_EXAMPLES=ON")
    endif()

    # WEBGPU-91/92: pixel-asserted readback coverage -- registered the same way as
    # WebGPU_Native2D_Smoke (SKIP_REGULAR_EXPRESSION for a headless/no-display environment),
    # since ReadBackbuffer() needs a real GPU-backed surface exactly like the smoke test does.
    macro(cna_webgpu_test target src)
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
        if(CNA_WEBGPU_RUNTIME_LIBRARY)
            add_custom_command(TARGET ${target} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CNA_WEBGPU_RUNTIME_LIBRARY}" "$<TARGET_FILE_DIR:${target}>"
                    COMMENT "Copying wgpu-native runtime next to ${target}")
            if(UNIX AND NOT APPLE)
                set_property(TARGET ${target} APPEND PROPERTY BUILD_RPATH "$ORIGIN")
            endif()
        endif()
    endmacro()

    cna_webgpu_test(cna_test_webgpu_clear_readback examples/webgpu_clear_readback_test.cpp)
    cna_register_backend_test(NAME WebGPU_Clear_Readback COMMAND cna_test_webgpu_clear_readback
        TIMEOUT 30 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Phase 57/63 vertical slice: DrawColoredPrimitives/DrawIndexedColoredPrimitives.
    cna_webgpu_test(cna_test_webgpu_colored3d examples/webgpu_colored3d_test.cpp)
    cna_register_backend_test(NAME WebGPU_Colored3D COMMAND cna_test_webgpu_colored3d
        TIMEOUT 30 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Phase 63: DrawPrimitivesEx/DrawIndexedPrimitivesEx real GpuDrawParams dispatch (stride 16).
    cna_webgpu_test(cna_test_webgpu_drawprimitivesex examples/webgpu_drawprimitivesex_test.cpp)
    cna_register_backend_test(NAME WebGPU_DrawPrimitivesEx COMMAND cna_test_webgpu_drawprimitivesex
        TIMEOUT 30 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Phase 58/59/63: textured3d.wgsl / GetOrCreatePipelineTextured3D / stride-20 dispatch.
    cna_webgpu_test(cna_test_webgpu_textured3d examples/webgpu_textured3d_test.cpp)
    cna_register_backend_test(NAME WebGPU_Textured3D COMMAND cna_test_webgpu_textured3d
        TIMEOUT 30 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Phase 58/59/63: colored_textured3d.wgsl / GetOrCreatePipelineColoredTextured3D / stride-24.
    cna_webgpu_test(cna_test_webgpu_coloredtextured3d examples/webgpu_coloredtextured3d_test.cpp)
    cna_register_backend_test(NAME WebGPU_ColoredTextured3D COMMAND cna_test_webgpu_coloredtextured3d
        TIMEOUT 30 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # WEBGPU-22: lit_textured3d.wgsl / GetOrCreatePipelineLitTextured3D / stride-32 dispatch,
    # real Blinn-Phong lighting.
    cna_webgpu_test(cna_test_webgpu_littextured3d examples/webgpu_littextured3d_test.cpp)
    cna_register_backend_test(NAME WebGPU_LitTextured3D COMMAND cna_test_webgpu_littextured3d
        TIMEOUT 30 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 1105 (plan_graphics.md Phase 80): GetOrCreatePipelineLitTextured3DVertexLit /
    # BasicEffect.PreferPerPixelLighting real dispatch.
    cna_webgpu_test(cna_test_webgpu_basiceffect_preferperpixellighting examples/webgpu_basiceffect_preferperpixellighting_test.cpp)
    cna_register_backend_test(NAME WebGPU_BasicEffect_PreferPerPixelLighting COMMAND cna_test_webgpu_basiceffect_preferperpixellighting
        TIMEOUT 30 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # WEBGPU-23/34/72: alpha_test3d.wgsl / GetOrCreatePipelineAlphaTest3D / AlphaTestEffect
    # per-pixel discard, strides 20/24/32.
    cna_webgpu_test(cna_test_webgpu_alphatest3d examples/webgpu_alphatest3d_test.cpp)
    cna_register_backend_test(NAME WebGPU_AlphaTest3D COMMAND cna_test_webgpu_alphatest3d
        TIMEOUT 30 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # WEBGPU-24: dual_texture3d.wgsl / GetOrCreatePipelineDualTexture3D / DualTextureEffect,
    # strides 20/24, second texture bind group.
    cna_webgpu_test(cna_test_webgpu_dualtexture3d examples/webgpu_dualtexture3d_test.cpp)
    cna_register_backend_test(NAME WebGPU_DualTexture3D COMMAND cna_test_webgpu_dualtexture3d
        TIMEOUT 30 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_cnj.md CNB-58 WebGPU counterpart: pbr3d.wgsl / GetOrCreatePipelinePbr3D / PbrEffect
    # (unskinned only, stride 48), the WebGPU backend's real glTF 2.0 metallic-roughness BRDF.
    cna_webgpu_test(cna_test_webgpu_pbr3d examples/webgpu_pbr3d_test.cpp)
    cna_register_backend_test(NAME WebGPU_Pbr3D COMMAND cna_test_webgpu_pbr3d
        TIMEOUT 30 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_cnj.md Phase 14J WebGPU counterpart: skinned3d.wgsl family / GetOrCreatePipelineSkinned3D
    # / SkinnedEffect (strides 52/56, both PreferPerPixelLighting variants, VertexColorEnabled) --
    # closes this backend's pre-existing "no skinning shader at all" gap.
    cna_webgpu_test(cna_test_webgpu_skinned3d examples/webgpu_skinned3d_test.cpp)
    cna_register_backend_test(NAME WebGPU_Skinned3D COMMAND cna_test_webgpu_skinned3d
        TIMEOUT 30 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_cnj.md Phase 14J WebGPU counterpart: skinned_pbr3d.wgsl / GetOrCreatePipelineSkinnedPbr3D
    # / SkinnedPbrEffect (stride 68, PBR + skinning combo).
    cna_webgpu_test(cna_test_webgpu_skinnedpbr3d examples/webgpu_skinnedpbr3d_test.cpp)
    cna_register_backend_test(NAME WebGPU_SkinnedPbr3D COMMAND cna_test_webgpu_skinnedpbr3d
        TIMEOUT 30 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # WEBGPU-41/77/78/79/80/81/82/83: real BlendState/RasterizerState (cull mode, wireframe)/
    # scissor/viewport wiring -- previously ApplyBlendState/ApplyRasterizerState/SetScissorRect/
    # SetViewport had no override at all on this backend, silently falling back to
    # IGraphicsBackend's no-op defaults for every 3D draw.
    cna_webgpu_test(cna_test_webgpu_graphicsstate examples/webgpu_graphicsstate_test.cpp)
    cna_register_backend_test(NAME WebGPU_GraphicsState COMMAND cna_test_webgpu_graphicsstate
        TIMEOUT 30 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
endif()
