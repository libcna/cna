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

# --- Graphics Backend Selection ---
# EasyGL is the default on Linux and Emscripten (WebGL 2 = OpenGL ES 3.0).
# Other platforms default to SDL_RENDERER.
if(EMSCRIPTEN OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(_cna_default_backend "EASYGL")
else()
    set(_cna_default_backend "SDL_RENDERER")
endif()
set(CNA_GRAPHICS_BACKEND "${_cna_default_backend}" CACHE STRING "Graphics backend to use (SDL_RENDERER, EASYGL, BGFX, VULKAN, WEBGPU, HEADLESS, SOFTWARE, D3D11, D3D12, CANVAS, ASCII, DX3, D3D9, or SDL_GPU)")
set_property(CACHE CNA_GRAPHICS_BACKEND PROPERTY STRINGS "SDL_RENDERER" "EASYGL" "BGFX" "VULKAN" "WEBGPU" "HEADLESS" "SOFTWARE" "D3D11" "D3D12" "CANVAS" "ASCII" "DX3" "D3D9" "SDL_GPU")

option(CNA_BACKEND_SDL_RENDERER "Enable SDL_Renderer graphics backend" OFF)
option(CNA_BACKEND_EASY_GL "Enable easy-gl graphics backend" OFF)
option(CNA_BACKEND_BGFX "Enable bgfx graphics backend" OFF)
option(CNA_BACKEND_VULKAN "Enable Vulkan graphics backend" OFF)
option(CNA_BACKEND_WEBGPU "Enable WebGPU graphics backend (wgpu-native)" OFF)
option(CNA_BACKEND_HEADLESS "Enable Headless (no GPU/window) graphics backend" OFF)
option(CNA_BACKEND_SOFTWARE "Enable Software (CPU rasterizer) graphics backend" OFF)
option(CNA_BACKEND_D3D11 "Enable Direct3D 11 graphics backend (Windows only)" OFF)
option(CNA_BACKEND_D3D12 "Enable Direct3D 12 graphics backend (Windows only)" OFF)
# plan_canvas.md: HTML Canvas 2D backend -- Emscripten-only (design decision 1), a browser-native,
# GPU-free 2D-only backend using canvas.getContext('2d') instead of EASYGL's WebGL2 context.
option(CNA_BACKEND_CANVAS "Enable HTML Canvas 2D graphics backend (Emscripten only)" OFF)
# plan_ascii.md: SDL-windowed retro text/glyph-grid backend -- a thin decorator around
# SDL_RENDERER's own SdlGraphicsBackend (see the shared cna_backend_graphics_sdl_renderer_core
# library below), not a real terminal/TTY backend.
option(CNA_BACKEND_ASCII "Enable ASCII (SDL-windowed glyph-grid) graphics backend" OFF)
option(CNA_BACKEND_DX3 "Enable DirectX 3 (DirectDraw, via the ../free-direct sibling) graphics backend" OFF)
option(CNA_BACKEND_D3D9 "Enable Direct3D 9 graphics backend (Windows only)" OFF)
option(CNA_BACKEND_SDL_GPU "Enable SDL_gpu graphics backend" OFF)

set(_cna_explicit_backend_selection OFF)
if(CNA_BACKEND_SDL_RENDERER OR CNA_BACKEND_EASY_GL OR CNA_BACKEND_BGFX OR CNA_BACKEND_VULKAN OR CNA_BACKEND_WEBGPU OR CNA_BACKEND_HEADLESS OR CNA_BACKEND_SOFTWARE OR CNA_BACKEND_D3D11 OR CNA_BACKEND_D3D12 OR CNA_BACKEND_CANVAS OR CNA_BACKEND_ASCII OR CNA_BACKEND_DX3 OR CNA_BACKEND_D3D9 OR CNA_BACKEND_SDL_GPU)
    set(_cna_explicit_backend_selection ON)
endif()

if(_cna_explicit_backend_selection)
    set(_cna_enabled_backends)
    if(CNA_BACKEND_SDL_RENDERER)
        list(APPEND _cna_enabled_backends "SDL_RENDERER")
    endif()
    if(CNA_BACKEND_EASY_GL)
        list(APPEND _cna_enabled_backends "EASYGL")
    endif()
    if(CNA_BACKEND_BGFX)
        list(APPEND _cna_enabled_backends "BGFX")
    endif()
    if(CNA_BACKEND_VULKAN)
        list(APPEND _cna_enabled_backends "VULKAN")
    endif()
    if(CNA_BACKEND_WEBGPU)
        list(APPEND _cna_enabled_backends "WEBGPU")
    endif()
    if(CNA_BACKEND_HEADLESS)
        list(APPEND _cna_enabled_backends "HEADLESS")
    endif()
    if(CNA_BACKEND_SOFTWARE)
        list(APPEND _cna_enabled_backends "SOFTWARE")
    endif()
    if(CNA_BACKEND_D3D11)
        list(APPEND _cna_enabled_backends "D3D11")
    endif()
    if(CNA_BACKEND_D3D12)
        list(APPEND _cna_enabled_backends "D3D12")
    endif()
    if(CNA_BACKEND_CANVAS)
        list(APPEND _cna_enabled_backends "CANVAS")
    endif()
    if(CNA_BACKEND_ASCII)
        list(APPEND _cna_enabled_backends "ASCII")
    endif()
    if(CNA_BACKEND_DX3)
        list(APPEND _cna_enabled_backends "DX3")
    endif()
    if(CNA_BACKEND_D3D9)
        list(APPEND _cna_enabled_backends "D3D9")
    endif()
    if(CNA_BACKEND_SDL_GPU)
        list(APPEND _cna_enabled_backends "SDL_GPU")
    endif()

    list(LENGTH _cna_enabled_backends _cna_enabled_backends_count)
    if(NOT _cna_enabled_backends_count EQUAL 1)
        message(FATAL_ERROR "CNA: Exactly one backend option must be ON when using CNA_BACKEND_* options.")
    endif()

    list(GET _cna_enabled_backends 0 CNA_GRAPHICS_BACKEND)
endif()

# plan_dx.md design decision 2: D3D11/D3D12 genuinely cannot build anywhere but Windows (native or
# MinGW/MSVC cross-compile) -- d3d11.h/d3d12.h/dxgi.h do not exist elsewhere. Unlike BGFX's soft
# WARNING-only platform check below, this is a hard FATAL_ERROR. plan_dx9.md design decision 1
# extends this same gate to D3D9 (d3d9.h is equally Windows-only).
if((CNA_GRAPHICS_BACKEND STREQUAL "D3D11" OR CNA_GRAPHICS_BACKEND STREQUAL "D3D12" OR CNA_GRAPHICS_BACKEND STREQUAL "D3D9")
        AND NOT CMAKE_SYSTEM_NAME STREQUAL "Windows")
    message(FATAL_ERROR
        "CNA: ${CNA_GRAPHICS_BACKEND} backend only builds when targeting Windows. Either build "
        "natively on Windows, or cross-compile from Linux with "
        "-DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64.cmake")
endif()

# plan_canvas.md design decision 1: HTML Canvas 2D is a browser DOM API and cannot exist outside
# an Emscripten/WebAssembly build -- same hard-gate shape as the D3D11/D3D12 Windows-only check
# just above, new condition.
if(CNA_GRAPHICS_BACKEND STREQUAL "CANVAS" AND NOT EMSCRIPTEN)
    message(FATAL_ERROR
        "CNA: CANVAS backend only builds when targeting Emscripten (HTML Canvas is a browser DOM "
        "API). Configure with -DCMAKE_TOOLCHAIN_FILE=\$EMSDK/upstream/emscripten/cmake/Modules/"
        "Platform/Emscripten.cmake (or use emcmake).")
endif()

if(CNA_GRAPHICS_BACKEND STREQUAL "EASYGL")
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(WARNING "CNA: EASYGL backend is primarily tested on Linux. Other platforms may require additional setup.")
    endif()
    # easy-gl is a SIBLING repository checkout, not a git submodule of this
    # repo (Task DEV-BUILD-001) -- see sharp-runtime's identical check above
    # for the full rationale.
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../easy-gl/CMakeLists.txt")
        message(FATAL_ERROR
            "CNA: Missing sibling repository 'easy-gl' at "
            "${CMAKE_CURRENT_SOURCE_DIR}/../easy-gl -- this is a separate git "
            "checkout expected next to this repo's own directory, not a git "
            "submodule (git submodule update --init will not fetch it). Fix: "
            "cd ${CMAKE_CURRENT_SOURCE_DIR}/.. && "
            "git clone https://github.com/openeggbert/easy-gl.git")
    endif()
    add_subdirectory(../easy-gl easy-gl)
endif()

# plan_dx3.md design decision 10 / Task DX3-2: free-direct is a SIBLING repository checkout, not a
# git submodule of this repo -- same rationale as sharp-runtime/easy-gl's identical checks above.
# free-direct's own CMakeLists.txt (add_subdirectory(../free-api ...)) resolves SDL3::SDL3/
# SDL3_image::SDL3_image/SDL3_mixer::SDL3_mixer from CNA's own already-vendored targets (set up by
# cna_configure_vendored_sdl() above, before backend selection runs), so no
# -DFREE_API_USE_SYSTEM_SDL3 flag is needed here, mirroring how ../free-eggbert/../planetblupi
# already consume free-direct today (design decision 10).
if(CNA_GRAPHICS_BACKEND STREQUAL "DX3")
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../free-direct/CMakeLists.txt")
        message(FATAL_ERROR
            "CNA: Missing sibling repository 'free-direct' at "
            "${CMAKE_CURRENT_SOURCE_DIR}/../free-direct -- this is a separate git "
            "checkout expected next to this repo's own directory, not a git "
            "submodule (git submodule update --init will not fetch it). Fix: "
            "cd ${CMAKE_CURRENT_SOURCE_DIR}/.. && "
            "git clone https://github.com/openeggbert/free-direct.git")
    endif()
    add_subdirectory(../free-direct free-direct)
endif()

if(CNA_GRAPHICS_BACKEND STREQUAL "BGFX" AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(WARNING "CNA: BGFX backend is primarily tested on Linux. Other platforms may require additional setup.")
endif()

if(CNA_GRAPHICS_BACKEND STREQUAL "SDL_RENDERER")
    message(STATUS "CNA: Using SDL_RENDERER graphics backend")
    set(BACKEND_DIR "src/CNA/Internal/Backends/SdlRenderer")
    set(BACKEND_TARGET "cna_backend_graphics_sdl_renderer")
    add_compile_definitions(CNA_BACKEND_SDL_RENDERER)
elseif(CNA_GRAPHICS_BACKEND STREQUAL "EASYGL")
    message(STATUS "CNA: Using EASYGL graphics backend")
    set(BACKEND_DIR "src/CNA/Internal/Backends/EasyGL")
    set(BACKEND_TARGET "cna_backend_graphics_easygl")
    add_compile_definitions(CNA_BACKEND_EASYGL)
elseif(CNA_GRAPHICS_BACKEND STREQUAL "BGFX")
    message(STATUS "CNA: Using BGFX graphics backend")
    set(BACKEND_DIR "src/CNA/Internal/Backends/Bgfx")
    set(BACKEND_TARGET "cna_backend_graphics_bgfx")
    add_compile_definitions(CNA_BACKEND_BGFX)

    include(FetchContent)
    option(CNA_BGFX_BUILD_SHADERC "Build bgfx shaderc tool (needed to compile 3D shaders)" OFF)
    set(BGFX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    if(NOT CNA_BGFX_BUILD_SHADERC)
        set(BGFX_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
    endif()
    set(BGFX_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(BGFX_INSTALL OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
        bgfx_cmake
        GIT_REPOSITORY https://github.com/bkaradzic/bgfx.cmake.git
        GIT_TAG master
        GIT_SHALLOW TRUE
        GIT_PROGRESS TRUE
        GIT_SUBMODULES_RECURSE TRUE
    )
    FetchContent_MakeAvailable(bgfx_cmake)

    set(CNA_BGFX_SHADER_INCLUDE_DIR "${bgfx_cmake_SOURCE_DIR}/bgfx/examples/common/imgui")
elseif(CNA_GRAPHICS_BACKEND STREQUAL "VULKAN")
    message(STATUS "CNA: Using VULKAN graphics backend")
    set(BACKEND_DIR "src/CNA/Internal/Backends/Vulkan")
    set(BACKEND_TARGET "cna_backend_graphics_vulkan")
    add_compile_definitions(CNA_BACKEND_VULKAN)
    find_package(Vulkan REQUIRED)
elseif(CNA_GRAPHICS_BACKEND STREQUAL "WEBGPU")
    message(STATUS "CNA: Using WEBGPU graphics backend")
    set(BACKEND_DIR "src/CNA/Internal/Backends/WebGPU")
    set(BACKEND_TARGET "cna_backend_graphics_webgpu")
    add_compile_definitions(CNA_BACKEND_WEBGPU)
    include(cmake/ThirdPartyWebGPU.cmake)
    cna_configure_webgpu()
elseif(CNA_GRAPHICS_BACKEND STREQUAL "HEADLESS")
    message(STATUS "CNA: Using HEADLESS (no GPU/window) graphics backend")
    set(BACKEND_DIR "src/CNA/Internal/Backends/Headless")
    set(BACKEND_TARGET "cna_backend_graphics_headless")
    add_compile_definitions(CNA_BACKEND_HEADLESS)
elseif(CNA_GRAPHICS_BACKEND STREQUAL "SOFTWARE")
    message(STATUS "CNA: Using SOFTWARE (CPU rasterizer) graphics backend")
    set(BACKEND_DIR "src/CNA/Internal/Backends/Software")
    set(BACKEND_TARGET "cna_backend_graphics_software")
    add_compile_definitions(CNA_BACKEND_SOFTWARE)
elseif(CNA_GRAPHICS_BACKEND STREQUAL "D3D11")
    message(STATUS "CNA: Using D3D11 graphics backend")
    set(BACKEND_DIR "src/CNA/Internal/Backends/D3D11")
    set(BACKEND_TARGET "cna_backend_graphics_d3d11")
    add_compile_definitions(CNA_BACKEND_D3D11)
elseif(CNA_GRAPHICS_BACKEND STREQUAL "D3D12")
    message(STATUS "CNA: Using D3D12 graphics backend")
    set(BACKEND_DIR "src/CNA/Internal/Backends/D3D12")
    set(BACKEND_TARGET "cna_backend_graphics_d3d12")
    add_compile_definitions(CNA_BACKEND_D3D12)
elseif(CNA_GRAPHICS_BACKEND STREQUAL "CANVAS")
    message(STATUS "CNA: Using CANVAS (HTML Canvas 2D) graphics backend")
    set(BACKEND_DIR "src/CNA/Internal/Backends/Canvas")
    set(BACKEND_TARGET "cna_backend_graphics_canvas")
    add_compile_definitions(CNA_BACKEND_CANVAS)
elseif(CNA_GRAPHICS_BACKEND STREQUAL "ASCII")
    message(STATUS "CNA: Using ASCII (SDL-windowed glyph-grid) graphics backend")
    set(BACKEND_DIR "src/CNA/Internal/Backends/Ascii")
    set(BACKEND_TARGET "cna_backend_graphics_ascii")
    add_compile_definitions(CNA_BACKEND_ASCII)
elseif(CNA_GRAPHICS_BACKEND STREQUAL "DX3")
    message(STATUS "CNA: Using DX3 (DirectDraw via free-direct) graphics backend")
    set(BACKEND_DIR "src/CNA/Internal/Backends/Dx3")
    set(BACKEND_TARGET "cna_backend_graphics_dx3")
    add_compile_definitions(CNA_BACKEND_DX3)
elseif(CNA_GRAPHICS_BACKEND STREQUAL "D3D9")
    message(STATUS "CNA: Using D3D9 graphics backend")
    set(BACKEND_DIR "src/CNA/Internal/Backends/D3D9")
    set(BACKEND_TARGET "cna_backend_graphics_d3d9")
    add_compile_definitions(CNA_BACKEND_D3D9)
elseif(CNA_GRAPHICS_BACKEND STREQUAL "SDL_GPU")
    message(STATUS "CNA: Using SDL_GPU graphics backend")
    set(BACKEND_DIR "src/CNA/Internal/Backends/SdlGpu")
    set(BACKEND_TARGET "cna_backend_graphics_sdl_gpu")
    add_compile_definitions(CNA_BACKEND_SDL_GPU)
else()

    message(FATAL_ERROR "CNA: Unknown graphics backend: ${CNA_GRAPHICS_BACKEND}")
endif()

# --- FFmpeg (video decoding) — not available on Emscripten/Android, nor on any Windows target
# (mingw-w64 cross-build OR a native MSVC build, e.g. this project's own D3D11/D3D12 Windows CI --
# neither has an FFmpeg pkg-config path; on the MinGW cross-build, pkg-config would otherwise
# silently resolve to the host's native FFmpeg and poison the cross-compile's include path with
# native glibc headers). WIN32 covers both MinGW and native MSVC uniformly; MINGW is kept for
# clarity/documentation even though it's now redundant with WIN32. ---
if(MINGW OR WIN32 OR EMSCRIPTEN OR ANDROID)
    set(CNA_FFMPEG_AVAILABLE OFF)
else()
    set(CNA_FFMPEG_AVAILABLE ON)
endif()

if(CNA_FFMPEG_AVAILABLE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBAVCODEC  REQUIRED libavcodec)
    pkg_check_modules(LIBAVFORMAT REQUIRED libavformat)
    pkg_check_modules(LIBAVUTIL   REQUIRED libavutil)
    pkg_check_modules(LIBSWRESAMPLE REQUIRED libswresample)
endif()

# Common backend interfaces
add_library(cna_backend_graphics_common INTERFACE)
target_include_directories(cna_backend_graphics_common INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# plan_dx.md design decision 4: D3DCommon shared core, consumed by cna_backend_graphics_d3d11 (and,
# once Phase DX12 is authorized, cna_backend_graphics_d3d12). Only built when actually needed --
# same discipline as every other on-demand backend dependency in this file.
# plan_dx9.md design decision 12: D3D9 deliberately does NOT join this condition. D3DFORMAT is a
# different enum space from DXGI_FORMAT and D3D9 has no state objects at all -- D3D9 gets its own
# D3D9FormatMapping/D3D9StateMapping/D3D9VertexDeclarations instead of expanding this shared core.
if(CNA_GRAPHICS_BACKEND STREQUAL "D3D11" OR CNA_GRAPHICS_BACKEND STREQUAL "D3D12")
    file(GLOB D3DCOMMON_SOURCES "src/CNA/Internal/Backends/D3DCommon/*.cpp")
    add_library(cna_backend_graphics_d3dcommon STATIC ${D3DCOMMON_SOURCES})
    target_link_libraries(cna_backend_graphics_d3dcommon PUBLIC cna_backend_graphics_common SHARP_RUNTIME d3d11 dxgi)
    target_include_directories(cna_backend_graphics_d3dcommon PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
endif()

# plan_ascii.md design decision 2: ASCII is a thin decorator around SDL_RENDERER's own
# SdlGraphicsBackend (composition, not reimplementation) -- it needs SdlGraphicsBackend.cpp's real
# implementation compiled in even though CNA_GRAPHICS_BACKEND=SDL_RENDERER was not itself selected.
# Mirrors the D3DCommon shared-core pattern just above: a small static lib, built only when
# actually needed. CNA_BACKEND_SDL_RENDERER is deliberately NOT defined for this build, so
# SdlGraphicsBackend.cpp's own #ifdef CNA_BACKEND_SDL_RENDERER-guarded CreateGraphicsBackend() is
# compiled out here, leaving Ascii's own CreateGraphicsBackend() (guarded by CNA_BACKEND_ASCII) as
# the only factory definition -- no duplicate-symbol clash.
if(CNA_GRAPHICS_BACKEND STREQUAL "ASCII")
    file(GLOB CNA_ASCII_SDLRENDERER_SOURCES "src/CNA/Internal/Backends/SdlRenderer/*.cpp")
    add_library(cna_backend_graphics_sdl_renderer_core STATIC ${CNA_ASCII_SDLRENDERER_SOURCES})
    target_link_libraries(cna_backend_graphics_sdl_renderer_core PUBLIC cna_backend_graphics_common SHARP_RUNTIME)
    target_link_libraries(cna_backend_graphics_sdl_renderer_core PRIVATE SDL3::SDL3)
    target_include_directories(cna_backend_graphics_sdl_renderer_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
endif()

# Backend target
file(GLOB BACKEND_SOURCES "${BACKEND_DIR}/*.cpp")

# plan_dx9.md Phase D9-11 (D9-110/D9-111), design decision 16: D3D9EffectBackend.cpp calls
# D3DCompile() (the custom-ShaderEffect runtime compile path), which needs d3dcompiler -- but the
# stock D3D9 pipeline must stay d3dcompiler-free (unlike D3D11/D3D12, which link it into their
# whole backend target unconditionally, DX-58/DX-121). Excluded from the main glob and built as its
# own isolated static library instead, with d3dcompiler linked ONLY there (D9-111's own "on this
# target only"). D3D9ConstantTable.cpp (D9-110's CTAB parser) moves here too -- it has no consumer
# outside D3D9EffectBackend.cpp, and leaving it in the main glob while D3D9EffectBackend.cpp moved
# out created a genuine link-order circular dependency between the two targets (this effect target
# calling ParseConstantTableEXT() while ALSO needing to be linked INTO the main backend target for
# D3D9GraphicsBackend::CreateEffectBackend() to construct one) -- found empirically via a real
# "undefined reference to ParseConstantTableEXT" link failure across every D3D9 test binary, not
# assumed in advance. ${BACKEND_TARGET} links this target (D9-112) so
# D3D9GraphicsBackend::CreateEffectBackend() can construct one.
set(D3D9_EFFECT_BACKEND_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/CNA/Internal/Backends/D3D9/D3D9EffectBackend.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/CNA/Internal/Backends/D3D9/D3D9ConstantTable.cpp"
)
if(CNA_GRAPHICS_BACKEND STREQUAL "D3D9" AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/CNA/Internal/Backends/D3D9/D3D9EffectBackend.cpp")
    list(REMOVE_ITEM BACKEND_SOURCES ${D3D9_EFFECT_BACKEND_SOURCES})
    add_library(cna_backend_graphics_d3d9_effect STATIC ${D3D9_EFFECT_BACKEND_SOURCES})
    target_link_libraries(cna_backend_graphics_d3d9_effect PUBLIC cna_backend_graphics_common SHARP_RUNTIME)
    target_link_libraries(cna_backend_graphics_d3d9_effect PRIVATE d3d9 d3dcompiler)
    target_include_directories(cna_backend_graphics_d3d9_effect PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
endif()

add_library(${BACKEND_TARGET} STATIC ${BACKEND_SOURCES})
target_link_libraries(${BACKEND_TARGET} PUBLIC cna_backend_graphics_common SHARP_RUNTIME)
target_include_directories(${BACKEND_TARGET} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

if(CNA_GRAPHICS_BACKEND STREQUAL "SDL_RENDERER")
    target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3)
elseif(CNA_GRAPHICS_BACKEND STREQUAL "EASYGL")
    target_link_libraries(${BACKEND_TARGET} PRIVATE easy-gl SDL3::SDL3)
elseif(CNA_GRAPHICS_BACKEND STREQUAL "BGFX")
    target_link_libraries(${BACKEND_TARGET} PUBLIC bgfx bx)
    target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3)
    target_include_directories(${BACKEND_TARGET} PRIVATE ${CNA_BGFX_SHADER_INCLUDE_DIR})
elseif(CNA_GRAPHICS_BACKEND STREQUAL "VULKAN")
    target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3 Vulkan::Vulkan)
elseif(CNA_GRAPHICS_BACKEND STREQUAL "WEBGPU")
    target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3 WebGPU::WebGPU)
    if(CNA_WEBGPU_RUNTIME_LIBRARY)
        add_custom_command(TARGET ${BACKEND_TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CNA_WEBGPU_RUNTIME_LIBRARY}" "$<TARGET_FILE_DIR:${BACKEND_TARGET}>"
            VERBATIM)
    endif()
elseif(CNA_GRAPHICS_BACKEND STREQUAL "D3D11")
    # plan_dx.md design decision 3 (DX-1): d3d11/dxgi is the confirmed minimal link set for the
    # stock offline-compiled shader pipeline. d3dcompiler is linked too, specifically for DX-58's
    # runtime D3DCompile() custom-ShaderEffect path (D3D11EffectBackend) -- confirmed safe to link
    # in isolation by DX-1/DX-14-compile's own spikes; the offline stock pipeline never calls it.
    target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3 d3d11 dxgi d3dcompiler cna_backend_graphics_d3dcommon)
elseif(CNA_GRAPHICS_BACKEND STREQUAL "DX3")
    # plan_dx3.md design decision 10: free-direct's own public target is the literal lowercase
    # `free-direct` (PUBLIC-links free-api::free-api, PUBLIC-exposes its own include/ dir so
    # #include <ddraw.h> resolves) -- no ALIAS namespace exists for it, unlike easy-gl's own target.
    target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3 free-direct)
elseif(CNA_GRAPHICS_BACKEND STREQUAL "D3D12")
    # plan_dx.md DX-100/DX-101: d3d12+dxgi alone was confirmed sufficient for device/queue/
    # command-list creation by DX-100's own spike -- start from that minimum, same discipline as
    # DX-12/design decision 3. dxguid is deliberately NOT linked (still unneeded). d3dcompiler was
    # added by DX-121 (D3D12EffectBackend's runtime D3DCompile() path) -- confirmed safe to link in
    # isolation by D3D11's own DX-14-compile/DX-58 precedent.
    target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3 d3d12 dxgi d3dcompiler cna_backend_graphics_d3dcommon)
elseif(CNA_GRAPHICS_BACKEND STREQUAL "CANVAS")
    # plan_canvas.md design decision 2: reuses the existing SDL-created window/canvas element,
    # so still links SDL3 even though actual rendering goes through EM_JS/Canvas2D, not SDL_Renderer.
    target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3)
elseif(CNA_GRAPHICS_BACKEND STREQUAL "ASCII")
    target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3 cna_backend_graphics_sdl_renderer_core)
elseif(CNA_GRAPHICS_BACKEND STREQUAL "D3D9")
    # plan_dx9.md design decision 16 / D9-2: d3d9 alone was confirmed sufficient (no dxguid, no
    # dxgi -- D3D9 predates DXGI) by D9-2's own spike. No cna_backend_graphics_d3dcommon (design
    # decision 12 -- D3D9 does not share D3DCommon). d3dcompiler is deliberately NOT linked here --
    # design decision 9/16 keep the stock pipeline dependency-free; it will only be added, later, to
    # a custom-ShaderEffect-only target if Phase D9-11 is authorized (ask-first per its own row).
    target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3 d3d9)
elseif(CNA_GRAPHICS_BACKEND STREQUAL "SDL_GPU")
    # plan_sdlgpu.md SDLGPU-1: SDL_gpu.h is part of SDL3 itself (SDL_gpu.c is already compiled
    # into the same SDL3 library every other backend links against) -- no separate find_package
    # or FetchContent is needed, unlike VULKAN/WEBGPU/BGFX above.
    target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3)

    # plan_sdlgpu.md SDLGPU-42/43: SdlGpuEffectBackend needs a REAL runtime GLSL->SPIR-V compile
    # for arbitrary user ShaderEffect source (SDL_gpu only accepts precompiled bytecode) -- unlike
    # D3D11/D3D12's d3dcompiler, this environment has no libshaderc-dev package (no unversioned
    # .so symlink, no headers), only the runtime libshaderc1 package's versioned .so.1. Prefer a
    # normal find_library() first (works if a dev package IS present, e.g. other machines/CI), and
    # fall back to globbing standard library directories for the exact versioned file otherwise --
    # mirrors compile_shaders.py's own ctypes loader fallback list.
    find_library(CNA_SHADERC_LIBRARY NAMES shaderc_shared shaderc)
    if(NOT CNA_SHADERC_LIBRARY)
        file(GLOB CNA_SHADERC_CANDIDATES
            /usr/lib/*/libshaderc.so*
            /usr/lib/libshaderc.so*
            /usr/local/lib/libshaderc.so*
        )
        if(CNA_SHADERC_CANDIDATES)
            list(GET CNA_SHADERC_CANDIDATES 0 CNA_SHADERC_LIBRARY)
        endif()
    endif()
    if(NOT CNA_SHADERC_LIBRARY)
        message(FATAL_ERROR "CNA: libshaderc not found (required for SDL_GPU's runtime ShaderEffect GLSL compile, SDLGPU-42/43) -- install libshaderc1 or libshaderc-dev")
    endif()
    message(STATUS "CNA: using libshaderc at ${CNA_SHADERC_LIBRARY}")
    target_link_libraries(${BACKEND_TARGET} PRIVATE "${CNA_SHADERC_LIBRARY}")
endif()

file(GLOB_RECURSE CNA_SOURCES CONFIGURE_DEPENDS
        "src/*.cpp"
)
# Exclude backend sources from main CNA library to avoid double compilation and conflicts
list(FILTER CNA_SOURCES EXCLUDE REGEX "src/CNA/Internal/Backends/.*")

# Exclude Net / GamerServices sources — they live in CNA_GamerServices and CNA_Net targets
list(FILTER CNA_SOURCES EXCLUDE REGEX "src/Microsoft/Xna/Framework/GamerServices/.*")
list(FILTER CNA_SOURCES EXCLUDE REGEX "src/Microsoft/Xna/Framework/Net/.*")
list(FILTER CNA_SOURCES EXCLUDE REGEX "src/CNA/Internal/Net/.*")

# Exclude FFmpeg-dependent sources on platforms where FFmpeg is unavailable
if(NOT CNA_FFMPEG_AVAILABLE)
    list(FILTER CNA_SOURCES EXCLUDE REGEX ".*/CNA/Internal/Media/VideoDecoder\\.cpp$")
    list(FILTER CNA_SOURCES EXCLUDE REGEX ".*/Media/Video/VideoPlayer\\.cpp$")
    list(FILTER CNA_SOURCES EXCLUDE REGEX ".*/Media/Video/Video\\.cpp$")
endif()

add_library(CNA STATIC
        ${CNA_SOURCES}
)

target_include_directories(CNA
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(CNA
        PUBLIC
        SOUND_ENABLED
        XNA5
        $<$<BOOL:${CNA_NOXNA}>:CNA_NOXNA>
        $<$<BOOL:${CNA_DEVICES}>:CNA_DEVICES>
)

target_link_libraries(CNA
        PUBLIC
        SHARP_RUNTIME
        ${BACKEND_TARGET}
        PRIVATE
        SDL3::SDL3
        SDL3_image::SDL3_image
        SDL3_mixer::SDL3_mixer
#        SDL3_ttf::SDL3_ttf
)

# D3D11's/D3D12's SpriteBatch backend (plan_dx.md DX-70/DX-71) calls back into
# Microsoft::Xna::Framework::Graphics::Effect::Apply() for SpriteBatch::Begin(effect)'s custom-
# Effect path -- a genuine, honest circular dependency between the backend static library and CNA
# itself (the backend needs a CNA-defined symbol, while CNA needs the backend to implement
# IGraphicsBackend). Under MinGW's single-pass archive resolution, any executable linking CNA
# (not just the D3D11/D3D12 CTest binaries, which never link the full CNA target) hit a real
# "undefined reference to Effect::Apply()" at link time -- e.g. cna_reference_dump/cna_demo_2d --
# because libCNA.a is fully scanned before libcna_backend_graphics_d3d11.a creates the need.
# CMake's documented static-library-cycle support (see LINK_INTERFACE_MULTIPLICITY) resolves this
# by repeating the archives on the final link line once this cycle is declared.
# plan_dx9.md D9-112 (Phase D9-11, authorized 2026-07-15): D3D9 now joins this condition too --
# D3D9SpriteBatchBackend::FlushBatch() calls customEffect_->Apply() (a CNA-defined symbol) for
# SpriteBatch::Begin(effect)'s custom-Effect path, the same genuine circular dependency D3D11/D3D12
# already have. (D9-10's own original note said "add D3D9 here when D9-112 actually needs it" --
# this is that task.)
#
# plan_sdlgpu.md: the SDL_GPU backend (SdlGpuGraphicsBackend.cpp) calls CNA::Logger::Warn (a
# CNA-defined symbol) for its own real, non-fatal capability-gap warnings -- the exact same
# single-pass archive-scanning problem under GNU ld, not just MinGW: any executable linking the
# plain CNA target without --start-group/--end-group (cna_reference_dump, cna_demo_xact) hit a
# real "undefined reference to CNA::Logger::Warn" at link time, because libCNA.a (which contains
# Logger.cpp.o) is fully scanned before libcna_backend_graphics_sdl_gpu.a creates the need. Only
# the SDL_GPU CTest targets (cna_sdlgpu_test's own --start-group/--end-group wrapping) avoided
# this; every other SDL_GPU-backend executable did not. Fixed the same way as D3D11/D3D12.
if(CNA_GRAPHICS_BACKEND STREQUAL "D3D11" OR CNA_GRAPHICS_BACKEND STREQUAL "D3D12" OR CNA_GRAPHICS_BACKEND STREQUAL "D3D9" OR CNA_GRAPHICS_BACKEND STREQUAL "SDL_GPU")
    target_link_libraries(${BACKEND_TARGET} PRIVATE CNA)
endif()

# D9-112: D3D9GraphicsBackend::CreateEffectBackend() (below) needs to construct a real
# D3D9EffectBackend, so ${BACKEND_TARGET} now links the isolated, d3dcompiler-carrying
# cna_backend_graphics_d3d9_effect target (D9-111, design decision 16) it didn't need before.
if(TARGET cna_backend_graphics_d3d9_effect)
    target_link_libraries(${BACKEND_TARGET} PRIVATE cna_backend_graphics_d3d9_effect)
endif()

if(ANDROID)
    # Detail::AndroidSensorBridge (Microsoft::Devices::Sensors) calls the NDK's
    # ASensorManager_*/ASensorEventQueue_*/ALooper_* API directly (no JNI) --
    # those symbols live in libandroid.so, not libc/libc++. PUBLIC so any
    # executable linking CNA (e.g. cna_demo_devices) picks up the transitive
    # dependency automatically (plan_devices.md Task DEVICES-0121).
    target_link_libraries(CNA PUBLIC android)
endif()

if(CNA_FFMPEG_AVAILABLE)
    target_link_libraries(CNA PRIVATE
        ${LIBAVCODEC_LIBRARIES}
        ${LIBAVFORMAT_LIBRARIES}
        ${LIBAVUTIL_LIBRARIES}
        ${LIBSWRESAMPLE_LIBRARIES}
    )
    target_include_directories(CNA PRIVATE
        ${LIBAVCODEC_INCLUDE_DIRS}
        ${LIBAVFORMAT_INCLUDE_DIRS}
        ${LIBAVUTIL_INCLUDE_DIRS}
        ${LIBSWRESAMPLE_INCLUDE_DIRS}
    )
endif()

# --- GamerServices + Net ---
if(CNA_ENABLE_NET)
    file(GLOB_RECURSE CNA_GAMERSERVICES_SOURCES CONFIGURE_DEPENDS
        "src/Microsoft/Xna/Framework/GamerServices/*.cpp"
    )
    add_library(CNA_GamerServices STATIC ${CNA_GAMERSERVICES_SOURCES})
    target_include_directories(CNA_GamerServices
        PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    # Guide.cpp calls SDL3 directly (message box); this was previously only compiling by
    # accident on hosts with a stray system-wide SDL3 install on the default include path.
    target_link_libraries(CNA_GamerServices PUBLIC CNA PRIVATE SDL3::SDL3)

    file(GLOB_RECURSE CNA_NET_SOURCES CONFIGURE_DEPENDS
        "src/Microsoft/Xna/Framework/Net/*.cpp"
        "src/CNA/Internal/Net/*.cpp"
    )
    add_library(CNA_Net STATIC ${CNA_NET_SOURCES})
    target_include_directories(CNA_Net
        PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(CNA_Net
        PUBLIC
        CNA_GamerServices
        enet
    )
endif()

# --- Examples ---
# The 3D house demo exercises the EasyGL 3D pipeline. Other backends throw
# "3D not supported", so the target is only enabled with the EASYGL backend.
# Emscripten uses WebGL 2 (= OpenGL ES 3.0), which the EasyGL backend targets.
option(CNA_BUILD_EXAMPLES "Build CNA example applications (house3d demo, demo_2d, ...)" ON)
if(CNA_BUILD_EXAMPLES)
    # ---- 2D sprite demo (works with all backends) ----------------------------
    add_executable(cna_demo_2d
        examples/demo_2d/src/Main.cpp
        examples/demo_2d/src/Game1.cpp
    )
    target_include_directories(cna_demo_2d PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_2d/src
    )

    if(EMSCRIPTEN)
        set_target_properties(cna_demo_2d PROPERTIES SUFFIX ".html")
        target_link_libraries(cna_demo_2d PRIVATE
            CNA ${BACKEND_TARGET} SDL3::SDL3-static SHARP_RUNTIME)
        target_link_options(cna_demo_2d PRIVATE
            -sALLOW_MEMORY_GROWTH=1
            -sSTACK_SIZE=1048576
            -sINITIAL_MEMORY=134217728
            -sFORCE_FILESYSTEM=1
            "SHELL:--preload-file ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_2d/Content@/Content"
        )
    else()
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(cna_demo_2d PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_2d PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_2d PRIVATE SDL3::SDL3main)
        endif()
        add_custom_command(TARGET cna_demo_2d POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_2d/Content"
                "$<TARGET_FILE_DIR:cna_demo_2d>/Content"
            COMMENT "Copying Content next to cna_demo_2d executable")
        if(CNA_GRAPHICS_BACKEND STREQUAL "WEBGPU" AND CNA_WEBGPU_RUNTIME_LIBRARY)
            add_custom_command(TARGET cna_demo_2d POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CNA_WEBGPU_RUNTIME_LIBRARY}" "$<TARGET_FILE_DIR:cna_demo_2d>"
                COMMENT "Copying wgpu-native runtime next to cna_demo_2d")
            if(UNIX AND NOT APPLE)
                set_property(TARGET cna_demo_2d APPEND PROPERTY BUILD_RPATH "$ORIGIN")
            endif()
        endif()
        if(WIN32)
            set_target_properties(cna_demo_2d PROPERTIES WIN32_EXECUTABLE TRUE)
            cna_copy_sdl_runtime(cna_demo_2d)
        endif()
    endif()

    # ---- Audio demo (works with all backends) --------------------------------
    add_executable(cna_demo_sound
        examples/demo_sound/src/Main.cpp
        examples/demo_sound/src/SoundDemo.cpp
    )
    target_include_directories(cna_demo_sound PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_sound/src
    )

    if(EMSCRIPTEN)
        set_target_properties(cna_demo_sound PROPERTIES SUFFIX ".html")
        target_link_libraries(cna_demo_sound PRIVATE
            CNA ${BACKEND_TARGET} SDL3::SDL3-static SHARP_RUNTIME)
        target_link_options(cna_demo_sound PRIVATE
            -sALLOW_MEMORY_GROWTH=1
            -sSTACK_SIZE=1048576
            -sINITIAL_MEMORY=134217728
            -sFORCE_FILESYSTEM=1
            "SHELL:--preload-file ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_sound/Content@/Content"
        )
    else()
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(cna_demo_sound PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_sound PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_sound PRIVATE SDL3::SDL3main)
        endif()
        add_custom_command(TARGET cna_demo_sound POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_sound/Content"
                "$<TARGET_FILE_DIR:cna_demo_sound>/Content"
            COMMENT "Copying Content next to cna_demo_sound executable")
        if(WIN32)
            set_target_properties(cna_demo_sound PROPERTIES WIN32_EXECUTABLE TRUE)
            cna_copy_sdl_runtime(cna_demo_sound)
        endif()
    endif()

    # ---- XACT demo (desktop only — XACT is Windows/Xbox-specific, no web content) -
    if(NOT EMSCRIPTEN AND NOT ANDROID)
    add_executable(cna_demo_xact
        examples/demo_xact/src/Main.cpp
        examples/demo_xact/src/XactDemo.cpp
    )
    target_include_directories(cna_demo_xact PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_xact/src
    )
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
        target_link_libraries(cna_demo_xact PRIVATE
            -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
            SHARP_RUNTIME)
    else()
        target_link_libraries(cna_demo_xact PRIVATE CNA SHARP_RUNTIME)
    endif()
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_demo_xact PRIVATE SDL3::SDL3main)
    endif()
    add_custom_command(TARGET cna_demo_xact POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_xact/Content"
            "$<TARGET_FILE_DIR:cna_demo_xact>/Content"
        COMMENT "Copying Content next to cna_demo_xact executable")
    if(WIN32)
        set_target_properties(cna_demo_xact PROPERTIES WIN32_EXECUTABLE TRUE)
        cna_copy_sdl_runtime(cna_demo_xact)
    endif()
    endif() # NOT EMSCRIPTEN AND NOT ANDROID

    # ---- Input & Touch demo (works with all backends) ------------------------
    add_executable(cna_demo_input
        examples/demo_input/src/Main.cpp
        examples/demo_input/src/InputDemo.cpp
    )
    target_include_directories(cna_demo_input PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_input/src
    )

    if(EMSCRIPTEN)
        set_target_properties(cna_demo_input PROPERTIES SUFFIX ".html")
        target_link_libraries(cna_demo_input PRIVATE
            CNA ${BACKEND_TARGET} SDL3::SDL3-static SHARP_RUNTIME)
        target_link_options(cna_demo_input PRIVATE
            -sALLOW_MEMORY_GROWTH=1
            -sSTACK_SIZE=1048576
            -sINITIAL_MEMORY=67108864
            -sFORCE_FILESYSTEM=1
        )
    else()
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(cna_demo_input PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_input PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_input PRIVATE SDL3::SDL3main)
        endif()
        if(WIN32)
            set_target_properties(cna_demo_input PROPERTIES WIN32_EXECUTABLE TRUE)
            cna_copy_sdl_runtime(cna_demo_input)
        endif()
    endif()

    # ---- Devices (Sensors/VibrateController) demo (works with all backends) --
    # Desktop/Emscripten only: NOT ANDROID (Task DEV-BUILD-004). A plain
    # add_executable() cannot be a valid Android app target at all --
    # <SDL3/SDL_main.h> #defines main to SDL_main on Android
    # (SDL_MAIN_NEEDED, so SDLActivity.java's dlsym("SDL_main") lookup on a
    # shared library finds a real symbol), which leaves no literal `main`
    # symbol for a plain ELF executable's C runtime startup
    # (crtbegin_dynamic.o) to link against -- confirmed via a real Android
    # cross-compile attempt: FAILS at the *link* step with "undefined
    # symbol: main", not at compile time. The real, working Android build of
    # this same demo is the entirely separate Gradle/CMake project under
    # examples/demo_devices/android/ (docs/devices-build.md Section 4.1),
    # which compiles Main.cpp/DevicesDemo.cpp into a genuine shared library
    # ("main") via its own app/jni/src/CMakeLists.txt -- not this target.
    if(NOT ANDROID)
    add_executable(cna_demo_devices
        examples/demo_devices/src/Main.cpp
        examples/demo_devices/src/DevicesDemo.cpp
    )
    target_include_directories(cna_demo_devices PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_devices/src
    )

    if(EMSCRIPTEN)
        set_target_properties(cna_demo_devices PROPERTIES SUFFIX ".html")
        target_link_libraries(cna_demo_devices PRIVATE
            CNA ${BACKEND_TARGET} SDL3::SDL3-static SHARP_RUNTIME)
        target_link_options(cna_demo_devices PRIVATE
            -sALLOW_MEMORY_GROWTH=1
            -sSTACK_SIZE=1048576
            -sINITIAL_MEMORY=67108864
            -sFORCE_FILESYSTEM=1
        )
    else()
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(cna_demo_devices PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_devices PRIVATE CNA SHARP_RUNTIME)
        endif()
        # examples/demo_devices/src/Main.cpp includes <SDL3/SDL_main.h>
        # directly (needed on Android so SDLActivity.java's dlsym("SDL_main")
        # lookup finds a real symbol) -- unlike every other demo target,
        # which never touches an SDL3 header directly and only goes through
        # CNA's own abstraction layer. CNA links SDL3::SDL3 PRIVATE (line
        # ~222 above), so that include path does not propagate transitively
        # to any executable that only links CNA. On desktop this went
        # unnoticed because the host compiler's own default system include
        # path (e.g. /usr/local/include on this container) happens to carry
        # a coincidentally-installed system SDL3 dev package -- cross-
        # compiling for Android has no such host-path fallback, and fails
        # with "SDL3/SDL_main.h file not found". Link SDL3::SDL3 directly so
        # this target gets its own real include path on every platform,
        # independent of host-system luck.
        target_link_libraries(cna_demo_devices PRIVATE SDL3::SDL3)
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_devices PRIVATE SDL3::SDL3main)
        endif()
        if(WIN32)
            set_target_properties(cna_demo_devices PROPERTIES WIN32_EXECUTABLE TRUE)
            cna_copy_sdl_runtime(cna_demo_devices)
        endif()
    endif()
    endif() # NOT ANDROID
endif()

# ---- Combined input smoke sample: Keyboard+Mouse+GamePad+Touch+TextInput -----
if(CNA_BUILD_EXAMPLES AND NOT EMSCRIPTEN AND NOT ANDROID)
    add_executable(cna_input_smoke examples/input_smoke/InputSmoke.cpp)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
        target_link_libraries(cna_input_smoke PRIVATE
            -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group SHARP_RUNTIME)
    else()
        target_link_libraries(cna_input_smoke PRIVATE CNA SHARP_RUNTIME)
    endif()
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_input_smoke PRIVATE SDL3::SDL3main)
    endif()
    if(WIN32)
        set_target_properties(cna_input_smoke PROPERTIES WIN32_EXECUTABLE TRUE)
        cna_copy_sdl_runtime(cna_input_smoke)
    endif()
