#include "CNA/Internal/Backends/Dx3/Dx3GraphicsBackend.hpp"

// plan_dx3.md Design decision 9: <ddraw.h> (and the <windows.h> compatibility shim it pulls in
// from free-api) is contained to this .cpp only -- see Dx3GraphicsBackend.hpp's own comment for
// why this backend goes further than D3D11/D3D12's precedent and keeps it out of its own header
// too (the fopen -> free_api_fopen macro leak risk).
#include <ddraw.h>

#include <cstring>
#include <stdexcept>
#include <string>

namespace CNA::Internal::Backends::Dx3
{
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;
    using namespace CNA::Internal::Backends;

    namespace
    {
        [[noreturn]] void ThrowHr(const char* what, HRESULT hr)
        {
            throw std::runtime_error(std::string(what) + " failed: HRESULT=0x" +
                                      std::to_string(static_cast<unsigned long>(hr)));
        }

        // ---- 3D pipeline: DirectDraw is 2D-only. All 3D calls throw. ----
        [[noreturn]] void ThrowNo3D(const char* methodName)
        {
            throw std::runtime_error(std::string("DX3 (DirectDraw) does not support 3D: ") + methodName);
        }

        // ---- Phase X3: shared offscreen-surface helpers, used by both Dx3TextureBackend and
        // Dx3RenderTargetBackend (same underlying DirectDraw mechanism -- design decision 4:
        // 32bpp DDSCAPS_OFFSCREENPLAIN only, no palette/8-bit path). Kept as free functions rather
        // than a shared base class: neither type is ever named outside this .cpp (only returned
        // polymorphically as ITextureBackend/IRenderTargetBackend), so there is no header/ddraw.h
        // containment reason to give them one (design decision 9).
        LPDIRECTDRAWSURFACE CreateOffscreenSurface(LPDIRECTDRAW dd, int width, int height)
        {
            DDSURFACEDESC desc{};
            desc.dwSize = sizeof(DDSURFACEDESC);
            desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
            desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN;
            desc.dwWidth = static_cast<DWORD>(width);
            desc.dwHeight = static_cast<DWORD>(height);
            LPDIRECTDRAWSURFACE surface = nullptr;
            const HRESULT hr = dd->CreateSurface(&desc, &surface, nullptr);
            if (FAILED(hr)) ThrowHr("IDirectDraw::CreateSurface(offscreen)", hr);
            return surface;
        }

        // Writes a full level-0 RGBA8 image into `surface` via Lock()/memcpy/Unlock(). `stride` is
        // the source row length in bytes; <=0 means tightly packed (width*4), matching
        // ITextureBackend::UpdatePixels's own documented convention.
        void WriteSurfacePixels(LPDIRECTDRAWSURFACE surface, int width, int height,
                                const uint8_t* rgba, int stride)
        {
            if (!rgba) return;
            DDSURFACEDESC desc{};
            desc.dwSize = sizeof(DDSURFACEDESC);
            const HRESULT hr = surface->Lock(nullptr, &desc, 0, nullptr);
            if (FAILED(hr)) ThrowHr("IDirectDrawSurface::Lock(write)", hr);

            auto* base = static_cast<uint8_t*>(desc.lpSurface);
            const std::size_t rowBytes = static_cast<std::size_t>(width) * 4u;
            const std::size_t srcStride = stride > 0 ? static_cast<std::size_t>(stride) : rowBytes;
            for (int row = 0; row < height; ++row)
            {
                std::memcpy(base + static_cast<std::size_t>(row) * static_cast<std::size_t>(desc.lPitch),
                           rgba + static_cast<std::size_t>(row) * srcStride, rowBytes);
            }
            surface->Unlock(desc.lpSurface);
        }

