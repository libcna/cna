// SPDX-License-Identifier: MS-PL
// plans/plan_metal.md Phase 14 (METAL-142-152) and plans/plan_apple_m4.md AM4-077: SpriteBatch::Begin(effect)
// with runtime-compiled MSL, the evidence docs/metal-shader-effect-contract.md requires before the
// facility is supported. Registered with Metal's API and shader validation layers on, so a binding
// or shader diagnostic fails the run as well as a wrong pixel.
//
// Check A -- a vertex+fragment MSL pair compiles (two separate libraries, one function each).
// Check B -- SpriteBatch::Begin(..., &invertEffect) draws through the custom shader: a red texel
//   comes out cyan, with alpha from buffer(3) uColor.a, which a stock pipeline cannot produce and
//   a broken uniform binding (uColor reading zero) would turn black.
// Check C -- the automatic buffer(1) transform places the quad: outside it the clear colour stays.
// Check D -- buffer(2) (float4x4) and buffer(4) (float) reach the fragment stage.
// Check E -- the effect's pipeline follows the batch's BlendState: Additive over the clear colour.
// Check F -- drawing into a RenderTarget2D works and reads back through GetData.
// Check G -- an MSL source that does not compile yields an invalid effect with a message, no throw.
// Check H -- a batch with no effect restores the stock pipeline; an effect disposed after its batch
//   leaves the device usable, and a new one draws.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "CNA/GraphicsCapability.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    int passCount = 0;
    int totalCount = 0;

    void check(bool ok, const std::string& label)
    {
        ++totalCount;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        if (ok) ++passCount;
    }

    bool near(int actual, int expected)
    {
        return std::abs(actual - expected) <= 1;
    }

    bool rgba(const Color& c, int r, int g, int b, int a)
    {
        return near(c.getRProperty(), r) && near(c.getGProperty(), g) && near(c.getBProperty(), b) &&
               near(c.getAProperty(), a);
    }

    std::string describe(const Color& c)
    {
        return "(" + std::to_string(c.getRProperty()) + "," + std::to_string(c.getGProperty()) + "," +
               std::to_string(c.getBProperty()) + "," + std::to_string(c.getAProperty()) + ")";
    }

    // The fixed SpriteBatch contract docs/metal-shader-effect-contract.md documents: buffer(0) the
    // sprite vertices (float2 position, float2 uv, float4 color), buffer(1) the automatic transform.
    const char* kVertexShaderSrc = R"(
#include <metal_stdlib>
using namespace metal;

struct V2In { float2 position; float2 uv; float4 color; };
struct U2D { float2 scale; float2 offset; };
struct V2Out { float4 position [[position]]; float2 uv; float4 color; };

vertex V2Out customVertexMain(uint vid [[vertex_id]],
                              const device V2In* v [[buffer(0)]],
                              constant U2D& u [[buffer(1)]])
{
    V2In i = v[vid];
    V2Out o;
    o.position = float4(i.position * u.scale + u.offset, 0.0, 1.0);
    o.uv = i.uv;
    o.color = i.color;
    return o;
}
)";

    // buffer(3) = uColor, the slot SetUniformVec4/Vec3/Vec2 write.
    const char* kInvertFragmentSrc = R"(
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

    // buffer(2) = uMatrix (SetUniformMat4), buffer(4) = uFloat0 (SetUniformFloat/Int).
    const char* kUniformFragmentSrc = R"(
#include <metal_stdlib>
using namespace metal;

struct V2Out { float4 position [[position]]; float2 uv; float4 color; };

fragment float4 uniformFragmentMain(V2Out in [[stage_in]],
                                    constant float4x4& uMatrix [[buffer(2)]],
                                    constant float& uFloat0 [[buffer(4)]])
{
    return float4(uMatrix[0][0], uFloat0, uMatrix[1][1], 1.0);
}
)";

    const char* kBrokenFragmentSrc = R"(
#include <metal_stdlib>
using namespace metal;
fragment float4 brokenFragmentMain() { return undeclared_identifier; }
)";
}

class MetalSpriteBatchCustomEffectTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int frame_ = 0;

    static Color ReadBack(GraphicsDevice& dev, int x, int y)
    {
        const Rectangle one(x, y, 1, 1);
        Color pixel(0, 0, 0, 0);
        dev.GetBackBufferData(&one, &pixel, 0, 1);
        return pixel;
    }

