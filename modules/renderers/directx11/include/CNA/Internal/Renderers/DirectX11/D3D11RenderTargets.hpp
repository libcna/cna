#pragma once

// plans/plan_dx.md Phase DIRECTX6 (DX-43/DX-45): real D3D11 offscreen render-target renderers.
//
// Both classes hold a non-owning DirectX11Renderer* (owner_) -- unlike D3D11Buffers/Textures,
// which are fully self-contained via their own ComPtr<ID3D11Device/DeviceContext>, a render
// target's Unbind must be able to restore the *owner's* back-buffer RTV/DSV/viewport, which only
// the owning renderer instance knows how to do (mirrors VulkanRenderTargetRenderer's identical
// owner_ pattern, for the identical reason).

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"

#include <d3d11.h>
#include <wrl/client.h>

namespace CNA::Internal::Renderers::DirectX11
{
    using Microsoft::WRL::ComPtr;

    class DirectX11Renderer;

    /// Real D3D11 2D render-target renderer (DX-43). Optional depth-stencil (DX-11-fmt's
    /// DepthFormatToDxgi table; None omits the attachment entirely, same as EasyGL/Bgfx --
    /// HasRealDepthBuffer()'s IRenderTargetRenderer default already reflects this honestly).
    /// Optional MSAA (DX-45): when the device reports support for the requested sample count, the
    /// color attachment is allocated MSAA and resolved into a separate non-MSAA texture (the one
    /// actually sampled/read back) on UnbindAsRenderTarget(), since flip-model swap chains and
    /// MSAA render-target SRVs cannot be sampled directly. Optional full mip chain (D3D11's own
    /// D3D11_RESOURCE_MISC_GENERATE_MIPS + ID3D11DeviceContext::GenerateMips(), regenerated on
    /// every UnbindAsRenderTarget() call -- the D3D11 equivalent of EasyGL's glGenerateMipmap-on-
    /// unbind / FNA3D's OPENGL_ResolveTarget, and much simpler than Vulkan's manual per-level
    /// vkCmdBlitImage cascade since D3D11 exposes this as one built-in device call).
    class D3D11RenderTargetRenderer final : public IRenderTargetRenderer
    {
    public:
        D3D11RenderTargetRenderer(DirectX11Renderer* owner, ID3D11Device* device, ID3D11DeviceContext* context,
                                 int w, int h, int depthFormat, bool mipMap, int multiSampleCount);

        [[nodiscard]] int GetWidth() const override { return width_; }
        [[nodiscard]] int GetHeight() const override { return height_; }

        void BindAsRenderTarget() override;
        void UnbindAsRenderTarget() override;

        /**
         * @brief Reads this target's rendered pixels back as tightly packed RGBA8 rows.
         *
         * REMED-GFX-127. D3D11's readback is the same staging-copy discipline
         * DirectX11Renderer::ReadBackbuffer already uses: copy the requested subresource region
         * into a D3D11_USAGE_STAGING texture with D3D11_CPU_ACCESS_READ, then Map it for reading --
         * a Map that blocks until the copy has completed, so no separate synchronisation, fence or
         * extra frame is involved. The source is always GetSampleableTextureEXT(), i.e. the
         * resolved copy when this target is MSAA, since a multisampled texture cannot be copied to
         * a staging resource region. Before this override existed the shared layer converted its
         * own zero-initialized scratch buffer for the caller, i.e. a fabricated transparent-black
         * frame.
         *
         * The staging texture is created and released inside this call, so repeated readbacks hold
         * no extra GPU memory. Storage is DXGI_FORMAT_R8G8B8A8_UNORM, so no swizzle applies; rows
         * are top-first, so no flip applies, and D3D11_MAPPED_SUBRESOURCE::RowPitch is honoured
         * rather than assuming tightly packed rows.
         *
         * @param level      Mip level; this target has a full chain only when it was created with
         *                   mipMap.
         * @param x          Left edge of the requested rectangle, in level pixels.
         * @param y          Top edge of the requested rectangle, in level pixels.
         * @param w          Width of the requested rectangle, in pixels.
         * @param h          Height of the requested rectangle, in pixels.
         * @param data       Destination for @p w * @p h tightly packed RGBA8 pixels.
         * @param dataLength Capacity of @p data in bytes.
         * @return True once the whole rectangle has been written; false if D3D11 could not create
         *         or map the staging texture, leaving @p data untouched.
         * @throws System::NotSupportedException if this target has no such mip level.
         * @throws System::ArgumentOutOfRangeException if @p level is negative, the rectangle leaves
         *         the level, or @p dataLength is too small for the rectangle.
         */
        [[nodiscard]] bool GetData(int level, int x, int y, int w, int h,
                                   void* data, int dataLength) const override;

