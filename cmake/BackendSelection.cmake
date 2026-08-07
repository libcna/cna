# --- Graphics Backend Selection ---
# EasyGL is the default on Linux and Emscripten (WebGL 2 = OpenGL ES 3.0).
# Other platforms default to SDL_RENDERER.
if(EMSCRIPTEN OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(_cna_default_backend "EASYGL")
else()
    set(_cna_default_backend "SDL_RENDERER")
endif()
set(CNA_GRAPHICS_BACKEND "${_cna_default_backend}" CACHE STRING "Graphics backend to use (SDL_RENDERER, EASYGL, BGFX, VULKAN, WEBGPU, MAGNUM, HEADLESS, SOFTWARE, STUB, D3D11, D3D12, CANVAS, SKIA, ASCII, FREEDIRECT, D3D9, DX1, DX2, DX3, DX5, DX6, DX7, DX8, D3D10, OPENGLES1, OPENGL4, OPENGL1, OPENGL2, SDL_GPU, WICKED, SOKOL, or DILIGENT)")
set_property(CACHE CNA_GRAPHICS_BACKEND PROPERTY STRINGS "SDL_RENDERER" "EASYGL" "BGFX" "VULKAN" "WEBGPU" "MAGNUM" "HEADLESS" "SOFTWARE" "STUB" "D3D11" "D3D12" "CANVAS" "SKIA" "ASCII" "FREEDIRECT" "D3D9" "DX1" "DX2" "DX3" "DX5" "DX6" "DX7" "DX8" "D3D10" "OPENGLES1" "OPENGL4" "OPENGL1" "OPENGL2" "SDL_GPU" "WICKED" "SOKOL" "DILIGENT")

option(CNA_BACKEND_SDL_RENDERER "Enable SDL_Renderer graphics backend" OFF)
option(CNA_BACKEND_EASY_GL "Enable easy-gl graphics backend" OFF)
option(CNA_BACKEND_BGFX "Enable bgfx graphics backend" OFF)
option(CNA_BACKEND_VULKAN "Enable Vulkan graphics backend" OFF)
option(CNA_BACKEND_WEBGPU "Enable WebGPU graphics backend (wgpu-native)" OFF)
# plan_magnum.md: Magnum (mosra/magnum) GL backend -- a desktop-OpenGL backend expressed entirely
# through Magnum's own typed GL wrappers, on the SDL3 window every other windowed backend uses.
option(CNA_BACKEND_MAGNUM "Enable Magnum (mosra/magnum) graphics backend" OFF)
option(CNA_BACKEND_HEADLESS "Enable Headless (no GPU/window) graphics backend" OFF)
option(CNA_BACKEND_SOFTWARE "Enable Software (CPU rasterizer) graphics backend" OFF)
# plan_stub.md: deliberately minimal no-op graphics backend -- renders nothing, touches no SDL
# window/video subsystem/GPU library, keeps no bookkeeping of any kind (unlike HEADLESS's
# validation modes/counters or SOFTWARE's real CPU rasterizer). Named "Stub" rather than "Null" to
# avoid colliding with the <cstddef>/<cstdlib> NULL macro (see plan_stub.md's naming section).
option(CNA_BACKEND_STUB "Enable Stub (no-op) graphics backend" OFF)
option(CNA_BACKEND_D3D11 "Enable Direct3D 11 graphics backend (Windows only)" OFF)
option(CNA_BACKEND_D3D12 "Enable Direct3D 12 graphics backend (Windows only)" OFF)
# plan_canvas.md: HTML Canvas 2D backend -- Emscripten-only (design decision 1), a browser-native,
# GPU-free 2D-only backend using canvas.getContext('2d') instead of EASYGL's WebGL2 context.
option(CNA_BACKEND_CANVAS "Enable HTML Canvas 2D graphics backend (Emscripten only)" OFF)
option(CNA_BACKEND_SKIA "Enable Skia 2D raster graphics backend" OFF)
# plan_ascii.md: SDL-windowed retro text/glyph-grid backend -- a thin decorator around
# SDL_RENDERER's own SdlGraphicsBackend (see the shared cna_backend_graphics_sdl_renderer_core
# library below), not a real terminal/TTY backend.
option(CNA_BACKEND_ASCII "Enable ASCII (SDL-windowed glyph-grid) graphics backend" OFF)
option(CNA_BACKEND_FREEDIRECT "Enable FreeDirect (DirectDraw via the ../free-direct sibling reimplementation; formerly DX3) graphics backend" OFF)
option(CNA_BACKEND_D3D9 "Enable Direct3D 9 graphics backend (Windows only)" OFF)
# plan_dx1.md: real DirectX 1 (DirectDraw v1) graphics backend -- genuine ddraw.h v1 COM
# interfaces (IDirectDraw/IDirectDrawSurface/DDSURFACEDESC, never IDirectDraw2+), Windows-only,
# same MinGW-cross-compile + Wine delivery route as D3D9/D3D11/D3D12 (Route B). Unlike FreeDirect (formerly DX3), this
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
# plan_dx3.md: real DirectX 3 graphics backend. Originally landed under the temporary DX30 name
# because the free-direct-backed backend owned "DX3" at the time; renamed to DX3 on 2026-08-04
# when that backend became FREEDIRECT (owner instruction -- see plan_dxold.md's naming-transition
# section). Mechanical port of DX2's own 2D layer (upgraded to IDirectDraw2) + 3D layer
# (verbatim, including Phase O9's CPU lighting).
option(CNA_BACKEND_DX3 "Enable Direct X 3 (real DirectDraw v2 + Direct3D v2 DrawPrimitive) graphics backend (Windows only)" OFF)
# plan_dx5.md: real DirectX 5 graphics backend -- DirectDraw v4 (IDirectDraw4/IDirectDrawSurface4/
# DDSURFACEDESC2/DDSCAPS2, every surface not just the top object) + Direct3D v3 (IDirect3D3/
# IDirect3DDevice3/IDirect3DViewport3), the first release where execute buffers are gone entirely
# (DrawPrimitive/DrawIndexedPrimitive only, selected via the D3DFVF_TLVERTEX FVF bitmask instead
# of the old D3DVERTEXTYPE enum). Mechanical port of DX3's own 2D+3D layers (including Phase O9's
# CPU lighting), upgraded further.
option(CNA_BACKEND_DX5 "Enable Direct X 5 (real DirectDraw v4 + Direct3D v3 FVF DrawPrimitive) graphics backend (Windows only)" OFF)
# plan_dx6.md: real DirectX 6 graphics backend -- the EXACT SAME COM interfaces DX5 already uses
# (IDirectDraw4/IDirect3D3/IDirect3DDevice3/IDirect3DViewport3, no new interface revision at this
# DirectX era). Its own delta: real stencil buffer operations (D3DRENDERSTATE_STENCIL*, spike-
# confirmed genuine write+test behavior) against a combined depth+stencil Z-buffer surface,
# resolving the "no real stencil until DX6" boundary DX2/DX3/DX5 all documented. Multitexture
# stays accepted-and-ignored (D3DTLVERTEX only carries one texture-coordinate pair).
option(CNA_BACKEND_DX6 "Enable Direct X 6 (real DirectDraw v4 + Direct3D v3, real stencil) graphics backend (Windows only)" OFF)
# plan_dx7.md: real DirectX 7 graphics backend -- genuinely new interfaces vs DX6: IDirectDraw7/
# IDirectDrawSurface7 (created via DirectDrawCreateEx) + IDirect3D7/IDirect3DDevice7. DX7 REMOVES
# the whole viewport-object concept (IDirect3DViewport3 no longer exists at all --
# IDirect3DDevice7::SetViewport/Clear are direct device methods instead) and simplifies texture
# binding to a direct SetTexture(stage, surface) call (no more texture-handle indirection). Stencil
# is unchanged from DX6, ported verbatim.
option(CNA_BACKEND_DX7 "Enable Direct X 7 (real DirectDraw v7 + Direct3D v7, flattened device model) graphics backend (Windows only)" OFF)
# plan_dx8.md: real DirectX 8 graphics backend -- "DirectDraw+Direct3D merged" (no DirectDraw at
# all): a single IDirect3D8::CreateDevice call creates both the device and its own swap chain.
# Delivered via DXVK (D8VK, merged into DXVK 2.0+), not Wine's own d3d8.dll -- mingw-w64 ships no
# real d3d8 import library for x86_64, so this backend links DXVK's own d3d8.dll.a directly.
# Fixed-function 3D only (scope decision): real XNA effects need ps_2_0+ regardless of Shader
# Model 1.x support, so a real SM1.x pipeline would not make CreateEffectBackend usable for actual
# XNA content. The 2D SpriteBatch layer is real GPU-rendered textured quads (DirectDraw does not
# exist at this era at all), not a CPU compositor like DX1..DX7's own.
option(CNA_BACKEND_DX8 "Enable Direct X 8 (real Direct3D 8, DXVK-delivered, fixed-function) graphics backend (Windows only)" OFF)
# plan_d3d10.md: real Direct3D 10 backend -- unlike DX1..DX8, D3D10 removed the fixed-function
# pipeline entirely, so every draw needs a real HLSL vs_4_0/ps_4_0 shader pair (D3DCompile,
# following D3D9/D3D11's own precedent), real state OBJECTS (ID3D10BlendState/etc, not per-call
# render states), and real MRT support. Delivered via Wine's own builtin d3d10.dll/d3d10_1.dll
# (DXVK 2.6.0 ships no d3d10.dll at all) forwarding to DXVK's real d3d10core.dll + dxgi.dll.
option(CNA_BACKEND_D3D10 "Enable Direct3D 10 (real ID3D10Device, DXVK-delivered via d3d10core, real HLSL shaders) graphics backend (Windows only)" OFF)
option(CNA_BACKEND_SDL_GPU "Enable SDL_gpu graphics backend" OFF)
# plan_opengles1.md design decision 1: a genuinely separate backend from EASYGL -- EASYGL targets
# WebGL2/OpenGL ES 3.0 (shader-based) and cannot create an OpenGL ES 1.1 (fixed-function "Common"
# profile) context at all. Requires a real system GLESv1_CM library/headers (see
# BackendLibraries.cmake's own find_library/find_path FATAL_ERROR gate below), same "hard system
# dependency, not vendored" shape as VULKAN's find_package(Vulkan REQUIRED).
option(CNA_BACKEND_OPENGLES1 "Enable OpenGL ES 1.1 (fixed-function) graphics backend" OFF)

# plan_opengl4.md GL4-1: real desktop OpenGL 4.x core-profile graphics backend -- deliberately
# independent of CNA_BACKEND_EASY_GL/easy-gl (which targets OpenGL ES 3.0 / WebGL2, see
# EasyGLGraphicsBackend's own SDL_GL_CONTEXT_PROFILE_ES context request, not a real desktop
# GL 4.x core profile).
option(CNA_BACKEND_OPENGL4 "Enable real desktop OpenGL 4.x core-profile graphics backend" OFF)

option(CNA_BACKEND_OPENGL1 "Enable native legacy OpenGL 1.x fixed-function graphics backend" OFF)

# plan_opengl2.md: native desktop OpenGL 2.1 (compatibility profile, GLSL 1.10) graphics backend --
# deliberately independent of CNA_BACKEND_EASY_GL/easy-gl (which targets OpenGL ES 3.0 / WebGL2
# and cannot create a desktop GL 2.1 compatibility context).
option(CNA_BACKEND_OPENGL2 "Enable native OpenGL 2.1 graphics backend (no EasyGL)" OFF)

# plan_wicked.md: Wicked Engine's render hardware interface (wi::graphics::GraphicsDevice), which
# itself dispatches to Vulkan (Linux/Windows) or D3D12 (Windows).
option(CNA_BACKEND_WICKED "Enable Wicked Engine graphics backend" OFF)

# plan_sokol.md: sokol_gfx (https://github.com/floooh/sokol) -- a single-header GPU abstraction
# that itself dispatches onto GL/D3D11/Metal/WebGPU, selected here via CNA_SOKOL_API below.
option(CNA_BACKEND_SOKOL "Enable sokol_gfx graphics backend" OFF)

# plan_diligent.md: Diligent Engine backend -- DiligentCore is itself an abstraction over
# D3D11/D3D12/Vulkan/OpenGL/Metal, so unlike every other entry here this one names a portable
# middleware layer rather than one native API; the device type is picked at runtime.
option(CNA_BACKEND_DILIGENT "Enable Diligent Engine graphics backend" OFF)

set(_cna_explicit_backend_selection OFF)
if(CNA_BACKEND_SDL_RENDERER OR CNA_BACKEND_EASY_GL OR CNA_BACKEND_BGFX OR CNA_BACKEND_VULKAN OR CNA_BACKEND_WEBGPU OR CNA_BACKEND_MAGNUM OR CNA_BACKEND_HEADLESS OR CNA_BACKEND_SOFTWARE OR CNA_BACKEND_STUB OR CNA_BACKEND_D3D11 OR CNA_BACKEND_D3D12 OR CNA_BACKEND_CANVAS OR CNA_BACKEND_SKIA OR CNA_BACKEND_ASCII OR CNA_BACKEND_FREEDIRECT OR CNA_BACKEND_D3D9 OR CNA_BACKEND_DX1 OR CNA_BACKEND_DX2 OR CNA_BACKEND_DX3 OR CNA_BACKEND_DX5 OR CNA_BACKEND_DX6 OR CNA_BACKEND_DX7 OR CNA_BACKEND_DX8 OR CNA_BACKEND_D3D10 OR CNA_BACKEND_OPENGLES1 OR CNA_BACKEND_OPENGL4 OR CNA_BACKEND_OPENGL1 OR CNA_BACKEND_OPENGL2 OR CNA_BACKEND_SDL_GPU OR CNA_BACKEND_WICKED OR CNA_BACKEND_SOKOL OR CNA_BACKEND_DILIGENT)
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
    if(CNA_BACKEND_MAGNUM)
        list(APPEND _cna_enabled_backends "MAGNUM")
    endif()
    if(CNA_BACKEND_HEADLESS)
        list(APPEND _cna_enabled_backends "HEADLESS")
    endif()
    if(CNA_BACKEND_SOFTWARE)
        list(APPEND _cna_enabled_backends "SOFTWARE")
    endif()
    if(CNA_BACKEND_STUB)
        list(APPEND _cna_enabled_backends "STUB")
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
    if(CNA_BACKEND_SKIA)
        list(APPEND _cna_enabled_backends "SKIA")
    endif()
    if(CNA_BACKEND_ASCII)
        list(APPEND _cna_enabled_backends "ASCII")
    endif()
    if(CNA_BACKEND_FREEDIRECT)
        list(APPEND _cna_enabled_backends "FREEDIRECT")
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
    if(CNA_BACKEND_DX3)
        list(APPEND _cna_enabled_backends "DX3")
    endif()
    if(CNA_BACKEND_DX5)
        list(APPEND _cna_enabled_backends "DX5")
    endif()
    if(CNA_BACKEND_DX6)
        list(APPEND _cna_enabled_backends "DX6")
    endif()
    if(CNA_BACKEND_DX7)
        list(APPEND _cna_enabled_backends "DX7")
    endif()
    if(CNA_BACKEND_DX8)
        list(APPEND _cna_enabled_backends "DX8")
    endif()
    if(CNA_BACKEND_D3D10)
        list(APPEND _cna_enabled_backends "D3D10")
    endif()
    if(CNA_BACKEND_SDL_GPU)
        list(APPEND _cna_enabled_backends "SDL_GPU")
    endif()
    if(CNA_BACKEND_OPENGLES1)
        list(APPEND _cna_enabled_backends "OPENGLES1")
    endif()
    if(CNA_BACKEND_OPENGL4)
        list(APPEND _cna_enabled_backends "OPENGL4")
    endif()
    if(CNA_BACKEND_OPENGL1)
        list(APPEND _cna_enabled_backends "OPENGL1")
    endif()
    if(CNA_BACKEND_OPENGL2)
        list(APPEND _cna_enabled_backends "OPENGL2")
    endif()
    if(CNA_BACKEND_WICKED)
        list(APPEND _cna_enabled_backends "WICKED")
    endif()
    if(CNA_BACKEND_SOKOL)
        list(APPEND _cna_enabled_backends "SOKOL")
    endif()
    if(CNA_BACKEND_DILIGENT)
        list(APPEND _cna_enabled_backends "DILIGENT")
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
# extends it again to DX1: unlike FreeDirect (formerly DX3; SDL3-backed ../free-direct, genuinely native-Linux-buildable),
# DX1 uses the real Windows ddraw.h, so it needs the exact same gate.
if((CNA_GRAPHICS_BACKEND STREQUAL "D3D11" OR CNA_GRAPHICS_BACKEND STREQUAL "D3D12" OR CNA_GRAPHICS_BACKEND STREQUAL "D3D9" OR CNA_GRAPHICS_BACKEND STREQUAL "DX1" OR CNA_GRAPHICS_BACKEND STREQUAL "DX2" OR CNA_GRAPHICS_BACKEND STREQUAL "DX3" OR CNA_GRAPHICS_BACKEND STREQUAL "DX5" OR CNA_GRAPHICS_BACKEND STREQUAL "DX6" OR CNA_GRAPHICS_BACKEND STREQUAL "DX7" OR CNA_GRAPHICS_BACKEND STREQUAL "DX8" OR CNA_GRAPHICS_BACKEND STREQUAL "D3D10")
        AND NOT CMAKE_SYSTEM_NAME STREQUAL "Windows")
    message(FATAL_ERROR
        "CNA: ${CNA_GRAPHICS_BACKEND} backend only builds when targeting Windows. Either build "
        "natively on Windows, or cross-compile from Linux with "
        "-DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64.cmake")
endif()

# plan_canvas.md design decision 1: HTML Canvas 2D is a browser DOM API and cannot exist outside
# an Emscripten/WebAssembly build -- same hard-gate shape as the D3D11/D3D12 Windows-only check
# just above, new condition.
if(CNA_GRAPHICS_BACKEND STREQUAL "OPENGL1" AND NOT (CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_SYSTEM_NAME STREQUAL "Windows"))
    message(FATAL_ERROR "CNA: OPENGL1 backend is intentionally supported only on desktop Linux and Windows.")
endif()

if(CNA_GRAPHICS_BACKEND STREQUAL "CANVAS" AND NOT EMSCRIPTEN)
    message(FATAL_ERROR
        "CNA: CANVAS backend only builds when targeting Emscripten (HTML Canvas is a browser DOM "
        "API). Configure with -DCMAKE_TOOLCHAIN_FILE=\$EMSDK/upstream/emscripten/cmake/Modules/"
        "Platform/Emscripten.cmake (or use emcmake).")
endif()

# plan_magnum.md design decision 2: Magnum's Platform::GLContext takes its OpenGL entry points
# from exactly one of Magnum's four platform context libraries (GLX/EGL/WGL/CGL), none of which
# exists for Emscripten -- there the loader is baked into EmscriptenApplication, which owns the
# window and event loop CNA already owns through SDL3. Same hard-gate shape as the CANVAS check
# just above, inverted condition.
if(CNA_GRAPHICS_BACKEND STREQUAL "MAGNUM" AND EMSCRIPTEN)
    message(FATAL_ERROR
        "CNA: MAGNUM backend cannot target Emscripten -- Magnum supplies no standalone WebGL "
        "function loader for an externally created context. Use -DCNA_GRAPHICS_BACKEND=EASYGL "
        "(WebGL 2) or CANVAS for browser builds.")
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

# plan_freedirect.md design decision 10 / Task DX3-2: free-direct is a SIBLING repository checkout, not a
# git submodule of this repo -- same rationale as sharp-runtime/easy-gl's identical checks above.
# free-direct's own CMakeLists.txt (add_subdirectory(../free-api ...)) resolves SDL3::SDL3/
# SDL3_image::SDL3_image/SDL3_mixer::SDL3_mixer from CNA's own already-vendored targets (set up by
# cna_configure_vendored_sdl() above, before backend selection runs), so no
# -DFREE_API_USE_SYSTEM_SDL3 flag is needed here, mirroring how ../free-eggbert/../planetblupi
# already consume free-direct today (design decision 10).
if(CNA_GRAPHICS_BACKEND STREQUAL "FREEDIRECT")
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

# plan_wicked.md design decision 3: Wicked Engine builds a native Vulkan (Linux/Windows) or D3D12
# (Windows) device from vendored headers -- there is no web/Emscripten target for it at all, so this
# is a hard gate in the same shape as the CANVAS/D3D11 ones above rather than a soft WARNING.
if(CNA_GRAPHICS_BACKEND STREQUAL "WICKED")
    if(EMSCRIPTEN)
        message(FATAL_ERROR
            "CNA: WICKED backend cannot target Emscripten -- Wicked Engine has no WebGPU/WebGL "
            "device. Use -DCNA_GRAPHICS_BACKEND=EASYGL or CANVAS for web builds.")
    endif()
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT CMAKE_SYSTEM_NAME STREQUAL "Windows")
        message(WARNING "CNA: WICKED backend is developed against Linux/Vulkan. Other platforms may require additional setup.")
    endif()
