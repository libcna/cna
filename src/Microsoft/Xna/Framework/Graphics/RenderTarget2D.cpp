// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"

#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    using CNA::Internal::Backends::IRenderTargetBackend;

    RenderTarget2D::RenderTarget2D(GraphicsDevice& device, int width, int height)
        : RenderTarget2D(device, width, height, false, SurfaceFormat::Color, DepthFormat::None)
    {
    }

    RenderTarget2D::RenderTarget2D(GraphicsDevice& device,
                                   int width,
                                   int height,
                                   bool mipMap,
                                   SurfaceFormat preferredFormat,
                                   DepthFormat preferredDepthFormat,
                                   int preferredMultiSampleCount,
                                   RenderTargetUsage usage)
        : Texture2D(device, width, height, preferredFormat, mipMap ? 1 : 1,
                    std::shared_ptr<IRenderTargetBackend>(
                        device.GetBackend().CreateRenderTarget2D(
                            width, height, preferredDepthFormat != DepthFormat::None)))
        , depthFormat_(preferredDepthFormat)
        , multiSampleCount_(preferredMultiSampleCount)
        , usage_(usage)
    {
        rtBackend_ = static_cast<IRenderTargetBackend*>(GetBackendRaw());
    }

    RenderTargetUsage RenderTarget2D::getRenderTargetUsageProperty() const { return usage_; }
    DepthFormat RenderTarget2D::getDepthStencilFormatProperty() const { return depthFormat_; }
    int RenderTarget2D::getMultiSampleCountProperty() const { return multiSampleCount_; }

    IRenderTargetBackend* RenderTarget2D::GetRenderTargetBackend() const
    {
        return rtBackend_;
    }
}
