# magnum renderer example/test registration (module-local; this file is the
# former cmake/Tests/MagnumTests.cmake, moved when examples moved into their owning
# physical modules).
#
# From the root CMakeLists.txt include site:
# plan_magnum.md: Magnum (mosra/magnum) renderer integration tests.

# Example/test executables historically live at the build root (scripts, docs and
# the golden-image working-directory conventions rely on it); keep them there.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
# Shared example-support headers ("common/PixelTestGame.hpp", ...) are owned by
# the graphics module's examples directory.
include_directories(${CNA_GRAPHICS_EXAMPLES_DIR})

# plan_magnum.md MAGNUM-41: MAGNUM renderer integration tests. Needs a real windowing system and a
# real OpenGL driver, so like the EASYGL/VULKAN blocks these run against CNA_TEST_DISPLAY (a live
# X server, or an Xvfb one with Mesa's software rasterizer behind it).
if(CNA_BUILD_TESTS AND NOT EMSCRIPTEN AND CNA_GRAPHICS_RENDERER STREQUAL "MAGNUM")
    enable_testing()

    macro(cna_magnum_test target src)
        add_executable(${target} ${src})
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            # CNA and the renderer target call into each other (GraphicsDevice constructs the
            # renderer; the renderer's factory lives in the renderer library), so they are linked as
            # one group rather than in a single pass -- the same shape every other renderer's own
            # test macro uses here.
            target_link_libraries(${target} PRIVATE
                CNA
                SHARP_RUNTIME SDL3::SDL3)
        else()
            target_link_libraries(${target} PRIVATE CNA ${RENDERER_TARGET} SHARP_RUNTIME SDL3::SDL3)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(${target} PRIVATE SDL3::SDL3main)
        endif()
    endmacro()

    cna_magnum_test(cna_test_magnum_smoke magnum_smoke_test.cpp)
    cna_register_renderer_test(NAME Magnum_Smoke COMMAND cna_test_magnum_smoke
        TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}"
        LABELS "GraphicsSmoke;Magnum")

    # plan_magnum.md MAGNUM-50: DualTextureEffect's overbright factor and second-layer
    # participation, both of which a 0/1-saturated scene cannot detect.
    cna_magnum_test(cna_test_magnum_dualtextureeffect magnum_dualtextureeffect_test.cpp)
    cna_register_renderer_test(NAME Magnum_DualTextureEffect
        COMMAND cna_test_magnum_dualtextureeffect
        TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}"
        LABELS "GraphicsSmoke;Magnum")

    # plan_magnum.md MAGNUM-51: EnvironmentMapEffect's reflection blend is a lerp, not an addition,
    # and the two only separate against a mid-tone cube map over a non-zero base.
    cna_magnum_test(cna_test_magnum_environmentmapeffect
                    magnum_environmentmapeffect_test.cpp)
    cna_register_renderer_test(NAME Magnum_EnvironmentMapEffect
        COMMAND cna_test_magnum_environmentmapeffect
        TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}"
        LABELS "GraphicsSmoke;Magnum")

    # plan_magnum.md MAGNUM-52: SkinnedEffect measured by where the geometry lands, which is what a
    # bone palette that never reaches the shader actually gets wrong.
    cna_magnum_test(cna_test_magnum_skinnedeffect magnum_skinnedeffect_test.cpp)
    cna_register_renderer_test(NAME Magnum_SkinnedEffect
        COMMAND cna_test_magnum_skinnedeffect
        TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}"
        LABELS "GraphicsSmoke;Magnum")

    # plan_magnum.md MAGNUM-53: PbrEffect's glTF metallic-roughness BRDF, on a rig where every dot
    # product is exactly 1 so each expected byte is derived from the formula, not captured.
    cna_magnum_test(cna_test_magnum_pbreffect magnum_pbreffect_test.cpp)
    cna_register_renderer_test(NAME Magnum_PbrEffect
        COMMAND cna_test_magnum_pbreffect
        TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}"
        LABELS "GraphicsSmoke;Magnum")

    # GLTF-343/344: IOR-derived dielectric F0 and factor-only KHR_materials_specular F0/F90,
    # repeated on both rigid and identity-skinned Magnum PBR programs.
    cna_magnum_test(cna_test_magnum_pbr_fresnel_factors
                    ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_pbr_fresnel_factors_test.cpp)
    cna_register_renderer_test(NAME Magnum_Pbr_FresnelFactors
        COMMAND cna_test_magnum_pbr_fresnel_factors
        TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}"
        LABELS "GraphicsSmoke;Magnum")

    # plan_magnum.md MAGNUM-57: a draw reusing a cached vertex array must still land where its own
    # base vertex and index offset say, not where the draw that populated the cache did.
    cna_magnum_test(cna_test_magnum_meshcache magnum_meshcache_test.cpp)
    cna_register_renderer_test(NAME Magnum_MeshCache
        COMMAND cna_test_magnum_meshcache
        TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}"
        LABELS "GraphicsSmoke;Magnum")

    # plan_magnum.md MAGNUM-56: a multisampled target must keep its multisample storage while it is
    # part of a multi-target set, which only a one-sample-per-pixel write can tell apart from a
    # resolved image.
    cna_magnum_test(cna_test_magnum_mrt_msaa magnum_mrt_msaa_test.cpp)
    cna_register_renderer_test(NAME Magnum_MrtMsaa
        COMMAND cna_test_magnum_mrt_msaa
        TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}"
        LABELS "GraphicsSmoke;Magnum")

    # plan_magnum.md MAGNUM-60: PreferPerPixelLighting picks between two shader families, which
    # only a specular highlight -- non-linear in a term that varies across the surface -- separates.
    cna_magnum_test(cna_test_magnum_pervertexlighting magnum_pervertexlighting_test.cpp)
    cna_register_renderer_test(NAME Magnum_PerVertexLighting
        COMMAND cna_test_magnum_pervertexlighting
        TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}"
        LABELS "GraphicsSmoke;Magnum")
endif()

# Module-local tests get the same headless-safe skip convention the root one-shot
# gives root-registered tests (see cmake/TestHelpers.cmake).
cna_apply_skip_convention()
