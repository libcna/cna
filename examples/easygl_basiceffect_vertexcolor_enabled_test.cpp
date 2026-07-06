// SPDX-License-Identifier: MS-PL
// Task 365: BasicEffect pixel test — VertexColorEnabled=true, no texture, vertex color
// multiplication (EasyGL backend).
//
// FNA reference (Graphics/Effect/StockEffects/BasicEffect.cs OnApply() + HLSL/BasicEffect.fx +
// Common.fxh): with LightingEnabled=false (default), TextureEnabled=false (default) and
// VertexColorEnabled=true (explicitly set here — the real FNA default is false, per Task 361),
// BasicEffect selects shaderIndex 1+2=3 -> VSBasicVcNoFog/PSBasicNoFog. Common.fxh's
// ComputeCommonVSOutput() sets vout.Diffuse = DiffuseColor (the *material* color, already
// alpha-premultiplied by EffectHelpers.SetMaterialColor: diffuse.rgb =
// (DiffuseColor+EmissiveColor)*Alpha, diffuse.a = Alpha), and — critically —
// VSBasicVcNoFog additionally executes `vout.Diffuse *= vin.Color`, i.e. the material color is
// multiplied component-wise (including alpha) by the per-vertex color attribute.
// PSBasicNoFog just returns pin.Diffuse unmodified. Net expected fragment output (EmissiveColor
// at its default (0,0,0)) = DiffuseColor * Alpha * VertexColor, component-wise.
//
// This is the inverse/complement of Task 364 (which proved VertexColorEnabled=false must ignore
// the vertex-color attribute entirely). Task 364's own fix added a `uVertexColorEnabled` gating
// uniform to EasyGL's colored3D fragment shader (`FragColor = vc*uDiffuseColor`, where
// `vc = (uVertexColorEnabled>0.5) ? vColor : vec4(1)`) — this test exercises the `true` branch of
// that same gate, which is the pre-existing multiply that already shipped before Task 364 (Task
// 364 only added/fixed the `false` branch), so this task is expected to be pure new-coverage
// verification rather than a bug fix, but that must be *confirmed* by an actual pixel readback,
// not assumed.
//
// Uses a distinctive, non-white vertex color (200,100,50,200 — an oddly-valued, semi-transparent
// color) and a distinctive DiffuseColor (0.8, 0.4, 0.6) chosen so that the correctly-multiplied
// result (160,40,30) is numerically distinct from either input alone: DiffuseColor-only would
// read back as (204,102,153); VertexColor-only would read back as (200,100,50). Only the genuine
// component-wise product should match.
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
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kSize = 64;

// Per-vertex color: an oddly-valued, semi-transparent color, deliberately not white/opaque.
static const Color kVertexColor(200, 100, 50, 200);
// DiffuseColor(0.8, 0.4, 0.6) * Alpha(1.0).
static const Vector3 kDiffuse(0.8f, 0.4f, 0.6f);

// Expected: VertexColor.rgb/255 * DiffuseColor * 255 = (160, 40, 30) exactly.
static const Color kExpected(160, 40, 30, 255);
// Failure-mode references, used only to prove discriminating power / diagnose a broken gate.
static const Color kDiffuseOnly(204, 102, 153, 255);
static const Color kVertexOnly(200, 100, 50, 255);

class BasicEffectVertexColorEnabledTest : public Game
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

        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.setDiffuseColorProperty(kDiffuse);

        // Full-screen NDC quad, VertexPositionColor with a distinctive per-vertex color that
        // must now be multiplied into the output since VertexColorEnabled is true.
        const Vector3 tl(-1.0f,  1.0f, 0.0f), bl(-1.0f, -1.0f, 0.0f);
        const Vector3 br( 1.0f, -1.0f, 0.0f), tr( 1.0f,  1.0f, 0.0f);
        const VertexPositionColor q[6] = {
            { tl, kVertexColor }, { bl, kVertexColor }, { br, kVertexColor },
            { tl, kVertexColor }, { br, kVertexColor }, { tr, kVertexColor },
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

        check(matches(got, kExpected), "VertexColorEnabled=true: pixel == VertexColor*DiffuseColor",
              got, "(160,40,30)");
        check(!matches(got, kDiffuseOnly), "VertexColorEnabled=true: pixel != DiffuseColor alone",
              got, "not (204,102,153)");
        check(!matches(got, kVertexOnly), "VertexColorEnabled=true: pixel != VertexColor alone",
              got, "not (200,100,50)");

        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    BasicEffectVertexColorEnabledTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    BasicEffectVertexColorEnabledTest game;
    game.Run();
    return game.getResult();
}
