if(CNA_BUILD_TESTS AND CNA_GRAPHICS_BACKEND STREQUAL "CANVAS")
    add_executable(cna_test_canvas_smoke examples/canvas_smoke_test.cpp)
    target_link_libraries(cna_test_canvas_smoke PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)

    # CNA::UnsupportedGraphicsCallBehavior: Canvas's unsupported "fire and forget" 3D calls can be
    # configured to silently no-op instead of throwing; resource creation always throws. Twin of
    # cna_test_sdl_unsupported_call_behavior/cna_test_dx3_unsupported_call_behavior.
    add_executable(cna_test_canvas_unsupported_call_behavior examples/canvas_unsupported_call_behavior_test.cpp)
    target_link_libraries(cna_test_canvas_unsupported_call_behavior PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
endif()
