#pragma once

#include "../Common/IGraphicsBackend.hpp"
#include "CNA/Internal/Backends/Skia/SkiaImageSource.hpp"

#include "include/core/SkImage.h"

#include <cstdint>
#include <vector>

namespace CNA::Internal::Backends::Skia
{
    /**
     * CPU-backed level-0 Texture2D image for the raster Skia backend.
     *
     * rawPixels_ stays in CNA's straight-alpha, top-row-first RGBA8 convention. Skia receives a
     * copy with an explicitly unpremultiplied image info, so its canvas pipeline performs normal
     * premultiplied compositing without changing the bytes Texture2D::GetData observes.
     */
    class SkiaTextureBackend final : public ITextureBackend, public SkiaImageSource
    {
    public:
        explicit SkiaTextureBackend(const ImageData& data);

        [[nodiscard]] int GetWidth() const override { return width_; }
        [[nodiscard]] int GetHeight() const override { return height_; }
        [[nodiscard]] SDL_Texture* GetNativeTexture() const override { return nullptr; }

        void UpdatePixels(const std::uint8_t* rgba, int stride) override;
        void UpdatePixelsLevel(int level, const std::uint8_t* rgba, int levelWidth, int levelHeight) override;
        [[nodiscard]] bool GetData(int level, int x, int y, int width, int height,
                                   void* data, int dataLength) const override;

        [[nodiscard]] const sk_sp<SkImage>& Image() const noexcept { return image_; }
        [[nodiscard]] sk_sp<SkImage> SnapshotImage() const override { return image_; }

    private:
        void RebuildImage();

        int width_ = 0;
        int height_ = 0;
        std::vector<std::uint8_t> rawPixels_;
        sk_sp<SkImage> image_;
    };
} // namespace CNA::Internal::Backends::Skia
