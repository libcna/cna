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

    # REMED-GFX-089 sibling oracle: the same backend-neutral public
    # GraphicsDevice/DepthStencilState A(Default)->B(DepthRead)->C(None)->A contract as D3D9.
    cna_d3d11_test(cna_test_d3d11_depth_contract examples/graphicsdevice_depth_contract_test.cpp)
    cna_d3d11_ctest_command(_d3d11_depth_contract_cmd cna_test_d3d11_depth_contract)
    cna_register_backend_test(NAME D3D11_GraphicsDevice_DepthContract
        COMMAND ${_d3d11_depth_contract_cmd}
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

    # plan_cnj.md CNB-58/CNB-67 follow-up: real-GPU pixel proof for PbrEffect (stride 48)/
    # SkinnedPbrEffect (stride 68)/SkinnedEffect vertex-color-on-skinned-mesh (stride 56,
    # Skinned3dColored/Skinned3dVertexLitColored) -- each new shader variant is actually selected
    # and executes correctly, not just linked.
    cna_d3d11_test(cna_test_d3d11_pbr_vertexcolor examples/d3d11_pbr_vertexcolor_test.cpp)
    cna_d3d11_ctest_command(_d3d11_pbr_vertexcolor_cmd cna_test_d3d11_pbr_vertexcolor)
    cna_register_backend_test(NAME D3D11_Pbr_VertexColor COMMAND ${_d3d11_pbr_vertexcolor_cmd}
        TIMEOUT 60 LABELS "D3D11")

    # REMED-GFX-007: EnvironmentMapEffect emissive must be added UNSCALED (FNA Lighting.fxh:
    # litRGB = lightSum*Diffuse + Emissive). The shared, backend-agnostic amount-zero test carries
    # the discriminating non-white-Diffuse case (pre-fix D3DCommon env_map3d.frag gives (50,25,12),
    # post-fix (100,50,25)). Reused verbatim from the Vulkan-authored source, same as the easygl_*
    # state tests above are reused here.
    cna_d3d11_test(cna_test_d3d11_environmentmapeffect_amount_zero examples/vulkan_environmentmapeffect_amount_zero_test.cpp)
    cna_d3d11_ctest_command(_d3d11_envmap_amount_zero_cmd cna_test_d3d11_environmentmapeffect_amount_zero)
    cna_register_backend_test(NAME D3D11_EnvironmentMapAmountZero COMMAND ${_d3d11_envmap_amount_zero_cmd}
        TIMEOUT 60 LABELS "D3D11")

    # REMED-GFX-006: SkinnedEffect must compose the World inverse-transpose normal matrix with the
    # bone-skin matrix. The shared, backend-agnostic world-normal test has a rotation case (WHITE vs
    # BLACK, discriminates Variant A) and a non-uniform-scale case (228 correct vs 114 raw-World vs
    # 180 no-world). Reused verbatim from the Vulkan-authored source.
    cna_d3d11_test(cna_test_d3d11_skinnedeffect_world_normal examples/vulkan_skinnedeffect_world_normal_test.cpp)
    cna_d3d11_ctest_command(_d3d11_skinned_world_normal_cmd cna_test_d3d11_skinnedeffect_world_normal)
    cna_register_backend_test(NAME D3D11_SkinnedEffect_WorldNormal COMMAND ${_d3d11_skinned_world_normal_cmd}
        TIMEOUT 60 LABELS "D3D11")

    # REMED-GFX-008: analytic SkinnedEffect ambient/emissive lighting conformance (shared harness).
    cna_d3d11_test(cna_test_d3d11_skinnedeffect_lighting_conformance examples/skinnedeffect_lighting_conformance_test.cpp)
    cna_d3d11_ctest_command(_d3d11_skinned_lighting_conformance_cmd cna_test_d3d11_skinnedeffect_lighting_conformance)
    cna_register_backend_test(NAME D3D11_SkinnedEffect_LightingConformance COMMAND ${_d3d11_skinned_lighting_conformance_cmd}
        TIMEOUT 60 LABELS "D3D11")

    # REMED-GFX-005/010: fog must use FNA's view-space fog vector (corrected, non-mirrored, eye-space
    # Z), not object-space Z. The shared transformed-camera harness (ViewSpaceFogRef) holds cases where
    # object-space Z != view-space Z, so it discriminates both the mirror (GFX-005) and the
    # object-vs-view-space (GFX-010) defects. Reused verbatim from the Vulkan-authored source.
    cna_d3d11_test(cna_test_d3d11_viewspace_fog examples/vulkan_viewspace_fog_test.cpp)
    cna_d3d11_ctest_command(_d3d11_viewspace_fog_cmd cna_test_d3d11_viewspace_fog)
    cna_register_backend_test(NAME D3D11_ViewSpaceFog COMMAND ${_d3d11_viewspace_fog_cmd}
        TIMEOUT 60 LABELS "D3D11")

    # REMED-GFX-005/010: AlphaTestEffect fog (its own scalar-fog cbuffer, restructured to carry the
    # FNA fog vector). Identity-View mirror discriminator: at z=0.45 the mirrored factor gives no fog,
    # the corrected FNA factor gives half fog. Reused verbatim from the Vulkan-authored source.
    cna_d3d11_test(cna_test_d3d11_alphatest_fog examples/vulkan_alphatest_fog_test.cpp)
    cna_d3d11_ctest_command(_d3d11_alphatest_fog_cmd cna_test_d3d11_alphatest_fog)
    cna_register_backend_test(NAME D3D11_AlphaTest_Fog COMMAND ${_d3d11_alphatest_fog_cmd}
        TIMEOUT 60 LABELS "D3D11")

    # REMED-GFX-061: representative public coverage for the remaining fog-capable XNA stock
    # effects. These use the same view-space-vector source and final RGB-only fog composition as
    # the canonical BasicEffect/SkinnedEffect fixture above; keeping them as focused family checks
    # avoids duplicating the full transformed-camera battery for every material path.
    cna_d3d11_test(cna_test_d3d11_dualtexture_fog examples/vulkan_dualtextureeffect_fog_test.cpp)
    cna_d3d11_ctest_command(_d3d11_dualtexture_fog_cmd cna_test_d3d11_dualtexture_fog)
    cna_register_backend_test(NAME D3D11_DualTextureEffect_Fog COMMAND ${_d3d11_dualtexture_fog_cmd}
        TIMEOUT 60 LABELS "D3D11")

    cna_d3d11_test(cna_test_d3d11_environmentmap_fog examples/vulkan_environmentmapeffect_fog_test.cpp)
    cna_d3d11_ctest_command(_d3d11_environmentmap_fog_cmd cna_test_d3d11_environmentmap_fog)
    cna_register_backend_test(NAME D3D11_EnvironmentMapEffect_Fog COMMAND ${_d3d11_environmentmap_fog_cmd}
        TIMEOUT 60 LABELS "D3D11")

    cna_d3d11_test(cna_test_d3d11_skinnedeffect_fog examples/vulkan_skinnedeffect_fog_test.cpp)
    cna_d3d11_ctest_command(_d3d11_skinnedeffect_fog_cmd cna_test_d3d11_skinnedeffect_fog)
    cna_register_backend_test(NAME D3D11_SkinnedEffect_Fog COMMAND ${_d3d11_skinnedeffect_fog_cmd}
        TIMEOUT 60 LABELS "D3D11")

    # REMED-GFX-072: D3D11 is the XNA-CORRECT ORACLE for SpriteBatch custom-Viewport behavior
    # (D3D11SpriteBatchBackend feeds the sprite2d ViewportSize from the LIVE viewport via
    # RSGetViewports, so its projection is already viewport-relative). This runs the SAME conformance
    # test the fixed backends run, and must pass 10/10 with NO D3D11 code change -- proving the
    # viewport-relative contract the other backends were corrected to match.
    cna_d3d11_test(cna_test_d3d11_spritebatch_custom_viewport examples/spritebatch_custom_viewport_test.cpp)
    cna_d3d11_ctest_command(_d3d11_spritebatch_custom_viewport_cmd cna_test_d3d11_spritebatch_custom_viewport)
    cna_register_backend_test(NAME D3D11_SpriteBatch_CustomViewport COMMAND ${_d3d11_spritebatch_custom_viewport_cmd}
        TIMEOUT 60 LABELS "D3D11")

    # REMED-GFX-127: every public Texture2D::GetData call must return the resource's real content or
    # reject the request deterministically -- never fabricate one. Pre-fix the shared render-target
    # fallback zero-initialized its own scratch buffer, handed it to ITextureBackend::GetData (whose
    # interface default did nothing) and converted it for the caller regardless, so a backend with no
    # readback returned a complete, uniformly transparent-black frame that satisfied both "the
    # destination was overwritten" and any transparent-black content expectation.
    cna_d3d11_test(cna_test_d3d11_texture2d_getdata_contract examples/texture2d_getdata_contract_test.cpp)
    cna_d3d11_ctest_command(_d3d11_texture2d_getdata_contract_cmd cna_test_d3d11_texture2d_getdata_contract)
    cna_register_backend_test(NAME D3D11_Texture2D_GetDataContract
        COMMAND ${_d3d11_texture2d_getdata_contract_cmd}
        TIMEOUT 60 LABELS "D3D11")

    # REMED-GFX-130: the TextureCube/Texture3D half of REMED-GFX-127's finding. Both public cube and
    # volume GetData paths converted their own zero-initialized scratch buffer for the caller
    # regardless of whether the backend read anything back.
    cna_d3d11_test(cna_test_d3d11_cube_volume_getdata_contract
                   examples/texturecube_texture3d_getdata_contract_test.cpp)
    cna_d3d11_ctest_command(_d3d11_cube_volume_getdata_contract_cmd cna_test_d3d11_cube_volume_getdata_contract)
    cna_register_backend_test(NAME D3D11_CubeVolume_GetDataContract
        COMMAND ${_d3d11_cube_volume_getdata_contract_cmd}
        TIMEOUT 60 LABELS "D3D11")

    # REMED-GFX-135: the WRITE half of the same finding. `TextureCube::SetData`/`Texture3D::SetData`
    # kept the pre-REMED-GFX-127 shape -- a `void` backend method behind `if (backend_)` -- so an
    # upload that stored nothing, or only part of the requested region, still returned normally.
    cna_d3d11_test(cna_test_d3d11_cube_volume_setdata_contract
                   examples/texturecube_texture3d_setdata_contract_test.cpp)
    cna_d3d11_ctest_command(_d3d11_cube_volume_setdata_contract_cmd cna_test_d3d11_cube_volume_setdata_contract)
    cna_register_backend_test(NAME D3D11_CubeVolume_SetDataContract
        COMMAND ${_d3d11_cube_volume_setdata_contract_cmd}
        TIMEOUT 60 LABELS "D3D11")

    # REMED-GFX-077 runtime verification for D3D11 lives inside cna_test_d3d11_smoke (Check GFX077):
    # D3D11 has no public RenderTarget2D GPU-readback path (only the back buffer via
    # GetBackBufferData / the backend's staging-copy), so ColorWriteChannels/MultiSampleMask are
    # verified through the smoke test's established back-buffer draw+readback discipline (Check P),
    # not the RT-readback Game harness the native SdlGpu/WebGPU backends use.
endif()
