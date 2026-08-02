#pragma once

#include "../Common/IGraphicsBackend.hpp"
#include "CNA/Internal/Backends/Skia/SkiaImageSource.hpp"
#include "CNA/Internal/Backends/Skia/SkiaResourceCounters.hpp"

#include "include/core/SkImage.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace CNA::Internal::Backends::Skia
{
    /**
     * CPU-backed level-0 Texture2D image for the raster Skia backend.
     *
     * rawPixels_ stays in CNA's top-row-first RGBA8 convention and is the exact public GetData
     * shadow.  The two Skia snapshots label those same bytes for the two XNA source-alpha
     * conventions; the active BlendState selects one at draw time without rewriting public data.
     */
    class SkiaTextureBackend final : public ITextureBackend, public SkiaImageSource
    {
    public:
        explicit SkiaTextureBackend(const ImageData& data,
                                    std::shared_ptr<SkiaResourceCounters> resourceCounters = {});
        ~SkiaTextureBackend() override;

        [[nodiscard]] int GetWidth() const override { return width_; }
        [[nodiscard]] int GetHeight() const override { return height_; }
        [[nodiscard]] SDL_Texture* GetNativeTexture() const override { return nullptr; }

        void UpdatePixels(const std::uint8_t* rgba, int stride) override;
        void UpdatePixelsLevel(int level, const std::uint8_t* rgba, int levelWidth, int levelHeight) override;
        [[nodiscard]] bool GetData(int level, int x, int y, int width, int height,
                                   void* data, int dataLength) const override;

        [[nodiscard]] SkiaSourceStorageAlpha StorageAlphaEXT() const noexcept override
        {
            return SkiaSourceStorageAlpha::CanonicalRgbaBytes;
        }
        [[nodiscard]] sk_sp<SkImage> SnapshotImage(
            SkiaSourceAlphaConvention alphaConvention) const override;

    private:
        void RebuildImage();

        int width_ = 0;
        int height_ = 0;
        std::vector<std::uint8_t> rawPixels_;
        sk_sp<SkImage> straightImage_;
        sk_sp<SkImage> premultipliedImage_;
        std::shared_ptr<SkiaResourceCounters> resourceCounters_;
        bool resourceRegistered_ = false;
    };
} // namespace CNA::Internal::Backends::Skia
