if(CNA_BUILD_TESTS AND CNA_GRAPHICS_BACKEND STREQUAL "METAL")
    enable_testing()

    add_executable(cna_test_metal_smoke examples/metal_smoke_test.cpp)
    target_link_libraries(cna_test_metal_smoke PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_test_metal_smoke PRIVATE SDL3::SDL3main)
    endif()

    cna_register_backend_test(NAME Metal_Smoke COMMAND cna_test_metal_smoke
        TIMEOUT 60 LABELS "Metal")

    # METAL-89: known-material probe-pixel checks for PbrEffect/SkinnedPbrEffect, real glTF
    # metallic-roughness BRDF shader. Reuses the shared, backend-agnostic EasyGL sources verbatim
    # (public XNA API only via examples/common/PixelTestGame.hpp -- no EasyGL/Vulkan-specific
    # headers), the same technique VulkanTests.cmake already uses for its own
    # Vulkan_PbrEffect_Golden/Vulkan_SkinnedPbrEffect_Golden tests, comparing against the same
    # checked-in golden PNGs with the same already-cross-backend-tolerant thresholds (20-35).
    # WORKING_DIRECTORY is the repo root since these sources reference their golden PNGs by a path
    # relative to it (examples/golden/...), not ctest's default CWD (the build directory) --
    # mirrors VulkanTests.cmake's/EasyGLTests.cmake's own identical requirement.
    add_executable(cna_test_metal_pbreffect_golden examples/easygl_pbreffect_golden_test.cpp)
    target_link_libraries(cna_test_metal_pbreffect_golden PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_test_metal_pbreffect_golden PRIVATE SDL3::SDL3main)
    endif()
    cna_register_backend_test(NAME Metal_PbrEffect_Golden COMMAND cna_test_metal_pbreffect_golden
        TIMEOUT 30 WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" LABELS "Metal")

    add_executable(cna_test_metal_skinnedpbreffect_golden examples/easygl_skinnedpbreffect_golden_test.cpp)
    target_link_libraries(cna_test_metal_skinnedpbreffect_golden PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_test_metal_skinnedpbreffect_golden PRIVATE SDL3::SDL3main)
    endif()
    cna_register_backend_test(NAME Metal_SkinnedPbrEffect_Golden COMMAND cna_test_metal_skinnedpbreffect_golden
        TIMEOUT 30 WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" LABELS "Metal")

    # plan_metal.md: minimal reduced-repro diagnostic (narrative item 72's own next step) --
    # Metal_PbrEffect_Golden/Metal_SkinnedPbrEffect_Golden's root cause remains undetermined after
    # ruling out every checkable-by-reading hypothesis (pipeline selection, viewport/cull/fill/
    # scissor, vertexStart, vertex descriptor layout, shader attribute indices, pixel format,
    # alpha-test discard). This reuses EasyGL's own existing, already-backend-agnostic
    # DrawUserPrimitives<VertexPositionColor> test verbatim (public XNA API only, no EasyGL-specific
    # includes) -- the simplest possible real 3D draw + same-process readback (PipelineKind::
    # Colored16, no lighting/texturing/PBR math at all), including its own vertexOffset=1 sub-test
    # (the exact class of offset item 68 fixed, in the simplest possible shader context). If this
    # ALSO fails identically (reads only the Clear color), that proves the remaining problem is
    # generic to "any real 3D draw + readback in this process" rather than PBR-specific; if it
    # passes, that narrows the remaining problem specifically to PBR's own shader/uniform path.
    add_executable(cna_test_metal_draw_user_primitives_vpc examples/easygl_draw_user_primitives_vpc_test.cpp)
    target_link_libraries(cna_test_metal_draw_user_primitives_vpc PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_test_metal_draw_user_primitives_vpc PRIVATE SDL3::SDL3main)
    endif()
    cna_register_backend_test(NAME Metal_DrawUserPrimitives_VPC COMMAND cna_test_metal_draw_user_primitives_vpc
        TIMEOUT 30 LABELS "Metal")

    # plan_metal.md Phase 14 (METAL-142-152): real runtime-compiled MSL custom ShaderEffect, wired
    # through SpriteBatch::Begin(effect). Mirrors D3D9_SpriteBatch_CustomEffect's own methodology
    # exactly (see examples/metal_spritebatch_customeffect_test.cpp's own header comment).
    add_executable(cna_test_metal_spritebatch_customeffect examples/metal_spritebatch_customeffect_test.cpp)
    target_link_libraries(cna_test_metal_spritebatch_customeffect PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_test_metal_spritebatch_customeffect PRIVATE SDL3::SDL3main)
    endif()
    cna_register_backend_test(NAME Metal_SpriteBatch_CustomEffect COMMAND cna_test_metal_spritebatch_customeffect
        TIMEOUT 30 LABELS "Metal")

    # plan_metal.md Phase 10 (METAL-112/113/118): real MRT. Reuses examples/easygl_mrt_test.cpp
    # verbatim -- public XNA API only (GraphicsDevice::SetRenderTargets(std::vector<
    # RenderTargetBinding>), no EasyGL-specific includes), the same reuse technique every other
    # Metal test in this file already uses. Its own final readback goes through
    # GetBackBufferData(), the same call already confirmed (items 67-76/82/84/85) to hit this
    # backend's own still-unresolved Clear-color-only readback bug -- expect this test to likely
    # fail for that same pre-existing reason even if the MRT binding/draw themselves are correct;
    # see plan_metal.md's own narrative for how to tell the two apart from the CI log.
    add_executable(cna_test_metal_mrt examples/easygl_mrt_test.cpp)
    target_link_libraries(cna_test_metal_mrt PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_test_metal_mrt PRIVATE SDL3::SDL3main)
    endif()
    cna_register_backend_test(NAME Metal_MRT COMMAND cna_test_metal_mrt
        TIMEOUT 30 LABELS "Metal")

    # plan_metal.md Phase 10 (METAL-104/105): real backbuffer MSAA. Reuses examples/
    # easygl_msaa_test.cpp verbatim (public XNA API only -- GraphicsDeviceManager.
    # PreferMultiSampling + SpriteBatch + GetBackBufferData, no EasyGL-specific includes). Its own
    # final readback goes through GetBackBufferData() -- expect the same pre-existing Clear-color-
    # only readback bug (items 67-76/82/84/85) to likely block this test too, independent of
    # whether the MSAA machinery itself (multisampled texture allocation, StoreAndMultisampleResolve,
    # per-sample-count pipeline cache key) actually works.
    add_executable(cna_test_metal_msaa examples/easygl_msaa_test.cpp)
    target_link_libraries(cna_test_metal_msaa PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_test_metal_msaa PRIVATE SDL3::SDL3main)
    endif()
    cna_register_backend_test(NAME Metal_MSAA COMMAND cna_test_metal_msaa
        TIMEOUT 30 LABELS "Metal")

    # plan_metal.md Phase 10 (METAL-104/105): real RenderTarget2D-level MSAA opt-in. Reuses
    # examples/easygl_rendertarget2d_msaa_test.cpp verbatim (public XNA API only) -- a genuinely
    # stronger test than Metal_MSAA above: it checks for real partially-covered (blended) pixels
    # along a diagonal triangle edge, a signature only a real multisample resolve can produce, not
    # just "solid colors survive the resolve unchanged" (differential: same triangle rendered into
    # a MultiSampleCount=0 RT and a MultiSampleCount=8 RT, both sampled 1:1 onto the backbuffer).
    # Its own final readback still goes through GetBackBufferData() (samples each RT onto the
    # backbuffer first, same as Metal_MSAA above), so it is equally exposed to the same
    # pre-existing readback bug -- not an independent, bug-immune code path.
    add_executable(cna_test_metal_rendertarget2d_msaa examples/easygl_rendertarget2d_msaa_test.cpp)
    target_link_libraries(cna_test_metal_rendertarget2d_msaa PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_test_metal_rendertarget2d_msaa PRIVATE SDL3::SDL3main)
    endif()
    cna_register_backend_test(NAME Metal_RenderTarget2D_MSAA COMMAND cna_test_metal_rendertarget2d_msaa
        TIMEOUT 30 LABELS "Metal")
endif()
