#pragma once

// plans/plan_dx.md Phase DX9 (DX-70/DX-71/DX-72): real D3D11 SpriteBatch renderer -- quad batching that
// feeds Phase DIRECTX8's stock sprite2d pipeline (DX-70), a custom-Effect draw path reusing
// D3D11EffectRenderer (DX-58/DX-71), and real TextureAddressMode::Wrap/Mirror support via the
// existing D3D11SamplerCache (DX-72).
//
// Structurally mirrors EasyGLSpriteBatchRenderer (immediate-flush-per-texture-change quad batcher,
// same destination/source-rect/origin/rotation/SpriteEffects-flip formula) rather than
// VulkanSpriteBatchRenderer's deferred-to-frame-end snapshot design -- D3D11's context is already
// immediate-mode like GL, so there's no command-buffer-recording reason to defer.
//
// One real, deliberate improvement over both existing precedents: SetTransformMatrix() is
// genuinely implemented here (VulkanSpriteBatchRenderer leaves it a silent no-op -- a known,
// undocumented-in-code gap). sprite2d.vert.hlsl's real contract (DX-13-hlsl) is a raw pixel-space
// Position input mapped straight to NDC via ViewportSize, with no projection-matrix uniform at
// all -- so this renderer applies the SpriteBatch transform matrix on the CPU, per vertex, via
// Vector2::Transform(), before upload. This is mathematically equivalent to XNA/EasyGL's
// combined = transformMatrix * orthographicProjection (both are affine maps composed in the same
// pixel-space domain XNA's SpriteBatch.Begin(transformMatrix) parameter operates in), just
// evaluated CPU-side instead of GPU-side, and it applies uniformly to both the stock sprite2d path
// and the custom-Effect path below (both draw from the same already-transformed vertex buffer).

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "D3D11Buffers.hpp"

#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>
#include <vector>

namespace CNA::Internal::Renderers::DirectX11
{
    using Microsoft::WRL::ComPtr;

    class DirectX11Renderer;
    class D3D11EffectRenderer;

    /// Real D3D11 SpriteBatch renderer (plans/plan_dx.md Phase DX9).
    class D3D11SpriteBatchRenderer final : public ISpriteBatchRenderer
    {
    public:
        /// Fixed vertex contract shared with D3D11EffectRenderer's own custom-shader input layout
        /// (x,y | u,v | r,g,b,a -- 32 bytes): POSITION0 R32G32 @0, TEXCOORD0 R32G32 @8,
        /// COLOR0 R32G32B32A32 @16.
        struct Sprite2DVertex { float x, y, u, v, r, g, b, a; };

        explicit D3D11SpriteBatchRenderer(DirectX11Renderer* owner);

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
        ID3D11InputLayout* GetOrCreateSprite2DInputLayout();
        ID3D11Buffer* GetOrCreatePerDrawBuffer();
        void GetCurrentViewportSize(float& width, float& height) const;

        DirectX11Renderer* owner_ = nullptr;
        ComPtr<ID3D11Device> device_;
        ComPtr<ID3D11DeviceContext> context_;

        D3D11VertexBufferRenderer vb_;
        D3D11IndexBufferRenderer ib_;
        ComPtr<ID3D11InputLayout> sprite2DInputLayout_;
        ComPtr<ID3D11Buffer> perDrawBuffer_;

        std::vector<Sprite2DVertex> pendingVertices_;
        std::vector<uint16_t> pendingIndices_;
        const ITextureRenderer* currentTexture_ = nullptr;

        bool begun_ = false;
        Matrix transform_ = Matrix::getIdentityProperty();
        Microsoft::Xna::Framework::Graphics::Effect* customEffect_ = nullptr;

        // Defaults mirror EasyGLSpriteBatchRenderer's own: Linear filter, Clamp address -- a
        // SpriteBatch that never calls SetSamplerFilter/SetSamplerAddressMode behaves exactly as
        // if the game had never touched sampler state.
        int pendingFilter_ = 0;   // TextureFilter::Linear
        int pendingAddressU_ = 1; // TextureAddressMode::Clamp
        int pendingAddressV_ = 1; // TextureAddressMode::Clamp
    };
}
