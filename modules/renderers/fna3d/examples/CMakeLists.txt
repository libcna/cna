# FNA3D renderer example/test registration (module-local, following the module-examples
# convention every other renderer family uses).
#
# The FNA3D driver order is SDL_GPU, Direct3D 11, OpenGL. Every test below pins
# FNA3D_FORCE_DRIVER=OpenGL so a CI machine with a partially functional Vulkan/SDL_GPU stack
# cannot silently change which driver the assertions were written against; a developer can still
# exercise another driver by setting the hint before running the binary directly.

# Example/test executables historically live at the build root (scripts, docs and
# the golden-image working-directory conventions rely on it); keep them there.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
# Shared example-support headers ("common/PixelTestGame.hpp", ...) are owned by
# the graphics module's examples directory.
include_directories(${CNA_GRAPHICS_EXAMPLES_DIR})

if(CNA_BUILD_TESTS AND CNA_GRAPHICS_RENDERER STREQUAL "FNA3D")
    enable_testing()

    macro(cna_fna3d_test target src)
        add_executable(${target} ${src})
        target_link_libraries(${target} PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
        if(TARGET SDL3::SDL3main)
            target_link_libraries(${target} PRIVATE SDL3::SDL3main)
        endif()
    endmacro()

    cna_fna3d_test(cna_test_fna3d_smoke fna3d_smoke_test.cpp)
    cna_register_renderer_test(NAME Fna3d_Smoke COMMAND cna_test_fna3d_smoke
        TIMEOUT 120 LABELS "Fna3d")

    cna_fna3d_test(cna_test_fna3d_2d fna3d_2d_test.cpp)
    cna_register_renderer_test(NAME Fna3d_2D COMMAND cna_test_fna3d_2d
        TIMEOUT 120 LABELS "Fna3d")

    cna_fna3d_test(cna_test_fna3d_3d fna3d_3d_test.cpp)
    cna_register_renderer_test(NAME Fna3d_3D COMMAND cna_test_fna3d_3d
        TIMEOUT 120 LABELS "Fna3d")

    cna_fna3d_test(cna_test_fna3d_rendertarget fna3d_rendertarget_test.cpp)
    cna_register_renderer_test(NAME Fna3d_RenderTarget COMMAND cna_test_fna3d_rendertarget
        TIMEOUT 120 LABELS "Fna3d")

    cna_fna3d_test(cna_test_fna3d_buffers fna3d_buffers_test.cpp)
    cna_register_renderer_test(NAME Fna3d_Buffers COMMAND cna_test_fna3d_buffers
        TIMEOUT 120 LABELS "Fna3d")

    cna_fna3d_test(cna_test_fna3d_rendertarget_advanced fna3d_rendertarget_advanced_test.cpp)
    cna_register_renderer_test(NAME Fna3d_RenderTarget_Advanced
        COMMAND cna_test_fna3d_rendertarget_advanced TIMEOUT 120 LABELS "Fna3d")

    cna_fna3d_test(cna_test_fna3d_lifetime fna3d_lifetime_test.cpp)
    cna_register_renderer_test(NAME Fna3d_Lifetime COMMAND cna_test_fna3d_lifetime
        TIMEOUT 120 LABELS "Fna3d")

    cna_fna3d_test(cna_test_fna3d_device_lifetime fna3d_device_lifetime_test.cpp)
    # This direct-renderer test pulls GraphicsDevice.cpp in through FNA3D without otherwise
    # referencing Input. On single-pass ELF linkers that exposes CNA's documented
    # graphics_core <-> input static-archive cycle, so re-scan just that cycle for this target.
    if(UNIX AND NOT APPLE AND NOT EMSCRIPTEN)
        target_link_libraries(cna_test_fna3d_device_lifetime PRIVATE
            "-Wl,--start-group" cna_input cna_graphics_core cna_renderer_fna3d
            "-Wl,--end-group")
    endif()
    cna_register_renderer_test(NAME Fna3d_Device_Lifetime
        COMMAND cna_test_fna3d_device_lifetime TIMEOUT 120 LABELS "Fna3d")

    cna_fna3d_test(cna_test_fna3d_sampler fna3d_sampler_test.cpp)
    cna_register_renderer_test(NAME Fna3d_Sampler COMMAND cna_test_fna3d_sampler
        TIMEOUT 120 LABELS "Fna3d")

    cna_fna3d_test(cna_test_fna3d_state fna3d_state_test.cpp)
    cna_register_renderer_test(NAME Fna3d_State COMMAND cna_test_fna3d_state
        TIMEOUT 120 LABELS "Fna3d")

    cna_fna3d_test(cna_test_fna3d_compressed fna3d_compressed_test.cpp)
    cna_register_renderer_test(NAME Fna3d_Compressed COMMAND cna_test_fna3d_compressed
        TIMEOUT 120 LABELS "Fna3d")

    # plans/plan_fna3d.md FNA3D-26: the same tools/xna-oracle/CnaOracleRender.cpp the D3D9, EasyGL,
    # OpenGLES1 and Skia branches already build, linked against this renderer, so the checked-in
    # scene corpus can be rendered through FNA3D and diffed against the real XNA 4.0 reference
    # images by scripts/run-oracle-corpus-diff-fna3d.sh. This corpus is the only thing in the tree
    # that drives DualTextureEffect, EnvironmentMapEffect, SkinnedEffect, the lighting variants and
    # fog through this renderer.
    cna_fna3d_test(cna_oracle_render_fna3d ${CMAKE_SOURCE_DIR}/tools/xna-oracle/CnaOracleRender.cpp)
    # plans/plan_fna3d.md FNA3D-27: registered as a real CTest, unlike the OpenGLES1 twin, because this
    # is the only permanent coverage DualTextureEffect, EnvironmentMapEffect and SkinnedEffect have
    # under this renderer. The script gates on a scene failing to RENDER, not on pixel equality --
    # the corpus carries a host-wide XNA-vs-GL divergence that is not renderer-specific, and
    # docs/fna3d-parity-report.md holds the per-scene numbers against the EasyGL baseline.
    cna_register_renderer_test(
        NAME Fna3d_XNA_Oracle
        COMMAND "${CMAKE_SOURCE_DIR}/scripts/run-oracle-corpus-diff-fna3d.sh"
                $<TARGET_FILE:cna_oracle_render_fna3d>
        TIMEOUT 600 LABELS "Fna3d")

    cna_fna3d_test(cna_test_fna3d_capabilities fna3d_capabilities_test.cpp)
    cna_register_renderer_test(NAME Fna3d_Capabilities COMMAND cna_test_fna3d_capabilities
        TIMEOUT 120 LABELS "Fna3d")

    # Keep the stated validation target executable, not merely documentary: every CTest in this
    # directory must exercise the OpenGL arm unless a developer runs a binary directly with a
    # different hint. The oracle script has the same default, but an explicit test property keeps
    # `ctest -L Fna3d` uniform and visible in `ctest -V`.
    get_property(_cna_fna3d_tests DIRECTORY PROPERTY TESTS)
    set_tests_properties(${_cna_fna3d_tests} PROPERTIES
        ENVIRONMENT "FNA3D_FORCE_DRIVER=OpenGL")
endif()

# Module-local tests get the same headless-safe skip convention the root one-shot
# gives root-registered tests (see cmake/TestHelpers.cmake).
cna_apply_skip_convention()
