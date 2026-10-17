#pragma once

#include <string_view>

#include "CNA/GraphicsRendererType.hpp"

namespace CNA
{
    /**
     * @brief What kind of implementation technology a graphics renderer uses.
     *
     * Orthogonal to GraphicsRendererMaturity, which classifies recommendation confidence instead
     * of implementation technology. For example FNA3D and DIRECTX9 both end up on Direct3D, but
     * FNA3D picks its driver at runtime (TranslationLayer) while DIRECTX9 talks to the real
     * Direct3D 9 COM interfaces (Native).
     */
    enum class GraphicsRendererCategory
    {
        /** @brief Compiled against exactly one fixed real graphics API, with no runtime backend negotiation. */
        Native,

        /** @brief An intermediate library picks/abstracts the real backend at runtime (a "portable RHI" such as FNA3D/SDL_GPU/WebGPU). */
        TranslationLayer,

        /** @brief Renders entirely on the CPU; no real GPU driver is involved. */
        Software,

        /** @brief Only available in a browser/Emscripten build. */
        Web,

        /** @brief Never renders a pixel; exists for testing, debugging, or as a minimal reference implementation. */
        Diagnostic
    };

    /**
     * @brief Returns the human-readable name of a GraphicsRendererCategory value.
     *
     * @param category The category value to name.
     * @return The category's name (e.g. "Native", "TranslationLayer"), or "Unknown" if category
     *         does not match any declared enumerator.
     */
    constexpr std::string_view toStringView(GraphicsRendererCategory category)
    {
        switch (category)
        {
            case GraphicsRendererCategory::Native:           return "Native";
            case GraphicsRendererCategory::TranslationLayer: return "TranslationLayer";
            case GraphicsRendererCategory::Software:         return "Software";
            case GraphicsRendererCategory::Web:              return "Web";
            case GraphicsRendererCategory::Diagnostic:       return "Diagnostic";
        }
        return "Unknown";
    }

    /**
     * @brief Returns the implementation-technology category of the given graphics renderer.
     *
     * Callable for any public GraphicsRendererType identity, not only the one compiled into the
     * current build.
     *
     * @param type The renderer identity to classify.
     * @return The renderer's category.
     */
    constexpr GraphicsRendererCategory getGraphicsRendererCategory(GraphicsRendererType type)
    {
        switch (type)
        {
            case GraphicsRendererType::OpenGLES3:
            case GraphicsRendererType::OpenGL33:
            case GraphicsRendererType::DirectX9:
                return GraphicsRendererCategory::Native;

            case GraphicsRendererType::WebGPU:
            case GraphicsRendererType::SdlGpu:
            case GraphicsRendererType::Fna3d:
                return GraphicsRendererCategory::TranslationLayer;

            case GraphicsRendererType::Software:
                return GraphicsRendererCategory::Software;

            case GraphicsRendererType::WebGL2:
                return GraphicsRendererCategory::Web;

            case GraphicsRendererType::Headless:
            case GraphicsRendererType::Stub:
                return GraphicsRendererCategory::Diagnostic;
        }
        // Unreachable as long as every GraphicsRendererType member has an arm above; kept as a
        // trailing statement (not `default:`) so an omitted member has no compiler diagnostic to
        // rely on -- GraphicsRendererCategoryTests.cpp's own exhaustiveness test is what catches it.
        return GraphicsRendererCategory::Native;
    }

    /**
     * @brief Returns the implementation-technology category of the graphics renderer compiled
     *        into this build.
     *
     * @return getGraphicsRendererCategory(getCurrentGraphicsRendererType()).
     */
    constexpr GraphicsRendererCategory getCurrentGraphicsRendererCategory()
    {
        return getGraphicsRendererCategory(getCurrentGraphicsRendererType());
    }
} // CNA
