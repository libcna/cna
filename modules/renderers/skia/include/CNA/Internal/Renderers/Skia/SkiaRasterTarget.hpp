#pragma once

namespace CNA::Internal::Renderers::Skia
{
    class SkiaSurface;

    /** Common lifetime/write boundary for every raster surface that can become the active target. */
    class SkiaRasterTarget
    {
    public:
        virtual ~SkiaRasterTarget() = default;

        [[nodiscard]] virtual SkiaSurface* BoundSurfaceEXT() noexcept = 0;
        [[nodiscard]] virtual const SkiaSurface* BoundSurfaceEXT() const noexcept = 0;
        /** Called before Clear/SpriteBatch mutates the selected surface. */
        virtual void BeforeWriteEXT() noexcept = 0;
        /** Called before switching away; cube targets use it to regenerate authored mip levels. */
        virtual void FinalizeWriteEXT() = 0;
    };
} // namespace CNA::Internal::Renderers::Skia
