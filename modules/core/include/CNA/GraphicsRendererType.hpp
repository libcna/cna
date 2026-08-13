#pragma once

#include <string_view>

namespace CNA
{
    /** @brief Identifies which CNA graphics renderer was selected for this build (compile-time choice, see CNA_GRAPHICS_RENDERER). */
    enum class GraphicsRendererType
    {
        /** @brief SDL_Renderer (2D-only). */
        SdlRenderer,

        /** @brief OpenGL ES 2.0 (desktop/mobile, GLSL ES 1.00), internally implemented by EasyGL. */
        OpenGLES2,

        /** @brief OpenGL ES (desktop/mobile GLES 3.0), internally implemented by EasyGL. */
        OpenGLES3,

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
        DirectX11,

        /** @brief Direct3D 12. */
        DirectX12,

        /** @brief Direct2D 1.1 (Windows, 2D-only). */
        Direct2D,

        /** @brief HTML Canvas 2D (Emscripten). */
        Canvas,

        /** @brief HTML DOM elements composited by CSS (Emscripten). */
        HtmlDom,

        /** @brief Skia 2D raster renderer. */
        Skia,

        /** @brief Blend2D 2D vector raster renderer. */
        Blend2D,
        /** @brief FreeDirect (DirectDraw via the ../free-direct sibling reimplementation; formerly DIRECTX3). */
        FreeDirect,

        /** @brief Direct3D 9. */
        DirectX9,

        /** @brief DIRECTX1 (real DirectDraw v1, no ../free-direct). */
        DirectX1,

        /** @brief DIRECTX2 (real DirectDraw v1 + Direct3D v2 DrawPrimitive, no execute buffers). */
        DirectX2,

        /** @brief DIRECTX3 (real DirectX 3 -- DirectDraw v2 + Direct3D v2 DrawPrimitive; originally
         * landed as DX30 while the free-direct renderer still owned the DIRECTX3 name, renamed once
         * that renderer became FreeDirect). */
        DirectX3,

        /** @brief DIRECTX5 (real DirectDraw v4 + Direct3D v3 FVF DrawPrimitive, no execute buffers). */
        DirectX5,

        /** @brief DIRECTX6 (real DirectDraw v4 + Direct3D v3, real stencil buffer operations). */
        DirectX6,

        /** @brief DIRECTX7 (real DirectDraw v7 + Direct3D v7, flattened device model -- no viewport
         * object, direct texture binding). */
        DirectX7,

        /** @brief DIRECTX8 (real Direct3D 8, DXVK-delivered, fixed-function only -- no DirectDraw). */
        DirectX8,

        /** @brief Direct3D 10 (real ID3D10Device, DXVK-delivered via d3d10core, real HLSL shaders
         * -- no fixed-function pipeline at all, unlike DIRECTX1..DIRECTX8). */
        DirectX10,

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
        Metal,

        /** @brief FNA3D (FNA-XNA/FNA3D), the XNA-shaped C graphics library FNA itself renders
         * through; it selects SDL_GPU, Direct3D 11 or OpenGL internally at runtime. */
        Fna3d,

        /** @brief SVG DOM (Emscripten only, 2D-only): SpriteBatch output as real SVG elements. */
        SvgDom,

        /** @brief OpenVG 1.1 vector graphics (2D-only), implemented by ShivaVG on top of desktop OpenGL. */
        OpenVg,

        /** @brief PortableGL (rswinkle/PortableGL, CPU software OpenGL 3.x). */
        PortableGL,

        /** @brief TinyGL (C-Chads/tinygl, CPU fixed-function OpenGL 1.x subset). */
        TinyGL
    };

