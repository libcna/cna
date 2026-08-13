// plan_dx9.md Phase D9-3 (D9-30/D9-31): real device creation + Clear/Present/ReadBackbuffer.
// Phase D9-6 (D9-60/D9-61/D9-62) render states landed here too, forced in early: GraphicsDevice's
// own constructor unconditionally pushes BlendState/DepthStencilState/RasterizerState defaults, so
// none of those methods could stay throwing stubs once a real device exists (see ApplyBlendState's
// own comment in the header for the full explanation).
// CNA/Logger.hpp (via LogLevel.hpp/LogCategory.hpp) must be included before
// DirectX9Renderer.hpp -- the latter drags in d3d9.h -> windows.h, which #defines a plain
// macro ERROR (wingdi.h) that collides with LogLevel::ERROR/LogCategory::ERROR's own enumerator
// names if windows.h is parsed first.
#include "CNA/Logger.hpp"
#include "CNA/Internal/Renderers/DirectX9/DirectX9Renderer.hpp"
#include "CNA/Internal/Renderers/Common/NotYetImplemented.hpp"
#include "CNA/Internal/Renderers/DirectX9/D3D9Buffers.hpp"
#include "CNA/Internal/Renderers/DirectX9/D3D9ConstantUpload.hpp"
#include "CNA/Internal/Renderers/DirectX9/D3D9EffectRenderer.hpp"
#include "CNA/Internal/Renderers/DirectX9/D3D9FormatMapping.hpp"
#include "CNA/Internal/Renderers/DirectX9/D3D9ShaderCache.hpp"
#include "CNA/Internal/Renderers/DirectX9/D3D9ShaderDispatch.hpp"
#include "CNA/Internal/Renderers/DirectX9/D3D9StateMapping.hpp"
#include "CNA/Internal/Renderers/DirectX9/D3D9Textures.hpp"
#include "CNA/Internal/Renderers/DirectX9/D3D9RenderTargets.hpp"
#include "CNA/Internal/Renderers/DirectX9/D3D9OcclusionQuery.hpp"
#include "CNA/Internal/Renderers/DirectX9/D3D9SpriteBatch.hpp"
#include "CNA/Internal/Renderers/DirectX9/D3D9VertexDeclarations.hpp"

#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DeviceLostException.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/NoSuitableGraphicsDeviceException.hpp"

#include <algorithm>
#include <bit>
#include <cstdio>
#include <cstring>
#include <iterator>

namespace CNA::Internal::Renderers::DirectX9
{
    namespace
    {
        std::string FormatHr(HRESULT hr)
        {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "0x%08lX", static_cast<unsigned long>(hr));
            return buf;
        }

        const char* HResultName(HRESULT hr)
        {
            switch (hr)
            {
            case D3D_OK:                    return "D3D_OK";
            case D3DERR_INVALIDCALL:        return "D3DERR_INVALIDCALL";
            case D3DERR_DEVICELOST:         return "D3DERR_DEVICELOST";
            case D3DERR_DEVICENOTRESET:     return "D3DERR_DEVICENOTRESET";
            case D3DERR_OUTOFVIDEOMEMORY:   return "D3DERR_OUTOFVIDEOMEMORY";
            case D3DERR_NOTFOUND:           return "D3DERR_NOTFOUND";
            case E_OUTOFMEMORY:             return "E_OUTOFMEMORY";
            default:                        return "unknown HRESULT";
            }
        }

        std::string DescribeHr(HRESULT hr)
        {
            return std::string(HResultName(hr)) + " (" + FormatHr(hr) + ")";
        }

        constexpr unsigned int kUnsafeBlendState = 1u << 0;
        constexpr unsigned int kUnsafeDepthState = 1u << 1;
        constexpr unsigned int kUnsafeRasterizerState = 1u << 2;
        constexpr unsigned int kUnsafeViewport = 1u << 3;
        constexpr unsigned int kUnsafeScissor = 1u << 4;
        constexpr unsigned int kUnsafeTargetBinding = 1u << 5;

        constexpr std::size_t NativeOperationIndex(D3D9NativeOperationEXT operation)
        {
            return static_cast<std::size_t>(operation);
        }

        /// D9-82: XNA's own PrimitiveType only ever needs these 4 D3DPRIMITIVETYPE values --
        /// unlike D3D11/Vulkan/D3D12's own per-renderer VertexCountForPrimitives() precedent, D3D9's
        /// DrawPrimitive/DrawIndexedPrimitive already take a PrimitiveCount directly (not a raw
        /// vertex/index count), so no equivalent conversion helper is needed here.
        D3DPRIMITIVETYPE ToD3D9Topology(PrimitiveType pt)
        {
            switch (pt)
            {
            case PrimitiveType::TriangleList:  return D3DPT_TRIANGLELIST;
            case PrimitiveType::TriangleStrip: return D3DPT_TRIANGLESTRIP;
            case PrimitiveType::LineList:      return D3DPT_LINELIST;
            case PrimitiveType::LineStrip:     return D3DPT_LINESTRIP;
            }
            return D3DPT_TRIANGLELIST;
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

    DirectX9Renderer::DirectX9Renderer(const GraphicsRendererCreateArgs& args)
        : surface_(args.surface, "DirectX9Renderer")
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
        CNA::Platform::Win32NativeWindow nativeWindow;
        if (!CNA::Platform::TryGetWin32(surface_.GetNativeHandle(), nativeWindow))
            throw std::runtime_error("DirectX9Renderer requires a Win32 native window.");
        hwnd_ = static_cast<HWND>(nativeWindow.hwnd);

        const auto drawableSize = surface_.GetDrawableSize();
        width_ = drawableSize.width;
        height_ = drawableSize.height;
        if (width_ <= 0) width_ = args.virtualWidth > 0 ? args.virtualWidth : 1024;
        if (height_ <= 0) height_ = args.virtualHeight > 0 ? args.virtualHeight : 768;

        CreateDeviceResources(args);

        CNA::Logger::Info(
            "D3D9 renderer initialised (" + std::to_string(width_) + "x" +
                std::to_string(height_) + "), VertexShaderVersion=" +
                FormatHr(caps_.VertexShaderVersion) + " PixelShaderVersion=" +
                FormatHr(caps_.PixelShaderVersion),
            CNA::LogCategory::RENDER);
    }

    DirectX9Renderer::~DirectX9Renderer() = default;

    void DirectX9Renderer::EnsureRenderReadyEXT() const
    {
        ThrowIfDeviceLost();
        if (unsafeRenderStateMask_ != 0)
        {
            throw std::runtime_error(
                "D3D9 rendering is blocked because a prior checked state or target operation "
                "failed; reapply the affected complete state or render target transition first");
        }
    }

    void DirectX9Renderer::InjectNextNativeFailureEXT(D3D9NativeOperationEXT operation,
                                                          HRESULT result)
    {
        if (!FAILED(result))
            throw std::invalid_argument("D3D9 native failure injection requires a failing HRESULT");
        nativeFailureInjection_ = {true, operation, result};
    }

    void DirectX9Renderer::ClearNativeFailureInjectionEXT()
    {
        nativeFailureInjection_.active = false;
    }

    bool DirectX9Renderer::HasNativeFailureInjectionEXT() const
    {
        return nativeFailureInjection_.active;
    }

    std::uint64_t DirectX9Renderer::GetNativeCallCountEXT(
        D3D9NativeOperationEXT operation) const
    {
        return nativeCallCounts_[NativeOperationIndex(operation)];
    }

    HRESULT DirectX9Renderer::TakeInjectedResultEXT(D3D9NativeOperationEXT operation)
    {
        if (!nativeFailureInjection_.active || nativeFailureInjection_.operation != operation)
            return D3D_OK;

        nativeFailureInjection_.active = false;
        return nativeFailureInjection_.result;
    }

    void DirectX9Renderer::MarkStateUnsafeEXT(unsigned int safetyBit)
    {
        unsafeRenderStateMask_ |= safetyBit;
    }

    void DirectX9Renderer::MarkStateKnownEXT(unsigned int safetyBit)
    {
        unsafeRenderStateMask_ &= ~safetyBit;
    }

    void DirectX9Renderer::MarkDeviceLostFromScopedFailureEXT()
    {
        if (deviceLost_) return;
        deviceLost_ = true;
        if (deviceEventCallback_) deviceEventCallback_(RendererDeviceEvent::Lost);
    }

