if(CNA_BUILD_TESTS AND CNA_GRAPHICS_BACKEND STREQUAL "DX1")
    enable_testing()

    macro(cna_dx1_test target src)
        add_executable(${target} ${src})
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

    # plan_dx1.md design decision 11: when cross-compiled from Linux (DX1 is Windows-only, design
    # decision 1), CTest itself still runs on the Linux host, so the test COMMAND wraps the real
    # .exe with scripts/run-wine-dx1.sh -- a real, native Windows/MSVC configure needs no Wine
    # wrapper at all and runs the .exe directly, mirroring D3D9Tests.cmake's own
    # cna_d3d9_ctest_command exactly.
    macro(cna_dx1_ctest_command out_var target_name)
        if(CMAKE_CROSSCOMPILING)
            set(${out_var} ${CMAKE_SOURCE_DIR}/scripts/run-wine-dx1.sh $<TARGET_FILE:${target_name}>)
        else()
            set(${out_var} $<TARGET_FILE:${target_name}>)
        endif()
    endmacro()

    # DX1-1 (plan_dx1.md section 1): a real, automated proof this backend never silently drifts
    # into DX3+ (or Direct3D) territory -- greps its own source for any IDirectDraw2+/
    # IDirectDrawSurface2+/DDSURFACEDESC2 symbol or any d3d.h/IDirect3D token (comments that merely
    # document the discipline are excluded). Pure text check via scripts/check-dx1-v1-only.sh, no
    # compiled binary, no Wine needed -- runs identically whether cross-compiling or not.
    add_test(NAME Dx1_V1OnlyDiscipline COMMAND bash "${CMAKE_SOURCE_DIR}/scripts/check-dx1-v1-only.sh" "${CMAKE_SOURCE_DIR}")
    set_tests_properties(Dx1_V1OnlyDiscipline PROPERTIES LABELS "DX1")

    # Phase O2 (DX1-1..DX1-18): device/window bring-up + Clear/Present/readback foundation.
    cna_dx1_test(cna_test_dx1_smoke examples/dx1_smoke_test.cpp)
    cna_dx1_ctest_command(_dx1_smoke_cmd cna_test_dx1_smoke)
    cna_register_backend_test(NAME Dx1_Smoke COMMAND ${_dx1_smoke_cmd}
        TIMEOUT 60 LABELS "DX1")

    # Phase O3 (DX1-20..DX1-28): texture/render-target backend CTest.
    cna_dx1_test(cna_test_dx1_texture_rendertarget examples/dx1_texture_rendertarget_test.cpp)
    cna_dx1_ctest_command(_dx1_texture_rendertarget_cmd cna_test_dx1_texture_rendertarget)
    cna_register_backend_test(NAME Dx1_TextureRenderTarget COMMAND ${_dx1_texture_rendertarget_cmd}
        TIMEOUT 60 LABELS "DX1")

    # Phase O4 (DX1-30..DX1-39): CPU compositor / SpriteBatch draw path CTest.
    cna_dx1_test(cna_test_dx1_spritebatch examples/dx1_spritebatch_test.cpp)
    cna_dx1_ctest_command(_dx1_spritebatch_cmd cna_test_dx1_spritebatch)
    cna_register_backend_test(NAME Dx1_SpriteBatch COMMAND ${_dx1_spritebatch_cmd}
        TIMEOUT 60 LABELS "DX1")

    # Phase O5 (DX1-40..DX1-44): blend-mode compositing math CTest.
    cna_dx1_test(cna_test_dx1_blend examples/dx1_blend_test.cpp)
    cna_dx1_ctest_command(_dx1_blend_cmd cna_test_dx1_blend)
    cna_register_backend_test(NAME Dx1_Blend COMMAND ${_dx1_blend_cmd}
        TIMEOUT 60 LABELS "DX1")

    # Phase O5 (DX1-45/DX1-46): TextureFilter + TextureAddressMode sampling CTest.
    cna_dx1_test(cna_test_dx1_sampling examples/dx1_sampling_test.cpp)
    cna_dx1_ctest_command(_dx1_sampling_cmd cna_test_dx1_sampling)
    cna_register_backend_test(NAME Dx1_AddressMode COMMAND ${_dx1_sampling_cmd}
        TIMEOUT 60 LABELS "DX1")

    # Phase O6 (DX1-50..DX1-54): SpriteFont / DrawString CTest.
    cna_dx1_test(cna_test_dx1_spritefont examples/dx1_spritefont_test.cpp)
    cna_dx1_ctest_command(_dx1_spritefont_cmd cna_test_dx1_spritefont)
    cna_register_backend_test(NAME Dx1_SpriteFont COMMAND ${_dx1_spritefont_cmd}
        TIMEOUT 60 LABELS "DX1")

    # Phase O7 (DX1-60..DX1-67, DX1-69): ThrowNo3D wiring / remaining-defaults CTest.
    cna_dx1_test(cna_test_dx1_no3d examples/dx1_no3d_test.cpp)
    cna_dx1_ctest_command(_dx1_no3d_cmd cna_test_dx1_no3d)
    cna_register_backend_test(NAME Dx1_No3D COMMAND ${_dx1_no3d_cmd}
        TIMEOUT 60 LABELS "DX1")

    # CNA::GraphicsCapability: DX1 is 2D-only -- SupportsCapability() reports which capabilities
    # are genuinely absent (ThreeD and everything that depends on it).
    cna_dx1_test(cna_test_dx1_graphics_capability examples/dx1_graphics_capability_test.cpp)
    cna_dx1_ctest_command(_dx1_graphics_capability_cmd cna_test_dx1_graphics_capability)
    cna_register_backend_test(NAME Dx1_GraphicsCapability COMMAND ${_dx1_graphics_capability_cmd}
        TIMEOUT 60 LABELS "DX1")

    # Phase O7 (DX1-68): logical/window coordinate transform CTest.
    cna_dx1_test(cna_test_dx1_logical_transform examples/dx1_logical_transform_test.cpp)
    cna_dx1_ctest_command(_dx1_logical_transform_cmd cna_test_dx1_logical_transform)
    cna_register_backend_test(NAME Dx1_LogicalTransform COMMAND ${_dx1_logical_transform_cmd}
        TIMEOUT 60 LABELS "DX1")
endif()
