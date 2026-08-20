# plans/plan_svg_dom.md: SVG DOM renderer examples. The genuine, in-browser vertical-slice smoke test
# only builds under Emscripten (SVG_DOM is Emscripten-only, same gate as CANVAS/HTML_DOM).
#
# Native host-contract targets for all browser renderers are registered centrally in
# modules/renderers/CMakeLists.txt so they remain reachable outside selected-renderer dispatch.
if(EMSCRIPTEN AND CNA_BUILD_TESTS)
    add_executable(cna_test_svgdom_smoke svgdom_smoke_test.cpp)
    target_link_libraries(cna_test_svgdom_smoke PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    set_target_properties(cna_test_svgdom_smoke PROPERTIES SUFFIX ".html")
    target_link_options(cna_test_svgdom_smoke PRIVATE
        -sALLOW_MEMORY_GROWTH=1
        -sEXIT_RUNTIME=1)

    # Deep-remediation browser test suite (SVGDOM-A..H): RT-based deterministic pixel checks
    # (SVGDOM-B/D/E/F) and the SVG-backbuffer draw-order regression (SVGDOM-A), which needs a real
    # Playwright screenshot since no API rasterizes a live SVG subtree to pixels synchronously.
    add_executable(cna_test_svgdom_pixel_verification svgdom_pixel_verification_test.cpp)
    target_link_libraries(cna_test_svgdom_pixel_verification PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    set_target_properties(cna_test_svgdom_pixel_verification PROPERTIES SUFFIX ".html")
    target_link_options(cna_test_svgdom_pixel_verification PRIVATE
        -sALLOW_MEMORY_GROWTH=1
        -sEXIT_RUNTIME=1)

    add_executable(cna_test_svgdom_scissor_order svgdom_scissor_order_test.cpp)
    target_link_libraries(cna_test_svgdom_scissor_order PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    set_target_properties(cna_test_svgdom_scissor_order PROPERTIES SUFFIX ".html")
    target_link_options(cna_test_svgdom_scissor_order PRIVATE
        -sALLOW_MEMORY_GROWTH=1
        -sEXIT_RUNTIME=1)
endif()

# Module-local tests get the same headless-safe skip convention the root one-shot
# gives root-registered tests (see cmake/TestHelpers.cmake).
cna_apply_skip_convention()