endif()

# plan_sokol.md design decision 2: sokol_gfx is itself a multi-API abstraction, so the SOKOL
# backend has a second axis of its own -- which native API sokol_gfx dispatches onto. This is a
# compile-time choice exactly like CNA_GRAPHICS_BACKEND, resolved here into the single SOKOL_*
# define sokol_gfx.h switches on. GLCORE (OpenGL 4.1 core, sokol_gfx's own minimum for that
# backend) is the default and the only value verified on this project's Linux dev machine; the
# rest are wired but unproven, and say so at configure time rather than pretending otherwise.
set(CNA_SOKOL_API "GLCORE" CACHE STRING "Native API sokol_gfx dispatches onto (GLCORE, GLES3, D3D11, METAL, WGPU)")
set_property(CACHE CNA_SOKOL_API PROPERTY STRINGS "GLCORE" "GLES3" "D3D11" "METAL" "WGPU")
if(CNA_GRAPHICS_BACKEND STREQUAL "SOKOL")
    if(CNA_SOKOL_API STREQUAL "GLCORE")
        set(CNA_SOKOL_API_DEFINE "SOKOL_GLCORE")
    elseif(CNA_SOKOL_API STREQUAL "GLES3")
        set(CNA_SOKOL_API_DEFINE "SOKOL_GLES3")
    elseif(CNA_SOKOL_API STREQUAL "D3D11")
        set(CNA_SOKOL_API_DEFINE "SOKOL_D3D11")
    elseif(CNA_SOKOL_API STREQUAL "METAL")
        set(CNA_SOKOL_API_DEFINE "SOKOL_METAL")
    elseif(CNA_SOKOL_API STREQUAL "WGPU")
        set(CNA_SOKOL_API_DEFINE "SOKOL_WGPU")
    else()
        message(FATAL_ERROR "CNA: Unknown CNA_SOKOL_API: ${CNA_SOKOL_API} (expected GLCORE, GLES3, D3D11, METAL, or WGPU)")
    endif()
    if(NOT CNA_SOKOL_API STREQUAL "GLCORE")
        message(WARNING
            "CNA: CNA_SOKOL_API=${CNA_SOKOL_API} is wired but unverified -- only GLCORE has been "
            "exercised against real hardware. SokolGraphicsBackend also creates its GPU context "
            "through SDL_GL_CreateContext, which only serves the GL APIs; a non-GL value needs "
            "that context path implemented for the chosen API first (plan_sokol.md Phase SOKOL-8).")
    endif()
