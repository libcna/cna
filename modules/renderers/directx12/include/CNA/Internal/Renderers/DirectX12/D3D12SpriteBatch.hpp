#pragma once

// plans/plan_dx.md Phase DX12 (DX-111/DX-112 follow-up): real D3D12 SpriteBatch renderer -- quad batching
// that feeds the stock sprite2d pipeline. Mirrors D3D11SpriteBatchRenderer's own structure and exact
// destination/source-rect/origin/rotation/SpriteEffects-flip formula (D3D11SpriteBatch.cpp) as
// closely as D3D12's genuinely different device/PSO/root-signature model allows -- CPU-side quad
// math is copied unchanged (it's XNA-level geometry, not renderer-specific), the GPU submission path
// is D3D12-native (root signature + PSO + command-list recording, not D3D11's immediate context).
//
// sprite2d's own dedicated input layout/PSO/root-signature (built directly here, NOT through
// D3D12PipelineStateCache/D3D12InputLayoutCache) -- the shared PSO cache resolves its input layout
// purely from byte stride (D3DVertexFormatHelper::InputElementsForStrideD3D12), and sprite2d's own
// 32-byte Sprite2DVertex (x,y|u,v|r,g,b,a) collides with VertexPositionNormalTexture's existing
// stride-32 meaning (lit_textured3d's own vertex format) -- exactly the same real collision risk
// D3D11's own DX-70 fork already flagged and worked around by keeping sprite2d's input layout fully
// separate. Root signature IS shared with alpha_test3d's (1,1,1) shape (1 CBV @ b0, 1 SRV @ t0, 1
// static sampler @ s0) -- a root signature only describes binding *slots*, not cbuffer contents, so
// this is a genuine, safe reuse, not a collision risk (D3D12RootSignatureCache.hpp's own doc
// comment already establishes this principle for lit_textured3d/textured3d sharing (2,1,1)).
//
// Scope notes:
//   - SetCustomEffect() (D3D11's own DX-71): CLOSED (DX-121) -- a valid custom Effect now draws
//     through its own real D3D12EffectRenderer PSO+constant-buffer, see FlushBatch()'s own branch.
//   - SetSamplerFilter()/SetSamplerAddressMode() (D3D11's own DX-72): CLOSED (DX-133) -- these now
//     genuinely drive slot 0's real sampler descriptor via DX-119's dynamic per-slot sampler system
//     (owner_->ApplySamplerState(0, ...) in FlushBatch(), mirroring D3D11SpriteBatchRenderer's own
//     exact call), not a fixed static sampler.
//   - Blend state: CLOSED (DX-163) -- stock SpriteBatch PSOs are cached by the currently-applied
//     XNA blend factors/functions, write mask, sample mask and render-target format. AlphaBlend,
//     Additive, NonPremultiplied, Opaque and custom BlendState values therefore reach the real
//     D3D12 output merger instead of every sprite silently using Opaque.

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/DirectX12/D3D12RendererReference.hpp"
#include "D3D12PipelineStateCache.hpp"
#include "D3D12Buffers.hpp"

#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>
#include <map>
#include <memory>
#include <tuple>
#include <vector>

namespace CNA::Internal::Renderers::DirectX12
{
    using Microsoft::WRL::ComPtr;

    class DirectX12Renderer;
#if defined(CNA_DIRECTX12_COMPILED_EFFECTS)
    class D3D12CompiledEffect;
#endif

    /// Real D3D12 SpriteBatch renderer (plans/plan_dx.md Phase DX12, DX-111/DX-112 follow-up).
    class D3D12SpriteBatchRenderer final : public ISpriteBatchRenderer
    {
    public:
        /// Fixed vertex contract, identical shape to D3D11SpriteBatchRenderer's own (x,y | u,v |
        /// r,g,b,a -- 32 bytes): POSITION0 R32G32 @0, TEXCOORD0 R32G32 @8, COLOR0 R32G32B32A32 @16.
        /// Only the custom-effect and compiled-effect paths use it, with the Begin transform already
        /// applied on the CPU -- the vertex those paths are written against.
        struct Sprite2DVertex { float x, y, u, v, r, g, b, a; };
        /// plans/plan_directx12_parity.md DX12-0013: the stock path's untransformed vertex,
        /// (x, y, layerDepth | u, v | r, g, b, a -- 36 bytes). sprite3d.vert.hlsl applies
        /// MatrixTransform = transform * ortho exactly as FNA's SpriteEffect does.
        struct SpriteVertex { float x, y, z, u, v, r, g, b, a; };