endif()

# ---- NOXNA settings example (no window needed — pure API test) ---------------
if(CNA_BUILD_EXAMPLES AND CNA_NOXNA)
    add_executable(cna_example_noxna_settings examples/noxna_settings_example.cpp)
    target_link_libraries(cna_example_noxna_settings PRIVATE CNA)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_example_noxna_settings PRIVATE SDL3::SDL3main)
    endif()
    if(CNA_BUILD_TESTS)
        add_test(NAME NOXNA_Settings_Compile_Run
                 COMMAND cna_example_noxna_settings)
    endif()
endif()

# ---- Task 479: CNA-side reference-value dump tool (no window/GraphicsDevice needed) -----------
# Dumps enums, state presets, PackedVector, and Viewport reference values as JSON, mirroring
# tools/fna-reference/*.cs exactly so scripts/compare-fna-reference.py can diff the two outputs.
# Not registered as a ctest: comparing against the FNA-side harness additionally requires mono/
# xbuild and a locally-built FNA.dll (tools/fna-reference/README.md), which isn't guaranteed on
# every machine that builds CNA -- this is a manually-invoked developer verification tool.
if(CNA_BUILD_EXAMPLES AND NOT EMSCRIPTEN AND NOT ANDROID)
    add_executable(cna_reference_dump tools/cna-reference/CnaReferenceDump.cpp)
    target_link_libraries(cna_reference_dump PRIVATE CNA)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_reference_dump PRIVATE SDL3::SDL3main)
    endif()
