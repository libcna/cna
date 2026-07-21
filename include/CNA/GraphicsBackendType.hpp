#pragma once

#include <string_view>

namespace CNA
{
    /** @brief Identifies which CNA graphics backend was selected for this build (compile-time choice, see CNA_GRAPHICS_BACKEND). */
    enum class GraphicsBackendType
    {
        /** @brief SDL_Renderer (2D-only). */
        SdlRenderer,

        /** @brief EasyGL (OpenGL ES). */
        EasyGL,

        /** @brief Bgfx. */
        Bgfx,

        /** @brief Vulkan. */
        Vulkan,

        /** @brief WebGPU (experimental). */
        WebGPU,

        /** @brief Headless (no GPU/window). */
        Headless,

        /** @brief Software (CPU rasterizer). */
        Software,

        /** @brief Direct3D 11. */
        D3D11,

        /** @brief Direct3D 12. */
        D3D12,

        /** @brief HTML Canvas 2D (Emscripten). */
        Canvas,

        /** @brief ASCII (SDL-windowed glyph grid). */
        Ascii,

        /** @brief DX3 (DirectDraw via free-direct). */
        Dx3,

        /** @brief Direct3D 9. */
        D3D9,

        /** @brief DX1 (real DirectDraw v1, no ../free-direct). */
        Dx1,

        /** @brief DX2 (real DirectDraw v1 + Direct3D v2 DrawPrimitive, no execute buffers). */
        Dx2,

        /** @brief DX30 (real DirectX 3 -- DirectDraw v2 + Direct3D v2 DrawPrimitive; temporarily
         * named DX30, see plan_dx30.md's own status note for why). */
        Dx30,

        /** @brief DX5 (real DirectDraw v4 + Direct3D v3 FVF DrawPrimitive, no execute buffers). */
        Dx5,

        /** @brief DX6 (real DirectDraw v4 + Direct3D v3, real stencil buffer operations). */
        Dx6,

        /** @brief DX7 (real DirectDraw v7 + Direct3D v7, flattened device model -- no viewport
         * object, direct texture binding). */
        Dx7,

        /** @brief DX8 (real Direct3D 8, DXVK-delivered, fixed-function only -- no DirectDraw). */
        Dx8,

        /** @brief SDL_GPU. */
        SdlGpu
    };

    /**
     * @brief Returns the graphics backend compiled into this build.
     *
     * Resolved entirely from the CNA_BACKEND_* compile definition cmake/BackendSelection.cmake
     * sets per backend, so this is a compile-time constant -- usable in a constant expression
     * (e.g. static_assert(CNA::getCurrentGraphicsBackendType() == CNA::GraphicsBackendType::EasyGL)).
     *
     * @return The active GraphicsBackendType, determined at compile time by CNA_GRAPHICS_BACKEND.
     */
    constexpr GraphicsBackendType getCurrentGraphicsBackendType()
    {
#if defined(CNA_BACKEND_SDL_RENDERER)
        return GraphicsBackendType::SdlRenderer;
#elif defined(CNA_BACKEND_EASYGL)
        return GraphicsBackendType::EasyGL;
#elif defined(CNA_BACKEND_BGFX)
        return GraphicsBackendType::Bgfx;
#elif defined(CNA_BACKEND_VULKAN)
        return GraphicsBackendType::Vulkan;
#elif defined(CNA_BACKEND_WEBGPU)
        return GraphicsBackendType::WebGPU;
#elif defined(CNA_BACKEND_HEADLESS)
        return GraphicsBackendType::Headless;
#elif defined(CNA_BACKEND_SOFTWARE)
        return GraphicsBackendType::Software;
#elif defined(CNA_BACKEND_D3D11)
        return GraphicsBackendType::D3D11;
#elif defined(CNA_BACKEND_D3D12)
        return GraphicsBackendType::D3D12;
#elif defined(CNA_BACKEND_CANVAS)
        return GraphicsBackendType::Canvas;
#elif defined(CNA_BACKEND_ASCII)
        return GraphicsBackendType::Ascii;
#elif defined(CNA_BACKEND_DX3)
        return GraphicsBackendType::Dx3;
#elif defined(CNA_BACKEND_D3D9)
        return GraphicsBackendType::D3D9;
#elif defined(CNA_BACKEND_DX1)
        return GraphicsBackendType::Dx1;
#elif defined(CNA_BACKEND_DX2)
        return GraphicsBackendType::Dx2;
#elif defined(CNA_BACKEND_DX30)
        return GraphicsBackendType::Dx30;
#elif defined(CNA_BACKEND_DX5)
        return GraphicsBackendType::Dx5;
#elif defined(CNA_BACKEND_DX6)
        return GraphicsBackendType::Dx6;
#elif defined(CNA_BACKEND_DX7)
        return GraphicsBackendType::Dx7;
#elif defined(CNA_BACKEND_DX8)
        return GraphicsBackendType::Dx8;
#elif defined(CNA_BACKEND_SDL_GPU)
        return GraphicsBackendType::SdlGpu;
#else
#error "CNA: no CNA_BACKEND_* compile definition set -- graphics backend selection (cmake/BackendSelection.cmake) is broken"
#endif
    }

    /**
     * @brief Returns the human-readable name of the graphics backend compiled into this build.
     *
     * The returned view matches the CNA_GRAPHICS_BACKEND CMake option value exactly
     * (e.g. "EASYGL", "SDL_RENDERER", "D3D9") and points at static storage (a string literal),
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
            case GraphicsBackendType::EasyGL:      return "EASYGL";
            case GraphicsBackendType::Bgfx:         return "BGFX";
            case GraphicsBackendType::Vulkan:       return "VULKAN";
            case GraphicsBackendType::WebGPU:       return "WEBGPU";
            case GraphicsBackendType::Headless:     return "HEADLESS";
            case GraphicsBackendType::Software:     return "SOFTWARE";
            case GraphicsBackendType::D3D11:        return "D3D11";
            case GraphicsBackendType::D3D12:        return "D3D12";
            case GraphicsBackendType::Canvas:       return "CANVAS";
            case GraphicsBackendType::Ascii:        return "ASCII";
            case GraphicsBackendType::Dx3:           return "DX3";
            case GraphicsBackendType::D3D9:          return "D3D9";
            case GraphicsBackendType::Dx1:            return "DX1";
            case GraphicsBackendType::Dx2:            return "DX2";
            case GraphicsBackendType::Dx30:           return "DX30";
            case GraphicsBackendType::Dx5:            return "DX5";
            case GraphicsBackendType::Dx6:            return "DX6";
            case GraphicsBackendType::Dx7:            return "DX7";
            case GraphicsBackendType::Dx8:            return "DX8";
            case GraphicsBackendType::SdlGpu:        return "SDL_GPU";
        }
        return "UNKNOWN";
    }
} // CNA