    [[noreturn]] void DirectX9Renderer::ThrowScopedFailureEXT(
        const char* operation, HRESULT hr, const std::string& detail)
    {
        std::string message = std::string("D3D9 ") + operation + " failed: " + DescribeHr(hr);
        if (!detail.empty()) message += " (" + detail + ")";

        // Device loss is not an internal rendering error.  Preserve the existing D9-34 lifecycle:
        // transition once to Lost, let Present()/TestCooperativeLevel drive Reset, and reject work
        // in the interval through the established DeviceLostException surface.
        if (hr == D3DERR_DEVICELOST || hr == D3DERR_DEVICENOTRESET)
        {
            MarkDeviceLostFromScopedFailureEXT();
            throw Microsoft::Xna::Framework::Graphics::DeviceLostException(message);
        }

        if (hr == D3DERR_OUTOFVIDEOMEMORY || hr == E_OUTOFMEMORY)
            message += " [resource allocation failure]";

        // One authoritative diagnostic at the native-operation boundary; callers receive the same
        // message in the exception and deliberately do not log it again.
        CNA::Logger::Error(message, CNA::LogCategory::GPU);
        throw std::runtime_error(message);
    }

    void DirectX9Renderer::SetRenderStateCheckedEXT(
        D3DRENDERSTATETYPE state, DWORD value, unsigned int safetyBit, const char* context)
    {
        ThrowIfDeviceLost();
        HRESULT hr = TakeInjectedResultEXT(D3D9NativeOperationEXT::SetRenderState);
        if (SUCCEEDED(hr))
        {
            ++nativeCallCounts_[NativeOperationIndex(D3D9NativeOperationEXT::SetRenderState)];
            hr = device_->SetRenderState(state, value);
        }
        if (FAILED(hr))
        {
            MarkStateUnsafeEXT(safetyBit);
            ThrowScopedFailureEXT("IDirect3DDevice9::SetRenderState", hr, context);
        }
    }

    void DirectX9Renderer::SetViewportCheckedEXT(
        const D3DVIEWPORT9& viewport, unsigned int safetyBit, const char* context)
    {
        ThrowIfDeviceLost();
        const HRESULT hr = InvokeSetViewportEXT(viewport);
        if (FAILED(hr))
        {
            MarkStateUnsafeEXT(safetyBit);
            ThrowScopedFailureEXT("IDirect3DDevice9::SetViewport", hr, context);
        }
    }

    void DirectX9Renderer::SetScissorRectCheckedEXT(
        const RECT& rect, unsigned int safetyBit, const char* context)
    {
        ThrowIfDeviceLost();
        const HRESULT hr = InvokeSetScissorRectEXT(rect);
        if (FAILED(hr))
        {
            MarkStateUnsafeEXT(safetyBit);
            ThrowScopedFailureEXT("IDirect3DDevice9::SetScissorRect", hr, context);
        }
    }

    HRESULT DirectX9Renderer::InvokeSetRenderTargetEXT(DWORD slot, IDirect3DSurface9* surface)
    {
        HRESULT hr = TakeInjectedResultEXT(D3D9NativeOperationEXT::SetRenderTarget);
        if (FAILED(hr)) return hr;
        ++nativeCallCounts_[NativeOperationIndex(D3D9NativeOperationEXT::SetRenderTarget)];
        return device_->SetRenderTarget(slot, surface);
    }

    HRESULT DirectX9Renderer::InvokeSetDepthStencilSurfaceEXT(IDirect3DSurface9* surface)
    {
        HRESULT hr = TakeInjectedResultEXT(D3D9NativeOperationEXT::SetDepthStencilSurface);
        if (FAILED(hr)) return hr;
        ++nativeCallCounts_[NativeOperationIndex(D3D9NativeOperationEXT::SetDepthStencilSurface)];
        return device_->SetDepthStencilSurface(surface);
    }

    HRESULT DirectX9Renderer::InvokeGetRenderTargetEXT(DWORD slot, IDirect3DSurface9** surface)
    {
        HRESULT hr = TakeInjectedResultEXT(D3D9NativeOperationEXT::GetRenderTarget);
        if (FAILED(hr)) return hr;
        ++nativeCallCounts_[NativeOperationIndex(D3D9NativeOperationEXT::GetRenderTarget)];
        return device_->GetRenderTarget(slot, surface);
    }

    HRESULT DirectX9Renderer::InvokeGetDepthStencilSurfaceEXT(IDirect3DSurface9** surface)
    {
        HRESULT hr = TakeInjectedResultEXT(D3D9NativeOperationEXT::GetDepthStencilSurface);
        if (FAILED(hr)) return hr;
        ++nativeCallCounts_[NativeOperationIndex(D3D9NativeOperationEXT::GetDepthStencilSurface)];
        return device_->GetDepthStencilSurface(surface);
    }