endif()

if(CNA_BUILD_EXAMPLES AND (CNA_GRAPHICS_BACKEND STREQUAL "EASYGL" OR CNA_GRAPHICS_BACKEND STREQUAL "VULKAN"))
    # ---- 3D house demo (EasyGL and Vulkan) -----------------------------------
    add_executable(cna_house3d_demo examples/house3d_demo.cpp)

    if(EMSCRIPTEN)
        # ---- Emscripten / WebAssembly ----------------------------------------
        # Produce a self-contained HTML page. The scene is fully procedural so
        # no asset preloading is required.
        set_target_properties(cna_house3d_demo PROPERTIES SUFFIX ".html")
        # wasm-ld resolves circular static-lib references without linker groups.
        target_link_libraries(cna_house3d_demo PRIVATE
            CNA ${BACKEND_TARGET} SDL3::SDL3-static SHARP_RUNTIME)
        target_link_options(cna_house3d_demo PRIVATE
            -sALLOW_MEMORY_GROWTH=1
            -sSTACK_SIZE=1048576
            -sINITIAL_MEMORY=67108864       # 64 MB — enough for a procedural scene
            -sFORCE_FILESYSTEM=1
            "-sMIN_WEBGL_VERSION=2"         # WebGL 2 = OpenGL ES 3.0
            "-sMAX_WEBGL_VERSION=2"
        )
    else()
        # ---- Native desktop --------------------------------------------------
        # The CNA library and the EasyGL backend static lib have circular symbol
        # references (backend uses Color/Rectangle from CNA, CNA uses backend
        # interfaces); wrap them in a linker group so GNU ld resolves both.
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(cna_house3d_demo PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_house3d_demo PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_house3d_demo PRIVATE SDL3::SDL3main)
        endif()
        if(WIN32)
            set_target_properties(cna_house3d_demo PROPERTIES WIN32_EXECUTABLE TRUE)
            cna_copy_sdl_runtime(cna_house3d_demo)
        endif()
    endif()

    # Task 11.11 (Phase 11b): real windowed proof that AvatarRenderer's real-rendering
    # extension (Phase 10) draws Phase 11a's procedurally-generated, non-synthetic avatar
    # content (tools/avatar_builder/ + tools/avatar_asset_pipeline/convert_avatar.py,
    # Task 11.10). Needs CNA_GamerServices (AvatarRenderer lives there, same gating as
    # cna_test_avatar_real_render below), already inside this EASYGL/VULKAN-only scope.
    # Emscripten isn't wired up for this demo yet (deferred, no immediate need).
    if(CNA_ENABLE_NET AND NOT EMSCRIPTEN)
        add_executable(cna_demo_avatar
            examples/demo_avatar/src/Main.cpp
            examples/demo_avatar/src/AvatarDemo.cpp
        )
        target_include_directories(cna_demo_avatar PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_avatar/src
        )
        target_link_libraries(cna_demo_avatar PRIVATE CNA_GamerServices)

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(cna_demo_avatar PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_avatar PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_avatar PRIVATE SDL3::SDL3main)
        endif()
        add_custom_command(TARGET cna_demo_avatar POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_avatar/Content"
                "$<TARGET_FILE_DIR:cna_demo_avatar>/Content"
            COMMENT "Copying Content next to cna_demo_avatar executable")
        if(WIN32)
            set_target_properties(cna_demo_avatar PROPERTIES WIN32_EXECUTABLE TRUE)
            cna_copy_sdl_runtime(cna_demo_avatar)
        endif()
    endif()

    # Task 15.1: cna_demo_net_client_server_arena - real two-process NetworkSession::Create/
    # Find/Join, LocalNetworkGamer::SendData/ReceiveData via PacketWriter/PacketReader, and
    # GamerJoined/SessionEnded events over real ENet, rendered as a small 2D arena. Needs
    # CNA_GamerServices (SignedInGamer) and CNA_Net (NetworkSession) - same gating as
    # cna_demo_avatar above.
    if(CNA_ENABLE_NET AND NOT EMSCRIPTEN)
        add_executable(cna_demo_net_client_server_arena
            examples/demo_net_client_server_arena/src/Main.cpp
            examples/demo_net_client_server_arena/src/ArenaGame.cpp
        )
        target_include_directories(cna_demo_net_client_server_arena PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_net_client_server_arena/src
        )
        target_link_libraries(cna_demo_net_client_server_arena PRIVATE CNA_GamerServices CNA_Net)

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(cna_demo_net_client_server_arena PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_net_client_server_arena PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_net_client_server_arena PRIVATE SDL3::SDL3main)
        endif()
        if(WIN32)
            set_target_properties(cna_demo_net_client_server_arena PROPERTIES WIN32_EXECUTABLE TRUE)
            cna_copy_sdl_runtime(cna_demo_net_client_server_arena)
        endif()
    endif()

    # Task 15.2: cna_demo_packet_roundtrip - every XNA-type PacketWriter::Write/PacketReader::Read
    # overload (Vector2/3/4, Matrix, Quaternion, Color, float, double), single process, console-only,
    # no networking or windowing at all - needs only CNA_Net.
    if(CNA_ENABLE_NET)
        add_executable(cna_demo_packet_roundtrip
            examples/demo_packet_roundtrip/src/Main.cpp
        )
        target_link_libraries(cna_demo_packet_roundtrip PRIVATE CNA_Net SHARP_RUNTIME)
    endif()

    # Task 15.3: cna_demo_qos_probe - QualityOfService/NetworkGamer::RoundtripTime measured
    # between two real gamers over real ENet. Console-only, no windowing - needs
    # CNA_GamerServices (SignedInGamer) and CNA_Net (NetworkSession/QualityOfService).
    if(CNA_ENABLE_NET)
        add_executable(cna_demo_qos_probe
            examples/demo_qos_probe/src/Main.cpp
        )
        target_link_libraries(cna_demo_qos_probe PRIVATE CNA_GamerServices CNA_Net SHARP_RUNTIME)
    endif()

    # Task 15.4: cna_demo_simulated_network_conditions - NetworkSession::SimulatedLatencyProperty/
    # SimulatedPacketLossProperty over a real two-process ENet session, rendered as a small
    # Pong-style match. Needs CNA_GamerServices (SignedInGamer) and CNA_Net (NetworkSession) - same
    # gating as cna_demo_net_client_server_arena above.
    if(CNA_ENABLE_NET AND NOT EMSCRIPTEN)
        add_executable(cna_demo_simulated_network_conditions
            examples/demo_simulated_network_conditions/src/Main.cpp
            examples/demo_simulated_network_conditions/src/SimGame.cpp
        )
        target_include_directories(cna_demo_simulated_network_conditions PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_simulated_network_conditions/src
        )
        target_link_libraries(cna_demo_simulated_network_conditions PRIVATE CNA_GamerServices CNA_Net)

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(cna_demo_simulated_network_conditions PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_simulated_network_conditions PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_simulated_network_conditions PRIVATE SDL3::SDL3main)
        endif()
        if(WIN32)
            set_target_properties(cna_demo_simulated_network_conditions PROPERTIES WIN32_EXECUTABLE TRUE)
            cna_copy_sdl_runtime(cna_demo_simulated_network_conditions)
        endif()
    endif()

    # Task 15.5: cna_demo_session_browser - NetworkSession::Find(...)/AvailableNetworkSessionCollection
    # rendered as a scrollable session-lobby list, Up/Down to select, Enter to Join. Needs
    # CNA_GamerServices (SignedInGamer) and CNA_Net (NetworkSession) - same gating as
    # cna_demo_net_client_server_arena above.
    if(CNA_ENABLE_NET AND NOT EMSCRIPTEN)
        add_executable(cna_demo_session_browser
            examples/demo_session_browser/src/Main.cpp
            examples/demo_session_browser/src/BrowserGame.cpp
        )
        target_include_directories(cna_demo_session_browser PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_session_browser/src
        )
        target_link_libraries(cna_demo_session_browser PRIVATE CNA_GamerServices CNA_Net)

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(cna_demo_session_browser PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_session_browser PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_session_browser PRIVATE SDL3::SDL3main)
        endif()
        if(WIN32)
            set_target_properties(cna_demo_session_browser PROPERTIES WIN32_EXECUTABLE TRUE)
            cna_copy_sdl_runtime(cna_demo_session_browser)
        endif()
    endif()

    # Task 15.6: cna_demo_gamer_roster_hud - the full gamer-roster event surface (GamerJoined/
    # GamerLeft/HostChanged/SessionEnded) plus per-gamer IsHost/IsLocal/IsReady/IsTalking flags,
    # rendered as a live roster panel. Needs CNA_GamerServices (SignedInGamer) and CNA_Net
    # (NetworkSession) - same gating as cna_demo_net_client_server_arena above.
    if(CNA_ENABLE_NET AND NOT EMSCRIPTEN)
        add_executable(cna_demo_gamer_roster_hud
            examples/demo_gamer_roster_hud/src/Main.cpp
            examples/demo_gamer_roster_hud/src/RosterGame.cpp
        )
        target_include_directories(cna_demo_gamer_roster_hud PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_gamer_roster_hud/src
        )
        target_link_libraries(cna_demo_gamer_roster_hud PRIVATE CNA_GamerServices CNA_Net)

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(cna_demo_gamer_roster_hud PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_gamer_roster_hud PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_gamer_roster_hud PRIVATE SDL3::SDL3main)
        endif()
        if(WIN32)
            set_target_properties(cna_demo_gamer_roster_hud PROPERTIES WIN32_EXECUTABLE TRUE)
            cna_copy_sdl_runtime(cna_demo_gamer_roster_hud)
        endif()
    endif()

    # Task 15.7: cna_demo_session_lifecycle_events - NetworkSession::StartGame()/EndGame(),
    # NetworkSessionState transitions, GameStarted/GameEnded, and a manual Raise() of the 3
    # leaderboard delegates. Console-only, single process, NetworkSessionType::Local (no real
    # networking) - needs CNA_GamerServices (SignedInGamer) and CNA_Net (NetworkSession).
    if(CNA_ENABLE_NET)
        add_executable(cna_demo_session_lifecycle_events
            examples/demo_session_lifecycle_events/src/Main.cpp
        )
        target_link_libraries(cna_demo_session_lifecycle_events PRIVATE CNA_GamerServices CNA_Net SHARP_RUNTIME)
    endif()

    # Task 15.8: cna_demo_gamerservices_signin_presence - real GamerServicesComponent
    # registration, Gamer::SignedInGamers population, SignedInGamer::SignedIn/SignedOut, and
    # GamerPresence. Needs CNA_GamerServices (SignedInGamer/GamerServicesComponent) and windowing
    # (SpriteBatch/SpriteFont HUD) - same gating as cna_demo_avatar above.
    if(CNA_ENABLE_NET AND NOT EMSCRIPTEN)
        add_executable(cna_demo_gamerservices_signin_presence
            examples/demo_gamerservices_signin_presence/src/Main.cpp
            examples/demo_gamerservices_signin_presence/src/PresenceGame.cpp
        )
        target_include_directories(cna_demo_gamerservices_signin_presence PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_gamerservices_signin_presence/src
        )
        target_link_libraries(cna_demo_gamerservices_signin_presence PRIVATE CNA_GamerServices)

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(cna_demo_gamerservices_signin_presence PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_gamerservices_signin_presence PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_gamerservices_signin_presence PRIVATE SDL3::SDL3main)
        endif()
        if(WIN32)
            set_target_properties(cna_demo_gamerservices_signin_presence PROPERTIES WIN32_EXECUTABLE TRUE)
            cna_copy_sdl_runtime(cna_demo_gamerservices_signin_presence)
        endif()
    endif()

    # Task 15.9: cna_demo_achievement_showcase - Achievement/AchievementCollection,
    # SignedInGamer::AwardAchievement/GetAchievements. Needs CNA_GamerServices and windowing
    # (SpriteBatch/SpriteFont HUD) - same gating as cna_demo_avatar above.
    if(CNA_ENABLE_NET AND NOT EMSCRIPTEN)
        add_executable(cna_demo_achievement_showcase
            examples/demo_achievement_showcase/src/Main.cpp
            examples/demo_achievement_showcase/src/AchievementGame.cpp
        )
        target_include_directories(cna_demo_achievement_showcase PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_achievement_showcase/src
        )
        target_link_libraries(cna_demo_achievement_showcase PRIVATE CNA_GamerServices)

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(cna_demo_achievement_showcase PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_achievement_showcase PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_achievement_showcase PRIVATE SDL3::SDL3main)
        endif()
        if(WIN32)
            set_target_properties(cna_demo_achievement_showcase PROPERTIES WIN32_EXECUTABLE TRUE)
            cna_copy_sdl_runtime(cna_demo_achievement_showcase)
        endif()
    endif()

    # Task 15.10: cna_demo_leaderboard_viewer - LeaderboardReader (CanPageUp/Down, Entries,
    # PageStart) plus LeaderboardWriter::GetLeaderboard's always-throws boundary. Needs
    # CNA_GamerServices and windowing (SpriteBatch/SpriteFont HUD) - same gating as
    # cna_demo_avatar above.
    if(CNA_ENABLE_NET AND NOT EMSCRIPTEN)
        add_executable(cna_demo_leaderboard_viewer
            examples/demo_leaderboard_viewer/src/Main.cpp
            examples/demo_leaderboard_viewer/src/LeaderboardGame.cpp
        )
        target_include_directories(cna_demo_leaderboard_viewer PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_leaderboard_viewer/src
        )
        target_link_libraries(cna_demo_leaderboard_viewer PRIVATE CNA_GamerServices)

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(cna_demo_leaderboard_viewer PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_leaderboard_viewer PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_leaderboard_viewer PRIVATE SDL3::SDL3main)
        endif()
        if(WIN32)
            set_target_properties(cna_demo_leaderboard_viewer PROPERTIES WIN32_EXECUTABLE TRUE)
            cna_copy_sdl_runtime(cna_demo_leaderboard_viewer)
        endif()
    endif()

    # Task 15.11: cna_demo_guide_overlay_console - the full Guide static API surface via a
    # numbered console menu. Console-only, single process, no graphics/window needed - needs only
    # CNA_GamerServices.
    if(CNA_ENABLE_NET)
        add_executable(cna_demo_guide_overlay_console
            examples/demo_guide_overlay_console/src/Main.cpp
        )
        target_link_libraries(cna_demo_guide_overlay_console PRIVATE CNA_GamerServices SHARP_RUNTIME)
    endif()

    # Task 15.12: cna_demo_gamerservices_dispatcher_watchdog - visual proof that Task 12.1's/Task
    # 7.1's historical GamerServicesDispatcher/NetworkSession/GetAchievements hangs are fixed.
    # Needs CNA_GamerServices and CNA_Net, plus windowing for the "waiting.../SUCCESS" HUD - same
    # gating as cna_demo_net_client_server_arena above.
    if(CNA_ENABLE_NET AND NOT EMSCRIPTEN)
        add_executable(cna_demo_gamerservices_dispatcher_watchdog
            examples/demo_gamerservices_dispatcher_watchdog/src/Main.cpp
            examples/demo_gamerservices_dispatcher_watchdog/src/WatchdogGame.cpp
        )
        target_include_directories(cna_demo_gamerservices_dispatcher_watchdog PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_gamerservices_dispatcher_watchdog/src
        )
        target_link_libraries(cna_demo_gamerservices_dispatcher_watchdog PRIVATE CNA_GamerServices CNA_Net)

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(cna_demo_gamerservices_dispatcher_watchdog PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_gamerservices_dispatcher_watchdog PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_gamerservices_dispatcher_watchdog PRIVATE SDL3::SDL3main)
        endif()
        if(WIN32)
            set_target_properties(cna_demo_gamerservices_dispatcher_watchdog PROPERTIES WIN32_EXECUTABLE TRUE)
            cna_copy_sdl_runtime(cna_demo_gamerservices_dispatcher_watchdog)
        endif()
    endif()

    # Task 15.13: cna_demo_gamer_profile_privileges - GamerProfile/GamerPrivileges via
    # Gamer::GetProfile()/SignedInGamer::getPrivilegesProperty(). Needs CNA_GamerServices and
    # windowing - same gating as cna_demo_avatar above.
    if(CNA_ENABLE_NET AND NOT EMSCRIPTEN)
        add_executable(cna_demo_gamer_profile_privileges
            examples/demo_gamer_profile_privileges/src/Main.cpp
            examples/demo_gamer_profile_privileges/src/ProfileGame.cpp
        )
        target_include_directories(cna_demo_gamer_profile_privileges PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_gamer_profile_privileges/src
        )
        target_link_libraries(cna_demo_gamer_profile_privileges PRIVATE CNA_GamerServices)

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(cna_demo_gamer_profile_privileges PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_gamer_profile_privileges PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_gamer_profile_privileges PRIVATE SDL3::SDL3main)
        endif()
        if(WIN32)
            set_target_properties(cna_demo_gamer_profile_privileges PROPERTIES WIN32_EXECUTABLE TRUE)
            cna_copy_sdl_runtime(cna_demo_gamer_profile_privileges)
        endif()
    endif()

    # Task 15.14: cna_demo_friends_and_gamercard - FriendCollection (via CreateInternal) and the
    # no-op Guide::ShowGamerCard/ShowFriendRequest/ShowFriends/ShowComposeMessage calls. Needs
    # CNA_GamerServices and windowing - same gating as cna_demo_avatar above.
    if(CNA_ENABLE_NET AND NOT EMSCRIPTEN)
        add_executable(cna_demo_friends_and_gamercard
            examples/demo_friends_and_gamercard/src/Main.cpp
            examples/demo_friends_and_gamercard/src/FriendsGame.cpp
        )
        target_include_directories(cna_demo_friends_and_gamercard PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_friends_and_gamercard/src
        )
        target_link_libraries(cna_demo_friends_and_gamercard PRIVATE CNA_GamerServices)

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(cna_demo_friends_and_gamercard PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_friends_and_gamercard PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_friends_and_gamercard PRIVATE SDL3::SDL3main)
        endif()
        if(WIN32)
            set_target_properties(cna_demo_friends_and_gamercard PROPERTIES WIN32_EXECUTABLE TRUE)
            cna_copy_sdl_runtime(cna_demo_friends_and_gamercard)
        endif()
    endif()

    # Task 15.15: cna_demo_avatar_animation_gallery - a completionist version of demo_avatar's
    # Space-cycling, iterating all 31 AvatarAnimationPreset values across both genders. Reuses
    # demo_avatar's Content/ directory (copied by CMake, not duplicated) - same gating as
    # cna_demo_avatar above.
    if(CNA_ENABLE_NET AND NOT EMSCRIPTEN)
        add_executable(cna_demo_avatar_animation_gallery
            examples/demo_avatar_animation_gallery/src/Main.cpp
            examples/demo_avatar_animation_gallery/src/GalleryDemo.cpp
        )
        target_include_directories(cna_demo_avatar_animation_gallery PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_avatar_animation_gallery/src
        )
        target_link_libraries(cna_demo_avatar_animation_gallery PRIVATE CNA_GamerServices)

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(cna_demo_avatar_animation_gallery PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_avatar_animation_gallery PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_avatar_animation_gallery PRIVATE SDL3::SDL3main)
        endif()
        add_custom_command(TARGET cna_demo_avatar_animation_gallery POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_avatar/Content"
                "$<TARGET_FILE_DIR:cna_demo_avatar_animation_gallery>/Content"
            COMMENT "Copying demo_avatar's Content next to cna_demo_avatar_animation_gallery executable")
        if(WIN32)
            set_target_properties(cna_demo_avatar_animation_gallery PROPERTIES WIN32_EXECUTABLE TRUE)
            cna_copy_sdl_runtime(cna_demo_avatar_animation_gallery)
        endif()
    endif()

    # Task 15.16: cna_demo_avatar_wardrobe_hotswap - SkinnedModelEXT::AttachPartEXT/RemovePartEXT
    # used repeatedly at runtime (Tab cycles baked-in hair / hair_Cap / hair_Ponytail). Reuses
    # demo_avatar's Content/ directory (copied by CMake, not duplicated) - same gating as
    # cna_demo_avatar above.
    if(CNA_ENABLE_NET AND NOT EMSCRIPTEN)
        add_executable(cna_demo_avatar_wardrobe_hotswap
            examples/demo_avatar_wardrobe_hotswap/src/Main.cpp
            examples/demo_avatar_wardrobe_hotswap/src/HotswapDemo.cpp
        )
        target_include_directories(cna_demo_avatar_wardrobe_hotswap PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_avatar_wardrobe_hotswap/src
        )
        target_link_libraries(cna_demo_avatar_wardrobe_hotswap PRIVATE CNA_GamerServices)

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(cna_demo_avatar_wardrobe_hotswap PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_avatar_wardrobe_hotswap PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_avatar_wardrobe_hotswap PRIVATE SDL3::SDL3main)
        endif()
        add_custom_command(TARGET cna_demo_avatar_wardrobe_hotswap POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_avatar/Content"
                "$<TARGET_FILE_DIR:cna_demo_avatar_wardrobe_hotswap>/Content"
            COMMENT "Copying demo_avatar's Content next to cna_demo_avatar_wardrobe_hotswap executable")
        if(WIN32)
            set_target_properties(cna_demo_avatar_wardrobe_hotswap PROPERTIES WIN32_EXECUTABLE TRUE)
            cna_copy_sdl_runtime(cna_demo_avatar_wardrobe_hotswap)
        endif()
    endif()

    # Task 15.17: cna_demo_avatar_appearance_tint_studio - AvatarAppearanceEXT's 5 tint slots and
    # AvatarRenderer::SetAppearanceEXT as a live color customization screen. Reuses demo_avatar's
    # Content/ directory (copied by CMake, not duplicated) - same gating as cna_demo_avatar above.
    if(CNA_ENABLE_NET AND NOT EMSCRIPTEN)
        add_executable(cna_demo_avatar_appearance_tint_studio
            examples/demo_avatar_appearance_tint_studio/src/Main.cpp
            examples/demo_avatar_appearance_tint_studio/src/TintStudioDemo.cpp
        )
        target_include_directories(cna_demo_avatar_appearance_tint_studio PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_avatar_appearance_tint_studio/src
        )
        target_link_libraries(cna_demo_avatar_appearance_tint_studio PRIVATE CNA_GamerServices)

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(cna_demo_avatar_appearance_tint_studio PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_avatar_appearance_tint_studio PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_avatar_appearance_tint_studio PRIVATE SDL3::SDL3main)
        endif()
        add_custom_command(TARGET cna_demo_avatar_appearance_tint_studio POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_avatar/Content"
                "$<TARGET_FILE_DIR:cna_demo_avatar_appearance_tint_studio>/Content"
            COMMENT "Copying demo_avatar's Content next to cna_demo_avatar_appearance_tint_studio executable")
        if(WIN32)
            set_target_properties(cna_demo_avatar_appearance_tint_studio PROPERTIES WIN32_EXECUTABLE TRUE)
            cna_copy_sdl_runtime(cna_demo_avatar_appearance_tint_studio)
        endif()
    endif()

    # Task 15.18: cna_demo_avatar_dual_compare - two independent AvatarRenderer/SkinnedModelEXT
    # instances alive and drawing simultaneously. Reuses demo_avatar's Content/ directory (copied
    # by CMake, not duplicated) - same gating as cna_demo_avatar above.
    if(CNA_ENABLE_NET AND NOT EMSCRIPTEN)
        add_executable(cna_demo_avatar_dual_compare
            examples/demo_avatar_dual_compare/src/Main.cpp
            examples/demo_avatar_dual_compare/src/DualCompareDemo.cpp
        )
        target_include_directories(cna_demo_avatar_dual_compare PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_avatar_dual_compare/src
        )
        target_link_libraries(cna_demo_avatar_dual_compare PRIVATE CNA_GamerServices)

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(cna_demo_avatar_dual_compare PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_avatar_dual_compare PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_avatar_dual_compare PRIVATE SDL3::SDL3main)
        endif()
        add_custom_command(TARGET cna_demo_avatar_dual_compare POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_avatar/Content"
                "$<TARGET_FILE_DIR:cna_demo_avatar_dual_compare>/Content"
            COMMENT "Copying demo_avatar's Content next to cna_demo_avatar_dual_compare executable")
        if(WIN32)
            set_target_properties(cna_demo_avatar_dual_compare PROPERTIES WIN32_EXECUTABLE TRUE)
            cna_copy_sdl_runtime(cna_demo_avatar_dual_compare)
        endif()
    endif()

    # Task 15.19: cna_demo_avatar_multi_attach_stress - SkinnedModelEXT::AttachPartEXT called
    # repeatedly (hair variants, then a growing series of synthetic quad accessories), proving
    # accumulation doesn't break skinning/tinting as Parts.size() grows. Reuses demo_avatar's
    # Content/ directory (copied by CMake, not duplicated) - same gating as cna_demo_avatar above.
    if(CNA_ENABLE_NET AND NOT EMSCRIPTEN)
        add_executable(cna_demo_avatar_multi_attach_stress
            examples/demo_avatar_multi_attach_stress/src/Main.cpp
            examples/demo_avatar_multi_attach_stress/src/StressDemo.cpp
        )
        target_include_directories(cna_demo_avatar_multi_attach_stress PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_avatar_multi_attach_stress/src
        )
        target_link_libraries(cna_demo_avatar_multi_attach_stress PRIVATE CNA_GamerServices)

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(cna_demo_avatar_multi_attach_stress PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_avatar_multi_attach_stress PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_avatar_multi_attach_stress PRIVATE SDL3::SDL3main)
        endif()
        add_custom_command(TARGET cna_demo_avatar_multi_attach_stress POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_avatar/Content"
                "$<TARGET_FILE_DIR:cna_demo_avatar_multi_attach_stress>/Content"
            COMMENT "Copying demo_avatar's Content next to cna_demo_avatar_multi_attach_stress executable")
        if(WIN32)
            set_target_properties(cna_demo_avatar_multi_attach_stress PROPERTIES WIN32_EXECUTABLE TRUE)
            cna_copy_sdl_runtime(cna_demo_avatar_multi_attach_stress)
        endif()
    endif()

    # Task 15.20: cna_demo_avatar_bone_state_boundary - documents the real, verified
    # AvatarRenderer skeleton-API boundary (State/ParentBones/BindPose) vs the working
    # SkinnedModelEXT EXT path. Reuses demo_avatar's Content/ directory (copied by CMake, not
    # duplicated) - same gating as cna_demo_avatar above.
    if(CNA_ENABLE_NET AND NOT EMSCRIPTEN)
        add_executable(cna_demo_avatar_bone_state_boundary
            examples/demo_avatar_bone_state_boundary/src/Main.cpp
            examples/demo_avatar_bone_state_boundary/src/BoundaryDemo.cpp
        )
        target_include_directories(cna_demo_avatar_bone_state_boundary PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_avatar_bone_state_boundary/src
        )
        target_link_libraries(cna_demo_avatar_bone_state_boundary PRIVATE CNA_GamerServices)

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(cna_demo_avatar_bone_state_boundary PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_avatar_bone_state_boundary PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_avatar_bone_state_boundary PRIVATE SDL3::SDL3main)
        endif()
        add_custom_command(TARGET cna_demo_avatar_bone_state_boundary POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_avatar/Content"
                "$<TARGET_FILE_DIR:cna_demo_avatar_bone_state_boundary>/Content"
            COMMENT "Copying demo_avatar's Content next to cna_demo_avatar_bone_state_boundary executable")
        if(WIN32)
            set_target_properties(cna_demo_avatar_bone_state_boundary PROPERTIES WIN32_EXECUTABLE TRUE)
            cna_copy_sdl_runtime(cna_demo_avatar_bone_state_boundary)
        endif()
    endif()

    # Task 15.21 (bonus, cross-cutting): cna_demo_net_avatar_sync - combines Net + Avatar over a
    # real two-process ENet session, each side rendering both its own and the remote peer's real
    # avatar. Needs CNA_GamerServices/CNA_Net (real networking) plus demo_avatar's Content/
    # directory (copied by CMake, not duplicated) - same gating as cna_demo_net_client_server_arena
    # above.
    if(CNA_ENABLE_NET AND NOT EMSCRIPTEN)
        add_executable(cna_demo_net_avatar_sync
            examples/demo_net_avatar_sync/src/Main.cpp
            examples/demo_net_avatar_sync/src/SyncGame.cpp
        )
        target_include_directories(cna_demo_net_avatar_sync PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_net_avatar_sync/src
        )
        target_link_libraries(cna_demo_net_avatar_sync PRIVATE CNA_GamerServices CNA_Net)

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(cna_demo_net_avatar_sync PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_net_avatar_sync PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_net_avatar_sync PRIVATE SDL3::SDL3main)
        endif()
        add_custom_command(TARGET cna_demo_net_avatar_sync POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_avatar/Content"
                "$<TARGET_FILE_DIR:cna_demo_net_avatar_sync>/Content"
            COMMENT "Copying demo_avatar's Content next to cna_demo_net_avatar_sync executable")
        if(WIN32)
            set_target_properties(cna_demo_net_avatar_sync PROPERTIES WIN32_EXECUTABLE TRUE)
            cna_copy_sdl_runtime(cna_demo_net_avatar_sync)
        endif()
    endif()
