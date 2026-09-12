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
    endif()
endif()

cna_apply_skip_convention()
