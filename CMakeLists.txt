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
# scope, where the final executable link step is defined (cmake/Examples.cmake). With the old
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

# Physical module composition (MODULARIZATION_PLAN.md §11): every subsystem and renderer
# family owns modules/<name>/{CMakeLists.txt,include/,src/,tests/}; the former central
# cmake/BackendLibraries.cmake + cmake/CnaLibrary.cmake manifests dissolved into the
# per-module files this directory composes.
add_subdirectory(modules)

include(cmake/Examples.cmake)

# EasyGL integration tests (Task 85, 86, 87). Self-contained: bundles the CNA_BUILD_EXAMPLES
# condition this block has always (historically) required -- see EasyGLTests.cmake's own comment.
include(cmake/Tests/EasyGLTests.cmake)

# SKIA raster/display integration tests. The module is a no-op unless SKIA is selected.
include(cmake/Tests/SkiaTests.cmake)

# Vulkan integration tests. Self-contained: bundles the CNA_BUILD_EXAMPLES condition this block
# has always (historically) required -- see VulkanTests.cmake's own comment.
include(cmake/Tests/VulkanTests.cmake)

# SDL_Renderer integration tests (Task 666 audit phase)
include(cmake/Tests/SdlRendererTests.cmake)

# plan_opengl1.md: OPENGL1 (native legacy fixed-function OpenGL) CTest registration --
# tests/opengl1/README.md's 8 priority smoke-coverage scenarios.
include(cmake/Tests/OpenGL1Tests.cmake)

# GLIDE's smoke executable is built only when selected. It is not registered as a normal CTest:
# running it requires a user-supplied, separately licensed glide3x.dll (e.g. dgVoodoo2).
include(cmake/Tests/GlideTests.cmake)

# Win32 GDI 2D renderer tests. Native Windows registers them directly; cross-built executables
# remain available for an explicit Wine/display invocation.
include(cmake/Tests/GdiTests.cmake)

include(cmake/Tests/BgfxTests.cmake)

include(cmake/Tests/WebGpuTests.cmake)

# plan_magnum.md: Magnum (mosra/magnum) renderer integration tests.
include(cmake/Tests/MagnumTests.cmake)

# plan_headless.md: Headless (no GPU/window) graphics renderer tests. Deliberately no
# SDL_VIDEODRIVER/DISPLAY environment -- the entire point of this renderer is running with neither.
include(cmake/Tests/HeadlessTests.cmake)

# plan_software.md: Software (CPU rasterizer) graphics renderer tests. Deliberately no
# SDL_VIDEODRIVER/DISPLAY environment -- like HEADLESS, this renderer never needs either.
include(cmake/Tests/SoftwareTests.cmake)

# plan_stub.md: Stub (no-op) graphics renderer tests. Deliberately no SDL_VIDEODRIVER/DISPLAY
# environment -- like HEADLESS/SOFTWARE, this renderer never needs either.
include(cmake/Tests/StubTests.cmake)

# plan_ascii.md Phase G2+: ASCII (SDL-windowed glyph-grid) renderer test registration.
include(cmake/Tests/AsciiTests.cmake)

# plan_freedirect.md Phase X2/X8 (DX3-18/DX3-80): DIRECTX3 (DirectDraw via free-direct) CTest registration.
# Unlike SDL_RENDERER's own window-based ctests, these do NOT need a real/virtual X11 display:
# every pixel assertion reads back through the shadow-backbuffer surface directly
# (FreeDirectRenderer::ReadBackbuffer/GetBackBufferData), never through real physical
# presentation, so SDL_VIDEODRIVER=dummy (no display required at all) produces identical,
# verified-correct results (confirmed empirically: all 8 binaries, 49/49 checks, under
# SDL_VIDEODRIVER=dummy) while being far more portable than requiring CNA_TEST_DISPLAY to point
# at a working X11 server. This was a real registration bug found in review: the original
# SDL_VIDEODRIVER=x11 registration (copy-pasted from SDL_RENDERER's own pattern, which genuinely
# does need a real display) made every one of these 8 ctests fail outright in any environment
# without a live DISPLAY, even though the underlying binaries themselves were always correct.
include(cmake/Tests/FreeDirectTests.cmake)

# plan_dx.md DX-80: D3D11 CTest registration. When cross-compiled from Linux (this renderer is
# Windows-only, design decision 2), CTest itself still runs on the Linux host, so the test COMMAND
# wraps the real .exe with scripts/run-wine-dxvk.sh (DX-3). A native MSVC configure on a real
# Windows machine (CMAKE_CROSSCOMPILING is false there) needs no Wine wrapper at all -- runs the
# .exe directly (see .github/workflows/d3d-windows-ci.yml, added specifically to exercise this
# path for real; it only covers MSVC-compile/unit-test/shader-generation-check, not a substitute
# for plan_dx.md DX-90's real swap-chain/tearing/device-lost/driver-parity items).
include(cmake/Tests/DirectX11Tests.cmake)