endif()

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

# --- Task 6.1: real two-process ENet loopback test harness ---
# A tiny standalone (non-GTest) executable playing "host" or "client", spawned twice as
# independent OS processes by tests/CNA/Internal/Net/TwoProcessLoopbackTest.cpp. See NEXT.md /
# the approved Phase 6 plan for why this exists separately from every Phase 5 single-process test.
# Uses POSIX-only <sys/resource.h>/posix_spawn -- excluded on the same platforms its own consumer
# test (TwoProcessLoopbackTest.cpp) already is, above, rather than only excluding the test source
# and leaving this executable target to fail the build on its own.
if(CNA_ENABLE_NET AND CNA_BUILD_TESTS AND NOT (WIN32 OR EMSCRIPTEN OR ANDROID))
    add_executable(cna_net_two_process_harness
        tools/net/net_two_process_harness.cpp
    )
    target_link_libraries(cna_net_two_process_harness
        PRIVATE
        CNA_GamerServices
        CNA_Net
        SHARP_RUNTIME
    )
endif()

# --- Task 12.1: GamerServicesDispatcher hang regression harness ---
# A tiny standalone (non-GTest) executable that calls GamerServicesDispatcher::Initialize() then
# NetworkSession::Create(), reproducing DEFERRED.md item #19 (../cna-samples) if it regresses.
# Must be a separate process, same reasoning as cna_net_two_process_harness above but for a
# different process-global-state hazard — see the harness file's own top-of-file comment.
if(CNA_ENABLE_NET AND CNA_BUILD_TESTS)
    add_executable(cna_net_gamerservices_dispatcher_harness
        tools/net/gamerservices_dispatcher_harness.cpp
    )
    target_link_libraries(cna_net_gamerservices_dispatcher_harness
        PRIVATE
        CNA_GamerServices
        CNA_Net
        SHARP_RUNTIME
    )
