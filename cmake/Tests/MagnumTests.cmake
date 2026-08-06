# plan_magnum.md MAGNUM-41: MAGNUM backend integration tests. Needs a real windowing system and a
# real OpenGL driver, so like the EASYGL/VULKAN blocks these run against CNA_TEST_DISPLAY (a live
# X server, or an Xvfb one with Mesa's software rasterizer behind it).
if(CNA_BUILD_TESTS AND NOT EMSCRIPTEN AND CNA_GRAPHICS_BACKEND STREQUAL "MAGNUM")
    enable_testing()

    macro(cna_magnum_test target src)
        add_executable(${target} ${src})
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            # CNA and the backend target call into each other (GraphicsDevice constructs the
            # backend; the backend's factory lives in the backend library), so they are linked as
            # one group rather than in a single pass -- the same shape every other backend's own
            # test macro uses here.
            target_link_libraries(${target} PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME SDL3::SDL3)
        else()
            target_link_libraries(${target} PRIVATE CNA ${BACKEND_TARGET} SHARP_RUNTIME SDL3::SDL3)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(${target} PRIVATE SDL3::SDL3main)
        endif()
    endmacro()

    cna_magnum_test(cna_test_magnum_smoke examples/magnum_smoke_test.cpp)
    cna_register_backend_test(NAME Magnum_Smoke COMMAND cna_test_magnum_smoke
        TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}"
        LABELS "GraphicsSmoke;Magnum")

    # plan_magnum.md MAGNUM-50: DualTextureEffect's overbright factor and second-layer
    # participation, both of which a 0/1-saturated scene cannot detect.
    cna_magnum_test(cna_test_magnum_dualtextureeffect examples/magnum_dualtextureeffect_test.cpp)
    cna_register_backend_test(NAME Magnum_DualTextureEffect
        COMMAND cna_test_magnum_dualtextureeffect
        TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}"
        LABELS "GraphicsSmoke;Magnum")

    # plan_magnum.md MAGNUM-51: EnvironmentMapEffect's reflection blend is a lerp, not an addition,
    # and the two only separate against a mid-tone cube map over a non-zero base.
    cna_magnum_test(cna_test_magnum_environmentmapeffect
                    examples/magnum_environmentmapeffect_test.cpp)
    cna_register_backend_test(NAME Magnum_EnvironmentMapEffect
        COMMAND cna_test_magnum_environmentmapeffect
        TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}"
        LABELS "GraphicsSmoke;Magnum")

    # plan_magnum.md MAGNUM-52: SkinnedEffect measured by where the geometry lands, which is what a
    # bone palette that never reaches the shader actually gets wrong.
    cna_magnum_test(cna_test_magnum_skinnedeffect examples/magnum_skinnedeffect_test.cpp)
    cna_register_backend_test(NAME Magnum_SkinnedEffect
        COMMAND cna_test_magnum_skinnedeffect
        TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}"
        LABELS "GraphicsSmoke;Magnum")

    # plan_magnum.md MAGNUM-53: PbrEffect's glTF metallic-roughness BRDF, on a rig where every dot
    # product is exactly 1 so each expected byte is derived from the formula, not captured.
    cna_magnum_test(cna_test_magnum_pbreffect examples/magnum_pbreffect_test.cpp)
    cna_register_backend_test(NAME Magnum_PbrEffect
        COMMAND cna_test_magnum_pbreffect
        TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}"
        LABELS "GraphicsSmoke;Magnum")
endif()
