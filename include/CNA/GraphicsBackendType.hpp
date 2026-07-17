#pragma once

#include <string>

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

        /** @brief SDL_GPU. */
        SdlGpu
    };

    /**
     * @brief Returns the graphics backend compiled into this build.
     * @return The active GraphicsBackendType, determined at compile time by CNA_GRAPHICS_BACKEND.
     */
    GraphicsBackendType getCurrentGraphicsBackendType();

    /**
     * @brief Returns the human-readable name of the graphics backend compiled into this build.
     *
     * The returned string matches the CNA_GRAPHICS_BACKEND CMake option value exactly
     * (e.g. "EASYGL", "SDL_RENDERER", "D3D9").
     *
     * @return The active backend's name.
     */
    const std::string& getCurrentGraphicsBackendName();
} // CNA
