if(CNA_BUILD_TESTS AND CNA_GRAPHICS_BACKEND STREQUAL "CANVAS")
    add_executable(cna_test_canvas_smoke examples/canvas_smoke_test.cpp)
    target_link_libraries(cna_test_canvas_smoke PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)

    # CNA::GraphicsCapability: Canvas is 2D-only -- SupportsCapability() reports which
    # capabilities are genuinely absent (ThreeD and everything that depends on it). Twin of
    # cna_test_sdl_graphics_capability/cna_test_dx3_graphics_capability.
    add_executable(cna_test_canvas_graphics_capability examples/canvas_graphics_capability_test.cpp)
    target_link_libraries(cna_test_canvas_graphics_capability PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
endif()
