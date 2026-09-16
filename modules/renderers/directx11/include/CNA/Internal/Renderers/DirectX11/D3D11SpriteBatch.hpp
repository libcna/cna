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
// SetTransformMatrix() is genuinely implemented (VulkanSpriteBatchRenderer leaves it a no-op). The
// stock path follows FNA's SpriteEffect: vertices stay untransformed (x, y, layerDepth) and the GPU
// applies transformMatrix * CreateOrthographicOffCenter(0, w, h, 0, 0, -1) through sprite3d.vert.hlsl
// (WINCLOSE-0014). It used to apply the transform on the CPU through Vector2::Transform, which keeps
// only the affine x/y part: layerDepth never reached the depth test, and a transform's depth and W --
// near-plane clipping, perspective -- were silently discarded. The custom-effect paths still receive
// the CPU-transformed 2D vertices they were written against (D3D11EffectRenderer binds a float2
// position with a viewport-size constant), so they behave exactly as before.

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "D3D11Buffers.hpp"

#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace CNA::Internal::Renderers::DirectX11
{
    using Microsoft::WRL::ComPtr;

    class DirectX11Renderer;
    class D3D11EffectRenderer;
#if defined(CNA_DIRECTX11_COMPILED_EFFECTS)
    class D3D11CompiledEffect;
#endif

    /// Real D3D11 SpriteBatch renderer (plans/plan_dx.md Phase DX9).
    class D3D11SpriteBatchRenderer final : public ISpriteBatchRenderer
    {
    public:
        /// Fixed vertex contract shared with D3D11EffectRenderer's own custom-shader input layout
        /// (x,y | u,v | r,g,b,a -- 32 bytes): POSITION0 R32G32 @0, TEXCOORD0 R32G32 @8,
        /// COLOR0 R32G32B32A32 @16.
        struct Sprite2DVertex { float x, y, u, v, r, g, b, a; };
        /// The stock path's vertex (36 bytes): untransformed pixel-space position with layerDepth
        /// as z -- POSITION0 R32G32B32 @0, TEXCOORD0 R32G32 @12, COLOR0 R32G32B32A32 @20.
        struct SpriteVertex { float x, y, z, u, v, r, g, b, a; };

        explicit D3D11SpriteBatchRenderer(DirectX11Renderer* owner);
        ~D3D11SpriteBatchRenderer() override;

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
        ID3D11InputLayout* GetOrCreateSprite2DInputLayout();
        ID3D11InputLayout* GetOrCreateSprite3DInputLayout();
        ID3D11Buffer* GetOrCreatePerDrawBuffer();
        ID3D11Buffer* GetOrCreateMatrixBuffer();
        /// The pending batch as CPU-transformed 2D vertices, for the custom-effect paths.
        const std::vector<Sprite2DVertex>& TransformedSprite2DVertices();
        void GetCurrentViewportSize(float& width, float& height) const;
#if defined(CNA_DIRECTX11_COMPILED_EFFECTS)
        void ApplyCompiledSpriteVertexShader(float viewportWidth, float viewportHeight);
        void FlushBatchWithCompiledEffect();
#endif

        DirectX11Renderer* owner_ = nullptr;
        ComPtr<ID3D11Device> device_;
        ComPtr<ID3D11DeviceContext> context_;

        D3D11VertexBufferRenderer vb_;
        D3D11IndexBufferRenderer ib_;
        D3D11VertexBufferRenderer vb3d_;
        ComPtr<ID3D11InputLayout> sprite2DInputLayout_;
        ComPtr<ID3D11InputLayout> sprite3DInputLayout_;
        ComPtr<ID3D11Buffer> perDrawBuffer_;
        ComPtr<ID3D11Buffer> matrixBuffer_;

        std::vector<SpriteVertex> pendingVertices_;
        std::vector<Sprite2DVertex> transformedVertices_;
        std::vector<uint16_t> pendingIndices_;
        const ITextureRenderer* currentTexture_ = nullptr;

        bool begun_ = false;
        Matrix transform_ = Matrix::getIdentityProperty();
        Microsoft::Xna::Framework::Graphics::Effect* customEffect_ = nullptr;
#if defined(CNA_DIRECTX11_COMPILED_EFFECTS)
        std::unique_ptr<D3D11CompiledEffect> spriteCompiledEffect_;
        std::uint32_t spriteMatrixParameterIndex_ = 0;
#endif

        // Defaults mirror EasyGLSpriteBatchRenderer's own: Linear filter, Clamp address -- a
        // SpriteBatch that never calls SetSamplerFilter/SetSamplerAddressMode behaves exactly as
        // if the game had never touched sampler state.
        int pendingFilter_ = 0;   // TextureFilter::Linear
        int pendingAddressU_ = 1; // TextureAddressMode::Clamp
        int pendingAddressV_ = 1; // TextureAddressMode::Clamp
        int pendingAddressW_ = 1; // TextureAddressMode::Clamp
        int pendingMaxMipLevel_ = 0;
        float pendingLodBias_ = 0.0f;
    };
}
