#pragma once

#include "CNA/Internal/Backends/Skia/SkiaGaneshContext.hpp"

#include <SDL3/SDL.h>

#include <cstdint>
#include <memory>

class SkCanvas;

namespace CNA::Internal::Backends::Skia
{
    /// SKIA-161: wraps SkiaGaneshContext's real GrDirectContext around the SDL-owned default
    /// OpenGL framebuffer (FBO 0), producing a real, presentable, read-backable SkSurface -- the
    /// piece SkiaGaneshContext (SKIA-160) explicitly deferred. Deliberately still not an
    /// `IGraphicsBackend`: no resize/loss/recovery *policy* (SKIA-162 -- `Resize()` here is a
    /// mechanism the caller must invoke, not an automatic reaction to a window event), and no
    /// wiring into `SpriteBatch`/`GraphicsDevice`. In `RASTER`-mode builds, construction always
    /// throws (inherited unconditionally from `SkiaGaneshContext`) before any Ganesh-only Skia
    /// symbol is referenced.
    ///
    /// Uses only the real default framebuffer (`kBottomLeft_GrSurfaceOrigin`, FBO id 0) -- never
    /// an off-screen FBO-based render target, which remains out of this task's scope. This class
    /// never includes or links anything from `src/CNA/Internal/Backends/EasyGL/`; its entire
    /// drawing path is Skia's own `SkCanvas` over the wrapped Ganesh surface.
    class SkiaGaneshSurface final
    {
    public:
        /// @brief Constructs a real Ganesh context (via SkiaGaneshContext) and wraps the window's
        /// current default framebuffer as a real SkSurface, sized to the window's current
        /// drawable size in pixels. Throws std::runtime_error transactionally if any step fails,
        /// including every failure mode SkiaGaneshContext itself can throw.
        explicit SkiaGaneshSurface(SDL_Window* window);

        /// @brief Releases the wrapped surface before the owned Ganesh context is released.
        /// The caller-owned window itself is untouched.
        ~SkiaGaneshSurface();

        SkiaGaneshSurface(const SkiaGaneshSurface&) = delete;
        SkiaGaneshSurface& operator=(const SkiaGaneshSurface&) = delete;
        SkiaGaneshSurface(SkiaGaneshSurface&&) = delete;
        SkiaGaneshSurface& operator=(SkiaGaneshSurface&&) = delete;

        /// @brief The real SkCanvas for the wrapped default framebuffer. Drawing through it is
        /// ordinary Skia 2D drawing, identical in API surface to the raster path's own SkCanvas
        /// usage -- only the destination surface differs.
        [[nodiscard]] SkCanvas* Canvas() const noexcept;

        /// @brief Rewraps the default framebuffer at its current drawable size. Must be called
        /// after the window resizes; the SkCanvas returned by a prior Canvas() call is invalid
        /// afterward. A no-op if the drawable size is unchanged.
        void Resize();

        /// @brief Flushes and submits all pending Skia GPU work for this surface, then swaps the
        /// window's front/back buffers via SDL_GL_SwapWindow.
        void Present();

        /// @brief Reads back exact RGBA8 premultiplied pixels from the given rectangle into
        /// outRgba8 (row-major, top row first, 4 bytes per pixel, no padding). Returns false (no
        /// partial write) if the rectangle is out of the surface's current bounds or the
        /// underlying readback otherwise fails.
        [[nodiscard]] bool ReadPixels(int x, int y, int width, int height, std::uint8_t* outRgba8) const;

        /// @brief The wrapped surface's current width in pixels.
        [[nodiscard]] int Width() const noexcept { return width_; }

        /// @brief The wrapped surface's current height in pixels.
        [[nodiscard]] int Height() const noexcept { return height_; }

    private:
        void WrapBackbuffer(int width, int height);

        SDL_Window* window_ = nullptr;
        // Declared before impl_ so impl_ (the wrapped SkSurface, which depends on the GrDirectContext
        // context_ owns) is destroyed first -- members destruct in reverse declaration order.
        SkiaGaneshContext context_;

        struct Impl;
        std::unique_ptr<Impl> impl_;
        int width_ = 0;
        int height_ = 0;
    };
} // namespace CNA::Internal::Backends::Skia
