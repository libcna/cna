#include "CNA/Internal/Renderers/Skia/SkiaGaneshContext.hpp"

#include "CNA/Internal/Renderers/Skia/SkiaStartupDiagnostic.hpp"

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#if defined(CNA_SKIA_MODE_GANESH)
#include "include/gpu/ganesh/GrDirectContext.h"
#include "include/gpu/ganesh/gl/GrGLDirectContext.h"

#include <cstdio>
#endif

namespace CNA::Internal::Renderers::Skia
{
#if defined(CNA_SKIA_MODE_GANESH)
    struct SkiaGaneshContext::Impl
    {
        sk_sp<GrDirectContext> grContext;
    };

    SkiaGaneshContext::SkiaGaneshContext(SDL_Window* window)
        : window_(window)
    {
        if (!window_)
            throw std::runtime_error("SkiaGaneshContext initialized with null window.");

        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
        // SKIA-161: matches EasyGL's own established precedent (SkiaRenderer has no GL
        // context of its own to compare against). Without this, SkiaGaneshSurface's
        // GrBackendRenderTargets::MakeGL wrap would see 0 stencil bits, silently disabling
        // stencil-based SkCanvas clip paths that raster's software clipper never needed to skip.
        SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

        glContext_ = SDL_GL_CreateContext(window_);
        if (!glContext_)
            throw std::runtime_error(std::string("SkiaGaneshContext SDL_GL_CreateContext failed: ") + SDL_GetError());

        try
        {
            if (!SDL_GL_MakeCurrent(window_, glContext_))
                throw std::runtime_error(std::string("SkiaGaneshContext SDL_GL_MakeCurrent failed: ") + SDL_GetError());

            impl_ = std::make_unique<Impl>();
            impl_->grContext = GrDirectContexts::MakeGL();
            if (!impl_->grContext)
            {
                throw std::runtime_error(
                    "SkiaGaneshContext: GrDirectContexts::MakeGL() returned null -- the Ganesh "
                    "artifact linked, but Skia's GL native interface could not be assembled over "
                    "this GL context.");
            }

            maxTextureSize_ = impl_->grContext->maxTextureSize();
            if (maxTextureSize_ <= 0)
            {
                throw std::runtime_error(
                    "SkiaGaneshContext: GrDirectContext reported a non-positive maxTextureSize.");
            }

            char diagnostic[256];
            std::snprintf(diagnostic, sizeof(diagnostic),
                "CNA: Skia capabilities -- revision=%s; surface=ganesh-gl; "
                "colour=RGBA_8888/premultiplied; max-texture-size=%d (queried); "
                "abandoned=%d",
                std::string(kSkiaPinnedRevision).c_str(), maxTextureSize_,
                impl_->grContext->abandoned() ? 1 : 0);
            startupDiagnostic_ = diagnostic;
        }
        catch (...)
        {
            impl_.reset();
            SDL_GL_DestroyContext(glContext_);
            glContext_ = nullptr;
            throw;
        }
    }

    SkiaGaneshContext::~SkiaGaneshContext()
    {
        if (impl_ && impl_->grContext)
            impl_->grContext->abandonContext();
        impl_.reset();
        if (glContext_)
            SDL_GL_DestroyContext(glContext_);
    }

    GrDirectContext* SkiaGaneshContext::NativeContextEXT() const noexcept
    {
        return impl_ ? impl_->grContext.get() : nullptr;
    }
#else
    // No SkiaGaneshContext::Impl definition exists in a RASTER-mode build: this build links
    // CNA::Skia (raster-only archives, skia_enable_ganesh=false), which contains no Ganesh/GL
    // object code at all. Requesting Ganesh mode here is therefore a deterministic,
    // display-independent refusal -- not a silent no-op -- and touches no SDL/GL API, so it needs
    // no real window or display to prove.
    struct SkiaGaneshContext::Impl
    {
    };

    SkiaGaneshContext::SkiaGaneshContext(SDL_Window* window)
        : window_(window)
    {
        throw std::runtime_error(
            "SkiaGaneshContext requires a build configured with -DCNA_SKIA_MODE=GANESH "
            "(and -DCNA_SKIA_GANESH_BUILD_DIR=<Ganesh GN output>); this build was configured for "
            "raster mode (the default) and links CNA::Skia, which has no Ganesh/GL code compiled "
            "in at all. See docs/skia-ganesh-artifact.md.");
    }

    SkiaGaneshContext::~SkiaGaneshContext() = default;

    GrDirectContext* SkiaGaneshContext::NativeContextEXT() const noexcept
    {
        return nullptr;
    }
#endif
} // namespace CNA::Internal::Renderers::Skia
