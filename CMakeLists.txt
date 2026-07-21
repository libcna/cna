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

# EasyGL integration tests (Task 85, 86, 87). Self-contained: bundles the CNA_BUILD_EXAMPLES
# condition this block has always (historically) required -- see EasyGLTests.cmake's own comment.
include(cmake/Tests/EasyGLTests.cmake)

# Vulkan integration tests. Self-contained: bundles the CNA_BUILD_EXAMPLES condition this block
# has always (historically) required -- see VulkanTests.cmake's own comment.
include(cmake/Tests/VulkanTests.cmake)

# SDL_Renderer integration tests (Task 666 audit phase)
include(cmake/Tests/SdlRendererTests.cmake)

include(cmake/Tests/BgfxTests.cmake)

include(cmake/Tests/WebGpuTests.cmake)

# plan_headless.md: Headless (no GPU/window) graphics backend tests. Deliberately no
# SDL_VIDEODRIVER/DISPLAY environment -- the entire point of this backend is running with neither.
include(cmake/Tests/HeadlessTests.cmake)

# plan_software.md: Software (CPU rasterizer) graphics backend tests. Deliberately no
# SDL_VIDEODRIVER/DISPLAY environment -- like HEADLESS, this backend never needs either.
include(cmake/Tests/SoftwareTests.cmake)

# plan_ascii.md Phase G2+: ASCII (SDL-windowed glyph-grid) backend test registration.
include(cmake/Tests/AsciiTests.cmake)

# plan_dx3.md Phase X2/X8 (DX3-18/DX3-80): DX3 (DirectDraw via free-direct) CTest registration.
# Unlike SDL_RENDERER's own window-based ctests, these do NOT need a real/virtual X11 display:
# every pixel assertion reads back through the shadow-backbuffer surface directly
# (Dx3GraphicsBackend::ReadBackbuffer/GetBackBufferData), never through real physical
# presentation, so SDL_VIDEODRIVER=dummy (no display required at all) produces identical,
# verified-correct results (confirmed empirically: all 8 binaries, 49/49 checks, under
# SDL_VIDEODRIVER=dummy) while being far more portable than requiring CNA_TEST_DISPLAY to point
# at a working X11 server. This was a real registration bug found in review: the original
# SDL_VIDEODRIVER=x11 registration (copy-pasted from SDL_RENDERER's own pattern, which genuinely
# does need a real display) made every one of these 8 ctests fail outright in any environment
# without a live DISPLAY, even though the underlying binaries themselves were always correct.
include(cmake/Tests/Dx3Tests.cmake)

# plan_dx.md DX-80: D3D11 CTest registration. When cross-compiled from Linux (this backend is
# Windows-only, design decision 2), CTest itself still runs on the Linux host, so the test COMMAND
# wraps the real .exe with scripts/run-wine-dxvk.sh (DX-3). A native MSVC configure on a real
# Windows machine (CMAKE_CROSSCOMPILING is false there) needs no Wine wrapper at all -- runs the
# .exe directly (see .github/workflows/d3d-windows-ci.yml, added specifically to exercise this
# path for real; it only covers MSVC-compile/unit-test/shader-generation-check, not a substitute
# for plan_dx.md DX-90's real swap-chain/tearing/device-lost/driver-parity items).
include(cmake/Tests/D3D11Tests.cmake)

# plan_dx.md DX-102/103/104/105: D3D12 CTest registration, mirroring DX-80's own D3D11 macro
# exactly, including the CMAKE_CROSSCOMPILING-aware command selection above (native MSVC runs the
# .exe directly; a Linux-hosted cross-compile wraps it with scripts/run-wine-vkd3d.sh, DX-100's own
# Wine+vkd3d-proton dev loop, a distinct Wine prefix from D3D11's DXVK one).
include(cmake/Tests/D3D12Tests.cmake)

# plan_canvas.md CANVAS-15: structural smoke-test build for the CANVAS (HTML Canvas 2D) backend.
# Deliberately NOT registered as a CTest (mirrors cna_diag_software's/d3d12_swapchain_diag's own
# "real executable, not a ctest" precedent above): SDL_Init(SDL_INIT_VIDEO) itself throws under
# `node CnaTests.js` (no real browser DOM -- confirmed empirically, "window is not defined"),
# before any Canvas-specific code even runs. This target only proves the smoke test configures and
# links here; it needs a real browser (e.g. `emrun`) against a page with a real <canvas> element to
# actually produce a PASS/FAIL (Design decision 9).
include(cmake/Tests/CanvasTests.cmake)

# plan_dx9.md D9-23: D3D9 CTest registration, mirroring D3D11's/D3D12's own cna_d3d11_test/
# cna_d3d12_test macros (design decision: mirror the shape, don't invent a new pattern). Uses the
# new scripts/run-wine-dxvk9.sh (D9-5), not the shared run-wine-dxvk.sh (cold-start Sec.4).
include(cmake/Tests/D3D9Tests.cmake)

# plan_sdlgpu.md SDLGPU-12: first end-to-end proof for the SDL_GPU graphics backend -- a real
# window, a real SDL_GPUDevice, and a 60-frame Clear()+Present() loop. Needs a real
# display/GPU, same as the Vulkan/Bgfx/D3D11 CTest entries above -- run with a real DISPLAY.
include(cmake/Tests/SdlGpuTests.cmake)

# plan_dx1.md: DX1 (real DirectDraw v1, no ../free-direct) CTest registration, mirroring D3D9's
# own cna_d3d9_test/cna_d3d9_ctest_command shape (Windows-only, MinGW cross-compile + Wine
# wrapper) rather than DX3's native-Linux shape, since DX1 genuinely needs real ddraw.h.
include(cmake/Tests/Dx1Tests.cmake)

# plan_dx2.md Phase O1/O2: DX2 (real DirectDraw v1 2D layer ported from DX1 + Direct3D v2
# DrawPrimitive 3D layer, landing in a later phase) CTest registration, mirroring Dx1Tests.cmake's
# own shape (Windows-only, MinGW cross-compile + Wine wrapper).
include(cmake/Tests/Dx2Tests.cmake)

# plan_dx30.md: real DirectX 3 backend, temporarily named DX30 (see plan_dx30.md's own status note
# for why) -- a mechanical port of DX2's own 2D+3D layers upgraded to IDirectDraw2, same
# Windows-only/MinGW-cross-compile/Wine-wrapper shape as Dx2Tests.cmake.
include(cmake/Tests/Dx30Tests.cmake)

include(cmake/Harnesses.cmake)

include(cmake/ToolGltfToCnj.cmake)

include(cmake/UnitTests.cmake)
