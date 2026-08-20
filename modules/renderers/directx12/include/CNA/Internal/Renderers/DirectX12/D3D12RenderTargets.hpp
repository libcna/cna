#pragma once

// plans/plan_dx.md Phase DX13 (DX-117): real D3D12 offscreen render-target renderers -- the real, public
// XNA-facing RenderTarget2D/RenderTargetCube path, replacing DX-116's test-only
// BindOffscreenColorTargetEXT() scaffolding as the thing games actually construct through
// GraphicsDevice::SetRenderTarget2D()/CreateRenderTarget2D(). Same explicit-resource-management
// discipline every other real D3D12 resource in this renderer already established
// (D3D12Buffers.hpp/D3D12Textures.hpp/D3D12TextureCube.hpp): a DEFAULT-heap ID3D12Resource, real
// RTV(s)/DSV via the device's own descriptor heaps (DX-103), and registration with the shared
// D3D12ResourceStateTracker (DX-106).
//
// plans/plan_dx.md DX-144: full mip-chain generation is now real. D3D12 has no single-call
// GenerateMips() equivalent the way D3D11 does; rather than a manual compute/pixel-shader mip
// cascade (real additional pipeline/shader infrastructure), this uses a synchronous CPU box-filter
// downsample cascade -- read a level back via a READBACK-heap CopyTextureRegion, box-filter it on
// the CPU, upload the result to the next level via an UPLOAD-heap CopyTextureRegion -- the same
// ExecuteCommandListAndWaitEXT-synchronous discipline this renderer's own D3D12Textures.cpp/
// D3D12Buffers.cpp already establish for every other real upload/readback path. Triggered from
// UnbindAsRenderTarget(), mirroring D3D11RenderTargetRenderer's own GenerateMips()-on-unbind timing.
//
// plans/plan_dx.md DX-117 MSAA follow-up: real, device-queried MSAA is now supported for
// D3D12RenderTargetRenderer (2D), mirroring D3D11RenderTargetRenderer's own DX-45 design exactly --
// never assumes a requested sample count is supported (ID3D12Device::CheckFeatureSupport with
// D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS), MSAA and a full mip chain are mutually exclusive on
// the same attachment (same rationale D3D11 already established), and the MSAA color resource is
// never sampled directly -- ResolveSubresource() into a separate single-sample resource on
// UnbindAsRenderTarget(), the D3D12-command-list equivalent of D3D11's own
// ID3D11DeviceContext::ResolveSubresource() call (D3D12's own version additionally needs explicit
// RESOLVE_SOURCE/RESOLVE_DEST resource-state transitions, which D3D11 doesn't).
//
// plans/plan_dx.md DX-152: MSAA is now also real for D3D12RenderTargetCubeRenderer (reopening what was
// originally a deliberate, matched scope exclusion on both D3D renderers), same design as the 2D
// leg above -- D3D12_SRV_DIMENSION_TEXTURECUBE has no multisampled variant (same restriction that
// keeps D3D11's own TextureCube SRV from ever being MSAA), so colorResource_ becomes the
// MSAA-or-not RTV-only target and a separate resolveResource_ (always single-sample) is the real
// TextureCube SRV source, ResolveSubresource()'d from ONLY the currently-active face on unbind --
// matching this class's own pre-existing "only one face is ever the active draw target at a time"
// convention that GenerateMipsEXT() already established.
//
// The depth-stencil resource+DSV this class creates (when depthFormat != DepthFormat::None) is
// real, but -- exactly like DX-116's own back-buffer depth-stencil buffer -- not yet wired into
// any OMSetRenderTargets call: every current draw path still hardcodes a null DSV
// (DX-107/DX-111's own documented depthEnable=false PSO simplification). Real depth-test support
// against a bound render target is DX-118's job, same as it is for the back buffer.

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "D3D12DescriptorHeaps.hpp"

#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>
#include <memory>

namespace CNA::Internal::Renderers::DirectX12
{
    using Microsoft::WRL::ComPtr;

    class DirectX12Renderer;

    /// Real D3D12 2D render-target renderer (DX-117).
    class D3D12RenderTargetRenderer final : public IRenderTargetRenderer
    {
    public:
        D3D12RenderTargetRenderer(DirectX12Renderer* owner, ID3D12Device* device,
                                 int w, int h, int depthFormat, bool mipMap = false,
                                 int multiSampleCount = 0);
        /// REMED-GFX-177: returns this target's SRV slot, RTV and (when it has depth) DSV to their
        /// allocators, which reissue them once the GPU has passed the fence value current at
        /// destruction. Before this, all three were consumed for the lifetime of the process.
        ~D3D12RenderTargetRenderer() override;

