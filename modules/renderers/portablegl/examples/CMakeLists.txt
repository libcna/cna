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
endif()

# Module-local tests get the same headless-safe skip convention the root one-shot
# gives root-registered tests (see cmake/TestHelpers.cmake).
cna_apply_skip_convention()
