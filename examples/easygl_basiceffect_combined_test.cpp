// SPDX-License-Identifier: MS-PL
// Task 370: BasicEffect cross-backend image comparison suite — closes Phase 42 (EasyGL backend).
//
// Combines everything Tasks 364-369 individually verified into one scene, to prove the fixes
// compose correctly rather than only working in isolation: TextureEnabled=true (Task 366/367),
// VertexColorEnabled=true (Task 364/365/367), DiffuseColor+EmissiveColor (Task 369), all with
// LightingEnabled=false (its real FNA default). FNA reference (same shaderIndex=7 ->
// VSBasicTxVcNoFog/PSBasicTxNoFog family as Task 367, now with EffectHelpers.SetMaterialColor's
// disabled-lighting `(DiffuseColor+EmissiveColor)*Alpha` folded into the diffuse parameter per
// Task 369's fix): expected fragment =
// **TextureColor × VertexColor × (DiffuseColor + EmissiveColor) × Alpha**, component-wise.
//
// Uses a real 2×2 multi-texel texture (the first BasicEffect pixel test in this project to do so,
// rather than a 1×1 solid color) — deliberately exercises the exact Bgfx `TexCoord0`-binding path
// Task 368 found and fixed (`MakeBgfxLayout()` previously left `a_texcoord0` permanently unbound
// for this stride, invisible with 1×1 textures since every UV samples the same texel regardless).
// 4 separate draws sample each of the 4 texels via a UV held constant across the whole quad
// (avoiding bilinear-filtering ambiguity by sampling exactly at each texel's center: 0.25/0.75 in
// each axis of a 2-wide/2-tall texture), each compared against its own independently-derived
// expected value — this is also a genuine "does per-vertex UV reach the right texel" check.
//
// This is the capstone test for Phase 42 (Tasks 361-370): identical assertions run unmodified (up
// to the standard per-backend workarounds) on Vulkan and Bgfx, so a 3-way match against the same
// FNA-derived expected values *is* the cross-backend image comparison this task calls for.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kSize = 64;

// 2x2 texture, row-major: [0]=top-left, [1]=top-right, [2]=bottom-left, [3]=bottom-right.
static const Color kTexels[4] = {
    Color(200, 100, 50,  255),
    Color( 50, 200, 100, 255),
    Color(100,  50, 200, 255),
    Color(150, 150, 150, 255),
};

static const Color kVertexColor(180, 220, 140, 255);
static const Vector3 kDiffuse(0.6f, 0.4f, 0.8f);
static const Vector3 kEmissive(0.1f, 0.2f, 0.05f);

struct Sample { Vector2 uv; Color expected; const char* label; };

// Expected = TexelColor * VertexColor/255 * (DiffuseColor+EmissiveColor), component-wise.
static const Sample kSamples[4] = {
    { Vector2(0.25f, 0.25f), Color( 99,  52,  23, 255), "top-left texel"     },
    { Vector2(0.75f, 0.25f), Color( 25, 104,  47, 255), "top-right texel"    },
    { Vector2(0.25f, 0.75f), Color( 49,  26,  93, 255), "bottom-left texel"  },
    { Vector2(0.75f, 0.75f), Color( 74,  78,  70, 255), "bottom-right texel" },
};

class BasicEffectCombinedTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int  pass_ = 0;
    int  fail_ = 0;

    void check(bool ok, const char* label, const Color& got, const Color& expected)
    {
        if (ok)
        {
            std::printf("[PASS] %s: got=(%d,%d,%d)\n", label,
                got.getRProperty(), got.getGProperty(), got.getBProperty());
            ++pass_;
        }
        else
        {
            std::printf("[FAIL] %s: got=(%d,%d,%d) expected=(%d,%d,%d)\n", label,
                got.getRProperty(), got.getGProperty(), got.getBProperty(),
                expected.getRProperty(), expected.getGProperty(), expected.getBProperty());
            ++fail_;
        }
    }

    static bool closeTo(int a, int b, int tol) { return std::abs(a - b) <= tol; }

    static bool matches(const Color& c, const Color& expected)
    {
        return closeTo(c.getRProperty(), expected.getRProperty(), 8)
            && closeTo(c.getGProperty(), expected.getGProperty(), 8)
            && closeTo(c.getBProperty(), expected.getBProperty(), 8);
    }

    Color readCenter(GraphicsDevice& dev)
    {
        const Rectangle reg(kSize / 2, kSize / 2, 1, 1);
        Color px(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &px, 0, 1);
        return px;
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();

        Texture2D tex(dev, 2, 2);
        tex.SetData(kTexels, 4);

        const Vector3 tl(-1.0f,  1.0f, 0.0f), bl(-1.0f, -1.0f, 0.0f);
        const Vector3 br( 1.0f, -1.0f, 0.0f), tr( 1.0f,  1.0f, 0.0f);

        for (const auto& s : kSamples)
        {
            BasicEffect fx(dev);
            fx.setTextureEnabledProperty(true);
            fx.setTextureProperty(&tex);
            fx.VertexColorEnabled = true;
            fx.setDiffuseColorProperty(kDiffuse);
            fx.setEmissiveColorProperty(kEmissive);

            // UV held constant across the whole quad so every pixel samples the exact same
            // texel — isolates "does this UV reach the right texel" from filtering/gradients.
            const VertexPositionColorTexture q[6] = {
                { tl, kVertexColor, s.uv }, { bl, kVertexColor, s.uv }, { br, kVertexColor, s.uv },
                { tl, kVertexColor, s.uv }, { br, kVertexColor, s.uv }, { tr, kVertexColor, s.uv },
            };

            Color got(0, 0, 0, 0);
            for (int i = 0; i < 20; ++i)
            {
                dev.Clear(Color(0, 0, 0, 255));
                dev.setBlendStateProperty(BlendState::Opaque);
                fx.Apply();
                // Task 896 finding (mirrors the Bgfx sibling's Task 364/884 fix): this quad's winding
                // is culled by the real default RasterizerState once EasyGL pushes it at construction.
                dev.setRasterizerStateProperty(RasterizerState::CullNone);
                dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);
                got = readCenter(dev);
                if (got.getRProperty() != 0 || got.getGProperty() != 0 || got.getBProperty() != 0)
                    break; // skip blank/black frames
            }

            check(matches(got, s.expected), s.label, got, s.expected);
        }

        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    BasicEffectCombinedTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    BasicEffectCombinedTest game;
    game.Run();
    return game.getResult();
}
