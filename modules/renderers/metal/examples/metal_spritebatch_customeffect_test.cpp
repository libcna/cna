// SPDX-License-Identifier: MS-PL
// plans/plan_metal.md Phase 14 (METAL-142-152): SpriteBatch::Begin(effect) wiring, real runtime-compiled
// MSL. Mirrors D3D9's own directx9_spritebatch_customeffect_test.cpp exactly (same 4 checks, same
// deliberate RGB-inversion methodology), adapted to Metal's own fixed vertex/uniform contract (see
// docs/metal-shader-effect-contract.md and MetalEffectRenderer's own header comment in
// MetalRenderer.mm) rather than D3D9's stride-24 SpriteVertex/HLSL shape.
//
// Check A -- ShaderEffect compiles for real (two separate MSL libraries, one function each).
// Check B -- SpriteBatch::Begin(..., &invertEffect) draws sprites through the custom shader
//   instead of the stock Sprite2D pipeline: a known red texel comes out cyan (RGB-inverted), with
//   alpha forced to 255 by the custom fragment shader's own uColor.a -- an exact, unambiguous
//   8-bit round-trip only possible if the CUSTOM shader's own math ran, not the stock one. Also
//   exercises SetUniformVec4()'s real wiring to buffer(3): uColor is set to (1,1,1,1) as an
//   identity multiplier -- if the uniform buffer wiring were broken (uColor silently reading its
//   zero-initialized default instead), the result would be pure black, not cyan, a clearly
//   distinguishable failure.
// Check C -- the vpSize-driven vertex transform is genuinely positioned correctly: sampling
//   OUTSIDE the sprite's destination rectangle stays the Clear() background, proving the custom
//   vertex shader's own NDC math (fed by the automatic U2D buffer(1)) places geometry at the real,
//   correct screen location.
// Check D -- a fresh batch with no custom effect restores the stock pipeline, drawing the
//   ORIGINAL, non-inverted color again -- proving SetCustomEffect(nullptr) genuinely restores the
//   stock Sprite2D path rather than leaving the custom shader stuck bound.
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

    // Matches MetalEffectRenderer's own documented fixed contract exactly (see this class's header
    // comment in MetalRenderer.mm / docs/metal-shader-effect-contract.md): buffer(0) vertex
    // data (V2In, 32 bytes: float2 position, float2 uv, float4 color), buffer(1) the automatic
    // letterbox-aware U2D transform. No fixed entry-point name required -- each source declares
    // exactly one function, found via MTLLibrary.functionNames.
    const char* kVertexShaderSrc = R"(
#include <metal_stdlib>
using namespace metal;

struct V2In { float2 position; float2 uv; float4 color; };
struct U2D { float2 scale; float2 offset; };
struct V2Out { float4 position [[position]]; float2 uv; float4 color; };

vertex V2Out invertVertexMain(uint vid [[vertex_id]],
                               const device V2In* v [[buffer(0)]],
                               constant U2D& u [[buffer(1)]])
{
    V2In i = v[vid];
    V2Out o;
    float2 ndc = i.position * u.scale + u.offset;
    o.position = float4(ndc, 0.0, 1.0);
    o.uv = i.uv;
    o.color = i.color;
    return o;
}
)";

    // buffer(3) = uColor (float4), the fixed slot SetUniformVec4()/SetUniformVec3()/
    // SetUniformVec2() all write into (see MetalEffectRenderer's own header comment).
    const char* kFragmentShaderSrc = R"(
#include <metal_stdlib>
using namespace metal;

struct V2Out { float4 position [[position]]; float2 uv; float4 color; };

fragment float4 invertFragmentMain(V2Out in [[stage_in]],
                                    texture2d<float> tex [[texture(0)]],
                                    sampler smp [[sampler(0)]],
                                    constant float4& uColor [[buffer(3)]])
{
    float4 texColor = tex.sample(smp, in.uv);
    return float4((1.0 - texColor.rgb) * uColor.rgb, uColor.a);
}
)";
}

class MetalSpriteBatchCustomEffectTest : public Game
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
        ShaderEffect invertEffect(dev, kVertexShaderSrc, kFragmentShaderSrc);
        check(invertEffect.IsEffectValid(),
              "ShaderEffect (Metal): a runtime-compiled custom MSL vertex+fragment pair for "
              "SpriteBatch's own fixed Sprite2D-shaped vertex contract compiles successfully "
              "(plans/plan_metal.md METAL-144)");
        invertEffect.SetUniformVec4(nullptr, 1.0f, 1.0f, 1.0f, 1.0f);

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
            // Red (255,0,0) inverted RGB = (0,255,255); alpha forced to 255 via uColor.a (set to
            // 1.0 above) -- only possible if the custom shader's own math AND its uColor uniform
            // buffer wiring both ran correctly, not the stock (non-inverting) Sprite2D pipeline.
            check(insideColor.getRProperty() == 0 && insideColor.getGProperty() == 255 &&
                  insideColor.getBProperty() == 255 && insideColor.getAProperty() == 255,
                  "MetalSpriteBatch + SpriteBatch::Begin(effect): sprites draw through the custom "
                  "shader's own RGB-inversion math and its real buffer(3) uColor uniform, not the "
                  "stock (non-inverting) Sprite2D pipeline");

            const Rectangle outside(10, 10, 1, 1);
            Color outsideColor(0, 0, 0, 0);
            dev.GetBackBufferData(&outside, &outsideColor, 0, 1);
            check(outsideColor.getRProperty() == kClear.getRProperty() &&
                  outsideColor.getGProperty() == kClear.getGProperty() &&
                  outsideColor.getBProperty() == kClear.getBProperty(),
                  "MetalSpriteBatch + SpriteBatch::Begin(effect): the custom vertex shader's own "
                  "U2D-driven NDC transform genuinely positions the sprite -- background outside "
                  "its destination rectangle is untouched, not a degenerate full-screen or "
                  "mispositioned quad");
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
                  "MetalSpriteBatch: a fresh SpriteBatch::Begin() with no custom effect genuinely "
                  "restores the stock (non-inverting) Sprite2D pipeline -- the custom shader does "
                  "not stay stuck bound from the previous batch");
        }

        std::printf("=== %d/%d PASS ===\n", passCount, totalCount);
        Exit();
    }

public:
    MetalSpriteBatchCustomEffectTest()
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
        MetalSpriteBatchCustomEffectTest game;
        game.Run();
    }

    std::printf("=== %d/%d PASS (total) ===\n", passCount, totalCount);
    return (passCount == totalCount) ? 0 : 1;
}
