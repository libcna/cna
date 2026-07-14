#pragma once

// plan_dx.md Phase DX13 (DX-117): real D3D12 offscreen render-target backends -- the real, public
// XNA-facing RenderTarget2D/RenderTargetCube path, replacing DX-116's test-only
// BindOffscreenColorTargetEXT() scaffolding as the thing games actually construct through
// GraphicsDevice::SetRenderTarget2D()/CreateRenderTarget2D(). Same explicit-resource-management
// discipline every other real D3D12 resource in this backend already established
// (D3D12Buffers.hpp/D3D12Textures.hpp/D3D12TextureCube.hpp): a DEFAULT-heap ID3D12Resource, real
// RTV(s)/DSV via the device's own descriptor heaps (DX-103), and registration with the shared
// D3D12ResourceStateTracker (DX-106).
//
// plan_dx.md DX-144: full mip-chain generation is now real. D3D12 has no single-call
// GenerateMips() equivalent the way D3D11 does; rather than a manual compute/pixel-shader mip
// cascade (real additional pipeline/shader infrastructure), this uses a synchronous CPU box-filter
// downsample cascade -- read a level back via a READBACK-heap CopyTextureRegion, box-filter it on
// the CPU, upload the result to the next level via an UPLOAD-heap CopyTextureRegion -- the same
// ExecuteCommandListAndWaitEXT-synchronous discipline this backend's own D3D12Textures.cpp/
// D3D12Buffers.cpp already establish for every other real upload/readback path. Triggered from
// UnbindAsRenderTarget(), mirroring D3D11RenderTargetBackend's own GenerateMips()-on-unbind timing.
//
// MSAA is still NOT included in this pass (honest, scoped follow-up, matching this whole session's
// established "prioritize the core case, document the gap" discipline -- see D3D11's own DX-45 for
// the precedent this backend doesn't yet mirror). GetMultiSampleCount() always reports 0 here.
//
// The depth-stencil resource+DSV this class creates (when depthFormat != DepthFormat::None) is
// real, but -- exactly like DX-116's own back-buffer depth-stencil buffer -- not yet wired into
// any OMSetRenderTargets call: every current draw path still hardcodes a null DSV
// (DX-107/DX-111's own documented depthEnable=false PSO simplification). Real depth-test support
// against a bound render target is DX-118's job, same as it is for the back buffer.

#include "../Common/IGraphicsBackend.hpp"

#include <d3d12.h>
#include <wrl/client.h>

namespace CNA::Internal::Backends::D3D12
{
    using Microsoft::WRL::ComPtr;

    class D3D12GraphicsBackend;

    /// Real D3D12 2D render-target backend (DX-117).
    class D3D12RenderTargetBackend final : public IRenderTargetBackend
    {
    public:
        D3D12RenderTargetBackend(D3D12GraphicsBackend* owner, ID3D12Device* device,
                                 int w, int h, int depthFormat, bool mipMap = false);

        [[nodiscard]] int GetWidth() const override { return width_; }
        [[nodiscard]] int GetHeight() const override { return height_; }
        [[nodiscard]] SDL_Texture* GetNativeTexture() const override { return nullptr; }

        void BindAsRenderTarget() override;
        void UnbindAsRenderTarget() override;
        [[nodiscard]] int GetMultiSampleCount() const override { return 0; }
        [[nodiscard]] bool HasRealDepthBuffer(bool depthFormatWasRequested) const override
        {
            return hasDepth_ && depthFormatWasRequested;
        }

        /// Real mip-chain level count this target was allocated with (1 when `mipMap` was false) --
        /// NOXNA, DX-144 subresource math/test introspection, mirrors D3D11RenderTargetBackend's
        /// own GetLevelCountEXT().
        [[nodiscard]] int GetLevelCountEXT() const { return levelCount_; }

