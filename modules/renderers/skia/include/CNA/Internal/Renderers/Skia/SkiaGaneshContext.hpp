#pragma once

#include "CNA/Internal/Renderers/Common/PlatformGlRendererState.hpp"

#include <memory>
#include <string>

class GrDirectContext;

namespace CNA::Internal::Renderers::Skia
{
    /// SKIA-160's construction-time Ganesh/OpenGL mode selector and diagnostic. This is
    /// deliberately not an `IGraphicsRenderer` implementation and does not wrap a presentable
    /// backbuffer -- that is SKIA-161's job (`docs/skia-ganesh-artifact.md`,
    /// `docs/skia-surface-mode-adr.md`). It exists to make one thing testable in isolation: does
    /// this build, on this machine, genuinely construct a working Ganesh `GrDirectContext`, or
    /// does it fail -- with no silent fallback either way.
    ///
    /// Whether this class can succeed is fixed at CMake configure time by `CNA_SKIA_MODE`
    /// (`RASTER`, the default, or `GANESH`), which selects which of the two mutually exclusive
    /// pinned Skia artifacts (`CNA::Skia` vs `CNA::SkiaGanesh`) this build links. In a `RASTER`
    /// build the constructor always throws immediately, without touching the platform or GL --
    /// requesting Ganesh mode in a build that was not configured for it is a deterministic,
    /// display-independent refusal, not a silent no-op. In a `GANESH` build the constructor
    /// performs the real sequence SKIA-159's probe already proved works (a platform GL context,
    /// made current, handed to `GrDirectContexts::MakeGL()`) and throws
    /// `std::runtime_error` if any step fails, leaving no partially constructed object and no
    /// leaked native GL resource.
    class SkiaGaneshContext final
    {
    public:
        /// @brief Attempts to construct a real Ganesh context on the given platform window.
        /// Throws std::runtime_error transactionally (no partial object, no leaked resource) if
        /// this build was not configured with -DCNA_SKIA_MODE=GANESH, or if platform/GL/Ganesh
        /// initialization fails for any reason on this machine.
        SkiaGaneshContext(CNA::Platform::IPlatformGlContext* service,
                          CNA::Platform::WindowId window);

        /// @brief Releases the owned GL context. The caller-owned window itself is untouched,
        /// matching SkiaRenderer's own window-ownership convention.
        ~SkiaGaneshContext();

        SkiaGaneshContext(const SkiaGaneshContext&) = delete;
        SkiaGaneshContext& operator=(const SkiaGaneshContext&) = delete;
        SkiaGaneshContext(SkiaGaneshContext&&) = delete;
        SkiaGaneshContext& operator=(SkiaGaneshContext&&) = delete;

        /// @brief Stable single-line startup capability report for this constructed Ganesh
        /// context. Unlike the raster path's compile-time constant diagnostic
        /// (`kSkiaStartupDiagnostic`), this interpolates values genuinely queried from the
        /// device at construction time (e.g. max texture size), since they are driver-dependent
        /// rather than a fixed fact about the pinned raster implementation.
        [[nodiscard]] const std::string& StartupDiagnostic() const noexcept { return startupDiagnostic_; }

        /// @brief The real, driver-reported maximum texture dimension for this Ganesh context.
        [[nodiscard]] int MaxTextureSize() const noexcept { return maxTextureSize_; }

        /// @brief The real GrDirectContext this instance owns, for SKIA-161's
        /// SkiaGaneshSurface to wrap a renderer render target with. Never null on a successfully
        /// constructed instance (construction throws otherwise); the type is only forward-
        /// declared here, matching this header's mode-agnostic, Ganesh-include-free design.
        [[nodiscard]] GrDirectContext* NativeContextEXT() const noexcept;
        /** @brief Presents the platform window associated with this Ganesh context. */
        void SwapBuffers();

    private:
        // The real sk_sp<GrDirectContext> lives behind this Pimpl (defined only in the .cpp) so
        // this header, included from mode-agnostic call sites, never needs a Ganesh-specific
        // Skia include.
        struct Impl;

        std::unique_ptr<PlatformGlContextOwner> platformContext_;
        std::unique_ptr<Impl> impl_;
        int maxTextureSize_ = 0;
        std::string startupDiagnostic_;
    };
} // namespace CNA::Internal::Renderers::Skia
