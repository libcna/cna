// plan_dx9.md Phase D9-3 (D9-30/D9-31): real device creation + Clear/Present/ReadBackbuffer.
// Phase D9-6 (D9-60/D9-61/D9-62) render states landed here too, forced in early: GraphicsDevice's
// own constructor unconditionally pushes BlendState/DepthStencilState/RasterizerState defaults, so
// none of those methods could stay throwing stubs once a real device exists (see ApplyBlendState's
// own comment in the header for the full explanation).
#include "CNA/Internal/Backends/D3D9/D3D9GraphicsBackend.hpp"
#include "CNA/Internal/Backends/Common/NotYetImplemented.hpp"
#include "CNA/Internal/Backends/D3D9/D3D9Buffers.hpp"
#include "CNA/Internal/Backends/D3D9/D3D9FormatMapping.hpp"
#include "CNA/Internal/Backends/D3D9/D3D9StateMapping.hpp"
#include "CNA/Internal/Backends/D3D9/D3D9Textures.hpp"

#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DeviceLostException.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/NoSuitableGraphicsDeviceException.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <bit>
#include <cstdio>
#include <cstring>

namespace CNA::Internal::Backends::D3D9
{
    namespace
    {
        std::string FormatHr(HRESULT hr)
        {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "0x%08lX", static_cast<unsigned long>(hr));
            return buf;
        }

        using Microsoft::Xna::Framework::Graphics::DepthFormat;

        bool HasDepthBuffer(int depthStencilFormatOrdinal)
        {
            return static_cast<DepthFormat>(depthStencilFormatOrdinal) != DepthFormat::None;
        }

        bool HasStencilBuffer(int depthStencilFormatOrdinal)
        {
            return static_cast<DepthFormat>(depthStencilFormatOrdinal) == DepthFormat::Depth24Stencil8;
        }

