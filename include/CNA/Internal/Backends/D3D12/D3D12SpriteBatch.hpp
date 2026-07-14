#pragma once

// plan_dx.md Phase DX12 (DX-111/DX-112 follow-up): real D3D12 SpriteBatch backend -- quad batching
// that feeds the stock sprite2d pipeline. Mirrors D3D11SpriteBatchBackend's own structure and exact
// destination/source-rect/origin/rotation/SpriteEffects-flip formula (D3D11SpriteBatch.cpp) as
// closely as D3D12's genuinely different device/PSO/root-signature model allows -- CPU-side quad
// math is copied unchanged (it's XNA-level geometry, not backend-specific), the GPU submission path
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
//   - SetCustomEffect() (D3D11's own DX-71): no D3D12 equivalent of D3D11EffectBackend (a runtime
//     D3DCompile()-driven custom-HLSL-effect backend) exists yet for D3D12 -- FlushBatch() throws a
//     named "not yet implemented" error if a non-null custom Effect is actually flushed, rather than
//     silently falling back to the stock shader (which would render the wrong thing without error).
//   - SetSamplerFilter()/SetSamplerAddressMode() (D3D11's own DX-72): D3D12 has no per-draw dynamic
//     sampler-state equivalent yet (no D3D12 Phase-DX7-equivalent state-object system exists) -- the
//     root signature's static sampler is fixed at LINEAR filter / WRAP address mode (matching every
//     other D3D12 stock-variant draw's own fixed static sampler, D3D12RootSignatureCache.hpp). The
//     values passed to these setters are stored but do not yet change actual sampling behavior --
//     documented here, not silently dropped.
//   - Blend state: D3D12 has no Phase-DX7-equivalent blend-state cache yet either -- this PSO uses
//     the same Opaque-blend default every other D3D12 draw uses today (D3D12PipelineStateDesc's own
//     default fields). Real alpha-blended sprite compositing is deferred to whenever a D3D12 blend-
//     state system lands; single non-overlapping sprite draws (this task's own test coverage) are
//     unaffected by this gap.

#include "../Common/IGraphicsBackend.hpp"
#include "D3D12Buffers.hpp"

#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>
#include <vector>

namespace CNA::Internal::Backends::D3D12
{
    using Microsoft::WRL::ComPtr;

    class D3D12GraphicsBackend;

    /// Real D3D12 SpriteBatch backend (plan_dx.md Phase DX12, DX-111/DX-112 follow-up).
    class D3D12SpriteBatchBackend final : public ISpriteBatchBackend
    {
    public:
        /// Fixed vertex contract, identical shape to D3D11SpriteBatchBackend's own (x,y | u,v |
        /// r,g,b,a -- 32 bytes): POSITION0 R32G32 @0, TEXCOORD0 R32G32 @8, COLOR0 R32G32B32A32 @16.
        struct Sprite2DVertex { float x, y, u, v, r, g, b, a; };

        explicit D3D12SpriteBatchBackend(D3D12GraphicsBackend* owner);

        void Begin() override;
        void End() override;
        void SetTransformMatrix(const Matrix& m) override;
        void SetCustomEffect(Microsoft::Xna::Framework::Graphics::Effect* effect) override;
        void SetSamplerFilter(int textureFilter) override;
        void SetSamplerAddressMode(int addressU, int addressV) override;

        void Draw(const ITextureBackend& texture, float x, float y) override;
        void Draw(const ITextureBackend& texture,
                  const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle,
                  const Color& color) override;
        void Draw(const ITextureBackend& texture,
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

        D3D12GraphicsBackend* owner_ = nullptr;
        ComPtr<ID3D12Device> device_;

        D3D12VertexBufferBackend vb_;
        D3D12IndexBufferBackend ib_;
        ComPtr<ID3D12PipelineState> sprite2DPso_;
        ComPtr<ID3D12Resource> perDrawConstantBuffer_;
        void* perDrawConstantBufferMapped_ = nullptr;

        std::vector<Sprite2DVertex> pendingVertices_;
        std::vector<uint16_t> pendingIndices_;
        const ITextureBackend* currentTexture_ = nullptr;

        bool begun_ = false;
        Matrix transform_ = Matrix::getIdentityProperty();
        Microsoft::Xna::Framework::Graphics::Effect* customEffect_ = nullptr;

        // Defaults mirror D3D11SpriteBatchBackend's own -- stored but not yet behaviorally real,
        // see this file's own header comment for why (no D3D12 dynamic-sampler-state system yet).
        int pendingFilter_ = 0;   // TextureFilter::Linear
        int pendingAddressU_ = 1; // TextureAddressMode::Clamp
        int pendingAddressV_ = 1; // TextureAddressMode::Clamp
    };
}
