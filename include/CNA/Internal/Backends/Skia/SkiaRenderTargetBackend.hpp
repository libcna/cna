#pragma once

#include "../Common/IGraphicsBackend.hpp"
#include "CNA/Internal/Backends/Skia/SkiaImageSource.hpp"
#include "CNA/Internal/Backends/Skia/SkiaMipChain2D.hpp"
#include "CNA/Internal/Backends/Skia/SkiaRasterTarget.hpp"
#include "CNA/Internal/Backends/Skia/SkiaRenderTargetBinding.hpp"
#include "CNA/Internal/Backends/Skia/SkiaResourceCounters.hpp"
#include "CNA/Internal/Backends/Skia/SkiaSurface.hpp"

#include <memory>
#include <vector>

namespace CNA::Internal::Backends::Skia
{
    /** CPU raster off-screen target with a bindable level zero and stable sampleable mip levels. */
    class SkiaRenderTargetBackend final : public IRenderTargetBackend,
                                          public SkiaImageSource,
                                          public SkiaRasterTarget
    {
    public:
        SkiaRenderTargetBackend(int width, int height, bool preserveContents,
                                std::weak_ptr<SkiaRenderTargetBinding> binding,
                                std::shared_ptr<SkiaResourceCounters> resourceCounters = {},
                                bool mipMap = false);
        ~SkiaRenderTargetBackend() override;

        [[nodiscard]] int GetWidth() const override { return Surface().Width(); }
        [[nodiscard]] int GetHeight() const override { return Surface().Height(); }
        [[nodiscard]] SDL_Texture* GetNativeTexture() const override { return nullptr; }
        void UpdatePixels(const std::uint8_t* rgba, int stride) override;
        void UpdatePixelsLevel(int level, const std::uint8_t* rgba,
                               int levelWidth, int levelHeight) override;
        [[nodiscard]] bool HasDefinedMipLevel(int level) const noexcept override
        {
            return mipChain_ && level >= 0 && level < mipChain_->LevelCount();
        }
        [[nodiscard]] bool GetData(int level, int x, int y, int width, int height,
                                   void* data, int dataLength) const override;
        void BindAsRenderTarget() override;
        void UnbindAsRenderTarget() override;
        [[nodiscard]] bool HasRealDepthBuffer(bool) const override { return false; }
        [[nodiscard]] SkiaSourceStorageAlpha StorageAlphaEXT() const noexcept override
        {
            return SkiaSourceStorageAlpha::PremultipliedSurface;
        }
        [[nodiscard]] sk_sp<SkImage> SnapshotImage(
            SkiaSourceAlphaConvention alphaConvention) const override;
        [[nodiscard]] sk_sp<SkImage> SnapshotMipLevelEXT(
            int level, SkiaSourceAlphaConvention alphaConvention) const override;
        [[nodiscard]] int MipLevelCountEXT() const noexcept override
        {
            return mipChain_ ? mipChain_->LevelCount() : 0;
        }

        [[nodiscard]] SkiaSurface& Surface() noexcept { return *surfaces_[0]; }
        [[nodiscard]] const SkiaSurface& Surface() const noexcept { return *surfaces_[0]; }
        [[nodiscard]] SkiaSurface* BoundSurfaceEXT() noexcept override { return &Surface(); }
        [[nodiscard]] const SkiaSurface* BoundSurfaceEXT() const noexcept override { return &Surface(); }
        void BeforeWriteEXT() noexcept override;
        void FinalizeWriteEXT() override;
        void PrepareForBind();
        /// Drops this target's one-entry sampling cache before a canvas or upload write.
        void InvalidateSnapshot() noexcept;

        NOXNA [[nodiscard]] std::size_t SurfaceStorageBytesEXT() const noexcept
        {
            return surfaceStorageBytes_;
        }
        NOXNA [[nodiscard]] const SkiaMipChain2D& MipChainEXT() const noexcept
        {
            return *mipChain_;
        }

    private:
        [[nodiscard]] SkiaSurface* LevelSurface(int level) noexcept;
        [[nodiscard]] const SkiaSurface* LevelSurface(int level) const noexcept;
        void SynchronizeRenderedLevelZero();
        void InvalidateSnapshot(int level) noexcept;

        std::unique_ptr<SkiaMipChain2D> mipChain_;
        std::vector<std::unique_ptr<SkiaSurface>> surfaces_;
        bool preserveContents_ = false;
        bool levelZeroDirty_ = false;
        std::size_t surfaceStorageBytes_ = 0u;
        std::weak_ptr<SkiaRenderTargetBinding> binding_;
        std::shared_ptr<SkiaResourceCounters> resourceCounters_;
        mutable sk_sp<SkImage> snapshot_;
        mutable int snapshotLevel_ = -1;
        bool resourceRegistered_ = false;
    };
} // namespace CNA::Internal::Backends::Skia
