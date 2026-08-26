// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Internal/Graphics/IContentLosable.hpp"
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
    class RenderTarget2D : public Texture2D, public IRenderTarget,
            public CNA::Internal::Graphics::IContentLosable
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
        CNAEXT ~RenderTarget2D() override = default;

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
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

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

        /**
         * @brief Whether this render target's contents were lost to a device reset.
         *
         * True from the moment a renderer reports a real device reset until this target is next
         * **bound for rendering**, which is when the caller takes ownership of its contents again.
         * Binding is the boundary rather than a subsequent draw or `SetData`, deliberately: a bound
         * target with the default `RenderTargetUsage::DiscardContents` has already had its previous
         * contents discarded, so there is nothing left to describe as lost, and a renderer-neutral
         * "a pixel was actually written" signal does not exist below this API. Renderers whose API
         * cannot lose a device never set it.
         */
        [[nodiscard]] bool getIsContentLostProperty() const { return contentLost_; }

        /** @brief Marks the content lost and raises ContentLost. */
        CNAEXT void NotifyContentLostEXT() override
        {
            contentLost_ = true;
            ContentLost.Raise(this, System::EventArgs::Empty);
        }

        /** @brief Clears the lost flag; called when this target is bound for rendering. */
        CNAEXT void ClearContentLostEXT() noexcept override { contentLost_ = false; }

        /**
         * @brief Raised when this render target's content is lost to a device reset.
         *
         * Raised for real on the renderers whose API can lose a device (DirectX9,
         * Direct2D, Skia). Families that cannot lose one never raise it.
         */
        System::EventHandler<System::EventArgs> ContentLost;

        /**
         * @brief Returns the renderer render target handle.
         *
         * Returns nullptr if the renderer does not support off-screen rendering.
         * @return Pointer to the IRenderTargetRenderer, or nullptr.
         */
        CNAEXT [[nodiscard]] CNA::Internal::Renderers::IRenderTargetRenderer* GetRenderTargetRenderer() const;

    private:
        DepthFormat depthFormat_      = DepthFormat::None;
        int multiSampleCount_         = 0;
        RenderTargetUsage usage_      = RenderTargetUsage::DiscardContents;

        // Non-owning pointer into Texture2D::renderer_ (which holds the shared_ptr).
        CNA::Internal::Renderers::IRenderTargetRenderer* rtRenderer_ = nullptr;

    private:
        /** @brief Set by a real renderer-reported device reset; cleared by the next write. */
        bool contentLost_ = false;
    };
}
