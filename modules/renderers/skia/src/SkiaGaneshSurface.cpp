#include "CNA/Internal/Renderers/Skia/SkiaGaneshSurface.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#if defined(CNA_SKIA_MODE_GANESH)
#include "include/core/SkCanvas.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkSurface.h"
#include "include/gpu/ganesh/GrBackendSurface.h"
#include "include/gpu/ganesh/GrDirectContext.h"
#include "include/gpu/ganesh/SkSurfaceGanesh.h"
#include "include/gpu/ganesh/gl/GrGLBackendSurface.h"
#include "include/gpu/ganesh/gl/GrGLTypes.h"

// Core OpenGL 1.x entry points (glGetIntegerv, GL_STENCIL_BITS, GL_RGBA8) are always present in
// libGL.so -- no extension loader needed for these. SkiaGaneshContext already made a real GL
// context current before this constructor body runs.
#include <GL/gl.h>
#endif

namespace CNA::Internal::Renderers::Skia
{
#if defined(CNA_SKIA_MODE_GANESH)
    struct SkiaGaneshSurface::Impl
    {
        sk_sp<SkSurface> surface;
    };

    SkiaGaneshSurface::SkiaGaneshSurface(const GraphicsRendererCreateArgs& args)
        : glService_(args.glContext)
        , surface_(args.surface)
        , context_(std::in_place, glService_, surface_.GetWindowId())
    {
        int width = 0;
        int height = 0;
        surface_.GetDrawableSize(width, height);
        if (width <= 0 || height <= 0)
            throw std::runtime_error("SkiaGaneshSurface has a non-positive drawable size.");

        impl_ = std::make_unique<Impl>();
        WrapBackbuffer(width, height);
    }

    SkiaGaneshSurface::~SkiaGaneshSurface() = default;

    void SkiaGaneshSurface::WrapBackbuffer(int width, int height)
    {
        GrDirectContext* grContext = context_->NativeContextEXT();

        GLint stencilBits = 0;
        glGetIntegerv(GL_STENCIL_BITS, &stencilBits);
        // GrBackendRenderTargets::MakeGL requires exactly 0, 8, or 16 -- any other driver-reported
        // value (rare, but not contractually impossible) is a real, reportable failure rather
        // than silently rounding to the nearest accepted value.
        if (stencilBits != 0 && stencilBits != 8 && stencilBits != 16)
        {
            throw std::runtime_error(
                "SkiaGaneshSurface: default framebuffer reported an unsupported stencil bit "
                "depth (" + std::to_string(stencilBits) + "); Skia's GL renderer render target "
                "requires exactly 0, 8, or 16.");
        }

        GrGLFramebufferInfo fbInfo{};
        fbInfo.fFBOID = 0; // The real window-system default framebuffer, never an off-screen FBO.
        fbInfo.fFormat = GL_RGBA8;

        GrBackendRenderTarget rendererRenderTarget =
            GrBackendRenderTargets::MakeGL(width, height, /*sampleCnt=*/1, stencilBits, fbInfo);
        if (!rendererRenderTarget.isValid())
        {
            throw std::runtime_error(
                "SkiaGaneshSurface: GrBackendRenderTargets::MakeGL produced an invalid renderer "
                "render target.");
        }

        // kBottomLeft_GrSurfaceOrigin: the real GL default framebuffer's row 0 is the bottom row
        // in device memory. SkSurface/SkCanvas hide this from every caller -- draws and
        // readPixels() both use ordinary top-down 2D coordinates regardless -- but only the
        // correct origin here makes that abstraction actually correct rather than accidentally
        // flipped; this is the exact fact SKIA-161's own acceptance text asks to be proven, not
        // just declared, by the accompanying pixel test.
        // No SkColorSpace (nullptr), matching the plain GL_RGBA8 default framebuffer this wraps:
        // no GL_FRAMEBUFFER_SRGB or GL_SRGB8_ALPHA8 was requested, so the framebuffer stores
        // plain gamma-encoded bytes with no automatic encode/decode. An earlier version of this
        // code passed SkColorSpace::MakeSRGBLinear() here (copying SkiaSurface.cpp's raster
        // convention verbatim) -- that told Skia this surface stores *linear-light* values, so it
        // silently gamma-encoded every SkColor draw on the way in and decoded every readPixels()
        // on the way out. Pure primaries (0/255 per channel) are fixed points of that transform
        // and so passed anyway; a genuine mid-tone clear color first exposed it (caught by this
        // file's own accompanying test) -- (128,64,200) read back as (55,13,147).
        sk_sp<SkSurface> surface = SkSurfaces::WrapBackendRenderTarget(
            grContext, rendererRenderTarget, kBottomLeft_GrSurfaceOrigin, kRGBA_8888_SkColorType,
            nullptr, nullptr);
        if (!surface)
        {
            throw std::runtime_error(
                "SkiaGaneshSurface: SkSurfaces::WrapBackendRenderTarget returned null.");
        }

        impl_->surface = std::move(surface);
        width_ = width;
        height_ = height;
    }

