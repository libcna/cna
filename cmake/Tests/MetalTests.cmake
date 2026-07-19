if(CNA_BUILD_TESTS AND CNA_GRAPHICS_BACKEND STREQUAL "METAL")
    enable_testing()

    add_executable(cna_test_metal_smoke examples/metal_smoke_test.cpp)
    target_link_libraries(cna_test_metal_smoke PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_test_metal_smoke PRIVATE SDL3::SDL3main)
    endif()

    cna_register_backend_test(NAME Metal_Smoke COMMAND cna_test_metal_smoke
        TIMEOUT 60 LABELS "Metal")
endif()
