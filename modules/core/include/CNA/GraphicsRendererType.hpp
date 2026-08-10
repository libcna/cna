#pragma once

#include <string_view>

namespace CNA
{
    /** @brief Identifies which CNA graphics renderer was selected for this build (compile-time choice, see CNA_GRAPHICS_RENDERER). */
    enum class GraphicsRendererType
    {
        /** @brief SDL_Renderer (2D-only). */
        SdlRenderer,

        /** @brief OpenGL ES (desktop/mobile GLES 3.0), internally implemented by EasyGL. */
        OpenGLES,

        /** @brief Desktop OpenGL 3.3 core profile, internally implemented by EasyGL. */
        OpenGL33,

        /** @brief WebGL 1 (Emscripten only, GLES 2.0), internally implemented by EasyGL. */
        WebGL1,

        /** @brief WebGL 2 (Emscripten only, GLES 3.0), internally implemented by EasyGL. */
        WebGL2,

        /** @brief Bgfx. */
        Bgfx,

        /** @brief Vulkan. */
        Vulkan,

        /** @brief WebGPU (experimental). */
        WebGPU,

        /** @brief Magnum (mosra/magnum, desktop OpenGL). */
        Magnum,

        /** @brief Headless (no GPU/window). */
        Headless,

        /** @brief Software (CPU rasterizer). */
        Software,

        /** @brief Stub (no-op, renders nothing). */
        Stub,

        /** @brief Direct3D 11. */
        D3D11,

        /** @brief Direct3D 12. */
        D3D12,

        /** @brief Direct2D 1.1 (Windows, 2D-only). */
        Direct2D,

        /** @brief HTML Canvas 2D (Emscripten). */
        Canvas,

        /** @brief HTML DOM elements composited by CSS (Emscripten). */
        HtmlDom,

        /** @brief Skia 2D raster renderer. */
        Skia,

        /** @brief ASCII (SDL-windowed glyph grid). */
        Ascii,

        /** @brief FreeDirect (DirectDraw via the ../free-direct sibling reimplementation; formerly DX3). */
        FreeDirect,

        /** @brief Direct3D 9. */
        D3D9,

        /** @brief DX1 (real DirectDraw v1, no ../free-direct). */
        Dx1,

        /** @brief DX2 (real DirectDraw v1 + Direct3D v2 DrawPrimitive, no execute buffers). */
        Dx2,

        /** @brief DX3 (real DirectX 3 -- DirectDraw v2 + Direct3D v2 DrawPrimitive; originally
         * landed as DX30 while the free-direct renderer still owned the DX3 name, renamed once
         * that renderer became FreeDirect). */
        Dx3,

        /** @brief DX5 (real DirectDraw v4 + Direct3D v3 FVF DrawPrimitive, no execute buffers). */
        Dx5,

        /** @brief DX6 (real DirectDraw v4 + Direct3D v3, real stencil buffer operations). */
        Dx6,

        /** @brief DX7 (real DirectDraw v7 + Direct3D v7, flattened device model -- no viewport
         * object, direct texture binding). */
        Dx7,

        /** @brief DX8 (real Direct3D 8, DXVK-delivered, fixed-function only -- no DirectDraw). */
        Dx8,

        /** @brief Direct3D 10 (real ID3D10Device, DXVK-delivered via d3d10core, real HLSL shaders
         * -- no fixed-function pipeline at all, unlike DX1..DX8). */
        D3D10,

        /** @brief SDL_GPU. */
        SdlGpu,

        /** @brief OpenGL ES 1.1 (fixed-function). */
        OpenGLES1,
        /** @brief Real desktop OpenGL 4.x core profile. */
        OpenGL4,
        /** @brief Legacy desktop OpenGL 1.x fixed-function pipeline. */
        OpenGL1,
        /** @brief Native desktop OpenGL 2.1 (no EasyGL). */
        OpenGL2,

        /** @brief Wicked Engine (wi::graphics RHI). */
        Wicked,

        /** @brief sokol_gfx (experimental). */
        Sokol,

        /** @brief Diligent Engine (experimental; picks its own native API at runtime). */
        Diligent,

        /** @brief 3dfx Glide 3.x, dynamically loaded from glide3x.dll. */
        Glide,

        /** @brief Classic Win32 GDI with private CPU 2D rasterization. */
        Gdi,

        /** @brief LLGL rendering abstraction; CNA's supported runtime uses its OpenGL module. */
        Llgl,

        /** @brief Native Apple Metal. */
        Metal
    };