        [[nodiscard]] int GetWidth() const override { return width_; }
        [[nodiscard]] int GetHeight() const override { return height_; }

        void BindAsRenderTarget() override;
        void UnbindAsRenderTarget() override;

        /**
         * @brief Reads this target's rendered pixels back as tightly packed RGBA8 rows.
         *
         * REMED-GFX-127. Reuses the READBACK-heap `CopyTextureRegion` + fence-wait + `Map`
         * discipline DX-144's own mip cascade already established in this file, against the
         * SAMPLEABLE colour resource -- the resolved single-sample copy when this target is MSAA,
         * since a multisampled resource cannot be copied to a readback buffer. The requested
         * rectangle is extracted from the read level on the CPU. Before this override existed the
         * shared layer converted its own zero-initialized scratch buffer for the caller, i.e. a
         * fabricated transparent-black frame.
         *
         * The readback buffer is created and released inside this call, so repeated readbacks hold
         * no extra GPU memory; the fence wait is the one D3D12 genuinely requires for a synchronous
         * read and is confined to this call. Storage is DXGI_FORMAT_R8G8B8A8_UNORM, so no swizzle
         * applies; rows are top-first, so no flip applies, and the placed footprint's RowPitch is
         * honoured rather than assuming tightly packed rows.
         *
         * @param level      Mip level; this target has a full chain only when it was created with
         *                   mipMap.
         * @param x          Left edge of the requested rectangle, in level pixels.
         * @param y          Top edge of the requested rectangle, in level pixels.
         * @param w          Width of the requested rectangle, in pixels.
         * @param h          Height of the requested rectangle, in pixels.
         * @param data       Destination for @p w * @p h tightly packed RGBA8 pixels.
         * @param dataLength Capacity of @p data in bytes.
         * @return True once the whole rectangle has been written; false if D3D12 could not complete
         *         the readback, leaving @p data untouched.
         * @throws System::NotSupportedException if this target has no such mip level.
         * @throws System::ArgumentOutOfRangeException if @p level is negative, the rectangle leaves
         *         the level, or @p dataLength is too small for the rectangle.
         */
        [[nodiscard]] bool GetData(int level, int x, int y, int w, int h,
                                   void* data, int dataLength) const override;

        [[nodiscard]] int GetMultiSampleCount() const override { return appliedMultiSampleCount_; }
        [[nodiscard]] bool HasRealDepthBuffer(bool depthFormatWasRequested) const override
        {
            return hasDepth_ && depthFormatWasRequested;
        }

        /// Real mip-chain level count this target was allocated with (1 when `mipMap` was false) --
        /// CNAEXT, DX-144 subresource math/test introspection, mirrors D3D11RenderTargetRenderer's
        /// own GetLevelCountEXT().
        [[nodiscard]] int GetLevelCountEXT() const { return levelCount_; }

