// SPDX-License-Identifier: MS-PL
// plans/plan_dx9.md Phase D9-11 (D9-112): SpriteBatch::Begin(effect) wiring. Mirrors D3D11's own
// DX-71 Check AA exactly (a runtime-compiled custom HLSL pair replaces the stock SpriteEffect
// shaders for the whole batch, driving a deliberate RGB color inversion), adapted to D3D9's own
// real SM2/SM3 HLSL syntax and its existing stride-24 SpriteVertex contract (POSITION0 FLOAT3,
// COLOR0 UBYTE4N, TEXCOORD0 FLOAT2) rather than D3D11's own Sprite2DVertex shape -- D9-111's own
// design note explicitly left vertex-declaration management to this task, since this renderer
// reuses the SAME declaration the stock path already has (D3D9's vertex declaration is a
// decoupled device state, not baked into the shader like D3D11's InputLayout).
//
// Check A -- ShaderEffect compiles for real (vs_2_0/ps_2_0 under Reach).
// Check B -- SpriteBatch::Begin(..., &invertEffect) draws sprites through the custom shader
//   instead of the stock SpriteEffect pipeline: a known red texel comes out cyan (RGB-inverted),
//   with alpha forced to 255 by the custom pixel shader itself -- an exact, unambiguous 8-bit
//   round-trip only possible if the CUSTOM shader's own math ran, not the stock one (which would
//   have painted the original, non-inverted red).
// Check C -- the vpSize-driven vertex transform is genuinely positioned correctly, not just
//   "some color at some place": sampling OUTSIDE the sprite's destination rectangle stays the
//   Clear() background, proving the custom vertex shader's own NDC math places geometry at the
//   real, correct screen location (not a degenerate full-screen quad or a mispositioned one).
// Check D -- switching back to the stock pipeline (SpriteBatch::Begin() with no effect, a fresh
//   batch) draws the ORIGINAL, non-inverted color again -- proving SetCustomEffect(nullptr)
//   genuinely restores the stock SpriteEffect path rather than leaving the custom shader stuck
//   bound.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"

#include <cstdio>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    int passCount = 0;
    int totalCount = 0;

    void check(bool ok, const char* label)
    {
        ++totalCount;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount;
    }

    // Matches Macros.fxh's own real DECLARE_TEXTURE/SAMPLE_TEXTURE macro expansion for SM2/SM3
    // (the exact syntax the vendored SpriteEffect.fx itself already compiles with in this
    // toolchain), so this is proven D3D9 HLSL, not a guess.
    const char* kVertexShaderSrc = R"(
float2 vpSize;

struct VSInput { float3 Position : POSITION0; float4 Color : COLOR0; float2 TexCoord : TEXCOORD0; };
struct VSOutput { float4 Position : POSITION0; float4 Color : COLOR0; float2 TexCoord : TEXCOORD0; };

VSOutput main(VSInput input)
{
    VSOutput output;
    float2 ndc = (input.Position.xy / vpSize) * 2.0 - 1.0;
    output.Position = float4(ndc.x, -ndc.y, 0.0, 1.0);
    output.Color = input.Color;
    output.TexCoord = input.TexCoord;
    return output;
}
)";

    const char* kPixelShaderSrc = R"(
texture2D Texture;
sampler TextureSampler : register(s0) = sampler_state { Texture = (Texture); };

float4 main(float4 color : COLOR0, float2 texCoord : TEXCOORD0) : COLOR0
{
    float4 texColor = tex2D(TextureSampler, texCoord);
    return float4(1.0 - texColor.rgb, 1.0);
}
)";
}

class D3D9SpriteBatchCustomEffectTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int frame_ = 0;

