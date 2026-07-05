// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"

#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"
#include "System/InvalidOperationException.hpp"

#include <algorithm>

namespace Microsoft::Xna::Framework::Graphics
{
    using CNA::Internal::Backends::IRenderTargetBackend;

    // Mirrors Texture2D.cpp's/TextureCube.cpp's CalculateMipLevels.
    static int CalculateMipLevels(int w, int h)
    {
        int levels = 1;
        while (w > 1 || h > 1) { w = std::max(1, w / 2); h = std::max(1, h / 2); ++levels; }
        return levels;
    }

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
        : Texture2D(device, width, height, preferredFormat,
                    mipMap ? CalculateMipLevels(width, height) : 1,
                    std::shared_ptr<IRenderTargetBackend>(
                        device.GetBackend().CreateRenderTarget2D(
                            width, height, preferredDepthFormat != DepthFormat::None,
                            usage == RenderTargetUsage::PreserveContents, mipMap)))
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

    const std::string& RenderTarget2D::GetTypeName() const
    {
        static const std::string name = "Microsoft.Xna.Framework.Graphics.RenderTarget2D";
        return name;
    }

    void RenderTarget2D::Dispose(bool disposing)
    {
        if (!isDisposed_ && graphicsDevice_ != nullptr)
        {
            for (const auto& binding : graphicsDevice_->GetRenderTargets())
            {
                if (binding.getRenderTargetProperty() == this)
                    throw System::InvalidOperationException("Disposing target that is still bound");
            }
        }
        Texture2D::Dispose(disposing);
    }
}
