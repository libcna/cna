#pragma once

#include "CanvasTextureRenderer.hpp"

namespace CNA::Internal::Renderers::Canvas
{
    /**
     * @brief Off-screen render target backed by the same private-canvas mechanism as
     * CanvasTextureRenderer (Design decision 3), plus Bind/UnbindAsRenderTarget() to switch which
     * `CanvasRenderingContext2D` subsequent Clear()/Draw() calls target (`Module['cnaCurrentCtx']`).
     */
    class CanvasRenderTargetRenderer final : public IRenderTargetRenderer
    {
    public:
        CanvasRenderTargetRenderer(int w, int h);
        ~CanvasRenderTargetRenderer() override;

        [[nodiscard]] int GetWidth() const override { return texture_.GetWidth(); }
        [[nodiscard]] int GetHeight() const override { return texture_.GetHeight(); }

        void UpdatePixels(const uint8_t* rgba, int stride) override { texture_.UpdatePixels(rgba, stride); }

        void BindAsRenderTarget() override;
        void UnbindAsRenderTarget() override;

        /**
         * @brief Reads this target's rendered pixels back as tightly packed RGBA8 rows.
         *
         * REMED-GFX-127. A Canvas2D render target is a real off-screen `<canvas>`, and
         * `CanvasRenderingContext2D.getImageData` is a synchronous, non-premultiplied RGBA8 read of
         * exactly this target's own context -- the same mechanism `ReadBackbuffer` already uses for
         * whichever context is current. Without this override the shared layer converted its own
         * zero-initialized scratch buffer for the caller, producing a fabricated transparent-black
         * frame; a native (non-Emscripten) build has no canvas at all and now reports that honestly
         * instead.
         *
         * @param level      Mip level; Canvas2D has no mip chain (same boundary as UpdatePixelsLevel).
         * @param x          Left edge of the requested rectangle, in pixels.
         * @param y          Top edge of the requested rectangle, in pixels.
         * @param w          Width of the requested rectangle, in pixels.
         * @param h          Height of the requested rectangle, in pixels.
         * @param data       Destination for @p w * @p h tightly packed RGBA8 pixels.
         * @param dataLength Capacity of @p data in bytes.
         * @return True once the whole rectangle has been written; false when this target's canvas
         *         is unavailable, which is always the case outside an Emscripten build.
         * @throws System::NotSupportedException if @p level is above 0.
         * @throws System::ArgumentOutOfRangeException if @p level is negative, the rectangle leaves
         *         the target, or @p dataLength is too small for the rectangle.
         */
        [[nodiscard]] bool GetData(int level, int x, int y, int w, int h,
                                   void* data, int dataLength) const override;

        // plans/plan_canvas.md CANVAS-23: no Canvas2D target -- main canvas or any off-screen one --
        // ever has a real depth/stencil buffer, regardless of what DepthFormat was requested at
        // construction (same reasoning/precedent as the native 2D renderer's Task 708 override).
        [[nodiscard]] bool HasRealDepthBuffer(bool /*depthFormatWasRequested*/) const override { return false; }

        [[nodiscard]] int GetCanvasId() const { return texture_.GetCanvasId(); }

    private:
        CanvasTextureRenderer texture_;
    };
}
