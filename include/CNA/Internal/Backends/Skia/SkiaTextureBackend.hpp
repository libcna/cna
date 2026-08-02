#pragma once

#include "../Common/IGraphicsBackend.hpp"
#include "CNA/Internal/Backends/Skia/SkiaImageSource.hpp"
#include "CNA/Internal/Backends/Skia/SkiaMipChain2D.hpp"
#include "CNA/Internal/Backends/Skia/SkiaResourceCounters.hpp"

#include "include/core/SkImage.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace CNA::Internal::Backends::Skia
{
    /**
     * CPU-backed Texture2D mip storage for the raster Skia backend.
     *
     * mipChain_ stays in CNA's top-row-first RGBA8 convention. The two level-0 Skia snapshots label
     * those bytes for the two XNA source-alpha conventions; the active BlendState selects one at
     * draw time without rewriting public data. Higher-level transfer and sampling are staged by
     * SKIA-127–129 after SKIA-126's complete zeroed allocation/property contract.
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

        NOXNA [[nodiscard]] int MipLevelCountEXT() const noexcept
        {
            return mipChain_ ? mipChain_->LevelCount() : 0;
        }
        NOXNA [[nodiscard]] const SkiaMipChain2D& MipChainEXT() const noexcept
        {
            return *mipChain_;
        }

    private:
        void RebuildImage();

        int width_ = 0;
        int height_ = 0;
        std::unique_ptr<SkiaMipChain2D> mipChain_;
        sk_sp<SkImage> straightImage_;
        sk_sp<SkImage> premultipliedImage_;
        std::shared_ptr<SkiaResourceCounters> resourceCounters_;
        bool resourceRegistered_ = false;
    };
} // namespace CNA::Internal::Backends::Skia
