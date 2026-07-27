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
    cna_software_test(cna_test_software_texture2d_getdata_contract
                      examples/texture2d_getdata_contract_test.cpp)
    cna_register_backend_test(NAME Software_Texture2D_GetDataContract
        COMMAND cna_test_software_texture2d_getdata_contract
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

    # plan_software.md SOFTWARE-61/84: this backend's half of the cross-backend diagnostic dump --
    # not registered as a ctest (see cna_diag_compare's own comment above), just a plain
    # executable a developer/script runs by hand.
    cna_software_test(cna_diag_software examples/cross_backend_diagnostic_scene.cpp)
endif()
