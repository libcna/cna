#pragma once

#include <string_view>

#include "CNA/GraphicsRendererType.hpp"

namespace CNA
{
    /**
     * @brief What kind of implementation technology a graphics backend uses.
     *
     * Orthogonal to GraphicsBackendMaturity, which classifies recommendation confidence instead
     * of implementation technology. For example FREEDIRECT and the real DIRECTX3 renderer both
     * target the DirectDraw/Direct3D 3 era, but FREEDIRECT reimplements that API surface itself
     * (TranslationLayer) while DIRECTX3 talks to the real COM interfaces (Native) -- the same
     * kind of split applies across most of the legacy DirectX family.
     */
    enum class GraphicsBackendCategory
    {
        /** @brief Compiled against exactly one fixed real graphics API, with no runtime backend negotiation. */
        Native,

        /** @brief An intermediate library picks/abstracts the real backend at runtime (a "portable RHI" such as Bgfx/LLGL/Sokol/Diligent/Wicked/FNA3D/SDL_RENDERER/SDL_GPU/WebGPU), or reimplements another API's surface itself (FreeDirect/OpenVG). */
        TranslationLayer,

        /** @brief Renders entirely on the CPU; no real GPU driver is involved. */
        Software,

        /** @brief Only available in a browser/Emscripten build. */
        Web,

        /** @brief Never renders a pixel; exists for testing, debugging, or as a minimal reference implementation. */
        Diagnostic
    };

    /**
     * @brief Returns the human-readable name of a GraphicsBackendCategory value.
     *
     * @param category The category value to name.
     * @return The category's name (e.g. "Native", "TranslationLayer"), or "Unknown" if category
     *         does not match any declared enumerator.
     */
    constexpr std::string_view toStringView(GraphicsBackendCategory category)
    {
        switch (category)
        {
            case GraphicsBackendCategory::Native:           return "Native";
            case GraphicsBackendCategory::TranslationLayer: return "TranslationLayer";
            case GraphicsBackendCategory::Software:         return "Software";
            case GraphicsBackendCategory::Web:              return "Web";
            case GraphicsBackendCategory::Diagnostic:       return "Diagnostic";
        }
        return "Unknown";
    }

    /**
     * @brief Returns the implementation-technology category of the given graphics renderer.
     *
     * Callable for any of the 47 public GraphicsRendererType identities, not only the one
     * compiled into the current build.
     *
     * @param type The renderer identity to classify.
     * @return The renderer's category.
     */
    constexpr GraphicsBackendCategory getGraphicsBackendCategory(GraphicsRendererType type)
    {
        switch (type)
        {
            case GraphicsRendererType::OpenGLES2:
            case GraphicsRendererType::OpenGLES3:
            case GraphicsRendererType::OpenGL33:
            case GraphicsRendererType::Vulkan:
            case GraphicsRendererType::Magnum:
            case GraphicsRendererType::DirectX11:
            case GraphicsRendererType::DirectX12:
            case GraphicsRendererType::Direct2D:
            case GraphicsRendererType::DirectX9:
            case GraphicsRendererType::DirectX1:
            case GraphicsRendererType::DirectX2:
            case GraphicsRendererType::DirectX3:
            case GraphicsRendererType::DirectX5:
            case GraphicsRendererType::DirectX6:
            case GraphicsRendererType::DirectX7:
            case GraphicsRendererType::DirectX8:
            case GraphicsRendererType::DirectX10:
            case GraphicsRendererType::OpenGLES1:
            case GraphicsRendererType::OpenGL4:
            case GraphicsRendererType::OpenGL1:
            case GraphicsRendererType::OpenGL2:
            case GraphicsRendererType::Glide:
            case GraphicsRendererType::Gdi:
            case GraphicsRendererType::Metal:
                return GraphicsBackendCategory::Native;

            case GraphicsRendererType::SdlRenderer:
            case GraphicsRendererType::Bgfx:
            case GraphicsRendererType::WebGPU:
            case GraphicsRendererType::FreeDirect:
            case GraphicsRendererType::SdlGpu:
            case GraphicsRendererType::Wicked:
            case GraphicsRendererType::Diligent:
            case GraphicsRendererType::Igl:
            case GraphicsRendererType::Fna3d:
            case GraphicsRendererType::OpenVg:
            case GraphicsRendererType::NanoVg:
                return GraphicsBackendCategory::TranslationLayer;

            case GraphicsRendererType::Software:
            case GraphicsRendererType::Blend2D:
            case GraphicsRendererType::PortableGL:
            case GraphicsRendererType::TinyGL:
                return GraphicsBackendCategory::Software;

            case GraphicsRendererType::WebGL1:
            case GraphicsRendererType::WebGL2:
            case GraphicsRendererType::Canvas:
            case GraphicsRendererType::HtmlDom:
            case GraphicsRendererType::SvgDom:
            case GraphicsRendererType::PixiJs:
                return GraphicsBackendCategory::Web;

            case GraphicsRendererType::Headless:
            case GraphicsRendererType::Stub:
                return GraphicsBackendCategory::Diagnostic;
        }
        // Unreachable as long as every GraphicsRendererType member has an arm above; kept as a
        // trailing statement (not `default:`) so an omitted member has no compiler diagnostic to
        // rely on -- GraphicsBackendCategoryTests.cpp's own exhaustiveness test is what catches it.
        return GraphicsBackendCategory::Native;
    }

    /**
     * @brief Returns the implementation-technology category of the graphics renderer compiled
     *        into this build.
     *
     * @return getGraphicsBackendCategory(getCurrentGraphicsRendererType()).
     */
    constexpr GraphicsBackendCategory getCurrentGraphicsBackendCategory()
    {
        return getGraphicsBackendCategory(getCurrentGraphicsRendererType());
    }
} // CNA
