# --- Graphics Renderer Selection ---
# plans/plan_glbackends.md: EasyGL is an internal implementation family, not a public renderer name.
# It is selected publicly via one of 5 GL-profile names -- OPENGLES2/OPENGLES3/OPENGL33
# (desktop/mobile, non-Emscripten) and WEBGL1/WEBGL2 (Emscripten only). OPENGLES3 on Linux is the
# default GL-family choice (was EASYGL); WEBGL2 is the default under Emscripten (was also EASYGL --
# Emscripten's GLES 3.0 request already mapped to a WebGL 2 context, it just had no name of its
# own). Other platforms default to SDL_RENDERER.
if(EMSCRIPTEN)
    set(_cna_default_renderer "WEBGL2")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(_cna_default_renderer "OPENGLES3")
else()
    set(_cna_default_renderer "SDL_RENDERER")
endif()
set(CNA_GRAPHICS_RENDERER "${_cna_default_renderer}" CACHE STRING "Graphics renderer to use (SDL_RENDERER, OPENGLES2, OPENGLES3, OPENGL33, WEBGL1, WEBGL2, BGFX, VULKAN, WEBGPU, HEADLESS, SOFTWARE, STUB, DIRECTX11, DIRECTX12, DIRECT2D, CANVAS, HTML_DOM, FREEDIRECT, DIRECTX9, DIRECTX1, DIRECTX2, DIRECTX3, DIRECTX5, DIRECTX6, DIRECTX7, DIRECTX8, DIRECTX10, SDL_GPU, OPENGLES1, OPENGL4, OPENGL1, OPENGL2, GLIDE, GDI, METAL, FNA3D, SVG_DOM, OPENVG, PORTABLEGL, TINYGL, PIXIJS, or NANOVG)")
set_property(CACHE CNA_GRAPHICS_RENDERER PROPERTY STRINGS "SDL_RENDERER" "OPENGLES2" "OPENGLES3" "OPENGL33" "WEBGL1" "WEBGL2" "BGFX" "VULKAN" "WEBGPU" "HEADLESS" "SOFTWARE" "STUB" "DIRECTX11" "DIRECTX12" "DIRECT2D" "CANVAS" "HTML_DOM" "FREEDIRECT" "DIRECTX9" "DIRECTX1" "DIRECTX2" "DIRECTX3" "DIRECTX5" "DIRECTX6" "DIRECTX7" "DIRECTX8" "DIRECTX10" "SDL_GPU" "OPENGLES1" "OPENGL4" "OPENGL1" "OPENGL2" "GLIDE" "GDI" "METAL" "FNA3D" "SVG_DOM" "OPENVG" "PORTABLEGL" "TINYGL" "PIXIJS")

option(CNA_RENDERER_SDL_RENDERER "Enable SDL_Renderer graphics renderer" OFF)
option(CNA_RENDERER_OPENGLES2 "Enable OpenGL ES 2.0 graphics renderer (internally: EasyGL)" OFF)
option(CNA_RENDERER_OPENGLES3 "Enable OpenGL ES graphics renderer (internally: EasyGL)" OFF)
option(CNA_RENDERER_OPENGL33 "Enable desktop OpenGL 3.3 core graphics renderer (internally: EasyGL)" OFF)
option(CNA_RENDERER_WEBGL1 "Enable WebGL 1 graphics renderer, Emscripten only (internally: EasyGL)" OFF)
option(CNA_RENDERER_WEBGL2 "Enable WebGL 2 graphics renderer, Emscripten only (internally: EasyGL)" OFF)
option(CNA_RENDERER_BGFX "Enable bgfx graphics renderer" OFF)
option(CNA_RENDERER_VULKAN "Enable Vulkan graphics renderer" OFF)
option(CNA_RENDERER_WEBGPU "Enable WebGPU graphics renderer (wgpu-native)" OFF)
option(CNA_RENDERER_HEADLESS "Enable Headless (no GPU/window) graphics renderer" OFF)
option(CNA_RENDERER_SOFTWARE "Enable Software (CPU rasterizer) graphics renderer" OFF)
# plans/plan_stub.md: deliberately minimal no-op graphics renderer -- renders nothing, touches no SDL
# window/video subsystem/GPU library, keeps no bookkeeping of any kind (unlike HEADLESS's
# validation modes/counters or SOFTWARE's real CPU rasterizer). Named "Stub" rather than "Null" to
# avoid colliding with the <cstddef>/<cstdlib> NULL macro (see plans/plan_stub.md's naming section).
option(CNA_RENDERER_STUB "Enable Stub (no-op) graphics renderer" OFF)
# rswinkle/PortableGL: a single-header, C99, CPU software implementation of an OpenGL 3.x-ish
# pipeline (real buffers, vertex attribs, programmable vertex/fragment shaders as C function
# pointers, glDrawArrays/glDrawElements, textures) -- no GPU/window required, same "genuine
# CPU-only renderer" category as HEADLESS/SOFTWARE/STUB above, but the rasterization/shading
# pipeline is delegated to real PortableGL API calls rather than a hand-rolled rasterizer.
option(CNA_RENDERER_PORTABLEGL "Enable PortableGL (rswinkle/PortableGL, CPU software OpenGL 3.x) graphics renderer" OFF)
# C-Chads/tinygl: a maintained fork of Fabrice Bellard's TinyGL -- a CPU implementation of a
# fixed-function OpenGL 1.x subset (no shaders at all), same "no GPU, no window" category as
# HEADLESS/SOFTWARE/STUB/PORTABLEGL above. Where PORTABLEGL is the shader-era CPU GL, TINYGL is the
# fixed-function one; see plans/plan_tinygl.md and docs/tinygl-renderer.md for the capability boundary.
option(CNA_RENDERER_TINYGL "Enable TinyGL (C-Chads/tinygl, CPU fixed-function OpenGL 1.x) graphics renderer" OFF)
option(CNA_RENDERER_DIRECTX11 "Enable Direct3D 11 graphics renderer (Windows only)" OFF)
option(CNA_RENDERER_DIRECTX12 "Enable Direct3D 12 graphics renderer (Windows only)" OFF)
option(CNA_RENDERER_DIRECT2D "Enable Direct2D 1.1 graphics renderer (Windows only, 2D-only)" OFF)
# plans/plan_canvas.md: HTML Canvas 2D renderer -- Emscripten-only (design decision 1), a browser-native,
# GPU-free 2D-only renderer using canvas.getContext('2d') instead of WEBGL2's WebGL context.
option(CNA_RENDERER_CANVAS "Enable HTML Canvas 2D graphics renderer (Emscripten only)" OFF)
# plans/plan_html_dom.md: HTML DOM renderer -- Emscripten-only (design decision 1), 2D-only, rendering
# SpriteBatch output as pooled CSS-transformed <div> elements instead of rasterizing into a canvas.
option(CNA_RENDERER_HTML_DOM "Enable HTML DOM (CSS-composited) graphics renderer (Emscripten only)" OFF)
option(CNA_RENDERER_FREEDIRECT "Enable FreeDirect (DirectDraw via the ../free-direct sibling reimplementation; formerly DIRECTX3) graphics renderer" OFF)
option(CNA_RENDERER_DIRECTX9 "Enable Direct3D 9 graphics renderer (Windows only)" OFF)
# plans/plan_dx1.md: real DirectX 1 (DirectDraw v1) graphics renderer -- genuine ddraw.h v1 COM
# interfaces (IDirectDraw/IDirectDrawSurface/DDSURFACEDESC, never IDirectDraw2+), Windows-only,
# same MinGW-cross-compile + Wine delivery route as DIRECTX9/DIRECTX11/DIRECTX12 (Route B). Unlike FreeDirect (formerly DIRECTX3), this
# renderer deliberately does NOT use ../free-direct -- see plans/plan_dxold.md's roadmap.
option(CNA_RENDERER_DIRECTX1 "Enable Direct X 1 (real DirectDraw v1) graphics renderer (Windows only)" OFF)
# plans/plan_dx2.md: real DirectX 2 graphics renderer -- 2D layer is a verbatim port of DIRECTX1's real
# DirectDraw v1 (IDirectDraw/IDirectDrawSurface/DDSURFACEDESC, still never IDirectDraw2+). 3D layer
# uses IDirect3D2/IDirectDrawSurface2::DrawPrimitive (immediate-mode, no execute buffers) -- the
# literal DirectX-2-SDK execute-buffer Direct3D model (IDirect3D/IDirect3DDevice::Execute/
# D3DOP_TRIANGLE) was spiked exhaustively and renders black in this environment's Wine, while
# IDirect3DDevice2::DrawPrimitive/DrawIndexedPrimitive (one interface revision later, DX3-SDK) was
# proven to work (real Gouraud interpolation, real Z-test occlusion, real texture sampling) -- see
# plans/plan_dx2.md's status note and `dx2-spike/README.md` for the full spike record and the project
# owner's confirmation of this scope choice.
option(CNA_RENDERER_DIRECTX2 "Enable Direct X 2 (real DirectDraw v1 + Direct3D v2 DrawPrimitive) graphics renderer (Windows only)" OFF)
# plans/plan_dx3.md: real DirectX 3 graphics renderer. Originally landed under the temporary DX30 name
# because the free-direct-backed renderer owned "DIRECTX3" at the time; renamed to DIRECTX3 on 2026-08-04
# (and to DIRECTX3 in the 2026-08 naming normalization)
# when that renderer became FREEDIRECT (owner instruction -- see plans/plan_dxold.md's naming-transition
# section). Mechanical port of DIRECTX2's own 2D layer (upgraded to IDirectDraw2) + 3D layer
# (verbatim, including Phase O9's CPU lighting).
option(CNA_RENDERER_DIRECTX3 "Enable Direct X 3 (real DirectDraw v2 + Direct3D v2 DrawPrimitive) graphics renderer (Windows only)" OFF)
# plans/plan_dx5.md: real DirectX 5 graphics renderer -- DirectDraw v4 (IDirectDraw4/IDirectDrawSurface4/
# DDSURFACEDESC2/DDSCAPS2, every surface not just the top object) + Direct3D v3 (IDirect3D3/
# IDirect3DDevice3/IDirect3DViewport3), the first release where execute buffers are gone entirely
# (DrawPrimitive/DrawIndexedPrimitive only, selected via the D3DFVF_TLVERTEX FVF bitmask instead
# of the old D3DVERTEXTYPE enum). Mechanical port of DIRECTX3's own 2D+3D layers (including Phase O9's
# CPU lighting), upgraded further.
option(CNA_RENDERER_DIRECTX5 "Enable Direct X 5 (real DirectDraw v4 + Direct3D v3 FVF DrawPrimitive) graphics renderer (Windows only)" OFF)
# plans/plan_dx6.md: real DirectX 6 graphics renderer -- the EXACT SAME COM interfaces DIRECTX5 already uses
# (IDirectDraw4/IDirect3D3/IDirect3DDevice3/IDirect3DViewport3, no new interface revision at this
# DirectX era). Its own delta: real stencil buffer operations (D3DRENDERSTATE_STENCIL*, spike-
# confirmed genuine write+test behavior) against a combined depth+stencil Z-buffer surface,
# resolving the "no real stencil until DIRECTX6" boundary DIRECTX2/DIRECTX3/DIRECTX5 all documented. Multitexture
# stays accepted-and-ignored (D3DTLVERTEX only carries one texture-coordinate pair).
option(CNA_RENDERER_DIRECTX6 "Enable Direct X 6 (real DirectDraw v4 + Direct3D v3, real stencil) graphics renderer (Windows only)" OFF)
# plans/plan_dx7.md: real DirectX 7 graphics renderer -- genuinely new interfaces vs DIRECTX6: IDirectDraw7/
# IDirectDrawSurface7 (created via DirectDrawCreateEx) + IDirect3D7/IDirect3DDevice7. DIRECTX7 REMOVES
# the whole viewport-object concept (IDirect3DViewport3 no longer exists at all --
# IDirect3DDevice7::SetViewport/Clear are direct device methods instead) and simplifies texture
# binding to a direct SetTexture(stage, surface) call (no more texture-handle indirection). Stencil
# is unchanged from DIRECTX6, ported verbatim.
option(CNA_RENDERER_DIRECTX7 "Enable Direct X 7 (real DirectDraw v7 + Direct3D v7, flattened device model) graphics renderer (Windows only)" OFF)
# plans/plan_dx8.md: real DirectX 8 graphics renderer -- "DirectDraw+Direct3D merged" (no DirectDraw at
# all): a single IDirect3D8::CreateDevice call creates both the device and its own swap chain.
# Delivered via DXVK (D8VK, merged into DXVK 2.0+), not Wine's own d3d8.dll -- mingw-w64 ships no
# real d3d8 import library for x86_64, so this renderer links DXVK's own d3d8.dll.a directly.
# Fixed-function 3D only (scope decision): real XNA effects need ps_2_0+ regardless of Shader
# Model 1.x support, so a real SM1.x pipeline would not make CreateEffectRenderer usable for actual
# XNA content. The 2D SpriteBatch layer is real GPU-rendered textured quads (DirectDraw does not
# exist at this era at all), not a CPU compositor like DIRECTX1..DIRECTX7's own.
option(CNA_RENDERER_DIRECTX8 "Enable Direct X 8 (real Direct3D 8, DXVK-delivered, fixed-function) graphics renderer (Windows only)" OFF)
# plans/plan_d3d10.md: real Direct3D 10 renderer -- unlike DIRECTX1..DIRECTX8, D3D10 removed the fixed-function
# pipeline entirely, so every draw needs a real HLSL vs_4_0/ps_4_0 shader pair (D3DCompile,
# following DIRECTX9/DIRECTX11's own precedent), real state OBJECTS (ID3D10BlendState/etc, not per-call
# render states), and real MRT support. Delivered via Wine's own builtin d3d10.dll/d3d10_1.dll
# (DXVK 2.6.0 ships no d3d10.dll at all) forwarding to DXVK's real d3d10core.dll + dxgi.dll.
option(CNA_RENDERER_DIRECTX10 "Enable Direct3D 10 (real ID3D10Device, DXVK-delivered via d3d10core, real HLSL shaders) graphics renderer (Windows only)" OFF)
option(CNA_RENDERER_SDL_GPU "Enable SDL_gpu graphics renderer" OFF)
# plans/plan_opengles1.md design decision 1: a genuinely separate renderer from EASYGL -- EASYGL targets
# WebGL2/OpenGL ES 3.0 (shader-based) and cannot create an OpenGL ES 1.1 (fixed-function "Common"
# profile) context at all. Requires a real system GLESv1_CM library/headers (see
# BackendLibraries.cmake's own find_library/find_path FATAL_ERROR gate below), same "hard system
# dependency, not vendored" shape as VULKAN's find_package(Vulkan REQUIRED).
option(CNA_RENDERER_OPENGLES1 "Enable OpenGL ES 1.1 (fixed-function) graphics renderer" OFF)