        explicit D3D12SpriteBatchRenderer(DirectX12Renderer* owner);
        ~D3D12SpriteBatchRenderer() override;

        void Begin() override;
        void End() override;
        void SetTransformMatrix(const Matrix& m) override;
        void SetCustomEffect(Microsoft::Xna::Framework::Graphics::Effect* effect) override;
        void SetSamplerFilter(int textureFilter) override;
        void SetSamplerAddressMode(int addressU, int addressV) override;
        void SetSamplerMipState(int maxMipLevel, float lodBias) override;
        void SetSamplerAddressW(int addressW) override;

        void Draw(const ITextureRenderer& texture, float x, float y) override;
        void Draw(const ITextureRenderer& texture,
                  const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle,
                  const Color& color) override;
        void Draw(const ITextureRenderer& texture,
                  const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle,
                  const Color& color,
                  float rotation,
                  const Vector2& origin,
                  SpriteEffects effects,
                  float layerDepth) override;

    private:
        void FlushBatch();
        /// DX12-0013: sprite3d binds a constant buffer at b0 in BOTH stages -- MatrixTransform for the
        /// vertex stage, the Direct3D 9 channel expansion for the pixel stage -- so its root signature
        /// gives each its own visibility; D3D12RootSignatureCache exposes every CBV to all stages.
        /// Recreated when the renderer's device has been replaced.
        ID3D12RootSignature* GetOrCreateSprite3DRootSignature();
        /// The pending vertices with the Begin transform applied in pixel space, for the paths that
        /// bind a 2D position (a custom ShaderEffect, a compiled Effect).
        const std::vector<Sprite2DVertex>& TransformedSprite2DVertices();
#if defined(CNA_DIRECTX12_COMPILED_EFFECTS)
        void ApplyCompiledSpriteVertexShader(float viewportWidth, float viewportHeight);
        void FlushBatchWithCompiledEffect();
#endif

        D3D12RendererReference owner_;
        ComPtr<ID3D12Device> device_;

        D3D12VertexBufferRenderer vb_;
        D3D12VertexBufferRenderer vb3d_;
        D3D12IndexBufferRenderer ib_;
        ComPtr<ID3D12RootSignature> sprite3DRootSignature_;
        ID3D12Device* sprite3DRootSignatureDevice_ = nullptr;

        std::vector<SpriteVertex> pendingVertices_;
        std::vector<Sprite2DVertex> transformedVertices_;
        std::vector<uint16_t> pendingIndices_;
        const ITextureRenderer* currentTexture_ = nullptr;

        bool begun_ = false;
        Matrix transform_ = Matrix::getIdentityProperty();
        Microsoft::Xna::Framework::Graphics::Effect* customEffect_ = nullptr;
#if defined(CNA_DIRECTX12_COMPILED_EFFECTS)
        std::unique_ptr<D3D12CompiledEffect> spriteCompiledEffect_;
        std::uint32_t spriteMatrixParameterIndex_ = 0;
#endif

        // Defaults mirror D3D11SpriteBatchRenderer's own and describe the complete slot-zero
        // sampler that FlushBatch applies before each draw.
        int pendingFilter_ = 0;   // TextureFilter::Linear
        int pendingAddressU_ = 1; // TextureAddressMode::Clamp
        int pendingAddressV_ = 1; // TextureAddressMode::Clamp
        int pendingAddressW_ = 1; // TextureAddressMode::Clamp
        int pendingMaxMipLevel_ = 0;
        float pendingLodBias_ = 0.0f;
    };
}
