# metal renderer example/test registration (module-local; this file is the
# former cmake/Tests/MetalTests.cmake, moved when examples moved into their owning
# physical modules).
#
# From the root CMakeLists.txt include site:
# Native Apple Metal renderer smoke test.

# Example/test executables historically live at the build root (scripts, docs and
# the golden-image working-directory conventions rely on it); keep them there.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
# Shared example-support headers ("common/PixelTestGame.hpp", ...) are owned by
# the graphics module's examples directory.
include_directories(${CNA_GRAPHICS_EXAMPLES_DIR})

if(CNA_BUILD_TESTS)
    # The Metal policy, format, stream, matrix, uniform-fill, and interface-shape tests are plain
    # C++ and intentionally stay independent of Objective-C++ and Apple frameworks.  Keep them in
    # a small aggregate executable as well as the repository-wide CnaTests binary so non-Apple and
    # sanitizer jobs can validate the complete portable contract without building unrelated
    # process harnesses.
    file(GLOB CNA_METAL_PORTABLE_TEST_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/../tests/CNA/Internal/Renderers/Metal/*.cpp")
    add_executable(cna_test_metal_portable ${CNA_METAL_PORTABLE_TEST_SOURCES})
    target_link_libraries(cna_test_metal_portable PRIVATE CNA SHARP_RUNTIME gtest_main)
    # plans/plan_x11.md X11-0090: this suite is the portable half of the Metal tests -- it
    # compiles the policy headers on every host and constructs no device at all. SDL3 was a
    # transitive habit rather than a dependency, so the edge is kept where SDL is configured and
    # omitted where it is not.
    if(TARGET SDL3::SDL3)
        target_link_libraries(cna_test_metal_portable PRIVATE SDL3::SDL3)
    endif()
    # The portable policy suite includes the metal module's policy headers on every
    # platform; they ride the module's unconditional header-interface target.
    target_link_libraries(cna_test_metal_portable PRIVATE cna_renderer_metal_headers)
    cna_register_renderer_test(NAME Metal_PortableHelpers COMMAND cna_test_metal_portable
        TIMEOUT 30 LABELS "Metal;Portable")
endif()

if(CNA_BUILD_TESTS AND CNA_GRAPHICS_RENDERER STREQUAL "METAL")
    enable_testing()

    # Configure-time control for Metal's real graphics-core<->renderer static-archive cycle. Native
    # test executables below use ordinary target links, not a test-only --start-group workaround.
    get_target_property(CNA_METAL_BACKEND_LINKS ${RENDERER_TARGET} LINK_LIBRARIES)
    if(NOT "cna_graphics_core" IN_LIST CNA_METAL_BACKEND_LINKS)
        message(FATAL_ERROR
            "METAL renderer must retain its reverse graphics-core link for out-of-line Effect symbols")
    endif()

    # Supported-contract smoke: real macOS device/window/view, buffer uploads, combined
    # color/depth/stencil clears, and presentation. It deliberately performs no backbuffer
    # readback; that path is a documented deterministic NotSupported boundary.
    add_executable(cna_test_metal_smoke metal_smoke_test.cpp)
    target_link_libraries(cna_test_metal_smoke PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_test_metal_smoke PRIVATE SDL3::SDL3main)
    endif()
    cna_register_renderer_test(NAME Metal_Smoke COMMAND cna_test_metal_smoke
        TIMEOUT 60 LABELS "Metal")

    # One runtime assertion per current GraphicsCapability. Unsupported features are asserted
    # false rather than left to IGraphicsRenderer's permissive default.
    add_executable(cna_test_metal_capabilities metal_capabilities_test.cpp)
    target_link_libraries(cna_test_metal_capabilities PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_test_metal_capabilities PRIVATE SDL3::SDL3main)
    endif()
    cna_register_renderer_test(NAME Metal_Capabilities COMMAND cna_test_metal_capabilities
        TIMEOUT 30 LABELS "Metal")

    # plans/plan_apple_m4.md AM4-026: the shared, renderer-neutral pixel and contract tests from
    # modules/graphics/examples now run against Metal too; until backbuffer readback worked
    # (AM4-025) none of them could observe a pixel. Each declares Metal's contract in its own
    # renderer table, so a feature Metal reports unsupported (MSAA, MRT, RenderTargetCube upload,
    # custom effects) is asserted as a refusal, not assumed. Not registered: the two
    # cross_renderer_* dump tools (no pass/fail of their own, as on EasyGL) and the instancing and
    # occlusion-query fixtures, whose features Metal reports false (docs/metal-renderer.md).
    macro(cna_metal_test target src)
        add_executable(${target} ${src})
        target_link_libraries(${target} PRIVATE CNA SHARP_RUNTIME)
        if(TARGET SDL3::SDL3)
            target_link_libraries(${target} PRIVATE SDL3::SDL3)
        else()
            target_compile_definitions(${target} PRIVATE CNA_EXAMPLES_NO_SDL)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(${target} PRIVATE SDL3::SDL3main)
        endif()
    endmacro()
    set(CNA_METAL_SHARED_GRAPHICS_TESTS
        additive_blend_contract_test
        alpha_test_effect_test
        alpha_test_integration_test
        backbuffer_first_read_test
        backbuffer_headless_reject_test
        backbuffer_pass_order_test
        backbuffer_readback_dimension_test
        basic_effect_test
        bound_target_lifetime_test
        colorspace_midtone_contract_test
        deferred_scissor_capture_test
        deferred_source_lifetime_test
        deferred_viewport_capture_test
        descriptor_capacity_contract_test
        dualtexture_slot_sampler_contract_test
        dualtextureeffect_vertexcolor_test
        dxt1_texture_test
        dxt_texturecube_test
        environmentmapeffect_alphascaledlerp_test
        envmap_cube_sampler_contract_test
        envmap_cube_sampler_state_test
        frontface_winding_test
        fullscreen_spritebatch_then_3d_test
        graphicsdevice_clear_depth_test
        graphicsdevice_default_state_occlusion_test
        graphicsdevice_depth_contract_test
        graphicsdevice_ordered_clear_test
        point_sampling_contract_test
        present_lifecycle_contract_test
        rasterizerstate_cullmode_camera_test
        rasterizerstate_cullmode_indexed_basiceffect_test
        rendertarget2d_depth_test
        rendertarget2d_golden_test
        rendertarget_backbuffer_consumer_test
        rendertarget_depthstencil_usage_test
        rendertarget_effect_source_test
        rendertarget_first_use_test
        rendertarget_invalid_mip_level_test
        rendertarget_msaa_depth_contract_test
        rendertarget_msaa_first_readback_test
        rendertarget_msaa_mip_readback_test
        rendertarget_pass_boundary_test
        rendertarget_preserve_across_resize_test
        rendertarget_producer_consumer_test
        rendertarget_sampling_orientation_test
        rendertarget_viewport_scissor_reset_test
        rendertargetcube_getdata_contract_test
        rendertargetcube_msaa_face_test
        rendertargetcube_plural_binding_test
        rendertargetcube_usage_test
        sampler_component_isolation_contract_test
        skinned_effect_integration_test
        skinned_effect_test
        skinnedeffect_lighting_conformance_test
        source_rectangle_orientation_test
        sprite_font_test
        spritebatch_3d_order_test
        spritebatch_begin_rasterizerstate_scissor_test
        spritebatch_custom_viewport_rt_test
        spritebatch_custom_viewport_test
        spritebatch_depthstencil_state_test
        spritebatch_presentation_test
        spritebatch_sampler0_publication_test
        spritebatch_sort_mode_semantics_test
        spritebatch_viewport_switch_test
        stock_effect_sampler_contract_test
        texture2d_getdata_contract_test
        texture2d_getdata_transfer_range_test
        texture2d_saveas_rendertarget_test
        texture_filter_mip_contract_test
        texture_filter_ordinal_contract_test
        texturecube_texture3d_getdata_contract_test
        texturecube_texture3d_setdata_contract_test
        triangle_strip_winding_test
        viewport_reset_after_resize_test
        xna_pixel_center_contract_test
        zero_length_buffer_test
    )
    foreach(_cna_metal_shared IN LISTS CNA_METAL_SHARED_GRAPHICS_TESTS)
        cna_metal_test(cna_test_metal_${_cna_metal_shared}
            ${CNA_GRAPHICS_EXAMPLES_DIR}/${_cna_metal_shared}.cpp)
        cna_register_renderer_test(NAME Metal_Shared_${_cna_metal_shared}
            COMMAND cna_test_metal_${_cna_metal_shared}
            TIMEOUT 120 LABELS "Metal;MetalShared")
    endforeach()
    unset(_cna_metal_shared)
endif()

# Module-local tests get the same headless-safe skip convention the root one-shot
# gives root-registered tests (see cmake/TestHelpers.cmake).
cna_apply_skip_convention()
