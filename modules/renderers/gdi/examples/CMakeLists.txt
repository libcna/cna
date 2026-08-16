# gdi renderer example/test registration (module-local; this file is the
# former cmake/Tests/GdiTests.cmake, moved when examples moved into their owning
# physical modules).
#
# From the root CMakeLists.txt include site:
# Win32 GDI 2D renderer tests. Native Windows registers them directly; cross-built executables
# remain available for an explicit Wine/display invocation.

# Example/test executables historically live at the build root (scripts, docs and
# the golden-image working-directory conventions rely on it); keep them there.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
# Shared example-support headers ("common/PixelTestGame.hpp", ...) are owned by
# the graphics module's examples directory.
include_directories(${CNA_GRAPHICS_EXAMPLES_DIR})

if(CNA_BUILD_TESTS AND CNA_GRAPHICS_RENDERER STREQUAL "GDI")
    function(cna_add_gdi_test target source)
        add_executable(${target} ${source})
        target_link_libraries(${target} PRIVATE CNA SHARP_RUNTIME SDL3::SDL3
            cna_renderer_software_headers)
        if(TARGET SDL3::SDL3main)
            target_link_libraries(${target} PRIVATE SDL3::SDL3main)
        endif()

        # Keep the cross-built test executable self-contained for Wine/native
        # Windows runs: the GCC and C++ runtimes are linked in, while the only
        # remaining MinGW runtime DLL is staged beside the executable.
        if(MINGW)
            target_link_options(${target} PRIVATE -static-libgcc -static-libstdc++)
            cna_copy_mingw_runtime(${target})
        endif()
        if(WIN32)
            cna_copy_sdl_runtime(${target})
        endif()
    endfunction()

    cna_add_gdi_test(cna_test_gdi_smoke gdi_smoke_test.cpp)
    cna_add_gdi_test(cna_test_gdi_2d_regression gdi_2d_regression_test.cpp)
    cna_add_gdi_test(cna_test_gdi_colormatrix_effect gdi_colormatrix_effect_test.cpp)
    cna_add_gdi_test(cna_test_gdi_public_stencil gdi_public_stencil_test.cpp)
    cna_add_gdi_test(cna_test_gdi_public_api gdi_public_api_test.cpp)
    cna_add_gdi_test(cna_test_gdi_applied_state gdi_applied_state_test.cpp)
    cna_add_gdi_test(cna_test_gdi_unsupported_features
        gdi_unsupported_features_test.cpp)
    cna_add_gdi_test(cna_test_gdi_dirty_damage gdi_dirty_damage_test.cpp)
    cna_add_gdi_test(cna_test_gdi_repaint_invalidation gdi_repaint_invalidation_test.cpp)
    cna_add_gdi_test(cna_test_gdi_presentation_oracle gdi_presentation_oracle_test.cpp)
    cna_add_gdi_test(cna_test_gdi_presentation_configuration
        gdi_presentation_configuration_test.cpp)
    cna_add_gdi_test(cna_test_gdi_window_metrics gdi_window_metrics_test.cpp)
    cna_add_gdi_test(cna_test_gdi_framebuffer_allocation
        gdi_framebuffer_allocation_test.cpp)
    cna_add_gdi_test(cna_test_gdi_msaa_contract gdi_msaa_contract_test.cpp)
    cna_add_gdi_test(cna_test_gdi_presentation_mode_transaction
        gdi_presentation_mode_transaction_test.cpp)
    cna_add_gdi_test(cna_test_gdi_dc_release_transaction
        gdi_dc_release_transaction_test.cpp)
    cna_add_gdi_test(cna_test_gdi_texture_allocation
        gdi_texture_allocation_test.cpp)
    # Benchmark target, deliberately not a CTest: wall-clock performance depends on the Win32/Wine
    # display compositor and the host CPU. It reports CPU raster and GDI Present separately.
    cna_add_gdi_test(cna_bench_gdi_2d gdi_2d_benchmark.cpp)

    # Native Windows is the automatic test target.  A cross-built PE executable needs the
    # developer's chosen Wine/display setup, so it is built but intentionally not registered as a
    # Linux-host CTest command.
    if(NOT CMAKE_CROSSCOMPILING)
        cna_register_renderer_test(NAME GDI_Smoke COMMAND cna_test_gdi_smoke
            TIMEOUT 30 LABELS "GDI")
        cna_register_renderer_test(NAME GDI_2D_Regression COMMAND cna_test_gdi_2d_regression
            TIMEOUT 30 LABELS "GDI")
        cna_register_renderer_test(NAME GDI_ColorMatrixEffect COMMAND cna_test_gdi_colormatrix_effect
            TIMEOUT 30 LABELS "GDI")
        cna_register_renderer_test(NAME GDI_PublicStencil COMMAND cna_test_gdi_public_stencil
            TIMEOUT 30 LABELS "GDI")
        cna_register_renderer_test(NAME GDI_PublicAPI COMMAND cna_test_gdi_public_api
            TIMEOUT 30 LABELS "GDI")
        cna_register_renderer_test(NAME GDI_AppliedState COMMAND cna_test_gdi_applied_state
            TIMEOUT 30 LABELS "GDI")
        cna_register_renderer_test(NAME GDI_UnsupportedFeatures
            COMMAND cna_test_gdi_unsupported_features
            TIMEOUT 30 LABELS "GDI")
        cna_register_renderer_test(NAME GDI_DirtyDamage COMMAND cna_test_gdi_dirty_damage
            TIMEOUT 30 LABELS "GDI")
        cna_register_renderer_test(NAME GDI_RepaintInvalidation COMMAND cna_test_gdi_repaint_invalidation
            TIMEOUT 30 LABELS "GDI")
        cna_register_renderer_test(NAME GDI_PresentationOracle COMMAND cna_test_gdi_presentation_oracle
            TIMEOUT 30 LABELS "GDI")
        cna_register_renderer_test(NAME GDI_WindowMetrics COMMAND cna_test_gdi_window_metrics
            TIMEOUT 30 LABELS "GDI"
            ENVIRONMENT "CNA_GDI_DWM_FLUSH=0")
        cna_register_renderer_test(NAME GDI_FramebufferAllocation
            COMMAND cna_test_gdi_framebuffer_allocation
            TIMEOUT 30 LABELS "GDI"
            ENVIRONMENT "CNA_GDI_DWM_FLUSH=0")
        cna_register_renderer_test(NAME GDI_MsaaContract
            COMMAND cna_test_gdi_msaa_contract
            TIMEOUT 30 LABELS "GDI"
            ENVIRONMENT "CNA_GDI_DWM_FLUSH=0")
        cna_register_renderer_test(NAME GDI_PresentationModeTransaction
            COMMAND cna_test_gdi_presentation_mode_transaction
            TIMEOUT 30 LABELS "GDI"
            ENVIRONMENT "CNA_GDI_DWM_FLUSH=0")
        cna_register_renderer_test(NAME GDI_DcReleaseTransaction
            COMMAND cna_test_gdi_dc_release_transaction
            TIMEOUT 30 LABELS "GDI"
            ENVIRONMENT "CNA_GDI_DWM_FLUSH=0")
        cna_register_renderer_test(NAME GDI_TextureAllocation
            COMMAND cna_test_gdi_texture_allocation
            TIMEOUT 30 LABELS "GDI"
            ENVIRONMENT "CNA_GDI_DWM_FLUSH=0")
        # GDI-056: keep these as separate tests so dashboards identify the exact configuration.
        # DwmFlush is a potentially blocking compositor hint and is deliberately excluded from
        # deterministic correctness coverage.
        cna_register_renderer_test(NAME GDI_Presentation_Default
            COMMAND cna_test_gdi_presentation_configuration default
            TIMEOUT 30 LABELS "GDI"
            ENVIRONMENT "CNA_GDI_DIRTY_PRESENTATION=0;CNA_GDI_PRESENT_FILTER=nearest;CNA_GDI_DWM_FLUSH=0")
        cna_register_renderer_test(NAME GDI_Presentation_Dirty
            COMMAND cna_test_gdi_presentation_configuration dirty
            TIMEOUT 30 LABELS "GDI"
            ENVIRONMENT "CNA_GDI_DIRTY_PRESENTATION=1;CNA_GDI_PRESENT_FILTER=nearest;CNA_GDI_DWM_FLUSH=0")
        cna_register_renderer_test(NAME GDI_Presentation_Halftone
            COMMAND cna_test_gdi_presentation_configuration halftone
            TIMEOUT 30 LABELS "GDI"
            ENVIRONMENT "CNA_GDI_DIRTY_PRESENTATION=1;CNA_GDI_PRESENT_FILTER=halftone;CNA_GDI_DWM_FLUSH=0")
    endif()
endif()

# Module-local tests get the same headless-safe skip convention the root one-shot
# gives root-registered tests (see cmake/TestHelpers.cmake).
cna_apply_skip_convention()
