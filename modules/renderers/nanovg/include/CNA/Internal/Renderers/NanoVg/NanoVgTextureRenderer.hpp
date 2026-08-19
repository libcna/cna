// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"

struct NVGcontext;

namespace CNA::Internal::Renderers::NanoVg
{
    /**
     * @brief Texture backed by a real NanoVG image, created via `nvgCreateImageRGBA`/
     * `nvgUpdateImage`.
     *
     * NanoVG's own coordinate system is top-left-origin, Y-down (matching HTML Canvas2D
     * semantics, unlike OpenVG's Y-up image space) -- so unlike `OpenVgTextureRenderer`, no
     * device-flip compensation is needed anywhere in this renderer family: pixels are uploaded
     * top-row-first, straight stride, and drawn top-row-first, straight up.
     *
     * `ImageData`/`UpdatePixels` always hand this class straight (non-premultiplied) RGBA8 bytes;
     * `NVG_IMAGE_PREMULTIPLIED` is never set at creation, matching every other CNA renderer's own
     * "textures are never premultiplied" convention.
     */
    class NanoVgTextureRenderer : public ITextureRenderer
    {
    public:
        NanoVgTextureRenderer(NVGcontext& ctx, const ImageData& data);
        ~NanoVgTextureRenderer() override;

        NanoVgTextureRenderer(const NanoVgTextureRenderer&) = delete;
        NanoVgTextureRenderer& operator=(const NanoVgTextureRenderer&) = delete;

        [[nodiscard]] int GetWidth() const override { return width_; }
        [[nodiscard]] int GetHeight() const override { return height_; }

        void UpdatePixels(const uint8_t* rgba, int stride) override;

        /// The NanoVG image handle (an `int`, scoped to the owning `NVGcontext`) this texture
        /// draws through `nvgImagePattern`.
        [[nodiscard]] int GetImageHandle() const { return image_; }

    private:
        NVGcontext& ctx_;
        int image_ = 0;
        int width_ = 0;
        int height_ = 0;
    };
}
