// SPDX-License-Identifier: MS-PL
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

    /// A cube texture that can be used as a render target.
    class RenderTargetCube : public TextureCube, public IRenderTarget
    {
    public:
        /// Constructs a RenderTargetCube with the given size, format, depth format, and usage.
        RenderTargetCube(GraphicsDevice& device, int size,
                         bool mipMap,
                         SurfaceFormat preferredFormat,
                         DepthFormat preferredDepthFormat,
                         int preferredMultiSampleCount = 0,
                         RenderTargetUsage usage = RenderTargetUsage::DiscardContents);

        // IRenderTarget
        /// Gets the width of this render target in pixels.
        [[nodiscard]] int getWidthProperty() const override;
        /// Gets the height of this render target in pixels.
        [[nodiscard]] int getHeightProperty() const override;
        /// Gets the number of mipmap levels.
        [[nodiscard]] int getLevelCountProperty() const override;
        /// Gets the depth/stencil format of this render target.
        [[nodiscard]] DepthFormat getDepthStencilFormatProperty() const override;
        /// Gets the number of multisample samples.
        [[nodiscard]] int getMultiSampleCountProperty() const override;
        /// Gets the render target usage mode.
        [[nodiscard]] RenderTargetUsage getRenderTargetUsageProperty() const override;

        /// Gets whether the render target content has been lost.
        [[nodiscard]] bool getIsContentLostProperty() const { return false; }

        /// Raised when the render target content is lost (never raised in CNA).
        System::EventHandler<System::EventArgs> ContentLost;

    private:
        int size_;
        DepthFormat depthFormat_;
        int multiSampleCount_;
        RenderTargetUsage usage_;
    };
}
