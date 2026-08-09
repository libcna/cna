if(CNA_BUILD_TESTS AND CNA_GRAPHICS_BACKEND STREQUAL "STUB")
    enable_testing()

    macro(cna_stub_test target src)
        add_executable(${target} ${src})
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(${target} PRIVATE
                CNA
                SHARP_RUNTIME SDL3::SDL3)
        else()
            target_link_libraries(${target} PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(${target} PRIVATE SDL3::SDL3main)
        endif()
    endmacro()

    cna_stub_test(cna_test_stub_smoke examples/stub_smoke_test.cpp)
    cna_register_backend_test(NAME Stub_Smoke COMMAND cna_test_stub_smoke
        TIMEOUT 30 LABELS "Stub")
endif()
