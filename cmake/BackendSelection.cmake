# --- Graphics Backend Selection ---
# EasyGL is the default on Linux and Emscripten (WebGL 2 = OpenGL ES 3.0).
# Other platforms default to SDL_RENDERER.
if(EMSCRIPTEN OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(_cna_default_backend "EASYGL")
else()
    set(_cna_default_backend "SDL_RENDERER")
endif()
set(CNA_GRAPHICS_BACKEND "${_cna_default_backend}" CACHE STRING "Graphics backend to use (SDL_RENDERER, EASYGL, BGFX, VULKAN, WEBGPU, HEADLESS, SOFTWARE, D3D11, D3D12, CANVAS, ASCII, DX3, D3D9, DX1, DX2, or SDL_GPU)")
set_property(CACHE CNA_GRAPHICS_BACKEND PROPERTY STRINGS "SDL_RENDERER" "EASYGL" "BGFX" "VULKAN" "WEBGPU" "HEADLESS" "SOFTWARE" "D3D11" "D3D12" "CANVAS" "ASCII" "DX3" "D3D9" "DX1" "DX2" "SDL_GPU")

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
# plan_dx1.md: real DirectX 1 (DirectDraw v1) graphics backend -- genuine ddraw.h v1 COM
# interfaces (IDirectDraw/IDirectDrawSurface/DDSURFACEDESC, never IDirectDraw2+), Windows-only,
# same MinGW-cross-compile + Wine delivery route as D3D9/D3D11/D3D12 (Route B). Unlike DX3, this
# backend deliberately does NOT use ../free-direct -- see plan_dxold.md's roadmap.
option(CNA_BACKEND_DX1 "Enable Direct X 1 (real DirectDraw v1) graphics backend (Windows only)" OFF)
# plan_dx2.md: real DirectX 2 graphics backend -- 2D layer is a verbatim port of DX1's real
# DirectDraw v1 (IDirectDraw/IDirectDrawSurface/DDSURFACEDESC, still never IDirectDraw2+). 3D layer
# uses IDirect3D2/IDirectDrawSurface2::DrawPrimitive (immediate-mode, no execute buffers) -- the
# literal DirectX-2-SDK execute-buffer Direct3D model (IDirect3D/IDirect3DDevice::Execute/
# D3DOP_TRIANGLE) was spiked exhaustively and renders black in this environment's Wine, while
# IDirect3DDevice2::DrawPrimitive/DrawIndexedPrimitive (one interface revision later, DX3-SDK) was
# proven to work (real Gouraud interpolation, real Z-test occlusion, real texture sampling) -- see
# plan_dx2.md's status note and `dx2-spike/README.md` for the full spike record and the project
# owner's confirmation of this scope choice.
option(CNA_BACKEND_DX2 "Enable Direct X 2 (real DirectDraw v1 + Direct3D v2 DrawPrimitive) graphics backend (Windows only)" OFF)
option(CNA_BACKEND_SDL_GPU "Enable SDL_gpu graphics backend" OFF)

set(_cna_explicit_backend_selection OFF)
if(CNA_BACKEND_SDL_RENDERER OR CNA_BACKEND_EASY_GL OR CNA_BACKEND_BGFX OR CNA_BACKEND_VULKAN OR CNA_BACKEND_WEBGPU OR CNA_BACKEND_HEADLESS OR CNA_BACKEND_SOFTWARE OR CNA_BACKEND_D3D11 OR CNA_BACKEND_D3D12 OR CNA_BACKEND_CANVAS OR CNA_BACKEND_ASCII OR CNA_BACKEND_DX3 OR CNA_BACKEND_D3D9 OR CNA_BACKEND_DX1 OR CNA_BACKEND_DX2 OR CNA_BACKEND_SDL_GPU)
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
    if(CNA_BACKEND_DX1)
        list(APPEND _cna_enabled_backends "DX1")
    endif()
    if(CNA_BACKEND_DX2)
        list(APPEND _cna_enabled_backends "DX2")
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
# extends this same gate to D3D9 (d3d9.h is equally Windows-only). plan_dx1.md design decision 1
# extends it again to DX1: unlike DX3 (SDL3-backed ../free-direct, genuinely native-Linux-buildable),
# DX1 uses the real Windows ddraw.h, so it needs the exact same gate.
if((CNA_GRAPHICS_BACKEND STREQUAL "D3D11" OR CNA_GRAPHICS_BACKEND STREQUAL "D3D12" OR CNA_GRAPHICS_BACKEND STREQUAL "D3D9" OR CNA_GRAPHICS_BACKEND STREQUAL "DX1" OR CNA_GRAPHICS_BACKEND STREQUAL "DX2")
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
    set(CNA_BACKEND_DEFINE "CNA_BACKEND_SDL_RENDERER")
elseif(CNA_GRAPHICS_BACKEND STREQUAL "EASYGL")
    message(STATUS "CNA: Using EASYGL graphics backend")
    set(BACKEND_DIR "src/CNA/Internal/Backends/EasyGL")
    set(BACKEND_TARGET "cna_backend_graphics_easygl")
    add_compile_definitions(CNA_BACKEND_EASYGL)
    set(CNA_BACKEND_DEFINE "CNA_BACKEND_EASYGL")
elseif(CNA_GRAPHICS_BACKEND STREQUAL "BGFX")
    message(STATUS "CNA: Using BGFX graphics backend")
    set(BACKEND_DIR "src/CNA/Internal/Backends/Bgfx")
    set(BACKEND_TARGET "cna_backend_graphics_bgfx")
    add_compile_definitions(CNA_BACKEND_BGFX)
    set(CNA_BACKEND_DEFINE "CNA_BACKEND_BGFX")

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
        # REMED-GFX-185: pin the dependency revision the capability patch below was authored
        # against. The patch exposes the exact renderer-selected render-target MSAA ceiling in
        # bgfx::Caps; CNA cannot report an honest applied count from bgfx's format-support bit
        # alone because the native renderers silently reduce RT_MSAA_Xn to a device limit.
        GIT_TAG 572868c0cb952add48019d267223453958e958b8
        GIT_SHALLOW FALSE
        GIT_PROGRESS TRUE
        GIT_SUBMODULES_RECURSE TRUE
        PATCH_COMMAND git -C bgfx apply --unidiff-zero --whitespace=nowarn
            ${CMAKE_CURRENT_SOURCE_DIR}/cmake/patches/bgfx-max-render-target-msaa.patch
    )
    FetchContent_MakeAvailable(bgfx_cmake)

    set(CNA_BGFX_SHADER_INCLUDE_DIR "${bgfx_cmake_SOURCE_DIR}/bgfx/examples/common/imgui")
elseif(CNA_GRAPHICS_BACKEND STREQUAL "VULKAN")
    message(STATUS "CNA: Using VULKAN graphics backend")
    set(BACKEND_DIR "src/CNA/Internal/Backends/Vulkan")
    set(BACKEND_TARGET "cna_backend_graphics_vulkan")
    add_compile_definitions(CNA_BACKEND_VULKAN)
    set(CNA_BACKEND_DEFINE "CNA_BACKEND_VULKAN")
    find_package(Vulkan REQUIRED)
elseif(CNA_GRAPHICS_BACKEND STREQUAL "WEBGPU")
    message(STATUS "CNA: Using WEBGPU graphics backend")
    set(BACKEND_DIR "src/CNA/Internal/Backends/WebGPU")
    set(BACKEND_TARGET "cna_backend_graphics_webgpu")
    add_compile_definitions(CNA_BACKEND_WEBGPU)
    set(CNA_BACKEND_DEFINE "CNA_BACKEND_WEBGPU")
    include(cmake/ThirdPartyWebGPU.cmake)
    cna_configure_webgpu()
elseif(CNA_GRAPHICS_BACKEND STREQUAL "HEADLESS")
    message(STATUS "CNA: Using HEADLESS (no GPU/window) graphics backend")
    set(BACKEND_DIR "src/CNA/Internal/Backends/Headless")
    set(BACKEND_TARGET "cna_backend_graphics_headless")
    add_compile_definitions(CNA_BACKEND_HEADLESS)
    set(CNA_BACKEND_DEFINE "CNA_BACKEND_HEADLESS")
elseif(CNA_GRAPHICS_BACKEND STREQUAL "SOFTWARE")
    message(STATUS "CNA: Using SOFTWARE (CPU rasterizer) graphics backend")
    set(BACKEND_DIR "src/CNA/Internal/Backends/Software")
    set(BACKEND_TARGET "cna_backend_graphics_software")
    add_compile_definitions(CNA_BACKEND_SOFTWARE)
    set(CNA_BACKEND_DEFINE "CNA_BACKEND_SOFTWARE")
elseif(CNA_GRAPHICS_BACKEND STREQUAL "D3D11")
    message(STATUS "CNA: Using D3D11 graphics backend")
    set(BACKEND_DIR "src/CNA/Internal/Backends/D3D11")
    set(BACKEND_TARGET "cna_backend_graphics_d3d11")
    add_compile_definitions(CNA_BACKEND_D3D11)
    set(CNA_BACKEND_DEFINE "CNA_BACKEND_D3D11")
elseif(CNA_GRAPHICS_BACKEND STREQUAL "D3D12")
    message(STATUS "CNA: Using D3D12 graphics backend")
    set(BACKEND_DIR "src/CNA/Internal/Backends/D3D12")
    set(BACKEND_TARGET "cna_backend_graphics_d3d12")
    add_compile_definitions(CNA_BACKEND_D3D12)
    set(CNA_BACKEND_DEFINE "CNA_BACKEND_D3D12")
elseif(CNA_GRAPHICS_BACKEND STREQUAL "CANVAS")
    message(STATUS "CNA: Using CANVAS (HTML Canvas 2D) graphics backend")
    set(BACKEND_DIR "src/CNA/Internal/Backends/Canvas")
    set(BACKEND_TARGET "cna_backend_graphics_canvas")
    add_compile_definitions(CNA_BACKEND_CANVAS)
    set(CNA_BACKEND_DEFINE "CNA_BACKEND_CANVAS")
elseif(CNA_GRAPHICS_BACKEND STREQUAL "ASCII")
    message(STATUS "CNA: Using ASCII (SDL-windowed glyph-grid) graphics backend")
    set(BACKEND_DIR "src/CNA/Internal/Backends/Ascii")
    set(BACKEND_TARGET "cna_backend_graphics_ascii")
    add_compile_definitions(CNA_BACKEND_ASCII)
    set(CNA_BACKEND_DEFINE "CNA_BACKEND_ASCII")
elseif(CNA_GRAPHICS_BACKEND STREQUAL "DX3")
    message(STATUS "CNA: Using DX3 (DirectDraw via free-direct) graphics backend")
    set(BACKEND_DIR "src/CNA/Internal/Backends/Dx3")
    set(BACKEND_TARGET "cna_backend_graphics_dx3")
    add_compile_definitions(CNA_BACKEND_DX3)
    set(CNA_BACKEND_DEFINE "CNA_BACKEND_DX3")
elseif(CNA_GRAPHICS_BACKEND STREQUAL "D3D9")
    message(STATUS "CNA: Using D3D9 graphics backend")
    set(BACKEND_DIR "src/CNA/Internal/Backends/D3D9")
    set(BACKEND_TARGET "cna_backend_graphics_d3d9")
    add_compile_definitions(CNA_BACKEND_D3D9)
    set(CNA_BACKEND_DEFINE "CNA_BACKEND_D3D9")
elseif(CNA_GRAPHICS_BACKEND STREQUAL "DX1")
    message(STATUS "CNA: Using DX1 (real DirectDraw v1) graphics backend")
    set(BACKEND_DIR "src/CNA/Internal/Backends/Dx1")
    set(BACKEND_TARGET "cna_backend_graphics_dx1")
    add_compile_definitions(CNA_BACKEND_DX1)
    set(CNA_BACKEND_DEFINE "CNA_BACKEND_DX1")
elseif(CNA_GRAPHICS_BACKEND STREQUAL "DX2")
    message(STATUS "CNA: Using DX2 (real DirectDraw v1 + Direct3D v2 DrawPrimitive) graphics backend")
    set(BACKEND_DIR "src/CNA/Internal/Backends/Dx2")
    set(BACKEND_TARGET "cna_backend_graphics_dx2")
    add_compile_definitions(CNA_BACKEND_DX2)
    set(CNA_BACKEND_DEFINE "CNA_BACKEND_DX2")
elseif(CNA_GRAPHICS_BACKEND STREQUAL "SDL_GPU")
    message(STATUS "CNA: Using SDL_GPU graphics backend")
    set(BACKEND_DIR "src/CNA/Internal/Backends/SdlGpu")
    set(BACKEND_TARGET "cna_backend_graphics_sdl_gpu")
    add_compile_definitions(CNA_BACKEND_SDL_GPU)
    set(CNA_BACKEND_DEFINE "CNA_BACKEND_SDL_GPU")
else()

    message(FATAL_ERROR "CNA: Unknown graphics backend: ${CNA_GRAPHICS_BACKEND}")
endif()