    SkCanvas* SkiaGaneshSurface::Canvas() const noexcept
    {
        return impl_->surface->getCanvas();
    }

    void SkiaGaneshSurface::Resize(const RendererSurfaceInfo& surface)
    {
        surface_.Update(surface);
        int width = 0;
        int height = 0;
        surface_.GetDrawableSize(width, height);
        if (width <= 0 || height <= 0)
            throw std::runtime_error("SkiaGaneshSurface has a non-positive drawable size.");
        if (width == width_ && height == height_)
            return;
        WrapBackbuffer(width, height);
    }

    void SkiaGaneshSurface::Present()
    {
        context_->NativeContextEXT()->flushAndSubmit(impl_->surface.get());
        context_->SwapBuffers();
    }

    bool SkiaGaneshSurface::ReadPixels(int x, int y, int width, int height, std::uint8_t* outRgba8) const
    {
        const SkImageInfo info = SkImageInfo::Make(
            width, height, kRGBA_8888_SkColorType, kPremul_SkAlphaType, nullptr);
        return impl_->surface->readPixels(info, outRgba8, static_cast<size_t>(width) * 4u, x, y);
    }

    void SkiaGaneshSurface::DebugSimulateContextLossEXT()
    {
        // Mirrors EasyGLRenderer::DebugSimulateContextLoss()'s own established desktop
        // path exactly: destroy the GL context, create a brand new one, and rebuild everything
        // that depended on the old one. There is no ResourceRegistry-equivalent recreate step
        // here (unlike EasyGL) because no textures/targets/effects exist in the Ganesh path yet
        // to recreate -- the wrapped backbuffer surface is the only thing that depends on the
        // context, and rewrapping it is exactly WrapBackbuffer's job.
        impl_->surface.reset(); // release the surface that depends on the OLD context first
        context_.reset();       // destroy the OLD GL context + GrDirectContext
        context_.emplace(glService_, surface_.GetWindowId());

        int width = 0;
        int height = 0;
        surface_.GetDrawableSize(width, height);
        if (width <= 0 || height <= 0)
            throw std::runtime_error("SkiaGaneshSurface has a non-positive drawable size.");
        WrapBackbuffer(width, height); // rewrap fresh, at the current (possibly changed) size
    }
#else
    // No SkiaGaneshSurface::Impl definition or Ganesh-symbol reference exists in a RASTER-mode
    // build. SkiaGaneshSurface's constructor never reaches this file's body: context_(window)'s
    // member-initializer-list evaluation always throws first (SkiaGaneshContext's own established
    // RASTER-mode refusal, SKIA-160). These stub method bodies exist purely so the translation
    // unit compiles and links against the raster-only CNA::Skia archive, which has no Ganesh/GL
    // object code at all.
    struct SkiaGaneshSurface::Impl
    {
    };

    SkiaGaneshSurface::SkiaGaneshSurface(const GraphicsRendererCreateArgs& args)
        : glService_(args.glContext)
        , surface_(args.surface)
        , context_(std::in_place, glService_, surface_.GetWindowId())
    {
    }

    SkiaGaneshSurface::~SkiaGaneshSurface() = default;

    void SkiaGaneshSurface::WrapBackbuffer(int, int) {}

    SkCanvas* SkiaGaneshSurface::Canvas() const noexcept { return nullptr; }

    void SkiaGaneshSurface::Resize(const RendererSurfaceInfo&) {}

    void SkiaGaneshSurface::DebugSimulateContextLossEXT() {}

    void SkiaGaneshSurface::Present() {}

    bool SkiaGaneshSurface::ReadPixels(int, int, int, int, std::uint8_t*) const { return false; }
#endif
} // namespace CNA::Internal::Renderers::Skia
