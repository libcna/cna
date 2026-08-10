# openvg renderer example/test registration (module-local, module-examples campaign).
#
# OPENVG creates a real desktop OpenGL context via SDL (same "own GL context" shape as
# OPENGL1/OPENGL2/SOFTWARE) and needs a real GPU/display -- these tests run under the same
# Xvfb-based headless validation this project's other windowed renderers use, and self-skip
# (exit 77) via PixelTestGame::RunPixelTest's own SDL_INIT_VIDEO probe when no display exists at
# all.

# Example/test executables historically live at the build root (scripts, docs and
# the golden-image working-directory conventions rely on it); keep them there.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
# Shared example-support headers ("common/PixelTestGame.hpp", ...) are owned by
# the graphics module's examples directory.
include_directories(${CNA_GRAPHICS_EXAMPLES_DIR})

if(CNA_BUILD_TESTS AND CNA_GRAPHICS_RENDERER STREQUAL "OPENVG")
    enable_testing()

    macro(cna_openvg_test target src)
        add_executable(${target} ${src})
        target_link_libraries(${target} PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
        if(TARGET SDL3::SDL3main)
            target_link_libraries(${target} PRIVATE SDL3::SDL3main)
        endif()
    endmacro()

    cna_openvg_test(cna_test_openvg_smoke openvg_smoke_test.cpp)
    cna_register_renderer_test(NAME OpenVg_Smoke COMMAND cna_test_openvg_smoke
        TIMEOUT 30 LABELS "OpenVg")

    # Decisive geometry oracle for OpenVgSpriteBatchRenderer's device-flip + translate/rotate/
    # scale/origin matrix composition -- see that class's own header comment and this test file's
    # own top comment.
    cna_openvg_test(cna_test_openvg_spritebatch_rotation openvg_spritebatch_rotation_test.cpp)
    cna_register_renderer_test(NAME OpenVg_SpriteBatch_Rotation
        COMMAND cna_test_openvg_spritebatch_rotation TIMEOUT 30 LABELS "OpenVg")

    # NOT the shared modules/graphics/examples/unsupported_3d_call_behavior_test.cpp fixture
    # Canvas/FreeDirect/ASCII/SDL_Renderer register: that file's Texture3D expectations assume
    # GraphicsCapability::Texture3D reports true (verified against SDL_Renderer's own inherited
    # default), which conflicts with Texture3DTests.cpp's own dual self-skipping fixture design
    # (Texture3DTest skips / Texture3DUnsupportedRendererTest asserts-throws based on that exact
    # flag) -- OpenVG reports Texture3D false to satisfy the latter, more authoritative suite, so
    # this renderer-owned test covers the same WarnAndStub/Throw ground truthfully instead. See
    # docs/openvg-renderer.md's capability table.
    cna_openvg_test(cna_test_openvg_unsupported_3d_behavior openvg_unsupported_3d_behavior_test.cpp)
    cna_register_renderer_test(NAME OpenVg_Unsupported3DBehavior
        COMMAND cna_test_openvg_unsupported_3d_behavior TIMEOUT 30 LABELS "OpenVg")
endif()

# Module-local tests get the same headless-safe skip convention the root one-shot
# gives root-registered tests (see cmake/TestHelpers.cmake).
cna_apply_skip_convention()
