# software renderer example/test registration (module-local; this file is the
# former cmake/Tests/SoftwareTests.cmake, moved when examples moved into their owning
# physical modules).
#
# From the root CMakeLists.txt include site:
# plan_software.md: Software (CPU rasterizer) graphics renderer tests. Deliberately no
# SDL_VIDEODRIVER/DISPLAY environment -- like HEADLESS, this renderer never needs either.

# Example/test executables historically live at the build root (scripts, docs and
# the golden-image working-directory conventions rely on it); keep them there.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
# Shared example-support headers ("common/PixelTestGame.hpp", ...) are owned by
# the graphics module's examples directory.
include_directories(${CNA_GRAPHICS_EXAMPLES_DIR})

if(CNA_BUILD_TESTS AND CNA_GRAPHICS_RENDERER STREQUAL "SOFTWARE")
    enable_testing()

    macro(cna_software_test target src)
        add_executable(${target} ${src})
        # CnaLibrary.cmake declares the real CNA <-> Software static-library cycle, so every
        # consumer gets a portable link line and these tests need no GNU-only archive group.
        target_link_libraries(${target} PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
        # plan_runtimerenderer.md P6: the CNA umbrella links the DEFAULT renderer only
        # (modules/CMakeLists.txt), so in a multi-renderer build where SOFTWARE is present but not
        # the default, these examples would not reach their own family's public-internal headers
        # (CNA/Internal/Renderers/Software/...). Naming the family's target is the same remedy
        # cmake/UnitTests.cmake applies to CnaTests for every present renderer. Not merge-related:
        # the umbrella has always linked one target; no earlier multi set contained SOFTWARE.
        if(TARGET cna_renderer_software)
            target_link_libraries(${target} PRIVATE cna_renderer_software)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(${target} PRIVATE SDL3::SDL3main)
        endif()
    endmacro()

    cna_software_test(cna_test_software_smoke software_smoke_test.cpp)
    cna_register_renderer_test(NAME Software_Smoke COMMAND cna_test_software_smoke
        TIMEOUT 30 LABELS "Software")

    cna_software_test(cna_test_software_rasterizer software_rasterizer_test.cpp)
    cna_register_renderer_test(NAME Software_Rasterizer COMMAND cna_test_software_rasterizer
        TIMEOUT 30 LABELS "Software")

    cna_software_test(cna_test_software_effects software_effects_test.cpp)
    cna_register_renderer_test(NAME Software_Effects COMMAND cna_test_software_effects
        TIMEOUT 30 LABELS "Software")

    # REMED-GFX-089 deterministic shared public GraphicsDevice depth contract. REMED-GFX-030
    # closes Software's former fixed-LessEqual/write-on-pass boundary, so DepthRead is asserted too.
    cna_software_test(cna_test_software_depth_contract ${CNA_GRAPHICS_EXAMPLES_DIR}/graphicsdevice_depth_contract_test.cpp)
    cna_register_renderer_test(NAME Software_GraphicsDevice_DepthContract
        COMMAND cna_test_software_depth_contract
        TIMEOUT 30 LABELS "Software")

    # REMED-GFX-030: full Software depth-state contract -- test/write disabled combinations, all
    # eight CompareFunction values, strict/inclusive equality, A->B->A capture, complete clears,
    # colored+shaded paths, viewport/bias interaction, and isolated backbuffer/RT depth storage.
    cna_software_test(cna_test_software_depthstate software_depthstate_test.cpp)
    cna_register_renderer_test(NAME Software_DepthState COMMAND cna_test_software_depthstate
        TIMEOUT 30 LABELS "Software")

    cna_software_test(cna_test_software_culling software_culling_test.cpp)
    cna_register_renderer_test(NAME Software_Culling COMMAND cna_test_software_culling
        TIMEOUT 30 LABELS "Software")

    cna_software_test(cna_test_software_clipping software_clipping_test.cpp)
    cna_register_renderer_test(NAME Software_Clipping COMMAND cna_test_software_clipping
        TIMEOUT 30 LABELS "Software")

    cna_software_test(cna_test_software_dual_envmap_skinned software_dual_envmap_skinned_test.cpp)
    cna_register_renderer_test(NAME Software_DualEnvmapSkinned COMMAND cna_test_software_dual_envmap_skinned
        TIMEOUT 30 LABELS "Software")

    # REMED-GFX-077: BlendState.ColorWriteChannels (per-channel colour write mask) + MultiSampleMask
    # must be honored now that both are plumbed through IGraphicsRenderer::ApplyBlendState.
    cna_software_test(cna_test_software_colorwritechannels software_colorwritechannels_test.cpp)
    cna_register_renderer_test(NAME Software_ColorWriteChannels COMMAND cna_test_software_colorwritechannels
        TIMEOUT 30 LABELS "Software")

    # REMED-GFX-073: SpriteBatch must honor a custom GraphicsDevice.Viewport (viewport-local
    # coordinates, Viewport.X/Y placement, Width/Height extent, viewport clipping) -- the Software
    # counterpart of the GFX-072 GPU-renderer SpriteBatch viewport campaign.
    cna_software_test(cna_test_software_spritebatch_viewport software_spritebatch_viewport_test.cpp)
    cna_register_renderer_test(NAME Software_SpriteBatch_CustomViewport COMMAND cna_test_software_spritebatch_viewport
        TIMEOUT 30 LABELS "Software")

    # REMED-GFX-079: the 3D raster path (DrawColoredPrimitives / DrawIndexedColoredPrimitives /
    # DrawPrimitivesEx / DrawIndexedPrimitivesEx) must honor a custom GraphicsDevice.Viewport --
    # X/Y offset, Width/Height sub-scale, framebuffer∩Viewport raster clip, and MinDepth/MaxDepth
    # depth-range remap -- the 3D counterpart of the GFX-073 SpriteBatch viewport fix.
    cna_software_test(cna_test_software_3d_viewport software_3d_viewport_test.cpp)
    cna_register_renderer_test(NAME Software_3D_CustomViewport COMMAND cna_test_software_3d_viewport
        TIMEOUT 30 LABELS "Software")

    # REMED-GFX-080: GraphicsDevice.ScissorRectangle must clip both the 2D SpriteBatch path and the
    # 3D raster path when RasterizerState.ScissorTestEnable is true, in framebuffer/target space
    # (effective clip = framebuffer ∩ Viewport ∩ Scissor) -- the Software counterpart of the GPU
    # renderers' scissor contract (GFX-013 Vulkan, GFX-068 SdlGpu). Reuses GFX-073/079's RasterClipRect.
    cna_software_test(cna_test_software_scissor software_scissor_test.cpp)
    cna_register_renderer_test(NAME Software_Scissor COMMAND cna_test_software_scissor
        TIMEOUT 30 LABELS "Software")

    # REMED-GFX-081: SpriteBatch.Begin must APPLY the RasterizerState it is passed (previously the
    # canonical 7-arg overload dropped it), matching FNA's PrepRenderState
    # (rasterizerState ?? RasterizerState.CullCounterClockwise). Software is the deterministic
    # CPU-readback oracle; the fix itself is in the shared SpriteBatch.cpp (all renderers).
    cna_software_test(cna_test_software_spritebatch_rasterizerstate software_spritebatch_rasterizerstate_test.cpp)
    cna_register_renderer_test(NAME Software_SpriteBatch_RasterizerState COMMAND cna_test_software_spritebatch_rasterizerstate
        TIMEOUT 30 LABELS "Software")

    # REMED-GFX-082: RasterizerState.FillMode must be honored -- FillMode.WireFrame rasterizes only
    # triangle EDGES (no interior fill) across all four 3D draw entry points and the SpriteBatch quad
    # geometry (through Begin, REMED-GFX-081), with culling/viewport/scissor/depth semantics identical
    # to Solid. Pre-fix ApplyRasterizerState dropped fillMode, so WireFrame rendered as a solid fill.
    cna_software_test(cna_test_software_wireframe software_wireframe_test.cpp)
    cna_register_renderer_test(NAME Software_Wireframe COMMAND cna_test_software_wireframe
        TIMEOUT 30 LABELS "Software")

    # REMED-GFX-083: RasterizerState.DepthBias / SlopeScaleDepthBias must offset the per-fragment depth
    # -- ApplyRasterizerState previously dropped both floats. Constant bias (DepthBias * r) and slope
    # bias (SlopeScaleDepthBias * max|dz/dx,dz/dy|) are added to the post-viewport window depth (sign:
    # positive pushes away from the camera), across the colored / shaded / wireframe / RT paths, with
    # a byte-identical zero-bias fast path. Deterministic exact-coplanar geometry (no z-fighting noise).
    cna_software_test(cna_test_software_depthbias software_depthbias_test.cpp)
    cna_register_renderer_test(NAME Software_DepthBias COMMAND cna_test_software_depthbias
        TIMEOUT 30 LABELS "Software")

    # REMED-GFX-110: the indexed raster paths must honor the exact public indexed-draw contract --
    # startIndex as an index-ELEMENT offset, baseVertex added exactly once to every decoded index,
    # primitiveCount deciding the exact topology-derived consumed count, both index widths, static
    # and dynamic buffers, minVertexIndex/numVertices staying hints, and deterministic rejection of
    # invalid ranges and of decoded vertex addresses outside the bound vertex buffer. Pre-fix both
    # loops read from element zero, ignored baseVertex, and never bounds-checked the vertex fetch.
    cna_software_test(cna_test_software_indexed_addressing software_indexed_addressing_test.cpp)
    cna_register_renderer_test(NAME Software_IndexedAddressing COMMAND cna_test_software_indexed_addressing
        TIMEOUT 30 LABELS "Software")

    # REMED-GFX-124: a RenderTarget2D must be readable and sampleable once it is no longer active --
    # GetData returns the rendered colour attachment (full level, rectangle, destination start index)
    # and the finished target samples as an ordinary texture through both SpriteBatch and a textured
    # 3D primitive. Pre-fix SoftwareRenderTargetRenderer overrode no ITextureRenderer::GetData (the
    # interface default silently leaves the caller's buffer untouched) and was not a
    # SoftwareTextureRenderer, so both samplers dynamic_cast it to null and drew untextured white.
    cna_software_test(cna_test_software_rendertarget_readback software_rendertarget_readback_test.cpp)
    cna_register_renderer_test(NAME Software_RenderTargetReadback
        COMMAND cna_test_software_rendertarget_readback
        TIMEOUT 30 LABELS "Software")

    # REMED-GFX-127: every public Texture2D::GetData call must return the resource's real content or
    # reject the request deterministically -- never fabricate one. Pre-fix the shared render-target
    # fallback zero-initialized its own scratch buffer, handed it to ITextureRenderer::GetData (whose
    # interface default did nothing) and converted it for the caller regardless, so a renderer with no
    # readback returned a complete, uniformly transparent-black frame that satisfied both "the
    # destination was overwritten" and any transparent-black content expectation.
    # REMED-GFX-131: SurfaceFormat::Color is a plain 8-bit UNORM byte format, so a mid-tone channel
    # must survive Clear/draw/sample/readback unchanged. Registered here as a cross-renderer control:
    # the defect was WebGPU-local (its render targets used the swapchain's *UnormSrgb format), and
    # these runs are what establish that byte-exact identity is CNA's existing behaviour everywhere
    # else rather than a value invented for the fix.
    cna_software_test(cna_test_software_colorspace_midtone
        ${CNA_GRAPHICS_EXAMPLES_DIR}/colorspace_midtone_contract_test.cpp)
    cna_register_renderer_test(NAME Software_ColorSpace_MidTone COMMAND cna_test_software_colorspace_midtone
        TIMEOUT 120 LABELS "Software")

    # REMED-GFX-148: Additive is SourceAlpha/One for both colour and alpha. Pixel-exact matrix over
    # backbuffer/RT, all effect-aware draw paths, preset/state transitions, repeated draws/frames,
    # deferred state capture, destination loading, clamping, disposal, and teardown.
    cna_software_test(cna_test_software_additive_blend_contract
        ${CNA_GRAPHICS_EXAMPLES_DIR}/additive_blend_contract_test.cpp)
    cna_register_renderer_test(NAME Software_AdditiveBlendContract
        COMMAND cna_test_software_additive_blend_contract
        TIMEOUT 120 LABELS "Software")

    # REMED-GFX-147: a RenderTarget2D used as a texture must sample in the same logical orientation
    # as an ordinary Texture2D holding identical bytes. Registered here as a cross-renderer control:
    # the defect was EasyGL-local (an OpenGL framebuffer's origin is bottom-left, so a target's
    # colour texture stores the image bottom-up and sampling did not compensate even though GetData
    # already did), and these runs are what establish that render-target and ordinary-texture
    # sampling already agree everywhere else rather than being made to agree by the fix.
    cna_software_test(cna_test_software_rt_sampling_orientation
        ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_sampling_orientation_test.cpp)
    cna_register_renderer_test(NAME Software_RenderTarget_SamplingOrientation COMMAND cna_test_software_rt_sampling_orientation
        TIMEOUT 120 LABELS "Software")

    # REMED-GFX-153 deterministic CPU control for source selection and destination orientation.
    cna_software_test(cna_test_software_gfx153_source_rectangle
        ${CNA_GRAPHICS_EXAMPLES_DIR}/source_rectangle_orientation_test.cpp)
    cna_register_renderer_test(NAME Software_GFX153_SourceRectangleOrientation
        COMMAND cna_test_software_gfx153_source_rectangle
        TIMEOUT 180 LABELS "Software")

    # REMED-GFX-151 cross-renderer control: the canonical XNA render-to-texture sequence -- render
    # into a target, unbind it, sample it -- must complete in ONE public frame with no intervening
    # GetData, Present, extra frame, manual flush or wait. The defect was Vulkan-local (its deferred
    # recorder's readback flush filtered the frame's segment list down to the target being READ, so a
    # producer's pass was never recorded before the consumer that sampled it); these runs are what
    # establish that every other renderer already honoured the contract rather than being made to.
    cna_software_test(cna_test_software_rt_producer_consumer
        ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_producer_consumer_test.cpp)
    cna_register_renderer_test(NAME Software_RenderTarget_ProducerConsumer COMMAND cna_test_software_rt_producer_consumer
        TIMEOUT 120 LABELS "Software")

    # REMED-GFX-152 cross-renderer control: a RenderTarget2D handed to a stock or custom 3D effect as
    # its texture must be sampled, not reinterpreted. The defect was SDL_GPU-local and fatal (its
    # stock-effect paths static_cast an ITextureRenderer* to the unrelated sibling
    # SdlGpuTextureRenderer, fabricating an SDL_GPUTexture* out of a render target's own fields); this
    # run is what establishes that SOFTWARE already honoured the contract rather than being made to.
    cna_software_test(cna_test_software_rt_effect_source
        ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_effect_source_test.cpp)
    cna_register_renderer_test(NAME Software_RenderTarget_EffectSource COMMAND cna_test_software_rt_effect_source
        TIMEOUT 300 LABELS "Software")

    # REMED-GFX-167 deferred-source lifetime: a resource sampled by a draw that has only been
    # QUEUED must stay bindable until that draw actually renders, and destroying the public wrapper
    # first must never terminate the process. The defect was WebGPU-local -- every deferred command
    # stored a raw pointer to the resource's RENDERER OBJECT and called a VIRTUAL method on it at
    # replay, so a RenderTarget2D produced, sampled onto the BACKBUFFER and dropped inside one
    # Draw() was a heap-use-after-free at Present(). These runs establish which renderers already
    # honoured the contract rather than being made to; each leg runs in its own process so a
    # SIGSEGV is an attributable result instead of a lost shard.
    cna_software_test(cna_test_software_deferred_source_lifetime
        ${CNA_GRAPHICS_EXAMPLES_DIR}/deferred_source_lifetime_test.cpp)
    cna_register_renderer_test(NAME Software_DeferredSourceLifetime COMMAND cna_test_software_deferred_source_lifetime
        TIMEOUT 300 LABELS "Software")

    # REMED-GFX-168 cross-renderer control: destroying a RenderTarget2D that is STILL the bound
    # render target must never make the next SetRenderTarget transition unsafe, and must leave the
    # next target and the backbuffer exactly correct. The defect was EasyGL-local -- it remembered
    # the bound destination as a raw IRenderTargetRenderer* and the next transition called
    # UnbindAsRenderTarget() on it, so a scoped target leaving scope while bound made that
    # transition a virtual call through freed storage. This run is what establishes that SOFTWARE
    # already honoured the contract rather than being made to; each leg runs in its own process so
    # a SIGSEGV is an attributable result instead of a lost shard.
    cna_software_test(cna_test_software_bound_target_lifetime
        ${CNA_GRAPHICS_EXAMPLES_DIR}/bound_target_lifetime_test.cpp)
    cna_register_renderer_test(NAME Software_BoundTargetLifetime COMMAND cna_test_software_bound_target_lifetime
        TIMEOUT 600 LABELS "Software")

    # REMED-GFX-180: the public render-target -> Present lifecycle contract. `SdlGpu_RenderState`
    # aborted with `Cannot present while render targets are bound`, which is the SECOND event in a
    # two-stage chain: an over-range DrawPrimitives (two triangles from a three-vertex buffer) was
    # correctly rejected by REMED-GFX-113's range guard, that rejection unwound past the fixture's own
    # SetRenderTarget(nullptr), and EndDraw then met a still-bound target. FNA's contract is REJECT,
    # never auto-unbind, so both guards are right and the fixture was wrong. Every leg runs in its own
    # process; leg C2 reproduces the abort end to end and its correct outcome is SIGABRT.
    cna_software_test(cna_test_software_present_lifecycle
        ${CNA_GRAPHICS_EXAMPLES_DIR}/present_lifecycle_contract_test.cpp)
    cna_register_renderer_test(NAME Software_PresentLifecycle COMMAND cna_test_software_present_lifecycle
        TIMEOUT 900 LABELS "Software")

    # REMED-GFX-163 cross-renderer control: the same public depth-backed MULTISAMPLED
    # RenderTarget2D construction and lifecycle that aborted the process on Bgfx. Only Bgfx
    # production changed; this renderer is registered to show the construction was always legal
    # and to keep it that way.
    cna_software_test(cna_test_software_msaa_depth_contract
        ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_msaa_depth_contract_test.cpp)
    cna_register_renderer_test(NAME Software_MsaaDepthContract COMMAND cna_test_software_msaa_depth_contract
        TIMEOUT 900 LABELS "Software")

    # REMED-GFX-154 cross-renderer control: the same PUBLIC first-readback contract on this
    # renderer. The defect was Bgfx-only and only Bgfx production changed; running the identical
    # fixture here is what makes that claim falsifiable rather than asserted.
    cna_software_test(cna_test_software_msaa_first_readback
        ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_msaa_first_readback_test.cpp)
    cna_register_renderer_test(NAME Software_MsaaFirstReadback COMMAND cna_test_software_msaa_first_readback
        TIMEOUT 900 LABELS "Software")

    # REMED-GFX-186 cross-renderer control: the same PUBLIC generated-mip readback contract on this
    # renderer. The SIGSEGV was SDL_GPU-only and only SDL_GPU production changed; running the
    # identical fixture here is what makes that claim falsifiable rather than asserted.
    cna_software_test(cna_test_software_msaa_mip_readback
        ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_msaa_mip_readback_test.cpp)
    cna_register_renderer_test(NAME Software_MsaaMipReadback COMMAND cna_test_software_msaa_mip_readback
        TIMEOUT 1200 LABELS "Software")
    # REMED-GFX-189 cross-renderer control: the same PUBLIC invalid-mip-level contract on this
    # renderer. The fabrication was Vulkan-only and only Vulkan production changed; running the
    # identical fixture here is what makes that claim falsifiable rather than asserted.
    cna_software_test(cna_test_software_invalid_mip_level
        ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_invalid_mip_level_test.cpp)
    cna_register_renderer_test(NAME Software_InvalidMipLevel COMMAND cna_test_software_invalid_mip_level
        TIMEOUT 1200 LABELS "Software")
    # REMED-GFX-165 cross-renderer control: the authoritative-backbuffer-dimension GetBackBufferData
    # contract. Software's backbuffer is a CPU buffer that genuinely resizes, so it is also where the
    # post-resize pixel oracle is meaningful. Software already honoured the contract (its viewport
    # equals the backbuffer); this run establishes that the shared fix left it byte-unchanged.
    cna_software_test(cna_test_software_backbuffer_readback_dimension
        ${CNA_GRAPHICS_EXAMPLES_DIR}/backbuffer_readback_dimension_test.cpp)
    cna_register_renderer_test(NAME Software_BackbufferReadbackDimension COMMAND cna_test_software_backbuffer_readback_dimension
        TIMEOUT 300 LABELS "Software")

    # REMED-GFX-161: cross-renderer control for the first-read completion contract.
    cna_software_test(cna_test_software_backbuffer_first_read
        ${CNA_GRAPHICS_EXAMPLES_DIR}/backbuffer_first_read_test.cpp)
    cna_register_renderer_test(NAME Software_BackbufferFirstRead COMMAND cna_test_software_backbuffer_first_read
        TIMEOUT 300 LABELS "Software")

    # REMED-GFX-162 cross-renderer control: a rasterizing renderer's GetBackBufferData SUCCEEDS and
    # fully writes the requested range, so the Headless rejection is specific, not a universal break.
    cna_software_test(cna_test_software_backbuffer_reject
        ${CNA_GRAPHICS_EXAMPLES_DIR}/backbuffer_headless_reject_test.cpp)
    cna_register_renderer_test(NAME Software_BackbufferReject COMMAND cna_test_software_backbuffer_reject
        TIMEOUT 300 LABELS "Software")

    # REMED-GFX-155 cross-renderer control: a render target produced and unbound earlier in a public
    # frame must be visible to a consumer that draws on the BACKBUFFER later in that same frame. The
    # defect was bgfx-local (it radix-sorts a frame's draws by their view's sort position, which
    # defaults to the numeric view id, and its backbuffer owns the lowest id -- so a backbuffer
    # consumer executed before its own producer); these runs are what establish that every other
    # renderer already honoured the contract rather than being made to.
    cna_software_test(cna_test_software_rt_backbuffer_consumer
        ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_backbuffer_consumer_test.cpp)
    cna_register_renderer_test(NAME Software_RenderTarget_BackbufferConsumer COMMAND cna_test_software_rt_backbuffer_consumer
        TIMEOUT 180 LABELS "Software")

    # REMED-GFX-158's control: a RenderTarget2D constructed during a public frame must be usable in
    # that same frame -- bound, cleared and/or drawn into, unbound and observed -- with no warm-up
    # frame, Present, GetData-before-first-render, dummy draw, empty bind cycle, manual frame advance
    # or wait. The defect was bgfx-local (`bgfx::reset()` discards every view's framebuffer binding,
    # including the bound target's), so this run is what establishes that SOFTWARE already honoured the
    # contract rather than being made to.
    cna_software_test(cna_test_software_rt_first_use
        ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_first_use_test.cpp)
    cna_register_renderer_test(NAME Software_RenderTarget_FirstUse COMMAND cna_test_software_rt_first_use
        TIMEOUT 180 LABELS "Software")

    # REMED-GFX-157: a stock 3D draw issued AFTER a SpriteBatch inside ONE render-target bind cycle
    # must execute after it rather than be dropped. REMED-GFX-155's leg I0 measured the region it
    # drew into still holding the cycle's clear colour on BGFX, VULKAN, SOFTWARE and EASYGL, with
    # WEBGPU clean, using ordinary Texture2D sources on both destinations. The oracle is a staircase
    # -- step i covers stripes [0, N-i), so one readback proves every draw happened AND that they
    # happened in the issued order.
    cna_software_test(cna_test_software_spritebatch_3d_order
        ${CNA_GRAPHICS_EXAMPLES_DIR}/spritebatch_3d_order_test.cpp)
    cna_register_renderer_test(NAME Software_SpriteBatch3DOrder
        COMMAND cna_test_software_spritebatch_3d_order
        TIMEOUT 300 LABELS "Software")

    # REMED-GFX-160: the XNA/FNA front-face winding contract. FNA's SpriteBatch lays its corners
    # out TL, TR, BL, BR and indexes them j,j+1,j+2 / j+3,j+2,j+1, so both of its triangles are
    # CLOCKWISE as displayed, and they must survive RasterizerState.CullCounterClockwise -- the
    # state SpriteBatch.Begin itself defaults to. Clockwise-as-displayed is therefore XNA's FRONT
    # face and each enum names the face it CULLS. Two triangles in disjoint quadrants, one wound
    # each way, make every cull mode's expected answer complementary in a single readback.
    cna_software_test(cna_test_software_frontface_winding
        ${CNA_GRAPHICS_EXAMPLES_DIR}/frontface_winding_test.cpp)
    cna_register_renderer_test(NAME Software_FrontFaceWinding
        COMMAND cna_test_software_frontface_winding
        TIMEOUT 300 LABELS "Software")

    # REMED-GFX-183 control: Software v1 explicitly declares its TriangleList-only 3D boundary.
    cna_software_test(cna_test_software_triangle_strip_winding
        ${CNA_GRAPHICS_EXAMPLES_DIR}/triangle_strip_winding_test.cpp)
    cna_register_renderer_test(NAME Software_TriangleStripWinding
        COMMAND cna_test_software_triangle_strip_winding
        TIMEOUT 300 LABELS "Software")

    cna_software_test(cna_test_software_texture2d_getdata_contract
                      ${CNA_GRAPHICS_EXAMPLES_DIR}/texture2d_getdata_contract_test.cpp)
    cna_register_renderer_test(NAME Software_Texture2D_GetDataContract
        COMMAND cna_test_software_texture2d_getdata_contract
        TIMEOUT 30 LABELS "Software")

    # REMED-GFX-149 (and REMED-GFX-128, the same expression): the exact XNA-compatible
    # startIndex/elementCount contract of every public Texture2D::GetData overload. startIndex is a
    # DESTINATION element offset and elementCount is the destination capacity available from it;
    # the whole-level overload applied startIndex to the SOURCE, rejected any non-zero offset on a
    # render target, rejected legal excess capacity, and silently returned a partial frame for an
    # undersized one.
    cna_software_test(cna_test_software_texture2d_getdata_transfer_range
                      ${CNA_GRAPHICS_EXAMPLES_DIR}/texture2d_getdata_transfer_range_test.cpp)
    cna_register_renderer_test(NAME Software_Texture2D_GetDataTransferRange
        COMMAND cna_test_software_texture2d_getdata_transfer_range
        TIMEOUT 30 LABELS "Software")

    # REMED-GFX-130: the TextureCube/Texture3D half of REMED-GFX-127's finding. Both public cube and
    # volume GetData paths converted their own zero-initialized scratch buffer for the caller
    # regardless of whether the renderer read anything back, so an unimplemented readback returned a
    # complete, uniformly transparent-black face/volume instead of rejecting the request.
    cna_software_test(cna_test_software_cube_volume_getdata_contract
                      ${CNA_GRAPHICS_EXAMPLES_DIR}/texturecube_texture3d_getdata_contract_test.cpp)
    cna_register_renderer_test(NAME Software_CubeVolume_GetDataContract
        COMMAND cna_test_software_cube_volume_getdata_contract
        TIMEOUT 30 LABELS "Software")

    # REMED-GFX-134: `RenderTargetCube::GetData` must return the face that was really rendered or
    # reject the request deterministically. REMED-GFX-130 made the reporting honest but left the
    # readback itself implemented on only two renderers, so a rendered cube face had no byte-exact
    # public oracle anywhere else. Drawn geometry (never Clear -- see REMED-GFX-129) paints an
    # asymmetric five-region pattern whose colours rotate per face, so a flip, a rotation, a wrong
    # array layer/subresource, a stale face or an unresolved multisample surface all fail.
    cna_software_test(cna_test_software_rendertargetcube_getdata_contract
                      ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertargetcube_getdata_contract_test.cpp)
    cna_register_renderer_test(NAME Software_RenderTargetCube_GetDataContract
        COMMAND cna_test_software_rendertargetcube_getdata_contract
        TIMEOUT 30 LABELS "Software")

    # REMED-GFX-136: IGraphicsRenderer::CreateRenderTargetCube had no `preserveContents` parameter,
    # unlike CreateRenderTarget2D, so a RenderTargetCube's real RenderTargetUsage never reached the
    # renderer and Vulkan/WebGPU discarded a PreserveContents cube face on every bind cycle. Reuses
    # REMED-GFX-134's asymmetric face producer, then rebinds and draws only a small marker: "the
    # marker landed" is what a discarding renderer also achieves, so it can never pass for
    # preservation.
    cna_software_test(cna_test_software_rendertargetcube_usage
                      ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertargetcube_usage_test.cpp)
    cna_register_renderer_test(NAME Software_RenderTargetCube_Usage
        COMMAND cna_test_software_rendertargetcube_usage
        TIMEOUT 30 LABELS "Software")

    # REMED-GFX-141: a MULTISAMPLED RenderTargetCube shared ONE multisample colour attachment
    # across all six faces on EasyGL, Vulkan and SdlGpu, so a PreserveContents face reloaded whichever
    # face was rendered last instead of its own samples. Renders face A, then face B, then rebinds A for
    # a small partial update -- with no readback, Present or flush in between -- and reads the whole of
    # A: the resolved single-sample layer still held the correct A, which is why a full redraw and an
    # immediate read both passed before this.
    cna_software_test(cna_test_software_rendertargetcube_msaa_face
                      ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertargetcube_msaa_face_test.cpp)
    cna_register_renderer_test(NAME Software_RenderTargetCube_MsaaFace
        COMMAND cna_test_software_rendertargetcube_msaa_face
        TIMEOUT 30 LABELS "Software")

    # REMED-GFX-142: RenderTargetUsage's DEPTH and STENCIL half. FNA3D's own header documents
    # `preserveTargetContents` as storing the "color/depth/stencil" contents, and FNA's DiscardContents
    # bind clears all three (Target|DepthBuffer|Stencil, MaxDepth, 0) -- so the enum governs depth and
    # stencil, not colour alone. Renders an occluder, unbinds, rebinds WITHOUT clearing and draws behind
    # it: three bands separate preserved depth from cleared depth, lost colour and a second pass that
    # never drew. A parallel stencil stamp/gate sequence does the same for stencil.
    cna_software_test(cna_test_software_rendertarget_depthstencil_usage
                      ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_depthstencil_usage_test.cpp)
    cna_register_renderer_test(NAME Software_RenderTarget_DepthStencilUsage
        COMMAND cna_test_software_rendertarget_depthstencil_usage
        TIMEOUT 30 LABELS "Software")

    # REMED-GFX-140: every public render-target bind/unbind cycle must be its own logical pass.
    # `VulkanRenderer::RecordCommandBuffer` collected ONE render pass per unique render-target
    # source per flush and replayed every queued batch for it inside that pass, so two bind cycles of
    # one target within a single flush window shared one load action. The decisive checks all use
    # DiscardContents -- collapsing is invisible on a preserving target, which is why REMED-GFX-136
    # needed an artificial readback barrier to see it -- and nothing between two cycles ever forces a
    # flush.
    cna_software_test(cna_test_software_rendertarget_pass_boundary
                      ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_pass_boundary_test.cpp)
    cna_register_renderer_test(NAME Software_RenderTarget_PassBoundary
        COMMAND cna_test_software_rendertarget_pass_boundary
        TIMEOUT 60 LABELS "Software")

    # REMED-GFX-129: cross-renderer control for Vulkan's ordered-Clear correction.
    cna_software_test(cna_test_software_ordered_clear
                      ${CNA_GRAPHICS_EXAMPLES_DIR}/graphicsdevice_ordered_clear_test.cpp)
    cna_register_renderer_test(NAME Software_GraphicsDevice_OrderedClear
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
                      ${CNA_GRAPHICS_EXAMPLES_DIR}/backbuffer_pass_order_test.cpp)
    cna_register_renderer_test(NAME Software_Backbuffer_PassOrder
        COMMAND cna_test_software_backbuffer_pass_order
        TIMEOUT 90 LABELS "Software")

    # REMED-GFX-116 cross-renderer control: every deferred draw must execute under the
    # GraphicsDevice.Viewport active at its own public call. WebGPU resolved it live when it
    # recorded the pass; this file is the same public fixture, so a renderer that regresses the
    # contract is caught here rather than assumed correct.
    cna_software_test(cna_test_software_deferred_viewport
                    ${CNA_GRAPHICS_EXAMPLES_DIR}/deferred_viewport_capture_test.cpp)
    cna_register_renderer_test(NAME Software_Deferred_Viewport COMMAND cna_test_software_deferred_viewport
        TIMEOUT 90 LABELS "Software")

    # REMED-GFX-146 cross-renderer control: every deferred draw must execute under the
    # GraphicsDevice.ScissorRectangle and RasterizerState.ScissorTestEnable active at its own
    # public call. WebGPU resolved both live when it recorded the pass; this file is the same
    # public fixture, so a renderer that regresses the contract is caught here rather than
    # assumed correct.
    cna_software_test(cna_test_software_deferred_scissor
                    ${CNA_GRAPHICS_EXAMPLES_DIR}/deferred_scissor_capture_test.cpp)
    cna_register_renderer_test(NAME Software_Deferred_Scissor COMMAND cna_test_software_deferred_scissor
        TIMEOUT 90 LABELS "Software")

    # REMED-GFX-135: the WRITE half of the same finding. `TextureCube::SetData`/`Texture3D::SetData`
    # kept the pre-REMED-GFX-127 shape -- a `void` renderer method behind `if (renderer_)` -- so an
    # upload that stored nothing, or only part of the requested region, still returned normally.
    cna_software_test(cna_test_software_cube_volume_setdata_contract
                      ${CNA_GRAPHICS_EXAMPLES_DIR}/texturecube_texture3d_setdata_contract_test.cpp)
    cna_register_renderer_test(NAME Software_CubeVolume_SetDataContract
        COMMAND cna_test_software_cube_volume_setdata_contract
        TIMEOUT 30 LABELS "Software")


    # REMED-GFX-169: every stock 3D effect must sample its textures through the public
    # GraphicsDevice.SamplerStates[slot], exactly like SpriteBatch already does. Vulkan's
    # ApplySamplerState is correct and SpriteBatch reads slotSamplers_[0], but six stock 3D
    # descriptor builders take NO sampler parameter at all, so all 15 of their combined-image-
    # sampler bindings are hardcoded to defaultSampler_ (LINEAR + CLAMP_TO_EDGE) and the sampler is
    # absent from their cache keys too. Vulkan is where the defect lives; the other renderers run the
    # same public fixture as controls.
    cna_software_test(cna_test_software_stock_effect_sampler
        ${CNA_GRAPHICS_EXAMPLES_DIR}/stock_effect_sampler_contract_test.cpp)
    cna_register_renderer_test(NAME Software_StockEffectSamplerContract COMMAND cna_test_software_stock_effect_sampler
        TIMEOUT 300 LABELS "Software")

    # REMED-GFX-150: TextureFilter::Point must select exactly ONE texel and TextureAddressMode must
    # decide which one, on SpriteBatch and on the device SamplerStates[0] 3D path alike. The defect
    # was Software-local: ApplySamplerState named none of its parameters, SoftwareSpriteBatchRenderer's
    # textureFilter_/addressU_/addressV_ were dead stores, and one SampleBilinear served every
    # textured fragment -- so every draw was LinearClamp whatever the game selected. Software is the
    # deterministic CPU oracle, so the full exact-pixel matrix runs here.
    # REMED-GFX-170: every public TextureFilter ordinal names a SEPARATE magnification, a separate
    # minification and a separate mipmap filter, so a renderer may not reduce the ordinal to one
    # boolean. WebGPU's SpriteBatch sampler and SDL_GPU's ONE shared sampler helper both resolved
    # `textureFilter == 0 ? LINEAR : NEAREST`, and both keyed their sampler cache on
    # `filter == 0 ? 0 : 1`, so Anisotropic, LinearMipPoint, MinPointMagLinearMipLinear and
    # MinPointMagLinearMipPoint all magnified with POINT. This fixture measures the two DIFFERENT
    # partitions of the nine ordinals that magnification and minification induce, on SpriteBatch and
    # on every textured stock family; the other renderers run it as controls.
    cna_software_test(cna_test_software_texture_filter_ordinal
                      ${CNA_GRAPHICS_EXAMPLES_DIR}/texture_filter_ordinal_contract_test.cpp)
    cna_register_renderer_test(NAME Software_TextureFilterOrdinalContract
        COMMAND cna_test_software_texture_filter_ordinal
        TIMEOUT 300 LABELS "Software")

    # REMED-GFX-173 cross-renderer control: EnvironmentMapEffect samples TWO independent
    # resources -- the ordinary 2D texture through GraphicsDevice.SamplerStates[0] and the
    # reflection cube through SamplerStates[1]. SDL_GPU bound a literal LinearClamp for the cube
    # and captured only slot 0, so slot 1 could not reach replay at all. This fixture makes the
    # two slots independently observable (EnvironmentMapAmount 1 leaves the cube the only
    # contributor, 0 leaves the 2D texture the only one) and measures what every other renderer
    # does with the same public state.
    cna_software_test(cna_test_software_envmap_cube_sampler
                      ${CNA_GRAPHICS_EXAMPLES_DIR}/envmap_cube_sampler_contract_test.cpp)
    cna_register_renderer_test(NAME Software_EnvMapCubeSamplerContract
        COMMAND cna_test_software_envmap_cube_sampler
        TIMEOUT 900 LABELS "Software")

    # REMED-GFX-172 cross-renderer control: DualTextureEffect's two layers have INDEPENDENT public
    # sampler slots -- FNA's DualTextureEffect.fx declares DECLARE_TEXTURE(Texture, 0) and
    # DECLARE_TEXTURE(Texture2, 1). WebGPU declared ONE WGSL sampler for both texture views, so
    # SamplerStates[1] was inexpressible and slot 1 inherited slot 0's. This fixture observes both
    # layers in the SAME image -- slot 0's texture varies along X only, slot 1's along Y only, and
    # the shader's product is separable -- so independence is measured, not inferred.
    cna_software_test(cna_test_software_dualtexture_slot_sampler
                      ${CNA_GRAPHICS_EXAMPLES_DIR}/dualtexture_slot_sampler_contract_test.cpp)
    cna_register_renderer_test(NAME Software_DualTextureSlotSamplerContract
        COMMAND cna_test_software_dualtexture_slot_sampler
        TIMEOUT 900 LABELS "Software")

    # REMED-GFX-182: `SoftwareRenderer::SampleCubeMap` took no sampler state at all, so the
    # reflection cube was always point-sampled at level 0 however SamplerStates[1] was set -- the
    # shape REMED-GFX-150 replaced on the ordinary 2D path and did not reach on the cube path. The
    # fixture above measures "is slot 1 consulted, per ordinal, per draw path, per face, per mip
    # level"; this one measures what a lost sampler state can ALSO fail -- returning to a previous
    # sampler, interleaving with an unrelated effect, sweeping either slot with the other fixed, the
    # magnification partition of the public enum, create/sample/destroy lifetime, a degenerate
    # reflection direction, and a declared-but-unwritten mip chain.
    cna_software_test(cna_test_software_envmap_cube_sampler_state
                      ${CNA_GRAPHICS_EXAMPLES_DIR}/envmap_cube_sampler_state_test.cpp)
    cna_register_renderer_test(NAME Software_EnvMapCubeSamplerState
        COMMAND cna_test_software_envmap_cube_sampler_state
        TIMEOUT 900 LABELS "Software")

    # REMED-GFX-174 control: Software is the GFX-150-corrected reference implementation, so it says
    # what the order-sensitive sampler-component contract owes independently of any GL behaviour.
    cna_software_test(cna_test_software_sampler_component_isolation
                      ${CNA_GRAPHICS_EXAMPLES_DIR}/sampler_component_isolation_contract_test.cpp)
    cna_register_renderer_test(NAME Software_SamplerComponentIsolation
        COMMAND cna_test_software_sampler_component_isolation
        TIMEOUT 300 LABELS "Software")

    # REMED-GFX-175: the mip component of a TextureFilter ordinal, measured against a chain whose
    # levels name themselves. Software is the CPU reference implementation, so what it does with a
    # real chain settles the contract independently of any GPU driver.
    cna_software_test(cna_test_software_texture_filter_mip_contract
                      ${CNA_GRAPHICS_EXAMPLES_DIR}/texture_filter_mip_contract_test.cpp)
    cna_register_renderer_test(NAME Software_TextureFilterMipContract
        COMMAND cna_test_software_texture_filter_mip_contract
        TIMEOUT 300 LABELS "Software")

    # REMED-GFX-177 cross-renderer control: descriptor/binding bookkeeping must be a function of what
    # is LIVE, never of what has ever existed. D3D12 owned four fixed-capacity heaps with a monotonic
    # bump cursor and no free list, so the 65th sampleable resource EVER CREATED threw even when six
    # were alive. Every draw here is checked against a self-identifying oracle that decodes back to an
    # integer, so a recycled or grown pool that aliases one resource onto another's binding names the
    # wrong resource rather than merely producing an odd colour.
    cna_software_test(cna_test_software_descriptor_capacity
                      ${CNA_GRAPHICS_EXAMPLES_DIR}/descriptor_capacity_contract_test.cpp)
    cna_register_renderer_test(NAME Software_DescriptorCapacityContract
        COMMAND cna_test_software_descriptor_capacity
        TIMEOUT 900 LABELS "Software")

    cna_software_test(cna_test_software_point_sampling
                      ${CNA_GRAPHICS_EXAMPLES_DIR}/point_sampling_contract_test.cpp)
    cna_register_renderer_test(NAME Software_PointSamplingContract
        COMMAND cna_test_software_point_sampling
        TIMEOUT 300 LABELS "Software")

    # plan_software.md SOFTWARE-61/84: this renderer's half of the cross-renderer diagnostic dump --
    # not registered as a ctest (see cna_diag_compare's own comment above), just a plain
    # executable a developer/script runs by hand.
    cna_software_test(cna_diag_software ${CNA_GRAPHICS_EXAMPLES_DIR}/cross_renderer_diagnostic_scene.cpp)
endif()

# Module-local tests get the same headless-safe skip convention the root one-shot
# gives root-registered tests (see cmake/TestHelpers.cmake).
cna_apply_skip_convention()
