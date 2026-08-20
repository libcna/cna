# freedirect renderer example/test registration (module-local; this file is the
# former cmake/Tests/FreeDirectTests.cmake, moved when examples moved into their owning
# physical modules).
#
# From the root CMakeLists.txt include site:
# plans/plan_freedirect.md Phase X2/X8 (DX3-18/DX3-80): DIRECTX3 (DirectDraw via free-direct) CTest registration.
# Unlike SDL_RENDERER's own window-based ctests, these do NOT need a real/virtual X11 display:
# every pixel assertion reads back through the shadow-backbuffer surface directly
# (FreeDirectRenderer::ReadBackbuffer/GetBackBufferData), never through real physical
# presentation, so SDL_VIDEODRIVER=dummy (no display required at all) produces identical,
# verified-correct results (confirmed empirically: all 8 binaries, 49/49 checks, under
# SDL_VIDEODRIVER=dummy) while being far more portable than requiring CNA_TEST_DISPLAY to point
# at a working X11 server. This was a real registration bug found in review: the original
# SDL_VIDEODRIVER=x11 registration (copy-pasted from SDL_RENDERER's own pattern, which genuinely
# does need a real display) made every one of these 8 ctests fail outright in any environment
# without a live DISPLAY, even though the underlying binaries themselves were always correct.

# Example/test executables historically live at the build root (scripts, docs and
# the golden-image working-directory conventions rely on it); keep them there.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
# Shared example-support headers ("common/PixelTestGame.hpp", ...) are owned by
# the graphics module's examples directory.
include_directories(${CNA_GRAPHICS_EXAMPLES_DIR})

