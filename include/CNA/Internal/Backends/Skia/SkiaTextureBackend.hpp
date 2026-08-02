#pragma once

#include "../Common/IGraphicsBackend.hpp"
#include "CNA/Internal/Backends/Skia/SkiaImageSource.hpp"
#include "CNA/Internal/Backends/Skia/SkiaMipChain2D.hpp"
#include "CNA/Internal/Backends/Skia/SkiaResourceCounters.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

#include "include/core/SkImage.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace CNA::Internal::Backends::Skia
{
    /**
     * CPU-backed Texture2D mip storage for the raster Skia backend.
     *
     * mipChain_ stays in CNA's exact top-row-first public transfer layout. Direct Skia colour types
     * label Color, Bgr565 and Rgba1010102 storage; Bgra4444 keeps exact A:R:G:B words and uses a
     * bounded RGBA working conversion. The active BlendState selects the source-alpha-labelled
     * view without rewriting public data. Every level supports exact CPU upload/readback and
     * deterministic native-precision generation.
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
        [[nodiscard]] bool HasDefinedMipLevel(int level) const noexcept override
        {
            return mipChain_ && level >= 0 && level < mipChain_->LevelCount();
        }
        [[nodiscard]] bool GetData(int level, int x, int y, int width, int height,
                                   void* data, int dataLength) const override;

        [[nodiscard]] SkiaSourceStorageAlpha StorageAlphaEXT() const noexcept override
        {
            return SkiaSourceStorageAlpha::CanonicalRgbaBytes;
        }
        [[nodiscard]] sk_sp<SkImage> SnapshotImage(
            SkiaSourceAlphaConvention alphaConvention) const override;
        [[nodiscard]] sk_sp<SkImage> SnapshotMipLevelEXT(
            int level, SkiaSourceAlphaConvention alphaConvention) const override;

        NOXNA [[nodiscard]] int MipLevelCountEXT() const noexcept override
        {
            return mipChain_ ? mipChain_->LevelCount() : 0;
        }
        NOXNA [[nodiscard]] const SkiaMipChain2D& MipChainEXT() const noexcept
        {
            return *mipChain_;
        }
        NOXNA [[nodiscard]] std::uint64_t MipGenerationCountEXT(int level) const;
        NOXNA [[nodiscard]] Microsoft::Xna::Framework::Graphics::SurfaceFormat FormatEXT() const
            noexcept
        {
            return format_;
        }

    private:
        void RebuildImage();
        void InvalidateGeneratedDescendants(int level);
        void GenerateDirtyMipLevels();
        void GenerateMipLevel(int level);

        int width_ = 0;
        int height_ = 0;
        Microsoft::Xna::Framework::Graphics::SurfaceFormat format_ =
            Microsoft::Xna::Framework::Graphics::SurfaceFormat::Color;
        std::size_t bytesPerTexel_ = 4u;
        std::size_t imageViewStorageBytes_ = 0u;
        std::unique_ptr<SkiaMipChain2D> mipChain_;
        std::vector<bool> authoredMipLevels_;
        std::vector<bool> dirtyMipLevels_;
        std::vector<std::uint64_t> mipGenerationCounts_;
        sk_sp<SkImage> straightImage_;
        sk_sp<SkImage> premultipliedImage_;
        std::shared_ptr<SkiaResourceCounters> resourceCounters_;
        bool resourceRegistered_ = false;
    };
} // namespace CNA::Internal::Backends::Skia
