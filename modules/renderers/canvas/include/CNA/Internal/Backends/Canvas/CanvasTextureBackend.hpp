#pragma once

#include "../Common/IGraphicsBackend.hpp"

namespace CNA::Internal::Backends::Canvas
{
    /**
     * @brief Texture backed by a private off-screen `<canvas>` (Design decision 3).
     *
     * Each instance owns one JS-side canvas + `CanvasRenderingContext2D` pair, registered under
     * a unique integer id in `Module['cnaTextures']` (see CanvasGraphicsBackend.cpp's `EM_JS`
     * functions) -- `SpriteBatch::Draw()` (Phase C4) sources `ctx.drawImage()` from it via that id.
     */
    class CanvasTextureBackend : public ITextureBackend
    {
    public:
        explicit CanvasTextureBackend(const ImageData& data);
        /// Blank (fully transparent), used by CanvasRenderTargetBackend -- no ImageData to upload.
        CanvasTextureBackend(int width, int height);
        ~CanvasTextureBackend() override;

        [[nodiscard]] int GetWidth() const override { return width_; }
        [[nodiscard]] int GetHeight() const override { return height_; }
        [[nodiscard]] SDL_Texture* GetNativeTexture() const override { return nullptr; }
        void UpdatePixels(const uint8_t* rgba, int stride) override;
        /// plan_canvas.md CANVAS-21: Canvas2D has no native mip chain either (same conclusion
        /// SDL_RENDERER reached, Task 681) -- level>0 throws, level=0 behaves like UpdatePixels.
        void UpdatePixelsLevel(int level, const uint8_t* rgba, int levelW, int levelH) override;

        /// Id into `Module['cnaTextures']`, used by CanvasSpriteBatchBackend (Phase C4) and
        /// CanvasRenderTargetBackend's bind/unbind (Phase C3).
        [[nodiscard]] int GetCanvasId() const { return id_; }

    protected:
        int id_ = 0;
        int width_ = 0;
        int height_ = 0;
    };
}
