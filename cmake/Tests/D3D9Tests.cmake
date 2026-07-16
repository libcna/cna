if(CNA_BUILD_TESTS AND CNA_GRAPHICS_BACKEND STREQUAL "D3D9")
    enable_testing()

    macro(cna_d3d9_test target src)
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

    macro(cna_d3d9_ctest_command out_var target_name)
        if(CMAKE_CROSSCOMPILING)
            set(${out_var} ${CMAKE_SOURCE_DIR}/scripts/run-wine-dxvk9.sh $<TARGET_FILE:${target_name}>)
        else()
            set(${out_var} $<TARGET_FILE:${target_name}>)
        endif()
    endmacro()

    # D9-20/D9-21/D9-22: pure-function checks for D3D9's own format/state/vertex-layout mapping
    # tables -- no device/window/GPU needed.
    cna_d3d9_test(cna_test_d3d9_common examples/d3d9_common_test.cpp)
    cna_d3d9_ctest_command(_d3d9_common_cmd cna_test_d3d9_common)
    # This binary never creates a D3D9 device (pure-function mapping-table checks only), so it
    # legitimately never prints a "DXVK: <version>" line -- tell the wrapper's DXVK-engagement
    # gate not to misreport that as a WineD3D fallback (mirrors D3D11_Common's own
    # CNA_D3D11_SKIP_DXVK_GATE usage). Harmless no-op when CMAKE_CROSSCOMPILING is false.
    cna_register_backend_test(NAME D3D9_Common COMMAND ${_d3d9_common_cmd}
        TIMEOUT 30 LABELS "D3D9" ENVIRONMENT "CNA_D3D9_SKIP_DXVK_GATE=1")

    # D9-80: pure-function checks for the transcribed XNA shader-permutation model
    # (ShaderIndex/VSIndices/PSIndices) -- no device/window/GPU needed.
    cna_d3d9_test(cna_test_d3d9_shaderdispatch examples/d3d9_shaderdispatch_test.cpp)
    cna_d3d9_ctest_command(_d3d9_shaderdispatch_cmd cna_test_d3d9_shaderdispatch)
    cna_register_backend_test(NAME D3D9_ShaderDispatch COMMAND ${_d3d9_shaderdispatch_cmd}
        TIMEOUT 30 LABELS "D3D9" ENVIRONMENT "CNA_D3D9_SKIP_DXVK_GATE=1")

    # D9-30/D9-31: real device/Clear/Present/readback smoke test -- this backend's first genuine
    # pixel-correctness proof, through the real public Game/GraphicsDeviceManager/GraphicsDevice
    # API (not the raw backend interface).
    cna_d3d9_test(cna_test_d3d9_smoke examples/d3d9_smoke_test.cpp)
    cna_d3d9_ctest_command(_d3d9_smoke_cmd cna_test_d3d9_smoke)
    cna_register_backend_test(NAME D3D9_Smoke COMMAND ${_d3d9_smoke_cmd}
        TIMEOUT 60 LABELS "D3D9")

    # D9-82: this backend's first real 3D triangle -- DrawColoredPrimitives/
    # DrawIndexedColoredPrimitives through the real public Game/GraphicsDeviceManager API.
    cna_d3d9_test(cna_test_d3d9_draw examples/d3d9_draw_test.cpp)
    cna_d3d9_ctest_command(_d3d9_draw_cmd cna_test_d3d9_draw)
    cna_register_backend_test(NAME D3D9_Draw COMMAND ${_d3d9_draw_cmd}
        TIMEOUT 60 LABELS "D3D9")

    # D9-82b: real effect-aware DrawPrimitivesEx/DrawIndexedPrimitivesEx dispatch (BasicEffect).
    cna_d3d9_test(cna_test_d3d9_drawex examples/d3d9_drawex_test.cpp)
    cna_d3d9_ctest_command(_d3d9_drawex_cmd cna_test_d3d9_drawex)
    cna_register_backend_test(NAME D3D9_DrawEx COMMAND ${_d3d9_drawex_cmd}
        TIMEOUT 60 LABELS "D3D9")

    # D9-83: real hardware instancing via SetStreamSourceFreq (CNA's own NOXNA Instanced3D shader).
    cna_d3d9_test(cna_test_d3d9_instanced examples/d3d9_instanced_test.cpp)
    cna_d3d9_ctest_command(_d3d9_instanced_cmd cna_test_d3d9_instanced)
    cna_register_backend_test(NAME D3D9_Instanced COMMAND ${_d3d9_instanced_cmd}
        TIMEOUT 60 LABELS "D3D9")

    # D9-90/D9-91/D9-92: real D3D9SpriteBatchBackend driving Microsoft's own SpriteEffect, tested
    # through the real public SpriteBatch/Texture2D API (D9-93's own explicit requirement).
    cna_d3d9_test(cna_test_d3d9_spritebatch examples/d3d9_spritebatch_test.cpp)
    cna_d3d9_ctest_command(_d3d9_spritebatch_cmd cna_test_d3d9_spritebatch)
    cna_register_backend_test(NAME D3D9_SpriteBatch COMMAND ${_d3d9_spritebatch_cmd}
        TIMEOUT 60 LABELS "D3D9")

    # D9-100/D9-101/D9-102/D9-103: GraphicsProfile.Reach/HiDef made real (IsProfileSupported(),
    # QueryRenderTargetFormat(), and Texture2D size enforcement), through the real public
    # GraphicsAdapter/Game/GraphicsDeviceManager/Texture2D API.
    cna_d3d9_test(cna_test_d3d9_graphicsprofile examples/d3d9_graphicsprofile_test.cpp)
    cna_d3d9_ctest_command(_d3d9_graphicsprofile_cmd cna_test_d3d9_graphicsprofile)
    cna_register_backend_test(NAME D3D9_GraphicsProfile COMMAND ${_d3d9_graphicsprofile_cmd}
        TIMEOUT 60 LABELS "D3D9")

    # D9-74: D3D9ShaderCache creates all 66 embedded stock-effect shaders through a live device --
    # needs both DXVK and the real compiler's own bytecode output at once (the compiler DLL itself
    # is no longer needed at this step, D9-71 already embedded its output).
    cna_d3d9_test(cna_test_d3d9_shadercache examples/d3d9_shadercache_test.cpp)
    cna_d3d9_ctest_command(_d3d9_shadercache_cmd cna_test_d3d9_shadercache)
    cna_register_backend_test(NAME D3D9_ShaderCache COMMAND ${_d3d9_shadercache_cmd}
        TIMEOUT 60 LABELS "D3D9")

    # D9-64: state-object pixel-behaviour tests, reusing the same backend-agnostic EasyGL-authored
    # sources D3D11/Vulkan already reuse verbatim (they only touch the public GraphicsDevice/
    # BasicEffect API, nothing EasyGL-specific) -- real behavioural proof through the full public
    # draw path, not just "the D3D9 state object was created/bound". Same 4-test subset
    # D3D11_BlendState_Opaque/D3D11_BlendState_AlphaBlend/D3D11_DepthStencilState_StencilEnable/
    # D3D11_RasterizerState_CullMode already established, not Vulkan's full set.
    cna_d3d9_test(cna_test_d3d9_blendstate_opaque examples/easygl_blendstate_opaque_test.cpp)
    cna_d3d9_ctest_command(_d3d9_blend_opaque_cmd cna_test_d3d9_blendstate_opaque)
    cna_register_backend_test(NAME D3D9_BlendState_Opaque COMMAND ${_d3d9_blend_opaque_cmd}
        TIMEOUT 60 LABELS "D3D9")

    cna_d3d9_test(cna_test_d3d9_blendstate_alphablend examples/easygl_blendstate_alphablend_test.cpp)
    cna_d3d9_ctest_command(_d3d9_blend_alpha_cmd cna_test_d3d9_blendstate_alphablend)
    cna_register_backend_test(NAME D3D9_BlendState_AlphaBlend COMMAND ${_d3d9_blend_alpha_cmd}
        TIMEOUT 60 LABELS "D3D9")

    cna_d3d9_test(cna_test_d3d9_depthstencilstate_stencil_enable examples/easygl_depthstencilstate_stencil_enable_test.cpp)
    cna_d3d9_ctest_command(_d3d9_ds_stencil_cmd cna_test_d3d9_depthstencilstate_stencil_enable)
    cna_register_backend_test(NAME D3D9_DepthStencilState_StencilEnable COMMAND ${_d3d9_ds_stencil_cmd}
        TIMEOUT 60 LABELS "D3D9")

    cna_d3d9_test(cna_test_d3d9_rasterizerstate_cullmode examples/easygl_rasterizerstate_cullmode_test.cpp)
    cna_d3d9_ctest_command(_d3d9_rast_cullmode_cmd cna_test_d3d9_rasterizerstate_cullmode)
    cna_register_backend_test(NAME D3D9_RasterizerState_CullMode COMMAND ${_d3d9_rast_cullmode_cmd}
        TIMEOUT 60 LABELS "D3D9")

    # D9-A3: scene-driven CNA-side renderer for the XNA-oracle diff harness -- a comparison TOOL,
    # not a CTest itself (has no pass/fail assertion of its own; D9-A4's xna-diff.py is what
    # judges its output against tools/xna-oracle/Oracle.cs's own, or -- D9-120, below -- against
    # the checked-in reference images). Reuses the cna_d3d9_test() macro purely for its build/link
    # setup, deliberately without registering IT DIRECTLY via add_test().
    cna_d3d9_test(cna_oracle_render tools/xna-oracle/CnaOracleRender.cpp)

    # D9-120: promotes the D9-A oracle corpus into a REAL CTest, running every checked-in scene
    # through cna_oracle_render.exe (above) and diffing against the checked-in real XNA 4.0
    # reference PNG (tools/xna-oracle/reference/*.png, D9-A4's own tolerance=0) -- deliberately
    # does NOT need the XNA Wine prefix at all, only the D3D9 one every other test here already
    # needs. See scripts/run-oracle-corpus-diff.sh's own header comment for the full contract.
    cna_register_backend_test(NAME D3D9_XNA_Diff COMMAND ${CMAKE_SOURCE_DIR}/scripts/run-oracle-corpus-diff.sh $<TARGET_FILE:cna_oracle_render>
        TIMEOUT 300 LABELS "D3D9")

    # D9-110 (Phase D9-11, ask-first, authorized 2026-07-15): validates the CTAB constant-table
    # parser (D3D9ConstantTable.cpp, already part of CNA via BACKEND_SOURCES) against real
    # D3DCompile()/D3DDisassemble() output. This TEST BINARY links d3dcompiler directly for its own
    # fixture (compiling a known shader + running the disassembler as a cross-check oracle) --
    # design decision 16 only forbids linking d3dcompiler into the shipped BACKEND_TARGET, which
    # this test does not touch; same category as fxc_tool/disasm_tool's own standalone
    # -ld3dcompiler linkage. Still needs Wine to run at all under cross-compilation (D3DCompile()
    # only exists inside a Windows/Wine d3dcompiler_47.dll), so this reuses
    # cna_d3d9_ctest_command() like every other D3D9 test here -- it is NOT a device-free binary
    # that can skip the wrapper. Critically, it must run against ~/.wine-cna-d3d9-spike
    # specifically: the default CNA_D3D9_WINEPREFIX (~/.wine-cna-d3d11) only has Wine's OWN builtin
    # d3dcompiler_47.dll, which cannot compile SM2/SM3 shaders at all (docs/d3d9-backend.md) -- the
    # real Microsoft compiler this test's whole premise depends on only lives in the spike prefix.
    cna_d3d9_test(cna_test_d3d9_constanttable examples/d3d9_constanttable_test.cpp)
    target_link_libraries(cna_test_d3d9_constanttable PRIVATE d3dcompiler)
    cna_d3d9_ctest_command(_d3d9_constanttable_cmd cna_test_d3d9_constanttable)
    # Never creates a D3D9 device (D3DCompile()/D3DDisassemble() are pure compiler-library
    # calls), so it legitimately never prints a "DXVK: <version>" line -- same reasoning as
    # D3D9_Common's own ENVIRONMENT override. CNA_D3D9_WINEPREFIX pins this specific test to
    # the real-compiler prefix regardless of whatever prefix the ambient shell/CI environment
    # might otherwise have set for the rest of the D3D9 suite.
    cna_register_backend_test(NAME D3D9_ConstantTable COMMAND ${_d3d9_constanttable_cmd}
        TIMEOUT 30 LABELS "D3D9"
        ENVIRONMENT "CNA_D3D9_SKIP_DXVK_GATE=1;CNA_D3D9_WINEPREFIX=$ENV{HOME}/.wine-cna-d3d9-spike")

    # D9-111 (Phase D9-11): validates D3D9EffectBackend against a real device (compile, bind, draw,
    # uniform-driven pixel readback). Needs BOTH d3dcompiler (via the isolated
    # cna_backend_graphics_d3d9_effect target, design decision 16) and a real D3D9 device (unlike
    # D3D9_ConstantTable, this genuinely opens one and draws), so it needs the real-compiler prefix
    # (~/.wine-cna-d3d9-spike) AND is NOT exempt from the DXVK-engagement gate.
    if(TARGET cna_backend_graphics_d3d9_effect)
        cna_d3d9_test(cna_test_d3d9_effectbackend examples/d3d9_effectbackend_test.cpp)
        target_link_libraries(cna_test_d3d9_effectbackend PRIVATE cna_backend_graphics_d3d9_effect)
        cna_d3d9_ctest_command(_d3d9_effectbackend_cmd cna_test_d3d9_effectbackend)
        cna_register_backend_test(NAME D3D9_EffectBackend COMMAND ${_d3d9_effectbackend_cmd}
            TIMEOUT 60 LABELS "D3D9" ENVIRONMENT "CNA_D3D9_WINEPREFIX=$ENV{HOME}/.wine-cna-d3d9-spike")

        # D9-112: SpriteBatch::Begin(effect) wiring, through the real public
        # SpriteBatch/ShaderEffect API (not the raw backend), mirroring D3D11's own DX-71 test
        # bar. CNA itself now links cna_backend_graphics_d3d9_effect (the circular-link fix
        # above), so this needs no extra target_link_libraries beyond the standard cna_d3d9_test()
        # macro set -- ShaderEffect's own constructor reaches D3D9GraphicsBackend::
        # CreateEffectBackend() transitively through the real device.
        cna_d3d9_test(cna_test_d3d9_spritebatch_customeffect examples/d3d9_spritebatch_customeffect_test.cpp)
        cna_d3d9_ctest_command(_d3d9_sb_customeffect_cmd cna_test_d3d9_spritebatch_customeffect)
        cna_register_backend_test(NAME D3D9_SpriteBatch_CustomEffect COMMAND ${_d3d9_sb_customeffect_cmd}
            TIMEOUT 60 LABELS "D3D9" ENVIRONMENT "CNA_D3D9_WINEPREFIX=$ENV{HOME}/.wine-cna-d3d9-spike")
    endif()
endif()
