// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/IRenderTarget.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "CNA/CNAHelper.hpp"

namespace CNA::Internal::Backends { class IRenderTargetBackend; }

namespace Microsoft::Xna::Framework::Graphics
{
    /// A 2D texture that can be used as a render target. Mirrors XNA 4.0 RenderTarget2D.
    class RenderTarget2D : public Texture2D, public IRenderTarget
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

        RenderTarget2D(const RenderTarget2D&)            = delete;
        RenderTarget2D& operator=(const RenderTarget2D&) = delete;
        RenderTarget2D(RenderTarget2D&&)                 = default;
        RenderTarget2D& operator=(RenderTarget2D&&)      = default;

        // Width, Height, Format, LevelCount are all inherited from Texture2D / Texture.
        // IRenderTarget pure virtuals satisfied:
        [[nodiscard]] int getWidthProperty()    const override { return Texture2D::getWidthProperty(); }
        [[nodiscard]] int getHeightProperty()   const override { return Texture2D::getHeightProperty(); }
        [[nodiscard]] int getLevelCountProperty() const override { return Texture::getLevelCountProperty(); }
        [[nodiscard]] RenderTargetUsage getRenderTargetUsageProperty() const override;
        [[nodiscard]] DepthFormat getDepthStencilFormatProperty() const override;
        [[nodiscard]] int getMultiSampleCountProperty() const override;

        /// Returns the backend render target handle. Returns nullptr if the
        /// backend does not support off-screen rendering.
        NOXNA [[nodiscard]] CNA::Internal::Backends::IRenderTargetBackend* GetRenderTargetBackend() const;

    private:
        DepthFormat depthFormat_      = DepthFormat::None;
        int multiSampleCount_         = 0;
        RenderTargetUsage usage_      = RenderTargetUsage::DiscardContents;

        // Non-owning pointer into Texture2D::backend_ (which holds the shared_ptr).
        CNA::Internal::Backends::IRenderTargetBackend* rtBackend_ = nullptr;
    };
}
