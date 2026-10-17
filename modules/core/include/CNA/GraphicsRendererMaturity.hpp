#pragma once

#include <string_view>

#include "CNA/GraphicsRendererType.hpp"

namespace CNA
{
    /**
     * @brief How confidently CNA recommends relying on a given graphics renderer for real use.
     *
     * Orthogonal to GraphicsRendererCategory, which classifies implementation technology instead
     * of recommendation confidence. Query getGraphicsRendererMaturity()/
     * getCurrentGraphicsRendererMaturity() rather than assuming a fixed maturity from a renderer's
     * name -- maturity can change over time as a renderer gains test coverage and real-hardware
     * verification.
     */
    enum class GraphicsRendererMaturity
    {
        /** @brief Fully recommended for regular use; broad, verified feature coverage. */
        Production,

        /** @brief Functional and maintained, but not necessarily full feature parity yet. */
        Supported,

        /** @brief Actively developed; public behavior may still change. */
        Experimental,

        /** @brief A legacy renderer kept for compatibility, research, or demonstrating an old API -- not recommended for new work. Unused by any renderer today. */
        Historical,

        /** @brief No longer recommended; kept only until removal. Unused by any renderer today. */
        Deprecated
    };

    /**
     * @brief Returns the human-readable name of a GraphicsRendererMaturity value.
     *
     * @param maturity The maturity value to name.
     * @return The maturity's name (e.g. "Production", "Experimental"), or "Unknown" if maturity
     *         does not match any declared enumerator.
     */
    constexpr std::string_view toStringView(GraphicsRendererMaturity maturity)
    {
        switch (maturity)
        {
            case GraphicsRendererMaturity::Production:   return "Production";
            case GraphicsRendererMaturity::Supported:    return "Supported";
            case GraphicsRendererMaturity::Experimental: return "Experimental";
            case GraphicsRendererMaturity::Historical:   return "Historical";
            case GraphicsRendererMaturity::Deprecated:   return "Deprecated";
        }
        return "Unknown";
    }

    /**
     * @brief Returns how confidently CNA recommends the given graphics renderer for real use.
     *
     * Callable for any public GraphicsRendererType identity, not only the one compiled into the
     * current build -- e.g. to list every renderer's maturity in a launcher or editor UI without
     * compiling every renderer variant.
     *
     * @param type The renderer identity to classify.
     * @return The renderer's maturity.
     */
    constexpr GraphicsRendererMaturity getGraphicsRendererMaturity(GraphicsRendererType type)
    {
        switch (type)
        {
            case GraphicsRendererType::OpenGLES3:
            case GraphicsRendererType::OpenGL33:
            case GraphicsRendererType::DirectX9:
                return GraphicsRendererMaturity::Production;

            case GraphicsRendererType::WebGL2:
            case GraphicsRendererType::WebGPU:
            case GraphicsRendererType::Headless:
            case GraphicsRendererType::Stub:
            case GraphicsRendererType::SdlGpu:
                return GraphicsRendererMaturity::Supported;

            case GraphicsRendererType::Software:
            case GraphicsRendererType::Fna3d:
                return GraphicsRendererMaturity::Experimental;
        }
        // Unreachable as long as every GraphicsRendererType member has an arm above; kept as a
        // trailing statement (not `default:`) so an omitted member has no compiler diagnostic to
        // rely on -- GraphicsRendererMaturityTests.cpp's own exhaustiveness test is what catches it.
        return GraphicsRendererMaturity::Experimental;
    }

    /**
     * @brief Returns the maturity of the graphics renderer compiled into this build.
     *
     * @return getGraphicsRendererMaturity(getCurrentGraphicsRendererType()).
     */
    constexpr GraphicsRendererMaturity getCurrentGraphicsRendererMaturity()
    {
        return getGraphicsRendererMaturity(getCurrentGraphicsRendererType());
    }
} // CNA