        [[nodiscard]] int GetMultiSampleCount() const override { return appliedMultiSampleCount_; }

        /// DX-143: the real MSAA-resolve/mip-regeneration work UnbindAsRenderTarget() does,
        /// extracted so DirectX11Renderer::SetRenderTargets()'s MRT (N>1) path can finalize
        /// each bound target individually when the MRT set itself is replaced/unbound, without
        /// also triggering UnbindAsRenderTarget()'s own back-buffer-restore side effect (MRT's own
        /// caller already handles that once, not per-target). UnbindAsRenderTarget() itself now
        /// just calls this plus the restore, so single-target behavior is unchanged. CNAEXT.
        void ResolveAndGenerateMipsEXT();

        /// Real ID3D11RenderTargetView for this target's color attachment (CNAEXT).
        [[nodiscard]] ID3D11RenderTargetView* GetRTVEXT() const { return rtv_.Get(); }
        /// Real ID3D11DepthStencilView, or null if depthFormat was None/unrecognized (CNAEXT).
        [[nodiscard]] ID3D11DepthStencilView* GetDSVEXT() const { return dsv_.Get(); }
        /// SRV of the sampleable (non-MSAA, post-resolve if MSAA) color texture (CNAEXT, Phase DIRECTX8).
        [[nodiscard]] ID3D11ShaderResourceView* GetShaderResourceViewEXT() const { return srv_.Get(); }
        [[nodiscard]] bool IsMsaaEXT() const { return isMsaa_; }
        /// The texture srv_ actually points at -- resolveTexture_ when isMsaa_, else colorTexture_
        /// itself (CNAEXT, test/diagnostics readback).
        [[nodiscard]] ID3D11Texture2D* GetSampleableTextureEXT() const
        {
            return isMsaa_ ? resolveTexture_.Get() : colorTexture_.Get();
        }
        /// Real mip-chain level count (1 when `mipMap` was false) -- CNAEXT, DX-144 subresource math.
        [[nodiscard]] int GetLevelCountEXT() const { return levelCount_; }

    private:
        DirectX11Renderer* owner_ = nullptr;
        ComPtr<ID3D11Device> device_;
        ComPtr<ID3D11DeviceContext> context_;

        ComPtr<ID3D11Texture2D> colorTexture_;      // MSAA texture when isMsaa_, else the sampleable texture itself.
        ComPtr<ID3D11RenderTargetView> rtv_;
        ComPtr<ID3D11Texture2D> resolveTexture_;    // Only allocated when isMsaa_ -- the sampleable, resolved copy.
        ComPtr<ID3D11ShaderResourceView> srv_;      // Points at resolveTexture_ when isMsaa_, else colorTexture_.
        ComPtr<ID3D11Texture2D> depthTexture_;
        ComPtr<ID3D11DepthStencilView> dsv_;

        int width_ = 0;
        int height_ = 0;
        bool mipMap_ = false;
        int levelCount_ = 1;
        bool isMsaa_ = false;
        int appliedMultiSampleCount_ = 0;
    };

    /// Real D3D11 cube-map render-target renderer (DX-43). One shared 6-slice texture array (same
    /// D3D11_RESOURCE_MISC_TEXTURECUBE shape as D3D11TextureCubeRenderer) with one RTV per face
    /// (D3D11_RTV_DIMENSION_TEXTURE2DARRAY, FirstArraySlice=face) and a single shared depth-
    /// stencil buffer reused across faces (only one face is ever the active draw target at a
    /// time, matching EasyGLRenderTargetCubeRenderer's identical one-depth-buffer-per-cube
    /// convention).
    ///
    /// DX-152: real, device-queried MSAA is now supported, mirroring D3D11RenderTargetRenderer's
    /// own DX-45 design -- never assumes a requested sample count is supported
    /// (ID3D11Device::CheckMultisampleQualityLevels), MSAA and a full mip chain are mutually
    /// exclusive on the same attachment (same rationale DX-45 already established). D3D11 cannot
    /// combine D3D11_RESOURCE_MISC_TEXTURECUBE with SampleDesc.Count > 1 on one resource (a
    /// TextureCube SRV can never be multisampled), so when MSAA is active, `texture_` becomes a
    /// PLAIN (non-cube) 6-slice Texture2DMSArray used ONLY as an RTV target -- never sampled
    /// directly, same "MSAA resource is render-target-only" rule DX-45 already established -- and
    /// a separate `resolveTexture_` (real D3D11_RESOURCE_MISC_TEXTURECUBE, single-sample) is
    /// ResolveSubresource()'d from it on UnbindAsRenderTarget(), only for the currently-active
    /// face (matching this class's own existing "only one face is ever active" mip-regen
    /// convention).
    class D3D11RenderTargetCubeRenderer final : public IRenderTargetCubeRenderer
    {
    public:
        D3D11RenderTargetCubeRenderer(DirectX11Renderer* owner, ID3D11Device* device, ID3D11DeviceContext* context,
                                     int size, int depthFormat, bool mipMap, int multiSampleCount = 0);

