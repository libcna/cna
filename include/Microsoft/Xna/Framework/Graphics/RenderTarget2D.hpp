#pragma once

#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsResource.hpp"
#include "Microsoft/Xna/Framework/Graphics/IRenderTarget.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /// A 2D texture that can be used as a render target.
    class RenderTarget2D : public GraphicsResource, public IRenderTarget
    {
    public:
        RenderTarget2D(GraphicsDevice& device,
                       int width,
                       int height);

        RenderTarget2D(GraphicsDevice& device,
                       int width,
                       int height,
                       bool mipMap,
                       SurfaceFormat preferredFormat,
                       DepthFormat preferredDepthFormat,
                       int preferredMultiSampleCount = 0,
                       RenderTargetUsage usage = RenderTargetUsage::DiscardContents);

        [[nodiscard]] int getWidthProperty() const override;
        [[nodiscard]] int getHeightProperty() const override;
        [[nodiscard]] int getLevelCountProperty() const override;
        [[nodiscard]] RenderTargetUsage getRenderTargetUsageProperty() const override;
        [[nodiscard]] DepthFormat getDepthStencilFormatProperty() const override;
        [[nodiscard]] int getMultiSampleCountProperty() const override;
        [[nodiscard]] SurfaceFormat getFormatProperty() const;

    private:
        int width_;
        int height_;
        int levelCount_;
        SurfaceFormat format_;
        DepthFormat depthFormat_;
        int multiSampleCount_;
        RenderTargetUsage usage_;
    };
}
