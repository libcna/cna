if(CNA_BUILD_TESTS)
    # The Metal policy, format, stream, matrix, uniform-fill, and interface-shape tests are plain
    # C++ and intentionally stay independent of Objective-C++ and Apple frameworks.  Keep them in
    # a small aggregate executable as well as the repository-wide CnaTests binary so non-Apple and
    # sanitizer jobs can validate the complete portable contract without building unrelated
    # process harnesses.
    file(GLOB CNA_METAL_PORTABLE_TEST_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/CNA/Internal/Backends/Metal/*.cpp")
    add_executable(cna_test_metal_portable ${CNA_METAL_PORTABLE_TEST_SOURCES})
    if(UNIX AND NOT APPLE)
        target_link_libraries(cna_test_metal_portable PRIVATE
            -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
            SHARP_RUNTIME gtest_main SDL3::SDL3)
    else()
        target_link_libraries(cna_test_metal_portable
            PRIVATE CNA SHARP_RUNTIME gtest_main SDL3::SDL3)
    endif()
    cna_register_backend_test(NAME Metal_PortableHelpers COMMAND cna_test_metal_portable
        TIMEOUT 30 LABELS "Metal;Portable")
endif()

if(CNA_BUILD_TESTS AND CNA_GRAPHICS_BACKEND STREQUAL "METAL")
    enable_testing()

    # Configure-time control for Metal's real CNA<->backend static-archive cycle. Native test
    # executables below use ordinary target links, not a test-only --start-group workaround.
    get_target_property(CNA_METAL_BACKEND_LINKS ${BACKEND_TARGET} LINK_LIBRARIES)
    if(NOT "CNA" IN_LIST CNA_METAL_BACKEND_LINKS)
        message(FATAL_ERROR
            "METAL backend must retain its reverse CNA link for out-of-line Effect/math symbols")
    endif()

    # Supported-contract smoke: real macOS device/window/view, buffer uploads, combined
    # color/depth/stencil clears, and presentation. It deliberately performs no backbuffer
    # readback; that path is a documented deterministic NotSupported boundary.
    add_executable(cna_test_metal_smoke examples/metal_smoke_test.cpp)
    target_link_libraries(cna_test_metal_smoke PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_test_metal_smoke PRIVATE SDL3::SDL3main)
    endif()
    cna_register_backend_test(NAME Metal_Smoke COMMAND cna_test_metal_smoke
        TIMEOUT 60 LABELS "Metal")

    # One runtime assertion per current GraphicsCapability. Unsupported features are asserted
    # false rather than left to IGraphicsBackend's permissive default.
    add_executable(cna_test_metal_capabilities examples/metal_capabilities_test.cpp)
    target_link_libraries(cna_test_metal_capabilities PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_test_metal_capabilities PRIVATE SDL3::SDL3main)
    endif()
    cna_register_backend_test(NAME Metal_Capabilities COMMAND cna_test_metal_capabilities
        TIMEOUT 30 LABELS "Metal")
endif()