endif()

if(CNA_GRAPHICS_BACKEND STREQUAL "SDL_RENDERER")
    message(STATUS "CNA: Using SDL_RENDERER graphics backend")
    set(BACKEND_DIR "src/CNA/Internal/Backends/SdlRenderer")
    set(BACKEND_TARGET "cna_backend_graphics_sdl_renderer")
    add_compile_definitions(CNA_BACKEND_SDL_RENDERER)
    set(CNA_BACKEND_DEFINE "CNA_BACKEND_SDL_RENDERER")
elseif(CNA_GRAPHICS_BACKEND STREQUAL "OPENGL1")
    message(STATUS "CNA: Using OPENGL1 native fixed-function graphics backend")
    set(BACKEND_DIR "src/CNA/Internal/Backends/OpenGL1")
    set(BACKEND_TARGET "cna_backend_graphics_opengl1")
    add_compile_definitions(CNA_BACKEND_OPENGL1)
    set(CNA_BACKEND_DEFINE "CNA_BACKEND_OPENGL1")
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
elseif(CNA_GRAPHICS_BACKEND STREQUAL "MAGNUM")
    message(STATUS "CNA: Using MAGNUM graphics backend")
    set(BACKEND_DIR "src/CNA/Internal/Backends/Magnum")
    set(BACKEND_TARGET "cna_backend_graphics_magnum")
    add_compile_definitions(CNA_BACKEND_MAGNUM)
    set(CNA_BACKEND_DEFINE "CNA_BACKEND_MAGNUM")
    include(cmake/ThirdPartyMagnum.cmake)
    cna_configure_magnum()
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
elseif(CNA_GRAPHICS_BACKEND STREQUAL "STUB")
    message(STATUS "CNA: Using STUB (no-op) graphics backend")
    set(BACKEND_DIR "src/CNA/Internal/Backends/Stub")
    set(BACKEND_TARGET "cna_backend_graphics_stub")
    add_compile_definitions(CNA_BACKEND_STUB)
    set(CNA_BACKEND_DEFINE "CNA_BACKEND_STUB")
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
elseif(CNA_GRAPHICS_BACKEND STREQUAL "SKIA")
    # SKIA-160: CNA_SKIA_MODE is a sub-selector of CNA_GRAPHICS_BACKEND=SKIA, not a separate
    # backend identity -- RASTER (default) and GANESH are two mutually exclusive GN builds of the
    # same pinned Skia checkout (docs/skia-ganesh-artifact.md), never linked together in one
    # binary. Every source file under BACKEND_DIR compiles in both modes (SkiaGaneshContext.cpp
    # branches on CNA_SKIA_MODE_GANESH internally, matching the established safe-default-throws
    # extensibility pattern); only which single Skia archive set gets linked differs.
    set(CNA_SKIA_MODE "RASTER" CACHE STRING
        "Skia surface mode: RASTER (default, release-gated) or GANESH (SKIA-159/160, experimental, requires CNA_SKIA_GANESH_BUILD_DIR)")
    set_property(CACHE CNA_SKIA_MODE PROPERTY STRINGS "RASTER" "GANESH")

    set(BACKEND_DIR "src/CNA/Internal/Backends/Skia")
    set(BACKEND_TARGET "cna_backend_graphics_skia")
    add_compile_definitions(CNA_BACKEND_SKIA)
    set(CNA_BACKEND_DEFINE "CNA_BACKEND_SKIA")

    if(CNA_SKIA_MODE STREQUAL "GANESH")
        message(STATUS "CNA: Using SKIA backend in Ganesh/OpenGL mode (experimental, SKIA-159/160)")
        add_compile_definitions(CNA_SKIA_MODE_GANESH)
        include(cmake/ThirdPartySkiaGanesh.cmake)
        cna_configure_skia_ganesh()
        set(CNA_SKIA_LINK_TARGET CNA::SkiaGanesh)
    else()
        message(STATUS "CNA: Using SKIA raster graphics backend")
        include(cmake/ThirdPartySkia.cmake)
        cna_configure_skia()
        set(CNA_SKIA_LINK_TARGET CNA::Skia)
    endif()
