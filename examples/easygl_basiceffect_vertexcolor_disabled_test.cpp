// SPDX-License-Identifier: MS-PL
// Task 364: BasicEffect pixel test — VertexColorEnabled=false, no texture, diffuse color only
// (EasyGL backend).
//
// FNA reference (Graphics/Effect/StockEffects/BasicEffect.cs OnApply() + HLSL/BasicEffect.fx):
// with LightingEnabled=false (default), TextureEnabled=false (default) and
// VertexColorEnabled=false (the real default per Task 361's fix), BasicEffect selects
// shaderIndex=1 → VSBasicNoFog/PSBasicNoFog. ComputeCommonVSOutput() sets
// vout.Diffuse = DiffuseColor (the *material* color, already alpha-premultiplied by
// EffectHelpers.SetMaterialColor: diffuse.rgb = (DiffuseColor+EmissiveColor)*Alpha,
// diffuse.a = Alpha) and — critically — VSBasicNoFog never executes
// `vout.Diffuse *= vin.Color`, so any per-vertex color attribute in the vertex buffer is
// completely ignored. PSBasicNoFog just returns pin.Diffuse unmodified. Net expected
// fragment output = DiffuseColor * Alpha (EmissiveColor default (0,0,0) here), regardless
// of whatever color is baked into the vertex buffer.
//
// This test draws a full-screen quad using VertexPositionColor vertices carrying a
// deliberately different (bright red) per-vertex color, with BasicEffect.DiffuseColor set
// to a distinctive teal-ish tone and VertexColorEnabled left at its real default (false).
// If VertexColorEnabled genuinely gates the vertex-color multiply at the shader/GPU level
// (not just a stored, inert C++ property), the read-back pixel must equal DiffuseColor,
// not the red vertex color and not some red/teal blend.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kSize = 64;

// DiffuseColor(0.2, 0.6, 0.9) * Alpha(1.0) * 255, rounded.
static const Color kExpected(51, 153, 230, 255);
static const Color kVertexRed(255, 0, 0, 255);

class BasicEffectVertexColorDisabledTest : public Game
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

    static bool matchesDiffuse(const Color& c)
    {
        return closeTo(c.getRProperty(), kExpected.getRProperty(), 10)
            && closeTo(c.getGProperty(), kExpected.getGProperty(), 10)
            && closeTo(c.getBProperty(), kExpected.getBProperty(), 10);
    }

    static bool looksRed(const Color& c)
    {
        return c.getRProperty() >= 200 && c.getGProperty() <= 60 && c.getBProperty() <= 60;
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

        BasicEffect fx(dev);
        // VertexColorEnabled and TextureEnabled are left at their real FNA-matching
        // defaults (both false, per Task 361) — deliberately not set here.
        fx.setDiffuseColorProperty(Vector3(0.2f, 0.6f, 0.9f));

        // Full-screen NDC quad, VertexPositionColor with a bright red per-vertex color
        // that must be ignored since VertexColorEnabled is false.
        const Vector3 tl(-1.0f,  1.0f, 0.0f), bl(-1.0f, -1.0f, 0.0f);
        const Vector3 br( 1.0f, -1.0f, 0.0f), tr( 1.0f,  1.0f, 0.0f);
        const VertexPositionColor q[6] = {
            { tl, kVertexRed }, { bl, kVertexRed }, { br, kVertexRed },
            { tl, kVertexRed }, { br, kVertexRed }, { tr, kVertexRed },
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

        check(matchesDiffuse(got), "VertexColorEnabled=false: pixel == DiffuseColor", got,
              "(51,153,230)");
        check(!looksRed(got), "VertexColorEnabled=false: pixel != vertex red", got, "not (255,0,0)");

        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    BasicEffectVertexColorDisabledTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    BasicEffectVertexColorDisabledTest game;
    game.Run();
    return game.getResult();
}
