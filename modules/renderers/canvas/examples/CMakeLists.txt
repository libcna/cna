# canvas renderer example/test registration (module-local; this file is the
# former cmake/Tests/CanvasTests.cmake, moved when examples moved into their owning
# physical modules).
#
# From the root CMakeLists.txt include site:
# plans/plan_canvas.md CANVAS-15: structural smoke-test build for the CANVAS (HTML Canvas 2D) renderer.
# Deliberately NOT registered as a CTest (mirrors cna_diag_software's/directx12_swapchain_diag's own
# "real executable, not a ctest" precedent above): SDL_Init(SDL_INIT_VIDEO) itself throws under
# `node CnaTests.js` (no real browser DOM -- confirmed empirically, "window is not defined"),
# before any Canvas-specific code even runs. This target only proves the smoke test configures and
# links here; it needs a real browser (e.g. `emrun`) against a page with a real <canvas> element to
# actually produce a PASS/FAIL (Design decision 9).

# Example/test executables historically live at the build root (scripts, docs and
# the golden-image working-directory conventions rely on it); keep them there.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
# Shared example-support headers ("common/PixelTestGame.hpp", ...) are owned by
# the graphics module's examples directory.
include_directories(${CNA_GRAPHICS_EXAMPLES_DIR})

if(CNA_BUILD_TESTS AND CNA_GRAPHICS_RENDERER STREQUAL "CANVAS")
    add_executable(cna_test_canvas_smoke canvas_smoke_test.cpp)
    target_link_libraries(cna_test_canvas_smoke PRIVATE CNA ${RENDERER_TARGET} SDL3::SDL3)

    # CNA::GraphicsCapability: Canvas is 2D-only -- SupportsCapability() reports which
    # capabilities are genuinely absent (ThreeD and everything that depends on it). Twin of
    # cna_test_sdl_graphics_capability/cna_test_freedirect_graphics_capability.
    add_executable(cna_test_canvas_graphics_capability canvas_graphics_capability_test.cpp)
    target_link_libraries(cna_test_canvas_graphics_capability PRIVATE CNA SDL3::SDL3)

    # REMED-GFX-127: every public Texture2D::GetData call must return the resource's real content or
    # reject the request deterministically -- never fabricate one. Built (not ctest-registered, like
    # every Canvas test: this renderer produces a wasm module that needs a browser), so the readback
    # contract at least stays compile-verified on the Emscripten toolchain.
    add_executable(cna_test_canvas_texture2d_getdata_contract ${CNA_GRAPHICS_EXAMPLES_DIR}/texture2d_getdata_contract_test.cpp)
    target_link_libraries(cna_test_canvas_texture2d_getdata_contract PRIVATE CNA SDL3::SDL3)

    # REMED-GFX-149 (and REMED-GFX-128, the same expression): the exact XNA-compatible
    # startIndex/elementCount contract of every public Texture2D::GetData overload. startIndex is a
    # DESTINATION element offset and elementCount is the destination capacity available from it;
    # the whole-level overload applied startIndex to the SOURCE, rejected any non-zero offset on a
    # render target, rejected legal excess capacity, and silently returned a partial frame for an
    # undersized one.
    add_executable(cna_test_canvas_texture2d_getdata_transfer_range ${CNA_GRAPHICS_EXAMPLES_DIR}/texture2d_getdata_transfer_range_test.cpp)
    target_link_libraries(cna_test_canvas_texture2d_getdata_transfer_range PRIVATE CNA SDL3::SDL3)

    # REMED-GFX-130: the TextureCube/Texture3D half of the same finding. Built (not ctest-registered,
    # like every Canvas test) so this renderer's deterministic cube/volume rejection stays
    # compile-verified on the Emscripten toolchain.
    add_executable(cna_test_canvas_cube_volume_getdata_contract
                   ${CNA_GRAPHICS_EXAMPLES_DIR}/texturecube_texture3d_getdata_contract_test.cpp)
    target_link_libraries(cna_test_canvas_cube_volume_getdata_contract PRIVATE CNA SDL3::SDL3)

    # REMED-GFX-134: `RenderTargetCube::GetData` must return the face that was really rendered or
    # reject the request deterministically. REMED-GFX-130 made the reporting honest but left the
    # readback itself implemented on only two renderers, so a rendered cube face had no byte-exact
    # public oracle anywhere else. Drawn geometry (never Clear -- see REMED-GFX-129) paints an
    # asymmetric five-region pattern whose colours rotate per face, so a flip, a rotation, a wrong
    # array layer/subresource, a stale face or an unresolved multisample surface all fail.
    # Compile-verified on the Emscripten toolchain only, like every other Canvas entry.
    add_executable(cna_test_canvas_rendertargetcube_getdata_contract
                   ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertargetcube_getdata_contract_test.cpp)
    target_link_libraries(cna_test_canvas_rendertargetcube_getdata_contract PRIVATE CNA SDL3::SDL3)

    # REMED-GFX-136: IGraphicsRenderer::CreateRenderTargetCube had no `preserveContents` parameter,
    # unlike CreateRenderTarget2D, so a RenderTargetCube's real RenderTargetUsage never reached the
    # renderer and Vulkan/WebGPU discarded a PreserveContents cube face on every bind cycle. Reuses
    # REMED-GFX-134's asymmetric face producer, then rebinds and draws only a small marker: "the
    # marker landed" is what a discarding renderer also achieves, so it can never pass for
    # preservation.
    add_executable(cna_test_canvas_rendertargetcube_usage
                   ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertargetcube_usage_test.cpp)
    target_link_libraries(cna_test_canvas_rendertargetcube_usage PRIVATE CNA SDL3::SDL3)

    # REMED-GFX-141: a MULTISAMPLED RenderTargetCube shared ONE multisample colour attachment
    # across all six faces on EasyGL, Vulkan and SdlGpu, so a PreserveContents face reloaded whichever
    # face was rendered last instead of its own samples. Renders face A, then face B, then rebinds A for
    # a small partial update -- with no readback, Present or flush in between -- and reads the whole of
    # A: the resolved single-sample layer still held the correct A, which is why a full redraw and an
    # immediate read both passed before this.
    # Built (not ctest-registered, like every Canvas test) so this renderer's own "no cube target"
    # declaration stays compile-verified on the Emscripten toolchain.
    add_executable(cna_test_canvas_rendertargetcube_msaa_face
                   ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertargetcube_msaa_face_test.cpp)
    target_link_libraries(cna_test_canvas_rendertargetcube_msaa_face PRIVATE CNA SDL3::SDL3)

    # REMED-GFX-142: RenderTargetUsage's DEPTH and STENCIL half. FNA3D's own header documents
    # `preserveTargetContents` as storing the "color/depth/stencil" contents, and FNA's DiscardContents
    # bind clears all three (Target|DepthBuffer|Stencil, MaxDepth, 0) -- so the enum governs depth and
    # stencil, not colour alone. Renders an occluder, unbinds, rebinds WITHOUT clearing and draws behind
    # it: three bands separate preserved depth from cleared depth, lost colour and a second pass that
    # never drew. A parallel stencil stamp/gate sequence does the same for stencil.
    add_executable(cna_test_canvas_rendertarget_depthstencil_usage
                   ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_depthstencil_usage_test.cpp)
    target_link_libraries(cna_test_canvas_rendertarget_depthstencil_usage PRIVATE CNA SDL3::SDL3)

    # REMED-GFX-140: every public render-target bind/unbind cycle must be its own logical pass.
    # `VulkanRenderer::RecordCommandBuffer` collected ONE render pass per unique render-target
    # source per flush and replayed every queued batch for it inside that pass, so two bind cycles of
    # one target within a single flush window shared one load action. The decisive checks all use
    # DiscardContents -- collapsing is invisible on a preserving target, which is why REMED-GFX-136
    # needed an artificial readback barrier to see it -- and nothing between two cycles ever forces a
    # flush.
    add_executable(cna_test_canvas_rendertarget_pass_boundary
                   ${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_pass_boundary_test.cpp)
    target_link_libraries(cna_test_canvas_rendertarget_pass_boundary PRIVATE CNA SDL3::SDL3)

    # REMED-GFX-143: backbuffer work and render-target work must replay in ONE ordered stream.
    # REMED-GFX-140 (Vulkan) and REMED-GFX-145 (SdlGpu) gave every render-target bind cycle its own
    # native pass in public order, but both kept the BACKBUFFER as one trailing pass, so every
    # backbuffer draw of a frame was replayed after every target pass regardless of when it was
    # issued. A frame that samples a target onto the backbuffer and then renders into that target
    # again therefore saw the FINAL content. Every check queues its whole public sequence with no
    # Present, GetData, flush or extra frame between the cycles, and only then reads once.
    add_executable(cna_test_canvas_backbuffer_pass_order
                   ${CNA_GRAPHICS_EXAMPLES_DIR}/backbuffer_pass_order_test.cpp)
    target_link_libraries(cna_test_canvas_backbuffer_pass_order PRIVATE CNA SDL3::SDL3)

    # REMED-GFX-135: the WRITE half of the same finding. `TextureCube::SetData`/`Texture3D::SetData`
    # kept the pre-REMED-GFX-127 shape -- a `void` renderer method behind `if (renderer_)` -- so an
    # upload that stored nothing, or only part of the requested region, still returned normally.
    add_executable(cna_test_canvas_cube_volume_setdata_contract
                   ${CNA_GRAPHICS_EXAMPLES_DIR}/texturecube_texture3d_setdata_contract_test.cpp)
    target_link_libraries(cna_test_canvas_cube_volume_setdata_contract PRIVATE CNA SDL3::SDL3)
    add_executable(cna_test_canvas_unsupported_3d_behavior
                   ${CNA_GRAPHICS_EXAMPLES_DIR}/unsupported_3d_call_behavior_test.cpp)
    target_link_libraries(cna_test_canvas_unsupported_3d_behavior
                          PRIVATE CNA SDL3::SDL3)
endif()

# Module-local tests get the same headless-safe skip convention the root one-shot
# gives root-registered tests (see cmake/TestHelpers.cmake).
cna_apply_skip_convention()
