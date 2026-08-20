// plans/plan_dx.md Phase DX12 (DX-111/DX-112 follow-up).
#include "CNA/Internal/Renderers/DirectX12/D3D12SpriteBatch.hpp"
#include "CNA/Internal/Renderers/DirectX12/DirectX12Renderer.hpp"
#include "CNA/Internal/Renderers/DirectX12/D3D12Textures.hpp"
#include "CNA/Internal/Renderers/DirectX12/D3D12EffectRenderer.hpp"
#include "CNA/Internal/Renderers/D3DCommon/D3DShaderCache.hpp"
#include "CNA/Internal/Renderers/D3DCommon/D3DConstantBuffers.hpp"

#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"

#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

namespace CNA::Internal::Renderers::DirectX12
{
    using namespace CNA::Internal::Renderers::D3DCommon;
    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Rectangle;
    using Microsoft::Xna::Framework::Vector2;
    using Microsoft::Xna::Framework::Graphics::Effect;

    namespace
    {
        std::string FormatHr(HRESULT hr)
        {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "0x%08lX", static_cast<unsigned long>(hr));
            return buf;
        }

        /// Same single-concrete-type SRV resolution as DirectX12Renderer.cpp's own (private,
        /// not exported) GetSrvGpuHandleForTextureEXT -- duplicated rather than factored into a
        /// shared header for a few lines of logic, matching D3D11SpriteBatch.cpp's own established
        /// precedent of duplicating GetSrvForTextureEXT locally rather than sharing it.
        D3D12_GPU_DESCRIPTOR_HANDLE GetSrvGpuHandle(const ITextureRenderer* tex)
        {
            D3D12_GPU_DESCRIPTOR_HANDLE handle{};
            if (tex == nullptr) return handle;
            if (const auto* t = dynamic_cast<const D3D12TextureRenderer*>(tex))
                return t->GetShaderResourceViewGpuHandleEXT();
            return handle;
        }