        /// Real GPU-resident color resource (NOXNA -- SetRenderTargets()/tests).
        [[nodiscard]] ID3D12Resource* GetColorResourceEXT() const { return colorResource_.Get(); }
        /// Real RTV for this target's color attachment (NOXNA).
        [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE GetRtvEXT() const { return rtv_; }
        /// Shader-visible-heap GPU handle for this target's own color SRV, so it can be sampled
        /// like any other texture once unbound (NOXNA -- GetSrvGpuHandleForTextureEXT's own
        /// two-concrete-type resolution, mirroring D3D11GraphicsBackend::GetSrvForTextureEXT).
        [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE GetShaderResourceViewGpuHandleEXT() const { return srvGpu_; }
        /// Real GPU-resident depth-stencil resource, or null if `depthFormat` was `None`/unrecognized
        /// (NOXNA -- DX-145 real DXGI-format-fidelity introspection).
        [[nodiscard]] ID3D12Resource* GetDepthResourceEXT() const { return depthResource_.Get(); }
        /// DX-146: the real DSV/format this render target created for its own depth-stencil resource.
        /// DX-117 created these but never passed them to BindAsRenderTarget(), so binding a render
        /// target with a depth buffer silently gave draws NO depth buffer at all -- see this class's
        /// own BindAsRenderTarget() for the fix.
        [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE GetDsvEXT() const { return dsv_; }
        [[nodiscard]] DXGI_FORMAT GetDsvFormatEXT() const { return dsvFormat_; }

    private:
        /// DX-144: CPU box-filter downsample cascade, base level (0) -> levelCount_-1, called from
        /// UnbindAsRenderTarget(). No-op when mipMap_ is false or levelCount_ is 1.
        void GenerateMipsEXT();

        D3D12GraphicsBackend* owner_ = nullptr;
        ComPtr<ID3D12Device> device_;

        ComPtr<ID3D12Resource> colorResource_;
        D3D12_CPU_DESCRIPTOR_HANDLE rtv_{};
        D3D12_CPU_DESCRIPTOR_HANDLE srvCpu_{};
        D3D12_GPU_DESCRIPTOR_HANDLE srvGpu_{};

        ComPtr<ID3D12Resource> depthResource_;
        D3D12_CPU_DESCRIPTOR_HANDLE dsv_{};
        DXGI_FORMAT dsvFormat_ = DXGI_FORMAT_UNKNOWN; // DX-146
        bool hasDepth_ = false;

        int width_ = 0;
        int height_ = 0;
        bool mipMap_ = false;
        int levelCount_ = 1;
    };

    /// Real D3D12 cube-map render-target backend (DX-117). One shared 6-slice texture array (same
    /// shape as D3D12TextureCubeBackend, DX-111) with one RTV per face
    /// (D3D12_RTV_DIMENSION_TEXTURE2DARRAY, FirstArraySlice=face) and a single shared depth-stencil
    /// buffer reused across faces (only one face is ever the active draw target at a time, matching
    /// D3D11RenderTargetCubeBackend's identical one-depth-buffer-per-cube convention).
    class D3D12RenderTargetCubeBackend final : public IRenderTargetCubeBackend
    {
    public:
        D3D12RenderTargetCubeBackend(D3D12GraphicsBackend* owner, ID3D12Device* device,
                                     int size, int depthFormat, bool mipMap = false);

        [[nodiscard]] int GetSize() const override { return size_; }
        void BindAsRenderTargetFace(int face) override;
        void UnbindAsRenderTarget() override;
        [[nodiscard]] int GetMultiSampleCount() const override { return 0; }

        [[nodiscard]] ID3D12Resource* GetColorResourceEXT() const { return colorResource_.Get(); }
        [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE GetShaderResourceViewGpuHandleEXT() const { return srvGpu_; }
        /// Real mip-chain level count this cube target was allocated with (1 when `mipMap` was
        /// false) -- NOXNA, DX-144 subresource math/test introspection. Face `f`'s mip level `m` is
        /// subresource `m + f * GetLevelCountEXT()` (the standard D3D12 texture-array/mip
        /// subresource-index convention, same as `D3D12TextureCubeBackend`'s own doc comment).
        [[nodiscard]] int GetLevelCountEXT() const { return levelCount_; }

    private:
        /// DX-144: CPU box-filter downsample cascade, per face, base level (0) -> levelCount_-1,
        /// called from UnbindAsRenderTarget(). No-op when mipMap_ is false or levelCount_ is 1.
        void GenerateMipsEXT();

        D3D12GraphicsBackend* owner_ = nullptr;
        ComPtr<ID3D12Device> device_;

        ComPtr<ID3D12Resource> colorResource_;
        D3D12_CPU_DESCRIPTOR_HANDLE rtv_[6]{};
        D3D12_CPU_DESCRIPTOR_HANDLE srvCpu_{};
        D3D12_GPU_DESCRIPTOR_HANDLE srvGpu_{};

        ComPtr<ID3D12Resource> depthResource_;
        D3D12_CPU_DESCRIPTOR_HANDLE dsv_{};
        bool hasDepth_ = false;

        int size_ = 0;
        int activeFace_ = -1;
        bool mipMap_ = false;
        int levelCount_ = 1;
    };
}
