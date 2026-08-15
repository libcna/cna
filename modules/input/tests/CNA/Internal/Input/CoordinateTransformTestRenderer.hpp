// SPDX-License-Identifier: MS-PL
#pragma once

// Minimal renderer used by input tests to exercise the platform-neutral WindowId coordinate
// contract. Rendering methods are inert because these tests need only the two affine transforms;
// keeping this fake SDL-free is what makes the same behavioral proof run under every platform.

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"

namespace CNA::Internal::Input::Testing {

    class CoordinateTransformTestRenderer final
        : public CNA::Internal::Renderers::IGraphicsRenderer
    {
    public:
        CoordinateTransformTestRenderer(const float logicalWidth, const float logicalHeight,
                                        const float viewportX, const float viewportY,
                                        const float viewportWidth, const float viewportHeight)
            : logicalWidth_(logicalWidth), logicalHeight_(logicalHeight),
              viewportX_(viewportX), viewportY_(viewportY),
              viewportWidth_(viewportWidth), viewportHeight_(viewportHeight)
        {
        }

        bool TransformWindowToLogical(const float windowX, const float windowY,
                                      float& logicalX, float& logicalY) const override
        {
            ++windowToLogicalCalls_;
            if (viewportWidth_ <= 0.0f || viewportHeight_ <= 0.0f)
                return false;
            logicalX = (windowX - viewportX_) * logicalWidth_ / viewportWidth_;
            logicalY = (windowY - viewportY_) * logicalHeight_ / viewportHeight_;
            return true;
        }

        bool TransformLogicalToWindow(const float logicalX, const float logicalY,
                                      float& windowX, float& windowY) const override
        {
            ++logicalToWindowCalls_;
            if (logicalWidth_ <= 0.0f || logicalHeight_ <= 0.0f)
                return false;
            windowX = viewportX_ + logicalX * viewportWidth_ / logicalWidth_;
            windowY = viewportY_ + logicalY * viewportHeight_ / logicalHeight_;
            return true;
        }

        [[nodiscard]] int WindowToLogicalCalls() const { return windowToLogicalCalls_; }
        [[nodiscard]] int LogicalToWindowCalls() const { return logicalToWindowCalls_; }

        void Clear(float, float, float, float) override {}
        void Present() override {}
        void GetViewportSize(int& width, int& height) override
        {
            width = static_cast<int>(logicalWidth_);
            height = static_cast<int>(logicalHeight_);
        }
        void SetVirtualResolution(const int width, const int height) override
        {
            logicalWidth_ = static_cast<float>(width);
            logicalHeight_ = static_cast<float>(height);
        }
        void SetPresentationMode(int) override {}

        std::unique_ptr<CNA::Internal::Renderers::ITextureRenderer> CreateTexture(
            const CNA::Internal::Renderers::ImageData&) override { return nullptr; }
        std::unique_ptr<CNA::Internal::Renderers::ISpriteBatchRenderer> CreateSpriteBatch() override
        {
            return nullptr;
        }
        void SetRenderTargets(
            const CNA::Internal::Renderers::RenderTargetBindingDescriptor*, int) override {}

        void ClearColorAndDepth(float, float, float, float, float) override {}
        void ClearDepth(float) override {}
        void ClearStencil(int) override {}
        void ClearDepthAndStencil(float, int) override {}
        void ClearColorAndStencil(float, float, float, float, int) override {}
        void ClearColorDepthAndStencil(float, float, float, float, float, int) override {}
        void SetDepthTestEnabled(bool) override {}
        void SetBlendEnabled(bool) override {}
        void SetDepthWriteEnabled(bool) override {}

        std::unique_ptr<CNA::Internal::Renderers::IVertexBufferRenderer> CreateVertexBuffer(
            int) override { return nullptr; }
        std::unique_ptr<CNA::Internal::Renderers::IIndexBufferRenderer> CreateIndexBuffer16(
            int) override { return nullptr; }

        void DrawColoredPrimitives(
            const CNA::Internal::Renderers::IVertexBufferRenderer&,
            const CNA::Internal::Renderers::Matrix&,
            const CNA::Internal::Renderers::Matrix&,
            const CNA::Internal::Renderers::Matrix&,
            CNA::Internal::Renderers::PrimitiveType, int) override {}
        void DrawIndexedColoredPrimitives(
            const CNA::Internal::Renderers::IVertexBufferRenderer&,
            const CNA::Internal::Renderers::IIndexBufferRenderer&,
            const CNA::Internal::Renderers::Matrix&,
            const CNA::Internal::Renderers::Matrix&,
            const CNA::Internal::Renderers::Matrix&,
            CNA::Internal::Renderers::PrimitiveType, int) override {}

    private:
        float logicalWidth_;
        float logicalHeight_;
        float viewportX_;
        float viewportY_;
        float viewportWidth_;
        float viewportHeight_;
        mutable int windowToLogicalCalls_ = 0;
        mutable int logicalToWindowCalls_ = 0;
    };

} // namespace CNA::Internal::Input::Testing
