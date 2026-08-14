# plan_runtimerenderer.md RTR-P9-14: renderer gates here test LIST MEMBERSHIP
# (CNA_RENDERER_IDENTITIES) rather than equality with the build default. In
# single-renderer mode the list has one entry and the two are identical; in a
# multi-renderer build a target is buildable when its renderer is compiled in at
# all, not only when it happens to be the default.
# CNAEXT graphics-extension examples (module-local registration; the target definitions
# moved here verbatim from cmake/Examples.cmake when examples moved into their owning
# physical modules).

# Example/demo executables historically live at the build root; keep them there.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# ---- CNAEXT settings example (no window needed — pure API test) ---------------
if(CNA_BUILD_EXAMPLES AND CNA_CNAEXT)
    add_executable(cna_example_cnaext_settings cnaext_settings_example.cpp)
    target_link_libraries(cna_example_cnaext_settings PRIVATE CNA)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_example_cnaext_settings PRIVATE SDL3::SDL3main)
    endif()
    if(CNA_BUILD_TESTS)
        add_test(NAME CNAEXT_Settings_Compile_Run
                 COMMAND cna_example_cnaext_settings)
        # Historical root-scope registration CWD, preserved (see cna_register_renderer_test).
        set_tests_properties(CNAEXT_Settings_Compile_Run PROPERTIES WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
    endif()
endif()

# ---- DepthEffect manual verification demo (renders + screenshots each colour-depth mode) -----
if(CNA_BUILD_EXAMPLES AND CNA_CNAEXT AND ("OPENGLES3" IN_LIST CNA_RENDERER_IDENTITIES OR "OPENGL33" IN_LIST CNA_RENDERER_IDENTITIES) AND NOT EMSCRIPTEN AND NOT ANDROID)
    add_executable(cna_depth_effect_demo depth_effect_demo_test.cpp)
    # The shared example-support headers ("common/ScreenshotEXT.hpp", ...) live with the
    # graphics module's examples.
    target_include_directories(cna_depth_effect_demo PRIVATE ${CNA_GRAPHICS_EXAMPLES_DIR})
    target_compile_definitions(cna_depth_effect_demo PRIVATE
        CNA_DEPTH_EFFECT_DEMO_CONTENT_ROOT="${CNA_GRAPHICS_EXAMPLES_DIR}/demo_2d/Content")
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
        target_link_libraries(cna_depth_effect_demo PRIVATE
            CNA SHARP_RUNTIME)
    else()
        target_link_libraries(cna_depth_effect_demo PRIVATE CNA SHARP_RUNTIME)
    endif()
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_depth_effect_demo PRIVATE SDL3::SDL3main)
    endif()
    if(WIN32)
        set_target_properties(cna_depth_effect_demo PROPERTIES WIN32_EXECUTABLE TRUE)
        cna_copy_sdl_runtime(cna_depth_effect_demo)
    endif()
endif()

# ---- CRTEffect manual verification demo (renders + screenshots each CRT parameter set) --------
if(CNA_BUILD_EXAMPLES AND CNA_CNAEXT AND ("OPENGLES3" IN_LIST CNA_RENDERER_IDENTITIES OR "OPENGL33" IN_LIST CNA_RENDERER_IDENTITIES) AND NOT EMSCRIPTEN AND NOT ANDROID)
    add_executable(cna_crt_effect_demo crt_effect_demo_test.cpp)
    target_include_directories(cna_crt_effect_demo PRIVATE ${CNA_GRAPHICS_EXAMPLES_DIR})
    target_compile_definitions(cna_crt_effect_demo PRIVATE
        CNA_CRT_EFFECT_DEMO_CONTENT_ROOT="${CNA_GRAPHICS_EXAMPLES_DIR}/demo_2d/Content")
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
        target_link_libraries(cna_crt_effect_demo PRIVATE
            CNA SHARP_RUNTIME)
    else()
        target_link_libraries(cna_crt_effect_demo PRIVATE CNA SHARP_RUNTIME)
    endif()
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_crt_effect_demo PRIVATE SDL3::SDL3main)
    endif()
    if(WIN32)
        set_target_properties(cna_crt_effect_demo PROPERTIES WIN32_EXECUTABLE TRUE)
        cna_copy_sdl_runtime(cna_crt_effect_demo)
    endif()
endif()

# ---- AsciiPostProcessEffect oracles -----------------------------------------------------------
# Migrated from the former ASCII graphics renderer (plan_ascii.md); unlike the CRTEffect/DepthEffect
# demos above, AsciiPostProcessEffect's portable CPU quantizer needs no GLSL shader compilation, so
# these run under every renderer (any CNA_GRAPHICS_RENDERER), not just OPENGLES3/OPENGL33 -- the
# whole point of this migration is that the effect is no longer tied to one specific renderer.
if(CNA_BUILD_TESTS AND CNA_CNAEXT)
    macro(cna_graphics_ext_test target src)
        add_executable(${target} ${src})
        target_link_libraries(${target} PRIVATE CNA SHARP_RUNTIME)
        if(TARGET SDL3::SDL3main)
            target_link_libraries(${target} PRIVATE SDL3::SDL3main)
        endif()
    endmacro()

    # Pure function, no GraphicsDevice/window needed at all.
    cna_graphics_ext_test(cna_test_ascii_quantizer ascii_quantizer_test.cpp)
    cna_register_renderer_test(NAME Ascii_Quantizer COMMAND cna_test_ascii_quantizer
        TIMEOUT 30)

    if(NOT EMSCRIPTEN AND NOT WIN32)
        cna_graphics_ext_test(cna_test_ascii_fontatlas ascii_fontatlas_test.cpp)
        cna_register_renderer_test(NAME Ascii_FontAtlas COMMAND cna_test_ascii_fontatlas
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_graphics_ext_test(cna_test_ascii_posteffect_pixel ascii_posteffect_pixel_test.cpp)
        cna_register_renderer_test(NAME AsciiPostProcessEffect_Pixel COMMAND cna_test_ascii_posteffect_pixel
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Proves the effect accepts a source produced by a genuine 3D draw call (BasicEffect +
        # VertexBuffer + depth-tested DrawPrimitives), not just a 2D SpriteBatch-composited scene.
        # Self-SKIPs (exit 77) via GraphicsCapability::ThreeD on renderers with no real 3D pipeline
        # (SDL_RENDERER, HEADLESS, ...) -- registered unconditionally here, same as the other two.
        cna_graphics_ext_test(cna_test_ascii_posteffect_3d_source ascii_posteffect_3d_source_test.cpp)
        cna_register_renderer_test(NAME AsciiPostProcessEffect_3DSource COMMAND cna_test_ascii_posteffect_3d_source
            TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
    endif()
endif()

# Module-local tests get the same headless-safe skip convention the root one-shot
# gives root-registered tests (see cmake/TestHelpers.cmake).
cna_apply_skip_convention()
