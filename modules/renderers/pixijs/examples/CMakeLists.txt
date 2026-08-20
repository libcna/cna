# pixijs renderer example/test registration (module-local), mirroring canvas/html-dom's own
# examples/CMakeLists.txt shape.
#
# plans/plan_pixijs.md PIXIJS-84 (2026-08-17): a real Emscripten toolchain build of this target was
# performed and it compiles/links cleanly. Emits an .html shell (SUFFIX below), same shape as
# cna_demo_2d/cna_house3d_demo -- the default Emscripten shell provides the <canvas> element this
# renderer's own Module['canvas'] lookup (and the selected CNA platform's web video backend)
# expects.

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
include_directories(${CNA_GRAPHICS_EXAMPLES_DIR})

if(CNA_BUILD_TESTS AND CNA_GRAPHICS_RENDERER STREQUAL "PIXIJS")
    add_executable(cna_test_pixijs_smoke pixijs_smoke_test.cpp)
    # plans/plan_platform.md PLAT-58/PLAT-61: no windowing library on this link line. The smoke test
    # drives the public XNA surface and the renderer's own platform-neutral transforms only.
    target_link_libraries(cna_test_pixijs_smoke PRIVATE CNA)
    if(EMSCRIPTEN)
        set_target_properties(cna_test_pixijs_smoke PROPERTIES SUFFIX ".html")
    endif()
endif()

cna_apply_skip_convention()
