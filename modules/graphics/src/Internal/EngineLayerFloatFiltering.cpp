// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Graphics/EngineLayerFloatFiltering.hpp"

#include "CNA/GraphicsCapability.hpp"
#include "CNA/RendererCapabilityProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

namespace CNA::Internal
{
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::SurfaceFormat;

    EngineLayerFloatFilteringScope::EngineLayerFloatFilteringScope(GraphicsDevice& device) noexcept
        : device_(device)
    {
        ++device_.engineLayerFloatFilteringDepth_;
    }

    EngineLayerFloatFilteringScope::~EngineLayerFloatFilteringScope()
    {
        --device_.engineLayerFloatFilteringDepth_;
    }

    bool EngineLayerFloatFilteringScope::IsPointFilterOnlyFormat(const SurfaceFormat format) noexcept
    {
        switch (format)
        {
            case SurfaceFormat::Single:
            case SurfaceFormat::Vector2:
            case SurfaceFormat::Vector4:
            case SurfaceFormat::HalfSingle:
            case SurfaceFormat::HalfVector2:
            case SurfaceFormat::HalfVector4:
            case SurfaceFormat::HdrBlendable:
                return true;
            default:
                return false;
        }
    }

    bool EngineLayerFloatFilteringScope::RendererFiltersFormat(const GraphicsDevice& device,
                                                              const SurfaceFormat format)
    {
        if (!IsPointFilterOnlyFormat(format))
            return true;

        const CNA::RendererFormatSupport support = device.GetRendererSurfaceFormatSupportEXT(format);
        if (support.IsKnown(CNA::RendererFormatUsage::Filterable))
            return support.Supports(CNA::RendererFormatUsage::Filterable);

        switch (format)
        {
            case SurfaceFormat::HalfSingle:
            case SurfaceFormat::HalfVector2:
            case SurfaceFormat::HalfVector4:
            case SurfaceFormat::HdrBlendable:
                return device.SupportsCapability(CNA::GraphicsCapability::HalfFloatTextureLinearFiltering);
            default:
                return false;
        }
    }
}
