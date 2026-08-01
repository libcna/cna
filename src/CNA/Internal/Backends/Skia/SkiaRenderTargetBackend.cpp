#include "CNA/Internal/Backends/Skia/SkiaRenderTargetBackend.hpp"

#include <cstdint>
#include <stdexcept>

namespace CNA::Internal::Backends::Skia
{
    SkiaRenderTargetBackend::SkiaRenderTargetBackend(int width, int height, bool preserveContents)
        : surface_(width, height)
        , preserveContents_(preserveContents)
    {
        surface_.Clear(0.0f, 0.0f, 0.0f, 0.0f);
    }

    void SkiaRenderTargetBackend::UpdatePixels(const std::uint8_t* rgba, int stride)
    {
        if (!surface_.WritePixels(0, 0, GetWidth(), GetHeight(), rgba, stride))
            throw std::runtime_error("Skia RenderTarget2D failed to replace level-0 RGBA pixels.");
    }

    bool SkiaRenderTargetBackend::GetData(int level, int x, int y, int width, int height,
                                          void* data, int dataLength) const
    {
        if (level != 0 || data == nullptr || width < 0 || height < 0
            || dataLength < width * height * 4)
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
        SkiaSourceAlphaConvention /*alphaConvention*/) const
    {
        // An SkSurface snapshot is already premultiplied.  Render-target source-convention
        // conversion needs its own explicit probe before it can claim straight-alpha support.
        return surface_.SnapshotImage();
    }

    void SkiaRenderTargetBackend::PrepareForBind()
    {
        if (!preserveContents_)
            surface_.Clear(0.0f, 0.0f, 0.0f, 0.0f);
    }
} // namespace CNA::Internal::Backends::Skia
