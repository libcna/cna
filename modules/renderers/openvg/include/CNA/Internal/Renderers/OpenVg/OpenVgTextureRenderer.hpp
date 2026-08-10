// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"

namespace CNA::Internal::Renderers::OpenVg
{
    /**
     * @brief Texture backed by a real ShivaVG `VGImage`, created via `vgCreateImage`/
     * `vgImageSubData` (VG_sRGBA_8888 -- straight, non-premultiplied alpha, matching
     * `ImageData`'s own convention).
     *
     * OpenVG images are Y-up (row 0 of image space is the BOTTOM row), while `ImageData`/
     * `UpdatePixels` hand this class top-row-first RGBA8 rows. Rather than flipping rows on the
     * CPU, every upload uses OpenVG's documented negative-`dataStride` trick (point at the last
     * source row and walk backwards) -- so the image's own row 0 genuinely IS the bottom row on
     * screen once drawn, with no separate CPU flip pass.
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
        [[nodiscard]] SDL_Texture* GetNativeTexture() const override { return nullptr; }
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
