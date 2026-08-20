# html-dom renderer example/test registration (module-local; this file is the
# former cmake/Tests/HtmlDomTests.cmake, moved when examples moved into their owning
# physical modules).
#
# From the root CMakeLists.txt include site:
# plans/plan_html_dom.md HTMLDOM-15/72: the HTML_DOM (CSS-composited DOM elements) renderer's smoke test.
# Like CANVAS's above it is a real executable rather than a ctest -- it needs a browser, not node --
# but unlike CANVAS it has a driver that actually runs it here
# (scripts/run-htmldom-browser-test.sh, headless Chromium via Playwright).

# Example/test executables historically live at the build root (scripts, docs and
# the golden-image working-directory conventions rely on it); keep them there.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
# Shared example-support headers ("common/PixelTestGame.hpp", ...) are owned by
# the graphics module's examples directory.
include_directories(${CNA_GRAPHICS_EXAMPLES_DIR})

# plans/plan_html_dom.md HTMLDOM-15/HTMLDOM-72: the HTML_DOM renderer's end-to-end smoke test.
#
# Deliberately NOT registered as a CTest, for the same reason every CANVAS entry is not: this
# renderer produces a wasm module that only runs inside a browser -- SDL_Init(SDL_INIT_VIDEO) itself
# throws under `node` (no DOM), before any renderer code runs. Unlike CANVAS, though, this one is
# genuinely runnable here: scripts/run-htmldom-browser-test.sh serves the generated page and drives
# it through headless Chromium, reading the PASS/FAIL lines the test prints.
#
# SUFFIX ".html" makes Emscripten emit its default shell page (which provides the <canvas> element
# SDL3 binds to and this renderer anchors its DOM surface over) alongside the .js/.wasm.
if(CNA_BUILD_TESTS AND CNA_GRAPHICS_RENDERER STREQUAL "HTML_DOM")
    add_executable(cna_test_htmldom_smoke htmldom_smoke_test.cpp)
    target_link_libraries(cna_test_htmldom_smoke PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    set_target_properties(cna_test_htmldom_smoke PROPERTIES SUFFIX ".html")
    # UTF8ToString is referenced from an EM_JS body rather than from C++, so it has to be kept
    # explicitly -- the linker cannot see that use and would otherwise drop it.
    target_link_options(cna_test_htmldom_smoke PRIVATE
        -sALLOW_MEMORY_GROWTH=1
        -sEXIT_RUNTIME=1
        -sEXPORTED_RUNTIME_METHODS=UTF8ToString)

    # CNAEXT. A small visual demo (htmldom_visual_demo.cpp) that exists to be looked at
    # rather than asserted against -- distinct from the smoke test above. Runs forever (no Exit()),
    # so it is driven by taking a screenshot after a short settle time rather than by reading
    # PASS/FAIL console lines.
    add_executable(cna_htmldom_visual_demo htmldom_visual_demo.cpp)
    target_link_libraries(cna_htmldom_visual_demo PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    set_target_properties(cna_htmldom_visual_demo PROPERTIES SUFFIX ".html")
    target_link_options(cna_htmldom_visual_demo PRIVATE -sALLOW_MEMORY_GROWTH=1)

    # plans/plan_html_dom.md Phase D9 (HTMLDOM-84/85/86/87): pixel-exact verification of the tint,
    # AlphaBlend un-premultiply, Opaque alpha-strip and Additive composite maths against real
    # rendered/read-back pixels, not just structural checks. Driven by the same
    # run-htmldom-browser-test.sh / htmldom-browser-test.mjs harness as the smoke test.
    add_executable(cna_test_htmldom_pixel_verification htmldom_pixel_verification_test.cpp)
    target_link_libraries(cna_test_htmldom_pixel_verification PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    set_target_properties(cna_test_htmldom_pixel_verification PROPERTIES SUFFIX ".html")
    target_link_options(cna_test_htmldom_pixel_verification PRIVATE
        -sALLOW_MEMORY_GROWTH=1
        -sEXIT_RUNTIME=1)

    # plans/plan_html_dom.md Phase D9 (HTMLDOM-89/90): a performance benchmark (500 animated sprites/
    # frame, steady-state ms/frame) and a 300-frame stability run (oscillating sprite counts,
    # cycling tints forcing real variant-cache LRU eviction). Driven by the same harness.
    add_executable(cna_test_htmldom_stress htmldom_stress_test.cpp)
    target_link_libraries(cna_test_htmldom_stress PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    set_target_properties(cna_test_htmldom_stress PROPERTIES SUFFIX ".html")
    target_link_options(cna_test_htmldom_stress PRIVATE
        -sALLOW_MEMORY_GROWTH=1
        -sEXIT_RUNTIME=1)

    # plans/plan_html_dom.md HTMLDOM-95: proves texture/render-target dispose actually shrinks the JS-side
    # texture registry (Module['cnaDomTextures']) back down, rather than trusting the destructor
    # chain by code review alone. Driven by the same harness.
    add_executable(cna_test_htmldom_dispose htmldom_dispose_test.cpp)
    target_link_libraries(cna_test_htmldom_dispose PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    set_target_properties(cna_test_htmldom_dispose PROPERTIES SUFFIX ".html")
    target_link_options(cna_test_htmldom_dispose PRIVATE
        -sALLOW_MEMORY_GROWTH=1
        -sEXIT_RUNTIME=1)

    # plans/plan_html_dom.md HTMLDOM-116: measures and bounds the sprite pool's own compositor-layer/DOM
    # memory retention (5,000/10,000-sprite bursts, then a genuinely settled long idle period) rather
    # than trusting the "targets normal 2D games" claim on paper. Driven by the same harness.
    add_executable(cna_test_htmldom_memory htmldom_memory_test.cpp)
    target_link_libraries(cna_test_htmldom_memory PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    set_target_properties(cna_test_htmldom_memory PROPERTIES SUFFIX ".html")
    target_link_options(cna_test_htmldom_memory PRIVATE
        -sALLOW_MEMORY_GROWTH=1
        -sEXIT_RUNTIME=1)

    # plans/plan_html_dom.md HTMLDOM-112: one target that builds every browser test page, so
    # scripts/run-htmldom-test-suite.sh (and CI) can build the whole suite with a single
    # `cmake --build ... --target cna_test_htmldom_all` instead of four separate target names.
    add_custom_target(cna_test_htmldom_all DEPENDS
        cna_test_htmldom_smoke
        cna_test_htmldom_pixel_verification
        cna_test_htmldom_stress
        cna_test_htmldom_dispose
        cna_test_htmldom_memory)
endif()

# Module-local tests get the same headless-safe skip convention the root one-shot
# gives root-registered tests (see cmake/TestHelpers.cmake).
cna_apply_skip_convention()
