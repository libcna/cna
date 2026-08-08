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
        if(CNA_DIRECT2D_TEST_RUNTIME STREQUAL "WINE")
            # The Direct2D device context creates a D3D11 device. The existing DXVK wrapper also
            # provides the project's configured Windows runtime prefix; its D3D11-only log gate is
            # intentionally skipped because d2d1.dll itself decides how its device is realized.
            set(_direct2d_smoke_command bash ${CMAKE_SOURCE_DIR}/scripts/run-wine-direct2d.sh
                $<TARGET_FILE:cna_test_direct2d_smoke>)
            set(_direct2d_smoke_environment "CNA_D3D11_SKIP_DXVK_GATE=1")
        elseif(CNA_DIRECT2D_TEST_RUNTIME STREQUAL "PROTON")
            set(_direct2d_smoke_command ${CMAKE_SOURCE_DIR}/scripts/run-proton-direct2d.sh
                $<TARGET_FILE:cna_test_direct2d_smoke>)
            unset(_direct2d_smoke_environment)
        else()
            message(FATAL_ERROR "CNA_DIRECT2D_TEST_RUNTIME must be WINE or PROTON, got '${CNA_DIRECT2D_TEST_RUNTIME}'.")
        endif()
    else()
        set(_direct2d_smoke_command $<TARGET_FILE:cna_test_direct2d_smoke>)
        unset(_direct2d_smoke_environment)
    endif()
    cna_register_backend_test(NAME Direct2D_Smoke COMMAND ${_direct2d_smoke_command}
        TIMEOUT 60 LABELS "Direct2D" ENVIRONMENT "${_direct2d_smoke_environment}")

    # D2D-1: baseline 2D parity matrix.  Keep this separate from the minimal construction smoke:
    # it proves the public Texture2D/SpriteBatch/RenderTarget2D round trip with pixel readback,
    # while later Direct2D tasks can add clipping, viewport, mip, and recovery cases here without
    # turning the one-purpose smoke executable into an unmaintainable catch-all.
    cna_direct2d_test(cna_test_direct2d_2d_parity examples/direct2d_2d_parity_test.cpp)
    if(CMAKE_CROSSCOMPILING)
        if(CNA_DIRECT2D_TEST_RUNTIME STREQUAL "WINE")
            set(_direct2d_2d_parity_command bash ${CMAKE_SOURCE_DIR}/scripts/run-wine-direct2d.sh
                $<TARGET_FILE:cna_test_direct2d_2d_parity>)
            set(_direct2d_2d_parity_environment "CNA_D3D11_SKIP_DXVK_GATE=1")
        elseif(CNA_DIRECT2D_TEST_RUNTIME STREQUAL "PROTON")
            set(_direct2d_2d_parity_command ${CMAKE_SOURCE_DIR}/scripts/run-proton-direct2d.sh
                $<TARGET_FILE:cna_test_direct2d_2d_parity>)
            unset(_direct2d_2d_parity_environment)
        else()
            message(FATAL_ERROR "CNA_DIRECT2D_TEST_RUNTIME must be WINE or PROTON, got '${CNA_DIRECT2D_TEST_RUNTIME}'.")
        endif()
        # WineD3D and Proton's Wine Direct2D both implement render/readback through the fallback,
        # but do not register ColorMatrix and ignore BOUNDED_SOURCE_COPY/Porter-Duff DrawImage
        # composite modes.
        # Keep the public RT-readback matrix active and reserve only those native Direct2D-specific
        # matrices for a real Windows runtime.
        set(_direct2d_2d_parity_environment
            "${_direct2d_2d_parity_environment};CNA_DIRECT2D_SKIP_RENDER_TARGET_DECORATION=1;CNA_DIRECT2D_SKIP_ADVANCED_BLEND=1")
    else()
        set(_direct2d_2d_parity_command $<TARGET_FILE:cna_test_direct2d_2d_parity>)
        unset(_direct2d_2d_parity_environment)
    endif()
    cna_register_backend_test(NAME Direct2D_2DParity COMMAND ${_direct2d_2d_parity_command}
        TIMEOUT 60 LABELS "Direct2D;2D" ENVIRONMENT "${_direct2d_2d_parity_environment}")

    # D2D-27: transient bitmaps/effects/image brushes must survive and then release correctly at
    # EndDraw boundaries (readback, target switches, resize and recovery), not only in a one-frame
    # parity probe. Wine/Proton use the CPU fallback; native runs also exercise effects and image brushes.
    cna_direct2d_test(cna_test_direct2d_lifetime examples/direct2d_lifetime_test.cpp)
    if(CMAKE_CROSSCOMPILING)
        if(CNA_DIRECT2D_TEST_RUNTIME STREQUAL "WINE")
            set(_direct2d_lifetime_command bash ${CMAKE_SOURCE_DIR}/scripts/run-wine-direct2d.sh
                $<TARGET_FILE:cna_test_direct2d_lifetime>)
            set(_direct2d_lifetime_environment "CNA_D3D11_SKIP_DXVK_GATE=1")
        elseif(CNA_DIRECT2D_TEST_RUNTIME STREQUAL "PROTON")
            set(_direct2d_lifetime_command ${CMAKE_SOURCE_DIR}/scripts/run-proton-direct2d.sh
                $<TARGET_FILE:cna_test_direct2d_lifetime>)
            unset(_direct2d_lifetime_environment)
        else()
            message(FATAL_ERROR "CNA_DIRECT2D_TEST_RUNTIME must be WINE or PROTON, got '${CNA_DIRECT2D_TEST_RUNTIME}'.")
        endif()
    else()
        set(_direct2d_lifetime_command $<TARGET_FILE:cna_test_direct2d_lifetime>)
        unset(_direct2d_lifetime_environment)
    endif()
    cna_register_backend_test(NAME Direct2D_Lifetime COMMAND ${_direct2d_lifetime_command}
        TIMEOUT 60 LABELS "Direct2D;2D;Lifetime" ENVIRONMENT "${_direct2d_lifetime_environment}")
endif()
