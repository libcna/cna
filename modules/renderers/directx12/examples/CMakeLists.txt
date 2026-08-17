# directx12 renderer example/test registration (module-local; this file is the
# former cmake/Tests/DirectX12Tests.cmake, moved when examples moved into their owning
# physical modules).
#
# From the root CMakeLists.txt include site:
# plan_dx.md DX-102/103/104/105: D3D12 CTest registration, mirroring DX-80's own D3D11 macro
# exactly, including the CMAKE_CROSSCOMPILING-aware command selection above (native MSVC runs the
# .exe directly; a Linux-hosted cross-compile wraps it with scripts/run-wine-vkd3d.sh, DX-100's own
# Wine+vkd3d-proton dev loop, a distinct Wine prefix from D3D11's DXVK one).

# Example/test executables historically live at the build root (scripts, docs and
# the golden-image working-directory conventions rely on it); keep them there.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
# Shared example-support headers ("common/PixelTestGame.hpp", ...) are owned by
# the graphics module's examples directory.
include_directories(${CNA_GRAPHICS_EXAMPLES_DIR})

if(CNA_BUILD_TESTS AND CNA_GRAPHICS_RENDERER STREQUAL "DIRECTX12")
    enable_testing()

    macro(cna_directx12_test target src)
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

    # DX-102/103/104/105: off-screen device/queue/heap/command-list/fence proof -- deliberately
    # never constructs a window/swap chain (see directx12_smoke_test.cpp's own header comment
    # for why), so this stays genuinely green on this Wine+vkd3d-proton dev loop.
    cna_directx12_test(cna_test_directx12_smoke directx12_smoke_test.cpp)
    if(CMAKE_CROSSCOMPILING)
        set(_directx12_smoke_cmd ${CMAKE_SOURCE_DIR}/scripts/run-wine-vkd3d.sh $<TARGET_FILE:cna_test_directx12_smoke>)
    else()
        set(_directx12_smoke_cmd cna_test_directx12_smoke)
    endif()
    cna_register_renderer_test(NAME DirectX12_Smoke COMMAND ${_directx12_smoke_cmd}
        TIMEOUT 60 LABELS "DIRECTX12")

    # plan_dx.md DX-102: real (window-attached) swap-chain diagnostic -- deliberately NOT
    # registered as a CTest (mirrors cna_diag_software's own "real executable, not a ctest"
    # precedent above), since DX-100's own spike already found this crashes under vanilla Wine's
    # dxgi.dll; a permanently-registered, always-crashing CTest would just be noise. Built so a
    # developer/script can still run it by hand for a real, up-to-date reading.
    cna_directx12_test(cna_diag_d3d12_swapchain directx12_swapchain_diag.cpp)

    # GLTF-213: compile the exact mid-grey transfer harness against the D3D12 frontend. It is not
    # CTest-registered on this Wine loop for the same pre-existing DX-100 swap-chain limitation as
    # the other Game-harness tests below; D3D11 executes the shared DXBC pixel shaders end-to-end.
    cna_directx12_test(cna_test_directx12_pbr_srgb_transfer
                       ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_pbr_srgb_transfer_test.cpp)

    # GLTF-343/344: compile-verify the shared rigid/skinned Fresnel harness for D3D12. Like the
    # sRGB harness above it is not CTest-registered under Wine because of DX-100's swap-chain limit.
    cna_directx12_test(cna_test_directx12_pbr_fresnel_factors
                       ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_pbr_fresnel_factors_test.cpp)

    # REMED-GFX-127: every public Texture2D::GetData call must return the resource's real content or
    # reject the request deterministically -- never fabricate one. Built but deliberately NOT
    # registered as a CTest, for exactly the reason cna_diag_d3d12_swapchain above is not: this is a
    # Game-harness test, so it constructs a window and swap chain, which DX-100's spike already
    # found crashes under this dev loop's vanilla Wine dxgi.dll. Building it keeps D3D12's readback
    # implementation compile-verified and runnable by hand on real Windows.
    cna_directx12_test(cna_test_directx12_texture2d_getdata_contract ${CNA_GRAPHICS_EXAMPLES_DIR}/texture2d_getdata_contract_test.cpp)

    # REMED-GFX-149 (and REMED-GFX-128, the same expression): the exact XNA-compatible
    # startIndex/elementCount contract of every public Texture2D::GetData overload. startIndex is a
    # DESTINATION element offset and elementCount is the destination capacity available from it;
    # the whole-level overload applied startIndex to the SOURCE, rejected any non-zero offset on a
    # render target, rejected legal excess capacity, and silently returned a partial frame for an
    # undersized one.
    cna_directx12_test(cna_test_directx12_texture2d_getdata_transfer_range ${CNA_GRAPHICS_EXAMPLES_DIR}/texture2d_getdata_transfer_range_test.cpp)

    # REMED-GFX-150 cross-renderer control: TextureFilter::Point must select exactly ONE texel and
    # TextureAddressMode must decide which one. The defect was Software-local. Built, not
    # ctest-registered, for the DX-100 reason below -- this is a Game-harness test, so it constructs
    # a window and swap chain, which crashes under this dev loop's vanilla Wine dxgi.dll.
    # REMED-GFX-170: every public TextureFilter ordinal names a SEPARATE magnification, a separate
    # minification and a separate mipmap filter, so a renderer may not reduce the ordinal to one
    # boolean. WebGPU's SpriteBatch sampler and SDL_GPU's ONE shared sampler helper both resolved
    # `textureFilter == 0 ? LINEAR : NEAREST`, and both keyed their sampler cache on
    # `filter == 0 ? 0 : 1`, so Anisotropic, LinearMipPoint, MinPointMagLinearMipLinear and
    # MinPointMagLinearMipPoint all magnified with POINT. This fixture measures the two DIFFERENT
    # partitions of the nine ordinals that magnification and minification induce, on SpriteBatch and
    # on every textured stock family; the other renderers run it as controls.
    cna_directx12_test(cna_test_directx12_texture_filter_ordinal ${CNA_GRAPHICS_EXAMPLES_DIR}/texture_filter_ordinal_contract_test.cpp)

    # REMED-GFX-173 cross-renderer control: EnvironmentMapEffect samples TWO independent
    # resources -- the ordinary 2D texture through GraphicsDevice.SamplerStates[0] and the
    # reflection cube through SamplerStates[1]. SDL_GPU bound a literal LinearClamp for the cube
    # and captured only slot 0, so slot 1 could not reach replay at all. This fixture makes the
    # two slots independently observable (EnvironmentMapAmount 1 leaves the cube the only
    # contributor, 0 leaves the 2D texture the only one) and measures what every other renderer
    # does with the same public state.
    cna_directx12_test(cna_test_directx12_envmap_cube_sampler ${CNA_GRAPHICS_EXAMPLES_DIR}/envmap_cube_sampler_contract_test.cpp)

    # REMED-GFX-172 cross-renderer control: DualTextureEffect's two layers have INDEPENDENT public
    # sampler slots -- FNA's DualTextureEffect.fx declares DECLARE_TEXTURE(Texture, 0) and
    # DECLARE_TEXTURE(Texture2, 1). Cross-built only; the D3D12 runtime is unavailable here for the
    # pre-existing DX-100 reason.
    cna_directx12_test(cna_test_directx12_dualtexture_slot_sampler ${CNA_GRAPHICS_EXAMPLES_DIR}/dualtexture_slot_sampler_contract_test.cpp)

    # REMED-GFX-175 cross-renderer control: the MIPMAP component of a TextureFilter ordinal.
    cna_directx12_test(cna_test_directx12_texture_filter_mip_contract ${CNA_GRAPHICS_EXAMPLES_DIR}/texture_filter_mip_contract_test.cpp)

    # REMED-GFX-177: this renderer owned four fixed-capacity descriptor heaps with a monotonic bump
    # cursor and no free list, so the 65th sampleable resource EVER CREATED threw
    # `CBV/SRV/UAV descriptor heap exhausted` even when six were alive -- REMED-GFX-175's contract
    # fixture reached exactly 64 checks here and aborted, having created and destroyed one
    # RenderTarget2D per measurement. The public fixture drives high-cardinality workloads through
    # the XNA API and checks every draw against a self-identifying oracle; every other renderer runs
    # the same file as a control. Built, not ctest-registered, for the DX-100 reason recorded above:
    # it is a Game-harness test, so it constructs a window and swap chain, which crashes under this
    # dev loop's vanilla Wine dxgi.dll. Run it by hand with SDL_VIDEODRIVER=dummy, which leaves the
    # renderer genuinely swap-chain-less and exercises every descriptor path unchanged.
    cna_directx12_test(cna_test_directx12_descriptor_capacity ${CNA_GRAPHICS_EXAMPLES_DIR}/descriptor_capacity_contract_test.cpp)

    # REMED-GFX-177, the native half: the cardinality and lifetime claims a rendered pixel cannot
    # show -- recycle counts, growth generations, retired-heap retention, stable indices across a
    # heap replacement, and a bounded steady state under churn. Off-screen (window == nullptr) like
    # DirectX12_Smoke, so it IS ctest-registered.
    cna_directx12_test(cna_test_directx12_descriptor_allocator directx12_descriptor_allocator_test.cpp)
    if(CMAKE_CROSSCOMPILING)
        set(_directx12_desc_alloc_cmd ${CMAKE_SOURCE_DIR}/scripts/run-wine-vkd3d.sh
            $<TARGET_FILE:cna_test_directx12_descriptor_allocator>)
    else()
        set(_directx12_desc_alloc_cmd cna_test_directx12_descriptor_allocator)
    endif()
    cna_register_renderer_test(NAME DirectX12_DescriptorAllocator COMMAND ${_directx12_desc_alloc_cmd}
        TIMEOUT 300 LABELS "DIRECTX12")

    cna_directx12_test(cna_test_directx12_point_sampling ${CNA_GRAPHICS_EXAMPLES_DIR}/point_sampling_contract_test.cpp)


    # REMED-GFX-169 cross-renderer control: every stock 3D effect must sample through the public
    # GraphicsDevice.SamplerStates[slot]. The defect was Vulkan-local (six stock descriptor builders
    # took no sampler parameter, so all 15 of their bindings used a hardcoded default and the
    # sampler was absent from their cache keys); this run establishes that this renderer already
    # honoured the contract rather than being made to.
    # Built, not ctest-registered, for the DX-100 reason below.
    cna_directx12_test(cna_test_directx12_stock_effect_sampler
                   ${CNA_GRAPHICS_EXAMPLES_DIR}/stock_effect_sampler_contract_test.cpp)

    # REMED-GFX-130: the TextureCube/Texture3D half of the same finding. Built, not ctest-registered,
    # for exactly the reason above -- this is a Game-harness test, so it constructs a window and swap
    # chain, which DX-100's spike already found crashes under this dev loop's vanilla Wine dxgi.dll.
    cna_directx12_test(cna_test_directx12_cube_volume_getdata_contract
                   ${CNA_GRAPHICS_EXAMPLES_DIR}/texturecube_texture3d_getdata_contract_test.cpp)

    # REMED-GFX-152 cross-renderer control: a RenderTarget2D handed to a stock 3D effect as its
    # texture must be sampled, not reinterpreted. The defect was SDL_GPU-local and fatal (its
    # stock-effect paths static_cast an ITextureRenderer* to the unrelated sibling
    # SdlGpuTextureRenderer). Built, not ctest-registered, for the same reason as the two above --
    # this is a Game-harness test, so it constructs a window and swap chain, which DX-100's spike
    # found crashes under this dev loop's vanilla Wine dxgi.dll. The cross-build is the control here.
    cna_directx12_test(cna_test_directx12_rt_effect_source
                   ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_effect_source_test.cpp)

    # REMED-GFX-134: `RenderTargetCube::GetData` must return the face that was really rendered or
    # reject the request deterministically. REMED-GFX-130 made the reporting honest but left the
    # readback itself implemented on only two renderers, so a rendered cube face had no byte-exact
    # public oracle anywhere else. Drawn geometry (never Clear -- see REMED-GFX-129) paints an
    # asymmetric five-region pattern whose colours rotate per face, so a flip, a rotation, a wrong
    # array layer/subresource, a stale face or an unresolved multisample surface all fail.
    cna_directx12_test(cna_test_directx12_rendertargetcube_getdata_contract
                   ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertargetcube_getdata_contract_test.cpp)

    # REMED-GFX-136: IGraphicsRenderer::CreateRenderTargetCube had no `preserveContents` parameter,
    # unlike CreateRenderTarget2D, so a RenderTargetCube's real RenderTargetUsage never reached the
    # renderer and Vulkan/WebGPU discarded a PreserveContents cube face on every bind cycle. Reuses
    # REMED-GFX-134's asymmetric face producer, then rebinds and draws only a small marker: "the
    # marker landed" is what a discarding renderer also achieves, so it can never pass for
    # preservation.
    cna_directx12_test(cna_test_directx12_rendertargetcube_usage
                   ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertargetcube_usage_test.cpp)

    # REMED-GFX-141: a MULTISAMPLED RenderTargetCube shared ONE multisample colour attachment
    # across all six faces on EasyGL, Vulkan and SdlGpu, so a PreserveContents face reloaded whichever
    # face was rendered last instead of its own samples. Renders face A, then face B, then rebinds A for
    # a small partial update -- with no readback, Present or flush in between -- and reads the whole of
    # A: the resolved single-sample layer still held the correct A, which is why a full redraw and an
    # immediate read both passed before this.
    cna_directx12_test(cna_test_directx12_rendertargetcube_msaa_face
                   ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertargetcube_msaa_face_test.cpp)

    # REMED-GFX-142: RenderTargetUsage's DEPTH and STENCIL half. FNA3D's own header documents
    # `preserveTargetContents` as storing the "color/depth/stencil" contents, and FNA's DiscardContents
    # bind clears all three (Target|DepthBuffer|Stencil, MaxDepth, 0) -- so the enum governs depth and
    # stencil, not colour alone. Renders an occluder, unbinds, rebinds WITHOUT clearing and draws behind
    # it: three bands separate preserved depth from cleared depth, lost colour and a second pass that
    # never drew. A parallel stencil stamp/gate sequence does the same for stencil.
    cna_directx12_test(cna_test_directx12_rendertarget_depthstencil_usage
                   ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_depthstencil_usage_test.cpp)

    # REMED-GFX-140: every public render-target bind/unbind cycle must be its own logical pass.
    # `VulkanRenderer::RecordCommandBuffer` collected ONE render pass per unique render-target
    # source per flush and replayed every queued batch for it inside that pass, so two bind cycles of
    # one target within a single flush window shared one load action. The decisive checks all use
    # DiscardContents -- collapsing is invisible on a preserving target, which is why REMED-GFX-136
    # needed an artificial readback barrier to see it -- and nothing between two cycles ever forces a
    # flush.
    cna_directx12_test(cna_test_directx12_rendertarget_pass_boundary
                   ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_pass_boundary_test.cpp)

    # REMED-GFX-143: backbuffer work and render-target work must replay in ONE ordered stream.
    # REMED-GFX-140 (Vulkan) and REMED-GFX-145 (SdlGpu) gave every render-target bind cycle its own
    # native pass in public order, but both kept the BACKBUFFER as one trailing pass, so every
    # backbuffer draw of a frame was replayed after every target pass regardless of when it was
    # issued. A frame that samples a target onto the backbuffer and then renders into that target
    # again therefore saw the FINAL content. Every check queues its whole public sequence with no
    # Present, GetData, flush or extra frame between the cycles, and only then reads once.
    cna_directx12_test(cna_test_directx12_backbuffer_pass_order
                   ${CNA_GRAPHICS_EXAMPLES_DIR}/backbuffer_pass_order_test.cpp)

    # REMED-GFX-135: the WRITE half of the same finding. `TextureCube::SetData`/`Texture3D::SetData`
    # kept the pre-REMED-GFX-127 shape -- a `void` renderer method behind `if (renderer_)` -- so an
    # upload that stored nothing, or only part of the requested region, still returned normally.
    cna_directx12_test(cna_test_directx12_cube_volume_setdata_contract
                   ${CNA_GRAPHICS_EXAMPLES_DIR}/texturecube_texture3d_setdata_contract_test.cpp)
endif()

# Module-local tests get the same headless-safe skip convention the root one-shot
# gives root-registered tests (see cmake/TestHelpers.cmake).
cna_apply_skip_convention()