elseif(CNA_GRAPHICS_BACKEND STREQUAL "ASCII")
    message(STATUS "CNA: Using ASCII (SDL-windowed glyph-grid) graphics backend")
    set(BACKEND_DIR "src/CNA/Internal/Backends/Ascii")
    set(BACKEND_TARGET "cna_backend_graphics_ascii")
    add_compile_definitions(CNA_BACKEND_ASCII)
    set(CNA_BACKEND_DEFINE "CNA_BACKEND_ASCII")
elseif(CNA_GRAPHICS_BACKEND STREQUAL "FREEDIRECT")
    message(STATUS "CNA: Using FreeDirect (DirectDraw via free-direct; formerly DX3) graphics backend")
    set(BACKEND_DIR "src/CNA/Internal/Backends/FreeDirect")
    set(BACKEND_TARGET "cna_backend_graphics_freedirect")
    add_compile_definitions(CNA_BACKEND_FREEDIRECT)
    set(CNA_BACKEND_DEFINE "CNA_BACKEND_FREEDIRECT")
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
elseif(CNA_GRAPHICS_BACKEND STREQUAL "DX3")
    message(STATUS "CNA: Using DX3 (real DirectX 3 -- DirectDraw v2 + Direct3D v2 DrawPrimitive) graphics backend")
    set(BACKEND_DIR "src/CNA/Internal/Backends/Dx3")
    set(BACKEND_TARGET "cna_backend_graphics_dx3")
    add_compile_definitions(CNA_BACKEND_DX3)
    set(CNA_BACKEND_DEFINE "CNA_BACKEND_DX3")
