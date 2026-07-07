// SPDX-License-Identifier: MS-PL
// Task 377: AlphaTestEffect vertex color × diffuse color interaction (EasyGL backend).
//
// FNA reference (Graphics/Effect/StockEffects/HLSL/AlphaTestEffect.fx): `VSAlphaTestVcNoFog`
// executes `vout.Diffuse *= vin.Color` on top of `ComputeCommonVSOutput()`'s `vout.Diffuse =
// DiffuseColor`; `PSAlphaTestLtGtNoFog`/`PSAlphaTestEqNeNoFog` both compute
// `color = SAMPLE_TEXTURE(Texture,pin.TexCoord) * pin.Diffuse` and run the alpha-test `clip(...)`
// against **that final, already-vertex-color-multiplied `color.a`** — not the material alpha
// alone. So `VertexColorEnabled` doesn't just tint the RGB output, it also feeds into whether the
// alpha test passes at all. Expected: **RGB = TextureColor × VertexColor × DiffuseColor × Alpha**,
// and the alpha-test reference comparison runs against
// **TextureAlpha × VertexAlpha × EffectAlpha** as a single combined value.
//
// CNA architecture note: EasyGL has no separate "AlphaTestEffect-only" shader family — it reuses
// the same generic per-stride textured/colored shaders (`EnsureColoredTextured3DProgram()` for
// this stride-24 case) that `BasicEffect` also uses, each already carrying its own `uAlphaTest`
// uniform + `discard` logic (present in every one of these shaders since long before Task 367).
// Task 367 already fixed and pixel-verified this exact shader's `DiffuseColor`/`VertexColorEnabled`
// formula for `BasicEffect`; this task confirms the *same* shader, driven by `AlphaTestEffect`
// instead, produces the same correct RGB **and** that the alpha test correctly reads the
// post-vertex-color-multiply alpha — genuine new coverage, not a duplicate of Task 367.
//
// CONFIRMED, NOT FIXED HERE: Vulkan and Bgfx take a fundamentally different, incompatible
// architecture for `AlphaTestEffect` — a single generic alpha-test pipeline/shader shared across
// all vertex strides (`alpha_test3d.vert/frag.glsl` on Vulkan, `vs/fs_alpha_test3d.sc` on Bgfx)
// that **only ever declares `position`+`texcoord` vertex inputs, never a color attribute** — so
// `AlphaTestEffect.VertexColorEnabled` has **zero effect on Vulkan or Bgfx**, and this is true by
// default: even `AlphaTestEffect`'s own default settings (`AlphaFunction=Greater`,
// `ReferenceAlpha=0`) already produce `alphaTest.z=-1`, which routes every draw through this
// vertex-color-blind pipeline unless `AlphaFunction=Always` is explicitly set (which defeats the
// point of using `AlphaTestEffect` at all). Fixing this properly means unifying Vulkan/Bgfx's
// alpha-test dispatch with their already-correct per-stride textured/colored-textured pipelines
// (mirroring EasyGL's architecture) — a genuinely large, multi-shader-file, multi-backend change,
// not a Task-377-sized fix. Tracked as new **Task 887**; no Vulkan/Bgfx test is added here to
// avoid encoding today's confirmed-wrong behavior as a "passing" assertion that would need
// updating (and could be forgotten) once Task 887 lands.
//
// Uses TextureColor=white (255,255,255,255, so texture is an identity factor, isolating the
// vertex-color × diffuse-color interaction specifically), VertexColor RGB=(200,100,50) Alpha=200,
// DiffuseColor=(0.6,0.4,0.8), effect Alpha=0.8. Two `CompareFunction::Greater` cases with
// different `ReferenceAlpha` values distinguish "combined alpha used" from "diffuse alpha alone
// used": reference=100 (combined alpha 160/255 passes → confirms RGB), reference=180 (combined
// alpha 160/255 fails, but the wrong "diffuse-alone" hypothesis's alpha of 204/255 would
// incorrectly pass — proving vertex alpha genuinely participates in the alpha test).
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaTestEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
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