endif()

# --- Task P9-HARDWARE-005: standalone no-audio-hardware harness ---
# A tiny standalone (non-GTest) executable that forces SDL_AUDIODRIVER to a nonexistent driver
# name before any SDL audio call in this fresh process, then proves NoAudioHardwareException is
# genuinely thrown. Spawned by tests/CNA/Internal/Audio/AudioMixerTests.cpp -- see that file /
# tools/audio/audio_no_hardware_harness.cpp for why this needs its own process (AudioMixer.cpp's
# g_mixer is a process-wide, once-ever-initialized cache).
if(CNA_BUILD_TESTS)
    add_executable(cna_audio_no_hardware_harness
        tools/audio/audio_no_hardware_harness.cpp
    )
    target_link_libraries(cna_audio_no_hardware_harness
        PRIVATE
        CNA
        SHARP_RUNTIME
    )
endif()

# --- plan_software.md SOFTWARE-61/84: cross-backend diagnostic comparator ---
# Standalone, backend-independent (no CNA/SHARP_RUNTIME dependency) tool that diffs two raw
# 64x64 RGBA8 dumps produced by cross_backend_diagnostic_scene.cpp (built once per backend, see
# the SOFTWARE and EASYGL sections below) within a tolerance. Not wired into a single ctest run --
# CNA_GRAPHICS_BACKEND is a compile-time choice, so comparing two backends' output needs two
# separate builds' dumps; see docs/software-backend.md's "Cross-backend diagnostic" section for
# the exact manual invocation.
if(CNA_BUILD_TESTS)
    add_executable(cna_diag_compare examples/cross_backend_diagnostic_compare.cpp)
