if(CNA_BUILD_TESTS AND CNA_GRAPHICS_BACKEND STREQUAL "HEADLESS")
    enable_testing()

    macro(cna_headless_test target src)
        add_executable(${target} ${src})
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(${target} PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME SDL3::SDL3)
        else()
            target_link_libraries(${target} PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(${target} PRIVATE SDL3::SDL3main)
        endif()
    endmacro()

    cna_headless_test(cna_test_headless_smoke examples/headless_smoke_test.cpp)
    cna_register_backend_test(NAME Headless_Smoke COMMAND cna_test_headless_smoke
        TIMEOUT 30 LABELS "Headless")

    cna_headless_test(cna_test_headless_resource_backends examples/headless_resource_backends_test.cpp)
    cna_register_backend_test(NAME Headless_ResourceBackends COMMAND cna_test_headless_resource_backends
        TIMEOUT 30 LABELS "Headless")

    cna_headless_test(cna_test_headless_validation_extras examples/headless_validation_extras_test.cpp)
    cna_register_backend_test(NAME Headless_ValidationExtras COMMAND cna_test_headless_validation_extras
        TIMEOUT 30 LABELS "Headless")

    cna_headless_test(cna_test_headless_coverage_gaps examples/headless_coverage_gaps_test.cpp)
    cna_register_backend_test(NAME Headless_CoverageGaps COMMAND cna_test_headless_coverage_gaps
        TIMEOUT 30 LABELS "Headless")

    cna_headless_test(cna_test_headless_effects examples/headless_effects_test.cpp)
    cna_register_backend_test(NAME Headless_Effects COMMAND cna_test_headless_effects
        TIMEOUT 30 LABELS "Headless")

    cna_headless_test(cna_test_headless_mode_dial examples/headless_mode_dial_test.cpp)
    cna_register_backend_test(NAME Headless_ModeDial COMMAND cna_test_headless_mode_dial
        TIMEOUT 30 LABELS "Headless")

    cna_headless_test(cna_test_headless_trace_diff examples/headless_trace_diff_test.cpp)
    cna_register_backend_test(NAME Headless_TraceDiff COMMAND cna_test_headless_trace_diff
        TIMEOUT 30 LABELS "Headless")

    # REMED-GFX-127: every public Texture2D::GetData call must return the resource's real content or
    # reject the request deterministically -- never fabricate one. This backend executes no
    # rasterization at all, so it is the one that must REJECT render-target colour readback: pre-fix
    # it answered with the shared layer's own zero-initialized scratch buffer, i.e. a complete,
    # uniformly transparent-black "rendered" frame it had never rendered.
    # REMED-GFX-131: SurfaceFormat::Color is a plain 8-bit UNORM byte format, so a mid-tone channel
    # must survive Clear/draw/sample/readback unchanged. Registered here as a cross-backend control:
    # the defect was WebGPU-local (its render targets used the swapchain's *UnormSrgb format), and
    # these runs are what establish that byte-exact identity is CNA's existing behaviour everywhere
    # else rather than a value invented for the fix.
    cna_headless_test(cna_test_headless_colorspace_midtone
        examples/colorspace_midtone_contract_test.cpp)
    cna_register_backend_test(NAME Headless_ColorSpace_MidTone COMMAND cna_test_headless_colorspace_midtone
        TIMEOUT 120 LABELS "Headless")

    # REMED-GFX-147: a RenderTarget2D used as a texture must sample in the same logical orientation
    # as an ordinary Texture2D holding identical bytes. Registered here as a cross-backend control:
    # the defect was EasyGL-local (an OpenGL framebuffer's origin is bottom-left, so a target's
    # colour texture stores the image bottom-up and sampling did not compensate even though GetData
    # already did), and these runs are what establish that render-target and ordinary-texture
    # sampling already agree everywhere else rather than being made to agree by the fix.
    cna_headless_test(cna_test_headless_rt_sampling_orientation
        examples/rendertarget_sampling_orientation_test.cpp)
    cna_register_backend_test(NAME Headless_RenderTarget_SamplingOrientation COMMAND cna_test_headless_rt_sampling_orientation
        TIMEOUT 120 LABELS "Headless")



    # REMED-GFX-169: every stock 3D effect must sample its textures through the public
    # GraphicsDevice.SamplerStates[slot], exactly like SpriteBatch already does. Vulkan's
    # ApplySamplerState is correct and SpriteBatch reads slotSamplers_[0], but six stock 3D
    # descriptor builders take NO sampler parameter at all, so all 15 of their combined-image-
    # sampler bindings are hardcoded to defaultSampler_ (LINEAR + CLAMP_TO_EDGE) and the sampler is
    # absent from their cache keys too. Vulkan is where the defect lives; the other backends run the
    # same public fixture as controls.
    cna_headless_test(cna_test_headless_stock_effect_sampler
        examples/stock_effect_sampler_contract_test.cpp)
    cna_register_backend_test(NAME Headless_StockEffectSamplerContract COMMAND cna_test_headless_stock_effect_sampler
        TIMEOUT 300 LABELS "Headless")

    # REMED-GFX-150 cross-backend control: TextureFilter::Point must select exactly ONE texel and
    # TextureAddressMode must decide which one, on SpriteBatch and on the device SamplerStates[0] 3D
    # path alike. The defect was Software-local (its ApplySamplerState named none of its parameters
    # and one bilinear function served every textured fragment, so every draw was LinearClamp); this
    # run is what establishes that this backend already honoured the contract rather than being made
    # to.
    # REMED-GFX-170: every public TextureFilter ordinal names a SEPARATE magnification, a separate
    # minification and a separate mipmap filter, so a backend may not reduce the ordinal to one
    # boolean. WebGPU's SpriteBatch sampler and SDL_GPU's ONE shared sampler helper both resolved
    # `textureFilter == 0 ? LINEAR : NEAREST`, and both keyed their sampler cache on
    # `filter == 0 ? 0 : 1`, so Anisotropic, LinearMipPoint, MinPointMagLinearMipLinear and
    # MinPointMagLinearMipPoint all magnified with POINT. This fixture measures the two DIFFERENT
    # partitions of the nine ordinals that magnification and minification induce, on SpriteBatch and
    # on every textured stock family; the other backends run it as controls.
    cna_headless_test(cna_test_headless_texture_filter_ordinal
        examples/texture_filter_ordinal_contract_test.cpp)
    cna_register_backend_test(NAME Headless_TextureFilterOrdinalContract COMMAND cna_test_headless_texture_filter_ordinal
        TIMEOUT 300 LABELS "Headless")

    # REMED-GFX-175 cross-backend control: the MIPMAP component of a TextureFilter ordinal.
    # EasyGL mapped ordinals 0 and 1 onto a GL filter with no mipmap term and Software had no
    # mip pipeline at all; this fixture measures what every other backend does with a chain
    # whose levels name themselves, so a divergence is classified rather than assumed.
    cna_headless_test(cna_test_headless_texture_filter_mip_contract
        examples/texture_filter_mip_contract_test.cpp)
    cna_register_backend_test(NAME Headless_TextureFilterMipContract COMMAND cna_test_headless_texture_filter_mip_contract
        TIMEOUT 300 LABELS "Headless")

    # REMED-GFX-177 cross-backend control: descriptor/binding bookkeeping must be a function of what
    # is LIVE, never of what has ever existed. D3D12 owned four fixed-capacity heaps with a monotonic
    # bump cursor and no free list, so the 65th sampleable resource EVER CREATED threw even when six
    # were alive. Every draw here is checked against a self-identifying oracle that decodes back to an
    # integer, so a recycled or grown pool that aliases one resource onto another's binding names the
    # wrong resource rather than merely producing an odd colour.
    cna_headless_test(cna_test_headless_descriptor_capacity
        examples/descriptor_capacity_contract_test.cpp)
    cna_register_backend_test(NAME Headless_DescriptorCapacityContract COMMAND cna_test_headless_descriptor_capacity
        TIMEOUT 900 LABELS "Headless")

    cna_headless_test(cna_test_headless_point_sampling
        examples/point_sampling_contract_test.cpp)
    cna_register_backend_test(NAME Headless_PointSamplingContract COMMAND cna_test_headless_point_sampling
        TIMEOUT 300 LABELS "Headless")

    # REMED-GFX-151 cross-backend control: the canonical XNA render-to-texture sequence -- render
    # into a target, unbind it, sample it -- must complete in ONE public frame with no intervening
    # GetData, Present, extra frame, manual flush or wait. The defect was Vulkan-local (its deferred
    # recorder's readback flush filtered the frame's segment list down to the target being READ, so a
    # producer's pass was never recorded before the consumer that sampled it); these runs are what
    # establish that every other backend already honoured the contract rather than being made to.
    cna_headless_test(cna_test_headless_rt_producer_consumer
        examples/rendertarget_producer_consumer_test.cpp)
    cna_register_backend_test(NAME Headless_RenderTarget_ProducerConsumer COMMAND cna_test_headless_rt_producer_consumer
        TIMEOUT 120 LABELS "Headless")

    # REMED-GFX-152 cross-backend control: a RenderTarget2D handed to a stock or custom 3D effect as
    # its texture must be sampled, not reinterpreted. The defect was SDL_GPU-local and fatal (its
    # stock-effect paths static_cast an ITextureBackend* to the unrelated sibling
    # SdlGpuTextureBackend, fabricating an SDL_GPUTexture* out of a render target's own fields); this
    # run is what establishes that HEADLESS already honoured the contract rather than being made to.
    cna_headless_test(cna_test_headless_rt_effect_source
        examples/rendertarget_effect_source_test.cpp)
    cna_register_backend_test(NAME Headless_RenderTarget_EffectSource COMMAND cna_test_headless_rt_effect_source
        TIMEOUT 300 LABELS "Headless")

    # REMED-GFX-167 deferred-source lifetime: a resource sampled by a draw that has only been
    # QUEUED must stay bindable until that draw actually renders, and destroying the public wrapper
    # first must never terminate the process. The defect was WebGPU-local -- every deferred command
    # stored a raw pointer to the resource's BACKEND OBJECT and called a VIRTUAL method on it at
    # replay, so a RenderTarget2D produced, sampled onto the BACKBUFFER and dropped inside one
    # Draw() was a heap-use-after-free at Present(). These runs establish which backends already
    # honoured the contract rather than being made to; each leg runs in its own process so a
    # SIGSEGV is an attributable result instead of a lost shard.
    cna_headless_test(cna_test_headless_deferred_source_lifetime
        examples/deferred_source_lifetime_test.cpp)
    cna_register_backend_test(NAME Headless_DeferredSourceLifetime COMMAND cna_test_headless_deferred_source_lifetime
        TIMEOUT 300 LABELS "Headless")

    # REMED-GFX-168 cross-backend control: destroying a RenderTarget2D that is STILL the bound
    # render target must never make the next SetRenderTarget transition unsafe, and must leave the
    # next target and the backbuffer exactly correct. The defect was EasyGL-local -- it remembered
    # the bound destination as a raw IRenderTargetBackend* and the next transition called
    # UnbindAsRenderTarget() on it, so a scoped target leaving scope while bound made that
    # transition a virtual call through freed storage. This run is what establishes that HEADLESS
    # already honoured the contract rather than being made to; each leg runs in its own process so
    # a SIGSEGV is an attributable result instead of a lost shard.
    cna_headless_test(cna_test_headless_bound_target_lifetime
        examples/bound_target_lifetime_test.cpp)
    cna_register_backend_test(NAME Headless_BoundTargetLifetime COMMAND cna_test_headless_bound_target_lifetime
        TIMEOUT 600 LABELS "Headless")

    # REMED-GFX-165 cross-backend control: Headless does not rasterize, so the pixel oracle is a
    # declared boundary; the run still exercises the shared dimension/rectangle-validation path.
    cna_headless_test(cna_test_headless_backbuffer_readback_dimension
        examples/backbuffer_readback_dimension_test.cpp)
    cna_register_backend_test(NAME Headless_BackbufferReadbackDimension COMMAND cna_test_headless_backbuffer_readback_dimension
        TIMEOUT 300 LABELS "Headless")

    # REMED-GFX-161: cross-backend control for the first-read completion contract.
    cna_headless_test(cna_test_headless_backbuffer_first_read
        examples/backbuffer_first_read_test.cpp)
    cna_register_backend_test(NAME Headless_BackbufferFirstRead COMMAND cna_test_headless_backbuffer_first_read
        TIMEOUT 300 LABELS "Headless")

    # REMED-GFX-162: the PRIMARY subject -- Headless rasterizes nothing, so GetBackBufferData must
    # reject (System::NotSupportedException) rather than fabricate the last Clear() colour, and must
    # leave the caller's destination untouched while keeping argument-validation precedence.
    cna_headless_test(cna_test_headless_backbuffer_reject
        examples/backbuffer_headless_reject_test.cpp)
    cna_register_backend_test(NAME Headless_BackbufferReject COMMAND cna_test_headless_backbuffer_reject
        TIMEOUT 300 LABELS "Headless")

    # REMED-GFX-155 cross-backend control: a render target produced and unbound earlier in a public
    # frame must be visible to a consumer that draws on the BACKBUFFER later in that same frame. The
    # defect was bgfx-local (it radix-sorts a frame's draws by their view's sort position, which
    # defaults to the numeric view id, and its backbuffer owns the lowest id -- so a backbuffer
    # consumer executed before its own producer); these runs are what establish that every other
    # backend already honoured the contract rather than being made to.
    cna_headless_test(cna_test_headless_rt_backbuffer_consumer
        examples/rendertarget_backbuffer_consumer_test.cpp)
    cna_register_backend_test(NAME Headless_RenderTarget_BackbufferConsumer COMMAND cna_test_headless_rt_backbuffer_consumer
        TIMEOUT 180 LABELS "Headless")

    # REMED-GFX-158's control: a RenderTarget2D constructed during a public frame must be usable in
    # that same frame -- bound, cleared and/or drawn into, unbound and observed -- with no warm-up
    # frame, Present, GetData-before-first-render, dummy draw, empty bind cycle, manual frame advance
    # or wait. The defect was bgfx-local (`bgfx::reset()` discards every view's framebuffer binding,
    # including the bound target's), so this run is what establishes that HEADLESS already honoured the
    # contract rather than being made to.
    cna_headless_test(cna_test_headless_rt_first_use
        examples/rendertarget_first_use_test.cpp)
    cna_register_backend_test(NAME Headless_RenderTarget_FirstUse COMMAND cna_test_headless_rt_first_use
        TIMEOUT 180 LABELS "Headless")

    # REMED-GFX-157: a stock 3D draw issued AFTER a SpriteBatch inside ONE render-target bind cycle
    # must execute after it rather than be dropped. REMED-GFX-155's leg I0 measured the region it
    # drew into still holding the cycle's clear colour on BGFX, VULKAN, SOFTWARE and EASYGL, with
    # WEBGPU clean, using ordinary Texture2D sources on both destinations. The oracle is a staircase
    # -- step i covers stripes [0, N-i), so one readback proves every draw happened AND that they
    # happened in the issued order.
    cna_headless_test(cna_test_headless_spritebatch_3d_order
        examples/spritebatch_3d_order_test.cpp)
    cna_register_backend_test(NAME Headless_SpriteBatch3DOrder
        COMMAND cna_test_headless_spritebatch_3d_order
        TIMEOUT 300 LABELS "Headless")

    # REMED-GFX-160: the XNA/FNA front-face winding contract. This backend rasterizes nothing, so
    # REMED-GFX-127/130's contract makes every readback reject and the fixture asserts that
    # rejection rather than a pixel value.
    cna_headless_test(cna_test_headless_frontface_winding
        examples/frontface_winding_test.cpp)
    cna_register_backend_test(NAME Headless_FrontFaceWinding
        COMMAND cna_test_headless_frontface_winding
        TIMEOUT 300 LABELS "Headless")

    cna_headless_test(cna_test_headless_texture2d_getdata_contract
                      examples/texture2d_getdata_contract_test.cpp)
    cna_register_backend_test(NAME Headless_Texture2D_GetDataContract
        COMMAND cna_test_headless_texture2d_getdata_contract
        TIMEOUT 30 LABELS "Headless")

    # REMED-GFX-149 (and REMED-GFX-128, the same expression): the exact XNA-compatible
    # startIndex/elementCount contract of every public Texture2D::GetData overload. startIndex is a
    # DESTINATION element offset and elementCount is the destination capacity available from it;
    # the whole-level overload applied startIndex to the SOURCE, rejected any non-zero offset on a
    # render target, rejected legal excess capacity, and silently returned a partial frame for an
    # undersized one.
    cna_headless_test(cna_test_headless_texture2d_getdata_transfer_range
                      examples/texture2d_getdata_transfer_range_test.cpp)
    cna_register_backend_test(NAME Headless_Texture2D_GetDataTransferRange
        COMMAND cna_test_headless_texture2d_getdata_transfer_range
        TIMEOUT 30 LABELS "Headless")

    # REMED-GFX-130: the TextureCube/Texture3D half of REMED-GFX-127's finding. This backend is the
    # one that must REJECT both -- its cube SetData is a trace entry rather than a write, and
    # pre-fix its cube GetData actively `std::fill_n`'d the caller's destination with zeros.
    cna_headless_test(cna_test_headless_cube_volume_getdata_contract
                      examples/texturecube_texture3d_getdata_contract_test.cpp)
    cna_register_backend_test(NAME Headless_CubeVolume_GetDataContract
        COMMAND cna_test_headless_cube_volume_getdata_contract
        TIMEOUT 30 LABELS "Headless")

    # REMED-GFX-134: `RenderTargetCube::GetData` must return the face that was really rendered or
    # reject the request deterministically. REMED-GFX-130 made the reporting honest but left the
    # readback itself implemented on only two backends, so a rendered cube face had no byte-exact
    # public oracle anywhere else. Drawn geometry (never Clear -- see REMED-GFX-129) paints an
    # asymmetric five-region pattern whose colours rotate per face, so a flip, a rotation, a wrong
    # array layer/subresource, a stale face or an unresolved multisample surface all fail.
    cna_headless_test(cna_test_headless_rendertargetcube_getdata_contract
                      examples/rendertargetcube_getdata_contract_test.cpp)
    cna_register_backend_test(NAME Headless_RenderTargetCube_GetDataContract
        COMMAND cna_test_headless_rendertargetcube_getdata_contract
        TIMEOUT 30 LABELS "Headless")

    # REMED-GFX-136: IGraphicsBackend::CreateRenderTargetCube had no `preserveContents` parameter,
    # unlike CreateRenderTarget2D, so a RenderTargetCube's real RenderTargetUsage never reached the
    # backend and Vulkan/WebGPU discarded a PreserveContents cube face on every bind cycle. Reuses
    # REMED-GFX-134's asymmetric face producer, then rebinds and draws only a small marker: "the
    # marker landed" is what a discarding backend also achieves, so it can never pass for
    # preservation.
    cna_headless_test(cna_test_headless_rendertargetcube_usage
                      examples/rendertargetcube_usage_test.cpp)
    cna_register_backend_test(NAME Headless_RenderTargetCube_Usage
        COMMAND cna_test_headless_rendertargetcube_usage
        TIMEOUT 30 LABELS "Headless")

    # REMED-GFX-141: a MULTISAMPLED RenderTargetCube shared ONE multisample colour attachment
    # across all six faces on EasyGL, Vulkan and SdlGpu, so a PreserveContents face reloaded whichever
    # face was rendered last instead of its own samples. Renders face A, then face B, then rebinds A for
    # a small partial update -- with no readback, Present or flush in between -- and reads the whole of
    # A: the resolved single-sample layer still held the correct A, which is why a full redraw and an
    # immediate read both passed before this.
    cna_headless_test(cna_test_headless_rendertargetcube_msaa_face
                      examples/rendertargetcube_msaa_face_test.cpp)
    cna_register_backend_test(NAME Headless_RenderTargetCube_MsaaFace
        COMMAND cna_test_headless_rendertargetcube_msaa_face
        TIMEOUT 30 LABELS "Headless")

    # REMED-GFX-142: RenderTargetUsage's DEPTH and STENCIL half. FNA3D's own header documents
    # `preserveTargetContents` as storing the "color/depth/stencil" contents, and FNA's DiscardContents
    # bind clears all three (Target|DepthBuffer|Stencil, MaxDepth, 0) -- so the enum governs depth and
    # stencil, not colour alone. Renders an occluder, unbinds, rebinds WITHOUT clearing and draws behind
    # it: three bands separate preserved depth from cleared depth, lost colour and a second pass that
    # never drew. A parallel stencil stamp/gate sequence does the same for stencil.
    cna_headless_test(cna_test_headless_rendertarget_depthstencil_usage
                      examples/rendertarget_depthstencil_usage_test.cpp)
    cna_register_backend_test(NAME Headless_RenderTarget_DepthStencilUsage
        COMMAND cna_test_headless_rendertarget_depthstencil_usage
        TIMEOUT 30 LABELS "Headless")

    # REMED-GFX-140: every public render-target bind/unbind cycle must be its own logical pass.
    # `VulkanGraphicsBackend::RecordCommandBuffer` collected ONE render pass per unique render-target
    # source per flush and replayed every queued batch for it inside that pass, so two bind cycles of
    # one target within a single flush window shared one load action. The decisive checks all use
    # DiscardContents -- collapsing is invisible on a preserving target, which is why REMED-GFX-136
    # needed an artificial readback barrier to see it -- and nothing between two cycles ever forces a
    # flush.
    cna_headless_test(cna_test_headless_rendertarget_pass_boundary
                      examples/rendertarget_pass_boundary_test.cpp)
    cna_register_backend_test(NAME Headless_RenderTarget_PassBoundary
        COMMAND cna_test_headless_rendertarget_pass_boundary
        TIMEOUT 60 LABELS "Headless")

    # REMED-GFX-129: cross-backend control for Vulkan's ordered-Clear correction.
    cna_headless_test(cna_test_headless_ordered_clear
                      examples/graphicsdevice_ordered_clear_test.cpp)
    cna_register_backend_test(NAME Headless_GraphicsDevice_OrderedClear
        COMMAND cna_test_headless_ordered_clear
        TIMEOUT 120 LABELS "Headless")

    # REMED-GFX-143: backbuffer work and render-target work must replay in ONE ordered stream.
    # REMED-GFX-140 (Vulkan) and REMED-GFX-145 (SdlGpu) gave every render-target bind cycle its own
    # native pass in public order, but both kept the BACKBUFFER as one trailing pass, so every
    # backbuffer draw of a frame was replayed after every target pass regardless of when it was
    # issued. A frame that samples a target onto the backbuffer and then renders into that target
    # again therefore saw the FINAL content. Every check queues its whole public sequence with no
    # Present, GetData, flush or extra frame between the cycles, and only then reads once.
    cna_headless_test(cna_test_headless_backbuffer_pass_order
                      examples/backbuffer_pass_order_test.cpp)
    cna_register_backend_test(NAME Headless_Backbuffer_PassOrder
        COMMAND cna_test_headless_backbuffer_pass_order
        TIMEOUT 90 LABELS "Headless")

    # REMED-GFX-116 cross-backend control: every deferred draw must execute under the
    # GraphicsDevice.Viewport active at its own public call. WebGPU resolved it live when it
    # recorded the pass; this file is the same public fixture, so a backend that regresses the
    # contract is caught here rather than assumed correct.
    cna_headless_test(cna_test_headless_deferred_viewport
                    examples/deferred_viewport_capture_test.cpp)
    cna_register_backend_test(NAME Headless_Deferred_Viewport COMMAND cna_test_headless_deferred_viewport
        TIMEOUT 90 LABELS "Headless")

    # REMED-GFX-146 cross-backend control: every deferred draw must execute under the
    # GraphicsDevice.ScissorRectangle and RasterizerState.ScissorTestEnable active at its own
    # public call. WebGPU resolved both live when it recorded the pass; this file is the same
    # public fixture, so a backend that regresses the contract is caught here rather than
    # assumed correct.
    cna_headless_test(cna_test_headless_deferred_scissor
                    examples/deferred_scissor_capture_test.cpp)
    cna_register_backend_test(NAME Headless_Deferred_Scissor COMMAND cna_test_headless_deferred_scissor
        TIMEOUT 90 LABELS "Headless")

    # REMED-GFX-135: the WRITE half of the same finding. `TextureCube::SetData`/`Texture3D::SetData`
    # kept the pre-REMED-GFX-127 shape -- a `void` backend method behind `if (backend_)` -- so an
    # upload that stored nothing, or only part of the requested region, still returned normally.
    cna_headless_test(cna_test_headless_cube_volume_setdata_contract
                      examples/texturecube_texture3d_setdata_contract_test.cpp)
    cna_register_backend_test(NAME Headless_CubeVolume_SetDataContract
        COMMAND cna_test_headless_cube_volume_setdata_contract
        TIMEOUT 30 LABELS "Headless")
endif()
