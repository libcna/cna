#include "CNA/Internal/Backends/Skia/SkiaRenderTargetBackend.hpp"
#include "CNA/Internal/Backends/Skia/SkiaResourcePolicy.hpp"

#include "System/NotSupportedException.hpp"

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace CNA::Internal::Backends::Skia
{
    SkiaRenderTargetBackend::SkiaRenderTargetBackend(int width, int height, bool preserveContents,
                                                     std::weak_ptr<SkiaRenderTargetBinding> binding,
                                                     std::shared_ptr<SkiaResourceCounters> resourceCounters,
                                                     bool mipMap)
        : preserveContents_(preserveContents)
        , binding_(std::move(binding))
        , resourceCounters_(std::move(resourceCounters))
    {
        std::vector<SkiaMipLevel2D> layout;
        SkiaMipChain2DLayoutError layoutError = SkiaMipChain2DLayoutError::None;
        std::size_t chainBytes = 0u;
        if (!TryBuildSkiaMipChain2DLayout(
                width, height, mipMap, 4u, layout, chainBytes, layoutError))
        {
            throw System::NotSupportedException(
                "Skia RenderTarget2D cannot allocate the requested checked RGBA8 mip chain.");
        }

        // Every level owns both a premultiplied SkSurface and an exact straight-RGBA transfer
        // shadow. Preflight their complete retained size before either representation allocates.
        std::size_t retainedBytes = 0u;
        if (!CheckedSizeMultiply(chainBytes, 2u, retainedBytes)
            || retainedBytes > kSkiaCpuTextureStorageLimitBytes)
        {
            throw System::NotSupportedException(
                "Skia RenderTarget2D surfaces and exact mip shadows exceed the checked "
                "256 MiB per-resource limit.");
        }

        mipChain_ = std::make_unique<SkiaMipChain2D>(
            width, height, mipMap, 4u, resourceCounters_);
        surfaceStorageBytes_ = chainBytes;
        surfaces_.reserve(layout.size());
        for (const SkiaMipLevel2D& level : layout)
        {
            auto surface = std::make_unique<SkiaSurface>(level.width, level.height);
            surface->Clear(0.0f, 0.0f, 0.0f, 0.0f);
            surfaces_.push_back(std::move(surface));
        }
        if (resourceCounters_)
        {
            resourceCounters_->AddRenderTarget(surfaceStorageBytes_);
            resourceRegistered_ = true;
        }
    }

    SkiaRenderTargetBackend::~SkiaRenderTargetBackend()
    {
        // `RenderTarget2D::Dispose()` rejects an explicitly disposed bound target, but a C++
        // destructor cannot throw. Detach here before the level surfaces are released so the next
        // Skia clear or SpriteBatch draw never observes a dangling active-surface pointer. The weak
        // binding intentionally expires when the graphics backend has already been destroyed.
        if (const auto binding = binding_.lock())
            binding->Detach(this);
        InvalidateSnapshot();
        if (resourceRegistered_)
            resourceCounters_->RemoveRenderTarget(surfaceStorageBytes_);
    }

    void SkiaRenderTargetBackend::UpdatePixels(const std::uint8_t* rgba, int stride)
    {
        if (!rgba || stride < GetWidth() * 4)
            throw std::runtime_error("Skia RenderTarget2D received an invalid level-0 RGBA upload.");
        if (!Surface().WritePixels(0, 0, GetWidth(), GetHeight(), rgba, stride))
            throw std::runtime_error("Skia RenderTarget2D failed to replace level-0 RGBA pixels.");
        for (int row = 0; row < GetHeight(); ++row)
        {
            std::memcpy(mipChain_->LevelData(0) + static_cast<std::size_t>(row) * GetWidth() * 4u,
                        rgba + static_cast<std::size_t>(row) * stride,
                        static_cast<std::size_t>(GetWidth()) * 4u);
        }
        levelZeroDirty_ = false;
        InvalidateSnapshot(0);
    }

    void SkiaRenderTargetBackend::UpdatePixelsLevel(
        int level, const std::uint8_t* rgba, int levelWidth, int levelHeight)
    {
        if (level == 0)
        {
            if (levelWidth != GetWidth() || levelHeight != GetHeight())
            {
                throw std::runtime_error(
                    "Skia RenderTarget2D level-zero upload dimensions do not match the target.");
            }
            UpdatePixels(rgba, levelWidth * 4);
            return;
        }
        SkiaSurface* surface = LevelSurface(level);
        if (!surface)
            throw std::out_of_range("Skia RenderTarget2D mip upload level is outside the chain.");
        if (!rgba)
            throw std::runtime_error("Skia RenderTarget2D mip upload received null RGBA data.");
        const SkiaMipLevel2D& target = mipChain_->Level(level);
        if (levelWidth != target.width || levelHeight != target.height)
        {
            throw std::runtime_error(
                "Skia RenderTarget2D mip upload dimensions do not match the requested level.");
        }
        if (!surface->WritePixels(0, 0, target.width, target.height,
                                  rgba, target.width * 4))
        {
            throw std::runtime_error("Skia RenderTarget2D failed to replace mip RGBA pixels.");
        }
        std::memcpy(mipChain_->LevelData(level), rgba, target.bytes);
        InvalidateSnapshot(level);
    }

    bool SkiaRenderTargetBackend::GetData(int level, int x, int y, int width, int height,
                                          void* data, int dataLength) const
    {
        std::size_t requiredBytes = 0;
        const SkiaSurface* surface = LevelSurface(level);
        if (!surface || data == nullptr || width <= 0 || height <= 0
            || x < 0 || y < 0 || width > surface->Width() || height > surface->Height()
            || x > surface->Width() - width || y > surface->Height() - height
            || !CheckedTexelBytes2D(static_cast<std::size_t>(width),
                                    static_cast<std::size_t>(height), 4u, requiredBytes)
            || dataLength < 0 || static_cast<std::size_t>(dataLength) < requiredBytes)
        {
            return false;
        }
        if (level == 0 && levelZeroDirty_)
            const_cast<SkiaRenderTargetBackend*>(this)->SynchronizeRenderedLevelZero();

        const SkiaMipLevel2D& source = mipChain_->Level(level);
        auto* destination = static_cast<std::uint8_t*>(data);
        for (int row = 0; row < height; ++row)
        {
            const std::size_t sourceOffset =
                (static_cast<std::size_t>(y + row) * source.width + x) * 4u;
            std::memcpy(destination + static_cast<std::size_t>(row) * width * 4u,
                        mipChain_->LevelData(level) + sourceOffset,
                        static_cast<std::size_t>(width) * 4u);
        }
        return true;
    }

    void SkiaRenderTargetBackend::BindAsRenderTarget()
    {
        // IGraphicsBackend::SetRenderTarget2D owns the active-surface switch. This method exists
        // solely for the common backend interface; it must not select an unrelated global target.
    }

    void SkiaRenderTargetBackend::UnbindAsRenderTarget()
    {
        // See BindAsRenderTarget(). Raster surfaces require no resolve or deferred submission.
    }

    sk_sp<SkImage> SkiaRenderTargetBackend::SnapshotImage(
        SkiaSourceAlphaConvention alphaConvention) const
    {
        return SnapshotMipLevelEXT(0, alphaConvention);
    }

    sk_sp<SkImage> SkiaRenderTargetBackend::SnapshotMipLevelEXT(
        int level, SkiaSourceAlphaConvention alphaConvention) const
    {
        if (ResolveSkiaWorkingSourceRoute(StorageAlphaEXT(), alphaConvention)
            != SkiaWorkingSourceRoute::ReusePremultipliedSurface)
        {
            return nullptr;
        }
        const SkiaSurface* surface = LevelSurface(level);
        if (!surface)
            return nullptr;
        // Keep exactly one immutable target snapshot across the complete chain. Mip-linear draws
        // retain the first returned image locally while this cache switches to the adjacent one,
        // so the backend cannot grow one retained cache entry per level.
        if (!snapshot_ || snapshotLevel_ != level)
        {
            const_cast<SkiaRenderTargetBackend*>(this)->InvalidateSnapshot();
            snapshot_ = surface->SnapshotImage();
            snapshotLevel_ = snapshot_ ? level : -1;
            if (snapshot_ && resourceCounters_)
                resourceCounters_->AddTargetSnapshot(surface->Width(), surface->Height());
        }
        return snapshot_;
    }

    void SkiaRenderTargetBackend::BeforeWriteEXT() noexcept
    {
        levelZeroDirty_ = true;
        InvalidateSnapshot(0);
    }

    void SkiaRenderTargetBackend::FinalizeWriteEXT()
    {
        SynchronizeRenderedLevelZero();
    }

    void SkiaRenderTargetBackend::PrepareForBind()
    {
        if (!preserveContents_)
        {
            BeforeWriteEXT();
            Surface().Clear(0.0f, 0.0f, 0.0f, 0.0f);
        }
    }

    SkiaSurface* SkiaRenderTargetBackend::LevelSurface(int level) noexcept
    {
        return level >= 0 && level < static_cast<int>(surfaces_.size())
            ? surfaces_[static_cast<std::size_t>(level)].get() : nullptr;
    }

    const SkiaSurface* SkiaRenderTargetBackend::LevelSurface(int level) const noexcept
    {
        return level >= 0 && level < static_cast<int>(surfaces_.size())
            ? surfaces_[static_cast<std::size_t>(level)].get() : nullptr;
    }

    void SkiaRenderTargetBackend::SynchronizeRenderedLevelZero()
    {
        if (!levelZeroDirty_)
            return;
        const SkiaMipLevel2D& levelZero = mipChain_->Level(0);
        if (!Surface().ReadPixels(0, 0, levelZero.width, levelZero.height,
                                  mipChain_->LevelData(0),
                                  static_cast<int>(levelZero.rowBytes)))
        {
            throw std::runtime_error(
                "Skia RenderTarget2D failed to synchronize rendered level-zero pixels.");
        }
        levelZeroDirty_ = false;
    }

    void SkiaRenderTargetBackend::InvalidateSnapshot(int level) noexcept
    {
        if (snapshotLevel_ == level)
            InvalidateSnapshot();
    }

    void SkiaRenderTargetBackend::InvalidateSnapshot() noexcept
    {
        if (!snapshot_)
            return;
        const SkiaSurface* surface = LevelSurface(snapshotLevel_);
        snapshot_.reset();
        if (resourceCounters_)
        {
            if (surface)
                resourceCounters_->RemoveTargetSnapshot(surface->Width(), surface->Height());
        }
        snapshotLevel_ = -1;
    }
} // namespace CNA::Internal::Backends::Skia