    HRESULT DirectX9Renderer::InvokeGetBackBufferEXT(IDirect3DSurface9** surface)
    {
        HRESULT hr = TakeInjectedResultEXT(D3D9NativeOperationEXT::GetBackBuffer);
        if (FAILED(hr)) return hr;
        ++nativeCallCounts_[NativeOperationIndex(D3D9NativeOperationEXT::GetBackBuffer)];
        return device_->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, surface);
    }

    HRESULT DirectX9Renderer::InvokeSetViewportEXT(const D3DVIEWPORT9& viewport)
    {
        HRESULT hr = TakeInjectedResultEXT(D3D9NativeOperationEXT::SetViewport);
        if (FAILED(hr)) return hr;
        ++nativeCallCounts_[NativeOperationIndex(D3D9NativeOperationEXT::SetViewport)];
        return device_->SetViewport(&viewport);
    }

    HRESULT DirectX9Renderer::InvokeSetScissorRectEXT(const RECT& rect)
    {
        HRESULT hr = TakeInjectedResultEXT(D3D9NativeOperationEXT::SetScissorRect);
        if (FAILED(hr)) return hr;
        ++nativeCallCounts_[NativeOperationIndex(D3D9NativeOperationEXT::SetScissorRect)];
        return device_->SetScissorRect(&rect);
    }

    HRESULT DirectX9Renderer::InvokeCreateDepthStencilSurfaceEXT(
        UINT width, UINT height, D3DFORMAT format, D3DMULTISAMPLE_TYPE multiSample, DWORD quality,
        BOOL discard, IDirect3DSurface9** surface)
    {
        if (surface) *surface = nullptr;
        HRESULT hr = TakeInjectedResultEXT(D3D9NativeOperationEXT::CreateDepthStencilSurface);
        if (FAILED(hr)) return hr;
        ++nativeCallCounts_[NativeOperationIndex(D3D9NativeOperationEXT::CreateDepthStencilSurface)];
        return device_->CreateDepthStencilSurface(width, height, format, multiSample, quality, discard,
                                                   surface, nullptr);
    }

    D3DPRESENT_PARAMETERS DirectX9Renderer::BuildPresentParameters() const
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

        pp.hDeviceWindow = hwnd_;

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

    void DirectX9Renderer::CreateDeviceResources(const GraphicsRendererCreateArgs&)
    {
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

        hr = d3d9_->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd_, vertexProcessingFlags,
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

        CacheDefaultDepthStencilSurfaceEXT();
    }

    void DirectX9Renderer::EnsureDeviceSize()
    {
        const auto drawableSize = surface_.GetDrawableSize();
        const int w = drawableSize.width;
        const int h = drawableSize.height;
        if (w <= 0 || h <= 0) return;

        const bool sizeChanged = (w != width_ || h != height_);
        if (!sizeChanged && !presentationDirty_) return;

        const int previousWidth = width_;
        const int previousHeight = height_;
        width_ = w;
        height_ = h;

        // D9-53 fix (found while wiring up render targets): Reset() genuinely fails/misbehaves if
        // any D3DPOOL_DEFAULT resource (a dynamic vertex/index buffer, a render target) is still
        // allocated -- this app-initiated resize path was calling Reset() directly without ever
        // releasing them, unlike PerformResetRecovery()'s own identical release loop. Each resource
        // recreates lazily on its own next use, same as the device-lost path.
        for (ID3D9DefaultPoolResourceEXT* resource : defaultPoolResources_)
        {
            resource->ReleaseDefaultPoolResourceEXT();
        }
        // D9-53 fix (found the hard way via DXVK's own "Device reset failed because device still
        // has alive losable resources" warning): a cached ComPtr<IDirect3DSurface9> obtained via
        // GetDepthStencilSurface()/GetBackBuffer() counts as an app-held reference to a losable
        // resource -- Reset() genuinely fails while one is still alive, a real, well-known D3D9
        // gotcha, not a DXVK quirk. Must release before Reset(), re-cache after (CacheDefault...()
        // below already does the re-cache half).
        defaultDepthStencilSurface_.Reset();

        D3DPRESENT_PARAMETERS pp = BuildPresentParameters();
        HRESULT hr = device_->Reset(&pp);
        if (FAILED(hr))
        {
            // Mirrors PerformResetRecovery()'s own D9-34 resilience: a transient/spurious size
            // observation (e.g. a window manager briefly reporting the full monitor resolution
            // while mapping/animating a brand-new window -- observed for real under Wine here,
            // see this file's own git history) can make Reset() genuinely fail. D3D11's DXGI
            // swapchain absorbs the exact same noise silently (VK_SUBOPTIMAL_KHR -> recreate);
            // D3D9 has no such automatic recovery, so this must do it explicitly. Stay on the
            // previous (last successfully-applied) size and retry on the next natural
            // EnsureDeviceSize() call (every Present()) instead of crashing the whole game --
            // matches XNA's own resilience, same bar as the device-lost path above.
            width_ = previousWidth;
            height_ = previousHeight;
            presentationDirty_ = true;
            CNA::Logger::Warn(
                "D3D9 Reset (resize to " + std::to_string(w) + "x" + std::to_string(h) +
                    ") failed, hr=" + FormatHr(hr) + " -- keeping previous size, retrying next frame",
                CNA::LogCategory::RENDER);
            return;
        }
        presentationDirty_ = false;

        // Reset() invalidates the device's own render-state defaults (e.g. the viewport) --
        // restore what GraphicsDevice itself does not proactively re-push after a resize.
        SetViewport(0, 0, width_, height_, 0.0f, 1.0f);
        CacheDefaultDepthStencilSurfaceEXT();
        // Reset() always reverts the active render target to the (new) back buffer -- any
        // previously-bound custom render target is no longer genuinely active, and its own
        // D3DPOOL_DEFAULT resources were just released above anyway.
        currentCustomRT_ = nullptr;
        currentCustomCubeRT_ = nullptr;
    }

    void DirectX9Renderer::OnSurfaceChanged(const RendererSurfaceInfo& surface)
    {
        surface_.Update(surface);
    }

    void DirectX9Renderer::UpdatePresentationFormatEXT(int backBufferFormat, int depthStencilFormat,
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
        // D9-64 fix: apply immediately rather than deferring to the next Present(). A game (or
        // test) that draws depth/stencil-dependent content on the very first frame, before ever
        // presenting, must see the newly-requested format take effect right away -- deferring left
        // Clear() calls against a stale (often absent) depth-stencil surface failing with
        // D3DERR_INVALIDCALL on that first frame. IGraphicsRenderer::UpdatePresentationFormatEXT()'s
        // own contract only requires applying "on the next natural resize/reset point", not
        // specifically Present() -- EnsureDeviceSize() IS that reset point, just invoked eagerly
        // here instead of lazily. device_ is already constructed by the time this is reachable (see
        // this method's own call site, GraphicsDevice::Reset()).
        EnsureDeviceSize();
    }

    void DirectX9Renderer::RegisterDefaultPoolResourceEXT(ID3D9DefaultPoolResourceEXT* resource)
    {
        defaultPoolResources_.push_back(resource);
    }

    void DirectX9Renderer::UnregisterDefaultPoolResourceEXT(ID3D9DefaultPoolResourceEXT* resource)
    {
        std::erase(defaultPoolResources_, resource);
    }

    void DirectX9Renderer::PerformResetRecovery()
    {
        if (deviceEventCallback_) deviceEventCallback_(RendererDeviceEvent::Resetting);

        // D3DPOOL_MANAGED user resources (D9-4's own spike already confirmed these survive Reset()
        // untouched, with no re-upload) need no action here. D3DPOOL_DEFAULT resources (D9-40's
        // dynamic vertex/index buffers) must be released before Reset() -- each one recreates its
        // own object lazily the next time it is actually used (real XNA/D3D9 behavior: a DYNAMIC
        // VertexBuffer's content does not survive DeviceReset; a game is expected to re-fill it).
        for (ID3D9DefaultPoolResourceEXT* resource : defaultPoolResources_)
        {
            resource->ReleaseDefaultPoolResourceEXT();
        }
        // D9-53 fix: see EnsureDeviceSize()'s identical line -- a cached ComPtr<IDirect3DSurface9>
        // from GetDepthStencilSurface() is itself an app-held reference to a losable resource and
        // must be released before Reset() too, not just the registered D3DPOOL_DEFAULT resources.
        defaultDepthStencilSurface_.Reset();

        D3DPRESENT_PARAMETERS pp = BuildPresentParameters();
        HRESULT hr = device_->Reset(&pp);
        if (FAILED(hr))
        {
            // Reset() can itself fail (genuinely still lost, or a real error) -- stay in the lost
            // state and let the next Present() try again, matching XNA's own resilience (a real
            // game keeps running frames while the device is lost, rather than crashing).
            return;
        }

        // A successful Reset restores a usable native device.  Clear the Lost gate before the
        // checked viewport/default-surface restoration calls; the public Reset event still fires
        // only after those calls complete below.
        deviceLost_ = false;
        SetViewport(0, 0, width_, height_, 0.0f, 1.0f);
        CacheDefaultDepthStencilSurfaceEXT();
        // Same reasoning as EnsureDeviceSize()'s identical line: Reset() always reverts to the back
        // buffer, and any previously-bound custom render target's D3DPOOL_DEFAULT resources were
        // just released above.
        currentCustomRT_ = nullptr;
        currentCustomCubeRT_ = nullptr;
        if (deviceEventCallback_) deviceEventCallback_(RendererDeviceEvent::Reset);
    }

    void DirectX9Renderer::PollDeviceLost()
    {
        HRESULT hr = device_->TestCooperativeLevel();
        if (hr == D3DERR_DEVICENOTRESET)
        {
            PerformResetRecovery();
        }
        // D3DERR_DEVICELOST (still lost) or any other result: nothing more to do this frame:
        // deviceLost_ stays true, and the next Present() will poll again.
    }

    void DirectX9Renderer::ThrowIfDeviceLost() const
    {
        if (deviceLost_)
        {
            throw Microsoft::Xna::Framework::Graphics::DeviceLostException();
        }
    }

    void DirectX9Renderer::GetViewportSize(int& width, int& height)
    {
        width = width_;
        height = height_;
    }

    void DirectX9Renderer::SetVirtualResolution(int width, int height)
    {
        virtualWidth_ = width;
        virtualHeight_ = height;
    }

    void DirectX9Renderer::SetPresentationMode(int mode)
    {
        presentationMode_ = mode;
    }

    void DirectX9Renderer::Clear(float r, float g, float b, float a)
    {
        EnsureRenderReadyEXT();
        HRESULT hr = device_->Clear(0, nullptr, D3DCLEAR_TARGET,
                                     D3DCOLOR_COLORVALUE(r, g, b, a), 1.0f, 0);
        if (FAILED(hr))
            throw std::runtime_error("D3D9 Clear failed, hr=" + FormatHr(hr));
    }

    void DirectX9Renderer::Present()
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
            if (deviceEventCallback_) deviceEventCallback_(RendererDeviceEvent::Lost);
            return;
        }
        if (FAILED(hr))
            throw std::runtime_error("D3D9 Present failed, hr=" + FormatHr(hr));
    }

    void DirectX9Renderer::ClearColorAndDepth(float r, float g, float b, float a, float depth)
    {
        EnsureRenderReadyEXT();
        DWORD flags = D3DCLEAR_TARGET;
        if (HasDepthBuffer(depthStencilFormatOrdinal_)) flags |= D3DCLEAR_ZBUFFER;
        HRESULT hr = device_->Clear(0, nullptr, flags, D3DCOLOR_COLORVALUE(r, g, b, a), depth, 0);
        if (FAILED(hr))
            throw std::runtime_error("D3D9 ClearColorAndDepth failed, hr=" + FormatHr(hr));
    }

    void DirectX9Renderer::ClearDepth(float depth)
    {
        EnsureRenderReadyEXT();
        if (!HasDepthBuffer(depthStencilFormatOrdinal_)) return;
        HRESULT hr = device_->Clear(0, nullptr, D3DCLEAR_ZBUFFER, 0, depth, 0);
        if (FAILED(hr))
            throw std::runtime_error("D3D9 ClearDepth failed, hr=" + FormatHr(hr));
    }

    void DirectX9Renderer::ClearStencil(int stencil)
    {
        EnsureRenderReadyEXT();
        if (!HasStencilBuffer(depthStencilFormatOrdinal_)) return;
        HRESULT hr = device_->Clear(0, nullptr, D3DCLEAR_STENCIL, 0, 1.0f,
                                     static_cast<DWORD>(stencil));
        if (FAILED(hr))
            throw std::runtime_error("D3D9 ClearStencil failed, hr=" + FormatHr(hr));
    }

    void DirectX9Renderer::ClearDepthAndStencil(float depth, int stencil)
    {
        EnsureRenderReadyEXT();
        if (!HasDepthBuffer(depthStencilFormatOrdinal_)) return;
        DWORD flags = D3DCLEAR_ZBUFFER;
        if (HasStencilBuffer(depthStencilFormatOrdinal_)) flags |= D3DCLEAR_STENCIL;
        HRESULT hr = device_->Clear(0, nullptr, flags, 0, depth, static_cast<DWORD>(stencil));
        if (FAILED(hr))
            throw std::runtime_error("D3D9 ClearDepthAndStencil failed, hr=" + FormatHr(hr));
    }

    void DirectX9Renderer::ClearColorAndStencil(float r, float g, float b, float a, int stencil)
    {
        EnsureRenderReadyEXT();
        DWORD flags = D3DCLEAR_TARGET;
        if (HasStencilBuffer(depthStencilFormatOrdinal_)) flags |= D3DCLEAR_STENCIL;
        HRESULT hr = device_->Clear(0, nullptr, flags, D3DCOLOR_COLORVALUE(r, g, b, a), 1.0f,
                                     static_cast<DWORD>(stencil));
        if (FAILED(hr))
            throw std::runtime_error("D3D9 ClearColorAndStencil failed, hr=" + FormatHr(hr));
    }

    void DirectX9Renderer::ClearColorDepthAndStencil(float r, float g, float b, float a,
                                                         float depth, int stencil)
    {
        EnsureRenderReadyEXT();
        DWORD flags = D3DCLEAR_TARGET;
        if (HasDepthBuffer(depthStencilFormatOrdinal_)) flags |= D3DCLEAR_ZBUFFER;
        if (HasStencilBuffer(depthStencilFormatOrdinal_)) flags |= D3DCLEAR_STENCIL;
        HRESULT hr = device_->Clear(0, nullptr, flags, D3DCOLOR_COLORVALUE(r, g, b, a), depth,
                                     static_cast<DWORD>(stencil));
        if (FAILED(hr))
            throw std::runtime_error("D3D9 ClearColorDepthAndStencil failed, hr=" + FormatHr(hr));
    }

    void DirectX9Renderer::ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels)
    {
        EnsureRenderReadyEXT();
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

    // D9-64 fix: these were silent-throw stubs left over from D9-11's skeleton, never wired up by
    // D9-61/D9-82 (which only ever push depth/stencil state through the multi-field
    // ApplyDepthStencilState() path). GraphicsDevice::SetDepthTestEnabled()/SetDepthWriteEnabled()
    // are real public (CNAEXT) API that EasyGL honours and D3D11/D3D12 both wire up -- a game (or,
    // as found here, the reused easygl_blendstate_opaque_test.cpp CTest) calling
    // dev.SetDepthTestEnabled(false) on D3D9 threw instead of taking effect. Unlike D3D11 (which
    // must rebuild a whole cached ID3D11DepthStencilState object from tracked fields), D3D9's
    // render states are independently settable, so this is a direct, single SetRenderState call --
    // no field-tracking struct needed.
    void DirectX9Renderer::SetDepthTestEnabled(bool enabled)
    {
        SetRenderStateCheckedEXT(D3DRS_ZENABLE, enabled ? D3DZB_TRUE : D3DZB_FALSE,
                                 kUnsafeDepthState, "SetDepthTestEnabled(D3DRS_ZENABLE)");
    }

    // Deliberate no-op, matching D3D11's/D3D12's own identical choice: a bare "enable blending" has
    // no defined blend factors in XNA -- real blend configuration always arrives via
    // ApplyBlendState(), which already unconditionally enables blending (D3DRS_ALPHABLENDENABLE)
    // whenever it's called.
    void DirectX9Renderer::SetBlendEnabled(bool) {}

    void DirectX9Renderer::SetDepthWriteEnabled(bool enabled)
    {
        SetRenderStateCheckedEXT(D3DRS_ZWRITEENABLE, enabled ? TRUE : FALSE,
                                 kUnsafeDepthState, "SetDepthWriteEnabled(D3DRS_ZWRITEENABLE)");
    }

    std::unique_ptr<IVertexBufferRenderer> DirectX9Renderer::CreateVertexBuffer(int vertex_capacity)
    {
        return std::make_unique<D3D9VertexBufferRenderer>(*this, device_.Get(), vertex_capacity);
    }

    std::unique_ptr<IIndexBufferRenderer> DirectX9Renderer::CreateIndexBuffer16(int index_capacity)
    {
        return std::make_unique<D3D9IndexBufferRenderer>(*this, device_.Get(), index_capacity, false);
    }

    std::unique_ptr<IIndexBufferRenderer> DirectX9Renderer::CreateIndexBuffer32(int index_capacity)
    {
        return std::make_unique<D3D9IndexBufferRenderer>(*this, device_.Get(), index_capacity, true);
    }

    IDirect3DVertexDeclaration9* DirectX9Renderer::GetOrCreateVertexDeclarationEXT(std::size_t strideInBytes)
    {
        auto it = vertexDeclCache_.find(strideInBytes);
        if (it != vertexDeclCache_.end()) return it->second.Get();

        UINT count = 0;
        const D3DVERTEXELEMENT9* elements = VertexElementsForStrideD3D9(strideInBytes, count);
        if (!elements || count == 0)
            throw std::runtime_error(
                "DirectX9Renderer: no vertex declaration for stride " + std::to_string(strideInBytes));

        ComPtr<IDirect3DVertexDeclaration9> decl;
        const HRESULT hr = device_->CreateVertexDeclaration(elements, decl.GetAddressOf());
        if (FAILED(hr))
            throw std::runtime_error("DirectX9Renderer: CreateVertexDeclaration failed, hr=" + FormatHr(hr));

        IDirect3DVertexDeclaration9* raw = decl.Get();
        vertexDeclCache_.emplace(strideInBytes, std::move(decl));
        return raw;
    }

    void DirectX9Renderer::DrawColoredPrimitives(const IVertexBufferRenderer& vb,
                                                     const Matrix& world, const Matrix& view, const Matrix& projection,
                                                     PrimitiveType primitive, int primitiveCount)
    {
        EnsureRenderReadyEXT();

        // D9-82: matches every other renderer's own DrawColoredPrimitives scope (BasicEffect with
        // VertexColorEnabled=true, DiffuseColor=white, fog disabled, no texture/lighting). Other
        // strides and full effect-aware dispatch are DrawPrimitivesEx's job, not yet implemented
        // (D9-82b/c) -- this row was split the same way D3D11's own DX-61 vs. DX-62..67 was.
        const auto& d3dVb = static_cast<const D3D9VertexBufferRenderer&>(vb);
        const std::size_t stride = d3dVb.GetStrideEXT() > 0 ? d3dVb.GetStrideEXT() : 16;
        if (stride != 16)
            throw std::runtime_error(
                "DirectX9Renderer::DrawColoredPrimitives: only stride-16 (VertexPositionColor) "
                "is implemented so far (plan_dx9.md D9-82)");

        if (!shaderCache_) shaderCache_ = std::make_unique<D3D9ShaderCache>(device_.Get());

        // fogEnabled=false, vertexColorEnabled=true, textureEnabled=false, lightingEnabled=false ->
        // ShaderIndex 3 -> "BasicEffect_VSBasicVcNoFog"/"BasicEffect_PSBasicNoFog" (D9-80's own
        // dispatch tables). Hardcoded to this one fixed variant (rather than a generic
        // name->register-table lookup) since this legacy path only ever needs this single shader
        // pair -- a generic lookup belongs to D9-82b/c's real effect-aware dispatch.
        const int shaderIndex = ComputeBasicEffectShaderIndex(
            /*fogEnabled=*/false, /*vertexColorEnabled=*/true, /*textureEnabled=*/false,
            /*lightingEnabled=*/false, /*preferPerPixelLighting=*/false, /*oneLight=*/false);
        IDirect3DVertexShader9* vs = shaderCache_->GetVertexShader(GetBasicEffectVertexShaderNameEXT(shaderIndex));
        IDirect3DPixelShader9* ps = shaderCache_->GetPixelShader(GetBasicEffectPixelShaderNameEXT(shaderIndex));
        device_->SetVertexShader(vs);
        device_->SetPixelShader(ps);

        // D9-72/D9-82: EffectParameter.SetValue(Matrix)'s real XNA upload convention -- register k
        // receives COLUMN k of the row-major World*View*Projection matrix (M1k,M2k,M3k,M4k), i.e.
        // the TRANSPOSE of Matrix::ToColumnMajor()'s own row-major-flat reading order. Verified
        // against FNA's EffectHelpers.SetWorldViewProjAndFog -> EffectParameter.SetValue(Matrix),
        // not assumed.
        const Matrix wvpT = Matrix::Transpose(world * view * projection);
        float wvpRegs[16];
        wvpT.ToColumnMajor(wvpRegs);
        UploadVertexShaderConstantEXT(device_.Get(), Shaders::kBasicEffect_VSBasicVcNoFog_Registers,
                                      static_cast<int>(std::size(Shaders::kBasicEffect_VSBasicVcNoFog_Registers)),
                                      "WorldViewProj", wvpRegs);
        const float diffuseWhite[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        UploadVertexShaderConstantEXT(device_.Get(), Shaders::kBasicEffect_VSBasicVcNoFog_Registers,
                                      static_cast<int>(std::size(Shaders::kBasicEffect_VSBasicVcNoFog_Registers)),
                                      "DiffuseColor", diffuseWhite);
        // BasicEffect_PSBasicNoFog has no named constants at all (D9-72's own register table is
        // empty for it) -- nothing to upload for the pixel stage.

        device_->SetVertexDeclaration(GetOrCreateVertexDeclarationEXT(stride));
        device_->SetStreamSource(0, d3dVb.GetBufferEXT(), 0, static_cast<UINT>(stride));

        device_->DrawPrimitive(ToD3D9Topology(primitive), 0, static_cast<UINT>(primitiveCount));
    }

    void DirectX9Renderer::DrawIndexedColoredPrimitives(const IVertexBufferRenderer& vb, const IIndexBufferRenderer& ib,
                                                            const Matrix& world, const Matrix& view, const Matrix& projection,
                                                            PrimitiveType primitive, int primitiveCount)
    {
        EnsureRenderReadyEXT();

        // D9-82: indexed counterpart of DrawColoredPrimitives above -- same colored-only scope.
        const auto& d3dVb = static_cast<const D3D9VertexBufferRenderer&>(vb);
        const auto& d3dIb = static_cast<const D3D9IndexBufferRenderer&>(ib);
        const std::size_t stride = d3dVb.GetStrideEXT() > 0 ? d3dVb.GetStrideEXT() : 16;
        if (stride != 16)
            throw std::runtime_error(
                "DirectX9Renderer::DrawIndexedColoredPrimitives: only stride-16 "
                "(VertexPositionColor) is implemented so far (plan_dx9.md D9-82)");

        if (!shaderCache_) shaderCache_ = std::make_unique<D3D9ShaderCache>(device_.Get());

        const int shaderIndex = ComputeBasicEffectShaderIndex(
            /*fogEnabled=*/false, /*vertexColorEnabled=*/true, /*textureEnabled=*/false,
            /*lightingEnabled=*/false, /*preferPerPixelLighting=*/false, /*oneLight=*/false);
        IDirect3DVertexShader9* vs = shaderCache_->GetVertexShader(GetBasicEffectVertexShaderNameEXT(shaderIndex));
        IDirect3DPixelShader9* ps = shaderCache_->GetPixelShader(GetBasicEffectPixelShaderNameEXT(shaderIndex));
        device_->SetVertexShader(vs);
        device_->SetPixelShader(ps);

        const Matrix wvpT = Matrix::Transpose(world * view * projection);
        float wvpRegs[16];
        wvpT.ToColumnMajor(wvpRegs);
        UploadVertexShaderConstantEXT(device_.Get(), Shaders::kBasicEffect_VSBasicVcNoFog_Registers,
                                      static_cast<int>(std::size(Shaders::kBasicEffect_VSBasicVcNoFog_Registers)),
                                      "WorldViewProj", wvpRegs);
        const float diffuseWhite[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        UploadVertexShaderConstantEXT(device_.Get(), Shaders::kBasicEffect_VSBasicVcNoFog_Registers,
                                      static_cast<int>(std::size(Shaders::kBasicEffect_VSBasicVcNoFog_Registers)),
                                      "DiffuseColor", diffuseWhite);

        device_->SetVertexDeclaration(GetOrCreateVertexDeclarationEXT(stride));
        device_->SetStreamSource(0, d3dVb.GetBufferEXT(), 0, static_cast<UINT>(stride));
        device_->SetIndices(d3dIb.GetBufferEXT());

        // NumVertices ("the number of vertices used, starting at BaseVertexIndex+MinVertexIndex" --
        // real D3D9 semantics, not "vertices to render") is safe to over-report as the full buffer's
        // own vertex count: IGraphicsRenderer::DrawIndexedColoredPrimitives() carries no separate
        // min-vertex-index/count from the caller to report a tighter range.
        device_->DrawIndexedPrimitive(ToD3D9Topology(primitive), 0, 0,
                                      static_cast<UINT>(d3dVb.GetVertexCount()),
                                      0, static_cast<UINT>(primitiveCount));
    }

    std::unique_ptr<ITextureRenderer> DirectX9Renderer::CreateTexture(const ImageData& data)
    {
        return std::make_unique<D3D9TextureRenderer>(device_.Get(), data);
    }

    std::unique_ptr<ITextureCubeRenderer> DirectX9Renderer::CreateTextureCube(int size, bool mipMap, int surfaceFormat)
    {
        if (!(caps_.TextureCaps & D3DPTEXTURECAPS_CUBEMAP)) return nullptr;
        return std::make_unique<D3D9TextureCubeRenderer>(device_.Get(), size, mipMap, surfaceFormat);
    }

    std::unique_ptr<ITexture3DRenderer> DirectX9Renderer::CreateTexture3D(int w, int h, int depth, bool mipMap, int surfaceFormat)
    {
        if (caps_.MaxVolumeExtent == 0) return nullptr;
        return std::make_unique<D3D9Texture3DRenderer>(device_.Get(), w, h, depth, mipMap, surfaceFormat);
    }

    std::unique_ptr<IRenderTargetRenderer> DirectX9Renderer::CreateRenderTarget2D(
        int w, int h, int depthFormat, bool /*preserveContents*/, bool /*mipMap*/, int multiSampleCount)
    {
        return std::make_unique<D3D9RenderTargetRenderer>(*this, device_.Get(), w, h, depthFormat, multiSampleCount);
    }

    std::unique_ptr<IRenderTargetCubeRenderer> DirectX9Renderer::CreateRenderTargetCube(
        int size, int depthFormat, bool preserveContents, bool /*mipMap*/, int /*multiSampleCount*/)
    {
        // REMED-GFX-136: consumed by being deliberately unused. D3D9 has no load action at all --
        // SetRenderTarget swaps the surface and leaves its contents alone -- so a cube face is
        // preserved by construction, and the only clear it ever sees is the one
        // GraphicsDevice::SetRenderTargets issues for a DiscardContents target.
        (void) preserveContents;
        return std::make_unique<D3D9RenderTargetCubeRenderer>(*this, device_.Get(), size, depthFormat);
    }

    int DirectX9Renderer::ClampMultiSampleCountEXT(D3DFORMAT format, int requested) const
    {
        if (requested <= 1) return 0;
        // D3D9's D3DMULTISAMPLE_TYPE enumerators for 2..16 samples are numerically equal to the
        // sample count itself (D3DMULTISAMPLE_2_SAMPLES=2, ... D3DMULTISAMPLE_16_SAMPLES=16), so
        // `requested` casts directly -- no lookup table needed.
        DWORD qualityLevels = 0;
        const HRESULT hr = d3d9_->CheckDeviceMultiSampleType(
            D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, format, !isFullScreen_,
            static_cast<D3DMULTISAMPLE_TYPE>(requested), &qualityLevels);
        return SUCCEEDED(hr) ? requested : 0;
    }

    void DirectX9Renderer::RestoreBackBufferRenderTargetEXT()
    {
        ComPtr<IDirect3DSurface9> backBuffer;
        const HRESULT hr = InvokeGetBackBufferEXT(backBuffer.GetAddressOf());
        if (FAILED(hr) || !backBuffer)
        {
            MarkStateUnsafeEXT(kUnsafeTargetBinding);
            ThrowScopedFailureEXT("IDirect3DDevice9::GetBackBuffer",
                                  FAILED(hr) ? hr : E_FAIL,
                                  "restoring the default render target");
        }

        IDirect3DSurface9* color = backBuffer.Get();
        BindRenderTargetSurfacesEXT(&color, 1, defaultDepthStencilSurface_.Get(),
                                    width_, height_, "restoring the default render target");
    }

    void DirectX9Renderer::CacheDefaultDepthStencilSurfaceEXT()
    {
        defaultDepthStencilSurface_.Reset();
        if (!HasDepthBuffer(depthStencilFormatOrdinal_)) return;

        const HRESULT hr = InvokeGetDepthStencilSurfaceEXT(
            defaultDepthStencilSurface_.GetAddressOf());
        if (FAILED(hr) || !defaultDepthStencilSurface_)
        {
            ThrowScopedFailureEXT("IDirect3DDevice9::GetDepthStencilSurface",
                                  FAILED(hr) ? hr : E_FAIL,
                                  "caching the default depth-stencil surface");
        }
    }

    void DirectX9Renderer::CreateDepthStencilSurfaceEXT(
        UINT width, UINT height, D3DFORMAT format, D3DMULTISAMPLE_TYPE multiSample, DWORD quality,
        BOOL discard, IDirect3DSurface9** surface, const char* context)
    {
        if (surface) *surface = nullptr;
        const HRESULT hr = InvokeCreateDepthStencilSurfaceEXT(
            width, height, format, multiSample, quality, discard, surface);
        if (FAILED(hr) || !surface || !*surface)
        {
            if (surface) *surface = nullptr;
            ThrowScopedFailureEXT("IDirect3DDevice9::CreateDepthStencilSurface",
                                  FAILED(hr) ? hr : E_FAIL, context);
        }
    }

    void DirectX9Renderer::BindRenderTargetSurfacesEXT(
        IDirect3DSurface9* const* colorSurfaces, int colorCount,
        IDirect3DSurface9* depthStencilSurface, int width, int height, const char* context)
    {
        ThrowIfDeviceLost();
        const int targetSlots = std::max(1, static_cast<int>(caps_.NumSimultaneousRTs));
        if (!colorSurfaces || colorCount <= 0 || colorCount > targetSlots || !colorSurfaces[0])
            throw std::invalid_argument("D3D9 render-target transition has invalid color surfaces");

        struct BindingSnapshot
        {
            std::vector<ComPtr<IDirect3DSurface9>> colors;
            ComPtr<IDirect3DSurface9> depth;
            D3DVIEWPORT9 viewport{};
        } previous;
        previous.colors.resize(static_cast<std::size_t>(targetSlots));

        HRESULT failure = D3D_OK;
        const char* failedOperation = nullptr;
        for (int slot = 0; slot < targetSlots; ++slot)
        {
            HRESULT hr = InvokeGetRenderTargetEXT(
                static_cast<DWORD>(slot), previous.colors[static_cast<std::size_t>(slot)].GetAddressOf());
            if (hr == D3DERR_NOTFOUND)
            {
                previous.colors[static_cast<std::size_t>(slot)].Reset();
                continue;
            }
            if (FAILED(hr) || (slot == 0 && !previous.colors[0]))
            {
                failure = FAILED(hr) ? hr : E_FAIL;
                failedOperation = "IDirect3DDevice9::GetRenderTarget";
                break;
            }
        }
        if (SUCCEEDED(failure))
        {
            HRESULT hr = InvokeGetDepthStencilSurfaceEXT(previous.depth.GetAddressOf());
            if (hr == D3DERR_NOTFOUND)
            {
                previous.depth.Reset();
            }
            else if (FAILED(hr))
            {
                failure = hr;
                failedOperation = "IDirect3DDevice9::GetDepthStencilSurface";
            }
        }
        if (SUCCEEDED(failure))
        {
            const HRESULT hr = device_->GetViewport(&previous.viewport);
            if (FAILED(hr))
            {
                failure = hr;
                failedOperation = "IDirect3DDevice9::GetViewport";
            }
        }
        if (FAILED(failure))
        {
            MarkStateUnsafeEXT(kUnsafeTargetBinding);
            ThrowScopedFailureEXT(failedOperation, failure,
                                  std::string(context) + "; could not snapshot the previous binding");
        }

        auto rollback = [&]()
        {
            bool restored = true;
            // Mirror D3D9's normal binding order (color slots, then depth, then viewport).  The
            // pre-transition pair remains valid while the color slots are restored, exactly as it
            // did before this attempted switch.
            for (int slot = 0; slot < targetSlots; ++slot)
            {
                if (FAILED(InvokeSetRenderTargetEXT(
                        static_cast<DWORD>(slot), previous.colors[static_cast<std::size_t>(slot)].Get())))
                    restored = false;
            }
            if (FAILED(InvokeSetDepthStencilSurfaceEXT(previous.depth.Get()))) restored = false;
            if (FAILED(InvokeSetViewportEXT(previous.viewport))) restored = false;
            return restored;
        };

        for (int slot = 0; slot < targetSlots; ++slot)
        {
            IDirect3DSurface9* surface = slot < colorCount ? colorSurfaces[slot] : nullptr;
            failure = InvokeSetRenderTargetEXT(static_cast<DWORD>(slot), surface);
            if (FAILED(failure))
            {
                const bool restored = rollback();
                if (!restored) MarkStateUnsafeEXT(kUnsafeTargetBinding);
                ThrowScopedFailureEXT("IDirect3DDevice9::SetRenderTarget", failure,
                                      std::string(context) + (restored
                                          ? "; previous target/depth/viewport restored"
                                          : "; rollback failed; target binding is unsafe"));
            }
        }

        failure = InvokeSetDepthStencilSurfaceEXT(depthStencilSurface);
        if (FAILED(failure))
        {
            const bool restored = rollback();
            if (!restored) MarkStateUnsafeEXT(kUnsafeTargetBinding);
            ThrowScopedFailureEXT("IDirect3DDevice9::SetDepthStencilSurface", failure,
                                  std::string(context) + (restored
                                      ? "; previous target/depth/viewport restored"
                                      : "; rollback failed; target binding is unsafe"));
        }

        D3DVIEWPORT9 viewport{};
        viewport.X = 0;
        viewport.Y = 0;
        viewport.Width = static_cast<DWORD>(width);
        viewport.Height = static_cast<DWORD>(height);
        viewport.MinZ = 0.0f;
        viewport.MaxZ = 1.0f;
        failure = InvokeSetViewportEXT(viewport);
        if (FAILED(failure))
        {
            const bool restored = rollback();
            if (!restored) MarkStateUnsafeEXT(kUnsafeTargetBinding);
            ThrowScopedFailureEXT("IDirect3DDevice9::SetViewport", failure,
                                  std::string(context) + (restored
                                      ? "; previous target/depth/viewport restored"
                                      : "; rollback failed; target binding is unsafe"));
        }

        MarkStateKnownEXT(kUnsafeTargetBinding);
    }

    void DirectX9Renderer::SetRenderTarget2D(IRenderTargetRenderer* rt)
    {
        if (!rt)
        {
            // Resolve before the checked transition, but do NOT restore first: the transaction
            // snapshots the currently-bound pair so a failed back-buffer restore can put this pair
            // back exactly instead of silently leaving an ambiguous half-switch.
            if (currentCustomRT_) currentCustomRT_->ResolveForTransitionEXT();
            RestoreBackBufferRenderTargetEXT();
            currentCustomRT_ = nullptr;
            currentCustomCubeRT_ = nullptr;
            return;
        }
        auto* d3d9Rt = static_cast<D3D9RenderTargetRenderer*>(rt);
        if (currentCustomRT_ && currentCustomRT_ != d3d9Rt)
            currentCustomRT_->ResolveForTransitionEXT();
        d3d9Rt->BindAsRenderTarget();
        // Commit renderer bookkeeping only after the entire color/depth/viewport transition
        // succeeds.  A throwing bind therefore leaves the previous native binding and pointer
        // authoritative for retry A -> failure(B) -> A/B.
        currentCustomRT_ = d3d9Rt;
        currentCustomCubeRT_ = nullptr;
    }

    void DirectX9Renderer::SetRenderTargetCubeFace(IRenderTargetCubeRenderer* rt, int face)
    {
        if (!rt)
        {
            if (currentCustomRT_) currentCustomRT_->ResolveForTransitionEXT();
            RestoreBackBufferRenderTargetEXT();
            currentCustomRT_ = nullptr;
            currentCustomCubeRT_ = nullptr;
            return;
        }
        auto* d3d9CubeRt = static_cast<D3D9RenderTargetCubeRenderer*>(rt);
        if (currentCustomRT_) currentCustomRT_->ResolveForTransitionEXT();
        d3d9CubeRt->BindAsRenderTargetFace(face);
        currentCustomCubeRT_ = d3d9CubeRt;
        currentCustomRT_ = nullptr;
    }

    void DirectX9Renderer::SetRenderTargets(
        const RenderTargetBindingDescriptor* renderTargets, int count)
    {
        if (!renderTargets || count <= 0)
        {
            SetRenderTarget2D(nullptr);
            return;
        }
        if (count == 1 && renderTargets[0].IsRenderTargetCubeFace())
        {
            SetRenderTargetCubeFace(
                renderTargets[0].GetRenderTargetCube(),
                renderTargets[0].GetCubeFace());
            return;
        }
        for (int i = 0; i < count; ++i)
            if (renderTargets[i].IsRenderTargetCubeFace())
                throw std::runtime_error(
                    "DirectX9Renderer::SetRenderTargets: cube faces in a multi-target "
                    "set are not implemented by this CNA renderer.");

        // design decision 13: over-request throws rather than silently degrading (see this
        // method's own header comment) -- NumSimultaneousRTs is a genuine D3DCAPS9 hardware limit
        // (commonly 4), not an arbitrary array-size cap.
        if (count > static_cast<int>(caps_.NumSimultaneousRTs))
        {
            throw std::runtime_error(
                "DirectX9Renderer::SetRenderTargets: requested " + std::to_string(count) +
                " render targets, this device only supports " +
                std::to_string(caps_.NumSimultaneousRTs) + " (D3DCAPS9::NumSimultaneousRTs)");
        }

        std::vector<IDirect3DSurface9*> colorSurfaces;
        colorSurfaces.reserve(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i)
        {
            auto* d3d9Rt = static_cast<D3D9RenderTargetRenderer*>(
                renderTargets[i].GetRenderTarget2D());
            d3d9Rt->EnsureReadyEXT();
            colorSurfaces.push_back(d3d9Rt->GetActiveColorSurfaceEXT());
        }

        auto* first = static_cast<D3D9RenderTargetRenderer*>(
            renderTargets[0].GetRenderTarget2D());
        if (currentCustomRT_) currentCustomRT_->ResolveForTransitionEXT();
        BindRenderTargetSurfacesEXT(colorSurfaces.data(), count, first->GetDepthStencilSurfaceEXT(),
                                    first->GetWidth(), first->GetHeight(), "binding multiple render targets");
        currentCustomRT_ = nullptr;
        currentCustomCubeRT_ = nullptr;

        // MRT binds are NOT tracked in currentCustomRT_/currentCustomCubeRT_ (a single pointer
        // can't represent N targets, matches D3D11's own identical MRT-tracking gap) -- unbinding
        // an MRT set must go through SetRenderTargets(nullptr, 0) or SetRenderTarget2D(nullptr)
        // (both unconditionally restore the back buffer), not a single-target bind expecting an
        // implicit MRT-aware unbind first.
    }

    std::unique_ptr<IOcclusionQueryRenderer> DirectX9Renderer::CreateOcclusionQuery()
    {
        // Official D3D9 support-probe idiom: CreateQuery(type, nullptr) succeeds iff the device
        // supports the query type, without actually creating one.
        if (FAILED(device_->CreateQuery(D3DQUERYTYPE_OCCLUSION, nullptr))) return nullptr;
        return std::make_unique<D3D9OcclusionQueryRenderer>(device_.Get());
    }

    std::unique_ptr<ISpriteBatchRenderer> DirectX9Renderer::CreateSpriteBatch()
    {
        return std::make_unique<D3D9SpriteBatchRenderer>(this);
    }

    std::unique_ptr<IEffectRenderer> DirectX9Renderer::CreateEffectRenderer(
        const std::string& vertSrc, const std::string& fragSrc)
    {
        using Microsoft::Xna::Framework::Graphics::GraphicsProfile;
        const bool hiDef = static_cast<GraphicsProfile>(graphicsProfileOrdinal_) == GraphicsProfile::HiDef;
        auto renderer = std::make_unique<D3D9EffectRenderer>(device_.Get(), hiDef);
        if (!vertSrc.empty() && !fragSrc.empty())
            renderer->CompileProgram(vertSrc, fragSrc);
        return renderer;
    }

    void DirectX9Renderer::SetSwapInterval(int interval)
    {
        // Unlike D3D11/D3D12 (DXGI's Present() takes a sync-interval argument directly, applied
        // ad hoc per frame), D3D9 has no such per-Present() knob -- PresentationInterval only
        // exists as a D3DPRESENT_PARAMETERS field, settable solely through CreateDevice/Reset().
        // So this must go through the same presentationDirty_/EnsureDeviceSize() reset path as
        // UpdatePresentationFormatEXT() above, including its D9-64 "apply immediately, don't defer
        // to the next Present()" reasoning: a game may Clear()/draw before ever presenting.
        if (interval == swapInterval_) return;
        swapInterval_ = interval;
        presentationDirty_ = true;
        EnsureDeviceSize();
    }

    void DirectX9Renderer::ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                                               int colorDstBlend, int alphaDstBlend,
                                               int colorBlendFunc, int alphaBlendFunc,
                                               const BlendWriteState& writeState)
    {
        SetRenderStateCheckedEXT(D3DRS_ALPHABLENDENABLE, TRUE, kUnsafeBlendState,
                                 "ApplyBlendState(D3DRS_ALPHABLENDENABLE)");
        SetRenderStateCheckedEXT(D3DRS_SEPARATEALPHABLENDENABLE, TRUE, kUnsafeBlendState,
                                 "ApplyBlendState(D3DRS_SEPARATEALPHABLENDENABLE)");
        SetRenderStateCheckedEXT(D3DRS_SRCBLEND, BlendToD3D9(colorSrcBlend), kUnsafeBlendState,
                                 "ApplyBlendState(D3DRS_SRCBLEND)");
        SetRenderStateCheckedEXT(D3DRS_DESTBLEND, BlendToD3D9(colorDstBlend), kUnsafeBlendState,
                                 "ApplyBlendState(D3DRS_DESTBLEND)");
        SetRenderStateCheckedEXT(D3DRS_BLENDOP, BlendFunctionToD3D9(colorBlendFunc), kUnsafeBlendState,
                                 "ApplyBlendState(D3DRS_BLENDOP)");
        SetRenderStateCheckedEXT(D3DRS_SRCBLENDALPHA, BlendToD3D9(alphaSrcBlend), kUnsafeBlendState,
                                 "ApplyBlendState(D3DRS_SRCBLENDALPHA)");
        SetRenderStateCheckedEXT(D3DRS_DESTBLENDALPHA, BlendToD3D9(alphaDstBlend), kUnsafeBlendState,
                                 "ApplyBlendState(D3DRS_DESTBLENDALPHA)");
        SetRenderStateCheckedEXT(D3DRS_BLENDOPALPHA, BlendFunctionToD3D9(alphaBlendFunc), kUnsafeBlendState,
                                 "ApplyBlendState(D3DRS_BLENDOPALPHA)");
        // REMED-GFX-077: XNA ColorWriteChannels (R=1,G=2,B=4,A=8) is bit-identical to
        // D3DCOLORWRITEENABLE_*, so the raw value drops straight into D3DRS_COLORWRITEENABLE. These
        // are dynamic render states (no state object → nothing to cache/key). Slot 0 is always
        // supported; the independent per-RT masks (slots 1/2/3) need D3DPMISCCAPS_INDEPENDENTWRITEMASKS.
        SetRenderStateCheckedEXT(D3DRS_COLORWRITEENABLE,
                                 static_cast<DWORD>(writeState.colorWriteChannels[0] & 0xF),
                                 kUnsafeBlendState, "ApplyBlendState(D3DRS_COLORWRITEENABLE)");
        if (caps_.PrimitiveMiscCaps & D3DPMISCCAPS_INDEPENDENTWRITEMASKS)
        {
            SetRenderStateCheckedEXT(D3DRS_COLORWRITEENABLE1,
                                     static_cast<DWORD>(writeState.colorWriteChannels[1] & 0xF),
                                     kUnsafeBlendState, "ApplyBlendState(D3DRS_COLORWRITEENABLE1)");
            SetRenderStateCheckedEXT(D3DRS_COLORWRITEENABLE2,
                                     static_cast<DWORD>(writeState.colorWriteChannels[2] & 0xF),
                                     kUnsafeBlendState, "ApplyBlendState(D3DRS_COLORWRITEENABLE2)");
            SetRenderStateCheckedEXT(D3DRS_COLORWRITEENABLE3,
                                     static_cast<DWORD>(writeState.colorWriteChannels[3] & 0xF),
                                     kUnsafeBlendState, "ApplyBlendState(D3DRS_COLORWRITEENABLE3)");
        }
        // BlendState.MultiSampleMask → D3DRS_MULTISAMPLEMASK (dynamic; only affects MSAA targets).
        SetRenderStateCheckedEXT(D3DRS_MULTISAMPLEMASK, static_cast<DWORD>(writeState.multiSampleMask),
                                 kUnsafeBlendState, "ApplyBlendState(D3DRS_MULTISAMPLEMASK)");
        MarkStateKnownEXT(kUnsafeBlendState);
    }

    void DirectX9Renderer::SetBlendFactor(float r, float g, float b, float a)
    {
        SetRenderStateCheckedEXT(D3DRS_BLENDFACTOR, D3DCOLOR_COLORVALUE(r, g, b, a),
                                 kUnsafeBlendState, "SetBlendFactor(D3DRS_BLENDFACTOR)");
    }

    void DirectX9Renderer::ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable,
                                                      int depthFunc,
                                                      bool stencilEnable, int stencilFunc,
                                                      int stencilPass, int stencilFail, int stencilDepthFail,
                                                      int stencilMask, int stencilWriteMask, int referenceStencil,
                                                      bool twoSidedStencilMode,
                                                      int ccwStencilFunc, int ccwStencilPass,
                                                      int ccwStencilFail, int ccwStencilDepthFail)
    {
        SetRenderStateCheckedEXT(D3DRS_ZENABLE, depthEnable ? D3DZB_TRUE : D3DZB_FALSE,
                                 kUnsafeDepthState, "ApplyDepthStencilState(D3DRS_ZENABLE)");
        SetRenderStateCheckedEXT(D3DRS_ZWRITEENABLE, depthWriteEnable ? TRUE : FALSE,
                                 kUnsafeDepthState, "ApplyDepthStencilState(D3DRS_ZWRITEENABLE)");
        SetRenderStateCheckedEXT(D3DRS_ZFUNC, CompareFunctionToD3D9(depthFunc), kUnsafeDepthState,
                                 "ApplyDepthStencilState(D3DRS_ZFUNC)");
        SetRenderStateCheckedEXT(D3DRS_STENCILENABLE, stencilEnable ? TRUE : FALSE,
                                 kUnsafeDepthState, "ApplyDepthStencilState(D3DRS_STENCILENABLE)");
        SetRenderStateCheckedEXT(D3DRS_STENCILFUNC, CompareFunctionToD3D9(stencilFunc), kUnsafeDepthState,
                                 "ApplyDepthStencilState(D3DRS_STENCILFUNC)");
        SetRenderStateCheckedEXT(D3DRS_STENCILPASS, StencilOperationToD3D9(stencilPass), kUnsafeDepthState,
                                 "ApplyDepthStencilState(D3DRS_STENCILPASS)");
        SetRenderStateCheckedEXT(D3DRS_STENCILFAIL, StencilOperationToD3D9(stencilFail), kUnsafeDepthState,
                                 "ApplyDepthStencilState(D3DRS_STENCILFAIL)");
        SetRenderStateCheckedEXT(D3DRS_STENCILZFAIL, StencilOperationToD3D9(stencilDepthFail), kUnsafeDepthState,
                                 "ApplyDepthStencilState(D3DRS_STENCILZFAIL)");
        SetRenderStateCheckedEXT(D3DRS_STENCILMASK, static_cast<DWORD>(stencilMask), kUnsafeDepthState,
                                 "ApplyDepthStencilState(D3DRS_STENCILMASK)");
        SetRenderStateCheckedEXT(D3DRS_STENCILWRITEMASK, static_cast<DWORD>(stencilWriteMask),
                                 kUnsafeDepthState, "ApplyDepthStencilState(D3DRS_STENCILWRITEMASK)");
        SetRenderStateCheckedEXT(D3DRS_STENCILREF, static_cast<DWORD>(referenceStencil), kUnsafeDepthState,
                                 "ApplyDepthStencilState(D3DRS_STENCILREF)");
        SetRenderStateCheckedEXT(D3DRS_TWOSIDEDSTENCILMODE, twoSidedStencilMode ? TRUE : FALSE,
                                 kUnsafeDepthState, "ApplyDepthStencilState(D3DRS_TWOSIDEDSTENCILMODE)");
        SetRenderStateCheckedEXT(D3DRS_CCW_STENCILFUNC, CompareFunctionToD3D9(ccwStencilFunc),
                                 kUnsafeDepthState, "ApplyDepthStencilState(D3DRS_CCW_STENCILFUNC)");
        SetRenderStateCheckedEXT(D3DRS_CCW_STENCILPASS, StencilOperationToD3D9(ccwStencilPass),
                                 kUnsafeDepthState, "ApplyDepthStencilState(D3DRS_CCW_STENCILPASS)");
        SetRenderStateCheckedEXT(D3DRS_CCW_STENCILFAIL, StencilOperationToD3D9(ccwStencilFail),
                                 kUnsafeDepthState, "ApplyDepthStencilState(D3DRS_CCW_STENCILFAIL)");
        SetRenderStateCheckedEXT(D3DRS_CCW_STENCILZFAIL, StencilOperationToD3D9(ccwStencilDepthFail),
                                 kUnsafeDepthState, "ApplyDepthStencilState(D3DRS_CCW_STENCILZFAIL)");
        MarkStateKnownEXT(kUnsafeDepthState);
    }

    void DirectX9Renderer::SetReferenceStencil(int value)
    {
        SetRenderStateCheckedEXT(D3DRS_STENCILREF, static_cast<DWORD>(value), kUnsafeDepthState,
                                 "SetReferenceStencil(D3DRS_STENCILREF)");
    }

    void DirectX9Renderer::ApplyRasterizerState(int cullMode, int fillMode,
                                                    bool scissorTestEnable,
                                                    float depthBias, float slopeScaleDepthBias)
    {
        SetRenderStateCheckedEXT(D3DRS_CULLMODE, CullModeToD3D9(cullMode), kUnsafeRasterizerState,
                                 "ApplyRasterizerState(D3DRS_CULLMODE)");
        SetRenderStateCheckedEXT(D3DRS_FILLMODE, FillModeToD3D9(fillMode), kUnsafeRasterizerState,
                                 "ApplyRasterizerState(D3DRS_FILLMODE)");
        SetRenderStateCheckedEXT(D3DRS_SCISSORTESTENABLE, scissorTestEnable ? TRUE : FALSE,
                                 kUnsafeRasterizerState,
                                 "ApplyRasterizerState(D3DRS_SCISSORTESTENABLE)");
        // D3D9's D3DRS_DEPTHBIAS/SLOPESCALEDEPTHBIAS are floats (unlike D3D11's INT DepthBias) --
        // XNA's own float DepthBias/SlopeScaleDepthBias (Task 767's "r"-scaled convention) map
        // through directly, with no unit conversion needed (D3D11 needed to round to an INT; D3D9
        // needs no such step since both are already float) -- SetRenderState() still takes a DWORD
        // parameter for these, so the float bits are reinterpreted, not numerically converted.
        SetRenderStateCheckedEXT(D3DRS_DEPTHBIAS, std::bit_cast<DWORD>(depthBias),
                                 kUnsafeRasterizerState, "ApplyRasterizerState(D3DRS_DEPTHBIAS)");
        SetRenderStateCheckedEXT(D3DRS_SLOPESCALEDEPTHBIAS, std::bit_cast<DWORD>(slopeScaleDepthBias),
                                 kUnsafeRasterizerState,
                                 "ApplyRasterizerState(D3DRS_SLOPESCALEDEPTHBIAS)");
        MarkStateKnownEXT(kUnsafeRasterizerState);
    }

    void DirectX9Renderer::SetScissorRect(int x, int y, int w, int h)
    {
        RECT rect{};
        rect.left = x;
        rect.top = y;
        rect.right = x + w;
        rect.bottom = y + h;
        SetScissorRectCheckedEXT(rect, kUnsafeScissor, "SetScissorRect");
        MarkStateKnownEXT(kUnsafeScissor);
    }

    void DirectX9Renderer::ApplySamplerState(int slot, int filter, int addressU, int addressV, int maxAnisotropy)
    {
        if (slot < 0 || slot >= static_cast<int>(caps_.MaxSimultaneousTextures)) return;

        const DWORD sampler = static_cast<DWORD>(slot);
        const D3D9FilterTriple filterTriple = TextureFilterToD3D9(filter);
        device_->SetSamplerState(sampler, D3DSAMP_MINFILTER, static_cast<DWORD>(filterTriple.min));
        device_->SetSamplerState(sampler, D3DSAMP_MAGFILTER, static_cast<DWORD>(filterTriple.mag));
        device_->SetSamplerState(sampler, D3DSAMP_MIPFILTER, static_cast<DWORD>(filterTriple.mip));
        device_->SetSamplerState(sampler, D3DSAMP_ADDRESSU, static_cast<DWORD>(TextureAddressModeToD3D9(addressU)));
        device_->SetSamplerState(sampler, D3DSAMP_ADDRESSV, static_cast<DWORD>(TextureAddressModeToD3D9(addressV)));
        device_->SetSamplerState(sampler, D3DSAMP_MAXANISOTROPY, static_cast<DWORD>(maxAnisotropy));
    }

    void DirectX9Renderer::SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth)
    {
        D3DVIEWPORT9 vp{};
        vp.X = static_cast<DWORD>(std::max(0, x));
        vp.Y = static_cast<DWORD>(std::max(0, y));
        vp.Width = static_cast<DWORD>(std::max(0, w));
        vp.Height = static_cast<DWORD>(std::max(0, h));
        vp.MinZ = minDepth;
        vp.MaxZ = maxDepth;
        SetViewportCheckedEXT(vp, kUnsafeViewport, "SetViewport");
        MarkStateKnownEXT(kUnsafeViewport);
    }

    void DirectX9Renderer::SetContextRecoveryEnabled(bool)
    {
        NotYetImplemented("DIRECTX9", "SetContextRecoveryEnabled");
    }

    void DirectX9Renderer::SetStringMarkerEXT(const char*)
    {
        NotYetImplemented("DIRECTX9", "SetStringMarkerEXT");
    }

    void DirectX9Renderer::DebugSimulateContextLoss()
    {
        // D9-34: this is the pre-existing, GraphicsDevice-calls-into-renderer test channel (design
        // decision: reuse it rather than invent a new one) -- genuinely sets deviceLost_ and fires
        // the real DeviceLost event, even though nothing actually lost the D3D9 device. Lets the
        // full Lost->Resetting->Reset event sequence and a real Reset() call be exercised
        // deterministically, since DXVK will rarely lose the device naturally (design decision 2's
        // own documented cost).
        if (deviceLost_) return;
        deviceLost_ = true;
        if (deviceEventCallback_) deviceEventCallback_(RendererDeviceEvent::Lost);
    }

    void DirectX9Renderer::DebugRestoreContext()
    {
        // D9-34: the simulated counterpart of PollDeviceLost() -- skips the real
        // TestCooperativeLevel() wait (nothing genuinely lost the device, so it would never report
        // D3DERR_DEVICENOTRESET) and goes straight to the real Resetting->Reset()->Reset sequence.
        if (!deviceLost_) return;
        PerformResetRecovery();
    }
}

namespace CNA::Internal::Renderers
{
    std::unique_ptr<IGraphicsRenderer> CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args)
    {
        return std::make_unique<DirectX9::DirectX9Renderer>(args);
    }
}
