#pragma once

#include <string_view>

#include "CNA/GraphicsRendererType.hpp"

namespace CNA
{
    /**
     * @brief How confidently CNA recommends relying on a given graphics backend for real use.
     *
     * Orthogonal to GraphicsBackendCategory, which classifies implementation technology instead
     * of recommendation confidence. Query getGraphicsBackendMaturity()/
     * getCurrentGraphicsBackendMaturity() rather than assuming a fixed maturity from a renderer's
     * name -- maturity can change over time as a backend gains test coverage and real-hardware
     * verification.
     */
    enum class GraphicsBackendMaturity
    {
        /** @brief Fully recommended for regular use; broad, verified feature coverage. */
        Production,

        /** @brief Functional and maintained, but not necessarily full feature parity yet. */
        Supported,

        /** @brief Actively developed; public behavior may still change. */
        Experimental,

        /** @brief A legacy backend kept for compatibility, research, or demonstrating an old API -- not recommended for new work. */
        Historical,

        /** @brief No longer recommended; kept only until removal. Unused by any renderer today. */
        Deprecated
    };

    /**
     * @brief Returns the human-readable name of a GraphicsBackendMaturity value.
     *
     * @param maturity The maturity value to name.
     * @return The maturity's name (e.g. "Production", "Experimental"), or "Unknown" if maturity
     *         does not match any declared enumerator.
     */
    constexpr std::string_view toStringView(GraphicsBackendMaturity maturity)
    {
        switch (maturity)
        {
            case GraphicsBackendMaturity::Production:   return "Production";
            case GraphicsBackendMaturity::Supported:    return "Supported";
            case GraphicsBackendMaturity::Experimental: return "Experimental";
            case GraphicsBackendMaturity::Historical:   return "Historical";
            case GraphicsBackendMaturity::Deprecated:   return "Deprecated";
        }
        return "Unknown";
    }

    /**
     * @brief Returns how confidently CNA recommends the given graphics renderer for real use.
     *
     * Callable for any of the 47 public GraphicsRendererType identities, not only the one
     * compiled into the current build -- e.g. to list every backend's maturity in a launcher or
     * editor UI without compiling all 47 renderer variants.
     *
     * @param type The renderer identity to classify.
     * @return The renderer's maturity.
     */
    constexpr GraphicsBackendMaturity getGraphicsBackendMaturity(GraphicsRendererType type)
    {
        switch (type)
        {
            case GraphicsRendererType::SdlRenderer:
            case GraphicsRendererType::OpenGLES2:
            case GraphicsRendererType::OpenGLES3:
            case GraphicsRendererType::OpenGL33:
            case GraphicsRendererType::Bgfx:
            case GraphicsRendererType::Vulkan:
            case GraphicsRendererType::DirectX9:
            case GraphicsRendererType::DirectX11:
            case GraphicsRendererType::DirectX12:
                return GraphicsBackendMaturity::Production;

            case GraphicsRendererType::WebGL1:
            case GraphicsRendererType::WebGL2:
            case GraphicsRendererType::Magnum:
            case GraphicsRendererType::Headless:
            case GraphicsRendererType::Stub:
            case GraphicsRendererType::Direct2D:
            case GraphicsRendererType::Canvas:
            case GraphicsRendererType::HtmlDom:
            case GraphicsRendererType::SdlGpu:
            case GraphicsRendererType::OpenGL4:
            case GraphicsRendererType::OpenGL2:
            case GraphicsRendererType::Gdi:
            case GraphicsRendererType::Metal:
                return GraphicsBackendMaturity::Supported;

            case GraphicsRendererType::WebGPU:
            case GraphicsRendererType::Software:
            case GraphicsRendererType::Blend2D:
            case GraphicsRendererType::FreeDirect:
            case GraphicsRendererType::Wicked:
            case GraphicsRendererType::Sokol:
            case GraphicsRendererType::Diligent:
            case GraphicsRendererType::Llgl:
            case GraphicsRendererType::Igl:
            case GraphicsRendererType::Fna3d:
            case GraphicsRendererType::SvgDom:
            case GraphicsRendererType::OpenVg:
            case GraphicsRendererType::PortableGL:
            case GraphicsRendererType::TinyGL:
            case GraphicsRendererType::PixiJs:
            case GraphicsRendererType::NanoVg:
                return GraphicsBackendMaturity::Experimental;

            case GraphicsRendererType::DirectX1:
            case GraphicsRendererType::DirectX2:
            case GraphicsRendererType::DirectX3:
            case GraphicsRendererType::DirectX5:
            case GraphicsRendererType::DirectX6:
            case GraphicsRendererType::DirectX7:
            case GraphicsRendererType::DirectX8:
            case GraphicsRendererType::DirectX10:
            case GraphicsRendererType::OpenGLES1:
            case GraphicsRendererType::OpenGL1:
            case GraphicsRendererType::Glide:
                return GraphicsBackendMaturity::Historical;
        }
        // Unreachable as long as every GraphicsRendererType member has an arm above; kept as a
        // trailing statement (not `default:`) so an omitted member has no compiler diagnostic to
        // rely on -- GraphicsBackendMaturityTests.cpp's own exhaustiveness test is what catches it.
        return GraphicsBackendMaturity::Experimental;
    }

    /**
     * @brief Returns the maturity of the graphics renderer compiled into this build.
     *
     * @return getGraphicsBackendMaturity(getCurrentGraphicsRendererType()).
     */
    constexpr GraphicsBackendMaturity getCurrentGraphicsBackendMaturity()
    {
        return getGraphicsBackendMaturity(getCurrentGraphicsRendererType());
    }
} // CNA
