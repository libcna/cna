# plans/plan_rlgl.md RLGL-007: runtime vertical-slice validation needs a real window and GL 3.3
# context. The executable is available even when CNA_BUILD_TESTS is off so dependency-constrained
# developer builds can run it directly under Xvfb; CTest registration remains opt-in with the
# repository's ordinary test switch.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
include_directories(${CNA_GRAPHICS_EXAMPLES_DIR})

if(NOT EMSCRIPTEN AND CNA_GRAPHICS_RENDERER STREQUAL "RLGL" AND TARGET SDL3::SDL3)
    add_executable(cna_test_rlgl_smoke rlgl_smoke_test.cpp)
    target_link_libraries(cna_test_rlgl_smoke PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_test_rlgl_smoke PRIVATE SDL3::SDL3main)
    endif()

    add_executable(cna_test_rlgl_texture rlgl_texture_test.cpp)
    target_include_directories(cna_test_rlgl_texture PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")
    target_link_libraries(cna_test_rlgl_texture PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_test_rlgl_texture PRIVATE SDL3::SDL3main)
    endif()

    add_executable(cna_test_rlgl_texture_cube rlgl_texture_cube_test.cpp)
    target_include_directories(cna_test_rlgl_texture_cube PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")
    target_link_libraries(cna_test_rlgl_texture_cube PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_test_rlgl_texture_cube PRIVATE SDL3::SDL3main)
    endif()

    add_executable(cna_test_rlgl_texture_cube_oracle
        "${CMAKE_CURRENT_SOURCE_DIR}/../../opengl4/examples/opengl4_texturecube_test.cpp")
    target_link_libraries(cna_test_rlgl_texture_cube_oracle
        PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_test_rlgl_texture_cube_oracle PRIVATE SDL3::SDL3main)
    endif()

    add_executable(cna_test_rlgl_sampler rlgl_sampler_test.cpp)
    target_include_directories(cna_test_rlgl_sampler PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")
    target_link_libraries(cna_test_rlgl_sampler PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_test_rlgl_sampler PRIVATE SDL3::SDL3main)
    endif()

    add_executable(cna_test_rlgl_state rlgl_state_test.cpp)
    target_include_directories(cna_test_rlgl_state PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")
    target_link_libraries(cna_test_rlgl_state PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_test_rlgl_state PRIVATE SDL3::SDL3main)
    endif()

    add_executable(cna_test_rlgl_spritebatch rlgl_spritebatch_test.cpp)
    target_link_libraries(cna_test_rlgl_spritebatch PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_test_rlgl_spritebatch PRIVATE SDL3::SDL3main)
    endif()

    add_executable(cna_test_rlgl_render_target_orientation
        "${CMAKE_CURRENT_SOURCE_DIR}/../../easygl/examples/easygl_render_target_test.cpp")
    target_link_libraries(cna_test_rlgl_render_target_orientation
        PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_test_rlgl_render_target_orientation PRIVATE SDL3::SDL3main)
    endif()

    add_executable(cna_test_rlgl_render_target rlgl_render_target_test.cpp)
    target_include_directories(cna_test_rlgl_render_target
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")
    target_link_libraries(cna_test_rlgl_render_target PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_test_rlgl_render_target PRIVATE SDL3::SDL3main)
    endif()

    add_executable(cna_test_rlgl_render_target_formats rlgl_render_target_format_test.cpp)
    target_link_libraries(cna_test_rlgl_render_target_formats
        PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_test_rlgl_render_target_formats PRIVATE SDL3::SDL3main)
    endif()

    add_executable(cna_test_rlgl_mrt rlgl_mrt_test.cpp)
    target_include_directories(cna_test_rlgl_mrt
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")
    target_link_libraries(cna_test_rlgl_mrt PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_test_rlgl_mrt PRIVATE SDL3::SDL3main)
    endif()

    add_executable(cna_test_rlgl_render_target_full
        "${CMAKE_CURRENT_SOURCE_DIR}/../../opengl4/examples/opengl4_rendertarget2d_test.cpp")
    target_link_libraries(cna_test_rlgl_render_target_full
        PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_test_rlgl_render_target_full PRIVATE SDL3::SDL3main)
    endif()

    add_executable(cna_test_rlgl_render_target_cube rlgl_render_target_cube_test.cpp)
    target_include_directories(cna_test_rlgl_render_target_cube
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")
    target_link_libraries(cna_test_rlgl_render_target_cube
        PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_test_rlgl_render_target_cube PRIVATE SDL3::SDL3main)
    endif()

    set(RLGL_RENDER_TARGET_CUBE_CONTRACTS
        getdata_contract
        usage
        msaa_face
        plural_binding)
    foreach(contract IN LISTS RLGL_RENDER_TARGET_CUBE_CONTRACTS)
        add_executable(cna_test_rlgl_rendertargetcube_${contract}
            "${CNA_GRAPHICS_EXAMPLES_DIR}/rendertargetcube_${contract}_test.cpp")
        target_link_libraries(cna_test_rlgl_rendertargetcube_${contract}
            PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_test_rlgl_rendertargetcube_${contract}
                PRIVATE SDL3::SDL3main)
        endif()
    endforeach()
    target_compile_definitions(cna_test_rlgl_rendertargetcube_plural_binding
        PRIVATE CNA_GFX096_CUBE_GETDATA=1)

    add_executable(cna_test_rlgl_msaa_first_readback
        "${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_msaa_first_readback_test.cpp")
    target_link_libraries(cna_test_rlgl_msaa_first_readback
        PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_test_rlgl_msaa_first_readback PRIVATE SDL3::SDL3main)
    endif()

    add_executable(cna_test_rlgl_msaa_mip_readback
        "${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_msaa_mip_readback_test.cpp")
    target_link_libraries(cna_test_rlgl_msaa_mip_readback
        PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_test_rlgl_msaa_mip_readback PRIVATE SDL3::SDL3main)
    endif()

    add_executable(cna_test_rlgl_buffer rlgl_buffer_test.cpp)
    target_include_directories(cna_test_rlgl_buffer PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")
    target_link_libraries(cna_test_rlgl_buffer PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_test_rlgl_buffer PRIVATE SDL3::SDL3main)
    endif()

    add_executable(cna_test_rlgl_primitive rlgl_primitive_test.cpp)
    target_include_directories(cna_test_rlgl_primitive PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")
    target_link_libraries(cna_test_rlgl_primitive PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_test_rlgl_primitive PRIVATE SDL3::SDL3main)
    endif()

    add_executable(cna_test_rlgl_multi_stream
        "${CNA_GRAPHICS_EXAMPLES_DIR}/parity/parity_multi_stream_split.cpp")
    target_link_libraries(cna_test_rlgl_multi_stream PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_test_rlgl_multi_stream PRIVATE SDL3::SDL3main)
    endif()

    add_executable(cna_test_rlgl_instanced_parity
        "${CNA_GRAPHICS_EXAMPLES_DIR}/parity/parity_instanced_draw.cpp")
    target_link_libraries(cna_test_rlgl_instanced_parity PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_test_rlgl_instanced_parity PRIVATE SDL3::SDL3main)
    endif()

    add_executable(cna_test_rlgl_occlusion_query
        "${CNA_GRAPHICS_EXAMPLES_DIR}/occlusion_query_test.cpp")
    target_link_libraries(cna_test_rlgl_occlusion_query PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_test_rlgl_occlusion_query PRIVATE SDL3::SDL3main)
    endif()

    add_executable(cna_test_rlgl_occlusion_query_visible
        "${CMAKE_CURRENT_SOURCE_DIR}/../../easygl/examples/easygl_occlusion_query_visible_quad_test.cpp")
    target_link_libraries(cna_test_rlgl_occlusion_query_visible
        PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_test_rlgl_occlusion_query_visible PRIVATE SDL3::SDL3main)
    endif()

    add_executable(cna_test_rlgl_occlusion_query_occluded
        "${CMAKE_CURRENT_SOURCE_DIR}/../../easygl/examples/easygl_occlusion_query_occluded_quad_test.cpp")
    target_link_libraries(cna_test_rlgl_occlusion_query_occluded
        PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_test_rlgl_occlusion_query_occluded PRIVATE SDL3::SDL3main)
    endif()

    add_executable(cna_test_rlgl_occlusion_query_lifetime
        rlgl_occlusion_query_lifetime_test.cpp)
    target_link_libraries(cna_test_rlgl_occlusion_query_lifetime
        PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_test_rlgl_occlusion_query_lifetime PRIVATE SDL3::SDL3main)
    endif()

    add_executable(cna_test_rlgl_effect rlgl_effect_test.cpp)
    target_link_libraries(cna_test_rlgl_effect PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_test_rlgl_effect PRIVATE SDL3::SDL3main)
    endif()

    add_executable(cna_test_rlgl_xna_pixel_center
        "${CNA_GRAPHICS_EXAMPLES_DIR}/xna_pixel_center_contract_test.cpp")
    target_link_libraries(cna_test_rlgl_xna_pixel_center PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_test_rlgl_xna_pixel_center PRIVATE SDL3::SDL3main)
    endif()

    set(RLGL_BASIC_EFFECT_TESTS
        one_light
        multilight_emissive
        specular
        preferperpixellighting
        lit_vertex_color
        vertex_color_clamp
        world_scale_precision)
    foreach(test_name IN LISTS RLGL_BASIC_EFFECT_TESTS)
        add_executable(cna_test_rlgl_basiceffect_${test_name}
            "${CMAKE_CURRENT_SOURCE_DIR}/../../easygl/examples/easygl_basiceffect_${test_name}_test.cpp")
        target_link_libraries(cna_test_rlgl_basiceffect_${test_name}
            PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_test_rlgl_basiceffect_${test_name} PRIVATE SDL3::SDL3main)
        endif()
    endforeach()

    add_executable(cna_test_rlgl_basiceffect_lit_vertex_color_perpixel
        "${CNA_GRAPHICS_EXAMPLES_DIR}/basiceffect_lit_vertex_color_perpixel_test.cpp")
    target_link_libraries(cna_test_rlgl_basiceffect_lit_vertex_color_perpixel
        PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_test_rlgl_basiceffect_lit_vertex_color_perpixel
            PRIVATE SDL3::SDL3main)
    endif()

    add_executable(cna_test_rlgl_basiceffect_normal_transform
        "${CMAKE_CURRENT_SOURCE_DIR}/../../bgfx/examples/bgfx_basiceffect_normaltransform_test.cpp")
    target_link_libraries(cna_test_rlgl_basiceffect_normal_transform
        PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_test_rlgl_basiceffect_normal_transform PRIVATE SDL3::SDL3main)
    endif()

    add_executable(cna_test_rlgl_basiceffect_lighting rlgl_basic_effect_lighting_test.cpp)
    target_link_libraries(cna_test_rlgl_basiceffect_lighting
        PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_test_rlgl_basiceffect_lighting PRIVATE SDL3::SDL3main)
    endif()

    add_executable(cna_test_rlgl_dual_texture_effect rlgl_dual_texture_effect_test.cpp)
    target_link_libraries(cna_test_rlgl_dual_texture_effect
        PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_test_rlgl_dual_texture_effect PRIVATE SDL3::SDL3main)
    endif()

    add_executable(cna_test_rlgl_dual_texture_independent_uv
        "${CMAKE_CURRENT_SOURCE_DIR}/../../easygl/examples/easygl_dualtextureeffect_independent_uv_test.cpp")
    target_link_libraries(cna_test_rlgl_dual_texture_independent_uv
        PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_test_rlgl_dual_texture_independent_uv PRIVATE SDL3::SDL3main)
    endif()

    add_executable(cna_test_rlgl_environment_map
        "${CMAKE_CURRENT_SOURCE_DIR}/../../opengl4/examples/opengl4_environmentmapeffect_test.cpp")
    target_link_libraries(cna_test_rlgl_environment_map
        PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_test_rlgl_environment_map PRIVATE SDL3::SDL3main)
    endif()

    add_executable(cna_test_rlgl_environment_map_terms
        "${CNA_GRAPHICS_EXAMPLES_DIR}/parity/parity_env_map_terms.cpp")
    target_link_libraries(cna_test_rlgl_environment_map_terms
        PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_test_rlgl_environment_map_terms PRIVATE SDL3::SDL3main)
    endif()

    add_executable(cna_test_rlgl_environment_map_alpha_scaled_lerp
        "${CNA_GRAPHICS_EXAMPLES_DIR}/environmentmapeffect_alphascaledlerp_test.cpp")
    target_link_libraries(cna_test_rlgl_environment_map_alpha_scaled_lerp
        PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_test_rlgl_environment_map_alpha_scaled_lerp
            PRIVATE SDL3::SDL3main)
    endif()

    add_executable(cna_test_rlgl_environment_map_fog
        "${CMAKE_CURRENT_SOURCE_DIR}/../../easygl/examples/easygl_environmentmapeffect_fog_test.cpp")
    target_link_libraries(cna_test_rlgl_environment_map_fog
        PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_test_rlgl_environment_map_fog PRIVATE SDL3::SDL3main)
    endif()

    set(RLGL_ENVIRONMENT_MAP_EFFECT_TESTS
        golden
        amount_zero
        amount_one
        specular
        fresnel
        fresnel_gradient
        eyeposition
        worldtransform
        combined
        multilight)
    foreach(test_name IN LISTS RLGL_ENVIRONMENT_MAP_EFFECT_TESTS)
        add_executable(cna_test_rlgl_environmentmapeffect_${test_name}
            "${CMAKE_CURRENT_SOURCE_DIR}/../../easygl/examples/easygl_environmentmapeffect_${test_name}_test.cpp")
        target_link_libraries(cna_test_rlgl_environmentmapeffect_${test_name}
            PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_test_rlgl_environmentmapeffect_${test_name}
                PRIVATE SDL3::SDL3main)
        endif()
    endforeach()

    add_executable(cna_test_rlgl_environment_map_amount_clamp
        "${CNA_GRAPHICS_EXAMPLES_DIR}/environmentmapeffect_amount_clamp_test.cpp")
    target_link_libraries(cna_test_rlgl_environment_map_amount_clamp
        PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_test_rlgl_environment_map_amount_clamp
            PRIVATE SDL3::SDL3main)
    endif()

    add_executable(cna_test_rlgl_environment_map_sampler_contract
        "${CNA_GRAPHICS_EXAMPLES_DIR}/envmap_cube_sampler_contract_test.cpp")
    target_link_libraries(cna_test_rlgl_environment_map_sampler_contract
        PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_test_rlgl_environment_map_sampler_contract
            PRIVATE SDL3::SDL3main)
    endif()

    add_executable(cna_test_rlgl_environment_map_sampler_state
        "${CNA_GRAPHICS_EXAMPLES_DIR}/envmap_cube_sampler_state_test.cpp")
    target_link_libraries(cna_test_rlgl_environment_map_sampler_state
        PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_test_rlgl_environment_map_sampler_state
            PRIVATE SDL3::SDL3main)
    endif()

    add_executable(cna_test_rlgl_environment_map_render_target_cube
        "${CNA_GRAPHICS_EXAMPLES_DIR}/rendertarget_effect_source_test.cpp")
    target_link_libraries(cna_test_rlgl_environment_map_render_target_cube
        PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_test_rlgl_environment_map_render_target_cube
            PRIVATE SDL3::SDL3main)
    endif()

    add_executable(cna_test_rlgl_skinned_terms
        "${CNA_GRAPHICS_EXAMPLES_DIR}/parity/parity_skinned_terms.cpp")
    target_link_libraries(cna_test_rlgl_skinned_terms
        PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_test_rlgl_skinned_terms PRIVATE SDL3::SDL3main)
    endif()

    add_executable(cna_test_rlgl_skinned_effect rlgl_skinned_effect_test.cpp)
    target_include_directories(cna_test_rlgl_skinned_effect
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")
    target_link_libraries(cna_test_rlgl_skinned_effect
        PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_test_rlgl_skinned_effect PRIVATE SDL3::SDL3main)
    endif()

    set(RLGL_SKINNED_EFFECT_TESTS
        identity_bones
        translation_bone
        twobone_blend
        vector4_bone_indices
        weightspervertex
        multilight
        specular
        vertexcolor
        preferperpixellighting
        fog
        world_normal
        combined)
    foreach(test_name IN LISTS RLGL_SKINNED_EFFECT_TESTS)
        add_executable(cna_test_rlgl_skinnedeffect_${test_name}
            "${CMAKE_CURRENT_SOURCE_DIR}/../../easygl/examples/easygl_skinnedeffect_${test_name}_test.cpp")
        target_link_libraries(cna_test_rlgl_skinnedeffect_${test_name}
            PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_test_rlgl_skinnedeffect_${test_name}
                PRIVATE SDL3::SDL3main)
        endif()
    endforeach()

    if(CNA_RLGL_COMPILED_EFFECTS)
        add_executable(cna_test_rlgl_compiled_effect_runtime
            rlgl_compiled_effect_runtime_test.cpp)
        target_compile_definitions(cna_test_rlgl_compiled_effect_runtime
            PRIVATE CNA_RLGL_SOURCE_DIR="${CNA_SOURCE_DIR}")
        target_link_libraries(cna_test_rlgl_compiled_effect_runtime
            PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_test_rlgl_compiled_effect_runtime PRIVATE SDL3::SDL3main)
        endif()

        add_executable(cna_test_rlgl_compiled_effect_draw
            rlgl_compiled_effect_draw_test.cpp)
        target_include_directories(cna_test_rlgl_compiled_effect_draw
            PRIVATE "${CNA_SOURCE_DIR}/tests/support")
        target_link_libraries(cna_test_rlgl_compiled_effect_draw
            PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_test_rlgl_compiled_effect_draw PRIVATE SDL3::SDL3main)
        endif()
    endif()

    if(CNA_BUILD_TESTS)
        enable_testing()
        cna_register_renderer_test(NAME Rlgl_Smoke COMMAND cna_test_rlgl_smoke
            TIMEOUT 60 LABELS "GraphicsSmoke;Rlgl"
            ENVIRONMENT "SDL_VIDEODRIVER=offscreen;LIBGL_ALWAYS_SOFTWARE=1")
        cna_register_renderer_test(NAME Rlgl_Texture COMMAND cna_test_rlgl_texture
            TIMEOUT 60 LABELS "GraphicsTexture;Rlgl"
            ENVIRONMENT "SDL_VIDEODRIVER=offscreen;LIBGL_ALWAYS_SOFTWARE=1")
        cna_register_renderer_test(NAME Rlgl_TextureCube COMMAND cna_test_rlgl_texture_cube
            TIMEOUT 60 LABELS "GraphicsTexture;GraphicsParity;Rlgl"
            ENVIRONMENT "SDL_VIDEODRIVER=offscreen;LIBGL_ALWAYS_SOFTWARE=1")
        cna_register_renderer_test(
            NAME Rlgl_TextureCube_dxt_fallback
            COMMAND cna_test_rlgl_texture_cube
            TIMEOUT 60 LABELS "GraphicsTexture;GraphicsParity;Rlgl"
            ENVIRONMENT
                "SDL_VIDEODRIVER=offscreen;LIBGL_ALWAYS_SOFTWARE=1;CNA_RLGL_FORCE_DXT_FALLBACK=1")
        cna_register_renderer_test(
            NAME Rlgl_TextureCube_oracle
            COMMAND cna_test_rlgl_texture_cube_oracle
            TIMEOUT 60 LABELS "GraphicsTexture;GraphicsParity;Rlgl"
            ENVIRONMENT "SDL_VIDEODRIVER=offscreen;LIBGL_ALWAYS_SOFTWARE=1")
        cna_register_renderer_test(NAME Rlgl_Sampler COMMAND cna_test_rlgl_sampler
            TIMEOUT 60 LABELS "GraphicsSampler;Rlgl"
            ENVIRONMENT "SDL_VIDEODRIVER=offscreen;LIBGL_ALWAYS_SOFTWARE=1")
        cna_register_renderer_test(NAME Rlgl_State COMMAND cna_test_rlgl_state
            TIMEOUT 60 LABELS "GraphicsState;Rlgl"
            ENVIRONMENT "SDL_VIDEODRIVER=offscreen;LIBGL_ALWAYS_SOFTWARE=1")
        cna_register_renderer_test(NAME Rlgl_SpriteBatch COMMAND cna_test_rlgl_spritebatch
            TIMEOUT 60 LABELS "Graphics2D;Rlgl"
            ENVIRONMENT "SDL_VIDEODRIVER=offscreen;LIBGL_ALWAYS_SOFTWARE=1")
        cna_register_renderer_test(
            NAME Rlgl_RenderTarget_orientation
            COMMAND cna_test_rlgl_render_target_orientation
            TIMEOUT 60 LABELS "GraphicsRenderTarget;GraphicsParity;Rlgl"
            ENVIRONMENT "SDL_VIDEODRIVER=offscreen;LIBGL_ALWAYS_SOFTWARE=1")
        cna_register_renderer_test(
            NAME Rlgl_RenderTarget
            COMMAND cna_test_rlgl_render_target
            TIMEOUT 60 LABELS "GraphicsRenderTarget;Rlgl"
            ENVIRONMENT "SDL_VIDEODRIVER=offscreen;LIBGL_ALWAYS_SOFTWARE=1")
        cna_register_renderer_test(
            NAME Rlgl_RenderTarget_formats
            COMMAND cna_test_rlgl_render_target_formats
            TIMEOUT 60 LABELS "GraphicsRenderTarget;GraphicsParity;Rlgl"
            ENVIRONMENT "SDL_VIDEODRIVER=offscreen;LIBGL_ALWAYS_SOFTWARE=1")
        cna_register_renderer_test(
            NAME Rlgl_Mrt
            COMMAND cna_test_rlgl_mrt
            TIMEOUT 60 LABELS "GraphicsRenderTarget;GraphicsParity;Rlgl"
            ENVIRONMENT "SDL_VIDEODRIVER=offscreen;LIBGL_ALWAYS_SOFTWARE=1")
        cna_register_renderer_test(
            NAME Rlgl_RenderTarget_full
            COMMAND cna_test_rlgl_render_target_full
            TIMEOUT 60 LABELS "GraphicsRenderTarget;GraphicsParity;Rlgl"
            ENVIRONMENT "SDL_VIDEODRIVER=offscreen;LIBGL_ALWAYS_SOFTWARE=1")
        cna_register_renderer_test(
            NAME Rlgl_RenderTargetCube
            COMMAND cna_test_rlgl_render_target_cube
            TIMEOUT 60 LABELS "GraphicsRenderTarget;GraphicsParity;Rlgl"
            ENVIRONMENT "SDL_VIDEODRIVER=offscreen;LIBGL_ALWAYS_SOFTWARE=1")
        foreach(contract IN LISTS RLGL_RENDER_TARGET_CUBE_CONTRACTS)
            cna_register_renderer_test(
                NAME Rlgl_RenderTargetCube_${contract}
                COMMAND cna_test_rlgl_rendertargetcube_${contract}
                TIMEOUT 180 LABELS "GraphicsRenderTarget;GraphicsParity;Rlgl"
                ENVIRONMENT "SDL_VIDEODRIVER=offscreen;LIBGL_ALWAYS_SOFTWARE=1")
        endforeach()
        cna_register_renderer_test(
            NAME Rlgl_MsaaFirstReadback
            COMMAND cna_test_rlgl_msaa_first_readback
            TIMEOUT 900 LABELS "GraphicsRenderTarget;GraphicsParity;Rlgl"
            ENVIRONMENT "SDL_VIDEODRIVER=offscreen;LIBGL_ALWAYS_SOFTWARE=1")
        cna_register_renderer_test(
            NAME Rlgl_MsaaMipReadback
            COMMAND cna_test_rlgl_msaa_mip_readback
            TIMEOUT 1200 LABELS "GraphicsRenderTarget;GraphicsParity;Rlgl"
            ENVIRONMENT "SDL_VIDEODRIVER=offscreen;LIBGL_ALWAYS_SOFTWARE=1")
        cna_register_renderer_test(NAME Rlgl_Buffer COMMAND cna_test_rlgl_buffer
            TIMEOUT 60 LABELS "GraphicsBuffer;Rlgl"
            ENVIRONMENT "SDL_VIDEODRIVER=offscreen;LIBGL_ALWAYS_SOFTWARE=1")
        cna_register_renderer_test(NAME Rlgl_Primitive COMMAND cna_test_rlgl_primitive
            TIMEOUT 60 LABELS "Graphics3D;Rlgl"
            ENVIRONMENT "SDL_VIDEODRIVER=offscreen;LIBGL_ALWAYS_SOFTWARE=1")
        cna_register_renderer_test(NAME Rlgl_MultiStream COMMAND cna_test_rlgl_multi_stream
            TIMEOUT 60 LABELS "Graphics3D;GraphicsParity;Rlgl"
            ENVIRONMENT "SDL_VIDEODRIVER=offscreen;LIBGL_ALWAYS_SOFTWARE=1")
        cna_register_renderer_test(
            NAME Rlgl_InstancedParity
            COMMAND cna_test_rlgl_instanced_parity
            TIMEOUT 60 LABELS "Graphics3D;GraphicsParity;Rlgl"
            ENVIRONMENT "SDL_VIDEODRIVER=offscreen;LIBGL_ALWAYS_SOFTWARE=1")
        cna_register_renderer_test(
            NAME Rlgl_OcclusionQuery_Cycle
            COMMAND cna_test_rlgl_occlusion_query
            TIMEOUT 60 LABELS "Graphics3D;GraphicsQuery;Rlgl"
            ENVIRONMENT "SDL_VIDEODRIVER=offscreen;LIBGL_ALWAYS_SOFTWARE=1")
        cna_register_renderer_test(
            NAME Rlgl_OcclusionQuery_Visible
            COMMAND cna_test_rlgl_occlusion_query_visible
            TIMEOUT 60 LABELS "Graphics3D;GraphicsQuery;GraphicsParity;Rlgl"
            ENVIRONMENT "SDL_VIDEODRIVER=offscreen;LIBGL_ALWAYS_SOFTWARE=1")
        cna_register_renderer_test(
            NAME Rlgl_OcclusionQuery_Occluded
            COMMAND cna_test_rlgl_occlusion_query_occluded
            TIMEOUT 60 LABELS "Graphics3D;GraphicsQuery;GraphicsParity;Rlgl"
            ENVIRONMENT "SDL_VIDEODRIVER=offscreen;LIBGL_ALWAYS_SOFTWARE=1")
        cna_register_renderer_test(
            NAME Rlgl_OcclusionQuery_Lifetime
            COMMAND cna_test_rlgl_occlusion_query_lifetime
            TIMEOUT 60 LABELS "Graphics3D;GraphicsQuery;GraphicsLifetime;Rlgl"
            ENVIRONMENT "SDL_VIDEODRIVER=offscreen;LIBGL_ALWAYS_SOFTWARE=1")
        cna_register_renderer_test(NAME Rlgl_Effect COMMAND cna_test_rlgl_effect
            TIMEOUT 60 LABELS "Graphics3D;GraphicsEffect;Rlgl"
            ENVIRONMENT "SDL_VIDEODRIVER=offscreen;LIBGL_ALWAYS_SOFTWARE=1")
        cna_register_renderer_test(NAME Rlgl_XnaPixelCenter COMMAND cna_test_rlgl_xna_pixel_center
            TIMEOUT 60 LABELS "Graphics3D;GraphicsContract;Rlgl"
            ENVIRONMENT "SDL_VIDEODRIVER=offscreen;LIBGL_ALWAYS_SOFTWARE=1")
        foreach(test_name IN LISTS RLGL_BASIC_EFFECT_TESTS)
            cna_register_renderer_test(
                NAME Rlgl_BasicEffect_${test_name}
                COMMAND cna_test_rlgl_basiceffect_${test_name}
                TIMEOUT 60 LABELS "Graphics3D;GraphicsEffect;Rlgl"
                ENVIRONMENT "SDL_VIDEODRIVER=offscreen;LIBGL_ALWAYS_SOFTWARE=1")
        endforeach()
        cna_register_renderer_test(
            NAME Rlgl_BasicEffect_lit_vertex_color_perpixel
            COMMAND cna_test_rlgl_basiceffect_lit_vertex_color_perpixel
            TIMEOUT 60 LABELS "Graphics3D;GraphicsEffect;Rlgl"
            ENVIRONMENT "SDL_VIDEODRIVER=offscreen;LIBGL_ALWAYS_SOFTWARE=1")
        cna_register_renderer_test(
            NAME Rlgl_BasicEffect_normal_transform
            COMMAND cna_test_rlgl_basiceffect_normal_transform
            TIMEOUT 60 LABELS "Graphics3D;GraphicsEffect;Rlgl"
            ENVIRONMENT "SDL_VIDEODRIVER=offscreen;LIBGL_ALWAYS_SOFTWARE=1")
        cna_register_renderer_test(
            NAME Rlgl_BasicEffect_lighting
            COMMAND cna_test_rlgl_basiceffect_lighting
            TIMEOUT 60 LABELS "Graphics3D;GraphicsEffect;Rlgl"
            ENVIRONMENT "SDL_VIDEODRIVER=offscreen;LIBGL_ALWAYS_SOFTWARE=1")
        cna_register_renderer_test(
            NAME Rlgl_DualTextureEffect
            COMMAND cna_test_rlgl_dual_texture_effect
            TIMEOUT 60 LABELS "Graphics3D;GraphicsEffect;GraphicsSampler;Rlgl"
            ENVIRONMENT "SDL_VIDEODRIVER=offscreen;LIBGL_ALWAYS_SOFTWARE=1")
        cna_register_renderer_test(
            NAME Rlgl_DualTextureEffect_independent_uv
            COMMAND cna_test_rlgl_dual_texture_independent_uv
            TIMEOUT 60 LABELS "Graphics3D;GraphicsEffect;Rlgl"
            ENVIRONMENT "SDL_VIDEODRIVER=offscreen;LIBGL_ALWAYS_SOFTWARE=1")
        cna_register_renderer_test(
            NAME Rlgl_EnvironmentMapEffect
            COMMAND cna_test_rlgl_environment_map
            TIMEOUT 60 LABELS "Graphics3D;GraphicsEffect;GraphicsParity;Rlgl"
            ENVIRONMENT "SDL_VIDEODRIVER=offscreen;LIBGL_ALWAYS_SOFTWARE=1")
        cna_register_renderer_test(
            NAME Rlgl_EnvironmentMapEffect_terms
            COMMAND cna_test_rlgl_environment_map_terms
            TIMEOUT 60 LABELS "Graphics3D;GraphicsEffect;GraphicsParity;Rlgl"
            ENVIRONMENT "SDL_VIDEODRIVER=offscreen;LIBGL_ALWAYS_SOFTWARE=1")
        cna_register_renderer_test(
            NAME Rlgl_EnvironmentMapEffect_alpha_scaled_lerp
            COMMAND cna_test_rlgl_environment_map_alpha_scaled_lerp
            TIMEOUT 60 LABELS "Graphics3D;GraphicsEffect;GraphicsParity;Rlgl"
            ENVIRONMENT "SDL_VIDEODRIVER=offscreen;LIBGL_ALWAYS_SOFTWARE=1")
        cna_register_renderer_test(
            NAME Rlgl_EnvironmentMapEffect_fog
            COMMAND cna_test_rlgl_environment_map_fog
            TIMEOUT 60 LABELS "Graphics3D;GraphicsEffect;GraphicsParity;Rlgl"
            ENVIRONMENT "SDL_VIDEODRIVER=offscreen;LIBGL_ALWAYS_SOFTWARE=1")
        foreach(test_name IN LISTS RLGL_ENVIRONMENT_MAP_EFFECT_TESTS)
            cna_register_renderer_test(
                NAME Rlgl_EnvironmentMapEffect_${test_name}
                COMMAND cna_test_rlgl_environmentmapeffect_${test_name}
                TIMEOUT 60 LABELS "Graphics3D;GraphicsEffect;GraphicsParity;Rlgl"
                ENVIRONMENT "SDL_VIDEODRIVER=offscreen;LIBGL_ALWAYS_SOFTWARE=1")
        endforeach()
        cna_register_renderer_test(
            NAME Rlgl_EnvironmentMapEffect_amount_clamp
            COMMAND cna_test_rlgl_environment_map_amount_clamp
            TIMEOUT 60 LABELS "Graphics3D;GraphicsEffect;GraphicsParity;Rlgl"
            ENVIRONMENT "SDL_VIDEODRIVER=offscreen;LIBGL_ALWAYS_SOFTWARE=1")
        cna_register_renderer_test(
            NAME Rlgl_EnvironmentMapEffect_sampler_contract
            COMMAND cna_test_rlgl_environment_map_sampler_contract
            TIMEOUT 300 LABELS "Graphics3D;GraphicsEffect;GraphicsSampler;GraphicsParity;Rlgl"
            ENVIRONMENT "SDL_VIDEODRIVER=offscreen;LIBGL_ALWAYS_SOFTWARE=1")
        cna_register_renderer_test(
            NAME Rlgl_EnvironmentMapEffect_sampler_state
            COMMAND cna_test_rlgl_environment_map_sampler_state
            TIMEOUT 180 LABELS "Graphics3D;GraphicsEffect;GraphicsSampler;GraphicsParity;Rlgl"
            ENVIRONMENT "SDL_VIDEODRIVER=offscreen;LIBGL_ALWAYS_SOFTWARE=1")
        cna_register_renderer_test(
            NAME Rlgl_EnvironmentMapEffect_render_target_cube
            COMMAND cna_test_rlgl_environment_map_render_target_cube --leg=L1
            TIMEOUT 180 LABELS "Graphics3D;GraphicsEffect;GraphicsRenderTarget;GraphicsParity;Rlgl"
            ENVIRONMENT "SDL_VIDEODRIVER=offscreen;LIBGL_ALWAYS_SOFTWARE=1")
        cna_register_renderer_test(
            NAME Rlgl_SkinnedEffect_terms
            COMMAND cna_test_rlgl_skinned_terms
            TIMEOUT 60 LABELS "Graphics3D;GraphicsEffect;GraphicsParity;Rlgl"
            ENVIRONMENT "SDL_VIDEODRIVER=offscreen;LIBGL_ALWAYS_SOFTWARE=1")
        cna_register_renderer_test(
            NAME Rlgl_SkinnedEffect
            COMMAND cna_test_rlgl_skinned_effect
            TIMEOUT 60 LABELS "Graphics3D;GraphicsEffect;Rlgl"
            ENVIRONMENT "SDL_VIDEODRIVER=offscreen;LIBGL_ALWAYS_SOFTWARE=1")
        foreach(test_name IN LISTS RLGL_SKINNED_EFFECT_TESTS)
            cna_register_renderer_test(
                NAME Rlgl_SkinnedEffect_${test_name}
                COMMAND cna_test_rlgl_skinnedeffect_${test_name}
                TIMEOUT 60 LABELS "Graphics3D;GraphicsEffect;Rlgl"
                ENVIRONMENT "SDL_VIDEODRIVER=offscreen;LIBGL_ALWAYS_SOFTWARE=1")
        endforeach()
        if(CNA_RLGL_COMPILED_EFFECTS)
            cna_register_renderer_test(
                NAME Rlgl_CompiledEffect_runtime
                COMMAND cna_test_rlgl_compiled_effect_runtime
                TIMEOUT 120 LABELS "Graphics3D;GraphicsEffect;GraphicsParity;Rlgl"
                ENVIRONMENT "SDL_VIDEODRIVER=offscreen;LIBGL_ALWAYS_SOFTWARE=1")
            cna_register_renderer_test(
                NAME Rlgl_CompiledEffect_draw
                COMMAND cna_test_rlgl_compiled_effect_draw
                TIMEOUT 120 LABELS "Graphics3D;GraphicsEffect;GraphicsParity;Rlgl"
                ENVIRONMENT "SDL_VIDEODRIVER=offscreen;LIBGL_ALWAYS_SOFTWARE=1")
        endif()
    endif()
endif()

cna_apply_skip_convention()
