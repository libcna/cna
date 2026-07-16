cmake_minimum_required(VERSION 3.20)

project(CNA LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Reuse compiler output across build directories when ccache is available.
# This is intentionally configured before sharp-runtime is added so the cache
# also covers that comparatively large dependency. Disable with
# -DCNA_USE_CCACHE=OFF when diagnosing compiler-launcher issues.
option(CNA_USE_CCACHE "Use ccache for CNA and sharp-runtime builds when available" ON)
if(CNA_USE_CCACHE)
    find_program(CNA_CCACHE_PROGRAM ccache)
    if(CNA_CCACHE_PROGRAM)
        if(NOT CMAKE_C_COMPILER_LAUNCHER)
            set(CMAKE_C_COMPILER_LAUNCHER "${CNA_CCACHE_PROGRAM}" CACHE STRING "C compiler launcher")
        endif()
        if(NOT CMAKE_CXX_COMPILER_LAUNCHER)
            set(CMAKE_CXX_COMPILER_LAUNCHER "${CNA_CCACHE_PROGRAM}" CACHE STRING "C++ compiler launcher")
        endif()
        message(STATUS "CNA: compiler cache available (${CNA_CCACHE_PROGRAM}); existing compiler launchers are preserved")
    else()
        message(STATUS "CNA: ccache not found; using the build directory's normal incremental object cache")
    endif()
endif()

# Emscripten disables C++ exception throw/catch support by default (an uncaught throw aborts
# the whole runtime instead of unwinding) - this codebase relies on exceptions pervasively
# (System::Exception hierarchy, EXPECT_THROW tests, etc.), so it must be enabled globally,
# before sharp-runtime is added below, since unwinding requires every frame in the call chain
# to be compiled with -fexceptions.
if(EMSCRIPTEN)
    add_compile_options(-fexceptions)
    add_link_options(-fexceptions -sNO_DISABLE_EXCEPTION_CATCHING=1)
endif()

option(CNA_BUILD_TESTS "Build CNA tests" ON)
option(CNA_NOXNA "Enable CNA NOXNA extended graphics layer (beyond XNA 4.0)" OFF)
option(CNA_DEVICES "Enable CNA-specific device/sensor extensions beyond XNA 4.0 (battery, camera, clipboard, ...)" OFF)
option(CNA_ENABLE_NET "Build CNA networking (GamerServices + Net, requires ENet)" ON)

# DISPLAY value baked into every GPU/window-creating ctest's ENVIRONMENT property (SDL_VIDEODRIVER=x11).
# Defaults to :0 for backward compatibility; override with e.g. -DCNA_TEST_DISPLAY=:99 to run the full
# suite against a virtual display (Xvfb) instead of the real desktop.
set(CNA_TEST_DISPLAY ":0" CACHE STRING "DISPLAY value used by GPU/window-creating ctest tests")

include(cmake/ThirdPartySDL.cmake)
cna_configure_vendored_sdl()

include(cmake/TestHelpers.cmake)

if(CNA_ENABLE_NET)
    include(cmake/ThirdPartyENet.cmake)
    cna_configure_vendored_enet()
endif()

# Optional sanitizers (INPUT-BUILD-006 / INPUT-CI-005). Empty = off. Comma-separated clang/gcc
# sanitizer list, e.g. -DCNA_SANITIZE=address,undefined. Applied from here on, so CNA, sharp-runtime,
# and the tests are instrumented; the vendored SDL/ENet builds above stay uninstrumented (ASan/UBSan
# tolerate mixing). For diagnostics/CI only — never ship a sanitized build.
set(CNA_SANITIZE "" CACHE STRING "Comma-separated sanitizers to enable (e.g. address,undefined); empty disables")
if(CNA_SANITIZE)
    message(STATUS "CNA: sanitizers enabled: ${CNA_SANITIZE}")
    add_compile_options(-fsanitize=${CNA_SANITIZE} -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=${CNA_SANITIZE})
endif()

set(SHARP_RUNTIME_BUILD_TESTS OFF CACHE BOOL "Build sharp-runtime tests" FORCE)
# sharp-runtime is a SIBLING repository checkout, not a git submodule of this
# repo (Task DEV-BUILD-001) -- `git submodule update --init` cannot fetch it.
# It must be cloned next to this repo's own directory, so that
# "${CMAKE_CURRENT_SOURCE_DIR}/../sharp-runtime" resolves to a real checkout.
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../sharp-runtime/CMakeLists.txt")
    message(FATAL_ERROR
        "CNA: Missing sibling repository 'sharp-runtime' at "
        "${CMAKE_CURRENT_SOURCE_DIR}/../sharp-runtime -- this is a separate git "
        "checkout expected next to this repo's own directory, not a git "
        "submodule (git submodule update --init will not fetch it). Fix: "
        "cd ${CMAKE_CURRENT_SOURCE_DIR}/.. && "
        "git clone https://github.com/openeggbert/sharp-runtime.git")
endif()
add_subdirectory(../sharp-runtime SHARP_RUNTIME)

#find_package(SDL3_ttf REQUIRED)

include(cmake/BackendSelection.cmake)

include(cmake/BackendLibraries.cmake)

include(cmake/CnaLibrary.cmake)

include(cmake/Examples.cmake)
