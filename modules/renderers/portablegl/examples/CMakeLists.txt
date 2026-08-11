# PortableGL renderer example/test registration (module-local, following the same pattern as
# stub/headless/software's own examples/CMakeLists.txt).
#
# From the root CMakeLists.txt include site: PortableGL (rswinkle/PortableGL, CPU software OpenGL
# 3.x-ish) graphics renderer tests. Deliberately no SDL_VIDEODRIVER/DISPLAY environment -- like
# HEADLESS/SOFTWARE/STUB, this renderer never needs either.

# Example/test executables historically live at the build root (scripts, docs and
# the golden-image working-directory conventions rely on it); keep them there.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
# Shared example-support headers ("common/PixelTestGame.hpp", ...) are owned by
# the graphics module's examples directory.
include_directories(${CNA_GRAPHICS_EXAMPLES_DIR})

if(CNA_BUILD_TESTS AND CNA_GRAPHICS_RENDERER STREQUAL "PORTABLEGL")
    enable_testing()

    macro(cna_portablegl_test target src)
        add_executable(${target} ${src})
        target_link_libraries(${target} PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
        if(TARGET SDL3::SDL3main)
            target_link_libraries(${target} PRIVATE SDL3::SDL3main)
        endif()
    endmacro()

    cna_portablegl_test(cna_test_portablegl_smoke portablegl_smoke_test.cpp)
    cna_register_renderer_test(NAME PortableGL_Smoke COMMAND cna_test_portablegl_smoke
        TIMEOUT 30 LABELS "PortableGL")

    cna_portablegl_test(cna_test_portablegl_textured_draw portablegl_textured_draw_test.cpp)
    cna_register_renderer_test(NAME PortableGL_TexturedDraw COMMAND cna_test_portablegl_textured_draw
        TIMEOUT 30 LABELS "PortableGL")

    cna_portablegl_test(cna_test_portablegl_pipeline_interleave portablegl_pipeline_interleave_test.cpp)
    cna_register_renderer_test(NAME PortableGL_PipelineInterleave
        COMMAND cna_test_portablegl_pipeline_interleave
        TIMEOUT 30 LABELS "PortableGL")

    cna_portablegl_test(cna_test_portablegl_depth_resize portablegl_depth_resize_test.cpp)
    cna_register_renderer_test(NAME PortableGL_DepthResize COMMAND cna_test_portablegl_depth_resize
        TIMEOUT 30 LABELS "PortableGL")

    cna_portablegl_test(cna_test_portablegl_edge_cases portablegl_edge_cases_test.cpp)
    cna_register_renderer_test(NAME PortableGL_EdgeCases COMMAND cna_test_portablegl_edge_cases
        TIMEOUT 60 LABELS "PortableGL")

    cna_portablegl_test(cna_test_portablegl_draw_offsets portablegl_draw_offsets_test.cpp)
    cna_register_renderer_test(NAME PortableGL_DrawOffsets COMMAND cna_test_portablegl_draw_offsets
        TIMEOUT 30 LABELS "PortableGL")

    cna_portablegl_test(cna_test_portablegl_effect_state portablegl_effect_state_test.cpp)
    cna_register_renderer_test(NAME PortableGL_EffectState COMMAND cna_test_portablegl_effect_state
        TIMEOUT 30 LABELS "PortableGL")

    cna_portablegl_test(cna_test_portablegl_blend portablegl_blend_test.cpp)
    cna_register_renderer_test(NAME PortableGL_Blend COMMAND cna_test_portablegl_blend
        TIMEOUT 30 LABELS "PortableGL")

    cna_portablegl_test(cna_test_portablegl_stencil portablegl_stencil_test.cpp)
    cna_register_renderer_test(NAME PortableGL_Stencil COMMAND cna_test_portablegl_stencil
        TIMEOUT 30 LABELS "PortableGL")

    cna_portablegl_test(cna_test_portablegl_raster_state portablegl_raster_state_test.cpp)
    cna_register_renderer_test(NAME PortableGL_RasterState COMMAND cna_test_portablegl_raster_state
        TIMEOUT 30 LABELS "PortableGL")

    cna_portablegl_test(cna_test_portablegl_sampler portablegl_sampler_test.cpp)
    cna_register_renderer_test(NAME PortableGL_Sampler COMMAND cna_test_portablegl_sampler
        TIMEOUT 30 LABELS "PortableGL")

    cna_portablegl_test(cna_test_portablegl_rejection portablegl_rejection_test.cpp)
    cna_register_renderer_test(NAME PortableGL_Rejection COMMAND cna_test_portablegl_rejection
        TIMEOUT 30 LABELS "PortableGL")
endif()

# Module-local tests get the same headless-safe skip convention the root one-shot
# gives root-registered tests (see cmake/TestHelpers.cmake).
cna_apply_skip_convention()
