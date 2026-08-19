# plan_runtimerenderer.md RTR-P9-13/P9-15 -- the rule these gates follow, corrected after getting it
# wrong once:
#
#   * A gate deciding whether a TARGET EXISTS, where that target runs against whatever renderer the
#     build DEFAULTS to, must test equality with CNA_GRAPHICS_RENDERER. Membership is wrong there: a
#     multi-renderer build defaulting to SOFTWARE but containing OPENGLES3 would build a demo that
#     needs 3D and then throw "3D not supported" at runtime -- a build that succeeds and a program
#     that cannot work.
#   * A gate deciding whether a RESOURCE is available -- a runtime library to copy, a renderer's own
#     device-free test sources -- may test membership, because the user really can select that
#     renderer at runtime in a multi build.
#
# In single-renderer mode the two are identical, which is exactly why the distinction is easy to
# miss.


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
if(CNA_BUILD_EXAMPLES AND CNA_CNAEXT AND (CNA_GRAPHICS_RENDERER STREQUAL "OPENGLES3" OR CNA_GRAPHICS_RENDERER STREQUAL "OPENGL33") AND NOT EMSCRIPTEN AND NOT ANDROID)
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
if(CNA_BUILD_EXAMPLES AND CNA_CNAEXT AND (CNA_GRAPHICS_RENDERER STREQUAL "OPENGLES3" OR CNA_GRAPHICS_RENDERER STREQUAL "OPENGL33") AND NOT EMSCRIPTEN AND NOT ANDROID)
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
        TIMEOUT 30 LABELS "CnaExt")

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

