// SPDX-License-Identifier: MS-PL
// DX-243 keeps this smoke test focused on D3D11-native integration that the renderer-neutral
// parity corpus cannot observe: native cache identity/keying and actual immediate-context binds.
// Public pixels, resources, effects, queries, presentation, and state semantics are covered by
// independent fixtures registered through DirectXParityTests.cmake.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"

#include "CNA/Internal/Renderers/DirectX11/DirectX11Renderer.hpp"
#include "CNA/Internal/Renderers/DirectX11/D3D11SamplerCache.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace CNA::Internal::Renderers::DirectX11;
using CNA::Internal::Renderers::BlendWriteState;
using CNA::Internal::Renderers::D3DCommon::D3DShaderVariant;

class D3D11SmokeTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> graphicsDeviceManager_;
    int checks_ = 0;
    int failures_ = 0;
    int frame_ = 0;

    void Check(bool condition, const char* label)
    {
        ++checks_;
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label);
        if (!condition)
            ++failures_;
    }

protected:
    void Draw(const GameTime&) override
    {
        if (frame_++ < 1)
            return;

        auto& renderer = static_cast<DirectX11Renderer&>(
            getGraphicsDeviceProperty().GetRenderer());
        ID3D11Device* device = renderer.GetDeviceEXT();
        ID3D11DeviceContext* context = renderer.GetContextEXT();

        // Family A: native device and input-layout cache.
        Check(device != nullptr && context != nullptr,
              "A1: native D3D11 device and immediate context exist");
        Check(renderer.GetFeatureLevelEXT() >= D3D_FEATURE_LEVEL_11_0,
              "A2: negotiated feature level is 11_0 or higher");

        auto& inputLayouts = renderer.GetInputLayoutCacheEXT();
        auto colored16 = inputLayouts.GetOrCreate(device, D3DShaderVariant::Colored3d, 16);
        auto colored16Again = inputLayouts.GetOrCreate(device, D3DShaderVariant::Colored3d, 16);
        auto textured20 = inputLayouts.GetOrCreate(device, D3DShaderVariant::Textured3d, 20);
        auto invalid17 = inputLayouts.GetOrCreate(device, D3DShaderVariant::Colored3d, 17);
        Check(colored16 != nullptr && colored16.Get() == colored16Again.Get(),
              "A3: identical input-layout key returns the same native object");
        Check(textured20 != nullptr && textured20.Get() != colored16.Get(),
              "A4: different shader/stride input-layout key returns a distinct object");
        Check(invalid17 == nullptr,
              "A5: unsupported input-layout stride is rejected instead of cached");

        // Family B: sampler cache fields that are easy to omit from a hand-built key.
        D3D11SamplerCache samplers;
        auto samplerBase = samplers.GetOrCreate(device, 0, 0, 0, 1, 0, 0, 0.0f);
        auto samplerBaseAgain = samplers.GetOrCreate(device, 0, 0, 0, 1, 0, 0, 0.0f);
        auto samplerW = samplers.GetOrCreate(device, 0, 0, 0, 1, 2, 0, 0.0f);
        auto samplerMip = samplers.GetOrCreate(device, 0, 0, 0, 1, 0, 2, 0.0f);
        auto samplerBias = samplers.GetOrCreate(device, 0, 0, 0, 1, 0, 0, -1.5f);
        Check(samplerBase != nullptr && samplerBase.Get() == samplerBaseAgain.Get(),
              "B1: identical sampler key returns the same native object");
        Check(samplerW.Get() != samplerBase.Get() && samplerMip.Get() != samplerBase.Get() &&
                  samplerBias.Get() != samplerBase.Get() && samplers.GetCacheSizeEXT() == 4,
              "B2: AddressW, MaxMipLevel, and LOD bias independently key sampler objects");

        D3D11_SAMPLER_DESC samplerWDesc{};
        D3D11_SAMPLER_DESC samplerMipDesc{};
        D3D11_SAMPLER_DESC samplerBiasDesc{};
        samplerW->GetDesc(&samplerWDesc);
        samplerMip->GetDesc(&samplerMipDesc);
        samplerBias->GetDesc(&samplerBiasDesc);
        Check(samplerWDesc.AddressW == D3D11_TEXTURE_ADDRESS_MIRROR,
              "B3: AddressW reaches the native sampler descriptor");
        Check(samplerMipDesc.MinLOD == 2.0f && samplerMipDesc.MaxLOD == D3D11_FLOAT32_MAX,
              "B4: XNA MaxMipLevel maps to native MinLOD");
        Check(samplerBiasDesc.MipLODBias == -1.5f,
              "B5: XNA mip LOD bias reaches the native descriptor");

        // Family C: native state-object cache identity and dynamic binding.
        auto& blendCache = renderer.GetBlendStateCacheEXT();
        auto blendA = blendCache.GetOrCreate(device, 4, 4, 5, 5, 0, 0, 15, 15, 15, 15);
        auto blendAAgain = blendCache.GetOrCreate(device, 4, 4, 5, 5, 0, 0, 15, 15, 15, 15);
        auto blendB = blendCache.GetOrCreate(device, 0, 0, 1, 1, 0, 0, 15, 15, 15, 15);
        Check(blendA != nullptr && blendA.Get() == blendAAgain.Get(),
              "C1: identical blend-state key returns the same native object");
        Check(blendB != nullptr && blendB.Get() != blendA.Get(),
              "C2: different blend-state key returns a distinct object");

        renderer.ApplyBlendState(4, 4, 5, 5, 0, 0, BlendWriteState{});
        renderer.SetBlendFactor(0.25f, 0.5f, 0.75f, 1.0f);
        ID3D11BlendState* boundBlend = nullptr;
        float blendFactor[4]{};
        UINT sampleMask = 0;
        context->OMGetBlendState(&boundBlend, blendFactor, &sampleMask);
        Check(boundBlend == blendA.Get() && blendFactor[0] == 0.25f &&
                  blendFactor[1] == 0.5f && blendFactor[2] == 0.75f &&
                  blendFactor[3] == 1.0f,
              "C3: blend object and dynamic factor are bound on the immediate context");
        if (boundBlend != nullptr)
            boundBlend->Release();

        auto& depthCache = renderer.GetDepthStencilStateCacheEXT();
        auto depthA = depthCache.GetOrCreate(device, true, true, 2, true, 0, 2, 0, 0,
                                             0xFF, 0xFF, false, 0, 0, 0, 0);
        auto depthAAgain = depthCache.GetOrCreate(device, true, true, 2, true, 0, 2, 0, 0,
                                                  0xFF, 0xFF, false, 0, 0, 0, 0);
        auto depthB = depthCache.GetOrCreate(device, false, false, 0, false, 0, 0, 0, 0,
                                             0xFF, 0xFF, false, 0, 0, 0, 0);
        Check(depthA != nullptr && depthA.Get() == depthAAgain.Get(),
              "C4: identical depth-stencil key returns the same native object");
        Check(depthB != nullptr && depthB.Get() != depthA.Get(),
              "C5: different depth-stencil key returns a distinct object");

        renderer.ApplyDepthStencilState(true, true, 2, true, 0, 2, 0, 0,
                                        0xFF, 0xFF, 77, false, 0, 0, 0, 0);
        renderer.SetReferenceStencil(123);
        ID3D11DepthStencilState* boundDepth = nullptr;
        UINT stencilReference = 0;
        context->OMGetDepthStencilState(&boundDepth, &stencilReference);
        Check(boundDepth == depthA.Get() && stencilReference == 123,
              "C6: depth-stencil object and dynamic reference are bound on the context");
        if (boundDepth != nullptr)
            boundDepth->Release();

        auto& rasterizerCache = renderer.GetRasterizerStateCacheEXT();
        auto rasterizerA = rasterizerCache.GetOrCreate(device, 2, 0, false, 0, 0.0f);
        auto rasterizerAAgain = rasterizerCache.GetOrCreate(device, 2, 0, false, 0, 0.0f);
        auto rasterizerB = rasterizerCache.GetOrCreate(device, 0, 1, true, 1, 2.0f);
        Check(rasterizerA != nullptr && rasterizerA.Get() == rasterizerAAgain.Get(),
              "C7: identical rasterizer key returns the same native object");
        Check(rasterizerB != nullptr && rasterizerB.Get() != rasterizerA.Get(),
              "C8: different rasterizer key returns a distinct object");

        renderer.ApplyRasterizerState(2, 0, false, 0.0f, 0.0f);
        ID3D11RasterizerState* boundRasterizer = nullptr;
        context->RSGetState(&boundRasterizer);
        Check(boundRasterizer == rasterizerA.Get(),
              "C9: rasterizer object is bound on the immediate context");
        if (boundRasterizer != nullptr)
            boundRasterizer->Release();

        renderer.SetViewport(2, 3, 40, 30, 0.1f, 0.9f);
        UINT viewportCount = 1;
        D3D11_VIEWPORT viewport{};
        context->RSGetViewports(&viewportCount, &viewport);
        Check(viewportCount == 1 && viewport.TopLeftX == 2.0f && viewport.TopLeftY == 3.0f &&
                  viewport.Width == 40.0f && viewport.Height == 30.0f &&
                  viewport.MinDepth == 0.1f && viewport.MaxDepth == 0.9f,
              "C10: viewport binding round-trips through the native context");

        renderer.SetScissorRect(5, 6, 20, 10);
        UINT rectCount = 1;
        D3D11_RECT rect{};
        context->RSGetScissorRects(&rectCount, &rect);
        Check(rectCount == 1 && rect.left == 5 && rect.top == 6 &&
                  rect.right == 25 && rect.bottom == 16,
              "C11: scissor binding round-trips through the native context");

        std::printf("=== %d/%d PASS ===\n", checks_ - failures_, checks_);
        Exit();
    }

public:
    D3D11SmokeTest()
    {
        graphicsDeviceManager_ = std::make_unique<GraphicsDeviceManager>(this);
        graphicsDeviceManager_->setPreferredBackBufferWidthProperty(64);
        graphicsDeviceManager_->setPreferredBackBufferHeightProperty(64);
    }

    [[nodiscard]] int GetResult() const
    {
        return failures_ == 0 ? 0 : 1;
    }
};

int main()
{
    D3D11SmokeTest game;
    game.Run();
    return game.GetResult();
}
