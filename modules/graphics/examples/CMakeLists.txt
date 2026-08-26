# plans/plan_runtimerenderer.md RTR-P9-13/P9-15 -- the rule these gates follow, corrected after getting it
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


# Example/demo executables historically live at the build root (docs, scripts and the
# golden-image working-directory conventions rely on it); keep them there even though
# their registration now lives in a subdirectory.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

if(CNA_BUILD_EXAMPLES)
    # ---- Runtime renderer selection reference (plans/plan_runtimerenderer.md RTR-P4-14) ----
    # Renderer-agnostic: it reports whatever this build contains rather than assuming one.
    add_executable(cna_demo_renderer_selection
        renderer_selection/renderer_selection_demo.cpp
    )
    target_link_libraries(cna_demo_renderer_selection PRIVATE CNA)
    cna_link_sharp_runtime(cna_demo_renderer_selection PRIVATE)

    # ---- Standalone-device probe (plans/plan_cabi.md CABI-10) -----------------
    add_executable(cna_standalone_device_probe standalone_device_probe.cpp)
    target_link_libraries(cna_standalone_device_probe PRIVATE CNA)
    cna_link_sharp_runtime(cna_standalone_device_probe PRIVATE)
    if(CNA_BUILD_TESTS)
        add_test(NAME StandaloneGraphicsDeviceProbe COMMAND cna_standalone_device_probe)
        set_tests_properties(StandaloneGraphicsDeviceProbe PROPERTIES
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
    endif()

    # ---- 2D sprite demo (works with all renderers) ----------------------------
    add_executable(cna_demo_2d
        demo_2d/src/Main.cpp
        demo_2d/src/Game1.cpp
    )
    target_include_directories(cna_demo_2d PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/demo_2d/src
    )

    if(EMSCRIPTEN)
        set_target_properties(cna_demo_2d PROPERTIES SUFFIX ".html")
        target_link_libraries(cna_demo_2d PRIVATE
            CNA ${RENDERER_TARGET} SDL3::SDL3-static SHARP_RUNTIME)
        target_link_options(cna_demo_2d PRIVATE
            -sALLOW_MEMORY_GROWTH=1
            -sSTACK_SIZE=1048576
            -sINITIAL_MEMORY=134217728
            -sFORCE_FILESYSTEM=1
            "SHELL:--preload-file ${CMAKE_CURRENT_SOURCE_DIR}/demo_2d/Content@/Content"
        )
        if(CNA_GRAPHICS_RENDERER STREQUAL "WEBGL1")
            target_link_options(cna_demo_2d PRIVATE
                "-sMIN_WEBGL_VERSION=1"
                "-sMAX_WEBGL_VERSION=1")
        elseif(CNA_GRAPHICS_RENDERER STREQUAL "WEBGL2")
            # Emscripten defaults to WebGL 1 unless the final executable requests WebGL 2.
            # EasyGL's WEBGL2 profile emits GLSL ES 3.00 shaders, which WebGL 1 rejects.
            target_link_options(cna_demo_2d PRIVATE
                "-sMIN_WEBGL_VERSION=2"
                "-sMAX_WEBGL_VERSION=2")
        endif()
    else()
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32 AND NOT APPLE)
            target_link_libraries(cna_demo_2d PRIVATE
                CNA
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_2d PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_2d PRIVATE SDL3::SDL3main)
        endif()
        add_custom_command(TARGET cna_demo_2d POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/demo_2d/Content"
                "$<TARGET_FILE_DIR:cna_demo_2d>/Content"
            COMMENT "Copying Content next to cna_demo_2d executable")
        if("WEBGPU" IN_LIST CNA_RENDERER_IDENTITIES AND CNA_WEBGPU_RUNTIME_LIBRARY)
            add_custom_command(TARGET cna_demo_2d POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CNA_WEBGPU_RUNTIME_LIBRARY}" "$<TARGET_FILE_DIR:cna_demo_2d>"
                COMMENT "Copying wgpu-native runtime next to cna_demo_2d")
            if(UNIX AND NOT APPLE)
                set_property(TARGET cna_demo_2d APPEND PROPERTY BUILD_RPATH "$ORIGIN")
            endif()
        endif()
        if(WIN32)
            set_target_properties(cna_demo_2d PROPERTIES WIN32_EXECUTABLE TRUE)
            # The cross-compiled demo is a distributable Windows GUI executable, just like the
            # Windows renderer smoke tests.  Keep libgcc/libstdc++ self-contained and copy the
            # remaining MinGW thread runtime so a clean Proton/Windows prefix can load it.
            if(MINGW)
                target_link_options(cna_demo_2d PRIVATE -static-libgcc -static-libstdc++)
                target_link_options(cna_demo_2d PRIVATE -Wl,--allow-multiple-definition)
                cna_copy_mingw_runtime(cna_demo_2d)
            endif()
            cna_copy_sdl_runtime(cna_demo_2d)
        endif()
        # The 2D demo is the cross-renderer visual verification target.  Stage
        # MinGW's dynamic runtimes so it runs under Wine/a clean Windows
        # installation without relying on a compiler-specific PATH.
        if(MINGW)
            cna_copy_mingw_cxx_runtime(cna_demo_2d)
        endif()
    endif()
