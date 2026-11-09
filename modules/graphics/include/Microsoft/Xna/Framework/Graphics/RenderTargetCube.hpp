// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Internal/Graphics/IContentLosable.hpp"
#include "CNA/Internal/Graphics/IRenderTargetBindingSource.hpp"
#include "System/EventArgs.hpp"
#include "System/EventHandler.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/IRenderTarget.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "CNA/CNAHelper.hpp"
#include <memory>

namespace CNA::Internal::Renderers { class IRenderTargetCubeRenderer; }

namespace Microsoft::Xna::Framework::Graphics
{
    class GraphicsDevice;

    /** @brief A cube map texture that can be used as a render target. */
    class RenderTargetCube : public TextureCube, public IRenderTarget,
            public CNA::Internal::Graphics::IContentLosable,
            public CNA::Internal::Graphics::IRenderTargetBindingSource
    {
    public:
        using TextureCube::Dispose;

        /**
         * @brief Creates a RenderTargetCube with the given size, format, depth format, and usage.
         * @param device                    The graphics device to create the render target on.
         * @param size                      Width and height of each cube face in pixels.
         * @param mipMap                    True to generate a full mipmap chain.
         * @param preferredFormat           The desired surface format.
         * @param preferredDepthFormat      The desired depth-stencil format.
         * @param preferredMultiSampleCount Desired multisample count (0 = no multisampling).
         * @param usage                     Specifies how the render target content is preserved.
         */
        RenderTargetCube(GraphicsDevice& device, int size,
                         bool mipMap,
                         SurfaceFormat preferredFormat,
                         DepthFormat preferredDepthFormat,
                         int preferredMultiSampleCount = 0,
                         RenderTargetUsage usage = RenderTargetUsage::DiscardContents);

        /** @brief Destructor. */
        CNAEXT ~RenderTargetCube() override = default;

        /**
         * @brief Releases the cube render target after rejecting disposal while it is bound.
         * @param disposing True when called from Dispose(); false from destructor.
         */
        void Dispose(bool disposing) override;

        /**
         * @brief Releases the target's renderer surfaces when the owning device is disposed,
         *        leaving it undisposed (Refresh RenderTargetCube::ReleaseNativeObject IL_0000-0012).
         */
        void ReleaseNativeResourcesForDeviceTeardown() override;

        /** @brief Returns the fully qualified .NET type name. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

        // IRenderTarget
        /** @brief Returns the width of each cube face in pixels. */
        [[nodiscard]] int getWidthProperty() const override;
        /** @brief Returns the height of each cube face in pixels. */
        [[nodiscard]] int getHeightProperty() const override;
        /** @brief Returns the number of mipmap levels. */
        [[nodiscard]] int getLevelCountProperty() const override;
        /** @brief Returns the depth-stencil buffer format. */
        [[nodiscard]] DepthFormat getDepthStencilFormatProperty() const override;
        /** @brief Returns the number of multisample locations. */
        [[nodiscard]] int getMultiSampleCountProperty() const override;
        /** @brief Returns the render target usage mode. */
        [[nodiscard]] RenderTargetUsage getRenderTargetUsageProperty() const override;

        /**
         * @brief Whether this render target's contents were lost to a device reset.
         *
         * True from the moment a renderer reports a real device reset until the content is written
         * again, exactly where XNA clears it: a draw while this target is bound (every
         * GraphicsDevice draw and a SpriteBatch flush; XNA's VerifyCanDraw IL_0011-003e, which runs
         * before the draw's own state checks), a completed `GraphicsDevice::Clear` while it is
         * bound (Clear IL_01e4-020f; a refused Clear leaves it set), or a completed `SetData` into
         * it (CopyData; a refused `SetData` and every `GetData` leave it set). Binding the target
         * alone does not clear it. Renderers whose API cannot lose a device never set it.
         */
        [[nodiscard]] bool getIsContentLostProperty() const { return contentLost_; }

        /** @brief Marks the content lost and raises ContentLost. */
        CNAEXT void NotifyContentLostEXT() override
        {
            contentLost_ = true;
            ContentLost.Raise(this, System::EventArgs::Empty);
        }

        /**
         * @brief Clears the lost flag; called by a draw or completed Clear while this target is
         *        bound, and by a completed SetData.
         */
        CNAEXT void ClearContentLostEXT() noexcept override { contentLost_ = false; }

        /**
         * @brief Raised when this render target's content is lost to a device reset.
         *
         * Raised for real on the renderers whose API can lose a device (DirectX9, for
         * example). Families that cannot lose one never raise it.
         */
        System::EventHandler<System::EventArgs> ContentLost;

        /** @brief Returns the renderer cube render target handle (CNA extension). */
        CNAEXT [[nodiscard]] CNA::Internal::Renderers::IRenderTargetCubeRenderer* GetRenderTargetCubeRenderer() const;

    private:
        int size_;
        DepthFormat depthFormat_;
        int multiSampleCount_;
        RenderTargetUsage usage_;
        // Non-owning pointer into TextureCube::renderer_ (which holds unique ownership).
        CNA::Internal::Renderers::IRenderTargetCubeRenderer* rtCubeRenderer_ = nullptr;

    private:
        /// GraphicsDevice binds this target through these (IRenderTargetBindingSource).
        [[nodiscard]] CNA::Internal::Renderers::RenderTargetBindingDescriptor
        DescribeRenderTargetBindingEXT(const RenderTargetBinding& binding) override;
        void QueryRealDepthStencilEXT(bool& depth, bool& stencil) const override;

    private:
        /** @brief Set by a real renderer-reported device reset; cleared by the next write. */
        bool contentLost_ = false;
    };
}
