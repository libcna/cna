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
    namespace
    {
        CNA::Platform::GlContextDescription RequestedGaneshContext()
        {
            CNA::Platform::GlContextDescription description;
            description.majorVersion = 3;
            description.minorVersion = 3;
            description.profile = CNA::Platform::GlProfile::Compatibility;
            description.depthBits = 0;
            description.stencilBits = 8;
            description.multisampleBuffers = 0;
            description.multisampleSamples = 0;
            description.doubleBuffer = true;
            return description;
        }
    }

#if defined(CNA_SKIA_MODE_GANESH)
    struct SkiaGaneshContext::Impl
    {
        sk_sp<GrDirectContext> grContext;
    };

    SkiaGaneshContext::SkiaGaneshContext(CNA::Platform::IPlatformGlContext* service,
                                         const CNA::Platform::WindowId window)
        : platformContext_(std::make_unique<PlatformGlContextOwner>(
              RequirePlatformGlContext(service, "SKIA Ganesh"), window,
              RequestedGaneshContext()))
    {
        try
        {
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
            platformContext_.reset();
            throw;
        }
    }

    SkiaGaneshContext::~SkiaGaneshContext()
    {
        if (impl_ && impl_->grContext)
            impl_->grContext->abandonContext();
        impl_.reset();
        platformContext_.reset();
    }

    GrDirectContext* SkiaGaneshContext::NativeContextEXT() const noexcept
    {
        return impl_ ? impl_->grContext.get() : nullptr;
    }

    void SkiaGaneshContext::SwapBuffers()
    {
        platformContext_->SwapBuffers();
    }
#else
    // No SkiaGaneshContext::Impl definition exists in a RASTER-mode build: this build links
    // CNA::Skia (raster-only archives, skia_enable_ganesh=false), which contains no Ganesh/GL
    // object code at all. Requesting Ganesh mode here is therefore a deterministic,
    // display-independent refusal -- not a silent no-op -- and touches no platform/GL API, so it needs
    // no real window or display to prove.
    struct SkiaGaneshContext::Impl
    {
    };

    SkiaGaneshContext::SkiaGaneshContext(CNA::Platform::IPlatformGlContext*,
                                         CNA::Platform::WindowId)
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


    void SkiaGaneshContext::SwapBuffers() {}
#endif
} // namespace CNA::Internal::Renderers::Skia
