#pragma once

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"

namespace CNA::Internal::Renderers::Canvas
{
    /**
     * @brief Texture backed by a private off-screen `<canvas>` (Design decision 3).
     *
     * Each instance owns one JS-side canvas + `CanvasRenderingContext2D` pair, registered under
     * a unique integer id in `Module['cnaTextures']` (see CanvasRenderer.cpp's `EM_JS`
     * functions) -- `SpriteBatch::Draw()` (Phase C4) sources `ctx.drawImage()` from it via that id.
     */
    class CanvasTextureRenderer : public ITextureRenderer
    {
    public:
        explicit CanvasTextureRenderer(const ImageData& data);
        /// Blank (fully transparent), used by CanvasRenderTargetRenderer -- no ImageData to upload.
        CanvasTextureRenderer(int width, int height);
        ~CanvasTextureRenderer() override;

        [[nodiscard]] int GetWidth() const override { return width_; }
        [[nodiscard]] int GetHeight() const override { return height_; }

        void UpdatePixels(const uint8_t* rgba, int stride) override;
        /// plans/plan_canvas.md CANVAS-21: Canvas2D has no native mip chain either (same conclusion
        /// the native 2D renderer reached, Task 681) -- level>0 throws, level=0 behaves like UpdatePixels.
        void UpdatePixelsLevel(int level, const uint8_t* rgba, int levelW, int levelH) override;

        /// Id into `Module['cnaTextures']`, used by CanvasSpriteBatchRenderer (Phase C4) and
        /// CanvasRenderTargetRenderer's bind/unbind (Phase C3).
        [[nodiscard]] int GetCanvasId() const { return id_; }

    protected:
        int id_ = 0;
        int width_ = 0;
        int height_ = 0;
    };
}
