// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/NanoVg/NanoVgTextureRenderer.hpp"

#include "nanovg.h"

#include <cstring>
#include <stdexcept>
#include <vector>

namespace CNA::Internal::Renderers::NanoVg
{
    namespace
    {
        // nvgCreateImageRGBA/nvgUpdateImage both take tightly packed RGBA8 rows (no stride
        // parameter at all) -- repack whenever the caller's stride differs from width*4.
        const uint8_t* TightlyPack(const uint8_t* rgba, int width, int height, int strideBytes,
                                   std::vector<uint8_t>& scratch)
        {
            const int tightStride = width * 4;
            if (strideBytes == tightStride)
                return rgba;

            scratch.resize(static_cast<std::size_t>(tightStride) * static_cast<std::size_t>(height));
            for (int row = 0; row < height; ++row)
            {
                std::memcpy(scratch.data() + static_cast<std::size_t>(row) * tightStride,
                           rgba + static_cast<std::size_t>(row) * strideBytes, tightStride);
            }
            return scratch.data();
        }
    }

    NanoVgTextureRenderer::NanoVgTextureRenderer(NVGcontext& ctx, const ImageData& data)
        : ctx_(ctx)
        , width_(data.width)
        , height_(data.height)
    {
        if (width_ <= 0 || height_ <= 0)
            throw std::runtime_error("NANOVG: texture width/height must be positive.");

        const int stride = width_ * 4;
        if (!data.pixels.empty() &&
            static_cast<std::size_t>(stride) * static_cast<std::size_t>(height_) > data.pixels.size())
        {
            throw std::runtime_error("NANOVG: ImageData.pixels is smaller than width*height*4.");
        }

        std::vector<uint8_t> scratch;
        const uint8_t* initial = data.pixels.empty()
            ? nullptr
            : TightlyPack(data.pixels.data(), width_, height_, stride, scratch);

        // NVG_IMAGE_NEAREST is a per-IMAGE creation flag, not a per-draw sampler state -- unlike
        // XNA's SamplerState.Filter (chosen per SpriteBatch.Begin(), independent of which texture
        // is drawn). NanoVG has no per-draw filter override, so every NanoVgTextureRenderer is
        // created linear-filtered; NanoVgSpriteBatchRenderer::SetSamplerFilter is a documented
        // no-op for the same reason (see that class's own comment).
        image_ = nvgCreateImageRGBA(&ctx_, width_, height_, 0, initial);
        if (image_ == 0)
            throw std::runtime_error("NANOVG: nvgCreateImageRGBA failed.");
    }

    NanoVgTextureRenderer::~NanoVgTextureRenderer()
    {
        if (image_ != 0)
            nvgDeleteImage(&ctx_, image_);
    }

    void NanoVgTextureRenderer::UpdatePixels(const uint8_t* rgba, int stride)
    {
        if (image_ == 0 || !rgba) return;
        std::vector<uint8_t> scratch;
        const uint8_t* packed = TightlyPack(rgba, width_, height_, stride, scratch);
        nvgUpdateImage(&ctx_, image_, packed);
    }
}