# plan_dx.md DX-102/103/104/105: D3D12 CTest registration, mirroring DX-80's own D3D11 macro
# exactly, including the CMAKE_CROSSCOMPILING-aware command selection above (native MSVC runs the
# .exe directly; a Linux-hosted cross-compile wraps it with scripts/run-wine-vkd3d.sh, DX-100's own
# Wine+vkd3d-proton dev loop, a distinct Wine prefix from D3D11's DXVK one).
include(cmake/Tests/DirectX12Tests.cmake)

# Direct2D 1.1 is a Windows-only 2D renderer. Its tests use the same native-Windows/Wine+DXVK
# execution model as D3D11 because Direct2D owns a real D3D11/DXGI presentation surface.
include(cmake/Tests/Direct2DTests.cmake)

# plan_canvas.md CANVAS-15: structural smoke-test build for the CANVAS (HTML Canvas 2D) renderer.
# Deliberately NOT registered as a CTest (mirrors cna_diag_software's/directx12_swapchain_diag's own
# "real executable, not a ctest" precedent above): SDL_Init(SDL_INIT_VIDEO) itself throws under
# `node CnaTests.js` (no real browser DOM -- confirmed empirically, "window is not defined"),
# before any Canvas-specific code even runs. This target only proves the smoke test configures and
# links here; it needs a real browser (e.g. `emrun`) against a page with a real <canvas> element to
# actually produce a PASS/FAIL (Design decision 9).
include(cmake/Tests/CanvasTests.cmake)

# plan_html_dom.md HTMLDOM-15/72: the HTML_DOM (CSS-composited DOM elements) renderer's smoke test.
# Like CANVAS's above it is a real executable rather than a ctest -- it needs a browser, not node --
# but unlike CANVAS it has a driver that actually runs it here
# (scripts/run-htmldom-browser-test.sh, headless Chromium via Playwright).
include(cmake/Tests/HtmlDomTests.cmake)

# plan_html_dom.md Phase D10: cross-renderer sprite benchmark, built for whichever Emscripten
# renderer is currently selected (not tied to one CNA_GRAPHICS_RENDERER value) -- see the file's
# own header comment for why a HTML_DOM-vs-CANVAS-vs-EASYGL comparison needs three separate
# configures rather than one.
include(cmake/Tests/CrossRendererBenchmark.cmake)

# plan_dx9.md D9-23: D3D9 CTest registration, mirroring D3D11's/D3D12's own cna_directx11_test/
# cna_directx12_test macros (design decision: mirror the shape, don't invent a new pattern). Uses the
# new scripts/run-wine-dxvk9.sh (D9-5), not the shared run-wine-dxvk.sh (cold-start Sec.4).
include(cmake/Tests/DirectX9Tests.cmake)

# plan_sdlgpu.md SDLGPU-12: first end-to-end proof for the SDL_GPU graphics renderer -- a real
# window, a real SDL_GPUDevice, and a 60-frame Clear()+Present() loop. Needs a real
# display/GPU, same as the Vulkan/Bgfx/D3D11 CTest entries above -- run with a real DISPLAY.
include(cmake/Tests/SdlGpuTests.cmake)

# plan_dx1.md: DIRECTX1 (real DirectDraw v1, no ../free-direct) CTest registration, mirroring D3D9's
# own cna_directx9_test/cna_directx9_ctest_command shape (Windows-only, MinGW cross-compile + Wine
# wrapper) rather than DIRECTX3's native-Linux shape, since DIRECTX1 genuinely needs real ddraw.h.
include(cmake/Tests/DirectX1Tests.cmake)

# plan_dx2.md Phase O1/O2: DIRECTX2 (real DirectDraw v1 2D layer ported from DIRECTX1 + Direct3D v2
# DrawPrimitive 3D layer, landing in a later phase) CTest registration, mirroring DirectX1Tests.cmake's
# own shape (Windows-only, MinGW cross-compile + Wine wrapper).
include(cmake/Tests/DirectX2Tests.cmake)

