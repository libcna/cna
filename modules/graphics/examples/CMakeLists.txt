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


# Example/demo executables historically live at the build root (docs, scripts and the
# golden-image working-directory conventions rely on it); keep them there even though
# their registration now lives in a subdirectory.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

if(CNA_BUILD_EXAMPLES)
    # ---- Runtime renderer selection reference (plan_runtimerenderer.md RTR-P4-14) ----
    # Renderer-agnostic: it reports whatever this build contains rather than assuming one.
    add_executable(cna_demo_renderer_selection
        renderer_selection/renderer_selection_demo.cpp
    )
    target_link_libraries(cna_demo_renderer_selection PRIVATE CNA)
    cna_link_sharp_runtime(cna_demo_renderer_selection PRIVATE)

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

if(CNA_BUILD_EXAMPLES AND (CNA_GRAPHICS_RENDERER STREQUAL "OPENGLES2" OR CNA_GRAPHICS_RENDERER STREQUAL "OPENGLES3"
        OR CNA_GRAPHICS_RENDERER STREQUAL "OPENGL33"
        OR CNA_GRAPHICS_RENDERER STREQUAL "WEBGL1" OR CNA_GRAPHICS_RENDERER STREQUAL "WEBGL2"
        OR CNA_GRAPHICS_RENDERER STREQUAL "VULKAN"))
    # ---- 3D house demo (EasyGL and Vulkan) -----------------------------------
    # The 3D house demo exercises the EasyGL 3D pipeline. Other renderers throw
    # "3D not supported", so the target is only enabled with a GL-family public renderer
    # (OPENGLES2/OPENGLES3/OPENGL33/WEBGL1/WEBGL2, all implemented by EasyGL -- see
    # plan_glbackends.md) or VULKAN.
    add_executable(cna_house3d_demo house3d_demo.cpp)

    if(EMSCRIPTEN)
        # ---- Emscripten / WebAssembly ----------------------------------------
        # Produce a self-contained HTML page. The scene is fully procedural so
        # no asset preloading is required.
        set_target_properties(cna_house3d_demo PROPERTIES SUFFIX ".html")
        # wasm-ld resolves circular static-lib references without linker groups.
        target_link_libraries(cna_house3d_demo PRIVATE
            CNA ${RENDERER_TARGET} SDL3::SDL3-static SHARP_RUNTIME)
        # plan_glbackends.md GLB-9 (revisited): MIN_WEBGL_VERSION/MAX_WEBGL_VERSION must match
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

# --- plan_software.md SOFTWARE-61/84: cross-renderer diagnostic comparator ---
# Standalone, renderer-independent (no CNA/SHARP_RUNTIME dependency) tool that diffs two raw
# 64x64 RGBA8 dumps produced by cross_renderer_diagnostic_scene.cpp (built once per renderer by
# the SOFTWARE and EASYGL suites) within a tolerance. Not wired into a single ctest run --
# CNA_GRAPHICS_RENDERER is a compile-time choice, so comparing two renderers' output needs two
# separate builds' dumps; see docs/software-renderer.md's "Cross-renderer diagnostic" section for
# the exact manual invocation.
if(CNA_BUILD_TESTS)
    add_executable(cna_diag_compare cross_renderer_diagnostic_compare.cpp)
endif()

# plan_html_dom.md Phase D10: a deliberately renderer-agnostic benchmark
# (graphics_renderer_benchmark.cpp), built for whichever Emscripten renderer the current
# configure selected. Comparing HTML_DOM against CANVAS/WEBGL2 means configuring and building this
# target three times, once per renderer (three separate build directories, e.g.
# cmake-build-canvas/cmake-build-webgl2/cmake-build-htmldom) -- this file just makes sure the
# target exists in each, not tied to any single CNA_GRAPHICS_RENDERER value the way the
# canvas/html-dom renderer example registrations are.
#
# Gated on EMSCRIPTEN alone: on a native (non-browser) renderer this benchmark's own JS timing
# (performance.now()) and result-publishing hooks don't exist, and the point of this benchmark is
# specifically a browser-vs-browser comparison.
if(CNA_BUILD_TESTS AND EMSCRIPTEN)
    add_executable(cna_bench_graphics_renderer graphics_renderer_benchmark.cpp)
    target_link_libraries(cna_bench_graphics_renderer PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    set_target_properties(cna_bench_graphics_renderer PROPERTIES SUFFIX ".html")
    target_link_options(cna_bench_graphics_renderer PRIVATE
        -sALLOW_MEMORY_GROWTH=1
        -sEXIT_RUNTIME=1
        -sEXPORTED_RUNTIME_METHODS=UTF8ToString)
    if(CNA_GRAPHICS_RENDERER STREQUAL "WEBGL2")
        # Emscripten defaults to -sMAX_WEBGL_VERSION=1; without this, the EasyGL implementation's
        # ES 3.0/WebGL2
        # SDL_GL_CONTEXT_MAJOR_VERSION=3 request (EasyGLRenderer.cpp) silently falls back to
        # a WebGL1 context, which then fails to compile EasyGL's GLSL ES 3.0 shaders. Mirrors the
        # cna_house3d_demo target's own WEBGL2/VULKAN Emscripten link options above.
        target_link_options(cna_bench_graphics_renderer PRIVATE
            "-sMIN_WEBGL_VERSION=2"
            "-sMAX_WEBGL_VERSION=2")
    endif()
endif()

# Module-local tests get the same headless-safe skip convention the root one-shot
# gives root-registered tests (see cmake/TestHelpers.cmake).
cna_apply_skip_convention()