# plans/plan_opengl4.md GL4-1: real desktop OpenGL 4.x core-profile graphics renderer -- deliberately
# independent of the GL-family OPENGLES3/OPENGL33/WEBGL1/WEBGL2 renderers (internally EasyGL; the
# OPENGLES3 default targets OpenGL ES 3.0 via EasyGLRenderer's own
# SDL_GL_CONTEXT_PROFILE_ES context request, not a real desktop GL 4.x core profile).
option(CNA_RENDERER_OPENGL4 "Enable real desktop OpenGL 4.x core-profile graphics renderer" OFF)

option(CNA_RENDERER_OPENGL1 "Enable native legacy OpenGL 1.x fixed-function graphics renderer" OFF)

# plans/plan_opengl2.md: native desktop OpenGL 2.1 (compatibility profile, GLSL 1.10) graphics renderer --
# deliberately independent of the GL-family OPENGLES3/OPENGL33/WEBGL1/WEBGL2 renderers (internally
# EasyGL, which targets OpenGL ES 3.0 / WebGL2 or a 3.3 core profile and cannot create a desktop
# GL 2.1 compatibility context).
option(CNA_RENDERER_OPENGL2 "Enable native OpenGL 2.1 graphics renderer (no EasyGL)" OFF)




# Real Glide calls are made dynamically to a caller-supplied glide3x.dll. CNA deliberately does
# not vendor dgVoodoo2: its redistribution terms do not permit bundling it into a framework.
option(CNA_RENDERER_GLIDE "Enable Glide 3.x graphics renderer (Windows, external glide3x.dll at runtime)" OFF)
option(CNA_RENDERER_GDI "Enable classic Win32 GDI (2D-only) graphics renderer" OFF)




# Metal owns the renderer directly; SDL provides only the native macOS window and CAMetalLayer.
option(CNA_RENDERER_METAL "Enable native Apple Metal graphics renderer (macOS only)" OFF)

# plans/plan_fna3d.md: FNA3D (https://github.com/FNA-XNA/FNA3D) -- the XNA-shaped C graphics library FNA
# itself renders through. Like LLGL/DILIGENT/SOKOL/BGFX this names a portable middleware layer
# rather than one native API: FNA3D picks SDL_GPU, Direct3D 11 or OpenGL at RUNTIME (overridable
# with the FNA3D_FORCE_DRIVER SDL hint). Shaders are Direct3D 9 Effect Framework binaries executed
# through MojoShader -- FNA3D has no other shader entry point at all, so the stock-effect blobs are
# fetched from the pinned FNA checkout alongside FNA3D itself (see cmake/ThirdPartyFNA3D.cmake).
option(CNA_RENDERER_FNA3D "Enable FNA3D graphics renderer (FNA-XNA/FNA3D + MojoShader)" OFF)

# plans/plan_svg_dom.md: SVG DOM renderer -- Emscripten-only (same design decision as CANVAS/HTML_DOM),
# 2D-only, rendering SpriteBatch output as real <svg>/<image> elements (an <svg> viewport per
# sprite crops its source rectangle; SVG-native feColorMatrix filters apply the tint) instead of
# either rasterizing into a <canvas> (CANVAS) or CSS-transforming pooled <div>s (HTML_DOM).
option(CNA_RENDERER_SVG_DOM "Enable SVG DOM graphics renderer (Emscripten only)" OFF)