    /**
     * @brief Returns the graphics renderer compiled into this build.
     *
     * Resolved entirely from the CNA_RENDERER_* compile definition cmake/RendererSelection.cmake
     * sets per renderer, so this is a compile-time constant -- usable in a constant expression
     * (e.g. static_assert(CNA::getCurrentGraphicsRendererType() == CNA::GraphicsRendererType::OpenGLES)).
     *
     * The 4 GL-family public renderers (OPENGLES/OPENGL33/WEBGL1/WEBGL2) all share the internal
     * CNA_RENDERER_EASYGL identity (see plan_glbackends.md) -- the CNA_GL_PROFILE_* compile
     * definition set alongside it distinguishes which of the 4 public names was selected.
     *
     * @return The active GraphicsRendererType, determined at compile time by CNA_GRAPHICS_RENDERER.
     */
    constexpr GraphicsRendererType getCurrentGraphicsRendererType()
    {
#if defined(CNA_RENDERER_SDL_RENDERER)
        return GraphicsRendererType::SdlRenderer;
#elif defined(CNA_RENDERER_EASYGL)
#if defined(CNA_GL_PROFILE_OPENGL33)
        return GraphicsRendererType::OpenGL33;
#elif defined(CNA_GL_PROFILE_WEBGL1)
        return GraphicsRendererType::WebGL1;
#elif defined(CNA_GL_PROFILE_WEBGL2)
        return GraphicsRendererType::WebGL2;
#else // CNA_GL_PROFILE_OPENGLES (default within CNA_RENDERER_EASYGL)
        return GraphicsRendererType::OpenGLES;
#endif
#elif defined(CNA_RENDERER_BGFX)
        return GraphicsRendererType::Bgfx;
#elif defined(CNA_RENDERER_VULKAN)
        return GraphicsRendererType::Vulkan;
#elif defined(CNA_RENDERER_WEBGPU)
        return GraphicsRendererType::WebGPU;
#elif defined(CNA_RENDERER_MAGNUM)
        return GraphicsRendererType::Magnum;
#elif defined(CNA_RENDERER_HEADLESS)
        return GraphicsRendererType::Headless;
#elif defined(CNA_RENDERER_SOFTWARE)
        return GraphicsRendererType::Software;
#elif defined(CNA_RENDERER_STUB)
        return GraphicsRendererType::Stub;
#elif defined(CNA_RENDERER_D3D11)
        return GraphicsRendererType::D3D11;
#elif defined(CNA_RENDERER_D3D12)
        return GraphicsRendererType::D3D12;
#elif defined(CNA_RENDERER_DIRECT2D)
        return GraphicsRendererType::Direct2D;
#elif defined(CNA_RENDERER_CANVAS)
        return GraphicsRendererType::Canvas;
#elif defined(CNA_RENDERER_HTML_DOM)
        return GraphicsRendererType::HtmlDom;
#elif defined(CNA_RENDERER_SKIA)
        return GraphicsRendererType::Skia;
#elif defined(CNA_RENDERER_ASCII)
        return GraphicsRendererType::Ascii;
#elif defined(CNA_RENDERER_FREEDIRECT)
        return GraphicsRendererType::FreeDirect;
#elif defined(CNA_RENDERER_D3D9)
        return GraphicsRendererType::D3D9;
#elif defined(CNA_RENDERER_DX1)
        return GraphicsRendererType::Dx1;
#elif defined(CNA_RENDERER_DX2)
        return GraphicsRendererType::Dx2;
#elif defined(CNA_RENDERER_DX3)
        return GraphicsRendererType::Dx3;
#elif defined(CNA_RENDERER_DX5)
        return GraphicsRendererType::Dx5;
#elif defined(CNA_RENDERER_DX6)
        return GraphicsRendererType::Dx6;
#elif defined(CNA_RENDERER_DX7)
        return GraphicsRendererType::Dx7;
#elif defined(CNA_RENDERER_DX8)
        return GraphicsRendererType::Dx8;
#elif defined(CNA_RENDERER_D3D10)
        return GraphicsRendererType::D3D10;
#elif defined(CNA_RENDERER_SDL_GPU)
        return GraphicsRendererType::SdlGpu;
#elif defined(CNA_RENDERER_OPENGLES1)
        return GraphicsRendererType::OpenGLES1;
#elif defined(CNA_RENDERER_OPENGL4)
        return GraphicsRendererType::OpenGL4;
#elif defined(CNA_RENDERER_OPENGL1)
        return GraphicsRendererType::OpenGL1;
#elif defined(CNA_RENDERER_OPENGL2)
        return GraphicsRendererType::OpenGL2;
#elif defined(CNA_RENDERER_WICKED)
        return GraphicsRendererType::Wicked;
#elif defined(CNA_RENDERER_SOKOL)
        return GraphicsRendererType::Sokol;
#elif defined(CNA_RENDERER_DILIGENT)
        return GraphicsRendererType::Diligent;
#elif defined(CNA_RENDERER_GLIDE)
        return GraphicsRendererType::Glide;
#elif defined(CNA_RENDERER_GDI)
        return GraphicsRendererType::Gdi;
#elif defined(CNA_RENDERER_LLGL)
        return GraphicsRendererType::Llgl;
#elif defined(CNA_RENDERER_METAL)
        return GraphicsRendererType::Metal;
#else
#error "CNA: no CNA_RENDERER_* compile definition set -- graphics renderer selection (cmake/RendererSelection.cmake) is broken"
#endif
    }