        /// Real GPU-resident color resource actually bound for rendering (CNAEXT --
        /// SetRenderTargets()/tests) -- the MSAA resource itself when `GetMultiSampleCount() > 0`
        /// (matches what the real RTV/OMSetRenderTargets binding points at); use
        /// `GetSampleableColorResourceEXT()` instead for readback/sampling, which is always the
        /// resolved single-sample resource.
        [[nodiscard]] ID3D12Resource* GetColorResourceEXT() const { return colorResource_.Get(); }
        /// The resource tests/shaders should actually read from -- `resolveResource_` when MSAA
        /// (post-`ResolveSubresource()`, only valid after `UnbindAsRenderTarget()` has run at least
        /// once), else the same object `GetColorResourceEXT()` already returns (CNAEXT, mirrors
        /// D3D11RenderTargetRenderer's own `GetSampleableTextureEXT()` naming/behavior exactly).
        [[nodiscard]] ID3D12Resource* GetSampleableColorResourceEXT() const
        {
            return isMsaa_ ? resolveResource_.Get() : colorResource_.Get();
        }
        /// Real RTV for this target's color attachment (CNAEXT).
        [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE GetRtvEXT() const { return rtv_; }
        /// Shader-visible-heap GPU handle for this target's own color SRV, so it can be sampled
        /// like any other texture once unbound (CNAEXT -- GetSrvGpuHandleForTextureEXT's own
        /// two-concrete-type resolution, mirroring DirectX11Renderer::GetSrvForTextureEXT).
        /// REMED-GFX-177: resolved on every call against whichever heap is current, never cached.
        [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE GetShaderResourceViewGpuHandleEXT() const
        {
            return heaps_ ? heaps_->cbvSrvUav.GpuHandle(srvIndex_) : D3D12_GPU_DESCRIPTOR_HANDLE{};
        }
        /// REMED-GFX-177: the stable shader-visible-heap slot index this target owns (CNAEXT).
        [[nodiscard]] std::uint32_t GetShaderResourceViewIndexEXT() const { return srvIndex_; }
        /// Real GPU-resident depth-stencil resource, or null if `depthFormat` was `None`/unrecognized
        /// (CNAEXT -- DX-145 real DXGI-format-fidelity introspection).
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
        /// DX-117 MSAA follow-up: ResolveSubresource() the MSAA color resource into
        /// resolveResource_, called from UnbindAsRenderTarget() before GenerateMipsEXT(). No-op
        /// when isMsaa_ is false.
        void ResolveMsaaEXT();

        DirectX12Renderer* owner_ = nullptr;
        ComPtr<ID3D12Device> device_;

        /// Kept alive independently of owner_ so the destructor can always free the descriptors.
        std::shared_ptr<D3D12DescriptorHeaps> heaps_;

        ComPtr<ID3D12Resource> colorResource_;
        D3D12_CPU_DESCRIPTOR_HANDLE rtv_{};
        std::uint32_t srvIndex_ = D3D12ShaderVisibleDescriptorAllocator::kInvalidIndex;
        /// MSAA follow-up: the separate single-sample resource `ResolveSubresource()` writes into
        /// on unbind. Only allocated/valid when `isMsaa_`.
        ComPtr<ID3D12Resource> resolveResource_;

        ComPtr<ID3D12Resource> depthResource_;
        D3D12_CPU_DESCRIPTOR_HANDLE dsv_{};
        DXGI_FORMAT dsvFormat_ = DXGI_FORMAT_UNKNOWN; // DX-146
        bool hasDepth_ = false;

        int width_ = 0;
        int height_ = 0;
        bool mipMap_ = false;
        int levelCount_ = 1;
        bool isMsaa_ = false;
        int appliedMultiSampleCount_ = 0;
    };

    /// Real D3D12 cube-map render-target renderer (DX-117). One shared 6-slice texture array (same
    /// shape as D3D12TextureCubeRenderer, DX-111) with one RTV per face
    /// (D3D12_RTV_DIMENSION_TEXTURE2DARRAY, FirstArraySlice=face) and a single shared depth-stencil
    /// buffer reused across faces (only one face is ever the active draw target at a time, matching
    /// D3D11RenderTargetCubeRenderer's identical one-depth-buffer-per-cube convention).
    class D3D12RenderTargetCubeRenderer final : public IRenderTargetCubeRenderer
    {
    public:
        D3D12RenderTargetCubeRenderer(DirectX12Renderer* owner, ID3D12Device* device,
                                     int size, int depthFormat, bool mipMap = false,
                                     int multiSampleCount = 0);
        /// REMED-GFX-177: returns this cube target's SRV slot, its SIX per-face RTVs and (when it
        /// has depth) its DSV to their allocators. A cube target is the largest single descriptor
        /// consumer in this renderer, so it is also the one a fixed bump allocator exhausted fastest.
        ~D3D12RenderTargetCubeRenderer() override;

        [[nodiscard]] int GetSize() const override { return size_; }
        void BindAsRenderTargetFace(int face) override;
        void UnbindAsRenderTarget() override;
        [[nodiscard]] int GetMultiSampleCount() const override { return appliedMultiSampleCount_; }

        /// The resource actually bound for rendering (CNAEXT) -- the MSAA array itself when
        /// `GetMultiSampleCount() > 0`; use `GetSampleableColorResourceEXT()` instead for
        /// readback/sampling, which is always the resolved single-sample resource.
        [[nodiscard]] ID3D12Resource* GetColorResourceEXT() const { return colorResource_.Get(); }
        /// The resource tests/shaders should actually read from -- `resolveResource_` when MSAA
        /// (post-`ResolveSubresource()`, only valid for the active face after
        /// `UnbindAsRenderTarget()` has run at least once), else the same object
        /// `GetColorResourceEXT()` already returns (CNAEXT, mirrors `D3D12RenderTargetRenderer`'s own
        /// `GetSampleableColorResourceEXT()` naming/behavior exactly).
        [[nodiscard]] ID3D12Resource* GetSampleableColorResourceEXT() const
        {
            return isMsaa_ ? resolveResource_.Get() : colorResource_.Get();
        }
        /// REMED-GFX-177: resolved on every call against whichever heap is current, never cached.
        [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE GetShaderResourceViewGpuHandleEXT() const
        {
            return heaps_ ? heaps_->cbvSrvUav.GpuHandle(srvIndex_) : D3D12_GPU_DESCRIPTOR_HANDLE{};
        }
        /// REMED-GFX-177: the stable shader-visible-heap slot index this cube target owns (CNAEXT).
        [[nodiscard]] std::uint32_t GetShaderResourceViewIndexEXT() const { return srvIndex_; }
        /// Real mip-chain level count this cube target was allocated with (1 when `mipMap` was
        /// false) -- CNAEXT, DX-144 subresource math/test introspection. Face `f`'s mip level `m` is
        /// subresource `m + f * GetLevelCountEXT()` (the standard D3D12 texture-array/mip
        /// subresource-index convention, same as `D3D12TextureCubeRenderer`'s own doc comment).
        [[nodiscard]] int GetLevelCountEXT() const { return levelCount_; }

        /**
         * @brief Reads a RENDERED cube face's mip level back to the CPU.
         *
         * REMED-GFX-134. `D3D12TextureCubeRenderer::GetData`'s mechanism -- a `D3D12_HEAP_TYPE_READBACK`
         * buffer, a placed-footprint `CopyTextureRegion` from the face/level subresource with the
         * required `D3D12_TEXTURE_DATA_PITCH_ALIGNMENT` row pitch, and the shared
         * `ExecuteCommandListAndWaitEXT` fence -- sourced from `GetSampleableColorResourceEXT()` so a
         * multisampled target is read through the single-sample resource `UnbindAsRenderTarget`'s
         * `ResolveSubresource()` already filled.
         *
         * The resource's tracked state is restored afterwards: this is a read, and leaving it in
         * COPY_SOURCE would desynchronise the shared state tracker for the next render pass.
         *
         * No row flip and no channel swizzle: D3D12's render-target origin is top-left and this
         * resource is `DXGI_FORMAT_R8G8B8A8_UNORM`.
         *
         * @param face       Cube face index (0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z).
         * @param level      Mip level to read.
         * @param x          Left edge of the requested region, in texels.
         * @param y          Top edge of the requested region, in texels.
         * @param w          Width of the requested region, in texels.
         * @param h          Height of the requested region, in texels.
         * @param data       Destination for tightly packed RGBA8 rows, top row first.
         * @param dataLength Size of @p data in bytes; at least w * h * 4.
         * @return True once the whole region was written; false for an out-of-range
         *         face/level/region, or a readback buffer this device refused to create or map.
         */
        [[nodiscard]] bool GetData(int face, int level, int x, int y, int w, int h,
                                   void* data, int dataLength) const override;

    private:
        /// DX-144: CPU box-filter downsample cascade, per face, base level (0) -> levelCount_-1,
        /// called from UnbindAsRenderTarget(). No-op when mipMap_ is false or levelCount_ is 1.
        void GenerateMipsEXT();
        /// DX-152: ResolveSubresource() the currently-active face from the MSAA color array into
        /// resolveResource_, called from UnbindAsRenderTarget() before GenerateMipsEXT(). No-op
        /// when isMsaa_ is false or no face is currently active.
        void ResolveMsaaEXT();

        DirectX12Renderer* owner_ = nullptr;
        ComPtr<ID3D12Device> device_;

        /// Kept alive independently of owner_ so the destructor can always free the descriptors.
        std::shared_ptr<D3D12DescriptorHeaps> heaps_;

        ComPtr<ID3D12Resource> colorResource_;
        D3D12_CPU_DESCRIPTOR_HANDLE rtv_[6]{};
        std::uint32_t srvIndex_ = D3D12ShaderVisibleDescriptorAllocator::kInvalidIndex;
        /// DX-152: the separate single-sample resource ResolveSubresource() writes into on unbind.
        /// Only allocated/valid when isMsaa_.
        ComPtr<ID3D12Resource> resolveResource_;

        ComPtr<ID3D12Resource> depthResource_;
        D3D12_CPU_DESCRIPTOR_HANDLE dsv_{};
        bool hasDepth_ = false;

        int size_ = 0;
        int activeFace_ = -1;
        bool mipMap_ = false;
        int levelCount_ = 1;
        bool isMsaa_ = false;
        int appliedMultiSampleCount_ = 0;
    };
}