# OpenVG 1.1 (Khronos vector graphics API), implemented by ShivaVG on top of a real desktop OpenGL
# context this renderer creates itself via SDL (same "own GL context, no EasyGL" shape as
# OPENGL1/OPENGL2) -- see cmake/ThirdPartyOpenVG.cmake. 2D-only: OpenVG has no 3D pipeline.
option(CNA_RENDERER_OPENVG "Enable OpenVG (ShivaVG) 2D vector graphics renderer" OFF)

# plans/plan_pixijs.md: PixiJS (https://pixijs.com/) renderer -- Emscripten-only (same design decision as
# CANVAS/HTML_DOM/SVG_DOM), 2D-only in v1 scope, rendering SpriteBatch output through PixiJS's own
# retained-mode WebGL scene graph (PIXI.Sprite/PIXI.RenderTexture) instead of hand-rolled WebGL
# batching (WEBGL2), raw Canvas2D calls (CANVAS) or pooled DOM elements (HTML_DOM).
option(CNA_RENDERER_PIXIJS "Enable PixiJS graphics renderer (Emscripten only)" OFF)


set(_cna_explicit_renderer_selection OFF)
if(CNA_RENDERER_SDL_RENDERER OR CNA_RENDERER_OPENGLES2 OR CNA_RENDERER_OPENGLES3 OR CNA_RENDERER_OPENGL33 OR CNA_RENDERER_WEBGL1 OR CNA_RENDERER_WEBGL2 OR CNA_RENDERER_BGFX OR CNA_RENDERER_VULKAN OR CNA_RENDERER_WEBGPU OR CNA_RENDERER_HEADLESS OR CNA_RENDERER_SOFTWARE OR CNA_RENDERER_STUB OR CNA_RENDERER_DIRECTX11 OR CNA_RENDERER_DIRECTX12 OR CNA_RENDERER_DIRECT2D OR CNA_RENDERER_CANVAS OR CNA_RENDERER_HTML_DOM OR CNA_RENDERER_FREEDIRECT OR CNA_RENDERER_DIRECTX9 OR CNA_RENDERER_DIRECTX1 OR CNA_RENDERER_DIRECTX2 OR CNA_RENDERER_DIRECTX3 OR CNA_RENDERER_DIRECTX5 OR CNA_RENDERER_DIRECTX6 OR CNA_RENDERER_DIRECTX7 OR CNA_RENDERER_DIRECTX8 OR CNA_RENDERER_DIRECTX10 OR CNA_RENDERER_SDL_GPU OR CNA_RENDERER_OPENGLES1 OR CNA_RENDERER_OPENGL4 OR CNA_RENDERER_OPENGL1 OR CNA_RENDERER_OPENGL2 OR CNA_RENDERER_GLIDE OR CNA_RENDERER_GDI OR CNA_RENDERER_METAL OR CNA_RENDERER_FNA3D OR CNA_RENDERER_SVG_DOM OR CNA_RENDERER_OPENVG OR CNA_RENDERER_PORTABLEGL OR CNA_RENDERER_TINYGL OR CNA_RENDERER_PIXIJS)
    set(_cna_explicit_renderer_selection ON)
endif()

if(_cna_explicit_renderer_selection)
    set(_cna_enabled_renderers)
    if(CNA_RENDERER_SDL_RENDERER)
        list(APPEND _cna_enabled_renderers "SDL_RENDERER")
    endif()
    if(CNA_RENDERER_OPENGLES2)
        list(APPEND _cna_enabled_renderers "OPENGLES2")
    endif()
    if(CNA_RENDERER_OPENGLES3)
        list(APPEND _cna_enabled_renderers "OPENGLES3")
    endif()
    if(CNA_RENDERER_OPENGL33)
        list(APPEND _cna_enabled_renderers "OPENGL33")
    endif()
    if(CNA_RENDERER_WEBGL1)
        list(APPEND _cna_enabled_renderers "WEBGL1")
    endif()
    if(CNA_RENDERER_WEBGL2)
        list(APPEND _cna_enabled_renderers "WEBGL2")
    endif()
    if(CNA_RENDERER_BGFX)
        list(APPEND _cna_enabled_renderers "BGFX")
    endif()
    if(CNA_RENDERER_VULKAN)
        list(APPEND _cna_enabled_renderers "VULKAN")
    endif()
    if(CNA_RENDERER_WEBGPU)
        list(APPEND _cna_enabled_renderers "WEBGPU")
    endif()
    if(CNA_RENDERER_HEADLESS)
        list(APPEND _cna_enabled_renderers "HEADLESS")
    endif()
    if(CNA_RENDERER_SOFTWARE)
        list(APPEND _cna_enabled_renderers "SOFTWARE")
    endif()
    if(CNA_RENDERER_STUB)
        list(APPEND _cna_enabled_renderers "STUB")
    endif()
    if(CNA_RENDERER_DIRECTX11)
        list(APPEND _cna_enabled_renderers "DIRECTX11")
    endif()
    if(CNA_RENDERER_DIRECTX12)
        list(APPEND _cna_enabled_renderers "DIRECTX12")
    endif()
    if(CNA_RENDERER_DIRECT2D)
        list(APPEND _cna_enabled_renderers "DIRECT2D")
    endif()
    if(CNA_RENDERER_CANVAS)
        list(APPEND _cna_enabled_renderers "CANVAS")
    endif()
    if(CNA_RENDERER_HTML_DOM)
        list(APPEND _cna_enabled_renderers "HTML_DOM")
    endif()
    if(CNA_RENDERER_FREEDIRECT)
        list(APPEND _cna_enabled_renderers "FREEDIRECT")
    endif()
    if(CNA_RENDERER_DIRECTX9)
        list(APPEND _cna_enabled_renderers "DIRECTX9")
    endif()
    if(CNA_RENDERER_DIRECTX1)
        list(APPEND _cna_enabled_renderers "DIRECTX1")
    endif()
    if(CNA_RENDERER_DIRECTX2)
        list(APPEND _cna_enabled_renderers "DIRECTX2")
    endif()
    if(CNA_RENDERER_DIRECTX3)
        list(APPEND _cna_enabled_renderers "DIRECTX3")
    endif()
    if(CNA_RENDERER_DIRECTX5)
        list(APPEND _cna_enabled_renderers "DIRECTX5")
    endif()
    if(CNA_RENDERER_DIRECTX6)
        list(APPEND _cna_enabled_renderers "DIRECTX6")
    endif()
    if(CNA_RENDERER_DIRECTX7)
        list(APPEND _cna_enabled_renderers "DIRECTX7")
    endif()
    if(CNA_RENDERER_DIRECTX8)
        list(APPEND _cna_enabled_renderers "DIRECTX8")
    endif()
    if(CNA_RENDERER_DIRECTX10)
        list(APPEND _cna_enabled_renderers "DIRECTX10")
    endif()
    if(CNA_RENDERER_SDL_GPU)
        list(APPEND _cna_enabled_renderers "SDL_GPU")
    endif()
    if(CNA_RENDERER_OPENGLES1)
        list(APPEND _cna_enabled_renderers "OPENGLES1")
    endif()
    if(CNA_RENDERER_OPENGL4)
        list(APPEND _cna_enabled_renderers "OPENGL4")
    endif()
    if(CNA_RENDERER_OPENGL1)
        list(APPEND _cna_enabled_renderers "OPENGL1")
    endif()
    if(CNA_RENDERER_OPENGL2)
        list(APPEND _cna_enabled_renderers "OPENGL2")
    endif()
    if(CNA_RENDERER_GLIDE)
        list(APPEND _cna_enabled_renderers "GLIDE")
    endif()
    if(CNA_RENDERER_GDI)
        list(APPEND _cna_enabled_renderers "GDI")
    endif()
    if(CNA_RENDERER_METAL)
        list(APPEND _cna_enabled_renderers "METAL")
    endif()
    if(CNA_RENDERER_FNA3D)
        list(APPEND _cna_enabled_renderers "FNA3D")
    endif()
    if(CNA_RENDERER_SVG_DOM)
        list(APPEND _cna_enabled_renderers "SVG_DOM")
    endif()
    if(CNA_RENDERER_OPENVG)
        list(APPEND _cna_enabled_renderers "OPENVG")
    endif()
    if(CNA_RENDERER_PORTABLEGL)
        list(APPEND _cna_enabled_renderers "PORTABLEGL")
    endif()
    if(CNA_RENDERER_TINYGL)
        list(APPEND _cna_enabled_renderers "TINYGL")
    endif()
    if(CNA_RENDERER_PIXIJS)
        list(APPEND _cna_enabled_renderers "PIXIJS")
    endif()

    list(LENGTH _cna_enabled_renderers _cna_enabled_renderers_count)
    if(NOT _cna_enabled_renderers_count EQUAL 1)
        message(FATAL_ERROR "CNA: Exactly one renderer option must be ON when using CNA_RENDERER_* options.")
    endif()

    list(GET _cna_enabled_renderers 0 CNA_GRAPHICS_RENDERER)