        D3D12_PRIMITIVE_TOPOLOGY_TYPE kSpriteTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    }

    D3D12SpriteBatchRenderer::D3D12SpriteBatchRenderer(DirectX12Renderer* owner)
        : owner_(owner)
        , device_(owner_->GetDeviceEXT())
        , vb_(owner_, 256)
        , ib_(owner_, 384, /*thirtyTwoBit=*/false)
    {
    }

    void D3D12SpriteBatchRenderer::Begin()
    {
        if (begun_) return;
        begun_ = true;
    }

    void D3D12SpriteBatchRenderer::End()
    {
        FlushBatch();
        begun_ = false;
    }

    void D3D12SpriteBatchRenderer::SetTransformMatrix(const Matrix& m)
    {
        transform_ = m;
    }

    void D3D12SpriteBatchRenderer::SetCustomEffect(Effect* effect)
    {
        if (customEffect_ != effect)
        {
            FlushBatch();
            customEffect_ = effect;
        }
    }

    void D3D12SpriteBatchRenderer::SetSamplerFilter(int textureFilter)
    {
        pendingFilter_ = textureFilter;
    }

    void D3D12SpriteBatchRenderer::SetSamplerAddressMode(int addressU, int addressV)
    {
        pendingAddressU_ = addressU;
        pendingAddressV_ = addressV;
    }

    ID3D12PipelineState* D3D12SpriteBatchRenderer::GetOrCreateSprite2DPso(ID3D12RootSignature* rootSig)
    {
        if (sprite2DPso_)
            return sprite2DPso_.Get();

        const uint8_t* vsBytes = nullptr; std::size_t vsSize = 0;
        const uint8_t* psBytes = nullptr; std::size_t psSize = 0;
        GetVertexShaderBytecode(D3DShaderVariant::Sprite2d, vsBytes, vsSize);
        GetPixelShaderBytecode(D3DShaderVariant::Sprite2d, psBytes, psSize);
        if (!vsBytes || !psBytes)
            throw std::runtime_error("D3D12SpriteBatchRenderer: missing sprite2d DXBC bytecode");

        // Fixed Sprite2DVertex contract -- deliberately NOT resolved via
        // D3DVertexFormatHelper::InputElementsForStrideD3D12(32, ...), which would incorrectly
        // return VertexPositionNormalTexture's layout (this file's own header comment explains the
        // real stride-32 collision this sidesteps, matching D3D11's own DX-70 precedent).
        static const D3D12_INPUT_ELEMENT_DESC kElements[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 8,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
        desc.pRootSignature = rootSig;
        desc.VS = {vsBytes, vsSize};
        desc.PS = {psBytes, psSize};
        desc.InputLayout = {kElements, static_cast<UINT>(std::size(kElements))};
        desc.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
        desc.PrimitiveTopologyType = kSpriteTopologyType;
        desc.SampleMask = UINT_MAX;
        desc.SampleDesc.Count = 1;
        desc.NodeMask = 0;

        // Same explicit-hardcoded-defaults simplification every other D3D12 draw uses today (no
        // D3D12 Phase-DX7-equivalent state-object cache exists yet -- this file's own header
        // comment documents the honest blend/sampler gap this implies for SpriteBatch specifically).
        desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        desc.RasterizerState.FrontCounterClockwise = FALSE;
        desc.RasterizerState.DepthClipEnable = TRUE;

        D3D12_RENDER_TARGET_BLEND_DESC& rt0 = desc.BlendState.RenderTarget[0];
        rt0.BlendEnable = FALSE;
        rt0.SrcBlend = D3D12_BLEND_ONE;
        rt0.DestBlend = D3D12_BLEND_ZERO;
        rt0.BlendOp = D3D12_BLEND_OP_ADD;
        rt0.SrcBlendAlpha = D3D12_BLEND_ONE;
        rt0.DestBlendAlpha = D3D12_BLEND_ZERO;
        rt0.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        rt0.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

        desc.DepthStencilState.DepthEnable = FALSE;
        desc.DepthStencilState.StencilEnable = FALSE;

        desc.NumRenderTargets = 1;
        desc.RTVFormats[0] = owner_->GetBoundColorFormatEXT();
        desc.DSVFormat = DXGI_FORMAT_UNKNOWN;

        HRESULT hr = device_->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(sprite2DPso_.ReleaseAndGetAddressOf()));
        if (FAILED(hr))
            throw std::runtime_error("D3D12SpriteBatchRenderer: CreateGraphicsPipelineState failed, hr=" + FormatHr(hr));
        return sprite2DPso_.Get();
    }

    ID3D12Resource* D3D12SpriteBatchRenderer::GetOrCreatePerDrawConstantBuffer()
    {
        if (perDrawConstantBuffer_)
            return perDrawConstantBuffer_.Get();

        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = sizeof(D3DSprite2DConstants);
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        HRESULT hr = device_->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(perDrawConstantBuffer_.ReleaseAndGetAddressOf()));
        if (FAILED(hr))
            throw std::runtime_error("D3D12SpriteBatchRenderer: constant buffer CreateCommittedResource failed, hr=" + FormatHr(hr));

        const D3D12_RANGE readRange{0, 0};
        hr = perDrawConstantBuffer_->Map(0, &readRange, &perDrawConstantBufferMapped_);
        if (FAILED(hr))
            throw std::runtime_error("D3D12SpriteBatchRenderer: constant buffer Map failed, hr=" + FormatHr(hr));
        return perDrawConstantBuffer_.Get();
    }

    void D3D12SpriteBatchRenderer::FlushBatch()
    {
        if (pendingVertices_.empty()) return;

        if (!owner_->HasBoundColorTargetEXT())
        {
            throw std::runtime_error(
                "D3D12SpriteBatchRenderer::FlushBatch: no off-screen color target bound -- "
                "BindOffscreenColorTargetEXT (plans/plan_dx.md DX-111's own scaffolding; a real D3D12 swap "
                "chain back buffer is unusable under this machine's Wine+vkd3d-proton dev loop, DX-100)");
        }

        const int targetW = owner_->GetBoundColorWidthEXT();
        const int targetH = owner_->GetBoundColorHeightEXT();

        // REMED-GFX-072: the sprite2d ViewportSize projection basis (the pixel->NDC divisor) must be
        // the ACTIVE GraphicsDevice.Viewport's width/height, not the full bound-target size. XNA/FNA
        // build the SpriteBatch ortho from Viewport.Width/Height (CreateOrthographicOffCenter(0,
        // Viewport.Width, Viewport.Height, 0)), so a custom sub-Viewport makes sprite coordinates
        // VIEWPORT-LOCAL. This mirrors D3D11SpriteBatchRenderer, which reads the live viewport size via
        // RSGetViewports. GetEffectiveViewportEXT() (REMED-GFX-064) already returns the custom
        // sub-region when a Viewport was set (else the full target), and it also drives the rasterizer
        // viewport below (RSSetViewports) -- previously only the rasterizer honored it while the
        // projection stayed full-target, squishing the sprite into the sub-region. The default
        // full-target viewport keeps vpW/vpH == the target size, byte-identical to the prior behavior.
        const D3D12_VIEWPORT effectiveViewport = owner_->GetEffectiveViewportEXT();
        const int vpW = static_cast<int>(effectiveViewport.Width);
        const int vpH = static_cast<int>(effectiveViewport.Height);

        auto rootSig = owner_->GetRootSignatureCacheEXT().GetOrCreate(device_.Get(), /*numCbvs=*/1, /*numSrvs=*/1, /*numSamplers=*/1);
        if (!rootSig)
            throw std::runtime_error("D3D12SpriteBatchRenderer: failed to create sprite2d root signature");

        // DX-121: a valid custom Effect (SpriteBatch::Begin(effect)) draws through its own
        // real, compiled PSO+constant-buffer instead of the stock sprite2d pipeline -- both share
        // the exact same (1,1,1) root signature above (D3D12RootSignatureCache caches by shape),
        // so every root-signature-relative binding below (CBV@0/SRV table@1/sampler table@2) stays
        // correct regardless of which path supplied pso/cb.
        D3D12EffectRenderer* customRenderer = nullptr;
        if (customEffect_)
            customRenderer = dynamic_cast<D3D12EffectRenderer*>(customEffect_->GetEffectRendererPtr());

        ID3D12PipelineState* pso = nullptr;
        ID3D12Resource* cb = nullptr;

        if (customRenderer && customRenderer->IsValid())
        {
            // DX-121: vpSize is set here, once per flush, mirroring D3D11EffectRenderer's own
            // "set automatically by the sprite-batch runtime" convention -- the game/effect
            // author never calls SetViewportSizeEXT() itself.
            customRenderer->SetViewportSizeEXT(static_cast<float>(vpW), static_cast<float>(vpH));
            customEffect_->Apply();
            customRenderer->Bind();
            pso = customRenderer->GetPipelineStateEXT();
            cb = customRenderer->GetConstantBufferEXT();
            if (!pso || !cb)
                throw std::runtime_error("D3D12SpriteBatchRenderer: custom Effect's D3D12EffectRenderer is not valid");
        }
        else
        {
            pso = GetOrCreateSprite2DPso(rootSig.Get());
            D3DSprite2DConstants c{};
            c.ViewportSize[0] = static_cast<float>(vpW);
            c.ViewportSize[1] = static_cast<float>(vpH);
            cb = GetOrCreatePerDrawConstantBuffer();
            std::memcpy(perDrawConstantBufferMapped_, &c, sizeof(c));
        }

        vb_.SetData(pendingVertices_.data(), static_cast<int>(pendingVertices_.size()), sizeof(Sprite2DVertex));
        ib_.SetData16(pendingIndices_.data(), static_cast<int>(pendingIndices_.size()));

        ID3D12CommandAllocator* allocator = owner_->GetCommandAllocatorEXT(0);
        ID3D12GraphicsCommandList* cmdList = owner_->GetCommandListEXT();
        allocator->Reset();
        cmdList->Reset(allocator, pso);

        owner_->GetResourceStateTrackerEXT().TransitionTo(cmdList, owner_->GetBoundColorResourceEXT(),
                                                           D3D12_RESOURCE_STATE_RENDER_TARGET);
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = owner_->GetBoundColorRtvEXT();
        cmdList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

        // REMED-GFX-064: honor a custom GraphicsDevice.Viewport for sprite draws (the GPU viewport
        // rectangle). REMED-GFX-072: the SAME effective viewport now also drives the ViewportSize
        // projection basis above, so sprite coordinates are viewport-relative. The scissor stays at
        // the full bound target (sprites are clipped to the viewport by the NDC->viewport transform,
        // matching D3D11's sprite path which sets no viewport-specific scissor).
        D3D12_RECT scissor{0, 0, targetW, targetH};
        cmdList->RSSetViewports(1, &effectiveViewport);
        cmdList->RSSetScissorRects(1, &scissor);

        cmdList->SetGraphicsRootSignature(rootSig.Get());
        cmdList->SetPipelineState(pso);
        cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        D3D12_VERTEX_BUFFER_VIEW vbView = vb_.GetViewEXT();
        cmdList->IASetVertexBuffers(0, 1, &vbView);
        D3D12_INDEX_BUFFER_VIEW ibView = ib_.GetViewEXT();
        cmdList->IASetIndexBuffer(&ibView);

        cmdList->SetGraphicsRootConstantBufferView(0, cb->GetGPUVirtualAddress());

        // DX-133: pendingFilter_/pendingAddressU_/pendingAddressV_ (set via SetSamplerFilter()/
        // SetSamplerAddressMode(), i.e. the SamplerState passed to SpriteBatch::Begin()) now
        // genuinely drive slot 0's real sampler descriptor via DX-119's dynamic sampler system --
        // mirrors D3D11SpriteBatchRenderer's own exact ApplySamplerState() call, same placement
        // (right before the sampler handle is read below). Before this task, SpriteBatch draws
        // silently used whatever slot 0's sampler happened to already be (or DirectX12Renderer's
        // own pre-DX-119 LINEAR/WRAP fallback if nothing had ever called ApplySamplerState) instead
        // of the SamplerState the game's own SpriteBatch::Begin() call actually requested.
        owner_->ApplySamplerState(0, pendingFilter_, pendingAddressU_, pendingAddressV_, 1);

        // DX-119: SpriteBatch always samples XNA texture slot 0 (GraphicsDevice.Textures[0]/
        // SamplerStates[0]) -- real, runtime-settable SamplerState, not a hardcoded default,
        // bound at root parameter index 2 (root sig shape (1,1,1): CBV@0, SRV table@1, sampler
        // table@2, D3D12RootSignatureCache's own real parameter ordering).
        ID3D12DescriptorHeap* heaps[] = {owner_->GetCbvSrvUavHeapEXT(), owner_->GetSamplerHeapEXT()};
        cmdList->SetDescriptorHeaps(2, heaps);
        cmdList->SetGraphicsRootDescriptorTable(1, GetSrvGpuHandle(currentTexture_));
        cmdList->SetGraphicsRootDescriptorTable(2, owner_->GetSamplerGpuHandleEXT(0));

        cmdList->DrawIndexedInstanced(static_cast<UINT>(pendingIndices_.size()), 1, 0, 0, 0);

        HRESULT hr = cmdList->Close();
        if (FAILED(hr))
            throw std::runtime_error("D3D12SpriteBatchRenderer::FlushBatch: command list Close failed, hr=" + FormatHr(hr));
        owner_->ExecuteCommandListAndWaitEXT(cmdList);

        pendingVertices_.clear();
        pendingIndices_.clear();
        currentTexture_ = nullptr;
    }

    void D3D12SpriteBatchRenderer::Draw(const ITextureRenderer& texture, float x, float y)
    {
        const int w = texture.GetWidth();
        const int h = texture.GetHeight();
        Draw(texture, Rectangle(static_cast<int>(x), static_cast<int>(y), w, h), Rectangle(0, 0, w, h),
             Color::White);
    }

    void D3D12SpriteBatchRenderer::Draw(const ITextureRenderer& texture,
                                       const Rectangle& destinationRectangle,
                                       const Rectangle& sourceRectangle,
                                       const Color& color)
    {
        Draw(texture, destinationRectangle, sourceRectangle, color, 0.0f, Vector2(0, 0), SpriteEffects::None, 0.0f);
    }

    void D3D12SpriteBatchRenderer::Draw(const ITextureRenderer& texture,
                                       const Rectangle& destinationRectangle,
                                       const Rectangle& sourceRectangle,
                                       const Color& color,
                                       float rotation,
                                       const Vector2& origin,
                                       SpriteEffects effects,
                                       float /*layerDepth*/)
    {
        if (!begun_) throw std::runtime_error("D3D12SpriteBatchRenderer::Draw called before Begin()");

        if (currentTexture_ != nullptr && currentTexture_ != &texture)
            FlushBatch();
        currentTexture_ = &texture;

        const float texW = static_cast<float>(texture.GetWidth());
        const float texH = static_cast<float>(texture.GetHeight());

        // No [0,1] clamp -- matches FNA (SpriteBatch.cs divides straight through), same convention
        // D3D11SpriteBatchRenderer::Draw already established.
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

        // SpriteBatch's own transform matrix is applied here, in pixel space, before upload -- same
        // reasoning D3D11SpriteBatchRenderer's own header comment already documents (sprite2d.vert
        // .hlsl's real contract has no projection-matrix uniform to fold it into GPU-side).
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
