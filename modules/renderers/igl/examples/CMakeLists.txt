# igl renderer example/test registration (module-local, the shape every renderer family uses since
# the module-examples campaign).
#
# plan_igl.md: IGL (facebook/igl) renderer integration tests. Like the LLGL/VULKAN/MAGNUM blocks
# these need a real windowing system and a real driver -- IGL creates its device from the platform
# window itself -- so they run against CNA_TEST_DISPLAY (a live X server, or an Xvfb one with Mesa's
# software rasterizer behind it). Which native API is underneath is decided at runtime by
# CNA_IGL_BACKEND, so the same binaries exercise whichever backend was selected.

# Example/test executables historically live at the build root (scripts, docs and the golden-image
# working-directory conventions rely on it); keep them there.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
# Shared example-support headers ("common/PixelTestGame.hpp", ...) are owned by the graphics
# module's examples directory.
include_directories(${CNA_GRAPHICS_EXAMPLES_DIR})

if(CNA_BUILD_TESTS AND NOT EMSCRIPTEN AND CNA_GRAPHICS_RENDERER STREQUAL "IGL")
    enable_testing()

    macro(cna_igl_test target src)
        add_executable(${target} ${src})
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            # CNA and the renderer target call into each other (GraphicsDevice constructs the
            # renderer; the renderer's factory lives in the renderer library), so they are linked as
            # one group rather than in a single pass -- the same shape every other renderer's own
            # test macro uses here.
            target_link_libraries(${target} PRIVATE
                CNA
                SHARP_RUNTIME SDL3::SDL3)
        else()
            target_link_libraries(${target} PRIVATE CNA ${RENDERER_TARGET} SHARP_RUNTIME SDL3::SDL3)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(${target} PRIVATE SDL3::SDL3main)
        endif()
    endmacro()

    # IGL-9: the device comes up, names its backend, and survives a real frame loop.
    cna_igl_test(cna_test_igl_smoke igl_smoke_test.cpp)
    cna_register_renderer_test(NAME Igl_Smoke COMMAND cna_test_igl_smoke
        TIMEOUT 90 LABELS "GraphicsSmoke;Igl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # IGL-14: SpriteBatch geometry, sampling, tinting and blend state, checked by pixel readback.
    cna_igl_test(cna_test_igl_2d igl_2d_test.cpp)
    cna_register_renderer_test(NAME Igl_2D COMMAND cna_test_igl_2d
        TIMEOUT 90 LABELS "Igl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # IGL-20: vertex/index buffers, the generated effect shader and a real depth test.
    cna_igl_test(cna_test_igl_3d igl_3d_test.cpp)
    cna_register_renderer_test(NAME Igl_3D COMMAND cna_test_igl_3d
        TIMEOUT 90 LABELS "Igl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # IGL-24: off-screen targets, their readback row order, and their sampling orientation.
    cna_igl_test(cna_test_igl_rendertarget igl_rendertarget_test.cpp)
    cna_register_renderer_test(NAME Igl_RenderTarget COMMAND cna_test_igl_rendertarget
        TIMEOUT 90 LABELS "Igl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
endif()