        // D9-30 real-hardware finding, verified against real Wine+DXVK (not assumed): a D3D9
        // swap-chain back buffer is restricted to a small set of DISPLAY-compatible D3DFORMATs
        // (traditionally D3DFMT_A8R8G8B8/X8R8G8B8/A2R10G10B10/X1R5G5B5/R5G6B5) -- this is a real
        // D3D9 API restriction (DXVK correctly replicates it, confirmed by
        // D3D9DeviceEx::ResetSwapChain: "Unsupported backbuffer format: D3D9Format::A8B8G8R8"),
        // NOT a DXVK quirk. D3D9FormatMapping::SurfaceFormatToD3D9() maps SurfaceFormat::Color to
        // D3DFMT_A8B8G8R8 -- correct for a general TEXTURE (D9-50), where that restriction does
        // not apply, but genuinely invalid for the swap chain specifically. Real D3D9 hardware
        // creates the back buffer as D3DFMT_A8R8G8B8 (its display-compatible BGRA sibling, same
        // alpha, same bit depth) when SurfaceFormat::Color is requested -- ReadBackbuffer() already
        // handles both A8B8G8R8 and A8R8G8B8 byte orders, so this substitution stays fully correct
        // end to end. Every other mapped format either is already display-compatible (R5G6B5,
        // ColorBgraEXT's own A8R8G8B8) or was never a sane back-buffer choice regardless (e.g. a
        // compressed format) -- this substitution is therefore scoped to the one real, confirmed
        // case, not a blanket "just try something else" fallback.
        D3DFORMAT BackBufferDisplayFormatForD3D9(D3DFORMAT requested)
        {
            if (requested == D3DFMT_A8B8G8R8) return D3DFMT_A8R8G8B8;
            return requested;
        }
    }

    D3D9GraphicsBackend::D3D9GraphicsBackend(const GraphicsBackendCreateArgs& args)
        : window_(args.window)
        , virtualWidth_(args.virtualWidth)
        , virtualHeight_(args.virtualHeight)
        , presentationMode_(static_cast<int>(args.presentationMode))
        , backBufferFormatOrdinal_(args.backBufferFormat)
        , depthStencilFormatOrdinal_(args.depthStencilFormat)
        , isFullScreen_(args.isFullScreen)
        , swapInterval_(args.swapInterval)
        , graphicsProfileOrdinal_(args.graphicsProfile)
        , deviceEventCallback_(args.deviceEventCallback)
    {
        if (window_)
        {
            SDL_GetWindowSizeInPixels(window_, &width_, &height_);
        }
        if (width_ <= 0) width_ = args.virtualWidth > 0 ? args.virtualWidth : 1024;
        if (height_ <= 0) height_ = args.virtualHeight > 0 ? args.virtualHeight : 768;

        CreateDeviceResources(args);

        SDL_Log("[D3D9] Backend initialised (%dx%d), VertexShaderVersion=0x%08lx PixelShaderVersion=0x%08lx",
                width_, height_,
                static_cast<unsigned long>(caps_.VertexShaderVersion),
                static_cast<unsigned long>(caps_.PixelShaderVersion));
    }

    D3D9GraphicsBackend::~D3D9GraphicsBackend() = default;

    D3DPRESENT_PARAMETERS D3D9GraphicsBackend::BuildPresentParameters() const
    {
        D3DPRESENT_PARAMETERS pp{};
        pp.Windowed = !isFullScreen_;
        pp.SwapEffect = D3DSWAPEFFECT_DISCARD;

        const D3DFORMAT requestedBackBufferFormat = SurfaceFormatToD3D9(backBufferFormatOrdinal_);
        // Fall back to Color's own format if the game requested something D3D9 has no equivalent
        // for (e.g. Bc7EXT as a back-buffer format would be nonsensical anyway) -- never silently
        // pick an arbitrary format, but a back buffer must exist to construct a device at all.
        // BackBufferDisplayFormatForD3D9() substitutes Color's own A8B8G8R8 for its
        // display-compatible A8R8G8B8 sibling -- a real, confirmed D3D9 swap-chain restriction,
        // not a texture-format concern (see that function's own doc comment).
        pp.BackBufferFormat = BackBufferDisplayFormatForD3D9(
            requestedBackBufferFormat != D3DFMT_UNKNOWN ? requestedBackBufferFormat : D3DFMT_A8B8G8R8);
        pp.BackBufferWidth = static_cast<UINT>(width_);
        pp.BackBufferHeight = static_cast<UINT>(height_);
        pp.BackBufferCount = 1;

        pp.hDeviceWindow = window_ ? static_cast<HWND>(SDL_GetPointerProperty(
            SDL_GetWindowProperties(window_), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr)) : nullptr;

        if (HasDepthBuffer(depthStencilFormatOrdinal_))
        {
            pp.EnableAutoDepthStencil = TRUE;
            pp.AutoDepthStencilFormat = DepthFormatToD3D9(depthStencilFormatOrdinal_);
        }
        else
        {
            pp.EnableAutoDepthStencil = FALSE;
        }

        // Mirrors GraphicsDevice.cpp's own toSwapInterval() convention (0=immediate, 1=one/default,
        // 2=two vertical retraces).
        switch (swapInterval_)
        {
            case 0:  pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE; break;
            case 2:  pp.PresentationInterval = D3DPRESENT_INTERVAL_TWO; break;
            default: pp.PresentationInterval = D3DPRESENT_INTERVAL_ONE; break;
        }

        return pp;
    }

    void D3D9GraphicsBackend::CreateDeviceResources(const GraphicsBackendCreateArgs&)
    {
        if (!window_)
            throw std::runtime_error("D3D9GraphicsBackend: no window available to create a device for");

        HWND hwnd = static_cast<HWND>(SDL_GetPointerProperty(
            SDL_GetWindowProperties(window_), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
        if (!hwnd)
            throw std::runtime_error("D3D9GraphicsBackend: could not obtain HWND from SDL window");

        // design decision 2: plain Direct3DCreate9, not D3D9Ex. Direct3DCreate9 returns an
        // already-AddRef'd pointer (unlike CreateDevice-style out-params) -- ComPtr::Attach is the
        // correct adoption method, not an assignment (which would AddRef a second time and leak).
        d3d9_.Attach(Direct3DCreate9(D3D_SDK_VERSION));
        if (!d3d9_)
            throw std::runtime_error("Direct3DCreate9 failed");

        D3DCAPS9 preCreateCaps{};
        HRESULT hr = d3d9_->GetDeviceCaps(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, &preCreateCaps);
        if (FAILED(hr))
            throw std::runtime_error("IDirect3D9::GetDeviceCaps failed, hr=" + FormatHr(hr));

        // A real D3D9-era device might lack hardware T&L (e.g. an old integrated GPU) -- query
        // before assuming hardware vertex processing is available, rather than hardcoding it.
        const DWORD vertexProcessingFlags = (preCreateCaps.DevCaps & D3DDEVCAPS_HWTRANSFORMANDLIGHT)
            ? D3DCREATE_HARDWARE_VERTEXPROCESSING
            : D3DCREATE_SOFTWARE_VERTEXPROCESSING;

        D3DPRESENT_PARAMETERS pp = BuildPresentParameters();

        hr = d3d9_->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd, vertexProcessingFlags,
                                  &pp, device_.ReleaseAndGetAddressOf());
        if (FAILED(hr))
            throw std::runtime_error("IDirect3D9::CreateDevice failed, hr=" + FormatHr(hr));

        hr = device_->GetDeviceCaps(&caps_);
        if (FAILED(hr))
            throw std::runtime_error("IDirect3DDevice9::GetDeviceCaps failed, hr=" + FormatHr(hr));

        // D9-32: enforce the profile floor at construction -- a specific, named diagnostic
        // (matching XNA's own NoSuitableGraphicsDeviceException) instead of a deferred
        // shader-creation failure much later. This checks only the shader-model floor XNA itself
        // documents for HiDef (vs_3_0/ps_3_0) -- the FULL Reach/HiDef capability table (texture
        // size limits, NPOT restrictions, instancing, MRT count, render-target/back-buffer format
        // whitelists, MSAA levels, ...) is D9-100's job (Phase D9-10, "a research task as much as
        // a coding one"), not invented here. Reach itself has no floor to check against on this
        // machine -- every real D3D9 device already exceeds vs_2_0/ps_2_0.
        using Microsoft::Xna::Framework::Graphics::GraphicsProfile;
        using Microsoft::Xna::Framework::Graphics::NoSuitableGraphicsDeviceException;
        if (static_cast<GraphicsProfile>(graphicsProfileOrdinal_) == GraphicsProfile::HiDef)
        {
            if (caps_.VertexShaderVersion < static_cast<DWORD>(D3DVS_VERSION(3, 0)) ||
                caps_.PixelShaderVersion < static_cast<DWORD>(D3DPS_VERSION(3, 0)))
            {
                throw NoSuitableGraphicsDeviceException(
                    "GraphicsProfile::HiDef requires vs_3_0/ps_3_0 -- this device reports "
                    "VertexShaderVersion=" + FormatHr(caps_.VertexShaderVersion) +
                    " PixelShaderVersion=" + FormatHr(caps_.PixelShaderVersion));
            }
        }
    }

    void D3D9GraphicsBackend::EnsureDeviceSize()
    {
        if (!window_) return;

        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(window_, &w, &h);
        if (w <= 0 || h <= 0) return;

        const bool sizeChanged = (w != width_ || h != height_);
        if (!sizeChanged && !presentationDirty_) return;

        width_ = w;
        height_ = h;
        presentationDirty_ = false;

        D3DPRESENT_PARAMETERS pp = BuildPresentParameters();
        HRESULT hr = device_->Reset(&pp);
        if (FAILED(hr))
            throw std::runtime_error("D3D9 Reset (resize) failed, hr=" + FormatHr(hr));

        // Reset() invalidates the device's own render-state defaults (e.g. the viewport) --
        // restore what GraphicsDevice itself does not proactively re-push after a resize.
        SetViewport(0, 0, width_, height_, 0.0f, 1.0f);
    }

    void D3D9GraphicsBackend::UpdatePresentationFormatEXT(int backBufferFormat, int depthStencilFormat,
                                                           bool isFullScreen)
    {
        if (backBufferFormat == backBufferFormatOrdinal_ &&
            depthStencilFormat == depthStencilFormatOrdinal_ &&
            isFullScreen == isFullScreen_)
        {
            return;
        }

        backBufferFormatOrdinal_ = backBufferFormat;
        depthStencilFormatOrdinal_ = depthStencilFormat;
        isFullScreen_ = isFullScreen;
        presentationDirty_ = true;
    }

    void D3D9GraphicsBackend::RegisterDefaultPoolResourceEXT(ID3D9DefaultPoolResourceEXT* resource)
    {
        defaultPoolResources_.push_back(resource);
    }

    void D3D9GraphicsBackend::UnregisterDefaultPoolResourceEXT(ID3D9DefaultPoolResourceEXT* resource)
    {
        std::erase(defaultPoolResources_, resource);
    }

    void D3D9GraphicsBackend::PerformResetRecovery()
    {
        if (deviceEventCallback_) deviceEventCallback_(BackendDeviceEvent::Resetting);

        // D3DPOOL_MANAGED user resources (D9-4's own spike already confirmed these survive Reset()
        // untouched, with no re-upload) need no action here. D3DPOOL_DEFAULT resources (D9-40's
        // dynamic vertex/index buffers) must be released before Reset() -- each one recreates its
        // own object lazily the next time it is actually used (real XNA/D3D9 behavior: a DYNAMIC
        // VertexBuffer's content does not survive DeviceReset; a game is expected to re-fill it).
        for (ID3D9DefaultPoolResourceEXT* resource : defaultPoolResources_)
        {
            resource->ReleaseDefaultPoolResourceEXT();
        }

        D3DPRESENT_PARAMETERS pp = BuildPresentParameters();
        HRESULT hr = device_->Reset(&pp);
        if (FAILED(hr))
        {
            // Reset() can itself fail (genuinely still lost, or a real error) -- stay in the lost
            // state and let the next Present() try again, matching XNA's own resilience (a real
            // game keeps running frames while the device is lost, rather than crashing).
            return;
        }

        SetViewport(0, 0, width_, height_, 0.0f, 1.0f);
        deviceLost_ = false;

        if (deviceEventCallback_) deviceEventCallback_(BackendDeviceEvent::Reset);
    }

    void D3D9GraphicsBackend::PollDeviceLost()
    {
        HRESULT hr = device_->TestCooperativeLevel();
        if (hr == D3DERR_DEVICENOTRESET)
        {
            PerformResetRecovery();
        }
        // D3DERR_DEVICELOST (still lost) or any other result: nothing more to do this frame:
        // deviceLost_ stays true, and the next Present() will poll again.
    }

    void D3D9GraphicsBackend::ThrowIfDeviceLost() const
    {
        if (deviceLost_)
        {
            throw Microsoft::Xna::Framework::Graphics::DeviceLostException();
        }
    }

    void D3D9GraphicsBackend::GetViewportSize(int& width, int& height)
    {
        width = width_;
        height = height_;
    }

    void D3D9GraphicsBackend::SetVirtualResolution(int width, int height)
    {
        virtualWidth_ = width;
        virtualHeight_ = height;
    }

    void D3D9GraphicsBackend::SetPresentationMode(int mode)
    {
        presentationMode_ = mode;
    }

    void D3D9GraphicsBackend::Clear(float r, float g, float b, float a)
    {
        ThrowIfDeviceLost();
        HRESULT hr = device_->Clear(0, nullptr, D3DCLEAR_TARGET,
                                     D3DCOLOR_COLORVALUE(r, g, b, a), 1.0f, 0);
        if (FAILED(hr))
            throw std::runtime_error("D3D9 Clear failed, hr=" + FormatHr(hr));
    }

    void D3D9GraphicsBackend::Present()
    {
        if (deviceLost_)
        {
            // Real XNA resilience: keep polling: do not resize/present/throw while recovering.
            PollDeviceLost();
            return;
        }

        EnsureDeviceSize();

        HRESULT hr = device_->Present(nullptr, nullptr, nullptr, nullptr);
        if (hr == D3DERR_DEVICELOST)
        {
            deviceLost_ = true;
            if (deviceEventCallback_) deviceEventCallback_(BackendDeviceEvent::Lost);
            return;
        }
        if (FAILED(hr))
            throw std::runtime_error("D3D9 Present failed, hr=" + FormatHr(hr));
    }

    void D3D9GraphicsBackend::ClearColorAndDepth(float r, float g, float b, float a, float depth)
    {
        ThrowIfDeviceLost();
        DWORD flags = D3DCLEAR_TARGET;
        if (HasDepthBuffer(depthStencilFormatOrdinal_)) flags |= D3DCLEAR_ZBUFFER;
        HRESULT hr = device_->Clear(0, nullptr, flags, D3DCOLOR_COLORVALUE(r, g, b, a), depth, 0);
        if (FAILED(hr))
            throw std::runtime_error("D3D9 ClearColorAndDepth failed, hr=" + FormatHr(hr));
    }

    void D3D9GraphicsBackend::ClearDepth(float depth)
    {
        ThrowIfDeviceLost();
        if (!HasDepthBuffer(depthStencilFormatOrdinal_)) return;
        HRESULT hr = device_->Clear(0, nullptr, D3DCLEAR_ZBUFFER, 0, depth, 0);
        if (FAILED(hr))
            throw std::runtime_error("D3D9 ClearDepth failed, hr=" + FormatHr(hr));
    }

    void D3D9GraphicsBackend::ClearStencil(int stencil)
    {
        ThrowIfDeviceLost();
        if (!HasStencilBuffer(depthStencilFormatOrdinal_)) return;
        HRESULT hr = device_->Clear(0, nullptr, D3DCLEAR_STENCIL, 0, 1.0f,
                                     static_cast<DWORD>(stencil));
        if (FAILED(hr))
            throw std::runtime_error("D3D9 ClearStencil failed, hr=" + FormatHr(hr));
    }

    void D3D9GraphicsBackend::ClearDepthAndStencil(float depth, int stencil)
    {
        ThrowIfDeviceLost();
        if (!HasDepthBuffer(depthStencilFormatOrdinal_)) return;
        DWORD flags = D3DCLEAR_ZBUFFER;
        if (HasStencilBuffer(depthStencilFormatOrdinal_)) flags |= D3DCLEAR_STENCIL;
        HRESULT hr = device_->Clear(0, nullptr, flags, 0, depth, static_cast<DWORD>(stencil));
        if (FAILED(hr))
            throw std::runtime_error("D3D9 ClearDepthAndStencil failed, hr=" + FormatHr(hr));
    }

    void D3D9GraphicsBackend::ClearColorAndStencil(float r, float g, float b, float a, int stencil)
    {
        ThrowIfDeviceLost();
        DWORD flags = D3DCLEAR_TARGET;
        if (HasStencilBuffer(depthStencilFormatOrdinal_)) flags |= D3DCLEAR_STENCIL;
        HRESULT hr = device_->Clear(0, nullptr, flags, D3DCOLOR_COLORVALUE(r, g, b, a), 1.0f,
                                     static_cast<DWORD>(stencil));
        if (FAILED(hr))
            throw std::runtime_error("D3D9 ClearColorAndStencil failed, hr=" + FormatHr(hr));
    }

    void D3D9GraphicsBackend::ClearColorDepthAndStencil(float r, float g, float b, float a,
                                                         float depth, int stencil)
    {
        ThrowIfDeviceLost();
        DWORD flags = D3DCLEAR_TARGET;
        if (HasDepthBuffer(depthStencilFormatOrdinal_)) flags |= D3DCLEAR_ZBUFFER;
        if (HasStencilBuffer(depthStencilFormatOrdinal_)) flags |= D3DCLEAR_STENCIL;
        HRESULT hr = device_->Clear(0, nullptr, flags, D3DCOLOR_COLORVALUE(r, g, b, a), depth,
                                     static_cast<DWORD>(stencil));
        if (FAILED(hr))
            throw std::runtime_error("D3D9 ClearColorDepthAndStencil failed, hr=" + FormatHr(hr));
    }

    void D3D9GraphicsBackend::ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels)
    {
        ThrowIfDeviceLost();
        if (w <= 0 || h <= 0) return;

        ComPtr<IDirect3DSurface9> backBuffer;
        HRESULT hr = device_->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO,
                                             backBuffer.ReleaseAndGetAddressOf());
        if (FAILED(hr))
            throw std::runtime_error("ReadBackbuffer: GetBackBuffer failed, hr=" + FormatHr(hr));

        D3DSURFACE_DESC desc{};
        backBuffer->GetDesc(&desc);

        // Only the two channel-order layouts D9-20 actually maps to a SurfaceFormat are handled --
        // Color (A8B8G8R8, already R,G,B,A in memory -- no swizzle needed) and ColorBgraEXT
        // (A8R8G8B8, B,G,R,A in memory -- needs an R/B swap). A general D3DFORMAT-to-RGBA8
        // converter is future work, not needed by anything this plan has built yet.
        bool swapRB;
        if (desc.Format == D3DFMT_A8B8G8R8) swapRB = false;
        else if (desc.Format == D3DFMT_A8R8G8B8) swapRB = true;
        else
            throw std::runtime_error(
                "ReadBackbuffer: back-buffer D3DFORMAT not yet supported for readback (only "
                "SurfaceFormat::Color/ColorBgraEXT handled so far)");

        ComPtr<IDirect3DSurface9> sysmem;
        hr = device_->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format,
                                                   D3DPOOL_SYSTEMMEM,
                                                   sysmem.ReleaseAndGetAddressOf(), nullptr);
        if (FAILED(hr))
            throw std::runtime_error(
                "ReadBackbuffer: CreateOffscreenPlainSurface failed, hr=" + FormatHr(hr));

        hr = device_->GetRenderTargetData(backBuffer.Get(), sysmem.Get());
        if (FAILED(hr))
            throw std::runtime_error("ReadBackbuffer: GetRenderTargetData failed, hr=" + FormatHr(hr));

        D3DLOCKED_RECT locked{};
        hr = sysmem->LockRect(&locked, nullptr, D3DLOCK_READONLY);
        if (FAILED(hr))
            throw std::runtime_error("ReadBackbuffer: LockRect failed, hr=" + FormatHr(hr));

        const int srcW = static_cast<int>(desc.Width);
        const int srcH = static_cast<int>(desc.Height);
        for (int row = 0; row < h; ++row)
        {
            uint8_t* dst = pixels + static_cast<std::size_t>(row) * static_cast<std::size_t>(w) * 4;
            const int srcY = y + row;
            if (srcY < 0 || srcY >= srcH || x >= srcW)
            {
                std::memset(dst, 0, static_cast<std::size_t>(w) * 4);
                continue;
            }
            const int copyW = std::max(0, std::min(w, srcW - std::max(x, 0)));
            const int srcX = std::max(x, 0);
            const uint8_t* src = static_cast<const uint8_t*>(locked.pBits)
                                + static_cast<std::size_t>(srcY) * static_cast<std::size_t>(locked.Pitch)
                                + static_cast<std::size_t>(srcX) * 4;
            if (!swapRB)
            {
                std::memcpy(dst, src, static_cast<std::size_t>(copyW) * 4);
            }
            else
            {
                for (int col = 0; col < copyW; ++col)
                {
                    dst[col * 4 + 0] = src[col * 4 + 2];
                    dst[col * 4 + 1] = src[col * 4 + 1];
                    dst[col * 4 + 2] = src[col * 4 + 0];
                    dst[col * 4 + 3] = src[col * 4 + 3];
                }
            }
            if (copyW < w)
                std::memset(dst + static_cast<std::size_t>(copyW) * 4, 0,
                            static_cast<std::size_t>(w - copyW) * 4);
        }

        sysmem->UnlockRect();
    }

    void D3D9GraphicsBackend::SetDepthTestEnabled(bool)
    {
        NotYetImplemented("D3D9", "SetDepthTestEnabled (see plan_dx9.md D9-61/D9-82)");
    }

    void D3D9GraphicsBackend::SetBlendEnabled(bool)
    {
        NotYetImplemented("D3D9", "SetBlendEnabled (see plan_dx9.md D9-60/D9-82)");
    }

    void D3D9GraphicsBackend::SetDepthWriteEnabled(bool)
    {
        NotYetImplemented("D3D9", "SetDepthWriteEnabled (see plan_dx9.md D9-61/D9-82)");
    }

    std::unique_ptr<IVertexBufferBackend> D3D9GraphicsBackend::CreateVertexBuffer(int vertex_capacity)
    {
        return std::make_unique<D3D9VertexBufferBackend>(*this, device_.Get(), vertex_capacity);
    }

    std::unique_ptr<IIndexBufferBackend> D3D9GraphicsBackend::CreateIndexBuffer16(int index_capacity)
    {
        return std::make_unique<D3D9IndexBufferBackend>(*this, device_.Get(), index_capacity, false);
    }

    std::unique_ptr<IIndexBufferBackend> D3D9GraphicsBackend::CreateIndexBuffer32(int index_capacity)
    {
        return std::make_unique<D3D9IndexBufferBackend>(*this, device_.Get(), index_capacity, true);
    }

    void D3D9GraphicsBackend::DrawColoredPrimitives(const IVertexBufferBackend&,
                                                     const Matrix&, const Matrix&, const Matrix&,
                                                     PrimitiveType, int)
    {
        NotYetImplemented("D3D9", "DrawColoredPrimitives (see plan_dx9.md D9-82)");
    }

    void D3D9GraphicsBackend::DrawIndexedColoredPrimitives(const IVertexBufferBackend&, const IIndexBufferBackend&,
                                                            const Matrix&, const Matrix&, const Matrix&,
                                                            PrimitiveType, int)
    {
        NotYetImplemented("D3D9", "DrawIndexedColoredPrimitives (see plan_dx9.md D9-82)");
    }

    std::unique_ptr<ITextureBackend> D3D9GraphicsBackend::CreateTexture(const ImageData& data)
    {
        return std::make_unique<D3D9TextureBackend>(device_.Get(), data);
    }

    std::unique_ptr<ITextureCubeBackend> D3D9GraphicsBackend::CreateTextureCube(int size, bool mipMap, int surfaceFormat)
    {
        if (!(caps_.TextureCaps & D3DPTEXTURECAPS_CUBEMAP)) return nullptr;
        return std::make_unique<D3D9TextureCubeBackend>(device_.Get(), size, mipMap, surfaceFormat);
    }

    std::unique_ptr<ITexture3DBackend> D3D9GraphicsBackend::CreateTexture3D(int w, int h, int depth, bool mipMap, int surfaceFormat)
    {
        if (caps_.MaxVolumeExtent == 0) return nullptr;
        return std::make_unique<D3D9Texture3DBackend>(device_.Get(), w, h, depth, mipMap, surfaceFormat);
    }

    std::unique_ptr<ISpriteBatchBackend> D3D9GraphicsBackend::CreateSpriteBatch()
    {
        NotYetImplemented("D3D9", "CreateSpriteBatch (see plan_dx9.md D9-90)");
    }

    void D3D9GraphicsBackend::SetSwapInterval(int)
    {
        NotYetImplemented("D3D9", "SetSwapInterval (see plan_dx9.md D9-30)");
    }

    void D3D9GraphicsBackend::SetRenderTarget2D(IRenderTargetBackend*)
    {
        NotYetImplemented("D3D9", "SetRenderTarget2D (see plan_dx9.md D9-53)");
    }

    void D3D9GraphicsBackend::ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                                               int colorDstBlend, int alphaDstBlend,
                                               int colorBlendFunc, int alphaBlendFunc)
    {
        device_->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        device_->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, TRUE);
        device_->SetRenderState(D3DRS_SRCBLEND, BlendToD3D9(colorSrcBlend));
        device_->SetRenderState(D3DRS_DESTBLEND, BlendToD3D9(colorDstBlend));
        device_->SetRenderState(D3DRS_BLENDOP, BlendFunctionToD3D9(colorBlendFunc));
        device_->SetRenderState(D3DRS_SRCBLENDALPHA, BlendToD3D9(alphaSrcBlend));
        device_->SetRenderState(D3DRS_DESTBLENDALPHA, BlendToD3D9(alphaDstBlend));
        device_->SetRenderState(D3DRS_BLENDOPALPHA, BlendFunctionToD3D9(alphaBlendFunc));
    }

    void D3D9GraphicsBackend::SetBlendFactor(float r, float g, float b, float a)
    {
        device_->SetRenderState(D3DRS_BLENDFACTOR, D3DCOLOR_COLORVALUE(r, g, b, a));
    }

    void D3D9GraphicsBackend::ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable,
                                                      int depthFunc,
                                                      bool stencilEnable, int stencilFunc,
                                                      int stencilPass, int stencilFail, int stencilDepthFail,
                                                      int stencilMask, int stencilWriteMask, int referenceStencil,
                                                      bool twoSidedStencilMode,
                                                      int ccwStencilFunc, int ccwStencilPass,
                                                      int ccwStencilFail, int ccwStencilDepthFail)
    {
        device_->SetRenderState(D3DRS_ZENABLE, depthEnable ? D3DZB_TRUE : D3DZB_FALSE);
        device_->SetRenderState(D3DRS_ZWRITEENABLE, depthWriteEnable ? TRUE : FALSE);
        device_->SetRenderState(D3DRS_ZFUNC, CompareFunctionToD3D9(depthFunc));

        device_->SetRenderState(D3DRS_STENCILENABLE, stencilEnable ? TRUE : FALSE);
        device_->SetRenderState(D3DRS_STENCILFUNC, CompareFunctionToD3D9(stencilFunc));
        device_->SetRenderState(D3DRS_STENCILPASS, StencilOperationToD3D9(stencilPass));
        device_->SetRenderState(D3DRS_STENCILFAIL, StencilOperationToD3D9(stencilFail));
        device_->SetRenderState(D3DRS_STENCILZFAIL, StencilOperationToD3D9(stencilDepthFail));
        device_->SetRenderState(D3DRS_STENCILMASK, static_cast<DWORD>(stencilMask));
        device_->SetRenderState(D3DRS_STENCILWRITEMASK, static_cast<DWORD>(stencilWriteMask));
        device_->SetRenderState(D3DRS_STENCILREF, static_cast<DWORD>(referenceStencil));

        device_->SetRenderState(D3DRS_TWOSIDEDSTENCILMODE, twoSidedStencilMode ? TRUE : FALSE);
        device_->SetRenderState(D3DRS_CCW_STENCILFUNC, CompareFunctionToD3D9(ccwStencilFunc));
        device_->SetRenderState(D3DRS_CCW_STENCILPASS, StencilOperationToD3D9(ccwStencilPass));
        device_->SetRenderState(D3DRS_CCW_STENCILFAIL, StencilOperationToD3D9(ccwStencilFail));
        device_->SetRenderState(D3DRS_CCW_STENCILZFAIL, StencilOperationToD3D9(ccwStencilDepthFail));
    }

    void D3D9GraphicsBackend::SetReferenceStencil(int value)
    {
        device_->SetRenderState(D3DRS_STENCILREF, static_cast<DWORD>(value));
    }

    void D3D9GraphicsBackend::ApplyRasterizerState(int cullMode, int fillMode,
                                                    bool scissorTestEnable,
                                                    float depthBias, float slopeScaleDepthBias)
    {
        device_->SetRenderState(D3DRS_CULLMODE, CullModeToD3D9(cullMode));
        device_->SetRenderState(D3DRS_FILLMODE, FillModeToD3D9(fillMode));
        device_->SetRenderState(D3DRS_SCISSORTESTENABLE, scissorTestEnable ? TRUE : FALSE);
        // D3D9's D3DRS_DEPTHBIAS/SLOPESCALEDEPTHBIAS are floats (unlike D3D11's INT DepthBias) --
        // XNA's own float DepthBias/SlopeScaleDepthBias (Task 767's "r"-scaled convention) map
        // through directly, with no unit conversion needed (D3D11 needed to round to an INT; D3D9
        // needs no such step since both are already float) -- SetRenderState() still takes a DWORD
        // parameter for these, so the float bits are reinterpreted, not numerically converted.
        device_->SetRenderState(D3DRS_DEPTHBIAS, std::bit_cast<DWORD>(depthBias));
        device_->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS, std::bit_cast<DWORD>(slopeScaleDepthBias));
    }

    void D3D9GraphicsBackend::SetScissorRect(int x, int y, int w, int h)
    {
        RECT rect{};
        rect.left = x;
        rect.top = y;
        rect.right = x + w;
        rect.bottom = y + h;
        device_->SetScissorRect(&rect);
    }

    void D3D9GraphicsBackend::ApplySamplerState(int, int, int, int, int)
    {
        NotYetImplemented("D3D9", "ApplySamplerState (see plan_dx9.md D9-63)");
    }

    void D3D9GraphicsBackend::SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth)
    {
        D3DVIEWPORT9 vp{};
        vp.X = static_cast<DWORD>(std::max(0, x));
        vp.Y = static_cast<DWORD>(std::max(0, y));
        vp.Width = static_cast<DWORD>(std::max(0, w));
        vp.Height = static_cast<DWORD>(std::max(0, h));
        vp.MinZ = minDepth;
        vp.MaxZ = maxDepth;
        HRESULT hr = device_->SetViewport(&vp);
        if (FAILED(hr))
            throw std::runtime_error("D3D9 SetViewport failed, hr=" + FormatHr(hr));
    }

    void D3D9GraphicsBackend::SetContextRecoveryEnabled(bool)
    {
        NotYetImplemented("D3D9", "SetContextRecoveryEnabled");
    }

    void D3D9GraphicsBackend::SetStringMarkerEXT(const char*)
    {
        NotYetImplemented("D3D9", "SetStringMarkerEXT");
    }

    void D3D9GraphicsBackend::DebugSimulateContextLoss()
    {
        // D9-34: this is the pre-existing, GraphicsDevice-calls-into-backend test channel (design
        // decision: reuse it rather than invent a new one) -- genuinely sets deviceLost_ and fires
        // the real DeviceLost event, even though nothing actually lost the D3D9 device. Lets the
        // full Lost->Resetting->Reset event sequence and a real Reset() call be exercised
        // deterministically, since DXVK will rarely lose the device naturally (design decision 2's
        // own documented cost).
        if (deviceLost_) return;
        deviceLost_ = true;
        if (deviceEventCallback_) deviceEventCallback_(BackendDeviceEvent::Lost);
    }

    void D3D9GraphicsBackend::DebugRestoreContext()
    {
        // D9-34: the simulated counterpart of PollDeviceLost() -- skips the real
        // TestCooperativeLevel() wait (nothing genuinely lost the device, so it would never report
        // D3DERR_DEVICENOTRESET) and goes straight to the real Resetting->Reset()->Reset sequence.
        if (!deviceLost_) return;
        PerformResetRecovery();
    }
}

namespace CNA::Internal::Backends
{
    std::unique_ptr<IGraphicsBackend> CreateGraphicsBackend(const GraphicsBackendCreateArgs& args)
    {
        return std::make_unique<D3D9::D3D9GraphicsBackend>(args);
    }
}
