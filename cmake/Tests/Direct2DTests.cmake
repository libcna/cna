if(CNA_BUILD_TESTS AND CNA_GRAPHICS_BACKEND STREQUAL "DIRECT2D")
    macro(cna_direct2d_test target source)
        add_executable(${target} ${source})
        target_link_libraries(${target} PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
        if(TARGET SDL3::SDL3main)
            target_link_libraries(${target} PRIVATE SDL3::SDL3main)
        endif()
        if(MINGW)
            target_link_options(${target} PRIVATE -static-libgcc -static-libstdc++)
            target_link_options(${target} PRIVATE -Wl,--allow-multiple-definition)
            cna_copy_mingw_runtime(${target})
            cna_copy_sdl_runtime(${target})
        endif()
    endmacro()

    cna_direct2d_test(cna_test_direct2d_smoke examples/direct2d_smoke_test.cpp)
    if(CMAKE_CROSSCOMPILING)
        # The Direct2D device context creates a D3D11 device. The existing DXVK wrapper also
        # provides the project's configured Windows runtime prefix; its D3D11-only log gate is
        # intentionally skipped because d2d1.dll itself decides how its device is realized.
        set(_direct2d_smoke_command ${CMAKE_SOURCE_DIR}/scripts/run-wine-dxvk.sh
            $<TARGET_FILE:cna_test_direct2d_smoke>)
        set(_direct2d_smoke_environment "CNA_D3D11_SKIP_DXVK_GATE=1")
    else()
        set(_direct2d_smoke_command $<TARGET_FILE:cna_test_direct2d_smoke>)
        unset(_direct2d_smoke_environment)
    endif()
    cna_register_backend_test(NAME Direct2D_Smoke COMMAND ${_direct2d_smoke_command}
        TIMEOUT 60 LABELS "Direct2D" ENVIRONMENT "${_direct2d_smoke_environment}")
endif()
