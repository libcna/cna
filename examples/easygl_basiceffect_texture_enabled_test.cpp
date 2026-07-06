// SPDX-License-Identifier: MS-PL
// Task 366: BasicEffect pixel test — TextureEnabled=true, no vertex color (EasyGL backend).
//
// FNA reference (Graphics/Effect/StockEffects/BasicEffect.cs OnApply() + HLSL/BasicEffect.fx +
// Common.fxh): with LightingEnabled=false (default), VertexColorEnabled=false (default, per Task
// 361) and TextureEnabled=true (explicitly set here), BasicEffect selects shaderIndex 1+4=5 ->
// VSBasicTxNoFog/PSBasicTxNoFog. Common.fxh's ComputeCommonVSOutput() sets vout.Diffuse =
// DiffuseColor (the *material* color, already alpha-premultiplied by
// EffectHelpers.SetMaterialColor: diffuse.rgb = (DiffuseColor+EmissiveColor)*Alpha, diffuse.a =
// Alpha) — VSBasicTxNoFog does NOT multiply by any vertex-color attribute (that only happens in
// the separate *Vc/*TxVc shader family). PSBasicTxNoFog returns
// `SAMPLE_TEXTURE(Texture, pin.TexCoord) * pin.Diffuse`, i.e. the sampled texture color multiplied
// component-wise (including alpha) by the material diffuse color. Net expected fragment output
// (EmissiveColor at its default (0,0,0)) = TextureColor * DiffuseColor * Alpha, component-wise.
//
// NOTE (deferred, not this task's scope): EffectHelpers.SetMaterialColor's disabled-lighting branch
// is `(DiffuseColor + EmissiveColor) * Alpha`; CNA's BasicEffect::FillGpuDrawParams() currently
// computes only `DiffuseColor * Alpha`, omitting `+ EmissiveColor`. Invisible here since this test
// (like Tasks 364/365) leaves EmissiveColor at its default (0,0,0) — tracked as part of Task 369
// ("ambient + emissive + specular combination"), not fixed in this texture-only pixel test.
//
// Pre-existing coverage check: Task 189's easygl_basiceffect_combinations_test.cpp already has a
// "texture only" case (white diffuse, colored texture) and a "diffuse tint" case (colored diffuse,
// white texture) — but neither combines a *non-white* texture with a *non-white* diffuse, so neither
// can distinguish "texture ignored" or "diffuse ignored" from "both correctly multiplied". This test
// closes that gap with values chosen so all 3 hypotheses produce numerically distinct results.
//
// Uses a distinctive, non-white 1x1 texture color (200,100,50) and a distinctive DiffuseColor
// (0.8, 0.4, 0.6) — the same numeric pair Task 365 used for vertex-color x diffuse, reused here for
// texture x diffuse so the correctly-multiplied result is the identical, independently-verified
// (160,40,30): texture-only would read back as (200,100,50); diffuse-only as (204,102,153). Only the
// genuine component-wise product should match.
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
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kSize = 64;

// 1x1 texture color: an oddly-valued, opaque color, deliberately not white.
static const Color kTexColor(200, 100, 50, 255);
// DiffuseColor(0.8, 0.4, 0.6) * Alpha(1.0).
static const Vector3 kDiffuse(0.8f, 0.4f, 0.6f);

// Expected: TextureColor.rgb/255 * DiffuseColor * 255 = (160, 40, 30) exactly.
static const Color kExpected(160, 40, 30, 255);
// Failure-mode references, used only to prove discriminating power / diagnose a broken path.
static const Color kDiffuseOnly(204, 102, 153, 255);
static const Color kTextureOnly(200, 100, 50, 255);

class BasicEffectTextureEnabledTest : public Game
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

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();

        Texture2D tex(dev, 1, 1);
        tex.SetData(&kTexColor, 1);

        BasicEffect fx(dev);
        fx.setTextureEnabledProperty(true);
        fx.setTextureProperty(&tex);
        fx.setDiffuseColorProperty(kDiffuse);

        // Full-screen NDC quad, VertexPositionTexture (no vertex color attribute at all).
        const Vector3 tl(-1.0f,  1.0f, 0.0f), bl(-1.0f, -1.0f, 0.0f);
        const Vector3 br( 1.0f, -1.0f, 0.0f), tr( 1.0f,  1.0f, 0.0f);
        const Vector2 uv0(0.0f, 0.0f), uv1(0.0f, 1.0f), uv2(1.0f, 1.0f), uv3(1.0f, 0.0f);
        const VertexPositionTexture q[6] = {
            { tl, uv0 }, { bl, uv1 }, { br, uv2 },
            { tl, uv0 }, { br, uv2 }, { tr, uv3 },
        };

        Color got(0, 0, 0, 0);
        for (int i = 0; i < 20; ++i)
        {
            dev.Clear(Color(0, 0, 0, 255));
            dev.setBlendStateProperty(BlendState::Opaque);
            fx.Apply();
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);
            got = readCenter(dev);
            if (got.getRProperty() != 0 || got.getGProperty() != 0 || got.getBProperty() != 0)
                break; // skip blank/black frames
        }

        check(matches(got, kExpected), "TextureEnabled=true: pixel == TextureColor*DiffuseColor",
              got, "(160,40,30)");
        check(!matches(got, kDiffuseOnly), "TextureEnabled=true: pixel != DiffuseColor alone",
              got, "not (204,102,153)");
        check(!matches(got, kTextureOnly), "TextureEnabled=true: pixel != TextureColor alone",
              got, "not (200,100,50)");

        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    BasicEffectTextureEnabledTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    BasicEffectTextureEnabledTest game;
    game.Run();
    return game.getResult();
}
