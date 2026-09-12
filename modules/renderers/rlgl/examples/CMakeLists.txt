# plans/plan_rlgl.md RLGL-007: runtime vertical-slice validation needs a real window and GL 3.3
# context. The executable is available even when CNA_BUILD_TESTS is off so dependency-constrained
# developer builds can run it directly under Xvfb; CTest registration remains opt-in with the
# repository's ordinary test switch.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
include_directories(${CNA_GRAPHICS_EXAMPLES_DIR})

if(NOT EMSCRIPTEN AND CNA_GRAPHICS_RENDERER STREQUAL "RLGL" AND TARGET SDL3::SDL3)
    add_executable(cna_test_rlgl_smoke rlgl_smoke_test.cpp)
    target_link_libraries(cna_test_rlgl_smoke PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_test_rlgl_smoke PRIVATE SDL3::SDL3main)
    endif()

    if(CNA_BUILD_TESTS)
        enable_testing()
        cna_register_renderer_test(NAME Rlgl_Smoke COMMAND cna_test_rlgl_smoke
            TIMEOUT 60 LABELS "GraphicsSmoke;Rlgl"
            ENVIRONMENT "SDL_VIDEODRIVER=offscreen;LIBGL_ALWAYS_SOFTWARE=1")
    endif()
endif()

cna_apply_skip_convention()
