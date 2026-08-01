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
    endif()
endif()
