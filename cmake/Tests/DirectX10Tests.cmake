if(CNA_BUILD_TESTS AND CNA_GRAPHICS_RENDERER STREQUAL "DIRECTX10")
    enable_testing()

    macro(cna_directx10_test target src)
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

    macro(cna_directx10_ctest_command out_var target_name)
        if(CMAKE_CROSSCOMPILING)
            set(${out_var} ${CMAKE_SOURCE_DIR}/scripts/run-wine-directx10.sh $<TARGET_FILE:${target_name}>)
        else()
            set(${out_var} $<TARGET_FILE:${target_name}>)
        endif()
    endmacro()

    # plan_d3d10.md: script that greps stripped source for forbidden legacy-interface symbols
    # (D3DFVF_*, D3DRENDERSTATE_*, D3DTLVERTEX -- none of which have any place in a real D3D10
    # renderer), mirroring DIRECTX7/DIRECTX8's own established discipline scripts.
    add_test(NAME DirectX10_LegacyInterfaceDiscipline
        COMMAND bash "${CMAKE_SOURCE_DIR}/scripts/check-directx10-legacy-interface-discipline.sh" "${CMAKE_SOURCE_DIR}")
    set_tests_properties(DirectX10_LegacyInterfaceDiscipline PROPERTIES LABELS "DIRECTX10")

    cna_directx10_test(cna_test_directx10_smoke examples/directx10_smoke_test.cpp)
    cna_directx10_ctest_command(_directx10_smoke_cmd cna_test_directx10_smoke)
    cna_register_renderer_test(NAME DirectX10_Smoke COMMAND ${_directx10_smoke_cmd}
        TIMEOUT 60 LABELS "DIRECTX10")

    cna_directx10_test(cna_test_directx10_device3d_smoke examples/directx10_device3d_smoke_test.cpp)
    cna_directx10_ctest_command(_directx10_device3d_cmd cna_test_directx10_device3d_smoke)
    cna_register_renderer_test(NAME DirectX10_Device3DSmoke COMMAND ${_directx10_device3d_cmd}
        TIMEOUT 60 LABELS "DIRECTX10")

    cna_directx10_test(cna_test_directx10_spritebatch examples/directx10_spritebatch_test.cpp)
    cna_directx10_ctest_command(_directx10_spritebatch_cmd cna_test_directx10_spritebatch)
    cna_register_renderer_test(NAME DirectX10_SpriteBatch COMMAND ${_directx10_spritebatch_cmd}
        TIMEOUT 60 LABELS "DIRECTX10")

    cna_directx10_test(cna_test_directx10_graphics_capability examples/directx10_graphics_capability_test.cpp)
    cna_directx10_ctest_command(_directx10_cap_cmd cna_test_directx10_graphics_capability)
    cna_register_renderer_test(NAME DirectX10_GraphicsCapability COMMAND ${_directx10_cap_cmd}
        TIMEOUT 60 LABELS "DIRECTX10")

    # plan_d3d10.md design decision 3/§1: D3D10 genuinely supports MRT (unlike DIRECTX1..DIRECTX8) --
    # verify SetRenderTargets(count>1) does NOT throw, the opposite assertion from every
    # DirectDraw-family renderer's own equivalent check.
    cna_directx10_test(cna_test_directx10_mrt examples/directx10_mrt_test.cpp)
    cna_directx10_ctest_command(_directx10_mrt_cmd cna_test_directx10_mrt)
    cna_register_renderer_test(NAME DirectX10_MultipleRenderTargets COMMAND ${_directx10_mrt_cmd}
        TIMEOUT 60 LABELS "DIRECTX10")

    # State-object pixel-behaviour tests, reusing the same renderer-agnostic EasyGL-authored
    # sources D3D11/Vulkan already reuse verbatim (BasicEffect.VertexColorEnabled=true only --
    # exactly this v1's own DrawColoredPrimitives scope, decision 3).
    cna_directx10_test(cna_test_directx10_blendstate_opaque examples/easygl_blendstate_opaque_test.cpp)
    cna_directx10_ctest_command(_directx10_blend_opaque_cmd cna_test_directx10_blendstate_opaque)
    cna_register_renderer_test(NAME DirectX10_BlendState_Opaque COMMAND ${_directx10_blend_opaque_cmd}
        TIMEOUT 60 LABELS "DIRECTX10")

    cna_directx10_test(cna_test_directx10_blendstate_alphablend examples/easygl_blendstate_alphablend_test.cpp)
    cna_directx10_ctest_command(_directx10_blend_alpha_cmd cna_test_directx10_blendstate_alphablend)
    cna_register_renderer_test(NAME DirectX10_BlendState_AlphaBlend COMMAND ${_directx10_blend_alpha_cmd}
        TIMEOUT 60 LABELS "DIRECTX10")

    cna_directx10_test(cna_test_directx10_depthstencilstate_stencil_enable examples/easygl_depthstencilstate_stencil_enable_test.cpp)
    cna_directx10_ctest_command(_directx10_ds_stencil_cmd cna_test_directx10_depthstencilstate_stencil_enable)
    cna_register_renderer_test(NAME DirectX10_DepthStencilState_StencilEnable COMMAND ${_directx10_ds_stencil_cmd}
        TIMEOUT 60 LABELS "DIRECTX10")

    cna_directx10_test(cna_test_directx10_rasterizerstate_cullmode examples/easygl_rasterizerstate_cullmode_test.cpp)
    cna_directx10_ctest_command(_directx10_rast_cullmode_cmd cna_test_directx10_rasterizerstate_cullmode)
    cna_register_renderer_test(NAME DirectX10_RasterizerState_CullMode COMMAND ${_directx10_rast_cullmode_cmd}
        TIMEOUT 60 LABELS "DIRECTX10")
endif()
