// plan_dx.md Phase DX2/DX4: D3D11 backend skeleton + device/swap-chain/back-buffer.
#include "CNA/Internal/Backends/D3D11/D3D11GraphicsBackend.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace CNA::Internal::Backends::D3D11
{
    namespace
    {
        std::string FormatHr(HRESULT hr)
        {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "0x%08lX", static_cast<unsigned long>(hr));
            return buf;
        }
    }

    D3D11GraphicsBackend::D3D11GraphicsBackend(const GraphicsBackendCreateArgs& args)
        : window_(args.window)
        , virtualWidth_(args.virtualWidth)
        , virtualHeight_(args.virtualHeight)
    {
        vsyncEnabled_ = args.swapInterval > 0;

        if (window_)
        {
            SDL_GetWindowSizeInPixels(window_, &width_, &height_);
        }
        if (width_ <= 0) width_ = args.virtualWidth > 0 ? args.virtualWidth : 1024;
        if (height_ <= 0) height_ = args.virtualHeight > 0 ? args.virtualHeight : 768;

        CreateDeviceResources();
        CreateSwapChainResources();
        CreateWindowSizeDependentViews();

        SDL_Log("[D3D11] Backend initialised (%dx%d), feature level 0x%04x, debug layer %s, tearing %s",
                width_, height_, static_cast<unsigned>(featureLevel_),
                debugLayerEnabled_ ? "enabled" : "disabled",
                allowTearingSupported_ ? "supported" : "unsupported");
    }

    D3D11GraphicsBackend::~D3D11GraphicsBackend() = default;

    void D3D11GraphicsBackend::CreateDeviceResources()
    {
        static const D3D_FEATURE_LEVEL kFeatureLevels[] = {
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0,
        };

        auto tryCreate = [&](UINT flags, const D3D_FEATURE_LEVEL* levels, UINT levelCount) -> HRESULT
        {
            return D3D11CreateDevice(
                nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                levels, levelCount, D3D11_SDK_VERSION,
                device_.ReleaseAndGetAddressOf(), &featureLevel_, context_.ReleaseAndGetAddressOf());
        };

        UINT flags = 0;
#ifndef NDEBUG
        flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

        HRESULT hr = tryCreate(flags, kFeatureLevels, ARRAYSIZE(kFeatureLevels));

        // design decision 12: the debug layer is best-effort, never a hard requirement.
        if (FAILED(hr) && (flags & D3D11_CREATE_DEVICE_DEBUG) && hr == DXGI_ERROR_SDK_COMPONENT_MISSING)
        {
            SDL_Log("[D3D11] D3D11 debug layer unavailable; retrying without it.");
            flags &= ~D3D11_CREATE_DEVICE_DEBUG;
            hr = tryCreate(flags, kFeatureLevels, ARRAYSIZE(kFeatureLevels));
        }

        // design decision 12: some drivers reject an explicit 11_1 request outright.
        if (hr == E_INVALIDARG)
        {
            SDL_Log("[D3D11] Feature level 11_1 rejected (E_INVALIDARG); retrying without it.");
            hr = tryCreate(flags, kFeatureLevels + 1, ARRAYSIZE(kFeatureLevels) - 1);
        }

        if (FAILED(hr))
        {
            throw std::runtime_error("D3D11CreateDevice failed, hr=" + FormatHr(hr));
        }

        debugLayerEnabled_ = (flags & D3D11_CREATE_DEVICE_DEBUG) != 0;

        // design decision 12: negotiation is broad, but acceptance is a hard floor -- Phase DX8's
        // Shader Model 5 stock shaders need feature level 11.0+.
        if (featureLevel_ < D3D_FEATURE_LEVEL_11_0)
        {
            throw std::runtime_error(
                "CNA's D3D11 backend requires feature level 11_0+; GPU only reports 0x"
                + FormatHr(featureLevel_));
        }

        // DX-22: factory chain + tearing-capability query (device lifetime -- done once).
        ComPtr<IDXGIDevice> dxgiDevice;
        hr = device_.As(&dxgiDevice);
        if (FAILED(hr))
            throw std::runtime_error("QueryInterface(IDXGIDevice) failed, hr=" + FormatHr(hr));

        ComPtr<IDXGIAdapter> adapter;
        hr = dxgiDevice->GetParent(IID_PPV_ARGS(adapter.ReleaseAndGetAddressOf()));
        if (FAILED(hr))
            throw std::runtime_error("IDXGIDevice::GetParent(IDXGIAdapter) failed, hr=" + FormatHr(hr));

        hr = adapter->GetParent(IID_PPV_ARGS(factory_.ReleaseAndGetAddressOf()));
        if (FAILED(hr))
            throw std::runtime_error("IDXGIAdapter::GetParent(IDXGIFactory2) failed, hr=" + FormatHr(hr));

        ComPtr<IDXGIFactory5> factory5;
        if (SUCCEEDED(factory_.As(&factory5)))
        {
            BOOL allowTearing = FALSE;
            if (SUCCEEDED(factory5->CheckFeatureSupport(
                    DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof(allowTearing))))
            {
                allowTearingSupported_ = allowTearing != FALSE;
            }
        }
    }

    void D3D11GraphicsBackend::CreateSwapChainResources()
    {
        if (!window_)
            throw std::runtime_error("D3D11GraphicsBackend: no window available to create a swap chain for");

        HWND hwnd = static_cast<HWND>(SDL_GetPointerProperty(
            SDL_GetWindowProperties(window_), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
        if (!hwnd)
            throw std::runtime_error("D3D11GraphicsBackend: could not obtain HWND from SDL window");

        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width = static_cast<UINT>(width_);
        desc.Height = static_cast<UINT>(height_);
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // maps to XNA SurfaceFormat::Color (DX-11-fmt)
        desc.SampleDesc.Count = 1; // flip-model swap chains are never MSAA directly (DX-45's note)
        desc.SampleDesc.Quality = 0;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        desc.Flags = (allowTearingSupported_ && allowTearingRequested_) ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

        HRESULT hr = factory_->CreateSwapChainForHwnd(
            device_.Get(), hwnd, &desc, nullptr, nullptr, swapChain_.ReleaseAndGetAddressOf());
        if (FAILED(hr))
            throw std::runtime_error("CreateSwapChainForHwnd failed, hr=" + FormatHr(hr));
    }

    void D3D11GraphicsBackend::CreateWindowSizeDependentViews()
    {
        HRESULT hr = swapChain_->GetBuffer(0, IID_PPV_ARGS(backBufferTexture_.ReleaseAndGetAddressOf()));
        if (FAILED(hr))
            throw std::runtime_error("IDXGISwapChain1::GetBuffer failed, hr=" + FormatHr(hr));

        hr = device_->CreateRenderTargetView(
            backBufferTexture_.Get(), nullptr, backBufferRTV_.ReleaseAndGetAddressOf());
        if (FAILED(hr))
            throw std::runtime_error("CreateRenderTargetView failed, hr=" + FormatHr(hr));

        D3D11_TEXTURE2D_DESC depthDesc{};
        depthDesc.Width = static_cast<UINT>(width_);
        depthDesc.Height = static_cast<UINT>(height_);
        depthDesc.MipLevels = 1;
        depthDesc.ArraySize = 1;
        depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        depthDesc.SampleDesc.Count = 1;
        depthDesc.Usage = D3D11_USAGE_DEFAULT;
        depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

        hr = device_->CreateTexture2D(&depthDesc, nullptr, depthStencilTexture_.ReleaseAndGetAddressOf());
        if (FAILED(hr))
            throw std::runtime_error("CreateTexture2D(depth) failed, hr=" + FormatHr(hr));

        hr = device_->CreateDepthStencilView(
            depthStencilTexture_.Get(), nullptr, depthStencilView_.ReleaseAndGetAddressOf());
        if (FAILED(hr))
            throw std::runtime_error("CreateDepthStencilView failed, hr=" + FormatHr(hr));

        ID3D11RenderTargetView* rtv = backBufferRTV_.Get();
        context_->OMSetRenderTargets(1, &rtv, depthStencilView_.Get());

        D3D11_VIEWPORT vp{};
        vp.TopLeftX = 0.0f;
        vp.TopLeftY = 0.0f;
        vp.Width = static_cast<float>(width_);
        vp.Height = static_cast<float>(height_);
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        context_->RSSetViewports(1, &vp);
    }

    void D3D11GraphicsBackend::ReleaseWindowSizeDependentViews()
    {
        ID3D11RenderTargetView* nullRtv[] = { nullptr };
        context_->OMSetRenderTargets(1, nullRtv, nullptr);
        depthStencilView_.Reset();
        depthStencilTexture_.Reset();
        backBufferRTV_.Reset();
        backBufferTexture_.Reset();
    }

    void D3D11GraphicsBackend::EnsureSwapChainSize()
    {
        if (!window_ || !swapChain_) return;

        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(window_, &w, &h);
        if (w <= 0 || h <= 0) return;
        if (w == width_ && h == height_) return;

        // DX-29: only the window-size group is torn down here -- device_/context_/factory_
        // (DX-20/21/22) and the swap chain object itself (DX-23) are untouched.
        ReleaseWindowSizeDependentViews();
        context_->Flush();

        const UINT flags =
            (allowTearingSupported_ && allowTearingRequested_) ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
        HRESULT hr = swapChain_->ResizeBuffers(
            0, static_cast<UINT>(w), static_cast<UINT>(h), DXGI_FORMAT_UNKNOWN, flags);
        if (FAILED(hr))
        {
            CheckDeviceRemoved(hr);
            throw std::runtime_error("IDXGISwapChain1::ResizeBuffers failed, hr=" + FormatHr(hr));
        }

        width_ = w;
        height_ = h;
        CreateWindowSizeDependentViews();
    }

    void D3D11GraphicsBackend::CheckDeviceRemoved(HRESULT hr) const
    {
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
        {
            const HRESULT reason = device_ ? device_->GetDeviceRemovedReason() : hr;
            SDL_Log("[D3D11] Device removed/reset! reason=%s", FormatHr(reason).c_str());
        }
    }

    void D3D11GraphicsBackend::Clear(float r, float g, float b, float a)
    {
        const float color[4] = { r, g, b, a };
        context_->ClearRenderTargetView(backBufferRTV_.Get(), color);
    }

    void D3D11GraphicsBackend::Present()
    {
        EnsureSwapChainSize();

        const bool mayTear =
            allowTearingSupported_ && allowTearingRequested_ && !vsyncEnabled_ && !exclusiveFullscreen_;
        const UINT syncInterval = vsyncEnabled_ ? 1 : 0;
        const UINT flags = mayTear ? DXGI_PRESENT_ALLOW_TEARING : 0;

        const HRESULT hr = swapChain_->Present(syncInterval, flags);
        if (FAILED(hr))
            CheckDeviceRemoved(hr);
    }

    void D3D11GraphicsBackend::GetViewportSize(int& width, int& height)
    {
        if (window_)
        {
            SDL_GetWindowSizeInPixels(window_, &width, &height);
        }
        else
        {
            width = width_;
            height = height_;
        }
    }

    void D3D11GraphicsBackend::SetVirtualResolution(int width, int height)
    {
        virtualWidth_ = width;
        virtualHeight_ = height;
    }

    void D3D11GraphicsBackend::SetPresentationMode(int mode)
    {
        (void)mode; // presentation-mode scaling: not yet implemented (out of Phase DX4's scope)
    }

    void D3D11GraphicsBackend::SetSwapInterval(int interval)
    {
        // DX-26: sync interval is backend state applied at the next Present(), not a direct
        // D3D11 API call -- there is no "set swap interval" entry point to call ahead of time.
        vsyncEnabled_ = interval > 0;
    }

    void D3D11GraphicsBackend::ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels)
    {
        if (!backBufferTexture_ || w <= 0 || h <= 0)
            return;

        D3D11_TEXTURE2D_DESC desc{};
        backBufferTexture_->GetDesc(&desc);

        D3D11_TEXTURE2D_DESC stagingDesc = desc;
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.BindFlags = 0;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        stagingDesc.MiscFlags = 0;

        ComPtr<ID3D11Texture2D> staging;
        HRESULT hr = device_->CreateTexture2D(&stagingDesc, nullptr, staging.GetAddressOf());
        if (FAILED(hr))
            throw std::runtime_error("ReadBackbuffer: staging texture creation failed, hr=" + FormatHr(hr));

        context_->CopyResource(staging.Get(), backBufferTexture_.Get());

        D3D11_MAPPED_SUBRESOURCE mapped{};
        hr = context_->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
        if (FAILED(hr))
            throw std::runtime_error("ReadBackbuffer: Map failed, hr=" + FormatHr(hr));

        // DX-28: must honor RowPitch per row -- the mapped rows are not guaranteed tightly packed.
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
            const uint8_t* src = static_cast<const uint8_t*>(mapped.pData)
                                + static_cast<std::size_t>(srcY) * mapped.RowPitch
                                + static_cast<std::size_t>(srcX) * 4;
            std::memcpy(dst, src, static_cast<std::size_t>(copyW) * 4);
            if (copyW < w)
                std::memset(dst + static_cast<std::size_t>(copyW) * 4, 0,
                            static_cast<std::size_t>(w - copyW) * 4);
        }

        context_->Unmap(staging.Get(), 0);
    }

    void D3D11GraphicsBackend::ClearColorAndDepth(float r, float g, float b, float a, float depth)
    {
        Clear(r, g, b, a);
        context_->ClearDepthStencilView(depthStencilView_.Get(), D3D11_CLEAR_DEPTH, depth, 0);
    }

    void D3D11GraphicsBackend::ClearDepth(float depth)
    {
        context_->ClearDepthStencilView(depthStencilView_.Get(), D3D11_CLEAR_DEPTH, depth, 0);
    }

    void D3D11GraphicsBackend::ClearStencil(int stencil)
    {
        context_->ClearDepthStencilView(
            depthStencilView_.Get(), D3D11_CLEAR_STENCIL, 1.0f, static_cast<UINT8>(stencil));
    }

    void D3D11GraphicsBackend::ClearDepthAndStencil(float depth, int stencil)
    {
        context_->ClearDepthStencilView(
            depthStencilView_.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, depth,
            static_cast<UINT8>(stencil));
    }

    void D3D11GraphicsBackend::ClearColorAndStencil(float r, float g, float b, float a, int stencil)
    {
        Clear(r, g, b, a);
        context_->ClearDepthStencilView(
            depthStencilView_.Get(), D3D11_CLEAR_STENCIL, 1.0f, static_cast<UINT8>(stencil));
    }

    void D3D11GraphicsBackend::ClearColorDepthAndStencil(
        float r, float g, float b, float a, float depth, int stencil)
    {
        Clear(r, g, b, a);
        context_->ClearDepthStencilView(
            depthStencilView_.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, depth,
            static_cast<UINT8>(stencil));
    }

    // State setters: stored/applied for real once Phase DX7 (state objects) lands. Storing them
    // as no-ops now (rather than throwing) matches this project's own precedent (SOFTWARE-3/
    // HEADLESS-3) of a skeleton that's honest about what's not real yet without crashing normal
    // GraphicsDevice construction, which applies default state on every backend.
    void D3D11GraphicsBackend::SetDepthTestEnabled(bool enabled) { (void)enabled; }
    void D3D11GraphicsBackend::SetBlendEnabled(bool enabled) { (void)enabled; }
    void D3D11GraphicsBackend::SetDepthWriteEnabled(bool enabled) { (void)enabled; }

    std::unique_ptr<ITextureBackend> D3D11GraphicsBackend::CreateTexture(const ImageData& data)
    {
        (void)data;
        throw std::runtime_error("D3D11GraphicsBackend::CreateTexture: not yet implemented (plan_dx.md Phase DX6)");
    }

    std::unique_ptr<ISpriteBatchBackend> D3D11GraphicsBackend::CreateSpriteBatch()
    {
        throw std::runtime_error("D3D11GraphicsBackend::CreateSpriteBatch: not yet implemented (plan_dx.md Phase DX9)");
    }

    std::unique_ptr<IVertexBufferBackend> D3D11GraphicsBackend::CreateVertexBuffer(int vertex_capacity)
    {
        (void)vertex_capacity;
        throw std::runtime_error("D3D11GraphicsBackend::CreateVertexBuffer: not yet implemented (plan_dx.md Phase DX5)");
    }

    std::unique_ptr<IIndexBufferBackend> D3D11GraphicsBackend::CreateIndexBuffer16(int index_capacity)
    {
        (void)index_capacity;
        throw std::runtime_error("D3D11GraphicsBackend::CreateIndexBuffer16: not yet implemented (plan_dx.md Phase DX5)");
    }

    void D3D11GraphicsBackend::DrawColoredPrimitives(
        const IVertexBufferBackend& vb, const Matrix& world, const Matrix& view, const Matrix& projection,
        PrimitiveType primitive, int primitiveCount)
    {
        (void)vb; (void)world; (void)view; (void)projection; (void)primitive; (void)primitiveCount;
        throw std::runtime_error("D3D11GraphicsBackend::DrawColoredPrimitives: not yet implemented (plan_dx.md Phase DX8)");
    }

    void D3D11GraphicsBackend::DrawIndexedColoredPrimitives(
        const IVertexBufferBackend& vb, const IIndexBufferBackend& ib,
        const Matrix& world, const Matrix& view, const Matrix& projection,
        PrimitiveType primitive, int primitiveCount)
    {
        (void)vb; (void)ib; (void)world; (void)view; (void)projection; (void)primitive; (void)primitiveCount;
        throw std::runtime_error("D3D11GraphicsBackend::DrawIndexedColoredPrimitives: not yet implemented (plan_dx.md Phase DX8)");
    }
}

namespace CNA::Internal::Backends
{
    std::unique_ptr<IGraphicsBackend> CreateGraphicsBackend(const GraphicsBackendCreateArgs& args)
    {
        return std::make_unique<D3D11::D3D11GraphicsBackend>(args);
    }
}
