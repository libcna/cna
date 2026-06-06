#pragma once

#include "System/EventArgs.hpp"
#include "System/EventHandler.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/IRenderTarget.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    class GraphicsDevice;

    class RenderTargetCube : public TextureCube, public IRenderTarget
    {
    public:
        RenderTargetCube(GraphicsDevice& device, int size,
                         bool mipMap,
                         SurfaceFormat preferredFormat,
                         DepthFormat preferredDepthFormat,
                         int preferredMultiSampleCount = 0,
                         RenderTargetUsage usage = RenderTargetUsage::DiscardContents);

        // IRenderTarget
        [[nodiscard]] int getWidthProperty() const override;
        [[nodiscard]] int getHeightProperty() const override;
        [[nodiscard]] int getLevelCountProperty() const override;
        [[nodiscard]] DepthFormat getDepthStencilFormatProperty() const override;
        [[nodiscard]] int getMultiSampleCountProperty() const override;
        [[nodiscard]] RenderTargetUsage getRenderTargetUsageProperty() const override;

        [[nodiscard]] bool getIsContentLostProperty() const { return false; }

        System::EventHandler<System::EventArgs> ContentLost;

    private:
        int size_;
        DepthFormat depthFormat_;
        int multiSampleCount_;
        RenderTargetUsage usage_;
    };
}
