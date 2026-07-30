if(CNA_BUILD_TESTS AND CNA_GRAPHICS_BACKEND STREQUAL "SOFTWARE")
    enable_testing()

    macro(cna_software_test target src)
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

    cna_software_test(cna_test_software_smoke examples/software_smoke_test.cpp)
    cna_register_backend_test(NAME Software_Smoke COMMAND cna_test_software_smoke
        TIMEOUT 30 LABELS "Software")

    cna_software_test(cna_test_software_rasterizer examples/software_rasterizer_test.cpp)
    cna_register_backend_test(NAME Software_Rasterizer COMMAND cna_test_software_rasterizer
        TIMEOUT 30 LABELS "Software")

    cna_software_test(cna_test_software_effects examples/software_effects_test.cpp)
    cna_register_backend_test(NAME Software_Effects COMMAND cna_test_software_effects
        TIMEOUT 30 LABELS "Software")

    # REMED-GFX-089 deterministic shared public GraphicsDevice depth contract. REMED-GFX-030
    # closes Software's former fixed-LessEqual/write-on-pass boundary, so DepthRead is asserted too.
    cna_software_test(cna_test_software_depth_contract examples/graphicsdevice_depth_contract_test.cpp)
    cna_register_backend_test(NAME Software_GraphicsDevice_DepthContract
        COMMAND cna_test_software_depth_contract
        TIMEOUT 30 LABELS "Software")

    # REMED-GFX-030: full Software depth-state contract -- test/write disabled combinations, all
    # eight CompareFunction values, strict/inclusive equality, A->B->A capture, complete clears,
    # colored+shaded paths, viewport/bias interaction, and isolated backbuffer/RT depth storage.
    cna_software_test(cna_test_software_depthstate examples/software_depthstate_test.cpp)
    cna_register_backend_test(NAME Software_DepthState COMMAND cna_test_software_depthstate
        TIMEOUT 30 LABELS "Software")

    cna_software_test(cna_test_software_culling examples/software_culling_test.cpp)
    cna_register_backend_test(NAME Software_Culling COMMAND cna_test_software_culling
        TIMEOUT 30 LABELS "Software")

    cna_software_test(cna_test_software_clipping examples/software_clipping_test.cpp)
    cna_register_backend_test(NAME Software_Clipping COMMAND cna_test_software_clipping
        TIMEOUT 30 LABELS "Software")

    cna_software_test(cna_test_software_dual_envmap_skinned examples/software_dual_envmap_skinned_test.cpp)
    cna_register_backend_test(NAME Software_DualEnvmapSkinned COMMAND cna_test_software_dual_envmap_skinned
        TIMEOUT 30 LABELS "Software")

    # REMED-GFX-077: BlendState.ColorWriteChannels (per-channel colour write mask) + MultiSampleMask
    # must be honored now that both are plumbed through IGraphicsBackend::ApplyBlendState.
    cna_software_test(cna_test_software_colorwritechannels examples/software_colorwritechannels_test.cpp)
    cna_register_backend_test(NAME Software_ColorWriteChannels COMMAND cna_test_software_colorwritechannels
        TIMEOUT 30 LABELS "Software")

    # REMED-GFX-073: SpriteBatch must honor a custom GraphicsDevice.Viewport (viewport-local
    # coordinates, Viewport.X/Y placement, Width/Height extent, viewport clipping) -- the Software
    # counterpart of the GFX-072 GPU-backend SpriteBatch viewport campaign.
    cna_software_test(cna_test_software_spritebatch_viewport examples/software_spritebatch_viewport_test.cpp)
    cna_register_backend_test(NAME Software_SpriteBatch_CustomViewport COMMAND cna_test_software_spritebatch_viewport
        TIMEOUT 30 LABELS "Software")

    # REMED-GFX-079: the 3D raster path (DrawColoredPrimitives / DrawIndexedColoredPrimitives /
    # DrawPrimitivesEx / DrawIndexedPrimitivesEx) must honor a custom GraphicsDevice.Viewport --
    # X/Y offset, Width/Height sub-scale, framebuffer∩Viewport raster clip, and MinDepth/MaxDepth
    # depth-range remap -- the 3D counterpart of the GFX-073 SpriteBatch viewport fix.
    cna_software_test(cna_test_software_3d_viewport examples/software_3d_viewport_test.cpp)
    cna_register_backend_test(NAME Software_3D_CustomViewport COMMAND cna_test_software_3d_viewport
        TIMEOUT 30 LABELS "Software")

    # REMED-GFX-080: GraphicsDevice.ScissorRectangle must clip both the 2D SpriteBatch path and the
    # 3D raster path when RasterizerState.ScissorTestEnable is true, in framebuffer/target space
    # (effective clip = framebuffer ∩ Viewport ∩ Scissor) -- the Software counterpart of the GPU
    # backends' scissor contract (GFX-013 Vulkan, GFX-068 SdlGpu). Reuses GFX-073/079's RasterClipRect.
    cna_software_test(cna_test_software_scissor examples/software_scissor_test.cpp)
    cna_register_backend_test(NAME Software_Scissor COMMAND cna_test_software_scissor
        TIMEOUT 30 LABELS "Software")

    # REMED-GFX-081: SpriteBatch.Begin must APPLY the RasterizerState it is passed (previously the
    # canonical 7-arg overload dropped it), matching FNA's PrepRenderState
    # (rasterizerState ?? RasterizerState.CullCounterClockwise). Software is the deterministic
    # CPU-readback oracle; the fix itself is in the shared SpriteBatch.cpp (all backends).
    cna_software_test(cna_test_software_spritebatch_rasterizerstate examples/software_spritebatch_rasterizerstate_test.cpp)
    cna_register_backend_test(NAME Software_SpriteBatch_RasterizerState COMMAND cna_test_software_spritebatch_rasterizerstate
        TIMEOUT 30 LABELS "Software")

    # REMED-GFX-082: RasterizerState.FillMode must be honored -- FillMode.WireFrame rasterizes only
    # triangle EDGES (no interior fill) across all four 3D draw entry points and the SpriteBatch quad
    # geometry (through Begin, REMED-GFX-081), with culling/viewport/scissor/depth semantics identical
    # to Solid. Pre-fix ApplyRasterizerState dropped fillMode, so WireFrame rendered as a solid fill.
    cna_software_test(cna_test_software_wireframe examples/software_wireframe_test.cpp)
    cna_register_backend_test(NAME Software_Wireframe COMMAND cna_test_software_wireframe
        TIMEOUT 30 LABELS "Software")

    # REMED-GFX-083: RasterizerState.DepthBias / SlopeScaleDepthBias must offset the per-fragment depth
    # -- ApplyRasterizerState previously dropped both floats. Constant bias (DepthBias * r) and slope
    # bias (SlopeScaleDepthBias * max|dz/dx,dz/dy|) are added to the post-viewport window depth (sign:
    # positive pushes away from the camera), across the colored / shaded / wireframe / RT paths, with
    # a byte-identical zero-bias fast path. Deterministic exact-coplanar geometry (no z-fighting noise).
    cna_software_test(cna_test_software_depthbias examples/software_depthbias_test.cpp)
    cna_register_backend_test(NAME Software_DepthBias COMMAND cna_test_software_depthbias
        TIMEOUT 30 LABELS "Software")

    # REMED-GFX-110: the indexed raster paths must honor the exact public indexed-draw contract --
    # startIndex as an index-ELEMENT offset, baseVertex added exactly once to every decoded index,
    # primitiveCount deciding the exact topology-derived consumed count, both index widths, static
    # and dynamic buffers, minVertexIndex/numVertices staying hints, and deterministic rejection of
    # invalid ranges and of decoded vertex addresses outside the bound vertex buffer. Pre-fix both
    # loops read from element zero, ignored baseVertex, and never bounds-checked the vertex fetch.
    cna_software_test(cna_test_software_indexed_addressing examples/software_indexed_addressing_test.cpp)
    cna_register_backend_test(NAME Software_IndexedAddressing COMMAND cna_test_software_indexed_addressing
        TIMEOUT 30 LABELS "Software")

    # REMED-GFX-124: a RenderTarget2D must be readable and sampleable once it is no longer active --
    # GetData returns the rendered colour attachment (full level, rectangle, destination start index)
    # and the finished target samples as an ordinary texture through both SpriteBatch and a textured
    # 3D primitive. Pre-fix SoftwareRenderTargetBackend overrode no ITextureBackend::GetData (the
    # interface default silently leaves the caller's buffer untouched) and was not a
    # SoftwareTextureBackend, so both samplers dynamic_cast it to null and drew untextured white.
    cna_software_test(cna_test_software_rendertarget_readback examples/software_rendertarget_readback_test.cpp)
    cna_register_backend_test(NAME Software_RenderTargetReadback
        COMMAND cna_test_software_rendertarget_readback
        TIMEOUT 30 LABELS "Software")

    # REMED-GFX-127: every public Texture2D::GetData call must return the resource's real content or
    # reject the request deterministically -- never fabricate one. Pre-fix the shared render-target
    # fallback zero-initialized its own scratch buffer, handed it to ITextureBackend::GetData (whose
    # interface default did nothing) and converted it for the caller regardless, so a backend with no
    # readback returned a complete, uniformly transparent-black frame that satisfied both "the
    # destination was overwritten" and any transparent-black content expectation.
    # REMED-GFX-131: SurfaceFormat::Color is a plain 8-bit UNORM byte format, so a mid-tone channel
    # must survive Clear/draw/sample/readback unchanged. Registered here as a cross-backend control:
    # the defect was WebGPU-local (its render targets used the swapchain's *UnormSrgb format), and
    # these runs are what establish that byte-exact identity is CNA's existing behaviour everywhere
    # else rather than a value invented for the fix.
    cna_software_test(cna_test_software_colorspace_midtone
        examples/colorspace_midtone_contract_test.cpp)
    cna_register_backend_test(NAME Software_ColorSpace_MidTone COMMAND cna_test_software_colorspace_midtone
        TIMEOUT 120 LABELS "Software")

    # REMED-GFX-147: a RenderTarget2D used as a texture must sample in the same logical orientation
    # as an ordinary Texture2D holding identical bytes. Registered here as a cross-backend control:
    # the defect was EasyGL-local (an OpenGL framebuffer's origin is bottom-left, so a target's
    # colour texture stores the image bottom-up and sampling did not compensate even though GetData
    # already did), and these runs are what establish that render-target and ordinary-texture
    # sampling already agree everywhere else rather than being made to agree by the fix.
    cna_software_test(cna_test_software_rt_sampling_orientation
        examples/rendertarget_sampling_orientation_test.cpp)
    cna_register_backend_test(NAME Software_RenderTarget_SamplingOrientation COMMAND cna_test_software_rt_sampling_orientation
        TIMEOUT 120 LABELS "Software")

    # REMED-GFX-151 cross-backend control: the canonical XNA render-to-texture sequence -- render
    # into a target, unbind it, sample it -- must complete in ONE public frame with no intervening
    # GetData, Present, extra frame, manual flush or wait. The defect was Vulkan-local (its deferred
    # recorder's readback flush filtered the frame's segment list down to the target being READ, so a
    # producer's pass was never recorded before the consumer that sampled it); these runs are what
    # establish that every other backend already honoured the contract rather than being made to.
    cna_software_test(cna_test_software_rt_producer_consumer
        examples/rendertarget_producer_consumer_test.cpp)
    cna_register_backend_test(NAME Software_RenderTarget_ProducerConsumer COMMAND cna_test_software_rt_producer_consumer
        TIMEOUT 120 LABELS "Software")

    # REMED-GFX-152 cross-backend control: a RenderTarget2D handed to a stock or custom 3D effect as
    # its texture must be sampled, not reinterpreted. The defect was SDL_GPU-local and fatal (its
    # stock-effect paths static_cast an ITextureBackend* to the unrelated sibling
    # SdlGpuTextureBackend, fabricating an SDL_GPUTexture* out of a render target's own fields); this
    # run is what establishes that SOFTWARE already honoured the contract rather than being made to.
    cna_software_test(cna_test_software_rt_effect_source
        examples/rendertarget_effect_source_test.cpp)
    cna_register_backend_test(NAME Software_RenderTarget_EffectSource COMMAND cna_test_software_rt_effect_source
        TIMEOUT 300 LABELS "Software")

    # REMED-GFX-167 deferred-source lifetime: a resource sampled by a draw that has only been
    # QUEUED must stay bindable until that draw actually renders, and destroying the public wrapper
    # first must never terminate the process. The defect was WebGPU-local -- every deferred command
    # stored a raw pointer to the resource's BACKEND OBJECT and called a VIRTUAL method on it at
    # replay, so a RenderTarget2D produced, sampled onto the BACKBUFFER and dropped inside one
    # Draw() was a heap-use-after-free at Present(). These runs establish which backends already
    # honoured the contract rather than being made to; each leg runs in its own process so a
    # SIGSEGV is an attributable result instead of a lost shard.
    cna_software_test(cna_test_software_deferred_source_lifetime
        examples/deferred_source_lifetime_test.cpp)
    cna_register_backend_test(NAME Software_DeferredSourceLifetime COMMAND cna_test_software_deferred_source_lifetime
        TIMEOUT 300 LABELS "Software")

    # REMED-GFX-168 cross-backend control: destroying a RenderTarget2D that is STILL the bound
    # render target must never make the next SetRenderTarget transition unsafe, and must leave the
    # next target and the backbuffer exactly correct. The defect was EasyGL-local -- it remembered
    # the bound destination as a raw IRenderTargetBackend* and the next transition called
    # UnbindAsRenderTarget() on it, so a scoped target leaving scope while bound made that
    # transition a virtual call through freed storage. This run is what establishes that SOFTWARE
    # already honoured the contract rather than being made to; each leg runs in its own process so
    # a SIGSEGV is an attributable result instead of a lost shard.
    cna_software_test(cna_test_software_bound_target_lifetime
        examples/bound_target_lifetime_test.cpp)
    cna_register_backend_test(NAME Software_BoundTargetLifetime COMMAND cna_test_software_bound_target_lifetime
        TIMEOUT 600 LABELS "Software")

    # REMED-GFX-165 cross-backend control: the authoritative-backbuffer-dimension GetBackBufferData
    # contract. Software's backbuffer is a CPU buffer that genuinely resizes, so it is also where the
    # post-resize pixel oracle is meaningful. Software already honoured the contract (its viewport
    # equals the backbuffer); this run establishes that the shared fix left it byte-unchanged.
    cna_software_test(cna_test_software_backbuffer_readback_dimension
        examples/backbuffer_readback_dimension_test.cpp)
    cna_register_backend_test(NAME Software_BackbufferReadbackDimension COMMAND cna_test_software_backbuffer_readback_dimension
        TIMEOUT 300 LABELS "Software")

    # REMED-GFX-161: cross-backend control for the first-read completion contract.
    cna_software_test(cna_test_software_backbuffer_first_read
        examples/backbuffer_first_read_test.cpp)
    cna_register_backend_test(NAME Software_BackbufferFirstRead COMMAND cna_test_software_backbuffer_first_read
        TIMEOUT 300 LABELS "Software")

    # REMED-GFX-162 cross-backend control: a rasterizing backend's GetBackBufferData SUCCEEDS and
    # fully writes the requested range, so the Headless rejection is specific, not a universal break.
    cna_software_test(cna_test_software_backbuffer_reject
        examples/backbuffer_headless_reject_test.cpp)
    cna_register_backend_test(NAME Software_BackbufferReject COMMAND cna_test_software_backbuffer_reject
        TIMEOUT 300 LABELS "Software")

    # REMED-GFX-155 cross-backend control: a render target produced and unbound earlier in a public
    # frame must be visible to a consumer that draws on the BACKBUFFER later in that same frame. The
    # defect was bgfx-local (it radix-sorts a frame's draws by their view's sort position, which
    # defaults to the numeric view id, and its backbuffer owns the lowest id -- so a backbuffer
    # consumer executed before its own producer); these runs are what establish that every other
    # backend already honoured the contract rather than being made to.
    cna_software_test(cna_test_software_rt_backbuffer_consumer
        examples/rendertarget_backbuffer_consumer_test.cpp)
    cna_register_backend_test(NAME Software_RenderTarget_BackbufferConsumer COMMAND cna_test_software_rt_backbuffer_consumer
        TIMEOUT 180 LABELS "Software")

    # REMED-GFX-158's control: a RenderTarget2D constructed during a public frame must be usable in
    # that same frame -- bound, cleared and/or drawn into, unbound and observed -- with no warm-up
    # frame, Present, GetData-before-first-render, dummy draw, empty bind cycle, manual frame advance
    # or wait. The defect was bgfx-local (`bgfx::reset()` discards every view's framebuffer binding,
    # including the bound target's), so this run is what establishes that SOFTWARE already honoured the
    # contract rather than being made to.
    cna_software_test(cna_test_software_rt_first_use
        examples/rendertarget_first_use_test.cpp)
    cna_register_backend_test(NAME Software_RenderTarget_FirstUse COMMAND cna_test_software_rt_first_use
        TIMEOUT 180 LABELS "Software")

    # REMED-GFX-157: a stock 3D draw issued AFTER a SpriteBatch inside ONE render-target bind cycle
    # must execute after it rather than be dropped. REMED-GFX-155's leg I0 measured the region it
    # drew into still holding the cycle's clear colour on BGFX, VULKAN, SOFTWARE and EASYGL, with
    # WEBGPU clean, using ordinary Texture2D sources on both destinations. The oracle is a staircase
    # -- step i covers stripes [0, N-i), so one readback proves every draw happened AND that they
    # happened in the issued order.
    cna_software_test(cna_test_software_spritebatch_3d_order
        examples/spritebatch_3d_order_test.cpp)
    cna_register_backend_test(NAME Software_SpriteBatch3DOrder
        COMMAND cna_test_software_spritebatch_3d_order
        TIMEOUT 300 LABELS "Software")

    # REMED-GFX-160: the XNA/FNA front-face winding contract. FNA's SpriteBatch lays its corners
    # out TL, TR, BL, BR and indexes them j,j+1,j+2 / j+3,j+2,j+1, so both of its triangles are
    # CLOCKWISE as displayed, and they must survive RasterizerState.CullCounterClockwise -- the
    # state SpriteBatch.Begin itself defaults to. Clockwise-as-displayed is therefore XNA's FRONT
    # face and each enum names the face it CULLS. Two triangles in disjoint quadrants, one wound
    # each way, make every cull mode's expected answer complementary in a single readback.
    cna_software_test(cna_test_software_frontface_winding
        examples/frontface_winding_test.cpp)
    cna_register_backend_test(NAME Software_FrontFaceWinding
        COMMAND cna_test_software_frontface_winding
        TIMEOUT 300 LABELS "Software")

    cna_software_test(cna_test_software_texture2d_getdata_contract
                      examples/texture2d_getdata_contract_test.cpp)
    cna_register_backend_test(NAME Software_Texture2D_GetDataContract
        COMMAND cna_test_software_texture2d_getdata_contract
        TIMEOUT 30 LABELS "Software")

    # REMED-GFX-149 (and REMED-GFX-128, the same expression): the exact XNA-compatible
    # startIndex/elementCount contract of every public Texture2D::GetData overload. startIndex is a
    # DESTINATION element offset and elementCount is the destination capacity available from it;
    # the whole-level overload applied startIndex to the SOURCE, rejected any non-zero offset on a
    # render target, rejected legal excess capacity, and silently returned a partial frame for an
    # undersized one.
    cna_software_test(cna_test_software_texture2d_getdata_transfer_range
                      examples/texture2d_getdata_transfer_range_test.cpp)
    cna_register_backend_test(NAME Software_Texture2D_GetDataTransferRange
        COMMAND cna_test_software_texture2d_getdata_transfer_range
        TIMEOUT 30 LABELS "Software")

    # REMED-GFX-130: the TextureCube/Texture3D half of REMED-GFX-127's finding. Both public cube and
    # volume GetData paths converted their own zero-initialized scratch buffer for the caller
    # regardless of whether the backend read anything back, so an unimplemented readback returned a
    # complete, uniformly transparent-black face/volume instead of rejecting the request.
    cna_software_test(cna_test_software_cube_volume_getdata_contract
                      examples/texturecube_texture3d_getdata_contract_test.cpp)
    cna_register_backend_test(NAME Software_CubeVolume_GetDataContract
        COMMAND cna_test_software_cube_volume_getdata_contract
        TIMEOUT 30 LABELS "Software")

    # REMED-GFX-134: `RenderTargetCube::GetData` must return the face that was really rendered or
    # reject the request deterministically. REMED-GFX-130 made the reporting honest but left the
    # readback itself implemented on only two backends, so a rendered cube face had no byte-exact
    # public oracle anywhere else. Drawn geometry (never Clear -- see REMED-GFX-129) paints an
    # asymmetric five-region pattern whose colours rotate per face, so a flip, a rotation, a wrong
    # array layer/subresource, a stale face or an unresolved multisample surface all fail.
    cna_software_test(cna_test_software_rendertargetcube_getdata_contract
                      examples/rendertargetcube_getdata_contract_test.cpp)
    cna_register_backend_test(NAME Software_RenderTargetCube_GetDataContract
        COMMAND cna_test_software_rendertargetcube_getdata_contract
        TIMEOUT 30 LABELS "Software")

    # REMED-GFX-136: IGraphicsBackend::CreateRenderTargetCube had no `preserveContents` parameter,
    # unlike CreateRenderTarget2D, so a RenderTargetCube's real RenderTargetUsage never reached the
    # backend and Vulkan/WebGPU discarded a PreserveContents cube face on every bind cycle. Reuses
    # REMED-GFX-134's asymmetric face producer, then rebinds and draws only a small marker: "the
    # marker landed" is what a discarding backend also achieves, so it can never pass for
    # preservation.
    cna_software_test(cna_test_software_rendertargetcube_usage
                      examples/rendertargetcube_usage_test.cpp)
    cna_register_backend_test(NAME Software_RenderTargetCube_Usage
        COMMAND cna_test_software_rendertargetcube_usage
        TIMEOUT 30 LABELS "Software")

    # REMED-GFX-141: a MULTISAMPLED RenderTargetCube shared ONE multisample colour attachment
    # across all six faces on EasyGL, Vulkan and SdlGpu, so a PreserveContents face reloaded whichever
    # face was rendered last instead of its own samples. Renders face A, then face B, then rebinds A for
    # a small partial update -- with no readback, Present or flush in between -- and reads the whole of
    # A: the resolved single-sample layer still held the correct A, which is why a full redraw and an
    # immediate read both passed before this.
    cna_software_test(cna_test_software_rendertargetcube_msaa_face
                      examples/rendertargetcube_msaa_face_test.cpp)
    cna_register_backend_test(NAME Software_RenderTargetCube_MsaaFace
        COMMAND cna_test_software_rendertargetcube_msaa_face
        TIMEOUT 30 LABELS "Software")

    # REMED-GFX-142: RenderTargetUsage's DEPTH and STENCIL half. FNA3D's own header documents
    # `preserveTargetContents` as storing the "color/depth/stencil" contents, and FNA's DiscardContents
    # bind clears all three (Target|DepthBuffer|Stencil, MaxDepth, 0) -- so the enum governs depth and
    # stencil, not colour alone. Renders an occluder, unbinds, rebinds WITHOUT clearing and draws behind
    # it: three bands separate preserved depth from cleared depth, lost colour and a second pass that
    # never drew. A parallel stencil stamp/gate sequence does the same for stencil.
    cna_software_test(cna_test_software_rendertarget_depthstencil_usage
                      examples/rendertarget_depthstencil_usage_test.cpp)
    cna_register_backend_test(NAME Software_RenderTarget_DepthStencilUsage
        COMMAND cna_test_software_rendertarget_depthstencil_usage
        TIMEOUT 30 LABELS "Software")

    # REMED-GFX-140: every public render-target bind/unbind cycle must be its own logical pass.
    # `VulkanGraphicsBackend::RecordCommandBuffer` collected ONE render pass per unique render-target
    # source per flush and replayed every queued batch for it inside that pass, so two bind cycles of
    # one target within a single flush window shared one load action. The decisive checks all use
    # DiscardContents -- collapsing is invisible on a preserving target, which is why REMED-GFX-136
    # needed an artificial readback barrier to see it -- and nothing between two cycles ever forces a
    # flush.
    cna_software_test(cna_test_software_rendertarget_pass_boundary
                      examples/rendertarget_pass_boundary_test.cpp)
    cna_register_backend_test(NAME Software_RenderTarget_PassBoundary
        COMMAND cna_test_software_rendertarget_pass_boundary
        TIMEOUT 60 LABELS "Software")

    # REMED-GFX-129: cross-backend control for Vulkan's ordered-Clear correction.
    cna_software_test(cna_test_software_ordered_clear
                      examples/graphicsdevice_ordered_clear_test.cpp)
    cna_register_backend_test(NAME Software_GraphicsDevice_OrderedClear
        COMMAND cna_test_software_ordered_clear
        TIMEOUT 120 LABELS "Software")

    # REMED-GFX-143: backbuffer work and render-target work must replay in ONE ordered stream.
    # REMED-GFX-140 (Vulkan) and REMED-GFX-145 (SdlGpu) gave every render-target bind cycle its own
    # native pass in public order, but both kept the BACKBUFFER as one trailing pass, so every
    # backbuffer draw of a frame was replayed after every target pass regardless of when it was
    # issued. A frame that samples a target onto the backbuffer and then renders into that target
    # again therefore saw the FINAL content. Every check queues its whole public sequence with no
    # Present, GetData, flush or extra frame between the cycles, and only then reads once.
    cna_software_test(cna_test_software_backbuffer_pass_order
                      examples/backbuffer_pass_order_test.cpp)
    cna_register_backend_test(NAME Software_Backbuffer_PassOrder
        COMMAND cna_test_software_backbuffer_pass_order
        TIMEOUT 90 LABELS "Software")

    # REMED-GFX-116 cross-backend control: every deferred draw must execute under the
    # GraphicsDevice.Viewport active at its own public call. WebGPU resolved it live when it
    # recorded the pass; this file is the same public fixture, so a backend that regresses the
    # contract is caught here rather than assumed correct.
    cna_software_test(cna_test_software_deferred_viewport
                    examples/deferred_viewport_capture_test.cpp)
    cna_register_backend_test(NAME Software_Deferred_Viewport COMMAND cna_test_software_deferred_viewport
        TIMEOUT 90 LABELS "Software")

    # REMED-GFX-146 cross-backend control: every deferred draw must execute under the
    # GraphicsDevice.ScissorRectangle and RasterizerState.ScissorTestEnable active at its own
    # public call. WebGPU resolved both live when it recorded the pass; this file is the same
    # public fixture, so a backend that regresses the contract is caught here rather than
    # assumed correct.
    cna_software_test(cna_test_software_deferred_scissor
                    examples/deferred_scissor_capture_test.cpp)
    cna_register_backend_test(NAME Software_Deferred_Scissor COMMAND cna_test_software_deferred_scissor
        TIMEOUT 90 LABELS "Software")

    # REMED-GFX-135: the WRITE half of the same finding. `TextureCube::SetData`/`Texture3D::SetData`
    # kept the pre-REMED-GFX-127 shape -- a `void` backend method behind `if (backend_)` -- so an
    # upload that stored nothing, or only part of the requested region, still returned normally.
    cna_software_test(cna_test_software_cube_volume_setdata_contract
                      examples/texturecube_texture3d_setdata_contract_test.cpp)
    cna_register_backend_test(NAME Software_CubeVolume_SetDataContract
        COMMAND cna_test_software_cube_volume_setdata_contract
        TIMEOUT 30 LABELS "Software")


    # REMED-GFX-169: every stock 3D effect must sample its textures through the public
    # GraphicsDevice.SamplerStates[slot], exactly like SpriteBatch already does. Vulkan's
    # ApplySamplerState is correct and SpriteBatch reads slotSamplers_[0], but six stock 3D
    # descriptor builders take NO sampler parameter at all, so all 15 of their combined-image-
    # sampler bindings are hardcoded to defaultSampler_ (LINEAR + CLAMP_TO_EDGE) and the sampler is
    # absent from their cache keys too. Vulkan is where the defect lives; the other backends run the
    # same public fixture as controls.
    cna_software_test(cna_test_software_stock_effect_sampler
        examples/stock_effect_sampler_contract_test.cpp)
    cna_register_backend_test(NAME Software_StockEffectSamplerContract COMMAND cna_test_software_stock_effect_sampler
        TIMEOUT 300 LABELS "Software")

    # REMED-GFX-150: TextureFilter::Point must select exactly ONE texel and TextureAddressMode must
    # decide which one, on SpriteBatch and on the device SamplerStates[0] 3D path alike. The defect
    # was Software-local: ApplySamplerState named none of its parameters, SoftwareSpriteBatchBackend's
    # textureFilter_/addressU_/addressV_ were dead stores, and one SampleBilinear served every
    # textured fragment -- so every draw was LinearClamp whatever the game selected. Software is the
    # deterministic CPU oracle, so the full exact-pixel matrix runs here.
    # REMED-GFX-170: every public TextureFilter ordinal names a SEPARATE magnification, a separate
    # minification and a separate mipmap filter, so a backend may not reduce the ordinal to one
    # boolean. WebGPU's SpriteBatch sampler and SDL_GPU's ONE shared sampler helper both resolved
    # `textureFilter == 0 ? LINEAR : NEAREST`, and both keyed their sampler cache on
    # `filter == 0 ? 0 : 1`, so Anisotropic, LinearMipPoint, MinPointMagLinearMipLinear and
    # MinPointMagLinearMipPoint all magnified with POINT. This fixture measures the two DIFFERENT
    # partitions of the nine ordinals that magnification and minification induce, on SpriteBatch and
    # on every textured stock family; the other backends run it as controls.
    cna_software_test(cna_test_software_texture_filter_ordinal
                      examples/texture_filter_ordinal_contract_test.cpp)
    cna_register_backend_test(NAME Software_TextureFilterOrdinalContract
        COMMAND cna_test_software_texture_filter_ordinal
        TIMEOUT 300 LABELS "Software")

    # REMED-GFX-174 control: Software is the GFX-150-corrected reference implementation, so it says
    # what the order-sensitive sampler-component contract owes independently of any GL behaviour.
    cna_software_test(cna_test_software_sampler_component_isolation
                      examples/sampler_component_isolation_contract_test.cpp)
    cna_register_backend_test(NAME Software_SamplerComponentIsolation
        COMMAND cna_test_software_sampler_component_isolation
        TIMEOUT 300 LABELS "Software")

    cna_software_test(cna_test_software_point_sampling
                      examples/point_sampling_contract_test.cpp)
    cna_register_backend_test(NAME Software_PointSamplingContract
        COMMAND cna_test_software_point_sampling
        TIMEOUT 300 LABELS "Software")

    # plan_software.md SOFTWARE-61/84: this backend's half of the cross-backend diagnostic dump --
    # not registered as a ctest (see cna_diag_compare's own comment above), just a plain
    # executable a developer/script runs by hand.
    cna_software_test(cna_diag_software examples/cross_backend_diagnostic_scene.cpp)
endif()