endif()

# --- Multi-renderer selection (plans/plan_runtimerenderer.md design decision 1, phase P6) ---
#
# CNA_GRAPHICS_RENDERER stays the primary, single-valued option: it names this build's DEFAULT
# renderer, and a build that sets nothing else behaves exactly as it always has.
#
# CNA_GRAPHICS_RENDERERS is the opt-in second mode -- a list of identities to compile in, from which
# one is chosen at runtime (CNA::GraphicsRendererSelection). When it is not set it is simply the
# single default, so every code path below runs identically for existing builds.
#
# Resolved by its own file, which `cmake -P` can run standalone, so the membership contract has a
# real test rather than only a comment (cmake/Tests/RendererDefaultCase.cmake). Included, not
# called as a function, because it publishes CNA_RENDERER_IDENTITIES into this scope.
include(cmake/RendererDefaultSelection.cmake)

# Design decision 11: reject an unbuildable combination here, with a reason, rather than letting it
# surface as a duplicate-symbol link error.
include(cmake/RendererCombinations.cmake)
cna_validate_renderer_combination(${_cna_renderer_identities})

list(LENGTH _cna_renderer_identities _cna_renderer_identity_count)
if(_cna_renderer_identity_count GREATER 1)
    # The set and its default were already announced by RendererDefaultSelection.cmake; this line
    # states only what is different about this mode.
    message(STATUS "CNA: multi-renderer build -- the renderer is chosen at runtime "
                   "(CNA::GraphicsRendererSelection)")
    # Consumed by GraphicsRendererType.hpp and the identity-reporting accessors (phase P7).
    add_compile_definitions(CNA_MULTI_RENDERER)
endif()

# The per-identity configuration below is a MACRO, not a function, on purpose: macros do not create
# a scope, so every set()/add_compile_definitions()/add_subdirectory() inside behaves exactly as it
# did when this was straight-line code. For a single-identity list the execution is identical.
#
# plans/plan_runtimerenderer.md RTR-P6-4: an identity's own CNA_RENDERER_<X> macro is announced by
# appending it to _cna_identity_defines -- NEVER by calling add_compile_definitions() here. The
# loop below applies the list to that family's own target and, for the DEFAULT identity only, to
# the whole project. add_compile_definitions() is directory-scoped and this file is included from
# the top-level CMakeLists.txt, so calling it from an arm defines that identity's macro
# project-wide for every identity in the list, not just the default -- which breaks the invariant
# every compile-time renderer question in the tree rests on ("the CNA_RENDERER_<X> that is defined
# names the DEFAULT"), including getCurrentGraphicsRendererType()'s #elif chain, which would then
# answer with whichever identity happens to sit earliest in it. TINYGL and PIXIJS were both added
# that way and are fixed; scripts/check_runtime_renderer_discipline.py now fails on a new one.
macro(cna_configure_renderer_identity)
    set(_cna_identity_defines)
# PLAT-140: a terminal consumes finished CPU frames through IPlatformSurfacePresenter; it has no
# graphical native window that a GPU API could bind. This check deliberately precedes every
# renderer dependency probe below, so an incompatible pair always fails with this explanation
# instead of, for example, first asking a TERMINAL+VULKAN build to install a Vulkan SDK. SKIA and
# BLEND2D are included because their rasterization is CPU-side; Phase 4 moves their presentation
# edge from SDL to the platform surface presenter.
set(_cna_terminal_renderers SOFTWARE SKIA BLEND2D PORTABLEGL HEADLESS STUB)
if(CNA_PLATFORM STREQUAL "TERMINAL" AND NOT CNA_GRAPHICS_RENDERER IN_LIST _cna_terminal_renderers)
    list(JOIN _cna_terminal_renderers ", " _cna_terminal_renderers_text)
    message(FATAL_ERROR
        "CNA: CNA_PLATFORM=TERMINAL has no native graphical window, so renderer "
        "${CNA_GRAPHICS_RENDERER} cannot be selected.\n"
        "Choose a CPU renderer: ${_cna_terminal_renderers_text}.")
endif()

# plans/plan_dx.md design decision 2: DIRECTX11/DIRECTX12 genuinely cannot build anywhere but Windows (native or
# MinGW/MSVC cross-compile) -- d3d11.h/d3d12.h/dxgi.h do not exist elsewhere. Unlike BGFX's soft
# WARNING-only platform check below, this is a hard FATAL_ERROR. plans/plan_dx9.md design decision 1
# extends this same gate to DIRECTX9 (d3d9.h is equally Windows-only). plans/plan_dx1.md design decision 1
# extends it again to DIRECTX1: unlike FreeDirect (formerly DIRECTX3; SDL3-backed ../free-direct, genuinely native-Linux-buildable),
# DIRECTX1 uses the real Windows ddraw.h, so it needs the exact same gate.
if((CNA_GRAPHICS_RENDERER STREQUAL "DIRECTX11" OR CNA_GRAPHICS_RENDERER STREQUAL "DIRECTX12" OR CNA_GRAPHICS_RENDERER STREQUAL "DIRECTX9" OR CNA_GRAPHICS_RENDERER STREQUAL "DIRECT2D" OR CNA_GRAPHICS_RENDERER STREQUAL "DIRECTX1" OR CNA_GRAPHICS_RENDERER STREQUAL "DIRECTX2" OR CNA_GRAPHICS_RENDERER STREQUAL "DIRECTX3" OR CNA_GRAPHICS_RENDERER STREQUAL "DIRECTX5" OR CNA_GRAPHICS_RENDERER STREQUAL "DIRECTX6" OR CNA_GRAPHICS_RENDERER STREQUAL "DIRECTX7" OR CNA_GRAPHICS_RENDERER STREQUAL "DIRECTX8" OR CNA_GRAPHICS_RENDERER STREQUAL "DIRECTX10" OR CNA_GRAPHICS_RENDERER STREQUAL "GLIDE" OR CNA_GRAPHICS_RENDERER STREQUAL "GDI")
        AND NOT CMAKE_SYSTEM_NAME STREQUAL "Windows")
    message(FATAL_ERROR
        "CNA: ${CNA_GRAPHICS_RENDERER} renderer only builds when targeting Windows. Either build "
        "natively on Windows, or cross-compile from Linux with "
        "-DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64.cmake")
endif()

# Glide's native application ABI carries the render-window handle in a 32-bit FxU32.  dgVoodoo2
# likewise documents its x86 Glide DLLs as the native-application variant; its x64 DLLs are for
# emulators such as 64-bit QEMU/DOSBox.  Do not silently build a pointer-truncating x64 target.
if(CNA_GRAPHICS_RENDERER STREQUAL "GLIDE" AND NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR
        "CNA: GLIDE targets the native 32-bit (x86) Glide 3.x ABI. Configure with an i686 "
        "Windows toolchain, for example cmake/toolchains/mingw-w64-i686.cmake.")
endif()

# plans/plan_apple.md APPLE-4: an iOS configure is rejected here unless CNA actually wires the selected
# renderer up for iOS. This runs before the individual per-renderer gates below so the failure
# names the platform rather than a dependency that was never configured for an iOS sysroot.
# No-op on macOS and on every non-Apple target.
cna_apple_validate_renderer("${CNA_GRAPHICS_RENDERER}")

