if(CNA_BUILD_TESTS AND CNA_GRAPHICS_BACKEND STREQUAL "HEADLESS")
    enable_testing()

    macro(cna_headless_test target src)
        add_executable(${target} ${src})
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(${target} PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME SDL3::SDL3)
        else()
            target_link_libraries(${target} PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(${target} PRIVATE SDL3::SDL3main)
        endif()
    endmacro()

    cna_headless_test(cna_test_headless_smoke examples/headless_smoke_test.cpp)
    cna_register_backend_test(NAME Headless_Smoke COMMAND cna_test_headless_smoke
        TIMEOUT 30 LABELS "Headless")

    cna_headless_test(cna_test_headless_resource_backends examples/headless_resource_backends_test.cpp)
    cna_register_backend_test(NAME Headless_ResourceBackends COMMAND cna_test_headless_resource_backends
        TIMEOUT 30 LABELS "Headless")

    cna_headless_test(cna_test_headless_validation_extras examples/headless_validation_extras_test.cpp)
    cna_register_backend_test(NAME Headless_ValidationExtras COMMAND cna_test_headless_validation_extras
        TIMEOUT 30 LABELS "Headless")

    cna_headless_test(cna_test_headless_coverage_gaps examples/headless_coverage_gaps_test.cpp)
    cna_register_backend_test(NAME Headless_CoverageGaps COMMAND cna_test_headless_coverage_gaps
        TIMEOUT 30 LABELS "Headless")

    cna_headless_test(cna_test_headless_effects examples/headless_effects_test.cpp)
    cna_register_backend_test(NAME Headless_Effects COMMAND cna_test_headless_effects
        TIMEOUT 30 LABELS "Headless")

    cna_headless_test(cna_test_headless_mode_dial examples/headless_mode_dial_test.cpp)
    cna_register_backend_test(NAME Headless_ModeDial COMMAND cna_test_headless_mode_dial
        TIMEOUT 30 LABELS "Headless")

    cna_headless_test(cna_test_headless_trace_diff examples/headless_trace_diff_test.cpp)
    cna_register_backend_test(NAME Headless_TraceDiff COMMAND cna_test_headless_trace_diff
        TIMEOUT 30 LABELS "Headless")

    # REMED-GFX-127: every public Texture2D::GetData call must return the resource's real content or
    # reject the request deterministically -- never fabricate one. This backend executes no
    # rasterization at all, so it is the one that must REJECT render-target colour readback: pre-fix
    # it answered with the shared layer's own zero-initialized scratch buffer, i.e. a complete,
    # uniformly transparent-black "rendered" frame it had never rendered.
    cna_headless_test(cna_test_headless_texture2d_getdata_contract
                      examples/texture2d_getdata_contract_test.cpp)
    cna_register_backend_test(NAME Headless_Texture2D_GetDataContract
        COMMAND cna_test_headless_texture2d_getdata_contract
        TIMEOUT 30 LABELS "Headless")
endif()
