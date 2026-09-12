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

    if(CNA_BUILD_TESTS)
        enable_testing()
        cna_register_renderer_test(NAME Rlgl_Smoke COMMAND cna_test_rlgl_smoke
            TIMEOUT 60 LABELS "GraphicsSmoke;Rlgl"
            ENVIRONMENT "SDL_VIDEODRIVER=offscreen;LIBGL_ALWAYS_SOFTWARE=1")
        cna_register_renderer_test(NAME Rlgl_Texture COMMAND cna_test_rlgl_texture
            TIMEOUT 60 LABELS "GraphicsTexture;Rlgl"
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
        cna_register_renderer_test(NAME Rlgl_Buffer COMMAND cna_test_rlgl_buffer
            TIMEOUT 60 LABELS "GraphicsBuffer;Rlgl"
            ENVIRONMENT "SDL_VIDEODRIVER=offscreen;LIBGL_ALWAYS_SOFTWARE=1")
        cna_register_renderer_test(NAME Rlgl_Primitive COMMAND cna_test_rlgl_primitive
            TIMEOUT 60 LABELS "Graphics3D;Rlgl"
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
    endif()
endif()

cna_apply_skip_convention()