        // Reads an (x, y, w, h) region back from `surface` via Lock()/memcpy/Unlock(), writing
        // tightly-packed RGBA8 rows into `pixels`. Shared by ReadBackbuffer (DX3-26) against
        // whichever surface is currently active.
        void ReadSurfacePixels(LPDIRECTDRAWSURFACE surface, int x, int y, int w, int h, uint8_t* pixels)
        {
            DDSURFACEDESC desc{};
            desc.dwSize = sizeof(DDSURFACEDESC);
            const HRESULT hr = surface->Lock(nullptr, &desc, 0, nullptr);
            if (FAILED(hr)) ThrowHr("IDirectDrawSurface::Lock(read)", hr);

            const auto* base = static_cast<const uint8_t*>(desc.lpSurface);
            for (int row = 0; row < h; ++row)
            {
                const uint8_t* src = base + static_cast<std::size_t>(y + row) * static_cast<std::size_t>(desc.lPitch) +
                                      static_cast<std::size_t>(x) * 4u;
                std::memcpy(pixels + static_cast<std::size_t>(row) * static_cast<std::size_t>(w) * 4u, src,
                           static_cast<std::size_t>(w) * 4u);
            }
            surface->Unlock(desc.lpSurface);
        }

        [[noreturn]] void ThrowMipLevelUnsupported(int level)
        {
            throw std::runtime_error(
                "DX3 (DirectDraw) does not support mip-level texture uploads (level " +
                std::to_string(level) + "): IDirectDrawSurface has no native mip chain or "
                "per-level LOD sampling. Use Texture2D::SetData(level=0, ...) only.");
        }
    }

    struct Dx3GraphicsBackend::Impl
    {
        // NOTE: SDL_Window is NOT owned by the backend -- same convention as every other
        // window-based CNA backend (GraphicsDevice/platform layer owns it).
        SDL_Window* window = nullptr;

        LPDIRECTDRAW dd = nullptr;
        // The real DirectDraw primary surface. Never Lock()'d directly for pixel access --
        // free-direct's own Lock() never exposes a writable pointer for a primary surface (see
        // Dx3GraphicsBackend.hpp's class comment). Written to only via the single identity Blt()
        // in Present().
        LPDIRECTDRAWSURFACE primary = nullptr;
        // DX3-owned "shadow backbuffer": a Lockable offscreen surface, sized to the logical/virtual
        // resolution, that Clear() and (from Phase X4 on) SpriteBatch draws always composite into.
        LPDIRECTDRAWSURFACE backBuffer = nullptr;

        // Phase X3: the offscreen surface owned by the currently-bound Dx3RenderTargetBackend, or
        // nullptr when no custom render target is bound (i.e. the shadow backbuffer is active).
        // Set directly by Dx3RenderTargetBackend::BindAsRenderTarget/UnbindAsRenderTarget via a
        // pointer to this field (passed at construction) -- kept as a plain LPDIRECTDRAWSURFACE
        // rather than a Dx3RenderTargetBackend* so Dx3RenderTargetBackend never needs to name this
        // private Impl type (it is defined later in this file, after Impl).
        LPDIRECTDRAWSURFACE currentTargetSurface = nullptr;

        int logicalWidth = 0;
        int logicalHeight = 0;
        CnaPresentationMode presentationMode = CnaPresentationMode::Overscan;

        // Resolves to whichever surface Clear()/ReadBackbuffer() should currently target: the
        // bound render target's surface if one is bound, else the shadow backbuffer. Present()
        // deliberately does NOT go through this -- it always Blt()s from the real shadow
        // backbuffer, matching FNA's own backbuffer-vs-render-target separation (a game must
        // SetRenderTarget(null) before presenting, same as every other CNA backend).
        [[nodiscard]] LPDIRECTDRAWSURFACE ActiveSurface() const
        {
            return currentTargetSurface ? currentTargetSurface : backBuffer;
        }

        ~Impl()
        {
            if (backBuffer) backBuffer->Release();
            if (primary) primary->Release();
            if (dd) dd->Release();
        }

