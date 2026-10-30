# headless renderer example/test registration (module-local; this file is the
# former cmake/Tests/HeadlessTests.cmake, moved when examples moved into their owning
# physical modules).
#
# From the root CMakeLists.txt include site:
# Headless renderer: Headless (no GPU/window) graphics renderer tests. Deliberately no
# SDL_VIDEODRIVER/DISPLAY environment -- the entire point of this renderer is running with neither.

# Example/test executables historically live at the build root (scripts, docs and
# the golden-image working-directory conventions rely on it); keep them there.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
# Shared example-support headers ("common/PixelTestGame.hpp", ...) are owned by
# the graphics module's examples directory.
include_directories(${CNA_GRAPHICS_EXAMPLES_DIR})

# Vulkan parity VKPAR-0003, the same RTR-P9-13 rule as the EasyGL block: these
# TUs include the renderer-private CNA/Internal/Renderers/Headless/HeadlessRenderer.hpp and
# assert the HEADLESS renderer's behaviour, but they run against whatever renderer the build
# DEFAULTS to -- so this block exists only when HEADLESS is the default. CNA_GRAPHICS_RENDERER
# names that default here (modules/renderers/CMakeLists.txt does not re-point it per family).
if(CNA_BUILD_TESTS AND CNA_GRAPHICS_RENDERER STREQUAL "HEADLESS")
    enable_testing()

    macro(cna_headless_test target src)
        add_executable(${target} ${src})
        target_link_libraries(${target} PRIVATE CNA SHARP_RUNTIME)
        # X11-0090: the headless renderer needs no window system, and these
        # tests exist precisely to prove that. SDL3 was linked here because it used to be the only
        # platform CNA had; it is kept where it is configured, and simply absent where it is not.
        if(TARGET SDL3::SDL3)
            target_link_libraries(${target} PRIVATE SDL3::SDL3)
            target_compile_definitions(${target} PRIVATE CNA_HEADLESS_TEST_HAS_SDL=1)
        else()
            target_compile_definitions(${target} PRIVATE CNA_EXAMPLES_NO_SDL)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(${target} PRIVATE SDL3::SDL3main)
        endif()
    endmacro()

    cna_headless_test(cna_test_headless_smoke headless_smoke_test.cpp)
    cna_register_renderer_test(NAME Headless_Smoke COMMAND cna_test_headless_smoke
        TIMEOUT 30 LABELS "Headless")

    cna_headless_test(cna_test_headless_resource_backends headless_resource_renderers_test.cpp)
    cna_register_renderer_test(NAME Headless_ResourceRenderers COMMAND cna_test_headless_resource_backends
        TIMEOUT 30 LABELS "Headless")

    cna_headless_test(cna_test_headless_validation_extras headless_validation_extras_test.cpp)
    cna_register_renderer_test(NAME Headless_ValidationExtras COMMAND cna_test_headless_validation_extras
        TIMEOUT 30 LABELS "Headless")

    cna_headless_test(cna_test_headless_coverage_gaps headless_coverage_gaps_test.cpp)
    cna_register_renderer_test(NAME Headless_CoverageGaps COMMAND cna_test_headless_coverage_gaps
        TIMEOUT 30 LABELS "Headless")

    cna_headless_test(cna_test_headless_effects headless_effects_test.cpp)
    cna_register_renderer_test(NAME Headless_Effects COMMAND cna_test_headless_effects
        TIMEOUT 30 LABELS "Headless")

    cna_headless_test(cna_test_headless_mode_dial headless_mode_dial_test.cpp)
    cna_register_renderer_test(NAME Headless_ModeDial COMMAND cna_test_headless_mode_dial
        TIMEOUT 30 LABELS "Headless")

    cna_headless_test(cna_test_headless_trace_diff headless_trace_diff_test.cpp)
    cna_register_renderer_test(NAME Headless_TraceDiff COMMAND cna_test_headless_trace_diff
        TIMEOUT 30 LABELS "Headless")

    # Every public Texture2D::GetData call must return the resource's real content or
    # reject the request deterministically -- never fabricate one. This renderer executes no
    # rasterization at all, so it is the one that must REJECT render-target colour readback: pre-fix
    # it answered with the shared layer's own zero-initialized scratch buffer, i.e. a complete,
    # uniformly transparent-black "rendered" frame it had never rendered.
    # SurfaceFormat::Color is a plain 8-bit UNORM byte format, so a mid-tone channel
    # must survive Clear/draw/sample/readback unchanged. Registered here as a cross-renderer control:
    # the defect was WebGPU-local (its render targets used the swapchain's *UnormSrgb format), and
    # these runs are what establish that byte-exact identity is CNA's existing behaviour everywhere
    # else rather than a value invented for the fix.
    cna_headless_test(cna_test_headless_colorspace_midtone
        ${CNA_GRAPHICS_EXAMPLES_DIR}/colorspace_midtone_contract_test.cpp)
    cna_register_renderer_test(NAME Headless_ColorSpace_MidTone COMMAND cna_test_headless_colorspace_midtone
        TIMEOUT 120 LABELS "Headless")

    cna_headless_test(cna_test_headless_additive_blend_contract
        ${CNA_GRAPHICS_EXAMPLES_DIR}/additive_blend_contract_test.cpp)
    cna_register_renderer_test(NAME Headless_AdditiveBlendContract
        COMMAND cna_test_headless_additive_blend_contract
        TIMEOUT 120 LABELS "Headless")

    # A RenderTarget2D used as a texture must sample in the same logical orientation
    # as an ordinary Texture2D holding identical bytes. Registered here as a cross-renderer control:
    # the defect was EasyGL-local (an OpenGL framebuffer's origin is bottom-left, so a target's
    # colour texture stores the image bottom-up and sampling did not compensate even though GetData
    # already did), and these runs are what establish that render-target and ordinary-texture
    # sampling already agree everywhere else rather than being made to agree by the fix.
    cna_headless_test(cna_test_headless_rt_sampling_orientation
        ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_sampling_orientation_test.cpp)
    cna_register_renderer_test(NAME Headless_RenderTarget_SamplingOrientation COMMAND cna_test_headless_rt_sampling_orientation
        TIMEOUT 120 LABELS "Headless")

    # Boundary control: no rasterization means target readback must refuse without
    # touching the destination, and the same resource graph must dispose cleanly.
    cna_headless_test(cna_test_headless_gfx153_source_rectangle
        ${CNA_GRAPHICS_EXAMPLES_DIR}/source_rectangle_orientation_test.cpp)
    cna_register_renderer_test(NAME Headless_GFX153_SourceRectangleOrientation
        COMMAND cna_test_headless_gfx153_source_rectangle
        TIMEOUT 180 LABELS "Headless")



    # Every stock 3D effect must sample its textures through the public
    # GraphicsDevice.SamplerStates[slot], exactly like SpriteBatch already does. The defect lived
    # in the retired VULKAN renderer, whose stock 3D descriptor builders hardcoded one default
    # sampler; this renderer ran the same public fixture as a control.
    cna_headless_test(cna_test_headless_stock_effect_sampler
        ${CNA_GRAPHICS_EXAMPLES_DIR}/stock_effect_sampler_contract_test.cpp)
    cna_register_renderer_test(NAME Headless_StockEffectSamplerContract COMMAND cna_test_headless_stock_effect_sampler
        TIMEOUT 300 LABELS "Headless")

    # Cross-renderer control: TextureFilter::Point must select exactly ONE texel and
    # TextureAddressMode must decide which one, on SpriteBatch and on the device SamplerStates[0] 3D
    # path alike. The defect was Software-local (its ApplySamplerState named none of its parameters
    # and one bilinear function served every textured fragment, so every draw was LinearClamp); this
    # run is what establishes that this renderer already honoured the contract rather than being made
    # to.
    # Every public TextureFilter ordinal names a SEPARATE magnification, a separate
    # minification and a separate mipmap filter, so a renderer may not reduce the ordinal to one
    # boolean. WebGPU's SpriteBatch sampler and SDLGPU's ONE shared sampler helper both resolved
    # `textureFilter == 0 ? LINEAR : NEAREST`, and both keyed their sampler cache on
    # `filter == 0 ? 0 : 1`, so Anisotropic, LinearMipPoint, MinPointMagLinearMipLinear and
    # MinPointMagLinearMipPoint all magnified with POINT. This fixture measures the two DIFFERENT
    # partitions of the nine ordinals that magnification and minification induce, on SpriteBatch and
    # on every textured stock family; the other renderers run it as controls.
    cna_headless_test(cna_test_headless_texture_filter_ordinal
        ${CNA_GRAPHICS_EXAMPLES_DIR}/texture_filter_ordinal_contract_test.cpp)
    cna_register_renderer_test(NAME Headless_TextureFilterOrdinalContract COMMAND cna_test_headless_texture_filter_ordinal
        TIMEOUT 300 LABELS "Headless")

    # Cross-renderer control: EnvironmentMapEffect samples TWO independent
    # resources -- the ordinary 2D texture through GraphicsDevice.SamplerStates[0] and the
    # reflection cube through SamplerStates[1]. SDLGPU bound a literal LinearClamp for the cube
    # and captured only slot 0, so slot 1 could not reach replay at all. This fixture makes the
    # two slots independently observable (EnvironmentMapAmount 1 leaves the cube the only
    # contributor, 0 leaves the 2D texture the only one) and measures what every other renderer
    # does with the same public state.
    cna_headless_test(cna_test_headless_envmap_cube_sampler
        ${CNA_GRAPHICS_EXAMPLES_DIR}/envmap_cube_sampler_contract_test.cpp)
    cna_register_renderer_test(NAME Headless_EnvMapCubeSamplerContract COMMAND cna_test_headless_envmap_cube_sampler
        TIMEOUT 600 LABELS "Headless")

    # Cross-renderer control: DualTextureEffect's two layers have INDEPENDENT public
    # sampler slots -- FNA's DualTextureEffect.fx declares DECLARE_TEXTURE(Texture, 0) and
    # DECLARE_TEXTURE(Texture2, 1). Headless does not rasterize, so it asserts the honest Texture2D::GetData contract
    # honest rejection of the readback instead of fabricating a sampled image.
    cna_headless_test(cna_test_headless_dualtexture_slot_sampler
        ${CNA_GRAPHICS_EXAMPLES_DIR}/dualtexture_slot_sampler_contract_test.cpp)
    cna_register_renderer_test(NAME Headless_DualTextureSlotSamplerContract COMMAND cna_test_headless_dualtexture_slot_sampler
        TIMEOUT 600 LABELS "Headless")

    # Cross-renderer control: the MIPMAP component of a TextureFilter ordinal.
    # EasyGL mapped ordinals 0 and 1 onto a GL filter with no mipmap term and Software had no
    # mip pipeline at all; this fixture measures what every other renderer does with a chain
    # whose levels name themselves, so a divergence is classified rather than assumed.
    cna_headless_test(cna_test_headless_texture_filter_mip_contract
        ${CNA_GRAPHICS_EXAMPLES_DIR}/texture_filter_mip_contract_test.cpp)
    cna_register_renderer_test(NAME Headless_TextureFilterMipContract COMMAND cna_test_headless_texture_filter_mip_contract
        TIMEOUT 300 LABELS "Headless")

    # Cross-renderer control: descriptor/binding bookkeeping must be a function of what
    # is LIVE, never of what has ever existed. D3D12 owned four fixed-capacity heaps with a monotonic
    # bump cursor and no free list, so the 65th sampleable resource EVER CREATED threw even when six
    # were alive. Every draw here is checked against a self-identifying oracle that decodes back to an
    # integer, so a recycled or grown pool that aliases one resource onto another's binding names the
    # wrong resource rather than merely producing an odd colour.
    cna_headless_test(cna_test_headless_descriptor_capacity
        ${CNA_GRAPHICS_EXAMPLES_DIR}/descriptor_capacity_contract_test.cpp)
    cna_register_renderer_test(NAME Headless_DescriptorCapacityContract COMMAND cna_test_headless_descriptor_capacity
        TIMEOUT 900 LABELS "Headless")

    # XNA addresses pixel centres at integer window coordinates and covers a 1x1
    # screen-space triangle -- measured on the real XNA 4.0 runtime, see spikes/xna-pixel-center-spike/.
    # The convention belongs to XNA rather than to any one renderer, so every renderer that can run
    # this owes it; one that has not implemented it says so by failing here rather than by never
    # being asked.
    cna_headless_test(cna_test_headless_xna_pixel_center
        ${CNA_GRAPHICS_EXAMPLES_DIR}/xna_pixel_center_contract_test.cpp)
    cna_register_renderer_test(NAME Headless_XnaPixelCenter COMMAND cna_test_headless_xna_pixel_center
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_headless_test(cna_test_headless_point_sampling
        ${CNA_GRAPHICS_EXAMPLES_DIR}/point_sampling_contract_test.cpp)
    cna_register_renderer_test(NAME Headless_PointSamplingContract COMMAND cna_test_headless_point_sampling
        TIMEOUT 300 LABELS "Headless")

    # Cross-renderer control: the canonical XNA render-to-texture sequence -- render
    # into a target, unbind it, sample it -- must complete in ONE public frame with no intervening
    # GetData, Present, extra frame, manual flush or wait. The defect was Vulkan-local (its deferred
    # recorder's readback flush filtered the frame's segment list down to the target being READ, so a
    # producer's pass was never recorded before the consumer that sampled it); these runs are what
    # establish that every other renderer already honoured the contract rather than being made to.
    cna_headless_test(cna_test_headless_rt_producer_consumer
        ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_producer_consumer_test.cpp)
    cna_register_renderer_test(NAME Headless_RenderTarget_ProducerConsumer COMMAND cna_test_headless_rt_producer_consumer
        TIMEOUT 120 LABELS "Headless")

    # Cross-renderer control: a RenderTarget2D handed to a stock or custom 3D effect as
    # its texture must be sampled, not reinterpreted. The defect was SDLGPU-local and fatal (its
    # stock-effect paths static_cast an ITextureRenderer* to the unrelated sibling
    # SdlGpuTextureRenderer, fabricating an SDL_GPUTexture* out of a render target's own fields); this
    # run is what establishes that HEADLESS already honoured the contract rather than being made to.
    cna_headless_test(cna_test_headless_rt_effect_source
        ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_effect_source_test.cpp)
    cna_register_renderer_test(NAME Headless_RenderTarget_EffectSource COMMAND cna_test_headless_rt_effect_source
        TIMEOUT 300 LABELS "Headless")

    # Deferred-source lifetime: a resource sampled by a draw that has only been
    # QUEUED must stay bindable until that draw actually renders, and destroying the public wrapper
    # first must never terminate the process. The defect was WebGPU-local -- every deferred command
    # stored a raw pointer to the resource's RENDERER OBJECT and called a VIRTUAL method on it at
    # replay, so a RenderTarget2D produced, sampled onto the BACKBUFFER and dropped inside one
    # Draw() was a heap-use-after-free at Present(). These runs establish which renderers already
    # honoured the contract rather than being made to; each leg runs in its own process so a
    # SIGSEGV is an attributable result instead of a lost shard.
    cna_headless_test(cna_test_headless_deferred_source_lifetime
        ${CNA_GRAPHICS_EXAMPLES_DIR}/deferred_source_lifetime_test.cpp)
    cna_register_renderer_test(NAME Headless_DeferredSourceLifetime COMMAND cna_test_headless_deferred_source_lifetime
        TIMEOUT 300 LABELS "Headless")

    # Cross-renderer control: destroying a RenderTarget2D that is STILL the bound
    # render target must never make the next SetRenderTarget transition unsafe, and must leave the
    # next target and the backbuffer exactly correct. The defect was EasyGL-local -- it remembered
    # the bound destination as a raw IRenderTargetRenderer* and the next transition called
    # UnbindAsRenderTarget() on it, so a scoped target leaving scope while bound made that
    # transition a virtual call through freed storage. This run is what establishes that HEADLESS
    # already honoured the contract rather than being made to; each leg runs in its own process so
    # a SIGSEGV is an attributable result instead of a lost shard.
    cna_headless_test(cna_test_headless_bound_target_lifetime
        ${CNA_GRAPHICS_EXAMPLES_DIR}/bound_target_lifetime_test.cpp)
    cna_register_renderer_test(NAME Headless_BoundTargetLifetime COMMAND cna_test_headless_bound_target_lifetime
        TIMEOUT 600 LABELS "Headless")

    # SOFTWARE-322: Present refuses while a target is bound independently of whether
    # the preceding native buffered range was forwarded or stopped by this renderer's compatibility
    # safety guard. C1 proves explicit-unbind recovery and C2 proves the refusal is SIGABRT.
    cna_headless_test(cna_test_headless_present_lifecycle
        ${CNA_GRAPHICS_EXAMPLES_DIR}/present_lifecycle_contract_test.cpp)
    cna_register_renderer_test(NAME Headless_PresentLifecycle COMMAND cna_test_headless_present_lifecycle
        TIMEOUT 900 LABELS "Headless")

    # NPV-0123: these four share
    # common/SdlTestGraphicsServices.hpp, an SDL-backed test harness, so they cannot link in a
    # build with no SDL configured -- a plain `cmake --build` of an SDL-free HEADLESS build
    # failed on them. They are cross-renderer controls for SDL-era defects; without SDL they are
    # skipped, and said so.
    if(TARGET SDL3::SDL3)
        # Cross-renderer control: the same public depth-backed MULTISAMPLED
        # RenderTarget2D construction and lifecycle that aborted the process on Bgfx, a renderer
        # retired on 2026-09-17. Only Bgfx production changed; this renderer is registered to show
        # the construction was always legal and to keep it that way.
        cna_headless_test(cna_test_headless_msaa_depth_contract
            ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_msaa_depth_contract_test.cpp)
        cna_register_renderer_test(NAME Headless_MsaaDepthContract COMMAND cna_test_headless_msaa_depth_contract
            TIMEOUT 900 LABELS "Headless")

        # Cross-renderer control: the same PUBLIC first-readback contract on this
        # renderer. The defect was Bgfx-only and only Bgfx production changed; running the identical
        # fixture here is what makes that claim falsifiable rather than asserted.
        cna_headless_test(cna_test_headless_msaa_first_readback
            ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_msaa_first_readback_test.cpp)
        cna_register_renderer_test(NAME Headless_MsaaFirstReadback COMMAND cna_test_headless_msaa_first_readback
            TIMEOUT 900 LABELS "Headless")

        # Cross-renderer control: the same PUBLIC generated-mip readback contract on this
        # renderer. The SIGSEGV was SDLGPU-only and only SDLGPU production changed; running the
        # identical fixture here is what makes that claim falsifiable rather than asserted.
        cna_headless_test(cna_test_headless_msaa_mip_readback
            ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_msaa_mip_readback_test.cpp)
        cna_register_renderer_test(NAME Headless_MsaaMipReadback COMMAND cna_test_headless_msaa_mip_readback
            TIMEOUT 1200 LABELS "Headless")
        # Cross-renderer control: the same PUBLIC invalid-mip-level contract on this
        # renderer. The fabrication was Vulkan-only and only Vulkan production changed; running the
        # identical fixture here is what makes that claim falsifiable rather than asserted.
        cna_headless_test(cna_test_headless_invalid_mip_level
            ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_invalid_mip_level_test.cpp)
        cna_register_renderer_test(NAME Headless_InvalidMipLevel COMMAND cna_test_headless_invalid_mip_level
            TIMEOUT 1200 LABELS "Headless")
    else()
        message(STATUS "CNA: no SDL3 target -- skipping the SDL-harness Headless controls "
                       "(MsaaDepthContract, MsaaFirstReadback, MsaaMipReadback, InvalidMipLevel)")
    endif()
    # Cross-renderer control: Headless does not rasterize, so the pixel oracle is a
    # declared boundary; the run still exercises the shared dimension/rectangle-validation path.
    cna_headless_test(cna_test_headless_backbuffer_readback_dimension
        ${CNA_GRAPHICS_EXAMPLES_DIR}/backbuffer_readback_dimension_test.cpp)
    cna_register_renderer_test(NAME Headless_BackbufferReadbackDimension COMMAND cna_test_headless_backbuffer_readback_dimension
        TIMEOUT 300 LABELS "Headless")

    # Cross-renderer control for the first-read completion contract.
    cna_headless_test(cna_test_headless_backbuffer_first_read
        ${CNA_GRAPHICS_EXAMPLES_DIR}/backbuffer_first_read_test.cpp)
    cna_register_renderer_test(NAME Headless_BackbufferFirstRead COMMAND cna_test_headless_backbuffer_first_read
        TIMEOUT 300 LABELS "Headless")

    # The PRIMARY subject -- Headless rasterizes nothing, so GetBackBufferData must
    # reject (System::NotSupportedException) rather than fabricate the last Clear() colour, and must
    # leave the caller's destination untouched while keeping argument-validation precedence.
    cna_headless_test(cna_test_headless_backbuffer_reject
        ${CNA_GRAPHICS_EXAMPLES_DIR}/backbuffer_headless_reject_test.cpp)
    cna_register_renderer_test(NAME Headless_BackbufferReject COMMAND cna_test_headless_backbuffer_reject
        TIMEOUT 300 LABELS "Headless")

    # Cross-renderer control: a render target produced and unbound earlier in a public
    # frame must be visible to a consumer that draws on the BACKBUFFER later in that same frame. The
    # defect was bgfx-local (it radix-sorts a frame's draws by their view's sort position, which
    # defaults to the numeric view id, and its backbuffer owns the lowest id -- so a backbuffer
    # consumer executed before its own producer); these runs are what establish that every other
    # renderer already honoured the contract rather than being made to.
    cna_headless_test(cna_test_headless_rt_backbuffer_consumer
        ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_backbuffer_consumer_test.cpp)
    cna_register_renderer_test(NAME Headless_RenderTarget_BackbufferConsumer COMMAND cna_test_headless_rt_backbuffer_consumer
        TIMEOUT 180 LABELS "Headless")

    # The control: a RenderTarget2D constructed during a public frame must be usable in
    # that same frame -- bound, cleared and/or drawn into, unbound and observed -- with no warm-up
    # frame, Present, GetData-before-first-render, dummy draw, empty bind cycle, manual frame advance
    # or wait. The defect was bgfx-local (`bgfx::reset()` discards every view's framebuffer binding,
    # including the bound target's), so this run is what establishes that HEADLESS already honoured the
    # contract rather than being made to.
    cna_headless_test(cna_test_headless_rt_first_use
        ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_first_use_test.cpp)
    cna_register_renderer_test(NAME Headless_RenderTarget_FirstUse COMMAND cna_test_headless_rt_first_use
        TIMEOUT 180 LABELS "Headless")

    # A stock 3D draw issued AFTER a SpriteBatch inside ONE render-target bind cycle
    # must execute after it rather than be dropped. the leg I0 measured the region it
    # drew into still holding the cycle's clear colour on BGFX, VULKAN, SOFTWARE and EASYGL, with
    # WEBGPU clean, using ordinary Texture2D sources on both destinations. The oracle is a staircase
    # -- step i covers stripes [0, N-i), so one readback proves every draw happened AND that they
    # happened in the issued order.
    cna_headless_test(cna_test_headless_spritebatch_3d_order
        ${CNA_GRAPHICS_EXAMPLES_DIR}/spritebatch_3d_order_test.cpp)
    cna_register_renderer_test(NAME Headless_SpriteBatch3DOrder
        COMMAND cna_test_headless_spritebatch_3d_order
        TIMEOUT 300 LABELS "Headless")

    # The XNA/FNA front-face winding contract. This renderer rasterizes nothing, so
    # The honest Texture2D::GetData contract's contract makes every readback reject and the fixture asserts that
    # rejection rather than a pixel value.
    cna_headless_test(cna_test_headless_frontface_winding
        ${CNA_GRAPHICS_EXAMPLES_DIR}/frontface_winding_test.cpp)
    cna_register_renderer_test(NAME Headless_FrontFaceWinding
        COMMAND cna_test_headless_frontface_winding
        TIMEOUT 300 LABELS "Headless")

    # Control: Headless declares its non-rasterizing boundary.
    cna_headless_test(cna_test_headless_triangle_strip_winding
        ${CNA_GRAPHICS_EXAMPLES_DIR}/triangle_strip_winding_test.cpp)
    cna_register_renderer_test(NAME Headless_TriangleStripWinding
        COMMAND cna_test_headless_triangle_strip_winding
        TIMEOUT 300 LABELS "Headless")

    cna_headless_test(cna_test_headless_texture2d_getdata_contract
                      ${CNA_GRAPHICS_EXAMPLES_DIR}/texture2d_getdata_contract_test.cpp)
    cna_register_renderer_test(NAME Headless_Texture2D_GetDataContract
        COMMAND cna_test_headless_texture2d_getdata_contract
        TIMEOUT 30 LABELS "Headless")

    # (and the GetData startIndex contract, the same expression): the exact XNA-compatible
    # startIndex/elementCount contract of every public Texture2D::GetData overload. startIndex is a
    # DESTINATION element offset and elementCount is the destination capacity available from it;
    # the whole-level overload applied startIndex to the SOURCE, rejected any non-zero offset on a
    # render target, rejected legal excess capacity, and silently returned a partial frame for an
    # undersized one.
    cna_headless_test(cna_test_headless_texture2d_getdata_transfer_range
                      ${CNA_GRAPHICS_EXAMPLES_DIR}/texture2d_getdata_transfer_range_test.cpp)
    cna_register_renderer_test(NAME Headless_Texture2D_GetDataTransferRange
        COMMAND cna_test_headless_texture2d_getdata_transfer_range
        TIMEOUT 30 LABELS "Headless")

    # The TextureCube/Texture3D half of the finding. Plain cube resources preserve their authored
    # CPU data; Texture3D remains unavailable because Headless exposes no volume capability.
    cna_headless_test(cna_test_headless_cube_volume_getdata_contract
                      ${CNA_GRAPHICS_EXAMPLES_DIR}/texturecube_texture3d_getdata_contract_test.cpp)
    cna_register_renderer_test(NAME Headless_CubeVolume_GetDataContract
        COMMAND cna_test_headless_cube_volume_getdata_contract
        TIMEOUT 30 LABELS "Headless")

    # `RenderTargetCube::GetData` must return the face that was really rendered or
    # reject the request deterministically. the honest TextureCube/Texture3D readback contract made the reporting honest but left the
    # readback itself implemented on only two renderers, so a rendered cube face had no byte-exact
    # public oracle anywhere else. Drawn geometry (never Clear -- paints an
    # asymmetric five-region pattern whose colours rotate per face, so a flip, a rotation, a wrong
    # array layer/subresource, a stale face or an unresolved multisample surface all fail.
    cna_headless_test(cna_test_headless_rendertargetcube_getdata_contract
                      ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertargetcube_getdata_contract_test.cpp)
    cna_register_renderer_test(NAME Headless_RenderTargetCube_GetDataContract
        COMMAND cna_test_headless_rendertargetcube_getdata_contract
        TIMEOUT 30 LABELS "Headless")

    # IGraphicsRenderer::CreateRenderTargetCube had no `preserveContents` parameter,
    # unlike CreateRenderTarget2D, so a RenderTargetCube's real RenderTargetUsage never reached the
    # renderer and Vulkan/WebGPU discarded a PreserveContents cube face on every bind cycle. Reuses
    # The asymmetric face producer, then rebinds and draws only a small marker: "the
    # marker landed" is what a discarding renderer also achieves, so it can never pass for
    # preservation.
    cna_headless_test(cna_test_headless_rendertargetcube_usage
                      ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertargetcube_usage_test.cpp)
    cna_register_renderer_test(NAME Headless_RenderTargetCube_Usage
        COMMAND cna_test_headless_rendertargetcube_usage
        TIMEOUT 30 LABELS "Headless")

    # A MULTISAMPLED RenderTargetCube shared ONE multisample colour attachment
    # across all six faces on EasyGL, Vulkan and SdlGpu, so a PreserveContents face reloaded whichever
    # face was rendered last instead of its own samples. Renders face A, then face B, then rebinds A for
    # a small partial update -- with no readback, Present or flush in between -- and reads the whole of
    # A: the resolved single-sample layer still held the correct A, which is why a full redraw and an
    # immediate read both passed before this.
    cna_headless_test(cna_test_headless_rendertargetcube_msaa_face
                      ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertargetcube_msaa_face_test.cpp)
    cna_register_renderer_test(NAME Headless_RenderTargetCube_MsaaFace
        COMMAND cna_test_headless_rendertargetcube_msaa_face
        TIMEOUT 30 LABELS "Headless")

    # RenderTargetUsage's DEPTH and STENCIL half. FNA3D's own header documents
    # `preserveTargetContents` as storing the "color/depth/stencil" contents, and FNA's DiscardContents
    # bind clears all three (Target|DepthBuffer|Stencil, MaxDepth, 0) -- so the enum governs depth and
    # stencil, not colour alone. Renders an occluder, unbinds, rebinds WITHOUT clearing and draws behind
    # it: three bands separate preserved depth from cleared depth, lost colour and a second pass that
    # never drew. A parallel stencil stamp/gate sequence does the same for stencil.
    cna_headless_test(cna_test_headless_rendertarget_depthstencil_usage
                      ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_depthstencil_usage_test.cpp)
    cna_register_renderer_test(NAME Headless_RenderTarget_DepthStencilUsage
        COMMAND cna_test_headless_rendertarget_depthstencil_usage
        TIMEOUT 30 LABELS "Headless")

    # Every public render-target bind/unbind cycle must be its own logical pass.
    # `VulkanRenderer::RecordCommandBuffer` collected ONE render pass per unique render-target
    # source per flush and replayed every queued batch for it inside that pass, so two bind cycles of
    # one target within a single flush window shared one load action. The decisive checks all use
    # DiscardContents -- collapsing is invisible on a preserving target, which is why the RenderTargetUsage propagation fix
    # needed an artificial readback barrier to see it -- and nothing between two cycles ever forces a
    # flush.
    cna_headless_test(cna_test_headless_rendertarget_pass_boundary
                      ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_pass_boundary_test.cpp)
    cna_register_renderer_test(NAME Headless_RenderTarget_PassBoundary
        COMMAND cna_test_headless_rendertarget_pass_boundary
        TIMEOUT 60 LABELS "Headless")

    # Clear() is an ordered command at its exact public call position.
    cna_headless_test(cna_test_headless_ordered_clear
                      ${CNA_GRAPHICS_EXAMPLES_DIR}/graphicsdevice_ordered_clear_test.cpp)
    cna_register_renderer_test(NAME Headless_GraphicsDevice_OrderedClear
        COMMAND cna_test_headless_ordered_clear
        TIMEOUT 120 LABELS "Headless")

    # Backbuffer work and render-target work must replay in ONE ordered stream.
    # (Vulkan) and the per-bind-cycle render pass fix (SdlGpu) gave every render-target bind cycle its own
    # native pass in public order, but both kept the BACKBUFFER as one trailing pass, so every
    # backbuffer draw of a frame was replayed after every target pass regardless of when it was
    # issued. A frame that samples a target onto the backbuffer and then renders into that target
    # again therefore saw the FINAL content. Every check queues its whole public sequence with no
    # Present, GetData, flush or extra frame between the cycles, and only then reads once.
    cna_headless_test(cna_test_headless_backbuffer_pass_order
                      ${CNA_GRAPHICS_EXAMPLES_DIR}/backbuffer_pass_order_test.cpp)
    cna_register_renderer_test(NAME Headless_Backbuffer_PassOrder
        COMMAND cna_test_headless_backbuffer_pass_order
        TIMEOUT 90 LABELS "Headless")

    # Cross-renderer control: every deferred draw must execute under the
    # GraphicsDevice.Viewport active at its own public call. WebGPU resolved it live when it
    # recorded the pass; this file is the same public fixture, so a renderer that regresses the
    # contract is caught here rather than assumed correct.
    cna_headless_test(cna_test_headless_deferred_viewport
                    ${CNA_GRAPHICS_EXAMPLES_DIR}/deferred_viewport_capture_test.cpp)
    cna_register_renderer_test(NAME Headless_Deferred_Viewport COMMAND cna_test_headless_deferred_viewport
        TIMEOUT 90 LABELS "Headless")

    # Cross-renderer control: every deferred draw must execute under the
    # GraphicsDevice.ScissorRectangle and RasterizerState.ScissorTestEnable active at its own
    # public call. WebGPU resolved both live when it recorded the pass; this file is the same
    # public fixture, so a renderer that regresses the contract is caught here rather than
    # assumed correct.
    cna_headless_test(cna_test_headless_deferred_scissor
                    ${CNA_GRAPHICS_EXAMPLES_DIR}/deferred_scissor_capture_test.cpp)
    cna_register_renderer_test(NAME Headless_Deferred_Scissor COMMAND cna_test_headless_deferred_scissor
        TIMEOUT 90 LABELS "Headless")

    # The WRITE half of the same finding. `TextureCube::SetData`/`Texture3D::SetData`
    # kept the pre-the honest Texture2D::GetData contract shape -- a `void` renderer method behind `if (renderer_)` -- so an
    # upload that stored nothing, or only part of the requested region, still returned normally.
    cna_headless_test(cna_test_headless_cube_volume_setdata_contract
                      ${CNA_GRAPHICS_EXAMPLES_DIR}/texturecube_texture3d_setdata_contract_test.cpp)
    cna_register_renderer_test(NAME Headless_CubeVolume_SetDataContract
        COMMAND cna_test_headless_cube_volume_setdata_contract
        TIMEOUT 30 LABELS "Headless")

    # RRC-045: the renderer-neutral conformance programs. HEADLESS makes no pixel claim, so a
    # program whose oracle is a read-back pixel skips with that reason; the API contracts (disposed
    # guards, adapter queries, capability truth, refusals) run.
    include(${CNA_GRAPHICS_EXAMPLES_DIR}/parity/ParityFixtures.cmake)
    cna_register_conformance_programs(
        BUILDER cna_headless_test TEST_PREFIX Headless TIMEOUT 120 LABELS "Headless")
endif()

# Module-local tests get the same headless-safe skip convention the root one-shot
# gives root-registered tests (see cmake/TestHelpers.cmake).
cna_apply_skip_convention()
