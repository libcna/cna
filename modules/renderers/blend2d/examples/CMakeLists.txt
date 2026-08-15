# plan_blend2d.md: BLEND2D renderer example/test registration (module-local). The module is a
# no-op unless BLEND2D is selected.
#
# Tests are split by execution requirement, mirroring the SKIA renderer's own registration
# (modules/renderers/skia/examples/CMakeLists.txt): Blend2DSurface itself is pure Blend2D CPU
# rasterization with no window dependency at all, while full GraphicsDevice/Game integration uses
# the selected platform and its surface presenter (a real X11/virtual display for the smoke test).
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
include_directories(${CNA_GRAPHICS_EXAMPLES_DIR})

if(CNA_BUILD_TESTS AND NOT EMSCRIPTEN AND NOT WIN32 AND CNA_GRAPHICS_RENDERER STREQUAL "BLEND2D")
    enable_testing()

    macro(cna_blend2d_test target src)
        add_executable(${target} ${src})
        # CNA's own aggregate static-archive order (modules/CMakeLists.txt) is not a true
        # topological sort of every private symbol dependency (cna_graphics_core privately
        # needs cna_input's TouchPanel/Mouse/TextInputEXT symbols, but cna_input is listed
        # ahead of it), so plain left-to-right archive resolution can leave a real symbol
        # unresolved depending on exactly which object files a given executable happens to
        # pull in -- observed for this test executable (which never references GraphicsDevice/
        # cna_input directly, unlike the Game-based Blend2D_Smoke test) under the ASan/UBSan
        # build. Wrapping "CNA" itself in --start-group/--end-group does not help: CMake
        # expands an INTERFACE library's transitive usage requirements at generation time
        # rather than literally splicing them between the surrounding raw flag strings, so the
        # group ends up empty and every real archive lands after --end-group anyway. Listing
        # CNA's own real static-library members here (mirroring modules/CMakeLists.txt's CNA
        # INTERFACE target exactly) inside the group avoids that expansion entirely -- ld then
        # re-scans the whole set until no new symbol resolves, which is correct regardless of
        # the underlying cause. Target-local: does not change CNA's own link order or any
        # other renderer's.
        target_link_libraries(${target} PRIVATE
            "-Wl,--start-group"
            cna_runtime cna_devices cna_devices_ext cna_graphics_ext cna_cnaext cna_storage
            cna_content cna_media cna_audio cna_input cna_graphics_core cna_core cna_math
            cna_build_flags ${RENDERER_TARGET}
            "-Wl,--end-group"
            SHARP_RUNTIME SDL3::SDL3)
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

    # Deep correctness/safety regression suite: drives Blend2DRenderer/Blend2DSpriteBatchRenderer
    # directly through the SDL test presenter, but SDL_VIDEODRIVER=dummy means no real X11/Wayland
    # display is required at all -- so this runs in any environment, including headless CI.
    cna_blend2d_test(cna_test_blend2d_correctness blend2d_renderer_correctness_test.cpp)
    cna_register_renderer_test(NAME Blend2D_Correctness COMMAND cna_test_blend2d_correctness
        TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=dummy" LABELS "Blend2D;Correctness")

    # plan_blend2d.md remediation (post-3da643712 review): RenderTarget2D public-property
    # truthfulness (DepthStencilFormat/MultiSampleCount/LevelCount) and custom-Effect rejection,
    # driven through the real Game/GraphicsDeviceManager/RenderTarget2D/SpriteBatch public API.
    # Uses SDL_VIDEODRIVER=dummy (verified against Game/GraphicsDeviceManager, same as
    # Blend2D_Correctness above) rather than Blend2D_Smoke's x11 requirement, so this runs
    # headlessly with no real X11/Wayland display needed.
    cna_blend2d_test(cna_test_blend2d_rendertarget_property blend2d_rendertarget_property_test.cpp)
    cna_register_renderer_test(NAME Blend2D_RenderTargetProperty
        COMMAND cna_test_blend2d_rendertarget_property
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=dummy"
        LABELS "Blend2D;Display")
endif()

cna_apply_skip_convention()
