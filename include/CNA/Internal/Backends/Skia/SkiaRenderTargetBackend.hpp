#pragma once

#include "../Common/IGraphicsBackend.hpp"
#include "CNA/Internal/Backends/Skia/SkiaImageSource.hpp"
#include "CNA/Internal/Backends/Skia/SkiaSurface.hpp"

namespace CNA::Internal::Backends::Skia
{
    /** CPU raster off-screen target that can be sampled as a Texture2D after it is unbound. */
    class SkiaRenderTargetBackend final : public IRenderTargetBackend, public SkiaImageSource
    {
    public:
        SkiaRenderTargetBackend(int width, int height, bool preserveContents);

        [[nodiscard]] int GetWidth() const override { return surface_.Width(); }
        [[nodiscard]] int GetHeight() const override { return surface_.Height(); }
        [[nodiscard]] SDL_Texture* GetNativeTexture() const override { return nullptr; }
        void UpdatePixels(const std::uint8_t* rgba, int stride) override;
        [[nodiscard]] bool GetData(int level, int x, int y, int width, int height,
                                   void* data, int dataLength) const override;
        void BindAsRenderTarget() override;
        void UnbindAsRenderTarget() override;
        [[nodiscard]] bool HasRealDepthBuffer(bool) const override { return false; }
        [[nodiscard]] sk_sp<SkImage> SnapshotImage() const override;

        [[nodiscard]] SkiaSurface& Surface() noexcept { return surface_; }
        [[nodiscard]] const SkiaSurface& Surface() const noexcept { return surface_; }
        void PrepareForBind();

    private:
        SkiaSurface surface_;
        bool preserveContents_ = false;
    };
} // namespace CNA::Internal::Backends::Skia