elseif(CNA_GRAPHICS_BACKEND STREQUAL "DX5")
    message(STATUS "CNA: Using DX5 (real DirectDraw v4 + Direct3D v3 FVF DrawPrimitive) graphics backend")
    set(BACKEND_DIR "src/CNA/Internal/Backends/Dx5")
    set(BACKEND_TARGET "cna_backend_graphics_dx5")
    add_compile_definitions(CNA_BACKEND_DX5)
    set(CNA_BACKEND_DEFINE "CNA_BACKEND_DX5")
elseif(CNA_GRAPHICS_BACKEND STREQUAL "DX6")
    message(STATUS "CNA: Using DX6 (real DirectDraw v4 + Direct3D v3, real stencil) graphics backend")
    set(BACKEND_DIR "src/CNA/Internal/Backends/Dx6")
    set(BACKEND_TARGET "cna_backend_graphics_dx6")
    add_compile_definitions(CNA_BACKEND_DX6)
    set(CNA_BACKEND_DEFINE "CNA_BACKEND_DX6")
elseif(CNA_GRAPHICS_BACKEND STREQUAL "DX7")
    message(STATUS "CNA: Using DX7 (real DirectDraw v7 + Direct3D v7, flattened device model) graphics backend")
    set(BACKEND_DIR "src/CNA/Internal/Backends/Dx7")
    set(BACKEND_TARGET "cna_backend_graphics_dx7")
    add_compile_definitions(CNA_BACKEND_DX7)
    set(CNA_BACKEND_DEFINE "CNA_BACKEND_DX7")
