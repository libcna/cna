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
    cna_headless_test(cna_test_headless_texture2d_getdata_contract
                      examples/texture2d_getdata_contract_test.cpp)
    cna_register_backend_test(NAME Headless_Texture2D_GetDataContract
        COMMAND cna_test_headless_texture2d_getdata_contract
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

    # REMED-GFX-135: the WRITE half of the same finding. `TextureCube::SetData`/`Texture3D::SetData`
    # kept the pre-REMED-GFX-127 shape -- a `void` backend method behind `if (backend_)` -- so an
    # upload that stored nothing, or only part of the requested region, still returned normally.
    cna_headless_test(cna_test_headless_cube_volume_setdata_contract
                      examples/texturecube_texture3d_setdata_contract_test.cpp)
    cna_register_backend_test(NAME Headless_CubeVolume_SetDataContract
        COMMAND cna_test_headless_cube_volume_setdata_contract
        TIMEOUT 30 LABELS "Headless")
endif()
