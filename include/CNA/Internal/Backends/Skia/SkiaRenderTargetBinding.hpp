#pragma once

namespace CNA::Internal::Backends::Skia
{
    class SkiaRenderTargetBackend;
    class SkiaSurface;

    /**
     * Backend-owned record of the current raster destination.
     *
     * Render targets retain this only weakly.  That makes a target destructor safe after the
     * graphics backend has already gone away, while still allowing a target that dies while bound
     * to detach the raw active-surface reference before its SkSurface is released.
     */
    class SkiaRenderTargetBinding final
    {
    public:
        void SetBackbuffer(SkiaSurface* backbuffer) noexcept
        {
            backbuffer_ = backbuffer;
            if (activeTarget_ == nullptr)
                activeSurface_ = backbuffer_;
        }

        void Bind(SkiaRenderTargetBackend* target, SkiaSurface* surface) noexcept
        {
            activeTarget_ = target;
            activeSurface_ = surface;
        }

        void UnbindToBackbuffer() noexcept
        {
            activeTarget_ = nullptr;
            activeSurface_ = backbuffer_;
        }

        void Detach(const SkiaRenderTargetBackend* target) noexcept
        {
            if (activeTarget_ == target)
                UnbindToBackbuffer();
        }

        [[nodiscard]] SkiaSurface* ActiveSurface() const noexcept { return activeSurface_; }
        [[nodiscard]] SkiaSurface*& ActiveSurfaceRef() noexcept { return activeSurface_; }
        [[nodiscard]] const SkiaRenderTargetBackend* ActiveTarget() const noexcept { return activeTarget_; }

    private:
        SkiaSurface* backbuffer_ = nullptr;
        SkiaSurface* activeSurface_ = nullptr;
        const SkiaRenderTargetBackend* activeTarget_ = nullptr;
    };
} // namespace CNA::Internal::Backends::Skia
