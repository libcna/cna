if(CNA_BUILD_TESTS AND CNA_GRAPHICS_BACKEND STREQUAL "CANVAS")
    add_executable(cna_test_canvas_smoke examples/canvas_smoke_test.cpp)
    target_link_libraries(cna_test_canvas_smoke PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)

    # CNA::GraphicsCapability: Canvas is 2D-only -- SupportsCapability() reports which
    # capabilities are genuinely absent (ThreeD and everything that depends on it). Twin of
    # cna_test_sdl_graphics_capability/cna_test_dx3_graphics_capability.
    add_executable(cna_test_canvas_graphics_capability examples/canvas_graphics_capability_test.cpp)
    target_link_libraries(cna_test_canvas_graphics_capability PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)

    # REMED-GFX-127: every public Texture2D::GetData call must return the resource's real content or
    # reject the request deterministically -- never fabricate one. Built (not ctest-registered, like
    # every Canvas test: this backend produces a wasm module that needs a browser), so the readback
    # contract at least stays compile-verified on the Emscripten toolchain.
    add_executable(cna_test_canvas_texture2d_getdata_contract examples/texture2d_getdata_contract_test.cpp)
    target_link_libraries(cna_test_canvas_texture2d_getdata_contract PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)

    # REMED-GFX-130: the TextureCube/Texture3D half of the same finding. Built (not ctest-registered,
    # like every Canvas test) so this backend's deterministic cube/volume rejection stays
    # compile-verified on the Emscripten toolchain.
    add_executable(cna_test_canvas_cube_volume_getdata_contract
                   examples/texturecube_texture3d_getdata_contract_test.cpp)
    target_link_libraries(cna_test_canvas_cube_volume_getdata_contract PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)

    # REMED-GFX-134: `RenderTargetCube::GetData` must return the face that was really rendered or
    # reject the request deterministically. REMED-GFX-130 made the reporting honest but left the
    # readback itself implemented on only two backends, so a rendered cube face had no byte-exact
    # public oracle anywhere else. Drawn geometry (never Clear -- see REMED-GFX-129) paints an
    # asymmetric five-region pattern whose colours rotate per face, so a flip, a rotation, a wrong
    # array layer/subresource, a stale face or an unresolved multisample surface all fail.
    # Compile-verified on the Emscripten toolchain only, like every other Canvas entry.
    add_executable(cna_test_canvas_rendertargetcube_getdata_contract
                   examples/rendertargetcube_getdata_contract_test.cpp)
    target_link_libraries(cna_test_canvas_rendertargetcube_getdata_contract PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)

    # REMED-GFX-136: IGraphicsBackend::CreateRenderTargetCube had no `preserveContents` parameter,
    # unlike CreateRenderTarget2D, so a RenderTargetCube's real RenderTargetUsage never reached the
    # backend and Vulkan/WebGPU discarded a PreserveContents cube face on every bind cycle. Reuses
    # REMED-GFX-134's asymmetric face producer, then rebinds and draws only a small marker: "the
    # marker landed" is what a discarding backend also achieves, so it can never pass for
    # preservation.
    add_executable(cna_test_canvas_rendertargetcube_usage
                   examples/rendertargetcube_usage_test.cpp)
    target_link_libraries(cna_test_canvas_rendertargetcube_usage PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)

    # REMED-GFX-141: a MULTISAMPLED RenderTargetCube shared ONE multisample colour attachment
    # across all six faces on EasyGL, Vulkan and SdlGpu, so a PreserveContents face reloaded whichever
    # face was rendered last instead of its own samples. Renders face A, then face B, then rebinds A for
    # a small partial update -- with no readback, Present or flush in between -- and reads the whole of
    # A: the resolved single-sample layer still held the correct A, which is why a full redraw and an
    # immediate read both passed before this.
    cna_canvas_test(cna_test_canvas_rendertargetcube_msaa_face
                    examples/rendertargetcube_msaa_face_test.cpp)
    target_link_libraries(cna_test_canvas_rendertargetcube_msaa_face PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)

    # REMED-GFX-140: every public render-target bind/unbind cycle must be its own logical pass.
    # `VulkanGraphicsBackend::RecordCommandBuffer` collected ONE render pass per unique render-target
    # source per flush and replayed every queued batch for it inside that pass, so two bind cycles of
    # one target within a single flush window shared one load action. The decisive checks all use
    # DiscardContents -- collapsing is invisible on a preserving target, which is why REMED-GFX-136
    # needed an artificial readback barrier to see it -- and nothing between two cycles ever forces a
    # flush.
    add_executable(cna_test_canvas_rendertarget_pass_boundary
                   examples/rendertarget_pass_boundary_test.cpp)
    target_link_libraries(cna_test_canvas_rendertarget_pass_boundary PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)

    # REMED-GFX-143: backbuffer work and render-target work must replay in ONE ordered stream.
    # REMED-GFX-140 (Vulkan) and REMED-GFX-145 (SdlGpu) gave every render-target bind cycle its own
    # native pass in public order, but both kept the BACKBUFFER as one trailing pass, so every
    # backbuffer draw of a frame was replayed after every target pass regardless of when it was
    # issued. A frame that samples a target onto the backbuffer and then renders into that target
    # again therefore saw the FINAL content. Every check queues its whole public sequence with no
    # Present, GetData, flush or extra frame between the cycles, and only then reads once.
    add_executable(cna_test_canvas_backbuffer_pass_order
                   examples/backbuffer_pass_order_test.cpp)
    target_link_libraries(cna_test_canvas_backbuffer_pass_order PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)

    # REMED-GFX-135: the WRITE half of the same finding. `TextureCube::SetData`/`Texture3D::SetData`
    # kept the pre-REMED-GFX-127 shape -- a `void` backend method behind `if (backend_)` -- so an
    # upload that stored nothing, or only part of the requested region, still returned normally.
    add_executable(cna_test_canvas_cube_volume_setdata_contract
                   examples/texturecube_texture3d_setdata_contract_test.cpp)
    target_link_libraries(cna_test_canvas_cube_volume_setdata_contract PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
endif()
