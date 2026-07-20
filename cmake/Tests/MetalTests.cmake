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
endif()