if(CNA_BUILD_TESTS AND NOT EMSCRIPTEN AND NOT WIN32
   AND CNA_GRAPHICS_RENDERER STREQUAL "FREEDIRECT")

    enable_testing()

    macro(cna_directx3_test target src)
        add_executable(${target} ${src})
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(${target} PRIVATE
                CNA
                SHARP_RUNTIME)
        else()
            target_link_libraries(${target} PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(${target} PRIVATE SDL3::SDL3main)
        endif()
    endmacro()

    cna_directx3_test(cna_test_freedirect_smoke freedirect_smoke_test.cpp)
    cna_register_renderer_test(NAME FreeDirect_Smoke COMMAND cna_test_freedirect_smoke
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=dummy" LABELS "FREEDIRECT")

    # REMED-GFX-029: transactional primary/shadow-backbuffer replacement, stage-by-stage
    # failure injection, native identity/destruction accounting, and public resize-state checks.
    cna_directx3_test(cna_test_freedirect_resize_transaction freedirect_resize_transaction_test.cpp)
    cna_register_renderer_test(NAME FreeDirect_ResizeTransaction COMMAND cna_test_freedirect_resize_transaction
        TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=dummy" LABELS "FREEDIRECT;REMED-GFX-029")

    # plans/plan_freedirect.md Phase X3 (DX3-20..DX3-28): texture/render-target renderer CTest.
    cna_directx3_test(cna_test_freedirect_texture_rendertarget freedirect_texture_rendertarget_test.cpp)
    cna_register_renderer_test(NAME FreeDirect_TextureRenderTarget COMMAND cna_test_freedirect_texture_rendertarget
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=dummy" LABELS "FREEDIRECT")

    # plans/plan_freedirect.md Phase X4 (DX3-30..DX3-39): CPU compositor / SpriteBatch draw path CTest.
    cna_directx3_test(cna_test_freedirect_spritebatch freedirect_spritebatch_test.cpp)
    cna_register_renderer_test(NAME FreeDirect_SpriteBatch COMMAND cna_test_freedirect_spritebatch
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=dummy" LABELS "FREEDIRECT")

    # plans/plan_freedirect.md Phase X5 (DX3-40..DX3-44): blend-mode compositing math CTest.
    cna_directx3_test(cna_test_freedirect_blend freedirect_blend_test.cpp)
    cna_register_renderer_test(NAME FreeDirect_Blend COMMAND cna_test_freedirect_blend
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=dummy" LABELS "FREEDIRECT")

    # plans/plan_freedirect.md Phase X5 (DX3-45/DX3-46): TextureFilter + TextureAddressMode sampling CTest.
    cna_directx3_test(cna_test_freedirect_sampling freedirect_sampling_test.cpp)
    cna_register_renderer_test(NAME FreeDirect_AddressMode COMMAND cna_test_freedirect_sampling
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=dummy" LABELS "FREEDIRECT")

    # plans/plan_freedirect.md Phase X6 (DX3-50..DX3-54): SpriteFont / DrawString CTest.
    cna_directx3_test(cna_test_freedirect_spritefont freedirect_spritefont_test.cpp)
    cna_register_renderer_test(NAME FreeDirect_SpriteFont COMMAND cna_test_freedirect_spritefont
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=dummy" LABELS "FREEDIRECT")

    # plans/plan_freedirect.md Phase X7 (DX3-60..DX3-67, DX3-69): ThrowNo3D wiring / remaining-defaults CTest.
    cna_directx3_test(cna_test_freedirect_no3d freedirect_no3d_test.cpp)
    cna_register_renderer_test(NAME FreeDirect_No3D COMMAND cna_test_freedirect_no3d
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=dummy" LABELS "FREEDIRECT")

    # CNA::GraphicsCapability: DIRECTX3 is 2D-only -- SupportsCapability() reports which capabilities
    # are genuinely absent (ThreeD and everything that depends on it).
    cna_directx3_test(cna_test_freedirect_graphics_capability freedirect_graphics_capability_test.cpp)
    cna_register_renderer_test(NAME FreeDirect_GraphicsCapability COMMAND cna_test_freedirect_graphics_capability
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=dummy" LABELS "FREEDIRECT")

    cna_directx3_test(cna_test_freedirect_unsupported_3d_behavior
                 ${CNA_GRAPHICS_EXAMPLES_DIR}/unsupported_3d_call_behavior_test.cpp)
    cna_register_renderer_test(NAME FreeDirect_Unsupported3DBehavior
        COMMAND cna_test_freedirect_unsupported_3d_behavior
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=dummy" LABELS "FREEDIRECT")

    # plans/plan_freedirect.md Phase X7 (DX3-68): logical/window coordinate transform CTest. Note:
    # SDL_VIDEODRIVER=dummy reports a fixed 1024x768 "window" size (no real display to query), so
    # this test's letterbox-invariant checks run against that fixed size rather than whatever a
    # real display would report -- still a genuine, non-trivial letterbox (1024x768 vs 64x64
    # logical) and still verified correct, just no longer dependent on the host's actual screen.
    cna_directx3_test(cna_test_freedirect_logical_transform freedirect_logical_transform_test.cpp)
    cna_register_renderer_test(NAME FreeDirect_LogicalTransform COMMAND cna_test_freedirect_logical_transform
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=dummy" LABELS "FREEDIRECT")

    # REMED-GFX-127: every public Texture2D::GetData call must return the resource's real content or
    # reject the request deterministically -- never fabricate one. Pre-fix the shared render-target
    # fallback zero-initialized its own scratch buffer, handed it to ITextureRenderer::GetData (whose
    # interface default did nothing) and converted it for the caller regardless, so a renderer with no
    # readback returned a complete, uniformly transparent-black frame that satisfied both "the
    # destination was overwritten" and any transparent-black content expectation.
    cna_directx3_test(cna_test_freedirect_texture2d_getdata_contract
                 ${CNA_GRAPHICS_EXAMPLES_DIR}/texture2d_getdata_contract_test.cpp)
    cna_register_renderer_test(NAME FreeDirect_Texture2D_GetDataContract COMMAND cna_test_freedirect_texture2d_getdata_contract
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=dummy" LABELS "FREEDIRECT")

    # REMED-GFX-149 (and REMED-GFX-128, the same expression): the exact XNA-compatible
    # startIndex/elementCount contract of every public Texture2D::GetData overload. startIndex is a
    # DESTINATION element offset and elementCount is the destination capacity available from it;
    # the whole-level overload applied startIndex to the SOURCE, rejected any non-zero offset on a
    # render target, rejected legal excess capacity, and silently returned a partial frame for an
    # undersized one.
    cna_directx3_test(cna_test_freedirect_texture2d_getdata_transfer_range
                 ${CNA_GRAPHICS_EXAMPLES_DIR}/texture2d_getdata_transfer_range_test.cpp)
    cna_register_renderer_test(NAME FreeDirect_Texture2D_GetDataTransferRange COMMAND cna_test_freedirect_texture2d_getdata_transfer_range
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=dummy" LABELS "FREEDIRECT")

    # REMED-GFX-130: the TextureCube/Texture3D half of REMED-GFX-127's finding. DirectDraw is 2D
    # only, so this renderer creates neither resource and both public readbacks must reject rather
    # than convert the shared layer's own zeroed scratch buffer into a complete face/volume.
    cna_directx3_test(cna_test_freedirect_cube_volume_getdata_contract
                 ${CNA_GRAPHICS_EXAMPLES_DIR}/texturecube_texture3d_getdata_contract_test.cpp)
    cna_register_renderer_test(NAME FreeDirect_CubeVolume_GetDataContract COMMAND cna_test_freedirect_cube_volume_getdata_contract
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=dummy" LABELS "FREEDIRECT")

    # REMED-GFX-134: `RenderTargetCube::GetData` must return the face that was really rendered or
    # reject the request deterministically. REMED-GFX-130 made the reporting honest but left the
    # readback itself implemented on only two renderers, so a rendered cube face had no byte-exact
    # public oracle anywhere else. Drawn geometry (never Clear -- see REMED-GFX-129) paints an
    # asymmetric five-region pattern whose colours rotate per face, so a flip, a rotation, a wrong
    # array layer/subresource, a stale face or an unresolved multisample surface all fail.
    cna_directx3_test(cna_test_freedirect_rendertargetcube_getdata_contract
                 ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertargetcube_getdata_contract_test.cpp)
    cna_register_renderer_test(NAME FreeDirect_RenderTargetCube_GetDataContract COMMAND cna_test_freedirect_rendertargetcube_getdata_contract
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=dummy" LABELS "FREEDIRECT")

    # REMED-GFX-136: IGraphicsRenderer::CreateRenderTargetCube had no `preserveContents` parameter,
    # unlike CreateRenderTarget2D, so a RenderTargetCube's real RenderTargetUsage never reached the
    # renderer and Vulkan/WebGPU discarded a PreserveContents cube face on every bind cycle. Reuses
    # REMED-GFX-134's asymmetric face producer, then rebinds and draws only a small marker: "the
    # marker landed" is what a discarding renderer also achieves, so it can never pass for
    # preservation.
    cna_directx3_test(cna_test_freedirect_rendertargetcube_usage
                 ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertargetcube_usage_test.cpp)
    cna_register_renderer_test(NAME FreeDirect_RenderTargetCube_Usage COMMAND cna_test_freedirect_rendertargetcube_usage
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=dummy" LABELS "FREEDIRECT")

    # REMED-GFX-141: a MULTISAMPLED RenderTargetCube shared ONE multisample colour attachment
    # across all six faces on EasyGL, Vulkan and SdlGpu, so a PreserveContents face reloaded whichever
    # face was rendered last instead of its own samples. Renders face A, then face B, then rebinds A for
    # a small partial update -- with no readback, Present or flush in between -- and reads the whole of
    # A: the resolved single-sample layer still held the correct A, which is why a full redraw and an
    # immediate read both passed before this.
    cna_directx3_test(cna_test_freedirect_rendertargetcube_msaa_face
                 ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertargetcube_msaa_face_test.cpp)
    cna_register_renderer_test(NAME FreeDirect_RenderTargetCube_MsaaFace COMMAND cna_test_freedirect_rendertargetcube_msaa_face
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=dummy" LABELS "FREEDIRECT")

    # REMED-GFX-142: RenderTargetUsage's DEPTH and STENCIL half. FNA3D's own header documents
    # `preserveTargetContents` as storing the "color/depth/stencil" contents, and FNA's DiscardContents
    # bind clears all three (Target|DepthBuffer|Stencil, MaxDepth, 0) -- so the enum governs depth and
    # stencil, not colour alone. Renders an occluder, unbinds, rebinds WITHOUT clearing and draws behind
    # it: three bands separate preserved depth from cleared depth, lost colour and a second pass that
    # never drew. A parallel stencil stamp/gate sequence does the same for stencil.
    cna_directx3_test(cna_test_freedirect_rendertarget_depthstencil_usage
                 ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_depthstencil_usage_test.cpp)
    cna_register_renderer_test(NAME FreeDirect_RenderTarget_DepthStencilUsage COMMAND cna_test_freedirect_rendertarget_depthstencil_usage
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=dummy" LABELS "FREEDIRECT")

    # REMED-GFX-140: every public render-target bind/unbind cycle must be its own logical pass.
    # `VulkanRenderer::RecordCommandBuffer` collected ONE render pass per unique render-target
    # source per flush and replayed every queued batch for it inside that pass, so two bind cycles of
    # one target within a single flush window shared one load action. The decisive checks all use
    # DiscardContents -- collapsing is invisible on a preserving target, which is why REMED-GFX-136
    # needed an artificial readback barrier to see it -- and nothing between two cycles ever forces a
    # flush.
    cna_directx3_test(cna_test_freedirect_rendertarget_pass_boundary
                 ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_pass_boundary_test.cpp)
    cna_register_renderer_test(NAME FreeDirect_RenderTarget_PassBoundary COMMAND cna_test_freedirect_rendertarget_pass_boundary
        TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=dummy" LABELS "FREEDIRECT")

    # REMED-GFX-143: backbuffer work and render-target work must replay in ONE ordered stream.
    # REMED-GFX-140 (Vulkan) and REMED-GFX-145 (SdlGpu) gave every render-target bind cycle its own
    # native pass in public order, but both kept the BACKBUFFER as one trailing pass, so every
    # backbuffer draw of a frame was replayed after every target pass regardless of when it was
    # issued. A frame that samples a target onto the backbuffer and then renders into that target
    # again therefore saw the FINAL content. Every check queues its whole public sequence with no
    # Present, GetData, flush or extra frame between the cycles, and only then reads once.
    cna_directx3_test(cna_test_freedirect_backbuffer_pass_order
                 ${CNA_GRAPHICS_EXAMPLES_DIR}/backbuffer_pass_order_test.cpp)
    cna_register_renderer_test(NAME FreeDirect_Backbuffer_PassOrder COMMAND cna_test_freedirect_backbuffer_pass_order
        TIMEOUT 90 ENVIRONMENT "SDL_VIDEODRIVER=dummy" LABELS "FREEDIRECT")

    # REMED-GFX-135: the WRITE half of the same finding. `TextureCube::SetData`/`Texture3D::SetData`
    # kept the pre-REMED-GFX-127 shape -- a `void` renderer method behind `if (renderer_)` -- so an
    # upload that stored nothing, or only part of the requested region, still returned normally.
    cna_directx3_test(cna_test_freedirect_cube_volume_setdata_contract
                 ${CNA_GRAPHICS_EXAMPLES_DIR}/texturecube_texture3d_setdata_contract_test.cpp)
    cna_register_renderer_test(NAME FreeDirect_CubeVolume_SetDataContract COMMAND cna_test_freedirect_cube_volume_setdata_contract
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=dummy" LABELS "FREEDIRECT")
endif()

# Module-local tests get the same headless-safe skip convention the root one-shot
# gives root-registered tests (see cmake/TestHelpers.cmake).
cna_apply_skip_convention()