        // Releases the current primary/shadow-backbuffer surfaces (if any) and recreates both at
        // (width, height): SetDisplayMode(width, height, 32) (design decision 4: 32bpp only) ->
        // primary CreateSurface (DDSCAPS_PRIMARYSURFACE) -> shadow CreateSurface
        // (DDSCAPS_OFFSCREENPLAIN, sized width x height).
        void CreateSurfaces(int width, int height)
        {
            if (backBuffer) { backBuffer->Release(); backBuffer = nullptr; }
            if (primary) { primary->Release(); primary = nullptr; }

            HRESULT hr = dd->SetDisplayMode(static_cast<DWORD>(width), static_cast<DWORD>(height), 32);
            if (FAILED(hr)) ThrowHr("IDirectDraw::SetDisplayMode", hr);

            DDSURFACEDESC primaryDesc{};
            primaryDesc.dwSize = sizeof(DDSURFACEDESC);
            primaryDesc.dwFlags = DDSD_CAPS;
            primaryDesc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
            hr = dd->CreateSurface(&primaryDesc, &primary, nullptr);
            if (FAILED(hr)) ThrowHr("IDirectDraw::CreateSurface(primary)", hr);

            DDSURFACEDESC backDesc{};
            backDesc.dwSize = sizeof(DDSURFACEDESC);
            backDesc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
            backDesc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN;
            backDesc.dwWidth = static_cast<DWORD>(width);
            backDesc.dwHeight = static_cast<DWORD>(height);
            hr = dd->CreateSurface(&backDesc, &backBuffer, nullptr);
            if (FAILED(hr))
            {
                primary->Release();
                primary = nullptr;
                ThrowHr("IDirectDraw::CreateSurface(shadow backbuffer)", hr);
            }

            logicalWidth = width;
            logicalHeight = height;
        }
    };

    Dx3GraphicsBackend::Dx3GraphicsBackend(const GraphicsBackendCreateArgs& args)
        : impl_(std::make_unique<Impl>())
    {
        if (!args.window) throw std::runtime_error("Dx3GraphicsBackend initialized with null window.");
        impl_->window = args.window;
        impl_->presentationMode = args.presentationMode;

        HRESULT hr = DirectDrawCreate(nullptr, &impl_->dd, nullptr);
        if (FAILED(hr)) ThrowHr("DirectDrawCreate", hr);

        // Design decision 2: free-direct's SetCooperativeLevel does
        // sdlWindow_ = reinterpret_cast<SDL_Window*>(hwnd) internally -- HWND is CNA's own already-
        // existing SDL_Window* in disguise, never a real Win32 handle and never a second window.
        hr = impl_->dd->SetCooperativeLevel(reinterpret_cast<HWND>(impl_->window), DDSCL_NORMAL);
        if (FAILED(hr)) ThrowHr("IDirectDraw::SetCooperativeLevel", hr);

        const int width = args.virtualWidth > 0 ? args.virtualWidth : 640;
        const int height = args.virtualHeight > 0 ? args.virtualHeight : 480;
        impl_->CreateSurfaces(width, height);
    }

    Dx3GraphicsBackend::~Dx3GraphicsBackend() = default;

    void Dx3GraphicsBackend::Clear(float r, float g, float b, float a)
    {
        (void)a; // DirectDraw ColorFill has no alpha channel concept (design decision 4: no palette/alpha surfaces).
        DDBLTFX fx{};
        fx.dwSize = sizeof(DDBLTFX);
        fx.dwFillColor = (static_cast<DWORD>(r * 255.0f) << 16) |
                         (static_cast<DWORD>(g * 255.0f) << 8) |
                          static_cast<DWORD>(b * 255.0f);
        const HRESULT hr = impl_->ActiveSurface()->Blt(nullptr, nullptr, nullptr, DDBLT_COLORFILL, &fx);
        if (FAILED(hr)) ThrowHr("IDirectDrawSurface::Blt(COLORFILL)", hr);
    }

