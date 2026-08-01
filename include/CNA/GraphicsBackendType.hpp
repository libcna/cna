#pragma once

#include <string_view>

namespace CNA
{
    /** @brief Identifies which CNA graphics backend was selected for this build (compile-time choice, see CNA_GRAPHICS_BACKEND). */
    enum class GraphicsBackendType
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

        /** @brief HTML Canvas 2D (Emscripten). */
        Canvas,

        /** @brief Skia 2D raster backend. */
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
         * landed as DX30 while the free-direct backend still owned the DX3 name, renamed once
         * that backend became FreeDirect). */
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
        Gdi
    };

    /**
     * @brief Returns the graphics backend compiled into this build.
     *
     * Resolved entirely from the CNA_BACKEND_* compile definition cmake/BackendSelection.cmake
     * sets per backend, so this is a compile-time constant -- usable in a constant expression
     * (e.g. static_assert(CNA::getCurrentGraphicsBackendType() == CNA::GraphicsBackendType::OpenGLES)).
     *
     * The 4 GL-family public backends (OPENGLES/OPENGL33/WEBGL1/WEBGL2) all share the internal
     * CNA_BACKEND_EASYGL identity (see plan_glbackends.md) -- the CNA_GL_PROFILE_* compile
     * definition set alongside it distinguishes which of the 4 public names was selected.
     *
     * @return The active GraphicsBackendType, determined at compile time by CNA_GRAPHICS_BACKEND.
     */
    constexpr GraphicsBackendType getCurrentGraphicsBackendType()
    {
#if defined(CNA_BACKEND_SDL_RENDERER)
        return GraphicsBackendType::SdlRenderer;
#elif defined(CNA_BACKEND_EASYGL)
#if defined(CNA_GL_PROFILE_OPENGL33)
        return GraphicsBackendType::OpenGL33;
#elif defined(CNA_GL_PROFILE_WEBGL1)
        return GraphicsBackendType::WebGL1;
#elif defined(CNA_GL_PROFILE_WEBGL2)
        return GraphicsBackendType::WebGL2;
#else // CNA_GL_PROFILE_OPENGLES (default within CNA_BACKEND_EASYGL)
        return GraphicsBackendType::OpenGLES;
#endif
#elif defined(CNA_BACKEND_BGFX)
        return GraphicsBackendType::Bgfx;
#elif defined(CNA_BACKEND_VULKAN)
        return GraphicsBackendType::Vulkan;
#elif defined(CNA_BACKEND_WEBGPU)
        return GraphicsBackendType::WebGPU;
#elif defined(CNA_BACKEND_MAGNUM)
        return GraphicsBackendType::Magnum;
#elif defined(CNA_BACKEND_HEADLESS)
        return GraphicsBackendType::Headless;
#elif defined(CNA_BACKEND_SOFTWARE)
        return GraphicsBackendType::Software;
#elif defined(CNA_BACKEND_STUB)
        return GraphicsBackendType::Stub;
#elif defined(CNA_BACKEND_D3D11)
        return GraphicsBackendType::D3D11;
#elif defined(CNA_BACKEND_D3D12)
        return GraphicsBackendType::D3D12;
#elif defined(CNA_BACKEND_CANVAS)
        return GraphicsBackendType::Canvas;
#elif defined(CNA_BACKEND_SKIA)
        return GraphicsBackendType::Skia;
#elif defined(CNA_BACKEND_ASCII)
        return GraphicsBackendType::Ascii;
#elif defined(CNA_BACKEND_FREEDIRECT)
        return GraphicsBackendType::FreeDirect;
#elif defined(CNA_BACKEND_D3D9)
        return GraphicsBackendType::D3D9;
#elif defined(CNA_BACKEND_DX1)
        return GraphicsBackendType::Dx1;
#elif defined(CNA_BACKEND_DX2)
        return GraphicsBackendType::Dx2;
#elif defined(CNA_BACKEND_DX3)
        return GraphicsBackendType::Dx3;
#elif defined(CNA_BACKEND_DX5)
        return GraphicsBackendType::Dx5;
#elif defined(CNA_BACKEND_DX6)
        return GraphicsBackendType::Dx6;
#elif defined(CNA_BACKEND_DX7)
        return GraphicsBackendType::Dx7;
#elif defined(CNA_BACKEND_DX8)
        return GraphicsBackendType::Dx8;
#elif defined(CNA_BACKEND_D3D10)
        return GraphicsBackendType::D3D10;
#elif defined(CNA_BACKEND_SDL_GPU)
        return GraphicsBackendType::SdlGpu;
#elif defined(CNA_BACKEND_OPENGLES1)
        return GraphicsBackendType::OpenGLES1;
#elif defined(CNA_BACKEND_OPENGL4)
        return GraphicsBackendType::OpenGL4;
#elif defined(CNA_BACKEND_OPENGL1)
        return GraphicsBackendType::OpenGL1;
#elif defined(CNA_BACKEND_OPENGL2)
        return GraphicsBackendType::OpenGL2;
#elif defined(CNA_BACKEND_WICKED)
        return GraphicsBackendType::Wicked;
#elif defined(CNA_BACKEND_SOKOL)
        return GraphicsBackendType::Sokol;
#elif defined(CNA_BACKEND_DILIGENT)
        return GraphicsBackendType::Diligent;
#elif defined(CNA_BACKEND_GLIDE)
        return GraphicsBackendType::Glide;
#elif defined(CNA_BACKEND_GDI)
        return GraphicsBackendType::Gdi;
#else
#error "CNA: no CNA_BACKEND_* compile definition set -- graphics backend selection (cmake/BackendSelection.cmake) is broken"
#endif
    }

    /**
     * @brief Returns the human-readable name of the graphics backend compiled into this build.
     *
     * The returned view matches the CNA_GRAPHICS_BACKEND CMake option value exactly
     * (e.g. "OPENGLES", "SDL_RENDERER", "D3D9") and points at static storage (a string literal),
     * so it stays valid for the lifetime of the program. Like getCurrentGraphicsBackendType(),
     * this is a compile-time constant.
     *
     * @return The active backend's name.
     */
    constexpr std::string_view getCurrentGraphicsBackendName()
    {
        switch (getCurrentGraphicsBackendType())
        {
            case GraphicsBackendType::SdlRenderer: return "SDL_RENDERER";
            case GraphicsBackendType::OpenGLES:    return "OPENGLES";
            case GraphicsBackendType::OpenGL33:    return "OPENGL33";
            case GraphicsBackendType::WebGL1:       return "WEBGL1";
            case GraphicsBackendType::WebGL2:       return "WEBGL2";
            case GraphicsBackendType::Bgfx:         return "BGFX";
            case GraphicsBackendType::Vulkan:       return "VULKAN";
            case GraphicsBackendType::WebGPU:       return "WEBGPU";
            case GraphicsBackendType::Magnum:       return "MAGNUM";
            case GraphicsBackendType::Headless:     return "HEADLESS";
            case GraphicsBackendType::Software:     return "SOFTWARE";
            case GraphicsBackendType::Stub:          return "STUB";
            case GraphicsBackendType::D3D11:        return "D3D11";
            case GraphicsBackendType::D3D12:        return "D3D12";
            case GraphicsBackendType::Canvas:       return "CANVAS";
            case GraphicsBackendType::Skia:         return "SKIA";
            case GraphicsBackendType::Ascii:        return "ASCII";
            case GraphicsBackendType::FreeDirect:           return "FREEDIRECT";
            case GraphicsBackendType::D3D9:          return "D3D9";
            case GraphicsBackendType::Dx1:            return "DX1";
            case GraphicsBackendType::Dx2:            return "DX2";
            case GraphicsBackendType::Dx3:           return "DX3";
            case GraphicsBackendType::Dx5:            return "DX5";
            case GraphicsBackendType::Dx6:            return "DX6";
            case GraphicsBackendType::Dx7:            return "DX7";
            case GraphicsBackendType::Dx8:            return "DX8";
            case GraphicsBackendType::D3D10:          return "D3D10";
            case GraphicsBackendType::SdlGpu:        return "SDL_GPU";
            case GraphicsBackendType::OpenGLES1:     return "OPENGLES1";
            case GraphicsBackendType::OpenGL4:       return "OPENGL4";
            case GraphicsBackendType::OpenGL1:       return "OPENGL1";
            case GraphicsBackendType::OpenGL2:       return "OPENGL2";
            case GraphicsBackendType::Wicked:        return "WICKED";
            case GraphicsBackendType::Sokol:         return "SOKOL";
            case GraphicsBackendType::Diligent:      return "DILIGENT";
            case GraphicsBackendType::Glide:         return "GLIDE";
            case GraphicsBackendType::Gdi:           return "GDI";
        }
        return "UNKNOWN";
    }
} // CNA