    /**
     * @brief Returns the graphics renderer compiled into this build.
     *
     * Resolved entirely from the CNA_RENDERER_* compile definition cmake/RendererSelection.cmake
     * sets per renderer, so this is a compile-time constant -- usable in a constant expression
     * (e.g. static_assert(CNA::getCurrentGraphicsRendererType() == CNA::GraphicsRendererType::OpenGLES3)).
     *
     * The 5 GL-family public renderers (OPENGLES2/OPENGLES3/OPENGL33/WEBGL1/WEBGL2) all share the
     * internal CNA_RENDERER_EASYGL identity (see plan_glbackends.md) -- the CNA_GL_PROFILE_*
     * compile definition set alongside it distinguishes which of the 5 public names was selected.
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
#elif defined(CNA_GL_PROFILE_OPENGLES2)
        return GraphicsRendererType::OpenGLES2;
#else // CNA_GL_PROFILE_OPENGLES3 (default within CNA_RENDERER_EASYGL)
        return GraphicsRendererType::OpenGLES3;
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
#elif defined(CNA_RENDERER_DIRECTX11)
        return GraphicsRendererType::DirectX11;
#elif defined(CNA_RENDERER_DIRECTX12)
        return GraphicsRendererType::DirectX12;
#elif defined(CNA_RENDERER_DIRECT2D)
        return GraphicsRendererType::Direct2D;
#elif defined(CNA_RENDERER_CANVAS)
        return GraphicsRendererType::Canvas;
#elif defined(CNA_RENDERER_HTML_DOM)
        return GraphicsRendererType::HtmlDom;
#elif defined(CNA_RENDERER_SKIA)
        return GraphicsRendererType::Skia;
#elif defined(CNA_RENDERER_BLEND2D)
        return GraphicsRendererType::Blend2D;
#elif defined(CNA_RENDERER_FREEDIRECT)
        return GraphicsRendererType::FreeDirect;
#elif defined(CNA_RENDERER_DIRECTX9)
        return GraphicsRendererType::DirectX9;
#elif defined(CNA_RENDERER_DIRECTX1)
        return GraphicsRendererType::DirectX1;
#elif defined(CNA_RENDERER_DIRECTX2)
        return GraphicsRendererType::DirectX2;
#elif defined(CNA_RENDERER_DIRECTX3)
        return GraphicsRendererType::DirectX3;
#elif defined(CNA_RENDERER_DIRECTX5)
        return GraphicsRendererType::DirectX5;
#elif defined(CNA_RENDERER_DIRECTX6)
        return GraphicsRendererType::DirectX6;
#elif defined(CNA_RENDERER_DIRECTX7)
        return GraphicsRendererType::DirectX7;
#elif defined(CNA_RENDERER_DIRECTX8)
        return GraphicsRendererType::DirectX8;
#elif defined(CNA_RENDERER_DIRECTX10)
        return GraphicsRendererType::DirectX10;
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
#elif defined(CNA_RENDERER_FNA3D)
        return GraphicsRendererType::Fna3d;
#elif defined(CNA_RENDERER_SVG_DOM)
        return GraphicsRendererType::SvgDom;
#elif defined(CNA_RENDERER_OPENVG)
        return GraphicsRendererType::OpenVg;
#elif defined(CNA_RENDERER_PORTABLEGL)
        return GraphicsRendererType::PortableGL;
#elif defined(CNA_RENDERER_TINYGL)
        return GraphicsRendererType::TinyGL;
#else
#error "CNA: no CNA_RENDERER_* compile definition set -- graphics renderer selection (cmake/RendererSelection.cmake) is broken"
#endif
    }

    /**
     * @brief Returns the human-readable name of the graphics renderer compiled into this build.
     *
     * The returned view matches the CNA_GRAPHICS_RENDERER CMake option value exactly
     * (e.g. "OPENGLES3", "SDL_RENDERER", "DIRECTX9") and points at static storage (a string literal),
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
            case GraphicsRendererType::OpenGLES2:    return "OPENGLES2";
            case GraphicsRendererType::OpenGLES3:    return "OPENGLES3";
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
            case GraphicsRendererType::DirectX11:        return "DIRECTX11";
            case GraphicsRendererType::DirectX12:        return "DIRECTX12";
            case GraphicsRendererType::Direct2D:     return "DIRECT2D";
            case GraphicsRendererType::Canvas:       return "CANVAS";
            case GraphicsRendererType::HtmlDom:      return "HTML_DOM";
            case GraphicsRendererType::Skia:         return "SKIA";
            case GraphicsRendererType::Blend2D:      return "BLEND2D";
            case GraphicsRendererType::FreeDirect:           return "FREEDIRECT";
            case GraphicsRendererType::DirectX9:          return "DIRECTX9";
            case GraphicsRendererType::DirectX1:            return "DIRECTX1";
            case GraphicsRendererType::DirectX2:            return "DIRECTX2";
            case GraphicsRendererType::DirectX3:           return "DIRECTX3";
            case GraphicsRendererType::DirectX5:            return "DIRECTX5";
            case GraphicsRendererType::DirectX6:            return "DIRECTX6";
            case GraphicsRendererType::DirectX7:            return "DIRECTX7";
            case GraphicsRendererType::DirectX8:            return "DIRECTX8";
            case GraphicsRendererType::DirectX10:          return "DIRECTX10";
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
            case GraphicsRendererType::Fna3d:         return "FNA3D";
            case GraphicsRendererType::SvgDom:         return "SVG_DOM";
            case GraphicsRendererType::OpenVg:         return "OPENVG";
            case GraphicsRendererType::PortableGL:    return "PORTABLEGL";
            case GraphicsRendererType::TinyGL:        return "TINYGL";
        }
        return "UNKNOWN";
    }
} // CNA
