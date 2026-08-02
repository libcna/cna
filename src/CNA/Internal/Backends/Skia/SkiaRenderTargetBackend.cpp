#include "CNA/Internal/Backends/Skia/SkiaRenderTargetBackend.hpp"
#include "CNA/Internal/Backends/Skia/SkiaResourcePolicy.hpp"

#include <cstdint>
#include <stdexcept>
#include <utility>

namespace CNA::Internal::Backends::Skia
{
    SkiaRenderTargetBackend::SkiaRenderTargetBackend(int width, int height, bool preserveContents,
                                                     std::weak_ptr<SkiaRenderTargetBinding> binding,
                                                     std::shared_ptr<SkiaResourceCounters> resourceCounters)
        : surface_(width, height)
        , preserveContents_(preserveContents)
        , binding_(std::move(binding))
        , resourceCounters_(std::move(resourceCounters))
    {
        surface_.Clear(0.0f, 0.0f, 0.0f, 0.0f);
        if (resourceCounters_)
            resourceCounters_->AddRenderTarget(width, height);
    }

    SkiaRenderTargetBackend::~SkiaRenderTargetBackend()
    {
        // `RenderTarget2D::Dispose()` rejects an explicitly disposed bound target, but a C++
        // destructor cannot throw.  Detach here before `surface_` is released so the next Skia
        // clear or SpriteBatch draw never observes a dangling active-surface pointer.  The weak
        // binding intentionally expires when the graphics backend has already been destroyed.
        if (const auto binding = binding_.lock())
            binding->Detach(this);
        InvalidateSnapshot();
        if (resourceCounters_)
            resourceCounters_->RemoveRenderTarget(GetWidth(), GetHeight());
    }

    void SkiaRenderTargetBackend::UpdatePixels(const std::uint8_t* rgba, int stride)
    {
        if (!surface_.WritePixels(0, 0, GetWidth(), GetHeight(), rgba, stride))
            throw std::runtime_error("Skia RenderTarget2D failed to replace level-0 RGBA pixels.");
        InvalidateSnapshot();
    }

    bool SkiaRenderTargetBackend::GetData(int level, int x, int y, int width, int height,
                                          void* data, int dataLength) const
    {
        std::size_t requiredBytes = 0;
        if (level != 0 || data == nullptr || width < 0 || height < 0
            || !CheckedTexelBytes2D(static_cast<std::size_t>(width),
                                    static_cast<std::size_t>(height), 4u, requiredBytes)
            || dataLength < 0 || static_cast<std::size_t>(dataLength) < requiredBytes)
        {
            return false;
        }
        return surface_.ReadPixels(x, y, width, height, static_cast<std::uint8_t*>(data), width * 4);
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
        if (ResolveSkiaWorkingSourceRoute(StorageAlphaEXT(), alphaConvention)
            != SkiaWorkingSourceRoute::ReusePremultipliedSurface)
        {
            return nullptr;
        }
        // An SkSurface snapshot is already premultiplied.  Keep only one immutable image per
        // target; any target write explicitly releases it before the canvas mutates, so callers
        // never sample stale pixels and repeated sprite draws cannot grow a hidden cache.
        if (!snapshot_)
        {
            snapshot_ = surface_.SnapshotImage();
            if (snapshot_ && resourceCounters_)
                resourceCounters_->AddTargetSnapshot(GetWidth(), GetHeight());
        }
        return snapshot_;
    }

    void SkiaRenderTargetBackend::PrepareForBind()
    {
        if (!preserveContents_)
        {
            InvalidateSnapshot();
            surface_.Clear(0.0f, 0.0f, 0.0f, 0.0f);
        }
    }

    void SkiaRenderTargetBackend::InvalidateSnapshot() noexcept
    {
        if (!snapshot_)
            return;
        snapshot_.reset();
        if (resourceCounters_)
            resourceCounters_->RemoveTargetSnapshot(GetWidth(), GetHeight());
    }
} // namespace CNA::Internal::Backends::Skia
