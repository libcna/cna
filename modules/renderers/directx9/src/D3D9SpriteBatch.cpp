#include "CNA/Internal/Renderers/DirectX9/D3D9SpriteBatch.hpp"
#include "CNA/Internal/Renderers/DirectX9/DirectX9Renderer.hpp"
#include "CNA/Internal/Renderers/DirectX9/D3D9Textures.hpp"
#include "CNA/Internal/Renderers/DirectX9/D3D9RenderTargets.hpp"
#include "CNA/Internal/Renderers/DirectX9/D3D9VertexDeclarations.hpp"
#include "CNA/Internal/Renderers/DirectX9/D3D9ConstantUpload.hpp"
#include "CNA/Internal/Renderers/DirectX9/D3D9EffectRenderer.hpp"
#include "shaders/d3d9_shaders.hpp"
#include "CNA/Internal/Renderers/DirectX9/shaders/D3D9ShaderRegisters.hpp"

#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"

#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

namespace CNA::Internal::Renderers::DirectX9
{
    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Rectangle;
    using Microsoft::Xna::Framework::Vector2;

    namespace
    {
        std::string FormatHr(HRESULT hr)
        {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "0x%08lX", static_cast<unsigned long>(hr));
            return buf;
        }

        D3DPRIMITIVETYPE ToD3D9Topology()
        {
            return D3DPT_TRIANGLELIST;
        }