# plan_dx3.md: real DirectX 3 renderer (originally landed under the temporary DX30 name; renamed
# to DX3 on 2026-08-04 once the free-direct renderer released the name as FREEDIRECT, and to
# DIRECTX3 in the 2026-08 naming normalization) -- a
# mechanical port of DIRECTX2's own 2D+3D layers upgraded to IDirectDraw2, same
# Windows-only/MinGW-cross-compile/Wine-wrapper shape as DirectX2Tests.cmake.
include(cmake/Tests/DirectX3Tests.cmake)

# plan_dx5.md: real DirectX 5 renderer -- DirectDraw v4 + Direct3D v3 FVF DrawPrimitive, a further
# port of DIRECTX3's own 2D+3D layers, same Windows-only/MinGW-cross-compile/Wine-wrapper shape.
include(cmake/Tests/DirectX5Tests.cmake)

# plan_dx6.md: real DirectX 6 renderer -- same DirectDraw v4 + Direct3D v3 interfaces as DIRECTX5, with
# real stencil buffer operations added; same Windows-only/MinGW-cross-compile/Wine-wrapper shape.
include(cmake/Tests/DirectX6Tests.cmake)

# plan_dx7.md: real DirectX 7 renderer -- new IDirectDraw7/IDirect3D7 interfaces, viewport object
# removed, texture binding simplified; same Windows-only/MinGW-cross-compile/Wine-wrapper shape.
include(cmake/Tests/DirectX7Tests.cmake)

# plan_dx8.md: real DirectX 8 renderer -- DXVK-delivered (not DirectDraw-based), fixed-function 3D
# only, real GPU-quad SpriteBatch; same Windows-only/MinGW-cross-compile/Wine-wrapper shape.
include(cmake/Tests/DirectX8Tests.cmake)

# plan_d3d10.md: real Direct3D 10 renderer -- no fixed-function at all, real HLSL vs_4_0/ps_4_0
# shaders throughout, DXVK-delivered via d3d10core.
include(cmake/Tests/DirectX10Tests.cmake)

# plan_opengles1.md: OPENGLES1 (fixed-function OpenGL ES 1.1) CTest registration. Needs a real
# EGL/GLESv1_CM driver capable of creating an actual ES1 context -- not every EGL implementation
# does (see docs/opengles1-renderer.md's own empirical finding on this project's Mesa/llvmpipe
# development host), same "run with a real DISPLAY and a real driver" caveat as the Vulkan/Bgfx/
# D3D11/SDL_GPU entries above.
include(cmake/Tests/OpenGLES1Tests.cmake)

# plan_opengl4.md GL4-1: real desktop OpenGL 4.x core-profile graphics renderer CTest
# registration. Needs a real display/GPU, same as the Vulkan/Bgfx/D3D11/SDL_GPU entries above.
include(cmake/Tests/OpenGL4Tests.cmake)

# plan_opengl2.md: first end-to-end proof for the native OpenGL 2.1 graphics renderer -- a real
# window, a real SDL GL context, and Clear/Present + Texture2D/SpriteBatch vertical-slice tests.
# Needs a real display/GPU, same as the SDL_GPU/Vulkan/Bgfx/D3D11 CTest entries above.
include(cmake/Tests/OpenGL2Tests.cmake)

# plan_wicked.md WICKED-70/74: the Wicked Engine renderer's device-independent pipeline-cache-key
# unit test, plus the same demo smoke test the other GPU renderers register (needs a real
# display/GPU and a Vulkan loader).
include(cmake/Tests/WickedTests.cmake)

# plan_sokol.md SOKOL-16/17: sokol_gfx renderer CTest registration. Needs a real display and GL
# context, same as the EasyGL/Vulkan/Bgfx entries above -- run with a real (or Xvfb) DISPLAY.
include(cmake/Tests/SokolTests.cmake)
# plan_diligent.md DILIGENT-16: real-GPU pixel proof for the Diligent Engine renderer.
# Skips (exit 77) rather than fails when no device can be created, since Diligent picks its
# native API at runtime and a headless machine may have none of them.
include(cmake/Tests/DiligentTests.cmake)

# plan_llgl.md: LLGL renderer CTest registration. Like the Vulkan/Bgfx/SDL_GPU entries above these
# need a real (or virtual) display; unlike them, the native API underneath is decided at runtime by
# LLGL itself, so the same binaries exercise whichever module was selected.
include(cmake/Tests/LlglTests.cmake)

# Native Apple Metal renderer smoke test.
include(cmake/Tests/MetalTests.cmake)

include(cmake/Harnesses.cmake)

include(cmake/ToolGltfToCnj.cmake)

include(cmake/UnitTests.cmake)

# Minimal-link module probes + renderer-identity registry gate (MODULARIZATION_PLAN.md §4).
include(cmake/Tests/ModuleProbes.cmake)