        [[nodiscard]] int GetSize() const override { return size_; }
        void BindAsRenderTargetFace(int face) override;
        void UnbindAsRenderTarget() override;
        [[nodiscard]] int GetMultiSampleCount() const override { return appliedMultiSampleCount_; }

        [[nodiscard]] ID3D11ShaderResourceView* GetShaderResourceViewEXT() const { return srv_.Get(); }
        /// The underlying 6-slice texture-array resource actually bound for rendering (CNAEXT --
        /// test/diagnostics) -- the MSAA array itself when `GetMultiSampleCount() > 0` (matches
        /// what the RTV binding points at); use `GetSampleableTextureEXT()` instead for
        /// readback/sampling/mip-subresource math, which is always the resolved single-sample
        /// resource. DX-144 subresource convention (when non-MSAA): face `f`'s mip level `m` is
        /// subresource `m + f * GetLevelCountEXT()`.
        [[nodiscard]] ID3D11Texture2D* GetColorTextureEXT() const { return texture_.Get(); }
        /// The resource tests/shaders should actually read from -- `resolveTexture_` when MSAA
        /// (post-`ResolveSubresource()`, only valid for the active face after
        /// `UnbindAsRenderTarget()` has run at least once), else the same object
        /// `GetColorTextureEXT()` already returns (CNAEXT, mirrors D3D11RenderTargetRenderer's own
        /// `GetSampleableTextureEXT()` naming/behavior exactly).
        [[nodiscard]] ID3D11Texture2D* GetSampleableTextureEXT() const
        {
            return isMsaa_ ? resolveTexture_.Get() : texture_.Get();
        }
        [[nodiscard]] bool IsMsaaEXT() const { return isMsaa_; }
        /// Real mip-chain level count (1 when `mipMap` was false) -- CNAEXT, DX-144 subresource math.
        [[nodiscard]] int GetLevelCountEXT() const { return levelCount_; }

        /**
         * @brief Reads a RENDERED cube face's mip level back to the CPU.
         *
         * REMED-GFX-134. `D3D11TextureCubeRenderer::GetData`'s mechanism -- a STAGING copy of the
         * whole resource followed by `Map` on the requested face/level subresource -- sourced from
         * `GetSampleableTextureEXT()` so a multisampled target is read through the single-sample
         * resource `UnbindAsRenderTarget`'s `ResolveSubresource()` already filled, never through
         * the raw multisample array (which `CopyResource` cannot stage anyway).
         *
         * No row flip and no channel swizzle: D3D11's render-target origin is top-left and this
         * resource is `DXGI_FORMAT_R8G8B8A8_UNORM`, so a rendered face is already stored exactly
         * as the public contract wants it.
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
         *         face/level/region, or a staging texture this device refused to create or map.
         */
        [[nodiscard]] bool GetData(int face, int level, int x, int y, int w, int h,
                                   void* data, int dataLength) const override;

    private:
        /// DX-152: ResolveSubresource() the active face from the MSAA color array into
        /// `resolveTexture_`, called from UnbindAsRenderTarget() before mip regeneration. No-op
        /// when `isMsaa_` is false or no face is currently active.
        void ResolveMsaaEXT();

        DirectX11Renderer* owner_ = nullptr;
        ComPtr<ID3D11Device> device_;
        ComPtr<ID3D11DeviceContext> context_;

        ComPtr<ID3D11Texture2D> texture_;
        ComPtr<ID3D11RenderTargetView> rtv_[6];
        ComPtr<ID3D11ShaderResourceView> srv_;
        /// DX-152: the separate, single-sample, real D3D11_RESOURCE_MISC_TEXTURECUBE resource
        /// ResolveSubresource() writes into on unbind. Only allocated/valid when `isMsaa_`.
        ComPtr<ID3D11Texture2D> resolveTexture_;
        ComPtr<ID3D11Texture2D> depthTexture_;
        ComPtr<ID3D11DepthStencilView> dsv_;

        int size_ = 0;
        bool mipMap_ = false;
        int levelCount_ = 1;
        int activeFace_ = -1;
        bool isMsaa_ = false;
        int appliedMultiSampleCount_ = 0;
    };
}
