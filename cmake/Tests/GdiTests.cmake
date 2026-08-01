if(CNA_BUILD_TESTS AND CNA_GRAPHICS_BACKEND STREQUAL "GDI")
    add_executable(cna_test_gdi_smoke examples/gdi_smoke_test.cpp)
    target_link_libraries(cna_test_gdi_smoke PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_test_gdi_smoke PRIVATE SDL3::SDL3main)
    endif()

    # Keep the cross-built smoke executable self-contained for Wine/native
    # Windows runs: the GCC and C++ runtimes are linked in, while the only
    # remaining MinGW runtime DLL is staged beside the executable.
    if(MINGW)
        target_link_options(cna_test_gdi_smoke PRIVATE -static-libgcc -static-libstdc++)
        cna_copy_mingw_runtime(cna_test_gdi_smoke)
    endif()
    if(WIN32)
        cna_copy_sdl_runtime(cna_test_gdi_smoke)
    endif()

    # Native Windows is the automatic test target.  A cross-built PE executable needs the
    # developer's chosen Wine/display setup, so it is built but intentionally not registered as a
    # Linux-host CTest command.
    if(NOT CMAKE_CROSSCOMPILING)
        cna_register_backend_test(NAME GDI_Smoke COMMAND cna_test_gdi_smoke
            TIMEOUT 30 LABELS "GDI")
    endif()
endif()
