#pragma once

// plans/plan_dx9.md Phase D9-9 (D9-90/D9-91/D9-92/D9-93): real D3D9 SpriteBatch renderer, driving
// Microsoft's own SpriteEffect (SpriteVertexShader/SpritePixelShader, vendored verbatim in
// shaders/xna/SpriteEffect.fx per design decision 3, compiled and register-mapped by the same
// D9-71/D9-72 pipeline every other Stock Effect already goes through -- SpriteEffect's own two
// entry points were already present in d3d9_shaders.hpp/D3D9ShaderRegisters.hpp before this file
// existed, a side effect of that pipeline sweeping every vendored .fx file, not new work here).
//
// Structurally mirrors D3D11SpriteBatchRenderer (CPU-side quad geometry: destination/source rect,
// origin, rotation, scale, SpriteEffects flip -- same formula, reused verbatim, since it is
// projection-agnostic pixel-space math) but drives a genuinely different shader and vertex
// contract: D3D11's own sprite2d.hlsl is a CNA invention with no XNA equivalent, whereas D3D9
// must draw Microsoft's real, unmodified SpriteEffect.fx (design decision 3) using its real
// vertex contract (SpriteVertexShader's inout float4 color : COLOR0 / float2 texCoord : TEXCOORD0
// / float4 position : SV_Position, matching FNA's own SpriteBatch.cs VertexPositionColorTexture4
// per-corner shape: Position Vector3 @0, Color @12 (D3DDECLTYPE_UBYTE4N, matching D9-82's own
// R,G,B,A-ascending finding, not D3DDECLTYPE_D3DCOLOR), TextureCoordinate Vector2 @16 -- the
// EXISTING stride-24 D3D9VertexDeclarations layout, D9-22's own "VertexPositionColorTexture"
// entry, reused as-is) and a real float4x4 MatrixTransform constant (v0, 4 registers) instead of
// D3D11's own scalar ViewportSize pair.
//
// Design decision 10: the D3D9 half-pixel offset (XNA's D3D9-era SpriteBatch compensates for
// Direct3D 9's texel-center-at-integer-coordinate convention, which D3D10+/modern APIs do not
// share) is baked into MatrixTransform's own translation terms here, CPU-side, at exactly one
// site -- see D3D9SpriteBatch.cpp's own BuildMatrixTransformEXT() for the formula and its
// oracle-verified derivation (D9-91).

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/DirectX9/D3D9Buffers.hpp"

#include <d3d9.h>
#include <wrl/client.h>

#include <cstdint>
#include <vector>

namespace CNA::Internal::Renderers::DirectX9
{
    using Microsoft::WRL::ComPtr;

    class DirectX9Renderer;

    /// Real D3D9 SpriteBatch renderer (plans/plan_dx9.md Phase D9-9).
    class D3D9SpriteBatchRenderer final : public ISpriteBatchRenderer
    {
    public:
        /// Matches the existing stride-24 D3D9VertexDeclarations layout (POSITION0 FLOAT3 @0,
        /// COLOR0 UBYTE4N @12, TEXCOORD0 FLOAT2 @16) byte-for-byte -- same shape FNA's own
        /// VertexPositionColorTexture4 uses per corner.
        struct SpriteVertex
        {
            float x, y, z;
            std::uint8_t r, g, b, a;
            float u, v;
        };

        explicit D3D9SpriteBatchRenderer(DirectX9Renderer* owner);

        void Begin() override;
        void End() override;
        void SetTransformMatrix(const Matrix& m) override;
        void SetCustomEffect(Effect* effect) override;
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
        void EnsureShadersEXT();
        void EnsureVertexDeclarationEXT();
        void GetCurrentViewportSizeEXT(float& width, float& height) const;
        /// Real MatrixTransform = SpriteBatch's own transform * (D3D9 half-pixel-corrected
        /// orthographic projection) -- see D3D9SpriteBatch.cpp for the derivation.
        Matrix BuildMatrixTransformEXT(float viewportWidth, float viewportHeight) const;

        DirectX9Renderer* owner_ = nullptr;
        ComPtr<IDirect3DDevice9> device_;

        ComPtr<IDirect3DVertexShader9> vertexShader_;
        ComPtr<IDirect3DPixelShader9> pixelShader_;
        ComPtr<IDirect3DVertexDeclaration9> vertexDecl_;

        D3D9VertexBufferRenderer vb_;
        D3D9IndexBufferRenderer ib_;

        std::vector<SpriteVertex> pendingVertices_;
        std::vector<std::uint16_t> pendingIndices_;
        const ITextureRenderer* currentTexture_ = nullptr;

        bool begun_ = false;
        Matrix transform_ = Matrix::getIdentityProperty();

        /// D9-112: the custom Effect passed to SpriteBatch::Begin(effect), or nullptr for the
        /// stock SpriteEffect path. Not owned -- the caller (SpriteBatch/the game) owns its own
        /// lifetime, matching D3D11SpriteBatchRenderer's own identical convention.
        Effect* customEffect_ = nullptr;

        // Defaults mirror D3D11SpriteBatchRenderer's own: Linear filter, Clamp address -- a
        // SpriteBatch that never calls SetSamplerFilter/SetSamplerAddressMode behaves exactly as
        // if the game had never touched sampler state. In practice SpriteBatch::Begin() (the
        // public XNA layer) always calls both before the first Draw of any batch, so these are
        // only ever observed pre-Begin().
        int pendingFilter_ = 0;   // TextureFilter::Linear
        int pendingAddressU_ = 1; // TextureAddressMode::Clamp
        int pendingAddressV_ = 1; // TextureAddressMode::Clamp
    };
}
