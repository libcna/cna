# directx9 renderer example/test registration (module-local; this file is the
# former cmake/Tests/DirectX9Tests.cmake, moved when examples moved into their owning
# physical modules).
#
# From the root CMakeLists.txt include site:
# plan_dx9.md D9-23: D3D9 CTest registration, mirroring D3D11's/D3D12's own cna_directx11_test/
# cna_directx12_test macros (design decision: mirror the shape, don't invent a new pattern). Uses the
# new scripts/run-wine-dxvk9.sh (D9-5), not the shared run-wine-dxvk.sh (cold-start Sec.4).

# Example/test executables historically live at the build root (scripts, docs and
# the golden-image working-directory conventions rely on it); keep them there.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
# Shared example-support headers ("common/PixelTestGame.hpp", ...) are owned by
# the graphics module's examples directory.
include_directories(${CNA_GRAPHICS_EXAMPLES_DIR})

if(CNA_BUILD_TESTS AND CNA_GRAPHICS_RENDERER STREQUAL "DIRECTX9")
    enable_testing()

    macro(cna_directx9_test target src)
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

    macro(cna_directx9_ctest_command out_var target_name)
        if(CMAKE_CROSSCOMPILING)
            set(${out_var} ${CMAKE_SOURCE_DIR}/scripts/run-wine-dxvk9.sh $<TARGET_FILE:${target_name}>)
        else()
            set(${out_var} $<TARGET_FILE:${target_name}>)
        endif()
    endmacro()

    # D9-20/D9-21/D9-22: pure-function checks for D3D9's own format/state/vertex-layout mapping
    # tables -- no device/window/GPU needed.
    cna_directx9_test(cna_test_directx9_common directx9_common_test.cpp)
    cna_directx9_ctest_command(_directx9_common_cmd cna_test_directx9_common)
    # This binary never creates a D3D9 device (pure-function mapping-table checks only), so it
    # legitimately never prints a "DXVK: <version>" line -- tell the wrapper's DXVK-engagement
    # gate not to misreport that as a WineD3D fallback (mirrors DirectX11_Common's own
    # CNA_D3D11_SKIP_DXVK_GATE usage). Harmless no-op when CMAKE_CROSSCOMPILING is false.
    cna_register_renderer_test(NAME DirectX9_Common COMMAND ${_directx9_common_cmd}
        TIMEOUT 30 LABELS "DIRECTX9" ENVIRONMENT "CNA_D3D9_SKIP_DXVK_GATE=1")

    # D9-80: pure-function checks for the transcribed XNA shader-permutation model
    # (ShaderIndex/VSIndices/PSIndices) -- no device/window/GPU needed.
    cna_directx9_test(cna_test_directx9_shaderdispatch directx9_shaderdispatch_test.cpp)
    cna_directx9_ctest_command(_directx9_shaderdispatch_cmd cna_test_directx9_shaderdispatch)
    cna_register_renderer_test(NAME DirectX9_ShaderDispatch COMMAND ${_directx9_shaderdispatch_cmd}
        TIMEOUT 30 LABELS "DIRECTX9" ENVIRONMENT "CNA_D3D9_SKIP_DXVK_GATE=1")

    # D9-30/D9-31: real device/Clear/Present/readback smoke test -- this renderer's first genuine
    # pixel-correctness proof, through the real public Game/GraphicsDeviceManager/GraphicsDevice
    # API (not the raw renderer interface).
    cna_directx9_test(cna_test_directx9_smoke directx9_smoke_test.cpp)
    cna_directx9_ctest_command(_directx9_smoke_cmd cna_test_directx9_smoke)
    cna_register_renderer_test(NAME DirectX9_Smoke COMMAND ${_directx9_smoke_cmd}
        TIMEOUT 60 LABELS "DIRECTX9")

    # REMED-GFX-092: deterministic, renderer-instance-local HRESULT injection for D3D9 state,
    # target/depth binding, depth allocation, retry, rollback, and lost-device mapping.  The
    # native success path is still exercised by DirectX9_Smoke and this test's setup/retry operations.
    cna_directx9_test(cna_test_directx9_hresult directx9_hresult_test.cpp)
    cna_directx9_ctest_command(_directx9_hresult_cmd cna_test_directx9_hresult)
    cna_register_renderer_test(NAME D3D9_HResultHandling COMMAND ${_directx9_hresult_cmd}
        TIMEOUT 60 LABELS "DIRECTX9")

    # REMED-GFX-089: renderer-neutral public GraphicsDevice/DepthStencilState contract.
    # A(Default)->B(DepthRead)->C(None)->A on the backbuffer, using only BasicEffect and the
    # normal public draw API. The smoke test independently covers the offscreen native surface.
    cna_directx9_test(cna_test_directx9_depth_contract ${CNA_GRAPHICS_EXAMPLES_DIR}/graphicsdevice_depth_contract_test.cpp)
    cna_directx9_ctest_command(_directx9_depth_contract_cmd cna_test_directx9_depth_contract)
    cna_register_renderer_test(NAME DirectX9_GraphicsDevice_DepthContract
        COMMAND ${_directx9_depth_contract_cmd}
        TIMEOUT 60 LABELS "DIRECTX9")

    # D9-82: this renderer's first real 3D triangle -- DrawColoredPrimitives/
    # DrawIndexedColoredPrimitives through the real public Game/GraphicsDeviceManager API.
    cna_directx9_test(cna_test_directx9_draw directx9_draw_test.cpp)
    cna_directx9_ctest_command(_directx9_draw_cmd cna_test_directx9_draw)
    cna_register_renderer_test(NAME DirectX9_Draw COMMAND ${_directx9_draw_cmd}
        TIMEOUT 60 LABELS "DIRECTX9")

    # REMED-GFX-077 runtime verification for D3D9 lives inside cna_test_directx9_smoke (Check GFX077):
    # ColorWriteChannels (-> D3DRS_COLORWRITEENABLE) + MultiSampleMask (-> D3DRS_MULTISAMPLEMASK) are
    # verified through an off-screen D3D9RenderTargetRenderer + GetData readback, alongside the smoke
    # test's other real-device checks (D9-53), rather than the RT-readback Game harness the native
    # SdlGpu/WebGPU renderers use.

    # D9-82b: real effect-aware DrawPrimitivesEx/DrawIndexedPrimitivesEx dispatch (BasicEffect).
    cna_directx9_test(cna_test_directx9_drawex directx9_drawex_test.cpp)
    cna_directx9_ctest_command(_directx9_drawex_cmd cna_test_directx9_drawex)
    cna_register_renderer_test(NAME DirectX9_DrawEx COMMAND ${_directx9_drawex_cmd}
        TIMEOUT 60 LABELS "DIRECTX9")

    # REMED-GFX-061: public stock-effect fog contract. The shared transformed-camera fixture
    # discriminates FNA's CPU-prepared view-space FogVector from the obsolete scalar/object-space
    # model, including post-skin position handling. The focused family fixtures cover the other
    # XNA fog-capable stock effects through reachable public effect state.
    cna_directx9_test(cna_test_directx9_viewspace_fog ${CMAKE_SOURCE_DIR}/modules/renderers/vulkan/examples/vulkan_viewspace_fog_test.cpp)
    cna_directx9_ctest_command(_directx9_viewspace_fog_cmd cna_test_directx9_viewspace_fog)
    cna_register_renderer_test(NAME DirectX9_ViewSpaceFog COMMAND ${_directx9_viewspace_fog_cmd}
        TIMEOUT 60 LABELS "DIRECTX9")

    cna_directx9_test(cna_test_directx9_alphatest_fog ${CMAKE_SOURCE_DIR}/modules/renderers/vulkan/examples/vulkan_alphatest_fog_test.cpp)
    cna_directx9_ctest_command(_directx9_alphatest_fog_cmd cna_test_directx9_alphatest_fog)
    cna_register_renderer_test(NAME DirectX9_AlphaTest_Fog COMMAND ${_directx9_alphatest_fog_cmd}
        TIMEOUT 60 LABELS "DIRECTX9")

    cna_directx9_test(cna_test_directx9_dualtexture_fog ${CMAKE_SOURCE_DIR}/modules/renderers/vulkan/examples/vulkan_dualtextureeffect_fog_test.cpp)
    cna_directx9_ctest_command(_directx9_dualtexture_fog_cmd cna_test_directx9_dualtexture_fog)
    cna_register_renderer_test(NAME DirectX9_DualTextureEffect_Fog COMMAND ${_directx9_dualtexture_fog_cmd}
        TIMEOUT 60 LABELS "DIRECTX9")

    cna_directx9_test(cna_test_directx9_environmentmap_fog ${CMAKE_SOURCE_DIR}/modules/renderers/vulkan/examples/vulkan_environmentmapeffect_fog_test.cpp)
    cna_directx9_ctest_command(_directx9_environmentmap_fog_cmd cna_test_directx9_environmentmap_fog)
    cna_register_renderer_test(NAME DirectX9_EnvironmentMapEffect_Fog COMMAND ${_directx9_environmentmap_fog_cmd}
        TIMEOUT 60 LABELS "DIRECTX9")

    cna_directx9_test(cna_test_directx9_skinnedeffect_fog ${CMAKE_SOURCE_DIR}/modules/renderers/vulkan/examples/vulkan_skinnedeffect_fog_test.cpp)
    cna_directx9_ctest_command(_directx9_skinnedeffect_fog_cmd cna_test_directx9_skinnedeffect_fog)
    cna_register_renderer_test(NAME DirectX9_SkinnedEffect_Fog COMMAND ${_directx9_skinnedeffect_fog_cmd}
        TIMEOUT 60 LABELS "DIRECTX9")

    # D9-83: real hardware instancing via SetStreamSourceFreq (CNA's own CNAEXT Instanced3D shader).
    cna_directx9_test(cna_test_directx9_instanced directx9_instanced_test.cpp)
    cna_directx9_ctest_command(_directx9_instanced_cmd cna_test_directx9_instanced)
    cna_register_renderer_test(NAME DirectX9_Instanced COMMAND ${_directx9_instanced_cmd}
        TIMEOUT 60 LABELS "DIRECTX9")

    # REMED-GFX-060: non-zero draw-offset regressions -- every effect-aware D3D9 draw path honors
    # DrawPrimitives(vertexStart)/DrawIndexedPrimitives(baseVertex,startIndex) instead of hardcoding
    # 0 into DrawPrimitive/DrawIndexedPrimitive (the D3D9 counterpart of REMED-GFX-020's D3D11 fix).
    cna_directx9_test(cna_test_directx9_drawoffset directx9_drawoffset_test.cpp)
    cna_directx9_ctest_command(_directx9_drawoffset_cmd cna_test_directx9_drawoffset)
    cna_register_renderer_test(NAME DirectX9_DrawOffset COMMAND ${_directx9_drawoffset_cmd}
        TIMEOUT 60 LABELS "DIRECTX9")

    # D3D9 PBR porting task: real DrawPrimitivesEx dispatch for CNA's own CNAEXT PbrEffect/
    # SkinnedPbrEffect (params.pbr) through CNA's own custom Pbr3D/PbrSkinned3D vs_3_0/ps_3_0
    # shaders (D3D9PbrDraw.cpp).
    cna_directx9_test(cna_test_directx9_pbr directx9_pbr_test.cpp)
    cna_directx9_ctest_command(_directx9_pbr_cmd cna_test_directx9_pbr)
    cna_register_renderer_test(NAME DirectX9_Pbr COMMAND ${_directx9_pbr_cmd}
        TIMEOUT 60 LABELS "DIRECTX9")

    # GLTF-213: exact mid-grey transfer witnesses on both SM3 PBR pixel shaders.
    cna_directx9_test(cna_test_directx9_pbr_srgb_transfer
                      ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_pbr_srgb_transfer_test.cpp)
    cna_directx9_ctest_command(_directx9_pbr_srgb_cmd cna_test_directx9_pbr_srgb_transfer)
    cna_register_renderer_test(NAME DirectX9_Pbr_SrgbTransfer COMMAND ${_directx9_pbr_srgb_cmd}
        TIMEOUT 60 LABELS "DIRECTX9")

    # GLTF-343/344: SM3 witnesses for dielectric F0/F90 on rigid and skinned PBR paths.
    cna_directx9_test(cna_test_directx9_pbr_fresnel_factors
                      ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_pbr_fresnel_factors_test.cpp)
    cna_directx9_ctest_command(_directx9_pbr_fresnel_cmd cna_test_directx9_pbr_fresnel_factors)
    cna_register_renderer_test(NAME DirectX9_Pbr_FresnelFactors COMMAND ${_directx9_pbr_fresnel_cmd}
        TIMEOUT 60 LABELS "DIRECTX9")

    # D3D9 skinned-vertex-color porting task: real DrawPrimitivesEx dispatch for CNA's own CNAEXT
    # SkinnedVertexColor3D shader (stride 56 -- SkinnedEffect + a vertex Color real XNA's own
    # compiled SkinnedEffect.fx bytecode never carries).
    cna_directx9_test(cna_test_directx9_skinnedvertexcolor directx9_skinnedvertexcolor_test.cpp)
    cna_directx9_ctest_command(_directx9_skinnedvertexcolor_cmd cna_test_directx9_skinnedvertexcolor)
    cna_register_renderer_test(NAME DirectX9_SkinnedVertexColor COMMAND ${_directx9_skinnedvertexcolor_cmd}
        TIMEOUT 60 LABELS "DIRECTX9")

    # D9-90/D9-91/D9-92: real D3D9SpriteBatchRenderer driving Microsoft's own SpriteEffect, tested
    # through the real public SpriteBatch/Texture2D API (D9-93's own explicit requirement).
    cna_directx9_test(cna_test_directx9_spritebatch directx9_spritebatch_test.cpp)
    cna_directx9_ctest_command(_directx9_spritebatch_cmd cna_test_directx9_spritebatch)
    cna_register_renderer_test(NAME DirectX9_SpriteBatch COMMAND ${_directx9_spritebatch_cmd}
        TIMEOUT 60 LABELS "DIRECTX9")

    # D9-100/D9-101/D9-102/D9-103: GraphicsProfile.Reach/HiDef made real (IsProfileSupported(),
    # QueryRenderTargetFormat(), and Texture2D size enforcement), through the real public
    # GraphicsAdapter/Game/GraphicsDeviceManager/Texture2D API.
    cna_directx9_test(cna_test_directx9_graphicsprofile directx9_graphicsprofile_test.cpp)
    cna_directx9_ctest_command(_directx9_graphicsprofile_cmd cna_test_directx9_graphicsprofile)
    cna_register_renderer_test(NAME DirectX9_GraphicsProfile COMMAND ${_directx9_graphicsprofile_cmd}
        TIMEOUT 60 LABELS "DIRECTX9")

    # D9-74: D3D9ShaderCache creates all 66 embedded stock-effect shaders through a live device --
    # needs both DXVK and the real compiler's own bytecode output at once (the compiler DLL itself
    # is no longer needed at this step, D9-71 already embedded its output).
    cna_directx9_test(cna_test_directx9_shadercache directx9_shadercache_test.cpp)
    # This one test includes the D3D9 renderer's generated shader header
    # (modules/renderers/directx9/src/shaders/d3d9_shaders.hpp) to compare live-device shader
    # creation against the embedded bytecode inventory. That implementation header is
    # deliberately NOT on any exported include path (the d3d9 module exports only its
    # include/ root), so this target gets the implementation root itself, scoped to it
    # alone -- the same includer-relative "shaders/..." spelling the renderer TUs use.
    target_include_directories(cna_test_directx9_shadercache PRIVATE
        ${CMAKE_SOURCE_DIR}/modules/renderers/directx9/src)
    cna_directx9_ctest_command(_directx9_shadercache_cmd cna_test_directx9_shadercache)
    cna_register_renderer_test(NAME DirectX9_ShaderCache COMMAND ${_directx9_shadercache_cmd}
        TIMEOUT 60 LABELS "DIRECTX9")

    # D9-64: state-object pixel-behaviour tests, reusing the same renderer-agnostic EasyGL-authored
    # sources D3D11/Vulkan already reuse verbatim (they only touch the public GraphicsDevice/
    # BasicEffect API, nothing EasyGL-specific) -- real behavioural proof through the full public
    # draw path, not just "the D3D9 state object was created/bound". Same 4-test subset
    # DirectX11_BlendState_Opaque/DirectX11_BlendState_AlphaBlend/DirectX11_DepthStencilState_StencilEnable/
    # DirectX11_RasterizerState_CullMode already established, not Vulkan's full set.
    cna_directx9_test(cna_test_directx9_blendstate_opaque ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_blendstate_opaque_test.cpp)
    cna_directx9_ctest_command(_directx9_blend_opaque_cmd cna_test_directx9_blendstate_opaque)
    cna_register_renderer_test(NAME DirectX9_BlendState_Opaque COMMAND ${_directx9_blend_opaque_cmd}
        TIMEOUT 60 LABELS "DIRECTX9")

    cna_directx9_test(cna_test_directx9_blendstate_alphablend ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_blendstate_alphablend_test.cpp)
    cna_directx9_ctest_command(_directx9_blend_alpha_cmd cna_test_directx9_blendstate_alphablend)
    cna_register_renderer_test(NAME DirectX9_BlendState_AlphaBlend COMMAND ${_directx9_blend_alpha_cmd}
        TIMEOUT 60 LABELS "DIRECTX9")

    cna_directx9_test(cna_test_directx9_depthstencilstate_stencil_enable ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_depthstencilstate_stencil_enable_test.cpp)
    cna_directx9_ctest_command(_directx9_ds_stencil_cmd cna_test_directx9_depthstencilstate_stencil_enable)
    cna_register_renderer_test(NAME DirectX9_DepthStencilState_StencilEnable COMMAND ${_directx9_ds_stencil_cmd}
        TIMEOUT 60 LABELS "DIRECTX9")

    cna_directx9_test(cna_test_directx9_rasterizerstate_cullmode ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_rasterizerstate_cullmode_test.cpp)
    cna_directx9_ctest_command(_directx9_rast_cullmode_cmd cna_test_directx9_rasterizerstate_cullmode)
    cna_register_renderer_test(NAME DirectX9_RasterizerState_CullMode COMMAND ${_directx9_rast_cullmode_cmd}
        TIMEOUT 60 LABELS "DIRECTX9")

    # D9-A3: scene-driven CNA-side renderer for the XNA-oracle diff harness -- a comparison TOOL,
    # not a CTest itself (has no pass/fail assertion of its own; D9-A4's xna-diff.py is what
    # judges its output against tools/xna-oracle/Oracle.cs's own, or -- D9-120, below -- against
    # the checked-in reference images). Reuses the cna_directx9_test() macro purely for its build/link
    # setup, deliberately without registering IT DIRECTLY via add_test().
    cna_directx9_test(cna_oracle_render ${CMAKE_SOURCE_DIR}/tools/xna-oracle/CnaOracleRender.cpp)

    # D9-120: promotes the D9-A oracle corpus into a REAL CTest, running every checked-in scene
    # through cna_oracle_render.exe (above) and diffing against the checked-in real XNA 4.0
    # reference PNG (tools/xna-oracle/reference/*.png, D9-A4's own tolerance=0) -- deliberately
    # does NOT need the XNA Wine prefix at all, only the D3D9 one every other test here already
    # needs. See scripts/run-oracle-corpus-diff.sh's own header comment for the full contract.
    cna_register_renderer_test(NAME D3D9_XNA_Diff COMMAND ${CMAKE_SOURCE_DIR}/scripts/run-oracle-corpus-diff.sh $<TARGET_FILE:cna_oracle_render>
        TIMEOUT 300 LABELS "DIRECTX9")

    # D9-110 (Phase D9-11, ask-first, authorized 2026-07-15): validates the CTAB constant-table
    # parser (D3D9ConstantTable.cpp, already part of CNA via BACKEND_SOURCES) against real
    # D3DCompile()/D3DDisassemble() output. This TEST BINARY links d3dcompiler directly for its own
    # fixture (compiling a known shader + running the disassembler as a cross-check oracle) --
    # design decision 16 only forbids linking d3dcompiler into the shipped RENDERER_TARGET, which
    # this test does not touch; same category as fxc_tool/disasm_tool's own standalone
    # -ld3dcompiler linkage. Still needs Wine to run at all under cross-compilation (D3DCompile()
    # only exists inside a Windows/Wine d3dcompiler_47.dll), so this reuses
    # cna_directx9_ctest_command() like every other D3D9 test here -- it is NOT a device-free binary
    # that can skip the wrapper. Critically, it must run against ~/.wine-cna-d3d9-spike
    # specifically: the default CNA_D3D9_WINEPREFIX (~/.wine-cna-d3d11) only has Wine's OWN builtin
    # d3dcompiler_47.dll, which cannot compile SM2/SM3 shaders at all (docs/directx9-renderer.md) -- the
    # real Microsoft compiler this test's whole premise depends on only lives in the spike prefix.
    cna_directx9_test(cna_test_directx9_constanttable directx9_constanttable_test.cpp)
    target_link_libraries(cna_test_directx9_constanttable PRIVATE d3dcompiler)
    cna_directx9_ctest_command(_directx9_constanttable_cmd cna_test_directx9_constanttable)
    # Never creates a D3D9 device (D3DCompile()/D3DDisassemble() are pure compiler-library
    # calls), so it legitimately never prints a "DXVK: <version>" line -- same reasoning as
    # DirectX9_Common's own ENVIRONMENT override. CNA_D3D9_WINEPREFIX pins this specific test to
    # the real-compiler prefix regardless of whatever prefix the ambient shell/CI environment
    # might otherwise have set for the rest of the D3D9 suite.
    cna_register_renderer_test(NAME DirectX9_ConstantTable COMMAND ${_directx9_constanttable_cmd}
        TIMEOUT 30 LABELS "DIRECTX9"
        ENVIRONMENT "CNA_D3D9_SKIP_DXVK_GATE=1;CNA_D3D9_WINEPREFIX=$ENV{HOME}/.wine-cna-d3d9-spike")

    # D9-111 (Phase D9-11): validates D3D9EffectRenderer against a real device (compile, bind, draw,
    # uniform-driven pixel readback). Needs BOTH d3dcompiler (via the isolated
    # cna_renderer_d3d9_effect target, design decision 16) and a real D3D9 device (unlike
    # DirectX9_ConstantTable, this genuinely opens one and draws), so it needs the real-compiler prefix
    # (~/.wine-cna-d3d9-spike) AND is NOT exempt from the DXVK-engagement gate.
    if(TARGET cna_renderer_d3d9_effect)
        cna_directx9_test(cna_test_directx9_effectbackend directx9_effectrenderer_test.cpp)
        target_link_libraries(cna_test_directx9_effectbackend PRIVATE cna_renderer_d3d9_effect)
        cna_directx9_ctest_command(_directx9_effectrenderer_cmd cna_test_directx9_effectbackend)
        cna_register_renderer_test(NAME DirectX9_EffectRenderer COMMAND ${_directx9_effectrenderer_cmd}
            TIMEOUT 60 LABELS "DIRECTX9" ENVIRONMENT "CNA_D3D9_WINEPREFIX=$ENV{HOME}/.wine-cna-d3d9-spike")

        # D9-112: SpriteBatch::Begin(effect) wiring, through the real public
        # SpriteBatch/ShaderEffect API (not the raw renderer), mirroring D3D11's own DX-71 test
        # bar. CNA itself now links cna_renderer_d3d9_effect (the circular-link fix
        # above), so this needs no extra target_link_libraries beyond the standard cna_directx9_test()
        # macro set -- ShaderEffect's own constructor reaches DirectX9Renderer::
        # CreateEffectRenderer() transitively through the real device.
        cna_directx9_test(cna_test_directx9_spritebatch_customeffect directx9_spritebatch_customeffect_test.cpp)
        cna_directx9_ctest_command(_directx9_sb_customeffect_cmd cna_test_directx9_spritebatch_customeffect)
        cna_register_renderer_test(NAME DirectX9_SpriteBatch_CustomEffect COMMAND ${_directx9_sb_customeffect_cmd}
            TIMEOUT 60 LABELS "DIRECTX9" ENVIRONMENT "CNA_D3D9_WINEPREFIX=$ENV{HOME}/.wine-cna-d3d9-spike")
    endif()

    # REMED-GFX-127: every public Texture2D::GetData call must return the resource's real content or
    # reject the request deterministically -- never fabricate one. Pre-fix the shared render-target
    # fallback zero-initialized its own scratch buffer, handed it to ITextureRenderer::GetData (whose
    # interface default did nothing) and converted it for the caller regardless, so a renderer with no
    # readback returned a complete, uniformly transparent-black frame that satisfied both "the
    # destination was overwritten" and any transparent-black content expectation.
    cna_directx9_test(cna_test_directx9_texture2d_getdata_contract ${CNA_GRAPHICS_EXAMPLES_DIR}/texture2d_getdata_contract_test.cpp)
    cna_directx9_ctest_command(_directx9_texture2d_getdata_contract_cmd cna_test_directx9_texture2d_getdata_contract)
    cna_register_renderer_test(NAME DirectX9_Texture2D_GetDataContract
        COMMAND ${_directx9_texture2d_getdata_contract_cmd}
        TIMEOUT 60 LABELS "DIRECTX9")

    # REMED-GFX-149 (and REMED-GFX-128, the same expression): the exact XNA-compatible
    # startIndex/elementCount contract of every public Texture2D::GetData overload. startIndex is a
    # DESTINATION element offset and elementCount is the destination capacity available from it;
    # the whole-level overload applied startIndex to the SOURCE, rejected any non-zero offset on a
    # render target, rejected legal excess capacity, and silently returned a partial frame for an
    # undersized one.
    cna_directx9_test(cna_test_directx9_texture2d_getdata_transfer_range ${CNA_GRAPHICS_EXAMPLES_DIR}/texture2d_getdata_transfer_range_test.cpp)
    cna_directx9_ctest_command(_directx9_texture2d_getdata_transfer_range_cmd cna_test_directx9_texture2d_getdata_transfer_range)
    cna_register_renderer_test(NAME DirectX9_Texture2D_GetDataTransferRange
        COMMAND ${_directx9_texture2d_getdata_transfer_range_cmd}
        TIMEOUT 60 LABELS "DIRECTX9")

    # REMED-GFX-147: a RenderTarget2D used as a texture must sample in the same logical orientation
    # as an ordinary Texture2D holding identical bytes. Registered here as a cross-renderer control:
    # the defect was EasyGL-local (an OpenGL framebuffer's origin is bottom-left, so a target's
    # colour texture stores the image bottom-up and sampling did not compensate even though GetData
    # already did), and these runs are what establish that render-target and ordinary-texture
    # sampling already agree everywhere else rather than being made to agree by the fix.
    cna_directx9_test(cna_test_directx9_rt_sampling_orientation
                   ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_sampling_orientation_test.cpp)
    cna_directx9_ctest_command(_directx9_rt_sampling_orientation_cmd cna_test_directx9_rt_sampling_orientation)
    cna_register_renderer_test(NAME DirectX9_RenderTarget_SamplingOrientation
        COMMAND ${_directx9_rt_sampling_orientation_cmd}
        TIMEOUT 120 LABELS "DIRECTX9")



    # REMED-GFX-169 cross-renderer control: every stock 3D effect must sample through the public
    # GraphicsDevice.SamplerStates[slot]. The defect was Vulkan-local (six stock descriptor builders
    # took no sampler parameter, so all 15 of their bindings used a hardcoded default and the
    # sampler was absent from their cache keys); this run establishes that this renderer already
    # honoured the contract rather than being made to.
    cna_directx9_test(cna_test_directx9_stock_effect_sampler
                   ${CNA_GRAPHICS_EXAMPLES_DIR}/stock_effect_sampler_contract_test.cpp)
    cna_directx9_ctest_command(_directx9_stock_effect_sampler_cmd cna_test_directx9_stock_effect_sampler)
    cna_register_renderer_test(NAME DirectX9_StockEffectSamplerContract
        COMMAND ${_directx9_stock_effect_sampler_cmd}
        TIMEOUT 300 LABELS "DIRECTX9")

    # REMED-GFX-150 cross-renderer control: TextureFilter::Point must select exactly ONE texel and
    # TextureAddressMode must decide which one, on SpriteBatch and on the device SamplerStates[0] 3D
    # path alike. The defect was Software-local (its ApplySamplerState named none of its parameters
    # and one bilinear function served every textured fragment, so every draw was LinearClamp); this
    # run is what establishes that this renderer already honoured the contract rather than being made
    # to.
    # REMED-GFX-170: every public TextureFilter ordinal names a SEPARATE magnification, a separate
    # minification and a separate mipmap filter, so a renderer may not reduce the ordinal to one
    # boolean. WebGPU's SpriteBatch sampler and SDL_GPU's ONE shared sampler helper both resolved
    # `textureFilter == 0 ? LINEAR : NEAREST`, and both keyed their sampler cache on
    # `filter == 0 ? 0 : 1`, so Anisotropic, LinearMipPoint, MinPointMagLinearMipLinear and
    # MinPointMagLinearMipPoint all magnified with POINT. This fixture measures the two DIFFERENT
    # partitions of the nine ordinals that magnification and minification induce, on SpriteBatch and
    # on every textured stock family; the other renderers run it as controls.
    cna_directx9_test(cna_test_directx9_texture_filter_ordinal
                   ${CNA_GRAPHICS_EXAMPLES_DIR}/texture_filter_ordinal_contract_test.cpp)
    cna_directx9_ctest_command(_directx9_texture_filter_ordinal_cmd cna_test_directx9_texture_filter_ordinal)
    cna_register_renderer_test(NAME DirectX9_TextureFilterOrdinalContract
        COMMAND ${_directx9_texture_filter_ordinal_cmd}
        TIMEOUT 300 LABELS "DIRECTX9")

    # REMED-GFX-173 cross-renderer control: EnvironmentMapEffect samples TWO independent
    # resources -- the ordinary 2D texture through GraphicsDevice.SamplerStates[0] and the
    # reflection cube through SamplerStates[1]. SDL_GPU bound a literal LinearClamp for the cube
    # and captured only slot 0, so slot 1 could not reach replay at all. This fixture makes the
    # two slots independently observable (EnvironmentMapAmount 1 leaves the cube the only
    # contributor, 0 leaves the 2D texture the only one) and measures what every other renderer
    # does with the same public state.
    cna_directx9_test(cna_test_directx9_envmap_cube_sampler
                   ${CNA_GRAPHICS_EXAMPLES_DIR}/envmap_cube_sampler_contract_test.cpp)
    cna_directx9_ctest_command(_directx9_envmap_cube_sampler_cmd cna_test_directx9_envmap_cube_sampler)
    cna_register_renderer_test(NAME DirectX9_EnvMapCubeSamplerContract
        COMMAND ${_directx9_envmap_cube_sampler_cmd}
        TIMEOUT 600 LABELS "DIRECTX9")

    # REMED-GFX-172 cross-renderer control: DualTextureEffect's two layers have INDEPENDENT public
    # sampler slots -- FNA's DualTextureEffect.fx declares DECLARE_TEXTURE(Texture, 0) and
    # DECLARE_TEXTURE(Texture2, 1). D3D9 declares stride 20 with vertexColor=false unsupported for
    # DualTextureEffect (plan_dx9.md D9-82d), so the fixture reports its own honest skip there.
    cna_directx9_test(cna_test_directx9_dualtexture_slot_sampler
                   ${CNA_GRAPHICS_EXAMPLES_DIR}/dualtexture_slot_sampler_contract_test.cpp)
    cna_directx9_ctest_command(_directx9_dualtexture_slot_sampler_cmd cna_test_directx9_dualtexture_slot_sampler)
    cna_register_renderer_test(NAME DirectX9_DualTextureSlotSamplerContract
        COMMAND ${_directx9_dualtexture_slot_sampler_cmd}
        TIMEOUT 600 LABELS "DIRECTX9")

    # REMED-GFX-175 cross-renderer control: the MIPMAP component of a TextureFilter ordinal.
    cna_directx9_test(cna_test_directx9_texture_filter_mip_contract
                   ${CNA_GRAPHICS_EXAMPLES_DIR}/texture_filter_mip_contract_test.cpp)
    cna_directx9_ctest_command(_directx9_texture_filter_mip_cmd cna_test_directx9_texture_filter_mip_contract)
    cna_register_renderer_test(NAME DirectX9_TextureFilterMipContract
        COMMAND ${_directx9_texture_filter_mip_cmd}
        TIMEOUT 300 LABELS "DIRECTX9")

    # REMED-GFX-177 cross-renderer control: descriptor/binding bookkeeping must be a function of what
    # is LIVE, never of what has ever existed. D3D12 owned four fixed-capacity heaps with a monotonic
    # bump cursor and no free list, so the 65th sampleable resource EVER CREATED threw even when six
    # were alive. Every draw here is checked against a self-identifying oracle that decodes back to an
    # integer, so a recycled or grown pool that aliases one resource onto another's binding names the
    # wrong resource rather than merely producing an odd colour.
    cna_directx9_test(cna_test_directx9_descriptor_capacity
                   ${CNA_GRAPHICS_EXAMPLES_DIR}/descriptor_capacity_contract_test.cpp)
    cna_directx9_ctest_command(_directx9_descriptor_capacity_cmd cna_test_directx9_descriptor_capacity)
    cna_register_renderer_test(NAME DirectX9_DescriptorCapacityContract
        COMMAND ${_directx9_descriptor_capacity_cmd}
        TIMEOUT 900 LABELS "DIRECTX9")

    cna_directx9_test(cna_test_directx9_point_sampling
                   ${CNA_GRAPHICS_EXAMPLES_DIR}/point_sampling_contract_test.cpp)
    cna_directx9_ctest_command(_directx9_point_sampling_cmd cna_test_directx9_point_sampling)
    cna_register_renderer_test(NAME DirectX9_PointSamplingContract
        COMMAND ${_directx9_point_sampling_cmd}
        TIMEOUT 300 LABELS "DIRECTX9")

    # REMED-GFX-151 cross-renderer control: the canonical XNA render-to-texture sequence -- render
    # into a target, unbind it, sample it -- must complete in ONE public frame with no intervening
    # GetData, Present, extra frame, manual flush or wait. The defect was Vulkan-local (its deferred
    # recorder's readback flush filtered the frame's segment list down to the target being READ, so a
    # producer's pass was never recorded before the consumer that sampled it); these runs are what
    # establish that every other renderer already honoured the contract rather than being made to.
    cna_directx9_test(cna_test_directx9_rt_producer_consumer
                   ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_producer_consumer_test.cpp)
    cna_directx9_ctest_command(_directx9_rt_producer_consumer_cmd cna_test_directx9_rt_producer_consumer)
    cna_register_renderer_test(NAME DirectX9_RenderTarget_ProducerConsumer
        COMMAND ${_directx9_rt_producer_consumer_cmd}
        TIMEOUT 120 LABELS "DIRECTX9")

    # REMED-GFX-152 cross-renderer control: a RenderTarget2D handed to a stock or custom 3D effect as
    # its texture must be sampled, not reinterpreted. The defect was SDL_GPU-local and fatal (its
    # stock-effect paths static_cast an ITextureRenderer* to the unrelated sibling
    # SdlGpuTextureRenderer, fabricating an SDL_GPUTexture* out of a render target's own fields); this
    # run is what establishes that D3D9 already honoured the contract rather than being made to.
    cna_directx9_test(cna_test_directx9_rt_effect_source
                   ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_effect_source_test.cpp)
    cna_directx9_ctest_command(_directx9_rt_effect_source_cmd cna_test_directx9_rt_effect_source)
    cna_register_renderer_test(NAME DirectX9_RenderTarget_EffectSource
        COMMAND ${_directx9_rt_effect_source_cmd}
        TIMEOUT 300 LABELS "DIRECTX9")

    # REMED-GFX-130: the TextureCube/Texture3D half of REMED-GFX-127's finding. Both public cube and
    # volume GetData paths converted their own zero-initialized scratch buffer for the caller
    # regardless of whether the renderer read anything back. The volume half needs a
    # GraphicsProfile::HiDef device (D9-100: Reach does not support volume textures at all), which
    # the test itself requests.
    cna_directx9_test(cna_test_directx9_cube_volume_getdata_contract
                  ${CNA_GRAPHICS_EXAMPLES_DIR}/texturecube_texture3d_getdata_contract_test.cpp)
    cna_directx9_ctest_command(_directx9_cube_volume_getdata_contract_cmd cna_test_directx9_cube_volume_getdata_contract)
    cna_register_renderer_test(NAME DirectX9_CubeVolume_GetDataContract
        COMMAND ${_directx9_cube_volume_getdata_contract_cmd}
        TIMEOUT 60 LABELS "DIRECTX9")

    # REMED-GFX-134: `RenderTargetCube::GetData` must return the face that was really rendered or
    # reject the request deterministically. REMED-GFX-130 made the reporting honest but left the
    # readback itself implemented on only two renderers, so a rendered cube face had no byte-exact
    # public oracle anywhere else. Drawn geometry (never Clear -- see REMED-GFX-129) paints an
    # asymmetric five-region pattern whose colours rotate per face, so a flip, a rotation, a wrong
    # array layer/subresource, a stale face or an unresolved multisample surface all fail.
    cna_directx9_test(cna_test_directx9_rendertargetcube_getdata_contract
                  ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertargetcube_getdata_contract_test.cpp)
    cna_directx9_ctest_command(_directx9_rendertargetcube_getdata_contract_cmd cna_test_directx9_rendertargetcube_getdata_contract)
    cna_register_renderer_test(NAME DirectX9_RenderTargetCube_GetDataContract
        COMMAND ${_directx9_rendertargetcube_getdata_contract_cmd}
        TIMEOUT 60 LABELS "DIRECTX9")

    # REMED-GFX-136: IGraphicsRenderer::CreateRenderTargetCube had no `preserveContents` parameter,
    # unlike CreateRenderTarget2D, so a RenderTargetCube's real RenderTargetUsage never reached the
    # renderer and Vulkan/WebGPU discarded a PreserveContents cube face on every bind cycle. Reuses
    # REMED-GFX-134's asymmetric face producer, then rebinds and draws only a small marker: "the
    # marker landed" is what a discarding renderer also achieves, so it can never pass for
    # preservation.
    cna_directx9_test(cna_test_directx9_rendertargetcube_usage
                  ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertargetcube_usage_test.cpp)
    cna_directx9_ctest_command(_directx9_rendertargetcube_usage_cmd cna_test_directx9_rendertargetcube_usage)
    cna_register_renderer_test(NAME DirectX9_RenderTargetCube_Usage
        COMMAND ${_directx9_rendertargetcube_usage_cmd}
        TIMEOUT 60 LABELS "DIRECTX9")

    # REMED-GFX-141: a MULTISAMPLED RenderTargetCube shared ONE multisample colour attachment
    # across all six faces on EasyGL, Vulkan and SdlGpu, so a PreserveContents face reloaded whichever
    # face was rendered last instead of its own samples. Renders face A, then face B, then rebinds A for
    # a small partial update -- with no readback, Present or flush in between -- and reads the whole of
    # A: the resolved single-sample layer still held the correct A, which is why a full redraw and an
    # immediate read both passed before this.
    cna_directx9_test(cna_test_directx9_rendertargetcube_msaa_face
                  ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertargetcube_msaa_face_test.cpp)
    cna_directx9_ctest_command(_directx9_rendertargetcube_msaa_face_cmd cna_test_directx9_rendertargetcube_msaa_face)
    cna_register_renderer_test(NAME DirectX9_RenderTargetCube_MsaaFace
        COMMAND ${_directx9_rendertargetcube_msaa_face_cmd}
        TIMEOUT 60 LABELS "DIRECTX9")

    # REMED-GFX-142: RenderTargetUsage's DEPTH and STENCIL half. FNA3D's own header documents
    # `preserveTargetContents` as storing the "color/depth/stencil" contents, and FNA's DiscardContents
    # bind clears all three (Target|DepthBuffer|Stencil, MaxDepth, 0) -- so the enum governs depth and
    # stencil, not colour alone. Renders an occluder, unbinds, rebinds WITHOUT clearing and draws behind
    # it: three bands separate preserved depth from cleared depth, lost colour and a second pass that
    # never drew. A parallel stencil stamp/gate sequence does the same for stencil.
    cna_directx9_test(cna_test_directx9_rendertarget_depthstencil_usage
                  ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_depthstencil_usage_test.cpp)
    cna_directx9_ctest_command(_directx9_rendertarget_depthstencil_usage_cmd cna_test_directx9_rendertarget_depthstencil_usage)
    cna_register_renderer_test(NAME DirectX9_RenderTarget_DepthStencilUsage
        COMMAND ${_directx9_rendertarget_depthstencil_usage_cmd}
        TIMEOUT 60 LABELS "DIRECTX9")

    # REMED-GFX-140: every public render-target bind/unbind cycle must be its own logical pass.
    # `VulkanRenderer::RecordCommandBuffer` collected ONE render pass per unique render-target
    # source per flush and replayed every queued batch for it inside that pass, so two bind cycles of
    # one target within a single flush window shared one load action. The decisive checks all use
    # DiscardContents -- collapsing is invisible on a preserving target, which is why REMED-GFX-136
    # needed an artificial readback barrier to see it -- and nothing between two cycles ever forces a
    # flush.
    cna_directx9_test(cna_test_directx9_rendertarget_pass_boundary
                  ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_pass_boundary_test.cpp)
    cna_directx9_ctest_command(_directx9_rendertarget_pass_boundary_cmd cna_test_directx9_rendertarget_pass_boundary)
    cna_register_renderer_test(NAME DirectX9_RenderTarget_PassBoundary
        COMMAND ${_directx9_rendertarget_pass_boundary_cmd}
        TIMEOUT 90 LABELS "DIRECTX9")

    # REMED-GFX-129: cross-renderer control for Vulkan's ordered-Clear correction.
    cna_directx9_test(cna_test_directx9_ordered_clear
                  ${CNA_GRAPHICS_EXAMPLES_DIR}/graphicsdevice_ordered_clear_test.cpp)
    cna_directx9_ctest_command(_directx9_ordered_clear_cmd cna_test_directx9_ordered_clear)
    cna_register_renderer_test(NAME DirectX9_GraphicsDevice_OrderedClear
        COMMAND ${_directx9_ordered_clear_cmd}
        TIMEOUT 120 LABELS "DIRECTX9")

    # REMED-GFX-143: backbuffer work and render-target work must replay in ONE ordered stream.
    # REMED-GFX-140 (Vulkan) and REMED-GFX-145 (SdlGpu) gave every render-target bind cycle its own
    # native pass in public order, but both kept the BACKBUFFER as one trailing pass, so every
    # backbuffer draw of a frame was replayed after every target pass regardless of when it was
    # issued. A frame that samples a target onto the backbuffer and then renders into that target
    # again therefore saw the FINAL content. Every check queues its whole public sequence with no
    # Present, GetData, flush or extra frame between the cycles, and only then reads once.
    cna_directx9_test(cna_test_directx9_backbuffer_pass_order
                  ${CNA_GRAPHICS_EXAMPLES_DIR}/backbuffer_pass_order_test.cpp)
    cna_directx9_ctest_command(_directx9_backbuffer_pass_order_cmd cna_test_directx9_backbuffer_pass_order)
    cna_register_renderer_test(NAME DirectX9_Backbuffer_PassOrder
        COMMAND ${_directx9_backbuffer_pass_order_cmd}
        TIMEOUT 120 LABELS "DIRECTX9")

    # REMED-GFX-116 cross-renderer control: every deferred draw must execute under the
    # GraphicsDevice.Viewport active at its own public call. WebGPU resolved it live when it
    # recorded the pass; this file is the same public fixture, so a renderer that regresses
    # the contract is caught here rather than assumed correct.
    cna_directx9_test(cna_test_directx9_deferred_viewport
                   ${CNA_GRAPHICS_EXAMPLES_DIR}/deferred_viewport_capture_test.cpp)
    cna_directx9_ctest_command(_directx9_deferred_viewport_cmd cna_test_directx9_deferred_viewport)
    cna_register_renderer_test(NAME DirectX9_Deferred_Viewport
        COMMAND ${_directx9_deferred_viewport_cmd}
        TIMEOUT 120 LABELS "DIRECTX9")

    # REMED-GFX-146 cross-renderer control: every deferred draw must execute under the
    # GraphicsDevice.ScissorRectangle and RasterizerState.ScissorTestEnable active at its own
    # public call. WebGPU resolved both live when it recorded the pass; this file is the same
    # public fixture, so a renderer that regresses the contract is caught here rather than
    # assumed correct.
    cna_directx9_test(cna_test_directx9_deferred_scissor
                   ${CNA_GRAPHICS_EXAMPLES_DIR}/deferred_scissor_capture_test.cpp)
    cna_directx9_ctest_command(_directx9_deferred_scissor_cmd cna_test_directx9_deferred_scissor)
    cna_register_renderer_test(NAME DirectX9_Deferred_Scissor
        COMMAND ${_directx9_deferred_scissor_cmd}
        TIMEOUT 120 LABELS "DIRECTX9")

    # REMED-GFX-135: the WRITE half of the same finding. `TextureCube::SetData`/`Texture3D::SetData`
    # kept the pre-REMED-GFX-127 shape -- a `void` renderer method behind `if (renderer_)` -- so an
    # upload that stored nothing, or only part of the requested region, still returned normally.
    cna_directx9_test(cna_test_directx9_cube_volume_setdata_contract
                  ${CNA_GRAPHICS_EXAMPLES_DIR}/texturecube_texture3d_setdata_contract_test.cpp)
    cna_directx9_ctest_command(_directx9_cube_volume_setdata_contract_cmd cna_test_directx9_cube_volume_setdata_contract)
    cna_register_renderer_test(NAME DirectX9_CubeVolume_SetDataContract
        COMMAND ${_directx9_cube_volume_setdata_contract_cmd}
        TIMEOUT 60 LABELS "DIRECTX9")
    # REMED-GFX-158's control: a RenderTarget2D constructed during a public frame must be usable in
    # that same frame -- bound, cleared and/or drawn into, unbound and observed -- with no warm-up
    # frame, Present, GetData-before-first-render, dummy draw, empty bind cycle, manual frame advance
    # or wait. The defect was bgfx-local (`bgfx::reset()` discards every view's framebuffer binding,
    # including the bound target's), so this run is what establishes that D3D9 already honoured the
    # contract rather than being made to.
    cna_directx9_test(cna_test_directx9_rt_first_use ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_first_use_test.cpp)
    cna_directx9_ctest_command(_directx9_rt_first_use_cmd cna_test_directx9_rt_first_use)
    cna_register_renderer_test(NAME DirectX9_RenderTarget_FirstUse
        COMMAND ${_directx9_rt_first_use_cmd}
        TIMEOUT 180 LABELS "DIRECTX9")

endif()

# Module-local tests get the same headless-safe skip convention the root one-shot
# gives root-registered tests (see cmake/TestHelpers.cmake).
cna_apply_skip_convention()
