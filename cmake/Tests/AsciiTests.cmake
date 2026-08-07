if(CNA_BUILD_TESTS AND NOT EMSCRIPTEN AND NOT WIN32
   AND CNA_GRAPHICS_BACKEND STREQUAL "ASCII")

    enable_testing()

    macro(cna_ascii_test target src)
        add_executable(${target} ${src})
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(${target} PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME)
        else()
            target_link_libraries(${target} PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(${target} PRIVATE SDL3::SDL3main)
        endif()
    endmacro()

    # Phase G2 (ASCII-10/11): hand-authored glyph-density font atlas smoke test.
    cna_ascii_test(cna_test_ascii_fontatlas examples/ascii_fontatlas_test.cpp)
    cna_register_backend_test(NAME Ascii_FontAtlas COMMAND cna_test_ascii_fontatlas
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Phase G3 (ASCII-20/21): offscreen gameTarget_ + SetRenderTarget(nullptr) redirect.
    cna_ascii_test(cna_test_ascii_offscreentarget examples/ascii_offscreentarget_test.cpp)
    cna_register_backend_test(NAME Ascii_OffscreenTarget COMMAND cna_test_ascii_offscreentarget
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Phase G4 (ASCII-30..33): quantizer -- pure function, no GraphicsDevice/window needed.
    cna_ascii_test(cna_test_ascii_quantizer examples/ascii_quantizer_test.cpp)
    cna_register_backend_test(NAME Ascii_Quantizer COMMAND cna_test_ascii_quantizer
        TIMEOUT 30)

    # Phase G5 (ASCII-40/41): Present() draws the quantized grid into the real window.
    cna_ascii_test(cna_test_ascii_present examples/ascii_present_test.cpp)
    cna_register_backend_test(NAME Ascii_Present COMMAND cna_test_ascii_present
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Phase G6 (ASCII-50/51): Mouse/Keyboard/GamePad need zero new code against a real window.
    cna_ascii_test(cna_test_ascii_input examples/ascii_input_test.cpp)
    cna_register_backend_test(NAME Ascii_Input COMMAND cna_test_ascii_input
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Phase G7 (ASCII-60): confirm the default Throw behavior for unsupported 3D calls.
    cna_ascii_test(cna_test_ascii_throwno3d examples/ascii_throwno3d_test.cpp)
    cna_register_backend_test(NAME Ascii_ThrowNo3D COMMAND cna_test_ascii_throwno3d
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-127: every public Texture2D::GetData call must return the resource's real content or
    # reject the request deterministically -- never fabricate one. This backend's render targets are
    # the wrapped SdlGraphicsBackend's own SDL_TEXTUREACCESS_TARGET textures, so it inherits that
    # backend's readback capability; the check belongs here too because the wrapper is what a game
    # actually runs against.
    cna_ascii_test(cna_test_ascii_texture2d_getdata_contract
                   examples/texture2d_getdata_contract_test.cpp)
    cna_register_backend_test(NAME Ascii_Texture2D_GetDataContract COMMAND cna_test_ascii_texture2d_getdata_contract
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-149 (and REMED-GFX-128, the same expression): the exact XNA-compatible
    # startIndex/elementCount contract of every public Texture2D::GetData overload. startIndex is a
    # DESTINATION element offset and elementCount is the destination capacity available from it;
    # the whole-level overload applied startIndex to the SOURCE, rejected any non-zero offset on a
    # render target, rejected legal excess capacity, and silently returned a partial frame for an
    # undersized one.
    cna_ascii_test(cna_test_ascii_texture2d_getdata_transfer_range
                   examples/texture2d_getdata_transfer_range_test.cpp)
    cna_register_backend_test(NAME Ascii_Texture2D_GetDataTransferRange COMMAND cna_test_ascii_texture2d_getdata_transfer_range
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-130: the TextureCube/Texture3D half of REMED-GFX-127's finding. This backend creates
    # neither a cube nor a volume resource, so both public readbacks must reject rather than convert
    # the shared layer's own zeroed scratch buffer into a complete face/volume.
    cna_ascii_test(cna_test_ascii_cube_volume_getdata_contract
                   examples/texturecube_texture3d_getdata_contract_test.cpp)
    cna_register_backend_test(NAME Ascii_CubeVolume_GetDataContract COMMAND cna_test_ascii_cube_volume_getdata_contract
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-134: `RenderTargetCube::GetData` must return the face that was really rendered or
    # reject the request deterministically. REMED-GFX-130 made the reporting honest but left the
    # readback itself implemented on only two backends, so a rendered cube face had no byte-exact
    # public oracle anywhere else. Drawn geometry (never Clear -- see REMED-GFX-129) paints an
    # asymmetric five-region pattern whose colours rotate per face, so a flip, a rotation, a wrong
    # array layer/subresource, a stale face or an unresolved multisample surface all fail.
    cna_ascii_test(cna_test_ascii_rendertargetcube_getdata_contract
                   examples/rendertargetcube_getdata_contract_test.cpp)
    cna_register_backend_test(NAME Ascii_RenderTargetCube_GetDataContract COMMAND cna_test_ascii_rendertargetcube_getdata_contract
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-136: IGraphicsBackend::CreateRenderTargetCube had no `preserveContents` parameter,
    # unlike CreateRenderTarget2D, so a RenderTargetCube's real RenderTargetUsage never reached the
    # backend and Vulkan/WebGPU discarded a PreserveContents cube face on every bind cycle. Reuses
    # REMED-GFX-134's asymmetric face producer, then rebinds and draws only a small marker: "the
    # marker landed" is what a discarding backend also achieves, so it can never pass for
    # preservation.
    cna_ascii_test(cna_test_ascii_rendertargetcube_usage
                   examples/rendertargetcube_usage_test.cpp)
    cna_register_backend_test(NAME Ascii_RenderTargetCube_Usage COMMAND cna_test_ascii_rendertargetcube_usage
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-141: a MULTISAMPLED RenderTargetCube shared ONE multisample colour attachment
    # across all six faces on EasyGL, Vulkan and SdlGpu, so a PreserveContents face reloaded whichever
    # face was rendered last instead of its own samples. Renders face A, then face B, then rebinds A for
    # a small partial update -- with no readback, Present or flush in between -- and reads the whole of
    # A: the resolved single-sample layer still held the correct A, which is why a full redraw and an
    # immediate read both passed before this.
    cna_ascii_test(cna_test_ascii_rendertargetcube_msaa_face
                   examples/rendertargetcube_msaa_face_test.cpp)
    cna_register_backend_test(NAME Ascii_RenderTargetCube_MsaaFace COMMAND cna_test_ascii_rendertargetcube_msaa_face
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-142: RenderTargetUsage's DEPTH and STENCIL half. FNA3D's own header documents
    # `preserveTargetContents` as storing the "color/depth/stencil" contents, and FNA's DiscardContents
    # bind clears all three (Target|DepthBuffer|Stencil, MaxDepth, 0) -- so the enum governs depth and
    # stencil, not colour alone. Renders an occluder, unbinds, rebinds WITHOUT clearing and draws behind
    # it: three bands separate preserved depth from cleared depth, lost colour and a second pass that
    # never drew. A parallel stencil stamp/gate sequence does the same for stencil.
    cna_ascii_test(cna_test_ascii_rendertarget_depthstencil_usage
                   examples/rendertarget_depthstencil_usage_test.cpp)
    cna_register_backend_test(NAME Ascii_RenderTarget_DepthStencilUsage COMMAND cna_test_ascii_rendertarget_depthstencil_usage
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-140: every public render-target bind/unbind cycle must be its own logical pass.
    # `VulkanGraphicsBackend::RecordCommandBuffer` collected ONE render pass per unique render-target
    # source per flush and replayed every queued batch for it inside that pass, so two bind cycles of
    # one target within a single flush window shared one load action. The decisive checks all use
    # DiscardContents -- collapsing is invisible on a preserving target, which is why REMED-GFX-136
    # needed an artificial readback barrier to see it -- and nothing between two cycles ever forces a
    # flush.
    cna_ascii_test(cna_test_ascii_rendertarget_pass_boundary
                   examples/rendertarget_pass_boundary_test.cpp)
    cna_register_backend_test(NAME Ascii_RenderTarget_PassBoundary COMMAND cna_test_ascii_rendertarget_pass_boundary
        TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-143: backbuffer work and render-target work must replay in ONE ordered stream.
    # REMED-GFX-140 (Vulkan) and REMED-GFX-145 (SdlGpu) gave every render-target bind cycle its own
    # native pass in public order, but both kept the BACKBUFFER as one trailing pass, so every
    # backbuffer draw of a frame was replayed after every target pass regardless of when it was
    # issued. A frame that samples a target onto the backbuffer and then renders into that target
    # again therefore saw the FINAL content. Every check queues its whole public sequence with no
    # Present, GetData, flush or extra frame between the cycles, and only then reads once.
    cna_ascii_test(cna_test_ascii_backbuffer_pass_order
                   examples/backbuffer_pass_order_test.cpp)
    cna_register_backend_test(NAME Ascii_Backbuffer_PassOrder COMMAND cna_test_ascii_backbuffer_pass_order
        TIMEOUT 90 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-135: the WRITE half of the same finding. `TextureCube::SetData`/`Texture3D::SetData`
    # kept the pre-REMED-GFX-127 shape -- a `void` backend method behind `if (backend_)` -- so an
    # upload that stored nothing, or only part of the requested region, still returned normally.
    cna_ascii_test(cna_test_ascii_cube_volume_setdata_contract
                   examples/texturecube_texture3d_setdata_contract_test.cpp)
    cna_register_backend_test(NAME Ascii_CubeVolume_SetDataContract COMMAND cna_test_ascii_cube_volume_setdata_contract
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_ascii_test(cna_test_ascii_unsupported_3d_behavior
                   examples/unsupported_3d_call_behavior_test.cpp)
    cna_register_backend_test(NAME Ascii_Unsupported3DBehavior
        COMMAND cna_test_ascii_unsupported_3d_behavior
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
endif()
