// SPDX-License-Identifier: MS-PL
// Task 887: AlphaTestEffect.VertexColorEnabled fix verification (Bgfx renderer).
//
// Direct port of examples/easygl_alphatest_vertexcolor_diffuse_test.cpp (Task 377's test), which
// found this exact gap on Vulkan/Bgfx: their alpha-test pipeline/shader only ever declared
// position+texcoord vertex inputs, never a color attribute, so VertexColorEnabled had zero effect.
// Fixed by adding a dedicated stride-24 (VertexPositionColorTexture) vertex shader variant
// (vs_alpha_test_colored3d.sc) that reads a_color0 and gates its multiply by VertexColorEnabled,
// dispatched whenever the alpha-test pipeline is active AND VertexColorEnabled=true. See that
// file's header comment for the full FNA-derived formula rationale (combined alpha =
// TextureAlpha*VertexAlpha*EffectAlpha gates the alpha test, not the diffuse alpha alone).
//
// Bgfx-only note (Task 364/896 finding): Bgfx's `RasterizerState` cull state (`BGFX_STATE_CULL_CCW`)
// is the only one of the 3 renderers that matches FNA's real default, so it's the only renderer that
// silently culls the standard NDC quad winding used here unless `RasterizerState::CullNone` is set
// explicitly — worked around identically to every other Bgfx pixel test in this family.
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

class AlphaTestVertexColorBgfxTest : public Game
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

    Color renderAndRead(GraphicsDevice& dev, Texture2D& tex, const VertexPositionColorTexture (&quad)[6],
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

        const Rectangle reg(kSize / 2, kSize / 2, 1, 1);
        Color got(0, 0, 0, 0);
        for (int i = 0; i < 20; ++i)
        {
            dev.Clear(kBlack);
            dev.setBlendStateProperty(BlendState::Opaque);
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
            dev.GetBackBufferData(&reg, &got, 0, 1);
            if (got.getRProperty() != 0 || got.getGProperty() != 0 || got.getBProperty() != 0)
                break; // Bgfx's GetBackBufferData only reliably reflects the first read per
                        // rendered frame (Task 406 finding); this test reads once per frame.
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
        const Color drawnGot = renderAndRead(dev, tex, quad, 100);
        check(matches(drawnGot, kExpectedRgb),
              "reference=100: passes, RGB == TextureColor*VertexColor*DiffuseColor*Alpha",
              drawnGot, "(96,32,32)");

        // Case B: reference=180 -> combined alpha (160/255) fails Greater and must be discarded.
        // If vertex alpha (or the whole vertex-color attribute) were ignored, the diffuse-alone
        // alpha (204/255) would incorrectly pass instead.
        const Color discardedGot = renderAndRead(dev, tex, quad, 180);
        check(matches(discardedGot, kBlack),
              "reference=180: discarded (combined alpha, not diffuse alone, gates the test)",
              discardedGot, "(0,0,0) [black clear, pixel discarded]");

        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    AlphaTestVertexColorBgfxTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    AlphaTestVertexColorBgfxTest game;
    game.Run();
    return game.getResult();
}
