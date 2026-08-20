# plans/plan_runtimerenderer.md RTR-P9-21: membership, not equality -- these standalone targets should
# build whenever WICKED is compiled in, including a multi-renderer build where it is not the default.
if(CNA_BUILD_TESTS AND NOT EMSCRIPTEN AND NOT WIN32
   AND "WICKED" IN_LIST CNA_RENDERER_IDENTITIES)

    enable_testing()

    macro(cna_wicked_test target src)
        add_executable(${target} ${src})
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(${target} PRIVATE
                CNA
                SHARP_RUNTIME)
        else()
            target_link_libraries(${target} PRIVATE CNA SHARP_RUNTIME)
        endif()
        target_link_libraries(${target} PRIVATE gtest gtest_main)
        if(TARGET SDL3::SDL3main)
            target_link_libraries(${target} PRIVATE SDL3::SDL3main)
        endif()
        # wi::shadercompiler dlopens "./libdxcompiler.so" relative to the CURRENT WORKING
        # DIRECTORY, so every executable that constructs the renderer needs it beside the binary and
        # must be run from there (docs/wicked-renderer.md).
        if(CNA_WICKED_DXCOMPILER)
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CNA_WICKED_DXCOMPILER}" "$<TARGET_FILE_DIR:${target}>"
                VERBATIM)
        endif()
    endmacro()

    macro(cna_wicked_game_test target src)
        add_executable(${target} ${src})
        target_include_directories(${target} PRIVATE
            ${CMAKE_SOURCE_DIR}/modules/graphics/examples)
        target_link_libraries(${target} PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
        if(TARGET SDL3::SDL3main)
            target_link_libraries(${target} PRIVATE SDL3::SDL3main)
        endif()
        if(CNA_WICKED_DXCOMPILER)
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CNA_WICKED_DXCOMPILER}" "$<TARGET_FILE_DIR:${target}>"
                VERBATIM)
        endif()
    endmacro()

    # plans/plan_wicked.md WICKED-70. Device-independent: the pipeline cache key is the one piece of this
    # renderer that can be tested without a GPU, and also the piece where a mistake is silent -- a
    # key that compares or hashes two different render states as equal makes the cache return a
    # pipeline built for the wrong state, which renders with no error at all.
    cna_wicked_test(cna_test_wicked_pipeline_key
        modules/renderers/wicked/tests/CNA/Internal/Renderers/Wicked/WickedPipelineKeyTest.cpp)
    add_test(NAME Wicked_PipelineKey COMMAND cna_test_wicked_pipeline_key)
    set_tests_properties(Wicked_PipelineKey PROPERTIES LABELS "Unit;Wicked" TIMEOUT 60)

    # plans/plan_wicked.md WICKED-78. The regression signal is the process surviving plain device
    # lifecycles: before the teardown fix, the first bare create/destroy aborted the binary inside
    # Wicked's VMA ("Some allocations were not freed"), which is also why the shared corpus could
    # not run under this renderer at all. Needs a real device, window and display, like the smoke
    # test below.
    cna_wicked_test(cna_test_wicked_device_lifecycle
        modules/renderers/wicked/tests/CNA/Internal/Renderers/Wicked/WickedDeviceLifecycleTest.cpp)
    add_test(NAME Wicked_DeviceLifecycle COMMAND cna_test_wicked_device_lifecycle)
    set_tests_properties(Wicked_DeviceLifecycle PROPERTIES LABELS "GraphicsSmoke;Wicked"
        TIMEOUT 120 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plans/plan_wicked.md WICKED-77. The instanced route carries the geometry stream's whole public
    # VertexOffset in the stream table (the ordinary routes fold it into baseVertex), and this
    # renderer dropped it at bind time -- an offset-selected record rendered nothing while the
    # identical bytes drew correctly through the ordinary indexed route.
    cna_wicked_test(cna_test_wicked_geometry_offset
        modules/renderers/wicked/tests/CNA/Internal/Renderers/Wicked/WickedGeometryVertexOffsetTest.cpp)
    add_test(NAME Wicked_GeometryVertexOffset COMMAND cna_test_wicked_geometry_offset)
    set_tests_properties(Wicked_GeometryVertexOffset PROPERTIES LABELS "GraphicsSmoke;Wicked"
        TIMEOUT 120 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plans/plan_wicked.md WICKED-80. Byte-exact staged-transfer matrix: narrow/aligned/boundary
    # Texture3D volumes, sub-box upload and readback, repeated readbacks, and the WICKED-79
    # Texture2D, TextureCube-face and small-mip controls that ride the same staging arithmetic.
    # Before cmake/patches/wicked-staging-footprint.patch the upstream device under-allocated
    # every narrow staging buffer and this matrix corrupts (preserved probe evidence in
    # cmake-build-wicked/wicked-repro/); with the patch it must stay green byte-for-byte.
    cna_wicked_test(cna_test_wicked_texture3d_staged_transfer
        modules/renderers/wicked/tests/CNA/Internal/Renderers/Wicked/WickedTexture3DStagedTransferTest.cpp)
    add_test(NAME Wicked_Texture3DStagedTransfer COMMAND cna_test_wicked_texture3d_staged_transfer)
    set_tests_properties(Wicked_Texture3DStagedTransfer PROPERTIES LABELS "GraphicsSmoke;Wicked"
        TIMEOUT 120 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # GLTF-213: exact mid-grey transfer witnesses on Wicked's rigid and skinned PBR pipelines.
    cna_wicked_game_test(cna_test_wicked_pbr_srgb_transfer
        modules/renderers/easygl/examples/easygl_pbr_srgb_transfer_test.cpp)
    add_test(NAME Wicked_Pbr_SrgbTransfer COMMAND cna_test_wicked_pbr_srgb_transfer)
    set_tests_properties(Wicked_Pbr_SrgbTransfer PROPERTIES LABELS "GraphicsSmoke;Wicked"
        TIMEOUT 120 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # GLTF-343/344: dielectric F0/F90 witnesses on Wicked's rigid and skinned PBR pipelines.
    cna_wicked_game_test(cna_test_wicked_pbr_fresnel_factors
        modules/renderers/easygl/examples/easygl_pbr_fresnel_factors_test.cpp)
    add_test(NAME Wicked_Pbr_FresnelFactors COMMAND cna_test_wicked_pbr_fresnel_factors)
    set_tests_properties(Wicked_Pbr_FresnelFactors PROPERTIES LABELS "GraphicsSmoke;Wicked"
        TIMEOUT 120 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plans/plan_wicked.md WICKED-74. Everything else in this renderer needs a real Vulkan device, a
    # window and a display, so it is covered by the same demo smoke test the other GPU renderers
    # register. It is registered only when the examples are built, and skips rather than fails when
    # no display is available.
    if(TARGET cna_demo_2d)
        cna_register_renderer_test(NAME Wicked_Demo2D_SmokeTest COMMAND cna_demo_2d --smoke 3
            TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}"
            LABELS "GraphicsSmoke;Wicked")
    endif()
endif()
