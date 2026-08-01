#pragma once

#include "../Common/IGraphicsBackend.hpp"
#include "CNA/Internal/Backends/Skia/SkiaImageSource.hpp"
#include "CNA/Internal/Backends/Skia/SkiaRasterTarget.hpp"
#include "CNA/Internal/Backends/Skia/SkiaRenderTargetBinding.hpp"
#include "CNA/Internal/Backends/Skia/SkiaResourceCounters.hpp"
#include "CNA/Internal/Backends/Skia/SkiaSurface.hpp"

#include <memory>

namespace CNA::Internal::Backends::Skia
{
    /** CPU raster off-screen target that can be sampled as a Texture2D after it is unbound. */
    class SkiaRenderTargetBackend final : public IRenderTargetBackend,
                                          public SkiaImageSource,
                                          public SkiaRasterTarget
    {
    public:
        SkiaRenderTargetBackend(int width, int height, bool preserveContents,
                                std::weak_ptr<SkiaRenderTargetBinding> binding,
                                std::shared_ptr<SkiaResourceCounters> resourceCounters = {});
        ~SkiaRenderTargetBackend() override;

        [[nodiscard]] int GetWidth() const override { return surface_.Width(); }
        [[nodiscard]] int GetHeight() const override { return surface_.Height(); }
        [[nodiscard]] SDL_Texture* GetNativeTexture() const override { return nullptr; }
        void UpdatePixels(const std::uint8_t* rgba, int stride) override;
        [[nodiscard]] bool GetData(int level, int x, int y, int width, int height,
                                   void* data, int dataLength) const override;
        void BindAsRenderTarget() override;
        void UnbindAsRenderTarget() override;
        [[nodiscard]] bool HasRealDepthBuffer(bool) const override { return false; }
        [[nodiscard]] sk_sp<SkImage> SnapshotImage(
            SkiaSourceAlphaConvention alphaConvention) const override;

        [[nodiscard]] SkiaSurface& Surface() noexcept { return surface_; }
        [[nodiscard]] const SkiaSurface& Surface() const noexcept { return surface_; }
        [[nodiscard]] SkiaSurface* BoundSurfaceEXT() noexcept override { return &surface_; }
        [[nodiscard]] const SkiaSurface* BoundSurfaceEXT() const noexcept override { return &surface_; }
        void BeforeWriteEXT() noexcept override { InvalidateSnapshot(); }
        void FinalizeWriteEXT() override {}
        void PrepareForBind();
        /// Drops this target's one-entry sampling cache before a canvas or upload write.
        void InvalidateSnapshot() noexcept;

    private:
        SkiaSurface surface_;
        bool preserveContents_ = false;
        std::weak_ptr<SkiaRenderTargetBinding> binding_;
        std::shared_ptr<SkiaResourceCounters> resourceCounters_;
        mutable sk_sp<SkImage> snapshot_;
    };
} // namespace CNA::Internal::Backends::Skia
