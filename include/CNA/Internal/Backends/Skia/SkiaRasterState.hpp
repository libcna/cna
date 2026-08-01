#pragma once

namespace CNA::Internal::Backends::Skia
{
    /**
     * Live 2D raster state owned by SkiaGraphicsBackend and observed at immediate SpriteBatch
     * submission.  GraphicsDevice resets the rectangle after every render-target switch, so its
     * coordinates are always relative to the currently active SkSurface.
     */
    struct SkiaRasterState
    {
        bool viewportSet = false;
        int viewportX = 0;
        int viewportY = 0;
        int viewportWidth = 0;
        int viewportHeight = 0;
        bool scissorTestEnabled = false;
        int scissorX = 0;
        int scissorY = 0;
        int scissorWidth = 0;
        int scissorHeight = 0;
    };
} // namespace CNA::Internal::Backends::Skia
