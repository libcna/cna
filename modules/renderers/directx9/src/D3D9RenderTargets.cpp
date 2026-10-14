#include "CNA/Internal/Renderers/DirectX9/D3D9RenderTargets.hpp"
#include "CNA/Internal/Renderers/DirectX9/DirectX9Renderer.hpp"
#include "CNA/Internal/Renderers/DirectX9/D3D9FormatMapping.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include "System/ArgumentOutOfRangeException.hpp"
#include "System/NotSupportedException.hpp"

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

        int FullMipLevelCount(int w, int h)
        {
            int levels = 1;
            for (int extent = std::max(w, h); extent > 1; extent >>= 1) ++levels;
            return levels;
        }

        int LevelExtent(int base, int level) { return std::max(1, base >> level); }

        // Downsamples `levels` consecutive mip surfaces of one chain, each from the one above it.
        // StretchRect is D3D9's render-target copy with filtering, and every level of a
        // D3DUSAGE_RENDERTARGET texture is a render-target surface, so it is the mip generation
        // that keeps the sublevels addressable (an autogen texture would hide them from
        // GetSurfaceLevel and therefore from GetData).
        template <typename GetLevelSurface>
        void DownsampleChain(IDirect3DDevice9* device, int levels, GetLevelSurface getSurface,
                             const char* who)
        {
            for (int level = 1; level < levels; ++level)
            {
                ComPtr<IDirect3DSurface9> src = getSurface(level - 1);
                ComPtr<IDirect3DSurface9> dst = getSurface(level);
                const HRESULT hr = device->StretchRect(src.Get(), nullptr, dst.Get(), nullptr,
                                                       D3DTEXF_LINEAR);
                if (FAILED(hr))
                    throw std::runtime_error(std::string(who) +
                                             ": StretchRect into mip level " +
                                             std::to_string(level) + " failed, hr=" + FormatHr(hr));
            }
        }
    }

    // -------------------------------------------------------------------------
    // D3D9RenderTargetRenderer
    // -------------------------------------------------------------------------

    D3D9RenderTargetRenderer::D3D9RenderTargetRenderer(
        DirectX9Renderer& owner, IDirect3DDevice9* device,
        int w, int h, int depthFormat, bool mipMap, int multiSampleCount)
        : owner_(&owner), device_(device)
        , width_(w), height_(h)
        , levelCount_(mipMap ? FullMipLevelCount(w, h) : 1)
        , depthFormatOrdinal_(depthFormat)
        , hasDepth_(DepthFormatToD3D9(depthFormat) != D3DFMT_UNKNOWN)
        , requestedMultiSampleCount_(multiSampleCount)
    {
        Recreate();
        owner_->RegisterDefaultPoolResourceEXT(this);
    }

    D3D9RenderTargetRenderer::~D3D9RenderTargetRenderer()
    {
        if (owner_) owner_->UnregisterDefaultPoolResourceEXT(this);
    }

    void D3D9RenderTargetRenderer::Recreate()
    {
        constexpr D3DFORMAT colorFormat = D3DFMT_A8B8G8R8;
        appliedMultiSampleCount_ = owner_->ClampMultiSampleCountEXT(colorFormat, requestedMultiSampleCount_);

        HRESULT hr = device_->CreateTexture(
            static_cast<UINT>(width_), static_cast<UINT>(height_),
            static_cast<UINT>(levelCount_),
            D3DUSAGE_RENDERTARGET, colorFormat, D3DPOOL_DEFAULT,
            colorTexture_.ReleaseAndGetAddressOf(), nullptr);
        if (FAILED(hr))
            throw std::runtime_error("D3D9RenderTargetRenderer: CreateTexture failed, hr=" + FormatHr(hr));

        hr = colorTexture_->GetSurfaceLevel(0, colorSurface_.ReleaseAndGetAddressOf());
        if (FAILED(hr))
            throw std::runtime_error("D3D9RenderTargetRenderer: GetSurfaceLevel failed, hr=" + FormatHr(hr));

        msaaSurface_.Reset();
        if (appliedMultiSampleCount_ > 1)
        {
            hr = device_->CreateRenderTarget(
                static_cast<UINT>(width_), static_cast<UINT>(height_), colorFormat,
                static_cast<D3DMULTISAMPLE_TYPE>(appliedMultiSampleCount_), 0, FALSE,
                msaaSurface_.ReleaseAndGetAddressOf(), nullptr);
            if (FAILED(hr))
                throw std::runtime_error("D3D9RenderTargetRenderer: CreateRenderTarget (MSAA) failed, hr=" + FormatHr(hr));
        }

        depthStencilSurface_.Reset();
        if (hasDepth_)
        {
            const D3DFORMAT depthFmt = DepthFormatToD3D9(depthFormatOrdinal_);
            const D3DMULTISAMPLE_TYPE dsMsaa = appliedMultiSampleCount_ > 1
                ? static_cast<D3DMULTISAMPLE_TYPE>(appliedMultiSampleCount_) : D3DMULTISAMPLE_NONE;
            owner_->CreateDepthStencilSurfaceEXT(
                static_cast<UINT>(width_), static_cast<UINT>(height_), depthFmt, dsMsaa, 0, TRUE,
                depthStencilSurface_.ReleaseAndGetAddressOf(),
                "D3D9RenderTargetRenderer depth allocation");
        }
    }

    void D3D9RenderTargetRenderer::BindAsRenderTarget()
    {
        // D9-40/D9-53: lazily recreate if a prior device-lost recovery released the D3DPOOL_DEFAULT
        // resources (ReleaseDefaultPoolResourceEXT()) -- same "next use recreates" convention D9-40's
        // vertex/index buffers already use, applied here since a render target's natural "next use"
        // is binding it, not a SetData() call.
        if (!colorTexture_) Recreate();

        IDirect3DSurface9* color = (appliedMultiSampleCount_ > 1 ? msaaSurface_ : colorSurface_).Get();
        owner_->BindRenderTargetSurfacesEXT(&color, 1, depthStencilSurface_.Get(),
                                            depthFormatOrdinal_, width_, height_,
                                            "binding RenderTarget2D");
    }

    void D3D9RenderTargetRenderer::ResolveForTransitionEXT()
    {
        if (appliedMultiSampleCount_ > 1 && msaaSurface_ && colorSurface_)
        {
            const HRESULT hr = device_->StretchRect(
                msaaSurface_.Get(), nullptr, colorSurface_.Get(), nullptr, D3DTEXF_NONE);
            if (FAILED(hr))
                throw std::runtime_error(
                    "D3D9RenderTargetRenderer::ResolveForTransitionEXT: StretchRect failed, hr=" +
                    FormatHr(hr));
        }
        RegenerateMipChainEXT();
    }

    void D3D9RenderTargetRenderer::RegenerateMipChainEXT()
    {
        if (levelCount_ <= 1 || !colorTexture_) return;
        DownsampleChain(device_.Get(), levelCount_,
                        [this](int level)
                        {
                            ComPtr<IDirect3DSurface9> surface;
                            const HRESULT hr = colorTexture_->GetSurfaceLevel(
                                static_cast<UINT>(level), surface.GetAddressOf());
                            if (FAILED(hr))
                                throw std::runtime_error(
                                    "D3D9RenderTargetRenderer: GetSurfaceLevel failed, hr=" +
                                    FormatHr(hr));
                            return surface;
                        },
                        "D3D9RenderTargetRenderer::RegenerateMipChainEXT");
    }

    void D3D9RenderTargetRenderer::UnbindAsRenderTarget()
    {
        ResolveForTransitionEXT();
        if (owner_) owner_->RestoreBackBufferRenderTargetEXT();
    }

    void D3D9RenderTargetRenderer::UploadLevel(int level, const uint8_t* data, int levelW,
                                              int levelH, int stride)
    {
        if (data == nullptr) return;
        if (level < 0)
            throw System::ArgumentOutOfRangeException(
                "level", std::to_string(level), "level must not be negative.");
        if (level >= levelCount_)
            throw System::NotSupportedException(
                "D3D9RenderTargetRenderer::UpdatePixelsLevel: this render target has " +
                std::to_string(levelCount_) + " mip level(s); level " + std::to_string(level) +
                " was requested.");
        const int w = LevelExtent(width_, level);
        const int h = LevelExtent(height_, level);
        if (levelW != w || levelH != h)
            throw std::invalid_argument(
                "D3D9RenderTargetRenderer::UpdatePixelsLevel: the upload does not match the "
                "level's dimensions.");
        const int rowBytes = w * 4;
        if (stride > 0 && stride < rowBytes)
            throw std::invalid_argument(
                "D3D9RenderTargetRenderer::UpdatePixelsLevel: stride is smaller than one row.");
        const int sourceStride = stride > 0 ? stride : rowBytes;
        if (!colorTexture_) Recreate();

        // A D3DUSAGE_RENDERTARGET surface in D3DPOOL_DEFAULT cannot be locked, so the upload goes
        // through a system-memory surface and UpdateSurface, the mirror image of GetData's
        // GetRenderTargetData readback. Storage is D3DFMT_A8B8G8R8, byte order R,G,B,A, rows
        // top-first, so the caller's bytes are copied row by row with no swizzle or flip.
        ComPtr<IDirect3DSurface9> staging;
        HRESULT hr = device_->CreateOffscreenPlainSurface(
            static_cast<UINT>(w), static_cast<UINT>(h), D3DFMT_A8B8G8R8, D3DPOOL_SYSTEMMEM,
            staging.GetAddressOf(), nullptr);
        if (FAILED(hr))
            throw std::runtime_error(
                "D3D9RenderTargetRenderer::UpdatePixels: CreateOffscreenPlainSurface failed, hr=" +
                FormatHr(hr));
        D3DLOCKED_RECT locked{};
        hr = staging->LockRect(&locked, nullptr, 0);
        if (FAILED(hr))
            throw std::runtime_error(
                "D3D9RenderTargetRenderer::UpdatePixels: LockRect failed, hr=" + FormatHr(hr));
        auto* dst = static_cast<std::uint8_t*>(locked.pBits);
        for (int row = 0; row < h; ++row)
            std::memcpy(dst + static_cast<std::size_t>(row) * locked.Pitch,
                        data + static_cast<std::size_t>(row) * sourceStride,
                        static_cast<std::size_t>(rowBytes));
        staging->UnlockRect();

        ComPtr<IDirect3DSurface9> target;
        hr = colorTexture_->GetSurfaceLevel(static_cast<UINT>(level), target.GetAddressOf());
        if (FAILED(hr))
            throw std::runtime_error(
                "D3D9RenderTargetRenderer::UpdatePixels: GetSurfaceLevel failed, hr=" + FormatHr(hr));
        hr = device_->UpdateSurface(staging.Get(), nullptr, target.Get(), nullptr);
        if (FAILED(hr))
            throw std::runtime_error(
                "D3D9RenderTargetRenderer::UpdatePixels: UpdateSurface failed, hr=" + FormatHr(hr));

        // A multisampled target renders into msaaSurface_ and resolves into colorSurface_ on
        // unbind; seed the multisampled surface too, so a draw that follows the upload composes
        // over the uploaded image instead of resolving stale samples over it.
        if (level == 0 && msaaSurface_)
        {
            hr = device_->StretchRect(colorSurface_.Get(), nullptr, msaaSurface_.Get(), nullptr,
                                      D3DTEXF_NONE);
            if (FAILED(hr))
                throw std::runtime_error(
                    "D3D9RenderTargetRenderer::UpdatePixels: StretchRect into the multisampled "
                    "surface failed, hr=" + FormatHr(hr));
        }
    }

    void D3D9RenderTargetRenderer::UpdatePixels(const uint8_t* data, int stride)
    {
        UploadLevel(0, data, width_, height_, stride);
    }

    void D3D9RenderTargetRenderer::UpdatePixelsLevel(int level, const uint8_t* data, int levelW,
                                                     int levelH)
    {
        UploadLevel(level, data, levelW, levelH, levelW * 4);
    }

    bool D3D9RenderTargetRenderer::GetData(int level, int x, int y, int w, int h,
                                          void* data, int dataLength) const
    {
        if (level < 0)
            throw System::ArgumentOutOfRangeException(
                "level", std::to_string(level), "level must not be negative.");
        // A level beyond the allocated chain is rejected rather than answered from level 0.
        if (level >= levelCount_)
            throw System::NotSupportedException(
                "D3D9RenderTargetRenderer::GetData: this render target has " +
                std::to_string(levelCount_) + " mip level(s); level " + std::to_string(level) +
                " was requested.");
        const int levelW = LevelExtent(width_, level);
        const int levelH = LevelExtent(height_, level);
        // 64-bit throughout, so a rectangle near INT_MAX is rejected rather than wrapping.
        const std::int64_t right = static_cast<std::int64_t>(x) + static_cast<std::int64_t>(w);
        const std::int64_t bottom = static_cast<std::int64_t>(y) + static_cast<std::int64_t>(h);
        if (x < 0 || y < 0 || w <= 0 || h <= 0 ||
            right > static_cast<std::int64_t>(levelW) || bottom > static_cast<std::int64_t>(levelH))
            throw System::ArgumentOutOfRangeException(
                "rect",
                std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(w) + "," +
                    std::to_string(h),
                "The requested rectangle leaves the " + std::to_string(levelW) + "x" +
                    std::to_string(levelH) + " render-target level.");
        const std::int64_t requiredBytes =
            static_cast<std::int64_t>(w) * static_cast<std::int64_t>(h) * 4;
        if (static_cast<std::int64_t>(dataLength) < requiredBytes)
            throw System::ArgumentOutOfRangeException(
                "dataLength", std::to_string(dataLength),
                "The destination holds fewer than the " + std::to_string(requiredBytes) +
                    " bytes the requested rectangle needs.");
        if (!device_ || !colorSurface_ || data == nullptr)
            return false;

        // Always the resolved, single-sample texture -- GetRenderTargetData rejects a multisampled
        // source, and UnbindAsRenderTarget has already StretchRect-resolved into it (and
        // regenerated the chain) when this target is MSAA or mipmapped.
        ComPtr<IDirect3DSurface9> levelSurface;
        HRESULT hr = colorTexture_->GetSurfaceLevel(static_cast<UINT>(level),
                                                    levelSurface.GetAddressOf());
        if (FAILED(hr)) return false;

        // A D3DUSAGE_RENDERTARGET surface in D3DPOOL_DEFAULT cannot be locked directly; D3D9's own
        // readback path is a copy into system memory, which also synchronises with the GPU.
        ComPtr<IDirect3DSurface9> staging;
        hr = device_->CreateOffscreenPlainSurface(
            static_cast<UINT>(levelW), static_cast<UINT>(levelH), D3DFMT_A8B8G8R8,
            D3DPOOL_SYSTEMMEM, staging.GetAddressOf(), nullptr);
        if (FAILED(hr)) return false;

        hr = device_->GetRenderTargetData(levelSurface.Get(), staging.Get());
        if (FAILED(hr)) return false;

        RECT rect{ static_cast<LONG>(x), static_cast<LONG>(y),
                   static_cast<LONG>(x + w), static_cast<LONG>(y + h) };
        D3DLOCKED_RECT locked{};
        hr = staging->LockRect(&locked, &rect, D3DLOCK_READONLY);
        if (FAILED(hr)) return false;

        auto* dst = static_cast<std::uint8_t*>(data);
        const auto* src = static_cast<const std::uint8_t*>(locked.pBits);
        const std::size_t rowBytes = static_cast<std::size_t>(w) * 4u;
        for (int row = 0; row < h; ++row)
            std::memcpy(dst + static_cast<std::size_t>(row) * rowBytes,
                        src + static_cast<std::size_t>(row) * locked.Pitch, rowBytes);

        staging->UnlockRect();
        return true;
    }

    void D3D9RenderTargetRenderer::ReleaseDefaultPoolResourceEXT()
    {
        colorSurface_.Reset();
        colorTexture_.Reset();
        msaaSurface_.Reset();
        depthStencilSurface_.Reset();
    }

    // -------------------------------------------------------------------------
    // D3D9RenderTargetCubeRenderer
    // -------------------------------------------------------------------------

    D3D9RenderTargetCubeRenderer::D3D9RenderTargetCubeRenderer(
        DirectX9Renderer& owner, IDirect3DDevice9* device, int size, int depthFormat, bool mipMap)
        : owner_(&owner), device_(device)
        , size_(size)
        , levelCount_(mipMap ? FullMipLevelCount(size, size) : 1)
        , depthFormatOrdinal_(depthFormat)
        , hasDepth_(DepthFormatToD3D9(depthFormat) != D3DFMT_UNKNOWN)
    {
        Recreate();
        owner_->RegisterDefaultPoolResourceEXT(this);
    }

    D3D9RenderTargetCubeRenderer::~D3D9RenderTargetCubeRenderer()
    {
        if (owner_) owner_->UnregisterDefaultPoolResourceEXT(this);
    }

    void D3D9RenderTargetCubeRenderer::Recreate()
    {
        HRESULT hr = device_->CreateCubeTexture(
            static_cast<UINT>(size_), static_cast<UINT>(levelCount_), D3DUSAGE_RENDERTARGET,
            D3DFMT_A8B8G8R8, D3DPOOL_DEFAULT, texture_.ReleaseAndGetAddressOf(), nullptr);
        if (FAILED(hr))
            throw std::runtime_error("D3D9RenderTargetCubeRenderer: CreateCubeTexture failed, hr=" + FormatHr(hr));

        depthStencilSurface_.Reset();
        if (hasDepth_)
        {
            const D3DFORMAT depthFmt = DepthFormatToD3D9(depthFormatOrdinal_);
            owner_->CreateDepthStencilSurfaceEXT(
                static_cast<UINT>(size_), static_cast<UINT>(size_), depthFmt, D3DMULTISAMPLE_NONE, 0, TRUE,
                depthStencilSurface_.ReleaseAndGetAddressOf(),
                "D3D9RenderTargetCubeRenderer depth allocation");
        }
    }

    void D3D9RenderTargetCubeRenderer::BindAsRenderTargetFace(int face)
    {
        if (face < 0 || face >= 6) return;
        if (!texture_) Recreate();

        // Switching faces without an intervening unbind still finishes the face just rendered.
        if (activeFace_ >= 0 && activeFace_ != face) RegenerateFaceMipChain(activeFace_);
        activeFace_ = face;
        ComPtr<IDirect3DSurface9> faceSurface;
        HRESULT hr = texture_->GetCubeMapSurface(static_cast<D3DCUBEMAP_FACES>(face), 0, faceSurface.GetAddressOf());
        if (FAILED(hr))
            throw std::runtime_error("D3D9RenderTargetCubeRenderer::BindAsRenderTargetFace: GetCubeMapSurface failed, hr=" + FormatHr(hr));

        IDirect3DSurface9* color = faceSurface.Get();
        owner_->BindRenderTargetSurfacesEXT(&color, 1, depthStencilSurface_.Get(),
                                            depthFormatOrdinal_, size_, size_,
                                            "binding RenderTargetCube face");
    }

    bool D3D9RenderTargetCubeRenderer::GetData(int face, int level, int x, int y, int w, int h,
                                              void* data, int dataLength) const
    {
        // Closes the refusal this class inherited from ITextureCubeRenderer's
        // `return false` default.
        if (!device_ || !texture_ || data == nullptr) return false;
        if (face < 0 || face >= 6 || w <= 0 || h <= 0) return false;
        // Only the allocated chain has storage to read.
        if (level < 0 || level >= levelCount_) return false;
        const int levelSize = LevelExtent(size_, level);
        if (x < 0 || y < 0 || x + w > levelSize || y + h > levelSize) return false;
        if (dataLength < w * h * 4) return false;

        ComPtr<IDirect3DSurface9> faceSurface;
        HRESULT hr = texture_->GetCubeMapSurface(static_cast<D3DCUBEMAP_FACES>(face),
                                                 static_cast<UINT>(level),
                                                 faceSurface.GetAddressOf());
        if (FAILED(hr)) return false;

        // Same reason as D3D9RenderTargetRenderer::GetData: a D3DUSAGE_RENDERTARGET surface in
        // D3DPOOL_DEFAULT cannot be locked, and GetRenderTargetData's copy into system memory is
        // also the synchronisation point.
        ComPtr<IDirect3DSurface9> staging;
        hr = device_->CreateOffscreenPlainSurface(
            static_cast<UINT>(levelSize), static_cast<UINT>(levelSize), D3DFMT_A8B8G8R8,
            D3DPOOL_SYSTEMMEM, staging.GetAddressOf(), nullptr);
        if (FAILED(hr)) return false;

        hr = device_->GetRenderTargetData(faceSurface.Get(), staging.Get());
        if (FAILED(hr)) return false;

        RECT rect{ static_cast<LONG>(x), static_cast<LONG>(y),
                   static_cast<LONG>(x + w), static_cast<LONG>(y + h) };
        D3DLOCKED_RECT locked{};
        hr = staging->LockRect(&locked, &rect, D3DLOCK_READONLY);
        if (FAILED(hr)) return false;

        auto* dst = static_cast<std::uint8_t*>(data);
        const auto* src = static_cast<const std::uint8_t*>(locked.pBits);
        const std::size_t rowBytes = static_cast<std::size_t>(w) * 4u;
        for (int row = 0; row < h; ++row)
            std::memcpy(dst + static_cast<std::size_t>(row) * rowBytes,
                        src + static_cast<std::size_t>(row) * locked.Pitch, rowBytes);

        staging->UnlockRect();
        return true;
    }

    void D3D9RenderTargetCubeRenderer::RegenerateFaceMipChain(int face)
    {
        if (levelCount_ <= 1 || !texture_ || face < 0 || face >= 6) return;
        DownsampleChain(device_.Get(), levelCount_,
                        [this, face](int level)
                        {
                            ComPtr<IDirect3DSurface9> surface;
                            const HRESULT hr = texture_->GetCubeMapSurface(
                                static_cast<D3DCUBEMAP_FACES>(face), static_cast<UINT>(level),
                                surface.GetAddressOf());
                            if (FAILED(hr))
                                throw std::runtime_error(
                                    "D3D9RenderTargetCubeRenderer: GetCubeMapSurface failed, hr=" +
                                    FormatHr(hr));
                            return surface;
                        },
                        "D3D9RenderTargetCubeRenderer::RegenerateFaceMipChain");
    }

    void D3D9RenderTargetCubeRenderer::ResolveForTransitionEXT()
    {
        RegenerateFaceMipChain(activeFace_);
    }

    void D3D9RenderTargetCubeRenderer::UnbindAsRenderTarget()
    {
        ResolveForTransitionEXT();
        activeFace_ = -1;
        if (owner_) owner_->RestoreBackBufferRenderTargetEXT();
    }

    void D3D9RenderTargetCubeRenderer::ReleaseDefaultPoolResourceEXT()
    {
        texture_.Reset();
        depthStencilSurface_.Reset();
    }
}
