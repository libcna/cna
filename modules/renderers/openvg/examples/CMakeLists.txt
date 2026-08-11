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

    # Presentation transform (Letterbox/Overscan/Stretch/FixedHeightDynamicWidth/NativeBackBuffer),
    # window<->logical coordinate transforms, custom Viewport placement/persistence, resize
    # without Clear(), RasterizerState-driven scissor, the single-live-ShivaVG-context guard, and
    # swap interval control -- see that file's own top comment.
    cna_openvg_test(cna_test_openvg_presentation_viewport_scissor
        openvg_presentation_viewport_scissor_test.cpp)
    cna_register_renderer_test(NAME OpenVg_PresentationViewportScissor
        COMMAND cna_test_openvg_presentation_viewport_scissor TIMEOUT 30 LABELS "OpenVg")

    # Texture upload format/orientation pixel oracle (P1-3) and out-of-bounds Clamp edge-extension
    # (P1-4) -- see that file's own top comment.
    cna_openvg_test(cna_test_openvg_texture_orientation openvg_texture_orientation_test.cpp)
    cna_register_renderer_test(NAME OpenVg_TextureOrientation
        COMMAND cna_test_openvg_texture_orientation TIMEOUT 30 LABELS "OpenVg")

    # Blend mode pixel oracle (Opaque/AlphaBlend/NonPremultiplied), Additive/custom-BlendState
    # rejection, and BlendWriteState.ColorWriteChannels honored via glColorMask -- see that file's
    # own top comment.
    cna_openvg_test(cna_test_openvg_blend openvg_blend_test.cpp)
    cna_register_renderer_test(NAME OpenVg_Blend
        COMMAND cna_test_openvg_blend TIMEOUT 30 LABELS "OpenVg")

    # 3D Draw* rejection through public GraphicsDevice APIs, meaningful DepthStencilState/
    # RasterizerState rejection, and transactional RenderTarget2D-binding rejection -- see that
    # file's own top comment.
    cna_openvg_test(cna_test_openvg_modern_state_guard openvg_modern_state_guard_test.cpp)
    cna_register_renderer_test(NAME OpenVg_ModernStateGuard
        COMMAND cna_test_openvg_modern_state_guard TIMEOUT 30 LABELS "OpenVg")
endif()

# Module-local tests get the same headless-safe skip convention the root one-shot
# gives root-registered tests (see cmake/TestHelpers.cmake).
cna_apply_skip_convention()