# Native Metal is currently available only when targeting macOS. SDL is used only for
# window/CAMetalLayer integration; all rendering is performed directly through Metal.
# iOS is Metal's other natural home and the Apple allow-list above already refuses it by default;
# CNA_APPLE_ALLOW_UNVALIDATED_RENDERER=ON is the single documented escape hatch for experimenting
# with it there (plans/plan_apple.md APPLE-11), and changes nothing about what is supported.
if(CNA_GRAPHICS_RENDERER STREQUAL "METAL" AND NOT CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    if(CNA_APPLE_IOS AND CNA_APPLE_ALLOW_UNVALIDATED_RENDERER)
        message(WARNING
            "CNA: configuring METAL for iOS. The renderer's supported contract covers macOS only "
            "(docs/metal-renderer.md); its iOS build has no compile, runtime or pixel evidence.")
    else()
        message(FATAL_ERROR
            "CNA: METAL renderer is currently supported only on macOS; iOS and tvOS remain unvalidated.")
    endif()
endif()

# plans/plan_canvas.md design decision 1: HTML Canvas 2D is a browser DOM API and cannot exist outside
# an Emscripten/WebAssembly build -- same hard-gate shape as the DIRECTX11/DIRECTX12 Windows-only check
# just above, new condition.
if(CNA_GRAPHICS_RENDERER STREQUAL "OPENGL1" AND NOT (CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_SYSTEM_NAME STREQUAL "Windows"))
    message(FATAL_ERROR "CNA: OPENGL1 renderer is intentionally supported only on desktop Linux and Windows.")
endif()

# ShivaVG (the OPENVG renderer's upstream) is fixed-function/immediate-mode desktop OpenGL --
# glBegin/glVertexPointer-era GL that neither WebGL (Emscripten) nor a GLES-only mobile target can
# create a context for. Its own upstream README documents Linux/Windows/macOS support (autotools on
# Unix, Visual C++/mingw on Windows, gcc on macOS) and nothing else -- same "genuinely native-only"
# shape as OPENGL1's gate just above.
if(CNA_GRAPHICS_RENDERER STREQUAL "OPENVG" AND NOT (CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_SYSTEM_NAME STREQUAL "Windows" OR CMAKE_SYSTEM_NAME STREQUAL "Darwin"))
    message(FATAL_ERROR "CNA: OPENVG renderer is intentionally supported only on desktop Linux, Windows and macOS (ShivaVG needs a real fixed-function OpenGL context).")
endif()

# NanoVG's GL2 backend needs a real desktop OpenGL 2.x+ context (GLSL 1.10 shaders) -- neither
# WebGL (Emscripten) nor a GLES-only mobile target can create one. Same "genuinely native-only"
# shape as OPENVG's gate just above.
if(CNA_GRAPHICS_RENDERER STREQUAL "NANOVG" AND NOT (CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_SYSTEM_NAME STREQUAL "Windows" OR CMAKE_SYSTEM_NAME STREQUAL "Darwin"))
    message(FATAL_ERROR "CNA: NANOVG renderer is intentionally supported only on desktop Linux, Windows and macOS (NanoVG's GL2 backend needs a real desktop OpenGL context).")
endif()

if(CNA_GRAPHICS_RENDERER STREQUAL "CANVAS" AND NOT EMSCRIPTEN)
    message(FATAL_ERROR
        "CNA: CANVAS renderer only builds when targeting Emscripten (HTML Canvas is a browser DOM "
        "API). Configure with -DCMAKE_TOOLCHAIN_FILE=\$EMSDK/upstream/emscripten/cmake/Modules/"
        "Platform/Emscripten.cmake (or use emcmake).")
endif()

# plans/plan_html_dom.md design decision 1: document, HTMLDivElement and CSS only exist inside a browser.
if(CNA_GRAPHICS_RENDERER STREQUAL "HTML_DOM" AND NOT EMSCRIPTEN)
    message(FATAL_ERROR
        "CNA: HTML_DOM renderer only builds when targeting Emscripten (it renders through real DOM "
        "elements and CSS). Configure with -DCMAKE_TOOLCHAIN_FILE=\$EMSDK/upstream/emscripten/"
        "cmake/Modules/Platform/Emscripten.cmake (or use emcmake).")
endif()

# plans/plan_svg_dom.md design decision 1: same reasoning as CANVAS/HTML_DOM above -- document, SVG
# namespace elements and the browser DOM only exist inside a browser.
if(CNA_GRAPHICS_RENDERER STREQUAL "SVG_DOM" AND NOT EMSCRIPTEN)
    message(FATAL_ERROR
        "CNA: SVG_DOM renderer only builds when targeting Emscripten (it renders through real SVG "
        "DOM elements). Configure with -DCMAKE_TOOLCHAIN_FILE=\$EMSDK/upstream/emscripten/"
        "cmake/Modules/Platform/Emscripten.cmake (or use emcmake).")
endif()

# plans/plan_pixijs.md design decision 1: same reasoning as CANVAS/HTML_DOM/SVG_DOM above -- PixiJS is a
# JavaScript library expecting a document/HTMLCanvasElement/WebGL context, none of which exist
# outside a browser.
if(CNA_GRAPHICS_RENDERER STREQUAL "PIXIJS" AND NOT EMSCRIPTEN)
    message(FATAL_ERROR
        "CNA: PIXIJS renderer only builds when targeting Emscripten (PixiJS is a browser-only "
        "JavaScript/WebGL library). Configure with -DCMAKE_TOOLCHAIN_FILE=\$EMSDK/upstream/"
        "emscripten/cmake/Modules/Platform/Emscripten.cmake (or use emcmake).")
endif()


# plans/plan_glbackends.md Phase A/GLB-7: all 5 GL-family public renderers (OPENGLES2/OPENGLES3/OPENGL33
# desktop, WEBGL1/WEBGL2 Emscripten) share one internal implementation (EasyGL, on top of the
# sibling easy-gl library) -- this block sets it up once regardless of which of the 5 was selected.
#
# GLB-38 done: the WebGL1 work (GLB-30..35) landed on easy-gl develop, so this builds against
# the canonical '../easy-gl' sibling checkout again; the temporary '../easy-glrvc' (branch
# 'rvc') redirect from GLB-7 is retired.
if(CNA_GRAPHICS_RENDERER STREQUAL "OPENGLES2" OR CNA_GRAPHICS_RENDERER STREQUAL "OPENGLES3"
        OR CNA_GRAPHICS_RENDERER STREQUAL "OPENGL33"
        OR CNA_GRAPHICS_RENDERER STREQUAL "WEBGL1" OR CNA_GRAPHICS_RENDERER STREQUAL "WEBGL2")
    if((CNA_GRAPHICS_RENDERER STREQUAL "OPENGLES2" OR CNA_GRAPHICS_RENDERER STREQUAL "OPENGLES3"
            OR CNA_GRAPHICS_RENDERER STREQUAL "OPENGL33") AND EMSCRIPTEN)
        message(FATAL_ERROR
            "CNA: ${CNA_GRAPHICS_RENDERER} is a desktop/mobile GL renderer and cannot target "
            "Emscripten -- use WEBGL1 or WEBGL2 instead.")
    endif()
    if((CNA_GRAPHICS_RENDERER STREQUAL "WEBGL1" OR CNA_GRAPHICS_RENDERER STREQUAL "WEBGL2") AND NOT EMSCRIPTEN)
        message(FATAL_ERROR
            "CNA: ${CNA_GRAPHICS_RENDERER} only builds when targeting Emscripten (WebGL is a "
            "browser API). Configure with -DCMAKE_TOOLCHAIN_FILE=\$EMSDK/upstream/emscripten/"
            "cmake/Modules/Platform/Emscripten.cmake (or use emcmake), or use OPENGLES2/OPENGLES3/"
            "OPENGL33 for a native desktop/mobile GL build.")
    endif()
    if((CNA_GRAPHICS_RENDERER STREQUAL "OPENGLES2" OR CNA_GRAPHICS_RENDERER STREQUAL "OPENGLES3")
            AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(WARNING "CNA: ${CNA_GRAPHICS_RENDERER} renderer is primarily tested on Linux. Other platforms may require additional setup.")
    endif()
    # easy-gl is a SIBLING repository checkout, not a git submodule of this
    # repo (Task DEV-BUILD-001) -- see sharp-runtime's identical check above
    # for the full rationale.
    # plans/plan_runtimerenderer.md P11: several GL identities can now be selected at once, and they all
    # share this one easy-gl subdirectory -- add it only for the first of them.
    if(NOT TARGET easy-gl AND NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../easy-gl/CMakeLists.txt")
        message(FATAL_ERROR
            "CNA: Missing sibling repository 'easy-gl' at "
            "${CMAKE_CURRENT_SOURCE_DIR}/../easy-gl -- this is a separate git "
            "checkout (branch 'develop' of easy-gl) expected next to this repo's "
            "own directory, not a git submodule (git submodule update --init will "
            "not fetch it). easy-gl itself expects its own sibling '../meta-gl' "
            "checkout (branch 'develop' of meta-gl).")
    endif()
    if(NOT _cna_easygl_subdir_added)
        if(EMSCRIPTEN)
            set(EASYGL_EMSCRIPTEN_EXCEPTION_MODEL "JS" CACHE STRING
                "Exception ABI used by easy-gl when embedded in CNA" FORCE)
        endif()
        add_subdirectory(../easy-gl easy-gl)
        set(_cna_easygl_subdir_added TRUE)
    endif()
endif()

# plans/plan_freedirect.md design decision 10 / Task DX3-2: free-direct is a SIBLING repository checkout, not a
# git submodule of this repo -- same rationale as sharp-runtime/easy-gl's identical checks above.
# free-direct's own CMakeLists.txt (add_subdirectory(../free-api ...)) resolves SDL3::SDL3/
# SDL3_image::SDL3_image/SDL3_mixer::SDL3_mixer from CNA's own already-vendored targets (set up by
# cna_configure_vendored_sdl() above, before renderer selection runs), so no
# -DFREE_API_USE_SYSTEM_SDL3 flag is needed here, mirroring how ../free-eggbert/../planetblupi
# already consume free-direct today (design decision 10).
if(CNA_GRAPHICS_RENDERER STREQUAL "FREEDIRECT")
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

if(CNA_GRAPHICS_RENDERER STREQUAL "BGFX" AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(WARNING "CNA: BGFX renderer is primarily tested on Linux. Other platforms may require additional setup.")
endif()


# plans/plan_sokol.md design decision 2: sokol_gfx is itself a multi-API abstraction, so the SOKOL
# renderer has a second axis of its own -- which native API sokol_gfx dispatches onto. This is a
# compile-time choice exactly like CNA_GRAPHICS_RENDERER, resolved here into the single SOKOL_*
# define sokol_gfx.h switches on. GLCORE (OpenGL 4.1 core, sokol_gfx's own minimum for that
# renderer) is the default and the only value verified on this project's Linux dev machine; the
# rest are wired but unproven, and say so at configure time rather than pretending otherwise.
set(CNA_SOKOL_API "GLCORE" CACHE STRING "Native API sokol_gfx dispatches onto (GLCORE, GLES3, D3D11, METAL, WGPU)")
set_property(CACHE CNA_SOKOL_API PROPERTY STRINGS "GLCORE" "GLES3" "DIRECTX11" "METAL" "WGPU")

if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
    message(STATUS "CNA: Using SDL_RENDERER graphics renderer")
    set(RENDERER_DIR "modules/renderers/sdl-renderer")
    set(RENDERER_TARGET "cna_renderer_sdl_renderer")
    list(APPEND _cna_identity_defines CNA_RENDERER_SDL_RENDERER)
    set(CNA_RENDERER_DEFINE "CNA_RENDERER_SDL_RENDERER")
elseif(CNA_GRAPHICS_RENDERER STREQUAL "OPENGL1")
    message(STATUS "CNA: Using OPENGL1 native fixed-function graphics renderer")
    set(RENDERER_DIR "modules/renderers/opengl1")
    set(RENDERER_TARGET "cna_renderer_opengl1")
    list(APPEND _cna_identity_defines CNA_RENDERER_OPENGL1)
    set(CNA_RENDERER_DEFINE "CNA_RENDERER_OPENGL1")
elseif(CNA_GRAPHICS_RENDERER STREQUAL "OPENGLES2" OR CNA_GRAPHICS_RENDERER STREQUAL "OPENGLES3"
        OR CNA_GRAPHICS_RENDERER STREQUAL "OPENGL33"
        OR CNA_GRAPHICS_RENDERER STREQUAL "WEBGL1" OR CNA_GRAPHICS_RENDERER STREQUAL "WEBGL2")
    message(STATUS "CNA: Using ${CNA_GRAPHICS_RENDERER} graphics renderer (internal implementation: EasyGL)")
    set(RENDERER_DIR "modules/renderers/easygl")
    set(RENDERER_TARGET "cna_renderer_easygl")
    # CNA_RENDERER_EASYGL is the internal implementation identity -- existing #ifdef
    # CNA_RENDERER_EASYGL guards elsewhere in the codebase keep working unmodified regardless of
    # which of the 5 public GL profiles below was selected (plans/plan_glbackends.md GLB-3).
    list(APPEND _cna_identity_defines CNA_RENDERER_EASYGL)
    set(CNA_RENDERER_DEFINE "CNA_RENDERER_EASYGL")
    # CNA_GL_PROFILE_* selects the GL context/shader profile within the shared EasyGL
    # implementation -- see plans/plan_glbackends.md Phase B/GLB-8 for how EasyGLRenderer.cpp
    # uses this to choose context-creation attributes and shader headers.
    list(APPEND _cna_identity_defines "CNA_GL_PROFILE_${CNA_GRAPHICS_RENDERER}")
    # plans/plan_fx.md FX-062: compiled XNA effects on this renderer go through MojoShader's own OpenGL
    # adapter, which emits GLSL/GLSLES/GLSLES3 source text for whichever profile it is asked for --
    # entirely in parallel to EasyGL's own GLSL ES 3.00-authored-and-string-rewritten stock shaders.
    # Off by default because it pulls a fetched dependency into a renderer that does not otherwise
    # need one. Uses add_compile_definitions directly rather than _cna_identity_defines, matching
    # the sibling CNA_SDL_GPU_COMPILED_EFFECTS option below: an opt-in flag, not a per-identity
    # define every build of this renderer needs.
    option(CNA_EASYGL_COMPILED_EFFECTS
           "Build EasyGL support for compiled XNA Effect bytecode (plans/plan_fx.md FX-062)" OFF)
    if(CNA_EASYGL_COMPILED_EFFECTS)
        include(cmake/ThirdPartyFNA3D.cmake)
        cna_configure_mojoshader()
        add_compile_definitions(CNA_EASYGL_COMPILED_EFFECTS)
    endif()
elseif(CNA_GRAPHICS_RENDERER STREQUAL "BGFX")
    message(STATUS "CNA: Using BGFX graphics renderer")
    set(RENDERER_DIR "modules/renderers/bgfx")
    set(RENDERER_TARGET "cna_renderer_bgfx")
    list(APPEND _cna_identity_defines CNA_RENDERER_BGFX)
    set(CNA_RENDERER_DEFINE "CNA_RENDERER_BGFX")

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
        PATCH_COMMAND "${CMAKE_COMMAND}"
            "-DCNA_BGFX_PATCH_FILE=${CMAKE_CURRENT_SOURCE_DIR}/cmake/patches/bgfx-max-render-target-msaa.patch"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/patches/apply-bgfx-max-render-target-msaa-patch.cmake"
    )
    FetchContent_MakeAvailable(bgfx_cmake)

    set(CNA_BGFX_SHADER_INCLUDE_DIR "${bgfx_cmake_SOURCE_DIR}/bgfx/examples/common/imgui")
elseif(CNA_GRAPHICS_RENDERER STREQUAL "VULKAN")
    message(STATUS "CNA: Using VULKAN graphics renderer")
    set(RENDERER_DIR "modules/renderers/vulkan")
    set(RENDERER_TARGET "cna_renderer_vulkan")
    list(APPEND _cna_identity_defines CNA_RENDERER_VULKAN)
    set(CNA_RENDERER_DEFINE "CNA_RENDERER_VULKAN")
    find_package(Vulkan REQUIRED)
    # plans/plan_fx.md FX-065: compiled XNA Effect bytecode through MojoShader's portable SPIR-V profile.
    # Off by default and shaped exactly like the CNA_EASYGL_COMPILED_EFFECTS and
    # CNA_SDL_GPU_COMPILED_EFFECTS options above, for the same reason: MojoShader is a fetched
    # dependency this renderer does not otherwise need. Unlike those two there is no
    # MojoShader-provided adapter to link against (there is no `mojoshader_vulkan.c`) -- the
    # nine-function effect backend is CNA's own, written directly against MOJOSHADER_parse with
    # the SPIR-V profile, which FX-064's existence gate proved against a real device.
    option(CNA_VULKAN_COMPILED_EFFECTS
           "Build Vulkan support for compiled XNA Effect bytecode (plans/plan_fx.md FX-065)" OFF)
    if(CNA_VULKAN_COMPILED_EFFECTS)
        include(cmake/ThirdPartyFNA3D.cmake)
        cna_configure_mojoshader()
        add_compile_definitions(CNA_VULKAN_COMPILED_EFFECTS)
    endif()
elseif(CNA_GRAPHICS_RENDERER STREQUAL "WEBGPU")
    message(STATUS "CNA: Using WEBGPU graphics renderer")
    set(RENDERER_DIR "modules/renderers/webgpu")
    set(RENDERER_TARGET "cna_renderer_webgpu")
    list(APPEND _cna_identity_defines CNA_RENDERER_WEBGPU)
    set(CNA_RENDERER_DEFINE "CNA_RENDERER_WEBGPU")
    include(cmake/ThirdPartyWebGPU.cmake)
    cna_configure_webgpu()
elseif(CNA_GRAPHICS_RENDERER STREQUAL "HEADLESS")
    message(STATUS "CNA: Using HEADLESS (no GPU/window) graphics renderer")
    set(RENDERER_DIR "modules/renderers/headless")
    set(RENDERER_TARGET "cna_renderer_headless")
    list(APPEND _cna_identity_defines CNA_RENDERER_HEADLESS)
    set(CNA_RENDERER_DEFINE "CNA_RENDERER_HEADLESS")
elseif(CNA_GRAPHICS_RENDERER STREQUAL "SOFTWARE")
    message(STATUS "CNA: Using SOFTWARE (CPU rasterizer) graphics renderer")
    set(RENDERER_DIR "modules/renderers/software")
    set(RENDERER_TARGET "cna_renderer_software")
    list(APPEND _cna_identity_defines CNA_RENDERER_SOFTWARE)
    set(CNA_RENDERER_DEFINE "CNA_RENDERER_SOFTWARE")
elseif(CNA_GRAPHICS_RENDERER STREQUAL "STUB")
    message(STATUS "CNA: Using STUB (no-op) graphics renderer")
    set(RENDERER_DIR "modules/renderers/stub")
    set(RENDERER_TARGET "cna_renderer_stub")
    list(APPEND _cna_identity_defines CNA_RENDERER_STUB)
    set(CNA_RENDERER_DEFINE "CNA_RENDERER_STUB")
elseif(CNA_GRAPHICS_RENDERER STREQUAL "DIRECTX11")
    message(STATUS "CNA: Using DIRECTX11 graphics renderer")
    set(RENDERER_DIR "modules/renderers/directx11")
    set(RENDERER_TARGET "cna_renderer_directx11")
    list(APPEND _cna_identity_defines CNA_RENDERER_DIRECTX11)
    set(CNA_RENDERER_DEFINE "CNA_RENDERER_DIRECTX11")
elseif(CNA_GRAPHICS_RENDERER STREQUAL "DIRECTX12")
    message(STATUS "CNA: Using DIRECTX12 graphics renderer")
    set(RENDERER_DIR "modules/renderers/directx12")
    set(RENDERER_TARGET "cna_renderer_directx12")
    list(APPEND _cna_identity_defines CNA_RENDERER_DIRECTX12)
    set(CNA_RENDERER_DEFINE "CNA_RENDERER_DIRECTX12")
elseif(CNA_GRAPHICS_RENDERER STREQUAL "DIRECT2D")
    message(STATUS "CNA: Using DIRECT2D graphics renderer (Windows-only, 2D-only)")
    set(RENDERER_DIR "modules/renderers/direct2d")
    set(RENDERER_TARGET "cna_renderer_direct2d")
    list(APPEND _cna_identity_defines CNA_RENDERER_DIRECT2D)
    set(CNA_RENDERER_DEFINE "CNA_RENDERER_DIRECT2D")
elseif(CNA_GRAPHICS_RENDERER STREQUAL "CANVAS")
    message(STATUS "CNA: Using CANVAS (HTML Canvas 2D) graphics renderer")
    set(RENDERER_DIR "modules/renderers/canvas")
    set(RENDERER_TARGET "cna_renderer_canvas")
    list(APPEND _cna_identity_defines CNA_RENDERER_CANVAS)
    set(CNA_RENDERER_DEFINE "CNA_RENDERER_CANVAS")
elseif(CNA_GRAPHICS_RENDERER STREQUAL "HTML_DOM")
    message(STATUS "CNA: Using HTML_DOM (CSS-composited DOM elements) graphics renderer")
    set(RENDERER_DIR "modules/renderers/html-dom")
    set(RENDERER_TARGET "cna_renderer_html_dom")
    list(APPEND _cna_identity_defines CNA_RENDERER_HTML_DOM)
    set(CNA_RENDERER_DEFINE "CNA_RENDERER_HTML_DOM")
elseif(CNA_GRAPHICS_RENDERER STREQUAL "FREEDIRECT")
    message(STATUS "CNA: Using FreeDirect (DirectDraw via free-direct; formerly DIRECTX3) graphics renderer")
    set(RENDERER_DIR "modules/renderers/freedirect")
    set(RENDERER_TARGET "cna_renderer_freedirect")
    list(APPEND _cna_identity_defines CNA_RENDERER_FREEDIRECT)
    set(CNA_RENDERER_DEFINE "CNA_RENDERER_FREEDIRECT")
elseif(CNA_GRAPHICS_RENDERER STREQUAL "DIRECTX9")
    message(STATUS "CNA: Using DIRECTX9 graphics renderer")
    set(RENDERER_DIR "modules/renderers/directx9")
    set(RENDERER_TARGET "cna_renderer_directx9")
    list(APPEND _cna_identity_defines CNA_RENDERER_DIRECTX9)
    set(CNA_RENDERER_DEFINE "CNA_RENDERER_DIRECTX9")
elseif(CNA_GRAPHICS_RENDERER STREQUAL "DIRECTX1")
    message(STATUS "CNA: Using DIRECTX1 (real DirectDraw v1) graphics renderer")
    set(RENDERER_DIR "modules/renderers/directx1")
    set(RENDERER_TARGET "cna_renderer_directx1")
    list(APPEND _cna_identity_defines CNA_RENDERER_DIRECTX1)
    set(CNA_RENDERER_DEFINE "CNA_RENDERER_DIRECTX1")
elseif(CNA_GRAPHICS_RENDERER STREQUAL "DIRECTX2")
    message(STATUS "CNA: Using DIRECTX2 (real DirectDraw v1 + Direct3D v2 DrawPrimitive) graphics renderer")
    set(RENDERER_DIR "modules/renderers/directx2")
    set(RENDERER_TARGET "cna_renderer_directx2")
    list(APPEND _cna_identity_defines CNA_RENDERER_DIRECTX2)
    set(CNA_RENDERER_DEFINE "CNA_RENDERER_DIRECTX2")
elseif(CNA_GRAPHICS_RENDERER STREQUAL "DIRECTX3")
    message(STATUS "CNA: Using DIRECTX3 (real DirectX 3 -- DirectDraw v2 + Direct3D v2 DrawPrimitive) graphics renderer")
    set(RENDERER_DIR "modules/renderers/directx3")
    set(RENDERER_TARGET "cna_renderer_directx3")
    list(APPEND _cna_identity_defines CNA_RENDERER_DIRECTX3)
    set(CNA_RENDERER_DEFINE "CNA_RENDERER_DIRECTX3")
elseif(CNA_GRAPHICS_RENDERER STREQUAL "DIRECTX5")
    message(STATUS "CNA: Using DIRECTX5 (real DirectDraw v4 + Direct3D v3 FVF DrawPrimitive) graphics renderer")
    set(RENDERER_DIR "modules/renderers/directx5")
    set(RENDERER_TARGET "cna_renderer_directx5")
    list(APPEND _cna_identity_defines CNA_RENDERER_DIRECTX5)
    set(CNA_RENDERER_DEFINE "CNA_RENDERER_DIRECTX5")
elseif(CNA_GRAPHICS_RENDERER STREQUAL "DIRECTX6")
    message(STATUS "CNA: Using DIRECTX6 (real DirectDraw v4 + Direct3D v3, real stencil) graphics renderer")
    set(RENDERER_DIR "modules/renderers/directx6")
    set(RENDERER_TARGET "cna_renderer_directx6")
    list(APPEND _cna_identity_defines CNA_RENDERER_DIRECTX6)
    set(CNA_RENDERER_DEFINE "CNA_RENDERER_DIRECTX6")
elseif(CNA_GRAPHICS_RENDERER STREQUAL "DIRECTX7")
    message(STATUS "CNA: Using DIRECTX7 (real DirectDraw v7 + Direct3D v7, flattened device model) graphics renderer")
    set(RENDERER_DIR "modules/renderers/directx7")
    set(RENDERER_TARGET "cna_renderer_directx7")
    list(APPEND _cna_identity_defines CNA_RENDERER_DIRECTX7)
    set(CNA_RENDERER_DEFINE "CNA_RENDERER_DIRECTX7")
elseif(CNA_GRAPHICS_RENDERER STREQUAL "DIRECTX8")
    message(STATUS "CNA: Using DIRECTX8 (real Direct3D 8, DXVK-delivered, fixed-function) graphics renderer")
    set(RENDERER_DIR "modules/renderers/directx8")
    set(RENDERER_TARGET "cna_renderer_directx8")
    list(APPEND _cna_identity_defines CNA_RENDERER_DIRECTX8)
    set(CNA_RENDERER_DEFINE "CNA_RENDERER_DIRECTX8")
elseif(CNA_GRAPHICS_RENDERER STREQUAL "DIRECTX10")
    message(STATUS "CNA: Using DIRECTX10 (real Direct3D 10, DXVK-delivered via d3d10core, real HLSL shaders) graphics renderer")
    set(RENDERER_DIR "modules/renderers/directx10")
    set(RENDERER_TARGET "cna_renderer_directx10")
    list(APPEND _cna_identity_defines CNA_RENDERER_DIRECTX10)
    set(CNA_RENDERER_DEFINE "CNA_RENDERER_DIRECTX10")
elseif(CNA_GRAPHICS_RENDERER STREQUAL "METAL")
    enable_language(OBJCXX)
    message(STATUS "CNA: Using native METAL graphics renderer")
    set(RENDERER_DIR "modules/renderers/metal")
    set(RENDERER_TARGET "cna_renderer_metal")
    list(APPEND _cna_identity_defines CNA_RENDERER_METAL)
    set(CNA_RENDERER_DEFINE "CNA_RENDERER_METAL")
elseif(CNA_GRAPHICS_RENDERER STREQUAL "SDL_GPU")
    message(STATUS "CNA: Using SDL_GPU graphics renderer")
    set(RENDERER_DIR "modules/renderers/sdl-gpu")
    set(RENDERER_TARGET "cna_renderer_sdl_gpu")
    list(APPEND _cna_identity_defines CNA_RENDERER_SDL_GPU)
    set(CNA_RENDERER_DEFINE "CNA_RENDERER_SDL_GPU")
    # plans/plan_fx.md FX-061: compiled XNA effects on this renderer go through MojoShader's own SDL_GPU
    # adapter, which emits SPIR-V -- the format this renderer already builds its pipelines from.
    # Off by default because it pulls a fetched dependency into a renderer that does not otherwise
    # need one; the capability stays false until the FX-060 shared suite passes here.
    option(CNA_SDL_GPU_COMPILED_EFFECTS
           "Build SDL_GPU support for compiled XNA Effect bytecode (plans/plan_fx.md FX-061)" OFF)
    if(CNA_SDL_GPU_COMPILED_EFFECTS)
        include(cmake/ThirdPartyFNA3D.cmake)
        cna_configure_mojoshader()
        add_compile_definitions(CNA_SDL_GPU_COMPILED_EFFECTS)
    endif()
elseif(CNA_GRAPHICS_RENDERER STREQUAL "OPENGLES1")
    message(STATUS "CNA: Using OPENGLES1 (fixed-function OpenGL ES 1.1) graphics renderer")
    set(RENDERER_DIR "modules/renderers/opengles1")
    set(RENDERER_TARGET "cna_renderer_opengles1")
    list(APPEND _cna_identity_defines CNA_RENDERER_OPENGLES1)
    set(CNA_RENDERER_DEFINE "CNA_RENDERER_OPENGLES1")
elseif(CNA_GRAPHICS_RENDERER STREQUAL "OPENGL4")
    message(STATUS "CNA: Using OPENGL4 (real desktop OpenGL 4.x core profile) graphics renderer")
    set(RENDERER_DIR "modules/renderers/opengl4")
    set(RENDERER_TARGET "cna_renderer_opengl4")
    list(APPEND _cna_identity_defines CNA_RENDERER_OPENGL4)
    set(CNA_RENDERER_DEFINE "CNA_RENDERER_OPENGL4")
    find_package(OpenGL REQUIRED)
elseif(CNA_GRAPHICS_RENDERER STREQUAL "OPENGL2")
    message(STATUS "CNA: Using native OPENGL2 graphics renderer (no EasyGL)")
    set(RENDERER_DIR "modules/renderers/opengl2")
    set(RENDERER_TARGET "cna_renderer_opengl2")
    list(APPEND _cna_identity_defines CNA_RENDERER_OPENGL2)
    set(CNA_RENDERER_DEFINE "CNA_RENDERER_OPENGL2")
elseif(CNA_GRAPHICS_RENDERER STREQUAL "GLIDE")
    message(STATUS "CNA: Using GLIDE 3.x graphics renderer (runtime glide3x.dll required)")
    set(RENDERER_DIR "modules/renderers/glide")
    set(RENDERER_TARGET "cna_renderer_glide")
    list(APPEND _cna_identity_defines CNA_RENDERER_GLIDE)
    set(CNA_RENDERER_DEFINE "CNA_RENDERER_GLIDE")
elseif(CNA_GRAPHICS_RENDERER STREQUAL "GDI")
    message(STATUS "CNA: Using classic Win32 GDI 2D graphics renderer")
    set(RENDERER_DIR "modules/renderers/gdi")
    set(RENDERER_TARGET "cna_renderer_gdi")
    list(APPEND _cna_identity_defines CNA_RENDERER_GDI)
    set(CNA_RENDERER_DEFINE "CNA_RENDERER_GDI")
elseif(CNA_GRAPHICS_RENDERER STREQUAL "FNA3D")
    message(STATUS "CNA: Using FNA3D graphics renderer (FNA-XNA/FNA3D + MojoShader)")
    set(RENDERER_DIR "modules/renderers/fna3d")
    set(RENDERER_TARGET "cna_renderer_fna3d")
    list(APPEND _cna_identity_defines CNA_RENDERER_FNA3D)
    set(CNA_RENDERER_DEFINE "CNA_RENDERER_FNA3D")
    include(cmake/ThirdPartyFNA3D.cmake)
    cna_configure_fna3d()
elseif(CNA_GRAPHICS_RENDERER STREQUAL "SVG_DOM")
    message(STATUS "CNA: Using SVG_DOM (real SVG DOM elements) graphics renderer")
    set(RENDERER_DIR "modules/renderers/svg-dom")
    set(RENDERER_TARGET "cna_renderer_svg_dom")
    list(APPEND _cna_identity_defines CNA_RENDERER_SVG_DOM)
    set(CNA_RENDERER_DEFINE "CNA_RENDERER_SVG_DOM")
elseif(CNA_GRAPHICS_RENDERER STREQUAL "OPENVG")
    message(STATUS "CNA: Using OPENVG (ShivaVG) 2D vector graphics renderer")
    set(RENDERER_DIR "modules/renderers/openvg")
    set(RENDERER_TARGET "cna_renderer_openvg")
    list(APPEND _cna_identity_defines CNA_RENDERER_OPENVG)
    set(CNA_RENDERER_DEFINE "CNA_RENDERER_OPENVG")
    include(cmake/ThirdPartyOpenVG.cmake)
    cna_configure_openvg()
elseif(CNA_GRAPHICS_RENDERER STREQUAL "PORTABLEGL")
    message(STATUS "CNA: Using PORTABLEGL (rswinkle/PortableGL, CPU software OpenGL 3.x) graphics renderer")
    set(RENDERER_DIR "modules/renderers/portablegl")
    set(RENDERER_TARGET "cna_renderer_portablegl")
    list(APPEND _cna_identity_defines CNA_RENDERER_PORTABLEGL)
    set(CNA_RENDERER_DEFINE "CNA_RENDERER_PORTABLEGL")
    include(cmake/ThirdPartyPortableGL.cmake)
    cna_configure_portablegl()
elseif(CNA_GRAPHICS_RENDERER STREQUAL "TINYGL")
    message(STATUS "CNA: Using TINYGL (C-Chads/tinygl, CPU fixed-function OpenGL 1.x) graphics renderer")
    set(RENDERER_DIR "modules/renderers/tinygl")
    set(RENDERER_TARGET "cna_renderer_tinygl")
    # plans/plan_modern.md MOD-134: this arm used to call add_compile_definitions() directly and never
    # append to _cna_identity_defines, which is the list the caller turns into this identity's entry
    # in CNA_RENDERER_TARGET_DEFINES. An empty entry makes that list empty rather than one element
    # long, and modules/renderers/CMakeLists.txt then failed with "list GET given empty list" --
    # TINYGL did not configure at all. Every other one of the 45 dispatch arms already did this.
    list(APPEND _cna_identity_defines CNA_RENDERER_TINYGL)
    set(CNA_RENDERER_DEFINE "CNA_RENDERER_TINYGL")
    include(cmake/ThirdPartyTinyGL.cmake)
    cna_configure_tinygl()
elseif(CNA_GRAPHICS_RENDERER STREQUAL "PIXIJS")
    message(STATUS "CNA: Using PIXIJS (pixijs.com WebGL scene graph) graphics renderer")
    set(RENDERER_DIR "modules/renderers/pixijs")
    set(RENDERER_TARGET "cna_renderer_pixijs")
    list(APPEND _cna_identity_defines CNA_RENDERER_PIXIJS)
    set(CNA_RENDERER_DEFINE "CNA_RENDERER_PIXIJS")
    # plans/plan_pixijs.md Design decision 4: vendor a pinned PixiJS UMD build and prepend it into the
    # generated glue code via --pre-js, so the global PIXI.* namespace exists before any of this
    # renderer's own EM_JS functions run.
    include(cmake/ThirdPartyPixiJS.cmake)
    cna_configure_pixijs()
else()

    message(FATAL_ERROR "CNA: Unknown graphics renderer: ${CNA_GRAPHICS_RENDERER}")
endif()
endmacro()

foreach(_cna_identity IN LISTS _cna_renderer_identities)
    set(CNA_GRAPHICS_RENDERER "${_cna_identity}")
    cna_configure_renderer_identity()
    list(APPEND CNA_RENDERER_TARGETS "${RENDERER_TARGET}")
    list(APPEND CNA_RENDERER_DIRS "${RENDERER_DIR}")

    # Each family's own sources guard on its identity macro, so every family gets its define on its
    # own target (applied in cna_renderer_common_setup once the target exists).
    string(REPLACE ";" "," _cna_joined_defines "${_cna_identity_defines}")
    list(APPEND CNA_RENDERER_TARGET_DEFINES "${_cna_joined_defines}")
    list(APPEND CNA_RENDERER_DEFINES "${CNA_RENDERER_DEFINE}")

    # Only the DEFAULT identity's macros are defined project-wide. That keeps a single-renderer
    # build exactly as it was, and keeps the compile-time accessors and the existing 892 test and
    # example #ifdef sites meaningful in a multi-renderer build: they describe the default. Making
    # the corpus itself renderer-agnostic is plans/plan_runtimerenderer.md phase P9.
    if(_cna_identity STREQUAL _cna_default_renderer_identity)
        foreach(_cna_define IN LISTS _cna_identity_defines)
            add_compile_definitions(${_cna_define})
        endforeach()
    endif()
endforeach()

# Restore the default identity, and leave RENDERER_TARGET/RENDERER_DIR pointing at it. Those two
# scalars are still read in ~128 places; in single-renderer mode they mean exactly what they always
# did, and in multi-renderer mode they mean "the default renderer".
set(CNA_GRAPHICS_RENDERER "${_cna_default_renderer_identity}")
list(GET CNA_RENDERER_TARGETS 0 RENDERER_TARGET)
list(GET CNA_RENDERER_DIRS 0 RENDERER_DIR)
# CNA_RENDERER_DEFINE rides cna_build_config INTERFACE, so it reaches EVERY module and every
# consumer. After the loop it would otherwise hold the LAST identity's macro rather than the
# default's -- which in a HEADLESS;SOFTWARE;STUB build meant the whole project compiled as though
# STUB were selected. It names the default renderer, like the two scalars above.
list(GET CNA_RENDERER_DEFINES 0 CNA_RENDERER_DEFINE)
