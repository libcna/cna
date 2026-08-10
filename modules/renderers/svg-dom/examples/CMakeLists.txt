# plan_svg_dom.md: SVG DOM renderer examples. The genuine, in-browser vertical-slice smoke test
# only builds under Emscripten (SVG_DOM is Emscripten-only, same gate as CANVAS/HTML_DOM).
#
# The native host-side contract test target (CNA_BUILD_SVG_DOM_HOST_TESTS) is NOT defined here:
# this directory is only reached via modules/renderers/CMakeLists.txt's ${CNA_SELECTED_RENDERER}
# dispatch, which requires SVG_DOM to be the SELECTED renderer -- itself gated to Emscripten-only
# builds by cmake/RendererSelection.cmake. That makes a host test defined here unreachable in any
# non-Emscripten configuration (the same latent gap HTML_DOM's own cna_test_htmldom_host has).
# See modules/renderers/CMakeLists.txt's own SHARED-HOOK REQUIREMENT comment for where the
# actually-reachable cna_test_svgdom_host target is defined instead, and why.
if(EMSCRIPTEN AND CNA_BUILD_TESTS)
    add_executable(cna_test_svgdom_smoke svgdom_smoke_test.cpp)
    target_link_libraries(cna_test_svgdom_smoke PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    set_target_properties(cna_test_svgdom_smoke PROPERTIES SUFFIX ".html")
    target_link_options(cna_test_svgdom_smoke PRIVATE
        -sALLOW_MEMORY_GROWTH=1
        -sEXIT_RUNTIME=1)
endif()

# Module-local tests get the same headless-safe skip convention the root one-shot
# gives root-registered tests (see cmake/TestHelpers.cmake).
cna_apply_skip_convention()