# ---- Shadow mapping, end to end (plan_modern.md MOD-852/MOD-856) -------------------------------
# Registered for every renderer, like the ASCII oracles above rather than like the GLSL demos:
# nothing here is renderer-specific, and a renderer that cannot raster 3D or compile the caster's
# shader SKIPs itself (exit 77) from inside, reporting which of the two it was.
if(CNA_BUILD_TESTS AND CNA_CNAEXT AND NOT EMSCRIPTEN AND NOT WIN32)
    cna_graphics_ext_test(cna_test_cnaext_shadowmap cnaext_shadowmap_test.cpp)
    cna_register_renderer_test(NAME CNAEXT_ShadowMap COMMAND cna_test_cnaext_shadowmap
        TIMEOUT 60 LABELS "CnaExt;CnaExtDisplay" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_graphics_ext_test(cna_test_cnaext_csm cnaext_csm_test.cpp)
    cna_register_renderer_test(NAME CNAEXT_CascadedShadowMap COMMAND cna_test_cnaext_csm
        TIMEOUT 90 LABELS "CnaExt;CnaExtDisplay" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_graphics_ext_test(cna_test_cnaext_pointshadow cnaext_pointshadow_test.cpp)
    cna_register_renderer_test(NAME CNAEXT_PointShadow COMMAND cna_test_cnaext_pointshadow
        TIMEOUT 90 LABELS "CnaExt;CnaExtDisplay" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_graphics_ext_test(cna_test_cnaext_skybox cnaext_skybox_test.cpp)
    cna_register_renderer_test(NAME CNAEXT_Skybox COMMAND cna_test_cnaext_skybox
        TIMEOUT 90 LABELS "CnaExt;CnaExtDisplay" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_graphics_ext_test(cna_test_cnaext_ibl cnaext_ibl_test.cpp)
    cna_register_renderer_test(NAME CNAEXT_ImageBasedLighting COMMAND cna_test_cnaext_ibl
        TIMEOUT 120 LABELS "CnaExt;CnaExtDisplay" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_graphics_ext_test(cna_test_cnaext_instancing_lod cnaext_instancing_lod_test.cpp)
    cna_register_renderer_test(NAME CNAEXT_InstancingAndLod COMMAND cna_test_cnaext_instancing_lod
        TIMEOUT 120 LABELS "CnaExt;CnaExtDisplay" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_graphics_ext_test(cna_test_cnaext_compute_particles cnaext_compute_particles_test.cpp)
    cna_register_renderer_test(NAME CNAEXT_ComputeParticles COMMAND cna_test_cnaext_compute_particles
        TIMEOUT 180 LABELS "CnaExt;CnaExtDisplay" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_graphics_ext_test(cna_test_cnaext_bloom cnaext_bloom_test.cpp)
    cna_register_renderer_test(NAME CNAEXT_BloomInTenLines COMMAND cna_test_cnaext_bloom
        TIMEOUT 90 LABELS "CnaExt;CnaExtDisplay" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_modern.md MOD-743: 1000 frames + 50 resizes, for a leak checker to watch.
    cna_graphics_ext_test(cna_test_cnaext_leak_loop cnaext_leak_loop_test.cpp)
    cna_register_renderer_test(NAME CNAEXT_LeakLoop COMMAND cna_test_cnaext_leak_loop
        TIMEOUT 600 LABELS "CnaExt;CnaExtDisplay" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_modern.md MOD-738..MOD-742: the layer's showcase -- 2D and 3D through the full stack.
    cna_graphics_ext_test(cna_test_cnaext_render_pipeline cnaext_render_pipeline_test.cpp)
    cna_register_renderer_test(NAME CNAEXT_RenderPipeline COMMAND cna_test_cnaext_render_pipeline
        TIMEOUT 120 LABELS "CnaExt;CnaExtDisplay" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_modern.md MOD-604/MOD-607/MOD-608: FXAA on and off, and its cost per image content.
    cna_graphics_ext_test(cna_test_cnaext_fxaa cnaext_fxaa_test.cpp)
    cna_register_renderer_test(NAME CNAEXT_Fxaa COMMAND cna_test_cnaext_fxaa
        TIMEOUT 90 LABELS "CnaExt;CnaExtDisplay" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_modern.md MOD-526/MOD-527/MOD-528: SSAO end to end, from the prepass to the frame.
    # plan_modern.md MOD-1808/MOD-1811: every subsystem in one frame, each one A/B'd against the
    # same frame with only itself switched off. Longer timeout than the single-subsystem examples
    # because it renders the scene seven times.
    cna_graphics_ext_test(cna_test_cnaext_showcase cnaext_showcase_test.cpp)
    cna_register_renderer_test(NAME CNAEXT_Showcase COMMAND cna_test_cnaext_showcase
        TIMEOUT 300 LABELS "CnaExt;CnaExtDisplay" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_modern.md MOD-1810: a material that arrived through the runtime glTF importer, driven
    # by the engine layer's IBL and shadows. The corpus path is a compile definition so the program
    # does not depend on being launched from the repository root; --content overrides it.
    cna_graphics_ext_test(cna_test_cnaext_gltf_pbr cnaext_gltf_pbr_test.cpp)
    target_compile_definitions(cna_test_cnaext_gltf_pbr PRIVATE
        CNAEXT_GLTF_PBR_CORPUS="${CMAKE_SOURCE_DIR}/tests/assets/gltf")
    cna_register_renderer_test(NAME CNAEXT_GltfPbr COMMAND cna_test_cnaext_gltf_pbr
        TIMEOUT 120 LABELS "CnaExt;CnaExtDisplay" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_graphics_ext_test(cna_test_cnaext_caps cnaext_caps_probe.cpp)

    cna_graphics_ext_test(cna_test_cnaext_ssao cnaext_ssao_test.cpp)
    cna_register_renderer_test(NAME CNAEXT_Ssao COMMAND cna_test_cnaext_ssao
        TIMEOUT 120 LABELS "CnaExt;CnaExtDisplay" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_modern.md MOD-315/MOD-318/MOD-319: the five tonemapping operators, measured as curves.
    cna_graphics_ext_test(cna_test_cnaext_tonemap cnaext_tonemap_test.cpp)
    cna_register_renderer_test(NAME CNAEXT_Tonemap COMMAND cna_test_cnaext_tonemap
        TIMEOUT 90 LABELS "CnaExt;CnaExtDisplay" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_modern.md MOD-229/MOD-230: three stacked passes, and what each pass costs.
    cna_graphics_ext_test(cna_test_cnaext_postprocess_chain cnaext_postprocess_chain_test.cpp)
    cna_register_renderer_test(NAME CNAEXT_PostProcessChain COMMAND cna_test_cnaext_postprocess_chain
        TIMEOUT 90 LABELS "CnaExt;CnaExtDisplay" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_modern.md MOD-132/MOD-138: what a float render target buys, on screen, and what it costs.
    cna_graphics_ext_test(cna_test_cnaext_hdr_target cnaext_hdr_target_test.cpp)
    cna_register_renderer_test(NAME CNAEXT_HdrRenderTarget COMMAND cna_test_cnaext_hdr_target
        TIMEOUT 90 LABELS "CnaExt;CnaExtDisplay" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
endif()

# ---- plan_modern.md MOD-1740: the guard script, as a test ---------------------------------------
# MOD-3's scripts/check_cnaext_guards.sh proves every engine-layer file is wrapped in
# `#ifdef CNA_CNAEXT`, which is what makes the layer genuinely absent from a default build. A rule
# nothing enforces is a rule that decays, so it runs as a ctest: pure text, no device, no display,
# and it runs in EVERY configuration -- including the CNA_CNAEXT=OFF one, where an unguarded file
# would otherwise not even be compiled.
if(CNA_BUILD_TESTS)
    add_test(NAME CNAEXT_GuardDiscipline
             COMMAND bash "${CMAKE_SOURCE_DIR}/scripts/check_cnaext_guards.sh" "${CMAKE_SOURCE_DIR}")
    set_tests_properties(CNAEXT_GuardDiscipline PROPERTIES
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}" LABELS "CnaExt")

    # plan_modern.md MOD-1698: and the matrix must name every renderer identity, derived from
    # CNA::GraphicsRendererType rather than hardcoded -- a renderer missing from a matrix reads as
    # "fine", which is exactly what an unexamined renderer is not.
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME CNAEXT_MatrixCompleteness
                 COMMAND ${Python3_EXECUTABLE}
                         "${CMAKE_SOURCE_DIR}/scripts/check_cnaext_matrix.py"
                         "${CMAKE_SOURCE_DIR}")
        set_tests_properties(CNAEXT_MatrixCompleteness PROPERTIES
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}" LABELS "CnaExt")
    endif()

    # plan_modern.md MOD-1902: the accessor conventions the API review settled -- every
    # value-returning get/is/has is [[nodiscard]], and is const when it takes no argument. A
    # one-time read of the headers decays with the next header, so the sweep runs as a test.
    if(Python3_Interpreter_FOUND)
        add_test(NAME CNAEXT_AccessorConventions
                 COMMAND ${Python3_EXECUTABLE}
                         "${CMAKE_SOURCE_DIR}/scripts/check_cnaext_nodiscard.py"
                         "${CMAKE_SOURCE_DIR}")
        set_tests_properties(CNAEXT_AccessorConventions PROPERTIES
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}" LABELS "CnaExt")
    endif()

    # plan_modern.md MOD-1900: MOD-6's naming rule -- verbs are lowerCamelCase here, because there
    # is no XNA name to preserve. Every UpperCamelCase function must therefore be a named exception
    # with its reason (an XNA base-class override, a System.Object mirror, or a C ABI surface), and
    # the script fails on a stale exemption too, so the list cannot outlive what it excuses.
    if(Python3_Interpreter_FOUND)
        add_test(NAME CNAEXT_NamingRule
                 COMMAND ${Python3_EXECUTABLE}
                         "${CMAKE_SOURCE_DIR}/scripts/check_cnaext_naming.py"
                         "${CMAKE_SOURCE_DIR}")
        set_tests_properties(CNAEXT_NamingRule PROPERTIES
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}" LABELS "CnaExt")
    endif()

    # plan_modern.md Phase 16: every renderer descriptor must at least parse. Three of them
    # (OpenGL1, LLGL, bgfx) were left syntactically broken by one merge and nobody noticed, because
    # a renderer identity is only compiled when someone selects it -- each was found by building
    # that renderer, one at a time. This checks all 44 at once, in milliseconds, with no renderer
    # selected and no compiler involved.
    if(Python3_Interpreter_FOUND)
        add_test(NAME CNAEXT_RendererDescriptorsParse
                 COMMAND ${Python3_EXECUTABLE}
                         "${CMAKE_SOURCE_DIR}/scripts/check_renderer_descriptors.py"
                         "${CMAKE_SOURCE_DIR}")
        set_tests_properties(CNAEXT_RendererDescriptorsParse PROPERTIES
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}" LABELS "CnaExt")
    endif()

    # docs/cnatests-mingw-setenv-proposal.md, kept from regressing. POSIX setenv/unsetenv do not
    # exist on MinGW-w64, so one call site anywhere stops every Windows cross-build's CnaTests.exe
    # compiling. The proposal replaced all 62 in 2026-07; eleven new ones had appeared by 2026-08,
    # found while measuring D3D11 for Phase 16. This fails on a Linux build instead.
    if(Python3_Interpreter_FOUND)
        add_test(NAME CNAEXT_NoPosixSetenv
                 COMMAND ${Python3_EXECUTABLE}
                         "${CMAKE_SOURCE_DIR}/scripts/check_no_posix_setenv.py"
                         "${CMAKE_SOURCE_DIR}")
        set_tests_properties(CNAEXT_NoPosixSetenv PROPERTIES
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}" LABELS "CnaExt")
    endif()

    # plan_modern.md MOD-7: the layer builds one Doxygen module page and has no undocumented public
    # member. SKIPs (77) where doxygen is not installed, since the check is about these headers and
    # not about the container.
    add_test(NAME CNAEXT_DoxygenGroup
             COMMAND bash "${CMAKE_SOURCE_DIR}/scripts/check_cnaext_doxygen.sh"
                     "${CMAKE_SOURCE_DIR}")
    set_tests_properties(CNAEXT_DoxygenGroup PROPERTIES
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}" LABELS "CnaExt" SKIP_RETURN_CODE 77)
endif()

# Module-local tests get the same headless-safe skip convention the root one-shot
# gives root-registered tests (see cmake/TestHelpers.cmake).
cna_apply_skip_convention()
