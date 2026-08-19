// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"

namespace CNA::Internal::Renderers::NanoVg
{
    class NanoVgRenderer;

    /**
     * @brief Texture backed by a real NanoVG image, created via `nvgCreateImageRGBA`/
     * `nvgUpdateImage`.
     *
     * NanoVG's own coordinate system is top-left-origin, Y-down (matching HTML Canvas2D
     * semantics, unlike OpenVG's Y-up image space) -- so unlike `OpenVgTextureRenderer`, no
     * device-flip compensation is needed anywhere in this renderer family: pixels are uploaded
     * top-row-first, straight stride, and drawn top-row-first, straight up.
     *
     * `ImageData`/`UpdatePixels` always hand this class straight (non-premultiplied) RGBA8 bytes,
     * matching every other CNA renderer's own "textures are never premultiplied" convention. The
     * image is nevertheless created WITH `NVG_IMAGE_PREMULTIPLIED`, because that flag does not
     * describe the uploaded bytes at all -- it selects the fragment-shader branch that leaves the
     * sampled texel alone instead of multiplying its RGB by its own alpha. Leaving the texel alone
     * is exactly what XNA's SpriteBatch pixel shader does, and it is what keeps `AlphaBlend`
     * (premultiplied source) and `NonPremultiplied` (straight source) genuinely distinct. See
     * NanoVgTextureRenderer.cpp's own comment for the full reasoning.
     *
     * A mip-mapped `Texture2D` is refused at construction: `nvgCreateImageRGBA` allocates exactly
     * one level and NanoVG exposes no per-level upload or LOD-sampling API, so a chain can be
     * neither stored nor sampled. `UpdatePixelsLevel` refuses any level above zero for the same
     * reason, rather than inheriting the base class's silently-discarding default.
     *
     * Holds a reference back to its owning `NanoVgRenderer` (not just its `NVGcontext*`) so
     * `UpdatePixels()` -- called any time after construction, possibly after a sibling
     * `NanoVgRenderer` instance last made ITS OWN context current -- can re-assert the correct GL
     * context first, the same reason `NanoVgSpriteBatchRenderer` holds one.
     */
    class NanoVgTextureRenderer : public ITextureRenderer
    {
    public:
        NanoVgTextureRenderer(NanoVgRenderer& owner, const ImageData& data);
        ~NanoVgTextureRenderer() override;

        NanoVgTextureRenderer(const NanoVgTextureRenderer&) = delete;
        NanoVgTextureRenderer& operator=(const NanoVgTextureRenderer&) = delete;

        [[nodiscard]] int GetWidth() const override { return width_; }
        [[nodiscard]] int GetHeight() const override { return height_; }

        void UpdatePixels(const uint8_t* rgba, int stride) override;

        /// Level 0 is an ordinary full-surface update; any higher level is refused. NanoVG images
        /// are single-level, so accepting one would discard the upload silently -- the base class's
        /// own default is an empty body, which is why this override exists.
        void UpdatePixelsLevel(int level, const uint8_t* rgba, int levelW, int levelH) override;

        /// The NanoVG image handle (an `int`, scoped to the owning `NVGcontext`) this texture
        /// draws through `nvgImagePattern`.
        [[nodiscard]] int GetImageHandle() const { return image_; }

        /// CNAEXT. The renderer whose `NVGcontext` `GetImageHandle()` is valid in.
        ///
        /// Image handles are small per-context integers that every `NVGcontext` allocates from its
        /// own counter starting at the same value, so a handle from one context is very likely to
        /// be a VALID but DIFFERENT image in another. Drawing a texture through a foreign
        /// `SpriteBatch` would therefore silently sample the wrong picture rather than fail, which
        /// is why `NanoVgSpriteBatchRenderer::Draw` compares this against its own owner.
        [[nodiscard]] const NanoVgRenderer* GetOwnerEXT() const { return &owner_; }

    private:
        NanoVgRenderer& owner_;
        int image_ = 0;
        int width_ = 0;
        int height_ = 0;
    };
}
