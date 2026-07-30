if(CNA_BUILD_TESTS AND CNA_GRAPHICS_BACKEND STREQUAL "D3D12")
    enable_testing()

    macro(cna_d3d12_test target src)
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
    # never constructs a window/swap chain (see examples/d3d12_smoke_test.cpp's own header comment
    # for why), so this stays genuinely green on this Wine+vkd3d-proton dev loop.
    cna_d3d12_test(cna_test_d3d12_smoke examples/d3d12_smoke_test.cpp)
    if(CMAKE_CROSSCOMPILING)
        set(_d3d12_smoke_cmd ${CMAKE_SOURCE_DIR}/scripts/run-wine-vkd3d.sh $<TARGET_FILE:cna_test_d3d12_smoke>)
    else()
        set(_d3d12_smoke_cmd cna_test_d3d12_smoke)
    endif()
    cna_register_backend_test(NAME D3D12_Smoke COMMAND ${_d3d12_smoke_cmd}
        TIMEOUT 60 LABELS "D3D12")

    # plan_dx.md DX-102: real (window-attached) swap-chain diagnostic -- deliberately NOT
    # registered as a CTest (mirrors cna_diag_software's own "real executable, not a ctest"
    # precedent above), since DX-100's own spike already found this crashes under vanilla Wine's
    # dxgi.dll; a permanently-registered, always-crashing CTest would just be noise. Built so a
    # developer/script can still run it by hand for a real, up-to-date reading.
    cna_d3d12_test(cna_diag_d3d12_swapchain examples/d3d12_swapchain_diag.cpp)

    # REMED-GFX-127: every public Texture2D::GetData call must return the resource's real content or
    # reject the request deterministically -- never fabricate one. Built but deliberately NOT
    # registered as a CTest, for exactly the reason cna_diag_d3d12_swapchain above is not: this is a
    # Game-harness test, so it constructs a window and swap chain, which DX-100's spike already
    # found crashes under this dev loop's vanilla Wine dxgi.dll. Building it keeps D3D12's readback
    # implementation compile-verified and runnable by hand on real Windows.
    cna_d3d12_test(cna_test_d3d12_texture2d_getdata_contract examples/texture2d_getdata_contract_test.cpp)

    # REMED-GFX-149 (and REMED-GFX-128, the same expression): the exact XNA-compatible
    # startIndex/elementCount contract of every public Texture2D::GetData overload. startIndex is a
    # DESTINATION element offset and elementCount is the destination capacity available from it;
    # the whole-level overload applied startIndex to the SOURCE, rejected any non-zero offset on a
    # render target, rejected legal excess capacity, and silently returned a partial frame for an
    # undersized one.
    cna_d3d12_test(cna_test_d3d12_texture2d_getdata_transfer_range examples/texture2d_getdata_transfer_range_test.cpp)

    # REMED-GFX-150 cross-backend control: TextureFilter::Point must select exactly ONE texel and
    # TextureAddressMode must decide which one. The defect was Software-local. Built, not
    # ctest-registered, for the DX-100 reason below -- this is a Game-harness test, so it constructs
    # a window and swap chain, which crashes under this dev loop's vanilla Wine dxgi.dll.
    # REMED-GFX-170: every public TextureFilter ordinal names a SEPARATE magnification, a separate
    # minification and a separate mipmap filter, so a backend may not reduce the ordinal to one
    # boolean. WebGPU's SpriteBatch sampler and SDL_GPU's ONE shared sampler helper both resolved
    # `textureFilter == 0 ? LINEAR : NEAREST`, and both keyed their sampler cache on
    # `filter == 0 ? 0 : 1`, so Anisotropic, LinearMipPoint, MinPointMagLinearMipLinear and
    # MinPointMagLinearMipPoint all magnified with POINT. This fixture measures the two DIFFERENT
    # partitions of the nine ordinals that magnification and minification induce, on SpriteBatch and
    # on every textured stock family; the other backends run it as controls.
    cna_d3d12_test(cna_test_d3d12_texture_filter_ordinal examples/texture_filter_ordinal_contract_test.cpp)

    # REMED-GFX-175 cross-backend control: the MIPMAP component of a TextureFilter ordinal.
    cna_d3d12_test(cna_test_d3d12_texture_filter_mip_contract examples/texture_filter_mip_contract_test.cpp)

    # REMED-GFX-177: this backend owned four fixed-capacity descriptor heaps with a monotonic bump
    # cursor and no free list, so the 65th sampleable resource EVER CREATED threw
    # `CBV/SRV/UAV descriptor heap exhausted` even when six were alive -- REMED-GFX-175's contract
    # fixture reached exactly 64 checks here and aborted, having created and destroyed one
    # RenderTarget2D per measurement. The public fixture drives high-cardinality workloads through
    # the XNA API and checks every draw against a self-identifying oracle; every other backend runs
    # the same file as a control. Built, not ctest-registered, for the DX-100 reason recorded above:
    # it is a Game-harness test, so it constructs a window and swap chain, which crashes under this
    # dev loop's vanilla Wine dxgi.dll. Run it by hand with SDL_VIDEODRIVER=dummy, which leaves the
    # backend genuinely swap-chain-less and exercises every descriptor path unchanged.
    cna_d3d12_test(cna_test_d3d12_descriptor_capacity examples/descriptor_capacity_contract_test.cpp)

    # REMED-GFX-177, the native half: the cardinality and lifetime claims a rendered pixel cannot
    # show -- recycle counts, growth generations, retired-heap retention, stable indices across a
    # heap replacement, and a bounded steady state under churn. Off-screen (window == nullptr) like
    # D3D12_Smoke, so it IS ctest-registered.
    cna_d3d12_test(cna_test_d3d12_descriptor_allocator examples/d3d12_descriptor_allocator_test.cpp)
    if(CMAKE_CROSSCOMPILING)
        set(_d3d12_desc_alloc_cmd ${CMAKE_SOURCE_DIR}/scripts/run-wine-vkd3d.sh
            $<TARGET_FILE:cna_test_d3d12_descriptor_allocator>)
    else()
        set(_d3d12_desc_alloc_cmd cna_test_d3d12_descriptor_allocator)
    endif()
    cna_register_backend_test(NAME D3D12_DescriptorAllocator COMMAND ${_d3d12_desc_alloc_cmd}
        TIMEOUT 300 LABELS "D3D12")

    cna_d3d12_test(cna_test_d3d12_point_sampling examples/point_sampling_contract_test.cpp)


    # REMED-GFX-169 cross-backend control: every stock 3D effect must sample through the public
    # GraphicsDevice.SamplerStates[slot]. The defect was Vulkan-local (six stock descriptor builders
    # took no sampler parameter, so all 15 of their bindings used a hardcoded default and the
    # sampler was absent from their cache keys); this run establishes that this backend already
    # honoured the contract rather than being made to.
    # Built, not ctest-registered, for the DX-100 reason below.
    cna_d3d12_test(cna_test_d3d12_stock_effect_sampler
                   examples/stock_effect_sampler_contract_test.cpp)

    # REMED-GFX-130: the TextureCube/Texture3D half of the same finding. Built, not ctest-registered,
    # for exactly the reason above -- this is a Game-harness test, so it constructs a window and swap
    # chain, which DX-100's spike already found crashes under this dev loop's vanilla Wine dxgi.dll.
    cna_d3d12_test(cna_test_d3d12_cube_volume_getdata_contract
                   examples/texturecube_texture3d_getdata_contract_test.cpp)

    # REMED-GFX-152 cross-backend control: a RenderTarget2D handed to a stock 3D effect as its
    # texture must be sampled, not reinterpreted. The defect was SDL_GPU-local and fatal (its
    # stock-effect paths static_cast an ITextureBackend* to the unrelated sibling
    # SdlGpuTextureBackend). Built, not ctest-registered, for the same reason as the two above --
    # this is a Game-harness test, so it constructs a window and swap chain, which DX-100's spike
    # found crashes under this dev loop's vanilla Wine dxgi.dll. The cross-build is the control here.
    cna_d3d12_test(cna_test_d3d12_rt_effect_source
                   examples/rendertarget_effect_source_test.cpp)

    # REMED-GFX-134: `RenderTargetCube::GetData` must return the face that was really rendered or
    # reject the request deterministically. REMED-GFX-130 made the reporting honest but left the
    # readback itself implemented on only two backends, so a rendered cube face had no byte-exact
    # public oracle anywhere else. Drawn geometry (never Clear -- see REMED-GFX-129) paints an
    # asymmetric five-region pattern whose colours rotate per face, so a flip, a rotation, a wrong
    # array layer/subresource, a stale face or an unresolved multisample surface all fail.
    cna_d3d12_test(cna_test_d3d12_rendertargetcube_getdata_contract
                   examples/rendertargetcube_getdata_contract_test.cpp)

    # REMED-GFX-136: IGraphicsBackend::CreateRenderTargetCube had no `preserveContents` parameter,
    # unlike CreateRenderTarget2D, so a RenderTargetCube's real RenderTargetUsage never reached the
    # backend and Vulkan/WebGPU discarded a PreserveContents cube face on every bind cycle. Reuses
    # REMED-GFX-134's asymmetric face producer, then rebinds and draws only a small marker: "the
    # marker landed" is what a discarding backend also achieves, so it can never pass for
    # preservation.
    cna_d3d12_test(cna_test_d3d12_rendertargetcube_usage
                   examples/rendertargetcube_usage_test.cpp)

    # REMED-GFX-141: a MULTISAMPLED RenderTargetCube shared ONE multisample colour attachment
    # across all six faces on EasyGL, Vulkan and SdlGpu, so a PreserveContents face reloaded whichever
    # face was rendered last instead of its own samples. Renders face A, then face B, then rebinds A for
    # a small partial update -- with no readback, Present or flush in between -- and reads the whole of
    # A: the resolved single-sample layer still held the correct A, which is why a full redraw and an
    # immediate read both passed before this.
    cna_d3d12_test(cna_test_d3d12_rendertargetcube_msaa_face
                   examples/rendertargetcube_msaa_face_test.cpp)

    # REMED-GFX-142: RenderTargetUsage's DEPTH and STENCIL half. FNA3D's own header documents
    # `preserveTargetContents` as storing the "color/depth/stencil" contents, and FNA's DiscardContents
    # bind clears all three (Target|DepthBuffer|Stencil, MaxDepth, 0) -- so the enum governs depth and
    # stencil, not colour alone. Renders an occluder, unbinds, rebinds WITHOUT clearing and draws behind
    # it: three bands separate preserved depth from cleared depth, lost colour and a second pass that
    # never drew. A parallel stencil stamp/gate sequence does the same for stencil.
    cna_d3d12_test(cna_test_d3d12_rendertarget_depthstencil_usage
                   examples/rendertarget_depthstencil_usage_test.cpp)

    # REMED-GFX-140: every public render-target bind/unbind cycle must be its own logical pass.
    # `VulkanGraphicsBackend::RecordCommandBuffer` collected ONE render pass per unique render-target
    # source per flush and replayed every queued batch for it inside that pass, so two bind cycles of
    # one target within a single flush window shared one load action. The decisive checks all use
    # DiscardContents -- collapsing is invisible on a preserving target, which is why REMED-GFX-136
    # needed an artificial readback barrier to see it -- and nothing between two cycles ever forces a
    # flush.
    cna_d3d12_test(cna_test_d3d12_rendertarget_pass_boundary
                   examples/rendertarget_pass_boundary_test.cpp)

    # REMED-GFX-143: backbuffer work and render-target work must replay in ONE ordered stream.
    # REMED-GFX-140 (Vulkan) and REMED-GFX-145 (SdlGpu) gave every render-target bind cycle its own
    # native pass in public order, but both kept the BACKBUFFER as one trailing pass, so every
    # backbuffer draw of a frame was replayed after every target pass regardless of when it was
    # issued. A frame that samples a target onto the backbuffer and then renders into that target
    # again therefore saw the FINAL content. Every check queues its whole public sequence with no
    # Present, GetData, flush or extra frame between the cycles, and only then reads once.
    cna_d3d12_test(cna_test_d3d12_backbuffer_pass_order
                   examples/backbuffer_pass_order_test.cpp)

    # REMED-GFX-135: the WRITE half of the same finding. `TextureCube::SetData`/`Texture3D::SetData`
    # kept the pre-REMED-GFX-127 shape -- a `void` backend method behind `if (backend_)` -- so an
    # upload that stored nothing, or only part of the requested region, still returned normally.
    cna_d3d12_test(cna_test_d3d12_cube_volume_setdata_contract
                   examples/texturecube_texture3d_setdata_contract_test.cpp)
endif()
