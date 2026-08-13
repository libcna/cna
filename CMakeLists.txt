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
# to agree on the same exception-handling ABI.
#
# plan_glbackends.md GLB-39: uses the modern Wasm exception-handling proposal
# (-fwasm-exceptions), not the legacy JS-based -fexceptions model this project used previously.
# Forced by a real link failure once the GL-family renderers (OPENGLES3/OPENGL33/WEBGL1/WEBGL2)
# started depending on the sibling easy-gl -> meta-gl chain: easy-gl's own
# cmake/EasyGlPlatform.cmake sets -fwasm-exceptions in ITS directory scope (needed for its own
# standalone Emscripten preset/tests), which only affects object files compiled within that
# subdirectory tree (meta-gl, easy-gl) -- it does NOT propagate to CNA's own top-level directory
# scope, where the final executable link steps are defined (the modules' example CMakeLists). With the old
# legacy -fexceptions here, meta-gl's objects (compiled under -fwasm-exceptions, referencing the
# Wasm EH exception-tag import symbol __cpp_exception) and the final link (configured for legacy
# JS EH, unaware of that symbol) disagreed on ABI, producing
# "wasm-ld: undefined symbol: __cpp_exception". Matching CNA's own top-level flag to
# -fwasm-exceptions makes every object file in the link agree, without needing to touch the
# sibling repo at all. -sNO_DISABLE_EXCEPTION_CATCHING is a legacy-JS-EH-only linker setting with
# no Wasm-EH equivalent (Wasm EH can always catch), so it is dropped rather than replaced.
if(EMSCRIPTEN)
    add_compile_options(-fwasm-exceptions)
    add_link_options(-fwasm-exceptions)
endif()

option(CNA_BUILD_TESTS "Build CNA tests" ON)
# Example/demo applications. The registrations themselves are module-local
# (modules/<name>/examples/CMakeLists.txt, modules/renderers/<family>/examples/CMakeLists.txt);
# this option keeps its historical configure-wide meaning.
option(CNA_BUILD_EXAMPLES "Build CNA example applications (house3d demo, demo_2d, ...)" ON)
option(CNA_CNAEXT "Enable CNA CNAEXT extended graphics layer (beyond XNA 4.0)" OFF)
option(CNA_DEVICES "Enable CNA-specific device/sensor extensions beyond XNA 4.0 (battery, camera, clipboard, ...)" OFF)
option(CNA_ENABLE_NET "Build CNA networking (GamerServices + Net, requires ENet)" ON)
# On a Linux cross-build, Direct2D tests normally use the project's dedicated system-Wine prefix.
# PROTON is an opt-in DXVK-backed alternative for a local Steam Proton installation; native
# Windows configurations still execute the .exe directly.
set(CNA_DIRECT2D_TEST_RUNTIME "WINE" CACHE STRING "Cross-compiled Direct2D test runtime (WINE or PROTON)")
set_property(CACHE CNA_DIRECT2D_TEST_RUNTIME PROPERTY STRINGS WINE PROTON)

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
# CNA_SHARP_RUNTIME_ROOT (MODULARIZATION_PLAN.md §3) points builds at a different
# checkout -- e.g. a pinned read-only worktree of the modular sharp-runtime --
# without moving worktrees around.
set(CNA_SHARP_RUNTIME_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../sharp-runtime" CACHE PATH
    "Path to the sharp-runtime checkout consumed by this build (sibling by default)")
if(NOT EXISTS "${CNA_SHARP_RUNTIME_ROOT}/CMakeLists.txt")
    message(FATAL_ERROR
        "CNA: Missing sharp-runtime checkout at ${CNA_SHARP_RUNTIME_ROOT} -- "
        "this is a separate git checkout (sibling '../sharp-runtime' by "
        "default, overridable with -DCNA_SHARP_RUNTIME_ROOT=<path>), not a "
        "git submodule (git submodule update --init will not fetch it). Fix: "
        "cd ${CMAKE_CURRENT_SOURCE_DIR}/.. && "
        "git clone https://github.com/openeggbert/sharp-runtime.git")
endif()
add_subdirectory("${CNA_SHARP_RUNTIME_ROOT}" SHARP_RUNTIME)

# Detect which sharp-runtime shape this build consumes (monolithic archive vs modular
# components) and provide cna_link_sharp_runtime() for the CNA module targets.
include(cmake/SharpRuntimeConsumption.cmake)
cna_detect_sharp_runtime_shape()

#find_package(SDL3_ttf REQUIRED)

include(cmake/RendererSelection.cmake)

# Platform implementation selection (plan_platform.md PLAT-11). A separate axis from
# CNA_GRAPHICS_RENDERER: the renderer decides how pixels are produced, the platform decides
# where the window, events and input come from.
include(cmake/PlatformSelection.cmake)

# SDL2 is an independent platform edge, not an SDL3 compatibility layer. Configure it only after
# selection, so a default build never downloads or compiles a second native toolkit.
if(CNA_PLATFORM STREQUAL "SDL2")
    include(cmake/ThirdPartySDL2.cmake)
    cna_configure_sdl2()
endif()

# Playback/capture implementation selection (plan_platform.md PLAT-93). This is intentionally
# independent from CNA_PLATFORM: window/input and audio backends are orthogonal choices.
include(cmake/AudioPlatformSelection.cmake)

# Platform ratchet (plan_platform.md PLAT-8): reports how much of CNA still calls SDL
# directly outside the PLAT-3 allowlist, and warns when that count has gone up. Warning
# only until PLAT-121 flips it strict; skips itself cleanly with no Python 3 present.
include(cmake/PlatformRatchet.cmake)

# Hot-path lint (plan_platform.md PLAT-9): rejects platform calls inside per-pixel,
# per-vertex, per-fragment, per-sample and per-event loops, per design decision 4. A hard
# error rather than a warning like the ratchet above: this one starts at zero violations
# and every regression is a single deliberate line with an obvious fix or an annotation.
include(cmake/PlatformHotPathLint.cmake)

# Example and test registrations live inside the module tree (modules/<name>/examples/,
# modules/renderers/<family>/examples/): testing must be enabled before the modules are
# composed so their subdirectory add_test() registrations take effect.
if(CNA_BUILD_TESTS)
    enable_testing()
endif()

# Physical module composition (MODULARIZATION_PLAN.md §11): every subsystem and renderer
# family owns modules/<name>/{CMakeLists.txt,include/,src/,tests/}; the former central
# cmake/BackendLibraries.cmake + cmake/CnaLibrary.cmake manifests dissolved into the
# per-module files this directory composes.
add_subdirectory(modules)

# Renderer-family example/test registration is module-local now: each family owns
# modules/renderers/<family>/examples/CMakeLists.txt (reached through
# add_subdirectory(modules) above, which enters exactly the families the selected
# renderer needs), and the framework-module demos own
# modules/<name>/examples/CMakeLists.txt the same way. The per-family registration
# knowledge that used to sit here as one include-per-family travels with those files.
#
# Wicked's registration stays: it registers only the module's own unit tests
# (modules/renderers/wicked/tests/), no examples.
include(cmake/Tests/WickedTests.cmake)

include(cmake/Harnesses.cmake)

include(cmake/ToolGltfToCnj.cmake)

include(cmake/UnitTests.cmake)

# Minimal-link module probes + renderer-identity registry gate (MODULARIZATION_PLAN.md §4).
include(cmake/Tests/ModuleProbes.cmake)