    void Dx3GraphicsBackend::Present()
    {
        // A single identity Blt() (null dest/src rects = full surface, unscaled 1:1 copy) from the
        // shadow backbuffer onto the real primary. This mirrors the exact "once-per-frame back-
        // buffer-to-primary Blt" call shape both of free-direct's real target games already use as
        // their hottest path (docs/directdraw-limitations.md), and relies on free-direct's own
        // auto-present-on-dirty-Blt behavior against the primary surface. Flip() is deliberately
        // never called: it would only set free-direct's internal usesFlip_ flag and permanently
        // *disable* that auto-present path for no benefit -- Flip() itself does not copy pixels
        // from anywhere, it just re-presents the primary's own current buffer.
        const HRESULT hr = impl_->primary->Blt(nullptr, impl_->backBuffer, nullptr, DDBLT_WAIT, nullptr);
        if (FAILED(hr)) ThrowHr("IDirectDrawSurface::Blt(present)", hr);
    }

    void Dx3GraphicsBackend::GetViewportSize(int& width, int& height)
    {
        // Unlike SDL_Renderer-based backends, DX3 has no independent "physical output size" to
        // fall back to -- the primary surface's own size (set via SetDisplayMode) IS the logical
        // size; free-direct's internal SDL_Renderer handles physical window scaling invisibly.
        width = impl_->logicalWidth;
        height = impl_->logicalHeight;
    }