elseif(CNA_GRAPHICS_BACKEND STREQUAL "DX8")
    message(STATUS "CNA: Using DX8 (real Direct3D 8, DXVK-delivered, fixed-function) graphics backend")
    set(BACKEND_DIR "src/CNA/Internal/Backends/Dx8")
    set(BACKEND_TARGET "cna_backend_graphics_dx8")
    add_compile_definitions(CNA_BACKEND_DX8)
    set(CNA_BACKEND_DEFINE "CNA_BACKEND_DX8")
elseif(CNA_GRAPHICS_BACKEND STREQUAL "D3D10")
    message(STATUS "CNA: Using D3D10 (real Direct3D 10, DXVK-delivered via d3d10core, real HLSL shaders) graphics backend")
    set(BACKEND_DIR "src/CNA/Internal/Backends/D3D10")
    set(BACKEND_TARGET "cna_backend_graphics_d3d10")
    add_compile_definitions(CNA_BACKEND_D3D10)
    set(CNA_BACKEND_DEFINE "CNA_BACKEND_D3D10")
elseif(CNA_GRAPHICS_BACKEND STREQUAL "DILIGENT")
    message(STATUS "CNA: Using DILIGENT (Diligent Engine) graphics backend")
    set(BACKEND_DIR "src/CNA/Internal/Backends/Diligent")
    set(BACKEND_TARGET "cna_backend_graphics_diligent")
    add_compile_definitions(CNA_BACKEND_DILIGENT)
    set(CNA_BACKEND_DEFINE "CNA_BACKEND_DILIGENT")
    include(cmake/ThirdPartyDiligent.cmake)
    cna_configure_diligent()
