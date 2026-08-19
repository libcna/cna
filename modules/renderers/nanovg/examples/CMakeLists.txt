# nanovg renderer example/test registration (module-local, module-examples campaign).
#
# NANOVG creates a real desktop OpenGL context via SDL (same "own GL context" shape as
# OPENGL1/OPENGL2/OPENVG/SOFTWARE) and needs a real GPU/display -- these tests run under the same
# Xvfb-based headless validation this project's other windowed renderers use, and self-skip
# (exit 77) via PixelTestGame::RunPixelTest's own SDL_INIT_VIDEO probe when no display exists at
# all.

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
include_directories(${CNA_GRAPHICS_EXAMPLES_DIR})

if(CNA_BUILD_TESTS AND CNA_GRAPHICS_RENDERER STREQUAL "NANOVG")
    enable_testing()

    macro(cna_nanovg_test target src)
        add_executable(${target} ${src})
        target_link_libraries(${target} PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
        if(TARGET SDL3::SDL3main)
            target_link_libraries(${target} PRIVATE SDL3::SDL3main)
        endif()
    endmacro()

    cna_nanovg_test(cna_test_nanovg_smoke nanovg_smoke_test.cpp)
    cna_register_renderer_test(NAME NanoVg_Smoke COMMAND cna_test_nanovg_smoke
        TIMEOUT 30 LABELS "NanoVg")

    # Decisive geometry oracle for NanoVgSpriteBatchRenderer's translate/rotate/scale/origin
    # matrix composition -- see that class's own header comment and this test file's own top
    # comment.
    cna_nanovg_test(cna_test_nanovg_spritebatch_rotation nanovg_spritebatch_rotation_test.cpp)
    cna_register_renderer_test(NAME NanoVg_SpriteBatch_Rotation
        COMMAND cna_test_nanovg_spritebatch_rotation TIMEOUT 30 LABELS "NanoVg")

    # NOT the shared modules/graphics/examples/unsupported_3d_call_behavior_test.cpp fixture
    # Canvas/FreeDirect/OpenVG/SDL_Renderer register: that file's Texture3D expectations assume
    # GraphicsCapability::Texture3D reports true, which conflicts with Texture3DTests.cpp's own
    # dual self-skipping fixture design -- NANOVG reports Texture3D false, so this renderer-owned
    # test covers the same WarnAndStub/Throw ground truthfully instead.
    cna_nanovg_test(cna_test_nanovg_unsupported_3d_behavior nanovg_unsupported_3d_behavior_test.cpp)
    cna_register_renderer_test(NAME NanoVg_Unsupported3DBehavior
        COMMAND cna_test_nanovg_unsupported_3d_behavior TIMEOUT 30 LABELS "NanoVg")

    # Blend mode pixel oracle (Opaque/AlphaBlend/NonPremultiplied/Additive), and deterministic
    # rejection of both a custom BlendState and a non-default ColorWriteChannels write mask --
    # see that file's own top comment.
    cna_nanovg_test(cna_test_nanovg_blend nanovg_blend_test.cpp)
    cna_register_renderer_test(NAME NanoVg_Blend
        COMMAND cna_test_nanovg_blend TIMEOUT 30 LABELS "NanoVg")

    # Texture upload orientation, UpdatePixels, partial-sourceRectangle nvgImagePattern box math
    # (including two spans beyond OpenVG's own precedent test), tint, rotation, both SpriteEffects
    # flips, and out-of-bounds Clamp/Wrap/Mirror sampler behavior -- see that file's own top comment.
    cna_nanovg_test(cna_test_nanovg_texture_orientation nanovg_texture_orientation_test.cpp)
    cna_register_renderer_test(NAME NanoVg_TextureOrientation
        COMMAND cna_test_nanovg_texture_orientation TIMEOUT 30 LABELS "NanoVg")

    # SamplerState honesty: Point vs Linear at sample points where the two genuinely disagree,
    # separate minification/magnification components, Clamp/Wrap/Mirror addressing on an
    # out-of-bounds source rectangle, per-batch sampler independence for one shared texture, and
    # deterministic rejection of Anisotropic -- see that file's own top comment.
    cna_nanovg_test(cna_test_nanovg_sampler_state nanovg_sampler_state_test.cpp)
    cna_register_renderer_test(NAME NanoVg_SamplerState
        COMMAND cna_test_nanovg_sampler_state TIMEOUT 30 LABELS "NanoVg")

    # SpriteSortMode::Immediate must be a real ordering guarantee: a GraphicsDevice operation
    # issued between two Draw() calls has to land between them, not before the whole batch --
    # see that file's own top comment.
    cna_nanovg_test(cna_test_nanovg_immediate_mode nanovg_immediate_mode_test.cpp)
    cna_register_renderer_test(NAME NanoVg_ImmediateMode
        COMMAND cna_test_nanovg_immediate_mode TIMEOUT 30 LABELS "NanoVg")

    # SpriteBatch quads must rasterize with XNA's hard per-pixel-centre coverage rule, not with
    # NanoVG's own vector-path edge feathering -- see that file's own top comment.
    cna_nanovg_test(cna_test_nanovg_sprite_rasterization nanovg_sprite_rasterization_test.cpp)
    cna_register_renderer_test(NAME NanoVg_SpriteRasterization
        COMMAND cna_test_nanovg_sprite_rasterization TIMEOUT 30 LABELS "NanoVg")

    # Presentation-mode viewport math, coordinate transforms, custom Viewport, resize-without-Clear,
    # RasterizerState-driven scissor, multi-instance coexistence, repeated construct/destroy cycling,
    # and SwapInterval -- see that file's own top comment.
    cna_nanovg_test(cna_test_nanovg_presentation_viewport_scissor
        nanovg_presentation_viewport_scissor_test.cpp)
    cna_register_renderer_test(NAME NanoVg_PresentationViewportScissor
        COMMAND cna_test_nanovg_presentation_viewport_scissor TIMEOUT 30 LABELS "NanoVg")
endif()

# Module-local tests get the same headless-safe skip convention the root one-shot
# gives root-registered tests (see cmake/TestHelpers.cmake).
cna_apply_skip_convention()