endif()

# A full terminal-platform smoke test.  The Python runner owns a pseudo-terminal so the real demo
# receives a TTY (unlike ordinary CTest stdout), a resize signal and keyboard input.  It is kept
# to the one tuple that has a CPU presenter capable of reaching a terminal.
if(CNA_BUILD_TESTS AND CNA_BUILD_EXAMPLES AND CNA_PLATFORM STREQUAL "TERMINAL"
   AND CNA_GRAPHICS_RENDERER STREQUAL "BLEND2D" AND TARGET Python3::Interpreter)
    add_test(NAME TerminalBlend2DDemoIntegration
        COMMAND Python3::Interpreter
            "${CMAKE_SOURCE_DIR}/tools/platform/terminal_demo_integration_test.py"
            "$<TARGET_FILE:cna_demo_2d>")
    set_tests_properties(TerminalBlend2DDemoIntegration PROPERTIES TIMEOUT 25)
endif()

if(CNA_BUILD_EXAMPLES AND (CNA_GRAPHICS_RENDERER STREQUAL "OPENGLES2" OR CNA_GRAPHICS_RENDERER STREQUAL "OPENGLES3"
        OR CNA_GRAPHICS_RENDERER STREQUAL "OPENGL33"
        OR CNA_GRAPHICS_RENDERER STREQUAL "WEBGL1" OR CNA_GRAPHICS_RENDERER STREQUAL "WEBGL2"
        OR CNA_GRAPHICS_RENDERER STREQUAL "VULKAN"
        OR CNA_GRAPHICS_RENDERER STREQUAL "FNA3D"))
    # ---- 3D house demo --------------------------------------------------------
    # The 3D house demo exercises the shared BasicEffect 3D path. The target is enabled
    # only with renderers that support every graphics operation used by the scene: a GL-family
    # public renderer
    # (OPENGLES2/OPENGLES3/OPENGL33/WEBGL1/WEBGL2, all implemented by EasyGL -- see
    # plans/plan_glbackends.md), VULKAN, or FNA3D.
    add_executable(cna_house3d_demo house3d_demo.cpp)

    if(EMSCRIPTEN)
        # ---- Emscripten / WebAssembly ----------------------------------------
        # Produce a self-contained HTML page. The scene is fully procedural so
        # no asset preloading is required.
        set_target_properties(cna_house3d_demo PROPERTIES SUFFIX ".html")
        # wasm-ld resolves circular static-lib references without linker groups.
        target_link_libraries(cna_house3d_demo PRIVATE
            CNA ${RENDERER_TARGET} SDL3::SDL3-static SHARP_RUNTIME)
        # plans/plan_glbackends.md GLB-9 (revisited): MIN_WEBGL_VERSION/MAX_WEBGL_VERSION must match
        # whichever GL profile this target is actually compiled for -- pre-GLB-9 this was
        # hardcoded to WebGL 2 unconditionally, which would have forced a WebGL 2 context even
        # under -DCNA_GRAPHICS_RENDERER=WEBGL1 (contradicting EasyGLRenderer's own
        # SDL_GL_CONTEXT_MAJOR_VERSION=2 request for that profile, GLB-8) -- found while verifying
        # GLB-36's real emcc WEBGL1 build.
        if(CNA_GRAPHICS_RENDERER STREQUAL "WEBGL1")
            set(_cna_house3d_min_webgl_version 1)
            set(_cna_house3d_max_webgl_version 1)
        else()
            set(_cna_house3d_min_webgl_version 2)
            set(_cna_house3d_max_webgl_version 2)
        endif()
        target_link_options(cna_house3d_demo PRIVATE
            -sALLOW_MEMORY_GROWTH=1
            -sSTACK_SIZE=1048576
            -sINITIAL_MEMORY=67108864       # 64 MB — enough for a procedural scene
            -sFORCE_FILESYSTEM=1
            "-sMIN_WEBGL_VERSION=${_cna_house3d_min_webgl_version}"
            "-sMAX_WEBGL_VERSION=${_cna_house3d_max_webgl_version}"
        )
    else()
        # ---- Native desktop --------------------------------------------------
        # The CNA library and the EasyGL renderer static lib have circular symbol
        # references (renderer uses Color/Rectangle from CNA, CNA uses renderer
        # interfaces); wrap them in a linker group so GNU ld resolves both.
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32 AND NOT APPLE)
            target_link_libraries(cna_house3d_demo PRIVATE
                CNA
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_house3d_demo PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_house3d_demo PRIVATE SDL3::SDL3main)
        endif()
        if(WIN32)
            set_target_properties(cna_house3d_demo PROPERTIES WIN32_EXECUTABLE TRUE)
            cna_copy_sdl_runtime(cna_house3d_demo)
        endif()
    endif()
