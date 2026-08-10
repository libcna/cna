# CNAEXT graphics-extension examples (module-local registration; the target definitions
# moved here verbatim from cmake/Examples.cmake when examples moved into their owning
# physical modules).

# Example/demo executables historically live at the build root; keep them there.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# ---- CNAEXT settings example (no window needed — pure API test) ---------------
if(CNA_BUILD_EXAMPLES AND CNA_CNAEXT)
    add_executable(cna_example_cnaext_settings cnaext_settings_example.cpp)
    target_link_libraries(cna_example_cnaext_settings PRIVATE CNA)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_example_cnaext_settings PRIVATE SDL3::SDL3main)
    endif()
    if(CNA_BUILD_TESTS)
        add_test(NAME CNAEXT_Settings_Compile_Run
                 COMMAND cna_example_cnaext_settings)
    endif()
endif()

# ---- DepthEffect manual verification demo (renders + screenshots each colour-depth mode) -----
if(CNA_BUILD_EXAMPLES AND CNA_CNAEXT AND (CNA_GRAPHICS_RENDERER STREQUAL "OPENGLES3" OR CNA_GRAPHICS_RENDERER STREQUAL "OPENGL33") AND NOT EMSCRIPTEN AND NOT ANDROID)
    add_executable(cna_depth_effect_demo depth_effect_demo_test.cpp)
    # The shared example-support headers ("common/ScreenshotEXT.hpp", ...) live with the
    # graphics module's examples.
    target_include_directories(cna_depth_effect_demo PRIVATE ${CNA_GRAPHICS_EXAMPLES_DIR})
    target_compile_definitions(cna_depth_effect_demo PRIVATE
        CNA_DEPTH_EFFECT_DEMO_CONTENT_ROOT="${CNA_GRAPHICS_EXAMPLES_DIR}/demo_2d/Content")
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
        target_link_libraries(cna_depth_effect_demo PRIVATE
            CNA SHARP_RUNTIME)
    else()
        target_link_libraries(cna_depth_effect_demo PRIVATE CNA SHARP_RUNTIME)
    endif()
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_depth_effect_demo PRIVATE SDL3::SDL3main)
    endif()
    if(WIN32)
        set_target_properties(cna_depth_effect_demo PROPERTIES WIN32_EXECUTABLE TRUE)
        cna_copy_sdl_runtime(cna_depth_effect_demo)
    endif()
endif()

# ---- CRTEffect manual verification demo (renders + screenshots each CRT parameter set) --------
if(CNA_BUILD_EXAMPLES AND CNA_CNAEXT AND (CNA_GRAPHICS_RENDERER STREQUAL "OPENGLES3" OR CNA_GRAPHICS_RENDERER STREQUAL "OPENGL33") AND NOT EMSCRIPTEN AND NOT ANDROID)
    add_executable(cna_crt_effect_demo crt_effect_demo_test.cpp)
    target_include_directories(cna_crt_effect_demo PRIVATE ${CNA_GRAPHICS_EXAMPLES_DIR})
    target_compile_definitions(cna_crt_effect_demo PRIVATE
        CNA_CRT_EFFECT_DEMO_CONTENT_ROOT="${CNA_GRAPHICS_EXAMPLES_DIR}/demo_2d/Content")
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
        target_link_libraries(cna_crt_effect_demo PRIVATE
            CNA SHARP_RUNTIME)
    else()
        target_link_libraries(cna_crt_effect_demo PRIVATE CNA SHARP_RUNTIME)
    endif()
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_crt_effect_demo PRIVATE SDL3::SDL3main)
    endif()
    if(WIN32)
        set_target_properties(cna_crt_effect_demo PROPERTIES WIN32_EXECUTABLE TRUE)
        cna_copy_sdl_runtime(cna_crt_effect_demo)
    endif()
endif()

# Module-local tests get the same headless-safe skip convention the root one-shot
# gives root-registered tests (see cmake/TestHelpers.cmake).
cna_apply_skip_convention()
