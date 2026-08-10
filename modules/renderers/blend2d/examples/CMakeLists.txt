# plan_blend2d.md: BLEND2D renderer example/test registration (module-local). The module is a
# no-op unless BLEND2D is selected.
#
# Tests are split by execution requirement, mirroring the SKIA renderer's own registration
# (modules/renderers/skia/examples/CMakeLists.txt): Blend2DSurface itself is pure Blend2D CPU
# rasterization with no SDL/window dependency at all, while the full renderer (window creation,
# GraphicsDevice/Game integration, presentation) needs a real X11/virtual display.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

if(CNA_BUILD_TESTS AND NOT EMSCRIPTEN AND NOT WIN32 AND CNA_GRAPHICS_RENDERER STREQUAL "BLEND2D")
    enable_testing()

    macro(cna_blend2d_test target src)
        add_executable(${target} ${src})
        target_link_libraries(${target} PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
        if(TARGET SDL3::SDL3main)
            target_link_libraries(${target} PRIVATE SDL3::SDL3main)
        endif()
    endmacro()

    # Window-independent: exercises Blend2DSurface (BLImage/BLContext) directly.
    cna_blend2d_test(cna_test_blend2d_surface_raster blend2d_surface_raster_test.cpp)
    cna_register_renderer_test(NAME Blend2D_Surface_Raster COMMAND cna_test_blend2d_surface_raster
        TIMEOUT 30 LABELS "Blend2D;Raster")

    # Display-required: real window/GraphicsDevice/Game integration and presentation.
    cna_blend2d_test(cna_test_blend2d_smoke blend2d_smoke_test.cpp)
    cna_register_renderer_test(NAME Blend2D_Smoke COMMAND cna_test_blend2d_smoke
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}"
        LABELS "Blend2D;Display")
endif()

cna_apply_skip_convention()
