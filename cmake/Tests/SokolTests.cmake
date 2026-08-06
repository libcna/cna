if(CNA_BUILD_TESTS AND CNA_BUILD_EXAMPLES
   AND CNA_GRAPHICS_BACKEND STREQUAL "SOKOL")
    enable_testing()

    # Mirrors the SDL_GPU/Bgfx test macros: the backend static library and CNA form a genuine
    # cycle (the backend implements IGraphicsBackend while calling back into CNA-defined symbols),
    # which GNU ld's single-pass archive scan only resolves inside --start-group/--end-group.
    macro(cna_sokol_test target src)
        add_executable(${target} ${src})
        target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/examples)
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
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

    # plan_sokol.md SOKOL-16: real window + real GPU context + real sg_setup, a 60-frame
    # Clear()/Present() loop, and a back-buffer readback of the clear colour.
    cna_sokol_test(cna_test_sokol_smoke examples/sokol_smoke_test.cpp)
    cna_register_backend_test(NAME Sokol_Smoke COMMAND cna_test_sokol_smoke
        TIMEOUT 120 LABELS "Sokol"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_sokol.md SOKOL-17: Texture2D + SpriteBatch vertical slice, every draw pixel-verified
    # through GetBackBufferData().
    cna_sokol_test(cna_test_sokol_2d examples/sokol_2d_test.cpp)
    cna_register_backend_test(NAME Sokol_2D COMMAND cna_test_sokol_2d
        TIMEOUT 120 LABELS "Sokol"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
endif()
