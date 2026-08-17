#pragma once

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"

namespace CNA::Internal::Renderers::PixiJs
{
    /**
     * @brief Texture backed by a synchronous, buffer-uploaded PIXI.Texture (Design decision 8).
     *
     * Each instance owns one JS-side PIXI.Texture (and, through it, its PIXI.BaseTexture and the
     * cache of per-source-rectangle views onto it), registered under a unique integer id in
     * `Module['cnaPixi'].textures` -- see PixiJsRenderer.cpp's EM_JS functions and
     * PixiJsSpriteBatchRenderer.cpp's flush, which resolves the sampled texture from that registry
     * by id.
     */
    class PixiJsTextureRenderer : public ITextureRenderer
    {
    public:
        /** @brief Uploads @p data synchronously as a new buffer-backed PixiJS texture. */
        explicit PixiJsTextureRenderer(const ImageData& data);
        /// Blank (fully transparent), used by PixiJsRenderTargetRenderer -- no ImageData to upload.
        PixiJsTextureRenderer(int width, int height);
        /** @brief Destroys the underlying PIXI.Texture and removes it from the id registry. */
        ~PixiJsTextureRenderer() override;

        [[nodiscard]] int GetWidth() const override { return width_; }
        [[nodiscard]] int GetHeight() const override { return height_; }
        void UpdatePixels(const uint8_t* rgba, int stride) override;
        /// plan_pixijs.md PIXIJS-31: investigated and decided, 2026-08-17 (not merely undecided) --
        /// `PIXI.BufferResource` (this renderer's own upload path) exposes only `upload()`/
        /// `dispose()`, and `PIXI.BaseTexture` exposes only a `mipmap` on/off mode, no per-level
        /// upload hook at all (confirmed live via a browser probe of both prototypes). PixiJS
        /// mipmaps are GPU-auto-generated from level 0 (`gl.generateMipmap`) only; there is no
        /// public API to upload a custom CPU-authored mip chain. Throws for level>0, matching
        /// Canvas2D's own structural conclusion, but for an independently investigated reason.
        void UpdatePixelsLevel(int level, const uint8_t* rgba, int levelW, int levelH) override;

        /// Id into `Module['cnaPixiTextures']`, used by PixiJsSpriteBatchRenderer and
        /// PixiJsRenderTargetRenderer's bind/unbind.
        [[nodiscard]] int GetPixiTextureId() const { return id_; }

    protected:
        int id_ = 0;
        int width_ = 0;
        int height_ = 0;
    };
}