endif()

# --- Task VERIFY-003/DEV-API-002: strict XNA API surface compile check ---
# Compiles tools/devices/StrictXnaApiSurfaceCheck.cpp with CNA_STRICT_XNA_API
# defined (turns the NOXNA macro into [[deprecated]], see CNAHelper.hpp) and
# -Werror=deprecated-declarations, so this target fails to build if that file
# ever calls a NOXNA-tagged Microsoft::Devices/Sensors member. A standalone
# executable, not a gtest binary — same tools/ precedent as
# cna_net_two_process_harness above, for the same reason (its own main()).
if(CNA_BUILD_TESTS AND (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang"))
    add_executable(cna_strict_xna_api_check
        tools/devices/StrictXnaApiSurfaceCheck.cpp
    )
    target_link_libraries(cna_strict_xna_api_check PRIVATE CNA)
    target_compile_definitions(cna_strict_xna_api_check PRIVATE CNA_STRICT_XNA_API)
    target_compile_options(cna_strict_xna_api_check PRIVATE -Werror=deprecated-declarations)
    add_test(NAME StrictXnaApiSurfaceCheck_Compile_Run COMMAND cna_strict_xna_api_check)
endif()

if(CNA_BUILD_TESTS)
    add_subdirectory(vendor/googletest)

    enable_testing()

    file(GLOB_RECURSE CNA_TEST_SOURCES CONFIGURE_DEPENDS
            "tests/*.cpp"
    )

    # TwoProcessLoopbackTest.cpp (Task 6.1) uses POSIX-only process APIs (posix_spawn, poll,
    # sys/wait.h) to orchestrate two independent OS processes. Compiles under mingw-w64 and
    # Emscripten (both provide POSIX-ish libc stubs) but can't actually work under either: no real
    # multi-process spawning exists in a single Node.js/Wasm module. Also excluded on Android: the
    # harness path baked in via CNA_NET_HARNESS_PATH is an absolute path on the build machine, not
    # the on-device filesystem, and CnaTests is run as a bare pushed executable, not a packaged app
    # with its own bundled assets. Not part of the Task 6.2/6.4 verification filters
    # (*Network*:*Gamer*:*ENet*:*Packet*) either, since its suite name is TwoProcessLoopbackTest.
    if(WIN32 OR EMSCRIPTEN OR ANDROID)
        list(FILTER CNA_TEST_SOURCES EXCLUDE REGEX ".*/CNA/Internal/Net/TwoProcessLoopbackTest\\.cpp$")
        # GamerServicesDispatcherHangRegressionTest.cpp (Task 12.1) uses the same POSIX-only
        # posix_spawn/sys-wait process APIs, for the same reasons — see TwoProcessLoopbackTest's
        # own exclusion comment just above.
        list(FILTER CNA_TEST_SOURCES EXCLUDE REGEX ".*/CNA/Internal/Net/GamerServicesDispatcherHangRegressionTest\\.cpp$")
    endif()

    # AudioMixerTests.cpp (Task P9-HARDWARE-005) uses the same POSIX-only process APIs
    # (posix_spawn, poll, sys/wait.h) to spawn tools/audio/audio_no_hardware_harness.cpp as an
    # independent OS process, for the same reason TwoProcessLoopbackTest.cpp needs one above.
    # Excluded on the same platforms and for the same reasons (no real process spawning in a
    # single Node.js/Wasm module; CNA_AUDIO_NO_HARDWARE_HARNESS_PATH is a build-machine absolute
    # path, meaningless on-device on Android).
    if(WIN32 OR EMSCRIPTEN OR ANDROID)
        list(FILTER CNA_TEST_SOURCES EXCLUDE REGEX ".*/CNA/Internal/Audio/AudioMixerTests\\.cpp$")
    endif()

    add_executable(CnaTests
            ${CNA_TEST_SOURCES}
    )

    # mingw-w64's <cmath> only exposes M_PI when _USE_MATH_DEFINES is set (unlike glibc, which
    # defines it unconditionally) — a handful of test files use M_PI directly as a reference value.
    if(MINGW)
        target_compile_definitions(CnaTests PRIVATE _USE_MATH_DEFINES)
        # Known mingw-w64/GCC PE-COFF toolchain limitation: std::type_info::operator== is emitted
        # as vague linkage (COMDAT) in libstdc++.a, but a test binary this large (many RTTI-using
        # TUs) trips a case where the linker fails to fold it against the identical libstdc++
        # definition. Both copies are byte-identical, so allowing the duplicate is safe.
        target_link_options(CnaTests PRIVATE -Wl,--allow-multiple-definition)
    endif()

    if(EMSCRIPTEN)
        # Emscripten's EXIT_RUNTIME defaults to 0 (deliberately keeps the JS runtime alive after
        # main() returns, so pending async JS/WebSocket handles can keep running) - without this,
        # `node CnaTests.js` never exits after printing its results, even on success.
        target_link_options(CnaTests PRIVATE -sEXIT_RUNTIME=1)

        # Emscripten's default build is fully synchronous/single-threaded: a real WebSocket
        # handshake structurally cannot complete while C++ code holds the call stack (confirmed
        # empirically - even a real 1s sleep loop never lets it finish, since nothing returns
        # control to Node's event loop). The SystemLink loopback tests need emscripten_sleep() to
        # actually yield back to Node between polls, which requires Asyncify. Scoped to CnaTests
        # only (a per-executable link-time transformation - does not affect native/Windows builds
        # or any other Emscripten executable target such as the demos or the two-process harness).
        target_link_options(CnaTests PRIVATE -sASYNCIFY=1)
    endif()

    target_link_libraries(CnaTests
            PRIVATE
            CNA
            SHARP_RUNTIME
            gtest_main
            SDL3::SDL3
    )

    if(CNA_ENABLE_NET)
        target_link_libraries(CnaTests PRIVATE CNA_GamerServices CNA_Net)
    endif()

    if(TARGET cna_net_two_process_harness)
        # Building CnaTests must also build the harness it spawns at test time (they're separate
        # executables, so CMake won't infer this dependency on its own), and the orchestrator test
        # needs the harness's real built path baked in at compile time.
        add_dependencies(CnaTests cna_net_two_process_harness)
        target_compile_definitions(CnaTests PRIVATE
            CNA_NET_HARNESS_PATH="$<TARGET_FILE:cna_net_two_process_harness>"
        )
    endif()

    if(TARGET cna_net_gamerservices_dispatcher_harness)
        # Same reasoning as cna_net_two_process_harness just above.
        add_dependencies(CnaTests cna_net_gamerservices_dispatcher_harness)
        target_compile_definitions(CnaTests PRIVATE
            CNA_NET_GAMERSERVICES_HANG_HARNESS_PATH="$<TARGET_FILE:cna_net_gamerservices_dispatcher_harness>"
        )
    endif()

    if(TARGET cna_audio_no_hardware_harness)
        # Same reasoning as cna_net_two_process_harness above, for AudioMixerTests.cpp.
        add_dependencies(CnaTests cna_audio_no_hardware_harness)
        target_compile_definitions(CnaTests PRIVATE
            CNA_AUDIO_NO_HARDWARE_HARNESS_PATH="$<TARGET_FILE:cna_audio_no_hardware_harness>"
        )
    endif()

    if(TARGET easy-gl)
        target_link_libraries(CnaTests PRIVATE easy-gl)
    endif()

    # plan_dx.md DX-15 follow-up + plan_dx9.md D9-123 follow-up (merge-reconciled 2026-07-16):
    # now that CnaTests.exe genuinely builds under the D3D9/D3D11/D3D12 MinGW cross-targets,
    # gtest_discover_tests(DISCOVERY_MODE PRE_TEST) below executes it directly to enumerate tests
    # -- and any add_test(COMMAND CnaTests ...) test (e.g. CnaInputTests, further below) does the
    # same at run time. All three need the same Wine wrapper D3D9_Smoke/D3D11_Smoke/D3D12_Smoke
    # already use, or every ctest invocation in this build tree fails outright trying to exec a
    # Windows PE natively on the Linux host. *_SKIP_*_GATE=1 because a bare --gtest_list_tests (or
    # most individual unit tests) never creates a real graphics device, so the DXVK/vkd3d-presence
    # gate those wrappers normally enforce would misfire here. MUST be set before
    # gtest_discover_tests() below -- it reads this property immediately at configure time, not
    # lazily at generate/test time. `CMAKE_CROSSCOMPILING` is used as the outer guard (equivalent
    # to `MINGW` for every real build configuration this project uses, since these three backends
    # are only ever built via the MinGW cross-toolchain) so a D3D9 branch could be added without
    # touching D3D11/D3D12's own already-working invocation style.
    if(CMAKE_CROSSCOMPILING)
        if(CNA_GRAPHICS_BACKEND STREQUAL "D3D9")
            # D9-123: this backend's own wrapper takes a simpler `env;VAR=1;script` form (no
            # `${CMAKE_COMMAND} -E env`/explicit `bash` prefix) -- proven end-to-end for D3D9
            # (`ctest -L D3D9` green) and for D3D11 when reused verbatim there too (Task 1106).
            set_target_properties(CnaTests PROPERTIES CROSSCOMPILING_EMULATOR
                "env;CNA_D3D9_SKIP_DXVK_GATE=1;${CMAKE_SOURCE_DIR}/scripts/run-wine-dxvk9.sh")
        elseif(CNA_GRAPHICS_BACKEND STREQUAL "D3D11")
            set_target_properties(CnaTests PROPERTIES
                CROSSCOMPILING_EMULATOR "${CMAKE_COMMAND};-E;env;CNA_D3D11_SKIP_DXVK_GATE=1;bash;${CMAKE_SOURCE_DIR}/scripts/run-wine-dxvk.sh")
        elseif(CNA_GRAPHICS_BACKEND STREQUAL "D3D12")
            set_target_properties(CnaTests PROPERTIES
                CROSSCOMPILING_EMULATOR "${CMAKE_COMMAND};-E;env;CNA_D3D12_SKIP_VKD3D_GATE=1;bash;${CMAKE_SOURCE_DIR}/scripts/run-wine-vkd3d.sh")
        endif()
    endif()

    include(GoogleTest)
    gtest_discover_tests(CnaTests DISCOVERY_MODE PRE_TEST)

    # INPUT-BUILD-003: canonical Input-test selector — SINGLE SOURCE OF TRUTH.
    # The input subset used to be selected by a long --gtest_filter string copy-pasted across the docs
    # and CI, which silently drifted whenever a new suite name fell outside its tokens (e.g. the
    # ButtonState/KeyState/Buttons suites from INPUT-TEST-001). Keep the token list HERE only; docs and
    # CI select the subset via `ctest -L input` (see docs/input-build-and-test.md). When you add an input
    # test suite whose name matches none of these tokens, extend this one string.
    set(CNA_INPUT_TEST_FILTER
        "*Keyboard*:*Mouse*:*GamePad*:*Touch*:*Gesture*:*TextInput*:*SdlInputBridge*:*InputResetAllForTests*:*FakeGamepad*:*SdlGamepadSubsystemInit*:*ButtonState*:*KeyState*:*Buttons*:*PublicApiInput*:*CnaInput*:*Joystick*:*Haptic*")
    # INPUT-BUILD-009: `--gtest_shuffle --gtest_repeat=5` is the standardized order-independence gate.
    # The input state is a process-wide singleton (InputManager / GestureDetector / MouseCursor stock
    # cursors persist for the whole binary), so a static-state leak would reintroduce order dependence;
    # running the filtered subset 5x under a fresh shuffle each iteration is the required check that
    # catches it. `ctest -L input` runs exactly this on every backend/CI job.
    cna_register_backend_test(NAME CnaInputTests COMMAND CnaTests --gtest_filter=${CNA_INPUT_TEST_FILTER} --gtest_shuffle --gtest_repeat=5
        LABELS "input" ENVIRONMENT "SDL_AUDIODRIVER=dummy")

    if(MINGW)
        # Statically link MinGW runtime into the test binary too, so it can
        # run outside CLion without libgcc_s_seh-1.dll / libstdc++-6.dll.
        target_link_options(CnaTests PRIVATE -static-libgcc -static-libstdc++)
        # libwinpthread-1.dll cannot be statically linked; copy it next to the exe.
        cna_copy_mingw_runtime(CnaTests)
    endif()

    if(WIN32)
        cna_copy_sdl_runtime(CnaTests)
        foreach(_gtest_target IN ITEMS gtest gtest_main gmock gmock_main)
            if(TARGET ${_gtest_target})
                add_custom_command(TARGET CnaTests POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        $<TARGET_FILE:${_gtest_target}>
                        $<TARGET_FILE_DIR:CnaTests>
                    VERBATIM)
            endif()
        endforeach()
    endif()
endif()

# Task 470: headless-safe CTest skip convention. A test that self-detects "no GPU/display
# available" (e.g. SDL_InitSubSystem(SDL_INIT_VIDEO) failing, see
# examples/common/PixelTestGame.hpp's RunPixelTest()) and exits with this sentinel code is
# reported by ctest as SKIPPED rather than FAILED. Applied here in one shot to every test
# already registered anywhere in this file, rather than adding a SKIP_RETURN_CODE property to
# each individual set_tests_properties() call across ~330 existing test registrations -- purely
# additive: a test that never exits with this code is completely unaffected. 77 is the
# conventional Automake/BSD "test skipped" exit code, also CMake's own documented example value
# for SKIP_RETURN_CODE.
get_property(_cna_all_tests DIRECTORY PROPERTY TESTS)
if(_cna_all_tests)
    set_tests_properties(${_cna_all_tests} PROPERTIES SKIP_RETURN_CODE 77)
endif()
