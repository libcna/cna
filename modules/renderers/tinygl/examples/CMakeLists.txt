# TinyGL renderer example/test registration (module-local, following the same pattern as
# portablegl/stub/headless/software's own examples/CMakeLists.txt).
#
# Deliberately no SDL_VIDEODRIVER/DISPLAY environment -- like HEADLESS/SOFTWARE/STUB/PORTABLEGL,
# this renderer never needs either.

# Example/test executables historically live at the build root (scripts, docs and
# the golden-image working-directory conventions rely on it); keep them there.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
# Shared example-support headers ("common/PixelTestGame.hpp", ...) are owned by
# the graphics module's examples directory.
include_directories(${CNA_GRAPHICS_EXAMPLES_DIR})

if(CNA_BUILD_TESTS AND CNA_GRAPHICS_RENDERER STREQUAL "TINYGL")
    enable_testing()

    macro(cna_tinygl_test target src)
        add_executable(${target} ${src})
        target_link_libraries(${target} PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
        if(TARGET SDL3::SDL3main)
            target_link_libraries(${target} PRIVATE SDL3::SDL3main)
        endif()
    endmacro()

    cna_tinygl_test(cna_test_tinygl_smoke tinygl_smoke_test.cpp)
    cna_register_renderer_test(NAME TinyGL_Smoke COMMAND cna_test_tinygl_smoke
        TIMEOUT 30 LABELS "TinyGL")

    cna_tinygl_test(cna_test_tinygl_texture_sprite tinygl_texture_sprite_test.cpp)
    cna_register_renderer_test(NAME TinyGL_TextureSprite COMMAND cna_test_tinygl_texture_sprite
        TIMEOUT 30 LABELS "TinyGL")

    cna_tinygl_test(cna_test_tinygl_3d tinygl_3d_test.cpp)
    cna_register_renderer_test(NAME TinyGL_3D COMMAND cna_test_tinygl_3d
        TIMEOUT 30 LABELS "TinyGL")

    cna_tinygl_test(cna_test_tinygl_state tinygl_state_test.cpp)
    cna_register_renderer_test(NAME TinyGL_State COMMAND cna_test_tinygl_state
        TIMEOUT 30 LABELS "TinyGL")

    cna_tinygl_test(cna_test_tinygl_rejection tinygl_rejection_test.cpp)
    cna_register_renderer_test(NAME TinyGL_Rejection COMMAND cna_test_tinygl_rejection
        TIMEOUT 30 LABELS "TinyGL")

    cna_tinygl_test(cna_test_tinygl_contract tinygl_contract_test.cpp)
    cna_register_renderer_test(NAME TinyGL_Contract COMMAND cna_test_tinygl_contract
        TIMEOUT 30 LABELS "TinyGL")
endif()

# Module-local tests get the same headless-safe skip convention the root one-shot
# gives root-registered tests (see cmake/TestHelpers.cmake).
cna_apply_skip_convention()