protected:
    void Draw(const GameTime&) override
    {
        if (frame_++ < 1) return;

        auto& dev = getGraphicsDeviceProperty();
        const Color kClear(10, 10, 10, 255);
        const Color kRed(255, 0, 0, 255);
        const Rectangle kDestination(50, 60, 80, 40);
        SamplerState pointClamp = SamplerState::PointClamp;

        check(dev.SupportsCapability(CNA::GraphicsCapability::CustomEffects),
              "Metal reports GraphicsCapability::CustomEffects (SpriteBatch-scoped MSL)");

        Texture2D tex(dev, 1, 1);
        const Color redPixel[1] = {kRed};
        tex.SetData(redPixel, 1);

        // Check A: compiles for real.
        auto invertEffect = std::make_unique<ShaderEffect>(dev, kVertexShaderSrc, kInvertFragmentSrc);
        check(invertEffect->IsEffectValid(),
              "A: a runtime-compiled MSL vertex+fragment pair for SpriteBatch's vertex contract "
              "compiles (" + invertEffect->GetCompileErrorEXT() + ")");
        invertEffect->SetUniformVec4(nullptr, 1.0f, 1.0f, 1.0f, 1.0f);

        // Check B/C: draw through the custom effect.
        {
            dev.Clear(kClear);
            SpriteBatch sb(dev);
            sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &pointClamp, nullptr, nullptr,
                     invertEffect.get());
            sb.Draw(tex, kDestination, kRed);
            sb.End();

            const Color inside = ReadBack(dev, 80, 79);
            check(rgba(inside, 0, 255, 255, 255),
                  "B: sprites draw through the custom shader's inversion and its buffer(3) uColor "
                  "(got " + describe(inside) + ", want (0,255,255,255))");
            const Color outside = ReadBack(dev, 10, 10);
            check(rgba(outside, 10, 10, 10, 255),
                  "C: the buffer(1) transform places the quad; outside it the clear colour stays "
                  "(got " + describe(outside) + ")");
        }

        // Check D: the matrix and scalar slots.
        {
            ShaderEffect uniformEffect(dev, kVertexShaderSrc, kUniformFragmentSrc);
            float matrix[16] = {};
            matrix[0] = 0.25f;  // column 0, row 0
            matrix[5] = 0.5f;   // column 1, row 1
            uniformEffect.SetUniformMat4(nullptr, matrix);
            uniformEffect.SetUniformFloat(nullptr, 0.75f);
            dev.Clear(kClear);
            SpriteBatch sb(dev);
            sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &pointClamp, nullptr, nullptr,
                     &uniformEffect);
            sb.Draw(tex, kDestination, kRed);
            sb.End();
            const Color inside = ReadBack(dev, 80, 79);
            check(uniformEffect.IsEffectValid() && rgba(inside, 64, 191, 128, 255),
                  "D: buffer(2) float4x4 and buffer(4) float reach the fragment stage (got " +
                      describe(inside) + ", want (64,191,128,255))");
        }

        // Check E: the pipeline follows the batch's blend state.
        {
            dev.Clear(kClear);
            SpriteBatch sb(dev);
            sb.Begin(SpriteSortMode::Deferred, BlendState::Additive, &pointClamp, nullptr, nullptr,
                     invertEffect.get());
            sb.Draw(tex, kDestination, kRed);
            sb.End();
            const Color inside = ReadBack(dev, 80, 79);
            check(rgba(inside, 10, 255, 255, 255),
                  "E: BlendState::Additive adds the custom output to the clear colour (got " +
                      describe(inside) + ", want (10,255,255,255))");
        }

        // Check F: a render target as the destination.
        {
            RenderTarget2D target(dev, 64, 64, false, SurfaceFormat::Color, DepthFormat::None);
            dev.SetRenderTarget(&target);
            dev.Clear(kClear);
            {
                SpriteBatch sb(dev);
                sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &pointClamp, nullptr, nullptr,
                         invertEffect.get());
                sb.Draw(tex, Rectangle(8, 8, 32, 32), kRed);
                sb.End();
            }
            dev.SetRenderTarget(nullptr);
            Color inside(0, 0, 0, 0);
            Color outside(0, 0, 0, 0);
            const Rectangle insideRect(20, 20, 1, 1);
            const Rectangle outsideRect(50, 50, 1, 1);
            target.GetData(0, &insideRect, &inside, 0, 1);
            target.GetData(0, &outsideRect, &outside, 0, 1);
            check(rgba(inside, 0, 255, 255, 255) && rgba(outside, 10, 10, 10, 255),
                  "F: the custom effect draws into a RenderTarget2D (inside " + describe(inside) +
                      ", outside " + describe(outside) + ")");
        }

        // Check G: a source that does not compile.
        {
            ShaderEffect broken(dev, kVertexShaderSrc, kBrokenFragmentSrc);
            check(!broken.IsEffectValid() && !broken.GetCompileErrorEXT().empty(),
                  "G: an MSL source that does not compile is an invalid effect with a message, "
                  "not a throw");
        }

        // Check H: stock restore, and effect lifetime.
        {
            invertEffect.reset();
            dev.Clear(kClear);
            SpriteBatch sb(dev);
            sb.Begin();
            sb.Draw(tex, kDestination, kRed);
            sb.End();
            const Color stock = ReadBack(dev, 80, 79);
            check(rgba(stock, 255, 0, 0, 255),
                  "H: a batch with no effect draws with the stock pipeline after the effect was "
                  "disposed (got " + describe(stock) + ")");

            ShaderEffect again(dev, kVertexShaderSrc, kInvertFragmentSrc);
            again.SetUniformVec4(nullptr, 1.0f, 1.0f, 1.0f, 1.0f);
            dev.Clear(kClear);
            SpriteBatch sb2(dev);
            sb2.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &pointClamp, nullptr, nullptr, &again);
            sb2.Draw(tex, kDestination, kRed);
            sb2.End();
            const Color custom = ReadBack(dev, 80, 79);
            check(rgba(custom, 0, 255, 255, 255),
                  "H: a new effect created after one was disposed draws (got " + describe(custom) + ")");
        }

        std::printf("=== %d/%d PASS ===\n", passCount, totalCount);
        Exit();
    }

public:
    MetalSpriteBatchCustomEffectTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        // GetBackBufferData is HiDef-only, as in XNA 4.0.
        gdm_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
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
    return (passCount == totalCount && totalCount > 0) ? 0 : 1;
}