static const Color kWhite(255, 255, 255, 255);
static const Color kVertexColor(200, 100, 50, 200);
static const Vector3 kDiffuse(0.6f, 0.4f, 0.8f);
static constexpr float kEffectAlpha = 0.8f;

// Expected RGB when drawn: TextureColor(1,1,1) * VertexColor(200,100,50)/255 * DiffuseColor*Alpha.
static const Color kExpectedRgb(96, 32, 32, 255);
static const Color kBlack(0, 0, 0, 255);

class AlphaTestVertexColorDiffuseTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int  pass_ = 0;
    int  fail_ = 0;

    void check(bool ok, const char* label, const Color& got, const char* expected)
    {
        if (ok)
        {
            std::printf("[PASS] %s: got=(%d,%d,%d)\n", label,
                got.getRProperty(), got.getGProperty(), got.getBProperty());
            ++pass_;
        }
        else
        {
            std::printf("[FAIL] %s: got=(%d,%d,%d) expected %s\n", label,
                got.getRProperty(), got.getGProperty(), got.getBProperty(), expected);
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

    Color renderWith(GraphicsDevice& dev, Texture2D& tex, const VertexPositionColorTexture (&quad)[6],
                      int referenceAlpha)
    {
        AlphaTestEffect fx(dev);
        fx.setTextureProperty(&tex);
        fx.setVertexColorEnabledProperty(true);
        fx.setDiffuseColorProperty(kDiffuse);
        fx.setAlphaProperty(kEffectAlpha);
        fx.setAlphaFunctionProperty(CompareFunction::Greater);
        fx.setReferenceAlphaProperty(referenceAlpha);
        fx.Apply();

        Color got(0, 0, 0, 0);
        for (int i = 0; i < 20; ++i)
        {
            dev.Clear(kBlack);
            dev.setBlendStateProperty(BlendState::Opaque);
            // Task 896 finding: this quad's winding is CCW/back-facing under CNA's real
            // default RasterizerState — needs CullNone.
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
            got = readCenter(dev);
            if (got.getRProperty() != 0 || got.getGProperty() != 0 || got.getBProperty() != 0)
                break; // skip blank/black frames (also happens to be the discarded-pixel case;
                        // handled correctly since the loop still returns the last-read value)
        }
        return got;
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();

        Texture2D tex(dev, 1, 1);
        tex.SetData(&kWhite, 1);

        const Vector3 tl(-1.0f,  1.0f, 0.0f), bl(-1.0f, -1.0f, 0.0f);
        const Vector3 br( 1.0f, -1.0f, 0.0f), tr( 1.0f,  1.0f, 0.0f);
        const Vector2 uv0(0.0f, 0.0f), uv1(0.0f, 1.0f), uv2(1.0f, 1.0f), uv3(1.0f, 0.0f);
        const VertexPositionColorTexture quad[6] = {
            { tl, kVertexColor, uv0 }, { bl, kVertexColor, uv1 }, { br, kVertexColor, uv2 },
            { tl, kVertexColor, uv0 }, { br, kVertexColor, uv2 }, { tr, kVertexColor, uv3 },
        };

        // Case A: reference=100 -> combined alpha (160/255) passes Greater. Confirms RGB formula.
        const Color drawnGot = renderWith(dev, tex, quad, 100);
        check(matches(drawnGot, kExpectedRgb),
              "reference=100: passes, RGB == TextureColor*VertexColor*DiffuseColor*Alpha",
              drawnGot, "(96,32,32)");

        // Case B: reference=180 -> combined alpha (160/255) fails Greater and must be discarded.
        // If vertex alpha were wrongly ignored, the diffuse-alone alpha (204/255) would incorrectly
        // pass instead.
        const Color discardedGot = renderWith(dev, tex, quad, 180);
        check(matches(discardedGot, kBlack),
              "reference=180: discarded (combined alpha, not diffuse alone, gates the test)",
              discardedGot, "(0,0,0) [black clear, pixel discarded]");

        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    AlphaTestVertexColorDiffuseTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    AlphaTestVertexColorDiffuseTest game;
    game.Run();
    return game.getResult();
}
