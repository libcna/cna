#pragma once

// plan_dx.md Phase DX6 (DX-43/DX-45): real D3D11 offscreen render-target backends.
//
// Both classes hold a non-owning D3D11GraphicsBackend* (owner_) -- unlike D3D11Buffers/Textures,
// which are fully self-contained via their own ComPtr<ID3D11Device/DeviceContext>, a render
// target's Unbind must be able to restore the *owner's* back-buffer RTV/DSV/viewport, which only
// the owning backend instance knows how to do (mirrors VulkanRenderTargetBackend's identical
// owner_ pattern, for the identical reason).

#include "../Common/IGraphicsBackend.hpp"

#include <d3d11.h>
#include <wrl/client.h>

namespace CNA::Internal::Backends::D3D11
{
    using Microsoft::WRL::ComPtr;

    class D3D11GraphicsBackend;

    /// Real D3D11 2D render-target backend (DX-43). Optional depth-stencil (DX-11-fmt's
    /// DepthFormatToDxgi table; None omits the attachment entirely, same as EasyGL/Bgfx --
    /// HasRealDepthBuffer()'s IRenderTargetBackend default already reflects this honestly).
    /// Optional MSAA (DX-45): when the device reports support for the requested sample count, the
    /// color attachment is allocated MSAA and resolved into a separate non-MSAA texture (the one
    /// actually sampled/read back) on UnbindAsRenderTarget(), since flip-model swap chains and
    /// MSAA render-target SRVs cannot be sampled directly. Optional full mip chain (D3D11's own
    /// D3D11_RESOURCE_MISC_GENERATE_MIPS + ID3D11DeviceContext::GenerateMips(), regenerated on
    /// every UnbindAsRenderTarget() call -- the D3D11 equivalent of EasyGL's glGenerateMipmap-on-
    /// unbind / FNA3D's OPENGL_ResolveTarget, and much simpler than Vulkan's manual per-level
    /// vkCmdBlitImage cascade since D3D11 exposes this as one built-in device call).
    class D3D11RenderTargetBackend final : public IRenderTargetBackend
    {
    public:
        D3D11RenderTargetBackend(D3D11GraphicsBackend* owner, ID3D11Device* device, ID3D11DeviceContext* context,
                                 int w, int h, int depthFormat, bool mipMap, int multiSampleCount);

        [[nodiscard]] int GetWidth() const override { return width_; }
        [[nodiscard]] int GetHeight() const override { return height_; }
        [[nodiscard]] SDL_Texture* GetNativeTexture() const override { return nullptr; }

        void BindAsRenderTarget() override;
        void UnbindAsRenderTarget() override;
        [[nodiscard]] int GetMultiSampleCount() const override { return appliedMultiSampleCount_; }

        /// DX-143: the real MSAA-resolve/mip-regeneration work UnbindAsRenderTarget() does,
        /// extracted so D3D11GraphicsBackend::SetRenderTargets()'s MRT (N>1) path can finalize
        /// each bound target individually when the MRT set itself is replaced/unbound, without
        /// also triggering UnbindAsRenderTarget()'s own back-buffer-restore side effect (MRT's own
        /// caller already handles that once, not per-target). UnbindAsRenderTarget() itself now
        /// just calls this plus the restore, so single-target behavior is unchanged. NOXNA.
        void ResolveAndGenerateMipsEXT();

        /// Real ID3D11RenderTargetView for this target's color attachment (NOXNA).
        [[nodiscard]] ID3D11RenderTargetView* GetRTVEXT() const { return rtv_.Get(); }
        /// Real ID3D11DepthStencilView, or null if depthFormat was None/unrecognized (NOXNA).
        [[nodiscard]] ID3D11DepthStencilView* GetDSVEXT() const { return dsv_.Get(); }
        /// SRV of the sampleable (non-MSAA, post-resolve if MSAA) color texture (NOXNA, Phase DX8).
        [[nodiscard]] ID3D11ShaderResourceView* GetShaderResourceViewEXT() const { return srv_.Get(); }
        [[nodiscard]] bool IsMsaaEXT() const { return isMsaa_; }
        /// The texture srv_ actually points at -- resolveTexture_ when isMsaa_, else colorTexture_
        /// itself (NOXNA, test/diagnostics readback).
        [[nodiscard]] ID3D11Texture2D* GetSampleableTextureEXT() const
        {
            return isMsaa_ ? resolveTexture_.Get() : colorTexture_.Get();
        }
        /// Real mip-chain level count (1 when `mipMap` was false) -- NOXNA, DX-144 subresource math.
        [[nodiscard]] int GetLevelCountEXT() const { return levelCount_; }

    private:
        D3D11GraphicsBackend* owner_ = nullptr;
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

    /// Real D3D11 cube-map render-target backend (DX-43). One shared 6-slice texture array (same
    /// D3D11_RESOURCE_MISC_TEXTURECUBE shape as D3D11TextureCubeBackend) with one RTV per face
    /// (D3D11_RTV_DIMENSION_TEXTURE2DARRAY, FirstArraySlice=face) and a single shared depth-
    /// stencil buffer reused across faces (only one face is ever the active draw target at a
    /// time, matching EasyGLRenderTargetCubeBackend's identical one-depth-buffer-per-cube
    /// convention). MSAA is deliberately NOT supported here (unlike the 2D variant, DX-45) --
    /// combining a texture-cube array with MSAA is a narrower, rarer case than 2D MSAA render
    /// targets and was out of this task's scope; GetMultiSampleCount() always reports 0.
    class D3D11RenderTargetCubeBackend final : public IRenderTargetCubeBackend
    {
    public:
        D3D11RenderTargetCubeBackend(D3D11GraphicsBackend* owner, ID3D11Device* device, ID3D11DeviceContext* context,
                                     int size, int depthFormat, bool mipMap);

        [[nodiscard]] int GetSize() const override { return size_; }
        void BindAsRenderTargetFace(int face) override;
        void UnbindAsRenderTarget() override;
        [[nodiscard]] int GetMultiSampleCount() const override { return 0; }

        [[nodiscard]] ID3D11ShaderResourceView* GetShaderResourceViewEXT() const { return srv_.Get(); }
        /// The underlying 6-slice texture-array resource (NOXNA, test/diagnostics mip readback --
        /// DX-144 -- face `f`'s mip level `m` is subresource `m + f * GetLevelCountEXT()`, the
        /// standard D3D11 texture-array/mip subresource-index convention).
        [[nodiscard]] ID3D11Texture2D* GetColorTextureEXT() const { return texture_.Get(); }
        /// Real mip-chain level count (1 when `mipMap` was false) -- NOXNA, DX-144 subresource math.
        [[nodiscard]] int GetLevelCountEXT() const { return levelCount_; }

    private:
        D3D11GraphicsBackend* owner_ = nullptr;
        ComPtr<ID3D11Device> device_;
        ComPtr<ID3D11DeviceContext> context_;

        ComPtr<ID3D11Texture2D> texture_;
        ComPtr<ID3D11RenderTargetView> rtv_[6];
        ComPtr<ID3D11ShaderResourceView> srv_;
        ComPtr<ID3D11Texture2D> depthTexture_;
        ComPtr<ID3D11DepthStencilView> dsv_;

        int size_ = 0;
        bool mipMap_ = false;
        int levelCount_ = 1;
        int activeFace_ = -1;
    };
}