    void Dx3GraphicsBackend::ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels)
    {
        // DX3-26: reads from whichever surface is currently active -- the bound render target's
        // surface if one is bound (SetRenderTarget2D), else the shadow backbuffer. Never through
        // free-direct's physical presentation path, so this is exact-pixel regardless of window
        // size/letterboxing (there is no scaling between the shadow buffer and the values read
        // here; scaling only happens later, once, when Present() Blt()s onto the primary).
        ReadSurfacePixels(impl_->ActiveSurface(), x, y, w, h, pixels);
    }

    void Dx3GraphicsBackend::SetVirtualResolution(int width, int height)
    {
        if (width <= 0 || height <= 0) return;
        if (width == impl_->logicalWidth && height == impl_->logicalHeight) return;
        // Known limitation (free-direct's own PresentPrimary sets its physical letterbox rect via
        // a one-time logicalPresentationSet_ flag, never re-applied on a later primary resize) --
        // a resolution change after the first Present() will keep presenting at the *old* physical
        // scale until free-direct itself is extended to support that (out of scope here, design
        // decision 8: cannot silently extend free-direct's own surface beyond what it implements).
        impl_->CreateSurfaces(width, height);
    }

    void Dx3GraphicsBackend::SetPresentationMode(int mode)
    {
        impl_->presentationMode = static_cast<CnaPresentationMode>(mode);
        // free-direct's own PresentPrimary hardcodes SDL_LOGICAL_PRESENTATION_LETTERBOX (see
        // src/directdraw/DirectDraw.cpp) -- DX3 cannot honor Stretch/Overscan/NativeBackBuffer's
        // real physical-scaling behavior without modifying free-direct itself (out of scope, design
        // decision 8). The mode is still stored so GetViewportSize()/logical-resolution bookkeeping
        // stays consistent with what the game requested.
    }

    SDL_Window* Dx3GraphicsBackend::GetWindowInternal() const
    {
        return impl_->window;
    }

    // ---- Phase X3: textures and render targets ----
    // Both classes below are never named outside this .cpp (only returned polymorphically), so
    // <ddraw.h> stays fully contained here -- see this file's own Dx3TextureBackend/
    // Dx3RenderTargetBackend definitions for the shared surface-creation helpers they use.

    class Dx3TextureBackend : public ITextureBackend
    {
    public:
        Dx3TextureBackend(LPDIRECTDRAW dd, int width, int height)
            : width_(width), height_(height), surface_(CreateOffscreenSurface(dd, width, height))
        {
        }

        Dx3TextureBackend(LPDIRECTDRAW dd, const ImageData& data)
            : Dx3TextureBackend(dd, data.width, data.height)
        {
            WriteSurfacePixels(surface_, width_, height_, data.pixels.data(), width_ * 4);
        }

        ~Dx3TextureBackend() override { if (surface_) surface_->Release(); }

        Dx3TextureBackend(const Dx3TextureBackend&) = delete;
        Dx3TextureBackend& operator=(const Dx3TextureBackend&) = delete;

        [[nodiscard]] int GetWidth() const override { return width_; }
        [[nodiscard]] int GetHeight() const override { return height_; }
        [[nodiscard]] SDL_Texture* GetNativeTexture() const override { return nullptr; }

        void UpdatePixels(const uint8_t* rgba, int stride) override
        {
            WriteSurfacePixels(surface_, width_, height_, rgba, stride);
        }

        // DX3-22: no native mip chain on IDirectDrawSurface -- level 0 is unaffected (always
        // routed through UpdatePixels by Texture2D::SetData), level>0 throws honestly rather than
        // silently discarding the upload, matching SDL_RENDERER's Task 681 precedent.
        void UpdatePixelsLevel(int level, const uint8_t*, int, int) override
        {
            ThrowMipLevelUnsupported(level);
        }

        [[nodiscard]] LPDIRECTDRAWSURFACE Surface() const { return surface_; }

    protected:
        int width_ = 0;
        int height_ = 0;
        LPDIRECTDRAWSURFACE surface_ = nullptr;
    };

    class Dx3RenderTargetBackend final : public IRenderTargetBackend
    {
    public:
        Dx3RenderTargetBackend(LPDIRECTDRAWSURFACE* currentTargetSlot, LPDIRECTDRAW dd,
                               int width, int height, int multiSampleCount)
            : currentTargetSlot_(currentTargetSlot), width_(width), height_(height),
              multiSampleCount_(multiSampleCount), surface_(CreateOffscreenSurface(dd, width, height))
        {
        }

        ~Dx3RenderTargetBackend() override
        {
            if (*currentTargetSlot_ == surface_) *currentTargetSlot_ = nullptr;
            if (surface_) surface_->Release();
        }

        Dx3RenderTargetBackend(const Dx3RenderTargetBackend&) = delete;
        Dx3RenderTargetBackend& operator=(const Dx3RenderTargetBackend&) = delete;

        [[nodiscard]] int GetWidth() const override { return width_; }
        [[nodiscard]] int GetHeight() const override { return height_; }
        [[nodiscard]] SDL_Texture* GetNativeTexture() const override { return nullptr; }

        void UpdatePixels(const uint8_t* rgba, int stride) override
        {
            WriteSurfacePixels(surface_, width_, height_, rgba, stride);
        }

        void UpdatePixelsLevel(int level, const uint8_t*, int, int) override
        {
            ThrowMipLevelUnsupported(level);
        }

        void BindAsRenderTarget() override { *currentTargetSlot_ = surface_; }
        void UnbindAsRenderTarget() override
        {
            if (*currentTargetSlot_ == surface_) *currentTargetSlot_ = nullptr;
        }

        [[nodiscard]] int GetMultiSampleCount() const override { return multiSampleCount_; }

        // DX3-24: IDirectDrawSurface has no depth-buffer concept at all, regardless of what
        // DepthFormat was requested -- always false, same reasoning SDL_RENDERER's Task 708 used.
        [[nodiscard]] bool HasRealDepthBuffer(bool /*depthFormatWasRequested*/) const override
        {
            return false;
        }

    private:
        LPDIRECTDRAWSURFACE* currentTargetSlot_;
        int width_ = 0;
        int height_ = 0;
        int multiSampleCount_ = 0;
        LPDIRECTDRAWSURFACE surface_ = nullptr;
    };

    std::unique_ptr<ITextureBackend> Dx3GraphicsBackend::CreateTexture(const ImageData& data)
    {
        return std::make_unique<Dx3TextureBackend>(impl_->dd, data);
    }

    std::unique_ptr<IRenderTargetBackend> Dx3GraphicsBackend::CreateRenderTarget2D(
        int w, int h, int /*depthFormat*/, bool /*preserveContents*/, bool /*mipMap*/, int multiSampleCount)
    {
        return std::make_unique<Dx3RenderTargetBackend>(&impl_->currentTargetSurface, impl_->dd,
                                                         w, h, multiSampleCount);
    }

    void Dx3GraphicsBackend::SetRenderTarget2D(IRenderTargetBackend* rt)
    {
        if (rt)
            rt->BindAsRenderTarget();
        else
            impl_->currentTargetSurface = nullptr;
    }

    void Dx3GraphicsBackend::SetRenderTargets(IRenderTargetBackend* const* rts, int count)
    {
        // DX3-27: DirectDraw has no multi-render-target concept -- single active surface only.
        if (count > 1)
            throw std::runtime_error(
                "DX3 (DirectDraw) does not support multiple simultaneous render targets (MRT): "
                "requested " + std::to_string(count) + ", but IDirectDrawSurface supports exactly "
                "one active render target at a time.");
        SetRenderTarget2D(count > 0 ? rts[0] : nullptr);
    }

    std::unique_ptr<ISpriteBatchBackend> Dx3GraphicsBackend::CreateSpriteBatch()
    {
        throw std::runtime_error("Dx3GraphicsBackend::CreateSpriteBatch: not yet implemented (plan_dx3.md Phase X4).");
    }

    void Dx3GraphicsBackend::ClearColorAndDepth(float, float, float, float, float) { ThrowNo3D("ClearColorAndDepth"); }
    void Dx3GraphicsBackend::ClearDepth(float) { ThrowNo3D("ClearDepth"); }
    void Dx3GraphicsBackend::ClearStencil(int) { ThrowNo3D("ClearStencil"); }
    void Dx3GraphicsBackend::ClearDepthAndStencil(float, int) { ThrowNo3D("ClearDepthAndStencil"); }
    void Dx3GraphicsBackend::ClearColorAndStencil(float, float, float, float, int) { ThrowNo3D("ClearColorAndStencil"); }
    void Dx3GraphicsBackend::ClearColorDepthAndStencil(float, float, float, float, float, int) { ThrowNo3D("ClearColorDepthAndStencil"); }
    void Dx3GraphicsBackend::SetDepthTestEnabled(bool)  { ThrowNo3D("SetDepthTestEnabled"); }
    void Dx3GraphicsBackend::SetBlendEnabled(bool)      { ThrowNo3D("SetBlendEnabled"); }
    void Dx3GraphicsBackend::SetDepthWriteEnabled(bool) { ThrowNo3D("SetDepthWriteEnabled"); }

    std::unique_ptr<IVertexBufferBackend> Dx3GraphicsBackend::CreateVertexBuffer(int)
    {
        ThrowNo3D("CreateVertexBuffer");
    }

    std::unique_ptr<IIndexBufferBackend> Dx3GraphicsBackend::CreateIndexBuffer16(int)
    {
        ThrowNo3D("CreateIndexBuffer16");
    }

    std::unique_ptr<IOcclusionQueryBackend> Dx3GraphicsBackend::CreateOcclusionQuery()
    {
        ThrowNo3D("CreateOcclusionQuery");
    }

    void Dx3GraphicsBackend::DrawColoredPrimitives(const IVertexBufferBackend&,
                                                   const Matrix&, const Matrix&, const Matrix&,
                                                   PrimitiveType, int) { ThrowNo3D("DrawColoredPrimitives"); }

    void Dx3GraphicsBackend::DrawIndexedColoredPrimitives(const IVertexBufferBackend&,
                                                          const IIndexBufferBackend&,
                                                          const Matrix&, const Matrix&, const Matrix&,
                                                          PrimitiveType, int) { ThrowNo3D("DrawIndexedColoredPrimitives"); }
}

namespace CNA::Internal::Backends
{
#ifdef CNA_BACKEND_DX3
    std::unique_ptr<IGraphicsBackend> CreateGraphicsBackend(const GraphicsBackendCreateArgs& args)
    {
        return std::make_unique<Dx3::Dx3GraphicsBackend>(args);
    }
#endif
}
