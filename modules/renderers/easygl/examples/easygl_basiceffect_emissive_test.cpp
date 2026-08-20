// SPDX-License-Identifier: MS-PL
// Task 369: BasicEffect pixel test — DiffuseColor + EmissiveColor combination with
// LightingEnabled=false (EasyGL renderer).
//
// FNA reference (Graphics/Effect/StockEffects/EffectHelpers.cs SetMaterialColor()): with
// LightingEnabled=false, ambient/directional lights are never computed at all — FNA folds
// EmissiveColor directly into the forwarded diffuse parameter on the CPU: `diffuse =
// (DiffuseColor + EmissiveColor) * Alpha`, and the shader just multiplies texture/vertex-color by
// this single value, same as Tasks 364-367's no-lighting shader paths. Net expected fragment
// (TextureEnabled=true, VertexColorEnabled=false, matching Task 366's setup) =
// **TextureColor × (DiffuseColor + EmissiveColor) × Alpha**, component-wise.
//
// REAL BUG FOUND AND FIXED by writing this test: `BasicEffect::FillGpuDrawParams()` forwarded
// `DiffuseColor*Alpha` alone, in all cases, **completely dropping `EmissiveColor` from the
// no-lighting formula** — a real, confirmed mismatch vs FNA, invisible in Tasks 364-367 because
// all of them left `EmissiveColor` at its default `(0,0,0)`. Fixed by baking `EmissiveColor` into
// the forwarded diffuse color when `LightingEnabled=false`, matching
// `EffectHelpers.SetMaterialColor`'s exact branching (this is shared C++ code, so one fix covers
// all 3 renderers — no shader changes needed, since the value is baked into the uniform the
// no-lighting shaders already multiply by).
//
// Scope note: `LightingEnabled=true`'s own emissive/ambient/specular combination (a much larger,
// separate finding — see plans/plan_graphics.md Task 369 write-up for the full audit) is deliberately
// NOT covered by this test; it needs its own dedicated task (new Task 885) since it also requires
// forwarding `SpecularColor`/`SpecularPower`/multi-light data that doesn't exist anywhere yet, and
// on Vulkan specifically would require expanding a push-constant budget shared with SkinnedEffect
// — an architectural change outside this task's scope.
//
// Uses a distinctive, non-white 1x1 texture color (200,100,50) — reused from Tasks 366/367 — with
// new DiffuseColor (0.3,0.2,0.1) and EmissiveColor (0.2,0.1,0.4) chosen so the correct sum
// (0.5,0.3,0.5) produces a result numerically distinct from either "component ignored" hypothesis.
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
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kSize = 64;

// 1x1 texture color: an oddly-valued, opaque color, deliberately not white.
static const Color kTexColor(200, 100, 50, 255);
static const Vector3 kDiffuse(0.3f, 0.2f, 0.1f);
static const Vector3 kEmissive(0.2f, 0.1f, 0.4f);

// Expected: TextureColor * (DiffuseColor+EmissiveColor) = TextureColor*(0.5,0.3,0.5).
static const Color kExpected(100, 30, 25, 255);
// Failure-mode references, used only to prove discriminating power.
static const Color kEmissiveIgnored(60, 20, 5, 255);   // TextureColor*DiffuseColor alone
static const Color kDiffuseIgnored(40, 10, 20, 255);   // TextureColor*EmissiveColor alone

class BasicEffectEmissiveTest : public Game
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
        fx.setEmissiveColorProperty(kEmissive);

        // Full-screen NDC quad, VertexPositionTexture (no vertex color, no lighting).
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
            // Task 896 finding (mirrors the Bgfx sibling's Task 364/884 fix): this quad's winding
            // is culled by the real default RasterizerState once EasyGL pushes it at construction.
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);
            got = readCenter(dev);
            if (got.getRProperty() != 0 || got.getGProperty() != 0 || got.getBProperty() != 0)
                break; // skip blank/black frames
        }

        check(matches(got, kExpected),
              "No lighting: pixel == TextureColor*(DiffuseColor+EmissiveColor)",
              got, "(100,30,25)");
        check(!matches(got, kEmissiveIgnored),
              "No lighting: pixel != DiffuseColor alone (EmissiveColor not ignored)",
              got, "not (60,20,5)");
        check(!matches(got, kDiffuseIgnored),
              "No lighting: pixel != EmissiveColor alone (DiffuseColor not ignored)",
              got, "not (40,10,20)");

        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    BasicEffectEmissiveTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    BasicEffectEmissiveTest game;
    game.Run();
    return game.getResult();
}