        /// Same two-concrete-type resolution as D3D9EffectDraw.cpp's own ResolveD3D9TextureEXT
        /// (RenderTarget2D used as a SpriteBatch texture is the same real, legal ITextureRenderer
        /// runtime type this project's D3D11 renderer already handles) -- duplicated per-file
        /// rather than factored into a shared header, matching D3D11SpriteBatch.cpp's own
        /// documented precedent for the identical situation. Previously this call site only tried
        /// D3D9TextureRenderer and silently dropped the texture (drew untextured) for a
        /// RenderTarget2D -- wrong, but not the crash; kept as dynamic_cast-safe here since the
        /// crash's own root cause was the *effect*-draw path's static_cast, not this one.
        IDirect3DTexture9* ResolveD3D9TextureEXT(const ITextureRenderer* tex)
        {
            if (tex == nullptr) return nullptr;
            if (const auto* t = dynamic_cast<const D3D9TextureRenderer*>(tex))
                return t->GetTextureEXT();
            if (const auto* rt = dynamic_cast<const D3D9RenderTargetRenderer*>(tex))
                return rt->GetTextureEXT();
            return nullptr;
        }
    }

    D3D9SpriteBatchRenderer::D3D9SpriteBatchRenderer(DirectX9Renderer* owner)
        : owner_(owner)
        , device_(owner_->GetDeviceEXT())
        , vb_(*owner_, device_.Get(), 256)
        , ib_(*owner_, device_.Get(), 384, false)
    {
    }

    void D3D9SpriteBatchRenderer::Begin()
    {
        if (begun_) return;
        begun_ = true;
    }

    void D3D9SpriteBatchRenderer::End()
    {
        FlushBatch();
        begun_ = false;
    }

    void D3D9SpriteBatchRenderer::SetTransformMatrix(const Matrix& m)
    {
        transform_ = m;
    }

    void D3D9SpriteBatchRenderer::SetCustomEffect(Effect* effect)
    {
        // D9-112: flush on change (mirrors D3D11SpriteBatchRenderer::SetCustomEffect() exactly) --
        // any pending batch was built expecting the PREVIOUS shader/effect, so it must be drawn
        // before the switch takes effect, not silently redrawn with the new one.
        if (customEffect_ != effect)
        {
            FlushBatch();
            customEffect_ = effect;
        }
    }

    void D3D9SpriteBatchRenderer::SetSamplerFilter(int textureFilter)
    {
        pendingFilter_ = textureFilter;
    }

    void D3D9SpriteBatchRenderer::SetSamplerAddressMode(int addressU, int addressV)
    {
        pendingAddressU_ = addressU;
        pendingAddressV_ = addressV;
    }

    void D3D9SpriteBatchRenderer::GetCurrentViewportSizeEXT(float& width, float& height) const
    {
        D3DVIEWPORT9 vp{};
        device_->GetViewport(&vp);
        width = static_cast<float>(vp.Width);
        height = static_cast<float>(vp.Height);
    }

    void D3D9SpriteBatchRenderer::EnsureShadersEXT()
    {
        if (vertexShader_ && pixelShader_) return;

        HRESULT hr = device_->CreateVertexShader(
            reinterpret_cast<const DWORD*>(Shaders::kSpriteEffect_SpriteVertexShader),
            vertexShader_.ReleaseAndGetAddressOf());
        if (FAILED(hr))
            throw std::runtime_error("D3D9SpriteBatchRenderer: CreateVertexShader failed, hr=" + FormatHr(hr));

        hr = device_->CreatePixelShader(
            reinterpret_cast<const DWORD*>(Shaders::kSpriteEffect_SpritePixelShader),
            pixelShader_.ReleaseAndGetAddressOf());
        if (FAILED(hr))
            throw std::runtime_error("D3D9SpriteBatchRenderer: CreatePixelShader failed, hr=" + FormatHr(hr));
    }

    void D3D9SpriteBatchRenderer::EnsureVertexDeclarationEXT()
    {
        if (vertexDecl_) return;

        UINT count = 0;
        const D3DVERTEXELEMENT9* elements = VertexElementsForStrideD3D9(24, count);
        if (elements == nullptr)
            throw std::runtime_error("D3D9SpriteBatchRenderer: no stride-24 vertex layout available");

        const HRESULT hr = device_->CreateVertexDeclaration(elements, vertexDecl_.ReleaseAndGetAddressOf());
        if (FAILED(hr))
            throw std::runtime_error("D3D9SpriteBatchRenderer: CreateVertexDeclaration failed, hr=" + FormatHr(hr));
    }

    Matrix D3D9SpriteBatchRenderer::BuildMatrixTransformEXT(float viewportWidth, float viewportHeight) const
    {
        // Design decision 10 / D9-91: real XNA 4.0's D3D9 SpriteBatch bakes a half-texel
        // correction into the same orthographic projection it already builds for
        // MatrixTransform, compensating for Direct3D 9's texel-center-at-integer-coordinate
        // convention (D3D10+/modern APIs put texel centers at pixel centers instead, which is
        // why FNA's own SpriteBatch.cs -- a modern-API-targeting reimplementation, not real XNA --
        // has no equivalent term at all; see this project's own plans/plan_dx9.md "Divergence 4").
        //
        // Formula empirically verified against the real XNA 4.0 runtime (D9-A oracle,
        // sprite_halfpixel_quad.scene): CreateOrthographicOffCenter(0, W, H, 0, 0, 1), then shift
        // the projection's own translation terms (M41/M42) by -0.5 texel's worth of NDC space
        // (half of one full-texel step, which CreateOrthographicOffCenter's own M11/M22 already
        // encode as "NDC units per pixel") -- NOT reasoned out from first principles and assumed
        // correct; see D9-91's own plans/plan_dx9.md row for the verification record.
        //
        // D9-93 finding: zFarPlane=1 (as originally written above and used by every D9-90/91/92
        // scene, all of which only ever pass layerDepth=0.0f) makes CreateOrthographicOffCenter's
        // own Z row M33=1/(zNear-zFar)=-1, M43=zNear/(zNear-zFar)=0 -- i.e. Z' = -layerDepth. Any
        // sprite drawn with layerDepth > 0 then lands outside Direct3D 9's valid [0,1] clip-space
        // Z range and gets hardware-clipped away entirely (reproduced: a second sprite drawn at
        // layerDepth=0.5 or 1.0 over the first silently vanished, confirmed NOT a depth-test
        // artifact since DepthStencilState.None -- ZENABLE=FALSE -- is already in effect; clip-
        // space culling is a separate, unconditional rasterizer stage). zFarPlane=-1 instead makes
        // M33=1/(0-(-1))=1, M43=0/(0-(-1))=0, i.e. an IDENTITY Z row (Z'=layerDepth, unclipped for
        // any layerDepth in XNA's documented [0,1] range) -- verified pixel-perfect against real
        // XNA 4.0 for both SpriteSortMode.Deferred and SpriteSortMode.BackToFront
        // (sprite_sortmode_deferred_quad.scene / sprite_sortmode_backtofront_quad.scene, D9-93).
        // Only the Z row changes; M41/M42's half-pixel shift below depends solely on M11/M22 (X/Y),
        // so every already-verified D9-90/91/92 scene is unaffected.
        Matrix projection = Matrix::CreateOrthographicOffCenter(
            0.0f, viewportWidth, viewportHeight, 0.0f, 0.0f, -1.0f);
        projection.M41 += -0.5f * projection.M11;
        projection.M42 += -0.5f * projection.M22;

        // Row-vector convention (matches every other effect's own World*View*Projection
        // ordering established throughout this renderer): the caller's own transform is applied
        // FIRST, then the projection.
        return transform_ * projection;
    }

    void D3D9SpriteBatchRenderer::FlushBatch()
    {
        if (pendingVertices_.empty()) return;

        owner_->EnsureRenderReadyEXT();

        EnsureShadersEXT();
        EnsureVertexDeclarationEXT();

        float vpW = 0.0f, vpH = 0.0f;
        GetCurrentViewportSizeEXT(vpW, vpH);

        // D9-112: a custom Effect (SpriteBatch::Begin(effect)) replaces Microsoft's own
        // SpriteEffect shaders entirely for this flush -- draws the SAME stride-24 SpriteVertex
        // data (position/color/texcoord), since D3D9's vertex declaration is a device state
        // decoupled from shader compilation, not baked into the shader like D3D11's InputLayout
        // (D9-111's own design note) -- no separate declaration/layout needed for this path.
        //
        // Unlike the stock path's real XNA "MatrixTransform" register, a custom shader receives
        // the current viewport size via a "vpSize" uniform instead (mirrors
        // D3D11SpriteBatchRenderer's own established, CNAEXT, documented CNA convention for this
        // mechanism -- SetViewportSizeEXT() there, the generic per-name SetUniformVec2() here,
        // since D3D9EffectRenderer's name-based lookup makes a dedicated method unnecessary): the
        // custom vertex shader is expected to build its own 2D->clip-space transform from
        // viewport width/height, the same responsibility D3D11's own custom-shader contract
        // already assigns to the shader author, for cross-renderer consistency.
        D3D9EffectRenderer* customRenderer = nullptr;
        if (customEffect_)
            customRenderer = dynamic_cast<D3D9EffectRenderer*>(customEffect_->GetEffectRendererPtr());

        if (customRenderer && customRenderer->IsValid())
        {
            customRenderer->SetUniformVec2("vpSize", vpW, vpH);
            customEffect_->Apply();
            customRenderer->Bind();
        }
        else
        {
            const Matrix matrixTransform = BuildMatrixTransformEXT(vpW, vpH);

            device_->SetVertexShader(vertexShader_.Get());
            device_->SetPixelShader(pixelShader_.Get());

            const Matrix transposed = Matrix::Transpose(matrixTransform);
            float regs[16];
            transposed.ToColumnMajor(regs);
            UploadVertexShaderConstantEXT(device_.Get(), Shaders::kSpriteEffect_SpriteVertexShader_Registers,
                                          static_cast<int>(std::size(Shaders::kSpriteEffect_SpriteVertexShader_Registers)),
                                          "MatrixTransform", regs);
        }

        device_->SetTexture(0, ResolveD3D9TextureEXT(currentTexture_));

        owner_->ApplySamplerState(0, pendingFilter_, pendingAddressU_, pendingAddressV_, 1);

        device_->SetVertexDeclaration(vertexDecl_.Get());

        vb_.SetData(pendingVertices_.data(), static_cast<int>(pendingVertices_.size()), sizeof(SpriteVertex));
        ib_.SetData16(pendingIndices_.data(), static_cast<int>(pendingIndices_.size()));

        device_->SetStreamSource(0, vb_.GetBufferEXT(), 0, static_cast<UINT>(sizeof(SpriteVertex)));
        device_->SetIndices(ib_.GetBufferEXT());
        device_->DrawIndexedPrimitive(ToD3D9Topology(), 0, 0,
                                      static_cast<UINT>(pendingVertices_.size()),
                                      0, static_cast<UINT>(pendingIndices_.size() / 3));

        pendingVertices_.clear();
        pendingIndices_.clear();
        currentTexture_ = nullptr;
    }

    void D3D9SpriteBatchRenderer::Draw(const ITextureRenderer& texture, float x, float y)
    {
        const int w = texture.GetWidth();
        const int h = texture.GetHeight();
        Draw(texture, Rectangle(static_cast<int>(x), static_cast<int>(y), w, h), Rectangle(0, 0, w, h),
             Color::White);
    }

    void D3D9SpriteBatchRenderer::Draw(const ITextureRenderer& texture,
                                      const Rectangle& destinationRectangle,
                                      const Rectangle& sourceRectangle,
                                      const Color& color)
    {
        Draw(texture, destinationRectangle, sourceRectangle, color, 0.0f, Vector2(0, 0), SpriteEffects::None, 0.0f);
    }

    void D3D9SpriteBatchRenderer::Draw(const ITextureRenderer& texture,
                                      const Rectangle& destinationRectangle,
                                      const Rectangle& sourceRectangle,
                                      const Color& color,
                                      float rotation,
                                      const Vector2& origin,
                                      SpriteEffects effects,
                                      float layerDepth)
    {
        if (!begun_) throw std::runtime_error("D3D9SpriteBatchRenderer::Draw called before Begin()");

        if (currentTexture_ != nullptr && currentTexture_ != &texture)
            FlushBatch();
        currentTexture_ = &texture;

        const float texW = static_cast<float>(texture.GetWidth());
        const float texH = static_cast<float>(texture.GetHeight());

        // No [0,1] clamp -- matches FNA (SpriteBatch.cs divides straight through). A
        // sourceRectangle extending past the texture bounds intentionally produces UVs outside
        // [0,1], letting the bound sampler's TextureAddressMode (D9-92) govern edge sampling.
        float u1 = static_cast<float>(sourceRectangle.X) / texW;
        float v1 = static_cast<float>(sourceRectangle.Y) / texH;
        float u2 = static_cast<float>(sourceRectangle.X + sourceRectangle.Width)  / texW;
        float v2 = static_cast<float>(sourceRectangle.Y + sourceRectangle.Height) / texH;

        if (static_cast<int>(effects) & static_cast<int>(SpriteEffects::FlipHorizontally)) std::swap(u1, u2);
        if (static_cast<int>(effects) & static_cast<int>(SpriteEffects::FlipVertically)) std::swap(v1, v2);

        const auto r = static_cast<std::uint8_t>(color.getRProperty());
        const auto g = static_cast<std::uint8_t>(color.getGProperty());
        const auto b = static_cast<std::uint8_t>(color.getBProperty());
        const auto a = static_cast<std::uint8_t>(color.getAProperty());

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

        const auto base = static_cast<std::uint16_t>(pendingVertices_.size());

        pendingVertices_.push_back({v0x, v0y, layerDepth, r, g, b, a, u1, v1});
        pendingVertices_.push_back({v1x, v1y, layerDepth, r, g, b, a, u2, v1});
        pendingVertices_.push_back({v2x, v2y, layerDepth, r, g, b, a, u2, v2});
        pendingVertices_.push_back({v3x, v3y, layerDepth, r, g, b, a, u1, v2});

        pendingIndices_.push_back(base + 0);
        pendingIndices_.push_back(base + 1);
        pendingIndices_.push_back(base + 2);
        pendingIndices_.push_back(base + 2);
        pendingIndices_.push_back(base + 3);
        pendingIndices_.push_back(base + 0);
    }
}
