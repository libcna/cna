if(CNA_BUILD_TESTS AND CNA_GRAPHICS_BACKEND STREQUAL "D3D11")
    enable_testing()

    macro(cna_d3d11_test target src)
        add_executable(${target} ${src})
        target_link_libraries(${target} PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
        if(TARGET SDL3::SDL3main)
            target_link_libraries(${target} PRIVATE SDL3::SDL3main)
        endif()
        if(MINGW)
            target_link_options(${target} PRIVATE -static-libgcc -static-libstdc++)
            # Same known mingw-w64/GCC PE-COFF toolchain limitation CnaTests already works around
            # (std::type_info::operator== emitted as vague linkage the linker fails to fold) --
            # see this file's own CnaTests MINGW block for the full explanation.
            target_link_options(${target} PRIVATE -Wl,--allow-multiple-definition)
            cna_copy_mingw_runtime(${target})
            cna_copy_sdl_runtime(${target})
        endif()
    endmacro()

    macro(cna_d3d11_ctest_command out_var target_name)
        if(CMAKE_CROSSCOMPILING)
            set(${out_var} ${CMAKE_SOURCE_DIR}/scripts/run-wine-dxvk.sh $<TARGET_FILE:${target_name}>)
        else()
            set(${out_var} $<TARGET_FILE:${target_name}>)
        endif()
    endmacro()

    cna_d3d11_test(cna_test_d3d11_smoke examples/d3d11_smoke_test.cpp)
    cna_d3d11_ctest_command(_d3d11_smoke_cmd cna_test_d3d11_smoke)
    cna_register_backend_test(NAME D3D11_Smoke COMMAND ${_d3d11_smoke_cmd}
        TIMEOUT 60 LABELS "D3D11")

    # Phase DX3: pure-function checks for D3DCommon's format/state/vertex-layout mapping tables --
    # no device/window/GPU needed.
    cna_d3d11_test(cna_test_d3d11_common examples/d3d11_common_test.cpp)
    cna_d3d11_ctest_command(_d3d11_common_cmd cna_test_d3d11_common)
    # DX-85: this binary never creates a D3D11 device (pure-function mapping-table checks
    # only), so it legitimately never prints a "DXVK: <version>" line -- tell the wrapper's
    # DXVK-engagement gate not to misreport that as a WineD3D fallback. Harmless no-op when
    # CMAKE_CROSSCOMPILING is false (native MSVC run, no wrapper script involved at all).
    cna_register_backend_test(NAME D3D11_Common COMMAND ${_d3d11_common_cmd}
        TIMEOUT 30 LABELS "D3D11" ENVIRONMENT "CNA_D3D11_SKIP_DXVK_GATE=1")

    # DX-82: state-object pixel-behaviour tests, reusing the same backend-agnostic EasyGL-authored
    # sources Vulkan already reuses verbatim (they only touch the public GraphicsDevice/BasicEffect
    # API, nothing EasyGL-specific) -- real behavioural proof through the full public draw path,
    # not just "the D3D11 state object was created/bound" (Phase DX7's own, narrower bar).
    cna_d3d11_test(cna_test_d3d11_blendstate_opaque examples/easygl_blendstate_opaque_test.cpp)
    cna_d3d11_ctest_command(_d3d11_blend_opaque_cmd cna_test_d3d11_blendstate_opaque)
    cna_register_backend_test(NAME D3D11_BlendState_Opaque COMMAND ${_d3d11_blend_opaque_cmd}
        TIMEOUT 60 LABELS "D3D11")

    cna_d3d11_test(cna_test_d3d11_blendstate_alphablend examples/easygl_blendstate_alphablend_test.cpp)
    cna_d3d11_ctest_command(_d3d11_blend_alpha_cmd cna_test_d3d11_blendstate_alphablend)
    cna_register_backend_test(NAME D3D11_BlendState_AlphaBlend COMMAND ${_d3d11_blend_alpha_cmd}
        TIMEOUT 60 LABELS "D3D11")

    cna_d3d11_test(cna_test_d3d11_depthstencilstate_stencil_enable examples/easygl_depthstencilstate_stencil_enable_test.cpp)
    cna_d3d11_ctest_command(_d3d11_ds_stencil_cmd cna_test_d3d11_depthstencilstate_stencil_enable)
    cna_register_backend_test(NAME D3D11_DepthStencilState_StencilEnable COMMAND ${_d3d11_ds_stencil_cmd}
        TIMEOUT 60 LABELS "D3D11")

    cna_d3d11_test(cna_test_d3d11_rasterizerstate_cullmode examples/easygl_rasterizerstate_cullmode_test.cpp)
    cna_d3d11_ctest_command(_d3d11_rast_cullmode_cmd cna_test_d3d11_rasterizerstate_cullmode)
    cna_register_backend_test(NAME D3D11_RasterizerState_CullMode COMMAND ${_d3d11_rast_cullmode_cmd}
        TIMEOUT 60 LABELS "D3D11")
endif()
