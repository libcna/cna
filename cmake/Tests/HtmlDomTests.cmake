# plan_html_dom.md HTMLDOM-15/HTMLDOM-72: the HTML_DOM backend's end-to-end smoke test.
#
# Deliberately NOT registered as a CTest, for the same reason every CANVAS entry is not: this
# backend produces a wasm module that only runs inside a browser -- SDL_Init(SDL_INIT_VIDEO) itself
# throws under `node` (no DOM), before any backend code runs. Unlike CANVAS, though, this one is
# genuinely runnable here: scripts/run-htmldom-browser-test.sh serves the generated page and drives
# it through headless Chromium, reading the PASS/FAIL lines the test prints.
#
# SUFFIX ".html" makes Emscripten emit its default shell page (which provides the <canvas> element
# SDL3 binds to and this backend anchors its DOM surface over) alongside the .js/.wasm.
if(CNA_BUILD_TESTS AND CNA_GRAPHICS_BACKEND STREQUAL "HTML_DOM")
    add_executable(cna_test_htmldom_smoke examples/htmldom_smoke_test.cpp)
    target_link_libraries(cna_test_htmldom_smoke PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    set_target_properties(cna_test_htmldom_smoke PROPERTIES SUFFIX ".html")
    # UTF8ToString is referenced from an EM_JS body rather than from C++, so it has to be kept
    # explicitly -- the linker cannot see that use and would otherwise drop it.
    target_link_options(cna_test_htmldom_smoke PRIVATE
        -sALLOW_MEMORY_GROWTH=1
        -sEXIT_RUNTIME=1
        -sEXPORTED_RUNTIME_METHODS=UTF8ToString)

    # NOXNA. A small visual demo (examples/htmldom_visual_demo.cpp) that exists to be looked at
    # rather than asserted against -- distinct from the smoke test above. Runs forever (no Exit()),
    # so it is driven by taking a screenshot after a short settle time rather than by reading
    # PASS/FAIL console lines.
    add_executable(cna_htmldom_visual_demo examples/htmldom_visual_demo.cpp)
    target_link_libraries(cna_htmldom_visual_demo PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    set_target_properties(cna_htmldom_visual_demo PROPERTIES SUFFIX ".html")
    target_link_options(cna_htmldom_visual_demo PRIVATE -sALLOW_MEMORY_GROWTH=1)

    # plan_html_dom.md Phase D9 (HTMLDOM-84/85/86/87): pixel-exact verification of the tint,
    # AlphaBlend un-premultiply, Opaque alpha-strip and Additive composite maths against real
    # rendered/read-back pixels, not just structural checks. Driven by the same
    # run-htmldom-browser-test.sh / htmldom-browser-test.mjs harness as the smoke test.
    add_executable(cna_test_htmldom_pixel_verification examples/htmldom_pixel_verification_test.cpp)
    target_link_libraries(cna_test_htmldom_pixel_verification PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    set_target_properties(cna_test_htmldom_pixel_verification PROPERTIES SUFFIX ".html")
    target_link_options(cna_test_htmldom_pixel_verification PRIVATE
        -sALLOW_MEMORY_GROWTH=1
        -sEXIT_RUNTIME=1)

    # plan_html_dom.md Phase D9 (HTMLDOM-89/90): a performance benchmark (500 animated sprites/
    # frame, steady-state ms/frame) and a 300-frame stability run (oscillating sprite counts,
    # cycling tints forcing real variant-cache LRU eviction). Driven by the same harness.
    add_executable(cna_test_htmldom_stress examples/htmldom_stress_test.cpp)
    target_link_libraries(cna_test_htmldom_stress PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    set_target_properties(cna_test_htmldom_stress PROPERTIES SUFFIX ".html")
    target_link_options(cna_test_htmldom_stress PRIVATE
        -sALLOW_MEMORY_GROWTH=1
        -sEXIT_RUNTIME=1)

    # plan_html_dom.md HTMLDOM-95: proves texture/render-target dispose actually shrinks the JS-side
    # texture registry (Module['cnaDomTextures']) back down, rather than trusting the destructor
    # chain by code review alone. Driven by the same harness.
    add_executable(cna_test_htmldom_dispose examples/htmldom_dispose_test.cpp)
    target_link_libraries(cna_test_htmldom_dispose PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    set_target_properties(cna_test_htmldom_dispose PROPERTIES SUFFIX ".html")
    target_link_options(cna_test_htmldom_dispose PRIVATE
        -sALLOW_MEMORY_GROWTH=1
        -sEXIT_RUNTIME=1)

    # plan_html_dom.md HTMLDOM-116: measures and bounds the sprite pool's own compositor-layer/DOM
    # memory retention (5,000/10,000-sprite bursts, then a genuinely settled long idle period) rather
    # than trusting the "targets normal 2D games" claim on paper. Driven by the same harness.
    add_executable(cna_test_htmldom_memory examples/htmldom_memory_test.cpp)
    target_link_libraries(cna_test_htmldom_memory PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    set_target_properties(cna_test_htmldom_memory PROPERTIES SUFFIX ".html")
    target_link_options(cna_test_htmldom_memory PRIVATE
        -sALLOW_MEMORY_GROWTH=1
        -sEXIT_RUNTIME=1)

    # plan_html_dom.md HTMLDOM-112: one target that builds every browser test page, so
    # scripts/run-htmldom-test-suite.sh (and CI) can build the whole suite with a single
    # `cmake --build ... --target cna_test_htmldom_all` instead of four separate target names.
    add_custom_target(cna_test_htmldom_all DEPENDS
        cna_test_htmldom_smoke
        cna_test_htmldom_pixel_verification
        cna_test_htmldom_stress
        cna_test_htmldom_dispose
        cna_test_htmldom_memory)
endif()

# Post-audit host-side contract coverage.  This deliberately compiles the HTML DOM implementation
# itself into a native GTest executable, where every EM_JS body is excluded by __EMSCRIPTEN__.
# It is not browser/runtime coverage; it exists so configurations without an Emscripten SDK can
# still exercise the CNA-owned validation, row-packing, draw-command and wrapper-lifetime code.
option(CNA_BUILD_HTML_DOM_HOST_TESTS
    "Build native host-side HTML DOM backend contract tests" OFF)

if(CNA_BUILD_TESTS AND CNA_BUILD_HTML_DOM_HOST_TESTS AND NOT EMSCRIPTEN)
    add_executable(cna_test_htmldom_host
        tests/CNA/Internal/Backends/HtmlDom/HtmlDomGraphicsBackendTests.cpp
        src/CNA/Internal/Backends/HtmlDom/HtmlDomGraphicsBackend.cpp
        src/CNA/Internal/Backends/HtmlDom/HtmlDomRenderTargetBackend.cpp
        src/CNA/Internal/Backends/HtmlDom/HtmlDomSpriteBatchBackend.cpp
        src/CNA/Internal/Backends/HtmlDom/HtmlDomState.cpp
        src/CNA/Internal/Backends/HtmlDom/HtmlDomTextureBackend.cpp)
    target_compile_definitions(cna_test_htmldom_host PRIVATE CNA_HTML_DOM_HOST_TESTS=1)
    target_link_libraries(cna_test_htmldom_host PRIVATE CNA SHARP_RUNTIME gtest_main SDL3::SDL3)
    add_test(NAME HtmlDomHostContracts COMMAND cna_test_htmldom_host)
    set_tests_properties(HtmlDomHostContracts PROPERTIES LABELS "HTML_DOM;host-only")
endif()