elseif(CNA_GRAPHICS_BACKEND STREQUAL "SDL_GPU")
    message(STATUS "CNA: Using SDL_GPU graphics backend")
    set(BACKEND_DIR "src/CNA/Internal/Backends/SdlGpu")
    set(BACKEND_TARGET "cna_backend_graphics_sdl_gpu")
    add_compile_definitions(CNA_BACKEND_SDL_GPU)
    set(CNA_BACKEND_DEFINE "CNA_BACKEND_SDL_GPU")
elseif(CNA_GRAPHICS_BACKEND STREQUAL "OPENGLES1")
    message(STATUS "CNA: Using OPENGLES1 (fixed-function OpenGL ES 1.1) graphics backend")
    set(BACKEND_DIR "src/CNA/Internal/Backends/OpenGLES1")
    set(BACKEND_TARGET "cna_backend_graphics_opengles1")
    add_compile_definitions(CNA_BACKEND_OPENGLES1)
    set(CNA_BACKEND_DEFINE "CNA_BACKEND_OPENGLES1")
elseif(CNA_GRAPHICS_BACKEND STREQUAL "OPENGL4")
    message(STATUS "CNA: Using OPENGL4 (real desktop OpenGL 4.x core profile) graphics backend")
    set(BACKEND_DIR "src/CNA/Internal/Backends/OpenGL4")
    set(BACKEND_TARGET "cna_backend_graphics_opengl4")
    add_compile_definitions(CNA_BACKEND_OPENGL4)
    set(CNA_BACKEND_DEFINE "CNA_BACKEND_OPENGL4")
    find_package(OpenGL REQUIRED)
