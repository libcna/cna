#pragma once

#include "CNA/GraphicsCapability.hpp"

namespace CNA::Internal::Renderers::Glide
{
    /** Returns only the capability CNA's implemented Glide path can honor faithfully. */
    [[nodiscard]] constexpr bool SupportsGlideCapability(CNA::GraphicsCapability capability)
    {
        // Keep this exhaustive: a newly added common capability must receive an explicit Glide
        // decision instead of inheriting an optimistic default.
        switch (capability)
        {
            case CNA::GraphicsCapability::ThreeD:
            case CNA::GraphicsCapability::AdditiveBlending:
                return true;
            case CNA::GraphicsCapability::DepthStencilBuffer:
            case CNA::GraphicsCapability::MultiSampleAntiAliasing:
            case CNA::GraphicsCapability::MultipleRenderTargets:
            case CNA::GraphicsCapability::AnisotropicFiltering:
            case CNA::GraphicsCapability::WireFrame:
            case CNA::GraphicsCapability::OcclusionQuery:
            case CNA::GraphicsCapability::CustomEffects:
            case CNA::GraphicsCapability::Texture3D:
            case CNA::GraphicsCapability::MultiStreamVertexInput:
            case CNA::GraphicsCapability::Instancing:
            case CNA::GraphicsCapability::StencilBuffer:
                return false;
        }
        return false;
    }
}
