// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Graphics/EngineLayerTextureSize.hpp"

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

#include <algorithm>

namespace CNA::Internal
{
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;

    EngineLayerTextureSizeScope::EngineLayerTextureSizeScope(GraphicsDevice& device) noexcept
        : device_(device)
    {
        ++device_.engineLayerTextureSizeDepth_;
    }

    EngineLayerTextureSizeScope::~EngineLayerTextureSizeScope()
    {
        --device_.engineLayerTextureSizeDepth_;
    }

    int EngineLayerTextureSizeScope::MaxRenderTargetSize(const GraphicsDevice& device)
    {
        const int profile = static_cast<int>(device.getGraphicsProfileProperty());
        const int profileMax = device.GetRenderer().GetMaxTextureSizeForProfileEXT(profile);
        if (device.engineLayerTextureSizeDepth_ <= 0)
            return profileMax;
        // Never below the profile's own ceiling: a scope widens what may be allocated, it does not
        // narrow it for a renderer that reports a small hardware limit.
        return std::max(profileMax, device.GetMaxTextureDimension());
    }
}
