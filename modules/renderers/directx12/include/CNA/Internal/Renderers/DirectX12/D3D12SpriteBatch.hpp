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
// Honest scope gaps, documented rather than silently omitted:
//   - SetCustomEffect() (D3D11's own DX-71): CLOSED (DX-121) -- a valid custom Effect now draws
//     through its own real D3D12EffectRenderer PSO+constant-buffer, see FlushBatch()'s own branch.
//   - SetSamplerFilter()/SetSamplerAddressMode() (D3D11's own DX-72): CLOSED (DX-133) -- these now
//     genuinely drive slot 0's real sampler descriptor via DX-119's dynamic per-slot sampler system
//     (owner_->ApplySamplerState(0, ...) in FlushBatch(), mirroring D3D11SpriteBatchRenderer's own
//     exact call), not a fixed static sampler.
//   - Blend state: D3D12 has no Phase-DX7-equivalent blend-state cache yet either -- this PSO uses
//     the same Opaque-blend default every other D3D12 draw uses today (D3D12PipelineStateDesc's own
//     default fields). Real alpha-blended sprite compositing is deferred to whenever a D3D12 blend-
//     state system lands; single non-overlapping sprite draws (this task's own test coverage) are
//     unaffected by this gap.

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "D3D12Buffers.hpp"

#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>
#include <vector>

namespace CNA::Internal::Renderers::DirectX12
{
    using Microsoft::WRL::ComPtr;

    class DirectX12Renderer;

    /// Real D3D12 SpriteBatch renderer (plans/plan_dx.md Phase DX12, DX-111/DX-112 follow-up).
    class D3D12SpriteBatchRenderer final : public ISpriteBatchRenderer
    {
    public:
        /// Fixed vertex contract, identical shape to D3D11SpriteBatchRenderer's own (x,y | u,v |
        /// r,g,b,a -- 32 bytes): POSITION0 R32G32 @0, TEXCOORD0 R32G32 @8, COLOR0 R32G32B32A32 @16.
        struct Sprite2DVertex { float x, y, u, v, r, g, b, a; };

        explicit D3D12SpriteBatchRenderer(DirectX12Renderer* owner);

        void Begin() override;
        void End() override;
        void SetTransformMatrix(const Matrix& m) override;
        void SetCustomEffect(Microsoft::Xna::Framework::Graphics::Effect* effect) override;
        void SetSamplerFilter(int textureFilter) override;
        void SetSamplerAddressMode(int addressU, int addressV) override;

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
        ID3D12PipelineState* GetOrCreateSprite2DPso(ID3D12RootSignature* rootSig);
        ID3D12Resource* GetOrCreatePerDrawConstantBuffer();

        DirectX12Renderer* owner_ = nullptr;
        ComPtr<ID3D12Device> device_;

        D3D12VertexBufferRenderer vb_;
        D3D12IndexBufferRenderer ib_;
        ComPtr<ID3D12PipelineState> sprite2DPso_;
        ComPtr<ID3D12Resource> perDrawConstantBuffer_;
        void* perDrawConstantBufferMapped_ = nullptr;

        std::vector<Sprite2DVertex> pendingVertices_;
        std::vector<uint16_t> pendingIndices_;
        const ITextureRenderer* currentTexture_ = nullptr;

        bool begun_ = false;
        Matrix transform_ = Matrix::getIdentityProperty();
        Microsoft::Xna::Framework::Graphics::Effect* customEffect_ = nullptr;

        // Defaults mirror D3D11SpriteBatchRenderer's own -- stored but not yet behaviorally real,
        // see this file's own header comment for why (no D3D12 dynamic-sampler-state system yet).
        int pendingFilter_ = 0;   // TextureFilter::Linear
        int pendingAddressU_ = 1; // TextureAddressMode::Clamp
        int pendingAddressV_ = 1; // TextureAddressMode::Clamp
    };
}
