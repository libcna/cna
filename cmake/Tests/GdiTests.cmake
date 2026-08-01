if(CNA_BUILD_TESTS AND CNA_GRAPHICS_BACKEND STREQUAL "GDI")
    function(cna_add_gdi_test target source)
        add_executable(${target} ${source})
        target_link_libraries(${target} PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
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

    cna_add_gdi_test(cna_test_gdi_smoke examples/gdi_smoke_test.cpp)
    cna_add_gdi_test(cna_test_gdi_2d_regression examples/gdi_2d_regression_test.cpp)
    cna_add_gdi_test(cna_test_gdi_colormatrix_effect examples/gdi_colormatrix_effect_test.cpp)
    cna_add_gdi_test(cna_test_gdi_public_stencil examples/gdi_public_stencil_test.cpp)
    cna_add_gdi_test(cna_test_gdi_public_api examples/gdi_public_api_test.cpp)
    cna_add_gdi_test(cna_test_gdi_applied_state examples/gdi_applied_state_test.cpp)
    cna_add_gdi_test(cna_test_gdi_unsupported_features
        examples/gdi_unsupported_features_test.cpp)
    cna_add_gdi_test(cna_test_gdi_dirty_damage examples/gdi_dirty_damage_test.cpp)
    cna_add_gdi_test(cna_test_gdi_repaint_invalidation examples/gdi_repaint_invalidation_test.cpp)
    cna_add_gdi_test(cna_test_gdi_presentation_oracle examples/gdi_presentation_oracle_test.cpp)
    cna_add_gdi_test(cna_test_gdi_presentation_configuration
        examples/gdi_presentation_configuration_test.cpp)
    cna_add_gdi_test(cna_test_gdi_window_metrics examples/gdi_window_metrics_test.cpp)
    cna_add_gdi_test(cna_test_gdi_framebuffer_allocation
        examples/gdi_framebuffer_allocation_test.cpp)
    cna_add_gdi_test(cna_test_gdi_msaa_contract examples/gdi_msaa_contract_test.cpp)
    # Benchmark target, deliberately not a CTest: wall-clock performance depends on the Win32/Wine
    # display compositor and the host CPU. It reports CPU raster and GDI Present separately.
    cna_add_gdi_test(cna_bench_gdi_2d examples/gdi_2d_benchmark.cpp)

    # Native Windows is the automatic test target.  A cross-built PE executable needs the
    # developer's chosen Wine/display setup, so it is built but intentionally not registered as a
    # Linux-host CTest command.
    if(NOT CMAKE_CROSSCOMPILING)
        cna_register_backend_test(NAME GDI_Smoke COMMAND cna_test_gdi_smoke
            TIMEOUT 30 LABELS "GDI")
        cna_register_backend_test(NAME GDI_2D_Regression COMMAND cna_test_gdi_2d_regression
            TIMEOUT 30 LABELS "GDI")
        cna_register_backend_test(NAME GDI_ColorMatrixEffect COMMAND cna_test_gdi_colormatrix_effect
            TIMEOUT 30 LABELS "GDI")
        cna_register_backend_test(NAME GDI_PublicStencil COMMAND cna_test_gdi_public_stencil
            TIMEOUT 30 LABELS "GDI")
        cna_register_backend_test(NAME GDI_PublicAPI COMMAND cna_test_gdi_public_api
            TIMEOUT 30 LABELS "GDI")
        cna_register_backend_test(NAME GDI_AppliedState COMMAND cna_test_gdi_applied_state
            TIMEOUT 30 LABELS "GDI")
        cna_register_backend_test(NAME GDI_UnsupportedFeatures
            COMMAND cna_test_gdi_unsupported_features
            TIMEOUT 30 LABELS "GDI")
        cna_register_backend_test(NAME GDI_DirtyDamage COMMAND cna_test_gdi_dirty_damage
            TIMEOUT 30 LABELS "GDI")
        cna_register_backend_test(NAME GDI_RepaintInvalidation COMMAND cna_test_gdi_repaint_invalidation
            TIMEOUT 30 LABELS "GDI")
        cna_register_backend_test(NAME GDI_PresentationOracle COMMAND cna_test_gdi_presentation_oracle
            TIMEOUT 30 LABELS "GDI")
        cna_register_backend_test(NAME GDI_WindowMetrics COMMAND cna_test_gdi_window_metrics
            TIMEOUT 30 LABELS "GDI"
            ENVIRONMENT "CNA_GDI_DWM_FLUSH=0")
        cna_register_backend_test(NAME GDI_FramebufferAllocation
            COMMAND cna_test_gdi_framebuffer_allocation
            TIMEOUT 30 LABELS "GDI"
            ENVIRONMENT "CNA_GDI_DWM_FLUSH=0")
        cna_register_backend_test(NAME GDI_MsaaContract
            COMMAND cna_test_gdi_msaa_contract
            TIMEOUT 30 LABELS "GDI"
            ENVIRONMENT "CNA_GDI_DWM_FLUSH=0")
        # GDI-056: keep these as separate tests so dashboards identify the exact configuration.
        # DwmFlush is a potentially blocking compositor hint and is deliberately excluded from
        # deterministic correctness coverage.
        cna_register_backend_test(NAME GDI_Presentation_Default
            COMMAND cna_test_gdi_presentation_configuration default
            TIMEOUT 30 LABELS "GDI"
            ENVIRONMENT "CNA_GDI_DIRTY_PRESENTATION=0;CNA_GDI_PRESENT_FILTER=nearest;CNA_GDI_DWM_FLUSH=0")
        cna_register_backend_test(NAME GDI_Presentation_Dirty
            COMMAND cna_test_gdi_presentation_configuration dirty
            TIMEOUT 30 LABELS "GDI"
            ENVIRONMENT "CNA_GDI_DIRTY_PRESENTATION=1;CNA_GDI_PRESENT_FILTER=nearest;CNA_GDI_DWM_FLUSH=0")
        cna_register_backend_test(NAME GDI_Presentation_Halftone
            COMMAND cna_test_gdi_presentation_configuration halftone
            TIMEOUT 30 LABELS "GDI"
            ENVIRONMENT "CNA_GDI_DIRTY_PRESENTATION=1;CNA_GDI_PRESENT_FILTER=halftone;CNA_GDI_DWM_FLUSH=0")
    endif()
endif()
