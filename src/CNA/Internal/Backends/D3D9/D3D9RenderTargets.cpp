#include "CNA/Internal/Backends/D3D9/D3D9RenderTargets.hpp"
#include "CNA/Internal/Backends/D3D9/D3D9GraphicsBackend.hpp"
#include "CNA/Internal/Backends/D3D9/D3D9FormatMapping.hpp"

#include <cstdio>
#include <stdexcept>

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
    }

    // -------------------------------------------------------------------------
    // D3D9RenderTargetBackend
    // -------------------------------------------------------------------------

    D3D9RenderTargetBackend::D3D9RenderTargetBackend(
        D3D9GraphicsBackend& owner, IDirect3DDevice9* device,
        int w, int h, int depthFormat, int multiSampleCount)
        : owner_(&owner), device_(device)
        , width_(w), height_(h), depthFormatOrdinal_(depthFormat)
        , hasDepth_(DepthFormatToD3D9(depthFormat) != D3DFMT_UNKNOWN)
        , requestedMultiSampleCount_(multiSampleCount)
    {
        Recreate();
        owner_->RegisterDefaultPoolResourceEXT(this);
    }

    D3D9RenderTargetBackend::~D3D9RenderTargetBackend()
    {
        if (owner_) owner_->UnregisterDefaultPoolResourceEXT(this);
    }

    void D3D9RenderTargetBackend::Recreate()
    {
        constexpr D3DFORMAT colorFormat = D3DFMT_A8B8G8R8;
        appliedMultiSampleCount_ = owner_->ClampMultiSampleCountEXT(colorFormat, requestedMultiSampleCount_);

        HRESULT hr = device_->CreateTexture(
            static_cast<UINT>(width_), static_cast<UINT>(height_), 1,
            D3DUSAGE_RENDERTARGET, colorFormat, D3DPOOL_DEFAULT,
            colorTexture_.ReleaseAndGetAddressOf(), nullptr);
        if (FAILED(hr))
            throw std::runtime_error("D3D9RenderTargetBackend: CreateTexture failed, hr=" + FormatHr(hr));

        hr = colorTexture_->GetSurfaceLevel(0, colorSurface_.ReleaseAndGetAddressOf());
        if (FAILED(hr))
            throw std::runtime_error("D3D9RenderTargetBackend: GetSurfaceLevel failed, hr=" + FormatHr(hr));

        msaaSurface_.Reset();
        if (appliedMultiSampleCount_ > 1)
        {
            hr = device_->CreateRenderTarget(
                static_cast<UINT>(width_), static_cast<UINT>(height_), colorFormat,
                static_cast<D3DMULTISAMPLE_TYPE>(appliedMultiSampleCount_), 0, FALSE,
                msaaSurface_.ReleaseAndGetAddressOf(), nullptr);
            if (FAILED(hr))
                throw std::runtime_error("D3D9RenderTargetBackend: CreateRenderTarget (MSAA) failed, hr=" + FormatHr(hr));
        }

        depthStencilSurface_.Reset();
        if (hasDepth_)
        {
            const D3DFORMAT depthFmt = DepthFormatToD3D9(depthFormatOrdinal_);
            const D3DMULTISAMPLE_TYPE dsMsaa = appliedMultiSampleCount_ > 1
                ? static_cast<D3DMULTISAMPLE_TYPE>(appliedMultiSampleCount_) : D3DMULTISAMPLE_NONE;
            hr = device_->CreateDepthStencilSurface(
                static_cast<UINT>(width_), static_cast<UINT>(height_), depthFmt, dsMsaa, 0, TRUE,
                depthStencilSurface_.ReleaseAndGetAddressOf(), nullptr);
            if (FAILED(hr))
                throw std::runtime_error("D3D9RenderTargetBackend: CreateDepthStencilSurface failed, hr=" + FormatHr(hr));
        }
    }

    void D3D9RenderTargetBackend::BindAsRenderTarget()
    {
        // D9-40/D9-53: lazily recreate if a prior device-lost recovery released the D3DPOOL_DEFAULT
        // resources (ReleaseDefaultPoolResourceEXT()) -- same "next use recreates" convention D9-40's
        // vertex/index buffers already use, applied here since a render target's natural "next use"
        // is binding it, not a SetData() call.
        if (!colorTexture_) Recreate();

        device_->SetRenderTarget(0, (appliedMultiSampleCount_ > 1 ? msaaSurface_ : colorSurface_).Get());
        device_->SetDepthStencilSurface(depthStencilSurface_.Get());

        D3DVIEWPORT9 vp{};
        vp.X = 0;
        vp.Y = 0;
        vp.Width = static_cast<DWORD>(width_);
        vp.Height = static_cast<DWORD>(height_);
        vp.MinZ = 0.0f;
        vp.MaxZ = 1.0f;
        device_->SetViewport(&vp);
    }

    void D3D9RenderTargetBackend::UnbindAsRenderTarget()
    {
        if (appliedMultiSampleCount_ > 1 && msaaSurface_ && colorSurface_)
        {
            device_->StretchRect(msaaSurface_.Get(), nullptr, colorSurface_.Get(), nullptr, D3DTEXF_NONE);
        }
        if (owner_) owner_->RestoreBackBufferRenderTargetEXT();
    }

    void D3D9RenderTargetBackend::ReleaseDefaultPoolResourceEXT()
    {
        colorSurface_.Reset();
        colorTexture_.Reset();
        msaaSurface_.Reset();
        depthStencilSurface_.Reset();
    }

    // -------------------------------------------------------------------------
    // D3D9RenderTargetCubeBackend
    // -------------------------------------------------------------------------

    D3D9RenderTargetCubeBackend::D3D9RenderTargetCubeBackend(
        D3D9GraphicsBackend& owner, IDirect3DDevice9* device, int size, int depthFormat)
        : owner_(&owner), device_(device)
        , size_(size), depthFormatOrdinal_(depthFormat)
        , hasDepth_(DepthFormatToD3D9(depthFormat) != D3DFMT_UNKNOWN)
    {
        Recreate();
        owner_->RegisterDefaultPoolResourceEXT(this);
    }

    D3D9RenderTargetCubeBackend::~D3D9RenderTargetCubeBackend()
    {
        if (owner_) owner_->UnregisterDefaultPoolResourceEXT(this);
    }

    void D3D9RenderTargetCubeBackend::Recreate()
    {
        HRESULT hr = device_->CreateCubeTexture(
            static_cast<UINT>(size_), 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8B8G8R8, D3DPOOL_DEFAULT,
            texture_.ReleaseAndGetAddressOf(), nullptr);
        if (FAILED(hr))
            throw std::runtime_error("D3D9RenderTargetCubeBackend: CreateCubeTexture failed, hr=" + FormatHr(hr));

        depthStencilSurface_.Reset();
        if (hasDepth_)
        {
            const D3DFORMAT depthFmt = DepthFormatToD3D9(depthFormatOrdinal_);
            hr = device_->CreateDepthStencilSurface(
                static_cast<UINT>(size_), static_cast<UINT>(size_), depthFmt, D3DMULTISAMPLE_NONE, 0, TRUE,
                depthStencilSurface_.ReleaseAndGetAddressOf(), nullptr);
            if (FAILED(hr))
                throw std::runtime_error("D3D9RenderTargetCubeBackend: CreateDepthStencilSurface failed, hr=" + FormatHr(hr));
        }
    }

    void D3D9RenderTargetCubeBackend::BindAsRenderTargetFace(int face)
    {
        if (face < 0 || face >= 6) return;
        if (!texture_) Recreate();

        activeFace_ = face;
        ComPtr<IDirect3DSurface9> faceSurface;
        HRESULT hr = texture_->GetCubeMapSurface(static_cast<D3DCUBEMAP_FACES>(face), 0, faceSurface.GetAddressOf());
        if (FAILED(hr))
            throw std::runtime_error("D3D9RenderTargetCubeBackend::BindAsRenderTargetFace: GetCubeMapSurface failed, hr=" + FormatHr(hr));

        device_->SetRenderTarget(0, faceSurface.Get());
        device_->SetDepthStencilSurface(depthStencilSurface_.Get());

        D3DVIEWPORT9 vp{};
        vp.X = 0;
        vp.Y = 0;
        vp.Width = static_cast<DWORD>(size_);
        vp.Height = static_cast<DWORD>(size_);
        vp.MinZ = 0.0f;
        vp.MaxZ = 1.0f;
        device_->SetViewport(&vp);
    }

    void D3D9RenderTargetCubeBackend::UnbindAsRenderTarget()
    {
        activeFace_ = -1;
        if (owner_) owner_->RestoreBackBufferRenderTargetEXT();
    }

    void D3D9RenderTargetCubeBackend::ReleaseDefaultPoolResourceEXT()
    {
        texture_.Reset();
        depthStencilSurface_.Reset();
    }
}
