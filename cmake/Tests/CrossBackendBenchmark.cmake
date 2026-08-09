# plan_html_dom.md Phase D10: a deliberately backend-agnostic benchmark
# (examples/graphics_backend_benchmark.cpp), built for whichever Emscripten backend the current
# configure selected. Comparing HTML_DOM against CANVAS/WEBGL2 means configuring and building this
# target three times, once per backend (three separate build directories, e.g.
# cmake-build-canvas/cmake-build-webgl2/cmake-build-htmldom) -- this file just makes sure the
# target exists in each, not tied to any single CNA_GRAPHICS_BACKEND value the way
# CanvasTests.cmake/HtmlDomTests.cmake are.
#
# Gated on EMSCRIPTEN alone: on a native (non-browser) backend this benchmark's own JS timing
# (performance.now()) and result-publishing hooks don't exist, and the point of this benchmark is
# specifically a browser-vs-browser comparison.
if(CNA_BUILD_TESTS AND EMSCRIPTEN)
    add_executable(cna_bench_graphics_backend examples/graphics_backend_benchmark.cpp)
    target_link_libraries(cna_bench_graphics_backend PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    set_target_properties(cna_bench_graphics_backend PROPERTIES SUFFIX ".html")
    target_link_options(cna_bench_graphics_backend PRIVATE
        -sALLOW_MEMORY_GROWTH=1
        -sEXIT_RUNTIME=1
        -sEXPORTED_RUNTIME_METHODS=UTF8ToString)
    if(CNA_GRAPHICS_BACKEND STREQUAL "WEBGL2")
        # Emscripten defaults to -sMAX_WEBGL_VERSION=1; without this, the EasyGL implementation's
        # ES 3.0/WebGL2
        # SDL_GL_CONTEXT_MAJOR_VERSION=3 request (EasyGLGraphicsBackend.cpp) silently falls back to
        # a WebGL1 context, which then fails to compile EasyGL's GLSL ES 3.0 shaders. Mirrors the
        # cna_house3d_demo target's own WEBGL2/VULKAN Emscripten link options above.
        target_link_options(cna_bench_graphics_backend PRIVATE
            "-sMIN_WEBGL_VERSION=2"
            "-sMAX_WEBGL_VERSION=2")
    endif()
endif()
