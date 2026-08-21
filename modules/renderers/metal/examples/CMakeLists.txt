# metal renderer example/test registration (module-local; this file is the
# former cmake/Tests/MetalTests.cmake, moved when examples moved into their owning
# physical modules).
#
# From the root CMakeLists.txt include site:
# Native Apple Metal renderer smoke test.

# Example/test executables historically live at the build root (scripts, docs and
# the golden-image working-directory conventions rely on it); keep them there.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
# Shared example-support headers ("common/PixelTestGame.hpp", ...) are owned by
# the graphics module's examples directory.
include_directories(${CNA_GRAPHICS_EXAMPLES_DIR})

if(CNA_BUILD_TESTS)
    # The Metal policy, format, stream, matrix, uniform-fill, and interface-shape tests are plain
    # C++ and intentionally stay independent of Objective-C++ and Apple frameworks.  Keep them in
    # a small aggregate executable as well as the repository-wide CnaTests binary so non-Apple and
    # sanitizer jobs can validate the complete portable contract without building unrelated
    # process harnesses.
    file(GLOB CNA_METAL_PORTABLE_TEST_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/modules/renderers/metal/tests/CNA/Internal/Renderers/Metal/*.cpp")
    add_executable(cna_test_metal_portable ${CNA_METAL_PORTABLE_TEST_SOURCES})
    if(UNIX AND NOT APPLE)
        target_link_libraries(cna_test_metal_portable PRIVATE
            CNA
            SHARP_RUNTIME gtest_main SDL3::SDL3)
    else()
        target_link_libraries(cna_test_metal_portable
            PRIVATE CNA SHARP_RUNTIME gtest_main SDL3::SDL3)
    endif()
    # The portable policy suite includes the metal module's policy headers on every
    # platform; they ride the module's unconditional header-interface target.
    target_link_libraries(cna_test_metal_portable PRIVATE cna_renderer_metal_headers)
    cna_register_renderer_test(NAME Metal_PortableHelpers COMMAND cna_test_metal_portable
        TIMEOUT 30 LABELS "Metal;Portable")
endif()

if(CNA_BUILD_TESTS AND CNA_GRAPHICS_RENDERER STREQUAL "METAL")
    enable_testing()

    # Configure-time control for Metal's real graphics-core<->renderer static-archive cycle. Native
    # test executables below use ordinary target links, not a test-only --start-group workaround.
    get_target_property(CNA_METAL_BACKEND_LINKS ${RENDERER_TARGET} LINK_LIBRARIES)
    if(NOT "cna_graphics_core" IN_LIST CNA_METAL_BACKEND_LINKS)
        message(FATAL_ERROR
            "METAL renderer must retain its reverse graphics-core link for out-of-line Effect symbols")
    endif()

    # Supported-contract smoke: real macOS device/window/view, buffer uploads, combined
    # color/depth/stencil clears, and presentation. It deliberately performs no backbuffer
    # readback; that path is a documented deterministic NotSupported boundary.
    add_executable(cna_test_metal_smoke metal_smoke_test.cpp)
    target_link_libraries(cna_test_metal_smoke PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_test_metal_smoke PRIVATE SDL3::SDL3main)
    endif()
    cna_register_renderer_test(NAME Metal_Smoke COMMAND cna_test_metal_smoke
        TIMEOUT 60 LABELS "Metal")

    # One runtime assertion per current GraphicsCapability. Unsupported features are asserted
    # false rather than left to IGraphicsRenderer's permissive default.
    add_executable(cna_test_metal_capabilities metal_capabilities_test.cpp)
    target_link_libraries(cna_test_metal_capabilities PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_test_metal_capabilities PRIVATE SDL3::SDL3main)
    endif()
    cna_register_renderer_test(NAME Metal_Capabilities COMMAND cna_test_metal_capabilities
        TIMEOUT 30 LABELS "Metal")
endif()

# Module-local tests get the same headless-safe skip convention the root one-shot
# gives root-registered tests (see cmake/TestHelpers.cmake).
cna_apply_skip_convention()