    /**
     * @brief Returns the human-readable name of the graphics renderer compiled into this build.
     *
     * The returned view matches the CNA_GRAPHICS_RENDERER CMake option value exactly
     * (e.g. "OPENGLES", "SDL_RENDERER", "D3D9") and points at static storage (a string literal),
     * so it stays valid for the lifetime of the program. Like getCurrentGraphicsRendererType(),
     * this is a compile-time constant.
     *
     * @return The active renderer's name.
     */
    constexpr std::string_view getCurrentGraphicsRendererName()
    {
        switch (getCurrentGraphicsRendererType())
        {
            case GraphicsRendererType::SdlRenderer: return "SDL_RENDERER";
            case GraphicsRendererType::OpenGLES:    return "OPENGLES";
            case GraphicsRendererType::OpenGL33:    return "OPENGL33";
            case GraphicsRendererType::WebGL1:       return "WEBGL1";
            case GraphicsRendererType::WebGL2:       return "WEBGL2";
            case GraphicsRendererType::Bgfx:         return "BGFX";
            case GraphicsRendererType::Vulkan:       return "VULKAN";
            case GraphicsRendererType::WebGPU:       return "WEBGPU";
            case GraphicsRendererType::Magnum:       return "MAGNUM";
            case GraphicsRendererType::Headless:     return "HEADLESS";
            case GraphicsRendererType::Software:     return "SOFTWARE";
            case GraphicsRendererType::Stub:          return "STUB";
            case GraphicsRendererType::D3D11:        return "D3D11";
            case GraphicsRendererType::D3D12:        return "D3D12";
            case GraphicsRendererType::Direct2D:     return "DIRECT2D";
            case GraphicsRendererType::Canvas:       return "CANVAS";
            case GraphicsRendererType::HtmlDom:      return "HTML_DOM";
            case GraphicsRendererType::Skia:         return "SKIA";
            case GraphicsRendererType::Ascii:        return "ASCII";
            case GraphicsRendererType::FreeDirect:           return "FREEDIRECT";
            case GraphicsRendererType::D3D9:          return "D3D9";
            case GraphicsRendererType::Dx1:            return "DX1";
            case GraphicsRendererType::Dx2:            return "DX2";
            case GraphicsRendererType::Dx3:           return "DX3";
            case GraphicsRendererType::Dx5:            return "DX5";
            case GraphicsRendererType::Dx6:            return "DX6";
            case GraphicsRendererType::Dx7:            return "DX7";
            case GraphicsRendererType::Dx8:            return "DX8";
            case GraphicsRendererType::D3D10:          return "D3D10";
            case GraphicsRendererType::SdlGpu:        return "SDL_GPU";
            case GraphicsRendererType::OpenGLES1:     return "OPENGLES1";
            case GraphicsRendererType::OpenGL4:       return "OPENGL4";
            case GraphicsRendererType::OpenGL1:       return "OPENGL1";
            case GraphicsRendererType::OpenGL2:       return "OPENGL2";
            case GraphicsRendererType::Wicked:        return "WICKED";
            case GraphicsRendererType::Sokol:         return "SOKOL";
            case GraphicsRendererType::Diligent:      return "DILIGENT";
            case GraphicsRendererType::Glide:         return "GLIDE";
            case GraphicsRendererType::Gdi:           return "GDI";
            case GraphicsRendererType::Llgl:          return "LLGL";
            case GraphicsRendererType::Metal:          return "METAL";
        }
        return "UNKNOWN";
    }
} // CNA
