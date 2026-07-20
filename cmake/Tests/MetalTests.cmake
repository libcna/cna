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
endif()
