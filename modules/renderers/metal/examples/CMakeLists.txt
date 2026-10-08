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
    # plans/plan_apple_m4.md AM4-144: the compiled-effect suite draws through a real device, so it
    # belongs to CnaTests only.
    list(FILTER CNA_METAL_PORTABLE_TEST_SOURCES EXCLUDE REGEX "MetalCompiledEffectTests\\.cpp$")
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
    # plans/plan_apple_m4.md AM4-102: the same 60 frames with the Objective-C runtime reporting every
    # object autoreleased while no pool is in place. A frame's drawable, command buffer, render
    # encoder or render-pass descriptor reported there is leaked for the life of the process (the
    # renderer is manual retain/release and the game loop has no pool of its own). AppKit's one-time
    # start-up allocations are not the renderer's and are not matched.
    cna_register_renderer_test(NAME Metal_Smoke_AutoreleasePools COMMAND cna_test_metal_smoke
        TIMEOUT 60 LABELS "Metal" ENVIRONMENT "OBJC_DEBUG_MISSING_POOLS=YES")
    set_tests_properties(Metal_Smoke_AutoreleasePools PROPERTIES FAIL_REGULAR_EXPRESSION
        "MISSING POOLS.* of class (CAMetalDrawable|MTLRenderPassDescriptorInternal|[A-Za-z0-9_]*CommandBuffer|[A-Za-z0-9_]*RenderContext|[A-Za-z0-9_]*BlitContext) ")

    # One runtime assertion per current GraphicsCapability. Unsupported features are asserted
    # false rather than left to IGraphicsRenderer's permissive default.
    add_executable(cna_test_metal_capabilities metal_capabilities_test.cpp)
    target_link_libraries(cna_test_metal_capabilities PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_test_metal_capabilities PRIVATE SDL3::SDL3main)
    endif()
    cna_register_renderer_test(NAME Metal_Capabilities COMMAND cna_test_metal_capabilities
        TIMEOUT 30 LABELS "Metal")

    # plans/plan_apple_m4.md AM4-077: SpriteBatch-scoped MSL custom effects, with Metal's API and
    # shader validation layers on so a binding or shader diagnostic fails the run too.
    add_executable(cna_test_metal_spritebatch_customeffect metal_spritebatch_customeffect_test.cpp)
    target_link_libraries(cna_test_metal_spritebatch_customeffect PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_test_metal_spritebatch_customeffect PRIVATE SDL3::SDL3main)
    endif()
    # plans/plan_apple_m4.md AM4-080: declarations carrying only what the BasicEffect permutation reads.
    add_executable(cna_test_metal_untextured_declaration metal_untextured_declaration_test.cpp)
    target_link_libraries(cna_test_metal_untextured_declaration PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_test_metal_untextured_declaration PRIVATE SDL3::SDL3main)
    endif()
    cna_register_renderer_test(NAME Metal_UntexturedDeclaration
        COMMAND cna_test_metal_untextured_declaration
        TIMEOUT 60 LABELS "Metal"
        ENVIRONMENT "MTL_DEBUG_LAYER=1;MTL_SHADER_VALIDATION=1")
    # plans/plan_apple_m4.md AM4-084: glTF COLOR_0 in the metallic-roughness product.
    add_executable(cna_test_metal_pbr_vertex_colour metal_pbr_vertex_colour_test.cpp)
    target_link_libraries(cna_test_metal_pbr_vertex_colour PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_test_metal_pbr_vertex_colour PRIVATE SDL3::SDL3main)
    endif()
    cna_register_renderer_test(NAME Metal_PbrVertexColour
        COMMAND cna_test_metal_pbr_vertex_colour
        TIMEOUT 60 LABELS "Metal"
        ENVIRONMENT "MTL_DEBUG_LAYER=1;MTL_SHADER_VALIDATION=1")
    # plans/plan_apple_m4.md AM4-085: the per-map coordinate set and KHR_materials_specular's maps.
    add_executable(cna_test_metal_pbr_dual_uv_specular metal_pbr_dual_uv_specular_test.cpp)
    target_link_libraries(cna_test_metal_pbr_dual_uv_specular PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_test_metal_pbr_dual_uv_specular PRIVATE SDL3::SDL3main)
    endif()
    cna_register_renderer_test(NAME Metal_PbrDualUvSpecular
        COMMAND cna_test_metal_pbr_dual_uv_specular
        TIMEOUT 60 LABELS "Metal"
        ENVIRONMENT "MTL_DEBUG_LAYER=1;MTL_SHADER_VALIDATION=1")

    cna_register_renderer_test(NAME Metal_SpriteBatch_CustomEffect
        COMMAND cna_test_metal_spritebatch_customeffect
        TIMEOUT 60 LABELS "Metal"
        ENVIRONMENT "MTL_DEBUG_LAYER=1;MTL_SHADER_VALIDATION=1")

    # plans/plan_apple_m4.md AM4-097: the shared MRT contract (SOFTWARE-120). Its multisampled
    # scenarios run since AM4-141 gave Metal MSAA.
    add_executable(cna_test_metal_mrt_stock_effect_contract
        ${CMAKE_SOURCE_DIR}/modules/graphics/examples/mrt_stock_effect_contract_test.cpp)
    target_link_libraries(cna_test_metal_mrt_stock_effect_contract PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_test_metal_mrt_stock_effect_contract PRIVATE SDL3::SDL3main)
    endif()
    cna_register_renderer_test(NAME Metal_MRT_StockEffectContract
        COMMAND cna_test_metal_mrt_stock_effect_contract
        TIMEOUT 60 LABELS "Metal"
        ENVIRONMENT "MTL_DEBUG_LAYER=1;MTL_SHADER_VALIDATION=1")

    # plans/plan_apple_m4.md AM4-026: the shared, renderer-neutral pixel and contract tests from
    # modules/graphics/examples now run against Metal too; until backbuffer readback worked
    # (AM4-025) none of them could observe a pixel. Each declares Metal's contract in its own
    # renderer table, so a feature Metal reports unsupported (MSAA and RenderTargetCube upload; MRT
    # and custom effects were refusals until AM4-097 and AM4-077) is asserted as a refusal, not
    # assumed. Not registered: the two
    # cross_renderer_* dump tools (no pass/fail of their own, as on EasyGL). The instancing fixture
    # joined with Metal's instancing (AM4-143).
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
        basic_effect_lighting_contract_test
        basic_effect_test
        bound_target_lifetime_test
        colorspace_midtone_contract_test
        deferred_scissor_capture_test
        deferred_source_lifetime_test
        deferred_viewport_capture_test
        depth_bias_magnitude_contract_test
        descriptor_capacity_contract_test
        dualtexture_slot_sampler_contract_test
        dualtextureeffect_vertexcolor_test
        dxt1_texture_test
        dxt_texturecube_test
        environmentmapeffect_alphascaledlerp_test
        envmap_cube_sampler_contract_test
        envmap_cube_sampler_state_test
        environment_map_effect_contract_test
        frontface_winding_test
        fullscreen_spritebatch_then_3d_test
        gfx077_colorwritechannels_3d_test
        graphicsdevice_clear_depth_test
        graphicsdevice_default_state_occlusion_test
        graphicsdevice_depth_contract_test
        graphicsdevice_ordered_clear_test
        instanced_textured_draw_test
        msaa_fragment_contract_test
        occlusion_query_test
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
        rendertarget_surface_format_contract_test
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
        spritebatch_subpixel_contract_test
        spritebatch_viewport_switch_test
        stock_effect_fog_contract_test
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
    # plans/plan_apple_m4.md AM4-134: the renderer-neutral depth-bias fixture, reused in place as the
    # DirectX parity registrations do. Its coplanar constant-bias leg cannot see a wrong scale on
    # Apple GPUs (any negative bias wins an equal LESS compare there); depth_bias_magnitude_contract_test
    # in the shared list below is the one that does.
    cna_metal_test(cna_test_metal_depth_bias
        ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_depth_bias_test.cpp)
    cna_register_renderer_test(NAME Metal_DepthBias COMMAND cna_test_metal_depth_bias
        TIMEOUT 120 LABELS "Metal")
    # plans/plan_apple_m4.md AM4-142: the three packed 16-bit formats sampled in a real draw -- a
    # readback is served from Texture2D's CPU copy and cannot see a wrong channel order, and the
    # 4444 one is rotated into ABGR4Unorm on upload. Each leg is gated on the renderer's own
    # verdict, so a GPU without the formats refuses them by name rather than failing.
    cna_metal_test(cna_test_metal_packed16_format
        ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_packed16_format_test.cpp)
    cna_register_renderer_test(NAME Metal_Packed16Format COMMAND cna_test_metal_packed16_format
        TIMEOUT 120 LABELS "Metal")
    # plans/plan_apple_m4.md AM4-141: backbuffer MSAA, its MultiSampleMask and its readback; every
    # Metal GPU multisamples at 4x.
    cna_metal_test(cna_test_metal_presentation_msaa_contract
        ${CNA_GRAPHICS_EXAMPLES_DIR}/presentation_msaa_contract_test.cpp)
    cna_register_renderer_test(NAME Metal_PresentationMsaaContract
        COMMAND cna_test_metal_presentation_msaa_contract 4 TIMEOUT 120 LABELS "Metal")
    foreach(_cna_metal_shared IN LISTS CNA_METAL_SHARED_GRAPHICS_TESTS)
        cna_metal_test(cna_test_metal_${_cna_metal_shared}
            ${CNA_GRAPHICS_EXAMPLES_DIR}/${_cna_metal_shared}.cpp)
        cna_register_renderer_test(NAME Metal_Shared_${_cna_metal_shared}
            COMMAND cna_test_metal_${_cna_metal_shared}
            TIMEOUT 120 LABELS "Metal;MetalShared")
    endforeach()
    # plans/plan_apple_m4.md AM4-141: the MSAA MultiSampleMask leg uses only all-or-nothing masks,
    # which Metal applies exactly.
    target_compile_definitions(cna_test_metal_gfx077_colorwritechannels_3d_test PRIVATE GFX077_MULTISAMPLEMASK_SUPPORTED)
    unset(_cna_metal_shared)
endif()

# Module-local tests get the same headless-safe skip convention the root one-shot
# gives root-registered tests (see cmake/TestHelpers.cmake).
cna_apply_skip_convention()