elseif(CNA_GRAPHICS_BACKEND STREQUAL "OPENGL2")
    message(STATUS "CNA: Using native OPENGL2 graphics backend (no EasyGL)")
    set(BACKEND_DIR "src/CNA/Internal/Backends/OpenGL2")
    set(BACKEND_TARGET "cna_backend_graphics_opengl2")
    add_compile_definitions(CNA_BACKEND_OPENGL2)
    set(CNA_BACKEND_DEFINE "CNA_BACKEND_OPENGL2")
elseif(CNA_GRAPHICS_BACKEND STREQUAL "WICKED")
    message(STATUS "CNA: Using WICKED (Wicked Engine) graphics backend")
    set(BACKEND_DIR "src/CNA/Internal/Backends/Wicked")
    set(BACKEND_TARGET "cna_backend_graphics_wicked")
    add_compile_definitions(CNA_BACKEND_WICKED)
    set(CNA_BACKEND_DEFINE "CNA_BACKEND_WICKED")
    include(cmake/ThirdPartyWicked.cmake)
    cna_configure_wicked()
elseif(CNA_GRAPHICS_BACKEND STREQUAL "SOKOL")
    message(STATUS "CNA: Using SOKOL graphics backend (sokol_gfx on ${CNA_SOKOL_API})")
    set(BACKEND_DIR "src/CNA/Internal/Backends/Sokol")
    set(BACKEND_TARGET "cna_backend_graphics_sokol")
    add_compile_definitions(CNA_BACKEND_SOKOL)
    set(CNA_BACKEND_DEFINE "CNA_BACKEND_SOKOL")
    include(cmake/ThirdPartySokol.cmake)
    cna_configure_sokol()
else()

    message(FATAL_ERROR "CNA: Unknown graphics backend: ${CNA_GRAPHICS_BACKEND}")
endif()
