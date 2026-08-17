#pragma once

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"

namespace CNA::Internal::Renderers::PixiJs
{
    /**
     * @brief Texture backed by a synchronous, buffer-uploaded PIXI.Texture (Design decision 8).
     *
     * Each instance owns one JS-side PIXI.BaseTexture/PIXI.Texture pair, registered under a unique
     * integer id in `Module['cnaPixiTextures']` (see PixiJsRenderer.cpp's EM_JS functions and
     * PixiJsSpriteBatchRenderer.cpp's Draw() path, which sources pooled PIXI.Sprite.texture
     * assignments from it via that id).
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
        [[nodiscard]] SDL_Texture* GetNativeTexture() const override { return nullptr; }
        void UpdatePixels(const uint8_t* rgba, int stride) override;
        /// plan_pixijs.md PIXIJS-31: mip level>0 policy is not yet decided (PixiJS textures can
        /// carry real mipmaps, unlike Canvas2D) -- throws for now rather than silently guessing.
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
