// plan_dx.md Phase DX2/DX4: D3D11 backend skeleton + device/swap-chain/back-buffer.
#include "CNA/Internal/Backends/D3D11/D3D11GraphicsBackend.hpp"
#include "CNA/Internal/Backends/D3D11/D3D11Buffers.hpp"
#include "CNA/Internal/Backends/D3D11/D3D11Textures.hpp"
#include "CNA/Internal/Backends/D3D11/D3D11RenderTargets.hpp"
#include "CNA/Internal/Backends/D3D11/D3D11OcclusionQuery.hpp"
#include "CNA/Internal/Backends/D3D11/D3D11EffectBackend.hpp"
#include "CNA/Internal/Backends/D3D11/D3D11SpriteBatch.hpp"
#include "CNA/Internal/Backends/D3DCommon/D3DShaderCache.hpp"
#include "CNA/Internal/Backends/D3DCommon/D3DConstantBuffers.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>

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

        /// Same per-PrimitiveType vertex-count formula every other backend duplicates locally
        /// (Vulkan/EasyGL's own VertexCountForPrimitives) -- not shared via a common header today,
        /// so this follows the existing precedent rather than introducing a new one.
        int VertexCountForPrimitives(PrimitiveType pt, int primitiveCount)
        {
            switch (pt)
            {
            case PrimitiveType::TriangleList:  return primitiveCount * 3;
            case PrimitiveType::TriangleStrip: return primitiveCount + 2;
            case PrimitiveType::LineList:      return primitiveCount * 2;
            case PrimitiveType::LineStrip:     return primitiveCount + 1;
            }
            return 0;
        }

        D3D11_PRIMITIVE_TOPOLOGY ToD3D11Topology(PrimitiveType pt)
        {
            switch (pt)
            {
            case PrimitiveType::TriangleList:  return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
            case PrimitiveType::TriangleStrip: return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
            case PrimitiveType::LineList:      return D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
            case PrimitiveType::LineStrip:     return D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP;
            }
            return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        }

        /// DX-62: resolves the real SRV to bind for a GpuDrawParams texture slot. Two concrete
        /// backend types can appear here -- a plain D3D11TextureBackend, or a D3D11RenderTargetBackend
        /// used as a sampled texture (IRenderTargetBackend derives ITextureBackend) -- neither shares
        /// a common "GetShaderResourceViewEXT()" base, so this tries both. Returns nullptr (an
        /// explicit unbind, not an error) if @p tex is null or neither cast matches.
        ID3D11ShaderResourceView* GetSrvForTextureEXT(const ITextureBackend* tex)
        {
            if (tex == nullptr) return nullptr;
            if (const auto* t = dynamic_cast<const D3D11TextureBackend*>(tex))
                return t->GetShaderResourceViewEXT();
            if (const auto* rt = dynamic_cast<const D3D11RenderTargetBackend*>(tex))
                return rt->GetShaderResourceViewEXT();
            return nullptr;
        }

        /// DX-66: same two-concrete-type resolution as GetSrvForTextureEXT above, but for
        /// ITextureCubeBackend (env_map3d's TextureCube) -- a plain D3D11TextureCubeBackend, or a
        /// D3D11RenderTargetCubeBackend used as a sampled cube texture.
        ID3D11ShaderResourceView* GetSrvForTextureCubeEXT(const ITextureCubeBackend* tex)
        {
            if (tex == nullptr) return nullptr;
            if (const auto* t = dynamic_cast<const D3D11TextureCubeBackend*>(tex))
                return t->GetShaderResourceViewEXT();
            if (const auto* rt = dynamic_cast<const D3D11RenderTargetCubeBackend*>(tex))
                return rt->GetShaderResourceViewEXT();
            return nullptr;
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

        // Phase DX6: Clear()/ClearX target whatever's tracked here -- initialise/reset it to the
        // back buffer every time this is (re)created (construction, and DX-29 resize).
        currentColorRTVs_[0] = backBufferRTV_.Get();
        currentRTVCount_ = 1;
        currentDSV_ = depthStencilView_.Get();
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
        // Phase DX6: clears whatever's currently bound (custom render target(s) or MRT set), not
        // always the back buffer -- matches every other backend's "clear the active target(s)"
        // semantics once SetRenderTarget2D/SetRenderTargets has bound something.
        for (int i = 0; i < currentRTVCount_; ++i)
        {
            if (currentColorRTVs_[i]) context_->ClearRenderTargetView(currentColorRTVs_[i], color);
        }
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
        if (currentDSV_) context_->ClearDepthStencilView(currentDSV_, D3D11_CLEAR_DEPTH, depth, 0);
    }

    void D3D11GraphicsBackend::ClearDepth(float depth)
    {
        if (currentDSV_) context_->ClearDepthStencilView(currentDSV_, D3D11_CLEAR_DEPTH, depth, 0);
    }

    void D3D11GraphicsBackend::ClearStencil(int stencil)
    {
        if (currentDSV_)
            context_->ClearDepthStencilView(
                currentDSV_, D3D11_CLEAR_STENCIL, 1.0f, static_cast<UINT8>(stencil));
    }

    void D3D11GraphicsBackend::ClearDepthAndStencil(float depth, int stencil)
    {
        if (currentDSV_)
            context_->ClearDepthStencilView(
                currentDSV_, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, depth, static_cast<UINT8>(stencil));
    }

    void D3D11GraphicsBackend::ClearColorAndStencil(float r, float g, float b, float a, int stencil)
    {
        Clear(r, g, b, a);
        if (currentDSV_)
            context_->ClearDepthStencilView(
                currentDSV_, D3D11_CLEAR_STENCIL, 1.0f, static_cast<UINT8>(stencil));
    }

    void D3D11GraphicsBackend::ClearColorDepthAndStencil(
        float r, float g, float b, float a, float depth, int stencil)
    {
        Clear(r, g, b, a);
        if (currentDSV_)
            context_->ClearDepthStencilView(
                currentDSV_, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, depth, static_cast<UINT8>(stencil));
    }

    // State setters: stored/applied for real once Phase DX7 (state objects) lands. Storing them
    // as no-ops now (rather than throwing) matches this project's own precedent (SOFTWARE-3/
    // HEADLESS-3) of a skeleton that's honest about what's not real yet without crashing normal
    // GraphicsDevice construction, which applies default state on every backend.
    // These carry a single bool each, so they rebuild the tracked depth-stencil state with just that
    // one field changed (see the ds*_ fields' own comment). They were silent no-ops before: a game
    // calling GraphicsDevice::SetDepthTestEnabled(true) on D3D11 got no depth test at all, while
    // EasyGL honoured it -- a real, silent cross-backend behavior divergence, found while wiring
    // D3D12's own equivalents (which were worse still: they threw).
    void D3D11GraphicsBackend::SetDepthTestEnabled(bool enabled)
    {
        if (dsDepthEnable_ == enabled) return;
        dsDepthEnable_ = enabled;
        RebindDepthStencilState();
    }

    void D3D11GraphicsBackend::SetDepthWriteEnabled(bool enabled)
    {
        if (dsDepthWriteEnable_ == enabled) return;
        dsDepthWriteEnable_ = enabled;
        RebindDepthStencilState();
    }

    // Deliberate no-op, matching D3D12's own equivalent: a bare "enable blending" has no defined
    // blend factors in XNA -- real blend configuration always arrives via ApplyBlendState().
    void D3D11GraphicsBackend::SetBlendEnabled(bool enabled) { (void)enabled; }

    std::unique_ptr<ITextureBackend> D3D11GraphicsBackend::CreateTexture(const ImageData& data)
    {
        return std::make_unique<D3D11TextureBackend>(device_.Get(), context_.Get(), data);
    }

    std::unique_ptr<ITexture3DBackend> D3D11GraphicsBackend::CreateTexture3D(
        int w, int h, int depth, bool mipMap, int surfaceFormat)
    {
        return std::make_unique<D3D11Texture3DBackend>(device_.Get(), context_.Get(), w, h, depth, mipMap, surfaceFormat);
    }

    std::unique_ptr<ITextureCubeBackend> D3D11GraphicsBackend::CreateTextureCube(
        int size, bool mipMap, int surfaceFormat)
    {
        return std::make_unique<D3D11TextureCubeBackend>(device_.Get(), context_.Get(), size, mipMap, surfaceFormat);
    }

    std::unique_ptr<IRenderTargetBackend> D3D11GraphicsBackend::CreateRenderTarget2D(
        int w, int h, int depthFormat, bool preserveContents, bool mipMap, int multiSampleCount)
    {
        (void)preserveContents; // D3D11_USAGE_DEFAULT + ResolveSubresource-on-unbind already always
                                 // preserves prior contents across binds (no "discard on bind" path
                                 // exists in this backend) -- matches EasyGL/Vulkan's own honoring
                                 // of RenderTargetUsage as a hint GraphicsDevice.SetRenderTarget()
                                 // itself acts on (an explicit Clear() call), not something the
                                 // backend needs to special-case at creation time.
        return std::make_unique<D3D11::D3D11RenderTargetBackend>(
            this, device_.Get(), context_.Get(), w, h, depthFormat, mipMap, multiSampleCount);
    }

    void D3D11GraphicsBackend::FlushPendingMRTResolveEXT()
    {
        if (currentMRTCount_ <= 0) return;
        for (int i = 0; i < currentMRTCount_; ++i)
        {
            if (currentMRTTargets_[i]) currentMRTTargets_[i]->ResolveAndGenerateMipsEXT();
            currentMRTTargets_[i] = nullptr;
        }
        currentMRTCount_ = 0;
    }

    void D3D11GraphicsBackend::SetRenderTarget2D(IRenderTargetBackend* rt)
    {
        // DX-143: finalize (MSAA resolve + mip regen) any MRT set that was previously bound via
        // SetRenderTargets() before switching to a single target or the back buffer -- the real
        // gap this task closes (an MRT set's own per-target finalize never ran before this).
        FlushPendingMRTResolveEXT();
        if (currentCustomRT_) currentCustomRT_->UnbindAsRenderTarget();
        if (!rt)
        {
            currentCustomRT_ = nullptr;
            return;
        }
        auto* d3drt = static_cast<D3D11RenderTargetBackend*>(rt);
        currentCustomRT_ = d3drt;
        d3drt->BindAsRenderTarget();
    }

    std::unique_ptr<IRenderTargetCubeBackend> D3D11GraphicsBackend::CreateRenderTargetCube(
        int size, int depthFormat, bool mipMap, int multiSampleCount)
    {
        (void)multiSampleCount; // D3D11RenderTargetCubeBackend deliberately doesn't support MSAA
                                 // (see its own header comment) -- silently ignored, same as every
                                 // other backend's undocumented-parameter-combination behavior
                                 // rather than throwing on a combination a game is unlikely to hit.
        return std::make_unique<D3D11::D3D11RenderTargetCubeBackend>(
            this, device_.Get(), context_.Get(), size, depthFormat, mipMap);
    }

    void D3D11GraphicsBackend::SetRenderTargets(IRenderTargetBackend* const* rts, int count)
    {
        // DX-143: finalize (MSAA resolve + mip regen) any PRIOR MRT set before doing anything else
        // -- handles MRT->MRT, MRT->single-target (via the currentCustomRT_ branch below not
        // applying), and MRT->back-buffer (the count<=0 branch below) transitions all in one place.
        FlushPendingMRTResolveEXT();
        if (currentCustomRT_)
        {
            currentCustomRT_->UnbindAsRenderTarget();
            currentCustomRT_ = nullptr;
        }
        if (!rts || count <= 0)
        {
            // Phase DX8 bugfix (found via DX-61's first real draw-call test): a prior MRT bind
            // (this same method, count>0 below) deliberately never sets currentCustomRT_ -- the
            // comment at the bottom of this function explains why -- so the branch above only
            // restores the back buffer when the prior bind went through SetRenderTarget2D().
            // Unconditionally (and idempotently -- harmless if it's already the back buffer)
            // restore here too, so unbinding after an MRT bind doesn't leave OMSetRenderTargets
            // (and this backend's own currentColorRTVs_/currentDSV_ tracking) pointing at render
            // target views the caller may be about to destroy (dangling GPU state).
            RestoreBackBufferRenderTargetEXT();
            return;
        }

        const int n = std::min(count, static_cast<int>(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT));
        ID3D11RenderTargetView* rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
        for (int i = 0; i < n; ++i)
        {
            auto* d3drt = rts[i] ? static_cast<D3D11RenderTargetBackend*>(rts[i]) : nullptr;
            rtvs[i] = d3drt ? d3drt->GetRTVEXT() : nullptr;
        }

        auto* first = rts[0] ? static_cast<D3D11RenderTargetBackend*>(rts[0]) : nullptr;
        ID3D11DepthStencilView* dsv = first ? first->GetDSVEXT() : nullptr;
        const int w = first ? first->GetWidth() : width_;
        const int h = first ? first->GetHeight() : height_;

        context_->OMSetRenderTargets(static_cast<UINT>(n), rtvs, dsv);

        D3D11_VIEWPORT vp{};
        vp.TopLeftX = 0.0f;
        vp.TopLeftY = 0.0f;
        vp.Width = static_cast<float>(w);
        vp.Height = static_cast<float>(h);
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        context_->RSSetViewports(1, &vp);

        TrackCurrentRenderTargetEXT(rtvs, n, dsv);
        // DX-143: MRT targets are still NOT tracked in currentCustomRT_ (a single pointer can't
        // represent N targets) -- instead tracked in currentMRTTargets_/currentMRTCount_, finalized
        // by FlushPendingMRTResolveEXT() the next time SetRenderTarget2D()/SetRenderTargets() is
        // called (this task's own real fix: every individual target's MSAA resolve / mip regen now
        // genuinely runs when this MRT set is replaced/unbound, not silently skipped).
        currentMRTCount_ = n;
        for (int i = 0; i < n; ++i)
            currentMRTTargets_[i] = rts[i] ? static_cast<D3D11RenderTargetBackend*>(rts[i]) : nullptr;
    }

    void D3D11GraphicsBackend::ApplySamplerState(int slot, int filter, int addressU, int addressV, int maxAnisotropy)
    {
        if (slot < 0 || slot >= D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT) return;
        auto sampler = samplerCache_.GetOrCreate(device_.Get(), filter, addressU, addressV, maxAnisotropy);
        ID3D11SamplerState* raw = sampler.Get();
        context_->PSSetSamplers(static_cast<UINT>(slot), 1, &raw);
    }

    std::unique_ptr<IOcclusionQueryBackend> D3D11GraphicsBackend::CreateOcclusionQuery()
    {
        return std::make_unique<D3D11OcclusionQueryBackend>(device_.Get(), context_.Get());
    }

    std::unique_ptr<IEffectBackend> D3D11GraphicsBackend::CreateEffectBackend(
        const std::string& vertSrc, const std::string& fragSrc)
    {
        auto backend = std::make_unique<D3D11EffectBackend>(device_.Get(), context_.Get());
        if (!vertSrc.empty() && !fragSrc.empty())
            backend->CompileProgram(vertSrc, fragSrc);
        return backend;
    }

    void D3D11GraphicsBackend::ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                                               int colorDstBlend, int alphaDstBlend,
                                               int colorBlendFunc, int alphaBlendFunc)
    {
        currentBlendState_ = blendStateCache_.GetOrCreate(
            device_.Get(), colorSrcBlend, alphaSrcBlend, colorDstBlend, alphaDstBlend,
            colorBlendFunc, alphaBlendFunc);
        context_->OMSetBlendState(currentBlendState_.Get(), currentBlendFactor_, 0xFFFFFFFFu);
    }

    void D3D11GraphicsBackend::ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable,
                                                       int depthFunc,
                                                       bool stencilEnable, int stencilFunc,
                                                       int stencilPass, int stencilFail, int stencilDepthFail,
                                                       int stencilMask, int stencilWriteMask, int referenceStencil,
                                                       bool twoSidedStencilMode,
                                                       int ccwStencilFunc, int ccwStencilPass,
                                                       int ccwStencilFail, int ccwStencilDepthFail)
    {
        dsDepthEnable_ = depthEnable;
        dsDepthWriteEnable_ = depthWriteEnable;
        dsDepthFunc_ = depthFunc;
        dsStencilEnable_ = stencilEnable;
        dsStencilFunc_ = stencilFunc;
        dsStencilPass_ = stencilPass;
        dsStencilFail_ = stencilFail;
        dsStencilDepthFail_ = stencilDepthFail;
        dsStencilMask_ = stencilMask;
        dsStencilWriteMask_ = stencilWriteMask;
        dsTwoSidedStencilMode_ = twoSidedStencilMode;
        dsCcwStencilFunc_ = ccwStencilFunc;
        dsCcwStencilPass_ = ccwStencilPass;
        dsCcwStencilFail_ = ccwStencilFail;
        dsCcwStencilDepthFail_ = ccwStencilDepthFail;
        currentReferenceStencil_ = referenceStencil;
        RebindDepthStencilState();
    }

    void D3D11GraphicsBackend::RebindDepthStencilState()
    {
        currentDepthStencilState_ = depthStencilStateCache_.GetOrCreate(
            device_.Get(), dsDepthEnable_, dsDepthWriteEnable_, dsDepthFunc_,
            dsStencilEnable_, dsStencilFunc_, dsStencilPass_, dsStencilFail_, dsStencilDepthFail_,
            dsStencilMask_, dsStencilWriteMask_,
            dsTwoSidedStencilMode_, dsCcwStencilFunc_, dsCcwStencilPass_, dsCcwStencilFail_,
            dsCcwStencilDepthFail_);
        context_->OMSetDepthStencilState(currentDepthStencilState_.Get(),
                                         static_cast<UINT>(currentReferenceStencil_));
    }

    void D3D11GraphicsBackend::ApplyRasterizerState(int cullMode, int fillMode,
                                                    bool scissorTestEnable,
                                                    float depthBias, float slopeScaleDepthBias)
    {
        auto state = rasterizerStateCache_.GetOrCreate(
            device_.Get(), cullMode, fillMode, scissorTestEnable, depthBias, slopeScaleDepthBias);
        context_->RSSetState(state.Get());
    }

    void D3D11GraphicsBackend::SetBlendFactor(float r, float g, float b, float a)
    {
        currentBlendFactor_[0] = r;
        currentBlendFactor_[1] = g;
        currentBlendFactor_[2] = b;
        currentBlendFactor_[3] = a;
        // Task 870/319 (mirrored from DX-51's own SetReferenceStencil below): BlendFactor is a
        // real, independent device property -- it must take effect immediately even if no new
        // ApplyBlendState() call happens, by re-binding whatever blend state is already current
        // with the new factor. If no blend state has been applied yet, there is nothing to
        // re-bind (the next ApplyBlendState() call will pick up currentBlendFactor_ itself).
        if (currentBlendState_)
            context_->OMSetBlendState(currentBlendState_.Get(), currentBlendFactor_, 0xFFFFFFFFu);
    }

    void D3D11GraphicsBackend::SetReferenceStencil(int value)
    {
        currentReferenceStencil_ = value;
        // Same standalone-property discipline as SetBlendFactor above (Task 870/319): re-bind the
        // current depth-stencil state object with the new reference value.
        if (currentDepthStencilState_)
            context_->OMSetDepthStencilState(currentDepthStencilState_.Get(),
                                             static_cast<UINT>(currentReferenceStencil_));
    }

    void D3D11GraphicsBackend::SetScissorRect(int x, int y, int w, int h)
    {
        D3D11_RECT rect{};
        rect.left = x;
        rect.top = y;
        rect.right = x + std::max(0, w);
        rect.bottom = y + std::max(0, h);
        context_->RSSetScissorRects(1, &rect);
    }

    void D3D11GraphicsBackend::SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth)
    {
        D3D11_VIEWPORT vp{};
        vp.TopLeftX = static_cast<float>(x);
        vp.TopLeftY = static_cast<float>(y);
        vp.Width = static_cast<float>(std::max(0, w));
        vp.Height = static_cast<float>(std::max(0, h));
        vp.MinDepth = minDepth;
        vp.MaxDepth = maxDepth;
        context_->RSSetViewports(1, &vp);
    }

    void D3D11GraphicsBackend::TrackCurrentRenderTargetEXT(
        ID3D11RenderTargetView* const* rtvs, int count, ID3D11DepthStencilView* dsv)
    {
        currentRTVCount_ = std::clamp(count, 0, static_cast<int>(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT));
        for (int i = 0; i < currentRTVCount_; ++i) currentColorRTVs_[i] = rtvs[i];
        for (int i = currentRTVCount_; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i) currentColorRTVs_[i] = nullptr;
        currentDSV_ = dsv;
    }

    void D3D11GraphicsBackend::RestoreBackBufferRenderTargetEXT()
    {
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

        currentColorRTVs_[0] = backBufferRTV_.Get();
        currentRTVCount_ = 1;
        for (int i = 1; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i) currentColorRTVs_[i] = nullptr;
        currentDSV_ = depthStencilView_.Get();
    }

    std::unique_ptr<ISpriteBatchBackend> D3D11GraphicsBackend::CreateSpriteBatch()
    {
        return std::make_unique<D3D11SpriteBatchBackend>(this);
    }

    std::unique_ptr<IVertexBufferBackend> D3D11GraphicsBackend::CreateVertexBuffer(int vertex_capacity)
    {
        return std::make_unique<D3D11VertexBufferBackend>(device_.Get(), context_.Get(), vertex_capacity);
    }

    std::unique_ptr<IIndexBufferBackend> D3D11GraphicsBackend::CreateIndexBuffer16(int index_capacity)
    {
        return std::make_unique<D3D11IndexBufferBackend>(device_.Get(), context_.Get(), index_capacity, false);
    }

    std::unique_ptr<IIndexBufferBackend> D3D11GraphicsBackend::CreateIndexBuffer32(int index_capacity)
    {
        return std::make_unique<D3D11IndexBufferBackend>(device_.Get(), context_.Get(), index_capacity, true);
    }

    ID3D11Buffer* D3D11GraphicsBackend::GetOrCreatePerDrawConstantBufferEXT()
    {
        if (!perDrawConstantBuffer_)
        {
            D3D11_BUFFER_DESC desc{};
            desc.ByteWidth = sizeof(D3DCommon::D3DPerDrawConstants);
            desc.Usage = D3D11_USAGE_DYNAMIC;
            desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            const HRESULT hr = device_->CreateBuffer(&desc, nullptr, perDrawConstantBuffer_.ReleaseAndGetAddressOf());
            if (FAILED(hr))
                throw std::runtime_error("D3D11GraphicsBackend: PerDraw constant buffer creation failed, hr=" + FormatHr(hr));
        }
        return perDrawConstantBuffer_.Get();
    }

    ID3D11Buffer* D3D11GraphicsBackend::GetOrCreateFogConstantBufferEXT()
    {
        if (!fogConstantBuffer_)
        {
            D3D11_BUFFER_DESC desc{};
            desc.ByteWidth = sizeof(D3DCommon::D3DFogConstants);
            desc.Usage = D3D11_USAGE_DYNAMIC;
            desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            const HRESULT hr = device_->CreateBuffer(&desc, nullptr, fogConstantBuffer_.ReleaseAndGetAddressOf());
            if (FAILED(hr))
                throw std::runtime_error("D3D11GraphicsBackend: Fog constant buffer creation failed, hr=" + FormatHr(hr));
        }
        return fogConstantBuffer_.Get();
    }

    void D3D11GraphicsBackend::UpdateDynamicConstantBufferEXT(
        ID3D11Buffer* buffer, const void* data, std::size_t byteCount)
    {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        const HRESULT hr = context_->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (FAILED(hr))
            throw std::runtime_error("D3D11GraphicsBackend: constant buffer Map failed, hr=" + FormatHr(hr));
        std::memcpy(mapped.pData, data, byteCount);
        context_->Unmap(buffer, 0);
    }

    void D3D11GraphicsBackend::DrawColoredPrimitives(
        const IVertexBufferBackend& vb, const Matrix& world, const Matrix& view, const Matrix& projection,
        PrimitiveType primitive, int primitiveCount)
    {
        // DX-61: colored3d (stride 16, unlit vertex-color) is the only real draw pipeline so far --
        // matches this method's own header doc ("equivalent to BasicEffect with
        // VertexColorEnabled = true"). Other strides/variants still throw via DrawPrimitivesEx's
        // default fallback until Phase DX8's remaining tasks (DX-62 onward) land them.
        const auto& d3dVb = static_cast<const D3D11VertexBufferBackend&>(vb);
        const std::size_t stride = d3dVb.GetStrideEXT() > 0 ? d3dVb.GetStrideEXT() : 16;
        if (stride != 16)
        {
            throw std::runtime_error(
                "D3D11GraphicsBackend::DrawColoredPrimitives: only stride-16 (VertexPositionColor) "
                "is implemented so far (plan_dx.md DX-61); other strides land in DX-62 onward");
        }

        constexpr auto variant = D3DCommon::D3DShaderVariant::Colored3d;
        auto vs = D3DCommon::CreateVertexShaderForVariant(device_.Get(), variant);
        auto ps = D3DCommon::CreatePixelShaderForVariant(device_.Get(), variant);
        if (!vs || !ps)
            throw std::runtime_error("DrawColoredPrimitives: failed to create colored3d shader objects");

        auto layout = inputLayoutCache_.GetOrCreate(device_.Get(), variant, stride);
        if (!layout)
            throw std::runtime_error("DrawColoredPrimitives: failed to create colored3d input layout");

        // DX-60: PerDraw (b0) -- historical "raw vertex color" convention for this no-GpuDrawParams
        // legacy path (diffuseColor=white, vertexColorEnabled=true), matching every other backend's
        // own DrawColoredPrimitives behavior (Task 364).
        D3DCommon::D3DPerDrawConstants perDraw{};
        const Matrix wvp = world * view * projection;
        wvp.ToColumnMajor(perDraw.Mvp); // row-major flat layout, matches HLSL row_major cbuffer field
        perDraw.DiffuseColor[0] = perDraw.DiffuseColor[1] = perDraw.DiffuseColor[2] = perDraw.DiffuseColor[3] = 1.0f;
        perDraw.VertexColorEnabled = 1.0f;

        // FogParams (b1) -- fog disabled; this legacy path carries no GpuDrawParams to enable it.
        D3DCommon::D3DFogConstants fog{};
        fog.FogStartEnd[1] = 1.0f;

        ID3D11Buffer* perDrawCB = GetOrCreatePerDrawConstantBufferEXT();
        ID3D11Buffer* fogCB = GetOrCreateFogConstantBufferEXT();
        UpdateDynamicConstantBufferEXT(perDrawCB, &perDraw, sizeof(perDraw));
        UpdateDynamicConstantBufferEXT(fogCB, &fog, sizeof(fog));

        ID3D11Buffer* vbRaw = d3dVb.GetBufferEXT();
        const UINT strideU = static_cast<UINT>(stride);
        const UINT offset = 0;
        context_->IASetVertexBuffers(0, 1, &vbRaw, &strideU, &offset);
        context_->IASetInputLayout(layout.Get());
        context_->IASetPrimitiveTopology(ToD3D11Topology(primitive));
        context_->VSSetShader(vs.Get(), nullptr, 0);
        context_->PSSetShader(ps.Get(), nullptr, 0);

        ID3D11Buffer* cbs[2] = { perDrawCB, fogCB };
        context_->VSSetConstantBuffers(0, 2, cbs);
        context_->PSSetConstantBuffers(0, 2, cbs);

        const UINT vertexCount = static_cast<UINT>(VertexCountForPrimitives(primitive, primitiveCount));
        context_->Draw(vertexCount, 0);
    }

    void D3D11GraphicsBackend::DrawIndexedColoredPrimitives(
        const IVertexBufferBackend& vb, const IIndexBufferBackend& ib,
        const Matrix& world, const Matrix& view, const Matrix& projection,
        PrimitiveType primitive, int primitiveCount)
    {
        // DX-61: indexed counterpart of DrawColoredPrimitives above -- same colored3d-only scope.
        const auto& d3dVb = static_cast<const D3D11VertexBufferBackend&>(vb);
        const auto& d3dIb = static_cast<const D3D11IndexBufferBackend&>(ib);
        const std::size_t stride = d3dVb.GetStrideEXT() > 0 ? d3dVb.GetStrideEXT() : 16;
        if (stride != 16)
        {
            throw std::runtime_error(
                "D3D11GraphicsBackend::DrawIndexedColoredPrimitives: only stride-16 "
                "(VertexPositionColor) is implemented so far (plan_dx.md DX-61)");
        }

        constexpr auto variant = D3DCommon::D3DShaderVariant::Colored3d;
        auto vs = D3DCommon::CreateVertexShaderForVariant(device_.Get(), variant);
        auto ps = D3DCommon::CreatePixelShaderForVariant(device_.Get(), variant);
        if (!vs || !ps)
            throw std::runtime_error("DrawIndexedColoredPrimitives: failed to create colored3d shader objects");

        auto layout = inputLayoutCache_.GetOrCreate(device_.Get(), variant, stride);
        if (!layout)
            throw std::runtime_error("DrawIndexedColoredPrimitives: failed to create colored3d input layout");

        D3DCommon::D3DPerDrawConstants perDraw{};
        const Matrix wvp = world * view * projection;
        wvp.ToColumnMajor(perDraw.Mvp);
        perDraw.DiffuseColor[0] = perDraw.DiffuseColor[1] = perDraw.DiffuseColor[2] = perDraw.DiffuseColor[3] = 1.0f;
        perDraw.VertexColorEnabled = 1.0f;

        D3DCommon::D3DFogConstants fog{};
        fog.FogStartEnd[1] = 1.0f;

        ID3D11Buffer* perDrawCB = GetOrCreatePerDrawConstantBufferEXT();
        ID3D11Buffer* fogCB = GetOrCreateFogConstantBufferEXT();
        UpdateDynamicConstantBufferEXT(perDrawCB, &perDraw, sizeof(perDraw));
        UpdateDynamicConstantBufferEXT(fogCB, &fog, sizeof(fog));

        ID3D11Buffer* vbRaw = d3dVb.GetBufferEXT();
        const UINT strideU = static_cast<UINT>(stride);
        const UINT offset = 0;
        context_->IASetVertexBuffers(0, 1, &vbRaw, &strideU, &offset);
        context_->IASetIndexBuffer(d3dIb.GetBufferEXT(), d3dIb.GetFormatEXT(), 0);
        context_->IASetInputLayout(layout.Get());
        context_->IASetPrimitiveTopology(ToD3D11Topology(primitive));
        context_->VSSetShader(vs.Get(), nullptr, 0);
        context_->PSSetShader(ps.Get(), nullptr, 0);

        ID3D11Buffer* cbs[2] = { perDrawCB, fogCB };
        context_->VSSetConstantBuffers(0, 2, cbs);
        context_->PSSetConstantBuffers(0, 2, cbs);

        const UINT indexCount = static_cast<UINT>(VertexCountForPrimitives(primitive, primitiveCount));
        context_->DrawIndexed(indexCount, 0, 0);
    }

    ID3D11Buffer* D3D11GraphicsBackend::GetOrCreateLightingConstantBufferEXT()
    {
        if (!lightingConstantBuffer_)
        {
            D3D11_BUFFER_DESC desc{};
            desc.ByteWidth = sizeof(D3DCommon::D3DLightingConstants);
            desc.Usage = D3D11_USAGE_DYNAMIC;
            desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            const HRESULT hr = device_->CreateBuffer(&desc, nullptr, lightingConstantBuffer_.ReleaseAndGetAddressOf());
            if (FAILED(hr))
                throw std::runtime_error("D3D11GraphicsBackend: Lighting constant buffer creation failed, hr=" + FormatHr(hr));
        }
        return lightingConstantBuffer_.Get();
    }

    ID3D11Buffer* D3D11GraphicsBackend::GetOrCreateAlphaTestConstantBufferEXT()
    {
        if (!alphaTestConstantBuffer_)
        {
            D3D11_BUFFER_DESC desc{};
            desc.ByteWidth = sizeof(D3DCommon::D3DAlphaTestConstants);
            desc.Usage = D3D11_USAGE_DYNAMIC;
            desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            const HRESULT hr = device_->CreateBuffer(&desc, nullptr, alphaTestConstantBuffer_.ReleaseAndGetAddressOf());
            if (FAILED(hr))
                throw std::runtime_error("D3D11GraphicsBackend: AlphaTest constant buffer creation failed, hr=" + FormatHr(hr));
        }
        return alphaTestConstantBuffer_.Get();
    }

    ID3D11Buffer* D3D11GraphicsBackend::GetOrCreateDualTexFogConstantBufferEXT()
    {
        if (!dualTexFogConstantBuffer_)
        {
            D3D11_BUFFER_DESC desc{};
            desc.ByteWidth = sizeof(D3DCommon::D3DFogConstants);
            desc.Usage = D3D11_USAGE_DYNAMIC;
            desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            const HRESULT hr = device_->CreateBuffer(&desc, nullptr, dualTexFogConstantBuffer_.ReleaseAndGetAddressOf());
            if (FAILED(hr))
                throw std::runtime_error("D3D11GraphicsBackend: DualTex Fog constant buffer creation failed, hr=" + FormatHr(hr));
        }
        return dualTexFogConstantBuffer_.Get();
    }

    ID3D11Buffer* D3D11GraphicsBackend::GetOrCreateEnvMapPerDrawConstantBufferEXT()
    {
        if (!envMapPerDrawConstantBuffer_)
        {
            D3D11_BUFFER_DESC desc{};
            desc.ByteWidth = sizeof(D3DCommon::D3DEnvMapPerDrawConstants);
            desc.Usage = D3D11_USAGE_DYNAMIC;
            desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            const HRESULT hr = device_->CreateBuffer(&desc, nullptr, envMapPerDrawConstantBuffer_.ReleaseAndGetAddressOf());
            if (FAILED(hr))
                throw std::runtime_error("D3D11GraphicsBackend: EnvMap PerDraw constant buffer creation failed, hr=" + FormatHr(hr));
        }
        return envMapPerDrawConstantBuffer_.Get();
    }

    ID3D11Buffer* D3D11GraphicsBackend::GetOrCreateEnvMapConstantBufferEXT()
    {
        if (!envMapConstantBuffer_)
        {
            D3D11_BUFFER_DESC desc{};
            desc.ByteWidth = sizeof(D3DCommon::D3DEnvMapConstants);
            desc.Usage = D3D11_USAGE_DYNAMIC;
            desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            const HRESULT hr = device_->CreateBuffer(&desc, nullptr, envMapConstantBuffer_.ReleaseAndGetAddressOf());
            if (FAILED(hr))
                throw std::runtime_error("D3D11GraphicsBackend: EnvMap constant buffer creation failed, hr=" + FormatHr(hr));
        }
        return envMapConstantBuffer_.Get();
    }

    ID3D11Buffer* D3D11GraphicsBackend::GetOrCreateBoneConstantBufferEXT()
    {
        if (!boneConstantBuffer_)
        {
            D3D11_BUFFER_DESC desc{};
            desc.ByteWidth = sizeof(D3DCommon::D3DBoneConstants);
            desc.Usage = D3D11_USAGE_DYNAMIC;
            desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            const HRESULT hr = device_->CreateBuffer(&desc, nullptr, boneConstantBuffer_.ReleaseAndGetAddressOf());
            if (FAILED(hr))
                throw std::runtime_error("D3D11GraphicsBackend: Bone constant buffer creation failed, hr=" + FormatHr(hr));
        }
        return boneConstantBuffer_.Get();
    }

    ID3D11Buffer* D3D11GraphicsBackend::GetOrCreateSkinnedExtraConstantBufferEXT()
    {
        if (!skinnedExtraConstantBuffer_)
        {
            D3D11_BUFFER_DESC desc{};
            desc.ByteWidth = sizeof(D3DCommon::D3DSkinnedExtraConstants);
            desc.Usage = D3D11_USAGE_DYNAMIC;
            desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            const HRESULT hr = device_->CreateBuffer(&desc, nullptr, skinnedExtraConstantBuffer_.ReleaseAndGetAddressOf());
            if (FAILED(hr))
                throw std::runtime_error("D3D11GraphicsBackend: Skinned Extra constant buffer creation failed, hr=" + FormatHr(hr));
        }
        return skinnedExtraConstantBuffer_.Get();
    }

    ID3D11InputLayout* D3D11GraphicsBackend::GetOrCreateInstancedInputLayoutEXT()
    {
        if (!instancedInputLayout_)
        {
            static const D3D11_INPUT_ELEMENT_DESC kElements[] = {
                { "POSITION",      0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D11_INPUT_PER_VERTEX_DATA,   0 },
                { "INSTANCEWORLD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0,  D3D11_INPUT_PER_INSTANCE_DATA, 1 },
                { "INSTANCEWORLD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
                { "INSTANCEWORLD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 32, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
                { "INSTANCEWORLD", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 48, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
            };

            const uint8_t* vsBytes = nullptr;
            std::size_t vsSize = 0;
            D3DCommon::GetVertexShaderBytecode(D3DCommon::D3DShaderVariant::Instanced3d, vsBytes, vsSize);
            if (vsBytes == nullptr || vsSize == 0)
                return nullptr;

            device_->CreateInputLayout(kElements, ARRAYSIZE(kElements), vsBytes, vsSize,
                                       instancedInputLayout_.ReleaseAndGetAddressOf());
        }
        return instancedInputLayout_.Get();
    }

    void D3D11GraphicsBackend::DrawPrimitivesExImpl(
        const IVertexBufferBackend& vb, const IIndexBufferBackend* ib,
        const Matrix& world, const Matrix& view, const Matrix& projection,
        PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params)
    {
        // DX-62/DX-63/DX-64/DX-65/DX-66/DX-67: real effect-aware variant dispatch.
        const auto& d3dVb = static_cast<const D3D11VertexBufferBackend&>(vb);
        const std::size_t stride = d3dVb.GetStrideEXT() > 0 ? d3dVb.GetStrideEXT() : 16;

        const bool needsAlphaTest   = (params.alphaTest[3] < 0.0f || params.alphaTest[2] < 0.0f);
        const bool needsDualTex     = params.dualTexture && !needsAlphaTest;
        const bool needsEnvMap      = params.envMapping  && !needsAlphaTest && !needsDualTex;
        const bool needsSkinned     = params.skinned     && !needsAlphaTest && !needsDualTex && !needsEnvMap;
        // stride==32 always uses lit_textured3d (BasicEffect's VertexPositionNormalTexture path,
        // lit or not -- the shader itself branches on LightingEnabled), unless a higher-priority
        // effect claims this draw first. Mirrors VulkanGraphicsBackend::DrawPrimitivesEx exactly.
        const bool needsLitTextured = (stride == 32) && !needsAlphaTest && !needsDualTex
                                     && !needsEnvMap && !needsSkinned;

        // DX-65: dual_texture3d.vert.hlsl's VSInput is Position+UV only (20 bytes) -- the 24-byte
        // dual_texture_colored3d variant was deliberately not ported (DX-13-hlsl's own row notes).
        if (needsDualTex && stride != 20)
            throw std::runtime_error(
                "D3D11GraphicsBackend::DrawPrimitivesEx: DualTextureEffect (dual_texture3d) only "
                "supports stride 20 (VertexPositionTexture); dual_texture_colored3d was not ported "
                "(plan_dx.md DX-13-hlsl)");
        // DX-66: env_map3d.vert.hlsl's VSInput is Position+Normal+UV (32 bytes).
        if (needsEnvMap && stride != 32)
            throw std::runtime_error(
                "D3D11GraphicsBackend::DrawPrimitivesEx: EnvironmentMapEffect (env_map3d) requires "
                "stride 32 (VertexPositionNormalTexture)");
        // DX-67: skinned3d.vert.hlsl's VSInput is Position+Normal+UV+BoneWeights+BoneIndices (52 bytes).
        if (needsSkinned && stride != 52)
            throw std::runtime_error(
                "D3D11GraphicsBackend::DrawPrimitivesEx: SkinnedEffect (skinned3d) requires stride "
                "52 (VertexPositionNormalTextureSkinned)");

        D3DCommon::D3DShaderVariant variant;
        if (needsAlphaTest)
            variant = D3DCommon::D3DShaderVariant::AlphaTest3d;
        else if (needsDualTex)
            variant = D3DCommon::D3DShaderVariant::DualTexture3d;
        else if (needsEnvMap)
            variant = D3DCommon::D3DShaderVariant::EnvMap3d;
        else if (needsSkinned)
            // plan_graphics.md Phase 80 (Task 1106): real XNA renders SkinnedEffect's lit path
            // per-vertex by default (PreferPerPixelLighting == false), not per-pixel.
            variant = (params.lightingEnabled && !params.preferPerPixelLighting)
                    ? D3DCommon::D3DShaderVariant::Skinned3dVertexLit
                    : D3DCommon::D3DShaderVariant::Skinned3d;
        else if (needsLitTextured)
            // Same real-default fix for BasicEffect's lit-textured bucket.
            variant = (params.lightingEnabled && !params.preferPerPixelLighting)
                    ? D3DCommon::D3DShaderVariant::LitTextured3dVertexLit
                    : D3DCommon::D3DShaderVariant::LitTextured3d;
        else
        {
            switch (stride)
            {
            case 16: variant = D3DCommon::D3DShaderVariant::Colored3d; break;
            case 20: variant = D3DCommon::D3DShaderVariant::Textured3d; break;
            case 24: variant = D3DCommon::D3DShaderVariant::ColoredTextured3d; break;
            default:
                throw std::runtime_error(
                    "D3D11GraphicsBackend::DrawPrimitivesEx: unsupported vertex stride " +
                    std::to_string(stride) + " for the colored/textured bundle (plan_dx.md DX-62)");
            }
        }

        auto vs = D3DCommon::CreateVertexShaderForVariant(device_.Get(), variant);
        auto ps = D3DCommon::CreatePixelShaderForVariant(device_.Get(), variant);
        if (!vs || !ps)
            throw std::runtime_error("DrawPrimitivesEx: failed to create shader objects for the selected variant");

        auto layout = inputLayoutCache_.GetOrCreate(device_.Get(), variant, stride);
        if (!layout)
            throw std::runtime_error("DrawPrimitivesEx: failed to create input layout for the selected variant/stride");

        const Matrix wvp = world * view * projection;

        // DX-65/DX-66: dual_texture3d needs t0+t1 (both Texture2D); env_map3d needs t0 (Texture2D)
        // + t1 (TextureCube). Every other variant only ever binds t0 -- srvs[1] stays null, which
        // is harmless for a shader that declares no t1 register.
        ID3D11ShaderResourceView* srvs[2] = { nullptr, nullptr };
        if (needsDualTex)
        {
            srvs[0] = GetSrvForTextureEXT(params.texture0);
            srvs[1] = GetSrvForTextureEXT(params.texture1);
        }
        else if (needsEnvMap)
        {
            srvs[0] = GetSrvForTextureEXT(params.texture0);
            srvs[1] = GetSrvForTextureCubeEXT(params.envMap);
        }
        else
        {
            srvs[0] = GetSrvForTextureEXT(params.texture0);
        }

        // 3 contiguous slots (b0/b1/b2) always fully rebound below (unused slots explicitly null)
        // so no variant can see a stale buffer left bound by a previous, different-shaped draw.
        ID3D11Buffer* cbs[3] = { nullptr, nullptr, nullptr };

        if (needsAlphaTest)
        {
            // DX-64: alpha_test3d's single combined PerDraw (b0) cbuffer -- see
            // D3DAlphaTestConstants's own doc comment for why this isn't D3DPerDrawConstants.
            D3DCommon::D3DAlphaTestConstants c{};
            wvp.ToColumnMajor(c.Mvp);
            c.DiffuseColor[0] = params.diffuseColor[0];
            c.DiffuseColor[1] = params.diffuseColor[1];
            c.DiffuseColor[2] = params.diffuseColor[2];
            c.DiffuseColor[3] = params.diffuseColor[3];
            c.AlphaRef           = params.alphaTest[0];
            c.AlphaTol           = params.alphaTest[1];
            c.AlphaPassW         = params.alphaTest[2];
            c.AlphaFailW         = params.alphaTest[3];
            c.VertexColorEnabled = params.vertexColorEnabled ? 1.0f : 0.0f;
            c.FogEnabled         = params.fogEnabled ? 1.0f : 0.0f;
            c.FogStart           = params.fogStart;
            c.FogEnd             = params.fogEnd;
            c.FogColor[0] = params.fogColor[0];
            c.FogColor[1] = params.fogColor[1];
            c.FogColor[2] = params.fogColor[2];

            ID3D11Buffer* cb = GetOrCreateAlphaTestConstantBufferEXT();
            UpdateDynamicConstantBufferEXT(cb, &c, sizeof(c));
            cbs[0] = cb;
        }
        else if (needsDualTex)
        {
            // DX-65: dual_texture3d's PerDraw (b0) is the same shape as D3DPerDrawConstants; its
            // FogParams cbuffer is at register(b2) instead of (b1) -- t0/s0 and t1/s1 are already
            // the two texture samplers, so fog moved to the next free slot (DX-13-hlsl's own note).
            D3DCommon::D3DPerDrawConstants perDraw{};
            wvp.ToColumnMajor(perDraw.Mvp);
            perDraw.DiffuseColor[0] = params.diffuseColor[0];
            perDraw.DiffuseColor[1] = params.diffuseColor[1];
            perDraw.DiffuseColor[2] = params.diffuseColor[2];
            perDraw.DiffuseColor[3] = params.diffuseColor[3];
            perDraw.TextureEnabled = params.textureEnabled ? 1.0f : 0.0f;
            perDraw.VertexColorEnabled = params.vertexColorEnabled ? 1.0f : 0.0f;

            D3DCommon::D3DFogConstants fog{};
            fog.FogColorEnabled[0] = params.fogColor[0];
            fog.FogColorEnabled[1] = params.fogColor[1];
            fog.FogColorEnabled[2] = params.fogColor[2];
            fog.FogColorEnabled[3] = params.fogEnabled ? 1.0f : 0.0f;
            fog.FogStartEnd[0] = params.fogStart;
            fog.FogStartEnd[1] = params.fogEnd;

            ID3D11Buffer* perDrawCB = GetOrCreatePerDrawConstantBufferEXT();
            ID3D11Buffer* fogCB = GetOrCreateDualTexFogConstantBufferEXT();
            UpdateDynamicConstantBufferEXT(perDrawCB, &perDraw, sizeof(perDraw));
            UpdateDynamicConstantBufferEXT(fogCB, &fog, sizeof(fog));
            cbs[0] = perDrawCB;
            cbs[2] = fogCB;
        }
        else if (needsEnvMap)
        {
            // DX-66: env_map3d's own PerDraw (b0) is Mvp+World only (D3DEnvMapPerDrawConstants) --
            // a genuinely different shape from D3DPerDrawConstants; material/lighting/fog live in
            // EnvMapParams (b2) instead (D3DEnvMapConstants), field-for-field matching
            // env_map3d.vert.hlsl/.frag.hlsl's real cbuffer declaration.
            D3DCommon::D3DEnvMapPerDrawConstants perDraw{};
            wvp.ToColumnMajor(perDraw.Mvp);
            world.ToColumnMajor(perDraw.World);

            D3DCommon::D3DEnvMapConstants c{};
            c.EyePosition[0] = params.eyePositionWorld[0];
            c.EyePosition[1] = params.eyePositionWorld[1];
            c.EyePosition[2] = params.eyePositionWorld[2];
            c.DiffuseColor[0] = params.diffuseColor[0];
            c.DiffuseColor[1] = params.diffuseColor[1];
            c.DiffuseColor[2] = params.diffuseColor[2];
            c.DiffuseColor[3] = params.diffuseColor[3];
            c.EmissiveAmount[0] = params.emissiveColor[0];
            c.EmissiveAmount[1] = params.emissiveColor[1];
            c.EmissiveAmount[2] = params.emissiveColor[2];
            c.EmissiveAmount[3] = params.envMapAmount;
            c.Light0Dir[0] = params.light0Dir[0];
            c.Light0Dir[1] = params.light0Dir[1];
            c.Light0Dir[2] = params.light0Dir[2];
            c.Light0DiffuseFresnel[0] = params.light0Diffuse[0];
            c.Light0DiffuseFresnel[1] = params.light0Diffuse[1];
            c.Light0DiffuseFresnel[2] = params.light0Diffuse[2];
            c.Light0DiffuseFresnel[3] = params.fresnelEnabled ? 1.0f : 0.0f;
            c.EnvMapSpecularFresnel[0] = params.envMapSpecular[0];
            c.EnvMapSpecularFresnel[1] = params.envMapSpecular[1];
            c.EnvMapSpecularFresnel[2] = params.envMapSpecular[2];
            c.EnvMapSpecularFresnel[3] = params.fresnelFactor;
            c.FogColorEnabled[0] = params.fogColor[0];
            c.FogColorEnabled[1] = params.fogColor[1];
            c.FogColorEnabled[2] = params.fogColor[2];
            c.FogColorEnabled[3] = params.fogEnabled ? 1.0f : 0.0f;
            c.FogStartEnd[0] = params.fogStart;
            c.FogStartEnd[1] = params.fogEnd;
            c.Light1Dir[0] = params.light1Dir[0];
            c.Light1Dir[1] = params.light1Dir[1];
            c.Light1Dir[2] = params.light1Dir[2];
            c.Light1Diffuse[0] = params.light1Diffuse[0];
            c.Light1Diffuse[1] = params.light1Diffuse[1];
            c.Light1Diffuse[2] = params.light1Diffuse[2];
            c.Light2Dir[0] = params.light2Dir[0];
            c.Light2Dir[1] = params.light2Dir[1];
            c.Light2Dir[2] = params.light2Dir[2];
            c.Light2Diffuse[0] = params.light2Diffuse[0];
            c.Light2Diffuse[1] = params.light2Diffuse[1];
            c.Light2Diffuse[2] = params.light2Diffuse[2];

            ID3D11Buffer* perDrawCB = GetOrCreateEnvMapPerDrawConstantBufferEXT();
            ID3D11Buffer* envCB = GetOrCreateEnvMapConstantBufferEXT();
            UpdateDynamicConstantBufferEXT(perDrawCB, &perDraw, sizeof(perDraw));
            UpdateDynamicConstantBufferEXT(envCB, &c, sizeof(c));
            cbs[0] = perDrawCB;
            cbs[2] = envCB;
        }
        else if (needsSkinned)
        {
            // DX-67: skinned3d's PerDraw (b0) is the same shape as D3DPerDrawConstants; BoneBlock
            // (b1, D3DBoneConstants, DX-60a) holds the 72-matrix array; FogParams (b2,
            // D3DSkinnedExtraConstants) carries fog + DirectionalLight1/2 + World + EyePosition +
            // specular (the 128-byte PerDraw buffer has no spare room for those, same reasoning
            // D3DLightingConstants documents for lit_textured3d).
            D3DCommon::D3DPerDrawConstants perDraw{};
            wvp.ToColumnMajor(perDraw.Mvp);
            perDraw.DiffuseColor[0] = params.diffuseColor[0];
            perDraw.DiffuseColor[1] = params.diffuseColor[1];
            perDraw.DiffuseColor[2] = params.diffuseColor[2];
            perDraw.DiffuseColor[3] = params.diffuseColor[3];
            perDraw.AmbientColor[0] = params.ambientColor[0];
            perDraw.AmbientColor[1] = params.ambientColor[1];
            perDraw.AmbientColor[2] = params.ambientColor[2];
            perDraw.LightingEnabled = params.lightingEnabled ? 1.0f : 0.0f;
            perDraw.Light0Dir[0] = params.light0Dir[0];
            perDraw.Light0Dir[1] = params.light0Dir[1];
            perDraw.Light0Dir[2] = params.light0Dir[2];
            perDraw.TextureEnabled = params.textureEnabled ? 1.0f : 0.0f;
            perDraw.Light0Diffuse[0] = params.light0Diffuse[0];
            perDraw.Light0Diffuse[1] = params.light0Diffuse[1];
            perDraw.Light0Diffuse[2] = params.light0Diffuse[2];
            perDraw.VertexColorEnabled = params.vertexColorEnabled ? 1.0f : 0.0f;

            // DX-60a/DX-67: params.boneTransforms is filled via Matrix::ToColumnMajor() at the
            // XNA-API call site (SkinnedEffect::SetBoneTransforms, SkinnedEffect.cpp:383) -- the
            // SAME function DX-60/DX-61's own report established emits raw row-major M11..M44
            // bytes, exactly what BoneBlock's `row_major float4x4 Bones[72]` wants unchanged. A
            // straight memcpy is therefore correct here, no per-matrix transpose needed.
            D3DCommon::D3DBoneConstants bones{};
            const int boneCount = std::min(params.boneCount, 72);
            if (boneCount > 0)
                std::memcpy(bones.Bones, params.boneTransforms,
                           static_cast<std::size_t>(boneCount) * 16u * sizeof(float));

            D3DCommon::D3DSkinnedExtraConstants extra{};
            extra.FogColorEnabled[0] = params.fogColor[0];
            extra.FogColorEnabled[1] = params.fogColor[1];
            extra.FogColorEnabled[2] = params.fogColor[2];
            extra.FogColorEnabled[3] = params.fogEnabled ? 1.0f : 0.0f;
            extra.FogStartEnd[0] = params.fogStart;
            extra.FogStartEnd[1] = params.fogEnd;
            extra.Light1Dir[0] = params.light1Dir[0];
            extra.Light1Dir[1] = params.light1Dir[1];
            extra.Light1Dir[2] = params.light1Dir[2];
            extra.Light1Diffuse[0] = params.light1Diffuse[0];
            extra.Light1Diffuse[1] = params.light1Diffuse[1];
            extra.Light1Diffuse[2] = params.light1Diffuse[2];
            extra.Light2Dir[0] = params.light2Dir[0];
            extra.Light2Dir[1] = params.light2Dir[1];
            extra.Light2Dir[2] = params.light2Dir[2];
            extra.Light2Diffuse[0] = params.light2Diffuse[0];
            extra.Light2Diffuse[1] = params.light2Diffuse[1];
            extra.Light2Diffuse[2] = params.light2Diffuse[2];
            world.ToColumnMajor(extra.World);
            extra.EyePosition[0] = params.eyePositionWorld[0];
            extra.EyePosition[1] = params.eyePositionWorld[1];
            extra.EyePosition[2] = params.eyePositionWorld[2];
            extra.EyePosition[3] = static_cast<float>(params.weightsPerVertex); // DX-67/Task 895
            extra.SpecularColorPower[0] = params.specularColor[0];
            extra.SpecularColorPower[1] = params.specularColor[1];
            extra.SpecularColorPower[2] = params.specularColor[2];
            extra.SpecularColorPower[3] = params.specularPower;
            extra.Light0Specular[0] = params.light0Specular[0];
            extra.Light0Specular[1] = params.light0Specular[1];
            extra.Light0Specular[2] = params.light0Specular[2];
            extra.Light1Specular[0] = params.light1Specular[0];
            extra.Light1Specular[1] = params.light1Specular[1];
            extra.Light1Specular[2] = params.light1Specular[2];
            extra.Light2Specular[0] = params.light2Specular[0];
            extra.Light2Specular[1] = params.light2Specular[1];
            extra.Light2Specular[2] = params.light2Specular[2];

            ID3D11Buffer* perDrawCB = GetOrCreatePerDrawConstantBufferEXT();
            ID3D11Buffer* boneCB = GetOrCreateBoneConstantBufferEXT();
            ID3D11Buffer* extraCB = GetOrCreateSkinnedExtraConstantBufferEXT();
            UpdateDynamicConstantBufferEXT(perDrawCB, &perDraw, sizeof(perDraw));
            UpdateDynamicConstantBufferEXT(boneCB, &bones, sizeof(bones));
            UpdateDynamicConstantBufferEXT(extraCB, &extra, sizeof(extra));
            cbs[0] = perDrawCB;
            cbs[1] = boneCB;
            cbs[2] = extraCB;
        }
        else if (needsLitTextured)
        {
            // DX-63: PerDraw (b0) + LitLightParams (b1) -- full per-light Blinn-Phong data, field-
            // for-field matching lit_textured3d.vert.hlsl/.frag.hlsl's real cbuffer declarations
            // (see D3DLightingConstants's own doc comment for the offset table).
            D3DCommon::D3DPerDrawConstants perDraw{};
            wvp.ToColumnMajor(perDraw.Mvp);
            perDraw.DiffuseColor[0] = params.diffuseColor[0];
            perDraw.DiffuseColor[1] = params.diffuseColor[1];
            perDraw.DiffuseColor[2] = params.diffuseColor[2];
            perDraw.DiffuseColor[3] = params.diffuseColor[3];
            perDraw.AmbientColor[0] = params.ambientColor[0];
            perDraw.AmbientColor[1] = params.ambientColor[1];
            perDraw.AmbientColor[2] = params.ambientColor[2];
            perDraw.LightingEnabled = params.lightingEnabled ? 1.0f : 0.0f;
            perDraw.Light0Dir[0] = params.light0Dir[0];
            perDraw.Light0Dir[1] = params.light0Dir[1];
            perDraw.Light0Dir[2] = params.light0Dir[2];
            perDraw.TextureEnabled = params.textureEnabled ? 1.0f : 0.0f;
            perDraw.Light0Diffuse[0] = params.light0Diffuse[0];
            perDraw.Light0Diffuse[1] = params.light0Diffuse[1];
            perDraw.Light0Diffuse[2] = params.light0Diffuse[2];
            perDraw.VertexColorEnabled = params.vertexColorEnabled ? 1.0f : 0.0f;

            D3DCommon::D3DLightingConstants lighting{};
            lighting.Light1Dir[0] = params.light1Dir[0];
            lighting.Light1Dir[1] = params.light1Dir[1];
            lighting.Light1Dir[2] = params.light1Dir[2];
            lighting.Light1Diffuse[0] = params.light1Diffuse[0];
            lighting.Light1Diffuse[1] = params.light1Diffuse[1];
            lighting.Light1Diffuse[2] = params.light1Diffuse[2];
            lighting.Light2Dir[0] = params.light2Dir[0];
            lighting.Light2Dir[1] = params.light2Dir[1];
            lighting.Light2Dir[2] = params.light2Dir[2];
            lighting.Light2Diffuse[0] = params.light2Diffuse[0];
            lighting.Light2Diffuse[1] = params.light2Diffuse[1];
            lighting.Light2Diffuse[2] = params.light2Diffuse[2];
            lighting.EmissiveColor[0] = params.emissiveColor[0];
            lighting.EmissiveColor[1] = params.emissiveColor[1];
            lighting.EmissiveColor[2] = params.emissiveColor[2];
            world.ToColumnMajor(lighting.World);
            lighting.EyePosition[0] = params.eyePositionWorld[0];
            lighting.EyePosition[1] = params.eyePositionWorld[1];
            lighting.EyePosition[2] = params.eyePositionWorld[2];
            lighting.Light0Specular[0] = params.light0Specular[0];
            lighting.Light0Specular[1] = params.light0Specular[1];
            lighting.Light0Specular[2] = params.light0Specular[2];
            lighting.Light1Specular[0] = params.light1Specular[0];
            lighting.Light1Specular[1] = params.light1Specular[1];
            lighting.Light1Specular[2] = params.light1Specular[2];
            lighting.Light2Specular[0] = params.light2Specular[0];
            lighting.Light2Specular[1] = params.light2Specular[1];
            lighting.Light2Specular[2] = params.light2Specular[2];
            lighting.SpecularColorPower[0] = params.specularColor[0];
            lighting.SpecularColorPower[1] = params.specularColor[1];
            lighting.SpecularColorPower[2] = params.specularColor[2];
            lighting.SpecularColorPower[3] = params.specularPower;
            lighting.FogColorEnabled[0] = params.fogColor[0];
            lighting.FogColorEnabled[1] = params.fogColor[1];
            lighting.FogColorEnabled[2] = params.fogColor[2];
            lighting.FogColorEnabled[3] = params.fogEnabled ? 1.0f : 0.0f;
            lighting.FogStartEnd[0] = params.fogStart;
            lighting.FogStartEnd[1] = params.fogEnd;

            ID3D11Buffer* perDrawCB  = GetOrCreatePerDrawConstantBufferEXT();
            ID3D11Buffer* lightingCB = GetOrCreateLightingConstantBufferEXT();
            UpdateDynamicConstantBufferEXT(perDrawCB, &perDraw, sizeof(perDraw));
            UpdateDynamicConstantBufferEXT(lightingCB, &lighting, sizeof(lighting));
            cbs[0] = perDrawCB;
            cbs[1] = lightingCB;
        }
        else
        {
            // DX-62: colored3d/textured3d/colored_textured3d bundle -- PerDraw (b0) + FogParams (b1),
            // real GpuDrawParams values this time (unlike DrawColoredPrimitives' hardcoded legacy path).
            D3DCommon::D3DPerDrawConstants perDraw{};
            wvp.ToColumnMajor(perDraw.Mvp);
            perDraw.DiffuseColor[0] = params.diffuseColor[0];
            perDraw.DiffuseColor[1] = params.diffuseColor[1];
            perDraw.DiffuseColor[2] = params.diffuseColor[2];
            perDraw.DiffuseColor[3] = params.diffuseColor[3];
            perDraw.TextureEnabled = params.textureEnabled ? 1.0f : 0.0f;
            perDraw.VertexColorEnabled = params.vertexColorEnabled ? 1.0f : 0.0f;

            D3DCommon::D3DFogConstants fog{};
            fog.FogColorEnabled[0] = params.fogColor[0];
            fog.FogColorEnabled[1] = params.fogColor[1];
            fog.FogColorEnabled[2] = params.fogColor[2];
            fog.FogColorEnabled[3] = params.fogEnabled ? 1.0f : 0.0f;
            fog.FogStartEnd[0] = params.fogStart;
            fog.FogStartEnd[1] = params.fogEnd;

            ID3D11Buffer* perDrawCB = GetOrCreatePerDrawConstantBufferEXT();
            ID3D11Buffer* fogCB     = GetOrCreateFogConstantBufferEXT();
            UpdateDynamicConstantBufferEXT(perDrawCB, &perDraw, sizeof(perDraw));
            UpdateDynamicConstantBufferEXT(fogCB, &fog, sizeof(fog));
            cbs[0] = perDrawCB;
            cbs[1] = fogCB;
        }

        ID3D11Buffer* vbRaw = d3dVb.GetBufferEXT();
        const UINT strideU = static_cast<UINT>(stride);
        const UINT offset = 0;
        context_->IASetVertexBuffers(0, 1, &vbRaw, &strideU, &offset);
        if (ib != nullptr)
        {
            const auto& d3dIb = static_cast<const D3D11IndexBufferBackend&>(*ib);
            context_->IASetIndexBuffer(d3dIb.GetBufferEXT(), d3dIb.GetFormatEXT(), 0);
        }
        context_->IASetInputLayout(layout.Get());
        context_->IASetPrimitiveTopology(ToD3D11Topology(primitive));
        context_->VSSetShader(vs.Get(), nullptr, 0);
        context_->PSSetShader(ps.Get(), nullptr, 0);

        // Always rebind the full 3-slot/2-SRV range (unused slots explicitly null, see cbs'/srvs'
        // own declaration comments above) -- no variant can see a stale binding left by whatever
        // differently-shaped draw call ran immediately before this one.
        context_->VSSetConstantBuffers(0, 3, cbs);
        context_->PSSetConstantBuffers(0, 3, cbs);
        context_->PSSetShaderResources(0, 2, srvs);

        if (ib != nullptr)
        {
            const UINT indexCount = static_cast<UINT>(VertexCountForPrimitives(primitive, primitiveCount));
            context_->DrawIndexed(indexCount, 0, 0);
        }
        else
        {
            const UINT vertexCount = static_cast<UINT>(VertexCountForPrimitives(primitive, primitiveCount));
            context_->Draw(vertexCount, 0);
        }
    }

    void D3D11GraphicsBackend::DrawPrimitivesEx(
        const IVertexBufferBackend& vb, const Matrix& world, const Matrix& view, const Matrix& projection,
        PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params)
    {
        DrawPrimitivesExImpl(vb, nullptr, world, view, projection, primitive, primitiveCount, params);
    }

    void D3D11GraphicsBackend::DrawIndexedPrimitivesEx(
        const IVertexBufferBackend& vb, const IIndexBufferBackend& ib,
        const Matrix& world, const Matrix& view, const Matrix& projection,
        PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params)
    {
        DrawPrimitivesExImpl(vb, &ib, world, view, projection, primitive, primitiveCount, params);
    }

    void D3D11GraphicsBackend::DrawInstancedPrimitivesEx(
        const IVertexBufferBackend& vb, const IIndexBufferBackend& ib,
        const Matrix& world, const Matrix& view, const Matrix& projection,
        PrimitiveType primitive, int primitiveCount, int instanceCount, const GpuDrawParams& params)
    {
        // DX-68: matches VulkanGraphicsBackend::DrawInstancedPrimitivesEx's own fallback -- no
        // per-instance VB means this isn't really an instanced draw at all.
        if (params.instanceVb == nullptr)
        {
            DrawIndexedPrimitivesEx(vb, ib, world, view, projection, primitive, primitiveCount, params);
            return;
        }

        const auto& d3dVb     = static_cast<const D3D11VertexBufferBackend&>(vb);
        const auto& d3dIb     = static_cast<const D3D11IndexBufferBackend&>(ib);
        const auto& d3dInstVb = static_cast<const D3D11VertexBufferBackend&>(*params.instanceVb);
        const std::size_t perVertexStride = d3dVb.GetStrideEXT() > 0 ? d3dVb.GetStrideEXT() : 16;
        constexpr std::size_t kInstanceStride = 64; // 4 x float4 rows (INSTANCEWORLD0-3)

        constexpr auto variant = D3DCommon::D3DShaderVariant::Instanced3d;
        auto vs = D3DCommon::CreateVertexShaderForVariant(device_.Get(), variant);
        auto ps = D3DCommon::CreatePixelShaderForVariant(device_.Get(), variant);
        if (!vs || !ps)
            throw std::runtime_error("DrawInstancedPrimitivesEx: failed to create instanced3d shader objects");

        ID3D11InputLayout* layout = GetOrCreateInstancedInputLayoutEXT();
        if (!layout)
            throw std::runtime_error("DrawInstancedPrimitivesEx: failed to create instanced3d input layout");

        // instanced3d.vert.hlsl's PerDraw (b0) is byte-identical to D3DPerDrawConstants -- its
        // first field is named "Vp" (view*projection only, world comes from the per-instance
        // buffer instead) rather than "Mvp", same struct reused for the byte layout only.
        D3DCommon::D3DPerDrawConstants perDraw{};
        const Matrix vp = view * projection;
        vp.ToColumnMajor(perDraw.Mvp);
        perDraw.DiffuseColor[0] = params.diffuseColor[0];
        perDraw.DiffuseColor[1] = params.diffuseColor[1];
        perDraw.DiffuseColor[2] = params.diffuseColor[2];
        perDraw.DiffuseColor[3] = params.diffuseColor[3];

        ID3D11Buffer* perDrawCB = GetOrCreatePerDrawConstantBufferEXT();
        UpdateDynamicConstantBufferEXT(perDrawCB, &perDraw, sizeof(perDraw));

        ID3D11Buffer* vbs[2] = { d3dVb.GetBufferEXT(), d3dInstVb.GetBufferEXT() };
        UINT strides[2] = { static_cast<UINT>(perVertexStride), static_cast<UINT>(kInstanceStride) };
        UINT offsets[2] = { 0, 0 };
        context_->IASetVertexBuffers(0, 2, vbs, strides, offsets);
        context_->IASetIndexBuffer(d3dIb.GetBufferEXT(), d3dIb.GetFormatEXT(), 0);
        context_->IASetInputLayout(layout);
        context_->IASetPrimitiveTopology(ToD3D11Topology(primitive));
        context_->VSSetShader(vs.Get(), nullptr, 0);
        context_->PSSetShader(ps.Get(), nullptr, 0);
        context_->VSSetConstantBuffers(0, 1, &perDrawCB);
        context_->PSSetConstantBuffers(0, 1, &perDrawCB);

        const UINT indexCount = static_cast<UINT>(VertexCountForPrimitives(primitive, primitiveCount));
        const UINT instCount = static_cast<UINT>(std::max(1, instanceCount));
        context_->DrawIndexedInstanced(indexCount, instCount, 0, 0, 0);
    }
}

namespace CNA::Internal::Backends
{
    std::unique_ptr<IGraphicsBackend> CreateGraphicsBackend(const GraphicsBackendCreateArgs& args)
    {
        return std::make_unique<D3D11::D3D11GraphicsBackend>(args);
    }
}
