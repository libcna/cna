#pragma once

#include "CNA/Internal/Renderers/Skia/SkiaGaneshContext.hpp"

#include <cstdint>
#include <memory>
#include <optional>

class SkCanvas;

namespace CNA::Internal::Renderers::Skia
{
    /// SKIA-161: wraps SkiaGaneshContext's real GrDirectContext around the platform default
    /// OpenGL framebuffer (FBO 0), producing a real, presentable, read-backable SkSurface -- the
    /// piece SkiaGaneshContext (SKIA-160) explicitly deferred. SKIA-162 added
    /// DebugSimulateContextLossEXT(), a genuine GL context destroy+recreate (mirroring
    /// EasyGLRenderer::DebugSimulateContextLoss()'s own established real recreate, not a
    /// simulated/faked one), proving the reconstruction path leaves no stale GPU object behind.
    /// Deliberately still not an `IGraphicsRenderer`: `Resize()`/loss-recovery here are mechanisms
    /// the caller must invoke, not an automatic reaction to a window/device event, and there is no
    /// wiring into `SpriteBatch`/`GraphicsDevice`. Also deliberately out of scope: any
    /// `CnaPresentationMode`-equivalent virtual-resolution/letterbox/overscan coordinate mapping
    /// (this class always uses the window's raw drawable pixels 1:1) and any cross-resource
    /// synchronization (no textures/targets/effects exist in the Ganesh path yet to synchronize
    /// against) -- both remain real, open, un-vacuous scope once `IGraphicsRenderer` integration
    /// actually happens (SKIA-163+), not attempted here. In `RASTER`-mode builds, construction
    /// always throws (inherited unconditionally from `SkiaGaneshContext`) before any Ganesh-only
    /// Skia symbol is referenced.
    ///
    /// Uses only the real default framebuffer (`kBottomLeft_GrSurfaceOrigin`, FBO id 0) -- never
    /// an off-screen FBO-based render target, which remains out of this task's scope. This class
    /// never includes or links anything from `src/CNA/Internal/Renderers/EasyGL/`; its entire
    /// drawing path is Skia's own `SkCanvas` over the wrapped Ganesh surface.
    class SkiaGaneshSurface final
    {
    public:
        /// @brief Constructs a real Ganesh context (via SkiaGaneshContext) and wraps the window's
        /// current default framebuffer as a real SkSurface, sized to the window's current
        /// drawable size in pixels. Throws std::runtime_error transactionally if any step fails,
        /// including every failure mode SkiaGaneshContext itself can throw.
        explicit SkiaGaneshSurface(const GraphicsRendererCreateArgs& args);

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
        void Resize(const RendererSurfaceInfo& surface);

        /// @brief Flushes and submits all pending Skia GPU work for this surface, then swaps the
        /// window's front/back buffers through the platform context service.
        void Present();

        /// @brief Reads back exact RGBA8 premultiplied pixels from the given rectangle into
        /// outRgba8 (row-major, top row first, 4 bytes per pixel, no padding). Returns false (no
        /// partial write) if the rectangle is out of the surface's current bounds or the
        /// underlying readback otherwise fails.
        [[nodiscard]] bool ReadPixels(int x, int y, int width, int height, std::uint8_t* outRgba8) const;

        /// @brief Destroys and reconstructs the underlying GL context, GrDirectContext, and
        /// wrapped surface from scratch, simulating recovery from a lost Ganesh/GL context. A real
        /// context loss cannot be safely forced on this platform (matching why
        /// SkiaRenderer/EasyGLRenderer's own debug loss simulations are also a real
        /// destroy+recreate cycle, not a forced fault); this proves the recreate path itself
        /// leaves a fully live, non-stale object behind. Throws std::runtime_error transactionally
        /// (same failure modes as the constructor) if reconstruction fails -- this instance must
        /// not be used afterward if it throws.
        void DebugSimulateContextLossEXT();

        /// @brief The wrapped surface's current width in pixels.
        [[nodiscard]] int Width() const noexcept { return width_; }

        /// @brief The wrapped surface's current height in pixels.
        [[nodiscard]] int Height() const noexcept { return height_; }

    private:
        void WrapBackbuffer(int width, int height);

        CNA::Platform::IPlatformGlContext* glService_ = nullptr;
        PlatformGlSurfaceState surface_;
        // Declared before impl_ so impl_ (the wrapped SkSurface, which depends on the GrDirectContext
        // context_ owns) is destroyed first -- members destruct in reverse declaration order.
        // std::optional (not a plain value) so DebugSimulateContextLossEXT() can destroy and
        // reconstruct it in place -- SkiaGaneshContext itself is a single-shot RAII object with no
        // reconstruct-in-place operation of its own, by design (SKIA-160).
        std::optional<SkiaGaneshContext> context_;

        struct Impl;
        std::unique_ptr<Impl> impl_;
        int width_ = 0;
        int height_ = 0;
    };
} // namespace CNA::Internal::Renderers::Skia
