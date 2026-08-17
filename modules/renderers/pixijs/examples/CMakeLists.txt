# pixijs renderer example/test registration (module-local), mirroring canvas/html-dom's own
# examples/CMakeLists.txt shape.
#
# plan_pixijs.md status block: this session has no Emscripten toolchain at all, so unlike
# CANVAS-15's own precedent ("configure/build verified under real emcc, run under node confirmed to
# fail before any Canvas-specific code runs"), NOTHING here has been configured or built even once.
# Registered the same way regardless (real executable target, not a ctest -- PIXIJS is
# Emscripten-only and this renderer needs a real browser to produce a meaningful PASS/FAIL, same
# reasoning as CANVAS-15) so the target exists and is ready to build the first time a real
# Emscripten toolchain is available (plan_pixijs.md PIXIJS-84).

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
include_directories(${CNA_GRAPHICS_EXAMPLES_DIR})

if(CNA_BUILD_TESTS AND CNA_GRAPHICS_RENDERER STREQUAL "PIXIJS")
    add_executable(cna_test_pixijs_smoke pixijs_smoke_test.cpp)
    target_link_libraries(cna_test_pixijs_smoke PRIVATE CNA SDL3::SDL3)
endif()

cna_apply_skip_convention()
