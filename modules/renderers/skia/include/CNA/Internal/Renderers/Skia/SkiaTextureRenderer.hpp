#pragma once

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/Skia/SkiaCompressedMipChain2D.hpp"
#include "CNA/Internal/Renderers/Skia/SkiaImageSource.hpp"
#include "CNA/Internal/Renderers/Skia/SkiaMipChain2D.hpp"
#include "CNA/Internal/Renderers/Skia/SkiaResourceCounters.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

#include "include/core/SkImage.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace CNA::Internal::Renderers::Skia
{
    /**
     * CPU-backed Texture2D mip storage for the raster Skia renderer.
     *
     * mipChain_ stays in CNA's exact top-row-first public transfer layout. Direct Skia colour types
     * label Color, Bgr565, Rgba1010102, Rg32, Rgba64, Alpha8, ColorBgraEXT, ColorSrgbEXT,
     * ByteEXT, UShortEXT, Vector4 and the half-float formats. Bgra4444 keeps exact A:R:G:B words
     * and uses a bounded RGBA8 working conversion. Bgra5551 and NormalizedByte2/4 retain exact
     * packed/SNORM words while using bounded RGBA32F working copies; Single and Vector2 likewise
     * retain exact IEEE transfer words and expand missing channels into RGBA32F. The sRGB colour
     * type decodes once into an explicitly linear-sRGB working space. Dxt1/Dxt3/Dxt5 retain their
     * exact compressed CPU blocks in a separate padded-block chain and expose a bounded decoded
     * RGBA8 sampling image; unlike every other format their descendant mip levels are never
     * generated and must be explicitly authored. The active BlendState selects the source-alpha-
     * labelled view without rewriting public data. Every level supports exact CPU upload/readback
     * and deterministic format-appropriate generation, except the compressed formats noted above.
     */
    class SkiaTextureRenderer final : public ITextureRenderer, public SkiaImageSource
    {
    public:
        explicit SkiaTextureRenderer(const ImageData& data,
                                    std::shared_ptr<SkiaResourceCounters> resourceCounters = {});
        ~SkiaTextureRenderer() override;

        [[nodiscard]] int GetWidth() const override { return width_; }
        [[nodiscard]] int GetHeight() const override { return height_; }

        void UpdatePixels(const std::uint8_t* rgba, int stride) override;
        void UpdatePixelsLevel(int level, const std::uint8_t* rgba, int levelWidth, int levelHeight) override;
        [[nodiscard]] bool HasDefinedMipLevel(int level) const noexcept override
        {
            if (compressedChain_)
                return level >= 0 && level < compressedChain_->LevelCount();
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

        CNAEXT [[nodiscard]] int MipLevelCountEXT() const noexcept override
        {
            if (compressedChain_) return compressedChain_->LevelCount();
            return mipChain_ ? mipChain_->LevelCount() : 0;
        }
        CNAEXT [[nodiscard]] const SkiaMipChain2D& MipChainEXT() const noexcept
        {
            return *mipChain_;
        }
        /** Block-compressed counterpart of MipChainEXT(); only valid for Dxt1/Dxt3/Dxt5. */
        CNAEXT [[nodiscard]] const SkiaCompressedMipChain2D& CompressedMipChainEXT() const noexcept
        {
            return *compressedChain_;
        }
        CNAEXT [[nodiscard]] std::uint64_t MipGenerationCountEXT(int level) const;
        CNAEXT [[nodiscard]] Microsoft::Xna::Framework::Graphics::SurfaceFormat FormatEXT() const
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
        std::unique_ptr<SkiaCompressedMipChain2D> compressedChain_;
        std::vector<bool> authoredMipLevels_;
        std::vector<bool> dirtyMipLevels_;
        std::vector<std::uint64_t> mipGenerationCounts_;
        sk_sp<SkImage> straightImage_;
        sk_sp<SkImage> premultipliedImage_;
        std::shared_ptr<SkiaResourceCounters> resourceCounters_;
        bool resourceRegistered_ = false;
    };
} // namespace CNA::Internal::Renderers::Skia