endif()

# --- plans/plan_software.md SOFTWARE-61/84: cross-renderer diagnostic comparator ---
# Standalone, renderer-independent (no CNA/SHARP_RUNTIME dependency) tool that diffs two raw
# 64x64 RGBA8 dumps produced by cross_renderer_diagnostic_scene.cpp (built once per renderer by
# the SOFTWARE and EASYGL suites) within a tolerance. Not wired into a single ctest run --
# CNA_GRAPHICS_RENDERER is a compile-time choice, so comparing two renderers' output needs two
# separate builds' dumps; see docs/software-renderer.md's "Cross-renderer diagnostic" section for
# the exact manual invocation.
if(CNA_BUILD_TESTS)
    add_executable(cna_diag_compare cross_renderer_diagnostic_compare.cpp)
endif()

# plans/plan_html_dom.md Phase D10 and plans/plan_platform.md PLAT-7/120: one renderer-agnostic fixed-scene
# benchmark, built against whichever renderer this configure selected. Browser builds publish via
# JavaScript as before; native builds use steady_clock and emit the raw samples as JSON. Keeping
# this under either examples or tests makes a lean benchmark-only configure possible.
if((CNA_BUILD_TESTS OR CNA_BUILD_EXAMPLES) AND NOT ANDROID)
    add_executable(cna_bench_graphics_renderer graphics_renderer_benchmark.cpp)
    target_link_libraries(cna_bench_graphics_renderer PRIVATE CNA SHARP_RUNTIME)
    if(EMSCRIPTEN)
        target_link_libraries(cna_bench_graphics_renderer PRIVATE SDL3::SDL3)
        set_target_properties(cna_bench_graphics_renderer PROPERTIES SUFFIX ".html")
        target_link_options(cna_bench_graphics_renderer PRIVATE
            -sALLOW_MEMORY_GROWTH=1
            -sEXIT_RUNTIME=1
            -sEXPORTED_RUNTIME_METHODS=UTF8ToString)
        if(CNA_GRAPHICS_RENDERER STREQUAL "WEBGL2")
            # Emscripten defaults to WebGL1; EasyGL's ES 3.0 shaders require WebGL2.
            target_link_options(cna_bench_graphics_renderer PRIVATE
                "-sMIN_WEBGL_VERSION=2"
                "-sMAX_WEBGL_VERSION=2")
        endif()
    elseif(TARGET SDL3::SDL3main)
        target_link_libraries(cna_bench_graphics_renderer PRIVATE SDL3::SDL3main)
    endif()
endif()

# Module-local tests get the same headless-safe skip convention the root one-shot
# gives root-registered tests (see cmake/TestHelpers.cmake).
cna_apply_skip_convention()
