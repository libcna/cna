// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/IRenderTarget.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "System/EventArgs.hpp"
#include "System/EventHandler.hpp"
#include "CNA/CNAHelper.hpp"

namespace CNA::Internal::Renderers { class IRenderTargetRenderer; }

namespace Microsoft::Xna::Framework::Graphics
{
    /** @brief A 2D texture that can be used as a render target. Mirrors XNA 4.0 RenderTarget2D. */
    class RenderTarget2D : public Texture2D, public IRenderTarget
    {
    public:
        using Texture2D::Dispose;

        /**
         * @brief Creates a RenderTarget2D with default format and no depth buffer.
         * @param device The graphics device to create the render target on.
         * @param width  Width in pixels.
         * @param height Height in pixels.
         */
        RenderTarget2D(GraphicsDevice& device,
                       int width,
                       int height);

        /**
         * @brief Creates a RenderTarget2D with explicit format, depth format, and usage.
         * @param device                    The graphics device to create the render target on.
         * @param width                     Width in pixels.
         * @param height                    Height in pixels.
         * @param mipMap                    True to generate a full mipmap chain.
         * @param preferredFormat           The desired surface format.
         * @param preferredDepthFormat      The desired depth-stencil format.
         * @param preferredMultiSampleCount Desired multisample count (0 = no multisampling).
         * @param usage                     Specifies how the render target content is preserved.
         */
        RenderTarget2D(GraphicsDevice& device,
                       int width,
                       int height,
                       bool mipMap,
                       SurfaceFormat preferredFormat,
                       DepthFormat preferredDepthFormat,
                       int preferredMultiSampleCount = 0,
                       RenderTargetUsage usage = RenderTargetUsage::DiscardContents);

        /** @brief Destructor. */
        NOXNA ~RenderTarget2D() override = default;

        /**
         * @brief Throws InvalidOperationException if this render target is still bound to the device.
         *
         * Matches FNA's RenderTarget2D.Dispose behaviour: disposing a render target
         * that is currently set on the device is a programming error.
         *
         * @param disposing True when called from Dispose(); false from destructor.
         */
        void Dispose(bool disposing) override;

        RenderTarget2D(const RenderTarget2D&)            = delete;
        RenderTarget2D& operator=(const RenderTarget2D&) = delete;
        RenderTarget2D(RenderTarget2D&&)                 = default;
        RenderTarget2D& operator=(RenderTarget2D&&)      = default;

        /** @brief Returns the fully qualified CNA type name. */
        NOXNA [[nodiscard]] const std::string& GetTypeName() const override;

        // Width, Height, Format, LevelCount are all inherited from Texture2D / Texture.
        // IRenderTarget pure virtuals satisfied:
        /** @brief Returns the render target width in pixels. */
        [[nodiscard]] int getWidthProperty()    const override { return Texture2D::getWidthProperty(); }
        /** @brief Returns the render target height in pixels. */
        [[nodiscard]] int getHeightProperty()   const override { return Texture2D::getHeightProperty(); }
        /** @brief Returns the number of mip levels. */
        [[nodiscard]] int getLevelCountProperty() const override { return Texture::getLevelCountProperty(); }
        /** @brief Returns the render target usage mode. */
        [[nodiscard]] RenderTargetUsage getRenderTargetUsageProperty() const override;
        /** @brief Returns the depth-stencil buffer format. */
        [[nodiscard]] DepthFormat getDepthStencilFormatProperty() const override;
        /** @brief Returns the number of multisample locations. */
        [[nodiscard]] int getMultiSampleCountProperty() const override;

        /** @brief Returns false; content is never lost in CNA. */
        [[nodiscard]] bool getIsContentLostProperty() const { return false; }

        /** @brief Raised when the render target content is lost (never raised in CNA). */
        System::EventHandler<System::EventArgs> ContentLost;

        /**
         * @brief Returns the renderer render target handle.
         *
         * Returns nullptr if the renderer does not support off-screen rendering.
         * @return Pointer to the IRenderTargetRenderer, or nullptr.
         */
        NOXNA [[nodiscard]] CNA::Internal::Renderers::IRenderTargetRenderer* GetRenderTargetRenderer() const;

    private:
        DepthFormat depthFormat_      = DepthFormat::None;
        int multiSampleCount_         = 0;
        RenderTargetUsage usage_      = RenderTargetUsage::DiscardContents;

        // Non-owning pointer into Texture2D::renderer_ (which holds the shared_ptr).
        CNA::Internal::Renderers::IRenderTargetRenderer* rtRenderer_ = nullptr;
    };
}
