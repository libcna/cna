# stub renderer example/test registration (module-local; this file is the
# former cmake/Tests/StubTests.cmake, moved when examples moved into their owning
# physical modules).
#
# From the root CMakeLists.txt include site:
# plan_stub.md: Stub (no-op) graphics renderer tests. Deliberately no SDL_VIDEODRIVER/DISPLAY
# environment -- like HEADLESS/SOFTWARE, this renderer never needs either.

# Example/test executables historically live at the build root (scripts, docs and
# the golden-image working-directory conventions rely on it); keep them there.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
# Shared example-support headers ("common/PixelTestGame.hpp", ...) are owned by
# the graphics module's examples directory.
include_directories(${CNA_GRAPHICS_EXAMPLES_DIR})

if(CNA_BUILD_TESTS AND CNA_GRAPHICS_RENDERER STREQUAL "STUB")
    enable_testing()

    macro(cna_stub_test target src)
        add_executable(${target} ${src})
        # The smoke test includes this renderer's own header. In a single-renderer build the CNA
        # umbrella already carries that include directory; in a multi-renderer build it carries the
        # DEFAULT renderer's, and STUB may not be the default -- so name it here, where it is true
        # in both. (Found by a `-DCNA_GRAPHICS_RENDERERS="HEADLESS;SOFTWARE;STUB;SDL_RENDERER"`
        # build, the combination CLAUDE.md documents.)
        target_include_directories(${target} PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/../include")
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

    cna_stub_test(cna_test_stub_smoke stub_smoke_test.cpp)
    cna_register_renderer_test(NAME Stub_Smoke COMMAND cna_test_stub_smoke
        TIMEOUT 30 LABELS "Stub")
endif()

# Module-local tests get the same headless-safe skip convention the root one-shot
# gives root-registered tests (see cmake/TestHelpers.cmake).
cna_apply_skip_convention()
