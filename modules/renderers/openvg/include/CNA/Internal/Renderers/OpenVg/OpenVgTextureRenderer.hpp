// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"

namespace CNA::Internal::Renderers::OpenVg
{
    /**
     * @brief Texture backed by a real ShivaVG `VGImage`, created via `vgCreateImage`/
     * `vgImageSubData` (`VG_sABGR_8888` -- straight, non-premultiplied alpha, matching
     * `ImageData`'s own byte order on this little-endian host; see OpenVgTextureRenderer.cpp's own
     * format-choice comment for why not the more obviously-named `VG_sRGBA_8888`).
     *
     * OpenVG images are Y-up (row 0 of image space is the BOTTOM row), while `ImageData`/
     * `UpdatePixels` hand this class top-row-first RGBA8 rows. This class does NOT flip rows on
     * upload, in either direction -- it uploads with a plain POSITIVE stride, keeping row 0 of the
     * `VGImage` as the top row of the source data. `OpenVgSpriteBatchRenderer`'s own per-draw
     * device-flip transform is the ONLY place orientation is decided (verified: baking a row flip
     * in at upload time instead only cancels out correctly for a non-rotated draw, since
     * `vgDrawImage`'s texcoord generation is independent of the modelview/rotation matrix -- see
     * that class's own comment). Proven with a decisive asymmetric-texel pixel oracle
     * (`openvg_texture_orientation_test.cpp`, P1-3): every corner of a small test texture has a
     * distinct, unmistakable RGBA value, verified after upload, `UpdatePixels`, partial source
     * rectangle, scaling, rotation, and both `SpriteEffects` flips.
     */
    class OpenVgTextureRenderer : public ITextureRenderer
    {
    public:
        explicit OpenVgTextureRenderer(const ImageData& data);
        ~OpenVgTextureRenderer() override;

        OpenVgTextureRenderer(const OpenVgTextureRenderer&) = delete;
        OpenVgTextureRenderer& operator=(const OpenVgTextureRenderer&) = delete;

        [[nodiscard]] int GetWidth() const override { return width_; }
        [[nodiscard]] int GetHeight() const override { return height_; }

        void UpdatePixels(const uint8_t* rgba, int stride) override;

        /// Raw `VGImage` handle (declared as `void*` here to keep `<VG/openvg.h>` out of this
        /// public header -- OpenVgSpriteBatchRenderer.cpp, the one consumer, includes it directly).
        [[nodiscard]] void* GetImageHandle() const { return image_; }

    private:
        void* image_ = nullptr;
        int width_ = 0;
        int height_ = 0;
    };
}
