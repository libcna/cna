# directx11 renderer example/test registration (module-local; this file is the
# former cmake/Tests/DirectX11Tests.cmake, moved when examples moved into their owning
# physical modules).
#
# From the root CMakeLists.txt include site:
# plan_dx.md DX-80: D3D11 CTest registration. When cross-compiled from Linux (this renderer is
# Windows-only, design decision 2), CTest itself still runs on the Linux host, so the test COMMAND
# wraps the real .exe with scripts/run-wine-dxvk.sh (DX-3). A native MSVC configure on a real
# Windows machine (CMAKE_CROSSCOMPILING is false there) needs no Wine wrapper at all -- runs the
# .exe directly (see .github/workflows/d3d-windows-ci.yml, added specifically to exercise this
# path for real; it only covers MSVC-compile/unit-test/shader-generation-check, not a substitute
# for plan_dx.md DX-90's real swap-chain/tearing/device-lost/driver-parity items).

# Example/test executables historically live at the build root (scripts, docs and
# the golden-image working-directory conventions rely on it); keep them there.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
# Shared example-support headers ("common/PixelTestGame.hpp", ...) are owned by
# the graphics module's examples directory.
include_directories(${CNA_GRAPHICS_EXAMPLES_DIR})

if(CNA_BUILD_TESTS AND CNA_GRAPHICS_RENDERER STREQUAL "DIRECTX11")
    enable_testing()

    macro(cna_directx11_test target src)
        add_executable(${target} ${src})
        target_link_libraries(${target} PRIVATE CNA SDL3::SDL3)
        cna_link_sharp_runtime(${target} PRIVATE)
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

    macro(cna_directx11_ctest_command out_var target_name)
        if(CMAKE_CROSSCOMPILING)
            set(${out_var} ${CMAKE_SOURCE_DIR}/scripts/run-wine-dxvk.sh $<TARGET_FILE:${target_name}>)
        else()
            set(${out_var} $<TARGET_FILE:${target_name}>)
        endif()
    endmacro()

    cna_directx11_test(cna_test_directx11_smoke directx11_smoke_test.cpp)
    cna_directx11_ctest_command(_directx11_smoke_cmd cna_test_directx11_smoke)
    cna_register_renderer_test(NAME DirectX11_Smoke COMMAND ${_directx11_smoke_cmd}
        TIMEOUT 60 LABELS "DIRECTX11")

    # REMED-GFX-089 sibling oracle: the same renderer-neutral public
    # GraphicsDevice/DepthStencilState A(Default)->B(DepthRead)->C(None)->A contract as D3D9.
    cna_directx11_test(cna_test_directx11_depth_contract ${CNA_GRAPHICS_EXAMPLES_DIR}/graphicsdevice_depth_contract_test.cpp)
    cna_directx11_ctest_command(_directx11_depth_contract_cmd cna_test_directx11_depth_contract)
    cna_register_renderer_test(NAME DirectX11_GraphicsDevice_DepthContract
        COMMAND ${_directx11_depth_contract_cmd}
        TIMEOUT 60 LABELS "DIRECTX11")

    # Phase DIRECTX3: pure-function checks for D3DCommon's format/state/vertex-layout mapping tables --
    # no device/window/GPU needed.
    cna_directx11_test(cna_test_directx11_common directx11_common_test.cpp)
    cna_directx11_ctest_command(_directx11_common_cmd cna_test_directx11_common)
    # DX-85: this binary never creates a D3D11 device (pure-function mapping-table checks
    # only), so it legitimately never prints a "DXVK: <version>" line -- tell the wrapper's
    # DXVK-engagement gate not to misreport that as a WineD3D fallback. Harmless no-op when
    # CMAKE_CROSSCOMPILING is false (native MSVC run, no wrapper script involved at all).
    cna_register_renderer_test(NAME DirectX11_Common COMMAND ${_directx11_common_cmd}
        TIMEOUT 30 LABELS "DIRECTX11" ENVIRONMENT "CNA_D3D11_SKIP_DXVK_GATE=1")

    # DX-82: state-object pixel-behaviour tests, reusing the same renderer-agnostic EasyGL-authored
    # sources Vulkan already reuses verbatim (they only touch the public GraphicsDevice/BasicEffect
    # API, nothing EasyGL-specific) -- real behavioural proof through the full public draw path,
    # not just "the D3D11 state object was created/bound" (Phase DIRECTX7's own, narrower bar).
    cna_directx11_test(cna_test_directx11_blendstate_opaque ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_blendstate_opaque_test.cpp)
    cna_directx11_ctest_command(_directx11_blend_opaque_cmd cna_test_directx11_blendstate_opaque)
    cna_register_renderer_test(NAME DirectX11_BlendState_Opaque COMMAND ${_directx11_blend_opaque_cmd}
        TIMEOUT 60 LABELS "DIRECTX11")

    cna_directx11_test(cna_test_directx11_blendstate_alphablend ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_blendstate_alphablend_test.cpp)
    cna_directx11_ctest_command(_directx11_blend_alpha_cmd cna_test_directx11_blendstate_alphablend)
    cna_register_renderer_test(NAME DirectX11_BlendState_AlphaBlend COMMAND ${_directx11_blend_alpha_cmd}
        TIMEOUT 60 LABELS "DIRECTX11")

    cna_directx11_test(cna_test_directx11_depthstencilstate_stencil_enable ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_depthstencilstate_stencil_enable_test.cpp)
    cna_directx11_ctest_command(_directx11_ds_stencil_cmd cna_test_directx11_depthstencilstate_stencil_enable)
    cna_register_renderer_test(NAME DirectX11_DepthStencilState_StencilEnable COMMAND ${_directx11_ds_stencil_cmd}
        TIMEOUT 60 LABELS "DIRECTX11")

    cna_directx11_test(cna_test_directx11_rasterizerstate_cullmode ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_rasterizerstate_cullmode_test.cpp)
    cna_directx11_ctest_command(_directx11_rast_cullmode_cmd cna_test_directx11_rasterizerstate_cullmode)
    cna_register_renderer_test(NAME DirectX11_RasterizerState_CullMode COMMAND ${_directx11_rast_cullmode_cmd}
        TIMEOUT 60 LABELS "DIRECTX11")

    # plan_cnj.md CNB-58/CNB-67 follow-up: real-GPU pixel proof for PbrEffect (stride 48)/
    # SkinnedPbrEffect (stride 68)/SkinnedEffect vertex-color-on-skinned-mesh (stride 56,
    # Skinned3dColored/Skinned3dVertexLitColored) -- each new shader variant is actually selected
    # and executes correctly, not just linked.
    cna_directx11_test(cna_test_directx11_pbr_vertexcolor directx11_pbr_vertexcolor_test.cpp)
    cna_directx11_ctest_command(_directx11_pbr_vertexcolor_cmd cna_test_directx11_pbr_vertexcolor)
    cna_register_renderer_test(NAME DirectX11_Pbr_VertexColor COMMAND ${_directx11_pbr_vertexcolor_cmd}
        TIMEOUT 60 LABELS "DIRECTX11")

    # GLTF-213: exact mid-grey transfer witnesses on the shared D3D PBR pixel shaders.
    cna_directx11_test(cna_test_directx11_pbr_srgb_transfer
                       ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_pbr_srgb_transfer_test.cpp)
    cna_directx11_ctest_command(_directx11_pbr_srgb_cmd cna_test_directx11_pbr_srgb_transfer)
    cna_register_renderer_test(NAME DirectX11_Pbr_SrgbTransfer COMMAND ${_directx11_pbr_srgb_cmd}
        TIMEOUT 60 LABELS "DIRECTX11")

    # GLTF-343/344: real DXBC witnesses for dielectric F0/F90 on rigid and skinned PBR paths.
    cna_directx11_test(cna_test_directx11_pbr_fresnel_factors
                       ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_pbr_fresnel_factors_test.cpp)
    cna_directx11_ctest_command(_directx11_pbr_fresnel_cmd cna_test_directx11_pbr_fresnel_factors)
    cna_register_renderer_test(NAME DirectX11_Pbr_FresnelFactors COMMAND ${_directx11_pbr_fresnel_cmd}
        TIMEOUT 60 LABELS "DIRECTX11")

    # REMED-GFX-007: EnvironmentMapEffect emissive must be added UNSCALED (FNA Lighting.fxh:
    # litRGB = lightSum*Diffuse + Emissive). The shared, renderer-agnostic amount-zero test carries
    # the discriminating non-white-Diffuse case (pre-fix D3DCommon env_map3d.frag gives (50,25,12),
    # post-fix (100,50,25)). Reused verbatim from the Vulkan-authored source, same as the easygl_*
    # state tests above are reused here.
    cna_directx11_test(cna_test_directx11_environmentmapeffect_amount_zero ${CMAKE_SOURCE_DIR}/modules/renderers/vulkan/examples/vulkan_environmentmapeffect_amount_zero_test.cpp)
    cna_directx11_ctest_command(_directx11_envmap_amount_zero_cmd cna_test_directx11_environmentmapeffect_amount_zero)
    cna_register_renderer_test(NAME DirectX11_EnvironmentMapAmountZero COMMAND ${_directx11_envmap_amount_zero_cmd}
        TIMEOUT 60 LABELS "DIRECTX11")

    # REMED-GFX-006: SkinnedEffect must compose the World inverse-transpose normal matrix with the
    # bone-skin matrix. The shared, renderer-agnostic world-normal test has a rotation case (WHITE vs
    # BLACK, discriminates Variant A) and a non-uniform-scale case (228 correct vs 114 raw-World vs
    # 180 no-world). Reused verbatim from the Vulkan-authored source.
    cna_directx11_test(cna_test_directx11_skinnedeffect_world_normal ${CMAKE_SOURCE_DIR}/modules/renderers/vulkan/examples/vulkan_skinnedeffect_world_normal_test.cpp)
    cna_directx11_ctest_command(_directx11_skinned_world_normal_cmd cna_test_directx11_skinnedeffect_world_normal)
    cna_register_renderer_test(NAME DirectX11_SkinnedEffect_WorldNormal COMMAND ${_directx11_skinned_world_normal_cmd}
        TIMEOUT 60 LABELS "DIRECTX11")

    # REMED-GFX-008: analytic SkinnedEffect ambient/emissive lighting conformance (shared harness).
    cna_directx11_test(cna_test_directx11_skinnedeffect_lighting_conformance ${CNA_GRAPHICS_EXAMPLES_DIR}/skinnedeffect_lighting_conformance_test.cpp)
    cna_directx11_ctest_command(_directx11_skinned_lighting_conformance_cmd cna_test_directx11_skinnedeffect_lighting_conformance)
    cna_register_renderer_test(NAME DirectX11_SkinnedEffect_LightingConformance COMMAND ${_directx11_skinned_lighting_conformance_cmd}
        TIMEOUT 60 LABELS "DIRECTX11")

    # REMED-GFX-005/010: fog must use FNA's view-space fog vector (corrected, non-mirrored, eye-space
    # Z), not object-space Z. The shared transformed-camera harness (ViewSpaceFogRef) holds cases where
    # object-space Z != view-space Z, so it discriminates both the mirror (GFX-005) and the
    # object-vs-view-space (GFX-010) defects. Reused verbatim from the Vulkan-authored source.
    cna_directx11_test(cna_test_directx11_viewspace_fog ${CMAKE_SOURCE_DIR}/modules/renderers/vulkan/examples/vulkan_viewspace_fog_test.cpp)
    cna_directx11_ctest_command(_directx11_viewspace_fog_cmd cna_test_directx11_viewspace_fog)
    cna_register_renderer_test(NAME DirectX11_ViewSpaceFog COMMAND ${_directx11_viewspace_fog_cmd}
        TIMEOUT 60 LABELS "DIRECTX11")

    # REMED-GFX-005/010: AlphaTestEffect fog (its own scalar-fog cbuffer, restructured to carry the
    # FNA fog vector). Identity-View mirror discriminator: at z=0.45 the mirrored factor gives no fog,
    # the corrected FNA factor gives half fog. Reused verbatim from the Vulkan-authored source.
    cna_directx11_test(cna_test_directx11_alphatest_fog ${CMAKE_SOURCE_DIR}/modules/renderers/vulkan/examples/vulkan_alphatest_fog_test.cpp)
    cna_directx11_ctest_command(_directx11_alphatest_fog_cmd cna_test_directx11_alphatest_fog)
    cna_register_renderer_test(NAME DirectX11_AlphaTest_Fog COMMAND ${_directx11_alphatest_fog_cmd}
        TIMEOUT 60 LABELS "DIRECTX11")

    # REMED-GFX-061: representative public coverage for the remaining fog-capable XNA stock
    # effects. These use the same view-space-vector source and final RGB-only fog composition as
    # the canonical BasicEffect/SkinnedEffect fixture above; keeping them as focused family checks
    # avoids duplicating the full transformed-camera battery for every material path.
    cna_directx11_test(cna_test_directx11_dualtexture_fog ${CMAKE_SOURCE_DIR}/modules/renderers/vulkan/examples/vulkan_dualtextureeffect_fog_test.cpp)
    cna_directx11_ctest_command(_directx11_dualtexture_fog_cmd cna_test_directx11_dualtexture_fog)
    cna_register_renderer_test(NAME DirectX11_DualTextureEffect_Fog COMMAND ${_directx11_dualtexture_fog_cmd}
        TIMEOUT 60 LABELS "DIRECTX11")

    cna_directx11_test(cna_test_directx11_environmentmap_fog ${CMAKE_SOURCE_DIR}/modules/renderers/vulkan/examples/vulkan_environmentmapeffect_fog_test.cpp)
    cna_directx11_ctest_command(_directx11_environmentmap_fog_cmd cna_test_directx11_environmentmap_fog)
    cna_register_renderer_test(NAME DirectX11_EnvironmentMapEffect_Fog COMMAND ${_directx11_environmentmap_fog_cmd}
        TIMEOUT 60 LABELS "DIRECTX11")

    cna_directx11_test(cna_test_directx11_skinnedeffect_fog ${CMAKE_SOURCE_DIR}/modules/renderers/vulkan/examples/vulkan_skinnedeffect_fog_test.cpp)
    cna_directx11_ctest_command(_directx11_skinnedeffect_fog_cmd cna_test_directx11_skinnedeffect_fog)
    cna_register_renderer_test(NAME DirectX11_SkinnedEffect_Fog COMMAND ${_directx11_skinnedeffect_fog_cmd}
        TIMEOUT 60 LABELS "DIRECTX11")

    # REMED-GFX-072: D3D11 is the XNA-CORRECT ORACLE for SpriteBatch custom-Viewport behavior
    # (D3D11SpriteBatchRenderer feeds the sprite2d ViewportSize from the LIVE viewport via
    # RSGetViewports, so its projection is already viewport-relative). This runs the SAME conformance
    # test the fixed renderers run, and must pass 10/10 with NO D3D11 code change -- proving the
    # viewport-relative contract the other renderers were corrected to match.
    cna_directx11_test(cna_test_directx11_spritebatch_custom_viewport ${CNA_GRAPHICS_EXAMPLES_DIR}/spritebatch_custom_viewport_test.cpp)
    cna_directx11_ctest_command(_directx11_spritebatch_custom_viewport_cmd cna_test_directx11_spritebatch_custom_viewport)
    cna_register_renderer_test(NAME DirectX11_SpriteBatch_CustomViewport COMMAND ${_directx11_spritebatch_custom_viewport_cmd}
        TIMEOUT 60 LABELS "DIRECTX11")

    # REMED-GFX-127: every public Texture2D::GetData call must return the resource's real content or
    # reject the request deterministically -- never fabricate one. Pre-fix the shared render-target
    # fallback zero-initialized its own scratch buffer, handed it to ITextureRenderer::GetData (whose
    # interface default did nothing) and converted it for the caller regardless, so a renderer with no
    # readback returned a complete, uniformly transparent-black frame that satisfied both "the
    # destination was overwritten" and any transparent-black content expectation.
    cna_directx11_test(cna_test_directx11_texture2d_getdata_contract ${CNA_GRAPHICS_EXAMPLES_DIR}/texture2d_getdata_contract_test.cpp)
    cna_directx11_ctest_command(_directx11_texture2d_getdata_contract_cmd cna_test_directx11_texture2d_getdata_contract)
    cna_register_renderer_test(NAME DirectX11_Texture2D_GetDataContract
        COMMAND ${_directx11_texture2d_getdata_contract_cmd}
        TIMEOUT 60 LABELS "DIRECTX11")

    # REMED-GFX-149 (and REMED-GFX-128, the same expression): the exact XNA-compatible
    # startIndex/elementCount contract of every public Texture2D::GetData overload. startIndex is a
    # DESTINATION element offset and elementCount is the destination capacity available from it;
    # the whole-level overload applied startIndex to the SOURCE, rejected any non-zero offset on a
    # render target, rejected legal excess capacity, and silently returned a partial frame for an
    # undersized one.
    cna_directx11_test(cna_test_directx11_texture2d_getdata_transfer_range ${CNA_GRAPHICS_EXAMPLES_DIR}/texture2d_getdata_transfer_range_test.cpp)
    cna_directx11_ctest_command(_directx11_texture2d_getdata_transfer_range_cmd cna_test_directx11_texture2d_getdata_transfer_range)
    cna_register_renderer_test(NAME DirectX11_Texture2D_GetDataTransferRange
        COMMAND ${_directx11_texture2d_getdata_transfer_range_cmd}
        TIMEOUT 60 LABELS "DIRECTX11")

    # REMED-GFX-147: a RenderTarget2D used as a texture must sample in the same logical orientation
    # as an ordinary Texture2D holding identical bytes. Registered here as a cross-renderer control:
    # the defect was EasyGL-local (an OpenGL framebuffer's origin is bottom-left, so a target's
    # colour texture stores the image bottom-up and sampling did not compensate even though GetData
    # already did), and these runs are what establish that render-target and ordinary-texture
    # sampling already agree everywhere else rather than being made to agree by the fix.
    cna_directx11_test(cna_test_directx11_rt_sampling_orientation
                   ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_sampling_orientation_test.cpp)
    cna_directx11_ctest_command(_directx11_rt_sampling_orientation_cmd cna_test_directx11_rt_sampling_orientation)
    cna_register_renderer_test(NAME DirectX11_RenderTarget_SamplingOrientation
        COMMAND ${_directx11_rt_sampling_orientation_cmd}
        TIMEOUT 120 LABELS "DIRECTX11")



    # REMED-GFX-169 cross-renderer control: every stock 3D effect must sample through the public
    # GraphicsDevice.SamplerStates[slot]. The defect was Vulkan-local (six stock descriptor builders
    # took no sampler parameter, so all 15 of their bindings used a hardcoded default and the
    # sampler was absent from their cache keys); this run establishes that this renderer already
    # honoured the contract rather than being made to.
    cna_directx11_test(cna_test_directx11_stock_effect_sampler
                   ${CNA_GRAPHICS_EXAMPLES_DIR}/stock_effect_sampler_contract_test.cpp)
    cna_directx11_ctest_command(_directx11_stock_effect_sampler_cmd cna_test_directx11_stock_effect_sampler)
    cna_register_renderer_test(NAME DirectX11_StockEffectSamplerContract
        COMMAND ${_directx11_stock_effect_sampler_cmd}
        TIMEOUT 300 LABELS "DIRECTX11")

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
    cna_directx11_test(cna_test_directx11_texture_filter_ordinal
                   ${CNA_GRAPHICS_EXAMPLES_DIR}/texture_filter_ordinal_contract_test.cpp)
    cna_directx11_ctest_command(_directx11_texture_filter_ordinal_cmd cna_test_directx11_texture_filter_ordinal)
    cna_register_renderer_test(NAME DirectX11_TextureFilterOrdinalContract
        COMMAND ${_directx11_texture_filter_ordinal_cmd}
        TIMEOUT 300 LABELS "DIRECTX11")

    # REMED-GFX-173 cross-renderer control: EnvironmentMapEffect samples TWO independent
    # resources -- the ordinary 2D texture through GraphicsDevice.SamplerStates[0] and the
    # reflection cube through SamplerStates[1]. SDL_GPU bound a literal LinearClamp for the cube
    # and captured only slot 0, so slot 1 could not reach replay at all. This fixture makes the
    # two slots independently observable (EnvironmentMapAmount 1 leaves the cube the only
    # contributor, 0 leaves the 2D texture the only one) and measures what every other renderer
    # does with the same public state.
    cna_directx11_test(cna_test_directx11_envmap_cube_sampler
                   ${CNA_GRAPHICS_EXAMPLES_DIR}/envmap_cube_sampler_contract_test.cpp)
    cna_directx11_ctest_command(_directx11_envmap_cube_sampler_cmd cna_test_directx11_envmap_cube_sampler)
    cna_register_renderer_test(NAME DirectX11_EnvMapCubeSamplerContract
        COMMAND ${_directx11_envmap_cube_sampler_cmd}
        TIMEOUT 600 LABELS "DIRECTX11")

    # REMED-GFX-172 cross-renderer control: DualTextureEffect's two layers have INDEPENDENT public
    # sampler slots -- FNA's DualTextureEffect.fx declares DECLARE_TEXTURE(Texture, 0) and
    # DECLARE_TEXTURE(Texture2, 1). WebGPU declared ONE WGSL sampler for both texture views, so
    # SamplerStates[1] was inexpressible and slot 1 inherited slot 0's. This fixture observes both
    # layers in the SAME image -- slot 0's texture varies along X only, slot 1's along Y only.
    cna_directx11_test(cna_test_directx11_dualtexture_slot_sampler
                   ${CNA_GRAPHICS_EXAMPLES_DIR}/dualtexture_slot_sampler_contract_test.cpp)
    cna_directx11_ctest_command(_directx11_dualtexture_slot_sampler_cmd cna_test_directx11_dualtexture_slot_sampler)
    cna_register_renderer_test(NAME DirectX11_DualTextureSlotSamplerContract
        COMMAND ${_directx11_dualtexture_slot_sampler_cmd}
        TIMEOUT 600 LABELS "DIRECTX11")

    # REMED-GFX-175 cross-renderer control: the MIPMAP component of a TextureFilter ordinal.
    cna_directx11_test(cna_test_directx11_texture_filter_mip_contract
                   ${CNA_GRAPHICS_EXAMPLES_DIR}/texture_filter_mip_contract_test.cpp)
    cna_directx11_ctest_command(_directx11_texture_filter_mip_cmd cna_test_directx11_texture_filter_mip_contract)
    cna_register_renderer_test(NAME DirectX11_TextureFilterMipContract
        COMMAND ${_directx11_texture_filter_mip_cmd}
        TIMEOUT 300 LABELS "DIRECTX11")

    # REMED-GFX-177 cross-renderer control: descriptor/binding bookkeeping must be a function of what
    # is LIVE, never of what has ever existed. D3D12 owned four fixed-capacity heaps with a monotonic
    # bump cursor and no free list, so the 65th sampleable resource EVER CREATED threw even when six
    # were alive. Every draw here is checked against a self-identifying oracle that decodes back to an
    # integer, so a recycled or grown pool that aliases one resource onto another's binding names the
    # wrong resource rather than merely producing an odd colour.
    cna_directx11_test(cna_test_directx11_descriptor_capacity
                   ${CNA_GRAPHICS_EXAMPLES_DIR}/descriptor_capacity_contract_test.cpp)
    cna_directx11_ctest_command(_directx11_descriptor_capacity_cmd cna_test_directx11_descriptor_capacity)
    cna_register_renderer_test(NAME DirectX11_DescriptorCapacityContract
        COMMAND ${_directx11_descriptor_capacity_cmd}
        TIMEOUT 900 LABELS "DIRECTX11")

    cna_directx11_test(cna_test_directx11_point_sampling
                   ${CNA_GRAPHICS_EXAMPLES_DIR}/point_sampling_contract_test.cpp)
    cna_directx11_ctest_command(_directx11_point_sampling_cmd cna_test_directx11_point_sampling)
    cna_register_renderer_test(NAME DirectX11_PointSamplingContract
        COMMAND ${_directx11_point_sampling_cmd}
        TIMEOUT 300 LABELS "DIRECTX11")

    # REMED-GFX-151 cross-renderer control: the canonical XNA render-to-texture sequence -- render
    # into a target, unbind it, sample it -- must complete in ONE public frame with no intervening
    # GetData, Present, extra frame, manual flush or wait. The defect was Vulkan-local (its deferred
    # recorder's readback flush filtered the frame's segment list down to the target being READ, so a
    # producer's pass was never recorded before the consumer that sampled it); these runs are what
    # establish that every other renderer already honoured the contract rather than being made to.
    cna_directx11_test(cna_test_directx11_rt_producer_consumer
                   ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_producer_consumer_test.cpp)
    cna_directx11_ctest_command(_directx11_rt_producer_consumer_cmd cna_test_directx11_rt_producer_consumer)
    cna_register_renderer_test(NAME DirectX11_RenderTarget_ProducerConsumer
        COMMAND ${_directx11_rt_producer_consumer_cmd}
        TIMEOUT 120 LABELS "DIRECTX11")

    # REMED-GFX-152 cross-renderer control: a RenderTarget2D handed to a stock or custom 3D effect as
    # its texture must be sampled, not reinterpreted. The defect was SDL_GPU-local and fatal (its
    # stock-effect paths static_cast an ITextureRenderer* to the unrelated sibling
    # SdlGpuTextureRenderer, fabricating an SDL_GPUTexture* out of a render target's own fields); this
    # run is what establishes that D3D11 already honoured the contract rather than being made to.
    cna_directx11_test(cna_test_directx11_rt_effect_source
                   ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_effect_source_test.cpp)
    cna_directx11_ctest_command(_directx11_rt_effect_source_cmd cna_test_directx11_rt_effect_source)
    cna_register_renderer_test(NAME DirectX11_RenderTarget_EffectSource
        COMMAND ${_directx11_rt_effect_source_cmd}
        TIMEOUT 300 LABELS "DIRECTX11")

    # REMED-GFX-130: the TextureCube/Texture3D half of REMED-GFX-127's finding. Both public cube and
    # volume GetData paths converted their own zero-initialized scratch buffer for the caller
    # regardless of whether the renderer read anything back.
    cna_directx11_test(cna_test_directx11_cube_volume_getdata_contract
                   ${CNA_GRAPHICS_EXAMPLES_DIR}/texturecube_texture3d_getdata_contract_test.cpp)
    cna_directx11_ctest_command(_directx11_cube_volume_getdata_contract_cmd cna_test_directx11_cube_volume_getdata_contract)
    cna_register_renderer_test(NAME DirectX11_CubeVolume_GetDataContract
        COMMAND ${_directx11_cube_volume_getdata_contract_cmd}
        TIMEOUT 60 LABELS "DIRECTX11")

    # REMED-GFX-134: `RenderTargetCube::GetData` must return the face that was really rendered or
    # reject the request deterministically. REMED-GFX-130 made the reporting honest but left the
    # readback itself implemented on only two renderers, so a rendered cube face had no byte-exact
    # public oracle anywhere else. Drawn geometry (never Clear -- see REMED-GFX-129) paints an
    # asymmetric five-region pattern whose colours rotate per face, so a flip, a rotation, a wrong
    # array layer/subresource, a stale face or an unresolved multisample surface all fail.
    cna_directx11_test(cna_test_directx11_rendertargetcube_getdata_contract
                   ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertargetcube_getdata_contract_test.cpp)
    cna_directx11_ctest_command(_directx11_rendertargetcube_getdata_contract_cmd cna_test_directx11_rendertargetcube_getdata_contract)
    cna_register_renderer_test(NAME DirectX11_RenderTargetCube_GetDataContract
        COMMAND ${_directx11_rendertargetcube_getdata_contract_cmd}
        TIMEOUT 60 LABELS "DIRECTX11")

    # REMED-GFX-136: IGraphicsRenderer::CreateRenderTargetCube had no `preserveContents` parameter,
    # unlike CreateRenderTarget2D, so a RenderTargetCube's real RenderTargetUsage never reached the
    # renderer and Vulkan/WebGPU discarded a PreserveContents cube face on every bind cycle. Reuses
    # REMED-GFX-134's asymmetric face producer, then rebinds and draws only a small marker: "the
    # marker landed" is what a discarding renderer also achieves, so it can never pass for
    # preservation.
    cna_directx11_test(cna_test_directx11_rendertargetcube_usage
                   ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertargetcube_usage_test.cpp)
    cna_directx11_ctest_command(_directx11_rendertargetcube_usage_cmd cna_test_directx11_rendertargetcube_usage)
    cna_register_renderer_test(NAME DirectX11_RenderTargetCube_Usage
        COMMAND ${_directx11_rendertargetcube_usage_cmd}
        TIMEOUT 60 LABELS "DIRECTX11")

    # REMED-GFX-141: a MULTISAMPLED RenderTargetCube shared ONE multisample colour attachment
    # across all six faces on EasyGL, Vulkan and SdlGpu, so a PreserveContents face reloaded whichever
    # face was rendered last instead of its own samples. Renders face A, then face B, then rebinds A for
    # a small partial update -- with no readback, Present or flush in between -- and reads the whole of
    # A: the resolved single-sample layer still held the correct A, which is why a full redraw and an
    # immediate read both passed before this.
    cna_directx11_test(cna_test_directx11_rendertargetcube_msaa_face
                   ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertargetcube_msaa_face_test.cpp)
    cna_directx11_ctest_command(_directx11_rendertargetcube_msaa_face_cmd cna_test_directx11_rendertargetcube_msaa_face)
    cna_register_renderer_test(NAME DirectX11_RenderTargetCube_MsaaFace
        COMMAND ${_directx11_rendertargetcube_msaa_face_cmd}
        TIMEOUT 60 LABELS "DIRECTX11")

    # REMED-GFX-142: RenderTargetUsage's DEPTH and STENCIL half. FNA3D's own header documents
    # `preserveTargetContents` as storing the "color/depth/stencil" contents, and FNA's DiscardContents
    # bind clears all three (Target|DepthBuffer|Stencil, MaxDepth, 0) -- so the enum governs depth and
    # stencil, not colour alone. Renders an occluder, unbinds, rebinds WITHOUT clearing and draws behind
    # it: three bands separate preserved depth from cleared depth, lost colour and a second pass that
    # never drew. A parallel stencil stamp/gate sequence does the same for stencil.
    cna_directx11_test(cna_test_directx11_rendertarget_depthstencil_usage
                   ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_depthstencil_usage_test.cpp)
    cna_directx11_ctest_command(_directx11_rendertarget_depthstencil_usage_cmd cna_test_directx11_rendertarget_depthstencil_usage)
    cna_register_renderer_test(NAME DirectX11_RenderTarget_DepthStencilUsage
        COMMAND ${_directx11_rendertarget_depthstencil_usage_cmd}
        TIMEOUT 60 LABELS "DIRECTX11")

    # REMED-GFX-140: every public render-target bind/unbind cycle must be its own logical pass.
    # `VulkanRenderer::RecordCommandBuffer` collected ONE render pass per unique render-target
    # source per flush and replayed every queued batch for it inside that pass, so two bind cycles of
    # one target within a single flush window shared one load action. The decisive checks all use
    # DiscardContents -- collapsing is invisible on a preserving target, which is why REMED-GFX-136
    # needed an artificial readback barrier to see it -- and nothing between two cycles ever forces a
    # flush.
    cna_directx11_test(cna_test_directx11_rendertarget_pass_boundary
                   ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_pass_boundary_test.cpp)
    cna_directx11_ctest_command(_directx11_rendertarget_pass_boundary_cmd cna_test_directx11_rendertarget_pass_boundary)
    cna_register_renderer_test(NAME DirectX11_RenderTarget_PassBoundary
        COMMAND ${_directx11_rendertarget_pass_boundary_cmd}
        TIMEOUT 90 LABELS "DIRECTX11")

    # REMED-GFX-129: cross-renderer control for Vulkan's ordered-Clear correction.
    cna_directx11_test(cna_test_directx11_ordered_clear
                   ${CNA_GRAPHICS_EXAMPLES_DIR}/graphicsdevice_ordered_clear_test.cpp)
    cna_directx11_ctest_command(_directx11_ordered_clear_cmd cna_test_directx11_ordered_clear)
    cna_register_renderer_test(NAME DirectX11_GraphicsDevice_OrderedClear
        COMMAND ${_directx11_ordered_clear_cmd}
        TIMEOUT 120 LABELS "DIRECTX11")

    # REMED-GFX-143: backbuffer work and render-target work must replay in ONE ordered stream.
    # REMED-GFX-140 (Vulkan) and REMED-GFX-145 (SdlGpu) gave every render-target bind cycle its own
    # native pass in public order, but both kept the BACKBUFFER as one trailing pass, so every
    # backbuffer draw of a frame was replayed after every target pass regardless of when it was
    # issued. A frame that samples a target onto the backbuffer and then renders into that target
    # again therefore saw the FINAL content. Every check queues its whole public sequence with no
    # Present, GetData, flush or extra frame between the cycles, and only then reads once.
    cna_directx11_test(cna_test_directx11_backbuffer_pass_order
                   ${CNA_GRAPHICS_EXAMPLES_DIR}/backbuffer_pass_order_test.cpp)
    cna_directx11_ctest_command(_directx11_backbuffer_pass_order_cmd cna_test_directx11_backbuffer_pass_order)
    cna_register_renderer_test(NAME DirectX11_Backbuffer_PassOrder
        COMMAND ${_directx11_backbuffer_pass_order_cmd}
        TIMEOUT 120 LABELS "DIRECTX11")

    # REMED-GFX-116 cross-renderer control: every deferred draw must execute under the
    # GraphicsDevice.Viewport active at its own public call. WebGPU resolved it live when it
    # recorded the pass; this file is the same public fixture, so a renderer that regresses
    # the contract is caught here rather than assumed correct.
    cna_directx11_test(cna_test_directx11_deferred_viewport
                   ${CNA_GRAPHICS_EXAMPLES_DIR}/deferred_viewport_capture_test.cpp)
    cna_directx11_ctest_command(_directx11_deferred_viewport_cmd cna_test_directx11_deferred_viewport)
    cna_register_renderer_test(NAME DirectX11_Deferred_Viewport
        COMMAND ${_directx11_deferred_viewport_cmd}
        TIMEOUT 120 LABELS "DIRECTX11")

    # REMED-GFX-146 cross-renderer control: every deferred draw must execute under the
    # GraphicsDevice.ScissorRectangle and RasterizerState.ScissorTestEnable active at its own
    # public call. WebGPU resolved both live when it recorded the pass; this file is the same
    # public fixture, so a renderer that regresses the contract is caught here rather than
    # assumed correct.
    cna_directx11_test(cna_test_directx11_deferred_scissor
                   ${CNA_GRAPHICS_EXAMPLES_DIR}/deferred_scissor_capture_test.cpp)
    cna_directx11_ctest_command(_directx11_deferred_scissor_cmd cna_test_directx11_deferred_scissor)
    cna_register_renderer_test(NAME DirectX11_Deferred_Scissor
        COMMAND ${_directx11_deferred_scissor_cmd}
        TIMEOUT 120 LABELS "DIRECTX11")

    # REMED-GFX-135: the WRITE half of the same finding. `TextureCube::SetData`/`Texture3D::SetData`
    # kept the pre-REMED-GFX-127 shape -- a `void` renderer method behind `if (renderer_)` -- so an
    # upload that stored nothing, or only part of the requested region, still returned normally.
    cna_directx11_test(cna_test_directx11_cube_volume_setdata_contract
                   ${CNA_GRAPHICS_EXAMPLES_DIR}/texturecube_texture3d_setdata_contract_test.cpp)
    cna_directx11_ctest_command(_directx11_cube_volume_setdata_contract_cmd cna_test_directx11_cube_volume_setdata_contract)
    cna_register_renderer_test(NAME DirectX11_CubeVolume_SetDataContract
        COMMAND ${_directx11_cube_volume_setdata_contract_cmd}
        TIMEOUT 60 LABELS "DIRECTX11")

    # REMED-GFX-077 runtime verification for D3D11 lives inside cna_test_directx11_smoke (Check GFX077):
    # D3D11 has no public RenderTarget2D GPU-readback path (only the back buffer via
    # GetBackBufferData / the renderer's staging-copy), so ColorWriteChannels/MultiSampleMask are
    # verified through the smoke test's established back-buffer draw+readback discipline (Check P),
    # not the RT-readback Game harness the native SdlGpu/WebGPU renderers use.
    # REMED-GFX-158's control: a RenderTarget2D constructed during a public frame must be usable in
    # that same frame -- bound, cleared and/or drawn into, unbound and observed -- with no warm-up
    # frame, Present, GetData-before-first-render, dummy draw, empty bind cycle, manual frame advance
    # or wait. The defect was bgfx-local (`bgfx::reset()` discards every view's framebuffer binding,
    # including the bound target's), so this run is what establishes that D3D11 already honoured the
    # contract rather than being made to.
    cna_directx11_test(cna_test_directx11_rt_first_use ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_first_use_test.cpp)
    cna_directx11_ctest_command(_directx11_rt_first_use_cmd cna_test_directx11_rt_first_use)
    cna_register_renderer_test(NAME DirectX11_RenderTarget_FirstUse
        COMMAND ${_directx11_rt_first_use_cmd}
        TIMEOUT 180 LABELS "DIRECTX11")

endif()

# Module-local tests get the same headless-safe skip convention the root one-shot
# gives root-registered tests (see cmake/TestHelpers.cmake).
cna_apply_skip_convention()
