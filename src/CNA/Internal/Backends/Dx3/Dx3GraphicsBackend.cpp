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

        int logicalWidth = 0;
        int logicalHeight = 0;
        CnaPresentationMode presentationMode = CnaPresentationMode::Overscan;

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
        const HRESULT hr = impl_->backBuffer->Blt(nullptr, nullptr, nullptr, DDBLT_COLORFILL, &fx);
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
        // Reads directly from the shadow backbuffer's own locked memory -- never through
        // free-direct's physical presentation path, so this is exact-pixel regardless of window
        // size/letterboxing (there is no scaling between the shadow buffer and the values read
        // here; scaling only happens later, once, when Present() Blt()s onto the primary).
        DDSURFACEDESC desc{};
        desc.dwSize = sizeof(DDSURFACEDESC);
        const HRESULT hr = impl_->backBuffer->Lock(nullptr, &desc, 0, nullptr);
        if (FAILED(hr)) ThrowHr("IDirectDrawSurface::Lock(shadow backbuffer)", hr);

        const auto* base = static_cast<const uint8_t*>(desc.lpSurface);
        for (int row = 0; row < h; ++row)
        {
            const uint8_t* src = base + static_cast<std::size_t>(y + row) * static_cast<std::size_t>(desc.lPitch) +
                                  static_cast<std::size_t>(x) * 4u;
            std::memcpy(pixels + static_cast<std::size_t>(row) * static_cast<std::size_t>(w) * 4u, src,
                        static_cast<std::size_t>(w) * 4u);
        }

        impl_->backBuffer->Unlock(desc.lpSurface);
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

    std::unique_ptr<ITextureBackend> Dx3GraphicsBackend::CreateTexture(const ImageData&)
    {
        throw std::runtime_error("Dx3GraphicsBackend::CreateTexture: not yet implemented (plan_dx3.md Phase X3).");
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
