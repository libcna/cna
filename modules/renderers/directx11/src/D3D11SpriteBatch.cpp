// plans/plan_dx.md Phase DX9 (DX-70/DX-71/DX-72).
#include "CNA/Internal/Renderers/DirectX11/D3D11SpriteBatch.hpp"
#include "CNA/Internal/Renderers/DirectX11/DirectX11Renderer.hpp"
#include "CNA/Internal/Renderers/DirectX11/D3D11Textures.hpp"
#include "CNA/Internal/Renderers/DirectX11/D3D11RenderTargets.hpp"
#include "CNA/Internal/Renderers/DirectX11/D3D11EffectRenderer.hpp"
#include "CNA/Internal/Renderers/D3DCommon/D3DShaderCache.hpp"
#include "CNA/Internal/Renderers/D3DCommon/D3DConstantBuffers.hpp"

#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"

#include <cmath>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace CNA::Internal::Renderers::DirectX11
{
    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Rectangle;
    using Microsoft::Xna::Framework::Vector2;
    using Microsoft::Xna::Framework::Graphics::Effect;

    namespace
    {
        /// Same two-concrete-type SRV resolution as DirectX11Renderer.cpp's own (anonymous-
        /// namespace-local, not exported) GetSrvForTextureEXT -- duplicated rather than factored
        /// into a shared header for 6 lines of logic, matching this codebase's existing precedent
        /// (DX-62's own fork report never factored it out either).
        ID3D11ShaderResourceView* GetSrvForTextureEXT(const ITextureRenderer* tex)
        {
            if (tex == nullptr) return nullptr;
            if (const auto* t = dynamic_cast<const D3D11TextureRenderer*>(tex))
                return t->GetShaderResourceViewEXT();
            if (const auto* rt = dynamic_cast<const D3D11RenderTargetRenderer*>(tex))
                return rt->GetShaderResourceViewEXT();
            return nullptr;
        }
    }

    D3D11SpriteBatchRenderer::D3D11SpriteBatchRenderer(DirectX11Renderer* owner)
        : owner_(owner)
        , device_(owner_->GetDeviceEXT())
        , context_(owner_->GetContextEXT())
        , vb_(device_.Get(), context_.Get(), 256)
        , ib_(device_.Get(), context_.Get(), 384, false)
    {
    }

    void D3D11SpriteBatchRenderer::Begin()
    {
        if (begun_) return;
        begun_ = true;
    }

    void D3D11SpriteBatchRenderer::End()
    {
        FlushBatch();
        begun_ = false;
    }

    void D3D11SpriteBatchRenderer::SetTransformMatrix(const Matrix& m)
    {
        transform_ = m;
    }

    void D3D11SpriteBatchRenderer::SetCustomEffect(Effect* effect)
    {
        if (customEffect_ != effect)
        {
            FlushBatch();
            customEffect_ = effect;
        }
    }

    void D3D11SpriteBatchRenderer::SetSamplerFilter(int textureFilter)
    {
        pendingFilter_ = textureFilter;
    }

    void D3D11SpriteBatchRenderer::SetSamplerAddressMode(int addressU, int addressV)
    {
        pendingAddressU_ = addressU;
        pendingAddressV_ = addressV;
    }

    void D3D11SpriteBatchRenderer::GetCurrentViewportSize(float& width, float& height) const
    {
        UINT numViewports = 1;
        D3D11_VIEWPORT vp{};
        context_->RSGetViewports(&numViewports, &vp);
        width = vp.Width;
        height = vp.Height;
    }

    ID3D11InputLayout* D3D11SpriteBatchRenderer::GetOrCreateSprite2DInputLayout()
    {
        if (!sprite2DInputLayout_)
        {
            // Fixed Sprite2DVertex contract -- identical shape to D3D11EffectRenderer's own
            // hardcoded custom-shader input layout (POSITION0 R32G32 @0, TEXCOORD0 R32G32 @8,
            // COLOR0 R32G32B32A32 @16), but created against sprite2d.vert.hlsl's own bytecode
            // (CreateInputLayout validates the element array against a specific vertex shader's
            // real input signature, so this can't be shared with D3D11EffectRenderer's layout,
            // which is validated against whatever custom HLSL a game compiled at runtime).
            static const D3D11_INPUT_ELEMENT_DESC kElements[] = {
                { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
                { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 8,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
                { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            };

            const uint8_t* vsBytes = nullptr;
            std::size_t vsSize = 0;
            D3DCommon::GetVertexShaderBytecode(D3DCommon::D3DShaderVariant::Sprite2d, vsBytes, vsSize);
            if (vsBytes == nullptr || vsSize == 0)
                return nullptr;

            device_->CreateInputLayout(kElements, ARRAYSIZE(kElements), vsBytes, vsSize,
                                       sprite2DInputLayout_.ReleaseAndGetAddressOf());
        }
        return sprite2DInputLayout_.Get();
    }

    ID3D11Buffer* D3D11SpriteBatchRenderer::GetOrCreatePerDrawBuffer()
    {
        if (!perDrawBuffer_)
        {
            D3D11_BUFFER_DESC desc{};
            desc.ByteWidth = sizeof(D3DCommon::D3DSprite2DConstants);
            desc.Usage = D3D11_USAGE_DYNAMIC;
            desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            device_->CreateBuffer(&desc, nullptr, perDrawBuffer_.ReleaseAndGetAddressOf());
        }
        return perDrawBuffer_.Get();
    }

    void D3D11SpriteBatchRenderer::FlushBatch()
    {
        if (pendingVertices_.empty()) return;

        float vpW = 0.0f, vpH = 0.0f;
        GetCurrentViewportSize(vpW, vpH);

        D3D11EffectRenderer* customRenderer = nullptr;
        if (customEffect_)
            customRenderer = dynamic_cast<D3D11EffectRenderer*>(customEffect_->GetEffectRendererPtr());

        if (customRenderer && customRenderer->IsValid())
        {
            // DX-71: vpSize is set here, once per flush, mirroring VulkanEffectRenderer's own
            // "set automatically by the sprite-batch runtime" convention -- the game/effect
            // author never calls SetViewportSizeEXT() itself.
            customRenderer->SetViewportSizeEXT(vpW, vpH);
            customEffect_->Apply();
            customRenderer->Bind();
        }
        else
        {
            auto vs = D3DCommon::CreateVertexShaderForVariant(device_.Get(), D3DCommon::D3DShaderVariant::Sprite2d);
            auto ps = D3DCommon::CreatePixelShaderForVariant(device_.Get(), D3DCommon::D3DShaderVariant::Sprite2d);
            if (!vs || !ps)
                throw std::runtime_error("D3D11SpriteBatchRenderer: failed to create sprite2d shader objects");

            ID3D11InputLayout* layout = GetOrCreateSprite2DInputLayout();
            if (!layout)
                throw std::runtime_error("D3D11SpriteBatchRenderer: failed to create sprite2d input layout");

            D3DCommon::D3DSprite2DConstants c{};
            c.ViewportSize[0] = vpW;
            c.ViewportSize[1] = vpH;
            ID3D11Buffer* cb = GetOrCreatePerDrawBuffer();
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (SUCCEEDED(context_->Map(cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
            {
                std::memcpy(mapped.pData, &c, sizeof(c));
                context_->Unmap(cb, 0);
            }

            context_->IASetInputLayout(layout);
            context_->VSSetShader(vs.Get(), nullptr, 0);
            context_->PSSetShader(ps.Get(), nullptr, 0);
            context_->VSSetConstantBuffers(0, 1, &cb);
        }

        // Texture unit 0 is always driven by the caller for both paths (IEffectRenderer::
        // BindTexture()'s own doc comment; D3D11EffectRenderer deliberately doesn't override it).
        ID3D11ShaderResourceView* srv = GetSrvForTextureEXT(currentTexture_);
        context_->PSSetShaderResources(0, 1, &srv);
        owner_->ApplySamplerState(0, pendingFilter_, pendingAddressU_, pendingAddressV_, 1);

        vb_.SetData(pendingVertices_.data(), static_cast<int>(pendingVertices_.size()), sizeof(Sprite2DVertex));
        ib_.SetData16(pendingIndices_.data(), static_cast<int>(pendingIndices_.size()));

        ID3D11Buffer* vbRaw = vb_.GetBufferEXT();
        const UINT stride = static_cast<UINT>(sizeof(Sprite2DVertex));
        const UINT offset = 0;
        context_->IASetVertexBuffers(0, 1, &vbRaw, &stride, &offset);
        context_->IASetIndexBuffer(ib_.GetBufferEXT(), ib_.GetFormatEXT(), 0);
        context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context_->DrawIndexed(static_cast<UINT>(pendingIndices_.size()), 0, 0);

        pendingVertices_.clear();
        pendingIndices_.clear();
        currentTexture_ = nullptr;
    }

    void D3D11SpriteBatchRenderer::Draw(const ITextureRenderer& texture, float x, float y)
    {
        const int w = texture.GetWidth();
        const int h = texture.GetHeight();
        Draw(texture, Rectangle(static_cast<int>(x), static_cast<int>(y), w, h), Rectangle(0, 0, w, h),
             Color::White);
    }

    void D3D11SpriteBatchRenderer::Draw(const ITextureRenderer& texture,
                                       const Rectangle& destinationRectangle,
                                       const Rectangle& sourceRectangle,
                                       const Color& color)
    {
        Draw(texture, destinationRectangle, sourceRectangle, color, 0.0f, Vector2(0, 0), SpriteEffects::None, 0.0f);
    }

    void D3D11SpriteBatchRenderer::Draw(const ITextureRenderer& texture,
                                       const Rectangle& destinationRectangle,
                                       const Rectangle& sourceRectangle,
                                       const Color& color,
                                       float rotation,
                                       const Vector2& origin,
                                       SpriteEffects effects,
                                       float /*layerDepth*/)
    {
        if (!begun_) throw std::runtime_error("D3D11SpriteBatchRenderer::Draw called before Begin()");

        if (currentTexture_ != nullptr && currentTexture_ != &texture)
            FlushBatch();
        currentTexture_ = &texture;

        const float texW = static_cast<float>(texture.GetWidth());
        const float texH = static_cast<float>(texture.GetHeight());

        // No [0,1] clamp -- matches FNA (SpriteBatch.cs divides straight through). A
        // sourceRectangle extending past the texture bounds intentionally produces UVs outside
        // [0,1], letting the bound SamplerState's TextureAddressMode (DX-72: Wrap/Mirror/Clamp,
        // all real on this renderer via D3D11SamplerCache) govern edge sampling.
        float u1 = static_cast<float>(sourceRectangle.X) / texW;
        float v1 = static_cast<float>(sourceRectangle.Y) / texH;
        float u2 = static_cast<float>(sourceRectangle.X + sourceRectangle.Width)  / texW;
        float v2 = static_cast<float>(sourceRectangle.Y + sourceRectangle.Height) / texH;

        if (static_cast<int>(effects) & static_cast<int>(SpriteEffects::FlipHorizontally)) std::swap(u1, u2);
        if (static_cast<int>(effects) & static_cast<int>(SpriteEffects::FlipVertically)) std::swap(v1, v2);

        const float r = static_cast<float>(color.getRProperty()) / 255.0f;
        const float g = static_cast<float>(color.getGProperty()) / 255.0f;
        const float b = static_cast<float>(color.getBProperty()) / 255.0f;
        const float a = static_cast<float>(color.getAProperty()) / 255.0f;

        const float dx = static_cast<float>(destinationRectangle.X);
        const float dy = static_cast<float>(destinationRectangle.Y);
        const float dw = static_cast<float>(destinationRectangle.Width);
        const float dh = static_cast<float>(destinationRectangle.Height);

        const float sw = static_cast<float>(sourceRectangle.Width);
        const float sh = static_cast<float>(sourceRectangle.Height);

        const float ox = origin.X;
        const float oy = origin.Y;

        const float scaleX = dw / sw;
        const float scaleY = dh / sh;

        const float p0x = (0.0f - ox) * scaleX, p0y = (0.0f - oy) * scaleY;
        const float p1x = (sw   - ox) * scaleX, p1y = (0.0f - oy) * scaleY;
        const float p2x = (sw   - ox) * scaleX, p2y = (sh   - oy) * scaleY;
        const float p3x = (0.0f - ox) * scaleX, p3y = (sh   - oy) * scaleY;

        const float cosR = std::cos(rotation);
        const float sinR = std::sin(rotation);

        auto rotateAndTranslate = [&](float px, float py, float& rx, float& ry)
        {
            rx = dx + px * cosR - py * sinR;
            ry = dy + px * sinR + py * cosR;
        };

        float v0x, v0y, v1x, v1y, v2x, v2y, v3x, v3y;
        rotateAndTranslate(p0x, p0y, v0x, v0y);
        rotateAndTranslate(p1x, p1y, v1x, v1y);
        rotateAndTranslate(p2x, p2y, v2x, v2y);
        rotateAndTranslate(p3x, p3y, v3x, v3y);

        // DX-70: apply SpriteBatch's own transform matrix here, in pixel space, before upload --
        // see this file's header comment for why this is the correct place (sprite2d.vert.hlsl's
        // real contract has no projection-matrix uniform to fold it into GPU-side).
        const Vector2 tv0 = Vector2::Transform(Vector2(v0x, v0y), transform_);
        const Vector2 tv1 = Vector2::Transform(Vector2(v1x, v1y), transform_);
        const Vector2 tv2 = Vector2::Transform(Vector2(v2x, v2y), transform_);
        const Vector2 tv3 = Vector2::Transform(Vector2(v3x, v3y), transform_);

        const auto base = static_cast<uint16_t>(pendingVertices_.size());

        pendingVertices_.push_back({tv0.X, tv0.Y, u1, v1, r, g, b, a});
        pendingVertices_.push_back({tv1.X, tv1.Y, u2, v1, r, g, b, a});
        pendingVertices_.push_back({tv2.X, tv2.Y, u2, v2, r, g, b, a});
        pendingVertices_.push_back({tv3.X, tv3.Y, u1, v2, r, g, b, a});

        pendingIndices_.push_back(base + 0);
        pendingIndices_.push_back(base + 1);
        pendingIndices_.push_back(base + 2);
        pendingIndices_.push_back(base + 2);
        pendingIndices_.push_back(base + 3);
        pendingIndices_.push_back(base + 0);
    }
}