protected:
    void Draw(const GameTime&) override
    {
        if (frame_++ < 1) return;

        auto& dev = getGraphicsDeviceProperty();
        const Color kClear(10, 10, 10, 255);
        const Color kRed(255, 0, 0, 255);

        // Check A: compiles for real.
        ShaderEffect invertEffect(dev, kVertexShaderSrc, kPixelShaderSrc);
        check(invertEffect.IsEffectValid(),
              "ShaderEffect (D3D9): a runtime-compiled custom HLSL pair for SpriteBatch's own "
              "stride-24 SpriteVertex contract compiles successfully (plans/plan_dx9.md D9-112)");

        Texture2D tex(dev, 1, 1);
        const Color redPixel[1] = {kRed};
        tex.SetData(redPixel, 1);

        SamplerState pointClamp = SamplerState::PointClamp;

        // Check B/C: draw through the custom effect.
        {
            dev.Clear(kClear);
            SpriteBatch sb(dev);
            sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &pointClamp, nullptr, nullptr, &invertEffect);
            sb.Draw(tex, Rectangle(50, 60, 80, 40), kRed);
            sb.End();

            const Rectangle inside(80, 79, 1, 1);
            Color insideColor(0, 0, 0, 0);
            dev.GetBackBufferData(&inside, &insideColor, 0, 1);
            // Red (255,0,0) inverted RGB = (0,255,255); alpha forced to 255 by the custom pixel
            // shader itself (not inverted from the source's own 255) -- only possible if the
            // custom shader's own math ran, not the stock (non-inverting) SpriteEffect pipeline.
            check(insideColor.getRProperty() == 0 && insideColor.getGProperty() == 255 &&
                  insideColor.getBProperty() == 255 && insideColor.getAProperty() == 255,
                  "D3D9SpriteBatchRenderer + SpriteBatch::Begin(effect): sprites draw through the "
                  "custom shader's own RGB-inversion math, not the stock (non-inverting) SpriteEffect pipeline");

            const Rectangle outside(10, 10, 1, 1);
            Color outsideColor(0, 0, 0, 0);
            dev.GetBackBufferData(&outside, &outsideColor, 0, 1);
            check(outsideColor.getRProperty() == kClear.getRProperty() &&
                  outsideColor.getGProperty() == kClear.getGProperty() &&
                  outsideColor.getBProperty() == kClear.getBProperty(),
                  "D3D9SpriteBatchRenderer + SpriteBatch::Begin(effect): the custom vertex shader's own "
                  "vpSize-driven NDC transform genuinely positions the sprite -- background outside its "
                  "destination rectangle is untouched, not a degenerate full-screen or mispositioned quad");
        }

        // Check D: a fresh batch with no custom effect restores the stock pipeline.
        {
            dev.Clear(kClear);
            SpriteBatch sb(dev);
            sb.Begin();
            sb.Draw(tex, Rectangle(50, 60, 80, 40), kRed);
            sb.End();

            const Rectangle inside(80, 79, 1, 1);
            Color insideColor(0, 0, 0, 0);
            dev.GetBackBufferData(&inside, &insideColor, 0, 1);
            check(insideColor.getRProperty() == 255 && insideColor.getGProperty() == 0 &&
                  insideColor.getBProperty() == 0 && insideColor.getAProperty() == 255,
                  "D3D9SpriteBatchRenderer: a fresh SpriteBatch::Begin() with no custom effect genuinely "
                  "restores the stock (non-inverting) SpriteEffect pipeline -- the custom shader does not "
                  "stay stuck bound from the previous batch");
        }

        std::printf("=== %d/%d PASS ===\n", passCount, totalCount);
        Exit();
    }

public:
    D3D9SpriteBatchCustomEffectTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(256);
        gdm_->setPreferredBackBufferHeightProperty(256);
        gdm_->setPreferredDepthStencilFormatProperty(DepthFormat::Depth24Stencil8);
    }
};

int main()
{
    {
        D3D9SpriteBatchCustomEffectTest game;
        game.Run();
    }

    std::printf("=== %d/%d PASS (total) ===\n", passCount, totalCount);
    return (passCount == totalCount) ? 0 : 1;
}
